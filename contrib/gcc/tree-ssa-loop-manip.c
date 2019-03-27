/* High-level loop manipulation functions.
   Copyright (C) 2004, 2005, 2006, 2007 Free Software Foundation, Inc.
   
This file is part of GCC.
   
GCC is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 2, or (at your option) any
later version.
   
GCC is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
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
#include "tm_p.h"
#include "hard-reg-set.h"
#include "basic-block.h"
#include "output.h"
#include "diagnostic.h"
#include "tree-flow.h"
#include "tree-dump.h"
#include "timevar.h"
#include "cfgloop.h"
#include "tree-pass.h"
#include "cfglayout.h"
#include "tree-scalar-evolution.h"
#include "params.h"

/* Creates an induction variable with value BASE + STEP * iteration in LOOP.
   It is expected that neither BASE nor STEP are shared with other expressions
   (unless the sharing rules allow this).  Use VAR as a base var_decl for it
   (if NULL, a new temporary will be created).  The increment will occur at
   INCR_POS (after it if AFTER is true, before it otherwise).  INCR_POS and 
   AFTER can be computed using standard_iv_increment_position.  The ssa versions
   of the variable before and after increment will be stored in VAR_BEFORE and
   VAR_AFTER (unless they are NULL).  */

void
create_iv (tree base, tree step, tree var, struct loop *loop,
	   block_stmt_iterator *incr_pos, bool after,
	   tree *var_before, tree *var_after)
{
  tree stmt, initial, step1, stmts;
  tree vb, va;
  enum tree_code incr_op = PLUS_EXPR;
  edge pe = loop_preheader_edge (loop);

  if (!var)
    {
      var = create_tmp_var (TREE_TYPE (base), "ivtmp");
      add_referenced_var (var);
    }

  vb = make_ssa_name (var, NULL_TREE);
  if (var_before)
    *var_before = vb;
  va = make_ssa_name (var, NULL_TREE);
  if (var_after)
    *var_after = va;

  /* For easier readability of the created code, produce MINUS_EXPRs
     when suitable.  */
  if (TREE_CODE (step) == INTEGER_CST)
    {
      if (TYPE_UNSIGNED (TREE_TYPE (step)))
	{
	  step1 = fold_build1 (NEGATE_EXPR, TREE_TYPE (step), step);
	  if (tree_int_cst_lt (step1, step))
	    {
	      incr_op = MINUS_EXPR;
	      step = step1;
	    }
	}
      else
	{
	  bool ovf;

	  if (!tree_expr_nonnegative_warnv_p (step, &ovf)
	      && may_negate_without_overflow_p (step))
	    {
	      incr_op = MINUS_EXPR;
	      step = fold_build1 (NEGATE_EXPR, TREE_TYPE (step), step);
	    }
	}
    }

  /* Gimplify the step if necessary.  We put the computations in front of the
     loop (i.e. the step should be loop invariant).  */
  step = force_gimple_operand (step, &stmts, true, var);
  if (stmts)
    bsi_insert_on_edge_immediate_loop (pe, stmts);

  stmt = build2 (MODIFY_EXPR, void_type_node, va,
		 build2 (incr_op, TREE_TYPE (base),
			 vb, step));
  SSA_NAME_DEF_STMT (va) = stmt;
  if (after)
    bsi_insert_after (incr_pos, stmt, BSI_NEW_STMT);
  else
    bsi_insert_before (incr_pos, stmt, BSI_NEW_STMT);

  initial = force_gimple_operand (base, &stmts, true, var);
  if (stmts)
    bsi_insert_on_edge_immediate_loop (pe, stmts);

  stmt = create_phi_node (vb, loop->header);
  SSA_NAME_DEF_STMT (vb) = stmt;
  add_phi_arg (stmt, initial, loop_preheader_edge (loop));
  add_phi_arg (stmt, va, loop_latch_edge (loop));
}

/* Add exit phis for the USE on EXIT.  */

