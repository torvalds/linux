/* Loop manipulation code for GNU compiler.
   Copyright (C) 2002, 2003, 2004, 2005 Free Software Foundation, Inc.

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
#include "rtl.h"
#include "hard-reg-set.h"
#include "obstack.h"
#include "basic-block.h"
#include "cfgloop.h"
#include "cfglayout.h"
#include "cfghooks.h"
#include "output.h"

static void duplicate_subloops (struct loops *, struct loop *, struct loop *);
static void copy_loops_to (struct loops *, struct loop **, int,
			   struct loop *);
static void loop_redirect_edge (edge, basic_block);
static bool loop_delete_branch_edge (edge, int);
static void remove_bbs (basic_block *, int);
static bool rpe_enum_p (basic_block, void *);
static int find_path (edge, basic_block **);
static bool alp_enum_p (basic_block, void *);
static void add_loop (struct loops *, struct loop *);
static void fix_loop_placements (struct loops *, struct loop *, bool *);
static bool fix_bb_placement (struct loops *, basic_block);
static void fix_bb_placements (struct loops *, basic_block, bool *);
static void place_new_loop (struct loops *, struct loop *);
static void scale_loop_frequencies (struct loop *, int, int);
static basic_block create_preheader (struct loop *, int);
static void unloop (struct loops *, struct loop *, bool *);

#define RDIV(X,Y) (((X) + (Y) / 2) / (Y))

/* Checks whether basic block BB is dominated by DATA.  */
static bool
rpe_enum_p (basic_block bb, void *data)
{
  return dominated_by_p (CDI_DOMINATORS, bb, data);
}

/* Remove basic blocks BBS from loop structure and dominance info,
   and delete them afterwards.  */
static void
remove_bbs (basic_block *bbs, int nbbs)
{
  int i;

  for (i = 0; i < nbbs; i++)
    {
      remove_bb_from_loops (bbs[i]);
      delete_basic_block (bbs[i]);
    }
}

/* Find path -- i.e. the basic blocks dominated by edge E and put them
   into array BBS, that will be allocated large enough to contain them.
   E->dest must have exactly one predecessor for this to work (it is
   easy to achieve and we do not put it here because we do not want to
   alter anything by this function).  The number of basic blocks in the
   path is returned.  */
static int
find_path (edge e, basic_block **bbs)
{
  gcc_assert (EDGE_COUNT (e->dest->preds) <= 1);

  /* Find bbs in the path.  */
  *bbs = XCNEWVEC (basic_block, n_basic_blocks);
  return dfs_enumerate_from (e->dest, 0, rpe_enum_p, *bbs,
			     n_basic_blocks, e->dest);
}

/* Fix placement of basic block BB inside loop hierarchy stored in LOOPS --
   Let L be a loop to that BB belongs.  Then every successor of BB must either
     1) belong to some superloop of loop L, or
     2) be a header of loop K such that K->outer is superloop of L
   Returns true if we had to move BB into other loop to enforce this condition,
   false if the placement of BB was already correct (provided that placements
   of its successors are correct).  */
static bool
fix_bb_placement (struct loops *loops, basic_block bb)
{
  edge e;
  edge_iterator ei;
  struct loop *loop = loops->tree_root, *act;

  FOR_EACH_EDGE (e, ei, bb->succs)
    {
      if (e->dest == EXIT_BLOCK_PTR)
	continue;

      act = e->dest->loop_father;
      if (act->header == e->dest)
	act = act->outer;

      if (flow_loop_nested_p (loop, act))
	loop = act;
    }

  if (loop == bb->loop_father)
    return false;

  remove_bb_from_loops (bb);
  add_bb_to_loop (bb, loop);

  return true;
}

/* Fix placements of basic blocks inside loop hierarchy stored in loops; i.e.
   enforce condition condition stated in description of fix_bb_placement. We
   start from basic block FROM that had some of its successors removed, so that
   his placement no longer has to be correct, and iteratively fix placement of
   its predecessors that may change if placement of FROM changed.  Also fix
   placement of subloops of FROM->loop_father, that might also be altered due
   to this change; the condition for them is similar, except that instead of
   successors we consider edges coming out of the loops.
 
   If the changes may invalidate the information about irreducible regions,
   IRRED_INVALIDATED is set to true.  */

static void
fix_bb_placements (struct loops *loops, basic_block from,
		   bool *irred_invalidated)
{
  sbitmap in_queue;
  basic_block *queue, *qtop, *qbeg, *qend;
  struct loop *base_loop;
  edge e;

  /* We pass through blocks back-reachable from FROM, testing whether some
     of their successors moved to outer loop.  It may be necessary to
     iterate several times, but it is finite, as we stop unless we move
     the basic block up the loop structure.  The whole story is a bit
     more complicated due to presence of subloops, those are moved using
     fix_loop_placement.  */

  base_loop = from->loop_father;
  if (base_loop == loops->tree_root)
    return;

  in_queue = sbitmap_alloc (last_basic_block);
  sbitmap_zero (in_queue);
  SET_BIT (in_queue, from->index);
  /* Prevent us from going out of the base_loop.  */
  SET_BIT (in_queue, base_loop->header->index);

  queue = XNEWVEC (basic_block, base_loop->num_nodes + 1);
  qtop = queue + base_loop->num_nodes + 1;
  qbeg = queue;
  qend = queue + 1;
  *qbeg = from;

  while (qbeg != qend)
    {
      edge_iterator ei;
      from = *qbeg;
      qbeg++;
      if (qbeg == qtop)
	qbeg = queue;
      RESET_BIT (in_queue, from->index);

      if (from->loop_father->header == from)
	{
	  /* Subloop header, maybe move the loop upward.  */
	  if (!fix_loop_placement (from->loop_father))
	    continue;
	}
      else
	{
	  /* Ordinary basic block.  */
	  if (!fix_bb_placement (loops, from))
	    continue;
	}

      FOR_EACH_EDGE (e, ei, from->succs)
	{
	  if (e->flags & EDGE_IRREDUCIBLE_LOOP)
	    *irred_invalidated = true;
	}

      /* Something has changed, insert predecessors into queue.  */
      FOR_EACH_EDGE (e, ei, from->preds)
	{
	  basic_block pred = e->src;
	  struct loop *nca;

	  if (e->flags & EDGE_IRREDUCIBLE_LOOP)
	    *irred_invalidated = true;

	  if (TEST_BIT (in_queue, pred->index))
	    continue;

	  /* If it is subloop, then it either was not moved, or
	     the path up the loop tree from base_loop do not contain
	     it.  */
	  nca = find_common_loop (pred->loop_father, base_loop);
	  if (pred->loop_father != base_loop
	      && (nca == base_loop
		  || nca != pred->loop_father))
	    pred = pred->loop_father->header;
	  else if (!flow_loop_nested_p (from->loop_father, pred->loop_father))
	    {
	      /* No point in processing it.  */
	      continue;
	    }

	  if (TEST_BIT (in_queue, pred->index))
	    continue;

	  /* Schedule the basic block.  */
	  *qend = pred;
	  qend++;
	  if (qend == qtop)
	    qend = queue;
	  SET_BIT (in_queue, pred->index);
	}
    }
  free (in_queue);
  free (queue);
}

