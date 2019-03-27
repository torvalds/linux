/* C++-specific tree lowering bits; see also c-gimplify.c and tree-gimple.c.

   Copyright (C) 2002, 2003, 2004, 2005, 2006 Free Software Foundation, Inc.
   Contributed by Jason Merrill <jason@redhat.com>

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
#include "cp-tree.h"
#include "c-common.h"
#include "toplev.h"
#include "tree-gimple.h"
#include "hashtab.h"
#include "pointer-set.h"
#include "flags.h"

/* Local declarations.  */

enum bc_t { bc_break = 0, bc_continue = 1 };

/* Stack of labels which are targets for "break" or "continue",
   linked through TREE_CHAIN.  */
static tree bc_label[2];

/* Begin a scope which can be exited by a break or continue statement.  BC
   indicates which.

   Just creates a label and pushes it into the current context.  */

static tree
begin_bc_block (enum bc_t bc)
{
  tree label = create_artificial_label ();
  TREE_CHAIN (label) = bc_label[bc];
  bc_label[bc] = label;
  return label;
}

/* Finish a scope which can be exited by a break or continue statement.
   LABEL was returned from the most recent call to begin_bc_block.  BODY is
   an expression for the contents of the scope.

   If we saw a break (or continue) in the scope, append a LABEL_EXPR to
   body.  Otherwise, just forget the label.  */

static tree
finish_bc_block (enum bc_t bc, tree label, tree body)
{
  gcc_assert (label == bc_label[bc]);

  if (TREE_USED (label))
    {
      tree t, sl = NULL;

      t = build1 (LABEL_EXPR, void_type_node, label);

      append_to_statement_list (body, &sl);
      append_to_statement_list (t, &sl);
      body = sl;
    }

  bc_label[bc] = TREE_CHAIN (label);
  TREE_CHAIN (label) = NULL_TREE;
  return body;
}

/* Build a GOTO_EXPR to represent a break or continue statement.  BC
   indicates which.  */

static tree
build_bc_goto (enum bc_t bc)
{
  tree label = bc_label[bc];

  if (label == NULL_TREE)
    {
      if (bc == bc_break)
	error ("break statement not within loop or switch");
      else
	error ("continue statement not within loop or switch");

      return NULL_TREE;
    }

  /* Mark the label used for finish_bc_block.  */
  TREE_USED (label) = 1;
  return build1 (GOTO_EXPR, void_type_node, label);
}

/* Genericize a TRY_BLOCK.  */

static void
genericize_try_block (tree *stmt_p)
{
  tree body = TRY_STMTS (*stmt_p);
  tree cleanup = TRY_HANDLERS (*stmt_p);

  gimplify_stmt (&body);

  if (CLEANUP_P (*stmt_p))
    /* A cleanup is an expression, so it doesn't need to be genericized.  */;
  else
    gimplify_stmt (&cleanup);

  *stmt_p = build2 (TRY_CATCH_EXPR, void_type_node, body, cleanup);
}

/* Genericize a HANDLER by converting to a CATCH_EXPR.  */

static void
genericize_catch_block (tree *stmt_p)
{
  tree type = HANDLER_TYPE (*stmt_p);
  tree body = HANDLER_BODY (*stmt_p);

  gimplify_stmt (&body);

  /* FIXME should the caught type go in TREE_TYPE?  */
  *stmt_p = build2 (CATCH_EXPR, void_type_node, type, body);
}

/* Genericize an EH_SPEC_BLOCK by converting it to a
   TRY_CATCH_EXPR/EH_FILTER_EXPR pair.  */

static void
genericize_eh_spec_block (tree *stmt_p)
{
  tree body = EH_SPEC_STMTS (*stmt_p);
  tree allowed = EH_SPEC_RAISES (*stmt_p);
  tree failure = build_call (call_unexpected_node,
			     tree_cons (NULL_TREE, build_exc_ptr (),
					NULL_TREE));
  gimplify_stmt (&body);

  *stmt_p = gimple_build_eh_filter (body, allowed, failure);
}

/* Genericize an IF_STMT by turning it into a COND_EXPR.  */