static void
add_exit_phis_edge (basic_block exit, tree use)
{
  tree phi, def_stmt = SSA_NAME_DEF_STMT (use);
  basic_block def_bb = bb_for_stmt (def_stmt);
  struct loop *def_loop;
  edge e;
  edge_iterator ei;

  /* Check that some of the edges entering the EXIT block exits a loop in
     that USE is defined.  */
  FOR_EACH_EDGE (e, ei, exit->preds)
    {
      def_loop = find_common_loop (def_bb->loop_father, e->src->loop_father);
      if (!flow_bb_inside_loop_p (def_loop, e->dest))
	break;
    }

  if (!e)
    return;

  phi = create_phi_node (use, exit);
  create_new_def_for (PHI_RESULT (phi), phi, PHI_RESULT_PTR (phi));
  FOR_EACH_EDGE (e, ei, exit->preds)
    add_phi_arg (phi, use, e);
}

/* Add exit phis for VAR that is used in LIVEIN.
   Exits of the loops are stored in EXITS.  */

static void
add_exit_phis_var (tree var, bitmap livein, bitmap exits)
{
  bitmap def;
  unsigned index;
  basic_block def_bb = bb_for_stmt (SSA_NAME_DEF_STMT (var));
  bitmap_iterator bi;

  if (is_gimple_reg (var))
    bitmap_clear_bit (livein, def_bb->index);
  else
    bitmap_set_bit (livein, def_bb->index);

  def = BITMAP_ALLOC (NULL);
  bitmap_set_bit (def, def_bb->index);
  compute_global_livein (livein, def);
  BITMAP_FREE (def);

  EXECUTE_IF_AND_IN_BITMAP (exits, livein, 0, index, bi)
    {
      add_exit_phis_edge (BASIC_BLOCK (index), var);
    }
}

/* Add exit phis for the names marked in NAMES_TO_RENAME.
   Exits of the loops are stored in EXITS.  Sets of blocks where the ssa
   names are used are stored in USE_BLOCKS.  */

static void
add_exit_phis (bitmap names_to_rename, bitmap *use_blocks, bitmap loop_exits)
{
  unsigned i;
  bitmap_iterator bi;

  EXECUTE_IF_SET_IN_BITMAP (names_to_rename, 0, i, bi)
    {
      add_exit_phis_var (ssa_name (i), use_blocks[i], loop_exits);
    }
}

/* Returns a bitmap of all loop exit edge targets.  */

static bitmap
get_loops_exits (void)
{
  bitmap exits = BITMAP_ALLOC (NULL);
  basic_block bb;
  edge e;
  edge_iterator ei;

  FOR_EACH_BB (bb)
    {
      FOR_EACH_EDGE (e, ei, bb->preds)
	if (e->src != ENTRY_BLOCK_PTR
	    && !flow_bb_inside_loop_p (e->src->loop_father, bb))
	  {
	    bitmap_set_bit (exits, bb->index);
	    break;
	  }
    }

  return exits;
}

/* For USE in BB, if it is used outside of the loop it is defined in,
   mark it for rewrite.  Record basic block BB where it is used
   to USE_BLOCKS.  Record the ssa name index to NEED_PHIS bitmap.  */

static void
find_uses_to_rename_use (basic_block bb, tree use, bitmap *use_blocks,
			 bitmap need_phis)
{
  unsigned ver;
  basic_block def_bb;
  struct loop *def_loop;

  if (TREE_CODE (use) != SSA_NAME)
    return;

  /* We don't need to keep virtual operands in loop-closed form.  */
  if (!is_gimple_reg (use))
    return;

  ver = SSA_NAME_VERSION (use);
  def_bb = bb_for_stmt (SSA_NAME_DEF_STMT (use));
  if (!def_bb)
    return;
  def_loop = def_bb->loop_father;

  /* If the definition is not inside loop, it is not interesting.  */
  if (!def_loop->outer)
    return;

  if (!use_blocks[ver])
    use_blocks[ver] = BITMAP_ALLOC (NULL);
  bitmap_set_bit (use_blocks[ver], bb->index);

  bitmap_set_bit (need_phis, ver);
}

/* For uses in STMT, mark names that are used outside of the loop they are
   defined to rewrite.  Record the set of blocks in that the ssa
   names are defined to USE_BLOCKS and the ssa names themselves to
   NEED_PHIS.  */

