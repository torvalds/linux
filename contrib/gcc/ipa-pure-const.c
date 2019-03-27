/* Callgraph based analysis of static variables.
   Copyright (C) 2004, 2005 Free Software Foundation, Inc.
   Contributed by Kenneth Zadeck <zadeck@naturalbridge.com>

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

/* This file mark functions as being either const (TREE_READONLY) or
   pure (DECL_IS_PURE).

   This must be run after inlining decisions have been made since
   otherwise, the local sets will not contain information that is
   consistent with post inlined state.  The global sets are not prone
   to this problem since they are by definition transitive.  */

/* The code in this module is called by the ipa pass manager. It
   should be one of the later passes since it's information is used by
   the rest of the compilation. */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "tree.h"
#include "tree-flow.h"
#include "tree-inline.h"
#include "tree-pass.h"
#include "langhooks.h"
#include "pointer-set.h"
#include "ggc.h"
#include "ipa-utils.h"
#include "c-common.h"
#include "tree-gimple.h"
#include "cgraph.h"
#include "output.h"
#include "flags.h"
#include "timevar.h"
#include "diagnostic.h"
#include "langhooks.h"
#include "target.h"

static struct pointer_set_t *visited_nodes;

/* Lattice values for const and pure functions.  Everything starts out
   being const, then may drop to pure and then neither depending on
   what is found.  */
enum pure_const_state_e
{
  IPA_CONST,
  IPA_PURE,
  IPA_NEITHER
};

/* Holder inserted into the ipa_dfs_info aux field to hold the
   const_state.  */
struct funct_state_d 
{
  enum pure_const_state_e pure_const_state;
  bool state_set_in_source;
};

typedef struct funct_state_d * funct_state;

/* Return the function state from NODE.  */ 

static inline funct_state
get_function_state (struct cgraph_node *node)
{
  struct ipa_dfs_info * info = node->aux;
  return info->aux;
}

/* Check to see if the use (or definition when CHECHING_WRITE is true) 
   variable T is legal in a function that is either pure or const.  */

static inline void 
check_decl (funct_state local, 
	    tree t, bool checking_write)
{
  /* If the variable has the "used" attribute, treat it as if it had a
     been touched by the devil.  */
  if (lookup_attribute ("used", DECL_ATTRIBUTES (t)))
    {
      local->pure_const_state = IPA_NEITHER;
      return;
    }

  /* Do not want to do anything with volatile except mark any
     function that uses one to be not const or pure.  */
  if (TREE_THIS_VOLATILE (t)) 
    { 
      local->pure_const_state = IPA_NEITHER;
      return;
    }

  /* Do not care about a local automatic that is not static.  */
  if (!TREE_STATIC (t) && !DECL_EXTERNAL (t))
    return;

  /* Since we have dealt with the locals and params cases above, if we
     are CHECKING_WRITE, this cannot be a pure or constant
     function.  */
  if (checking_write) 
    local->pure_const_state = IPA_NEITHER;

  if (DECL_EXTERNAL (t) || TREE_PUBLIC (t))
    {
      /* If the front end set the variable to be READONLY and
	 constant, we can allow this variable in pure or const
	 functions but the scope is too large for our analysis to set
	 these bits ourselves.  */
      
      if (TREE_READONLY (t)
	  && DECL_INITIAL (t)
	  && is_gimple_min_invariant (DECL_INITIAL (t)))
	; /* Read of a constant, do not change the function state.  */
      else 
	{
	  /* Just a regular read.  */
	  if (local->pure_const_state == IPA_CONST)
	    local->pure_const_state = IPA_PURE;
	}
    }
  
  /* Compilation level statics can be read if they are readonly
     variables.  */
  if (TREE_READONLY (t))
    return;

  /* Just a regular read.  */
  if (local->pure_const_state == IPA_CONST)
    local->pure_const_state = IPA_PURE;
}

