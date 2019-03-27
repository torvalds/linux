/* Thread edges through blocks and update the control flow and SSA graphs.
   Copyright (C) 2004, 2005, 2006 Free Software Foundation, Inc.

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
#include "flags.h"
#include "rtl.h"
#include "tm_p.h"
#include "ggc.h"
#include "basic-block.h"
#include "output.h"
#include "expr.h"
#include "function.h"
#include "diagnostic.h"
#include "tree-flow.h"
#include "tree-dump.h"
#include "tree-pass.h"
#include "cfgloop.h"

/* Given a block B, update the CFG and SSA graph to reflect redirecting
   one or more in-edges to B to instead reach the destination of an
   out-edge from B while preserving any side effects in B.

   i.e., given A->B and B->C, change A->B to be A->C yet still preserve the
   side effects of executing B.

     1. Make a copy of B (including its outgoing edges and statements).  Call
	the copy B'.  Note B' has no incoming edges or PHIs at this time.

     2. Remove the control statement at the end of B' and all outgoing edges
	except B'->C.

     3. Add a new argument to each PHI in C with the same value as the existing
	argument associated with edge B->C.  Associate the new PHI arguments
	with the edge B'->C.

     4. For each PHI in B, find or create a PHI in B' with an identical
	PHI_RESULT.  Add an argument to the PHI in B' which has the same
	value as the PHI in B associated with the edge A->B.  Associate
	the new argument in the PHI in B' with the edge A->B.

     5. Change the edge A->B to A->B'.

	5a. This automatically deletes any PHI arguments associated with the
	    edge A->B in B.

	5b. This automatically associates each new argument added in step 4
	    with the edge A->B'.

     6. Repeat for other incoming edges into B.

     7. Put the duplicated resources in B and all the B' blocks into SSA form.

   Note that block duplication can be minimized by first collecting the
   the set of unique destination blocks that the incoming edges should
   be threaded to.  Block duplication can be further minimized by using
   B instead of creating B' for one destination if all edges into B are
   going to be threaded to a successor of B.

   We further reduce the number of edges and statements we create by
   not copying all the outgoing edges and the control statement in
   step #1.  We instead create a template block without the outgoing
   edges and duplicate the template.  */


/* Steps #5 and #6 of the above algorithm are best implemented by walking
   all the incoming edges which thread to the same destination edge at
   the same time.  That avoids lots of table lookups to get information
   for the destination edge.

   To realize that implementation we create a list of incoming edges
   which thread to the same outgoing edge.  Thus to implement steps
   #5 and #6 we traverse our hash table of outgoing edge information.
   For each entry we walk the list of incoming edges which thread to
   the current outgoing edge.  */

struct el
{
  edge e;
  struct el *next;
};

/* Main data structure recording information regarding B's duplicate
   blocks.  */

/* We need to efficiently record the unique thread destinations of this
   block and specific information associated with those destinations.  We
   may have many incoming edges threaded to the same outgoing edge.  This
   can be naturally implemented with a hash table.  */

struct redirection_data
{
  /* A duplicate of B with the trailing control statement removed and which
     targets a single successor of B.  */
  basic_block dup_block;

  /* An outgoing edge from B.  DUP_BLOCK will have OUTGOING_EDGE->dest as
     its single successor.  */
  edge outgoing_edge;

  /* A list of incoming edges which we want to thread to
     OUTGOING_EDGE->dest.  */
  struct el *incoming_edges;

  /* Flag indicating whether or not we should create a duplicate block
     for this thread destination.  This is only true if we are threading
     all incoming edges and thus are using BB itself as a duplicate block.  */
  bool do_not_duplicate;
};

/* Main data structure to hold information for duplicates of BB.  */
static htab_t redirection_data;

/* Data structure of information to pass to hash table traversal routines.  */
struct local_info
{
  /* The current block we are working on.  */
  basic_block bb;

  /* A template copy of BB with no outgoing edges or control statement that
     we use for creating copies.  */
  basic_block template_block;

  /* TRUE if we thread one or more jumps, FALSE otherwise.  */
  bool jumps_threaded;
};

