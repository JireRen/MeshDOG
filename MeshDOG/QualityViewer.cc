//=============================================================================
//                                                                            
//   Example code for the full-day course
//
//   M. Botsch, M. Pauly, C. Roessl, S. Bischoff, L. Kobbelt,
//   "Geometric Modeling Based on Triangle Meshes"
//   held at SIGGRAPH 2006, Boston, and Eurographics 2006, Vienna.
//
//   Copyright (C) 2006 by  Computer Graphics Laboratory, ETH Zurich, 
//                      and Computer Graphics Group,      RWTH Aachen
//
//                                                                            
//-----------------------------------------------------------------------------
//                                                                            
//                                License                                     
//                                                                            
//   This program is free software; you can redistribute it and/or
//   modify it under the terms of the GNU General Public License
//   as published by the Free Software Foundation; either version 2
//   of the License, or (at your option) any later version.
//   
//   This program is distributed in the hope that it will be useful,
//   but WITHOUT ANY WARRANTY; without even the implied warranty of
//   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//   GNU General Public License for more details.
//   
//   You should have received a copy of the GNU General Public License
//   along with this program; if not, write to the Free Software
//   Foundation, Inc., 51 Franklin Street, Fifth Floor, 
//   Boston, MA  02110-1301, USA.
//                                                                            
//=============================================================================
//=============================================================================
//
//  CLASS QualityViewer - IMPLEMENTATION
//
//=============================================================================

//== INCLUDES =================================================================

#include "QualityViewer.hh"
#include <vector>
#include <float.h>
#include <math.h>
#include <algorithm>

#include <OpenMesh/Core/IO/MeshIO.hh>
//== IMPLEMENTATION ========================================================== 

QualityViewer::QualityViewer(const char* _title, int _width, int _height)
: MeshViewer(_title, _width, _height)
{ 
    mesh_.request_vertex_colors();

    mesh_.add_property(vcurvature_);
    mesh_.add_property(vunicurvature_);
    mesh_.add_property(vweight_);
    mesh_.add_property(eweight_);
    mesh_.add_property(tshape_);
    mesh_.add_property(vgausscurvature_);

    add_draw_mode("Uniform Mean Curvature");
    add_draw_mode("Mean Curvature");
    add_draw_mode("Gaussian Curvature");
    add_draw_mode("Triangle Shape");
    add_draw_mode("Reflection Lines");
    
    //== MeshDOG ============================================================
    /// request vertex status, if not, *.ply format will throw seg fault
    mesh_.request_vertex_status();
    /// add display mode
    add_draw_mode("MeshDOG");
    add_draw_mode("MeshDOG curvature");
    add_draw_mode("MeshDOG curvature DOG");
    add_draw_mode("MeshDOG feature points");
    
    /// add vertex property
    mesh_.add_property(vmeshdog_f_);
    mesh_.add_property(vmeshdog_dog_);
    mesh_.add_property(veavg_);
    mesh_.add_property(vgaussianconv_);
    
    /// default num of iters for gaussian conv
    _iters = 10;

    init();
}


//-----------------------------------------------------------------------------

QualityViewer::~QualityViewer()
{
    if (glIsTexture(textureID_))  
        glDeleteTextures( 1, &textureID_);
}

//-----------------------------------------------------------------------------

void QualityViewer::init()
{
    // base class first
    MeshViewer::init();


    // generate checkerboard-like image
    GLubyte tex[256*256*3], *tp=tex;
    for (int x=0; x<256; ++x)
        for (int y=0; y<256; ++y)
            if (((x+2)/4 % 10) == 0 || ((y+2)/4 % 10) == 0)
            {
                *(tp++) = 0;
                *(tp++) = 0;
                *(tp++) = 0;
            }
            else
            {
                *(tp++) = 255;
                *(tp++) = 255;
                *(tp++) = 255;
            }


            // generate texture
            if (!glIsTexture(textureID_))
                glGenTextures(1, &textureID_);
            glBindTexture(GL_TEXTURE_2D, textureID_);


            // copy texture to GL
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexImage2D(GL_TEXTURE_2D, 0, 3, 256, 256,
                0, GL_RGB, GL_UNSIGNED_BYTE, tex);
}



//-----------------------------------------------------------------------------