static void
find_uses_to_rename_stmt (tree stmt, bitmap *use_blocks, bitmap need_phis)
{
  ssa_op_iter iter;
  tree var;
  basic_block bb = bb_for_stmt (stmt);

  FOR_EACH_SSA_TREE_OPERAND (var, stmt, iter, SSA_OP_ALL_USES | SSA_OP_ALL_KILLS)
    find_uses_to_rename_use (bb, var, use_blocks, need_phis);
}

/* Marks names that are used in BB and outside of the loop they are
   defined in for rewrite.  Records the set of blocks in that the ssa
   names are defined to USE_BLOCKS.  Record the SSA names that will
   need exit PHIs in NEED_PHIS.  */

static void
find_uses_to_rename_bb (basic_block bb, bitmap *use_blocks, bitmap need_phis)
{
  block_stmt_iterator bsi;
  edge e;
  edge_iterator ei;
  tree phi;

  FOR_EACH_EDGE (e, ei, bb->succs)
    for (phi = phi_nodes (e->dest); phi; phi = PHI_CHAIN (phi))
      find_uses_to_rename_use (bb, PHI_ARG_DEF_FROM_EDGE (phi, e),
			       use_blocks, need_phis);
 
  for (bsi = bsi_start (bb); !bsi_end_p (bsi); bsi_next (&bsi))
    find_uses_to_rename_stmt (bsi_stmt (bsi), use_blocks, need_phis);
}
     
/* Marks names that are used outside of the loop they are defined in
   for rewrite.  Records the set of blocks in that the ssa
   names are defined to USE_BLOCKS.  If CHANGED_BBS is not NULL,
   scan only blocks in this set.  */

static void
find_uses_to_rename (bitmap changed_bbs, bitmap *use_blocks, bitmap need_phis)
{
  basic_block bb;
  unsigned index;
  bitmap_iterator bi;

  if (changed_bbs && !bitmap_empty_p (changed_bbs))
    {
      EXECUTE_IF_SET_IN_BITMAP (changed_bbs, 0, index, bi)
	{
	  find_uses_to_rename_bb (BASIC_BLOCK (index), use_blocks, need_phis);
	}
    }
  else
    {
      FOR_EACH_BB (bb)
	{
	  find_uses_to_rename_bb (bb, use_blocks, need_phis);
	}
    }
}

/* Rewrites the program into a loop closed ssa form -- i.e. inserts extra
   phi nodes to ensure that no variable is used outside the loop it is
   defined in.

   This strengthening of the basic ssa form has several advantages:

   1) Updating it during unrolling/peeling/versioning is trivial, since
      we do not need to care about the uses outside of the loop.
   2) The behavior of all uses of an induction variable is the same.
      Without this, you need to distinguish the case when the variable
      is used outside of the loop it is defined in, for example

      for (i = 0; i < 100; i++)
	{
	  for (j = 0; j < 100; j++)
	    {
	      k = i + j;
	      use1 (k);
	    }
	  use2 (k);
	}

      Looking from the outer loop with the normal SSA form, the first use of k
      is not well-behaved, while the second one is an induction variable with
      base 99 and step 1.
      
      If CHANGED_BBS is not NULL, we look for uses outside loops only in
      the basic blocks in this set.

      UPDATE_FLAG is used in the call to update_ssa.  See
      TODO_update_ssa* for documentation.  */

void
rewrite_into_loop_closed_ssa (bitmap changed_bbs, unsigned update_flag)
{
  bitmap loop_exits = get_loops_exits ();
  bitmap *use_blocks;
  unsigned i, old_num_ssa_names;
  bitmap names_to_rename = BITMAP_ALLOC (NULL);

  /* If the pass has caused the SSA form to be out-of-date, update it
     now.  */
  update_ssa (update_flag);

  old_num_ssa_names = num_ssa_names;
  use_blocks = XCNEWVEC (bitmap, old_num_ssa_names);

  /* Find the uses outside loops.  */
  find_uses_to_rename (changed_bbs, use_blocks, names_to_rename);

  /* Add the PHI nodes on exits of the loops for the names we need to
     rewrite.  */
  add_exit_phis (names_to_rename, use_blocks, loop_exits);

  for (i = 0; i < old_num_ssa_names; i++)
    BITMAP_FREE (use_blocks[i]);
  free (use_blocks);
  BITMAP_FREE (loop_exits);
  BITMAP_FREE (names_to_rename);

  /* Fix up all the names found to be used outside their original
     loops.  */
  update_ssa (TODO_update_ssa);
}