/* If T is a VAR_DECL check to see if it is an allowed reference.  */

static void
check_operand (funct_state local, 
	       tree t, bool checking_write)
{
  if (!t) return;

  if (TREE_CODE (t) == VAR_DECL)
    check_decl (local, t, checking_write); 
}

/* Examine tree T for references.  */

static void
check_tree (funct_state local, tree t, bool checking_write)
{
  if ((TREE_CODE (t) == EXC_PTR_EXPR) || (TREE_CODE (t) == FILTER_EXPR))
    return;

  /* Any tree which is volatile disqualifies thie function from being
     const or pure. */
  if (TREE_THIS_VOLATILE (t))
    {
      local->pure_const_state = IPA_NEITHER;
      return;
    }

  while (TREE_CODE (t) == REALPART_EXPR 
	 || TREE_CODE (t) == IMAGPART_EXPR
	 || handled_component_p (t))
    {
      if (TREE_CODE (t) == ARRAY_REF)
	check_operand (local, TREE_OPERAND (t, 1), false);
      t = TREE_OPERAND (t, 0);
    }

  /* The bottom of an indirect reference can only be read, not
     written.  */
  if (INDIRECT_REF_P (t))
    {
      check_tree (local, TREE_OPERAND (t, 0), false);
      
      /* Any indirect reference that occurs on the lhs
	 disqualifies the function from being pure or const. Any
	 indirect reference that occurs on the rhs disqualifies the
	 function from being const.  */
      if (checking_write) 
	{
	  local->pure_const_state = IPA_NEITHER;
	  return;
	}
      else if (local->pure_const_state == IPA_CONST)
	local->pure_const_state = IPA_PURE;
    }

  if (SSA_VAR_P (t))
    check_operand (local, t, checking_write);
}

/* Scan tree T to see if there are any addresses taken in within T.  */

static void 
look_for_address_of (funct_state local, tree t)
{
  if (TREE_CODE (t) == ADDR_EXPR)
    {
      tree x = get_base_var (t);
      if (TREE_CODE (x) == VAR_DECL) 
	{
	  check_decl (local, x, false);
	  
	  /* Taking the address of something appears to be reasonable
	     in PURE code.  Not allowed in const.  */
	  if (local->pure_const_state == IPA_CONST)
	    local->pure_const_state = IPA_PURE;
	}
    }
}

/* Check to see if T is a read or address of operation on a var we are
   interested in analyzing.  LOCAL is passed in to get access to its
   bit vectors.  */

static void
check_rhs_var (funct_state local, tree t)
{
  look_for_address_of (local, t);

  /* Memcmp and strlen can both trap and they are declared pure.  */
  if (tree_could_trap_p (t)
      && local->pure_const_state == IPA_CONST)
    local->pure_const_state = IPA_PURE;

  check_tree(local, t, false);
}

/* Check to see if T is an assignment to a var we are interested in
   analyzing.  LOCAL is passed in to get access to its bit vectors. */

static void
check_lhs_var (funct_state local, tree t)
{
  /* Memcmp and strlen can both trap and they are declared pure.
     Which seems to imply that we can apply the same rule here.  */
  if (tree_could_trap_p (t)
      && local->pure_const_state == IPA_CONST)
    local->pure_const_state = IPA_PURE;
    
  check_tree(local, t, true);
}

/* This is a scaled down version of get_asm_expr_operands from
   tree_ssa_operands.c.  The version there runs much later and assumes
   that aliasing information is already available. Here we are just
   trying to find if the set of inputs and outputs contain references
   or address of operations to local static variables.  STMT is the
   actual asm statement.  */

