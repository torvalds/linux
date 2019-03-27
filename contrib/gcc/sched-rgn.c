/* Instruction scheduling pass.
   Copyright (C) 1992, 1993, 1994, 1995, 1996, 1997, 1998,
   1999, 2000, 2001, 2002, 2003, 2004, 2005 Free Software Foundation, Inc.
   Contributed by Michael Tiemann (tiemann@cygnus.com) Enhanced by,
   and currently maintained by, Jim Wilson (wilson@cygnus.com)

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

/* This pass implements list scheduling within basic blocks.  It is
   run twice: (1) after flow analysis, but before register allocation,
   and (2) after register allocation.

   The first run performs interblock scheduling, moving insns between
   different blocks in the same "region", and the second runs only
   basic block scheduling.

   Interblock motions performed are useful motions and speculative
   motions, including speculative loads.  Motions requiring code
   duplication are not supported.  The identification of motion type
   and the check for validity of speculative motions requires
   construction and analysis of the function's control flow graph.

   The main entry point for this pass is schedule_insns(), called for
   each function.  The work of the scheduler is organized in three
   levels: (1) function level: insns are subject to splitting,
   control-flow-graph is constructed, regions are computed (after
   reload, each region is of one block), (2) region level: control
   flow graph attributes required for interblock scheduling are
   computed (dominators, reachability, etc.), data dependences and
   priorities are computed, and (3) block level: insns in the block
   are actually scheduled.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "toplev.h"
#include "rtl.h"
#include "tm_p.h"
#include "hard-reg-set.h"
#include "regs.h"
#include "function.h"
#include "flags.h"
#include "insn-config.h"
#include "insn-attr.h"
#include "except.h"
#include "toplev.h"
#include "recog.h"
#include "cfglayout.h"
#include "params.h"
#include "sched-int.h"
#include "target.h"
#include "timevar.h"
#include "tree-pass.h"

/* Define when we want to do count REG_DEAD notes before and after scheduling
   for sanity checking.  We can't do that when conditional execution is used,
   as REG_DEAD exist only for unconditional deaths.  */

#if !defined (HAVE_conditional_execution) && defined (ENABLE_CHECKING)
#define CHECK_DEAD_NOTES 1
#else
#define CHECK_DEAD_NOTES 0
#endif


#ifdef INSN_SCHEDULING
/* Some accessor macros for h_i_d members only used within this file.  */
#define INSN_REF_COUNT(INSN)	(h_i_d[INSN_UID (INSN)].ref_count)
#define FED_BY_SPEC_LOAD(insn)	(h_i_d[INSN_UID (insn)].fed_by_spec_load)
#define IS_LOAD_INSN(insn)	(h_i_d[INSN_UID (insn)].is_load_insn)

/* nr_inter/spec counts interblock/speculative motion for the function.  */
static int nr_inter, nr_spec;

static int is_cfg_nonregular (void);
static bool sched_is_disabled_for_current_region_p (void);

/* A region is the main entity for interblock scheduling: insns
   are allowed to move between blocks in the same region, along
   control flow graph edges, in the 'up' direction.  */
typedef struct
{
  /* Number of extended basic blocks in region.  */
  int rgn_nr_blocks;
  /* cblocks in the region (actually index in rgn_bb_table).  */
  int rgn_blocks;
  /* Dependencies for this region are already computed.  Basically, indicates,
     that this is a recovery block.  */
  unsigned int dont_calc_deps : 1;
  /* This region has at least one non-trivial ebb.  */
  unsigned int has_real_ebb : 1;
}
region;

/* Number of regions in the procedure.  */
static int nr_regions;

/* Table of region descriptions.  */
static region *rgn_table;

/* Array of lists of regions' blocks.  */
static int *rgn_bb_table;

/* Topological order of blocks in the region (if b2 is reachable from
   b1, block_to_bb[b2] > block_to_bb[b1]).  Note: A basic block is
   always referred to by either block or b, while its topological
   order name (in the region) is referred to by bb.  */
static int *block_to_bb;

/* The number of the region containing a block.  */
static int *containing_rgn;

/* The minimum probability of reaching a source block so that it will be
   considered for speculative scheduling.  */
static int min_spec_prob;

#define RGN_NR_BLOCKS(rgn) (rgn_table[rgn].rgn_nr_blocks)
#define RGN_BLOCKS(rgn) (rgn_table[rgn].rgn_blocks)
#define RGN_DONT_CALC_DEPS(rgn) (rgn_table[rgn].dont_calc_deps)
#define RGN_HAS_REAL_EBB(rgn) (rgn_table[rgn].has_real_ebb)
#define BLOCK_TO_BB(block) (block_to_bb[block])
#define CONTAINING_RGN(block) (containing_rgn[block])

void debug_regions (void);
static void find_single_block_region (void);
static void find_rgns (void);
static void extend_rgns (int *, int *, sbitmap, int *);
static bool too_large (int, int *, int *);

extern void debug_live (int, int);

/* Blocks of the current region being scheduled.  */
static int current_nr_blocks;
static int current_blocks;

static int rgn_n_insns;

/* The mapping from ebb to block.  */
/* ebb_head [i] - is index in rgn_bb_table, while
   EBB_HEAD (i) - is basic block index.
   BASIC_BLOCK (EBB_HEAD (i)) - head of ebb.  */
#define BB_TO_BLOCK(ebb) (rgn_bb_table[ebb_head[ebb]])
#define EBB_FIRST_BB(ebb) BASIC_BLOCK (BB_TO_BLOCK (ebb))
#define EBB_LAST_BB(ebb) BASIC_BLOCK (rgn_bb_table[ebb_head[ebb + 1] - 1])

/* Target info declarations.

   The block currently being scheduled is referred to as the "target" block,
   while other blocks in the region from which insns can be moved to the
   target are called "source" blocks.  The candidate structure holds info
   about such sources: are they valid?  Speculative?  Etc.  */
typedef struct
{
  basic_block *first_member;
  int nr_members;
}
bblst;

typedef struct
{
  char is_valid;
  char is_speculative;
  int src_prob;
  bblst split_bbs;
  bblst update_bbs;
}
candidate;

static candidate *candidate_table;

/* A speculative motion requires checking live information on the path
   from 'source' to 'target'.  The split blocks are those to be checked.
   After a speculative motion, live information should be modified in
   the 'update' blocks.

   Lists of split and update blocks for each candidate of the current
   target are in array bblst_table.  */
static basic_block *bblst_table;
static int bblst_size, bblst_last;

#define IS_VALID(src) ( candidate_table[src].is_valid )
#define IS_SPECULATIVE(src) ( candidate_table[src].is_speculative )
#define SRC_PROB(src) ( candidate_table[src].src_prob )

/* The bb being currently scheduled.  */
static int target_bb;

/* List of edges.  */
typedef struct
{
  edge *first_member;
  int nr_members;
}
edgelst;

static edge *edgelst_table;
static int edgelst_last;

static void extract_edgelst (sbitmap, edgelst *);


/* Target info functions.  */
static void split_edges (int, int, edgelst *);
static void compute_trg_info (int);
void debug_candidate (int);
void debug_candidates (int);

/* Dominators array: dom[i] contains the sbitmap of dominators of
   bb i in the region.  */
static sbitmap *dom;

/* bb 0 is the only region entry.  */
#define IS_RGN_ENTRY(bb) (!bb)

/* Is bb_src dominated by bb_trg.  */
#define IS_DOMINATED(bb_src, bb_trg)                                 \
( TEST_BIT (dom[bb_src], bb_trg) )

/* Probability: Prob[i] is an int in [0, REG_BR_PROB_BASE] which is
   the probability of bb i relative to the region entry.  */
static int *prob;

/* Bit-set of edges, where bit i stands for edge i.  */
typedef sbitmap edgeset;

/* Number of edges in the region.  */
static int rgn_nr_edges;

/* Array of size rgn_nr_edges.  */
static edge *rgn_edges;

/* Mapping from each edge in the graph to its number in the rgn.  */
#define EDGE_TO_BIT(edge) ((int)(size_t)(edge)->aux)
#define SET_EDGE_TO_BIT(edge,nr) ((edge)->aux = (void *)(size_t)(nr))

/* The split edges of a source bb is different for each target
   bb.  In order to compute this efficiently, the 'potential-split edges'
   are computed for each bb prior to scheduling a region.  This is actually
   the split edges of each bb relative to the region entry.

   pot_split[bb] is the set of potential split edges of bb.  */
static edgeset *pot_split;

/* For every bb, a set of its ancestor edges.  */
static edgeset *ancestor_edges;

/* Array of EBBs sizes.  Currently we can get a ebb only through 
   splitting of currently scheduling block, therefore, we don't need
   ebb_head array for every region, its sufficient to hold it only
   for current one.  */
static int *ebb_head;

static void compute_dom_prob_ps (int);

#define INSN_PROBABILITY(INSN) (SRC_PROB (BLOCK_TO_BB (BLOCK_NUM (INSN))))
#define IS_SPECULATIVE_INSN(INSN) (IS_SPECULATIVE (BLOCK_TO_BB (BLOCK_NUM (INSN))))
#define INSN_BB(INSN) (BLOCK_TO_BB (BLOCK_NUM (INSN)))

/* Speculative scheduling functions.  */
static int check_live_1 (int, rtx);
static void update_live_1 (int, rtx);
static int check_live (rtx, int);
static void update_live (rtx, int);
static void set_spec_fed (rtx);
static int is_pfree (rtx, int, int);
static int find_conditional_protection (rtx, int);
static int is_conditionally_protected (rtx, int, int);
static int is_prisky (rtx, int, int);
static int is_exception_free (rtx, int, int);

static bool sets_likely_spilled (rtx);
static void sets_likely_spilled_1 (rtx, rtx, void *);
static void add_branch_dependences (rtx, rtx);
static void compute_block_backward_dependences (int);
void debug_dependencies (void);

static void init_regions (void);
static void schedule_region (int);
static rtx concat_INSN_LIST (rtx, rtx);
static void concat_insn_mem_list (rtx, rtx, rtx *, rtx *);
static void propagate_deps (int, struct deps *);
static void free_pending_lists (void);

/* Functions for construction of the control flow graph.  */

/* Return 1 if control flow graph should not be constructed, 0 otherwise.

   We decide not to build the control flow graph if there is possibly more
   than one entry to the function, if computed branches exist, if we
   have nonlocal gotos, or if we have an unreachable loop.  */

static int
is_cfg_nonregular (void)
{
  basic_block b;
  rtx insn;

  /* If we have a label that could be the target of a nonlocal goto, then
     the cfg is not well structured.  */
  if (nonlocal_goto_handler_labels)
    return 1;

  /* If we have any forced labels, then the cfg is not well structured.  */
  if (forced_labels)
    return 1;

  /* If we have exception handlers, then we consider the cfg not well
     structured.  ?!?  We should be able to handle this now that flow.c
     computes an accurate cfg for EH.  */
  if (current_function_has_exception_handlers ())
    return 1;

  /* If we have non-jumping insns which refer to labels, then we consider
     the cfg not well structured.  */
  FOR_EACH_BB (b)
    FOR_BB_INSNS (b, insn)
      {
	/* Check for labels referred by non-jump insns.  */
	if (NONJUMP_INSN_P (insn) || CALL_P (insn))
	  {
	    rtx note = find_reg_note (insn, REG_LABEL, NULL_RTX);
	    if (note
		&& ! (JUMP_P (NEXT_INSN (insn))
		      && find_reg_note (NEXT_INSN (insn), REG_LABEL,
					XEXP (note, 0))))
	      return 1;
	  }
	/* If this function has a computed jump, then we consider the cfg
	   not well structured.  */
	else if (JUMP_P (insn) && computed_jump_p (insn))
	  return 1;
      }

  /* Unreachable loops with more than one basic block are detected
     during the DFS traversal in find_rgns.

     Unreachable loops with a single block are detected here.  This
     test is redundant with the one in find_rgns, but it's much
     cheaper to go ahead and catch the trivial case here.  */
  FOR_EACH_BB (b)
    {
      if (EDGE_COUNT (b->preds) == 0
	  || (single_pred_p (b)
	      && single_pred (b) == b))
	return 1;
    }

  /* All the tests passed.  Consider the cfg well structured.  */
  return 0;
}

/* Extract list of edges from a bitmap containing EDGE_TO_BIT bits.  */

static void
extract_edgelst (sbitmap set, edgelst *el)
{
  unsigned int i = 0;
  sbitmap_iterator sbi;

  /* edgelst table space is reused in each call to extract_edgelst.  */
  edgelst_last = 0;

  el->first_member = &edgelst_table[edgelst_last];
  el->nr_members = 0;

  /* Iterate over each word in the bitset.  */
  EXECUTE_IF_SET_IN_SBITMAP (set, 0, i, sbi)
    {
      edgelst_table[edgelst_last++] = rgn_edges[i];
      el->nr_members++;
    }
}

/* Functions for the construction of regions.  */

/* Print the regions, for debugging purposes.  Callable from debugger.  */

void
debug_regions (void)
{
  int rgn, bb;

  fprintf (sched_dump, "\n;;   ------------ REGIONS ----------\n\n");
  for (rgn = 0; rgn < nr_regions; rgn++)
    {
      fprintf (sched_dump, ";;\trgn %d nr_blocks %d:\n", rgn,
	       rgn_table[rgn].rgn_nr_blocks);
      fprintf (sched_dump, ";;\tbb/block: ");

      /* We don't have ebb_head initialized yet, so we can't use
	 BB_TO_BLOCK ().  */
      current_blocks = RGN_BLOCKS (rgn);

      for (bb = 0; bb < rgn_table[rgn].rgn_nr_blocks; bb++)
	fprintf (sched_dump, " %d/%d ", bb, rgn_bb_table[current_blocks + bb]);

      fprintf (sched_dump, "\n\n");
    }
}