bool QualityViewer::open_mesh(const char* _filename)
{
    // load mesh
    if (MeshViewer::open_mesh(_filename))
    {
        // compute curvature stuff
        calc_weights();
        calc_mean_curvature();
        calc_uniform_mean_curvature();
        calc_gauss_curvature();
        calc_triangle_quality();
        face_color_coding();
        
        //==MeshDOG============================================================
        init_meshdog();
        detect_meshdog(_iters);
        save_meshdog();

        glutPostRedisplay();
        return true;
    }
    return false;
}


//-----------------------------------------------------------------------------



void QualityViewer::calc_weights()
{
    Mesh::VertexIter        v_it, v_end(mesh_.vertices_end());
    Mesh::EdgeIter          e_it, e_end(mesh_.edges_end());
    Mesh::VertexFaceIter    vf_it;
    Mesh::FaceVertexIter    fv_it;
    Mesh::HalfedgeHandle    h0, h1, h2;
    Mesh::VertexHandle      v0, v1;
    Mesh::Point             p0, p1, p2, d0, d1;
    Mesh::Scalar            w, area;



    for (e_it=mesh_.edges_begin(); e_it!=e_end; ++e_it)
    {
        w  = 0.0;

        h0 = mesh_.halfedge_handle(e_it.handle(), 0);
        v0 = mesh_.to_vertex_handle(h0);
        p0 = mesh_.point(v0);

        h1 = mesh_.halfedge_handle(e_it.handle(), 1);
        v1 = mesh_.to_vertex_handle(h1);
        p1 = mesh_.point(v1);

        h2 = mesh_.next_halfedge_handle(h0);
        p2 = mesh_.point(mesh_.to_vertex_handle(h2));
        d0 = (p0 - p2).normalize();
        d1 = (p1 - p2).normalize();
        w += 1.0 / tan(acos(std::min(0.99f, std::max(-0.99f, (d0|d1)))));

        h2 = mesh_.next_halfedge_handle(h1);
        p2 = mesh_.point(mesh_.to_vertex_handle(h2));
        d0 = (p0 - p2).normalize();
        d1 = (p1 - p2).normalize();
        w += 1.0 / tan(acos(std::min(0.99f, std::max(-0.99f, (d0|d1)))));

        w = std::max(0.0f, w);
        mesh_.property(eweight_,e_it) = w;
    }


    for (v_it=mesh_.vertices_begin(); v_it!=v_end; ++v_it)
    {
        area = 0.0;

        for (vf_it=mesh_.vf_iter(v_it); vf_it; ++vf_it)
        {
            fv_it = mesh_.fv_iter(vf_it);
 
            const Mesh::Point& P = mesh_.point(fv_it);  ++fv_it;
            const Mesh::Point& Q = mesh_.point(fv_it);  ++fv_it;
            const Mesh::Point& R = mesh_.point(fv_it);

            area += ((Q-P)%(R-P)).norm() * 0.5f * 0.3333f;
        }

        mesh_.property(vweight_,v_it) = 1.0 / (2.0 * area);
    }
}

//-----------------------------------------------------------------------------

void QualityViewer::calc_mean_curvature()
{
    Mesh::VertexIter        v_it, v_end(mesh_.vertices_end());
    Mesh::HalfedgeHandle    h;
    Mesh::EdgeHandle        e;
    Mesh::VertexVertexIter  vv_it;
    Mesh::Point             laplace(0.0, 0.0, 0.0);

    // ------------- IMPLEMENT HERE ---------
    // TASK 4.3.a Approximate mean curvature using the length of the Laplace-Beltrami approximation
    // Save your approximation in vcurvature_ vertex property of the mesh.
    // Use the weights from calc_weights(): eweight_ and vweight_
    // ------------- IMPLEMENT HERE ---------

    // the outgoing vertex half edge iterator
    Mesh::VertexOHalfedgeIter voh_it;
    Mesh::VertexHandle vi;
    Mesh::Point vvi;

    for (v_it = mesh_.vertices_begin(); v_it != v_end; ++v_it)
    {
        laplace[0] = laplace[1] = laplace[2] = 0.0;
        for(voh_it = mesh_.voh_iter(v_it); voh_it; ++voh_it)
        {
            // get vi handle and vi - v
            vi = mesh_.to_vertex_handle(voh_it);
            vvi = mesh_.point(vi) - mesh_.point(v_it);

            // get edge handle where vi is on
            e = mesh_.edge_handle(voh_it.handle());

            // sum(wi * (vi - v))
            laplace += mesh_.property(eweight_, e) * vvi;
        }

        // use half of the norm of LB(v) as mean curvature
        mesh_.property(vcurvature_, v_it) = (mesh_.property(vweight_, v_it) * laplace).norm() / 2;
    }

}