/* Removes path beginning at edge E, i.e. remove basic blocks dominated by E
   and update loop structure stored in LOOPS and dominators.  Return true if
   we were able to remove the path, false otherwise (and nothing is affected
   then).  */
bool
remove_path (struct loops *loops, edge e)
{
  edge ae;
  basic_block *rem_bbs, *bord_bbs, *dom_bbs, from, bb;
  int i, nrem, n_bord_bbs, n_dom_bbs;
  sbitmap seen;
  bool deleted, irred_invalidated = false;

  if (!loop_delete_branch_edge (e, 0))
    return false;

  /* Keep track of whether we need to update information about irreducible
     regions.  This is the case if the removed area is a part of the
     irreducible region, or if the set of basic blocks that belong to a loop
     that is inside an irreducible region is changed, or if such a loop is
     removed.  */
  if (e->flags & EDGE_IRREDUCIBLE_LOOP)
    irred_invalidated = true;

  /* We need to check whether basic blocks are dominated by the edge
     e, but we only have basic block dominators.  This is easy to
     fix -- when e->dest has exactly one predecessor, this corresponds
     to blocks dominated by e->dest, if not, split the edge.  */
  if (!single_pred_p (e->dest))
    e = single_pred_edge (loop_split_edge_with (e, NULL_RTX));

  /* It may happen that by removing path we remove one or more loops
     we belong to.  In this case first unloop the loops, then proceed
     normally.   We may assume that e->dest is not a header of any loop,
     as it now has exactly one predecessor.  */
  while (e->src->loop_father->outer
	 && dominated_by_p (CDI_DOMINATORS,
			    e->src->loop_father->latch, e->dest))
    unloop (loops, e->src->loop_father, &irred_invalidated);

  /* Identify the path.  */
  nrem = find_path (e, &rem_bbs);

  n_bord_bbs = 0;
  bord_bbs = XCNEWVEC (basic_block, n_basic_blocks);
  seen = sbitmap_alloc (last_basic_block);
  sbitmap_zero (seen);

  /* Find "border" hexes -- i.e. those with predecessor in removed path.  */
  for (i = 0; i < nrem; i++)
    SET_BIT (seen, rem_bbs[i]->index);
  for (i = 0; i < nrem; i++)
    {
      edge_iterator ei;
      bb = rem_bbs[i];
      FOR_EACH_EDGE (ae, ei, rem_bbs[i]->succs)
	if (ae->dest != EXIT_BLOCK_PTR && !TEST_BIT (seen, ae->dest->index))
	  {
	    SET_BIT (seen, ae->dest->index);
	    bord_bbs[n_bord_bbs++] = ae->dest;
	  
	    if (ae->flags & EDGE_IRREDUCIBLE_LOOP)
	      irred_invalidated = true;
	  }
    }

  /* Remove the path.  */
  from = e->src;
  deleted = loop_delete_branch_edge (e, 1);
  gcc_assert (deleted);
  dom_bbs = XCNEWVEC (basic_block, n_basic_blocks);

  /* Cancel loops contained in the path.  */
  for (i = 0; i < nrem; i++)
    if (rem_bbs[i]->loop_father->header == rem_bbs[i])
      cancel_loop_tree (loops, rem_bbs[i]->loop_father);

  remove_bbs (rem_bbs, nrem);
  free (rem_bbs);

  /* Find blocks whose dominators may be affected.  */
  n_dom_bbs = 0;
  sbitmap_zero (seen);
  for (i = 0; i < n_bord_bbs; i++)
    {
      basic_block ldom;

      bb = get_immediate_dominator (CDI_DOMINATORS, bord_bbs[i]);
      if (TEST_BIT (seen, bb->index))
	continue;
      SET_BIT (seen, bb->index);

      for (ldom = first_dom_son (CDI_DOMINATORS, bb);
	   ldom;
	   ldom = next_dom_son (CDI_DOMINATORS, ldom))
	if (!dominated_by_p (CDI_DOMINATORS, from, ldom))
	  dom_bbs[n_dom_bbs++] = ldom;
    }

  free (seen);

  /* Recount dominators.  */
  iterate_fix_dominators (CDI_DOMINATORS, dom_bbs, n_dom_bbs);
  free (dom_bbs);
  free (bord_bbs);

  /* Fix placements of basic blocks inside loops and the placement of
     loops in the loop tree.  */
  fix_bb_placements (loops, from, &irred_invalidated);
  fix_loop_placements (loops, from->loop_father, &irred_invalidated);

  if (irred_invalidated
      && (loops->state & LOOPS_HAVE_MARKED_IRREDUCIBLE_REGIONS) != 0)
    mark_irreducible_loops (loops);

  return true;
}

/* Predicate for enumeration in add_loop.  */
static bool
alp_enum_p (basic_block bb, void *alp_header)
{
  return bb != (basic_block) alp_header;
}

/* Given LOOP structure with filled header and latch, find the body of the
   corresponding loop and add it to LOOPS tree.  */
static void
add_loop (struct loops *loops, struct loop *loop)
{
  basic_block *bbs;
  int i, n;

  /* Add it to loop structure.  */
  place_new_loop (loops, loop);
  loop->level = 1;

  /* Find its nodes.  */
  bbs = XCNEWVEC (basic_block, n_basic_blocks);
  n = dfs_enumerate_from (loop->latch, 1, alp_enum_p,
			  bbs, n_basic_blocks, loop->header);

  for (i = 0; i < n; i++)
    add_bb_to_loop (bbs[i], loop);
  add_bb_to_loop (loop->header, loop);

  free (bbs);
}

