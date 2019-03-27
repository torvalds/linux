/* Tree lowering pass.  Lowers GIMPLE into unstructured form.

   Copyright (C) 2003, 2005 Free Software Foundation, Inc.

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
#include "tree.h"
#include "rtl.h"
#include "varray.h"
#include "tree-gimple.h"
#include "tree-inline.h"
#include "diagnostic.h"
#include "langhooks.h"
#include "langhooks-def.h"
#include "tree-flow.h"
#include "timevar.h"
#include "except.h"
#include "hashtab.h"
#include "flags.h"
#include "function.h"
#include "expr.h"
#include "toplev.h"
#include "tree-pass.h"

struct lower_data
{
  /* Block the current statement belongs to.  */
  tree block;

  /* A TREE_LIST of label and return statements to be moved to the end
     of the function.  */
  tree return_statements;

  /* True if the function calls __builtin_setjmp.  */
  bool calls_builtin_setjmp;
};

static void lower_stmt (tree_stmt_iterator *, struct lower_data *);
static void lower_bind_expr (tree_stmt_iterator *, struct lower_data *);
static void lower_cond_expr (tree_stmt_iterator *, struct lower_data *);
static void lower_return_expr (tree_stmt_iterator *, struct lower_data *);
static void lower_builtin_setjmp (tree_stmt_iterator *);

/* Lower the body of current_function_decl.  */

static unsigned int
lower_function_body (void)
{
  struct lower_data data;
  tree *body_p = &DECL_SAVED_TREE (current_function_decl);
  tree bind = *body_p;
  tree_stmt_iterator i;
  tree t, x;

  gcc_assert (TREE_CODE (bind) == BIND_EXPR);

  memset (&data, 0, sizeof (data));
  data.block = DECL_INITIAL (current_function_decl);
  BLOCK_SUBBLOCKS (data.block) = NULL_TREE;
  BLOCK_CHAIN (data.block) = NULL_TREE;
  TREE_ASM_WRITTEN (data.block) = 1;

  *body_p = alloc_stmt_list ();
  i = tsi_start (*body_p);
  tsi_link_after (&i, bind, TSI_NEW_STMT);
  lower_bind_expr (&i, &data);

  i = tsi_last (*body_p);

  /* If the function falls off the end, we need a null return statement.
     If we've already got one in the return_statements list, we don't
     need to do anything special.  Otherwise build one by hand.  */
  if (block_may_fallthru (*body_p)
      && (data.return_statements == NULL
          || TREE_OPERAND (TREE_VALUE (data.return_statements), 0) != NULL))
    {
      x = build1 (RETURN_EXPR, void_type_node, NULL);
      SET_EXPR_LOCATION (x, cfun->function_end_locus);
      tsi_link_after (&i, x, TSI_CONTINUE_LINKING);
    }

  /* If we lowered any return statements, emit the representative
     at the end of the function.  */
  for (t = data.return_statements ; t ; t = TREE_CHAIN (t))
    {
      x = build1 (LABEL_EXPR, void_type_node, TREE_PURPOSE (t));
      tsi_link_after (&i, x, TSI_CONTINUE_LINKING);

      /* Remove the line number from the representative return statement.
	 It now fills in for many such returns.  Failure to remove this
	 will result in incorrect results for coverage analysis.  */
      x = TREE_VALUE (t);
#ifdef USE_MAPPED_LOCATION
      SET_EXPR_LOCATION (x, UNKNOWN_LOCATION);
#else
      SET_EXPR_LOCUS (x, NULL);
#endif
      tsi_link_after (&i, x, TSI_CONTINUE_LINKING);
    }

  /* If the function calls __builtin_setjmp, we need to emit the computed
     goto that will serve as the unique dispatcher for all the receivers.  */
  if (data.calls_builtin_setjmp)
    {
      tree disp_label, disp_var, arg;

      /* Build 'DISP_LABEL:' and insert.  */
      disp_label = create_artificial_label ();
      /* This mark will create forward edges from every call site.  */
      DECL_NONLOCAL (disp_label) = 1;
      current_function_has_nonlocal_label = 1;
      x = build1 (LABEL_EXPR, void_type_node, disp_label);
      tsi_link_after (&i, x, TSI_CONTINUE_LINKING);

      /* Build 'DISP_VAR = __builtin_setjmp_dispatcher (DISP_LABEL);'
	 and insert.  */
      disp_var = create_tmp_var (ptr_type_node, "setjmpvar");
      t = build_addr (disp_label, current_function_decl);
      arg = tree_cons (NULL, t, NULL);
      t = implicit_built_in_decls[BUILT_IN_SETJMP_DISPATCHER];
      t = build_function_call_expr (t,arg);
      x = build2 (MODIFY_EXPR, void_type_node, disp_var, t);

      /* Build 'goto DISP_VAR;' and insert.  */
      tsi_link_after (&i, x, TSI_CONTINUE_LINKING);
      x = build1 (GOTO_EXPR, void_type_node, disp_var);
      tsi_link_after (&i, x, TSI_CONTINUE_LINKING);
    }

  gcc_assert (data.block == DECL_INITIAL (current_function_decl));
  BLOCK_SUBBLOCKS (data.block)
    = blocks_nreverse (BLOCK_SUBBLOCKS (data.block));

  clear_block_marks (data.block);
  return 0;
}

