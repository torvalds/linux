/* CFG cleanup for trees.
   Copyright (C) 2001, 2002, 2003, 2004, 2005, 2006, 2007
   Free Software Foundation, Inc.

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
#include "hard-reg-set.h"
#include "basic-block.h"
#include "output.h"
#include "toplev.h"
#include "flags.h"
#include "function.h"
#include "expr.h"
#include "ggc.h"
#include "langhooks.h"
#include "diagnostic.h"
#include "tree-flow.h"
#include "timevar.h"
#include "tree-dump.h"
#include "tree-pass.h"
#include "toplev.h"
#include "except.h"
#include "cfgloop.h"
#include "cfglayout.h"
#include "hashtab.h"
#include "tree-ssa-propagate.h"
#include "tree-scalar-evolution.h"

/* Remove any fallthru edge from EV.  Return true if an edge was removed.  */

static bool
remove_fallthru_edge (VEC(edge,gc) *ev)
{
  edge_iterator ei;
  edge e;

  FOR_EACH_EDGE (e, ei, ev)
    if ((e->flags & EDGE_FALLTHRU) != 0)
      {
	remove_edge (e);
	return true;
      }
  return false;
}

/* Disconnect an unreachable block in the control expression starting
   at block BB.  */

static bool
cleanup_control_expr_graph (basic_block bb, block_stmt_iterator bsi)
{
  edge taken_edge;
  bool retval = false;
  tree expr = bsi_stmt (bsi), val;

  if (!single_succ_p (bb))
    {
      edge e;
      edge_iterator ei;
      bool warned;

      fold_defer_overflow_warnings ();

      switch (TREE_CODE (expr))
	{
	case COND_EXPR:
	  val = fold (COND_EXPR_COND (expr));
	  break;

	case SWITCH_EXPR:
	  val = fold (SWITCH_COND (expr));
	  if (TREE_CODE (val) != INTEGER_CST)
	    {
	      fold_undefer_and_ignore_overflow_warnings ();
	      return false;
	    }
	  break;

	default:
	  gcc_unreachable ();
	}

      taken_edge = find_taken_edge (bb, val);
      if (!taken_edge)
	{
	  fold_undefer_and_ignore_overflow_warnings ();
	  return false;
	}

      /* Remove all the edges except the one that is always executed.  */
      warned = false;
      for (ei = ei_start (bb->succs); (e = ei_safe_edge (ei)); )
	{
	  if (e != taken_edge)
	    {
	      if (!warned)
		{
		  fold_undefer_overflow_warnings
		    (true, expr, WARN_STRICT_OVERFLOW_CONDITIONAL);
		  warned = true;
		}

	      taken_edge->probability += e->probability;
	      taken_edge->count += e->count;
	      remove_edge (e);
	      retval = true;
	    }
	  else
	    ei_next (&ei);
	}
      if (!warned)
	fold_undefer_and_ignore_overflow_warnings ();
      if (taken_edge->probability > REG_BR_PROB_BASE)
	taken_edge->probability = REG_BR_PROB_BASE;
    }
  else
    taken_edge = single_succ_edge (bb);

  bsi_remove (&bsi, true);
  taken_edge->flags = EDGE_FALLTHRU;

  /* We removed some paths from the cfg.  */
  free_dominance_info (CDI_DOMINATORS);

  return retval;
}

/* A list of all the noreturn calls passed to modify_stmt.
   cleanup_control_flow uses it to detect cases where a mid-block
   indirect call has been turned into a noreturn call.  When this
   happens, all the instructions after the call are no longer
   reachable and must be deleted as dead.  */

VEC(tree,gc) *modified_noreturn_calls;

/* Try to remove superfluous control structures.  */