static void
get_asm_expr_operands (funct_state local, tree stmt)
{
  int noutputs = list_length (ASM_OUTPUTS (stmt));
  const char **oconstraints
    = (const char **) alloca ((noutputs) * sizeof (const char *));
  int i;
  tree link;
  const char *constraint;
  bool allows_mem, allows_reg, is_inout;
  
  for (i=0, link = ASM_OUTPUTS (stmt); link; ++i, link = TREE_CHAIN (link))
    {
      oconstraints[i] = constraint
	= TREE_STRING_POINTER (TREE_VALUE (TREE_PURPOSE (link)));
      parse_output_constraint (&constraint, i, 0, 0,
			       &allows_mem, &allows_reg, &is_inout);
      
      check_lhs_var (local, TREE_VALUE (link));
    }

  for (link = ASM_INPUTS (stmt); link; link = TREE_CHAIN (link))
    {
      constraint
	= TREE_STRING_POINTER (TREE_VALUE (TREE_PURPOSE (link)));
      parse_input_constraint (&constraint, 0, 0, noutputs, 0,
			      oconstraints, &allows_mem, &allows_reg);
      
      check_rhs_var (local, TREE_VALUE (link));
    }
  
  for (link = ASM_CLOBBERS (stmt); link; link = TREE_CHAIN (link))
    if (simple_cst_equal(TREE_VALUE (link), memory_identifier_string) == 1) 
      /* Abandon all hope, ye who enter here. */
      local->pure_const_state = IPA_NEITHER;

  if (ASM_VOLATILE_P (stmt))
    local->pure_const_state = IPA_NEITHER;
}

/* Check the parameters of a function call to CALL_EXPR to see if
   there are any references in the parameters that are not allowed for
   pure or const functions.  Also check to see if this is either an
   indirect call, a call outside the compilation unit, or has special
   attributes that may also effect the purity.  The CALL_EXPR node for
   the entire call expression.  */

static void
check_call (funct_state local, tree call_expr) 
{
  int flags = call_expr_flags(call_expr);
  tree operand_list = TREE_OPERAND (call_expr, 1);
  tree operand;
  tree callee_t = get_callee_fndecl (call_expr);
  struct cgraph_node* callee;
  enum availability avail = AVAIL_NOT_AVAILABLE;

  for (operand = operand_list;
       operand != NULL_TREE;
       operand = TREE_CHAIN (operand))
    {
      tree argument = TREE_VALUE (operand);
      check_rhs_var (local, argument);
    }
  
  /* The const and pure flags are set by a variety of places in the
     compiler (including here).  If someone has already set the flags
     for the callee, (such as for some of the builtins) we will use
     them, otherwise we will compute our own information. 
  
     Const and pure functions have less clobber effects than other
     functions so we process these first.  Otherwise if it is a call
     outside the compilation unit or an indirect call we punt.  This
     leaves local calls which will be processed by following the call
     graph.  */  
  if (callee_t)
    {
      callee = cgraph_node(callee_t);
      avail = cgraph_function_body_availability (callee);

      /* When bad things happen to bad functions, they cannot be const
	 or pure.  */
      if (setjmp_call_p (callee_t))
	local->pure_const_state = IPA_NEITHER;

      if (DECL_BUILT_IN_CLASS (callee_t) == BUILT_IN_NORMAL)
	switch (DECL_FUNCTION_CODE (callee_t))
	  {
	  case BUILT_IN_LONGJMP:
	  case BUILT_IN_NONLOCAL_GOTO:
	    local->pure_const_state = IPA_NEITHER;
	    break;
	  default:
	    break;
	  }
    }

  /* The callee is either unknown (indirect call) or there is just no
     scannable code for it (external call) .  We look to see if there
     are any bits available for the callee (such as by declaration or
     because it is builtin) and process solely on the basis of those
     bits. */
  if (avail == AVAIL_NOT_AVAILABLE || avail == AVAIL_OVERWRITABLE)
    {
      if (flags & ECF_PURE) 
	{
	  if (local->pure_const_state == IPA_CONST)
	    local->pure_const_state = IPA_PURE;
	}
      else 
	local->pure_const_state = IPA_NEITHER;
    }
  else
    {
      /* We have the code and we will scan it for the effects. */
      if (flags & ECF_PURE) 
	{
	  if (local->pure_const_state == IPA_CONST)
	    local->pure_const_state = IPA_PURE;
	}
    }
}