/* Build a single block region for each basic block in the function.
   This allows for using the same code for interblock and basic block
   scheduling.  */

static void
find_single_block_region (void)
{
  basic_block bb;

  nr_regions = 0;

  FOR_EACH_BB (bb)
    {
      rgn_bb_table[nr_regions] = bb->index;
      RGN_NR_BLOCKS (nr_regions) = 1;
      RGN_BLOCKS (nr_regions) = nr_regions;
      RGN_DONT_CALC_DEPS (nr_regions) = 0;
      RGN_HAS_REAL_EBB (nr_regions) = 0;
      CONTAINING_RGN (bb->index) = nr_regions;
      BLOCK_TO_BB (bb->index) = 0;
      nr_regions++;
    }
}

/* Update number of blocks and the estimate for number of insns
   in the region.  Return true if the region is "too large" for interblock
   scheduling (compile time considerations).  */

static bool
too_large (int block, int *num_bbs, int *num_insns)
{
  (*num_bbs)++;
  (*num_insns) += (INSN_LUID (BB_END (BASIC_BLOCK (block)))
		   - INSN_LUID (BB_HEAD (BASIC_BLOCK (block))));

  return ((*num_bbs > PARAM_VALUE (PARAM_MAX_SCHED_REGION_BLOCKS))
	  || (*num_insns > PARAM_VALUE (PARAM_MAX_SCHED_REGION_INSNS)));
}

/* Update_loop_relations(blk, hdr): Check if the loop headed by max_hdr[blk]
   is still an inner loop.  Put in max_hdr[blk] the header of the most inner
   loop containing blk.  */
#define UPDATE_LOOP_RELATIONS(blk, hdr)		\
{						\
  if (max_hdr[blk] == -1)			\
    max_hdr[blk] = hdr;				\
  else if (dfs_nr[max_hdr[blk]] > dfs_nr[hdr])	\
    RESET_BIT (inner, hdr);			\
  else if (dfs_nr[max_hdr[blk]] < dfs_nr[hdr])	\
    {						\
      RESET_BIT (inner,max_hdr[blk]);		\
      max_hdr[blk] = hdr;			\
    }						\
}

/* Find regions for interblock scheduling.

   A region for scheduling can be:

     * A loop-free procedure, or

     * A reducible inner loop, or

     * A basic block not contained in any other region.

   ?!? In theory we could build other regions based on extended basic
   blocks or reverse extended basic blocks.  Is it worth the trouble?

   Loop blocks that form a region are put into the region's block list
   in topological order.

   This procedure stores its results into the following global (ick) variables

     * rgn_nr
     * rgn_table
     * rgn_bb_table
     * block_to_bb
     * containing region

   We use dominator relationships to avoid making regions out of non-reducible
   loops.

   This procedure needs to be converted to work on pred/succ lists instead
   of edge tables.  That would simplify it somewhat.  */

static void
find_rgns (void)
{
  int *max_hdr, *dfs_nr, *degree;
  char no_loops = 1;
  int node, child, loop_head, i, head, tail;
  int count = 0, sp, idx = 0;
  edge_iterator current_edge;
  edge_iterator *stack;
  int num_bbs, num_insns, unreachable;
  int too_large_failure;
  basic_block bb;

  /* Note if a block is a natural loop header.  */
  sbitmap header;

  /* Note if a block is a natural inner loop header.  */
  sbitmap inner;

  /* Note if a block is in the block queue.  */
  sbitmap in_queue;

  /* Note if a block is in the block queue.  */
  sbitmap in_stack;

  /* Perform a DFS traversal of the cfg.  Identify loop headers, inner loops
     and a mapping from block to its loop header (if the block is contained
     in a loop, else -1).

     Store results in HEADER, INNER, and MAX_HDR respectively, these will
     be used as inputs to the second traversal.

     STACK, SP and DFS_NR are only used during the first traversal.  */

  /* Allocate and initialize variables for the first traversal.  */
  max_hdr = XNEWVEC (int, last_basic_block);
  dfs_nr = XCNEWVEC (int, last_basic_block);
  stack = XNEWVEC (edge_iterator, n_edges);

  inner = sbitmap_alloc (last_basic_block);
  sbitmap_ones (inner);

  header = sbitmap_alloc (last_basic_block);
  sbitmap_zero (header);

  in_queue = sbitmap_alloc (last_basic_block);
  sbitmap_zero (in_queue);

  in_stack = sbitmap_alloc (last_basic_block);
  sbitmap_zero (in_stack);

  for (i = 0; i < last_basic_block; i++)
    max_hdr[i] = -1;

  #define EDGE_PASSED(E) (ei_end_p ((E)) || ei_edge ((E))->aux)
  #define SET_EDGE_PASSED(E) (ei_edge ((E))->aux = ei_edge ((E)))

  /* DFS traversal to find inner loops in the cfg.  */

  current_edge = ei_start (single_succ (ENTRY_BLOCK_PTR)->succs);
  sp = -1;

  while (1)
    {
      if (EDGE_PASSED (current_edge))
	{
	  /* We have reached a leaf node or a node that was already
	     processed.  Pop edges off the stack until we find
	     an edge that has not yet been processed.  */
	  while (sp >= 0 && EDGE_PASSED (current_edge))
	    {
	      /* Pop entry off the stack.  */
	      current_edge = stack[sp--];
	      node = ei_edge (current_edge)->src->index;
	      gcc_assert (node != ENTRY_BLOCK);
	      child = ei_edge (current_edge)->dest->index;
	      gcc_assert (child != EXIT_BLOCK);
	      RESET_BIT (in_stack, child);
	      if (max_hdr[child] >= 0 && TEST_BIT (in_stack, max_hdr[child]))
		UPDATE_LOOP_RELATIONS (node, max_hdr[child]);
	      ei_next (&current_edge);
	    }

	  /* See if have finished the DFS tree traversal.  */
	  if (sp < 0 && EDGE_PASSED (current_edge))
	    break;

	  /* Nope, continue the traversal with the popped node.  */
	  continue;
	}

      /* Process a node.  */
      node = ei_edge (current_edge)->src->index;
      gcc_assert (node != ENTRY_BLOCK);
      SET_BIT (in_stack, node);
      dfs_nr[node] = ++count;

      /* We don't traverse to the exit block.  */
      child = ei_edge (current_edge)->dest->index;
      if (child == EXIT_BLOCK)
	{
	  SET_EDGE_PASSED (current_edge);
	  ei_next (&current_edge);
	  continue;
	}

      /* If the successor is in the stack, then we've found a loop.
	 Mark the loop, if it is not a natural loop, then it will
	 be rejected during the second traversal.  */
      if (TEST_BIT (in_stack, child))
	{
	  no_loops = 0;
	  SET_BIT (header, child);
	  UPDATE_LOOP_RELATIONS (node, child);
	  SET_EDGE_PASSED (current_edge);
	  ei_next (&current_edge);
	  continue;
	}

      /* If the child was already visited, then there is no need to visit
	 it again.  Just update the loop relationships and restart
	 with a new edge.  */
      if (dfs_nr[child])
	{
	  if (max_hdr[child] >= 0 && TEST_BIT (in_stack, max_hdr[child]))
	    UPDATE_LOOP_RELATIONS (node, max_hdr[child]);
	  SET_EDGE_PASSED (current_edge);
	  ei_next (&current_edge);
	  continue;
	}

      /* Push an entry on the stack and continue DFS traversal.  */
      stack[++sp] = current_edge;
      SET_EDGE_PASSED (current_edge);
      current_edge = ei_start (ei_edge (current_edge)->dest->succs);
    }

  /* Reset ->aux field used by EDGE_PASSED.  */
  FOR_ALL_BB (bb)
    {
      edge_iterator ei;
      edge e;
      FOR_EACH_EDGE (e, ei, bb->succs)
	e->aux = NULL;
    }


  /* Another check for unreachable blocks.  The earlier test in
     is_cfg_nonregular only finds unreachable blocks that do not
     form a loop.

     The DFS traversal will mark every block that is reachable from
     the entry node by placing a nonzero value in dfs_nr.  Thus if
     dfs_nr is zero for any block, then it must be unreachable.  */
  unreachable = 0;
  FOR_EACH_BB (bb)
    if (dfs_nr[bb->index] == 0)
      {
	unreachable = 1;
	break;
      }

  /* Gross.  To avoid wasting memory, the second pass uses the dfs_nr array
     to hold degree counts.  */
  degree = dfs_nr;

  FOR_EACH_BB (bb)
    degree[bb->index] = EDGE_COUNT (bb->preds);

  /* Do not perform region scheduling if there are any unreachable
     blocks.  */
  if (!unreachable)
    {
      int *queue, *degree1 = NULL;
      /* We use EXTENDED_RGN_HEADER as an addition to HEADER and put
	 there basic blocks, which are forced to be region heads.
	 This is done to try to assemble few smaller regions 
	 from a too_large region.  */
      sbitmap extended_rgn_header = NULL;
      bool extend_regions_p;

      if (no_loops)
	SET_BIT (header, 0);

      /* Second traversal:find reducible inner loops and topologically sort
	 block of each region.  */

      queue = XNEWVEC (int, n_basic_blocks);
      
      extend_regions_p = PARAM_VALUE (PARAM_MAX_SCHED_EXTEND_REGIONS_ITERS) > 0;
      if (extend_regions_p)
        {
          degree1 = xmalloc (last_basic_block * sizeof (int));
          extended_rgn_header = sbitmap_alloc (last_basic_block);
          sbitmap_zero (extended_rgn_header);
	}

      /* Find blocks which are inner loop headers.  We still have non-reducible
	 loops to consider at this point.  */
      FOR_EACH_BB (bb)
	{
	  if (TEST_BIT (header, bb->index) && TEST_BIT (inner, bb->index))
	    {
	      edge e;
	      edge_iterator ei;
	      basic_block jbb;

	      /* Now check that the loop is reducible.  We do this separate
		 from finding inner loops so that we do not find a reducible
		 loop which contains an inner non-reducible loop.

		 A simple way to find reducible/natural loops is to verify
		 that each block in the loop is dominated by the loop
		 header.

		 If there exists a block that is not dominated by the loop
		 header, then the block is reachable from outside the loop
		 and thus the loop is not a natural loop.  */
	      FOR_EACH_BB (jbb)
		{
		  /* First identify blocks in the loop, except for the loop
		     entry block.  */
		  if (bb->index == max_hdr[jbb->index] && bb != jbb)
		    {
		      /* Now verify that the block is dominated by the loop
			 header.  */
		      if (!dominated_by_p (CDI_DOMINATORS, jbb, bb))
			break;
		    }
		}

	      /* If we exited the loop early, then I is the header of
		 a non-reducible loop and we should quit processing it
		 now.  */
	      if (jbb != EXIT_BLOCK_PTR)
		continue;

	      /* I is a header of an inner loop, or block 0 in a subroutine
		 with no loops at all.  */
	      head = tail = -1;
	      too_large_failure = 0;
	      loop_head = max_hdr[bb->index];

              if (extend_regions_p)
                /* We save degree in case when we meet a too_large region 
		   and cancel it.  We need a correct degree later when 
                   calling extend_rgns.  */
                memcpy (degree1, degree, last_basic_block * sizeof (int));
	      
	      /* Decrease degree of all I's successors for topological
		 ordering.  */
	      FOR_EACH_EDGE (e, ei, bb->succs)
		if (e->dest != EXIT_BLOCK_PTR)
		  --degree[e->dest->index];

	      /* Estimate # insns, and count # blocks in the region.  */
	      num_bbs = 1;
	      num_insns = (INSN_LUID (BB_END (bb))
			   - INSN_LUID (BB_HEAD (bb)));

	      /* Find all loop latches (blocks with back edges to the loop
		 header) or all the leaf blocks in the cfg has no loops.

		 Place those blocks into the queue.  */
	      if (no_loops)
		{
		  FOR_EACH_BB (jbb)
		    /* Leaf nodes have only a single successor which must
		       be EXIT_BLOCK.  */
		    if (single_succ_p (jbb)
			&& single_succ (jbb) == EXIT_BLOCK_PTR)
		      {
			queue[++tail] = jbb->index;
			SET_BIT (in_queue, jbb->index);

			if (too_large (jbb->index, &num_bbs, &num_insns))
			  {
			    too_large_failure = 1;
			    break;
			  }
		      }
		}
	      else
		{
		  edge e;

		  FOR_EACH_EDGE (e, ei, bb->preds)
		    {
		      if (e->src == ENTRY_BLOCK_PTR)
			continue;

		      node = e->src->index;

		      if (max_hdr[node] == loop_head && node != bb->index)
			{
			  /* This is a loop latch.  */
			  queue[++tail] = node;
			  SET_BIT (in_queue, node);

			  if (too_large (node, &num_bbs, &num_insns))
			    {
			      too_large_failure = 1;
			      break;
			    }
			}
		    }
		}

	      /* Now add all the blocks in the loop to the queue.

	     We know the loop is a natural loop; however the algorithm
	     above will not always mark certain blocks as being in the
	     loop.  Consider:
		node   children
		 a	  b,c
		 b	  c
		 c	  a,d
		 d	  b

	     The algorithm in the DFS traversal may not mark B & D as part
	     of the loop (i.e. they will not have max_hdr set to A).

	     We know they can not be loop latches (else they would have
	     had max_hdr set since they'd have a backedge to a dominator
	     block).  So we don't need them on the initial queue.

	     We know they are part of the loop because they are dominated
	     by the loop header and can be reached by a backwards walk of
	     the edges starting with nodes on the initial queue.

	     It is safe and desirable to include those nodes in the
	     loop/scheduling region.  To do so we would need to decrease
	     the degree of a node if it is the target of a backedge
	     within the loop itself as the node is placed in the queue.

	     We do not do this because I'm not sure that the actual
	     scheduling code will properly handle this case. ?!? */

	      while (head < tail && !too_large_failure)
		{
		  edge e;
		  child = queue[++head];

		  FOR_EACH_EDGE (e, ei, BASIC_BLOCK (child)->preds)
		    {
		      node = e->src->index;

		      /* See discussion above about nodes not marked as in
			 this loop during the initial DFS traversal.  */
		      if (e->src == ENTRY_BLOCK_PTR
			  || max_hdr[node] != loop_head)
			{
			  tail = -1;
			  break;
			}
		      else if (!TEST_BIT (in_queue, node) && node != bb->index)
			{
			  queue[++tail] = node;
			  SET_BIT (in_queue, node);

			  if (too_large (node, &num_bbs, &num_insns))
			    {
			      too_large_failure = 1;
			      break;
			    }
			}
		    }
		}

	      if (tail >= 0 && !too_large_failure)
		{
		  /* Place the loop header into list of region blocks.  */
		  degree[bb->index] = -1;
		  rgn_bb_table[idx] = bb->index;
		  RGN_NR_BLOCKS (nr_regions) = num_bbs;
		  RGN_BLOCKS (nr_regions) = idx++;
                  RGN_DONT_CALC_DEPS (nr_regions) = 0;
		  RGN_HAS_REAL_EBB (nr_regions) = 0;
		  CONTAINING_RGN (bb->index) = nr_regions;
		  BLOCK_TO_BB (bb->index) = count = 0;

		  /* Remove blocks from queue[] when their in degree
		     becomes zero.  Repeat until no blocks are left on the
		     list.  This produces a topological list of blocks in
		     the region.  */
		  while (tail >= 0)
		    {
		      if (head < 0)
			head = tail;
		      child = queue[head];
		      if (degree[child] == 0)
			{
			  edge e;

			  degree[child] = -1;
			  rgn_bb_table[idx++] = child;
			  BLOCK_TO_BB (child) = ++count;
			  CONTAINING_RGN (child) = nr_regions;
			  queue[head] = queue[tail--];

			  FOR_EACH_EDGE (e, ei, BASIC_BLOCK (child)->succs)
			    if (e->dest != EXIT_BLOCK_PTR)
			      --degree[e->dest->index];
			}
		      else
			--head;
		    }
		  ++nr_regions;
		}
              else if (extend_regions_p)
                {
                  /* Restore DEGREE.  */
                  int *t = degree;

                  degree = degree1;
                  degree1 = t;
                  
                  /* And force successors of BB to be region heads.
		     This may provide several smaller regions instead
		     of one too_large region.  */
                  FOR_EACH_EDGE (e, ei, bb->succs)
                    if (e->dest != EXIT_BLOCK_PTR)
                      SET_BIT (extended_rgn_header, e->dest->index);
                }
	    }
	}
      free (queue);

      if (extend_regions_p)
        {
          free (degree1);
          
          sbitmap_a_or_b (header, header, extended_rgn_header);
          sbitmap_free (extended_rgn_header);
 
          extend_rgns (degree, &idx, header, max_hdr);
        }
    }

  /* Any block that did not end up in a region is placed into a region
     by itself.  */
  FOR_EACH_BB (bb)
    if (degree[bb->index] >= 0)
      {
	rgn_bb_table[idx] = bb->index;
	RGN_NR_BLOCKS (nr_regions) = 1;
	RGN_BLOCKS (nr_regions) = idx++;
        RGN_DONT_CALC_DEPS (nr_regions) = 0;
	RGN_HAS_REAL_EBB (nr_regions) = 0;
	CONTAINING_RGN (bb->index) = nr_regions++;
	BLOCK_TO_BB (bb->index) = 0;
      }

  free (max_hdr);
  free (degree);
  free (stack);
  sbitmap_free (header);
  sbitmap_free (inner);
  sbitmap_free (in_queue);
  sbitmap_free (in_stack);
}