struct tree_opt_pass pass_lower_cf = 
{
  "lower",				/* name */
  NULL,					/* gate */
  lower_function_body,			/* execute */
  NULL,					/* sub */
  NULL,					/* next */
  0,					/* static_pass_number */
  0,					/* tv_id */
  PROP_gimple_any,			/* properties_required */
  PROP_gimple_lcf,			/* properties_provided */
  0,					/* properties_destroyed */
  0,					/* todo_flags_start */
  TODO_dump_func,			/* todo_flags_finish */
  0					/* letter */
};


/* Lower the EXPR.  Unlike gimplification the statements are not relowered
   when they are changed -- if this has to be done, the lowering routine must
   do it explicitly.  DATA is passed through the recursion.  */

static void
lower_stmt_body (tree expr, struct lower_data *data)
{
  tree_stmt_iterator tsi;

  for (tsi = tsi_start (expr); !tsi_end_p (tsi); )
    lower_stmt (&tsi, data);
}


/* Lower the OpenMP directive statement pointed by TSI.  DATA is
   passed through the recursion.  */

static void
lower_omp_directive (tree_stmt_iterator *tsi, struct lower_data *data)
{
  tree stmt;
  
  stmt = tsi_stmt (*tsi);

  lower_stmt_body (OMP_BODY (stmt), data);
  tsi_link_before (tsi, stmt, TSI_SAME_STMT);
  tsi_link_before (tsi, OMP_BODY (stmt), TSI_SAME_STMT);
  OMP_BODY (stmt) = NULL_TREE;
  tsi_delink (tsi);
}


/* Lower statement TSI.  DATA is passed through the recursion.  */

