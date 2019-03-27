/* Hooks for cfg representation specific functions.
   Copyright (C) 2003, 2004, 2005 Free Software Foundation, Inc.
   Contributed by Sebastian Pop <s.pop@laposte.net>

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
#include "basic-block.h"
#include "tree-flow.h"
#include "timevar.h"
#include "toplev.h"

/* A pointer to one of the hooks containers.  */
static struct cfg_hooks *cfg_hooks;

/* Initialization of functions specific to the rtl IR.  */
void
rtl_register_cfg_hooks (void)
{
  cfg_hooks = &rtl_cfg_hooks;
}

/* Initialization of functions specific to the rtl IR.  */
void
cfg_layout_rtl_register_cfg_hooks (void)
{
  cfg_hooks = &cfg_layout_rtl_cfg_hooks;
}

/* Initialization of functions specific to the tree IR.  */

void
tree_register_cfg_hooks (void)
{
  cfg_hooks = &tree_cfg_hooks;
}

/* Returns current ir type (rtl = 0, trees = 1).  */

int
ir_type (void)
{
  return cfg_hooks == &tree_cfg_hooks ? 1 : 0;
}

/* Verify the CFG consistency.

   Currently it does following: checks edge and basic block list correctness
   and calls into IL dependent checking then.  */

void
verify_flow_info (void)
{
  size_t *edge_checksum;
  int err = 0;
  basic_block bb, last_bb_seen;
  basic_block *last_visited;

  timevar_push (TV_CFG_VERIFY);
  last_visited = XCNEWVEC (basic_block, last_basic_block);
  edge_checksum = XCNEWVEC (size_t, last_basic_block);

  /* Check bb chain & numbers.  */
  last_bb_seen = ENTRY_BLOCK_PTR;
  FOR_BB_BETWEEN (bb, ENTRY_BLOCK_PTR->next_bb, NULL, next_bb)
    {
      if (bb != EXIT_BLOCK_PTR
	  && bb != BASIC_BLOCK (bb->index))
	{
	  error ("bb %d on wrong place", bb->index);
	  err = 1;
	}

      if (bb->prev_bb != last_bb_seen)
	{
	  error ("prev_bb of %d should be %d, not %d",
		 bb->index, last_bb_seen->index, bb->prev_bb->index);
	  err = 1;
	}

      last_bb_seen = bb;
    }

  /* Now check the basic blocks (boundaries etc.) */
  FOR_EACH_BB_REVERSE (bb)
    {
      int n_fallthru = 0;
      edge e;
      edge_iterator ei;

      if (bb->count < 0)
	{
	  error ("verify_flow_info: Wrong count of block %i %i",
		 bb->index, (int)bb->count);
	  err = 1;
	}
      if (bb->frequency < 0)
	{
	  error ("verify_flow_info: Wrong frequency of block %i %i",
		 bb->index, bb->frequency);
	  err = 1;
	}
      FOR_EACH_EDGE (e, ei, bb->succs)
	{
	  if (last_visited [e->dest->index] == bb)
	    {
	      error ("verify_flow_info: Duplicate edge %i->%i",
		     e->src->index, e->dest->index);
	      err = 1;
	    }
	  if (e->probability < 0 || e->probability > REG_BR_PROB_BASE)
	    {
	      error ("verify_flow_info: Wrong probability of edge %i->%i %i",
		     e->src->index, e->dest->index, e->probability);
	      err = 1;
	    }
	  if (e->count < 0)
	    {
	      error ("verify_flow_info: Wrong count of edge %i->%i %i",
		     e->src->index, e->dest->index, (int)e->count);
	      err = 1;
	    }

	  last_visited [e->dest->index] = bb;

	  if (e->flags & EDGE_FALLTHRU)
	    n_fallthru++;

	  if (e->src != bb)
	    {
	      error ("verify_flow_info: Basic block %d succ edge is corrupted",
		     bb->index);
	      fprintf (stderr, "Predecessor: ");
	      dump_edge_info (stderr, e, 0);
	      fprintf (stderr, "\nSuccessor: ");
	      dump_edge_info (stderr, e, 1);
	      fprintf (stderr, "\n");
	      err = 1;
	    }

	  edge_checksum[e->dest->index] += (size_t) e;
	}
      if (n_fallthru > 1)
	{
	  error ("wrong amount of branch edges after unconditional jump %i", bb->index);
	  err = 1;
	}

      FOR_EACH_EDGE (e, ei, bb->preds)
	{
	  if (e->dest != bb)
	    {
	      error ("basic block %d pred edge is corrupted", bb->index);
	      fputs ("Predecessor: ", stderr);
	      dump_edge_info (stderr, e, 0);
	      fputs ("\nSuccessor: ", stderr);
	      dump_edge_info (stderr, e, 1);
	      fputc ('\n', stderr);
	      err = 1;
	    }

	  if (ei.index != e->dest_idx)
	    {
	      error ("basic block %d pred edge is corrupted", bb->index);
	      error ("its dest_idx should be %d, not %d",
		     ei.index, e->dest_idx);
	      fputs ("Predecessor: ", stderr);
	      dump_edge_info (stderr, e, 0);
	      fputs ("\nSuccessor: ", stderr);
	      dump_edge_info (stderr, e, 1);
	      fputc ('\n', stderr);
	      err = 1;
	    }

	  edge_checksum[e->dest->index] -= (size_t) e;
	}
    }

  /* Complete edge checksumming for ENTRY and EXIT.  */
  {
    edge e;
    edge_iterator ei;

    FOR_EACH_EDGE (e, ei, ENTRY_BLOCK_PTR->succs)
      edge_checksum[e->dest->index] += (size_t) e;

    FOR_EACH_EDGE (e, ei, EXIT_BLOCK_PTR->preds)
      edge_checksum[e->dest->index] -= (size_t) e;
  }

  FOR_BB_BETWEEN (bb, ENTRY_BLOCK_PTR, NULL, next_bb)
    if (edge_checksum[bb->index])
      {
	error ("basic block %i edge lists are corrupted", bb->index);
	err = 1;
      }

  last_bb_seen = ENTRY_BLOCK_PTR;

  /* Clean up.  */
  free (last_visited);
  free (edge_checksum);

  if (cfg_hooks->verify_flow_info)
    err |= cfg_hooks->verify_flow_info ();
  if (err)
    internal_error ("verify_flow_info failed");
  timevar_pop (TV_CFG_VERIFY);
}