/* Check invariants of the loop closed ssa form for the USE in BB.  */

static void
check_loop_closed_ssa_use (basic_block bb, tree use)
{
  tree def;
  basic_block def_bb;
  
  if (TREE_CODE (use) != SSA_NAME || !is_gimple_reg (use))
    return;

  def = SSA_NAME_DEF_STMT (use);
  def_bb = bb_for_stmt (def);
  gcc_assert (!def_bb
	      || flow_bb_inside_loop_p (def_bb->loop_father, bb));
}

/* Checks invariants of loop closed ssa form in statement STMT in BB.  */

static void
check_loop_closed_ssa_stmt (basic_block bb, tree stmt)
{
  ssa_op_iter iter;
  tree var;

  FOR_EACH_SSA_TREE_OPERAND (var, stmt, iter, SSA_OP_ALL_USES | SSA_OP_ALL_KILLS)
    check_loop_closed_ssa_use (bb, var);
}

/* Checks that invariants of the loop closed ssa form are preserved.  */

void
verify_loop_closed_ssa (void)
{
  basic_block bb;
  block_stmt_iterator bsi;
  tree phi;
  unsigned i;

  if (current_loops == NULL)
    return;

  verify_ssa (false);

  FOR_EACH_BB (bb)
    {
      for (phi = phi_nodes (bb); phi; phi = PHI_CHAIN (phi))
	for (i = 0; i < (unsigned) PHI_NUM_ARGS (phi); i++)
	  check_loop_closed_ssa_use (PHI_ARG_EDGE (phi, i)->src,
				     PHI_ARG_DEF (phi, i));

      for (bsi = bsi_start (bb); !bsi_end_p (bsi); bsi_next (&bsi))
	check_loop_closed_ssa_stmt (bb, bsi_stmt (bsi));
    }
}

/* Split loop exit edge EXIT.  The things are a bit complicated by a need to
   preserve the loop closed ssa form.  */

void
split_loop_exit_edge (edge exit)
{
  basic_block dest = exit->dest;
  basic_block bb = loop_split_edge_with (exit, NULL);
  tree phi, new_phi, new_name, name;
  use_operand_p op_p;

  for (phi = phi_nodes (dest); phi; phi = PHI_CHAIN (phi))
    {
      op_p = PHI_ARG_DEF_PTR_FROM_EDGE (phi, single_succ_edge (bb));

      name = USE_FROM_PTR (op_p);

      /* If the argument of the phi node is a constant, we do not need
	 to keep it inside loop.  */
      if (TREE_CODE (name) != SSA_NAME)
	continue;

      /* Otherwise create an auxiliary phi node that will copy the value
	 of the ssa name out of the loop.  */
      new_name = duplicate_ssa_name (name, NULL);
      new_phi = create_phi_node (new_name, bb);
      SSA_NAME_DEF_STMT (new_name) = new_phi;
      add_phi_arg (new_phi, name, exit);
      SET_USE (op_p, new_name);
    }
}

/* Insert statement STMT to the edge E and update the loop structures.
   Returns the newly created block (if any).  */

basic_block
bsi_insert_on_edge_immediate_loop (edge e, tree stmt)
{
  basic_block src, dest, new_bb;
  struct loop *loop_c;

  src = e->src;
  dest = e->dest;

  loop_c = find_common_loop (src->loop_father, dest->loop_father);

  new_bb = bsi_insert_on_edge_immediate (e, stmt);

  if (!new_bb)
    return NULL;

  add_bb_to_loop (new_bb, loop_c);
  if (dest->loop_father->latch == src)
    dest->loop_father->latch = new_bb;

  return new_bb;
}

/* Returns the basic block in that statements should be emitted for induction
   variables incremented at the end of the LOOP.  */

