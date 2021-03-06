// Copyright (C) 2010-2013 von Karman Institute for Fluid Dynamics, Belgium
//
// This software is distributed under the terms of the
// GNU Lesser General Public License version 3 (LGPLv3).
// See doc/lgpl.txt and doc/gpl.txt for the license text.

#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE "Test module for cf3::dcm"


#include <boost/test/unit_test.hpp>
#include <boost/assign/list_of.hpp>
#include "cf3/common/Log.hpp"
#include "cf3/common/Core.hpp"
#include "cf3/common/Environment.hpp"
#include "cf3/common/OSystem.hpp"
#include "cf3/common/OSystemLayer.hpp"
#include "cf3/common/OptionList.hpp"
#include "cf3/common/List.hpp"
#include "cf3/common/Group.hpp"
#include "cf3/common/PE/Comm.hpp"
#include "cf3/common/PE/debug.hpp"
#include "cf3/math/Consts.hpp"
#include "cf3/math/VariablesDescriptor.hpp"
#include "cf3/mesh/Domain.hpp"
#include "cf3/mesh/Mesh.hpp"
#include "cf3/mesh/Dictionary.hpp"
#include "cf3/mesh/Field.hpp"
#include "cf3/mesh/Space.hpp"
#include "cf3/mesh/Cells.hpp"
#include "cf3/mesh/SimpleMeshGenerator.hpp"
#include "cf3/mesh/MeshTransformer.hpp"
#include "cf3/mesh/Region.hpp"
#include "cf3/mesh/actions/LoadBalance.hpp"
#include "cf3/mesh/actions/Rotate.hpp"
#include "cf3/mesh/actions/CreateField.hpp"
#include "cf3/dcm/Model.hpp"
#include "cf3/dcm/tools/ComputeFieldGradientBR2.hpp"

using namespace boost;
using namespace boost::assign;
using namespace cf3;
using namespace cf3::math;
using namespace cf3::common;
using namespace cf3::common::PE;
using namespace cf3::mesh;
using namespace cf3::mesh::actions;
using namespace cf3::dcm;
using namespace cf3::dcm::core;
using namespace cf3::dcm::tools;

struct dcm_test_fixture
{
  /// common setup for each test case
  dcm_test_fixture()
  {
    m_argc = boost::unit_test::framework::master_test_suite().argc;
    m_argv = boost::unit_test::framework::master_test_suite().argv;
  }

  /// common tear-down for each test case
  ~dcm_test_fixture()
  {
  }
  /// possibly common functions used on the tests below


  /// common values accessed by all tests goes here
  int    m_argc;
  char** m_argv;

};

////////////////////////////////////////////////////////////////////////////////

BOOST_FIXTURE_TEST_SUITE( dcm_test_testsuite, dcm_test_fixture )

////////////////////////////////////////////////////////////////////////////////

BOOST_AUTO_TEST_CASE( init_mpi )
{
  PE::Comm::instance().init(m_argc,m_argv);
  Core::instance().environment().options().set("log_level", (Uint)DEBUG);
}

////////////////////////////////////////////////////////////////////////////////