static int gather_region_statistics (int **);
static void print_region_statistics (int *, int, int *, int);

/* Calculate the histogram that shows the number of regions having the 
   given number of basic blocks, and store it in the RSP array.  Return 
   the size of this array.  */
static int
gather_region_statistics (int **rsp)
{
  int i, *a = 0, a_sz = 0;

  /* a[i] is the number of regions that have (i + 1) basic blocks.  */
  for (i = 0; i < nr_regions; i++)
    {
      int nr_blocks = RGN_NR_BLOCKS (i);

      gcc_assert (nr_blocks >= 1);

      if (nr_blocks > a_sz)
	{	 
	  a = xrealloc (a, nr_blocks * sizeof (*a));
	  do
	    a[a_sz++] = 0;
	  while (a_sz != nr_blocks);
	}

      a[nr_blocks - 1]++;
    }

  *rsp = a;
  return a_sz;
}

/* Print regions statistics.  S1 and S2 denote the data before and after 
   calling extend_rgns, respectively.  */
static void
print_region_statistics (int *s1, int s1_sz, int *s2, int s2_sz)
{
  int i;
  
  /* We iterate until s2_sz because extend_rgns does not decrease 
     the maximal region size.  */
  for (i = 1; i < s2_sz; i++)
    {
      int n1, n2;

      n2 = s2[i];

      if (n2 == 0)
	continue;

      if (i >= s1_sz)
	n1 = 0;
      else
	n1 = s1[i];

      fprintf (sched_dump, ";; Region extension statistics: size %d: " \
	       "was %d + %d more\n", i + 1, n1, n2 - n1);
    }
}

/* Extend regions.
   DEGREE - Array of incoming edge count, considering only
   the edges, that don't have their sources in formed regions yet.
   IDXP - pointer to the next available index in rgn_bb_table.
   HEADER - set of all region heads.
   LOOP_HDR - mapping from block to the containing loop
   (two blocks can reside within one region if they have
   the same loop header).  */
static void
extend_rgns (int *degree, int *idxp, sbitmap header, int *loop_hdr)
{
  int *order, i, rescan = 0, idx = *idxp, iter = 0, max_iter, *max_hdr;
  int nblocks = n_basic_blocks - NUM_FIXED_BLOCKS;

  max_iter = PARAM_VALUE (PARAM_MAX_SCHED_EXTEND_REGIONS_ITERS);

  max_hdr = xmalloc (last_basic_block * sizeof (*max_hdr));

  order = xmalloc (last_basic_block * sizeof (*order));
  post_order_compute (order, false);

  for (i = nblocks - 1; i >= 0; i--)
    {
      int bbn = order[i];
      if (degree[bbn] >= 0)
	{
	  max_hdr[bbn] = bbn;
	  rescan = 1;
	}
      else
        /* This block already was processed in find_rgns.  */
        max_hdr[bbn] = -1;
    }
  
  /* The idea is to topologically walk through CFG in top-down order.
     During the traversal, if all the predecessors of a node are
     marked to be in the same region (they all have the same max_hdr),
     then current node is also marked to be a part of that region. 
     Otherwise the node starts its own region.
     CFG should be traversed until no further changes are made.  On each 
     iteration the set of the region heads is extended (the set of those 
     blocks that have max_hdr[bbi] == bbi).  This set is upper bounded by the 
     set of all basic blocks, thus the algorithm is guaranteed to terminate.  */

  while (rescan && iter < max_iter)
    {
      rescan = 0;
      
      for (i = nblocks - 1; i >= 0; i--)
	{
	  edge e;
	  edge_iterator ei;
	  int bbn = order[i];
	
	  if (max_hdr[bbn] != -1 && !TEST_BIT (header, bbn))
	    {
	      int hdr = -1;

	      FOR_EACH_EDGE (e, ei, BASIC_BLOCK (bbn)->preds)
		{
		  int predn = e->src->index;

		  if (predn != ENTRY_BLOCK
		      /* If pred wasn't processed in find_rgns.  */
		      && max_hdr[predn] != -1
		      /* And pred and bb reside in the same loop.
			 (Or out of any loop).  */
		      && loop_hdr[bbn] == loop_hdr[predn])
		    {
		      if (hdr == -1)
			/* Then bb extends the containing region of pred.  */
			hdr = max_hdr[predn];
		      else if (hdr != max_hdr[predn])
			/* Too bad, there are at least two predecessors
			   that reside in different regions.  Thus, BB should
			   begin its own region.  */
			{
			  hdr = bbn;
			  break;
			}		    
		    }
		  else
		    /* BB starts its own region.  */
		    {
		      hdr = bbn;
		      break;
		    }		
		}
	    
	      if (hdr == bbn)
		{
		  /* If BB start its own region,
		     update set of headers with BB.  */
		  SET_BIT (header, bbn);
		  rescan = 1;
		}
	      else
		gcc_assert (hdr != -1);	    

	      max_hdr[bbn] = hdr;
	    }
	}

      iter++;
    }
  
  /* Statistics were gathered on the SPEC2000 package of tests with
     mainline weekly snapshot gcc-4.1-20051015 on ia64.
     
     Statistics for SPECint:
     1 iteration : 1751 cases (38.7%)
     2 iterations: 2770 cases (61.3%)
     Blocks wrapped in regions by find_rgns without extension: 18295 blocks
     Blocks wrapped in regions by 2 iterations in extend_rgns: 23821 blocks
     (We don't count single block regions here).
     
     Statistics for SPECfp:
     1 iteration : 621 cases (35.9%)
     2 iterations: 1110 cases (64.1%)
     Blocks wrapped in regions by find_rgns without extension: 6476 blocks
     Blocks wrapped in regions by 2 iterations in extend_rgns: 11155 blocks
     (We don't count single block regions here).

     By default we do at most 2 iterations.
     This can be overridden with max-sched-extend-regions-iters parameter:
     0 - disable region extension,
     N > 0 - do at most N iterations.  */
  
  if (sched_verbose && iter != 0)
    fprintf (sched_dump, ";; Region extension iterations: %d%s\n", iter,
	     rescan ? "... failed" : "");
    
  if (!rescan && iter != 0)
    {
      int *s1 = NULL, s1_sz = 0;

      /* Save the old statistics for later printout.  */
      if (sched_verbose >= 6)
	s1_sz = gather_region_statistics (&s1);

      /* We have succeeded.  Now assemble the regions.  */
      for (i = nblocks - 1; i >= 0; i--)
	{
	  int bbn = order[i];

	  if (max_hdr[bbn] == bbn)
	    /* BBN is a region head.  */
	    {
	      edge e;
	      edge_iterator ei;
	      int num_bbs = 0, j, num_insns = 0, large;
	
	      large = too_large (bbn, &num_bbs, &num_insns);

	      degree[bbn] = -1;
	      rgn_bb_table[idx] = bbn;
	      RGN_BLOCKS (nr_regions) = idx++;
	      RGN_DONT_CALC_DEPS (nr_regions) = 0;
	      RGN_HAS_REAL_EBB (nr_regions) = 0;
	      CONTAINING_RGN (bbn) = nr_regions;
	      BLOCK_TO_BB (bbn) = 0;

	      FOR_EACH_EDGE (e, ei, BASIC_BLOCK (bbn)->succs)
		if (e->dest != EXIT_BLOCK_PTR)
		  degree[e->dest->index]--;

	      if (!large)
		/* Here we check whether the region is too_large.  */
		for (j = i - 1; j >= 0; j--)
		  {
		    int succn = order[j];
		    if (max_hdr[succn] == bbn)
		      {
			if ((large = too_large (succn, &num_bbs, &num_insns)))
			  break;
		      }
		  }

	      if (large)
		/* If the region is too_large, then wrap every block of
		   the region into single block region.
		   Here we wrap region head only.  Other blocks are
		   processed in the below cycle.  */
		{
		  RGN_NR_BLOCKS (nr_regions) = 1;
		  nr_regions++;
		}          

	      num_bbs = 1;

	      for (j = i - 1; j >= 0; j--)
		{
		  int succn = order[j];

		  if (max_hdr[succn] == bbn)
		    /* This cycle iterates over all basic blocks, that 
		       are supposed to be in the region with head BBN,
		       and wraps them into that region (or in single
		       block region).  */
		    {
		      gcc_assert (degree[succn] == 0);

		      degree[succn] = -1;
		      rgn_bb_table[idx] = succn;		 
		      BLOCK_TO_BB (succn) = large ? 0 : num_bbs++;
		      CONTAINING_RGN (succn) = nr_regions;

		      if (large)
			/* Wrap SUCCN into single block region.  */
			{
			  RGN_BLOCKS (nr_regions) = idx;
			  RGN_NR_BLOCKS (nr_regions) = 1;
			  RGN_DONT_CALC_DEPS (nr_regions) = 0;
			  RGN_HAS_REAL_EBB (nr_regions) = 0;
			  nr_regions++;
			}

		      idx++;
				
		      FOR_EACH_EDGE (e, ei, BASIC_BLOCK (succn)->succs)
			if (e->dest != EXIT_BLOCK_PTR)
			  degree[e->dest->index]--;
		    }
		}

	      if (!large)
		{
		  RGN_NR_BLOCKS (nr_regions) = num_bbs;
		  nr_regions++;
		}
	    }
	}

      if (sched_verbose >= 6)
	{
	  int *s2, s2_sz;

          /* Get the new statistics and print the comparison with the 
             one before calling this function.  */
	  s2_sz = gather_region_statistics (&s2);
	  print_region_statistics (s1, s1_sz, s2, s2_sz);
	  free (s1);
	  free (s2);
	}
    }
  
  free (order);
  free (max_hdr);

  *idxp = idx; 
}

/* Functions for regions scheduling information.  */

/* Compute dominators, probability, and potential-split-edges of bb.
   Assume that these values were already computed for bb's predecessors.  */