basic_block
ip_end_pos (struct loop *loop)
{
  return loop->latch;
}

/* Returns the basic block in that statements should be emitted for induction
   variables incremented just before exit condition of a LOOP.  */

basic_block
ip_normal_pos (struct loop *loop)
{
  tree last;
  basic_block bb;
  edge exit;

  if (!single_pred_p (loop->latch))
    return NULL;

  bb = single_pred (loop->latch);
  last = last_stmt (bb);
  if (TREE_CODE (last) != COND_EXPR)
    return NULL;

  exit = EDGE_SUCC (bb, 0);
  if (exit->dest == loop->latch)
    exit = EDGE_SUCC (bb, 1);

  if (flow_bb_inside_loop_p (loop, exit->dest))
    return NULL;

  return bb;
}

/* Stores the standard position for induction variable increment in LOOP
   (just before the exit condition if it is available and latch block is empty,
   end of the latch block otherwise) to BSI.  INSERT_AFTER is set to true if
   the increment should be inserted after *BSI.  */

void
standard_iv_increment_position (struct loop *loop, block_stmt_iterator *bsi,
				bool *insert_after)
{
  basic_block bb = ip_normal_pos (loop), latch = ip_end_pos (loop);
  tree last = last_stmt (latch);

  if (!bb
      || (last && TREE_CODE (last) != LABEL_EXPR))
    {
      *bsi = bsi_last (latch);
      *insert_after = true;
    }
  else
    {
      *bsi = bsi_last (bb);
      *insert_after = false;
    }
}

/* Copies phi node arguments for duplicated blocks.  The index of the first
   duplicated block is FIRST_NEW_BLOCK.  */

static void
copy_phi_node_args (unsigned first_new_block)
{
  unsigned i;

  for (i = first_new_block; i < (unsigned) last_basic_block; i++)
    BASIC_BLOCK (i)->flags |= BB_DUPLICATED;

  for (i = first_new_block; i < (unsigned) last_basic_block; i++)
    add_phi_args_after_copy_bb (BASIC_BLOCK (i));

  for (i = first_new_block; i < (unsigned) last_basic_block; i++)
    BASIC_BLOCK (i)->flags &= ~BB_DUPLICATED;
}


/* The same as cfgloopmanip.c:duplicate_loop_to_header_edge, but also
   updates the PHI nodes at start of the copied region.  In order to
   achieve this, only loops whose exits all lead to the same location
   are handled.

   Notice that we do not completely update the SSA web after
   duplication.  The caller is responsible for calling update_ssa
   after the loop has been duplicated.  */

bool
tree_duplicate_loop_to_header_edge (struct loop *loop, edge e,
				    struct loops *loops,
				    unsigned int ndupl, sbitmap wont_exit,
				    edge orig, edge *to_remove,
				    unsigned int *n_to_remove, int flags)
{
  unsigned first_new_block;

  if (!(loops->state & LOOPS_HAVE_SIMPLE_LATCHES))
    return false;
  if (!(loops->state & LOOPS_HAVE_PREHEADERS))
    return false;

#ifdef ENABLE_CHECKING
  verify_loop_closed_ssa ();
#endif

  first_new_block = last_basic_block;
  if (!duplicate_loop_to_header_edge (loop, e, loops, ndupl, wont_exit,
				      orig, to_remove, n_to_remove, flags))
    return false;

  /* Readd the removed phi args for e.  */
  flush_pending_stmts (e);

  /* Copy the phi node arguments.  */
  copy_phi_node_args (first_new_block);

  scev_reset ();

  return true;
}

/* Build if (COND) goto THEN_LABEL; else goto ELSE_LABEL;  */

static tree
build_if_stmt (tree cond, tree then_label, tree else_label)
{
  return build3 (COND_EXPR, void_type_node,
		 cond,
		 build1 (GOTO_EXPR, void_type_node, then_label),
		 build1 (GOTO_EXPR, void_type_node, else_label));
}

/* Returns true if we can unroll LOOP FACTOR times.  Number
   of iterations of the loop is returned in NITER.  */

