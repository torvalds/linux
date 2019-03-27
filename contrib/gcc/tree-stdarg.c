/* Pass computing data for optimizing stdarg functions.
   Copyright (C) 2004, 2005 Free Software Foundation, Inc.
   Contributed by Jakub Jelinek <jakub@redhat.com>

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
#include "function.h"
#include "langhooks.h"
#include "diagnostic.h"
#include "target.h"
#include "tree-flow.h"
#include "tree-pass.h"
#include "tree-stdarg.h"

/* A simple pass that attempts to optimize stdarg functions on architectures
   that need to save register arguments to stack on entry to stdarg functions.
   If the function doesn't use any va_start macros, no registers need to
   be saved.  If va_start macros are used, the va_list variables don't escape
   the function, it is only necessary to save registers that will be used
   in va_arg macros.  E.g. if va_arg is only used with integral types
   in the function, floating point registers don't need to be saved, etc.  */


/* Return true if basic block VA_ARG_BB is dominated by VA_START_BB and
   is executed at most as many times as VA_START_BB.  */

static bool
reachable_at_most_once (basic_block va_arg_bb, basic_block va_start_bb)
{
  edge *stack, e;
  edge_iterator ei;
  int sp;
  sbitmap visited;
  bool ret;

  if (va_arg_bb == va_start_bb)
    return true;

  if (! dominated_by_p (CDI_DOMINATORS, va_arg_bb, va_start_bb))
    return false;

  stack = XNEWVEC (edge, n_basic_blocks + 1);
  sp = 0;

  visited = sbitmap_alloc (last_basic_block);
  sbitmap_zero (visited);
  ret = true;

  FOR_EACH_EDGE (e, ei, va_arg_bb->preds)
    stack[sp++] = e;

  while (sp)
    {
      basic_block src;

      --sp;
      e = stack[sp];
      src = e->src;

      if (e->flags & EDGE_COMPLEX)
	{
	  ret = false;
	  break;
	}

      if (src == va_start_bb)
	continue;

      /* va_arg_bb can be executed more times than va_start_bb.  */
      if (src == va_arg_bb)
	{
	  ret = false;
	  break;
	}

      gcc_assert (src != ENTRY_BLOCK_PTR);

      if (! TEST_BIT (visited, src->index))
	{
	  SET_BIT (visited, src->index);
	  FOR_EACH_EDGE (e, ei, src->preds)
	    stack[sp++] = e;
	}
    }

  free (stack);
  sbitmap_free (visited);
  return ret;
}


/* For statement COUNTER = RHS, if RHS is COUNTER + constant,
   return constant, otherwise return (unsigned HOST_WIDE_INT) -1.
   GPR_P is true if this is GPR counter.  */