static void
compute_dom_prob_ps (int bb)
{
  edge_iterator in_ei;
  edge in_edge;

  /* We shouldn't have any real ebbs yet.  */
  gcc_assert (ebb_head [bb] == bb + current_blocks);
  
  if (IS_RGN_ENTRY (bb))
    {
      SET_BIT (dom[bb], 0);
      prob[bb] = REG_BR_PROB_BASE;
      return;
    }

  prob[bb] = 0;

  /* Initialize dom[bb] to '111..1'.  */
  sbitmap_ones (dom[bb]);

  FOR_EACH_EDGE (in_edge, in_ei, BASIC_BLOCK (BB_TO_BLOCK (bb))->preds)
    {
      int pred_bb;
      edge out_edge;
      edge_iterator out_ei;

      if (in_edge->src == ENTRY_BLOCK_PTR)
	continue;

      pred_bb = BLOCK_TO_BB (in_edge->src->index);
      sbitmap_a_and_b (dom[bb], dom[bb], dom[pred_bb]);
      sbitmap_a_or_b (ancestor_edges[bb],
		      ancestor_edges[bb], ancestor_edges[pred_bb]);

      SET_BIT (ancestor_edges[bb], EDGE_TO_BIT (in_edge));

      sbitmap_a_or_b (pot_split[bb], pot_split[bb], pot_split[pred_bb]);

      FOR_EACH_EDGE (out_edge, out_ei, in_edge->src->succs)
	SET_BIT (pot_split[bb], EDGE_TO_BIT (out_edge));

      prob[bb] += ((prob[pred_bb] * in_edge->probability) / REG_BR_PROB_BASE);
    }

  SET_BIT (dom[bb], bb);
  sbitmap_difference (pot_split[bb], pot_split[bb], ancestor_edges[bb]);

  if (sched_verbose >= 2)
    fprintf (sched_dump, ";;  bb_prob(%d, %d) = %3d\n", bb, BB_TO_BLOCK (bb),
	     (100 * prob[bb]) / REG_BR_PROB_BASE);
}

/* Functions for target info.  */

/* Compute in BL the list of split-edges of bb_src relatively to bb_trg.
   Note that bb_trg dominates bb_src.  */

static void
split_edges (int bb_src, int bb_trg, edgelst *bl)
{
  sbitmap src = sbitmap_alloc (pot_split[bb_src]->n_bits);
  sbitmap_copy (src, pot_split[bb_src]);

  sbitmap_difference (src, src, pot_split[bb_trg]);
  extract_edgelst (src, bl);
  sbitmap_free (src);
}

/* Find the valid candidate-source-blocks for the target block TRG, compute
   their probability, and check if they are speculative or not.
   For speculative sources, compute their update-blocks and split-blocks.  */

static void
compute_trg_info (int trg)
{
  candidate *sp;
  edgelst el;
  int i, j, k, update_idx;
  basic_block block;
  sbitmap visited;
  edge_iterator ei;
  edge e;

  /* Define some of the fields for the target bb as well.  */
  sp = candidate_table + trg;
  sp->is_valid = 1;
  sp->is_speculative = 0;
  sp->src_prob = REG_BR_PROB_BASE;

  visited = sbitmap_alloc (last_basic_block);

  for (i = trg + 1; i < current_nr_blocks; i++)
    {
      sp = candidate_table + i;

      sp->is_valid = IS_DOMINATED (i, trg);
      if (sp->is_valid)
	{
	  int tf = prob[trg], cf = prob[i];

	  /* In CFGs with low probability edges TF can possibly be zero.  */
	  sp->src_prob = (tf ? ((cf * REG_BR_PROB_BASE) / tf) : 0);
	  sp->is_valid = (sp->src_prob >= min_spec_prob);
	}

      if (sp->is_valid)
	{
	  split_edges (i, trg, &el);
	  sp->is_speculative = (el.nr_members) ? 1 : 0;
	  if (sp->is_speculative && !flag_schedule_speculative)
	    sp->is_valid = 0;
	}

      if (sp->is_valid)
	{
	  /* Compute split blocks and store them in bblst_table.
	     The TO block of every split edge is a split block.  */
	  sp->split_bbs.first_member = &bblst_table[bblst_last];
	  sp->split_bbs.nr_members = el.nr_members;
	  for (j = 0; j < el.nr_members; bblst_last++, j++)
	    bblst_table[bblst_last] = el.first_member[j]->dest;
	  sp->update_bbs.first_member = &bblst_table[bblst_last];

	  /* Compute update blocks and store them in bblst_table.
	     For every split edge, look at the FROM block, and check
	     all out edges.  For each out edge that is not a split edge,
	     add the TO block to the update block list.  This list can end
	     up with a lot of duplicates.  We need to weed them out to avoid
	     overrunning the end of the bblst_table.  */

	  update_idx = 0;
	  sbitmap_zero (visited);
	  for (j = 0; j < el.nr_members; j++)
	    {
	      block = el.first_member[j]->src;
	      FOR_EACH_EDGE (e, ei, block->succs)
		{
		  if (!TEST_BIT (visited, e->dest->index))
		    {
		      for (k = 0; k < el.nr_members; k++)
			if (e == el.first_member[k])
			  break;

		      if (k >= el.nr_members)
			{
			  bblst_table[bblst_last++] = e->dest;
			  SET_BIT (visited, e->dest->index);
			  update_idx++;
			}
		    }
		}
	    }
	  sp->update_bbs.nr_members = update_idx;

	  /* Make sure we didn't overrun the end of bblst_table.  */
	  gcc_assert (bblst_last <= bblst_size);
	}
      else
	{
	  sp->split_bbs.nr_members = sp->update_bbs.nr_members = 0;

	  sp->is_speculative = 0;
	  sp->src_prob = 0;
	}
    }

  sbitmap_free (visited);
}

/* Print candidates info, for debugging purposes.  Callable from debugger.  */

void
debug_candidate (int i)
{
  if (!candidate_table[i].is_valid)
    return;

  if (candidate_table[i].is_speculative)
    {
      int j;
      fprintf (sched_dump, "src b %d bb %d speculative \n", BB_TO_BLOCK (i), i);

      fprintf (sched_dump, "split path: ");
      for (j = 0; j < candidate_table[i].split_bbs.nr_members; j++)
	{
	  int b = candidate_table[i].split_bbs.first_member[j]->index;

	  fprintf (sched_dump, " %d ", b);
	}
      fprintf (sched_dump, "\n");

      fprintf (sched_dump, "update path: ");
      for (j = 0; j < candidate_table[i].update_bbs.nr_members; j++)
	{
	  int b = candidate_table[i].update_bbs.first_member[j]->index;

	  fprintf (sched_dump, " %d ", b);
	}
      fprintf (sched_dump, "\n");
    }
  else
    {
      fprintf (sched_dump, " src %d equivalent\n", BB_TO_BLOCK (i));
    }
}

/* Print candidates info, for debugging purposes.  Callable from debugger.  */

void
debug_candidates (int trg)
{
  int i;

  fprintf (sched_dump, "----------- candidate table: target: b=%d bb=%d ---\n",
	   BB_TO_BLOCK (trg), trg);
  for (i = trg + 1; i < current_nr_blocks; i++)
    debug_candidate (i);
}

/* Functions for speculative scheduling.  */

/* Return 0 if x is a set of a register alive in the beginning of one
   of the split-blocks of src, otherwise return 1.  */

static int
check_live_1 (int src, rtx x)
{
  int i;
  int regno;
  rtx reg = SET_DEST (x);

  if (reg == 0)
    return 1;

  while (GET_CODE (reg) == SUBREG
	 || GET_CODE (reg) == ZERO_EXTRACT
	 || GET_CODE (reg) == STRICT_LOW_PART)
    reg = XEXP (reg, 0);

  if (GET_CODE (reg) == PARALLEL)
    {
      int i;

      for (i = XVECLEN (reg, 0) - 1; i >= 0; i--)
	if (XEXP (XVECEXP (reg, 0, i), 0) != 0)
	  if (check_live_1 (src, XEXP (XVECEXP (reg, 0, i), 0)))
	    return 1;

      return 0;
    }

  if (!REG_P (reg))
    return 1;

  regno = REGNO (reg);

  if (regno < FIRST_PSEUDO_REGISTER && global_regs[regno])
    {
      /* Global registers are assumed live.  */
      return 0;
    }
  else
    {
      if (regno < FIRST_PSEUDO_REGISTER)
	{
	  /* Check for hard registers.  */
	  int j = hard_regno_nregs[regno][GET_MODE (reg)];
	  while (--j >= 0)
	    {
	      for (i = 0; i < candidate_table[src].split_bbs.nr_members; i++)
		{
		  basic_block b = candidate_table[src].split_bbs.first_member[i];

		  /* We can have split blocks, that were recently generated.
		     such blocks are always outside current region.  */
		  gcc_assert (glat_start[b->index]
			      || CONTAINING_RGN (b->index)
			      != CONTAINING_RGN (BB_TO_BLOCK (src)));
		  if (!glat_start[b->index]
		      || REGNO_REG_SET_P (glat_start[b->index],
					  regno + j))
		    {
		      return 0;
		    }
		}
	    }
	}
      else
	{
	  /* Check for pseudo registers.  */
	  for (i = 0; i < candidate_table[src].split_bbs.nr_members; i++)
	    {
	      basic_block b = candidate_table[src].split_bbs.first_member[i];

	      gcc_assert (glat_start[b->index]
			  || CONTAINING_RGN (b->index)
			  != CONTAINING_RGN (BB_TO_BLOCK (src)));
	      if (!glat_start[b->index]
		  || REGNO_REG_SET_P (glat_start[b->index], regno))
		{
		  return 0;
		}
	    }
	}
    }

  return 1;
}

/* If x is a set of a register R, mark that R is alive in the beginning
   of every update-block of src.  */

static void
update_live_1 (int src, rtx x)
{
  int i;
  int regno;
  rtx reg = SET_DEST (x);

  if (reg == 0)
    return;

  while (GET_CODE (reg) == SUBREG
	 || GET_CODE (reg) == ZERO_EXTRACT
	 || GET_CODE (reg) == STRICT_LOW_PART)
    reg = XEXP (reg, 0);

  if (GET_CODE (reg) == PARALLEL)
    {
      int i;

      for (i = XVECLEN (reg, 0) - 1; i >= 0; i--)
	if (XEXP (XVECEXP (reg, 0, i), 0) != 0)
	  update_live_1 (src, XEXP (XVECEXP (reg, 0, i), 0));

      return;
    }

  if (!REG_P (reg))
    return;

  /* Global registers are always live, so the code below does not apply
     to them.  */

  regno = REGNO (reg);

  if (regno >= FIRST_PSEUDO_REGISTER || !global_regs[regno])
    {
      if (regno < FIRST_PSEUDO_REGISTER)
	{
	  int j = hard_regno_nregs[regno][GET_MODE (reg)];
	  while (--j >= 0)
	    {
	      for (i = 0; i < candidate_table[src].update_bbs.nr_members; i++)
		{
		  basic_block b = candidate_table[src].update_bbs.first_member[i];

		  SET_REGNO_REG_SET (glat_start[b->index], regno + j);
		}
	    }
	}
      else
	{
	  for (i = 0; i < candidate_table[src].update_bbs.nr_members; i++)
	    {
	      basic_block b = candidate_table[src].update_bbs.first_member[i];

	      SET_REGNO_REG_SET (glat_start[b->index], regno);
	    }
	}
    }
}

/* Return 1 if insn can be speculatively moved from block src to trg,
   otherwise return 0.  Called before first insertion of insn to
   ready-list or before the scheduling.  */

static int
check_live (rtx insn, int src)
{
  /* Find the registers set by instruction.  */
  if (GET_CODE (PATTERN (insn)) == SET
      || GET_CODE (PATTERN (insn)) == CLOBBER)
    return check_live_1 (src, PATTERN (insn));
  else if (GET_CODE (PATTERN (insn)) == PARALLEL)
    {
      int j;
      for (j = XVECLEN (PATTERN (insn), 0) - 1; j >= 0; j--)
	if ((GET_CODE (XVECEXP (PATTERN (insn), 0, j)) == SET
	     || GET_CODE (XVECEXP (PATTERN (insn), 0, j)) == CLOBBER)
	    && !check_live_1 (src, XVECEXP (PATTERN (insn), 0, j)))
	  return 0;

      return 1;
    }

  return 1;
}

/* Update the live registers info after insn was moved speculatively from
   block src to trg.  */

static void
update_live (rtx insn, int src)
{
  /* Find the registers set by instruction.  */
  if (GET_CODE (PATTERN (insn)) == SET
      || GET_CODE (PATTERN (insn)) == CLOBBER)
    update_live_1 (src, PATTERN (insn));
  else if (GET_CODE (PATTERN (insn)) == PARALLEL)
    {
      int j;
      for (j = XVECLEN (PATTERN (insn), 0) - 1; j >= 0; j--)
	if (GET_CODE (XVECEXP (PATTERN (insn), 0, j)) == SET
	    || GET_CODE (XVECEXP (PATTERN (insn), 0, j)) == CLOBBER)
	  update_live_1 (src, XVECEXP (PATTERN (insn), 0, j));
    }
}

/* Nonzero if block bb_to is equal to, or reachable from block bb_from.  */
#define IS_REACHABLE(bb_from, bb_to)					\
  (bb_from == bb_to							\
   || IS_RGN_ENTRY (bb_from)						\
   || (TEST_BIT (ancestor_edges[bb_to],					\
	 EDGE_TO_BIT (single_pred_edge (BASIC_BLOCK (BB_TO_BLOCK (bb_from)))))))

/* Turns on the fed_by_spec_load flag for insns fed by load_insn.  */

static void
set_spec_fed (rtx load_insn)
{
  rtx link;

  for (link = INSN_DEPEND (load_insn); link; link = XEXP (link, 1))
    if (GET_MODE (link) == VOIDmode)
      FED_BY_SPEC_LOAD (XEXP (link, 0)) = 1;
}				/* set_spec_fed */

/* On the path from the insn to load_insn_bb, find a conditional
branch depending on insn, that guards the speculative load.  */

