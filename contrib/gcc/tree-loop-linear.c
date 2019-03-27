/* Linear Loop transforms
   Copyright (C) 2003, 2004, 2005 Free Software Foundation, Inc.
   Contributed by Daniel Berlin <dberlin@dberlin.org>.

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


#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "ggc.h"
#include "tree.h"
#include "target.h"

#include "rtl.h"
#include "basic-block.h"
#include "diagnostic.h"
#include "tree-flow.h"
#include "tree-dump.h"
#include "timevar.h"
#include "cfgloop.h"
#include "expr.h"
#include "optabs.h"
#include "tree-chrec.h"
#include "tree-data-ref.h"
#include "tree-scalar-evolution.h"
#include "tree-pass.h"
#include "lambda.h"

/* Linear loop transforms include any composition of interchange,
   scaling, skewing, and reversal.  They are used to change the
   iteration order of loop nests in order to optimize data locality of
   traversals, or remove dependences that prevent
   parallelization/vectorization/etc.  

   TODO: Determine reuse vectors/matrix and use it to determine optimal
   transform matrix for locality purposes.
   TODO: Completion of partial transforms.  */

/* Gather statistics for loop interchange.  LOOP is the loop being
   considered. The first loop in the considered loop nest is
   FIRST_LOOP, and consequently, the index of the considered loop is
   obtained by LOOP->DEPTH - FIRST_LOOP->DEPTH
   
   Initializes:
   - DEPENDENCE_STEPS the sum of all the data dependence distances
   carried by loop LOOP,

   - NB_DEPS_NOT_CARRIED_BY_LOOP the number of dependence relations
   for which the loop LOOP is not carrying any dependence,

   - ACCESS_STRIDES the sum of all the strides in LOOP.

   Example: for the following loop,

   | loop_1 runs 1335 times
   |   loop_2 runs 1335 times
   |     A[{{0, +, 1}_1, +, 1335}_2]
   |     B[{{0, +, 1}_1, +, 1335}_2]
   |   endloop_2
   |   A[{0, +, 1336}_1]
   | endloop_1

   gather_interchange_stats (in loop_1) will return 
   DEPENDENCE_STEPS = 3002
   NB_DEPS_NOT_CARRIED_BY_LOOP = 5
   ACCESS_STRIDES = 10694

   gather_interchange_stats (in loop_2) will return 
   DEPENDENCE_STEPS = 3000
   NB_DEPS_NOT_CARRIED_BY_LOOP = 7
   ACCESS_STRIDES = 8010
*/

static void
gather_interchange_stats (VEC (ddr_p, heap) *dependence_relations,
			  VEC (data_reference_p, heap) *datarefs,
			  struct loop *loop,
			  struct loop *first_loop,
			  unsigned int *dependence_steps, 
			  unsigned int *nb_deps_not_carried_by_loop, 
			  unsigned int *access_strides)
{
  unsigned int i, j;
  struct data_dependence_relation *ddr;
  struct data_reference *dr;

  *dependence_steps = 0;
  *nb_deps_not_carried_by_loop = 0;
  *access_strides = 0;

  for (i = 0; VEC_iterate (ddr_p, dependence_relations, i, ddr); i++)
    {
      /* If we don't know anything about this dependence, or the distance
	 vector is NULL, or there is no dependence, then there is no reuse of
	 data.  */
      if (DDR_ARE_DEPENDENT (ddr) == chrec_dont_know
	  || DDR_ARE_DEPENDENT (ddr) == chrec_known
	  || DDR_NUM_DIST_VECTS (ddr) == 0)
	continue;

      for (j = 0; j < DDR_NUM_DIST_VECTS (ddr); j++)
	{
	  int dist = DDR_DIST_VECT (ddr, j)[loop->depth - first_loop->depth];

	  if (dist == 0)
	    (*nb_deps_not_carried_by_loop) += 1;

	  else if (dist < 0)
	    (*dependence_steps) += -dist;

	  else
	    (*dependence_steps) += dist;
	}
    }

  /* Compute the access strides.  */
  for (i = 0; VEC_iterate (data_reference_p, datarefs, i, dr); i++)
    {
      unsigned int it;
      tree stmt = DR_STMT (dr);
      struct loop *stmt_loop = loop_containing_stmt (stmt);
      struct loop *inner_loop = first_loop->inner;
      
      if (inner_loop != stmt_loop 
	  && !flow_loop_nested_p (inner_loop, stmt_loop))
	continue;
      for (it = 0; it < DR_NUM_DIMENSIONS (dr); it++)
	{
	  tree chrec = DR_ACCESS_FN (dr, it);
	  tree tstride = evolution_part_in_loop_num 
	    (chrec, loop->num);
	  
	  if (tstride == NULL_TREE
	      || TREE_CODE (tstride) != INTEGER_CST)
	    continue;
	  
	  (*access_strides) += int_cst_value (tstride);
	}
    }
}