static void
gimplify_if_stmt (tree *stmt_p)
{
  tree stmt, cond, then_, else_;

  stmt = *stmt_p;
  cond = IF_COND (stmt);
  then_ = THEN_CLAUSE (stmt);
  else_ = ELSE_CLAUSE (stmt);

  if (!then_)
    then_ = build_empty_stmt ();
  if (!else_)
    else_ = build_empty_stmt ();

  if (integer_nonzerop (cond) && !TREE_SIDE_EFFECTS (else_))
    stmt = then_;
  else if (integer_zerop (cond) && !TREE_SIDE_EFFECTS (then_))
    stmt = else_;
  else
    stmt = build3 (COND_EXPR, void_type_node, cond, then_, else_);
  *stmt_p = stmt;
}

/* Build a generic representation of one of the C loop forms.  COND is the
   loop condition or NULL_TREE.  BODY is the (possibly compound) statement
   controlled by the loop.  INCR is the increment expression of a for-loop,
   or NULL_TREE.  COND_IS_FIRST indicates whether the condition is
   evaluated before the loop body as in while and for loops, or after the
   loop body as in do-while loops.  */

static tree
/* APPLE LOCAL begin for-fsf-4_4 3274130 5295549 */ \
gimplify_cp_loop (tree cond, tree body, tree incr, tree attrs,
		  bool cond_is_first, tree inner_foreach)
/* APPLE LOCAL end for-fsf-4_4 3274130 5295549 */ \
{
  tree top, entry, exit, cont_block, break_block, stmt_list, t;
  location_t stmt_locus;

  stmt_locus = input_location;
  stmt_list = NULL_TREE;
  entry = NULL_TREE;

  /* APPLE LOCAL begin C* language */
  /* Order of label addition to stack is important for objc's foreach-stmt. */
  /* APPLE LOCAL radar 4667060 */
  if (inner_foreach == integer_zero_node)
    {
      cont_block = begin_bc_block (bc_continue);
      break_block = begin_bc_block (bc_break);
    }
  else
    {
      break_block = begin_bc_block (bc_break);
      cont_block = begin_bc_block (bc_continue);
    }
  /* APPLE LOCAL end C* language */

  /* If condition is zero don't generate a loop construct.  */
  if (cond && integer_zerop (cond))
    {
      top = NULL_TREE;
      exit = NULL_TREE;
      if (cond_is_first)
	{
	  t = build_bc_goto (bc_break);
	  append_to_statement_list (t, &stmt_list);
	}
    }
  else
    {
      /* If we use a LOOP_EXPR here, we have to feed the whole thing
	 back through the main gimplifier to lower it.  Given that we
	 have to gimplify the loop body NOW so that we can resolve
	 break/continue stmts, seems easier to just expand to gotos.  */
      top = build1 (LABEL_EXPR, void_type_node, NULL_TREE);

      /* If we have an exit condition, then we build an IF with gotos either
	 out of the loop, or to the top of it.  If there's no exit condition,
	 then we just build a jump back to the top.  */
      exit = build_and_jump (&LABEL_EXPR_LABEL (top));
/* APPLE LOCAL begin for-fsf-4_4 3274130 5295549 */ \

      /* Add the attributes to the 'top' label.  */
      decl_attributes (&LABEL_EXPR_LABEL (top), attrs, 0);

/* APPLE LOCAL end for-fsf-4_4 3274130 5295549 */ \
      if (cond && !integer_nonzerop (cond))
	{
	  t = build_bc_goto (bc_break);
	  exit = fold_build3 (COND_EXPR, void_type_node, cond, exit, t);
	  gimplify_stmt (&exit);

	  if (cond_is_first)
	    {
	      if (incr)
		{
		  entry = build1 (LABEL_EXPR, void_type_node, NULL_TREE);
		  t = build_and_jump (&LABEL_EXPR_LABEL (entry));
		}
	      else
		t = build_bc_goto (bc_continue);
	      append_to_statement_list (t, &stmt_list);
	    }
	}
    }

  /* APPLE LOCAL begin radar 4547045 */
  /* Pop foreach's inner loop break label so outer loop's
     break label becomes target of inner loop body's break statements.
  */
  t = NULL_TREE;
  gimplify_stmt (&body);
  gimplify_stmt (&incr);

  body = finish_bc_block (bc_continue, cont_block, body);
  /* APPLE LOCAL begin radar 4547045 */
  /* Push back inner loop's own 'break' label so rest
     of code works seemlessly. */
  /* APPLE LOCAL radar 4667060 */

  append_to_statement_list (top, &stmt_list);
  append_to_statement_list (body, &stmt_list);
  append_to_statement_list (incr, &stmt_list);
  append_to_statement_list (entry, &stmt_list);
  append_to_statement_list (exit, &stmt_list);

  annotate_all_with_locus (&stmt_list, stmt_locus);

  return finish_bc_block (bc_break, break_block, stmt_list);
}