static void
lower_stmt (tree_stmt_iterator *tsi, struct lower_data *data)
{
  tree stmt = tsi_stmt (*tsi);

  if (EXPR_HAS_LOCATION (stmt) && data)
    TREE_BLOCK (stmt) = data->block;

  switch (TREE_CODE (stmt))
    {
    case BIND_EXPR:
      lower_bind_expr (tsi, data);
      return;
    case COND_EXPR:
      lower_cond_expr (tsi, data);
      return;
    case RETURN_EXPR:
      lower_return_expr (tsi, data);
      return;

    case TRY_FINALLY_EXPR:
    case TRY_CATCH_EXPR:
      lower_stmt_body (TREE_OPERAND (stmt, 0), data);
      lower_stmt_body (TREE_OPERAND (stmt, 1), data);
      break;
    case CATCH_EXPR:
      lower_stmt_body (CATCH_BODY (stmt), data);
      break;
    case EH_FILTER_EXPR:
      lower_stmt_body (EH_FILTER_FAILURE (stmt), data);
      break;
      
    case NOP_EXPR:
    case ASM_EXPR:
    case GOTO_EXPR:
    case LABEL_EXPR:
    case SWITCH_EXPR:
    case OMP_FOR:
    case OMP_SECTIONS:
    case OMP_SECTION:
    case OMP_SINGLE:
    case OMP_MASTER:
    case OMP_ORDERED:
    case OMP_CRITICAL:
    case OMP_RETURN:
    case OMP_CONTINUE:
      break;

    case MODIFY_EXPR:
      if (TREE_CODE (TREE_OPERAND (stmt, 1)) == CALL_EXPR)
	stmt = TREE_OPERAND (stmt, 1);
      else
	break;
      /* FALLTHRU */

    case CALL_EXPR:
      {
	tree decl = get_callee_fndecl (stmt);
	if (decl
	    && DECL_BUILT_IN_CLASS (decl) == BUILT_IN_NORMAL
	    && DECL_FUNCTION_CODE (decl) == BUILT_IN_SETJMP)
	  {
	    data->calls_builtin_setjmp = true;
	    lower_builtin_setjmp (tsi);
	    return;
	  }
      }
      break;

    case OMP_PARALLEL:
      lower_omp_directive (tsi, data);
      return;

    default:
      gcc_unreachable ();
    }

  tsi_next (tsi);
}

/* Lower a bind_expr TSI.  DATA is passed through the recursion.  */

static void
lower_bind_expr (tree_stmt_iterator *tsi, struct lower_data *data)
{
  tree old_block = data->block;
  tree stmt = tsi_stmt (*tsi);
  tree new_block = BIND_EXPR_BLOCK (stmt);

  if (new_block)
    {
      if (new_block == old_block)
	{
	  /* The outermost block of the original function may not be the
	     outermost statement chain of the gimplified function.  So we
	     may see the outermost block just inside the function.  */
	  gcc_assert (new_block == DECL_INITIAL (current_function_decl));
	  new_block = NULL;
	}
      else
	{
	  /* We do not expect to handle duplicate blocks.  */
	  gcc_assert (!TREE_ASM_WRITTEN (new_block));
	  TREE_ASM_WRITTEN (new_block) = 1;

	  /* Block tree may get clobbered by inlining.  Normally this would
	     be fixed in rest_of_decl_compilation using block notes, but
	     since we are not going to emit them, it is up to us.  */
	  BLOCK_CHAIN (new_block) = BLOCK_SUBBLOCKS (old_block);
	  BLOCK_SUBBLOCKS (old_block) = new_block;
	  BLOCK_SUBBLOCKS (new_block) = NULL_TREE;
	  BLOCK_SUPERCONTEXT (new_block) = old_block;

	  data->block = new_block;
	}
    }

  record_vars (BIND_EXPR_VARS (stmt));
  lower_stmt_body (BIND_EXPR_BODY (stmt), data);

  if (new_block)
    {
      gcc_assert (data->block == new_block);

      BLOCK_SUBBLOCKS (new_block)
	= blocks_nreverse (BLOCK_SUBBLOCKS (new_block));
      data->block = old_block;
    }

  /* The BIND_EXPR no longer carries any useful information -- kill it.  */
  tsi_link_before (tsi, BIND_EXPR_BODY (stmt), TSI_SAME_STMT);
  tsi_delink (tsi);
}

/* Try to determine whether a TRY_CATCH expression can fall through.
   This is a subroutine of block_may_fallthru.  */