static int
find_conditional_protection (rtx insn, int load_insn_bb)
{
  rtx link;

  /* Iterate through DEF-USE forward dependences.  */
  for (link = INSN_DEPEND (insn); link; link = XEXP (link, 1))
    {
      rtx next = XEXP (link, 0);
      if ((CONTAINING_RGN (BLOCK_NUM (next)) ==
	   CONTAINING_RGN (BB_TO_BLOCK (load_insn_bb)))
	  && IS_REACHABLE (INSN_BB (next), load_insn_bb)
	  && load_insn_bb != INSN_BB (next)
	  && GET_MODE (link) == VOIDmode
	  && (JUMP_P (next)
	      || find_conditional_protection (next, load_insn_bb)))
	return 1;
    }
  return 0;
}				/* find_conditional_protection */

/* Returns 1 if the same insn1 that participates in the computation
   of load_insn's address is feeding a conditional branch that is
   guarding on load_insn. This is true if we find a the two DEF-USE
   chains:
   insn1 -> ... -> conditional-branch
   insn1 -> ... -> load_insn,
   and if a flow path exist:
   insn1 -> ... -> conditional-branch -> ... -> load_insn,
   and if insn1 is on the path
   region-entry -> ... -> bb_trg -> ... load_insn.

   Locate insn1 by climbing on LOG_LINKS from load_insn.
   Locate the branch by following INSN_DEPEND from insn1.  */

static int
is_conditionally_protected (rtx load_insn, int bb_src, int bb_trg)
{
  rtx link;

  for (link = LOG_LINKS (load_insn); link; link = XEXP (link, 1))
    {
      rtx insn1 = XEXP (link, 0);

      /* Must be a DEF-USE dependence upon non-branch.  */
      if (GET_MODE (link) != VOIDmode
	  || JUMP_P (insn1))
	continue;

      /* Must exist a path: region-entry -> ... -> bb_trg -> ... load_insn.  */
      if (INSN_BB (insn1) == bb_src
	  || (CONTAINING_RGN (BLOCK_NUM (insn1))
	      != CONTAINING_RGN (BB_TO_BLOCK (bb_src)))
	  || (!IS_REACHABLE (bb_trg, INSN_BB (insn1))
	      && !IS_REACHABLE (INSN_BB (insn1), bb_trg)))
	continue;

      /* Now search for the conditional-branch.  */
      if (find_conditional_protection (insn1, bb_src))
	return 1;

      /* Recursive step: search another insn1, "above" current insn1.  */
      return is_conditionally_protected (insn1, bb_src, bb_trg);
    }

  /* The chain does not exist.  */
  return 0;
}				/* is_conditionally_protected */

/* Returns 1 if a clue for "similar load" 'insn2' is found, and hence
   load_insn can move speculatively from bb_src to bb_trg.  All the
   following must hold:

   (1) both loads have 1 base register (PFREE_CANDIDATEs).
   (2) load_insn and load1 have a def-use dependence upon
   the same insn 'insn1'.
   (3) either load2 is in bb_trg, or:
   - there's only one split-block, and
   - load1 is on the escape path, and

   From all these we can conclude that the two loads access memory
   addresses that differ at most by a constant, and hence if moving
   load_insn would cause an exception, it would have been caused by
   load2 anyhow.  */

static int
is_pfree (rtx load_insn, int bb_src, int bb_trg)
{
  rtx back_link;
  candidate *candp = candidate_table + bb_src;

  if (candp->split_bbs.nr_members != 1)
    /* Must have exactly one escape block.  */
    return 0;

  for (back_link = LOG_LINKS (load_insn);
       back_link; back_link = XEXP (back_link, 1))
    {
      rtx insn1 = XEXP (back_link, 0);

      if (GET_MODE (back_link) == VOIDmode)
	{
	  /* Found a DEF-USE dependence (insn1, load_insn).  */
	  rtx fore_link;

	  for (fore_link = INSN_DEPEND (insn1);
	       fore_link; fore_link = XEXP (fore_link, 1))
	    {
	      rtx insn2 = XEXP (fore_link, 0);
	      if (GET_MODE (fore_link) == VOIDmode)
		{
		  /* Found a DEF-USE dependence (insn1, insn2).  */
		  if (haifa_classify_insn (insn2) != PFREE_CANDIDATE)
		    /* insn2 not guaranteed to be a 1 base reg load.  */
		    continue;

		  if (INSN_BB (insn2) == bb_trg)
		    /* insn2 is the similar load, in the target block.  */
		    return 1;

		  if (*(candp->split_bbs.first_member) == BLOCK_FOR_INSN (insn2))
		    /* insn2 is a similar load, in a split-block.  */
		    return 1;
		}
	    }
	}
    }

  /* Couldn't find a similar load.  */
  return 0;
}				/* is_pfree */

/* Return 1 if load_insn is prisky (i.e. if load_insn is fed by
   a load moved speculatively, or if load_insn is protected by
   a compare on load_insn's address).  */

static int
is_prisky (rtx load_insn, int bb_src, int bb_trg)
{
  if (FED_BY_SPEC_LOAD (load_insn))
    return 1;

  if (LOG_LINKS (load_insn) == NULL)
    /* Dependence may 'hide' out of the region.  */
    return 1;

  if (is_conditionally_protected (load_insn, bb_src, bb_trg))
    return 1;

  return 0;
}

/* Insn is a candidate to be moved speculatively from bb_src to bb_trg.
   Return 1 if insn is exception-free (and the motion is valid)
   and 0 otherwise.  */

static int
is_exception_free (rtx insn, int bb_src, int bb_trg)
{
  int insn_class = haifa_classify_insn (insn);

  /* Handle non-load insns.  */
  switch (insn_class)
    {
    case TRAP_FREE:
      return 1;
    case TRAP_RISKY:
      return 0;
    default:;
    }

  /* Handle loads.  */
  if (!flag_schedule_speculative_load)
    return 0;
  IS_LOAD_INSN (insn) = 1;
  switch (insn_class)
    {
    case IFREE:
      return (1);
    case IRISKY:
      return 0;
    case PFREE_CANDIDATE:
      if (is_pfree (insn, bb_src, bb_trg))
	return 1;
      /* Don't 'break' here: PFREE-candidate is also PRISKY-candidate.  */
    case PRISKY_CANDIDATE:
      if (!flag_schedule_speculative_load_dangerous
	  || is_prisky (insn, bb_src, bb_trg))
	return 0;
      break;
    default:;
    }

  return flag_schedule_speculative_load_dangerous;
}

/* The number of insns from the current block scheduled so far.  */
static int sched_target_n_insns;
/* The number of insns from the current block to be scheduled in total.  */
static int target_n_insns;
/* The number of insns from the entire region scheduled so far.  */
static int sched_n_insns;

/* Implementations of the sched_info functions for region scheduling.  */
static void init_ready_list (void);
static int can_schedule_ready_p (rtx);
static void begin_schedule_ready (rtx, rtx);
static ds_t new_ready (rtx, ds_t);
static int schedule_more_p (void);
static const char *rgn_print_insn (rtx, int);
static int rgn_rank (rtx, rtx);
static int contributes_to_priority (rtx, rtx);
static void compute_jump_reg_dependencies (rtx, regset, regset, regset);

/* Functions for speculative scheduling.  */
static void add_remove_insn (rtx, int);
static void extend_regions (void);
static void add_block1 (basic_block, basic_block);
static void fix_recovery_cfg (int, int, int);
static basic_block advance_target_bb (basic_block, rtx);
static void check_dead_notes1 (int, sbitmap);
#ifdef ENABLE_CHECKING
static int region_head_or_leaf_p (basic_block, int);
#endif

/* Return nonzero if there are more insns that should be scheduled.  */

static int
schedule_more_p (void)
{
  return sched_target_n_insns < target_n_insns;
}

/* Add all insns that are initially ready to the ready list READY.  Called
   once before scheduling a set of insns.  */

static void
init_ready_list (void)
{
  rtx prev_head = current_sched_info->prev_head;
  rtx next_tail = current_sched_info->next_tail;
  int bb_src;
  rtx insn;

  target_n_insns = 0;
  sched_target_n_insns = 0;
  sched_n_insns = 0;

  /* Print debugging information.  */
  if (sched_verbose >= 5)
    debug_dependencies ();

  /* Prepare current target block info.  */
  if (current_nr_blocks > 1)
    {
      candidate_table = XNEWVEC (candidate, current_nr_blocks);

      bblst_last = 0;
      /* bblst_table holds split blocks and update blocks for each block after
	 the current one in the region.  split blocks and update blocks are
	 the TO blocks of region edges, so there can be at most rgn_nr_edges
	 of them.  */
      bblst_size = (current_nr_blocks - target_bb) * rgn_nr_edges;
      bblst_table = XNEWVEC (basic_block, bblst_size);

      edgelst_last = 0;
      edgelst_table = XNEWVEC (edge, rgn_nr_edges);

      compute_trg_info (target_bb);
    }

  /* Initialize ready list with all 'ready' insns in target block.
     Count number of insns in the target block being scheduled.  */
  for (insn = NEXT_INSN (prev_head); insn != next_tail; insn = NEXT_INSN (insn))
    {      
      try_ready (insn);
      target_n_insns++;

      gcc_assert (!(TODO_SPEC (insn) & BEGIN_CONTROL));
    }

  /* Add to ready list all 'ready' insns in valid source blocks.
     For speculative insns, check-live, exception-free, and
     issue-delay.  */
  for (bb_src = target_bb + 1; bb_src < current_nr_blocks; bb_src++)
    if (IS_VALID (bb_src))
      {
	rtx src_head;
	rtx src_next_tail;
	rtx tail, head;

	get_ebb_head_tail (EBB_FIRST_BB (bb_src), EBB_LAST_BB (bb_src),
			   &head, &tail);
	src_next_tail = NEXT_INSN (tail);
	src_head = head;

	for (insn = src_head; insn != src_next_tail; insn = NEXT_INSN (insn))
	  if (INSN_P (insn))
	    try_ready (insn);
      }
}

/* Called after taking INSN from the ready list.  Returns nonzero if this
   insn can be scheduled, nonzero if we should silently discard it.  */

static int
can_schedule_ready_p (rtx insn)
{
  /* An interblock motion?  */
  if (INSN_BB (insn) != target_bb
      && IS_SPECULATIVE_INSN (insn)
      && !check_live (insn, INSN_BB (insn)))
    return 0;          
  else
    return 1;
}

/* Updates counter and other information.  Split from can_schedule_ready_p ()
   because when we schedule insn speculatively then insn passed to
   can_schedule_ready_p () differs from the one passed to
   begin_schedule_ready ().  */
static void
begin_schedule_ready (rtx insn, rtx last ATTRIBUTE_UNUSED)
{
  /* An interblock motion?  */
  if (INSN_BB (insn) != target_bb)
    {
      if (IS_SPECULATIVE_INSN (insn))
	{
	  gcc_assert (check_live (insn, INSN_BB (insn)));

	  update_live (insn, INSN_BB (insn));

	  /* For speculative load, mark insns fed by it.  */
	  if (IS_LOAD_INSN (insn) || FED_BY_SPEC_LOAD (insn))
	    set_spec_fed (insn);

	  nr_spec++;
	}
      nr_inter++;
    }
  else
    {
      /* In block motion.  */
      sched_target_n_insns++;
    }
  sched_n_insns++;
}

/* Called after INSN has all its hard dependencies resolved and the speculation
   of type TS is enough to overcome them all.
   Return nonzero if it should be moved to the ready list or the queue, or zero
   if we should silently discard it.  */
static ds_t
new_ready (rtx next, ds_t ts)
{
  if (INSN_BB (next) != target_bb)
    {
      int not_ex_free = 0;

      /* For speculative insns, before inserting to ready/queue,
	 check live, exception-free, and issue-delay.  */	
      if (!IS_VALID (INSN_BB (next))
	  || CANT_MOVE (next)
	  || (IS_SPECULATIVE_INSN (next)
	      && ((recog_memoized (next) >= 0
		   && min_insn_conflict_delay (curr_state, next, next) 
                   > PARAM_VALUE (PARAM_MAX_SCHED_INSN_CONFLICT_DELAY))
                  || IS_SPECULATION_CHECK_P (next)
		  || !check_live (next, INSN_BB (next))
		  || (not_ex_free = !is_exception_free (next, INSN_BB (next),
							target_bb)))))
	{
	  if (not_ex_free
	      /* We are here because is_exception_free () == false.
		 But we possibly can handle that with control speculation.  */
	      && current_sched_info->flags & DO_SPECULATION)
            /* Here we got new control-speculative instruction.  */
            ts = set_dep_weak (ts, BEGIN_CONTROL, MAX_DEP_WEAK);
	  else
            ts = (ts & ~SPECULATIVE) | HARD_DEP;
	}
    }
  
  return ts;
}

/* Return a string that contains the insn uid and optionally anything else
   necessary to identify this insn in an output.  It's valid to use a
   static buffer for this.  The ALIGNED parameter should cause the string
   to be formatted so that multiple output lines will line up nicely.  */

static const char *
rgn_print_insn (rtx insn, int aligned)
{
  static char tmp[80];

  if (aligned)
    sprintf (tmp, "b%3d: i%4d", INSN_BB (insn), INSN_UID (insn));
  else
    {
      if (current_nr_blocks > 1 && INSN_BB (insn) != target_bb)
	sprintf (tmp, "%d/b%d", INSN_UID (insn), INSN_BB (insn));
      else
	sprintf (tmp, "%d", INSN_UID (insn));
    }
  return tmp;
}

/* Compare priority of two insns.  Return a positive number if the second
   insn is to be preferred for scheduling, and a negative one if the first
   is to be preferred.  Zero if they are equally good.  */