/* Gimplify a FOR_STMT node.  Move the stuff in the for-init-stmt into the
   prequeue and hand off to gimplify_cp_loop.  */

static void
gimplify_for_stmt (tree *stmt_p, tree *pre_p)
{
  tree stmt = *stmt_p;

  if (FOR_INIT_STMT (stmt))
    gimplify_and_add (FOR_INIT_STMT (stmt), pre_p);

/* APPLE LOCAL begin for-fsf-4_4 3274130 5295549 */ \
  *stmt_p = gimplify_cp_loop (FOR_COND (stmt), FOR_BODY (stmt),
			      FOR_EXPR (stmt), FOR_ATTRIBUTES (stmt), 1,
			      NULL_TREE);
/* APPLE LOCAL end for-fsf-4_4 3274130 5295549 */ \
}

/* Gimplify a WHILE_STMT node.  */

static void
gimplify_while_stmt (tree *stmt_p)
{
  tree stmt = *stmt_p;
/* APPLE LOCAL begin for-fsf-4_4 3274130 5295549 */ \
  *stmt_p = gimplify_cp_loop (WHILE_COND (stmt), WHILE_BODY (stmt),
			      NULL_TREE, WHILE_ATTRIBUTES (stmt), 1,
			      NULL_TREE);
/* APPLE LOCAL end for-fsf-4_4 3274130 5295549 */ \
}

/* Gimplify a DO_STMT node.  */

static void
gimplify_do_stmt (tree *stmt_p)
{
  tree stmt = *stmt_p;
/* APPLE LOCAL begin for-fsf-4_4 3274130 5295549 */ \
  *stmt_p = gimplify_cp_loop (DO_COND (stmt), DO_BODY (stmt),
			      NULL_TREE, DO_ATTRIBUTES (stmt), 0,
			      DO_FOREACH (stmt));
/* APPLE LOCAL end for-fsf-4_4 3274130 5295549 */ \
}

/* Genericize a SWITCH_STMT by turning it into a SWITCH_EXPR.  */

static void
gimplify_switch_stmt (tree *stmt_p)
{
  tree stmt = *stmt_p;
  tree break_block, body;
  location_t stmt_locus = input_location;

  break_block = begin_bc_block (bc_break);

  body = SWITCH_STMT_BODY (stmt);
  if (!body)
    body = build_empty_stmt ();

  *stmt_p = build3 (SWITCH_EXPR, SWITCH_STMT_TYPE (stmt),
		    SWITCH_STMT_COND (stmt), body, NULL_TREE);
  SET_EXPR_LOCATION (*stmt_p, stmt_locus);
  gimplify_stmt (stmt_p);

  *stmt_p = finish_bc_block (bc_break, break_block, *stmt_p);
}

/* Hook into the middle of gimplifying an OMP_FOR node.  This is required
   in order to properly gimplify CONTINUE statements.  Here we merely
   manage the continue stack; the rest of the job is performed by the
   regular gimplifier.  */

