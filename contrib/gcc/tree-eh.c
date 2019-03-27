/* Exception handling semantics and decomposition for trees.
   Copyright (C) 2003, 2004, 2005, 2006, 2007 Free Software Foundation, Inc.

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
#include "tm_p.h"
#include "flags.h"
#include "function.h"
#include "except.h"
#include "tree-flow.h"
#include "tree-dump.h"
#include "tree-inline.h"
#include "tree-iterator.h"
#include "tree-pass.h"
#include "timevar.h"
#include "langhooks.h"
#include "ggc.h"
#include "toplev.h"


/* Nonzero if we are using EH to handle cleanups.  */
static int using_eh_for_cleanups_p = 0;

void
using_eh_for_cleanups (void)
{
  using_eh_for_cleanups_p = 1;
}

/* Misc functions used in this file.  */

/* Compare and hash for any structure which begins with a canonical
   pointer.  Assumes all pointers are interchangeable, which is sort
   of already assumed by gcc elsewhere IIRC.  */

static int
struct_ptr_eq (const void *a, const void *b)
{
  const void * const * x = (const void * const *) a;
  const void * const * y = (const void * const *) b;
  return *x == *y;
}

static hashval_t
struct_ptr_hash (const void *a)
{
  const void * const * x = (const void * const *) a;
  return (size_t)*x >> 4;
}


/* Remember and lookup EH region data for arbitrary statements.
   Really this means any statement that could_throw_p.  We could
   stuff this information into the stmt_ann data structure, but:

   (1) We absolutely rely on this information being kept until
   we get to rtl.  Once we're done with lowering here, if we lose
   the information there's no way to recover it!

   (2) There are many more statements that *cannot* throw as
   compared to those that can.  We should be saving some amount
   of space by only allocating memory for those that can throw.  */

static void
record_stmt_eh_region (struct eh_region *region, tree t)
{
  if (!region)
    return;

  add_stmt_to_eh_region (t, get_eh_region_number (region));
}

void
add_stmt_to_eh_region_fn (struct function *ifun, tree t, int num)
{
  struct throw_stmt_node *n;
  void **slot;

  gcc_assert (num >= 0);
  gcc_assert (TREE_CODE (t) != RESX_EXPR);

  n = GGC_NEW (struct throw_stmt_node);
  n->stmt = t;
  n->region_nr = num;

  if (!get_eh_throw_stmt_table (ifun))
    set_eh_throw_stmt_table (ifun, htab_create_ggc (31, struct_ptr_hash,
						    struct_ptr_eq,
						    ggc_free));

  slot = htab_find_slot (get_eh_throw_stmt_table (ifun), n, INSERT);
  gcc_assert (!*slot);
  *slot = n;
  /* ??? For the benefit of calls.c, converting all this to rtl,
     we need to record the call expression, not just the outer
     modify statement.  */
  if (TREE_CODE (t) == MODIFY_EXPR
      && (t = get_call_expr_in (t)))
    add_stmt_to_eh_region_fn (ifun, t, num);
}

void
add_stmt_to_eh_region (tree t, int num)
{
  add_stmt_to_eh_region_fn (cfun, t, num);
}

bool
remove_stmt_from_eh_region_fn (struct function *ifun, tree t)
{
  struct throw_stmt_node dummy;
  void **slot;

  if (!get_eh_throw_stmt_table (ifun))
    return false;

  dummy.stmt = t;
  slot = htab_find_slot (get_eh_throw_stmt_table (ifun), &dummy,
                        NO_INSERT);
  if (slot)
    {
      htab_clear_slot (get_eh_throw_stmt_table (ifun), slot);
      /* ??? For the benefit of calls.c, converting all this to rtl,
	 we need to record the call expression, not just the outer
	 modify statement.  */
      if (TREE_CODE (t) == MODIFY_EXPR
	  && (t = get_call_expr_in (t)))
	remove_stmt_from_eh_region_fn (ifun, t);
      return true;
    }
  else
    return false;
}

bool
remove_stmt_from_eh_region (tree t)
{
  return remove_stmt_from_eh_region_fn (cfun, t);
}

int
lookup_stmt_eh_region_fn (struct function *ifun, tree t)
{
  struct throw_stmt_node *p, n;

  if (!get_eh_throw_stmt_table (ifun))
    return -2;

  n.stmt = t;
  p = (struct throw_stmt_node *) htab_find (get_eh_throw_stmt_table (ifun),
                                            &n);

  return (p ? p->region_nr : -1);
}

int
lookup_stmt_eh_region (tree t)
{
  /* We can get called from initialized data when -fnon-call-exceptions
     is on; prevent crash.  */
  if (!cfun)
    return -1;
  return lookup_stmt_eh_region_fn (cfun, t);
}


/* First pass of EH node decomposition.  Build up a tree of TRY_FINALLY_EXPR
   nodes and LABEL_DECL nodes.  We will use this during the second phase to
   determine if a goto leaves the body of a TRY_FINALLY_EXPR node.  */

struct finally_tree_node
{
  tree child, parent;
};

/* Note that this table is *not* marked GTY.  It is short-lived.  */
static htab_t finally_tree;

static void
record_in_finally_tree (tree child, tree parent)
{
  struct finally_tree_node *n;
  void **slot;

  n = XNEW (struct finally_tree_node);
  n->child = child;
  n->parent = parent;

  slot = htab_find_slot (finally_tree, n, INSERT);
  gcc_assert (!*slot);
  *slot = n;
}

static void
collect_finally_tree (tree t, tree region)
{
 tailrecurse:
  switch (TREE_CODE (t))
    {
    case LABEL_EXPR:
      record_in_finally_tree (LABEL_EXPR_LABEL (t), region);
      break;

    case TRY_FINALLY_EXPR:
      record_in_finally_tree (t, region);
      collect_finally_tree (TREE_OPERAND (t, 0), t);
      t = TREE_OPERAND (t, 1);
      goto tailrecurse;

    case TRY_CATCH_EXPR:
      collect_finally_tree (TREE_OPERAND (t, 0), region);
      t = TREE_OPERAND (t, 1);
      goto tailrecurse;

    case CATCH_EXPR:
      t = CATCH_BODY (t);
      goto tailrecurse;

    case EH_FILTER_EXPR:
      t = EH_FILTER_FAILURE (t);
      goto tailrecurse;

    case STATEMENT_LIST:
      {
	tree_stmt_iterator i;
	for (i = tsi_start (t); !tsi_end_p (i); tsi_next (&i))
	  collect_finally_tree (tsi_stmt (i), region);
      }
      break;

    default:
      /* A type, a decl, or some kind of statement that we're not
	 interested in.  Don't walk them.  */
      break;
    }
}

/* Use the finally tree to determine if a jump from START to TARGET
   would leave the try_finally node that START lives in.  */

static bool
outside_finally_tree (tree start, tree target)
{
  struct finally_tree_node n, *p;

  do
    {
      n.child = start;
      p = (struct finally_tree_node *) htab_find (finally_tree, &n);
      if (!p)
	return true;
      start = p->parent;
    }
  while (start != target);

  return false;
}

/* Second pass of EH node decomposition.  Actually transform the TRY_FINALLY
   and TRY_CATCH nodes into a set of gotos, magic labels, and eh regions.
   The eh region creation is straight-forward, but frobbing all the gotos
   and such into shape isn't.  */

/* State of the world while lowering.  */

struct leh_state
{
  /* What's "current" while constructing the eh region tree.  These
     correspond to variables of the same name in cfun->eh, which we
     don't have easy access to.  */
  struct eh_region *cur_region;
  struct eh_region *prev_try;

  /* Processing of TRY_FINALLY requires a bit more state.  This is
     split out into a separate structure so that we don't have to
     copy so much when processing other nodes.  */
  struct leh_tf_state *tf;
};

struct leh_tf_state
{
  /* Pointer to the TRY_FINALLY node under discussion.  The try_finally_expr
     is the original TRY_FINALLY_EXPR.  We need to retain this so that
     outside_finally_tree can reliably reference the tree used in the
     collect_finally_tree data structures.  */
  tree try_finally_expr;
  tree *top_p;