static int
rgn_rank (rtx insn1, rtx insn2)
{
  /* Some comparison make sense in interblock scheduling only.  */
  if (INSN_BB (insn1) != INSN_BB (insn2))
    {
      int spec_val, prob_val;

      /* Prefer an inblock motion on an interblock motion.  */
      if ((INSN_BB (insn2) == target_bb) && (INSN_BB (insn1) != target_bb))
	return 1;
      if ((INSN_BB (insn1) == target_bb) && (INSN_BB (insn2) != target_bb))
	return -1;

      /* Prefer a useful motion on a speculative one.  */
      spec_val = IS_SPECULATIVE_INSN (insn1) - IS_SPECULATIVE_INSN (insn2);
      if (spec_val)
	return spec_val;

      /* Prefer a more probable (speculative) insn.  */
      prob_val = INSN_PROBABILITY (insn2) - INSN_PROBABILITY (insn1);
      if (prob_val)
	return prob_val;
    }
  return 0;
}

/* NEXT is an instruction that depends on INSN (a backward dependence);
   return nonzero if we should include this dependence in priority
   calculations.  */

static int
contributes_to_priority (rtx next, rtx insn)
{
  /* NEXT and INSN reside in one ebb.  */
  return BLOCK_TO_BB (BLOCK_NUM (next)) == BLOCK_TO_BB (BLOCK_NUM (insn));
}

/* INSN is a JUMP_INSN, COND_SET is the set of registers that are
   conditionally set before INSN.  Store the set of registers that
   must be considered as used by this jump in USED and that of
   registers that must be considered as set in SET.  */

static void
compute_jump_reg_dependencies (rtx insn ATTRIBUTE_UNUSED,
			       regset cond_exec ATTRIBUTE_UNUSED,
			       regset used ATTRIBUTE_UNUSED,
			       regset set ATTRIBUTE_UNUSED)
{
  /* Nothing to do here, since we postprocess jumps in
     add_branch_dependences.  */
}

/* Used in schedule_insns to initialize current_sched_info for scheduling
   regions (or single basic blocks).  */

static struct sched_info region_sched_info =
{
  init_ready_list,
  can_schedule_ready_p,
  schedule_more_p,
  new_ready,
  rgn_rank,
  rgn_print_insn,
  contributes_to_priority,
  compute_jump_reg_dependencies,

  NULL, NULL,
  NULL, NULL,
  0, 0, 0,

  add_remove_insn,
  begin_schedule_ready,
  add_block1,
  advance_target_bb,
  fix_recovery_cfg,
#ifdef ENABLE_CHECKING
  region_head_or_leaf_p,
#endif
  SCHED_RGN | USE_GLAT
#ifdef ENABLE_CHECKING
  | DETACH_LIFE_INFO
#endif
};

/* Determine if PAT sets a CLASS_LIKELY_SPILLED_P register.  */

static bool
sets_likely_spilled (rtx pat)
{
  bool ret = false;
  note_stores (pat, sets_likely_spilled_1, &ret);
  return ret;
}

static void
sets_likely_spilled_1 (rtx x, rtx pat, void *data)
{
  bool *ret = (bool *) data;

  if (GET_CODE (pat) == SET
      && REG_P (x)
      && REGNO (x) < FIRST_PSEUDO_REGISTER
      && CLASS_LIKELY_SPILLED_P (REGNO_REG_CLASS (REGNO (x))))
    *ret = true;
}

/* Add dependences so that branches are scheduled to run last in their
   block.  */

static void
add_branch_dependences (rtx head, rtx tail)
{
  rtx insn, last;

  /* For all branches, calls, uses, clobbers, cc0 setters, and instructions
     that can throw exceptions, force them to remain in order at the end of
     the block by adding dependencies and giving the last a high priority.
     There may be notes present, and prev_head may also be a note.

     Branches must obviously remain at the end.  Calls should remain at the
     end since moving them results in worse register allocation.  Uses remain
     at the end to ensure proper register allocation.

     cc0 setters remain at the end because they can't be moved away from
     their cc0 user.

     COND_EXEC insns cannot be moved past a branch (see e.g. PR17808).

     Insns setting CLASS_LIKELY_SPILLED_P registers (usually return values)
     are not moved before reload because we can wind up with register
     allocation failures.  */

  insn = tail;
  last = 0;
  while (CALL_P (insn)
	 || JUMP_P (insn)
	 || (NONJUMP_INSN_P (insn)
	     && (GET_CODE (PATTERN (insn)) == USE
		 || GET_CODE (PATTERN (insn)) == CLOBBER
		 || can_throw_internal (insn)
#ifdef HAVE_cc0
		 || sets_cc0_p (PATTERN (insn))
#endif
		 || (!reload_completed
		     && sets_likely_spilled (PATTERN (insn)))))
	 || NOTE_P (insn))
    {
      if (!NOTE_P (insn))
	{
	  if (last != 0 && !find_insn_list (insn, LOG_LINKS (last)))
	    {
	      if (! sched_insns_conditions_mutex_p (last, insn))
		add_dependence (last, insn, REG_DEP_ANTI);
	      INSN_REF_COUNT (insn)++;
	    }

	  CANT_MOVE (insn) = 1;

	  last = insn;
	}

      /* Don't overrun the bounds of the basic block.  */
      if (insn == head)
	break;

      insn = PREV_INSN (insn);
    }

  /* Make sure these insns are scheduled last in their block.  */
  insn = last;
  if (insn != 0)
    while (insn != head)
      {
	insn = prev_nonnote_insn (insn);

	if (INSN_REF_COUNT (insn) != 0)
	  continue;

	if (! sched_insns_conditions_mutex_p (last, insn))
	  add_dependence (last, insn, REG_DEP_ANTI);
	INSN_REF_COUNT (insn) = 1;
      }

#ifdef HAVE_conditional_execution
  /* Finally, if the block ends in a jump, and we are doing intra-block
     scheduling, make sure that the branch depends on any COND_EXEC insns
     inside the block to avoid moving the COND_EXECs past the branch insn.

     We only have to do this after reload, because (1) before reload there
     are no COND_EXEC insns, and (2) the region scheduler is an intra-block
     scheduler after reload.

     FIXME: We could in some cases move COND_EXEC insns past the branch if
     this scheduler would be a little smarter.  Consider this code:

		T = [addr]
	C  ?	addr += 4
	!C ?	X += 12
	C  ?	T += 1
	C  ?	jump foo

     On a target with a one cycle stall on a memory access the optimal
     sequence would be:

		T = [addr]
	C  ?	addr += 4
	C  ?	T += 1
	C  ?	jump foo
	!C ?	X += 12

     We don't want to put the 'X += 12' before the branch because it just
     wastes a cycle of execution time when the branch is taken.

     Note that in the example "!C" will always be true.  That is another
     possible improvement for handling COND_EXECs in this scheduler: it
     could remove always-true predicates.  */

  if (!reload_completed || ! JUMP_P (tail))
    return;

  insn = tail;
  while (insn != head)
    {
      insn = PREV_INSN (insn);

      /* Note that we want to add this dependency even when
	 sched_insns_conditions_mutex_p returns true.  The whole point
	 is that we _want_ this dependency, even if these insns really
	 are independent.  */
      if (INSN_P (insn) && GET_CODE (PATTERN (insn)) == COND_EXEC)
	add_dependence (tail, insn, REG_DEP_ANTI);
    }
#endif
}

/* Data structures for the computation of data dependences in a regions.  We
   keep one `deps' structure for every basic block.  Before analyzing the
   data dependences for a bb, its variables are initialized as a function of
   the variables of its predecessors.  When the analysis for a bb completes,
   we save the contents to the corresponding bb_deps[bb] variable.  */

static struct deps *bb_deps;

/* Duplicate the INSN_LIST elements of COPY and prepend them to OLD.  */

static rtx
concat_INSN_LIST (rtx copy, rtx old)
{
  rtx new = old;
  for (; copy ; copy = XEXP (copy, 1))
    new = alloc_INSN_LIST (XEXP (copy, 0), new);
  return new;
}

static void
concat_insn_mem_list (rtx copy_insns, rtx copy_mems, rtx *old_insns_p,
		      rtx *old_mems_p)
{
  rtx new_insns = *old_insns_p;
  rtx new_mems = *old_mems_p;

  while (copy_insns)
    {
      new_insns = alloc_INSN_LIST (XEXP (copy_insns, 0), new_insns);
      new_mems = alloc_EXPR_LIST (VOIDmode, XEXP (copy_mems, 0), new_mems);
      copy_insns = XEXP (copy_insns, 1);
      copy_mems = XEXP (copy_mems, 1);
    }

  *old_insns_p = new_insns;
  *old_mems_p = new_mems;
}

/* After computing the dependencies for block BB, propagate the dependencies
   found in TMP_DEPS to the successors of the block.  */
static void
propagate_deps (int bb, struct deps *pred_deps)
{
  basic_block block = BASIC_BLOCK (BB_TO_BLOCK (bb));
  edge_iterator ei;
  edge e;

  /* bb's structures are inherited by its successors.  */
  FOR_EACH_EDGE (e, ei, block->succs)
    {
      struct deps *succ_deps;
      unsigned reg;
      reg_set_iterator rsi;

      /* Only bbs "below" bb, in the same region, are interesting.  */
      if (e->dest == EXIT_BLOCK_PTR
	  || CONTAINING_RGN (block->index) != CONTAINING_RGN (e->dest->index)
	  || BLOCK_TO_BB (e->dest->index) <= bb)
	continue;

      succ_deps = bb_deps + BLOCK_TO_BB (e->dest->index);

      /* The reg_last lists are inherited by successor.  */
      EXECUTE_IF_SET_IN_REG_SET (&pred_deps->reg_last_in_use, 0, reg, rsi)
	{
	  struct deps_reg *pred_rl = &pred_deps->reg_last[reg];
	  struct deps_reg *succ_rl = &succ_deps->reg_last[reg];

	  succ_rl->uses = concat_INSN_LIST (pred_rl->uses, succ_rl->uses);
	  succ_rl->sets = concat_INSN_LIST (pred_rl->sets, succ_rl->sets);
	  succ_rl->clobbers = concat_INSN_LIST (pred_rl->clobbers,
						succ_rl->clobbers);
	  succ_rl->uses_length += pred_rl->uses_length;
	  succ_rl->clobbers_length += pred_rl->clobbers_length;
	}
      IOR_REG_SET (&succ_deps->reg_last_in_use, &pred_deps->reg_last_in_use);

      /* Mem read/write lists are inherited by successor.  */
      concat_insn_mem_list (pred_deps->pending_read_insns,
			    pred_deps->pending_read_mems,
			    &succ_deps->pending_read_insns,
			    &succ_deps->pending_read_mems);
      concat_insn_mem_list (pred_deps->pending_write_insns,
			    pred_deps->pending_write_mems,
			    &succ_deps->pending_write_insns,
			    &succ_deps->pending_write_mems);

      succ_deps->last_pending_memory_flush
	= concat_INSN_LIST (pred_deps->last_pending_memory_flush,
			    succ_deps->last_pending_memory_flush);

      succ_deps->pending_lists_length += pred_deps->pending_lists_length;
      succ_deps->pending_flush_length += pred_deps->pending_flush_length;

      /* last_function_call is inherited by successor.  */
      succ_deps->last_function_call
	= concat_INSN_LIST (pred_deps->last_function_call,
			      succ_deps->last_function_call);

      /* sched_before_next_call is inherited by successor.  */
      succ_deps->sched_before_next_call
	= concat_INSN_LIST (pred_deps->sched_before_next_call,
			    succ_deps->sched_before_next_call);
    }

  /* These lists should point to the right place, for correct
     freeing later.  */
  bb_deps[bb].pending_read_insns = pred_deps->pending_read_insns;
  bb_deps[bb].pending_read_mems = pred_deps->pending_read_mems;
  bb_deps[bb].pending_write_insns = pred_deps->pending_write_insns;
  bb_deps[bb].pending_write_mems = pred_deps->pending_write_mems;

  /* Can't allow these to be freed twice.  */
  pred_deps->pending_read_insns = 0;
  pred_deps->pending_read_mems = 0;
  pred_deps->pending_write_insns = 0;
  pred_deps->pending_write_mems = 0;
}

/* Compute backward dependences inside bb.  In a multiple blocks region:
   (1) a bb is analyzed after its predecessors, and (2) the lists in
   effect at the end of bb (after analyzing for bb) are inherited by
   bb's successors.

   Specifically for reg-reg data dependences, the block insns are
   scanned by sched_analyze () top-to-bottom.  Two lists are
   maintained by sched_analyze (): reg_last[].sets for register DEFs,
   and reg_last[].uses for register USEs.

   When analysis is completed for bb, we update for its successors:
   ;  - DEFS[succ] = Union (DEFS [succ], DEFS [bb])
   ;  - USES[succ] = Union (USES [succ], DEFS [bb])

   The mechanism for computing mem-mem data dependence is very
   similar, and the result is interblock dependences in the region.  */

static void
compute_block_backward_dependences (int bb)
{
  rtx head, tail;
  struct deps tmp_deps;

  tmp_deps = bb_deps[bb];

  /* Do the analysis for this block.  */
  gcc_assert (EBB_FIRST_BB (bb) == EBB_LAST_BB (bb));
  get_ebb_head_tail (EBB_FIRST_BB (bb), EBB_LAST_BB (bb), &head, &tail);
  sched_analyze (&tmp_deps, head, tail);
  add_branch_dependences (head, tail);

  if (current_nr_blocks > 1)
    propagate_deps (bb, &tmp_deps);

  /* Free up the INSN_LISTs.  */
  free_deps (&tmp_deps);
}

/* Remove all INSN_LISTs and EXPR_LISTs from the pending lists and add
   them to the unused_*_list variables, so that they can be reused.  */