bool
can_unroll_loop_p (struct loop *loop, unsigned factor,
		   struct tree_niter_desc *niter)
{
  edge exit;

  /* Check whether unrolling is possible.  We only want to unroll loops
     for that we are able to determine number of iterations.  We also
     want to split the extra iterations of the loop from its end,
     therefore we require that the loop has precisely one
     exit.  */

  exit = single_dom_exit (loop);
  if (!exit)
    return false;

  if (!number_of_iterations_exit (loop, exit, niter, false)
      || niter->cmp == ERROR_MARK
      /* Scalar evolutions analysis might have copy propagated
	 the abnormal ssa names into these expressions, hence
	 emiting the computations based on them during loop
	 unrolling might create overlapping life ranges for
	 them, and failures in out-of-ssa.  */
      || contains_abnormal_ssa_name_p (niter->may_be_zero)
      || contains_abnormal_ssa_name_p (niter->control.base)
      || contains_abnormal_ssa_name_p (niter->control.step)
      || contains_abnormal_ssa_name_p (niter->bound))
    return false;

  /* And of course, we must be able to duplicate the loop.  */
  if (!can_duplicate_loop_p (loop))
    return false;

  /* The final loop should be small enough.  */
  if (tree_num_loop_insns (loop) * factor
      > (unsigned) PARAM_VALUE (PARAM_MAX_UNROLLED_INSNS))
    return false;

  return true;
}

/* Determines the conditions that control execution of LOOP unrolled FACTOR
   times.  DESC is number of iterations of LOOP.  ENTER_COND is set to
   condition that must be true if the main loop can be entered.
   EXIT_BASE, EXIT_STEP, EXIT_CMP and EXIT_BOUND are set to values describing
   how the exit from the unrolled loop should be controlled.  */

static void
determine_exit_conditions (struct loop *loop, struct tree_niter_desc *desc,
			   unsigned factor, tree *enter_cond,
			   tree *exit_base, tree *exit_step,
			   enum tree_code *exit_cmp, tree *exit_bound)
{
  tree stmts;
  tree base = desc->control.base;
  tree step = desc->control.step;
  tree bound = desc->bound;
  tree type = TREE_TYPE (base);
  tree bigstep, delta;
  tree min = lower_bound_in_type (type, type);
  tree max = upper_bound_in_type (type, type);
  enum tree_code cmp = desc->cmp;
  tree cond = boolean_true_node, assum;

  *enter_cond = boolean_false_node;
  *exit_base = NULL_TREE;
  *exit_step = NULL_TREE;
  *exit_cmp = ERROR_MARK;
  *exit_bound = NULL_TREE;
  gcc_assert (cmp != ERROR_MARK);

  /* We only need to be correct when we answer question
     "Do at least FACTOR more iterations remain?" in the unrolled loop.
     Thus, transforming BASE + STEP * i <> BOUND to
     BASE + STEP * i < BOUND is ok.  */
  if (cmp == NE_EXPR)
    {
      if (tree_int_cst_sign_bit (step))
	cmp = GT_EXPR;
      else
	cmp = LT_EXPR;
    }
  else if (cmp == LT_EXPR)
    {
      gcc_assert (!tree_int_cst_sign_bit (step));
    }
  else if (cmp == GT_EXPR)
    {
      gcc_assert (tree_int_cst_sign_bit (step));
    }
  else
    gcc_unreachable ();

  /* The main body of the loop may be entered iff:

     1) desc->may_be_zero is false.
     2) it is possible to check that there are at least FACTOR iterations
	of the loop, i.e., BOUND - step * FACTOR does not overflow.
     3) # of iterations is at least FACTOR  */

  if (!zero_p (desc->may_be_zero))
    cond = fold_build2 (TRUTH_AND_EXPR, boolean_type_node,
			invert_truthvalue (desc->may_be_zero),
			cond);