static unsigned HOST_WIDE_INT
va_list_counter_bump (struct stdarg_info *si, tree counter, tree rhs,
		      bool gpr_p)
{
  tree stmt, lhs, orig_lhs;
  unsigned HOST_WIDE_INT ret = 0, val, counter_val;
  unsigned int max_size;

  if (si->offsets == NULL)
    {
      unsigned int i;

      si->offsets = XNEWVEC (int, num_ssa_names);
      for (i = 0; i < num_ssa_names; ++i)
	si->offsets[i] = -1;
    }

  counter_val = gpr_p ? cfun->va_list_gpr_size : cfun->va_list_fpr_size;
  max_size = gpr_p ? VA_LIST_MAX_GPR_SIZE : VA_LIST_MAX_FPR_SIZE;
  orig_lhs = lhs = rhs;
  while (lhs)
    {
      if (si->offsets[SSA_NAME_VERSION (lhs)] != -1)
	{
	  if (counter_val >= max_size)
	    {
	      ret = max_size;
	      break;
	    }

	  ret -= counter_val - si->offsets[SSA_NAME_VERSION (lhs)];
	  break;
	}

      stmt = SSA_NAME_DEF_STMT (lhs);

      if (TREE_CODE (stmt) != MODIFY_EXPR
	  || TREE_OPERAND (stmt, 0) != lhs)
	return (unsigned HOST_WIDE_INT) -1;

      rhs = TREE_OPERAND (stmt, 1);
      if (TREE_CODE (rhs) == WITH_SIZE_EXPR)
	rhs = TREE_OPERAND (rhs, 0);

      if (TREE_CODE (rhs) == SSA_NAME)
	{
	  lhs = rhs;
	  continue;
	}

      if ((TREE_CODE (rhs) == NOP_EXPR
	   || TREE_CODE (rhs) == CONVERT_EXPR)
	  && TREE_CODE (TREE_OPERAND (rhs, 0)) == SSA_NAME)
	{
	  lhs = TREE_OPERAND (rhs, 0);
	  continue;
	}

      if (TREE_CODE (rhs) == PLUS_EXPR
	  && TREE_CODE (TREE_OPERAND (rhs, 0)) == SSA_NAME
	  && TREE_CODE (TREE_OPERAND (rhs, 1)) == INTEGER_CST
	  && host_integerp (TREE_OPERAND (rhs, 1), 1))
	{
	  ret += tree_low_cst (TREE_OPERAND (rhs, 1), 1);
	  lhs = TREE_OPERAND (rhs, 0);
	  continue;
	}

      if (TREE_CODE (counter) != TREE_CODE (rhs))
	return (unsigned HOST_WIDE_INT) -1;

      if (TREE_CODE (counter) == COMPONENT_REF)
	{
	  if (get_base_address (counter) != get_base_address (rhs)
	      || TREE_CODE (TREE_OPERAND (rhs, 1)) != FIELD_DECL
	      || TREE_OPERAND (counter, 1) != TREE_OPERAND (rhs, 1))
	    return (unsigned HOST_WIDE_INT) -1;
	}
      else if (counter != rhs)
	return (unsigned HOST_WIDE_INT) -1;

      lhs = NULL;
    }

  lhs = orig_lhs;
  val = ret + counter_val;
  while (lhs)
    {
      if (si->offsets[SSA_NAME_VERSION (lhs)] != -1)
	break;

      if (val >= max_size)
	si->offsets[SSA_NAME_VERSION (lhs)] = max_size;
      else
	si->offsets[SSA_NAME_VERSION (lhs)] = val;

      stmt = SSA_NAME_DEF_STMT (lhs);

      rhs = TREE_OPERAND (stmt, 1);
      if (TREE_CODE (rhs) == WITH_SIZE_EXPR)
	rhs = TREE_OPERAND (rhs, 0);

      if (TREE_CODE (rhs) == SSA_NAME)
	{
	  lhs = rhs;
	  continue;
	}

      if ((TREE_CODE (rhs) == NOP_EXPR
	   || TREE_CODE (rhs) == CONVERT_EXPR)
	  && TREE_CODE (TREE_OPERAND (rhs, 0)) == SSA_NAME)
	{
	  lhs = TREE_OPERAND (rhs, 0);
	  continue;
	}

      if (TREE_CODE (rhs) == PLUS_EXPR
	  && TREE_CODE (TREE_OPERAND (rhs, 0)) == SSA_NAME
	  && TREE_CODE (TREE_OPERAND (rhs, 1)) == INTEGER_CST
	  && host_integerp (TREE_OPERAND (rhs, 1), 1))
	{
	  val -= tree_low_cst (TREE_OPERAND (rhs, 1), 1);
	  lhs = TREE_OPERAND (rhs, 0);
	  continue;
	}

      lhs = NULL;
    }

  return ret;
}


/* Called by walk_tree to look for references to va_list variables.  */

static tree
find_va_list_reference (tree *tp, int *walk_subtrees ATTRIBUTE_UNUSED,
			void *data)
{
  bitmap va_list_vars = (bitmap) data;
  tree var = *tp;

  if (TREE_CODE (var) == SSA_NAME)
    var = SSA_NAME_VAR (var);

  if (TREE_CODE (var) == VAR_DECL
      && bitmap_bit_p (va_list_vars, DECL_UID (var)))
    return var;

  return NULL_TREE;
}


/* Helper function of va_list_counter_struct_op.  Compute
   cfun->va_list_{g,f}pr_size.  AP is a va_list GPR/FPR counter,
   if WRITE_P is true, seen in AP = VAR, otherwise seen in VAR = AP
   statement.  GPR_P is true if AP is a GPR counter, false if it is
   a FPR counter.  */