void QualityViewer::calc_uniform_mean_curvature()
{
    Mesh::VertexIter        v_it, v_end(mesh_.vertices_end());
    Mesh::VertexVertexIter  vv_it;
    Mesh::Point             laplace(0.0, 0.0, 0.0);

    // ------------- IMPLEMENT HERE ---------
    // TASK 4.1.a Approximate mean curvature using the length of the uniform Laplacian approximation
    // Save your approximation in vunicurvature_ vertex property of the mesh.
    // ------------- IMPLEMENT HERE ---------
    Mesh::Scalar counter;

    for (v_it = mesh_.vertices_begin(); v_it != v_end; ++v_it)
    {
        laplace[0] = laplace[1] = laplace[2] = counter = 0.0;
        
        for(vv_it = mesh_.vv_iter( v_it ); vv_it; ++vv_it)
        {
            laplace += mesh_.point( vv_it );
            counter++;
        }

        // get Lu(v) and vunicurvature_
        laplace = (laplace / counter) - mesh_.point( v_it );
        mesh_.property(vunicurvature_, v_it) = laplace.norm() / 2;

    }

}

void QualityViewer::calc_gauss_curvature()
{
    Mesh::VertexIter        v_it, v_end(mesh_.vertices_end());
    Mesh::VertexVertexIter  vv_it, vv_it2;
    Mesh::Point             d0, d1, d2;
    Mesh::Scalar            angles, cos_angle;
    Mesh::Scalar            lb(-1.0), ub(1.0);

    // ------------- IMPLEMENT HERE ---------
    // TASK 4.4 Approximate Gaussian curvature.
    // Hint: When calculating angles out of cross products make sure the value 
    // you pass to the acos function is between -1.0 and 1.0.
    // Use the vweight_ property for the area weight.
    // ------------- IMPLEMENT HERE ---------

    Mesh::Scalar vert_weight;
    for( v_it = mesh_.vertices_begin(); v_it != v_end; ++v_it)
    {
        angles = 0.0;
        d2 = mesh_.point( v_it );
        for(vv_it = mesh_.vv_iter(v_it); vv_it; ++vv_it)
        {
            vv_it2 = vv_it;
            ++vv_it;

            d0 = mesh_.point( vv_it2 ) - d2;
            d1 = mesh_.point( vv_it ) - d2;

            cos_angle = (d0[0] * d1[0] + d0[1] * d1[1] + d0[2] * d1[2]) / ( d0.norm() * d1.norm() );

            if (cos_angle < -1.0)
                cos_angle = -1.0;
            else if (cos_angle > 1.0)
                cos_angle = 1.0;

            angles += acos(cos_angle);
            --vv_it;
        }

        vert_weight = mesh_.property(vweight_, v_it);
        mesh_.property(vgausscurvature_, v_it) = 2 * vert_weight * ( 2 * 3.1415926 - angles );
    }
}

//-----------------------------------------------------------------------------