/* Multiply all frequencies in LOOP by NUM/DEN.  */
static void
scale_loop_frequencies (struct loop *loop, int num, int den)
{
  basic_block *bbs;

  bbs = get_loop_body (loop);
  scale_bbs_frequencies_int (bbs, loop->num_nodes, num, den);
  free (bbs);
}

/* Make area between HEADER_EDGE and LATCH_EDGE a loop by connecting
   latch to header and update loop tree stored in LOOPS and dominators
   accordingly. Everything between them plus LATCH_EDGE destination must
   be dominated by HEADER_EDGE destination, and back-reachable from
   LATCH_EDGE source.  HEADER_EDGE is redirected to basic block SWITCH_BB,
   FALSE_EDGE of SWITCH_BB to original destination of HEADER_EDGE and
   TRUE_EDGE of SWITCH_BB to original destination of LATCH_EDGE.
   Returns newly created loop.  */

struct loop *
loopify (struct loops *loops, edge latch_edge, edge header_edge,
	 basic_block switch_bb, edge true_edge, edge false_edge,
	 bool redirect_all_edges)
{
  basic_block succ_bb = latch_edge->dest;
  basic_block pred_bb = header_edge->src;
  basic_block *dom_bbs, *body;
  unsigned n_dom_bbs, i;
  sbitmap seen;
  struct loop *loop = XCNEW (struct loop);
  struct loop *outer = succ_bb->loop_father->outer;
  int freq, prob, tot_prob;
  gcov_type cnt;
  edge e;
  edge_iterator ei;

  loop->header = header_edge->dest;
  loop->latch = latch_edge->src;

  freq = EDGE_FREQUENCY (header_edge);
  cnt = header_edge->count;
  prob = EDGE_SUCC (switch_bb, 0)->probability;
  tot_prob = prob + EDGE_SUCC (switch_bb, 1)->probability;
  if (tot_prob == 0)
    tot_prob = 1;

  /* Redirect edges.  */
  loop_redirect_edge (latch_edge, loop->header);
  loop_redirect_edge (true_edge, succ_bb);

  /* During loop versioning, one of the switch_bb edge is already properly
     set. Do not redirect it again unless redirect_all_edges is true.  */
  if (redirect_all_edges)
    {
      loop_redirect_edge (header_edge, switch_bb);
      loop_redirect_edge (false_edge, loop->header);

      /* Update dominators.  */
      set_immediate_dominator (CDI_DOMINATORS, switch_bb, pred_bb);
      set_immediate_dominator (CDI_DOMINATORS, loop->header, switch_bb);
    }

  set_immediate_dominator (CDI_DOMINATORS, succ_bb, switch_bb);

  /* Compute new loop.  */
  add_loop (loops, loop);
  flow_loop_tree_node_add (outer, loop);

  /* Add switch_bb to appropriate loop.  */
  add_bb_to_loop (switch_bb, outer);

  /* Fix frequencies.  */
  switch_bb->frequency = freq;
  switch_bb->count = cnt;
  FOR_EACH_EDGE (e, ei, switch_bb->succs)
    e->count = (switch_bb->count * e->probability) / REG_BR_PROB_BASE;
  scale_loop_frequencies (loop, prob, tot_prob);
  scale_loop_frequencies (succ_bb->loop_father, tot_prob - prob, tot_prob);

  /* Update dominators of blocks outside of LOOP.  */
  dom_bbs = XCNEWVEC (basic_block, n_basic_blocks);
  n_dom_bbs = 0;
  seen = sbitmap_alloc (last_basic_block);
  sbitmap_zero (seen);
  body = get_loop_body (loop);

  for (i = 0; i < loop->num_nodes; i++)
    SET_BIT (seen, body[i]->index);

  for (i = 0; i < loop->num_nodes; i++)
    {
      basic_block ldom;

      for (ldom = first_dom_son (CDI_DOMINATORS, body[i]);
	   ldom;
	   ldom = next_dom_son (CDI_DOMINATORS, ldom))
	if (!TEST_BIT (seen, ldom->index))
	  {
	    SET_BIT (seen, ldom->index);
	    dom_bbs[n_dom_bbs++] = ldom;
	  }
    }

  iterate_fix_dominators (CDI_DOMINATORS, dom_bbs, n_dom_bbs);

  free (body);
  free (seen);
  free (dom_bbs);

  return loop;
}

/* Remove the latch edge of a LOOP and update LOOPS tree to indicate that
   the LOOP was removed.  After this function, original loop latch will
   have no successor, which caller is expected to fix somehow.

   If this may cause the information about irreducible regions to become
   invalid, IRRED_INVALIDATED is set to true.  */

static void
unloop (struct loops *loops, struct loop *loop, bool *irred_invalidated)
{
  basic_block *body;
  struct loop *ploop;
  unsigned i, n;
  basic_block latch = loop->latch;
  bool dummy = false;

  if (loop_preheader_edge (loop)->flags & EDGE_IRREDUCIBLE_LOOP)
    *irred_invalidated = true;

  /* This is relatively straightforward.  The dominators are unchanged, as
     loop header dominates loop latch, so the only thing we have to care of
     is the placement of loops and basic blocks inside the loop tree.  We
     move them all to the loop->outer, and then let fix_bb_placements do
     its work.  */

  body = get_loop_body (loop);
  n = loop->num_nodes;
  for (i = 0; i < n; i++)
    if (body[i]->loop_father == loop)
      {
	remove_bb_from_loops (body[i]);
	add_bb_to_loop (body[i], loop->outer);
      }
  free(body);

  while (loop->inner)
    {
      ploop = loop->inner;
      flow_loop_tree_node_remove (ploop);
      flow_loop_tree_node_add (loop->outer, ploop);
    }

  /* Remove the loop and free its data.  */
  flow_loop_tree_node_remove (loop);
  loops->parray[loop->num] = NULL;
  flow_loop_free (loop);

  remove_edge (single_succ_edge (latch));

  /* We do not pass IRRED_INVALIDATED to fix_bb_placements here, as even if
     there is an irreducible region inside the cancelled loop, the flags will
     be still correct.  */
  fix_bb_placements (loops, latch, &dummy);
}

/* Fix placement of LOOP inside loop tree, i.e. find the innermost superloop
   FATHER of LOOP such that all of the edges coming out of LOOP belong to
   FATHER, and set it as outer loop of LOOP.  Return true if placement of
   LOOP changed.  */