/* Print out one basic block.  This function takes care of the purely
   graph related information.  The cfg hook for the active representation
   should dump representation-specific information.  */

void
dump_bb (basic_block bb, FILE *outf, int indent)
{
  edge e;
  edge_iterator ei;
  char *s_indent;

  s_indent = alloca ((size_t) indent + 1);
  memset (s_indent, ' ', (size_t) indent);
  s_indent[indent] = '\0';

  fprintf (outf, ";;%s basic block %d, loop depth %d, count ",
	   s_indent, bb->index, bb->loop_depth);
  fprintf (outf, HOST_WIDEST_INT_PRINT_DEC, (HOST_WIDEST_INT) bb->count);
  putc ('\n', outf);

  fprintf (outf, ";;%s prev block ", s_indent);
  if (bb->prev_bb)
    fprintf (outf, "%d, ", bb->prev_bb->index);
  else
    fprintf (outf, "(nil), ");
  fprintf (outf, "next block ");
  if (bb->next_bb)
    fprintf (outf, "%d", bb->next_bb->index);
  else
    fprintf (outf, "(nil)");
  putc ('\n', outf);

  fprintf (outf, ";;%s pred:      ", s_indent);
  FOR_EACH_EDGE (e, ei, bb->preds)
    dump_edge_info (outf, e, 0);
  putc ('\n', outf);

  fprintf (outf, ";;%s succ:      ", s_indent);
  FOR_EACH_EDGE (e, ei, bb->succs)
    dump_edge_info (outf, e, 1);
  putc ('\n', outf);

  if (cfg_hooks->dump_bb)
    cfg_hooks->dump_bb (bb, outf, indent);
}