  /* The state outside this try_finally node.  */
  struct leh_state *outer;

  /* The exception region created for it.  */
  struct eh_region *region;

  /* The GOTO_QUEUE is is an array of GOTO_EXPR and RETURN_EXPR statements
     that are seen to escape this TRY_FINALLY_EXPR node.  */
  struct goto_queue_node {
    tree stmt;
    tree repl_stmt;
    tree cont_stmt;
    int index;
  } *goto_queue;
  size_t goto_queue_size;
  size_t goto_queue_active;

  /* The set of unique labels seen as entries in the goto queue.  */
  VEC(tree,heap) *dest_array;

  /* A label to be added at the end of the completed transformed
     sequence.  It will be set if may_fallthru was true *at one time*,
     though subsequent transformations may have cleared that flag.  */
  tree fallthru_label;

  /* A label that has been registered with except.c to be the
     landing pad for this try block.  */
  tree eh_label;

  /* True if it is possible to fall out the bottom of the try block.
     Cleared if the fallthru is converted to a goto.  */
  bool may_fallthru;

  /* True if any entry in goto_queue is a RETURN_EXPR.  */
  bool may_return;

  /* True if the finally block can receive an exception edge.
     Cleared if the exception case is handled by code duplication.  */
  bool may_throw;
};

static void lower_eh_filter (struct leh_state *, tree *);
static void lower_eh_constructs_1 (struct leh_state *, tree *);

/* Comparison function for qsort/bsearch.  We're interested in
   searching goto queue elements for source statements.  */

static int
goto_queue_cmp (const void *x, const void *y)
{
  tree a = ((const struct goto_queue_node *)x)->stmt;
  tree b = ((const struct goto_queue_node *)y)->stmt;
  return (a == b ? 0 : a < b ? -1 : 1);
}

/* Search for STMT in the goto queue.  Return the replacement,
   or null if the statement isn't in the queue.  */

static tree
find_goto_replacement (struct leh_tf_state *tf, tree stmt)
{
  struct goto_queue_node tmp, *ret;
  tmp.stmt = stmt;
  ret = (struct goto_queue_node *)
     bsearch (&tmp, tf->goto_queue, tf->goto_queue_active,
		 sizeof (struct goto_queue_node), goto_queue_cmp);
  return (ret ? ret->repl_stmt : NULL);
}

/* A subroutine of replace_goto_queue_1.  Handles the sub-clauses of a
   lowered COND_EXPR.  If, by chance, the replacement is a simple goto,
   then we can just splat it in, otherwise we add the new stmts immediately
   after the COND_EXPR and redirect.  */

static void
replace_goto_queue_cond_clause (tree *tp, struct leh_tf_state *tf,
				tree_stmt_iterator *tsi)
{
  tree new, one, label;

  new = find_goto_replacement (tf, *tp);
  if (!new)
    return;

  one = expr_only (new);
  if (one && TREE_CODE (one) == GOTO_EXPR)
    {
      *tp = one;
      return;
    }

  label = build1 (LABEL_EXPR, void_type_node, NULL_TREE);
  *tp = build_and_jump (&LABEL_EXPR_LABEL (label));

  tsi_link_after (tsi, label, TSI_CONTINUE_LINKING);
  tsi_link_after (tsi, new, TSI_CONTINUE_LINKING);
}

/* The real work of replace_goto_queue.  Returns with TSI updated to
   point to the next statement.  */

static void replace_goto_queue_stmt_list (tree, struct leh_tf_state *);

static void
replace_goto_queue_1 (tree t, struct leh_tf_state *tf, tree_stmt_iterator *tsi)
{
  switch (TREE_CODE (t))
    {
    case GOTO_EXPR:
    case RETURN_EXPR:
      t = find_goto_replacement (tf, t);
      if (t)
	{
	  tsi_link_before (tsi, t, TSI_SAME_STMT);
	  tsi_delink (tsi);
	  return;
	}
      break;

    case COND_EXPR:
      replace_goto_queue_cond_clause (&COND_EXPR_THEN (t), tf, tsi);
      replace_goto_queue_cond_clause (&COND_EXPR_ELSE (t), tf, tsi);
      break;

    case TRY_FINALLY_EXPR:
    case TRY_CATCH_EXPR:
      replace_goto_queue_stmt_list (TREE_OPERAND (t, 0), tf);
      replace_goto_queue_stmt_list (TREE_OPERAND (t, 1), tf);
      break;
    case CATCH_EXPR:
      replace_goto_queue_stmt_list (CATCH_BODY (t), tf);
      break;
    case EH_FILTER_EXPR:
      replace_goto_queue_stmt_list (EH_FILTER_FAILURE (t), tf);
      break;

    case STATEMENT_LIST:
      gcc_unreachable ();

    default:
      /* These won't have gotos in them.  */
      break;
    }

  tsi_next (tsi);
}

/* A subroutine of replace_goto_queue.  Handles STATEMENT_LISTs.  */

static void
replace_goto_queue_stmt_list (tree t, struct leh_tf_state *tf)
{
  tree_stmt_iterator i = tsi_start (t);
  while (!tsi_end_p (i))
    replace_goto_queue_1 (tsi_stmt (i), tf, &i);
}

/* Replace all goto queue members.  */

static void
replace_goto_queue (struct leh_tf_state *tf)
{
  if (tf->goto_queue_active == 0)
    return;
  replace_goto_queue_stmt_list (*tf->top_p, tf);
}

/* For any GOTO_EXPR or RETURN_EXPR, decide whether it leaves a try_finally
   node, and if so record that fact in the goto queue associated with that
   try_finally node.  */

static void
maybe_record_in_goto_queue (struct leh_state *state, tree stmt)
{
  struct leh_tf_state *tf = state->tf;
  struct goto_queue_node *q;
  size_t active, size;
  int index;

  if (!tf)
    return;

  switch (TREE_CODE (stmt))
    {
    case GOTO_EXPR:
      {
	tree lab = GOTO_DESTINATION (stmt);

	/* Computed and non-local gotos do not get processed.  Given
	   their nature we can neither tell whether we've escaped the
	   finally block nor redirect them if we knew.  */
	if (TREE_CODE (lab) != LABEL_DECL)
	  return;

	/* No need to record gotos that don't leave the try block.  */
	if (! outside_finally_tree (lab, tf->try_finally_expr))
	  return;

	if (! tf->dest_array)
	  {
	    tf->dest_array = VEC_alloc (tree, heap, 10);
	    VEC_quick_push (tree, tf->dest_array, lab);
	    index = 0;
	  }
	else
	  {
	    int n = VEC_length (tree, tf->dest_array);
	    for (index = 0; index < n; ++index)
	      if (VEC_index (tree, tf->dest_array, index) == lab)
		break;
	    if (index == n)
	      VEC_safe_push (tree, heap, tf->dest_array, lab);
	  }
      }
      break;

    case RETURN_EXPR:
      tf->may_return = true;
      index = -1;
      break;

    default:
      gcc_unreachable ();
    }

  active = tf->goto_queue_active;
  size = tf->goto_queue_size;
  if (active >= size)
    {
      size = (size ? size * 2 : 32);
      tf->goto_queue_size = size;
      tf->goto_queue
         = XRESIZEVEC (struct goto_queue_node, tf->goto_queue, size);
    }

  q = &tf->goto_queue[active];
  tf->goto_queue_active = active + 1;

  memset (q, 0, sizeof (*q));
  q->stmt = stmt;
  q->index = index;
}

#ifdef ENABLE_CHECKING
/* We do not process SWITCH_EXPRs for now.  As long as the original source
   was in fact structured, and we've not yet done jump threading, then none
   of the labels will leave outer TRY_FINALLY_EXPRs.  Verify this.  */

static void
verify_norecord_switch_expr (struct leh_state *state, tree switch_expr)
{
  struct leh_tf_state *tf = state->tf;
  size_t i, n;
  tree vec;

  if (!tf)
    return;

  vec = SWITCH_LABELS (switch_expr);
  n = TREE_VEC_LENGTH (vec);

  for (i = 0; i < n; ++i)
    {
      tree lab = CASE_LABEL (TREE_VEC_ELT (vec, i));
      gcc_assert (!outside_finally_tree (lab, tf->try_finally_expr));
    }
}
#else
#define verify_norecord_switch_expr(state, switch_expr)
#endif