static void
va_list_counter_op (struct stdarg_info *si, tree ap, tree var, bool gpr_p,
		    bool write_p)
{
  unsigned HOST_WIDE_INT increment;

  if (si->compute_sizes < 0)
    {
      si->compute_sizes = 0;
      if (si->va_start_count == 1
	  && reachable_at_most_once (si->bb, si->va_start_bb))
	si->compute_sizes = 1;

      if (dump_file && (dump_flags & TDF_DETAILS))
	fprintf (dump_file,
		 "bb%d will %sbe executed at most once for each va_start "
		 "in bb%d\n", si->bb->index, si->compute_sizes ? "" : "not ",
		 si->va_start_bb->index);
    }

  if (write_p
      && si->compute_sizes
      && (increment = va_list_counter_bump (si, ap, var, gpr_p)) + 1 > 1)
    {
      if (gpr_p && cfun->va_list_gpr_size + increment < VA_LIST_MAX_GPR_SIZE)
	{
	  cfun->va_list_gpr_size += increment;
	  return;
	}

      if (!gpr_p && cfun->va_list_fpr_size + increment < VA_LIST_MAX_FPR_SIZE)
	{
	  cfun->va_list_fpr_size += increment;
	  return;
	}
    }

  if (write_p || !si->compute_sizes)
    {
      if (gpr_p)
	cfun->va_list_gpr_size = VA_LIST_MAX_GPR_SIZE;
      else
	cfun->va_list_fpr_size = VA_LIST_MAX_FPR_SIZE;
    }
}


/* If AP is a va_list GPR/FPR counter, compute cfun->va_list_{g,f}pr_size.
   If WRITE_P is true, AP has been seen in AP = VAR assignment, if WRITE_P
   is false, AP has been seen in VAR = AP assignment.
   Return true if the AP = VAR (resp. VAR = AP) statement is a recognized
   va_arg operation that doesn't cause the va_list variable to escape
   current function.  */

static bool
va_list_counter_struct_op (struct stdarg_info *si, tree ap, tree var,
			   bool write_p)
{
  tree base;

  if (TREE_CODE (ap) != COMPONENT_REF
      || TREE_CODE (TREE_OPERAND (ap, 1)) != FIELD_DECL)
    return false;

  if (TREE_CODE (var) != SSA_NAME
      || bitmap_bit_p (si->va_list_vars, DECL_UID (SSA_NAME_VAR (var))))
    return false;

  base = get_base_address (ap);
  if (TREE_CODE (base) != VAR_DECL
      || !bitmap_bit_p (si->va_list_vars, DECL_UID (base)))
    return false;

  if (TREE_OPERAND (ap, 1) == va_list_gpr_counter_field)
    va_list_counter_op (si, ap, var, true, write_p);
  else if (TREE_OPERAND (ap, 1) == va_list_fpr_counter_field)
    va_list_counter_op (si, ap, var, false, write_p);

  return true;
}


/* Check for TEM = AP.  Return true if found and the caller shouldn't
   search for va_list references in the statement.  */

static bool
va_list_ptr_read (struct stdarg_info *si, tree ap, tree tem)
{
  if (TREE_CODE (ap) != VAR_DECL
      || !bitmap_bit_p (si->va_list_vars, DECL_UID (ap)))
    return false;

  if (TREE_CODE (tem) != SSA_NAME
      || bitmap_bit_p (si->va_list_vars,
		       DECL_UID (SSA_NAME_VAR (tem)))
      || is_global_var (SSA_NAME_VAR (tem)))
    return false;

  if (si->compute_sizes < 0)
    {
      si->compute_sizes = 0;
      if (si->va_start_count == 1
	  && reachable_at_most_once (si->bb, si->va_start_bb))
	si->compute_sizes = 1;

      if (dump_file && (dump_flags & TDF_DETAILS))
	fprintf (dump_file,
		 "bb%d will %sbe executed at most once for each va_start "
		 "in bb%d\n", si->bb->index, si->compute_sizes ? "" : "not ",
		 si->va_start_bb->index);
    }

  /* For void * or char * va_list types, there is just one counter.
     If va_arg is used in a loop, we don't know how many registers need
     saving.  */
  if (! si->compute_sizes)
    return false;

  if (va_list_counter_bump (si, ap, tem, true) == (unsigned HOST_WIDE_INT) -1)
    return false;

  /* Note the temporary, as we need to track whether it doesn't escape
     the current function.  */
  bitmap_set_bit (si->va_list_escape_vars,
		  DECL_UID (SSA_NAME_VAR (tem)));
  return true;
}


/* Check for:
     tem1 = AP;
     TEM2 = tem1 + CST;
     AP = TEM2;
   sequence and update cfun->va_list_gpr_size.  Return true if found.  */

