/****************************************************************************
 * Copyright (c) 2018-2021 by the Cabana authors                            *
 * All rights reserved.                                                     *
 *                                                                          *
 * This file is part of the Cabana library. Cabana is distributed under a   *
 * BSD 3-clause license. For the licensing terms see the LICENSE file in    *
 * the top-level directory.                                                 *
 *                                                                          *
 * SPDX-License-Identifier: BSD-3-Clause                                    *
 ****************************************************************************/

#include <Cajita.hpp>

#include <Kokkos_Core.hpp>

#include <mpi.h>

#include <array>
#include <vector>

//---------------------------------------------------------------------------//
// Structured Solver Example
//---------------------------------------------------------------------------//
void structuredSolverExample()
{
    std::cout << "Cajita Structured Solver Example\n" << std::endl;

    using MemorySpace = Kokkos::HostSpace;
    using ExecutionSpace = Kokkos::DefaultHostExecutionSpace;

    // Create the global grid.
    double cell_size = 0.25;
    std::array<bool, 3> is_dim_periodic = { false, false, false };
    std::array<double, 3> global_low_corner = { -1.0, -2.0, -1.0 };
    std::array<double, 3> global_high_corner = { 1.0, 1.0, 0.5 };
    auto global_mesh = Cajita::createUniformGlobalMesh(
        global_low_corner, global_high_corner, cell_size );

    // Create the global grid.
    Cajita::DimBlockPartitioner<3> partitioner;
    auto global_grid = Cajita::createGlobalGrid( MPI_COMM_WORLD, global_mesh,
                                                 is_dim_periodic, partitioner );

    // Create a local grid.
    auto local_mesh = createLocalGrid( global_grid, 1 );
    auto owned_space = local_mesh->indexSpace( Cajita::Own(), Cajita::Cell(),
                                               Cajita::Local() );

    // Create the RHS.
    auto vector_layout = createArrayLayout( local_mesh, 1, Cajita::Cell() );
    auto rhs = Cajita::createArray<double, MemorySpace>( "rhs", vector_layout );
    Cajita::ArrayOp::assign( *rhs, 1.0, Cajita::Own() );

    // Create a 7-point 3d laplacian stencil.
    std::vector<std::array<int, 3>> stencil = {
        { 0, 0, 0 }, { -1, 0, 0 }, { 1, 0, 0 }, { 0, -1, 0 },
        { 0, 1, 0 }, { 0, 0, -1 }, { 0, 0, 1 } };
    // Create a solver reference for comparison.
    auto lhs_ref =
        Cajita::createArray<double, MemorySpace>( "lhs_ref", vector_layout );
    Cajita::ArrayOp::assign( *lhs_ref, 0.0, Cajita::Own() );

    auto ref_solver =
        Cajita::createReferenceConjugateGradient<double, MemorySpace>(
            *vector_layout );
    ref_solver->setMatrixStencil( stencil );
    const auto& ref_entries = ref_solver->getMatrixValues();
    auto matrix_view = ref_entries.view();
    auto global_space = local_mesh->indexSpace( Cajita::Own(), Cajita::Cell(),
                                                Cajita::Global() );
    int ncell_i =
        global_grid->globalNumEntity( Cajita::Cell(), Cajita::Dim::I );
    int ncell_j =
        global_grid->globalNumEntity( Cajita::Cell(), Cajita::Dim::J );
    int ncell_k =
        global_grid->globalNumEntity( Cajita::Cell(), Cajita::Dim::K );
    Kokkos::parallel_for(
        "fill_ref_entries",
        createExecutionPolicy( owned_space, ExecutionSpace() ),
        KOKKOS_LAMBDA( const int i, const int j, const int k ) {
            int gi = i + global_space.min( Cajita::Dim::I ) -
                     owned_space.min( Cajita::Dim::I );
            int gj = j + global_space.min( Cajita::Dim::J ) -
                     owned_space.min( Cajita::Dim::J );
            int gk = k + global_space.min( Cajita::Dim::K ) -
                     owned_space.min( Cajita::Dim::K );
            matrix_view( i, j, k, 0 ) = 6.0;
            matrix_view( i, j, k, 1 ) = ( gi - 1 >= 0 ) ? -1.0 : 0.0;
            matrix_view( i, j, k, 2 ) = ( gi + 1 < ncell_i ) ? -1.0 : 0.0;
            matrix_view( i, j, k, 3 ) = ( gj - 1 >= 0 ) ? -1.0 : 0.0;
            matrix_view( i, j, k, 4 ) = ( gj + 1 < ncell_j ) ? -1.0 : 0.0;
            matrix_view( i, j, k, 5 ) = ( gk - 1 >= 0 ) ? -1.0 : 0.0;
            matrix_view( i, j, k, 6 ) = ( gk + 1 < ncell_k ) ? -1.0 : 0.0;
        } );

    std::vector<std::array<int, 3>> diag_stencil = { { 0, 0, 0 } };
    ref_solver->setPreconditionerStencil( diag_stencil );
    const auto& preconditioner_entries = ref_solver->getPreconditionerValues();
    auto preconditioner_view = preconditioner_entries.view();
    Kokkos::parallel_for(
        "fill_preconditioner_entries",
        createExecutionPolicy( owned_space, ExecutionSpace() ),
        KOKKOS_LAMBDA( const int i, const int j, const int k ) {
            preconditioner_view( i, j, k, 0 ) = 1.0 / 6.0;
        } );

    ref_solver->setTolerance( 1.0e-11 );
    ref_solver->setPrintLevel( 2 );
    ref_solver->setup();
    ref_solver->solve( *rhs, *lhs_ref );

    // Compute another reference solution.
    Cajita::ArrayOp::assign( *lhs_ref, 0.0, Cajita::Own() );
    ref_solver->solve( *rhs, *lhs_ref );
}

//---------------------------------------------------------------------------//
// Main.
//---------------------------------------------------------------------------//
int main( int argc, char* argv[] )
{
    // MPI only needed to create the grid/mesh. Not intended to be run with
    // multiple ranks.
    MPI_Init( &argc, &argv );
    {
        Kokkos::ScopeGuard scope_guard( argc, argv );

        structuredSolverExample();
    }
    MPI_Finalize();

    return 0;
}
//---------------------------------------------------------------------------//