static bool
cleanup_control_flow (void)
{
  basic_block bb;
  block_stmt_iterator bsi;
  bool retval = false;
  tree stmt;

  /* Detect cases where a mid-block call is now known not to return.  */
  while (VEC_length (tree, modified_noreturn_calls))
    {
      stmt = VEC_pop (tree, modified_noreturn_calls);
      bb = bb_for_stmt (stmt);
      if (bb != NULL && last_stmt (bb) != stmt && noreturn_call_p (stmt))
	split_block (bb, stmt);
    }

  FOR_EACH_BB (bb)
    {
      bsi = bsi_last (bb);

      /* If the last statement of the block could throw and now cannot,
	 we need to prune cfg.  */
      retval |= tree_purge_dead_eh_edges (bb);

      if (bsi_end_p (bsi))
	continue;

      stmt = bsi_stmt (bsi);

      if (TREE_CODE (stmt) == COND_EXPR
	  || TREE_CODE (stmt) == SWITCH_EXPR)
	retval |= cleanup_control_expr_graph (bb, bsi);
      /* If we had a computed goto which has a compile-time determinable
	 destination, then we can eliminate the goto.  */
      else if (TREE_CODE (stmt) == GOTO_EXPR
	       && TREE_CODE (GOTO_DESTINATION (stmt)) == ADDR_EXPR
	       && (TREE_CODE (TREE_OPERAND (GOTO_DESTINATION (stmt), 0))
		   == LABEL_DECL))
	{
	  edge e;
	  tree label;
	  edge_iterator ei;
	  basic_block target_block;
	  bool removed_edge = false;

	  /* First look at all the outgoing edges.  Delete any outgoing
	     edges which do not go to the right block.  For the one
	     edge which goes to the right block, fix up its flags.  */
	  label = TREE_OPERAND (GOTO_DESTINATION (stmt), 0);
	  target_block = label_to_block (label);
	  for (ei = ei_start (bb->succs); (e = ei_safe_edge (ei)); )
	    {
	      if (e->dest != target_block)
		{
		  removed_edge = true;
		  remove_edge (e);
		}
	      else
	        {
		  /* Turn off the EDGE_ABNORMAL flag.  */
		  e->flags &= ~EDGE_ABNORMAL;

		  /* And set EDGE_FALLTHRU.  */
		  e->flags |= EDGE_FALLTHRU;
		  ei_next (&ei);
		}
	    }

	  /* If we removed one or more edges, then we will need to fix the
	     dominators.  It may be possible to incrementally update them.  */
	  if (removed_edge)
	    free_dominance_info (CDI_DOMINATORS);

	  /* Remove the GOTO_EXPR as it is not needed.  The CFG has all the
	     relevant information we need.  */
	  bsi_remove (&bsi, true);
	  retval = true;
	}

      /* Check for indirect calls that have been turned into
	 noreturn calls.  */
      else if (noreturn_call_p (stmt) && remove_fallthru_edge (bb->succs))
	{
	  free_dominance_info (CDI_DOMINATORS);
	  retval = true;
	}
    }
  return retval;
}

/* Return true if basic block BB does nothing except pass control
   flow to another block and that we can safely insert a label at
   the start of the successor block.

   As a precondition, we require that BB be not equal to
   ENTRY_BLOCK_PTR.  */

static bool
tree_forwarder_block_p (basic_block bb, bool phi_wanted)
{
  block_stmt_iterator bsi;
  edge_iterator ei;
  edge e, succ;
  basic_block dest;

  /* BB must have a single outgoing edge.  */
  if (single_succ_p (bb) != 1
      /* If PHI_WANTED is false, BB must not have any PHI nodes.
	 Otherwise, BB must have PHI nodes.  */
      || (phi_nodes (bb) != NULL_TREE) != phi_wanted
      /* BB may not be a predecessor of EXIT_BLOCK_PTR.  */
      || single_succ (bb) == EXIT_BLOCK_PTR
      /* Nor should this be an infinite loop.  */
      || single_succ (bb) == bb
      /* BB may not have an abnormal outgoing edge.  */
      || (single_succ_edge (bb)->flags & EDGE_ABNORMAL))
    return false;

#if ENABLE_CHECKING
  gcc_assert (bb != ENTRY_BLOCK_PTR);
#endif

  /* Now walk through the statements backward.  We can ignore labels,
     anything else means this is not a forwarder block.  */
  for (bsi = bsi_last (bb); !bsi_end_p (bsi); bsi_prev (&bsi))
    {
      tree stmt = bsi_stmt (bsi);

      switch (TREE_CODE (stmt))
	{
	case LABEL_EXPR:
	  if (DECL_NONLOCAL (LABEL_EXPR_LABEL (stmt)))
	    return false;
	  break;

	default:
	  return false;
	}
    }

  if (find_edge (ENTRY_BLOCK_PTR, bb))
    return false;

  if (current_loops)
    {
      basic_block dest;
      /* Protect loop latches, headers and preheaders.  */
      if (bb->loop_father->header == bb)
	return false;
      dest = EDGE_SUCC (bb, 0)->dest;

      if (dest->loop_father->header == dest)
	return false;
    }

  /* If we have an EH edge leaving this block, make sure that the
     destination of this block has only one predecessor.  This ensures
     that we don't get into the situation where we try to remove two
     forwarders that go to the same basic block but are handlers for
     different EH regions.  */
  succ = single_succ_edge (bb);
  dest = succ->dest;
  FOR_EACH_EDGE (e, ei, bb->preds)
    {
      if (e->flags & EDGE_EH)
        {
	  if (!single_pred_p (dest))
	    return false;
	}
    }

  return true;
}