static bool
va_list_ptr_write (struct stdarg_info *si, tree ap, tree tem2)
{
  unsigned HOST_WIDE_INT increment;

  if (TREE_CODE (ap) != VAR_DECL
      || !bitmap_bit_p (si->va_list_vars, DECL_UID (ap)))
    return false;

  if (TREE_CODE (tem2) != SSA_NAME
      || bitmap_bit_p (si->va_list_vars, DECL_UID (SSA_NAME_VAR (tem2))))
    return false;

  if (si->compute_sizes <= 0)
    return false;

  increment = va_list_counter_bump (si, ap, tem2, true);
  if (increment + 1 <= 1)
    return false;

  if (cfun->va_list_gpr_size + increment < VA_LIST_MAX_GPR_SIZE)
    cfun->va_list_gpr_size += increment;
  else
    cfun->va_list_gpr_size = VA_LIST_MAX_GPR_SIZE;

  return true;
}


/* If RHS is X, (some type *) X or X + CST for X a temporary variable
   containing value of some va_list variable plus optionally some constant,
   either set si->va_list_escapes or add LHS to si->va_list_escape_vars,
   depending whether LHS is a function local temporary.  */

static void
check_va_list_escapes (struct stdarg_info *si, tree lhs, tree rhs)
{
  if (! POINTER_TYPE_P (TREE_TYPE (rhs)))
    return;

  if ((TREE_CODE (rhs) == PLUS_EXPR
       && TREE_CODE (TREE_OPERAND (rhs, 1)) == INTEGER_CST)
      || TREE_CODE (rhs) == NOP_EXPR
      || TREE_CODE (rhs) == CONVERT_EXPR)
    rhs = TREE_OPERAND (rhs, 0);

  if (TREE_CODE (rhs) != SSA_NAME
      || ! bitmap_bit_p (si->va_list_escape_vars,
			 DECL_UID (SSA_NAME_VAR (rhs))))
    return;

  if (TREE_CODE (lhs) != SSA_NAME || is_global_var (SSA_NAME_VAR (lhs)))
    {
      si->va_list_escapes = true;
      return;
    }

  if (si->compute_sizes < 0)
    {
      si->compute_sizes = 0;
      if (si->va_start_count == 1
	  && reachable_at_most_once (si->bb, si->va_start_bb))
	si->compute_sizes = 1;

      if (dump_file && (dump_flags & TDF_DETAILS))
	fprintf (dump_file,
		 "bb%d will %sbe executed at most once for each va_start "
		 "in bb%d\n", si->bb->index, si->compute_sizes ? "" : "not ",
		 si->va_start_bb->index);
    }

  /* For void * or char * va_list types, there is just one counter.
     If va_arg is used in a loop, we don't know how many registers need
     saving.  */
  if (! si->compute_sizes)
    {
      si->va_list_escapes = true;
      return;
    }

  if (va_list_counter_bump (si, si->va_start_ap, lhs, true)
      == (unsigned HOST_WIDE_INT) -1)
    {
      si->va_list_escapes = true;
      return;
    }

  bitmap_set_bit (si->va_list_escape_vars,
		  DECL_UID (SSA_NAME_VAR (lhs)));
}


/* Check all uses of temporaries from si->va_list_escape_vars bitmap.
   Return true if va_list might be escaping.  */