/* Passes which use the jump threading code register jump threading
   opportunities as they are discovered.  We keep the registered
   jump threading opportunities in this vector as edge pairs
   (original_edge, target_edge).  */
DEF_VEC_ALLOC_P(edge,heap);
static VEC(edge,heap) *threaded_edges;


/* Jump threading statistics.  */

struct thread_stats_d
{
  unsigned long num_threaded_edges;
};

struct thread_stats_d thread_stats;


/* Remove the last statement in block BB if it is a control statement
   Also remove all outgoing edges except the edge which reaches DEST_BB.
   If DEST_BB is NULL, then remove all outgoing edges.  */

static void
remove_ctrl_stmt_and_useless_edges (basic_block bb, basic_block dest_bb)
{
  block_stmt_iterator bsi;
  edge e;
  edge_iterator ei;

  bsi = bsi_last (bb);

  /* If the duplicate ends with a control statement, then remove it.

     Note that if we are duplicating the template block rather than the
     original basic block, then the duplicate might not have any real
     statements in it.  */
  if (!bsi_end_p (bsi)
      && bsi_stmt (bsi)
      && (TREE_CODE (bsi_stmt (bsi)) == COND_EXPR
	  || TREE_CODE (bsi_stmt (bsi)) == GOTO_EXPR
	  || TREE_CODE (bsi_stmt (bsi)) == SWITCH_EXPR))
    bsi_remove (&bsi, true);

  for (ei = ei_start (bb->succs); (e = ei_safe_edge (ei)); )
    {
      if (e->dest != dest_bb)
	remove_edge (e);
      else
	ei_next (&ei);
    }
}

/* Create a duplicate of BB which only reaches the destination of the edge
   stored in RD.  Record the duplicate block in RD.  */

static void
create_block_for_threading (basic_block bb, struct redirection_data *rd)
{
  /* We can use the generic block duplication code and simply remove
     the stuff we do not need.  */
  rd->dup_block = duplicate_block (bb, NULL, NULL);

  /* Zero out the profile, since the block is unreachable for now.  */
  rd->dup_block->frequency = 0;
  rd->dup_block->count = 0;

  /* The call to duplicate_block will copy everything, including the
     useless COND_EXPR or SWITCH_EXPR at the end of BB.  We just remove
     the useless COND_EXPR or SWITCH_EXPR here rather than having a
     specialized block copier.  We also remove all outgoing edges
     from the duplicate block.  The appropriate edge will be created
     later.  */
  remove_ctrl_stmt_and_useless_edges (rd->dup_block, NULL);
}

/* Hashing and equality routines for our hash table.  */
static hashval_t
redirection_data_hash (const void *p)
{
  edge e = ((struct redirection_data *)p)->outgoing_edge;
  return e->dest->index;
}

static int
redirection_data_eq (const void *p1, const void *p2)
{
  edge e1 = ((struct redirection_data *)p1)->outgoing_edge;
  edge e2 = ((struct redirection_data *)p2)->outgoing_edge;

  return e1 == e2;
}

/* Given an outgoing edge E lookup and return its entry in our hash table.

   If INSERT is true, then we insert the entry into the hash table if
   it is not already present.  INCOMING_EDGE is added to the list of incoming
   edges associated with E in the hash table.  */

static struct redirection_data *
lookup_redirection_data (edge e, edge incoming_edge, enum insert_option insert)
{
  void **slot;
  struct redirection_data *elt;

 /* Build a hash table element so we can see if E is already
     in the table.  */
  elt = XNEW (struct redirection_data);
  elt->outgoing_edge = e;
  elt->dup_block = NULL;
  elt->do_not_duplicate = false;
  elt->incoming_edges = NULL;

  slot = htab_find_slot (redirection_data, elt, insert);

  /* This will only happen if INSERT is false and the entry is not
     in the hash table.  */
  if (slot == NULL)
    {
      free (elt);
      return NULL;
    }

  /* This will only happen if E was not in the hash table and
     INSERT is true.  */
  if (*slot == NULL)
    {
      *slot = (void *)elt;
      elt->incoming_edges = XNEW (struct el);
      elt->incoming_edges->e = incoming_edge;
      elt->incoming_edges->next = NULL;
      return elt;
    }
  /* E was in the hash table.  */
  else
    {
      /* Free ELT as we do not need it anymore, we will extract the
	 relevant entry from the hash table itself.  */
      free (elt);

      /* Get the entry stored in the hash table.  */
      elt = (struct redirection_data *) *slot;

      /* If insertion was requested, then we need to add INCOMING_EDGE
	 to the list of incoming edges associated with E.  */
      if (insert)
	{
          struct el *el = XNEW (struct el);
	  el->next = elt->incoming_edges;
	  el->e = incoming_edge;
	  elt->incoming_edges = el;
	}

      return elt;
    }
}