/* Redirect edge E to the given basic block DEST and update underlying program
   representation.  Returns edge representing redirected branch (that may not
   be equivalent to E in the case of duplicate edges being removed) or NULL
   if edge is not easily redirectable for whatever reason.  */

edge
redirect_edge_and_branch (edge e, basic_block dest)
{
  edge ret;

  if (!cfg_hooks->redirect_edge_and_branch)
    internal_error ("%s does not support redirect_edge_and_branch",
		    cfg_hooks->name);

  ret = cfg_hooks->redirect_edge_and_branch (e, dest);

  return ret;
}

/* Redirect the edge E to basic block DEST even if it requires creating
   of a new basic block; then it returns the newly created basic block.
   Aborts when redirection is impossible.  */

basic_block
redirect_edge_and_branch_force (edge e, basic_block dest)
{
  basic_block ret;

  if (!cfg_hooks->redirect_edge_and_branch_force)
    internal_error ("%s does not support redirect_edge_and_branch_force",
		    cfg_hooks->name);

  ret = cfg_hooks->redirect_edge_and_branch_force (e, dest);

  return ret;
}

/* Splits basic block BB after the specified instruction I (but at least after
   the labels).  If I is NULL, splits just after labels.  The newly created edge
   is returned.  The new basic block is created just after the old one.  */

edge
split_block (basic_block bb, void *i)
{
  basic_block new_bb;

  if (!cfg_hooks->split_block)
    internal_error ("%s does not support split_block", cfg_hooks->name);

  new_bb = cfg_hooks->split_block (bb, i);
  if (!new_bb)
    return NULL;

  new_bb->count = bb->count;
  new_bb->frequency = bb->frequency;
  new_bb->loop_depth = bb->loop_depth;

  if (dom_info_available_p (CDI_DOMINATORS))
    {
      redirect_immediate_dominators (CDI_DOMINATORS, bb, new_bb);
      set_immediate_dominator (CDI_DOMINATORS, new_bb, bb);
    }

  return make_single_succ_edge (bb, new_bb, EDGE_FALLTHRU);
}

/* Splits block BB just after labels.  The newly created edge is returned.  */

edge
split_block_after_labels (basic_block bb)
{
  return split_block (bb, NULL);
}

/* Moves block BB immediately after block AFTER.  Returns false if the
   movement was impossible.  */

bool
move_block_after (basic_block bb, basic_block after)
{
  bool ret;

  if (!cfg_hooks->move_block_after)
    internal_error ("%s does not support move_block_after", cfg_hooks->name);

  ret = cfg_hooks->move_block_after (bb, after);

  return ret;
}

/* Deletes the basic block BB.  */

void
delete_basic_block (basic_block bb)
{
  if (!cfg_hooks->delete_basic_block)
    internal_error ("%s does not support delete_basic_block", cfg_hooks->name);

  cfg_hooks->delete_basic_block (bb);

  /* Remove the edges into and out of this block.  Note that there may
     indeed be edges in, if we are removing an unreachable loop.  */
  while (EDGE_COUNT (bb->preds) != 0)
    remove_edge (EDGE_PRED (bb, 0));
  while (EDGE_COUNT (bb->succs) != 0)
    remove_edge (EDGE_SUCC (bb, 0));

  if (dom_computed[CDI_DOMINATORS])
    delete_from_dominance_info (CDI_DOMINATORS, bb);
  if (dom_computed[CDI_POST_DOMINATORS])
    delete_from_dominance_info (CDI_POST_DOMINATORS, bb);

  /* Remove the basic block from the array.  */
  expunge_block (bb);
}

/* Splits edge E and returns the newly created basic block.  */