static bool
try_catch_may_fallthru (tree stmt)
{
  tree_stmt_iterator i;

  /* If the TRY block can fall through, the whole TRY_CATCH can
     fall through.  */
  if (block_may_fallthru (TREE_OPERAND (stmt, 0)))
    return true;

  i = tsi_start (TREE_OPERAND (stmt, 1));
  switch (TREE_CODE (tsi_stmt (i)))
    {
    case CATCH_EXPR:
      /* We expect to see a sequence of CATCH_EXPR trees, each with a
	 catch expression and a body.  The whole TRY_CATCH may fall
	 through iff any of the catch bodies falls through.  */
      for (; !tsi_end_p (i); tsi_next (&i))
	{
	  if (block_may_fallthru (CATCH_BODY (tsi_stmt (i))))
	    return true;
	}
      return false;

    case EH_FILTER_EXPR:
      /* The exception filter expression only matters if there is an
	 exception.  If the exception does not match EH_FILTER_TYPES,
	 we will execute EH_FILTER_FAILURE, and we will fall through
	 if that falls through.  If the exception does match
	 EH_FILTER_TYPES, the stack unwinder will continue up the
	 stack, so we will not fall through.  We don't know whether we
	 will throw an exception which matches EH_FILTER_TYPES or not,
	 so we just ignore EH_FILTER_TYPES and assume that we might
	 throw an exception which doesn't match.  */
      return block_may_fallthru (EH_FILTER_FAILURE (tsi_stmt (i)));

    default:
      /* This case represents statements to be executed when an
	 exception occurs.  Those statements are implicitly followed
	 by a RESX_EXPR to resume execution after the exception.  So
	 in this case the TRY_CATCH never falls through.  */
      return false;
    }
}

/* Try to determine if we can fall out of the bottom of BLOCK.  This guess
   need not be 100% accurate; simply be conservative and return true if we
   don't know.  This is used only to avoid stupidly generating extra code.
   If we're wrong, we'll just delete the extra code later.  */

bool
block_may_fallthru (tree block)
{
  tree stmt = expr_last (block);

  switch (stmt ? TREE_CODE (stmt) : ERROR_MARK)
    {
    case GOTO_EXPR:
    case RETURN_EXPR:
    case RESX_EXPR:
      /* Easy cases.  If the last statement of the block implies 
	 control transfer, then we can't fall through.  */
      return false;

    case SWITCH_EXPR:
      /* If SWITCH_LABELS is set, this is lowered, and represents a
	 branch to a selected label and hence can not fall through.
	 Otherwise SWITCH_BODY is set, and the switch can fall
	 through.  */
      return SWITCH_LABELS (stmt) == NULL_TREE;

    case COND_EXPR:
      if (block_may_fallthru (COND_EXPR_THEN (stmt)))
	return true;
      return block_may_fallthru (COND_EXPR_ELSE (stmt));

    case BIND_EXPR:
      return block_may_fallthru (BIND_EXPR_BODY (stmt));

    case TRY_CATCH_EXPR:
      return try_catch_may_fallthru (stmt);

    case TRY_FINALLY_EXPR:
      /* The finally clause is always executed after the try clause,
	 so if it does not fall through, then the try-finally will not
	 fall through.  Otherwise, if the try clause does not fall
	 through, then when the finally clause falls through it will
	 resume execution wherever the try clause was going.  So the
	 whole try-finally will only fall through if both the try
	 clause and the finally clause fall through.  */
      return (block_may_fallthru (TREE_OPERAND (stmt, 0))
	      && block_may_fallthru (TREE_OPERAND (stmt, 1)));

    case MODIFY_EXPR:
      if (TREE_CODE (TREE_OPERAND (stmt, 1)) == CALL_EXPR)
	stmt = TREE_OPERAND (stmt, 1);
      else
	return true;
      /* FALLTHRU */

    case CALL_EXPR:
      /* Functions that do not return do not fall through.  */
      return (call_expr_flags (stmt) & ECF_NORETURN) == 0;
    
    case CLEANUP_POINT_EXPR:
      return block_may_fallthru (TREE_OPERAND (stmt, 0));

    default:
      return true;
    }
}

/* Lower a cond_expr TSI.  DATA is passed through the recursion.  */

