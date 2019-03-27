/* Calculate branch probabilities, and basic block execution counts.
   Copyright (C) 1990, 1991, 1992, 1993, 1994, 1996, 1997, 1998, 1999,
   2000, 2001, 2002, 2003, 2004, 2005  Free Software Foundation, Inc.
   Contributed by James E. Wilson, UC Berkeley/Cygnus Support;
   based on some ideas from Dain Samples of UC Berkeley.
   Further mangling by Bob Manson, Cygnus Support.
   Converted to use trees by Dale Johannesen, Apple Computer.

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

/* Generate basic block profile instrumentation and auxiliary files.
   Tree-based version.  See profile.c for overview.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "rtl.h"
#include "flags.h"
#include "output.h"
#include "regs.h"
#include "expr.h"
#include "function.h"
#include "toplev.h"
#include "coverage.h"
#include "tree.h"
#include "tree-flow.h"
#include "tree-dump.h"
#include "tree-pass.h"
#include "timevar.h"
#include "value-prof.h"
#include "ggc.h"

static GTY(()) tree gcov_type_node;
static GTY(()) tree tree_interval_profiler_fn;
static GTY(()) tree tree_pow2_profiler_fn;
static GTY(()) tree tree_one_value_profiler_fn;


/* Do initialization work for the edge profiler.  */

static void
tree_init_edge_profiler (void)
{
  tree interval_profiler_fn_type;
  tree pow2_profiler_fn_type;
  tree one_value_profiler_fn_type;
  tree gcov_type_ptr;

  if (!gcov_type_node)
    {
      gcov_type_node = get_gcov_type ();
      gcov_type_ptr = build_pointer_type (gcov_type_node);

      /* void (*) (gcov_type *, gcov_type, int, unsigned)  */
      interval_profiler_fn_type
	      = build_function_type_list (void_type_node,
					  gcov_type_ptr, gcov_type_node,
					  integer_type_node,
					  unsigned_type_node, NULL_TREE);
      tree_interval_profiler_fn
	      = build_fn_decl ("__gcov_interval_profiler",
				     interval_profiler_fn_type);

      /* void (*) (gcov_type *, gcov_type)  */
      pow2_profiler_fn_type
	      = build_function_type_list (void_type_node,
					  gcov_type_ptr, gcov_type_node,
					  NULL_TREE);
      tree_pow2_profiler_fn = build_fn_decl ("__gcov_pow2_profiler",
						   pow2_profiler_fn_type);

      /* void (*) (gcov_type *, gcov_type)  */
      one_value_profiler_fn_type
	      = build_function_type_list (void_type_node,
					  gcov_type_ptr, gcov_type_node,
					  NULL_TREE);
      tree_one_value_profiler_fn
	      = build_fn_decl ("__gcov_one_value_profiler",
				     one_value_profiler_fn_type);
    }
}

/* Output instructions as GIMPLE trees to increment the edge 
   execution count, and insert them on E.  We rely on 
   bsi_insert_on_edge to preserve the order.  */

static void
tree_gen_edge_profiler (int edgeno, edge e)
{
  tree tmp1 = create_tmp_var (gcov_type_node, "PROF");
  tree tmp2 = create_tmp_var (gcov_type_node, "PROF");
  tree ref = tree_coverage_counter_ref (GCOV_COUNTER_ARCS, edgeno);
  tree stmt1 = build2 (MODIFY_EXPR, gcov_type_node, tmp1, ref);
  tree stmt2 = build2 (MODIFY_EXPR, gcov_type_node, tmp2,
		       build2 (PLUS_EXPR, gcov_type_node, 
			      tmp1, integer_one_node));
  tree stmt3 = build2 (MODIFY_EXPR, gcov_type_node, ref, tmp2);
  bsi_insert_on_edge (e, stmt1);
  bsi_insert_on_edge (e, stmt2);
  bsi_insert_on_edge (e, stmt3);
}

/* Emits code to get VALUE to instrument at BSI, and returns the
   variable containing the value.  */

static tree
prepare_instrumented_value (block_stmt_iterator *bsi,
			    histogram_value value)
{
  tree val = value->hvalue.value;
  return force_gimple_operand_bsi (bsi, fold_convert (gcov_type_node, val),
				   true, NULL_TREE);
}

/* Output instructions as GIMPLE trees to increment the interval histogram 
   counter.  VALUE is the expression whose value is profiled.  TAG is the 
   tag of the section for counters, BASE is offset of the counter position.  */

static void
tree_gen_interval_profiler (histogram_value value, unsigned tag, unsigned base)
{
  tree stmt = value->hvalue.stmt;
  block_stmt_iterator bsi = bsi_for_stmt (stmt);
  tree ref = tree_coverage_counter_ref (tag, base), ref_ptr;
  tree args, call, val;
  tree start = build_int_cst_type (integer_type_node, value->hdata.intvl.int_start);
  tree steps = build_int_cst_type (unsigned_type_node, value->hdata.intvl.steps);
  
  ref_ptr = force_gimple_operand_bsi (&bsi,
				      build_addr (ref, current_function_decl),
				      true, NULL_TREE);
  val = prepare_instrumented_value (&bsi, value);
  args = tree_cons (NULL_TREE, ref_ptr,
		    tree_cons (NULL_TREE, val,
			       tree_cons (NULL_TREE, start,
					  tree_cons (NULL_TREE, steps,
						     NULL_TREE))));
  call = build_function_call_expr (tree_interval_profiler_fn, args);
  bsi_insert_before (&bsi, call, BSI_SAME_STMT);
}