  bigstep = fold_build2 (MULT_EXPR, type, step,
			 build_int_cst_type (type, factor));
  delta = fold_build2 (MINUS_EXPR, type, bigstep, step);
  if (cmp == LT_EXPR)
    assum = fold_build2 (GE_EXPR, boolean_type_node,
			 bound,
			 fold_build2 (PLUS_EXPR, type, min, delta));
  else
    assum = fold_build2 (LE_EXPR, boolean_type_node,
			 bound,
			 fold_build2 (PLUS_EXPR, type, max, delta));
  cond = fold_build2 (TRUTH_AND_EXPR, boolean_type_node, assum, cond);

  bound = fold_build2 (MINUS_EXPR, type, bound, delta);
  assum = fold_build2 (cmp, boolean_type_node, base, bound);
  cond = fold_build2 (TRUTH_AND_EXPR, boolean_type_node, assum, cond);

  cond = force_gimple_operand (unshare_expr (cond), &stmts, false, NULL_TREE);
  if (stmts)
    bsi_insert_on_edge_immediate_loop (loop_preheader_edge (loop), stmts);
  /* cond now may be a gimple comparison, which would be OK, but also any
     other gimple rhs (say a && b).  In this case we need to force it to
     operand.  */
  if (!is_gimple_condexpr (cond))
    {
      cond = force_gimple_operand (cond, &stmts, true, NULL_TREE);
      if (stmts)
	bsi_insert_on_edge_immediate_loop (loop_preheader_edge (loop), stmts);
    }
  *enter_cond = cond;

  base = force_gimple_operand (unshare_expr (base), &stmts, true, NULL_TREE);
  if (stmts)
    bsi_insert_on_edge_immediate_loop (loop_preheader_edge (loop), stmts);
  bound = force_gimple_operand (unshare_expr (bound), &stmts, true, NULL_TREE);
  if (stmts)
    bsi_insert_on_edge_immediate_loop (loop_preheader_edge (loop), stmts);

  *exit_base = base;
  *exit_step = bigstep;
  *exit_cmp = cmp;
  *exit_bound = bound;
}

/* Unroll LOOP FACTOR times.  LOOPS is the loops tree.  DESC describes
   number of iterations of LOOP.  EXIT is the exit of the loop to that
   DESC corresponds.
   
   If N is number of iterations of the loop and MAY_BE_ZERO is the condition
   under that loop exits in the first iteration even if N != 0,
   
   while (1)
     {
       x = phi (init, next);

       pre;
       if (st)
         break;
       post;
     }

   becomes (with possibly the exit conditions formulated a bit differently,
   avoiding the need to create a new iv):
   
   if (MAY_BE_ZERO || N < FACTOR)
     goto rest;

   do
     {
       x = phi (init, next);

       pre;
       post;
       pre;
       post;
       ...
       pre;
       post;
       N -= FACTOR;
       
     } while (N >= FACTOR);

   rest:
     init' = phi (init, x);

   while (1)
     {
       x = phi (init', next);

       pre;
       if (st)
         break;
       post;
     } */