static enum gimplify_status
cp_gimplify_omp_for (tree *expr_p)
{
  tree for_stmt = *expr_p;
  tree cont_block;

  /* Protect ourselves from recursion.  */
  if (OMP_FOR_GIMPLIFYING_P (for_stmt))
    return GS_UNHANDLED;
  OMP_FOR_GIMPLIFYING_P (for_stmt) = 1;

  /* Note that while technically the continue label is enabled too soon
     here, we should have already diagnosed invalid continues nested within
     statement expressions within the INIT, COND, or INCR expressions.  */
  cont_block = begin_bc_block (bc_continue);

  gimplify_stmt (expr_p);

  OMP_FOR_BODY (for_stmt)
    = finish_bc_block (bc_continue, cont_block, OMP_FOR_BODY (for_stmt));
  OMP_FOR_GIMPLIFYING_P (for_stmt) = 0;

  return GS_ALL_DONE;
}

/*  Gimplify an EXPR_STMT node.  */

static void
gimplify_expr_stmt (tree *stmt_p)
{
  tree stmt = EXPR_STMT_EXPR (*stmt_p);

  if (stmt == error_mark_node)
    stmt = NULL;

  /* Gimplification of a statement expression will nullify the
     statement if all its side effects are moved to *PRE_P and *POST_P.

     In this case we will not want to emit the gimplified statement.
     However, we may still want to emit a warning, so we do that before
     gimplification.  */
  if (stmt && (extra_warnings || warn_unused_value))
    {
      if (!TREE_SIDE_EFFECTS (stmt))
	{
	  if (!IS_EMPTY_STMT (stmt)
	      && !VOID_TYPE_P (TREE_TYPE (stmt))
	      && !TREE_NO_WARNING (stmt))
	    warning (OPT_Wextra, "statement with no effect");
	}
      else if (warn_unused_value)
	warn_if_unused_value (stmt, input_location);
    }

  if (stmt == NULL_TREE)
    stmt = alloc_stmt_list ();

  *stmt_p = stmt;
}

/* Gimplify initialization from an AGGR_INIT_EXPR.  */

static void
cp_gimplify_init_expr (tree *expr_p, tree *pre_p, tree *post_p)
{
  tree from = TREE_OPERAND (*expr_p, 1);
  tree to = TREE_OPERAND (*expr_p, 0);
  tree sub;

  /* What about code that pulls out the temp and uses it elsewhere?  I
     think that such code never uses the TARGET_EXPR as an initializer.  If
     I'm wrong, we'll abort because the temp won't have any RTL.  In that
     case, I guess we'll need to replace references somehow.  */
  if (TREE_CODE (from) == TARGET_EXPR)
    from = TARGET_EXPR_INITIAL (from);

  /* Look through any COMPOUND_EXPRs, since build_compound_expr pushes them
     inside the TARGET_EXPR.  */
  sub = expr_last (from);

  /* If we are initializing from an AGGR_INIT_EXPR, drop the INIT_EXPR and
     replace the slot operand with our target.

     Should we add a target parm to gimplify_expr instead?  No, as in this
     case we want to replace the INIT_EXPR.  */
  if (TREE_CODE (sub) == AGGR_INIT_EXPR)
    {
      gimplify_expr (&to, pre_p, post_p, is_gimple_lvalue, fb_lvalue);
      TREE_OPERAND (sub, 2) = to;
      *expr_p = from;

      /* The initialization is now a side-effect, so the container can
	 become void.  */
      if (from != sub)
	TREE_TYPE (from) = void_type_node;
    }
}

/* Gimplify a MUST_NOT_THROW_EXPR.  */

static void
gimplify_must_not_throw_expr (tree *expr_p, tree *pre_p)
{
  tree stmt = *expr_p;
  tree temp = voidify_wrapper_expr (stmt, NULL);
  tree body = TREE_OPERAND (stmt, 0);

  gimplify_stmt (&body);

  stmt = gimple_build_eh_filter (body, NULL_TREE,
				 build_call (terminate_node, NULL_TREE));

  if (temp)
    {
      append_to_statement_list (stmt, pre_p);
      *expr_p = temp;
    }
  else
    *expr_p = stmt;
}

/* Do C++-specific gimplification.  Args are as for gimplify_expr.  */