/* Output instructions as GIMPLE trees to increment the power of two histogram 
   counter.  VALUE is the expression whose value is profiled.  TAG is the tag 
   of the section for counters, BASE is offset of the counter position.  */

static void
tree_gen_pow2_profiler (histogram_value value, unsigned tag, unsigned base)
{
  tree stmt = value->hvalue.stmt;
  block_stmt_iterator bsi = bsi_for_stmt (stmt);
  tree ref = tree_coverage_counter_ref (tag, base), ref_ptr;
  tree args, call, val;
  
  ref_ptr = force_gimple_operand_bsi (&bsi,
				      build_addr (ref, current_function_decl),
				      true, NULL_TREE);
  val = prepare_instrumented_value (&bsi, value);
  args = tree_cons (NULL_TREE, ref_ptr,
		    tree_cons (NULL_TREE, val,
			       NULL_TREE));
  call = build_function_call_expr (tree_pow2_profiler_fn, args);
  bsi_insert_before (&bsi, call, BSI_SAME_STMT);
}

/* Output instructions as GIMPLE trees for code to find the most common value.
   VALUE is the expression whose value is profiled.  TAG is the tag of the
   section for counters, BASE is offset of the counter position.  */

static void
tree_gen_one_value_profiler (histogram_value value, unsigned tag, unsigned base)
{
  tree stmt = value->hvalue.stmt;
  block_stmt_iterator bsi = bsi_for_stmt (stmt);
  tree ref = tree_coverage_counter_ref (tag, base), ref_ptr;
  tree args, call, val;
  
  ref_ptr = force_gimple_operand_bsi (&bsi,
				      build_addr (ref, current_function_decl),
				      true, NULL_TREE);
  val = prepare_instrumented_value (&bsi, value);
  args = tree_cons (NULL_TREE, ref_ptr,
		    tree_cons (NULL_TREE, val,
			       NULL_TREE));
  call = build_function_call_expr (tree_one_value_profiler_fn, args);
  bsi_insert_before (&bsi, call, BSI_SAME_STMT);
}

/* Output instructions as GIMPLE trees for code to find the most common value 
   of a difference between two evaluations of an expression.
   VALUE is the expression whose value is profiled.  TAG is the tag of the
   section for counters, BASE is offset of the counter position.  */

static void
tree_gen_const_delta_profiler (histogram_value value ATTRIBUTE_UNUSED, 
				unsigned tag ATTRIBUTE_UNUSED,
				unsigned base ATTRIBUTE_UNUSED)
{
  /* FIXME implement this.  */
#ifdef ENABLE_CHECKING
  internal_error ("unimplemented functionality");
#endif
  gcc_unreachable ();
}

/* Return 1 if tree-based profiling is in effect, else 0.
   If it is, set up hooks for tree-based profiling.
   Gate for pass_tree_profile.  */

static bool
do_tree_profiling (void)
{
  if (profile_arc_flag || flag_test_coverage || flag_branch_probabilities)
    {
      tree_register_profile_hooks ();
      tree_register_value_prof_hooks ();
      return true;
    }
  return false;
}

static unsigned int
tree_profiling (void)
{
  branch_prob ();
  if (flag_branch_probabilities
      && flag_profile_values
      && flag_value_profile_transformations)
    value_profile_transformations ();
  /* The above could hose dominator info.  Currently there is
     none coming in, this is a safety valve.  It should be
     easy to adjust it, if and when there is some.  */
  free_dominance_info (CDI_DOMINATORS);
  free_dominance_info (CDI_POST_DOMINATORS);
  return 0;
}

struct tree_opt_pass pass_tree_profile = 
{
  "tree_profile",			/* name */
  do_tree_profiling,			/* gate */
  tree_profiling,			/* execute */
  NULL,					/* sub */
  NULL,					/* next */
  0,					/* static_pass_number */
  TV_BRANCH_PROB,			/* tv_id */
  PROP_gimple_leh | PROP_cfg,		/* properties_required */
  PROP_gimple_leh | PROP_cfg,		/* properties_provided */
  0,					/* properties_destroyed */
  0,					/* todo_flags_start */
  TODO_verify_stmts,			/* todo_flags_finish */
  0					/* letter */
};

/* Return 1 if tree-based profiling is in effect, else 0.
   If it is, set up hooks for tree-based profiling.
   Gate for pass_tree_profile.  */

static bool
do_early_tree_profiling (void)
{
  return (do_tree_profiling () && (!flag_unit_at_a_time || !optimize));
}

struct tree_opt_pass pass_early_tree_profile = 
{
  "early_tree_profile",			/* name */
  do_early_tree_profiling,		/* gate */
  tree_profiling,			/* execute */
  NULL,					/* sub */
  NULL,					/* next */
  0,					/* static_pass_number */
  TV_BRANCH_PROB,			/* tv_id */
  PROP_gimple_leh | PROP_cfg,		/* properties_required */
  PROP_gimple_leh | PROP_cfg,		/* properties_provided */
  0,					/* properties_destroyed */
  0,					/* todo_flags_start */
  TODO_verify_stmts,			/* todo_flags_finish */
  0					/* letter */
};

struct profile_hooks tree_profile_hooks =
{
  tree_init_edge_profiler,      /* init_edge_profiler */
  tree_gen_edge_profiler,	/* gen_edge_profiler */
  tree_gen_interval_profiler,   /* gen_interval_profiler */
  tree_gen_pow2_profiler,       /* gen_pow2_profiler */
  tree_gen_one_value_profiler,  /* gen_one_value_profiler */
  tree_gen_const_delta_profiler /* gen_const_delta_profiler */
};

#include "gt-tree-profile.h"