static void
free_pending_lists (void)
{
  int bb;

  for (bb = 0; bb < current_nr_blocks; bb++)
    {
      free_INSN_LIST_list (&bb_deps[bb].pending_read_insns);
      free_INSN_LIST_list (&bb_deps[bb].pending_write_insns);
      free_EXPR_LIST_list (&bb_deps[bb].pending_read_mems);
      free_EXPR_LIST_list (&bb_deps[bb].pending_write_mems);
    }
}

/* Print dependences for debugging, callable from debugger.  */

void
debug_dependencies (void)
{
  int bb;

  fprintf (sched_dump, ";;   --------------- forward dependences: ------------ \n");
  for (bb = 0; bb < current_nr_blocks; bb++)
    {
      rtx head, tail;
      rtx next_tail;
      rtx insn;

      gcc_assert (EBB_FIRST_BB (bb) == EBB_LAST_BB (bb));
      get_ebb_head_tail (EBB_FIRST_BB (bb), EBB_LAST_BB (bb), &head, &tail);
      next_tail = NEXT_INSN (tail);
      fprintf (sched_dump, "\n;;   --- Region Dependences --- b %d bb %d \n",
	       BB_TO_BLOCK (bb), bb);

      fprintf (sched_dump, ";;   %7s%6s%6s%6s%6s%6s%14s\n",
	       "insn", "code", "bb", "dep", "prio", "cost",
	       "reservation");
      fprintf (sched_dump, ";;   %7s%6s%6s%6s%6s%6s%14s\n",
	       "----", "----", "--", "---", "----", "----",
	       "-----------");

      for (insn = head; insn != next_tail; insn = NEXT_INSN (insn))
	{
	  rtx link;

	  if (! INSN_P (insn))
	    {
	      int n;
	      fprintf (sched_dump, ";;   %6d ", INSN_UID (insn));
	      if (NOTE_P (insn))
		{
		  n = NOTE_LINE_NUMBER (insn);
		  if (n < 0)
		    fprintf (sched_dump, "%s\n", GET_NOTE_INSN_NAME (n));
		  else
		    {
		      expanded_location xloc;
		      NOTE_EXPANDED_LOCATION (xloc, insn);
		      fprintf (sched_dump, "line %d, file %s\n",
			       xloc.line, xloc.file);
		    }
		}
	      else
		fprintf (sched_dump, " {%s}\n", GET_RTX_NAME (GET_CODE (insn)));
	      continue;
	    }

	  fprintf (sched_dump,
		   ";;   %s%5d%6d%6d%6d%6d%6d   ",
		   (SCHED_GROUP_P (insn) ? "+" : " "),
		   INSN_UID (insn),
		   INSN_CODE (insn),
		   INSN_BB (insn),
		   INSN_DEP_COUNT (insn),
		   INSN_PRIORITY (insn),
		   insn_cost (insn, 0, 0));

	  if (recog_memoized (insn) < 0)
	    fprintf (sched_dump, "nothing");
	  else
	    print_reservation (sched_dump, insn);

	  fprintf (sched_dump, "\t: ");
	  for (link = INSN_DEPEND (insn); link; link = XEXP (link, 1))
	    fprintf (sched_dump, "%d ", INSN_UID (XEXP (link, 0)));
	  fprintf (sched_dump, "\n");
	}
    }
  fprintf (sched_dump, "\n");
}

/* Returns true if all the basic blocks of the current region have
   NOTE_DISABLE_SCHED_OF_BLOCK which means not to schedule that region.  */
static bool
sched_is_disabled_for_current_region_p (void)
{
  int bb;

  for (bb = 0; bb < current_nr_blocks; bb++)
    if (!(BASIC_BLOCK (BB_TO_BLOCK (bb))->flags & BB_DISABLE_SCHEDULE))
      return false;

  return true;
}

/* Schedule a region.  A region is either an inner loop, a loop-free
   subroutine, or a single basic block.  Each bb in the region is
   scheduled after its flow predecessors.  */

static void
schedule_region (int rgn)
{
  basic_block block;
  edge_iterator ei;
  edge e;
  int bb;
  int sched_rgn_n_insns = 0;

  rgn_n_insns = 0;
  /* Set variables for the current region.  */
  current_nr_blocks = RGN_NR_BLOCKS (rgn);
  current_blocks = RGN_BLOCKS (rgn);
  
  /* See comments in add_block1, for what reasons we allocate +1 element.  */ 
  ebb_head = xrealloc (ebb_head, (current_nr_blocks + 1) * sizeof (*ebb_head));
  for (bb = 0; bb <= current_nr_blocks; bb++)
    ebb_head[bb] = current_blocks + bb;

  /* Don't schedule region that is marked by
     NOTE_DISABLE_SCHED_OF_BLOCK.  */
  if (sched_is_disabled_for_current_region_p ())
    return;

  if (!RGN_DONT_CALC_DEPS (rgn))
    {
      init_deps_global ();

      /* Initializations for region data dependence analysis.  */
      bb_deps = XNEWVEC (struct deps, current_nr_blocks);
      for (bb = 0; bb < current_nr_blocks; bb++)
	init_deps (bb_deps + bb);

      /* Compute LOG_LINKS.  */
      for (bb = 0; bb < current_nr_blocks; bb++)
        compute_block_backward_dependences (bb);

      /* Compute INSN_DEPEND.  */
      for (bb = current_nr_blocks - 1; bb >= 0; bb--)
        {
          rtx head, tail;

	  gcc_assert (EBB_FIRST_BB (bb) == EBB_LAST_BB (bb));
          get_ebb_head_tail (EBB_FIRST_BB (bb), EBB_LAST_BB (bb), &head, &tail);

          compute_forward_dependences (head, tail);

          if (targetm.sched.dependencies_evaluation_hook)
            targetm.sched.dependencies_evaluation_hook (head, tail);
        }

      free_pending_lists ();

      finish_deps_global ();

      free (bb_deps);
    }
  else
    /* This is a recovery block.  It is always a single block region.  */
    gcc_assert (current_nr_blocks == 1);
      
  /* Set priorities.  */
  current_sched_info->sched_max_insns_priority = 0;
  for (bb = 0; bb < current_nr_blocks; bb++)
    {
      rtx head, tail;
      
      gcc_assert (EBB_FIRST_BB (bb) == EBB_LAST_BB (bb));
      get_ebb_head_tail (EBB_FIRST_BB (bb), EBB_LAST_BB (bb), &head, &tail);

      rgn_n_insns += set_priorities (head, tail);
    }
  current_sched_info->sched_max_insns_priority++;

  /* Compute interblock info: probabilities, split-edges, dominators, etc.  */
  if (current_nr_blocks > 1)
    {
      prob = XNEWVEC (int, current_nr_blocks);

      dom = sbitmap_vector_alloc (current_nr_blocks, current_nr_blocks);
      sbitmap_vector_zero (dom, current_nr_blocks);

      /* Use ->aux to implement EDGE_TO_BIT mapping.  */
      rgn_nr_edges = 0;
      FOR_EACH_BB (block)
	{
	  if (CONTAINING_RGN (block->index) != rgn)
	    continue;
	  FOR_EACH_EDGE (e, ei, block->succs)
	    SET_EDGE_TO_BIT (e, rgn_nr_edges++);
	}

      rgn_edges = XNEWVEC (edge, rgn_nr_edges);
      rgn_nr_edges = 0;
      FOR_EACH_BB (block)
	{
	  if (CONTAINING_RGN (block->index) != rgn)
	    continue;
	  FOR_EACH_EDGE (e, ei, block->succs)
	    rgn_edges[rgn_nr_edges++] = e;
	}

      /* Split edges.  */
      pot_split = sbitmap_vector_alloc (current_nr_blocks, rgn_nr_edges);
      sbitmap_vector_zero (pot_split, current_nr_blocks);
      ancestor_edges = sbitmap_vector_alloc (current_nr_blocks, rgn_nr_edges);
      sbitmap_vector_zero (ancestor_edges, current_nr_blocks);

      /* Compute probabilities, dominators, split_edges.  */
      for (bb = 0; bb < current_nr_blocks; bb++)
	compute_dom_prob_ps (bb);

      /* Cleanup ->aux used for EDGE_TO_BIT mapping.  */
      /* We don't need them anymore.  But we want to avoid duplication of
	 aux fields in the newly created edges.  */
      FOR_EACH_BB (block)
	{
	  if (CONTAINING_RGN (block->index) != rgn)
	    continue;
	  FOR_EACH_EDGE (e, ei, block->succs)
	    e->aux = NULL;
        }
    }

  /* Now we can schedule all blocks.  */
  for (bb = 0; bb < current_nr_blocks; bb++)
    {
      basic_block first_bb, last_bb, curr_bb;
      rtx head, tail;
      int b = BB_TO_BLOCK (bb);

      first_bb = EBB_FIRST_BB (bb);
      last_bb = EBB_LAST_BB (bb);

      get_ebb_head_tail (first_bb, last_bb, &head, &tail);

      if (no_real_insns_p (head, tail))
	{
	  gcc_assert (first_bb == last_bb);
	  continue;
	}

      current_sched_info->prev_head = PREV_INSN (head);
      current_sched_info->next_tail = NEXT_INSN (tail);

      if (write_symbols != NO_DEBUG)
	{
	  save_line_notes (b, head, tail);
	  rm_line_notes (head, tail);
	}

      /* rm_other_notes only removes notes which are _inside_ the
	 block---that is, it won't remove notes before the first real insn
	 or after the last real insn of the block.  So if the first insn
	 has a REG_SAVE_NOTE which would otherwise be emitted before the
	 insn, it is redundant with the note before the start of the
	 block, and so we have to take it out.  */
      if (INSN_P (head))
	{
	  rtx note;

	  for (note = REG_NOTES (head); note; note = XEXP (note, 1))
	    if (REG_NOTE_KIND (note) == REG_SAVE_NOTE)
	      remove_note (head, note);
	}
      else
	/* This means that first block in ebb is empty.
	   It looks to me as an impossible thing.  There at least should be
	   a recovery check, that caused the splitting.  */
	gcc_unreachable ();

      /* Remove remaining note insns from the block, save them in
	 note_list.  These notes are restored at the end of
	 schedule_block ().  */
      rm_other_notes (head, tail);

      unlink_bb_notes (first_bb, last_bb);

      target_bb = bb;

      gcc_assert (flag_schedule_interblock || current_nr_blocks == 1);
      current_sched_info->queue_must_finish_empty = current_nr_blocks == 1;

      curr_bb = first_bb;
      schedule_block (&curr_bb, rgn_n_insns);
      gcc_assert (EBB_FIRST_BB (bb) == first_bb);
      sched_rgn_n_insns += sched_n_insns;

      /* Clean up.  */
      if (current_nr_blocks > 1)
	{
	  free (candidate_table);
	  free (bblst_table);
	  free (edgelst_table);
	}
    }

  /* Sanity check: verify that all region insns were scheduled.  */
  gcc_assert (sched_rgn_n_insns == rgn_n_insns);

  /* Restore line notes.  */
  if (write_symbols != NO_DEBUG)
    {
      for (bb = 0; bb < current_nr_blocks; bb++)
	{
	  rtx head, tail;

	  get_ebb_head_tail (EBB_FIRST_BB (bb), EBB_LAST_BB (bb), &head, &tail);
	  restore_line_notes (head, tail);
	}
    }

  /* Done with this region.  */

  if (current_nr_blocks > 1)
    {
      free (prob);
      sbitmap_vector_free (dom);
      sbitmap_vector_free (pot_split);
      sbitmap_vector_free (ancestor_edges);
      free (rgn_edges);
    }
}

/* Indexed by region, holds the number of death notes found in that region.
   Used for consistency checks.  */
static int *deaths_in_region;

/* Initialize data structures for region scheduling.  */

static void
init_regions (void)
{
  sbitmap blocks;
  int rgn;

  nr_regions = 0;
  rgn_table = 0;
  rgn_bb_table = 0;
  block_to_bb = 0;
  containing_rgn = 0;
  extend_regions ();

  /* Compute regions for scheduling.  */
  if (reload_completed
      || n_basic_blocks == NUM_FIXED_BLOCKS + 1
      || !flag_schedule_interblock
      || is_cfg_nonregular ())
    {
      find_single_block_region ();
    }
  else
    {
      /* Compute the dominators and post dominators.  */
      calculate_dominance_info (CDI_DOMINATORS);

      /* Find regions.  */
      find_rgns ();

      if (sched_verbose >= 3)
	debug_regions ();

      /* For now.  This will move as more and more of haifa is converted
	 to using the cfg code in flow.c.  */
      free_dominance_info (CDI_DOMINATORS);
    }
  RGN_BLOCKS (nr_regions) = RGN_BLOCKS (nr_regions - 1) +
    RGN_NR_BLOCKS (nr_regions - 1);


  if (CHECK_DEAD_NOTES)
    {
      blocks = sbitmap_alloc (last_basic_block);
      deaths_in_region = XNEWVEC (int, nr_regions);
      /* Remove all death notes from the subroutine.  */
      for (rgn = 0; rgn < nr_regions; rgn++)
        check_dead_notes1 (rgn, blocks);

      sbitmap_free (blocks);
    }
  else
    count_or_remove_death_notes (NULL, 1);
}

/* The one entry point in this file.  */