static void
lower_cond_expr (tree_stmt_iterator *tsi, struct lower_data *data)
{
  tree stmt = tsi_stmt (*tsi);
  bool then_is_goto, else_is_goto;
  tree then_branch, else_branch;
  tree then_goto, else_goto;
  
  then_branch = COND_EXPR_THEN (stmt);
  else_branch = COND_EXPR_ELSE (stmt);

  lower_stmt_body (then_branch, data);
  lower_stmt_body (else_branch, data);

  then_goto = expr_only (then_branch);
  then_is_goto = then_goto && simple_goto_p (then_goto);

  else_goto = expr_only (else_branch);
  else_is_goto = else_goto && simple_goto_p (else_goto);

  if (!then_is_goto || !else_is_goto)
    {
      tree then_label, else_label, end_label, t;

      then_label = NULL_TREE;
      else_label = NULL_TREE;
      end_label = NULL_TREE;
 
      /* Replace the cond_expr with explicit gotos.  */
      if (!then_is_goto)
	{
	  t = build1 (LABEL_EXPR, void_type_node, NULL_TREE);
	  if (TREE_SIDE_EFFECTS (then_branch))
	    then_label = t;
	  else
	    end_label = t;
	  then_goto = build_and_jump (&LABEL_EXPR_LABEL (t));
	}

      if (!else_is_goto)
	{
	  t = build1 (LABEL_EXPR, void_type_node, NULL_TREE);
	  if (TREE_SIDE_EFFECTS (else_branch))
	    else_label = t;
	  else
	    {
	      /* Both THEN and ELSE can be no-ops if one or both contained an
	         empty BIND_EXPR that was associated with the toplevel block
	         of an inlined function.  In that case remove_useless_stmts
	         can't have cleaned things up for us; kill the whole 
	         conditional now.  */
	      if (end_label)
		{
		  tsi_delink (tsi);
		  return;
		}
	      else
		end_label = t;
	    }
	  else_goto = build_and_jump (&LABEL_EXPR_LABEL (t));
	}

      if (then_label)
	{
	  bool may_fallthru = block_may_fallthru (then_branch);

	  tsi_link_after (tsi, then_label, TSI_CONTINUE_LINKING);
	  tsi_link_after (tsi, then_branch, TSI_CONTINUE_LINKING);
  
	  if (else_label && may_fallthru)
	    {
	      end_label = build1 (LABEL_EXPR, void_type_node, NULL_TREE);
	      t = build_and_jump (&LABEL_EXPR_LABEL (end_label));
	      tsi_link_after (tsi, t, TSI_CONTINUE_LINKING);
	    }
	}
  
      if (else_label)
	{
	  tsi_link_after (tsi, else_label, TSI_CONTINUE_LINKING);
	  tsi_link_after (tsi, else_branch, TSI_CONTINUE_LINKING);
	}

      if (end_label)
	tsi_link_after (tsi, end_label, TSI_CONTINUE_LINKING);
    }

  COND_EXPR_THEN (stmt) = then_goto;
  COND_EXPR_ELSE (stmt) = else_goto;

  tsi_next (tsi);
}

/* Lower a return_expr TSI.  DATA is passed through the recursion.  */

static void
lower_return_expr (tree_stmt_iterator *tsi, struct lower_data *data)
{
  tree stmt = tsi_stmt (*tsi);
  tree value, t, label;

  /* Extract the value being returned.  */
  value = TREE_OPERAND (stmt, 0);
  if (value && TREE_CODE (value) == MODIFY_EXPR)
    value = TREE_OPERAND (value, 1);

  /* Match this up with an existing return statement that's been created.  */
  for (t = data->return_statements; t ; t = TREE_CHAIN (t))
    {
      tree tvalue = TREE_OPERAND (TREE_VALUE (t), 0);
      if (tvalue && TREE_CODE (tvalue) == MODIFY_EXPR)
	tvalue = TREE_OPERAND (tvalue, 1);

      if (value == tvalue)
	{
	  label = TREE_PURPOSE (t);
	  goto found;
	}
    }

  /* Not found.  Create a new label and record the return statement.  */
  label = create_artificial_label ();
  data->return_statements = tree_cons (label, stmt, data->return_statements);

  /* Generate a goto statement and remove the return statement.  */
 found:
  t = build1 (GOTO_EXPR, void_type_node, label);
  SET_EXPR_LOCUS (t, EXPR_LOCUS (stmt));
  tsi_link_before (tsi, t, TSI_SAME_STMT);
  tsi_delink (tsi);
}