basic_block
split_edge (edge e)
{
  basic_block ret;
  gcov_type count = e->count;
  int freq = EDGE_FREQUENCY (e);
  edge f;
  bool irr = (e->flags & EDGE_IRREDUCIBLE_LOOP) != 0;

  if (!cfg_hooks->split_edge)
    internal_error ("%s does not support split_edge", cfg_hooks->name);

  ret = cfg_hooks->split_edge (e);
  ret->count = count;
  ret->frequency = freq;
  single_succ_edge (ret)->probability = REG_BR_PROB_BASE;
  single_succ_edge (ret)->count = count;

  if (irr)
    {
      ret->flags |= BB_IRREDUCIBLE_LOOP;
      single_pred_edge (ret)->flags |= EDGE_IRREDUCIBLE_LOOP;
      single_succ_edge (ret)->flags |= EDGE_IRREDUCIBLE_LOOP;
    }

  if (dom_computed[CDI_DOMINATORS])
    set_immediate_dominator (CDI_DOMINATORS, ret, single_pred (ret));

  if (dom_computed[CDI_DOMINATORS] >= DOM_NO_FAST_QUERY)
    {
      /* There are two cases:

	 If the immediate dominator of e->dest is not e->src, it
	 remains unchanged.

	 If immediate dominator of e->dest is e->src, it may become
	 ret, provided that all other predecessors of e->dest are
	 dominated by e->dest.  */

      if (get_immediate_dominator (CDI_DOMINATORS, single_succ (ret))
	  == single_pred (ret))
	{
	  edge_iterator ei;
	  FOR_EACH_EDGE (f, ei, single_succ (ret)->preds)
	    {
	      if (f == single_succ_edge (ret))
		continue;

	      if (!dominated_by_p (CDI_DOMINATORS, f->src,
				   single_succ (ret)))
		break;
	    }

	  if (!f)
	    set_immediate_dominator (CDI_DOMINATORS, single_succ (ret), ret);
	}
    };

  return ret;
}

/* Creates a new basic block just after the basic block AFTER.
   HEAD and END are the first and the last statement belonging
   to the block.  If both are NULL, an empty block is created.  */

basic_block
create_basic_block (void *head, void *end, basic_block after)
{
  basic_block ret;

  if (!cfg_hooks->create_basic_block)
    internal_error ("%s does not support create_basic_block", cfg_hooks->name);

  ret = cfg_hooks->create_basic_block (head, end, after);

  if (dom_computed[CDI_DOMINATORS])
    add_to_dominance_info (CDI_DOMINATORS, ret);
  if (dom_computed[CDI_POST_DOMINATORS])
    add_to_dominance_info (CDI_POST_DOMINATORS, ret);

  return ret;
}

/* Creates an empty basic block just after basic block AFTER.  */

basic_block
create_empty_bb (basic_block after)
{
  return create_basic_block (NULL, NULL, after);
}

/* Checks whether we may merge blocks BB1 and BB2.  */

bool
can_merge_blocks_p (basic_block bb1, basic_block bb2)
{
  bool ret;

  if (!cfg_hooks->can_merge_blocks_p)
    internal_error ("%s does not support can_merge_blocks_p", cfg_hooks->name);

  ret = cfg_hooks->can_merge_blocks_p (bb1, bb2);

  return ret;
}

void
predict_edge (edge e, enum br_predictor predictor, int probability)
{
  if (!cfg_hooks->predict_edge)
    internal_error ("%s does not support predict_edge", cfg_hooks->name);

  cfg_hooks->predict_edge (e, predictor, probability);
}

bool
predicted_by_p (basic_block bb, enum br_predictor predictor)
{
  if (!cfg_hooks->predict_edge)
    internal_error ("%s does not support predicted_by_p", cfg_hooks->name);

  return cfg_hooks->predicted_by_p (bb, predictor);
}

/* Merges basic block B into basic block A.  */