int
fix_loop_placement (struct loop *loop)
{
  basic_block *body;
  unsigned i;
  edge e;
  edge_iterator ei;
  struct loop *father = loop->pred[0], *act;

  body = get_loop_body (loop);
  for (i = 0; i < loop->num_nodes; i++)
    FOR_EACH_EDGE (e, ei, body[i]->succs)
      if (!flow_bb_inside_loop_p (loop, e->dest))
	{
	  act = find_common_loop (loop, e->dest->loop_father);
	  if (flow_loop_nested_p (father, act))
	    father = act;
	}
  free (body);

  if (father != loop->outer)
    {
      for (act = loop->outer; act != father; act = act->outer)
	act->num_nodes -= loop->num_nodes;
      flow_loop_tree_node_remove (loop);
      flow_loop_tree_node_add (father, loop);
      return 1;
    }
  return 0;
}

/* Fix placement of superloops of LOOP inside loop tree, i.e. ensure that
   condition stated in description of fix_loop_placement holds for them.
   It is used in case when we removed some edges coming out of LOOP, which
   may cause the right placement of LOOP inside loop tree to change.
 
   IRRED_INVALIDATED is set to true if a change in the loop structures might
   invalidate the information about irreducible regions.  */

static void
fix_loop_placements (struct loops *loops, struct loop *loop,
		     bool *irred_invalidated)
{
  struct loop *outer;

  while (loop->outer)
    {
      outer = loop->outer;
      if (!fix_loop_placement (loop))
	break;

      /* Changing the placement of a loop in the loop tree may alter the
	 validity of condition 2) of the description of fix_bb_placement
	 for its preheader, because the successor is the header and belongs
	 to the loop.  So call fix_bb_placements to fix up the placement
	 of the preheader and (possibly) of its predecessors.  */
      fix_bb_placements (loops, loop_preheader_edge (loop)->src,
			 irred_invalidated);
      loop = outer;
    }
}

/* Creates place for a new LOOP in LOOPS structure.  */
static void
place_new_loop (struct loops *loops, struct loop *loop)
{
  loops->parray =
    xrealloc (loops->parray, (loops->num + 1) * sizeof (struct loop *));
  loops->parray[loops->num] = loop;

  loop->num = loops->num++;
}

/* Copies copy of LOOP as subloop of TARGET loop, placing newly
   created loop into LOOPS structure.  */
struct loop *
duplicate_loop (struct loops *loops, struct loop *loop, struct loop *target)
{
  struct loop *cloop;
  cloop = XCNEW (struct loop);
  place_new_loop (loops, cloop);

  /* Initialize copied loop.  */
  cloop->level = loop->level;

  /* Set it as copy of loop.  */
  loop->copy = cloop;

  /* Add it to target.  */
  flow_loop_tree_node_add (target, cloop);

  return cloop;
}

/* Copies structure of subloops of LOOP into TARGET loop, placing
   newly created loops into loop tree stored in LOOPS.  */
static void
duplicate_subloops (struct loops *loops, struct loop *loop, struct loop *target)
{
  struct loop *aloop, *cloop;

  for (aloop = loop->inner; aloop; aloop = aloop->next)
    {
      cloop = duplicate_loop (loops, aloop, target);
      duplicate_subloops (loops, aloop, cloop);
    }
}

/* Copies structure of subloops of N loops, stored in array COPIED_LOOPS,
   into TARGET loop, placing newly created loops into loop tree LOOPS.  */
static void
copy_loops_to (struct loops *loops, struct loop **copied_loops, int n, struct loop *target)
{
  struct loop *aloop;
  int i;

  for (i = 0; i < n; i++)
    {
      aloop = duplicate_loop (loops, copied_loops[i], target);
      duplicate_subloops (loops, copied_loops[i], aloop);
    }
}

/* Redirects edge E to basic block DEST.  */
static void
loop_redirect_edge (edge e, basic_block dest)
{
  if (e->dest == dest)
    return;

  redirect_edge_and_branch_force (e, dest);
}

/* Deletes edge E from a branch if possible.  Unless REALLY_DELETE is set,
   just test whether it is possible to remove the edge.  */
static bool
loop_delete_branch_edge (edge e, int really_delete)
{
  basic_block src = e->src;
  basic_block newdest;
  int irr;
  edge snd;

  gcc_assert (EDGE_COUNT (src->succs) > 1);

  /* Cannot handle more than two exit edges.  */
  if (EDGE_COUNT (src->succs) > 2)
    return false;
  /* And it must be just a simple branch.  */
  if (!any_condjump_p (BB_END (src)))
    return false;

  snd = e == EDGE_SUCC (src, 0) ? EDGE_SUCC (src, 1) : EDGE_SUCC (src, 0);
  newdest = snd->dest;
  if (newdest == EXIT_BLOCK_PTR)
    return false;

  /* Hopefully the above conditions should suffice.  */
  if (!really_delete)
    return true;

  /* Redirecting behaves wrongly wrto this flag.  */
  irr = snd->flags & EDGE_IRREDUCIBLE_LOOP;

  if (!redirect_edge_and_branch (e, newdest))
    return false;
  single_succ_edge (src)->flags &= ~EDGE_IRREDUCIBLE_LOOP;
  single_succ_edge (src)->flags |= irr;

  return true;
}

/* Check whether LOOP's body can be duplicated.  */
bool
can_duplicate_loop_p (struct loop *loop)
{
  int ret;
  basic_block *bbs = get_loop_body (loop);

  ret = can_copy_bbs_p (bbs, loop->num_nodes);
  free (bbs);

  return ret;
}

/* The NBBS blocks in BBS will get duplicated and the copies will be placed
   to LOOP.  Update the single_exit information in superloops of LOOP.  */

static void
update_single_exits_after_duplication (basic_block *bbs, unsigned nbbs,
				       struct loop *loop)
{
  unsigned i;

  for (i = 0; i < nbbs; i++)
    bbs[i]->flags |= BB_DUPLICATED;

  for (; loop->outer; loop = loop->outer)
    {
      if (!loop->single_exit)
	continue;

      if (loop->single_exit->src->flags & BB_DUPLICATED)
	loop->single_exit = NULL;
    }

  for (i = 0; i < nbbs; i++)
    bbs[i]->flags &= ~BB_DUPLICATED;
}