/* Lower a __builtin_setjmp TSI.

   __builtin_setjmp is passed a pointer to an array of five words (not
   all will be used on all machines).  It operates similarly to the C
   library function of the same name, but is more efficient.

   It is lowered into 3 other builtins, namely __builtin_setjmp_setup,
   __builtin_setjmp_dispatcher and __builtin_setjmp_receiver, but with
   __builtin_setjmp_dispatcher shared among all the instances; that's
   why it is only emitted at the end by lower_function_body.

   After full lowering, the body of the function should look like:

    {
      void * setjmpvar.0;
      int D.1844;
      int D.2844;

      [...]

      __builtin_setjmp_setup (&buf, &<D1847>);
      D.1844 = 0;
      goto <D1846>;
      <D1847>:;
      __builtin_setjmp_receiver (&<D1847>);
      D.1844 = 1;
      <D1846>:;
      if (D.1844 == 0) goto <D1848>; else goto <D1849>;

      [...]

      __builtin_setjmp_setup (&buf, &<D2847>);
      D.2844 = 0;
      goto <D2846>;
      <D2847>:;
      __builtin_setjmp_receiver (&<D2847>);
      D.2844 = 1;
      <D2846>:;
      if (D.2844 == 0) goto <D2848>; else goto <D2849>;

      [...]

      <D3850>:;
      return;
      <D3853>: [non-local];
      setjmpvar.0 = __builtin_setjmp_dispatcher (&<D3853>);
      goto setjmpvar.0;
    }

   The dispatcher block will be both the unique destination of all the
   abnormal call edges and the unique source of all the abnormal edges
   to the receivers, thus keeping the complexity explosion localized.  */

static void
lower_builtin_setjmp (tree_stmt_iterator *tsi)
{
  tree stmt = tsi_stmt (*tsi);
  tree cont_label = create_artificial_label ();
  tree next_label = create_artificial_label ();
  tree dest, t, arg;

  /* NEXT_LABEL is the label __builtin_longjmp will jump to.  Its address is
     passed to both __builtin_setjmp_setup and __builtin_setjmp_receiver.  */
  FORCED_LABEL (next_label) = 1;

  if (TREE_CODE (stmt) == MODIFY_EXPR)
    {
      dest = TREE_OPERAND (stmt, 0);
      stmt = TREE_OPERAND (stmt, 1);
    }
  else
    dest = NULL_TREE;

  /* Build '__builtin_setjmp_setup (BUF, NEXT_LABEL)' and insert.  */
  t = build_addr (next_label, current_function_decl);
  arg = tree_cons (NULL, t, NULL);
  t = TREE_VALUE (TREE_OPERAND (stmt, 1));
  arg = tree_cons (NULL, t, arg);
  t = implicit_built_in_decls[BUILT_IN_SETJMP_SETUP];
  t = build_function_call_expr (t, arg);
  SET_EXPR_LOCUS (t, EXPR_LOCUS (stmt));
  tsi_link_before (tsi, t, TSI_SAME_STMT);

  /* Build 'DEST = 0' and insert.  */
  if (dest)
    {
      t = build2 (MODIFY_EXPR, void_type_node, dest, integer_zero_node);
      SET_EXPR_LOCUS (t, EXPR_LOCUS (stmt));
      tsi_link_before (tsi, t, TSI_SAME_STMT);
    }

  /* Build 'goto CONT_LABEL' and insert.  */
  t = build1 (GOTO_EXPR, void_type_node, cont_label);
  tsi_link_before (tsi, t, TSI_SAME_STMT);

  /* Build 'NEXT_LABEL:' and insert.  */
  t = build1 (LABEL_EXPR, void_type_node, next_label);
  tsi_link_before (tsi, t, TSI_SAME_STMT);

  /* Build '__builtin_setjmp_receiver (NEXT_LABEL)' and insert.  */
  t = build_addr (next_label, current_function_decl);
  arg = tree_cons (NULL, t, NULL);
  t = implicit_built_in_decls[BUILT_IN_SETJMP_RECEIVER];
  t = build_function_call_expr (t, arg);
  SET_EXPR_LOCUS (t, EXPR_LOCUS (stmt));
  tsi_link_before (tsi, t, TSI_SAME_STMT);

  /* Build 'DEST = 1' and insert.  */
  if (dest)
    {
      t = build2 (MODIFY_EXPR, void_type_node, dest, integer_one_node);
      SET_EXPR_LOCUS (t, EXPR_LOCUS (stmt));
      tsi_link_before (tsi, t, TSI_SAME_STMT);
    }

  /* Build 'CONT_LABEL:' and insert.  */
  t = build1 (LABEL_EXPR, void_type_node, cont_label);
  tsi_link_before (tsi, t, TSI_SAME_STMT);

  /* Remove the call to __builtin_setjmp.  */
  tsi_delink (tsi);
}