/* TP is the part of the tree currently under the microscope.
   WALK_SUBTREES is part of the walk_tree api but is unused here.
   DATA is cgraph_node of the function being walked.  */

/* FIXME: When this is converted to run over SSA form, this code
   should be converted to use the operand scanner.  */

static tree
scan_function (tree *tp, 
		      int *walk_subtrees, 
		      void *data)
{
  struct cgraph_node *fn = data;
  tree t = *tp;
  funct_state local = get_function_state (fn);

  switch (TREE_CODE (t))  
    {
    case VAR_DECL:
      if (DECL_INITIAL (t))
	walk_tree (&DECL_INITIAL (t), scan_function, fn, visited_nodes);
      *walk_subtrees = 0;
      break;

    case MODIFY_EXPR:
      {
	/* First look on the lhs and see what variable is stored to */
	tree lhs = TREE_OPERAND (t, 0);
	tree rhs = TREE_OPERAND (t, 1);
	check_lhs_var (local, lhs);

	/* For the purposes of figuring out what the cast affects */

	/* Next check the operands on the rhs to see if they are ok. */
	switch (TREE_CODE_CLASS (TREE_CODE (rhs))) 
	  {
	  case tcc_binary:	    
 	    {
 	      tree op0 = TREE_OPERAND (rhs, 0);
 	      tree op1 = TREE_OPERAND (rhs, 1);
 	      check_rhs_var (local, op0);
 	      check_rhs_var (local, op1);
	    }
	    break;
	  case tcc_unary:
 	    {
 	      tree op0 = TREE_OPERAND (rhs, 0);
 	      check_rhs_var (local, op0);
 	    }

	    break;
	  case tcc_reference:
	    check_rhs_var (local, rhs);
	    break;
	  case tcc_declaration:
	    check_rhs_var (local, rhs);
	    break;
	  case tcc_expression:
	    switch (TREE_CODE (rhs)) 
	      {
	      case ADDR_EXPR:
		check_rhs_var (local, rhs);
		break;
	      case CALL_EXPR: 
		check_call (local, rhs);
		break;
	      default:
		break;
	      }
	    break;
	  default:
	    break;
	  }
	*walk_subtrees = 0;
      }
      break;

    case ADDR_EXPR:
      /* This case is here to find addresses on rhs of constructors in
	 decl_initial of static variables. */
      check_rhs_var (local, t);
      *walk_subtrees = 0;
      break;

    case LABEL_EXPR:
      if (DECL_NONLOCAL (TREE_OPERAND (t, 0)))
	/* Target of long jump. */
	local->pure_const_state = IPA_NEITHER;
      break;

    case CALL_EXPR: 
      check_call (local, t);
      *walk_subtrees = 0;
      break;
      
    case ASM_EXPR:
      get_asm_expr_operands (local, t);
      *walk_subtrees = 0;
      break;
      
    default:
      break;
    }
  return NULL;
}

/* This is the main routine for finding the reference patterns for
   global variables within a function FN.  */