/* Duplicates body of LOOP to given edge E NDUPL times.  Takes care of updating
   LOOPS structure and dominators.  E's destination must be LOOP header for
   this to work, i.e. it must be entry or latch edge of this loop; these are
   unique, as the loops must have preheaders for this function to work
   correctly (in case E is latch, the function unrolls the loop, if E is entry
   edge, it peels the loop).  Store edges created by copying ORIG edge from
   copies corresponding to set bits in WONT_EXIT bitmap (bit 0 corresponds to
   original LOOP body, the other copies are numbered in order given by control
   flow through them) into TO_REMOVE array.  Returns false if duplication is
   impossible.  */
bool
duplicate_loop_to_header_edge (struct loop *loop, edge e, struct loops *loops,
			       unsigned int ndupl, sbitmap wont_exit,
			       edge orig, edge *to_remove,
			       unsigned int *n_to_remove, int flags)
{
  struct loop *target, *aloop;
  struct loop **orig_loops;
  unsigned n_orig_loops;
  basic_block header = loop->header, latch = loop->latch;
  basic_block *new_bbs, *bbs, *first_active;
  basic_block new_bb, bb, first_active_latch = NULL;
  edge ae, latch_edge;
  edge spec_edges[2], new_spec_edges[2];
#define SE_LATCH 0
#define SE_ORIG 1
  unsigned i, j, n;
  int is_latch = (latch == e->src);
  int scale_act = 0, *scale_step = NULL, scale_main = 0;
  int p, freq_in, freq_le, freq_out_orig;
  int prob_pass_thru, prob_pass_wont_exit, prob_pass_main;
  int add_irreducible_flag;
  basic_block place_after;

  gcc_assert (e->dest == loop->header);
  gcc_assert (ndupl > 0);

  if (orig)
    {
      /* Orig must be edge out of the loop.  */
      gcc_assert (flow_bb_inside_loop_p (loop, orig->src));
      gcc_assert (!flow_bb_inside_loop_p (loop, orig->dest));
    }

  n = loop->num_nodes;
  bbs = get_loop_body_in_dom_order (loop);
  gcc_assert (bbs[0] == loop->header);
  gcc_assert (bbs[n  - 1] == loop->latch);

  /* Check whether duplication is possible.  */
  if (!can_copy_bbs_p (bbs, loop->num_nodes))
    {
      free (bbs);
      return false;
    }
  new_bbs = XNEWVEC (basic_block, loop->num_nodes);

  /* In case we are doing loop peeling and the loop is in the middle of
     irreducible region, the peeled copies will be inside it too.  */
  add_irreducible_flag = e->flags & EDGE_IRREDUCIBLE_LOOP;
  gcc_assert (!is_latch || !add_irreducible_flag);

  /* Find edge from latch.  */
  latch_edge = loop_latch_edge (loop);

  if (flags & DLTHE_FLAG_UPDATE_FREQ)
    {
      /* Calculate coefficients by that we have to scale frequencies
	 of duplicated loop bodies.  */
      freq_in = header->frequency;
      freq_le = EDGE_FREQUENCY (latch_edge);
      if (freq_in == 0)
	freq_in = 1;
      if (freq_in < freq_le)
	freq_in = freq_le;
      freq_out_orig = orig ? EDGE_FREQUENCY (orig) : freq_in - freq_le;
      if (freq_out_orig > freq_in - freq_le)
	freq_out_orig = freq_in - freq_le;
      prob_pass_thru = RDIV (REG_BR_PROB_BASE * freq_le, freq_in);
      prob_pass_wont_exit =
	      RDIV (REG_BR_PROB_BASE * (freq_le + freq_out_orig), freq_in);

      scale_step = XNEWVEC (int, ndupl);

	for (i = 1; i <= ndupl; i++)
	  scale_step[i - 1] = TEST_BIT (wont_exit, i)
				? prob_pass_wont_exit
				: prob_pass_thru;

      /* Complete peeling is special as the probability of exit in last
	 copy becomes 1.  */
      if (flags & DLTHE_FLAG_COMPLETTE_PEEL)
	{
	  int wanted_freq = EDGE_FREQUENCY (e);

	  if (wanted_freq > freq_in)
	    wanted_freq = freq_in;

	  gcc_assert (!is_latch);
	  /* First copy has frequency of incoming edge.  Each subsequent
	     frequency should be reduced by prob_pass_wont_exit.  Caller
	     should've managed the flags so all except for original loop
	     has won't exist set.  */
	  scale_act = RDIV (wanted_freq * REG_BR_PROB_BASE, freq_in);
	  /* Now simulate the duplication adjustments and compute header
	     frequency of the last copy.  */
	  for (i = 0; i < ndupl; i++)
	    wanted_freq = RDIV (wanted_freq * scale_step[i], REG_BR_PROB_BASE);
	  scale_main = RDIV (wanted_freq * REG_BR_PROB_BASE, freq_in);
	}
      else if (is_latch)
	{
	  prob_pass_main = TEST_BIT (wont_exit, 0)
				? prob_pass_wont_exit
				: prob_pass_thru;
	  p = prob_pass_main;
	  scale_main = REG_BR_PROB_BASE;
	  for (i = 0; i < ndupl; i++)
	    {
	      scale_main += p;
	      p = RDIV (p * scale_step[i], REG_BR_PROB_BASE);
	    }
	  scale_main = RDIV (REG_BR_PROB_BASE * REG_BR_PROB_BASE, scale_main);
	  scale_act = RDIV (scale_main * prob_pass_main, REG_BR_PROB_BASE);
	}
      else
	{
	  scale_main = REG_BR_PROB_BASE;
	  for (i = 0; i < ndupl; i++)
	    scale_main = RDIV (scale_main * scale_step[i], REG_BR_PROB_BASE);
	  scale_act = REG_BR_PROB_BASE - prob_pass_thru;
	}
      for (i = 0; i < ndupl; i++)
	gcc_assert (scale_step[i] >= 0 && scale_step[i] <= REG_BR_PROB_BASE);
      gcc_assert (scale_main >= 0 && scale_main <= REG_BR_PROB_BASE
		  && scale_act >= 0  && scale_act <= REG_BR_PROB_BASE);
    }

  /* Loop the new bbs will belong to.  */
  target = e->src->loop_father;

  /* Original loops.  */
  n_orig_loops = 0;
  for (aloop = loop->inner; aloop; aloop = aloop->next)
    n_orig_loops++;
  orig_loops = XCNEWVEC (struct loop *, n_orig_loops);
  for (aloop = loop->inner, i = 0; aloop; aloop = aloop->next, i++)
    orig_loops[i] = aloop;

  loop->copy = target;

  first_active = XNEWVEC (basic_block, n);
  if (is_latch)
    {
      memcpy (first_active, bbs, n * sizeof (basic_block));
      first_active_latch = latch;
    }

  /* Update the information about single exits.  */
  if (loops->state & LOOPS_HAVE_MARKED_SINGLE_EXITS)
    update_single_exits_after_duplication (bbs, n, target);

  /* Record exit edge in original loop body.  */
  if (orig && TEST_BIT (wont_exit, 0))
    to_remove[(*n_to_remove)++] = orig;

  spec_edges[SE_ORIG] = orig;
  spec_edges[SE_LATCH] = latch_edge;

  place_after = e->src;
  for (j = 0; j < ndupl; j++)
    {
      /* Copy loops.  */
      copy_loops_to (loops, orig_loops, n_orig_loops, target);

      /* Copy bbs.  */
      copy_bbs (bbs, n, new_bbs, spec_edges, 2, new_spec_edges, loop,
		place_after);
      place_after = new_spec_edges[SE_LATCH]->src;

      if (flags & DLTHE_RECORD_COPY_NUMBER)
	for (i = 0; i < n; i++)
	  {
	    gcc_assert (!new_bbs[i]->aux);
	    new_bbs[i]->aux = (void *)(size_t)(j + 1);
	  }

      /* Note whether the blocks and edges belong to an irreducible loop.  */
      if (add_irreducible_flag)
	{
	  for (i = 0; i < n; i++)
	    new_bbs[i]->flags |= BB_DUPLICATED;
	  for (i = 0; i < n; i++)
	    {
	      edge_iterator ei;
	      new_bb = new_bbs[i];
	      if (new_bb->loop_father == target)
		new_bb->flags |= BB_IRREDUCIBLE_LOOP;

	      FOR_EACH_EDGE (ae, ei, new_bb->succs)
		if ((ae->dest->flags & BB_DUPLICATED)
		    && (ae->src->loop_father == target
			|| ae->dest->loop_father == target))
		  ae->flags |= EDGE_IRREDUCIBLE_LOOP;
	    }
	  for (i = 0; i < n; i++)
	    new_bbs[i]->flags &= ~BB_DUPLICATED;
	}

      /* Redirect the special edges.  */
      if (is_latch)
	{
	  redirect_edge_and_branch_force (latch_edge, new_bbs[0]);
	  redirect_edge_and_branch_force (new_spec_edges[SE_LATCH],
					  loop->header);
	  set_immediate_dominator (CDI_DOMINATORS, new_bbs[0], latch);
	  latch = loop->latch = new_bbs[n - 1];
	  e = latch_edge = new_spec_edges[SE_LATCH];
	}
      else
	{
	  redirect_edge_and_branch_force (new_spec_edges[SE_LATCH],
					  loop->header);
	  redirect_edge_and_branch_force (e, new_bbs[0]);
	  set_immediate_dominator (CDI_DOMINATORS, new_bbs[0], e->src);
	  e = new_spec_edges[SE_LATCH];
	}

      /* Record exit edge in this copy.  */
      if (orig && TEST_BIT (wont_exit, j + 1))
	to_remove[(*n_to_remove)++] = new_spec_edges[SE_ORIG];

      /* Record the first copy in the control flow order if it is not
	 the original loop (i.e. in case of peeling).  */
      if (!first_active_latch)
	{
	  memcpy (first_active, new_bbs, n * sizeof (basic_block));
	  first_active_latch = new_bbs[n - 1];
	}

      /* Set counts and frequencies.  */
      if (flags & DLTHE_FLAG_UPDATE_FREQ)
	{
	  scale_bbs_frequencies_int (new_bbs, n, scale_act, REG_BR_PROB_BASE);
	  scale_act = RDIV (scale_act * scale_step[j], REG_BR_PROB_BASE);
	}
    }
  free (new_bbs);
  free (orig_loops);

  /* Update the original loop.  */
  if (!is_latch)
    set_immediate_dominator (CDI_DOMINATORS, e->dest, e->src);
  if (flags & DLTHE_FLAG_UPDATE_FREQ)
    {
      scale_bbs_frequencies_int (bbs, n, scale_main, REG_BR_PROB_BASE);
      free (scale_step);
    }

  /* Update dominators of outer blocks if affected.  */
  for (i = 0; i < n; i++)
    {
      basic_block dominated, dom_bb, *dom_bbs;
      int n_dom_bbs,j;

      bb = bbs[i];
      bb->aux = 0;

      n_dom_bbs = get_dominated_by (CDI_DOMINATORS, bb, &dom_bbs);
      for (j = 0; j < n_dom_bbs; j++)
	{
	  dominated = dom_bbs[j];
	  if (flow_bb_inside_loop_p (loop, dominated))
	    continue;
	  dom_bb = nearest_common_dominator (
			CDI_DOMINATORS, first_active[i], first_active_latch);
	  set_immediate_dominator (CDI_DOMINATORS, dominated, dom_bb);
	}
      free (dom_bbs);
    }
  free (first_active);

  free (bbs);

  return true;
}