/* Redirect a RETURN_EXPR pointed to by STMT_P to FINLAB.  Place in CONT_P
   whatever is needed to finish the return.  If MOD is non-null, insert it
   before the new branch.  RETURN_VALUE_P is a cache containing a temporary
   variable to be used in manipulating the value returned from the function.  */

static void
do_return_redirection (struct goto_queue_node *q, tree finlab, tree mod,
		       tree *return_value_p)
{
  tree ret_expr = TREE_OPERAND (q->stmt, 0);
  tree x;

  if (ret_expr)
    {
      /* The nasty part about redirecting the return value is that the
	 return value itself is to be computed before the FINALLY block
	 is executed.  e.g.

		int x;
		int foo (void)
		{
		  x = 0;
		  try {
		    return x;
		  } finally {
		    x++;
		  }
		}

	  should return 0, not 1.  Arrange for this to happen by copying
	  computed the return value into a local temporary.  This also
	  allows us to redirect multiple return statements through the
	  same destination block; whether this is a net win or not really
	  depends, I guess, but it does make generation of the switch in
	  lower_try_finally_switch easier.  */

      switch (TREE_CODE (ret_expr))
	{
	case RESULT_DECL:
	  if (!*return_value_p)
	    *return_value_p = ret_expr;
	  else
	    gcc_assert (*return_value_p == ret_expr);
	  q->cont_stmt = q->stmt;
	  break;

	case MODIFY_EXPR:
	  {
	    tree result = TREE_OPERAND (ret_expr, 0);
	    tree new, old = TREE_OPERAND (ret_expr, 1);

	    if (!*return_value_p)
	      {
		if (aggregate_value_p (TREE_TYPE (result),
				      TREE_TYPE (current_function_decl)))
		  /* If this function returns in memory, copy the argument
		    into the return slot now.  Otherwise, we might need to
		    worry about magic return semantics, so we need to use a
		    temporary to hold the value until we're actually ready
		    to return.  */
		  new = result;
		else
		  new = create_tmp_var (TREE_TYPE (old), "rettmp");
		*return_value_p = new;
	      }
	    else
	      new = *return_value_p;

	    x = build2 (MODIFY_EXPR, TREE_TYPE (new), new, old);
	    append_to_statement_list (x, &q->repl_stmt);

	    if (new == result)
	      x = result;
	    else
	      x = build2 (MODIFY_EXPR, TREE_TYPE (result), result, new);
	    q->cont_stmt = build1 (RETURN_EXPR, void_type_node, x);
	  }

	default:
	  gcc_unreachable ();
	}
    }
  else
    {
      /* If we don't return a value, all return statements are the same.  */
      q->cont_stmt = q->stmt;
    }

  if (mod)
    append_to_statement_list (mod, &q->repl_stmt);

  x = build1 (GOTO_EXPR, void_type_node, finlab);
  append_to_statement_list (x, &q->repl_stmt);
}

/* Similar, but easier, for GOTO_EXPR.  */

static void
do_goto_redirection (struct goto_queue_node *q, tree finlab, tree mod)
{
  tree x;

  q->cont_stmt = q->stmt;
  if (mod)
    append_to_statement_list (mod, &q->repl_stmt);

  x = build1 (GOTO_EXPR, void_type_node, finlab);
  append_to_statement_list (x, &q->repl_stmt);
}

/* We want to transform
	try { body; } catch { stuff; }
   to
	body; goto over; lab: stuff; over:

   T is a TRY_FINALLY or TRY_CATCH node.  LAB is the label that
   should be placed before the second operand, or NULL.  OVER is
   an existing label that should be put at the exit, or NULL.  */

static void
frob_into_branch_around (tree *tp, tree lab, tree over)
{
  tree x, op1;

  op1 = TREE_OPERAND (*tp, 1);
  *tp = TREE_OPERAND (*tp, 0);

  if (block_may_fallthru (*tp))
    {
      if (!over)
	over = create_artificial_label ();
      x = build1 (GOTO_EXPR, void_type_node, over);
      append_to_statement_list (x, tp);
    }

  if (lab)
    {
      x = build1 (LABEL_EXPR, void_type_node, lab);
      append_to_statement_list (x, tp);
    }

  append_to_statement_list (op1, tp);

  if (over)
    {
      x = build1 (LABEL_EXPR, void_type_node, over);
      append_to_statement_list (x, tp);
    }
}

/* A subroutine of lower_try_finally.  Duplicate the tree rooted at T.
   Make sure to record all new labels found.  */

static tree
lower_try_finally_dup_block (tree t, struct leh_state *outer_state)
{
  tree region = NULL;

  t = unsave_expr_now (t);

  if (outer_state->tf)
    region = outer_state->tf->try_finally_expr;
  collect_finally_tree (t, region);

  return t;
}

/* A subroutine of lower_try_finally.  Create a fallthru label for
   the given try_finally state.  The only tricky bit here is that
   we have to make sure to record the label in our outer context.  */

static tree
lower_try_finally_fallthru_label (struct leh_tf_state *tf)
{
  tree label = tf->fallthru_label;
  if (!label)
    {
      label = create_artificial_label ();
      tf->fallthru_label = label;
      if (tf->outer->tf)
        record_in_finally_tree (label, tf->outer->tf->try_finally_expr);
    }
  return label;
}

/* A subroutine of lower_try_finally.  If lang_protect_cleanup_actions
   returns non-null, then the language requires that the exception path out
   of a try_finally be treated specially.  To wit: the code within the
   finally block may not itself throw an exception.  We have two choices here.
   First we can duplicate the finally block and wrap it in a must_not_throw
   region.  Second, we can generate code like

	try {
	  finally_block;
	} catch {
	  if (fintmp == eh_edge)
	    protect_cleanup_actions;
	}

   where "fintmp" is the temporary used in the switch statement generation
   alternative considered below.  For the nonce, we always choose the first
   option.

   THIS_STATE may be null if this is a try-cleanup, not a try-finally.  */

static void
honor_protect_cleanup_actions (struct leh_state *outer_state,
			       struct leh_state *this_state,
			       struct leh_tf_state *tf)
{
  tree protect_cleanup_actions, finally, x;
  tree_stmt_iterator i;
  bool finally_may_fallthru;

  /* First check for nothing to do.  */
  if (lang_protect_cleanup_actions)
    protect_cleanup_actions = lang_protect_cleanup_actions ();
  else
    protect_cleanup_actions = NULL;

  finally = TREE_OPERAND (*tf->top_p, 1);

  /* If the EH case of the finally block can fall through, this may be a
     structure of the form
	try {
	  try {
	    throw ...;
	  } cleanup {
	    try {
	      throw ...;
	    } catch (...) {
	    }
	  }
	} catch (...) {
	  yyy;
	}
    E.g. with an inline destructor with an embedded try block.  In this
    case we must save the runtime EH data around the nested exception.

    This complication means that any time the previous runtime data might
    be used (via fallthru from the finally) we handle the eh case here,
    whether or not protect_cleanup_actions is active.  */

  finally_may_fallthru = block_may_fallthru (finally);
  if (!finally_may_fallthru && !protect_cleanup_actions)
    return;

  /* Duplicate the FINALLY block.  Only need to do this for try-finally,
     and not for cleanups.  */
  if (this_state)
    finally = lower_try_finally_dup_block (finally, outer_state);