int
cp_gimplify_expr (tree *expr_p, tree *pre_p, tree *post_p)
{
  int saved_stmts_are_full_exprs_p = 0;
  enum tree_code code = TREE_CODE (*expr_p);
  enum gimplify_status ret;

  if (STATEMENT_CODE_P (code))
    {
      saved_stmts_are_full_exprs_p = stmts_are_full_exprs_p ();
      current_stmt_tree ()->stmts_are_full_exprs_p
	= STMT_IS_FULL_EXPR_P (*expr_p);
    }

  switch (code)
    {
    case PTRMEM_CST:
      *expr_p = cplus_expand_constant (*expr_p);
      ret = GS_OK;
      break;

    case AGGR_INIT_EXPR:
      simplify_aggr_init_expr (expr_p);
      ret = GS_OK;
      break;

    case THROW_EXPR:
      /* FIXME communicate throw type to backend, probably by moving
	 THROW_EXPR into ../tree.def.  */
      *expr_p = TREE_OPERAND (*expr_p, 0);
      ret = GS_OK;
      break;

    case MUST_NOT_THROW_EXPR:
      gimplify_must_not_throw_expr (expr_p, pre_p);
      ret = GS_OK;
      break;

      /* We used to do this for MODIFY_EXPR as well, but that's unsafe; the
	 LHS of an assignment might also be involved in the RHS, as in bug
	 25979.  */
    case INIT_EXPR:
      cp_gimplify_init_expr (expr_p, pre_p, post_p);
      ret = GS_OK;
      break;

    case EMPTY_CLASS_EXPR:
      /* We create an empty CONSTRUCTOR with RECORD_TYPE.  */
      *expr_p = build_constructor (TREE_TYPE (*expr_p), NULL);
      ret = GS_OK;
      break;

    case BASELINK:
      *expr_p = BASELINK_FUNCTIONS (*expr_p);
      ret = GS_OK;
      break;

    case TRY_BLOCK:
      genericize_try_block (expr_p);
      ret = GS_OK;
      break;

    case HANDLER:
      genericize_catch_block (expr_p);
      ret = GS_OK;
      break;

    case EH_SPEC_BLOCK:
      genericize_eh_spec_block (expr_p);
      ret = GS_OK;
      break;

    case USING_STMT:
      /* Just ignore for now.  Eventually we will want to pass this on to
	 the debugger.  */
      *expr_p = build_empty_stmt ();
      ret = GS_ALL_DONE;
      break;

    case IF_STMT:
      gimplify_if_stmt (expr_p);
      ret = GS_OK;
      break;

    case FOR_STMT:
      gimplify_for_stmt (expr_p, pre_p);
      ret = GS_ALL_DONE;
      break;

    case WHILE_STMT:
      gimplify_while_stmt (expr_p);
      ret = GS_ALL_DONE;
      break;

    case DO_STMT:
      gimplify_do_stmt (expr_p);
      ret = GS_ALL_DONE;
      break;

    case SWITCH_STMT:
      gimplify_switch_stmt (expr_p);
      ret = GS_ALL_DONE;
      break;

    case OMP_FOR:
      ret = cp_gimplify_omp_for (expr_p);
      break;

    case CONTINUE_STMT:
      *expr_p = build_bc_goto (bc_continue);
      ret = GS_ALL_DONE;
      break;

    case BREAK_STMT:
      *expr_p = build_bc_goto (bc_break);
      ret = GS_ALL_DONE;
      break;

    case EXPR_STMT:
      gimplify_expr_stmt (expr_p);
      ret = GS_OK;
      break;

    case UNARY_PLUS_EXPR:
      {
	tree arg = TREE_OPERAND (*expr_p, 0);
	tree type = TREE_TYPE (*expr_p);
	*expr_p = (TREE_TYPE (arg) != type) ? fold_convert (type, arg)
					    : arg;
	ret = GS_OK;
      }
      break;

    default:
      ret = c_gimplify_expr (expr_p, pre_p, post_p);
      break;
    }

  /* Restore saved state.  */
  if (STATEMENT_CODE_P (code))
    current_stmt_tree ()->stmts_are_full_exprs_p
      = saved_stmts_are_full_exprs_p;

  return ret;
}

static inline bool
is_invisiref_parm (tree t)
{
  return ((TREE_CODE (t) == PARM_DECL || TREE_CODE (t) == RESULT_DECL)
	  && DECL_BY_REFERENCE (t));
}

/* Return true if the uid in both int tree maps are equal.  */