/* Given a duplicate block and its single destination (both stored
   in RD).  Create an edge between the duplicate and its single
   destination.

   Add an additional argument to any PHI nodes at the single
   destination.  */

static void
create_edge_and_update_destination_phis (struct redirection_data *rd)
{
  edge e = make_edge (rd->dup_block, rd->outgoing_edge->dest, EDGE_FALLTHRU);
  tree phi;

  e->probability = REG_BR_PROB_BASE;
  e->count = rd->dup_block->count;

  /* If there are any PHI nodes at the destination of the outgoing edge
     from the duplicate block, then we will need to add a new argument
     to them.  The argument should have the same value as the argument
     associated with the outgoing edge stored in RD.  */
  for (phi = phi_nodes (e->dest); phi; phi = PHI_CHAIN (phi))
    {
      int indx = rd->outgoing_edge->dest_idx;
      add_phi_arg (phi, PHI_ARG_DEF (phi, indx), e);
    }
}

/* Hash table traversal callback routine to create duplicate blocks.  */

static int
create_duplicates (void **slot, void *data)
{
  struct redirection_data *rd = (struct redirection_data *) *slot;
  struct local_info *local_info = (struct local_info *)data;

  /* If this entry should not have a duplicate created, then there's
     nothing to do.  */
  if (rd->do_not_duplicate)
    return 1;

  /* Create a template block if we have not done so already.  Otherwise
     use the template to create a new block.  */
  if (local_info->template_block == NULL)
    {
      create_block_for_threading (local_info->bb, rd);
      local_info->template_block = rd->dup_block;

      /* We do not create any outgoing edges for the template.  We will
	 take care of that in a later traversal.  That way we do not
	 create edges that are going to just be deleted.  */
    }
  else
    {
      create_block_for_threading (local_info->template_block, rd);

      /* Go ahead and wire up outgoing edges and update PHIs for the duplicate
         block.  */
      create_edge_and_update_destination_phis (rd);
    }

  /* Keep walking the hash table.  */
  return 1;
}

/* We did not create any outgoing edges for the template block during
   block creation.  This hash table traversal callback creates the
   outgoing edge for the template block.  */

static int
fixup_template_block (void **slot, void *data)
{
  struct redirection_data *rd = (struct redirection_data *) *slot;
  struct local_info *local_info = (struct local_info *)data;

  /* If this is the template block, then create its outgoing edges
     and halt the hash table traversal.  */
  if (rd->dup_block && rd->dup_block == local_info->template_block)
    {
      create_edge_and_update_destination_phis (rd);
      return 0;
    }

  return 1;
}

/* Not all jump threading requests are useful.  In particular some
   jump threading requests can create irreducible regions which are
   undesirable.

   This routine will examine the BB's incoming edges for jump threading
   requests which, if acted upon, would create irreducible regions.  Any
   such jump threading requests found will be pruned away.  */