/* A callback for make_forwarder block, to redirect all edges except for
   MFB_KJ_EDGE to the entry part.  E is the edge for that we should decide
   whether to redirect it.  */

static edge mfb_kj_edge;
static bool
mfb_keep_just (edge e)
{
  return e != mfb_kj_edge;
}

/* A callback for make_forwarder block, to update data structures for a basic
   block JUMP created by redirecting an edge (only the latch edge is being
   redirected).  */

static void
mfb_update_loops (basic_block jump)
{
  struct loop *loop = single_succ (jump)->loop_father;

  if (dom_computed[CDI_DOMINATORS])
    set_immediate_dominator (CDI_DOMINATORS, jump, single_pred (jump));
  add_bb_to_loop (jump, loop);
  loop->latch = jump;
}

/* Creates a pre-header for a LOOP.  Returns newly created block.  Unless
   CP_SIMPLE_PREHEADERS is set in FLAGS, we only force LOOP to have single
   entry; otherwise we also force preheader block to have only one successor.
   The function also updates dominators.  */

static basic_block
create_preheader (struct loop *loop, int flags)
{
  edge e, fallthru;
  basic_block dummy;
  struct loop *cloop, *ploop;
  int nentry = 0;
  bool irred = false;
  bool latch_edge_was_fallthru;
  edge one_succ_pred = 0;
  edge_iterator ei;

  cloop = loop->outer;

  FOR_EACH_EDGE (e, ei, loop->header->preds)
    {
      if (e->src == loop->latch)
	continue;
      irred |= (e->flags & EDGE_IRREDUCIBLE_LOOP) != 0;
      nentry++;
      if (single_succ_p (e->src))
	one_succ_pred = e;
    }
  gcc_assert (nentry);
  if (nentry == 1)
    {
      /* Get an edge that is different from the one from loop->latch
	 to loop->header.  */
      e = EDGE_PRED (loop->header,
		     EDGE_PRED (loop->header, 0)->src == loop->latch);

      if (!(flags & CP_SIMPLE_PREHEADERS) || single_succ_p (e->src))
	return NULL;
    }

  mfb_kj_edge = loop_latch_edge (loop);
  latch_edge_was_fallthru = (mfb_kj_edge->flags & EDGE_FALLTHRU) != 0;
  fallthru = make_forwarder_block (loop->header, mfb_keep_just,
				   mfb_update_loops);
  dummy = fallthru->src;
  loop->header = fallthru->dest;

  /* The header could be a latch of some superloop(s); due to design of
     split_block, it would now move to fallthru->dest.  */
  for (ploop = loop; ploop; ploop = ploop->outer)
    if (ploop->latch == dummy)
      ploop->latch = fallthru->dest;

  /* Try to be clever in placing the newly created preheader.  The idea is to
     avoid breaking any "fallthruness" relationship between blocks.

     The preheader was created just before the header and all incoming edges
     to the header were redirected to the preheader, except the latch edge.
     So the only problematic case is when this latch edge was a fallthru
     edge: it is not anymore after the preheader creation so we have broken
     the fallthruness.  We're therefore going to look for a better place.  */
  if (latch_edge_was_fallthru)
    {
      if (one_succ_pred)
	e = one_succ_pred;
      else
	e = EDGE_PRED (dummy, 0);

      move_block_after (dummy, e->src);
    }

  loop->header->loop_father = loop;
  add_bb_to_loop (dummy, cloop);

  if (irred)
    {
      dummy->flags |= BB_IRREDUCIBLE_LOOP;
      single_succ_edge (dummy)->flags |= EDGE_IRREDUCIBLE_LOOP;
    }

  if (dump_file)
    fprintf (dump_file, "Created preheader block for loop %i\n",
	     loop->num);

  return dummy;
}