/* Return true if BB has at least one abnormal incoming edge.  */

static inline bool
has_abnormal_incoming_edge_p (basic_block bb)
{
  edge e;
  edge_iterator ei;

  FOR_EACH_EDGE (e, ei, bb->preds)
    if (e->flags & EDGE_ABNORMAL)
      return true;

  return false;
}

/* If all the PHI nodes in DEST have alternatives for E1 and E2 and
   those alternatives are equal in each of the PHI nodes, then return
   true, else return false.  */

static bool
phi_alternatives_equal (basic_block dest, edge e1, edge e2)
{
  int n1 = e1->dest_idx;
  int n2 = e2->dest_idx;
  tree phi;

  for (phi = phi_nodes (dest); phi; phi = PHI_CHAIN (phi))
    {
      tree val1 = PHI_ARG_DEF (phi, n1);
      tree val2 = PHI_ARG_DEF (phi, n2);

      gcc_assert (val1 != NULL_TREE);
      gcc_assert (val2 != NULL_TREE);

      if (!operand_equal_for_phi_arg_p (val1, val2))
	return false;
    }

  return true;
}

/* Removes forwarder block BB.  Returns false if this failed.  If a new
   forwarder block is created due to redirection of edges, it is
   stored to worklist.  */

static bool
remove_forwarder_block (basic_block bb, basic_block **worklist)
{
  edge succ = single_succ_edge (bb), e, s;
  basic_block dest = succ->dest;
  tree label;
  tree phi;
  edge_iterator ei;
  block_stmt_iterator bsi, bsi_to;
  bool seen_abnormal_edge = false;

  /* We check for infinite loops already in tree_forwarder_block_p.
     However it may happen that the infinite loop is created
     afterwards due to removal of forwarders.  */
  if (dest == bb)
    return false;

  /* If the destination block consists of a nonlocal label, do not merge
     it.  */
  label = first_stmt (dest);
  if (label
      && TREE_CODE (label) == LABEL_EXPR
      && DECL_NONLOCAL (LABEL_EXPR_LABEL (label)))
    return false;

  /* If there is an abnormal edge to basic block BB, but not into
     dest, problems might occur during removal of the phi node at out
     of ssa due to overlapping live ranges of registers.

     If there is an abnormal edge in DEST, the problems would occur
     anyway since cleanup_dead_labels would then merge the labels for
     two different eh regions, and rest of exception handling code
     does not like it.

     So if there is an abnormal edge to BB, proceed only if there is
     no abnormal edge to DEST and there are no phi nodes in DEST.  */
  if (has_abnormal_incoming_edge_p (bb))
    {
      seen_abnormal_edge = true;

      if (has_abnormal_incoming_edge_p (dest)
	  || phi_nodes (dest) != NULL_TREE)
	return false;
    }

  /* If there are phi nodes in DEST, and some of the blocks that are
     predecessors of BB are also predecessors of DEST, check that the
     phi node arguments match.  */
  if (phi_nodes (dest))
    {
      FOR_EACH_EDGE (e, ei, bb->preds)
	{
	  s = find_edge (e->src, dest);
	  if (!s)
	    continue;

	  if (!phi_alternatives_equal (dest, succ, s))
	    return false;
	}
    }

  /* Redirect the edges.  */
  for (ei = ei_start (bb->preds); (e = ei_safe_edge (ei)); )
    {
      if (e->flags & EDGE_ABNORMAL)
	{
	  /* If there is an abnormal edge, redirect it anyway, and
	     move the labels to the new block to make it legal.  */
	  s = redirect_edge_succ_nodup (e, dest);
	}
      else
	s = redirect_edge_and_branch (e, dest);

      if (s == e)
	{
	  /* Create arguments for the phi nodes, since the edge was not
	     here before.  */
	  for (phi = phi_nodes (dest); phi; phi = PHI_CHAIN (phi))
	    add_phi_arg (phi, PHI_ARG_DEF (phi, succ->dest_idx), s);
	}
      else
	{
	  /* The source basic block might become a forwarder.  We know
	     that it was not a forwarder before, since it used to have
	     at least two outgoing edges, so we may just add it to
	     worklist.  */
	  if (tree_forwarder_block_p (s->src, false))
	    *(*worklist)++ = s->src;
	}
    }

  if (seen_abnormal_edge)
    {
      /* Move the labels to the new block, so that the redirection of
	 the abnormal edges works.  */

      bsi_to = bsi_start (dest);
      for (bsi = bsi_start (bb); !bsi_end_p (bsi); )
	{
	  label = bsi_stmt (bsi);
	  gcc_assert (TREE_CODE (label) == LABEL_EXPR);
	  bsi_remove (&bsi, false);
	  bsi_insert_before (&bsi_to, label, BSI_CONTINUE_LINKING);
	}
    }

  /* Update the dominators.  */
  if (dom_info_available_p (CDI_DOMINATORS))
    {
      basic_block dom, dombb, domdest;

      dombb = get_immediate_dominator (CDI_DOMINATORS, bb);
      domdest = get_immediate_dominator (CDI_DOMINATORS, dest);
      if (domdest == bb)
	{
	  /* Shortcut to avoid calling (relatively expensive)
	     nearest_common_dominator unless necessary.  */
	  dom = dombb;
	}
      else
	dom = nearest_common_dominator (CDI_DOMINATORS, domdest, dombb);

      set_immediate_dominator (CDI_DOMINATORS, dest, dom);
    }

  /* And kill the forwarder block.  */
  delete_basic_block (bb);

  return true;
}