int
cxx_int_tree_map_eq (const void *va, const void *vb)
{
  const struct cxx_int_tree_map *a = (const struct cxx_int_tree_map *) va;
  const struct cxx_int_tree_map *b = (const struct cxx_int_tree_map *) vb;
  return (a->uid == b->uid);
}

/* Hash a UID in a cxx_int_tree_map.  */

unsigned int
cxx_int_tree_map_hash (const void *item)
{
  return ((const struct cxx_int_tree_map *)item)->uid;
}

/* Perform any pre-gimplification lowering of C++ front end trees to
   GENERIC.  */

static tree
cp_genericize_r (tree *stmt_p, int *walk_subtrees, void *data)
{
  tree stmt = *stmt_p;
  struct pointer_set_t *p_set = (struct pointer_set_t*) data;

  if (is_invisiref_parm (stmt)
      /* Don't dereference parms in a thunk, pass the references through. */
      && !(DECL_THUNK_P (current_function_decl)
	   && TREE_CODE (stmt) == PARM_DECL))
    {
      *stmt_p = convert_from_reference (stmt);
      *walk_subtrees = 0;
      return NULL;
    }

  /* Map block scope extern declarations to visible declarations with the
     same name and type in outer scopes if any.  */
  if (cp_function_chain->extern_decl_map
      && (TREE_CODE (stmt) == FUNCTION_DECL || TREE_CODE (stmt) == VAR_DECL)
      && DECL_EXTERNAL (stmt))
    {
      struct cxx_int_tree_map *h, in;
      in.uid = DECL_UID (stmt);
      h = (struct cxx_int_tree_map *)
	  htab_find_with_hash (cp_function_chain->extern_decl_map,
			       &in, in.uid);
      if (h)
	{
	  *stmt_p = h->to;
	  *walk_subtrees = 0;
	  return NULL;
	}
    }

  /* Other than invisiref parms, don't walk the same tree twice.  */
  if (pointer_set_contains (p_set, stmt))
    {
      *walk_subtrees = 0;
      return NULL_TREE;
    }

  if (TREE_CODE (stmt) == ADDR_EXPR
      && is_invisiref_parm (TREE_OPERAND (stmt, 0)))
    {
      *stmt_p = convert (TREE_TYPE (stmt), TREE_OPERAND (stmt, 0));
      *walk_subtrees = 0;
    }
  else if (TREE_CODE (stmt) == RETURN_EXPR
	   && TREE_OPERAND (stmt, 0)
	   && is_invisiref_parm (TREE_OPERAND (stmt, 0)))
    /* Don't dereference an invisiref RESULT_DECL inside a RETURN_EXPR.  */
    *walk_subtrees = 0;
  else if (TREE_CODE (stmt) == OMP_CLAUSE)
    switch (OMP_CLAUSE_CODE (stmt))
      {
      case OMP_CLAUSE_PRIVATE:
      case OMP_CLAUSE_SHARED:
      case OMP_CLAUSE_FIRSTPRIVATE:
      case OMP_CLAUSE_LASTPRIVATE:
      case OMP_CLAUSE_COPYIN:
      case OMP_CLAUSE_COPYPRIVATE:
	/* Don't dereference an invisiref in OpenMP clauses.  */
	if (is_invisiref_parm (OMP_CLAUSE_DECL (stmt)))
	  *walk_subtrees = 0;
	break;
      case OMP_CLAUSE_REDUCTION:
	gcc_assert (!is_invisiref_parm (OMP_CLAUSE_DECL (stmt)));
	break;
      default:
	break;
      }
  else if (IS_TYPE_OR_DECL_P (stmt))
    *walk_subtrees = 0;

  /* Due to the way voidify_wrapper_expr is written, we don't get a chance
     to lower this construct before scanning it, so we need to lower these
     before doing anything else.  */
  else if (TREE_CODE (stmt) == CLEANUP_STMT)
    *stmt_p = build2 (CLEANUP_EH_ONLY (stmt) ? TRY_CATCH_EXPR
					     : TRY_FINALLY_EXPR,
		      void_type_node,
		      CLEANUP_BODY (stmt),
		      CLEANUP_EXPR (stmt));

  pointer_set_insert (p_set, *stmt_p);

  return NULL;
}