/* Create preheaders for each loop from loop tree stored in LOOPS; for meaning
   of FLAGS see create_preheader.  */
void
create_preheaders (struct loops *loops, int flags)
{
  unsigned i;
  for (i = 1; i < loops->num; i++)
    create_preheader (loops->parray[i], flags);
  loops->state |= LOOPS_HAVE_PREHEADERS;
}

/* Forces all loop latches of loops from loop tree LOOPS to have only single
   successor.  */
void
force_single_succ_latches (struct loops *loops)
{
  unsigned i;
  struct loop *loop;
  edge e;

  for (i = 1; i < loops->num; i++)
    {
      loop = loops->parray[i];
      if (loop->latch != loop->header && single_succ_p (loop->latch))
	continue;

      e = find_edge (loop->latch, loop->header);

      loop_split_edge_with (e, NULL_RTX);
    }
  loops->state |= LOOPS_HAVE_SIMPLE_LATCHES;
}

/* A quite stupid function to put INSNS on edge E. They are supposed to form
   just one basic block.  Jumps in INSNS are not handled, so cfg do not have to
   be ok after this function.  The created block is placed on correct place
   in LOOPS structure and its dominator is set.  */
basic_block
loop_split_edge_with (edge e, rtx insns)
{
  basic_block src, dest, new_bb;
  struct loop *loop_c;

  src = e->src;
  dest = e->dest;

  loop_c = find_common_loop (src->loop_father, dest->loop_father);

  /* Create basic block for it.  */

  new_bb = split_edge (e);
  add_bb_to_loop (new_bb, loop_c);
  new_bb->flags |= (insns ? BB_SUPERBLOCK : 0);

  if (insns)
    emit_insn_after (insns, BB_END (new_bb));

  if (dest->loop_father->latch == src)
    dest->loop_father->latch = new_bb;

  return new_bb;
}

/* This function is called from loop_version.  It splits the entry edge
   of the loop we want to version, adds the versioning condition, and
   adjust the edges to the two versions of the loop appropriately.
   e is an incoming edge. Returns the basic block containing the
   condition.

   --- edge e ---- > [second_head]

   Split it and insert new conditional expression and adjust edges.

    --- edge e ---> [cond expr] ---> [first_head]
			|
			+---------> [second_head]
*/

static basic_block
lv_adjust_loop_entry_edge (basic_block first_head,
			   basic_block second_head,
			   edge e,
			   void *cond_expr)
{
  basic_block new_head = NULL;
  edge e1;

  gcc_assert (e->dest == second_head);

  /* Split edge 'e'. This will create a new basic block, where we can
     insert conditional expr.  */
  new_head = split_edge (e);


  lv_add_condition_to_bb (first_head, second_head, new_head,
			  cond_expr);

  /* Don't set EDGE_TRUE_VALUE in RTL mode, as it's invalid there.  */
  e1 = make_edge (new_head, first_head, ir_type () ? EDGE_TRUE_VALUE : 0);
  set_immediate_dominator (CDI_DOMINATORS, first_head, new_head);
  set_immediate_dominator (CDI_DOMINATORS, second_head, new_head);

  /* Adjust loop header phi nodes.  */
  lv_adjust_loop_header_phi (first_head, second_head, new_head, e1);

  return new_head;
}

/* Main entry point for Loop Versioning transformation.

   This transformation given a condition and a loop, creates
   -if (condition) { loop_copy1 } else { loop_copy2 },
   where loop_copy1 is the loop transformed in one way, and loop_copy2
   is the loop transformed in another way (or unchanged). 'condition'
   may be a run time test for things that were not resolved by static
   analysis (overlapping ranges (anti-aliasing), alignment, etc.).

   If PLACE_AFTER is true, we place the new loop after LOOP in the
   instruction stream, otherwise it is placed before LOOP.  */

