/* Language independent return value optimizations
   Copyright (C) 2004, 2005 Free Software Foundation, Inc.

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

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "tree.h"
#include "rtl.h"
#include "function.h"
#include "basic-block.h"
#include "expr.h"
#include "diagnostic.h"
#include "tree-flow.h"
#include "timevar.h"
#include "tree-dump.h"
#include "tree-pass.h"
#include "langhooks.h"

/* This file implements return value optimizations for functions which
   return aggregate types.

   Basically this pass searches the function for return statements which
   return a local aggregate.  When converted to RTL such statements will
   generate a copy from the local aggregate to final return value destination
   mandated by the target's ABI.

   That copy can often be avoided by directly constructing the return value
   into the final destination mandated by the target's ABI.

   This is basically a generic equivalent to the C++ front-end's 
   Named Return Value optimization.  */

struct nrv_data
{
  /* This is the temporary (a VAR_DECL) which appears in all of
     this function's RETURN_EXPR statements.  */
  tree var;

  /* This is the function's RESULT_DECL.  We will replace all occurrences
     of VAR with RESULT_DECL when we apply this optimization.  */
  tree result;
};

static tree finalize_nrv_r (tree *, int *, void *);

/* Callback for the tree walker.

   If TP refers to a RETURN_EXPR, then set the expression being returned
   to nrv_data->result.

   If TP refers to nrv_data->var, then replace nrv_data->var with
   nrv_data->result.

   If we reach a node where we know all the subtrees are uninteresting,
   then set *WALK_SUBTREES to zero.  */

static tree
finalize_nrv_r (tree *tp, int *walk_subtrees, void *data)
{
  struct nrv_data *dp = (struct nrv_data *)data;

  /* No need to walk into types.  */
  if (TYPE_P (*tp))
    *walk_subtrees = 0;

  /* Otherwise replace all occurrences of VAR with RESULT.  */
  else if (*tp == dp->var)
    *tp = dp->result;

  /* Keep iterating.  */
  return NULL_TREE;
}

/* Main entry point for return value optimizations.

   If this function always returns the same local variable, and that
   local variable is an aggregate type, then replace the variable with
   the function's DECL_RESULT.

   This is the equivalent of the C++ named return value optimization
   applied to optimized trees in a language independent form.  If we
   ever encounter languages which prevent this kind of optimization,
   then we could either have the languages register the optimization or
   we could change the gating function to check the current language.  */
   
static unsigned int
tree_nrv (void)
{
  tree result = DECL_RESULT (current_function_decl);
  tree result_type = TREE_TYPE (result);
  tree found = NULL;
  basic_block bb;
  block_stmt_iterator bsi;
  struct nrv_data data;

  /* If this function does not return an aggregate type in memory, then
     there is nothing to do.  */
  if (!aggregate_value_p (result, current_function_decl))
    return 0;

  /* Look through each block for assignments to the RESULT_DECL.  */
  FOR_EACH_BB (bb)
    {
      for (bsi = bsi_start (bb); !bsi_end_p (bsi); bsi_next (&bsi))
	{
	  tree stmt = bsi_stmt (bsi);
	  tree ret_expr;

	  if (TREE_CODE (stmt) == RETURN_EXPR)
	    {
	      /* In a function with an aggregate return value, the
		 gimplifier has changed all non-empty RETURN_EXPRs to
		 return the RESULT_DECL.  */
	      ret_expr = TREE_OPERAND (stmt, 0);
	      if (ret_expr)
		gcc_assert (ret_expr == result);
	    }
	  else if (TREE_CODE (stmt) == MODIFY_EXPR
		   && TREE_OPERAND (stmt, 0) == result)
	    {
	      ret_expr = TREE_OPERAND (stmt, 1);

	      /* Now verify that this return statement uses the same value
		 as any previously encountered return statement.  */
	      if (found != NULL)
		{
		  /* If we found a return statement using a different variable
		     than previous return statements, then we can not perform
		     NRV optimizations.  */
		  if (found != ret_expr)
		    return 0;
		}
	      else
		found = ret_expr;

	      /* The returned value must be a local automatic variable of the
		 same type and alignment as the function's result.  */
	      if (TREE_CODE (found) != VAR_DECL
		  || TREE_THIS_VOLATILE (found)
		  || DECL_CONTEXT (found) != current_function_decl
		  || TREE_STATIC (found)
		  || TREE_ADDRESSABLE (found)
		  || DECL_ALIGN (found) > DECL_ALIGN (result)
		  || !lang_hooks.types_compatible_p (TREE_TYPE (found), 
						     result_type))
		return 0;
	    }
	  else if (TREE_CODE (stmt) == MODIFY_EXPR)
	    {
	      tree addr = get_base_address (TREE_OPERAND (stmt, 0));
	       /* If there's any MODIFY of component of RESULT, 
		  then bail out.  */
	      if (addr && addr == result)
		return 0;
	    }
	}
    }

  if (!found)
    return 0;

  /* If dumping details, then note once and only the NRV replacement.  */
  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "NRV Replaced: ");
      print_generic_expr (dump_file, found, dump_flags);
      fprintf (dump_file, "  with: ");
      print_generic_expr (dump_file, result, dump_flags);
      fprintf (dump_file, "\n");
    }

  /* At this point we know that all the return statements return the
     same local which has suitable attributes for NRV.   Copy debugging
     information from FOUND to RESULT.  */
  DECL_NAME (result) = DECL_NAME (found);
  DECL_SOURCE_LOCATION (result) = DECL_SOURCE_LOCATION (found);
  DECL_ABSTRACT_ORIGIN (result) = DECL_ABSTRACT_ORIGIN (found);
  TREE_ADDRESSABLE (result) = TREE_ADDRESSABLE (found);

  /* Now walk through the function changing all references to VAR to be
     RESULT.  */
  data.var = found;
  data.result = result;
  FOR_EACH_BB (bb)
    {
      for (bsi = bsi_start (bb); !bsi_end_p (bsi); )
	{
	  tree *tp = bsi_stmt_ptr (bsi);
	  /* If this is a copy from VAR to RESULT, remove it.  */
	  if (TREE_CODE (*tp) == MODIFY_EXPR
	      && TREE_OPERAND (*tp, 0) == result
	      && TREE_OPERAND (*tp, 1) == found)
	    bsi_remove (&bsi, true);
	  else
	    {
	      walk_tree (tp, finalize_nrv_r, &data, 0);
	      bsi_next (&bsi);
	    }
	}
    }

  /* FOUND is no longer used.  Ensure it gets removed.  */
  var_ann (found)->used = 0;
  return 0;
}