/* Attempt to apply interchange transformations to TRANS to maximize the
   spatial and temporal locality of the loop.  
   Returns the new transform matrix.  The smaller the reuse vector
   distances in the inner loops, the fewer the cache misses.
   FIRST_LOOP is the loop->num of the first loop in the analyzed loop
   nest.  */


static lambda_trans_matrix
try_interchange_loops (lambda_trans_matrix trans, 
		       unsigned int depth,		       
		       VEC (ddr_p, heap) *dependence_relations,
		       VEC (data_reference_p, heap) *datarefs,
		       struct loop *first_loop)
{
  struct loop *loop_i;
  struct loop *loop_j;
  unsigned int dependence_steps_i, dependence_steps_j;
  unsigned int access_strides_i, access_strides_j;
  unsigned int nb_deps_not_carried_by_i, nb_deps_not_carried_by_j;
  struct data_dependence_relation *ddr;

  if (VEC_length (ddr_p, dependence_relations) == 0)
    return trans;

  /* When there is an unknown relation in the dependence_relations, we
     know that it is no worth looking at this loop nest: give up.  */
  ddr = VEC_index (ddr_p, dependence_relations, 0);
  if (ddr == NULL || DDR_ARE_DEPENDENT (ddr) == chrec_dont_know)
    return trans;
  
  /* LOOP_I is always the outer loop.  */
  for (loop_j = first_loop->inner; 
       loop_j; 
       loop_j = loop_j->inner)
    for (loop_i = first_loop; 
	 loop_i->depth < loop_j->depth; 
	 loop_i = loop_i->inner)
      {
	gather_interchange_stats (dependence_relations, datarefs,
				  loop_i, first_loop,
				  &dependence_steps_i, 
				  &nb_deps_not_carried_by_i,
				  &access_strides_i);
	gather_interchange_stats (dependence_relations, datarefs,
				  loop_j, first_loop,
				  &dependence_steps_j, 
				  &nb_deps_not_carried_by_j, 
				  &access_strides_j);
	
	/* Heuristics for loop interchange profitability:

	   1. (spatial locality) Inner loops should have smallest
              dependence steps.

	   2. (spatial locality) Inner loops should contain more
	   dependence relations not carried by the loop.

	   3. (temporal locality) Inner loops should have smallest 
	      array access strides.
	*/
	if (dependence_steps_i < dependence_steps_j 
	    || nb_deps_not_carried_by_i > nb_deps_not_carried_by_j
	    || access_strides_i < access_strides_j)
	  {
	    lambda_matrix_row_exchange (LTM_MATRIX (trans),
					loop_i->depth - first_loop->depth,
					loop_j->depth - first_loop->depth);
	    /* Validate the resulting matrix.  When the transformation
	       is not valid, reverse to the previous transformation.  */
	    if (!lambda_transform_legal_p (trans, depth, dependence_relations))
	      lambda_matrix_row_exchange (LTM_MATRIX (trans), 
					  loop_i->depth - first_loop->depth, 
					  loop_j->depth - first_loop->depth);
	  }
      }