  /* Resume execution after the exception.  Adding this now lets
     lower_eh_filter not add unnecessary gotos, as it is clear that
     we never fallthru from this copy of the finally block.  */
  if (finally_may_fallthru)
    {
      tree save_eptr, save_filt;

      save_eptr = create_tmp_var (ptr_type_node, "save_eptr");
      save_filt = create_tmp_var (integer_type_node, "save_filt");

      i = tsi_start (finally);
      x = build0 (EXC_PTR_EXPR, ptr_type_node);
      x = build2 (MODIFY_EXPR, void_type_node, save_eptr, x);
      tsi_link_before (&i, x, TSI_CONTINUE_LINKING);

      x = build0 (FILTER_EXPR, integer_type_node);
      x = build2 (MODIFY_EXPR, void_type_node, save_filt, x);
      tsi_link_before (&i, x, TSI_CONTINUE_LINKING);

      i = tsi_last (finally);
      x = build0 (EXC_PTR_EXPR, ptr_type_node);
      x = build2 (MODIFY_EXPR, void_type_node, x, save_eptr);
      tsi_link_after (&i, x, TSI_CONTINUE_LINKING);

      x = build0 (FILTER_EXPR, integer_type_node);
      x = build2 (MODIFY_EXPR, void_type_node, x, save_filt);
      tsi_link_after (&i, x, TSI_CONTINUE_LINKING);

      x = build_resx (get_eh_region_number (tf->region));
      tsi_link_after (&i, x, TSI_CONTINUE_LINKING);
    }

  /* Wrap the block with protect_cleanup_actions as the action.  */
  if (protect_cleanup_actions)
    {
      x = build2 (EH_FILTER_EXPR, void_type_node, NULL, NULL);
      append_to_statement_list (protect_cleanup_actions, &EH_FILTER_FAILURE (x));
      EH_FILTER_MUST_NOT_THROW (x) = 1;
      finally = build2 (TRY_CATCH_EXPR, void_type_node, finally, x);
      lower_eh_filter (outer_state, &finally);
    }
  else
    lower_eh_constructs_1 (outer_state, &finally);

  /* Hook this up to the end of the existing try block.  If we
     previously fell through the end, we'll have to branch around.
     This means adding a new goto, and adding it to the queue.  */

  i = tsi_last (TREE_OPERAND (*tf->top_p, 0));

  if (tf->may_fallthru)
    {
      x = lower_try_finally_fallthru_label (tf);
      x = build1 (GOTO_EXPR, void_type_node, x);
      tsi_link_after (&i, x, TSI_CONTINUE_LINKING);

      if (this_state)
        maybe_record_in_goto_queue (this_state, x);

      tf->may_fallthru = false;
    }

  x = build1 (LABEL_EXPR, void_type_node, tf->eh_label);
  tsi_link_after (&i, x, TSI_CONTINUE_LINKING);
  tsi_link_after (&i, finally, TSI_CONTINUE_LINKING);

  /* Having now been handled, EH isn't to be considered with
     the rest of the outgoing edges.  */
  tf->may_throw = false;
}

/* A subroutine of lower_try_finally.  We have determined that there is
   no fallthru edge out of the finally block.  This means that there is
   no outgoing edge corresponding to any incoming edge.  Restructure the
   try_finally node for this special case.  */

static void
lower_try_finally_nofallthru (struct leh_state *state, struct leh_tf_state *tf)
{
  tree x, finally, lab, return_val;
  struct goto_queue_node *q, *qe;

  if (tf->may_throw)
    lab = tf->eh_label;
  else
    lab = create_artificial_label ();

  finally = TREE_OPERAND (*tf->top_p, 1);
  *tf->top_p = TREE_OPERAND (*tf->top_p, 0);

  x = build1 (LABEL_EXPR, void_type_node, lab);
  append_to_statement_list (x, tf->top_p);

  return_val = NULL;
  q = tf->goto_queue;
  qe = q + tf->goto_queue_active;
  for (; q < qe; ++q)
    if (q->index < 0)
      do_return_redirection (q, lab, NULL, &return_val);
    else
      do_goto_redirection (q, lab, NULL);

  replace_goto_queue (tf);

  lower_eh_constructs_1 (state, &finally);
  append_to_statement_list (finally, tf->top_p);
}

/* A subroutine of lower_try_finally.  We have determined that there is
   exactly one destination of the finally block.  Restructure the
   try_finally node for this special case.  */

static void
lower_try_finally_onedest (struct leh_state *state, struct leh_tf_state *tf)
{
  struct goto_queue_node *q, *qe;
  tree x, finally, finally_label;

  finally = TREE_OPERAND (*tf->top_p, 1);
  *tf->top_p = TREE_OPERAND (*tf->top_p, 0);

  lower_eh_constructs_1 (state, &finally);

  if (tf->may_throw)
    {
      /* Only reachable via the exception edge.  Add the given label to
         the head of the FINALLY block.  Append a RESX at the end.  */

      x = build1 (LABEL_EXPR, void_type_node, tf->eh_label);
      append_to_statement_list (x, tf->top_p);

      append_to_statement_list (finally, tf->top_p);

      x = build_resx (get_eh_region_number (tf->region));

      append_to_statement_list (x, tf->top_p);

      return;
    }

  if (tf->may_fallthru)
    {
      /* Only reachable via the fallthru edge.  Do nothing but let
	 the two blocks run together; we'll fall out the bottom.  */
      append_to_statement_list (finally, tf->top_p);
      return;
    }

  finally_label = create_artificial_label ();
  x = build1 (LABEL_EXPR, void_type_node, finally_label);
  append_to_statement_list (x, tf->top_p);

  append_to_statement_list (finally, tf->top_p);

  q = tf->goto_queue;
  qe = q + tf->goto_queue_active;

  if (tf->may_return)
    {
      /* Reachable by return expressions only.  Redirect them.  */
      tree return_val = NULL;
      for (; q < qe; ++q)
	do_return_redirection (q, finally_label, NULL, &return_val);
      replace_goto_queue (tf);
    }
  else
    {
      /* Reachable by goto expressions only.  Redirect them.  */
      for (; q < qe; ++q)
	do_goto_redirection (q, finally_label, NULL);
      replace_goto_queue (tf);

      if (VEC_index (tree, tf->dest_array, 0) == tf->fallthru_label)
	{
	  /* Reachable by goto to fallthru label only.  Redirect it
	     to the new label (already created, sadly), and do not
	     emit the final branch out, or the fallthru label.  */
	  tf->fallthru_label = NULL;
	  return;
	}
    }

  append_to_statement_list (tf->goto_queue[0].cont_stmt, tf->top_p);
  maybe_record_in_goto_queue (state, tf->goto_queue[0].cont_stmt);
}

/* A subroutine of lower_try_finally.  There are multiple edges incoming
   and outgoing from the finally block.  Implement this by duplicating the
   finally block for every destination.  */

static void
lower_try_finally_copy (struct leh_state *state, struct leh_tf_state *tf)
{
  tree finally, new_stmt;
  tree x;

  finally = TREE_OPERAND (*tf->top_p, 1);
  *tf->top_p = TREE_OPERAND (*tf->top_p, 0);

  new_stmt = NULL_TREE;

  if (tf->may_fallthru)
    {
      x = lower_try_finally_dup_block (finally, state);
      lower_eh_constructs_1 (state, &x);
      append_to_statement_list (x, &new_stmt);

      x = lower_try_finally_fallthru_label (tf);
      x = build1 (GOTO_EXPR, void_type_node, x);
      append_to_statement_list (x, &new_stmt);
    }

  if (tf->may_throw)
    {
      x = build1 (LABEL_EXPR, void_type_node, tf->eh_label);
      append_to_statement_list (x, &new_stmt);

      x = lower_try_finally_dup_block (finally, state);
      lower_eh_constructs_1 (state, &x);
      append_to_statement_list (x, &new_stmt);

      x = build_resx (get_eh_region_number (tf->region));
      append_to_statement_list (x, &new_stmt);
    }

  if (tf->goto_queue)
    {
      struct goto_queue_node *q, *qe;
      tree return_val = NULL;
      int return_index, index;
      struct labels_s
      {
	struct goto_queue_node *q;
	tree label;
      } *labels;

      return_index = VEC_length (tree, tf->dest_array);
      labels = XCNEWVEC (struct labels_s, return_index + 1);

      q = tf->goto_queue;
      qe = q + tf->goto_queue_active;
      for (; q < qe; q++)
	{
	  index = q->index < 0 ? return_index : q->index;

	  if (!labels[index].q)
	    labels[index].q = q;
	}

      for (index = 0; index < return_index + 1; index++)
	{
	  tree lab;

	  q = labels[index].q;
	  if (! q)
	    continue;

	  lab = labels[index].label = create_artificial_label ();

	  if (index == return_index)
	    do_return_redirection (q, lab, NULL, &return_val);
	  else
	    do_goto_redirection (q, lab, NULL);

	  x = build1 (LABEL_EXPR, void_type_node, lab);
	  append_to_statement_list (x, &new_stmt);

	  x = lower_try_finally_dup_block (finally, state);
	  lower_eh_constructs_1 (state, &x);
	  append_to_statement_list (x, &new_stmt);

	  append_to_statement_list (q->cont_stmt, &new_stmt);
	  maybe_record_in_goto_queue (state, q->cont_stmt);
	}

      for (q = tf->goto_queue; q < qe; q++)
	{
	  tree lab;

	  index = q->index < 0 ? return_index : q->index;

	  if (labels[index].q == q)
	    continue;

	  lab = labels[index].label;

	  if (index == return_index)
	    do_return_redirection (q, lab, NULL, &return_val);
	  else
	    do_goto_redirection (q, lab, NULL);
	}
	
      replace_goto_queue (tf);
      free (labels);
    }

  /* Need to link new stmts after running replace_goto_queue due
     to not wanting to process the same goto stmts twice.  */
  append_to_statement_list (new_stmt, tf->top_p);
}