static void
prune_undesirable_thread_requests (basic_block bb)
{
  edge e;
  edge_iterator ei;
  bool may_create_irreducible_region = false;
  unsigned int num_outgoing_edges_into_loop = 0;

  /* For the heuristics below, we need to know if BB has more than
     one outgoing edge into a loop.  */
  FOR_EACH_EDGE (e, ei, bb->succs)
    num_outgoing_edges_into_loop += ((e->flags & EDGE_LOOP_EXIT) == 0);

  if (num_outgoing_edges_into_loop > 1)
    {
      edge backedge = NULL;

      /* Consider the effect of threading the edge (0, 1) to 2 on the left
	 CFG to produce the right CFG:
    

             0            0
             |            |
             1<--+        2<--------+
            / \  |        |         |
           2   3 |        4<----+   |
            \ /  |       / \    |   |
             4---+      E   1-- | --+
             |              |   |
             E              3---+


 	Threading the (0, 1) edge to 2 effectively creates two loops
 	(2, 4, 1) and (4, 1, 3) which are neither disjoint nor nested.
	This is not good.

	However, we do need to be able to thread  (0, 1) to 2 or 3
	in the left CFG below (which creates the middle and right
	CFGs with nested loops).

             0          0             0
             |          |             |
             1<--+      2<----+       3<-+<-+
            /|   |      |     |       |  |  |
           2 |   |      3<-+  |       1--+  |
            \|   |      |  |  |       |     |
             3---+      1--+--+       2-----+

	 
	 A safe heuristic appears to be to only allow threading if BB
	 has a single incoming backedge from one of its direct successors.  */

      FOR_EACH_EDGE (e, ei, bb->preds)
	{
	  if (e->flags & EDGE_DFS_BACK)
	    {
	      if (backedge)
		{
		  backedge = NULL;
		  break;
		}
	      else
		{
		  backedge = e;
		}
	    }
	}

      if (backedge && find_edge (bb, backedge->src))
	;
      else
        may_create_irreducible_region = true;
    }
  else
    {
      edge dest = NULL;

      /* If we thread across the loop entry block (BB) into the
	 loop and BB is still reached from outside the loop, then
	 we would create an irreducible CFG.  Consider the effect
	 of threading the edge (1, 4) to 5 on the left CFG to produce
	 the right CFG

             0               0
            / \             / \
           1   2           1   2
            \ /            |   |
             4<----+       5<->4
            / \    |           |
           E   5---+           E


	 Threading the (1, 4) edge to 5 creates two entry points
	 into the loop (4, 5) (one from block 1, the other from
	 block 2).  A classic irreducible region. 

	 So look at all of BB's incoming edges which are not
	 backedges and which are not threaded to the loop exit.
	 If that subset of incoming edges do not all thread
	 to the same block, then threading any of them will create
	 an irreducible region.  */

      FOR_EACH_EDGE (e, ei, bb->preds)
	{
	  edge e2;

	  /* We ignore back edges for now.  This may need refinement
    	     as threading a backedge creates an inner loop which
	     we would need to verify has a single entry point. 

	     If all backedges thread to new locations, then this
	     block will no longer have incoming backedges and we
	     need not worry about creating irreducible regions
	     by threading through BB.  I don't think this happens
	     enough in practice to worry about it.  */
	  if (e->flags & EDGE_DFS_BACK)
	    continue;

	  /* If the incoming edge threads to the loop exit, then it
	     is clearly safe.  */
	  e2 = e->aux;
	  if (e2 && (e2->flags & EDGE_LOOP_EXIT))
	    continue;

	  /* E enters the loop header and is not threaded.  We can
	     not allow any other incoming edges to thread into
	     the loop as that would create an irreducible region.  */
	  if (!e2)
	    {
	      may_create_irreducible_region = true;
	      break;
	    }

	  /* We know that this incoming edge threads to a block inside
	     the loop.  This edge must thread to the same target in
	     the loop as any previously seen threaded edges.  Otherwise
	     we will create an irreducible region.  */
	  if (!dest)
	    dest = e2;
	  else if (e2 != dest)
	    {
	      may_create_irreducible_region = true;
	      break;
	    }
	}
    }

  /* If we might create an irreducible region, then cancel any of
     the jump threading requests for incoming edges which are
     not backedges and which do not thread to the exit block.  */
  if (may_create_irreducible_region)
    {
      FOR_EACH_EDGE (e, ei, bb->preds)
	{
	  edge e2;

	  /* Ignore back edges.  */
	  if (e->flags & EDGE_DFS_BACK)
	    continue;

	  e2 = e->aux;

	  /* If this incoming edge was not threaded, then there is
	     nothing to do.  */
	  if (!e2)
	    continue;

	  /* If this incoming edge threaded to the loop exit,
	     then it can be ignored as it is safe.  */
	  if (e2->flags & EDGE_LOOP_EXIT)
	    continue;

	  if (e2)
	    {
	      /* This edge threaded into the loop and the jump thread
		 request must be cancelled.  */
	      if (dump_file && (dump_flags & TDF_DETAILS))
		fprintf (dump_file, "  Not threading jump %d --> %d to %d\n",
			 e->src->index, e->dest->index, e2->dest->index);
	      e->aux = NULL;
	    }
	}
    }
}