void
cp_genericize (tree fndecl)
{
  tree t;
  struct pointer_set_t *p_set;

  /* Fix up the types of parms passed by invisible reference.  */
  for (t = DECL_ARGUMENTS (fndecl); t; t = TREE_CHAIN (t))
    if (TREE_ADDRESSABLE (TREE_TYPE (t)))
      {
	/* If a function's arguments are copied to create a thunk,
	   then DECL_BY_REFERENCE will be set -- but the type of the
	   argument will be a pointer type, so we will never get
	   here.  */
	gcc_assert (!DECL_BY_REFERENCE (t));
	gcc_assert (DECL_ARG_TYPE (t) != TREE_TYPE (t));
	TREE_TYPE (t) = DECL_ARG_TYPE (t);
	DECL_BY_REFERENCE (t) = 1;
	TREE_ADDRESSABLE (t) = 0;
	relayout_decl (t);
      }

  /* Do the same for the return value.  */
  if (TREE_ADDRESSABLE (TREE_TYPE (DECL_RESULT (fndecl))))
    {
      t = DECL_RESULT (fndecl);
      TREE_TYPE (t) = build_reference_type (TREE_TYPE (t));
      DECL_BY_REFERENCE (t) = 1;
      TREE_ADDRESSABLE (t) = 0;
      relayout_decl (t);
    }

  /* If we're a clone, the body is already GIMPLE.  */
  if (DECL_CLONED_FUNCTION_P (fndecl))
    return;

  /* We do want to see every occurrence of the parms, so we can't just use
     walk_tree's hash functionality.  */
  p_set = pointer_set_create ();
  walk_tree (&DECL_SAVED_TREE (fndecl), cp_genericize_r, p_set, NULL);
  pointer_set_destroy (p_set);

  /* Do everything else.  */
  c_genericize (fndecl);

  gcc_assert (bc_label[bc_break] == NULL);
  gcc_assert (bc_label[bc_continue] == NULL);
}

/* Build code to apply FN to each member of ARG1 and ARG2.  FN may be
   NULL if there is in fact nothing to do.  ARG2 may be null if FN
   actually only takes one argument.  */