/* A subroutine of lower_try_finally.  There are multiple edges incoming
   and outgoing from the finally block.  Implement this by instrumenting
   each incoming edge and creating a switch statement at the end of the
   finally block that branches to the appropriate destination.  */

static void
lower_try_finally_switch (struct leh_state *state, struct leh_tf_state *tf)
{
  struct goto_queue_node *q, *qe;
  tree return_val = NULL;
  tree finally, finally_tmp, finally_label;
  int return_index, eh_index, fallthru_index;
  int nlabels, ndests, j, last_case_index;
  tree case_label_vec, switch_stmt, last_case, switch_body;
  tree x;

  /* Mash the TRY block to the head of the chain.  */
  finally = TREE_OPERAND (*tf->top_p, 1);
  *tf->top_p = TREE_OPERAND (*tf->top_p, 0);

  /* Lower the finally block itself.  */
  lower_eh_constructs_1 (state, &finally);

  /* Prepare for switch statement generation.  */
  nlabels = VEC_length (tree, tf->dest_array);
  return_index = nlabels;
  eh_index = return_index + tf->may_return;
  fallthru_index = eh_index + tf->may_throw;
  ndests = fallthru_index + tf->may_fallthru;

  finally_tmp = create_tmp_var (integer_type_node, "finally_tmp");
  finally_label = create_artificial_label ();

  case_label_vec = make_tree_vec (ndests);
  switch_stmt = build3 (SWITCH_EXPR, integer_type_node, finally_tmp,
		        NULL_TREE, case_label_vec);
  switch_body = NULL;
  last_case = NULL;
  last_case_index = 0;

  /* Begin inserting code for getting to the finally block.  Things
     are done in this order to correspond to the sequence the code is
     layed out.  */

  if (tf->may_fallthru)
    {
      x = build2 (MODIFY_EXPR, void_type_node, finally_tmp,
		  build_int_cst (NULL_TREE, fallthru_index));
      append_to_statement_list (x, tf->top_p);

      if (tf->may_throw)
	{
	  x = build1 (GOTO_EXPR, void_type_node, finally_label);
	  append_to_statement_list (x, tf->top_p);
	}


      last_case = build3 (CASE_LABEL_EXPR, void_type_node,
			  build_int_cst (NULL_TREE, fallthru_index), NULL,
			  create_artificial_label ());
      TREE_VEC_ELT (case_label_vec, last_case_index) = last_case;
      last_case_index++;

      x = build1 (LABEL_EXPR, void_type_node, CASE_LABEL (last_case));
      append_to_statement_list (x, &switch_body);

      x = lower_try_finally_fallthru_label (tf);
      x = build1 (GOTO_EXPR, void_type_node, x);
      append_to_statement_list (x, &switch_body);
    }

  if (tf->may_throw)
    {
      x = build1 (LABEL_EXPR, void_type_node, tf->eh_label);
      append_to_statement_list (x, tf->top_p);

      x = build2 (MODIFY_EXPR, void_type_node, finally_tmp,
		  build_int_cst (NULL_TREE, eh_index));
      append_to_statement_list (x, tf->top_p);

      last_case = build3 (CASE_LABEL_EXPR, void_type_node,
			  build_int_cst (NULL_TREE, eh_index), NULL,
			  create_artificial_label ());
      TREE_VEC_ELT (case_label_vec, last_case_index) = last_case;
      last_case_index++;

      x = build1 (LABEL_EXPR, void_type_node, CASE_LABEL (last_case));
      append_to_statement_list (x, &switch_body);
      x = build_resx (get_eh_region_number (tf->region));
      append_to_statement_list (x, &switch_body);
    }

  x = build1 (LABEL_EXPR, void_type_node, finally_label);
  append_to_statement_list (x, tf->top_p);

  append_to_statement_list (finally, tf->top_p);

  /* Redirect each incoming goto edge.  */
  q = tf->goto_queue;
  qe = q + tf->goto_queue_active;
  j = last_case_index + tf->may_return;
  for (; q < qe; ++q)
    {
      tree mod;
      int switch_id, case_index;

      if (q->index < 0)
	{
	  mod = build2 (MODIFY_EXPR, void_type_node, finally_tmp,
		        build_int_cst (NULL_TREE, return_index));
	  do_return_redirection (q, finally_label, mod, &return_val);
	  switch_id = return_index;
	}
      else
	{
	  mod = build2 (MODIFY_EXPR, void_type_node, finally_tmp,
		        build_int_cst (NULL_TREE, q->index));
	  do_goto_redirection (q, finally_label, mod);
	  switch_id = q->index;
	}

      case_index = j + q->index;
      if (!TREE_VEC_ELT (case_label_vec, case_index))
	TREE_VEC_ELT (case_label_vec, case_index)
	  = build3 (CASE_LABEL_EXPR, void_type_node,
		    build_int_cst (NULL_TREE, switch_id), NULL,
		    /* We store the cont_stmt in the
		       CASE_LABEL, so that we can recover it
		       in the loop below.  We don't create
		       the new label while walking the
		       goto_queue because pointers don't
		       offer a stable order.  */
		    q->cont_stmt);
    }
  for (j = last_case_index; j < last_case_index + nlabels; j++)
    {
      tree label;
      tree cont_stmt;

      last_case = TREE_VEC_ELT (case_label_vec, j);

      gcc_assert (last_case);

      cont_stmt = CASE_LABEL (last_case);

      label = create_artificial_label ();
      CASE_LABEL (last_case) = label;

      x = build1 (LABEL_EXPR, void_type_node, label);
      append_to_statement_list (x, &switch_body);
      append_to_statement_list (cont_stmt, &switch_body);
      maybe_record_in_goto_queue (state, cont_stmt);
    }
  replace_goto_queue (tf);

  /* Make sure that the last case is the default label, as one is required.
     Then sort the labels, which is also required in GIMPLE.  */
  CASE_LOW (last_case) = NULL;
  sort_case_labels (case_label_vec);

  /* Need to link switch_stmt after running replace_goto_queue due
     to not wanting to process the same goto stmts twice.  */
  append_to_statement_list (switch_stmt, tf->top_p);
  append_to_statement_list (switch_body, tf->top_p);
}

/* Decide whether or not we are going to duplicate the finally block.
   There are several considerations.

   First, if this is Java, then the finally block contains code
   written by the user.  It has line numbers associated with it,
   so duplicating the block means it's difficult to set a breakpoint.
   Since controlling code generation via -g is verboten, we simply
   never duplicate code without optimization.

   Second, we'd like to prevent egregious code growth.  One way to
   do this is to estimate the size of the finally block, multiply
   that by the number of copies we'd need to make, and compare against
   the estimate of the size of the switch machinery we'd have to add.  */