void
tree_unroll_loop (struct loops *loops, struct loop *loop, unsigned factor,
		  edge exit, struct tree_niter_desc *desc)
{
  tree dont_exit, exit_if, ctr_before, ctr_after;
  tree enter_main_cond, exit_base, exit_step, exit_bound;
  enum tree_code exit_cmp;
  tree phi_old_loop, phi_new_loop, phi_rest, init, next, new_init, var;
  struct loop *new_loop;
  basic_block rest, exit_bb;
  edge old_entry, new_entry, old_latch, precond_edge, new_exit;
  edge nonexit, new_nonexit;
  block_stmt_iterator bsi;
  use_operand_p op;
  bool ok;
  unsigned est_niter;
  unsigned irr = loop_preheader_edge (loop)->flags & EDGE_IRREDUCIBLE_LOOP;
  sbitmap wont_exit;

  est_niter = expected_loop_iterations (loop);
  determine_exit_conditions (loop, desc, factor,
			     &enter_main_cond, &exit_base, &exit_step,
			     &exit_cmp, &exit_bound);

  new_loop = loop_version (loops, loop, enter_main_cond, NULL, true);
  gcc_assert (new_loop != NULL);
  update_ssa (TODO_update_ssa);

  /* Unroll the loop and remove the old exits.  */
  dont_exit = ((exit->flags & EDGE_TRUE_VALUE)
	       ? boolean_false_node
	       : boolean_true_node);
  if (exit == EDGE_SUCC (exit->src, 0))
    nonexit = EDGE_SUCC (exit->src, 1);
  else
    nonexit = EDGE_SUCC (exit->src, 0);
  nonexit->probability = REG_BR_PROB_BASE;
  exit->probability = 0;
  nonexit->count += exit->count;
  exit->count = 0;
  exit_if = last_stmt (exit->src);
  COND_EXPR_COND (exit_if) = dont_exit;
  update_stmt (exit_if);
      
  wont_exit = sbitmap_alloc (factor);
  sbitmap_ones (wont_exit);
  ok = tree_duplicate_loop_to_header_edge
	  (loop, loop_latch_edge (loop), loops, factor - 1,
	   wont_exit, NULL, NULL, NULL, DLTHE_FLAG_UPDATE_FREQ);
  free (wont_exit);
  gcc_assert (ok);
  update_ssa (TODO_update_ssa);

  /* Prepare the cfg and update the phi nodes.  */
  rest = loop_preheader_edge (new_loop)->src;
  precond_edge = single_pred_edge (rest);
  loop_split_edge_with (loop_latch_edge (loop), NULL);
  exit_bb = single_pred (loop->latch);

  new_exit = make_edge (exit_bb, rest, EDGE_FALSE_VALUE | irr);
  new_exit->count = loop_preheader_edge (loop)->count;
  est_niter = est_niter / factor + 1;
  new_exit->probability = REG_BR_PROB_BASE / est_niter;

  new_nonexit = single_pred_edge (loop->latch);
  new_nonexit->flags = EDGE_TRUE_VALUE;
  new_nonexit->probability = REG_BR_PROB_BASE - new_exit->probability;

  old_entry = loop_preheader_edge (loop);
  new_entry = loop_preheader_edge (new_loop);
  old_latch = loop_latch_edge (loop);
  for (phi_old_loop = phi_nodes (loop->header),
       phi_new_loop = phi_nodes (new_loop->header);
       phi_old_loop;
       phi_old_loop = PHI_CHAIN (phi_old_loop),
       phi_new_loop = PHI_CHAIN (phi_new_loop))
    {
      init = PHI_ARG_DEF_FROM_EDGE (phi_old_loop, old_entry);
      op = PHI_ARG_DEF_PTR_FROM_EDGE (phi_new_loop, new_entry);
      gcc_assert (operand_equal_for_phi_arg_p (init, USE_FROM_PTR (op)));
      next = PHI_ARG_DEF_FROM_EDGE (phi_old_loop, old_latch);

      /* Prefer using original variable as a base for the new ssa name.
	 This is necessary for virtual ops, and useful in order to avoid
	 losing debug info for real ops.  */
      if (TREE_CODE (next) == SSA_NAME)
	var = SSA_NAME_VAR (next);
      else if (TREE_CODE (init) == SSA_NAME)
	var = SSA_NAME_VAR (init);
      else
	{
	  var = create_tmp_var (TREE_TYPE (init), "unrinittmp");
	  add_referenced_var (var);
	}

      new_init = make_ssa_name (var, NULL_TREE);
      phi_rest = create_phi_node (new_init, rest);
      SSA_NAME_DEF_STMT (new_init) = phi_rest;

      add_phi_arg (phi_rest, init, precond_edge);
      add_phi_arg (phi_rest, next, new_exit);
      SET_USE (op, new_init);
    }

  /* Finally create the new counter for number of iterations and add the new
     exit instruction.  */
  bsi = bsi_last (exit_bb);
  create_iv (exit_base, exit_step, NULL_TREE, loop,
	     &bsi, true, &ctr_before, &ctr_after);
  exit_if = build_if_stmt (build2 (exit_cmp, boolean_type_node, ctr_after,
				   exit_bound),
			   tree_block_label (loop->latch),
			   tree_block_label (rest));
  bsi_insert_after (&bsi, exit_if, BSI_NEW_STMT);

  verify_flow_info ();
  verify_dominators (CDI_DOMINATORS);
  verify_loop_structure (loops);
  verify_loop_closed_ssa ();
}