BOOST_AUTO_TEST_CASE( test_compute_grad_1d )
{
  Handle<dcm::Model> model = Core::instance().root().create_component<dcm::Model>("model2d");

  Handle<Mesh>          mesh           = model->domain()->create_component<Mesh>("mesh");
  Handle<MeshGenerator> mesh_generator = model->tools()->create_component("mesh_generator","cf3.mesh.SimpleMeshGenerator")->handle<MeshGenerator>();
  Real length=2*math::Consts::pi();
  Uint nb_cells = 10;
  mesh_generator->options().set("mesh",mesh->uri());
  mesh_generator->options().set("lengths",std::vector<Real>(1,length));
  mesh_generator->options().set("nb_cells",std::vector<Uint>(1,nb_cells));
  mesh_generator->execute();
  allocate_component<LoadBalance>("repartitioner")->transform(mesh);

  Handle<Dictionary> solution_space = model->create_space("solution_space","cf3.dcm.core.LegendreGaussEndP2", std::vector< Handle<Component> >(1,mesh->handle()));

  Field& field  = solution_space->create_field("field", SCALAR);
  Field& coords = solution_space->coordinates();
  boost_foreach( const Handle<Space>& space, field.spaces() )
  {
    for( Uint e=0; e<space->size(); ++e )
    {
      for( Uint n=0; n<space->shape_function().nb_nodes(); ++n )
      {
        const Uint p = space->connectivity()[e][n];
        const Real& x = coords[p][0];
        Real& y = field[p][0];
        y = cos(2.*math::Consts::pi()*x/length);
        Real overshoot=0.2;
        if (n==0)
        {
          if(e<nb_cells/2)
            y += std::abs(y*overshoot);
          else
            y -= std::abs(y*overshoot);
        }
        if (n==space->shape_function().nb_nodes()-1 )
        {
          if(e<nb_cells/2)
            y -= std::abs(y*overshoot);
          else
            y += std::abs(y*overshoot);
        }
      }
    }
  }

//  boost::shared_ptr<CreateField> create_field = allocate_component<CreateField>("create_field");
//  std::vector<std::string> functions;
//  functions.push_back("f=cos(x)");
//  create_field->options().set("functions",functions);
//  create_field->options().set("name",std::string("field"));
//  create_field->options().set("dict",solution_space->uri());
//  create_field->transform(mesh);

//  Field& field = *solution_space->get_child_checked("field")->handle<Field>();
  Field& grad = solution_space->create_field("grad",SCALAR);

  boost::shared_ptr<dcm::tools::ComputeFieldGradientBR2> compute_gradient = allocate_component<dcm::tools::ComputeFieldGradientBR2>("compute_gradient");

  compute_gradient->options().set("mesh",mesh->handle());
  compute_gradient->options().set("field",field.handle());
  compute_gradient->options().set("field_gradient",grad.handle());

  compute_gradient->options().set("BR2",true);
  compute_gradient->options().set("alpha",0.33);

  compute_gradient->execute();

  std::vector<URI> fields;
  fields.push_back(field.uri());
  fields.push_back(grad.uri());
  mesh->write_mesh("file:out-utest-dcm-tools-fieldgradient-1d.msh",fields);

  std::ofstream of("plot.tsv");
  boost_foreach( const Handle<Space>& space, field.spaces() )
  {
    if (space->support().handle<Cells>())
    {
      std::cout << space->uri() << std::endl;
      for( Uint e=0; e<space->size(); ++e )
      {
        std::cout << e << std::endl;
        for( Uint n=0; n<space->shape_function().nb_nodes(); ++n )
        {
          const Uint p   = space->connectivity()[e][n];
          const Real& x  = coords[p][0];
          const Real& y  = field[p][0];
          const Real& dy = grad[p][0];
          of << x << "\t" << y << "\t" << dy << std::endl;
        }
      }
    }
  }

  CFinfo << mesh->tree() << CFendl;
}

////////////////////////////////////////////////////////////////////////////////

BOOST_AUTO_TEST_CASE( test_compute_grad_2d )
{
  Handle<dcm::Model> model = Core::instance().root().create_component<dcm::Model>("model2d");

  Handle<Mesh>          mesh           = model->domain()->create_component<Mesh>("mesh");
  Handle<MeshGenerator> mesh_generator = model->tools()->create_component("mesh_generator","cf3.mesh.SimpleMeshGenerator")->handle<MeshGenerator>();
  mesh_generator->options().set("mesh",mesh->uri());
  mesh_generator->options().set("lengths",std::vector<Real>(2,2*math::Consts::pi()));
  mesh_generator->options().set("nb_cells",std::vector<Uint>(2,20));
  mesh_generator->execute();
  allocate_component<LoadBalance>("repartitioner")->transform(mesh);

  boost::shared_ptr<Rotate> rotate_mesh = allocate_component<Rotate>("rotate_mesh");
  rotate_mesh->options().set("mesh",mesh->handle());
  rotate_mesh->options().set("angle",math::Consts::pi()/4.);
  rotate_mesh->options().set("axis_point",std::vector<Real>(2,math::Consts::pi()));
  rotate_mesh->execute();

  Handle<Dictionary> solution_space = model->create_space("solution_space","cf3.dcm.core.LegendreGaussEndP3", std::vector< Handle<Component> >(1,mesh->handle()));

  boost::shared_ptr<CreateField> create_field = allocate_component<CreateField>("create_field");
  std::vector<std::string> functions;
  functions.push_back("f=cos(x)+cos(y)");
  create_field->options().set("functions",functions);
  create_field->options().set("name",std::string("field"));
  create_field->options().set("dict",solution_space->uri());
  create_field->transform(mesh);

  Field& field = *solution_space->get_child_checked("field")->handle<Field>();
  Field& grad = mesh->geometry_fields().create_field("grad","dfdx,dfdy");

  boost::shared_ptr<dcm::tools::ComputeFieldGradientBR2> compute_gradient = allocate_component<dcm::tools::ComputeFieldGradientBR2>("compute_gradient");

  compute_gradient->options().set("mesh",mesh->handle());
  compute_gradient->options().set("field",field.handle());
  compute_gradient->options().set("field_gradient",grad.handle());

  compute_gradient->execute();

  std::vector<URI> fields;
  fields.push_back(field.uri());
  fields.push_back(grad.uri());
  mesh->write_mesh("file:out-utest-dcm-tools-fieldgradient-2d.msh",fields);

  CFinfo << mesh->tree() << CFendl;
}

////////////////////////////////////////////////////////////////////////////////

BOOST_AUTO_TEST_CASE( finalize_mpi )
{
  PE::Comm::instance().finalize();
}

////////////////////////////////////////////////////////////////////////////////

BOOST_AUTO_TEST_SUITE_END()

////////////////////////////////////////////////////////////////////////////////