void
schedule_insns (void)
{
  sbitmap large_region_blocks, blocks;
  int rgn;
  int any_large_regions;
  basic_block bb;

  /* Taking care of this degenerate case makes the rest of
     this code simpler.  */
  if (n_basic_blocks == NUM_FIXED_BLOCKS)
    return;

  nr_inter = 0;
  nr_spec = 0;

  /* We need current_sched_info in init_dependency_caches, which is
     invoked via sched_init.  */
  current_sched_info = &region_sched_info;

  sched_init ();

  min_spec_prob = ((PARAM_VALUE (PARAM_MIN_SPEC_PROB) * REG_BR_PROB_BASE)
		    / 100);

  init_regions ();

  /* EBB_HEAD is a region-scope structure.  But we realloc it for
     each region to save time/memory/something else.  */
  ebb_head = 0;
  
  /* Schedule every region in the subroutine.  */
  for (rgn = 0; rgn < nr_regions; rgn++)
    schedule_region (rgn);
  
  free(ebb_head);

  /* Update life analysis for the subroutine.  Do single block regions
     first so that we can verify that live_at_start didn't change.  Then
     do all other blocks.  */
  /* ??? There is an outside possibility that update_life_info, or more
     to the point propagate_block, could get called with nonzero flags
     more than once for one basic block.  This would be kinda bad if it
     were to happen, since REG_INFO would be accumulated twice for the
     block, and we'd have twice the REG_DEAD notes.

     I'm fairly certain that this _shouldn't_ happen, since I don't think
     that live_at_start should change at region heads.  Not sure what the
     best way to test for this kind of thing...  */

  if (current_sched_info->flags & DETACH_LIFE_INFO)
    /* this flag can be set either by the target or by ENABLE_CHECKING.  */
    attach_life_info ();

  allocate_reg_life_data ();

  any_large_regions = 0;
  large_region_blocks = sbitmap_alloc (last_basic_block);
  sbitmap_zero (large_region_blocks);
  FOR_EACH_BB (bb)
    SET_BIT (large_region_blocks, bb->index);

  blocks = sbitmap_alloc (last_basic_block);
  sbitmap_zero (blocks);

  /* Update life information.  For regions consisting of multiple blocks
     we've possibly done interblock scheduling that affects global liveness.
     For regions consisting of single blocks we need to do only local
     liveness.  */
  for (rgn = 0; rgn < nr_regions; rgn++)    
    if (RGN_NR_BLOCKS (rgn) > 1
	/* Or the only block of this region has been split.  */
	|| RGN_HAS_REAL_EBB (rgn)
	/* New blocks (e.g. recovery blocks) should be processed
	   as parts of large regions.  */
	|| !glat_start[rgn_bb_table[RGN_BLOCKS (rgn)]])
      any_large_regions = 1;
    else
      {
	SET_BIT (blocks, rgn_bb_table[RGN_BLOCKS (rgn)]);
	RESET_BIT (large_region_blocks, rgn_bb_table[RGN_BLOCKS (rgn)]);
      }

  /* Don't update reg info after reload, since that affects
     regs_ever_live, which should not change after reload.  */
  update_life_info (blocks, UPDATE_LIFE_LOCAL,
		    (reload_completed ? PROP_DEATH_NOTES
		     : (PROP_DEATH_NOTES | PROP_REG_INFO)));
  if (any_large_regions)
    {
      update_life_info (large_region_blocks, UPDATE_LIFE_GLOBAL,
			(reload_completed ? PROP_DEATH_NOTES
			 : (PROP_DEATH_NOTES | PROP_REG_INFO)));

#ifdef ENABLE_CHECKING
      check_reg_live (true);
#endif
    }

  if (CHECK_DEAD_NOTES)
    {
      /* Verify the counts of basic block notes in single basic block
         regions.  */
      for (rgn = 0; rgn < nr_regions; rgn++)
	if (RGN_NR_BLOCKS (rgn) == 1)
	  {
	    sbitmap_zero (blocks);
	    SET_BIT (blocks, rgn_bb_table[RGN_BLOCKS (rgn)]);

	    gcc_assert (deaths_in_region[rgn]
			== count_or_remove_death_notes (blocks, 0));
	  }
      free (deaths_in_region);
    }

  /* Reposition the prologue and epilogue notes in case we moved the
     prologue/epilogue insns.  */
  if (reload_completed)
    reposition_prologue_and_epilogue_notes (get_insns ());

  /* Delete redundant line notes.  */
  if (write_symbols != NO_DEBUG)
    rm_redundant_line_notes ();

  if (sched_verbose)
    {
      if (reload_completed == 0 && flag_schedule_interblock)
	{
	  fprintf (sched_dump,
		   "\n;; Procedure interblock/speculative motions == %d/%d \n",
		   nr_inter, nr_spec);
	}
      else
	gcc_assert (nr_inter <= 0);
      fprintf (sched_dump, "\n\n");
    }

  /* Clean up.  */
  free (rgn_table);
  free (rgn_bb_table);
  free (block_to_bb);
  free (containing_rgn);

  sched_finish ();

  sbitmap_free (blocks);
  sbitmap_free (large_region_blocks);
}

/* INSN has been added to/removed from current region.  */
static void
add_remove_insn (rtx insn, int remove_p)
{
  if (!remove_p)
    rgn_n_insns++;
  else
    rgn_n_insns--;

  if (INSN_BB (insn) == target_bb)
    {
      if (!remove_p)
	target_n_insns++;
      else
	target_n_insns--;
    }
}

/* Extend internal data structures.  */
static void
extend_regions (void)
{
  rgn_table = XRESIZEVEC (region, rgn_table, n_basic_blocks);
  rgn_bb_table = XRESIZEVEC (int, rgn_bb_table, n_basic_blocks);
  block_to_bb = XRESIZEVEC (int, block_to_bb, last_basic_block);
  containing_rgn = XRESIZEVEC (int, containing_rgn, last_basic_block);
}

/* BB was added to ebb after AFTER.  */
static void
add_block1 (basic_block bb, basic_block after)
{
  extend_regions ();

  if (after == 0 || after == EXIT_BLOCK_PTR)
    {
      int i;
      
      i = RGN_BLOCKS (nr_regions);
      /* I - first free position in rgn_bb_table.  */

      rgn_bb_table[i] = bb->index;
      RGN_NR_BLOCKS (nr_regions) = 1;
      RGN_DONT_CALC_DEPS (nr_regions) = after == EXIT_BLOCK_PTR;
      RGN_HAS_REAL_EBB (nr_regions) = 0;
      CONTAINING_RGN (bb->index) = nr_regions;
      BLOCK_TO_BB (bb->index) = 0;

      nr_regions++;
      
      RGN_BLOCKS (nr_regions) = i + 1;

      if (CHECK_DEAD_NOTES)
        {
          sbitmap blocks = sbitmap_alloc (last_basic_block);
          deaths_in_region = xrealloc (deaths_in_region, nr_regions *
				       sizeof (*deaths_in_region));

          check_dead_notes1 (nr_regions - 1, blocks);
      
          sbitmap_free (blocks);
        }
    }
  else
    { 
      int i, pos;

      /* We need to fix rgn_table, block_to_bb, containing_rgn
	 and ebb_head.  */

      BLOCK_TO_BB (bb->index) = BLOCK_TO_BB (after->index);

      /* We extend ebb_head to one more position to
	 easily find the last position of the last ebb in 
	 the current region.  Thus, ebb_head[BLOCK_TO_BB (after) + 1]
	 is _always_ valid for access.  */

      i = BLOCK_TO_BB (after->index) + 1;
      pos = ebb_head[i] - 1;
      /* Now POS is the index of the last block in the region.  */

      /* Find index of basic block AFTER.  */
      for (; rgn_bb_table[pos] != after->index; pos--);

      pos++;
      gcc_assert (pos > ebb_head[i - 1]);

      /* i - ebb right after "AFTER".  */
      /* ebb_head[i] - VALID.  */

      /* Source position: ebb_head[i]
	 Destination position: ebb_head[i] + 1
	 Last position: 
	   RGN_BLOCKS (nr_regions) - 1
	 Number of elements to copy: (last_position) - (source_position) + 1
       */
      
      memmove (rgn_bb_table + pos + 1,
	       rgn_bb_table + pos,
	       ((RGN_BLOCKS (nr_regions) - 1) - (pos) + 1)
	       * sizeof (*rgn_bb_table));

      rgn_bb_table[pos] = bb->index;
      
      for (; i <= current_nr_blocks; i++)
	ebb_head [i]++;

      i = CONTAINING_RGN (after->index);
      CONTAINING_RGN (bb->index) = i;
      
      RGN_HAS_REAL_EBB (i) = 1;

      for (++i; i <= nr_regions; i++)
	RGN_BLOCKS (i)++;

      /* We don't need to call check_dead_notes1 () because this new block
	 is just a split of the old.  We don't want to count anything twice.  */
    }
}

/* Fix internal data after interblock movement of jump instruction.
   For parameter meaning please refer to
   sched-int.h: struct sched_info: fix_recovery_cfg.  */
static void
fix_recovery_cfg (int bbi, int check_bbi, int check_bb_nexti)
{
  int old_pos, new_pos, i;

  BLOCK_TO_BB (check_bb_nexti) = BLOCK_TO_BB (bbi);
  
  for (old_pos = ebb_head[BLOCK_TO_BB (check_bbi) + 1] - 1;
       rgn_bb_table[old_pos] != check_bb_nexti;
       old_pos--);
  gcc_assert (old_pos > ebb_head[BLOCK_TO_BB (check_bbi)]);

  for (new_pos = ebb_head[BLOCK_TO_BB (bbi) + 1] - 1;
       rgn_bb_table[new_pos] != bbi;
       new_pos--);
  new_pos++;
  gcc_assert (new_pos > ebb_head[BLOCK_TO_BB (bbi)]);
  
  gcc_assert (new_pos < old_pos);

  memmove (rgn_bb_table + new_pos + 1,
	   rgn_bb_table + new_pos,
	   (old_pos - new_pos) * sizeof (*rgn_bb_table));

  rgn_bb_table[new_pos] = check_bb_nexti;

  for (i = BLOCK_TO_BB (bbi) + 1; i <= BLOCK_TO_BB (check_bbi); i++)
    ebb_head[i]++;
}

/* Return next block in ebb chain.  For parameter meaning please refer to
   sched-int.h: struct sched_info: advance_target_bb.  */
static basic_block
advance_target_bb (basic_block bb, rtx insn)
{
  if (insn)
    return 0;

  gcc_assert (BLOCK_TO_BB (bb->index) == target_bb
	      && BLOCK_TO_BB (bb->next_bb->index) == target_bb);
  return bb->next_bb;
}

/* Count and remove death notes in region RGN, which consists of blocks
   with indecies in BLOCKS.  */
static void
check_dead_notes1 (int rgn, sbitmap blocks)
{
  int b;

  sbitmap_zero (blocks);
  for (b = RGN_NR_BLOCKS (rgn) - 1; b >= 0; --b)
    SET_BIT (blocks, rgn_bb_table[RGN_BLOCKS (rgn) + b]);

  deaths_in_region[rgn] = count_or_remove_death_notes (blocks, 1);
}

#ifdef ENABLE_CHECKING
/* Return non zero, if BB is head or leaf (depending of LEAF_P) block in
   current region.  For more information please refer to
   sched-int.h: struct sched_info: region_head_or_leaf_p.  */
static int
region_head_or_leaf_p (basic_block bb, int leaf_p)
{
  if (!leaf_p)    
    return bb->index == rgn_bb_table[RGN_BLOCKS (CONTAINING_RGN (bb->index))];
  else
    {
      int i;
      edge e;
      edge_iterator ei;
      
      i = CONTAINING_RGN (bb->index);

      FOR_EACH_EDGE (e, ei, bb->succs)
	if (e->dest != EXIT_BLOCK_PTR
            && CONTAINING_RGN (e->dest->index) == i
	    /* except self-loop.  */
	    && e->dest != bb)
	  return 0;
      
      return 1;
    }
}
#endif /* ENABLE_CHECKING  */

#endif

static bool
gate_handle_sched (void)
{
#ifdef INSN_SCHEDULING
  return flag_schedule_insns;
#else
  return 0;
#endif
}

/* Run instruction scheduler.  */
static unsigned int
rest_of_handle_sched (void)
{
#ifdef INSN_SCHEDULING
  /* Do control and data sched analysis,
     and write some of the results to dump file.  */

  schedule_insns ();
#endif
  return 0;
}

static bool
gate_handle_sched2 (void)
{
#ifdef INSN_SCHEDULING
  return optimize > 0 && flag_schedule_insns_after_reload;
#else
  return 0;
#endif
}

/* Run second scheduling pass after reload.  */
static unsigned int
rest_of_handle_sched2 (void)
{
#ifdef INSN_SCHEDULING
  /* Do control and data sched analysis again,
     and write some more of the results to dump file.  */

  split_all_insns (1);

  if (flag_sched2_use_superblocks || flag_sched2_use_traces)
    {
      schedule_ebbs ();
      /* No liveness updating code yet, but it should be easy to do.
         reg-stack recomputes the liveness when needed for now.  */
      count_or_remove_death_notes (NULL, 1);
      cleanup_cfg (CLEANUP_EXPENSIVE);
    }
  else
    schedule_insns ();
#endif
  return 0;
}

struct tree_opt_pass pass_sched =
{
  "sched1",                             /* name */
  gate_handle_sched,                    /* gate */
  rest_of_handle_sched,                 /* execute */
  NULL,                                 /* sub */
  NULL,                                 /* next */
  0,                                    /* static_pass_number */
  TV_SCHED,                             /* tv_id */
  0,                                    /* properties_required */
  0,                                    /* properties_provided */
  0,                                    /* properties_destroyed */
  0,                                    /* todo_flags_start */
  TODO_dump_func |
  TODO_ggc_collect,                     /* todo_flags_finish */
  'S'                                   /* letter */
};

struct tree_opt_pass pass_sched2 =
{
  "sched2",                             /* name */
  gate_handle_sched2,                   /* gate */
  rest_of_handle_sched2,                /* execute */
  NULL,                                 /* sub */
  NULL,                                 /* next */
  0,                                    /* static_pass_number */
  TV_SCHED2,                            /* tv_id */
  0,                                    /* properties_required */
  0,                                    /* properties_provided */
  0,                                    /* properties_destroyed */
  0,                                    /* todo_flags_start */
  TODO_dump_func |
  TODO_ggc_collect,                     /* todo_flags_finish */
  'R'                                   /* letter */
};

