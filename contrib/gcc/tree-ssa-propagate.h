/* Data structures and function declarations for the SSA value propagation
   engine.
   Copyright (C) 2004, 2005 Free Software Foundation, Inc.
   Contributed by Diego Novillo <dnovillo@redhat.com>

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

GCC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to
the Free Software Foundation, 51 Franklin Street, Fifth Floor,
Boston, MA 02110-1301, USA.  */

#ifndef _TREE_SSA_PROPAGATE_H
#define _TREE_SSA_PROPAGATE_H 1

/* Use the TREE_VISITED bitflag to mark statements and PHI nodes that
   have been deemed varying and should not be simulated again.  */
#define DONT_SIMULATE_AGAIN(T)	TREE_VISITED (T)

/* Lattice values used for propagation purposes.  Specific instances
   of a propagation engine must return these values from the statement
   and PHI visit functions to direct the engine.  */
enum ssa_prop_result {
    /* The statement produces nothing of interest.  No edges will be
       added to the work lists.  */
    SSA_PROP_NOT_INTERESTING,

    /* The statement produces an interesting value.  The set SSA_NAMEs
       returned by SSA_PROP_VISIT_STMT should be added to
       INTERESTING_SSA_EDGES.  If the statement being visited is a
       conditional jump, SSA_PROP_VISIT_STMT should indicate which edge
       out of the basic block should be marked executable.  */
    SSA_PROP_INTERESTING,

    /* The statement produces a varying (i.e., useless) value and
       should not be simulated again.  If the statement being visited
       is a conditional jump, all the edges coming out of the block
       will be considered executable.  */
    SSA_PROP_VARYING
};


struct prop_value_d {
    /* Lattice value.  Each propagator is free to define its own
       lattice and this field is only meaningful while propagating.
       It will not be used by substitute_and_fold.  */
    unsigned lattice_val;

    /* Propagated value.  */
    tree value;

    /* If this value is held in an SSA name for a non-register
       variable, this field holds the actual memory reference
       associated with this value.  This field is taken from 
       the LHS of the assignment that generated the associated SSA
       name.  However, in the case of PHI nodes, this field is copied
       from the PHI arguments (assuming that all the arguments have
       the same memory reference).  See replace_vuses_in for a more
       detailed description.  */
    tree mem_ref;
};

typedef struct prop_value_d prop_value_t;


/* Type of value ranges.  See value_range_d for a description of these
   types.  */
enum value_range_type { VR_UNDEFINED, VR_RANGE, VR_ANTI_RANGE, VR_VARYING };

/* Range of values that can be associated with an SSA_NAME after VRP
   has executed.  */
struct value_range_d
{
  /* Lattice value represented by this range.  */
  enum value_range_type type;

  /* Minimum and maximum values represented by this range.  These
     values should be interpreted as follows:

	- If TYPE is VR_UNDEFINED or VR_VARYING then MIN and MAX must
	  be NULL.

	- If TYPE == VR_RANGE then MIN holds the minimum value and
	  MAX holds the maximum value of the range [MIN, MAX].

	- If TYPE == ANTI_RANGE the variable is known to NOT
	  take any values in the range [MIN, MAX].  */
  tree min;
  tree max;

  /* Set of SSA names whose value ranges are equivalent to this one.
     This set is only valid when TYPE is VR_RANGE or VR_ANTI_RANGE.  */
  bitmap equiv;
};

typedef struct value_range_d value_range_t;


/* Call-back functions used by the value propagation engine.  */
typedef enum ssa_prop_result (*ssa_prop_visit_stmt_fn) (tree, edge *, tree *);
typedef enum ssa_prop_result (*ssa_prop_visit_phi_fn) (tree);


/* In tree-ssa-propagate.c  */
void ssa_propagate (ssa_prop_visit_stmt_fn, ssa_prop_visit_phi_fn);
tree get_rhs (tree);
bool set_rhs (tree *, tree);
tree first_vdef (tree);
bool stmt_makes_single_load (tree);
bool stmt_makes_single_store (tree);
prop_value_t *get_value_loaded_by (tree, prop_value_t *);
bool replace_uses_in (tree, bool *, prop_value_t *);
void substitute_and_fold (prop_value_t *, bool);

#endif /* _TREE_SSA_PROPAGATE_H  */