void QualityViewer::calc_triangle_quality()
{
    Mesh::FaceIter              f_it, f_end(mesh_.faces_end());
    Mesh::ConstFaceVertexIter   cfvIt;
    OpenMesh::Vec3f             v0,v1,v2;
    OpenMesh::Vec3f             v0v1,v0v2,v1v2;
    Mesh::Scalar                denom, circum_radius_sq, min_length_sq;

    // ------------- IMPLEMENT HERE ---------
    // TASK 4.2 Compute triangle shape measure and save it in the tshape_ property
    // For numerical stability you might want to set the property value to
    // a predifined large value (e.g. FLT_MAX) if the denominator is smaller than FLT_MIN
    // ------------- IMPLEMENT HERE ---------

    float a,b,c;
    for(f_it = mesh_.faces_sbegin(); f_it != f_end; ++f_it)
    {
        // initialize fv_iter
        cfvIt = mesh_.fv_iter( f_it );

        // get the vertex of triangle
        v0 = mesh_.point( cfvIt ); ++cfvIt;
        v1 = mesh_.point( cfvIt ); ++cfvIt;
        v2 = mesh_.point( cfvIt );

        // get the edge
        v0v1 = v1 - v0; a = v0v1.norm();
        v0v2 = v2 - v0; b = v0v2.norm();
        v1v2 = v1 - v2; c = v1v2.norm();

        // get the min_length_sq
        min_length_sq = FLT_MAX;
        min_length_sq = std::min(a, min_length_sq);
        min_length_sq = std::min(b, min_length_sq);
        min_length_sq = std::min(c, min_length_sq);

        denom = std::sqrt(std::pow(v0v1[1]*v0v2[2] - v0v1[2]*v0v2[1], 2) +
            std::pow(v0v1[2]*v0v2[0] - v0v1[0]*v0v2[2], 2) + 
            std::pow(v0v1[0]*v0v2[1] - v0v1[1]*v0v2[0], 2));

        if ( denom < FLT_MIN )
            mesh_.property(tshape_, f_it) = FLT_MAX;
        else
        {
            circum_radius_sq = ( a * b * c ) / ( 2 * denom );
            mesh_.property(tshape_, f_it) = circum_radius_sq / min_length_sq;
        }
    }
}

//-----------------------------------------------------------------------------

void QualityViewer::face_color_coding()
{
    Mesh::ConstFaceIter        f_it, f_end(mesh_.faces_end());
    Mesh::Scalar      sh, min_shape(FLT_MAX), max_shape(-FLT_MAX);
    Mesh::Color       col;

    face_colors_.clear();
    face_colors_.reserve(mesh_.n_faces()*3);

    min_shape = 0.6f;
    max_shape = 2.0f;

    // map curvatures to colors
    for (f_it = mesh_.faces_sbegin(); f_it!=f_end; ++f_it)
    {
        sh = mesh_.property(tshape_,f_it);
        col = value_to_color(sh, min_shape, max_shape);

        face_colors_.push_back((float)col[0]/255);
        face_colors_.push_back((float)col[1]/255);
        face_colors_.push_back((float)col[2]/255);
    }
}

//-----------------------------------------------------------------------------

void QualityViewer::color_coding(Vertex_property prop)
{
    Mesh::VertexIter  v_it, v_end(mesh_.vertices_end());
    Mesh::Scalar      curv, min(FLT_MAX), max(-FLT_MAX);
    Mesh::Color       col;
    
    // put all values into one array
    std::vector<Mesh::Scalar> values;
    values.reserve(mesh_.n_vertices());
    for (v_it=mesh_.vertices_begin(); v_it!=v_end; ++v_it)
        values.push_back(mesh_.property(prop, v_it));

    //discard upper and lower 5%
    unsigned int n = values.size()-1;
    unsigned int i = n / 20;
    std::sort(values.begin(), values.end());
    min = values[i];
    max = values[n-1-i];

    // map curvatures to colors
    for (v_it=mesh_.vertices_begin(); v_it!=v_end; ++v_it)
    {
        curv = mesh_.property(prop, v_it);
        mesh_.set_color(v_it, value_to_color(curv, min, max));
    }
}

QualityViewer::Mesh::Color QualityViewer::value_to_color(QualityViewer::Mesh::Scalar value, QualityViewer::Mesh::Scalar min, QualityViewer::Mesh::Scalar max) 
{
    Mesh::Scalar v0, v1, v2, v3, v4;
    v0 = min + 0.0/4.0 * (max - min);
    v1 = min + 1.0/4.0 * (max - min);
    v2 = min + 2.0/4.0 * (max - min);
    v3 = min + 3.0/4.0 * (max - min);
    v4 = min + 4.0/4.0 * (max - min);

    Mesh::Color col = Mesh::Color(255,255,255);

    unsigned char u;

    if (value < v0) col = Mesh::Color(0, 0, 255);
    else if (value > v4) col = Mesh::Color(255, 0, 0);

    else if (value <= v2) 
    {
        if (value <= v1) // [v0, v1]
        {
            u = (unsigned char) (255.0 * (value - v0) / (v1 - v0));
            col = Mesh::Color(0, u, 255);
        }      
        else // ]v1, v2]
        {
            u = (unsigned char) (255.0 * (value - v1) / (v2 - v1));
            col = Mesh::Color(0, 255, 255-u);
        }
    }
    else 
    {
        if (value <= v3) // ]v2, v3]
        {
            u = (unsigned char) (255.0 * (value - v2) / (v3 - v2));
            col = Mesh::Color(u, 255, 0);
        }
        else // ]v3, v4]
        {
            u = (unsigned char) (255.0 * (value - v3) / (v4 - v3));
            col = Mesh::Color(255, 255-u, 0);
        }
    }

    return col;
}