  return trans;
}

/* Perform a set of linear transforms on LOOPS.  */

void
linear_transform_loops (struct loops *loops)
{
  bool modified = false;
  unsigned int i;
  VEC(tree,heap) *oldivs = NULL;
  VEC(tree,heap) *invariants = NULL;
  
  for (i = 1; i < loops->num; i++)
    {
      unsigned int depth = 0;
      VEC (ddr_p, heap) *dependence_relations;
      VEC (data_reference_p, heap) *datarefs;
      struct loop *loop_nest = loops->parray[i];
      struct loop *temp;
      lambda_loopnest before, after;
      lambda_trans_matrix trans;
      bool problem = false;
      /* If it's not a loop nest, we don't want it.
         We also don't handle sibling loops properly, 
         which are loops of the following form:
         for (i = 0; i < 50; i++)
           {
             for (j = 0; j < 50; j++)
               {
	        ...
               }
             for (j = 0; j < 50; j++)
               {
                ...
               }
           } */
      if (!loop_nest || !loop_nest->inner || !loop_nest->single_exit)
	continue;
      VEC_truncate (tree, oldivs, 0);
      VEC_truncate (tree, invariants, 0);
      depth = 1;
      for (temp = loop_nest->inner; temp; temp = temp->inner)
	{
	  /* If we have a sibling loop or multiple exit edges, jump ship.  */
	  if (temp->next || !temp->single_exit)
	    {
	      problem = true;
	      break;
	    }
	  depth ++;
	}
      if (problem)
	continue;

      /* Analyze data references and dependence relations using scev.  */      
 
      datarefs = VEC_alloc (data_reference_p, heap, 10);
      dependence_relations = VEC_alloc (ddr_p, heap, 10 * 10);
      compute_data_dependences_for_loop (loop_nest, true, &datarefs,
					 &dependence_relations);

      if (dump_file && (dump_flags & TDF_DETAILS))
	dump_ddrs (dump_file, dependence_relations);

      /* Build the transformation matrix.  */
      trans = lambda_trans_matrix_new (depth, depth);
      lambda_matrix_id (LTM_MATRIX (trans), depth);
      trans = try_interchange_loops (trans, depth, dependence_relations,
				     datarefs, loop_nest);

      if (lambda_trans_matrix_id_p (trans))
	{
	  if (dump_file)
	   fprintf (dump_file, "Won't transform loop. Optimal transform is the identity transform\n");
	  goto free_and_continue;
	}

      /* Check whether the transformation is legal.  */
      if (!lambda_transform_legal_p (trans, depth, dependence_relations))
	{
	  if (dump_file)
	    fprintf (dump_file, "Can't transform loop, transform is illegal:\n");
	  goto free_and_continue;
	}

      before = gcc_loopnest_to_lambda_loopnest (loops, loop_nest, &oldivs,
						&invariants);

      if (!before)
	goto free_and_continue;

      if (dump_file)
	{
	  fprintf (dump_file, "Before:\n");
	  print_lambda_loopnest (dump_file, before, 'i');
	}
  
      after = lambda_loopnest_transform (before, trans);

      if (dump_file)
	{
	  fprintf (dump_file, "After:\n");
	  print_lambda_loopnest (dump_file, after, 'u');
	}

      lambda_loopnest_to_gcc_loopnest (loop_nest, oldivs, invariants,
				       after, trans);
      modified = true;

      if (dump_file)
	fprintf (dump_file, "Successfully transformed loop.\n");

    free_and_continue:
      free_dependence_relations (dependence_relations);
      free_data_refs (datarefs);
    }

  VEC_free (tree, heap, oldivs);
  VEC_free (tree, heap, invariants);
  scev_reset ();

  if (modified)
    rewrite_into_loop_closed_ssa (NULL, TODO_update_ssa_full_phi);
}