struct tree_opt_pass pass_nrv = 
{
  "nrv",				/* name */
  NULL,					/* gate */
  tree_nrv,				/* execute */
  NULL,					/* sub */
  NULL,					/* next */
  0,					/* static_pass_number */
  TV_TREE_NRV,				/* tv_id */
  PROP_cfg,				/* properties_required */
  0,					/* properties_provided */
  0,					/* properties_destroyed */
  0,					/* todo_flags_start */
  TODO_dump_func | TODO_ggc_collect,			/* todo_flags_finish */
  0					/* letter */
};

/* Determine (pessimistically) whether DEST is available for NRV
   optimization, where DEST is expected to be the LHS of a modify
   expression where the RHS is a function returning an aggregate.

   We search for a base VAR_DECL and look to see if it, or any of its
   subvars are clobbered.  Note that we could do better, for example, by
   attempting to doing points-to analysis on INDIRECT_REFs.  */

static bool
dest_safe_for_nrv_p (tree dest)
{
  switch (TREE_CODE (dest))
    {
      case VAR_DECL:
	{
	  subvar_t subvar;
	  if (is_call_clobbered (dest))
	    return false;
	  for (subvar = get_subvars_for_var (dest);
	       subvar;
	       subvar = subvar->next)
	    if (is_call_clobbered (subvar->var))
	      return false;
	  return true;
	}
      case ARRAY_REF:
      case COMPONENT_REF:
	return dest_safe_for_nrv_p (TREE_OPERAND (dest, 0));
      default:
	return false;
    }
}

/* Walk through the function looking for MODIFY_EXPRs with calls that
   return in memory on the RHS.  For each of these, determine whether it is
   safe to pass the address of the LHS as the return slot, and mark the
   call appropriately if so.

   The NRV shares the return slot with a local variable in the callee; this
   optimization shares the return slot with the target of the call within
   the caller.  If the NRV is performed (which we can't know in general),
   this optimization is safe if the address of the target has not
   escaped prior to the call.  If it has, modifications to the local
   variable will produce visible changes elsewhere, as in PR c++/19317.  */

static unsigned int
execute_return_slot_opt (void)
{
  basic_block bb;

  FOR_EACH_BB (bb)
    {
      block_stmt_iterator i;
      for (i = bsi_start (bb); !bsi_end_p (i); bsi_next (&i))
	{
	  tree stmt = bsi_stmt (i);
	  tree call;

	  if (TREE_CODE (stmt) == MODIFY_EXPR
	      && (call = TREE_OPERAND (stmt, 1),
		  TREE_CODE (call) == CALL_EXPR)
	      && !CALL_EXPR_RETURN_SLOT_OPT (call)
	      && aggregate_value_p (call, call))
	    /* Check if the location being assigned to is
	       call-clobbered.  */
	    CALL_EXPR_RETURN_SLOT_OPT (call) =
	      dest_safe_for_nrv_p (TREE_OPERAND (stmt, 0)) ? 1 : 0;
	}
    }
  return 0;
}

struct tree_opt_pass pass_return_slot = 
{
  "retslot",				/* name */
  NULL,					/* gate */
  execute_return_slot_opt,		/* execute */
  NULL,					/* sub */
  NULL,					/* next */
  0,					/* static_pass_number */
  0,					/* tv_id */
  PROP_ssa | PROP_alias,		/* properties_required */
  0,					/* properties_provided */
  0,					/* properties_destroyed */
  0,					/* todo_flags_start */
  0,					/* todo_flags_finish */
  0					/* letter */
};