void
merge_blocks (basic_block a, basic_block b)
{
  edge e;
  edge_iterator ei;

  if (!cfg_hooks->merge_blocks)
    internal_error ("%s does not support merge_blocks", cfg_hooks->name);

  cfg_hooks->merge_blocks (a, b);

  /* Normally there should only be one successor of A and that is B, but
     partway though the merge of blocks for conditional_execution we'll
     be merging a TEST block with THEN and ELSE successors.  Free the
     whole lot of them and hope the caller knows what they're doing.  */

  while (EDGE_COUNT (a->succs) != 0)
   remove_edge (EDGE_SUCC (a, 0));

  /* Adjust the edges out of B for the new owner.  */
  FOR_EACH_EDGE (e, ei, b->succs)
    e->src = a;
  a->succs = b->succs;
  a->flags |= b->flags;

  /* B hasn't quite yet ceased to exist.  Attempt to prevent mishap.  */
  b->preds = b->succs = NULL;

  if (dom_computed[CDI_DOMINATORS])
    redirect_immediate_dominators (CDI_DOMINATORS, b, a);

  if (dom_computed[CDI_DOMINATORS])
    delete_from_dominance_info (CDI_DOMINATORS, b);
  if (dom_computed[CDI_POST_DOMINATORS])
    delete_from_dominance_info (CDI_POST_DOMINATORS, b);

  expunge_block (b);
}

/* Split BB into entry part and the rest (the rest is the newly created block).
   Redirect those edges for that REDIRECT_EDGE_P returns true to the entry
   part.  Returns the edge connecting the entry part to the rest.  */

edge
make_forwarder_block (basic_block bb, bool (*redirect_edge_p) (edge),
		      void (*new_bb_cbk) (basic_block))
{
  edge e, fallthru;
  edge_iterator ei;
  basic_block dummy, jump;

  if (!cfg_hooks->make_forwarder_block)
    internal_error ("%s does not support make_forwarder_block",
		    cfg_hooks->name);

  fallthru = split_block_after_labels (bb);
  dummy = fallthru->src;
  bb = fallthru->dest;

  /* Redirect back edges we want to keep.  */
  for (ei = ei_start (dummy->preds); (e = ei_safe_edge (ei)); )
    {
      if (redirect_edge_p (e))
	{
	  ei_next (&ei);
	  continue;
	}

      dummy->frequency -= EDGE_FREQUENCY (e);
      dummy->count -= e->count;
      if (dummy->frequency < 0)
	dummy->frequency = 0;
      if (dummy->count < 0)
	dummy->count = 0;
      fallthru->count -= e->count;
      if (fallthru->count < 0)
	fallthru->count = 0;

      jump = redirect_edge_and_branch_force (e, bb);
      if (jump)
	new_bb_cbk (jump);
    }

  if (dom_info_available_p (CDI_DOMINATORS))
    {
      basic_block doms_to_fix[2];

      doms_to_fix[0] = dummy;
      doms_to_fix[1] = bb;
      iterate_fix_dominators (CDI_DOMINATORS, doms_to_fix, 2);
    }

  cfg_hooks->make_forwarder_block (fallthru);

  return fallthru;
}

void
tidy_fallthru_edge (edge e)
{
  if (cfg_hooks->tidy_fallthru_edge)
    cfg_hooks->tidy_fallthru_edge (e);
}

/* Fix up edges that now fall through, or rather should now fall through
   but previously required a jump around now deleted blocks.  Simplify
   the search by only examining blocks numerically adjacent, since this
   is how find_basic_blocks created them.  */