/* Removes forwarder blocks.  */

static bool
cleanup_forwarder_blocks (void)
{
  basic_block bb;
  bool changed = false;
  basic_block *worklist = XNEWVEC (basic_block, n_basic_blocks);
  basic_block *current = worklist;

  FOR_EACH_BB (bb)
    {
      if (tree_forwarder_block_p (bb, false))
	*current++ = bb;
    }

  while (current != worklist)
    {
      bb = *--current;
      changed |= remove_forwarder_block (bb, &current);
    }

  free (worklist);
  return changed;
}

/* Do one round of CFG cleanup.  */

static bool
cleanup_tree_cfg_1 (void)
{
  bool retval;

  retval = cleanup_control_flow ();
  retval |= delete_unreachable_blocks ();

  /* Forwarder blocks can carry line number information which is
     useful when debugging, so we only clean them up when
     optimizing.  */

  if (optimize > 0)
    {
      /* cleanup_forwarder_blocks can redirect edges out of
	 SWITCH_EXPRs, which can get expensive.  So we want to enable
	 recording of edge to CASE_LABEL_EXPR mappings around the call
	 to cleanup_forwarder_blocks.  */
      start_recording_case_labels ();
      retval |= cleanup_forwarder_blocks ();
      end_recording_case_labels ();
    }

  /* Merging the blocks may create new opportunities for folding
     conditional branches (due to the elimination of single-valued PHI
     nodes).  */
  retval |= merge_seq_blocks ();

  return retval;
}


/* Remove unreachable blocks and other miscellaneous clean up work.
   Return true if the flowgraph was modified, false otherwise.  */

