/* Scalar evolution detector.
   Copyright (C) 2003, 2004, 2005 Free Software Foundation, Inc.
   Contributed by Sebastian Pop <s.pop@laposte.net>

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 2, or (at your option) any later
version.

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to the Free
Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
02110-1301, USA.  */

#ifndef GCC_TREE_SCALAR_EVOLUTION_H
#define GCC_TREE_SCALAR_EVOLUTION_H

extern tree number_of_iterations_in_loop (struct loop *);
extern tree get_loop_exit_condition (struct loop *);

extern void scev_initialize (struct loops *loops);
extern void scev_reset (void);
extern void scev_finalize (void);
extern tree analyze_scalar_evolution (struct loop *, tree);
extern tree instantiate_parameters (struct loop *, tree);
extern void gather_stats_on_scev_database (void);
extern void scev_analysis (void);
unsigned int scev_const_prop (void);

extern bool simple_iv (struct loop *, tree, tree, affine_iv *, bool);

#endif  /* GCC_TREE_SCALAR_EVOLUTION_H  */