static bool
check_all_va_list_escapes (struct stdarg_info *si)
{
  basic_block bb;

  FOR_EACH_BB (bb)
    {
      block_stmt_iterator i;

      for (i = bsi_start (bb); !bsi_end_p (i); bsi_next (&i))
	{
	  tree stmt = bsi_stmt (i), use;
	  ssa_op_iter iter;

	  FOR_EACH_SSA_TREE_OPERAND (use, stmt, iter, SSA_OP_ALL_USES)
	    {
	      if (! bitmap_bit_p (si->va_list_escape_vars,
				  DECL_UID (SSA_NAME_VAR (use))))
		continue;

	      if (TREE_CODE (stmt) == MODIFY_EXPR)
		{
		  tree lhs = TREE_OPERAND (stmt, 0);
		  tree rhs = TREE_OPERAND (stmt, 1);

		  if (TREE_CODE (rhs) == WITH_SIZE_EXPR)
		    rhs = TREE_OPERAND (rhs, 0);

		  /* x = *ap_temp;  */
		  if (TREE_CODE (rhs) == INDIRECT_REF
		      && TREE_OPERAND (rhs, 0) == use
		      && TYPE_SIZE_UNIT (TREE_TYPE (rhs))
		      && host_integerp (TYPE_SIZE_UNIT (TREE_TYPE (rhs)), 1)
		      && si->offsets[SSA_NAME_VERSION (use)] != -1)
		    {
		      unsigned HOST_WIDE_INT gpr_size;
		      tree access_size = TYPE_SIZE_UNIT (TREE_TYPE (rhs));

		      gpr_size = si->offsets[SSA_NAME_VERSION (use)]
				 + tree_low_cst (access_size, 1);
		      if (gpr_size >= VA_LIST_MAX_GPR_SIZE)
			cfun->va_list_gpr_size = VA_LIST_MAX_GPR_SIZE;
		      else if (gpr_size > cfun->va_list_gpr_size)
			cfun->va_list_gpr_size = gpr_size;
		      continue;
		    }

		  /* va_arg sequences may contain
		     other_ap_temp = ap_temp;
		     other_ap_temp = ap_temp + constant;
		     other_ap_temp = (some_type *) ap_temp;
		     ap = ap_temp;
		     statements.  */
		  if ((TREE_CODE (rhs) == PLUS_EXPR
		       && TREE_CODE (TREE_OPERAND (rhs, 1)) == INTEGER_CST)
		      || TREE_CODE (rhs) == NOP_EXPR
		      || TREE_CODE (rhs) == CONVERT_EXPR)
		    rhs = TREE_OPERAND (rhs, 0);

		  if (rhs == use)
		    {
		      if (TREE_CODE (lhs) == SSA_NAME
			  && bitmap_bit_p (si->va_list_escape_vars,
					   DECL_UID (SSA_NAME_VAR (lhs))))
			continue;

		      if (TREE_CODE (lhs) == VAR_DECL
			  && bitmap_bit_p (si->va_list_vars,
					   DECL_UID (lhs)))
			continue;
		    }
		}

	      if (dump_file && (dump_flags & TDF_DETAILS))
		{
		  fputs ("va_list escapes in ", dump_file);
		  print_generic_expr (dump_file, stmt, dump_flags);
		  fputc ('\n', dump_file);
		}
	      return true;
	    }
	}
    }

  return false;
}


/* Return true if this optimization pass should be done.
   It makes only sense for stdarg functions.  */

static bool
gate_optimize_stdarg (void)
{
  /* This optimization is only for stdarg functions.  */
  return current_function_stdarg != 0;
}


/* Entry point to the stdarg optimization pass.  */