static bool
decide_copy_try_finally (int ndests, tree finally)
{
  int f_estimate, sw_estimate;

  if (!optimize)
    return false;

  /* Finally estimate N times, plus N gotos.  */
  f_estimate = estimate_num_insns (finally);
  f_estimate = (f_estimate + 1) * ndests;

  /* Switch statement (cost 10), N variable assignments, N gotos.  */
  sw_estimate = 10 + 2 * ndests;

  /* Optimize for size clearly wants our best guess.  */
  if (optimize_size)
    return f_estimate < sw_estimate;

  /* ??? These numbers are completely made up so far.  */
  if (optimize > 1)
    return f_estimate < 100 || f_estimate < sw_estimate * 2;
  else
    return f_estimate < 40 || f_estimate * 2 < sw_estimate * 3;
}

/* A subroutine of lower_eh_constructs_1.  Lower a TRY_FINALLY_EXPR nodes
   to a sequence of labels and blocks, plus the exception region trees
   that record all the magic.  This is complicated by the need to
   arrange for the FINALLY block to be executed on all exits.  */

static void
lower_try_finally (struct leh_state *state, tree *tp)
{
  struct leh_tf_state this_tf;
  struct leh_state this_state;
  int ndests;

  /* Process the try block.  */

  memset (&this_tf, 0, sizeof (this_tf));
  this_tf.try_finally_expr = *tp;
  this_tf.top_p = tp;
  this_tf.outer = state;
  if (using_eh_for_cleanups_p)
    this_tf.region
      = gen_eh_region_cleanup (state->cur_region, state->prev_try);
  else
    this_tf.region = NULL;

  this_state.cur_region = this_tf.region;
  this_state.prev_try = state->prev_try;
  this_state.tf = &this_tf;

  lower_eh_constructs_1 (&this_state, &TREE_OPERAND (*tp, 0));

  /* Determine if the try block is escaped through the bottom.  */
  this_tf.may_fallthru = block_may_fallthru (TREE_OPERAND (*tp, 0));

  /* Determine if any exceptions are possible within the try block.  */
  if (using_eh_for_cleanups_p)
    this_tf.may_throw = get_eh_region_may_contain_throw (this_tf.region);
  if (this_tf.may_throw)
    {
      this_tf.eh_label = create_artificial_label ();
      set_eh_region_tree_label (this_tf.region, this_tf.eh_label);
      honor_protect_cleanup_actions (state, &this_state, &this_tf);
    }

  /* Sort the goto queue for efficient searching later.  */
  if (this_tf.goto_queue_active > 1)
    qsort (this_tf.goto_queue, this_tf.goto_queue_active,
	   sizeof (struct goto_queue_node), goto_queue_cmp);

  /* Determine how many edges (still) reach the finally block.  Or rather,
     how many destinations are reached by the finally block.  Use this to
     determine how we process the finally block itself.  */

  ndests = VEC_length (tree, this_tf.dest_array);
  ndests += this_tf.may_fallthru;
  ndests += this_tf.may_return;
  ndests += this_tf.may_throw;

  /* If the FINALLY block is not reachable, dike it out.  */
  if (ndests == 0)
    *tp = TREE_OPERAND (*tp, 0);

  /* If the finally block doesn't fall through, then any destination
     we might try to impose there isn't reached either.  There may be
     some minor amount of cleanup and redirection still needed.  */
  else if (!block_may_fallthru (TREE_OPERAND (*tp, 1)))
    lower_try_finally_nofallthru (state, &this_tf);

  /* We can easily special-case redirection to a single destination.  */
  else if (ndests == 1)
    lower_try_finally_onedest (state, &this_tf);

  else if (decide_copy_try_finally (ndests, TREE_OPERAND (*tp, 1)))
    lower_try_finally_copy (state, &this_tf);
  else
    lower_try_finally_switch (state, &this_tf);

  /* If someone requested we add a label at the end of the transformed
     block, do so.  */
  if (this_tf.fallthru_label)
    {
      tree x = build1 (LABEL_EXPR, void_type_node, this_tf.fallthru_label);
      append_to_statement_list (x, tp);
    }

  VEC_free (tree, heap, this_tf.dest_array);
  if (this_tf.goto_queue)
    free (this_tf.goto_queue);
}

/* A subroutine of lower_eh_constructs_1.  Lower a TRY_CATCH_EXPR with a
   list of CATCH_EXPR nodes to a sequence of labels and blocks, plus the
   exception region trees that record all the magic.  */

static void
lower_catch (struct leh_state *state, tree *tp)
{
  struct eh_region *try_region;
  struct leh_state this_state;
  tree_stmt_iterator i;
  tree out_label;

  try_region = gen_eh_region_try (state->cur_region);
  this_state.cur_region = try_region;
  this_state.prev_try = try_region;
  this_state.tf = state->tf;

  lower_eh_constructs_1 (&this_state, &TREE_OPERAND (*tp, 0));

  if (!get_eh_region_may_contain_throw (try_region))
    {
      *tp = TREE_OPERAND (*tp, 0);
      return;
    }

  out_label = NULL;
  for (i = tsi_start (TREE_OPERAND (*tp, 1)); !tsi_end_p (i); )
    {
      struct eh_region *catch_region;
      tree catch, x, eh_label;

      catch = tsi_stmt (i);
      catch_region = gen_eh_region_catch (try_region, CATCH_TYPES (catch));

      this_state.cur_region = catch_region;
      this_state.prev_try = state->prev_try;
      lower_eh_constructs_1 (&this_state, &CATCH_BODY (catch));

      eh_label = create_artificial_label ();
      set_eh_region_tree_label (catch_region, eh_label);

      x = build1 (LABEL_EXPR, void_type_node, eh_label);
      tsi_link_before (&i, x, TSI_SAME_STMT);

      if (block_may_fallthru (CATCH_BODY (catch)))
	{
	  if (!out_label)
	    out_label = create_artificial_label ();

	  x = build1 (GOTO_EXPR, void_type_node, out_label);
	  append_to_statement_list (x, &CATCH_BODY (catch));
	}

      tsi_link_before (&i, CATCH_BODY (catch), TSI_SAME_STMT);
      tsi_delink (&i);
    }

  frob_into_branch_around (tp, NULL, out_label);
}

/* A subroutine of lower_eh_constructs_1.  Lower a TRY_CATCH_EXPR with a
   EH_FILTER_EXPR to a sequence of labels and blocks, plus the exception
   region trees that record all the magic.  */

static void
lower_eh_filter (struct leh_state *state, tree *tp)
{
  struct leh_state this_state;
  struct eh_region *this_region;
  tree inner = expr_first (TREE_OPERAND (*tp, 1));
  tree eh_label;

  if (EH_FILTER_MUST_NOT_THROW (inner))
    this_region = gen_eh_region_must_not_throw (state->cur_region);
  else
    this_region = gen_eh_region_allowed (state->cur_region,
					 EH_FILTER_TYPES (inner));
  this_state = *state;
  this_state.cur_region = this_region;
  /* For must not throw regions any cleanup regions inside it
     can't reach outer catch regions.  */
  if (EH_FILTER_MUST_NOT_THROW (inner))
    this_state.prev_try = NULL;

  lower_eh_constructs_1 (&this_state, &TREE_OPERAND (*tp, 0));

  if (!get_eh_region_may_contain_throw (this_region))
    {
      *tp = TREE_OPERAND (*tp, 0);
      return;
    }

  lower_eh_constructs_1 (state, &EH_FILTER_FAILURE (inner));
  TREE_OPERAND (*tp, 1) = EH_FILTER_FAILURE (inner);

  eh_label = create_artificial_label ();
  set_eh_region_tree_label (this_region, eh_label);

  frob_into_branch_around (tp, eh_label, NULL);
}

/* Implement a cleanup expression.  This is similar to try-finally,
   except that we only execute the cleanup block for exception edges.  */