/* Hash table traversal callback to redirect each incoming edge
   associated with this hash table element to its new destination.  */

static int
redirect_edges (void **slot, void *data)
{
  struct redirection_data *rd = (struct redirection_data *) *slot;
  struct local_info *local_info = (struct local_info *)data;
  struct el *next, *el;

  /* Walk over all the incoming edges associated associated with this
     hash table entry.  */
  for (el = rd->incoming_edges; el; el = next)
    {
      edge e = el->e;

      /* Go ahead and free this element from the list.  Doing this now
	 avoids the need for another list walk when we destroy the hash
	 table.  */
      next = el->next;
      free (el);

      /* Go ahead and clear E->aux.  It's not needed anymore and failure
         to clear it will cause all kinds of unpleasant problems later.  */
      e->aux = NULL;

      thread_stats.num_threaded_edges++;

      if (rd->dup_block)
	{
	  edge e2;

	  if (dump_file && (dump_flags & TDF_DETAILS))
	    fprintf (dump_file, "  Threaded jump %d --> %d to %d\n",
		     e->src->index, e->dest->index, rd->dup_block->index);

	  rd->dup_block->count += e->count;
	  rd->dup_block->frequency += EDGE_FREQUENCY (e);
	  EDGE_SUCC (rd->dup_block, 0)->count += e->count;
	  /* Redirect the incoming edge to the appropriate duplicate
	     block.  */
	  e2 = redirect_edge_and_branch (e, rd->dup_block);
	  flush_pending_stmts (e2);

	  if ((dump_file && (dump_flags & TDF_DETAILS))
	      && e->src != e2->src)
	    fprintf (dump_file, "    basic block %d created\n", e2->src->index);
	}
      else
	{
	  if (dump_file && (dump_flags & TDF_DETAILS))
	    fprintf (dump_file, "  Threaded jump %d --> %d to %d\n",
		     e->src->index, e->dest->index, local_info->bb->index);

	  /* We are using BB as the duplicate.  Remove the unnecessary
	     outgoing edges and statements from BB.  */
	  remove_ctrl_stmt_and_useless_edges (local_info->bb,
					      rd->outgoing_edge->dest);

	  /* And fixup the flags on the single remaining edge.  */
	  single_succ_edge (local_info->bb)->flags
	    &= ~(EDGE_TRUE_VALUE | EDGE_FALSE_VALUE | EDGE_ABNORMAL);
	  single_succ_edge (local_info->bb)->flags |= EDGE_FALLTHRU;
	}
    }

  /* Indicate that we actually threaded one or more jumps.  */
  if (rd->incoming_edges)
    local_info->jumps_threaded = true;

  return 1;
}

/* Return true if this block has no executable statements other than
   a simple ctrl flow instruction.  When the number of outgoing edges
   is one, this is equivalent to a "forwarder" block.  */

static bool
redirection_block_p (basic_block bb)
{
  block_stmt_iterator bsi;

  /* Advance to the first executable statement.  */
  bsi = bsi_start (bb);
  while (!bsi_end_p (bsi)
          && (TREE_CODE (bsi_stmt (bsi)) == LABEL_EXPR
              || IS_EMPTY_STMT (bsi_stmt (bsi))))
    bsi_next (&bsi);

  /* Check if this is an empty block.  */
  if (bsi_end_p (bsi))
    return true;

  /* Test that we've reached the terminating control statement.  */
  return bsi_stmt (bsi)
	 && (TREE_CODE (bsi_stmt (bsi)) == COND_EXPR
	     || TREE_CODE (bsi_stmt (bsi)) == GOTO_EXPR
	     || TREE_CODE (bsi_stmt (bsi)) == SWITCH_EXPR);
}