static tree
cxx_omp_clause_apply_fn (tree fn, tree arg1, tree arg2)
{
  tree defparm, parm;
  int i;

  if (fn == NULL)
    return NULL;

  defparm = TREE_CHAIN (TYPE_ARG_TYPES (TREE_TYPE (fn)));
  if (arg2)
    defparm = TREE_CHAIN (defparm);

  if (TREE_CODE (TREE_TYPE (arg1)) == ARRAY_TYPE)
    {
      tree inner_type = TREE_TYPE (arg1);
      tree start1, end1, p1;
      tree start2 = NULL, p2 = NULL;
      tree ret = NULL, lab, t;

      start1 = arg1;
      start2 = arg2;
      do
	{
	  inner_type = TREE_TYPE (inner_type);
	  start1 = build4 (ARRAY_REF, inner_type, start1,
			   size_zero_node, NULL, NULL);
	  if (arg2)
	    start2 = build4 (ARRAY_REF, inner_type, start2,
			     size_zero_node, NULL, NULL);
	}
      while (TREE_CODE (inner_type) == ARRAY_TYPE);
      start1 = build_fold_addr_expr (start1);
      if (arg2)
	start2 = build_fold_addr_expr (start2);

      end1 = TYPE_SIZE_UNIT (TREE_TYPE (arg1));
      end1 = fold_convert (TREE_TYPE (start1), end1);
      end1 = build2 (PLUS_EXPR, TREE_TYPE (start1), start1, end1);

      p1 = create_tmp_var (TREE_TYPE (start1), NULL);
      t = build2 (MODIFY_EXPR, void_type_node, p1, start1);
      append_to_statement_list (t, &ret);

      if (arg2)
	{
	  p2 = create_tmp_var (TREE_TYPE (start2), NULL);
	  t = build2 (MODIFY_EXPR, void_type_node, p2, start2);
	  append_to_statement_list (t, &ret);
	}

      lab = create_artificial_label ();
      t = build1 (LABEL_EXPR, void_type_node, lab);
      append_to_statement_list (t, &ret);

      t = tree_cons (NULL, p1, NULL);
      if (arg2)
	t = tree_cons (NULL, p2, t);
      /* Handle default arguments.  */
      i = 1 + (arg2 != NULL);
      for (parm = defparm; parm != void_list_node; parm = TREE_CHAIN (parm))
	t = tree_cons (NULL, convert_default_arg (TREE_VALUE (parm),
						  TREE_PURPOSE (parm),
						  fn, i++), t);
      t = build_call (fn, nreverse (t));
      append_to_statement_list (t, &ret);

      t = fold_convert (TREE_TYPE (p1), TYPE_SIZE_UNIT (inner_type));
      t = build2 (PLUS_EXPR, TREE_TYPE (p1), p1, t);
      t = build2 (MODIFY_EXPR, void_type_node, p1, t);
      append_to_statement_list (t, &ret);

      if (arg2)
	{
	  t = fold_convert (TREE_TYPE (p2), TYPE_SIZE_UNIT (inner_type));
	  t = build2 (PLUS_EXPR, TREE_TYPE (p2), p2, t);
	  t = build2 (MODIFY_EXPR, void_type_node, p2, t);
	  append_to_statement_list (t, &ret);
	}

      t = build2 (NE_EXPR, boolean_type_node, p1, end1);
      t = build3 (COND_EXPR, void_type_node, t, build_and_jump (&lab), NULL);
      append_to_statement_list (t, &ret);

      return ret;
    }
  else
    {
      tree t = tree_cons (NULL, build_fold_addr_expr (arg1), NULL);
      if (arg2)
	t = tree_cons (NULL, build_fold_addr_expr (arg2), t);
      /* Handle default arguments.  */
      i = 1 + (arg2 != NULL);
      for (parm = defparm; parm != void_list_node; parm = TREE_CHAIN (parm))
	t = tree_cons (NULL, convert_default_arg (TREE_VALUE (parm),
						  TREE_PURPOSE (parm),
						  fn, i++), t);
      return build_call (fn, nreverse (t));
    }
}

/* Return code to initialize DECL with its default constructor, or
   NULL if there's nothing to do.  */

tree
cxx_omp_clause_default_ctor (tree clause, tree decl)
{
  tree info = CP_OMP_CLAUSE_INFO (clause);
  tree ret = NULL;

  if (info)
    ret = cxx_omp_clause_apply_fn (TREE_VEC_ELT (info, 0), decl, NULL);

  return ret;
}

/* Return code to initialize DST with a copy constructor from SRC.  */

tree
cxx_omp_clause_copy_ctor (tree clause, tree dst, tree src)
{
  tree info = CP_OMP_CLAUSE_INFO (clause);
  tree ret = NULL;

  if (info)
    ret = cxx_omp_clause_apply_fn (TREE_VEC_ELT (info, 0), dst, src);
  if (ret == NULL)
    ret = build2 (MODIFY_EXPR, void_type_node, dst, src);

  return ret;
}

/* Similarly, except use an assignment operator instead.  */

tree
cxx_omp_clause_assign_op (tree clause, tree dst, tree src)
{
  tree info = CP_OMP_CLAUSE_INFO (clause);
  tree ret = NULL;

  if (info)
    ret = cxx_omp_clause_apply_fn (TREE_VEC_ELT (info, 2), dst, src);
  if (ret == NULL)
    ret = build2 (MODIFY_EXPR, void_type_node, dst, src);

  return ret;
}

/* Return code to destroy DECL.  */

tree
cxx_omp_clause_dtor (tree clause, tree decl)
{
  tree info = CP_OMP_CLAUSE_INFO (clause);
  tree ret = NULL;

  if (info)
    ret = cxx_omp_clause_apply_fn (TREE_VEC_ELT (info, 1), decl, NULL);

  return ret;
}

/* True if OpenMP should privatize what this DECL points to rather
   than the DECL itself.  */

bool
cxx_omp_privatize_by_reference (tree decl)
{
  return is_invisiref_parm (decl);
}