static void
lower_cleanup (struct leh_state *state, tree *tp)
{
  struct leh_state this_state;
  struct eh_region *this_region;
  struct leh_tf_state fake_tf;

  /* If not using eh, then exception-only cleanups are no-ops.  */
  if (!flag_exceptions)
    {
      *tp = TREE_OPERAND (*tp, 0);
      lower_eh_constructs_1 (state, tp);
      return;
    }

  this_region = gen_eh_region_cleanup (state->cur_region, state->prev_try);
  this_state = *state;
  this_state.cur_region = this_region;

  lower_eh_constructs_1 (&this_state, &TREE_OPERAND (*tp, 0));

  if (!get_eh_region_may_contain_throw (this_region))
    {
      *tp = TREE_OPERAND (*tp, 0);
      return;
    }

  /* Build enough of a try-finally state so that we can reuse
     honor_protect_cleanup_actions.  */
  memset (&fake_tf, 0, sizeof (fake_tf));
  fake_tf.top_p = tp;
  fake_tf.outer = state;
  fake_tf.region = this_region;
  fake_tf.may_fallthru = block_may_fallthru (TREE_OPERAND (*tp, 0));
  fake_tf.may_throw = true;

  fake_tf.eh_label = create_artificial_label ();
  set_eh_region_tree_label (this_region, fake_tf.eh_label);

  honor_protect_cleanup_actions (state, NULL, &fake_tf);

  if (fake_tf.may_throw)
    {
      /* In this case honor_protect_cleanup_actions had nothing to do,
	 and we should process this normally.  */
      lower_eh_constructs_1 (state, &TREE_OPERAND (*tp, 1));
      frob_into_branch_around (tp, fake_tf.eh_label, fake_tf.fallthru_label);
    }
  else
    {
      /* In this case honor_protect_cleanup_actions did nearly all of
	 the work.  All we have left is to append the fallthru_label.  */

      *tp = TREE_OPERAND (*tp, 0);
      if (fake_tf.fallthru_label)
	{
	  tree x = build1 (LABEL_EXPR, void_type_node, fake_tf.fallthru_label);
	  append_to_statement_list (x, tp);
	}
    }
}

/* Main loop for lowering eh constructs.  */

static void
lower_eh_constructs_1 (struct leh_state *state, tree *tp)
{
  tree_stmt_iterator i;
  tree t = *tp;

  switch (TREE_CODE (t))
    {
    case COND_EXPR:
      lower_eh_constructs_1 (state, &COND_EXPR_THEN (t));
      lower_eh_constructs_1 (state, &COND_EXPR_ELSE (t));
      break;

    case CALL_EXPR:
      /* Look for things that can throw exceptions, and record them.  */
      if (state->cur_region && tree_could_throw_p (t))
	{
	  record_stmt_eh_region (state->cur_region, t);
	  note_eh_region_may_contain_throw (state->cur_region);
	}
      break;

    case MODIFY_EXPR:
      /* Look for things that can throw exceptions, and record them.  */
      if (state->cur_region && tree_could_throw_p (t))
	{
	  record_stmt_eh_region (state->cur_region, t);
	  note_eh_region_may_contain_throw (state->cur_region);
	}
      break;

    case GOTO_EXPR:
    case RETURN_EXPR:
      maybe_record_in_goto_queue (state, t);
      break;
    case SWITCH_EXPR:
      verify_norecord_switch_expr (state, t);
      break;

    case TRY_FINALLY_EXPR:
      lower_try_finally (state, tp);
      break;

    case TRY_CATCH_EXPR:
      i = tsi_start (TREE_OPERAND (t, 1));
      switch (TREE_CODE (tsi_stmt (i)))
	{
	case CATCH_EXPR:
	  lower_catch (state, tp);
	  break;
	case EH_FILTER_EXPR:
	  lower_eh_filter (state, tp);
	  break;
	default:
	  lower_cleanup (state, tp);
	  break;
	}
      break;

    case STATEMENT_LIST:
      for (i = tsi_start (t); !tsi_end_p (i); )
	{
	  lower_eh_constructs_1 (state, tsi_stmt_ptr (i));
	  t = tsi_stmt (i);
	  if (TREE_CODE (t) == STATEMENT_LIST)
	    {
	      tsi_link_before (&i, t, TSI_SAME_STMT);
	      tsi_delink (&i);
	    }
	  else
	    tsi_next (&i);
	}
      break;

    default:
      /* A type, a decl, or some kind of statement that we're not
	 interested in.  Don't walk them.  */
      break;
    }
}

static unsigned int
lower_eh_constructs (void)
{
  struct leh_state null_state;
  tree *tp = &DECL_SAVED_TREE (current_function_decl);

  finally_tree = htab_create (31, struct_ptr_hash, struct_ptr_eq, free);

  collect_finally_tree (*tp, NULL);

  memset (&null_state, 0, sizeof (null_state));
  lower_eh_constructs_1 (&null_state, tp);

  htab_delete (finally_tree);

  collect_eh_region_array ();
  return 0;
}

struct tree_opt_pass pass_lower_eh =
{
  "eh",					/* name */
  NULL,					/* gate */
  lower_eh_constructs,			/* execute */
  NULL,					/* sub */
  NULL,					/* next */
  0,					/* static_pass_number */
  TV_TREE_EH,				/* tv_id */
  PROP_gimple_lcf,			/* properties_required */
  PROP_gimple_leh,			/* properties_provided */
  0,					/* properties_destroyed */
  0,					/* todo_flags_start */
  TODO_dump_func,			/* todo_flags_finish */
  0					/* letter */
};


/* Construct EH edges for STMT.  */

static void
make_eh_edge (struct eh_region *region, void *data)
{
  tree stmt, lab;
  basic_block src, dst;

  stmt = (tree) data;
  lab = get_eh_region_tree_label (region);

  src = bb_for_stmt (stmt);
  dst = label_to_block (lab);

  make_edge (src, dst, EDGE_ABNORMAL | EDGE_EH);
}

void
make_eh_edges (tree stmt)
{
  int region_nr;
  bool is_resx;

  if (TREE_CODE (stmt) == RESX_EXPR)
    {
      region_nr = TREE_INT_CST_LOW (TREE_OPERAND (stmt, 0));
      is_resx = true;
    }
  else
    {
      region_nr = lookup_stmt_eh_region (stmt);
      if (region_nr < 0)
	return;
      is_resx = false;
    }

  foreach_reachable_handler (region_nr, is_resx, make_eh_edge, stmt);
}

static bool mark_eh_edge_found_error;

/* Mark edge make_eh_edge would create for given region by setting it aux
   field, output error if something goes wrong.  */
static void
mark_eh_edge (struct eh_region *region, void *data)
{
  tree stmt, lab;
  basic_block src, dst;
  edge e;

  stmt = (tree) data;
  lab = get_eh_region_tree_label (region);

  src = bb_for_stmt (stmt);
  dst = label_to_block (lab);

  e = find_edge (src, dst);
  if (!e)
    {
      error ("EH edge %i->%i is missing", src->index, dst->index);
      mark_eh_edge_found_error = true;
    }
  else if (!(e->flags & EDGE_EH))
    {
      error ("EH edge %i->%i miss EH flag", src->index, dst->index);
      mark_eh_edge_found_error = true;
    }
  else if (e->aux)
    {
      /* ??? might not be mistake.  */
      error ("EH edge %i->%i has duplicated regions", src->index, dst->index);
      mark_eh_edge_found_error = true;
    }
  else
    e->aux = (void *)1;
}

/* Verify that BB containing stmt as last stmt has precisely the edges
   make_eh_edges would create.  */
bool
verify_eh_edges (tree stmt)
{
  int region_nr;
  bool is_resx;
  basic_block bb = bb_for_stmt (stmt);
  edge_iterator ei;
  edge e;

  FOR_EACH_EDGE (e, ei, bb->succs)
    gcc_assert (!e->aux);
  mark_eh_edge_found_error = false;
  if (TREE_CODE (stmt) == RESX_EXPR)
    {
      region_nr = TREE_INT_CST_LOW (TREE_OPERAND (stmt, 0));
      is_resx = true;
    }
  else
    {
      region_nr = lookup_stmt_eh_region (stmt);
      if (region_nr < 0)
	{
	  FOR_EACH_EDGE (e, ei, bb->succs)
	    if (e->flags & EDGE_EH)
	      {
		error ("BB %i can not throw but has EH edges", bb->index);
		return true;
	      }
	   return false;
	}
      if (!tree_could_throw_p (stmt))
	{
	  error ("BB %i last statement has incorrectly set region", bb->index);
	  return true;
	}
      is_resx = false;
    }

  foreach_reachable_handler (region_nr, is_resx, mark_eh_edge, stmt);
  FOR_EACH_EDGE (e, ei, bb->succs)
    {
      if ((e->flags & EDGE_EH) && !e->aux)
	{
	  error ("unnecessary EH edge %i->%i", bb->index, e->dest->index);
	  mark_eh_edge_found_error = true;
	  return true;
	}
      e->aux = NULL;
    }
  return mark_eh_edge_found_error;
}