/* BB is a block which ends with a COND_EXPR or SWITCH_EXPR and when BB
   is reached via one or more specific incoming edges, we know which
   outgoing edge from BB will be traversed.

   We want to redirect those incoming edges to the target of the
   appropriate outgoing edge.  Doing so avoids a conditional branch
   and may expose new optimization opportunities.  Note that we have
   to update dominator tree and SSA graph after such changes.

   The key to keeping the SSA graph update manageable is to duplicate
   the side effects occurring in BB so that those side effects still
   occur on the paths which bypass BB after redirecting edges.

   We accomplish this by creating duplicates of BB and arranging for
   the duplicates to unconditionally pass control to one specific
   successor of BB.  We then revector the incoming edges into BB to
   the appropriate duplicate of BB.

   BB and its duplicates will have assignments to the same set of
   SSA_NAMEs.  Right now, we just call into update_ssa to update the
   SSA graph for those names.

   We are also going to experiment with a true incremental update
   scheme for the duplicated resources.  One of the interesting
   properties we can exploit here is that all the resources set
   in BB will have the same IDFS, so we have one IDFS computation
   per block with incoming threaded edges, which can lower the
   cost of the true incremental update algorithm.  */

static bool
thread_block (basic_block bb)
{
  /* E is an incoming edge into BB that we may or may not want to
     redirect to a duplicate of BB.  */
  edge e;
  edge_iterator ei;
  struct local_info local_info;

  /* FOUND_BACKEDGE indicates that we found an incoming backedge
     into BB, in which case we may ignore certain jump threads
     to avoid creating irreducible regions.  */
  bool found_backedge = false;

  /* ALL indicates whether or not all incoming edges into BB should
     be threaded to a duplicate of BB.  */
  bool all = true;

  /* If optimizing for size, only thread this block if we don't have
     to duplicate it or it's an otherwise empty redirection block.  */
  if (optimize_size
      && EDGE_COUNT (bb->preds) > 1
      && !redirection_block_p (bb))
    {
      FOR_EACH_EDGE (e, ei, bb->preds)
	e->aux = NULL;
      return false;
    }

  /* To avoid scanning a linear array for the element we need we instead
     use a hash table.  For normal code there should be no noticeable
     difference.  However, if we have a block with a large number of
     incoming and outgoing edges such linear searches can get expensive.  */
  redirection_data = htab_create (EDGE_COUNT (bb->succs),
				  redirection_data_hash,
				  redirection_data_eq,
				  free);

  FOR_EACH_EDGE (e, ei, bb->preds)
    found_backedge |= ((e->flags & EDGE_DFS_BACK) != 0);

  /* If BB has incoming backedges, then threading across BB might
     introduce an irreducible region, which would be undesirable
     as that inhibits various optimizations later.  Prune away
     any jump threading requests which we know will result in
     an irreducible region.  */
  if (found_backedge)
    prune_undesirable_thread_requests (bb);

  /* Record each unique threaded destination into a hash table for
     efficient lookups.  */
  FOR_EACH_EDGE (e, ei, bb->preds)
    {
      if (!e->aux)
	{
	  all = false;
	}
      else
	{
	  edge e2 = e->aux;
	  update_bb_profile_for_threading (e->dest, EDGE_FREQUENCY (e),
					   e->count, e->aux);

	  /* Insert the outgoing edge into the hash table if it is not
	     already in the hash table.  */
	  lookup_redirection_data (e2, e, INSERT);
	}
    }

  /* If we are going to thread all incoming edges to an outgoing edge, then
     BB will become unreachable.  Rather than just throwing it away, use
     it for one of the duplicates.  Mark the first incoming edge with the
     DO_NOT_DUPLICATE attribute.  */
  if (all)
    {
      edge e = EDGE_PRED (bb, 0)->aux;
      lookup_redirection_data (e, NULL, NO_INSERT)->do_not_duplicate = true;
    }

  /* Now create duplicates of BB.

     Note that for a block with a high outgoing degree we can waste
     a lot of time and memory creating and destroying useless edges.

     So we first duplicate BB and remove the control structure at the
     tail of the duplicate as well as all outgoing edges from the
     duplicate.  We then use that duplicate block as a template for
     the rest of the duplicates.  */
  local_info.template_block = NULL;
  local_info.bb = bb;
  local_info.jumps_threaded = false;
  htab_traverse (redirection_data, create_duplicates, &local_info);

  /* The template does not have an outgoing edge.  Create that outgoing
     edge and update PHI nodes as the edge's target as necessary.

     We do this after creating all the duplicates to avoid creating
     unnecessary edges.  */
  htab_traverse (redirection_data, fixup_template_block, &local_info);

  /* The hash table traversals above created the duplicate blocks (and the
     statements within the duplicate blocks).  This loop creates PHI nodes for
     the duplicated blocks and redirects the incoming edges into BB to reach
     the duplicates of BB.  */
  htab_traverse (redirection_data, redirect_edges, &local_info);

  /* Done with this block.  Clear REDIRECTION_DATA.  */
  htab_delete (redirection_data);
  redirection_data = NULL;

  /* Indicate to our caller whether or not any jumps were threaded.  */
  return local_info.jumps_threaded;
}