static void
analyze_function (struct cgraph_node *fn)
{
  funct_state l = XCNEW (struct funct_state_d);
  tree decl = fn->decl;
  struct ipa_dfs_info * w_info = fn->aux;

  w_info->aux = l;

  l->pure_const_state = IPA_CONST;
  l->state_set_in_source = false;

  /* If this function does not return normally or does not bind local,
     do not touch this unless it has been marked as const or pure by the
     front end.  */
  if (TREE_THIS_VOLATILE (decl)
      || !targetm.binds_local_p (decl))
    {
      l->pure_const_state = IPA_NEITHER;
      return;
    }

  if (TREE_READONLY (decl))
    {
      l->pure_const_state = IPA_CONST;
      l->state_set_in_source = true;
    }
  if (DECL_IS_PURE (decl))
    {
      l->pure_const_state = IPA_PURE;
      l->state_set_in_source = true;
    }

  if (dump_file)
    {
      fprintf (dump_file, "\n local analysis of %s with initial value = %d\n ", 
	       cgraph_node_name (fn),
	       l->pure_const_state);
    }
  
  if (!l->state_set_in_source)
    {
      struct function *this_cfun = DECL_STRUCT_FUNCTION (decl);
      basic_block this_block;
      
      FOR_EACH_BB_FN (this_block, this_cfun)
	{
	  block_stmt_iterator bsi;
	  for (bsi = bsi_start (this_block); !bsi_end_p (bsi); bsi_next (&bsi))
	    {
	      walk_tree (bsi_stmt_ptr (bsi), scan_function, 
			 fn, visited_nodes);
	      if (l->pure_const_state == IPA_NEITHER) 
		goto end;
	    }
	}

      if (l->pure_const_state != IPA_NEITHER)
	{
	  tree old_decl = current_function_decl;
	  /* Const functions cannot have back edges (an
	     indication of possible infinite loop side
	     effect.  */
	    
	  current_function_decl = fn->decl;

	  /* The C++ front end, has a tendency to some times jerk away
	     a function after it has created it.  This should have
	     been fixed.  */
	  gcc_assert (DECL_STRUCT_FUNCTION (fn->decl));
	  
	  push_cfun (DECL_STRUCT_FUNCTION (fn->decl));
	  
	  if (mark_dfs_back_edges ())
	    l->pure_const_state = IPA_NEITHER;
	  
	  current_function_decl = old_decl;
	  pop_cfun ();
	}
    }

end:
  if (dump_file)
    {
      fprintf (dump_file, "after local analysis of %s with initial value = %d\n ", 
	       cgraph_node_name (fn),
	       l->pure_const_state);
    }
}


/* Produce the global information by preforming a transitive closure
   on the local information that was produced by ipa_analyze_function
   and ipa_analyze_variable.  */