struct loop *
loop_version (struct loops *loops, struct loop * loop,
	      void *cond_expr, basic_block *condition_bb,
	      bool place_after)
{
  basic_block first_head, second_head;
  edge entry, latch_edge, exit, true_edge, false_edge;
  int irred_flag;
  struct loop *nloop;
  basic_block cond_bb;

  /* CHECKME: Loop versioning does not handle nested loop at this point.  */
  if (loop->inner)
    return NULL;

  /* Record entry and latch edges for the loop */
  entry = loop_preheader_edge (loop);
  irred_flag = entry->flags & EDGE_IRREDUCIBLE_LOOP;
  entry->flags &= ~EDGE_IRREDUCIBLE_LOOP;

  /* Note down head of loop as first_head.  */
  first_head = entry->dest;

  /* Duplicate loop.  */
  if (!cfg_hook_duplicate_loop_to_header_edge (loop, entry, loops, 1,
					       NULL, NULL, NULL, NULL, 0))
    return NULL;

  /* After duplication entry edge now points to new loop head block.
     Note down new head as second_head.  */
  second_head = entry->dest;

  /* Split loop entry edge and insert new block with cond expr.  */
  cond_bb =  lv_adjust_loop_entry_edge (first_head, second_head,
					entry, cond_expr);
  if (condition_bb)
    *condition_bb = cond_bb;

  if (!cond_bb)
    {
      entry->flags |= irred_flag;
      return NULL;
    }

  latch_edge = single_succ_edge (get_bb_copy (loop->latch));

  extract_cond_bb_edges (cond_bb, &true_edge, &false_edge);
  nloop = loopify (loops,
		   latch_edge,
		   single_pred_edge (get_bb_copy (loop->header)),
		   cond_bb, true_edge, false_edge,
		   false /* Do not redirect all edges.  */);

  exit = loop->single_exit;
  if (exit)
    nloop->single_exit = find_edge (get_bb_copy (exit->src), exit->dest);

  /* loopify redirected latch_edge. Update its PENDING_STMTS.  */
  lv_flush_pending_stmts (latch_edge);

  /* loopify redirected condition_bb's succ edge. Update its PENDING_STMTS.  */
  extract_cond_bb_edges (cond_bb, &true_edge, &false_edge);
  lv_flush_pending_stmts (false_edge);
  /* Adjust irreducible flag.  */
  if (irred_flag)
    {
      cond_bb->flags |= BB_IRREDUCIBLE_LOOP;
      loop_preheader_edge (loop)->flags |= EDGE_IRREDUCIBLE_LOOP;
      loop_preheader_edge (nloop)->flags |= EDGE_IRREDUCIBLE_LOOP;
      single_pred_edge (cond_bb)->flags |= EDGE_IRREDUCIBLE_LOOP;
    }

  if (place_after)
    {
      basic_block *bbs = get_loop_body_in_dom_order (nloop), after;
      unsigned i;

      after = loop->latch;

      for (i = 0; i < nloop->num_nodes; i++)
	{
	  move_block_after (bbs[i], after);
	  after = bbs[i];
	}
      free (bbs);
    }

  /* At this point condition_bb is loop predheader with two successors,
     first_head and second_head.   Make sure that loop predheader has only
     one successor.  */
  loop_split_edge_with (loop_preheader_edge (loop), NULL);
  loop_split_edge_with (loop_preheader_edge (nloop), NULL);

  return nloop;
}

/* The structure of LOOPS might have changed.  Some loops might get removed
   (and their headers and latches were set to NULL), loop exists might get
   removed (thus the loop nesting may be wrong), and some blocks and edges
   were changed (so the information about bb --> loop mapping does not have
   to be correct).  But still for the remaining loops the header dominates
   the latch, and loops did not get new subloobs (new loops might possibly
   get created, but we are not interested in them).  Fix up the mess.

   If CHANGED_BBS is not NULL, basic blocks whose loop has changed are
   marked in it.  */

void
fix_loop_structure (struct loops *loops, bitmap changed_bbs)
{
  basic_block bb;
  struct loop *loop, *ploop;
  unsigned i;

  /* Remove the old bb -> loop mapping.  */
  FOR_EACH_BB (bb)
    {
      bb->aux = (void *) (size_t) bb->loop_father->depth;
      bb->loop_father = loops->tree_root;
    }

  /* Remove the dead loops from structures.  */
  loops->tree_root->num_nodes = n_basic_blocks;
  for (i = 1; i < loops->num; i++)
    {
      loop = loops->parray[i];
      if (!loop)
	continue;

      loop->num_nodes = 0;
      if (loop->header)
	continue;

      while (loop->inner)
	{
	  ploop = loop->inner;
	  flow_loop_tree_node_remove (ploop);
	  flow_loop_tree_node_add (loop->outer, ploop);
	}

      /* Remove the loop and free its data.  */
      flow_loop_tree_node_remove (loop);
      loops->parray[loop->num] = NULL;
      flow_loop_free (loop);
    }

  /* Rescan the bodies of loops, starting from the outermost.  */
  loop = loops->tree_root;
  while (1)
    {
      if (loop->inner)
	loop = loop->inner;
      else
	{
	  while (!loop->next
		 && loop != loops->tree_root)
	    loop = loop->outer;
	  if (loop == loops->tree_root)
	    break;

	  loop = loop->next;
	}

      loop->num_nodes = flow_loop_nodes_find (loop->header, loop);
    }

  /* Now fix the loop nesting.  */
  for (i = 1; i < loops->num; i++)
    {
      loop = loops->parray[i];
      if (!loop)
	continue;

      bb = loop_preheader_edge (loop)->src;
      if (bb->loop_father != loop->outer)
	{
	  flow_loop_tree_node_remove (loop);
	  flow_loop_tree_node_add (bb->loop_father, loop);
	}
    }

  /* Mark the blocks whose loop has changed.  */
  FOR_EACH_BB (bb)
    {
      if (changed_bbs
	  && (void *) (size_t) bb->loop_father->depth != bb->aux)
	bitmap_set_bit (changed_bbs, bb->index);

      bb->aux = NULL;
    }

  if (loops->state & LOOPS_HAVE_MARKED_SINGLE_EXITS)
    mark_single_exit_loops (loops);
  if (loops->state & LOOPS_HAVE_MARKED_IRREDUCIBLE_REGIONS)
    mark_irreducible_loops (loops);
}