bool
cleanup_tree_cfg (void)
{
  bool retval, changed;

  timevar_push (TV_TREE_CLEANUP_CFG);

  /* Iterate until there are no more cleanups left to do.  If any
     iteration changed the flowgraph, set CHANGED to true.  */
  changed = false;
  do
    {
      retval = cleanup_tree_cfg_1 ();
      changed |= retval;
    }
  while (retval);

  compact_blocks ();

#ifdef ENABLE_CHECKING
  verify_flow_info ();
#endif

  timevar_pop (TV_TREE_CLEANUP_CFG);

  return changed;
}

/* Cleanup cfg and repair loop structures.  */

void
cleanup_tree_cfg_loop (void)
{
  bool changed = cleanup_tree_cfg ();

  if (changed)
    {
      bitmap changed_bbs = BITMAP_ALLOC (NULL);
      fix_loop_structure (current_loops, changed_bbs);
      calculate_dominance_info (CDI_DOMINATORS);

      /* This usually does nothing.  But sometimes parts of cfg that originally
	 were inside a loop get out of it due to edge removal (since they
	 become unreachable by back edges from latch).  */
      rewrite_into_loop_closed_ssa (changed_bbs, TODO_update_ssa);

      BITMAP_FREE (changed_bbs);

#ifdef ENABLE_CHECKING
      verify_loop_structure (current_loops);
#endif
      scev_reset ();
    }
}

/* Merge the PHI nodes at BB into those at BB's sole successor.  */

static void
remove_forwarder_block_with_phi (basic_block bb)
{
  edge succ = single_succ_edge (bb);
  basic_block dest = succ->dest;
  tree label;
  basic_block dombb, domdest, dom;

  /* We check for infinite loops already in tree_forwarder_block_p.
     However it may happen that the infinite loop is created
     afterwards due to removal of forwarders.  */
  if (dest == bb)
    return;

  /* If the destination block consists of a nonlocal label, do not
     merge it.  */
  label = first_stmt (dest);
  if (label
      && TREE_CODE (label) == LABEL_EXPR
      && DECL_NONLOCAL (LABEL_EXPR_LABEL (label)))
    return;

  /* Redirect each incoming edge to BB to DEST.  */
  while (EDGE_COUNT (bb->preds) > 0)
    {
      edge e = EDGE_PRED (bb, 0), s;
      tree phi;

      s = find_edge (e->src, dest);
      if (s)
	{
	  /* We already have an edge S from E->src to DEST.  If S and
	     E->dest's sole successor edge have the same PHI arguments
	     at DEST, redirect S to DEST.  */
	  if (phi_alternatives_equal (dest, s, succ))
	    {
	      e = redirect_edge_and_branch (e, dest);
	      PENDING_STMT (e) = NULL_TREE;
	      continue;
	    }

	  /* PHI arguments are different.  Create a forwarder block by
	     splitting E so that we can merge PHI arguments on E to
	     DEST.  */
	  e = single_succ_edge (split_edge (e));
	}

      s = redirect_edge_and_branch (e, dest);

      /* redirect_edge_and_branch must not create a new edge.  */
      gcc_assert (s == e);

      /* Add to the PHI nodes at DEST each PHI argument removed at the
	 destination of E.  */
      for (phi = phi_nodes (dest); phi; phi = PHI_CHAIN (phi))
	{
	  tree def = PHI_ARG_DEF (phi, succ->dest_idx);

	  if (TREE_CODE (def) == SSA_NAME)
	    {
	      tree var;

	      /* If DEF is one of the results of PHI nodes removed during
		 redirection, replace it with the PHI argument that used
		 to be on E.  */
	      for (var = PENDING_STMT (e); var; var = TREE_CHAIN (var))
		{
		  tree old_arg = TREE_PURPOSE (var);
		  tree new_arg = TREE_VALUE (var);

		  if (def == old_arg)
		    {
		      def = new_arg;
		      break;
		    }
		}
	    }

	  add_phi_arg (phi, def, s);
	}

      PENDING_STMT (e) = NULL;
    }

  /* Update the dominators.  */
  dombb = get_immediate_dominator (CDI_DOMINATORS, bb);
  domdest = get_immediate_dominator (CDI_DOMINATORS, dest);
  if (domdest == bb)
    {
      /* Shortcut to avoid calling (relatively expensive)
	 nearest_common_dominator unless necessary.  */
      dom = dombb;
    }
  else
    dom = nearest_common_dominator (CDI_DOMINATORS, domdest, dombb);

  set_immediate_dominator (CDI_DOMINATORS, dest, dom);

  /* Remove BB since all of BB's incoming edges have been redirected
     to DEST.  */
  delete_basic_block (bb);
}