/* Record the variables in VARS into function FN.  */

void
record_vars_into (tree vars, tree fn)
{
  struct function *saved_cfun = cfun;

  if (fn != current_function_decl)
    cfun = DECL_STRUCT_FUNCTION (fn);

  for (; vars; vars = TREE_CHAIN (vars))
    {
      tree var = vars;

      /* BIND_EXPRs contains also function/type/constant declarations
         we don't need to care about.  */
      if (TREE_CODE (var) != VAR_DECL)
	continue;

      /* Nothing to do in this case.  */
      if (DECL_EXTERNAL (var))
	continue;

      /* Record the variable.  */
      cfun->unexpanded_var_list = tree_cons (NULL_TREE, var,
					     cfun->unexpanded_var_list);
    }

  if (fn != current_function_decl)
    cfun = saved_cfun;
}


/* Record the variables in VARS into current_function_decl.  */

void
record_vars (tree vars)
{
  record_vars_into (vars, current_function_decl);
}


/* Mark BLOCK used if it has a used variable in it, then recurse over its
   subblocks.  */

static void
mark_blocks_with_used_vars (tree block)
{
  tree var;
  tree subblock;

  if (!TREE_USED (block))
    {
      for (var = BLOCK_VARS (block);
	   var;
	   var = TREE_CHAIN (var))
	{
	  if (TREE_USED (var))
	    {
	      TREE_USED (block) = true;
	      break;
	    }
	}
    }
  for (subblock = BLOCK_SUBBLOCKS (block);
       subblock;
       subblock = BLOCK_CHAIN (subblock))
    mark_blocks_with_used_vars (subblock);
}

/* Mark the used attribute on blocks correctly.  */
  
static unsigned int
mark_used_blocks (void)
{  
  mark_blocks_with_used_vars (DECL_INITIAL (current_function_decl));
  return 0;
}


struct tree_opt_pass pass_mark_used_blocks = 
{
  "blocks",				/* name */
  NULL,					/* gate */
  mark_used_blocks,			/* execute */
  NULL,					/* sub */
  NULL,					/* next */
  0,					/* static_pass_number */
  0,					/* tv_id */
  0,					/* properties_required */
  0,					/* properties_provided */
  0,					/* properties_destroyed */
  0,					/* todo_flags_start */
  TODO_dump_func,			/* todo_flags_finish */
  0					/* letter */
};