static unsigned int
static_execute (void)
{
  struct cgraph_node *node;
  struct cgraph_node *w;
  struct cgraph_node **order =
    XCNEWVEC (struct cgraph_node *, cgraph_n_nodes);
  int order_pos = order_pos = ipa_utils_reduced_inorder (order, true, false);
  int i;
  struct ipa_dfs_info * w_info;

  if (!memory_identifier_string)
    memory_identifier_string = build_string(7, "memory");

  /* There are some shared nodes, in particular the initializers on
     static declarations.  We do not need to scan them more than once
     since all we would be interested in are the addressof
     operations.  */
  visited_nodes = pointer_set_create ();

  /* Process all of the functions. 

     We do not want to process any of the clones so we check that this
     is a master clone.  However, we do NOT process any
     AVAIL_OVERWRITABLE functions (these are never clones) we cannot
     guarantee that what we learn about the one we see will be true
     for the one that overriders it.
  */
  for (node = cgraph_nodes; node; node = node->next)
    if (node->analyzed && cgraph_is_master_clone (node))
      analyze_function (node);

  pointer_set_destroy (visited_nodes);
  visited_nodes = NULL;
  if (dump_file)
    {
      dump_cgraph (dump_file);
      ipa_utils_print_order(dump_file, "reduced", order, order_pos);
    }

  /* Propagate the local information thru the call graph to produce
     the global information.  All the nodes within a cycle will have
     the same info so we collapse cycles first.  Then we can do the
     propagation in one pass from the leaves to the roots.  */
  for (i = 0; i < order_pos; i++ )
    {
      enum pure_const_state_e pure_const_state = IPA_CONST;
      int count = 0;
      node = order[i];

      /* Find the worst state for any node in the cycle.  */
      w = node;
      while (w)
	{
	  funct_state w_l = get_function_state (w);
	  if (pure_const_state < w_l->pure_const_state)
	    pure_const_state = w_l->pure_const_state;

	  if (pure_const_state == IPA_NEITHER) 
	    break;

	  if (!w_l->state_set_in_source)
	    {
	      struct cgraph_edge *e;
	      count++;

	      /* FIXME!!!  Because of pr33826, we cannot have either
		 immediate or transitive recursive functions marked as
		 pure or const because dce can delete a function that
		 is in reality an infinite loop.  A better solution
		 than just outlawing them is to add another bit the
		 functions to distinguish recursive from non recursive
		 pure and const function.  This would allow the
		 recursive ones to be cse'd but not dce'd.  In this
		 same vein, we could allow functions with loops to
		 also be cse'd but not dce'd.

		 Unfortunately we are late in stage 3, and the fix
		 described above is is not appropriate.  */
	      if (count > 1)
		{
		  pure_const_state = IPA_NEITHER;
		  break;
		}
		    
	      for (e = w->callees; e; e = e->next_callee) 
		{
		  struct cgraph_node *y = e->callee;
		  /* Only look at the master nodes and skip external nodes.  */
		  y = cgraph_master_clone (y);

		  /* Check for immediate recursive functions.  See the
		     FIXME above.  */
		  if (w == y)
		    {
		      pure_const_state = IPA_NEITHER;
		      break;
		    }
		  if (y)
		    {
		      funct_state y_l = get_function_state (y);
		      if (pure_const_state < y_l->pure_const_state)
			pure_const_state = y_l->pure_const_state;
		      if (pure_const_state == IPA_NEITHER) 
			break;
		    }
		}
	    }
	  w_info = w->aux;
	  w = w_info->next_cycle;
	}

      /* Copy back the region's pure_const_state which is shared by
	 all nodes in the region.  */
      w = node;
      while (w)
	{
	  funct_state w_l = get_function_state (w);

	  /* All nodes within a cycle share the same info.  */
	  if (!w_l->state_set_in_source)
	    {
	      w_l->pure_const_state = pure_const_state;
	      switch (pure_const_state)
		{
		case IPA_CONST:
		  TREE_READONLY (w->decl) = 1;
		  if (dump_file)
		    fprintf (dump_file, "Function found to be const: %s\n",  
			     lang_hooks.decl_printable_name(w->decl, 2)); 
		  break;
		  
		case IPA_PURE:
		  DECL_IS_PURE (w->decl) = 1;
		  if (dump_file)
		    fprintf (dump_file, "Function found to be pure: %s\n",  
			     lang_hooks.decl_printable_name(w->decl, 2)); 
		  break;
		  
		default:
		  break;
		}
	    }
	  w_info = w->aux;
	  w = w_info->next_cycle;
	}
    }

  /* Cleanup. */
  for (node = cgraph_nodes; node; node = node->next)
    /* Get rid of the aux information.  */
    if (node->aux)
      {
	w_info = node->aux;
	if (w_info->aux)
	  free (w_info->aux);
	free (node->aux);
	node->aux = NULL;
      }

  free (order);
  return 0;
}

static bool
gate_pure_const (void)
{
  return (flag_unit_at_a_time != 0 && flag_ipa_pure_const 
	  /* Don't bother doing anything if the program has errors.  */
	  && !(errorcount || sorrycount));
}

struct tree_opt_pass pass_ipa_pure_const =
{
  "pure-const",		                /* name */
  gate_pure_const,			/* gate */
  static_execute,			/* execute */
  NULL,					/* sub */
  NULL,					/* next */
  0,					/* static_pass_number */
  TV_IPA_PURE_CONST,		        /* tv_id */
  0,	                                /* properties_required */
  0,					/* properties_provided */
  0,					/* properties_destroyed */
  0,					/* todo_flags_start */
  0,                                    /* todo_flags_finish */
  0					/* letter */
};