void
tidy_fallthru_edges (void)
{
  basic_block b, c;

  if (!cfg_hooks->tidy_fallthru_edge)
    return;

  if (ENTRY_BLOCK_PTR->next_bb == EXIT_BLOCK_PTR)
    return;

  FOR_BB_BETWEEN (b, ENTRY_BLOCK_PTR->next_bb, EXIT_BLOCK_PTR->prev_bb, next_bb)
    {
      edge s;

      c = b->next_bb;

      /* We care about simple conditional or unconditional jumps with
	 a single successor.

	 If we had a conditional branch to the next instruction when
	 find_basic_blocks was called, then there will only be one
	 out edge for the block which ended with the conditional
	 branch (since we do not create duplicate edges).

	 Furthermore, the edge will be marked as a fallthru because we
	 merge the flags for the duplicate edges.  So we do not want to
	 check that the edge is not a FALLTHRU edge.  */

      if (single_succ_p (b))
	{
	  s = single_succ_edge (b);
	  if (! (s->flags & EDGE_COMPLEX)
	      && s->dest == c
	      && !find_reg_note (BB_END (b), REG_CROSSING_JUMP, NULL_RTX))
	    tidy_fallthru_edge (s);
	}
    }
}

/* Returns true if we can duplicate basic block BB.  */

bool
can_duplicate_block_p (basic_block bb)
{
  edge e;

  if (!cfg_hooks->can_duplicate_block_p)
    internal_error ("%s does not support can_duplicate_block_p",
		    cfg_hooks->name);

  if (bb == EXIT_BLOCK_PTR || bb == ENTRY_BLOCK_PTR)
    return false;

  /* Duplicating fallthru block to exit would require adding a jump
     and splitting the real last BB.  */
  e = find_edge (bb, EXIT_BLOCK_PTR);
  if (e && (e->flags & EDGE_FALLTHRU))
    return false;

  return cfg_hooks->can_duplicate_block_p (bb);
}

/* Duplicates basic block BB and redirects edge E to it.  Returns the
   new basic block.  The new basic block is placed after the basic block
   AFTER.  */

basic_block
duplicate_block (basic_block bb, edge e, basic_block after)
{
  edge s, n;
  basic_block new_bb;
  gcov_type new_count = e ? e->count : 0;
  edge_iterator ei;

  if (!cfg_hooks->duplicate_block)
    internal_error ("%s does not support duplicate_block",
		    cfg_hooks->name);

  if (bb->count < new_count)
    new_count = bb->count;

#ifdef ENABLE_CHECKING
  gcc_assert (can_duplicate_block_p (bb));
#endif

  new_bb = cfg_hooks->duplicate_block (bb);
  if (after)
    move_block_after (new_bb, after);

  new_bb->loop_depth = bb->loop_depth;
  new_bb->flags = bb->flags;
  FOR_EACH_EDGE (s, ei, bb->succs)
    {
      /* Since we are creating edges from a new block to successors
	 of another block (which therefore are known to be disjoint), there
	 is no need to actually check for duplicated edges.  */
      n = unchecked_make_edge (new_bb, s->dest, s->flags);
      n->probability = s->probability;
      if (e && bb->count)
	{
	  /* Take care for overflows!  */
	  n->count = s->count * (new_count * 10000 / bb->count) / 10000;
	  s->count -= n->count;
	}
      else
	n->count = s->count;
      n->aux = s->aux;
    }

  if (e)
    {
      new_bb->count = new_count;
      bb->count -= new_count;

      new_bb->frequency = EDGE_FREQUENCY (e);
      bb->frequency -= EDGE_FREQUENCY (e);

      redirect_edge_and_branch_force (e, new_bb);

      if (bb->count < 0)
	bb->count = 0;
      if (bb->frequency < 0)
	bb->frequency = 0;
    }
  else
    {
      new_bb->count = bb->count;
      new_bb->frequency = bb->frequency;
    }

  set_bb_original (new_bb, bb);
  set_bb_copy (bb, new_bb);

  return new_bb;
}

/* Return 1 if BB ends with a call, possibly followed by some
   instructions that must stay with the call, 0 otherwise.  */

bool
block_ends_with_call_p (basic_block bb)
{
  if (!cfg_hooks->block_ends_with_call_p)
    internal_error ("%s does not support block_ends_with_call_p", cfg_hooks->name);

  return (cfg_hooks->block_ends_with_call_p) (bb);
}

/* Return 1 if BB ends with a conditional branch, 0 otherwise.  */