//-----------------------------------------------------------------------------

void QualityViewer::draw(const std::string& _draw_mode)
{
    if (indices_.empty())
    {
        MeshViewer::draw(_draw_mode);
        return;
    }

    if (_draw_mode == "Mean Curvature") color_coding(vcurvature_);
    if (_draw_mode == "Gaussian Curvature") color_coding(vgausscurvature_);
    if (_draw_mode == "Uniform Mean Curvature") color_coding(vunicurvature_);
    
    //== MeshDOG ============================================================
    if (_draw_mode == "MeshDOG curvature") color_coding(vmeshdog_f_);
    if (_draw_mode == "MeshDOG curvature DOG") color_coding(vmeshdog_dog_);
    //-----------------------------------------------------------------------
    
    if (_draw_mode == "Mean Curvature" || _draw_mode == "Gaussian Curvature" || _draw_mode == "Uniform Mean Curvature" || _draw_mode == "MeshDOG curvature" || _draw_mode == "MeshDOG curvature DOG")
    {

        glDisable(GL_LIGHTING);
        glShadeModel(GL_SMOOTH);

        glEnableClientState(GL_VERTEX_ARRAY);
        glEnableClientState(GL_NORMAL_ARRAY);
        glEnableClientState(GL_COLOR_ARRAY);
        GL::glVertexPointer(mesh_.points());
        GL::glNormalPointer(mesh_.vertex_normals());
        GL::glColorPointer(mesh_.vertex_colors());
        
        glDrawElements(GL_TRIANGLES, indices_.size(), GL_UNSIGNED_INT, &indices_[0]);

        glDisableClientState(GL_VERTEX_ARRAY);
        glDisableClientState(GL_NORMAL_ARRAY);
        glDisableClientState(GL_COLOR_ARRAY);

    }

    if (_draw_mode == "Triangle Shape")
    {

        glDisable(GL_LIGHTING);
        glShadeModel(GL_FLAT);

        glEnableClientState(GL_VERTEX_ARRAY);
        glEnableClientState(GL_NORMAL_ARRAY);
        GL::glVertexPointer(mesh_.points());
        GL::glNormalPointer(mesh_.vertex_normals());


        glDepthRange(0.01, 1.0);
        glBegin(GL_TRIANGLES);
        for (unsigned i=0; i<indices_.size(); i++)
        {
            if (i%3==0) glColor3f(face_colors_[i], face_colors_[i+1], face_colors_[i+2]);
            glArrayElement(indices_[i]);
        }
        glEnd();


        glDisableClientState(GL_VERTEX_ARRAY);
        glDisableClientState(GL_NORMAL_ARRAY);
        glDisableClientState(GL_COLOR_ARRAY);

        glColor3f(0.3, 0.3, 0.3);

        glEnableClientState(GL_VERTEX_ARRAY);
        GL::glVertexPointer(mesh_.points());

        glDrawBuffer(GL_BACK);
        glDepthRange(0.0, 1.0);
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        glDepthFunc(GL_LEQUAL);
        glDrawElements(GL_TRIANGLES, indices_.size(), GL_UNSIGNED_INT, &indices_[0]);

        glDisableClientState(GL_VERTEX_ARRAY);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        glDepthFunc(GL_LESS);
    }

    else if (_draw_mode == "Reflection Lines")
    {
        glTexGeni( GL_S, GL_TEXTURE_GEN_MODE, GL_SPHERE_MAP );
        glTexGeni( GL_T, GL_TEXTURE_GEN_MODE, GL_SPHERE_MAP );
        glEnable( GL_TEXTURE_GEN_S );
        glEnable( GL_TEXTURE_GEN_T );
        glEnable( GL_TEXTURE_2D );    
        glEnable(GL_LIGHTING);
        glShadeModel(GL_SMOOTH);

        glEnableClientState(GL_VERTEX_ARRAY);
        glEnableClientState(GL_NORMAL_ARRAY);
        GL::glVertexPointer(mesh_.points());
        GL::glNormalPointer(mesh_.vertex_normals());

        glDrawElements(GL_TRIANGLES, indices_.size(), GL_UNSIGNED_INT, &indices_[0]);

        glDisableClientState(GL_VERTEX_ARRAY);
        glDisableClientState(GL_NORMAL_ARRAY);

        glDisable( GL_TEXTURE_GEN_S );
        glDisable( GL_TEXTURE_GEN_T );
        glDisable( GL_TEXTURE_2D );
    }
    // == MeshDOG ==================================================================
    else if (_draw_mode == "MeshDOG")
    {
        
        
        // draw points
        glDisable(GL_LIGHTING);
        glColor3f(0, 1.0, 0);
        glPointSize(5.0);
        
        glEnableClientState(GL_VERTEX_ARRAY);
        GL::glVertexPointer(mesh_.points());
        
        glDrawElements(GL_POINTS, static_cast<GLsizei>(indices_.size()), GL_UNSIGNED_INT, &indices_[0]);
        
        glDisableClientState(GL_VERTEX_ARRAY);
        
        
        
        // draw polygon
        glColor3f(1.0, 1.0, 1.0);
        glEnableClientState(GL_VERTEX_ARRAY);
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        GL::glVertexPointer(mesh_.points());
        
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indices_.size()), GL_UNSIGNED_INT, &indices_[0]);
        
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        glDisableClientState(GL_VERTEX_ARRAY);
    }
    else if (_draw_mode == "MeshDOG feature points")
    {
        // draw points
        glDisable(GL_LIGHTING);
        glColor3f(0, 1.0, 0);
        glPointSize(5.0);
        
        glEnableClientState(GL_VERTEX_ARRAY);
        GL::glVertexPointer(mesh_.points());
        
        glDrawElements(GL_POINTS, static_cast<GLsizei>(_dog_feature_points.size()), GL_UNSIGNED_INT, &_dog_feature_points[0]);
        
        glDisableClientState(GL_VERTEX_ARRAY);
    }

    else MeshViewer::draw(_draw_mode);
}