/* Walk through the registered jump threads and convert them into a
   form convenient for this pass.

   Any block which has incoming edges threaded to outgoing edges
   will have its entry in THREADED_BLOCK set.

   Any threaded edge will have its new outgoing edge stored in the
   original edge's AUX field.

   This form avoids the need to walk all the edges in the CFG to
   discover blocks which need processing and avoids unnecessary
   hash table lookups to map from threaded edge to new target.  */

static void
mark_threaded_blocks (bitmap threaded_blocks)
{
  unsigned int i;

  for (i = 0; i < VEC_length (edge, threaded_edges); i += 2)
    {
      edge e = VEC_index (edge, threaded_edges, i);
      edge e2 = VEC_index (edge, threaded_edges, i + 1);

      e->aux = e2;
      bitmap_set_bit (threaded_blocks, e->dest->index);
    }
}


/* Walk through all blocks and thread incoming edges to the appropriate
   outgoing edge for each edge pair recorded in THREADED_EDGES.

   It is the caller's responsibility to fix the dominance information
   and rewrite duplicated SSA_NAMEs back into SSA form.

   Returns true if one or more edges were threaded, false otherwise.  */

bool
thread_through_all_blocks (void)
{
  bool retval = false;
  unsigned int i;
  bitmap_iterator bi;
  bitmap threaded_blocks;

  if (threaded_edges == NULL)
    return false;

  threaded_blocks = BITMAP_ALLOC (NULL);
  memset (&thread_stats, 0, sizeof (thread_stats));

  mark_threaded_blocks (threaded_blocks);

  EXECUTE_IF_SET_IN_BITMAP (threaded_blocks, 0, i, bi)
    {
      basic_block bb = BASIC_BLOCK (i);

      if (EDGE_COUNT (bb->preds) > 0)
	retval |= thread_block (bb);
    }

  if (dump_file && (dump_flags & TDF_STATS))
    fprintf (dump_file, "\nJumps threaded: %lu\n",
	     thread_stats.num_threaded_edges);

  BITMAP_FREE (threaded_blocks);
  threaded_blocks = NULL;
  VEC_free (edge, heap, threaded_edges);
  threaded_edges = NULL;
  return retval;
}

/* Register a jump threading opportunity.  We queue up all the jump
   threading opportunities discovered by a pass and update the CFG
   and SSA form all at once.

   E is the edge we can thread, E2 is the new target edge.  ie, we
   are effectively recording that E->dest can be changed to E2->dest
   after fixing the SSA graph.  */

void
register_jump_thread (edge e, edge e2)
{
  if (threaded_edges == NULL)
    threaded_edges = VEC_alloc (edge, heap, 10);

  VEC_safe_push (edge, heap, threaded_edges, e);
  VEC_safe_push (edge, heap, threaded_edges, e2);
}