bool
block_ends_with_condjump_p (basic_block bb)
{
  if (!cfg_hooks->block_ends_with_condjump_p)
    internal_error ("%s does not support block_ends_with_condjump_p",
		    cfg_hooks->name);

  return (cfg_hooks->block_ends_with_condjump_p) (bb);
}

/* Add fake edges to the function exit for any non constant and non noreturn
   calls, volatile inline assembly in the bitmap of blocks specified by
   BLOCKS or to the whole CFG if BLOCKS is zero.  Return the number of blocks
   that were split.

   The goal is to expose cases in which entering a basic block does not imply
   that all subsequent instructions must be executed.  */

int
flow_call_edges_add (sbitmap blocks)
{
  if (!cfg_hooks->flow_call_edges_add)
    internal_error ("%s does not support flow_call_edges_add",
		    cfg_hooks->name);

  return (cfg_hooks->flow_call_edges_add) (blocks);
}

/* This function is called immediately after edge E is added to the
   edge vector E->dest->preds.  */

void
execute_on_growing_pred (edge e)
{
  if (cfg_hooks->execute_on_growing_pred)
    cfg_hooks->execute_on_growing_pred (e);
}

/* This function is called immediately before edge E is removed from
   the edge vector E->dest->preds.  */

void
execute_on_shrinking_pred (edge e)
{
  if (cfg_hooks->execute_on_shrinking_pred)
    cfg_hooks->execute_on_shrinking_pred (e);
}

/* This is used inside loop versioning when we want to insert
   stmts/insns on the edges, which have a different behavior
   in tree's and in RTL, so we made a CFG hook.  */
void
lv_flush_pending_stmts (edge e)
{
  if (cfg_hooks->flush_pending_stmts)
    cfg_hooks->flush_pending_stmts (e);
}

/* Loop versioning uses the duplicate_loop_to_header_edge to create
   a new version of the loop basic-blocks, the parameters here are
   exactly the same as in duplicate_loop_to_header_edge or
   tree_duplicate_loop_to_header_edge; while in tree-ssa there is
   additional work to maintain ssa information that's why there is
   a need to call the tree_duplicate_loop_to_header_edge rather
   than duplicate_loop_to_header_edge when we are in tree mode.  */
bool
cfg_hook_duplicate_loop_to_header_edge (struct loop *loop, edge e,
					struct loops *loops, unsigned int ndupl,
					sbitmap wont_exit, edge orig,
					edge *to_remove,
					unsigned int *n_to_remove, int flags)
{
  gcc_assert (cfg_hooks->cfg_hook_duplicate_loop_to_header_edge);
  return cfg_hooks->cfg_hook_duplicate_loop_to_header_edge (loop, e, loops,
							    ndupl, wont_exit,
							    orig, to_remove,
							    n_to_remove, flags);
}

/* Conditional jumps are represented differently in trees and RTL,
   this hook takes a basic block that is known to have a cond jump
   at its end and extracts the taken and not taken eges out of it
   and store it in E1 and E2 respectively.  */
void
extract_cond_bb_edges (basic_block b, edge *e1, edge *e2)
{
  gcc_assert (cfg_hooks->extract_cond_bb_edges);
  cfg_hooks->extract_cond_bb_edges (b, e1, e2);
}

/* Responsible for updating the ssa info (PHI nodes) on the
   new condition basic block that guards the versioned loop.  */
void
lv_adjust_loop_header_phi (basic_block first, basic_block second,
			   basic_block new, edge e)
{
  if (cfg_hooks->lv_adjust_loop_header_phi)
    cfg_hooks->lv_adjust_loop_header_phi (first, second, new, e);
}

/* Conditions in trees and RTL are different so we need
   a different handling when we add the condition to the
   versioning code.  */
void
lv_add_condition_to_bb (basic_block first, basic_block second,
			basic_block new, void *cond)
{
  gcc_assert (cfg_hooks->lv_add_condition_to_bb);
  cfg_hooks->lv_add_condition_to_bb (first, second, new, cond);
}