//-----------------------------------------------------------------------------

// == MeshDOG ==================================================================
void QualityViewer::init_meshdog()
{
    // ------------- IMPLEMENT HERE ---------
    // initialize the MeshDOG
    // a). initialize the vmesh_ value to mean curvature
    // ------------- IMPLEMENT HERE ---------
    
    // initialize the value to some type of curvature
    Mesh::VertexIter v_it, v_end(mesh_.vertices_end());
    Mesh::VertexVertexIter vv_it;
    Mesh::Scalar eavg;
    Mesh::Point v0, v1;
    int valence = 0;
    
    for (v_it = mesh_.vertices_begin(); v_it != v_end; ++v_it)
    {
        // initialize the vmeshdog_ value to curvature
        mesh_.property(vmeshdog_f_, v_it) = mesh_.property(vunicurvature_, v_it);
        valence = 0; eavg = 0;
        v0 = mesh_.point( v_it );
        
        // calculate e_avg for each vertex
        for (vv_it = mesh_.vv_iter( v_it ); vv_it; ++vv_it)
        {
            v1 = mesh_.point( vv_it );
            valence++;
            eavg += (v0 - v1).norm();
        }
        
        // delete single points
        // techniquely not actually delete it, just set the f(v_i) to 0
        if (valence == 0)
            //mesh_.property(vmeshdog_f_, v_it) = 0;
            std::cout<<mesh_.property(vcurvature_, v_it)<<std::endl;
            //mesh_.delete_vertex(v_it);
        else
            mesh_.property(veavg_, v_it) = eavg / valence;
    }
    
    //mesh_.garbage_collection();
}