/* Return true if the expr can trap, as in dereferencing an invalid pointer
   location or floating point arithmetic.  C.f. the rtl version, may_trap_p.
   This routine expects only GIMPLE lhs or rhs input.  */

bool
tree_could_trap_p (tree expr)
{
  enum tree_code code = TREE_CODE (expr);
  bool honor_nans = false;
  bool honor_snans = false;
  bool fp_operation = false;
  bool honor_trapv = false;
  tree t, base;

  if (TREE_CODE_CLASS (code) == tcc_comparison
      || TREE_CODE_CLASS (code) == tcc_unary
      || TREE_CODE_CLASS (code) == tcc_binary)
    {
      t = TREE_TYPE (expr);
      fp_operation = FLOAT_TYPE_P (t);
      if (fp_operation)
	{
	  honor_nans = flag_trapping_math && !flag_finite_math_only;
	  honor_snans = flag_signaling_nans != 0;
	}
      else if (INTEGRAL_TYPE_P (t) && TYPE_OVERFLOW_TRAPS (t))
	honor_trapv = true;
    }

 restart:
  switch (code)
    {
    case TARGET_MEM_REF:
      /* For TARGET_MEM_REFs use the information based on the original
	 reference.  */
      expr = TMR_ORIGINAL (expr);
      code = TREE_CODE (expr);
      goto restart;

    case COMPONENT_REF:
    case REALPART_EXPR:
    case IMAGPART_EXPR:
    case BIT_FIELD_REF:
    case VIEW_CONVERT_EXPR:
    case WITH_SIZE_EXPR:
      expr = TREE_OPERAND (expr, 0);
      code = TREE_CODE (expr);
      goto restart;

    case ARRAY_RANGE_REF:
      base = TREE_OPERAND (expr, 0);
      if (tree_could_trap_p (base))
	return true;

      if (TREE_THIS_NOTRAP (expr))
	return false;

      return !range_in_array_bounds_p (expr);

    case ARRAY_REF:
      base = TREE_OPERAND (expr, 0);
      if (tree_could_trap_p (base))
	return true;

      if (TREE_THIS_NOTRAP (expr))
	return false;

      return !in_array_bounds_p (expr);

    case INDIRECT_REF:
    case ALIGN_INDIRECT_REF:
    case MISALIGNED_INDIRECT_REF:
      return !TREE_THIS_NOTRAP (expr);

    case ASM_EXPR:
      return TREE_THIS_VOLATILE (expr);

    case TRUNC_DIV_EXPR:
    case CEIL_DIV_EXPR:
    case FLOOR_DIV_EXPR:
    case ROUND_DIV_EXPR:
    case EXACT_DIV_EXPR:
    case CEIL_MOD_EXPR:
    case FLOOR_MOD_EXPR:
    case ROUND_MOD_EXPR:
    case TRUNC_MOD_EXPR:
    case RDIV_EXPR:
      if (honor_snans || honor_trapv)
	return true;
      if (fp_operation)
	return flag_trapping_math;
      t = TREE_OPERAND (expr, 1);
      if (!TREE_CONSTANT (t) || integer_zerop (t))
        return true;
      return false;

    case LT_EXPR:
    case LE_EXPR:
    case GT_EXPR:
    case GE_EXPR:
    case LTGT_EXPR:
      /* Some floating point comparisons may trap.  */
      return honor_nans;

    case EQ_EXPR:
    case NE_EXPR:
    case UNORDERED_EXPR:
    case ORDERED_EXPR:
    case UNLT_EXPR:
    case UNLE_EXPR:
    case UNGT_EXPR:
    case UNGE_EXPR:
    case UNEQ_EXPR:
      return honor_snans;

    case CONVERT_EXPR:
    case FIX_TRUNC_EXPR:
    case FIX_CEIL_EXPR:
    case FIX_FLOOR_EXPR:
    case FIX_ROUND_EXPR:
      /* Conversion of floating point might trap.  */
      return honor_nans;

    case NEGATE_EXPR:
    case ABS_EXPR:
    case CONJ_EXPR:
      /* These operations don't trap with floating point.  */
      if (honor_trapv)
	return true;
      return false;

    case PLUS_EXPR:
    case MINUS_EXPR:
    case MULT_EXPR:
      /* Any floating arithmetic may trap.  */
      if (fp_operation && flag_trapping_math)
	return true;
      if (honor_trapv)
	return true;
      return false;

    case CALL_EXPR:
      t = get_callee_fndecl (expr);
      /* Assume that calls to weak functions may trap.  */
      if (!t || !DECL_P (t) || DECL_WEAK (t))
	return true;
      return false;

    default:
      /* Any floating arithmetic may trap.  */
      if (fp_operation && flag_trapping_math)
	return true;
      return false;
    }
}

bool
tree_could_throw_p (tree t)
{
  if (!flag_exceptions)
    return false;
  if (TREE_CODE (t) == MODIFY_EXPR)
    {
      if (flag_non_call_exceptions
	  && tree_could_trap_p (TREE_OPERAND (t, 0)))
	return true;
      t = TREE_OPERAND (t, 1);
    }

  if (TREE_CODE (t) == WITH_SIZE_EXPR)
    t = TREE_OPERAND (t, 0);
  if (TREE_CODE (t) == CALL_EXPR)
    return (call_expr_flags (t) & ECF_NOTHROW) == 0;
  if (flag_non_call_exceptions)
    return tree_could_trap_p (t);
  return false;
}

bool
tree_can_throw_internal (tree stmt)
{
  int region_nr;
  bool is_resx = false;

  if (TREE_CODE (stmt) == RESX_EXPR)
    region_nr = TREE_INT_CST_LOW (TREE_OPERAND (stmt, 0)), is_resx = true;
  else
    region_nr = lookup_stmt_eh_region (stmt);
  if (region_nr < 0)
    return false;
  return can_throw_internal_1 (region_nr, is_resx);
}

bool
tree_can_throw_external (tree stmt)
{
  int region_nr;
  bool is_resx = false;

  if (TREE_CODE (stmt) == RESX_EXPR)
    region_nr = TREE_INT_CST_LOW (TREE_OPERAND (stmt, 0)), is_resx = true;
  else
    region_nr = lookup_stmt_eh_region (stmt);
  if (region_nr < 0)
    return tree_could_throw_p (stmt);
  else
    return can_throw_external_1 (region_nr, is_resx);
}

/* Given a statement OLD_STMT and a new statement NEW_STMT that has replaced
   OLD_STMT in the function, remove OLD_STMT from the EH table and put NEW_STMT
   in the table if it should be in there.  Return TRUE if a replacement was
   done that my require an EH edge purge.  */

bool 
maybe_clean_or_replace_eh_stmt (tree old_stmt, tree new_stmt) 
{
  int region_nr = lookup_stmt_eh_region (old_stmt);

  if (region_nr >= 0)
    {
      bool new_stmt_could_throw = tree_could_throw_p (new_stmt);

      if (new_stmt == old_stmt && new_stmt_could_throw)
	return false;

      remove_stmt_from_eh_region (old_stmt);
      if (new_stmt_could_throw)
	{
	  add_stmt_to_eh_region (new_stmt, region_nr);
	  return false;
	}
      else
	return true;
    }

  return false;
}

#ifdef ENABLE_CHECKING
static int
verify_eh_throw_stmt_node (void **slot, void *data ATTRIBUTE_UNUSED)
{
  struct throw_stmt_node *node = (struct throw_stmt_node *)*slot;

  gcc_assert (node->stmt->common.ann == NULL);
  return 1;
}

void
verify_eh_throw_table_statements (void)
{
  if (!get_eh_throw_stmt_table (cfun))
    return;
  htab_traverse (get_eh_throw_stmt_table (cfun),
		 verify_eh_throw_stmt_node,
		 NULL);
}

#endif