/* This pass merges PHI nodes if one feeds into another.  For example,
   suppose we have the following:

  goto <bb 9> (<L9>);

<L8>:;
  tem_17 = foo ();

  # tem_6 = PHI <tem_17(8), tem_23(7)>;
<L9>:;

  # tem_3 = PHI <tem_6(9), tem_2(5)>;
<L10>:;

  Then we merge the first PHI node into the second one like so:

  goto <bb 9> (<L10>);

<L8>:;
  tem_17 = foo ();

  # tem_3 = PHI <tem_23(7), tem_2(5), tem_17(8)>;
<L10>:;
*/

static unsigned int
merge_phi_nodes (void)
{
  basic_block *worklist = XNEWVEC (basic_block, n_basic_blocks);
  basic_block *current = worklist;
  basic_block bb;

  calculate_dominance_info (CDI_DOMINATORS);

  /* Find all PHI nodes that we may be able to merge.  */
  FOR_EACH_BB (bb)
    {
      basic_block dest;

      /* Look for a forwarder block with PHI nodes.  */
      if (!tree_forwarder_block_p (bb, true))
	continue;

      dest = single_succ (bb);

      /* We have to feed into another basic block with PHI
	 nodes.  */
      if (!phi_nodes (dest)
	  /* We don't want to deal with a basic block with
	     abnormal edges.  */
	  || has_abnormal_incoming_edge_p (bb))
	continue;

      if (!dominated_by_p (CDI_DOMINATORS, dest, bb))
	{
	  /* If BB does not dominate DEST, then the PHI nodes at
	     DEST must be the only users of the results of the PHI
	     nodes at BB.  */
	  *current++ = bb;
	}
      else
	{
	  tree phi;
	  unsigned int dest_idx = single_succ_edge (bb)->dest_idx;

	  /* BB dominates DEST.  There may be many users of the PHI
	     nodes in BB.  However, there is still a trivial case we
	     can handle.  If the result of every PHI in BB is used
	     only by a PHI in DEST, then we can trivially merge the
	     PHI nodes from BB into DEST.  */
	  for (phi = phi_nodes (bb); phi; phi = PHI_CHAIN (phi))
	    {
	      tree result = PHI_RESULT (phi);
	      use_operand_p imm_use;
	      tree use_stmt;

	      /* If the PHI's result is never used, then we can just
		 ignore it.  */
	      if (has_zero_uses (result))
		continue;

	      /* Get the single use of the result of this PHI node.  */
  	      if (!single_imm_use (result, &imm_use, &use_stmt)
		  || TREE_CODE (use_stmt) != PHI_NODE
		  || bb_for_stmt (use_stmt) != dest
		  || PHI_ARG_DEF (use_stmt, dest_idx) != result)
		break;
	    }

	  /* If the loop above iterated through all the PHI nodes
	     in BB, then we can merge the PHIs from BB into DEST.  */
	  if (!phi)
	    *current++ = bb;
	}
    }

  /* Now let's drain WORKLIST.  */
  while (current != worklist)
    {
      bb = *--current;
      remove_forwarder_block_with_phi (bb);
    }

  free (worklist);
  return 0;
}

static bool
gate_merge_phi (void)
{
  return 1;
}

struct tree_opt_pass pass_merge_phi = {
  "mergephi",			/* name */
  gate_merge_phi,		/* gate */
  merge_phi_nodes,		/* execute */
  NULL,				/* sub */
  NULL,				/* next */
  0,				/* static_pass_number */
  TV_TREE_MERGE_PHI,		/* tv_id */
  PROP_cfg | PROP_ssa,		/* properties_required */
  0,				/* properties_provided */
  0,				/* properties_destroyed */
  0,				/* todo_flags_start */
  TODO_dump_func | TODO_ggc_collect	/* todo_flags_finish */
  | TODO_verify_ssa,
  0				/* letter */
};