//-----------------------------------------------------------------------------
void QualityViewer::detect_meshdog(int _iters)
{
    // ------------- IMPLEMENT HERE ---------
    // detect MeshDOG feature
    // b). gaussian convolution
    // c). thresholding, top 5% will be sorted
    // d). corner detection
    // ------------- IMPLEMENT HERE ---------
    Mesh::VertexIter            v_it, v_end(mesh_.vertices_end());
    Mesh::VertexVertexIter      vv_it;
    Mesh::Scalar                f0, f1(0);
    Mesh::Point                 vi, vj;
    float                       theta, k, K(0);
    
    // perform gaussian convolution
    for (int i = 0; i < _iters; ++ i)
    {
        for (v_it = mesh_.vertices_begin(); v_it != v_end; ++ v_it)
        {
            // check if the vertex is a single point
            if (!isnan(mesh_.property(vcurvature_, v_it)))
            {
                theta = mesh_.property(veavg_, v_it);
                f0 = mesh_.property(vmeshdog_f_, v_it);
                f1 = 0; K = 0;
                vi = mesh_.point(v_it);
                theta = pow(2, 1.0/3.0) * mesh_.property(veavg_, v_it);
                
                for (vv_it = mesh_.vv_iter(v_it); vv_it; ++vv_it)
                {
                    vj = mesh_.point(vv_it);
                    k = gaussian_conv((vi - vj).norm(), theta); K += k;
                    f1 += mesh_.property(vmeshdog_f_, vv_it) * k;
                    //std::cout<<gaussian_conv((vi - vj).norm(), theta)<<std::endl;
                }
                f1 = f1 / K;
                
                mesh_.property(vmeshdog_dog_, v_it) = f1 - f0;
                mesh_.property(vmeshdog_f_, v_it) = f1;
            }
        }
    }
    
    // debug
//    for (v_it = mesh_.vertices_begin(); v_it != v_end; ++ v_it)
//    {
//        std::cout<< mesh_.property(vmeshdog_dog_, v_it) << std::endl;
//    }
    
    // thresholding
    std::vector<Mesh::Scalar>   vec_dog;
    Mesh::Scalar                threshold;
    std::vector<Mesh::VIter>    vec_viter;
    //std::vector<int>            dog_feature_points;
    int thresh_index = int(mesh_.n_vertices() * 0.95);
    
    for (v_it = mesh_.vertices_begin(); v_it != v_end; ++v_it)
    {
        vec_dog.push_back(mesh_.property(vmeshdog_dog_, v_it));
    }
    
    std::sort(vec_dog.begin(), vec_dog.end());
    threshold = vec_dog[thresh_index];
    
    _dog_feature_points.clear();
    _dog_feature_handles.clear();
    
    for( v_it = mesh_.vertices_begin(); v_it != v_end; ++v_it)
    {
        // if DOG exists and above threshold
        if (!isnan(mesh_.property(vcurvature_, v_it)) && mesh_.property(vmeshdog_dog_, v_it) >= threshold)
        {
            _dog_feature_points.push_back(v_it.handle().idx());
            _dog_feature_handles.push_back(v_it.handle());
        }
    }
    
    // debug
    std::cout<<"Detected "<<_dog_feature_points.size()<<" feature points"<<std::endl;
    
    // corner detection
    Mesh::Scalar dxx, dyy;
    // conrner detection is not necessary for this project
    // TODO
    
}

//-----------------------------------------------------------------------------
float QualityViewer::gaussian_conv(float _edge_length, float _theta)
{
    // ------------- IMPLEMENT HERE ---------
    // helper function for gaussian conv
    // return k(||vivj||) of the paper
    // ------------- IMPLEMENT HERE ---------

    float k;
    k = exp(-pow(_edge_length, 2) / (2 * pow(_theta, 2)))
    / (_theta * sqrt(2 * M_PI));
    return k;
}

//-----------------------------------------------------------------------------
void QualityViewer::save_meshdog()
{
    Mesh::Point p;
    Mesh::VertexHandle vh;
    std::vector<Mesh::VertexHandle>::iterator iter;
    
    for(iter = _dog_feature_handles.begin(); iter != _dog_feature_handles.end(); ++iter)
    {
        vh = *iter;
        p = mesh_.point(vh);
        new_mesh.add_vertex(p);
    }
    
    // save mesh to file
    if ( !OpenMesh::IO::write_mesh(new_mesh, "dog_points.ply" ))
        std::cout<<"Failed to save the mesh!"<<std::endl;
}

//=============================================================================