static unsigned int
execute_optimize_stdarg (void)
{
  basic_block bb;
  bool va_list_escapes = false;
  bool va_list_simple_ptr;
  struct stdarg_info si;
  const char *funcname = NULL;

  cfun->va_list_gpr_size = 0;
  cfun->va_list_fpr_size = 0;
  memset (&si, 0, sizeof (si));
  si.va_list_vars = BITMAP_ALLOC (NULL);
  si.va_list_escape_vars = BITMAP_ALLOC (NULL);

  if (dump_file)
    funcname = lang_hooks.decl_printable_name (current_function_decl, 2);

  va_list_simple_ptr = POINTER_TYPE_P (va_list_type_node)
		       && (TREE_TYPE (va_list_type_node) == void_type_node
			   || TREE_TYPE (va_list_type_node) == char_type_node);
  gcc_assert (is_gimple_reg_type (va_list_type_node) == va_list_simple_ptr);

  FOR_EACH_BB (bb)
    {
      block_stmt_iterator i;

      for (i = bsi_start (bb); !bsi_end_p (i); bsi_next (&i))
	{
	  tree stmt = bsi_stmt (i);
	  tree call = get_call_expr_in (stmt), callee;
	  tree ap;

	  if (!call)
	    continue;

	  callee = get_callee_fndecl (call);
	  if (!callee
	      || DECL_BUILT_IN_CLASS (callee) != BUILT_IN_NORMAL)
	    continue;

	  switch (DECL_FUNCTION_CODE (callee))
	    {
	    case BUILT_IN_VA_START:
	      break;
	      /* If old style builtins are used, don't optimize anything.  */
	    case BUILT_IN_SAVEREGS:
	    case BUILT_IN_STDARG_START:
	    case BUILT_IN_ARGS_INFO:
	    case BUILT_IN_NEXT_ARG:
	      va_list_escapes = true;
	      continue;
	    default:
	      continue;
	    }

	  si.va_start_count++;
	  ap = TREE_VALUE (TREE_OPERAND (call, 1));

	  if (TREE_CODE (ap) != ADDR_EXPR)
	    {
	      va_list_escapes = true;
	      break;
	    }
	  ap = TREE_OPERAND (ap, 0);
	  if (TREE_CODE (ap) == ARRAY_REF)
	    {
	      if (! integer_zerop (TREE_OPERAND (ap, 1)))
	        {
	          va_list_escapes = true;
	          break;
		}
	      ap = TREE_OPERAND (ap, 0);
	    }
	  if (TYPE_MAIN_VARIANT (TREE_TYPE (ap))
	      != TYPE_MAIN_VARIANT (va_list_type_node)
	      || TREE_CODE (ap) != VAR_DECL)
	    {
	      va_list_escapes = true;
	      break;
	    }

	  if (is_global_var (ap))
	    {
	      va_list_escapes = true;
	      break;
	    }

	  bitmap_set_bit (si.va_list_vars, DECL_UID (ap));

	  /* VA_START_BB and VA_START_AP will be only used if there is just
	     one va_start in the function.  */
	  si.va_start_bb = bb;
	  si.va_start_ap = ap;
	}

      if (va_list_escapes)
	break;
    }

  /* If there were no va_start uses in the function, there is no need to
     save anything.  */
  if (si.va_start_count == 0)
    goto finish;

  /* If some va_list arguments weren't local, we can't optimize.  */
  if (va_list_escapes)
    goto finish;

  /* For void * or char * va_list, something useful can be done only
     if there is just one va_start.  */
  if (va_list_simple_ptr && si.va_start_count > 1)
    {
      va_list_escapes = true;
      goto finish;
    }

  /* For struct * va_list, if the backend didn't tell us what the counter fields
     are, there is nothing more we can do.  */
  if (!va_list_simple_ptr
      && va_list_gpr_counter_field == NULL_TREE
      && va_list_fpr_counter_field == NULL_TREE)
    {
      va_list_escapes = true;
      goto finish;
    }

  /* For void * or char * va_list there is just one counter
     (va_list itself).  Use VA_LIST_GPR_SIZE for it.  */
  if (va_list_simple_ptr)
    cfun->va_list_fpr_size = VA_LIST_MAX_FPR_SIZE;

  calculate_dominance_info (CDI_DOMINATORS);

  FOR_EACH_BB (bb)
    {
      block_stmt_iterator i;

      si.compute_sizes = -1;
      si.bb = bb;

      /* For va_list_simple_ptr, we have to check PHI nodes too.  We treat
	 them as assignments for the purpose of escape analysis.  This is
	 not needed for non-simple va_list because virtual phis don't perform
	 any real data movement.  */
      if (va_list_simple_ptr)
	{
	  tree phi, lhs, rhs;
	  use_operand_p uop;
	  ssa_op_iter soi;

	  for (phi = phi_nodes (bb); phi; phi = PHI_CHAIN (phi))
	    {
	      lhs = PHI_RESULT (phi);

	      if (!is_gimple_reg (lhs))
		continue;

	      FOR_EACH_PHI_ARG (uop, phi, soi, SSA_OP_USE)
		{
		  rhs = USE_FROM_PTR (uop);
		  if (va_list_ptr_read (&si, rhs, lhs))
		    continue;
		  else if (va_list_ptr_write (&si, lhs, rhs))
		    continue;
		  else
		    check_va_list_escapes (&si, lhs, rhs);

		  if (si.va_list_escapes
		      || walk_tree (&phi, find_va_list_reference,
				    si.va_list_vars, NULL))
		    {
		      if (dump_file && (dump_flags & TDF_DETAILS))
			{
			  fputs ("va_list escapes in ", dump_file);
			  print_generic_expr (dump_file, phi, dump_flags);
			  fputc ('\n', dump_file);
			}
		      va_list_escapes = true;
		    }
		}
	    }
	}

      for (i = bsi_start (bb);
	   !bsi_end_p (i) && !va_list_escapes;
	   bsi_next (&i))
	{
	  tree stmt = bsi_stmt (i);
	  tree call;

	  /* Don't look at __builtin_va_{start,end}, they are ok.  */
	  call = get_call_expr_in (stmt);
	  if (call)
	    {
	      tree callee = get_callee_fndecl (call);

	      if (callee
		  && DECL_BUILT_IN_CLASS (callee) == BUILT_IN_NORMAL
		  && (DECL_FUNCTION_CODE (callee) == BUILT_IN_VA_START
		      || DECL_FUNCTION_CODE (callee) == BUILT_IN_VA_END))
		continue;
	    }

	  if (TREE_CODE (stmt) == MODIFY_EXPR)
	    {
	      tree lhs = TREE_OPERAND (stmt, 0);
	      tree rhs = TREE_OPERAND (stmt, 1);

	      if (TREE_CODE (rhs) == WITH_SIZE_EXPR)
		rhs = TREE_OPERAND (rhs, 0);

	      if (va_list_simple_ptr)
		{
		  /* Check for tem = ap.  */
		  if (va_list_ptr_read (&si, rhs, lhs))
		    continue;

		  /* Check for the last insn in:
		     tem1 = ap;
		     tem2 = tem1 + CST;
		     ap = tem2;
		     sequence.  */
		  else if (va_list_ptr_write (&si, lhs, rhs))
		    continue;

		  else
		    check_va_list_escapes (&si, lhs, rhs);
		}
	      else
		{
		  /* Check for ap[0].field = temp.  */
		  if (va_list_counter_struct_op (&si, lhs, rhs, true))
		    continue;

		  /* Check for temp = ap[0].field.  */
		  else if (va_list_counter_struct_op (&si, rhs, lhs, false))
		    continue;

		  /* Do any architecture specific checking.  */
		  else if (targetm.stdarg_optimize_hook
			   && targetm.stdarg_optimize_hook (&si, lhs, rhs))
		    continue;
		}
	    }

	  /* All other uses of va_list are either va_copy (that is not handled
	     in this optimization), taking address of va_list variable or
	     passing va_list to other functions (in that case va_list might
	     escape the function and therefore va_start needs to set it up
	     fully), or some unexpected use of va_list.  None of these should
	     happen in a gimplified VA_ARG_EXPR.  */
	  if (si.va_list_escapes
	      || walk_tree (&stmt, find_va_list_reference,
			    si.va_list_vars, NULL))
	    {
	      if (dump_file && (dump_flags & TDF_DETAILS))
		{
		  fputs ("va_list escapes in ", dump_file);
		  print_generic_expr (dump_file, stmt, dump_flags);
		  fputc ('\n', dump_file);
		}
	      va_list_escapes = true;
	    }
	}

      if (va_list_escapes)
	break;
    }

  if (! va_list_escapes
      && va_list_simple_ptr
      && ! bitmap_empty_p (si.va_list_escape_vars)
      && check_all_va_list_escapes (&si))
    va_list_escapes = true;

finish:
  if (va_list_escapes)
    {
      cfun->va_list_gpr_size = VA_LIST_MAX_GPR_SIZE;
      cfun->va_list_fpr_size = VA_LIST_MAX_FPR_SIZE;
    }
  BITMAP_FREE (si.va_list_vars);
  BITMAP_FREE (si.va_list_escape_vars);
  free (si.offsets);
  if (dump_file)
    {
      fprintf (dump_file, "%s: va_list escapes %d, needs to save ",
	       funcname, (int) va_list_escapes);
      if (cfun->va_list_gpr_size >= VA_LIST_MAX_GPR_SIZE)
	fputs ("all", dump_file);
      else
	fprintf (dump_file, "%d", cfun->va_list_gpr_size);
      fputs (" GPR units and ", dump_file);
      if (cfun->va_list_fpr_size >= VA_LIST_MAX_FPR_SIZE)
	fputs ("all", dump_file);
      else
	fprintf (dump_file, "%d", cfun->va_list_fpr_size);
      fputs (" FPR units.\n", dump_file);
    }
  return 0;
}


struct tree_opt_pass pass_stdarg =
{
  "stdarg",				/* name */
  gate_optimize_stdarg,			/* gate */
  execute_optimize_stdarg,		/* execute */
  NULL,					/* sub */
  NULL,					/* next */
  0,					/* static_pass_number */
  0,					/* tv_id */
  PROP_cfg | PROP_ssa | PROP_alias,	/* properties_required */
  0,					/* properties_provided */
  0,					/* properties_destroyed */
  0,					/* todo_flags_start */
  TODO_dump_func,			/* todo_flags_finish */
  0					/* letter */
};
