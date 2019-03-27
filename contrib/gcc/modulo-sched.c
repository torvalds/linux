/* Swing Modulo Scheduling implementation.
   Copyright (C) 2004, 2005, 2006
   Free Software Foundation, Inc.
   Contributed by Ayal Zaks and Mustafa Hagog <zaks,mustafa@il.ibm.com>

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
#include "sched-int.h"
#include "target.h"
#include "cfglayout.h"
#include "cfgloop.h"
#include "cfghooks.h"
#include "expr.h"
#include "params.h"
#include "gcov-io.h"
#include "df.h"
#include "ddg.h"
#include "timevar.h"
#include "tree-pass.h"

#ifdef INSN_SCHEDULING

/* This file contains the implementation of the Swing Modulo Scheduler,
   described in the following references:
   [1] J. Llosa, A. Gonzalez, E. Ayguade, M. Valero., and J. Eckhardt.
       Lifetime--sensitive modulo scheduling in a production environment.
       IEEE Trans. on Comps., 50(3), March 2001
   [2] J. Llosa, A. Gonzalez, E. Ayguade, and M. Valero.
       Swing Modulo Scheduling: A Lifetime Sensitive Approach.
       PACT '96 , pages 80-87, October 1996 (Boston - Massachusetts - USA).

   The basic structure is:
   1. Build a data-dependence graph (DDG) for each loop.
   2. Use the DDG to order the insns of a loop (not in topological order
      necessarily, but rather) trying to place each insn after all its
      predecessors _or_ after all its successors.
   3. Compute MII: a lower bound on the number of cycles to schedule the loop.
   4. Use the ordering to perform list-scheduling of the loop:
      1. Set II = MII.  We will try to schedule the loop within II cycles.
      2. Try to schedule the insns one by one according to the ordering.
	 For each insn compute an interval of cycles by considering already-
	 scheduled preds and succs (and associated latencies); try to place
	 the insn in the cycles of this window checking for potential
	 resource conflicts (using the DFA interface).
	 Note: this is different from the cycle-scheduling of schedule_insns;
	 here the insns are not scheduled monotonically top-down (nor bottom-
	 up).
      3. If failed in scheduling all insns - bump II++ and try again, unless
	 II reaches an upper bound MaxII, in which case report failure.
   5. If we succeeded in scheduling the loop within II cycles, we now
      generate prolog and epilog, decrease the counter of the loop, and
      perform modulo variable expansion for live ranges that span more than
      II cycles (i.e. use register copies to prevent a def from overwriting
      itself before reaching the use).
*/


/* This page defines partial-schedule structures and functions for
   modulo scheduling.  */

typedef struct partial_schedule *partial_schedule_ptr;
typedef struct ps_insn *ps_insn_ptr;

/* The minimum (absolute) cycle that a node of ps was scheduled in.  */
#define PS_MIN_CYCLE(ps) (((partial_schedule_ptr)(ps))->min_cycle)

/* The maximum (absolute) cycle that a node of ps was scheduled in.  */
#define PS_MAX_CYCLE(ps) (((partial_schedule_ptr)(ps))->max_cycle)

/* Perform signed modulo, always returning a non-negative value.  */
#define SMODULO(x,y) ((x) % (y) < 0 ? ((x) % (y) + (y)) : (x) % (y))

/* The number of different iterations the nodes in ps span, assuming
   the stage boundaries are placed efficiently.  */
#define PS_STAGE_COUNT(ps) ((PS_MAX_CYCLE (ps) - PS_MIN_CYCLE (ps) \
			     + 1 + (ps)->ii - 1) / (ps)->ii)

/* A single instruction in the partial schedule.  */
struct ps_insn
{
  /* The corresponding DDG_NODE.  */
  ddg_node_ptr node;

  /* The (absolute) cycle in which the PS instruction is scheduled.
     Same as SCHED_TIME (node).  */
  int cycle;

  /* The next/prev PS_INSN in the same row.  */
  ps_insn_ptr next_in_row,
	      prev_in_row;

  /* The number of nodes in the same row that come after this node.  */
  int row_rest_count;
};

/* Holds the partial schedule as an array of II rows.  Each entry of the
   array points to a linked list of PS_INSNs, which represents the
   instructions that are scheduled for that row.  */
struct partial_schedule
{
  int ii;	/* Number of rows in the partial schedule.  */
  int history;  /* Threshold for conflict checking using DFA.  */

  /* rows[i] points to linked list of insns scheduled in row i (0<=i<ii).  */
  ps_insn_ptr *rows;

  /* The earliest absolute cycle of an insn in the partial schedule.  */
  int min_cycle;

  /* The latest absolute cycle of an insn in the partial schedule.  */
  int max_cycle;

  ddg_ptr g;	/* The DDG of the insns in the partial schedule.  */
};

/* We use this to record all the register replacements we do in
   the kernel so we can undo SMS if it is not profitable.  */
struct undo_replace_buff_elem
{
  rtx insn;
  rtx orig_reg;
  rtx new_reg;
  struct undo_replace_buff_elem *next;
};


  
static partial_schedule_ptr create_partial_schedule (int ii, ddg_ptr, int history);
static void free_partial_schedule (partial_schedule_ptr);
static void reset_partial_schedule (partial_schedule_ptr, int new_ii);
void print_partial_schedule (partial_schedule_ptr, FILE *);
static int kernel_number_of_cycles (rtx first_insn, rtx last_insn);
static ps_insn_ptr ps_add_node_check_conflicts (partial_schedule_ptr,
						ddg_node_ptr node, int cycle,
						sbitmap must_precede,
						sbitmap must_follow);
static void rotate_partial_schedule (partial_schedule_ptr, int);
void set_row_column_for_ps (partial_schedule_ptr);
static bool ps_unschedule_node (partial_schedule_ptr, ddg_node_ptr );


/* This page defines constants and structures for the modulo scheduling
   driver.  */

/* As in haifa-sched.c:  */
/* issue_rate is the number of insns that can be scheduled in the same
   machine cycle.  It can be defined in the config/mach/mach.h file,
   otherwise we set it to 1.  */

static int issue_rate;

static int sms_order_nodes (ddg_ptr, int, int * result);
static void set_node_sched_params (ddg_ptr);
static partial_schedule_ptr sms_schedule_by_order (ddg_ptr, int, int, int *);
static void permute_partial_schedule (partial_schedule_ptr ps, rtx last);
static void generate_prolog_epilog (partial_schedule_ptr ,struct loop * loop, rtx);
static void duplicate_insns_of_cycles (partial_schedule_ptr ps,
				       int from_stage, int to_stage,
				       int is_prolog);

#define SCHED_ASAP(x) (((node_sched_params_ptr)(x)->aux.info)->asap)
#define SCHED_TIME(x) (((node_sched_params_ptr)(x)->aux.info)->time)
#define SCHED_FIRST_REG_MOVE(x) \
	(((node_sched_params_ptr)(x)->aux.info)->first_reg_move)
#define SCHED_NREG_MOVES(x) \
	(((node_sched_params_ptr)(x)->aux.info)->nreg_moves)
#define SCHED_ROW(x) (((node_sched_params_ptr)(x)->aux.info)->row)
#define SCHED_STAGE(x) (((node_sched_params_ptr)(x)->aux.info)->stage)
#define SCHED_COLUMN(x) (((node_sched_params_ptr)(x)->aux.info)->column)

/* The scheduling parameters held for each node.  */
typedef struct node_sched_params
{
  int asap;	/* A lower-bound on the absolute scheduling cycle.  */
  int time;	/* The absolute scheduling cycle (time >= asap).  */

  /* The following field (first_reg_move) is a pointer to the first
     register-move instruction added to handle the modulo-variable-expansion
     of the register defined by this node.  This register-move copies the
     original register defined by the node.  */
  rtx first_reg_move;

  /* The number of register-move instructions added, immediately preceding
     first_reg_move.  */
  int nreg_moves;

  int row;    /* Holds time % ii.  */
  int stage;  /* Holds time / ii.  */

  /* The column of a node inside the ps.  If nodes u, v are on the same row,
     u will precede v if column (u) < column (v).  */
  int column;
} *node_sched_params_ptr;


/* The following three functions are copied from the current scheduler
   code in order to use sched_analyze() for computing the dependencies.
   They are used when initializing the sched_info structure.  */
static const char *
sms_print_insn (rtx insn, int aligned ATTRIBUTE_UNUSED)
{
  static char tmp[80];

  sprintf (tmp, "i%4d", INSN_UID (insn));
  return tmp;
}

static void
compute_jump_reg_dependencies (rtx insn ATTRIBUTE_UNUSED,
			       regset cond_exec ATTRIBUTE_UNUSED,
			       regset used ATTRIBUTE_UNUSED,
			       regset set ATTRIBUTE_UNUSED)
{
}

static struct sched_info sms_sched_info =
{
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  sms_print_insn,
  NULL,
  compute_jump_reg_dependencies,
  NULL, NULL,
  NULL, NULL,
  0, 0, 0,

  NULL, NULL, NULL, NULL, NULL,
#ifdef ENABLE_CHECKING
  NULL,
#endif
  0
};


/* Return the register decremented and tested in INSN,
   or zero if it is not a decrement-and-branch insn.  */

static rtx
doloop_register_get (rtx insn ATTRIBUTE_UNUSED)
{
#ifdef HAVE_doloop_end
  rtx pattern, reg, condition;

  if (! JUMP_P (insn))
    return NULL_RTX;

  pattern = PATTERN (insn);
  condition = doloop_condition_get (pattern);
  if (! condition)
    return NULL_RTX;

  if (REG_P (XEXP (condition, 0)))
    reg = XEXP (condition, 0);
  else if (GET_CODE (XEXP (condition, 0)) == PLUS
	   && REG_P (XEXP (XEXP (condition, 0), 0)))
    reg = XEXP (XEXP (condition, 0), 0);
  else
    gcc_unreachable ();

  return reg;
#else
  return NULL_RTX;
#endif
}

/* Check if COUNT_REG is set to a constant in the PRE_HEADER block, so
   that the number of iterations is a compile-time constant.  If so,
   return the rtx that sets COUNT_REG to a constant, and set COUNT to
   this constant.  Otherwise return 0.  */
static rtx
const_iteration_count (rtx count_reg, basic_block pre_header,
		       HOST_WIDEST_INT * count)
{
  rtx insn;
  rtx head, tail;

  if (! pre_header)
    return NULL_RTX;

  get_ebb_head_tail (pre_header, pre_header, &head, &tail);

  for (insn = tail; insn != PREV_INSN (head); insn = PREV_INSN (insn))
    if (INSN_P (insn) && single_set (insn) &&
	rtx_equal_p (count_reg, SET_DEST (single_set (insn))))
      {
	rtx pat = single_set (insn);

	if (GET_CODE (SET_SRC (pat)) == CONST_INT)
	  {
	    *count = INTVAL (SET_SRC (pat));
	    return insn;
	  }

	return NULL_RTX;
      }

  return NULL_RTX;
}

/* A very simple resource-based lower bound on the initiation interval.
   ??? Improve the accuracy of this bound by considering the
   utilization of various units.  */
static int
res_MII (ddg_ptr g)
{
  return (g->num_nodes / issue_rate);
}


/* Points to the array that contains the sched data for each node.  */
static node_sched_params_ptr node_sched_params;

/* Allocate sched_params for each node and initialize it.  Assumes that
   the aux field of each node contain the asap bound (computed earlier),
   and copies it into the sched_params field.  */
static void
set_node_sched_params (ddg_ptr g)
{
  int i;

  /* Allocate for each node in the DDG a place to hold the "sched_data".  */
  /* Initialize ASAP/ALAP/HIGHT to zero.  */
  node_sched_params = (node_sched_params_ptr)
		       xcalloc (g->num_nodes,
				sizeof (struct node_sched_params));

  /* Set the pointer of the general data of the node to point to the
     appropriate sched_params structure.  */
  for (i = 0; i < g->num_nodes; i++)
    {
      /* Watch out for aliasing problems?  */
      node_sched_params[i].asap = g->nodes[i].aux.count;
      g->nodes[i].aux.info = &node_sched_params[i];
    }
}

static void
print_node_sched_params (FILE * file, int num_nodes)
{
  int i;

  if (! file)
    return;
  for (i = 0; i < num_nodes; i++)
    {
      node_sched_params_ptr nsp = &node_sched_params[i];
      rtx reg_move = nsp->first_reg_move;
      int j;

      fprintf (file, "Node %d:\n", i);
      fprintf (file, " asap = %d:\n", nsp->asap);
      fprintf (file, " time = %d:\n", nsp->time);
      fprintf (file, " nreg_moves = %d:\n", nsp->nreg_moves);
      for (j = 0; j < nsp->nreg_moves; j++)
	{
	  fprintf (file, " reg_move = ");
	  print_rtl_single (file, reg_move);
	  reg_move = PREV_INSN (reg_move);
	}
    }
}

/* Calculate an upper bound for II.  SMS should not schedule the loop if it
   requires more cycles than this bound.  Currently set to the sum of the
   longest latency edge for each node.  Reset based on experiments.  */
static int
calculate_maxii (ddg_ptr g)
{
  int i;
  int maxii = 0;

  for (i = 0; i < g->num_nodes; i++)
    {
      ddg_node_ptr u = &g->nodes[i];
      ddg_edge_ptr e;
      int max_edge_latency = 0;

      for (e = u->out; e; e = e->next_out)
	max_edge_latency = MAX (max_edge_latency, e->latency);

      maxii += max_edge_latency;
    }
  return maxii;
}

/*
   Breaking intra-loop register anti-dependences:
   Each intra-loop register anti-dependence implies a cross-iteration true
   dependence of distance 1. Therefore, we can remove such false dependencies
   and figure out if the partial schedule broke them by checking if (for a
   true-dependence of distance 1): SCHED_TIME (def) < SCHED_TIME (use) and
   if so generate a register move.   The number of such moves is equal to:
              SCHED_TIME (use) - SCHED_TIME (def)       { 0 broken
   nreg_moves = ----------------------------------- + 1 - {   dependence.
                            ii                          { 1 if not.
*/
static struct undo_replace_buff_elem *
generate_reg_moves (partial_schedule_ptr ps)
{
  ddg_ptr g = ps->g;
  int ii = ps->ii;
  int i;
  struct undo_replace_buff_elem *reg_move_replaces = NULL;

  for (i = 0; i < g->num_nodes; i++)
    {
      ddg_node_ptr u = &g->nodes[i];
      ddg_edge_ptr e;
      int nreg_moves = 0, i_reg_move;
      sbitmap *uses_of_defs;
      rtx last_reg_move;
      rtx prev_reg, old_reg;

      /* Compute the number of reg_moves needed for u, by looking at life
	 ranges started at u (excluding self-loops).  */
      for (e = u->out; e; e = e->next_out)
	if (e->type == TRUE_DEP && e->dest != e->src)
	  {
	    int nreg_moves4e = (SCHED_TIME (e->dest) - SCHED_TIME (e->src)) / ii;

            if (e->distance == 1)
              nreg_moves4e = (SCHED_TIME (e->dest) - SCHED_TIME (e->src) + ii) / ii;

	    /* If dest precedes src in the schedule of the kernel, then dest
	       will read before src writes and we can save one reg_copy.  */
	    if (SCHED_ROW (e->dest) == SCHED_ROW (e->src)
		&& SCHED_COLUMN (e->dest) < SCHED_COLUMN (e->src))
	      nreg_moves4e--;

	    nreg_moves = MAX (nreg_moves, nreg_moves4e);
	  }

      if (nreg_moves == 0)
	continue;

      /* Every use of the register defined by node may require a different
	 copy of this register, depending on the time the use is scheduled.
	 Set a bitmap vector, telling which nodes use each copy of this
	 register.  */
      uses_of_defs = sbitmap_vector_alloc (nreg_moves, g->num_nodes);
      sbitmap_vector_zero (uses_of_defs, nreg_moves);
      for (e = u->out; e; e = e->next_out)
	if (e->type == TRUE_DEP && e->dest != e->src)
	  {
	    int dest_copy = (SCHED_TIME (e->dest) - SCHED_TIME (e->src)) / ii;

	    if (e->distance == 1)
	      dest_copy = (SCHED_TIME (e->dest) - SCHED_TIME (e->src) + ii) / ii;

	    if (SCHED_ROW (e->dest) == SCHED_ROW (e->src)
		&& SCHED_COLUMN (e->dest) < SCHED_COLUMN (e->src))
	      dest_copy--;

	    if (dest_copy)
	      SET_BIT (uses_of_defs[dest_copy - 1], e->dest->cuid);
	  }

      /* Now generate the reg_moves, attaching relevant uses to them.  */
      SCHED_NREG_MOVES (u) = nreg_moves;
      old_reg = prev_reg = copy_rtx (SET_DEST (single_set (u->insn)));
      last_reg_move = u->insn;

      for (i_reg_move = 0; i_reg_move < nreg_moves; i_reg_move++)
	{
	  unsigned int i_use = 0;
	  rtx new_reg = gen_reg_rtx (GET_MODE (prev_reg));
	  rtx reg_move = gen_move_insn (new_reg, prev_reg);
	  sbitmap_iterator sbi;

	  add_insn_before (reg_move, last_reg_move);
	  last_reg_move = reg_move;

	  if (!SCHED_FIRST_REG_MOVE (u))
	    SCHED_FIRST_REG_MOVE (u) = reg_move;

	  EXECUTE_IF_SET_IN_SBITMAP (uses_of_defs[i_reg_move], 0, i_use, sbi)
	    {
	      struct undo_replace_buff_elem *rep;

	      rep = (struct undo_replace_buff_elem *)
		    xcalloc (1, sizeof (struct undo_replace_buff_elem));
	      rep->insn = g->nodes[i_use].insn;
	      rep->orig_reg = old_reg;
	      rep->new_reg = new_reg;

	      if (! reg_move_replaces)
		reg_move_replaces = rep;
	      else
		{
		  rep->next = reg_move_replaces;
		  reg_move_replaces = rep;
		}

	      replace_rtx (g->nodes[i_use].insn, old_reg, new_reg);
	    }

	  prev_reg = new_reg;
	}
      sbitmap_vector_free (uses_of_defs);
    }
  return reg_move_replaces;
}

/* We call this when we want to undo the SMS schedule for a given loop.
   One of the things that we do is to delete the register moves generated
   for the sake of SMS; this function deletes the register move instructions
   recorded in the undo buffer.  */
static void
undo_generate_reg_moves (partial_schedule_ptr ps,
			 struct undo_replace_buff_elem *reg_move_replaces)
{
  int i,j;

  for (i = 0; i < ps->g->num_nodes; i++)
    {
      ddg_node_ptr u = &ps->g->nodes[i];
      rtx prev;
      rtx crr = SCHED_FIRST_REG_MOVE (u);

      for (j = 0; j < SCHED_NREG_MOVES (u); j++)
	{
	  prev = PREV_INSN (crr);
	  delete_insn (crr);
	  crr = prev;
	}
      SCHED_FIRST_REG_MOVE (u) = NULL_RTX;
    }

  while (reg_move_replaces)
    {
      struct undo_replace_buff_elem *rep = reg_move_replaces;

      reg_move_replaces = reg_move_replaces->next;
      replace_rtx (rep->insn, rep->new_reg, rep->orig_reg);
    }
}

/* Free memory allocated for the undo buffer.  */
static void
free_undo_replace_buff (struct undo_replace_buff_elem *reg_move_replaces)
{

  while (reg_move_replaces)
    {
      struct undo_replace_buff_elem *rep = reg_move_replaces;

      reg_move_replaces = reg_move_replaces->next;
      free (rep);
    }
}

/* Bump the SCHED_TIMEs of all nodes to start from zero.  Set the values
   of SCHED_ROW and SCHED_STAGE.  */
static void
normalize_sched_times (partial_schedule_ptr ps)
{
  int i;
  ddg_ptr g = ps->g;
  int amount = PS_MIN_CYCLE (ps);
  int ii = ps->ii;

  /* Don't include the closing branch assuming that it is the last node.  */
  for (i = 0; i < g->num_nodes - 1; i++)
    {
      ddg_node_ptr u = &g->nodes[i];
      int normalized_time = SCHED_TIME (u) - amount;

      gcc_assert (normalized_time >= 0);

      SCHED_TIME (u) = normalized_time;
      SCHED_ROW (u) = normalized_time % ii;
      SCHED_STAGE (u) = normalized_time / ii;
    }
}

/* Set SCHED_COLUMN of each node according to its position in PS.  */
static void
set_columns_for_ps (partial_schedule_ptr ps)
{
  int row;

  for (row = 0; row < ps->ii; row++)
    {
      ps_insn_ptr cur_insn = ps->rows[row];
      int column = 0;

      for (; cur_insn; cur_insn = cur_insn->next_in_row)
	SCHED_COLUMN (cur_insn->node) = column++;
    }
}

/* Permute the insns according to their order in PS, from row 0 to
   row ii-1, and position them right before LAST.  This schedules
   the insns of the loop kernel.  */
static void
permute_partial_schedule (partial_schedule_ptr ps, rtx last)
{
  int ii = ps->ii;
  int row;
  ps_insn_ptr ps_ij;

  for (row = 0; row < ii ; row++)
    for (ps_ij = ps->rows[row]; ps_ij; ps_ij = ps_ij->next_in_row)
      if (PREV_INSN (last) != ps_ij->node->insn)
      	reorder_insns_nobb (ps_ij->node->first_note, ps_ij->node->insn,
			    PREV_INSN (last));
}

/* As part of undoing SMS we return to the original ordering of the
   instructions inside the loop kernel.  Given the partial schedule PS, this
   function returns the ordering of the instruction according to their CUID
   in the DDG (PS->G), which is the original order of the instruction before
   performing SMS.  */
static void
undo_permute_partial_schedule (partial_schedule_ptr ps, rtx last)
{
  int i;

  for (i = 0 ; i < ps->g->num_nodes; i++)
    if (last == ps->g->nodes[i].insn
	|| last == ps->g->nodes[i].first_note)
      break;
    else if (PREV_INSN (last) != ps->g->nodes[i].insn)
      reorder_insns_nobb (ps->g->nodes[i].first_note, ps->g->nodes[i].insn,
			  PREV_INSN (last));
}

/* Used to generate the prologue & epilogue.  Duplicate the subset of
   nodes whose stages are between FROM_STAGE and TO_STAGE (inclusive
   of both), together with a prefix/suffix of their reg_moves.  */
static void
duplicate_insns_of_cycles (partial_schedule_ptr ps, int from_stage,
			   int to_stage, int for_prolog)
{
  int row;
  ps_insn_ptr ps_ij;

  for (row = 0; row < ps->ii; row++)
    for (ps_ij = ps->rows[row]; ps_ij; ps_ij = ps_ij->next_in_row)
      {
	ddg_node_ptr u_node = ps_ij->node;
	int j, i_reg_moves;
	rtx reg_move = NULL_RTX;

	if (for_prolog)
	  {
	    /* SCHED_STAGE (u_node) >= from_stage == 0.  Generate increasing
	       number of reg_moves starting with the second occurrence of
	       u_node, which is generated if its SCHED_STAGE <= to_stage.  */
	    i_reg_moves = to_stage - SCHED_STAGE (u_node) + 1;
	    i_reg_moves = MAX (i_reg_moves, 0);
	    i_reg_moves = MIN (i_reg_moves, SCHED_NREG_MOVES (u_node));

	    /* The reg_moves start from the *first* reg_move backwards.  */
	    if (i_reg_moves)
	      {
		reg_move = SCHED_FIRST_REG_MOVE (u_node);
		for (j = 1; j < i_reg_moves; j++)
		  reg_move = PREV_INSN (reg_move);
	      }
	  }
	else /* It's for the epilog.  */
	  {
	    /* SCHED_STAGE (u_node) <= to_stage.  Generate all reg_moves,
	       starting to decrease one stage after u_node no longer occurs;
	       that is, generate all reg_moves until
	       SCHED_STAGE (u_node) == from_stage - 1.  */
	    i_reg_moves = SCHED_NREG_MOVES (u_node)
	    	       - (from_stage - SCHED_STAGE (u_node) - 1);
	    i_reg_moves = MAX (i_reg_moves, 0);
	    i_reg_moves = MIN (i_reg_moves, SCHED_NREG_MOVES (u_node));

	    /* The reg_moves start from the *last* reg_move forwards.  */
	    if (i_reg_moves)
	      {
		reg_move = SCHED_FIRST_REG_MOVE (u_node);
		for (j = 1; j < SCHED_NREG_MOVES (u_node); j++)
		  reg_move = PREV_INSN (reg_move);
	      }
	  }

	for (j = 0; j < i_reg_moves; j++, reg_move = NEXT_INSN (reg_move))
	  emit_insn (copy_rtx (PATTERN (reg_move)));
	if (SCHED_STAGE (u_node) >= from_stage
	    && SCHED_STAGE (u_node) <= to_stage)
	  duplicate_insn_chain (u_node->first_note, u_node->insn);
      }
}


/* Generate the instructions (including reg_moves) for prolog & epilog.  */
static void
generate_prolog_epilog (partial_schedule_ptr ps, struct loop * loop, rtx count_reg)
{
  int i;
  int last_stage = PS_STAGE_COUNT (ps) - 1;
  edge e;

  /* Generate the prolog, inserting its insns on the loop-entry edge.  */
  start_sequence ();

  if (count_reg)
   /* Generate a subtract instruction at the beginning of the prolog to
      adjust the loop count by STAGE_COUNT.  */
   emit_insn (gen_sub2_insn (count_reg, GEN_INT (last_stage)));

  for (i = 0; i < last_stage; i++)
    duplicate_insns_of_cycles (ps, 0, i, 1);

  /* Put the prolog ,  on the one and only entry edge.  */
  e = loop_preheader_edge (loop);
  loop_split_edge_with(e , get_insns());

  end_sequence ();

  /* Generate the epilog, inserting its insns on the loop-exit edge.  */
  start_sequence ();

  for (i = 0; i < last_stage; i++)
    duplicate_insns_of_cycles (ps, i + 1, last_stage, 0);

  /* Put the epilogue on the one and only one exit edge.  */
  gcc_assert (loop->single_exit);
  e = loop->single_exit;
  loop_split_edge_with(e , get_insns());
  end_sequence ();
}

/* Return the line note insn preceding INSN, for debugging.  Taken from
   emit-rtl.c.  */
static rtx
find_line_note (rtx insn)
{
  for (; insn; insn = PREV_INSN (insn))
    if (NOTE_P (insn)
	&& NOTE_LINE_NUMBER (insn) >= 0)
      break;

  return insn;
}

/* Return true if all the BBs of the loop are empty except the
   loop header.  */
static bool
loop_single_full_bb_p (struct loop *loop)
{
  unsigned i;
  basic_block *bbs = get_loop_body (loop);

  for (i = 0; i < loop->num_nodes ; i++)
    {
      rtx head, tail;
      bool empty_bb = true;

      if (bbs[i] == loop->header)
        continue;

      /* Make sure that basic blocks other than the header
         have only notes labels or jumps.  */
      get_ebb_head_tail (bbs[i], bbs[i], &head, &tail);
      for (; head != NEXT_INSN (tail); head = NEXT_INSN (head))
        {
          if (NOTE_P (head) || LABEL_P (head)
 	      || (INSN_P (head) && JUMP_P (head)))
 	    continue;
 	  empty_bb = false;
 	  break;
        }

      if (! empty_bb)
        {
          free (bbs);
          return false;
        }
    }
  free (bbs);
  return true;
}

/* A simple loop from SMS point of view; it is a loop that is composed of
   either a single basic block or two BBs - a header and a latch.  */
#define SIMPLE_SMS_LOOP_P(loop) ((loop->num_nodes < 3 ) 		    \
				  && (EDGE_COUNT (loop->latch->preds) == 1) \
                                  && (EDGE_COUNT (loop->latch->succs) == 1))

/* Return true if the loop is in its canonical form and false if not.
   i.e. SIMPLE_SMS_LOOP_P and have one preheader block, and single exit.  */
static bool
loop_canon_p (struct loop *loop)
{

  if (loop->inner || ! loop->outer)
    return false;

  if (!loop->single_exit)
    {
      if (dump_file)
	{
	  rtx line_note = find_line_note (BB_END (loop->header));

	  fprintf (dump_file, "SMS loop many exits ");
	  if (line_note)
	    {
	      expanded_location xloc;
	      NOTE_EXPANDED_LOCATION (xloc, line_note);
	      fprintf (dump_file, " %s %d (file, line)\n",
		       xloc.file, xloc.line);
	    }
	}
      return false;
    }

  if (! SIMPLE_SMS_LOOP_P (loop) && ! loop_single_full_bb_p (loop))
    {
      if (dump_file)
	{
	  rtx line_note = find_line_note (BB_END (loop->header));

	  fprintf (dump_file, "SMS loop many BBs. ");
	  if (line_note)
	    {
	      expanded_location xloc;
  	      NOTE_EXPANDED_LOCATION (xloc, line_note);
	      fprintf (dump_file, " %s %d (file, line)\n",
		       xloc.file, xloc.line);
	    }
	}
      return false;
    }

    return true;
}

/* If there are more than one entry for the loop,
   make it one by splitting the first entry edge and
   redirecting the others to the new BB.  */
static void
canon_loop (struct loop *loop)
{
  edge e;
  edge_iterator i;

  /* Avoid annoying special cases of edges going to exit
     block.  */
  FOR_EACH_EDGE (e, i, EXIT_BLOCK_PTR->preds)
    if ((e->flags & EDGE_FALLTHRU) && (EDGE_COUNT (e->src->succs) > 1))
      loop_split_edge_with (e, NULL_RTX);

  if (loop->latch == loop->header
      || EDGE_COUNT (loop->latch->succs) > 1)
    {
      FOR_EACH_EDGE (e, i, loop->header->preds)
        if (e->src == loop->latch)
          break;
      loop_split_edge_with (e, NULL_RTX);
    }
}

/* Main entry point, perform SMS scheduling on the loops of the function
   that consist of single basic blocks.  */
static void
sms_schedule (void)
{
  static int passes = 0;
  rtx insn;
  ddg_ptr *g_arr, g;
  int * node_order;
  int maxii;
  unsigned i,num_loops;
  partial_schedule_ptr ps;
  struct df *df;
  struct loops *loops;
  basic_block bb = NULL;
  /* vars to the versioning only if needed*/
  struct loop * nloop;
  basic_block condition_bb = NULL;
  edge latch_edge;
  gcov_type trip_count = 0;

  loops = loop_optimizer_init (LOOPS_HAVE_PREHEADERS
			       | LOOPS_HAVE_MARKED_SINGLE_EXITS);
  if (!loops)
    return;  /* There is no loops to schedule.  */

  /* Initialize issue_rate.  */
  if (targetm.sched.issue_rate)
    {
      int temp = reload_completed;

      reload_completed = 1;
      issue_rate = targetm.sched.issue_rate ();
      reload_completed = temp;
    }
  else
    issue_rate = 1;

  /* Initialize the scheduler.  */
  current_sched_info = &sms_sched_info;
  sched_init ();

  /* Init Data Flow analysis, to be used in interloop dep calculation.  */
  df = df_init (DF_HARD_REGS | DF_EQUIV_NOTES | DF_SUBREGS);
  df_rd_add_problem (df, 0);
  df_ru_add_problem (df, 0);
  df_chain_add_problem (df, DF_DU_CHAIN | DF_UD_CHAIN);
  df_analyze (df);

  if (dump_file)
    df_dump (df, dump_file);

  /* Allocate memory to hold the DDG array one entry for each loop.
     We use loop->num as index into this array.  */
  g_arr = XCNEWVEC (ddg_ptr, loops->num);


  /* Build DDGs for all the relevant loops and hold them in G_ARR
     indexed by the loop index.  */
  for (i = 0; i < loops->num; i++)
    {
      rtx head, tail;
      rtx count_reg;
      struct loop *loop = loops->parray[i];

      /* For debugging.  */
      if ((passes++ > MAX_SMS_LOOP_NUMBER) && (MAX_SMS_LOOP_NUMBER != -1))
        {
          if (dump_file)
            fprintf (dump_file, "SMS reached MAX_PASSES... \n");

          break;
        }

      if (! loop_canon_p (loop))
        continue;

      if (! loop_single_full_bb_p (loop))
	continue;

      bb = loop->header;

      get_ebb_head_tail (bb, bb, &head, &tail);
      latch_edge = loop_latch_edge (loop);
      gcc_assert (loop->single_exit);
      if (loop->single_exit->count)
	trip_count = latch_edge->count / loop->single_exit->count;

      /* Perfrom SMS only on loops that their average count is above threshold.  */

      if ( latch_edge->count
          && (latch_edge->count < loop->single_exit->count * SMS_LOOP_AVERAGE_COUNT_THRESHOLD))
	{
	  if (dump_file)
	    {
	      rtx line_note = find_line_note (tail);

	      if (line_note)
		{
		  expanded_location xloc;
		  NOTE_EXPANDED_LOCATION (xloc, line_note);
		  fprintf (dump_file, "SMS bb %s %d (file, line)\n",
			   xloc.file, xloc.line);
		}
	      fprintf (dump_file, "SMS single-bb-loop\n");
	      if (profile_info && flag_branch_probabilities)
	    	{
	      	  fprintf (dump_file, "SMS loop-count ");
	      	  fprintf (dump_file, HOST_WIDEST_INT_PRINT_DEC,
	             	   (HOST_WIDEST_INT) bb->count);
	      	  fprintf (dump_file, "\n");
                  fprintf (dump_file, "SMS trip-count ");
                  fprintf (dump_file, HOST_WIDEST_INT_PRINT_DEC,
                           (HOST_WIDEST_INT) trip_count);
                  fprintf (dump_file, "\n");
	      	  fprintf (dump_file, "SMS profile-sum-max ");
	      	  fprintf (dump_file, HOST_WIDEST_INT_PRINT_DEC,
	          	   (HOST_WIDEST_INT) profile_info->sum_max);
	      	  fprintf (dump_file, "\n");
	    	}
	    }
          continue;
        }

      /* Make sure this is a doloop.  */
      if ( !(count_reg = doloop_register_get (tail)))
	continue;

      /* Don't handle BBs with calls or barriers, or !single_set insns.  */
      for (insn = head; insn != NEXT_INSN (tail); insn = NEXT_INSN (insn))
	if (CALL_P (insn)
	    || BARRIER_P (insn)
	    || (INSN_P (insn) && !JUMP_P (insn)
		&& !single_set (insn) && GET_CODE (PATTERN (insn)) != USE))
	  break;

      if (insn != NEXT_INSN (tail))
	{
	  if (dump_file)
	    {
	      if (CALL_P (insn))
		fprintf (dump_file, "SMS loop-with-call\n");
	      else if (BARRIER_P (insn))
		fprintf (dump_file, "SMS loop-with-barrier\n");
	      else
		fprintf (dump_file, "SMS loop-with-not-single-set\n");
	      print_rtl_single (dump_file, insn);
	    }

	  continue;
	}

      if (! (g = create_ddg (bb, df, 0)))
        {
          if (dump_file)
	    fprintf (dump_file, "SMS doloop\n");
	  continue;
        }

      g_arr[i] = g;
    }

  /* Release Data Flow analysis data structures.  */
  df_finish (df);
  df = NULL;

  /* We don't want to perform SMS on new loops - created by versioning.  */
  num_loops = loops->num;
  /* Go over the built DDGs and perfrom SMS for each one of them.  */
  for (i = 0; i < num_loops; i++)
    {
      rtx head, tail;
      rtx count_reg, count_init;
      int mii, rec_mii;
      unsigned stage_count = 0;
      HOST_WIDEST_INT loop_count = 0;
      struct loop *loop = loops->parray[i];

      if (! (g = g_arr[i]))
        continue;

      if (dump_file)
	print_ddg (dump_file, g);

      get_ebb_head_tail (loop->header, loop->header, &head, &tail);

      latch_edge = loop_latch_edge (loop);
      gcc_assert (loop->single_exit);
      if (loop->single_exit->count)
	trip_count = latch_edge->count / loop->single_exit->count;

      if (dump_file)
	{
	  rtx line_note = find_line_note (tail);

	  if (line_note)
	    {
	      expanded_location xloc;
	      NOTE_EXPANDED_LOCATION (xloc, line_note);
	      fprintf (dump_file, "SMS bb %s %d (file, line)\n",
		       xloc.file, xloc.line);
	    }
	  fprintf (dump_file, "SMS single-bb-loop\n");
	  if (profile_info && flag_branch_probabilities)
	    {
	      fprintf (dump_file, "SMS loop-count ");
	      fprintf (dump_file, HOST_WIDEST_INT_PRINT_DEC,
	               (HOST_WIDEST_INT) bb->count);
	      fprintf (dump_file, "\n");
	      fprintf (dump_file, "SMS profile-sum-max ");
	      fprintf (dump_file, HOST_WIDEST_INT_PRINT_DEC,
	               (HOST_WIDEST_INT) profile_info->sum_max);
	      fprintf (dump_file, "\n");
	    }
	  fprintf (dump_file, "SMS doloop\n");
	  fprintf (dump_file, "SMS built-ddg %d\n", g->num_nodes);
          fprintf (dump_file, "SMS num-loads %d\n", g->num_loads);
          fprintf (dump_file, "SMS num-stores %d\n", g->num_stores);
	}


      /* In case of th loop have doloop register it gets special
	 handling.  */
      count_init = NULL_RTX;
      if ((count_reg = doloop_register_get (tail)))
	{
	  basic_block pre_header;

	  pre_header = loop_preheader_edge (loop)->src;
	  count_init = const_iteration_count (count_reg, pre_header,
					      &loop_count);
	}
      gcc_assert (count_reg);

      if (dump_file && count_init)
        {
          fprintf (dump_file, "SMS const-doloop ");
          fprintf (dump_file, HOST_WIDEST_INT_PRINT_DEC,
		     loop_count);
          fprintf (dump_file, "\n");
        }

      node_order = XNEWVEC (int, g->num_nodes);

      mii = 1; /* Need to pass some estimate of mii.  */
      rec_mii = sms_order_nodes (g, mii, node_order);
      mii = MAX (res_MII (g), rec_mii);
      maxii = (calculate_maxii (g) * SMS_MAX_II_FACTOR) / 100;

      if (dump_file)
	fprintf (dump_file, "SMS iis %d %d %d (rec_mii, mii, maxii)\n",
		 rec_mii, mii, maxii);

      /* After sms_order_nodes and before sms_schedule_by_order, to copy over
	 ASAP.  */
      set_node_sched_params (g);

      ps = sms_schedule_by_order (g, mii, maxii, node_order);

      if (ps)
	stage_count = PS_STAGE_COUNT (ps);

      /* Stage count of 1 means that there is no interleaving between
         iterations, let the scheduling passes do the job.  */
      if (stage_count < 1
	  || (count_init && (loop_count <= stage_count))
	  || (flag_branch_probabilities && (trip_count <= stage_count)))
	{
	  if (dump_file)
	    {
	      fprintf (dump_file, "SMS failed... \n");
	      fprintf (dump_file, "SMS sched-failed (stage-count=%d, loop-count=", stage_count);
	      fprintf (dump_file, HOST_WIDEST_INT_PRINT_DEC, loop_count);
	      fprintf (dump_file, ", trip-count=");
	      fprintf (dump_file, HOST_WIDEST_INT_PRINT_DEC, trip_count);
	      fprintf (dump_file, ")\n");
	    }
	  continue;
	}
      else
	{
	  int orig_cycles = kernel_number_of_cycles (BB_HEAD (g->bb), BB_END (g->bb));
	  int new_cycles;
	  struct undo_replace_buff_elem *reg_move_replaces;

	  if (dump_file)
	    {
	      fprintf (dump_file,
		       "SMS succeeded %d %d (with ii, sc)\n", ps->ii,
		       stage_count);
	      print_partial_schedule (ps, dump_file);
	      fprintf (dump_file,
		       "SMS Branch (%d) will later be scheduled at cycle %d.\n",
		       g->closing_branch->cuid, PS_MIN_CYCLE (ps) - 1);
	    }

	  /* Set the stage boundaries.  If the DDG is built with closing_branch_deps,
	     the closing_branch was scheduled and should appear in the last (ii-1)
	     row.  Otherwise, we are free to schedule the branch, and we let nodes
	     that were scheduled at the first PS_MIN_CYCLE cycle appear in the first
	     row; this should reduce stage_count to minimum.  */
	  normalize_sched_times (ps);
	  rotate_partial_schedule (ps, PS_MIN_CYCLE (ps));
	  set_columns_for_ps (ps);

	  /* Generate the kernel just to be able to measure its cycles.  */
	  permute_partial_schedule (ps, g->closing_branch->first_note);
	  reg_move_replaces = generate_reg_moves (ps);

	  /* Get the number of cycles the new kernel expect to execute in.  */
	  new_cycles = kernel_number_of_cycles (BB_HEAD (g->bb), BB_END (g->bb));

	  /* Get back to the original loop so we can do loop versioning.  */
	  undo_permute_partial_schedule (ps, g->closing_branch->first_note);
	  if (reg_move_replaces)
	    undo_generate_reg_moves (ps, reg_move_replaces);

	  if ( new_cycles >= orig_cycles)
	    {
	      /* SMS is not profitable so undo the permutation and reg move generation
	         and return the kernel to its original state.  */
	      if (dump_file)
		fprintf (dump_file, "Undoing SMS because it is not profitable.\n");

	    }
	  else
	    {
	      canon_loop (loop);

              /* case the BCT count is not known , Do loop-versioning */
	      if (count_reg && ! count_init)
		{
		  rtx comp_rtx = gen_rtx_fmt_ee (GT, VOIDmode, count_reg,
						 GEN_INT(stage_count));

		  nloop = loop_version (loops, loop, comp_rtx, &condition_bb,
					true);
		}

	      /* Set new iteration count of loop kernel.  */
              if (count_reg && count_init)
		SET_SRC (single_set (count_init)) = GEN_INT (loop_count
							     - stage_count + 1);

	      /* Now apply the scheduled kernel to the RTL of the loop.  */
	      permute_partial_schedule (ps, g->closing_branch->first_note);

              /* Mark this loop as software pipelined so the later
	      scheduling passes doesn't touch it.  */
	      if (! flag_resched_modulo_sched)
		g->bb->flags |= BB_DISABLE_SCHEDULE;
	      /* The life-info is not valid any more.  */
	      g->bb->flags |= BB_DIRTY;

	      reg_move_replaces = generate_reg_moves (ps);
	      if (dump_file)
		print_node_sched_params (dump_file, g->num_nodes);
	      /* Generate prolog and epilog.  */
	      if (count_reg && !count_init)
		generate_prolog_epilog (ps, loop, count_reg);
	      else
	 	generate_prolog_epilog (ps, loop, NULL_RTX);
	    }
	  free_undo_replace_buff (reg_move_replaces);
	}

      free_partial_schedule (ps);
      free (node_sched_params);
      free (node_order);
      free_ddg (g);
    }

  free (g_arr);

  /* Release scheduler data, needed until now because of DFA.  */
  sched_finish ();
  loop_optimizer_finalize (loops);
}

/* The SMS scheduling algorithm itself
   -----------------------------------
   Input: 'O' an ordered list of insns of a loop.
   Output: A scheduling of the loop - kernel, prolog, and epilogue.

   'Q' is the empty Set
   'PS' is the partial schedule; it holds the currently scheduled nodes with
	their cycle/slot.
   'PSP' previously scheduled predecessors.
   'PSS' previously scheduled successors.
   't(u)' the cycle where u is scheduled.
   'l(u)' is the latency of u.
   'd(v,u)' is the dependence distance from v to u.
   'ASAP(u)' the earliest time at which u could be scheduled as computed in
	     the node ordering phase.
   'check_hardware_resources_conflicts(u, PS, c)'
			     run a trace around cycle/slot through DFA model
			     to check resource conflicts involving instruction u
			     at cycle c given the partial schedule PS.
   'add_to_partial_schedule_at_time(u, PS, c)'
			     Add the node/instruction u to the partial schedule
			     PS at time c.
   'calculate_register_pressure(PS)'
			     Given a schedule of instructions, calculate the register
			     pressure it implies.  One implementation could be the
			     maximum number of overlapping live ranges.
   'maxRP' The maximum allowed register pressure, it is usually derived from the number
	   registers available in the hardware.

   1. II = MII.
   2. PS = empty list
   3. for each node u in O in pre-computed order
   4.   if (PSP(u) != Q && PSS(u) == Q) then
   5.     Early_start(u) = max ( t(v) + l(v) - d(v,u)*II ) over all every v in PSP(u).
   6.     start = Early_start; end = Early_start + II - 1; step = 1
   11.  else if (PSP(u) == Q && PSS(u) != Q) then
   12.      Late_start(u) = min ( t(v) - l(v) + d(v,u)*II ) over all every v in PSS(u).
   13.     start = Late_start; end = Late_start - II + 1; step = -1
   14.  else if (PSP(u) != Q && PSS(u) != Q) then
   15.     Early_start(u) = max ( t(v) + l(v) - d(v,u)*II ) over all every v in PSP(u).
   16.     Late_start(u) = min ( t(v) - l(v) + d(v,u)*II ) over all every v in PSS(u).
   17.     start = Early_start;
   18.     end = min(Early_start + II - 1 , Late_start);
   19.     step = 1
   20.     else "if (PSP(u) == Q && PSS(u) == Q)"
   21.	  start = ASAP(u); end = start + II - 1; step = 1
   22.  endif

   23.  success = false
   24.  for (c = start ; c != end ; c += step)
   25.     if check_hardware_resources_conflicts(u, PS, c) then
   26.       add_to_partial_schedule_at_time(u, PS, c)
   27.       success = true
   28.       break
   29.     endif
   30.  endfor
   31.  if (success == false) then
   32.    II = II + 1
   33.    if (II > maxII) then
   34.       finish - failed to schedule
   35.	 endif
   36.    goto 2.
   37.  endif
   38. endfor
   39. if (calculate_register_pressure(PS) > maxRP) then
   40.    goto 32.
   41. endif
   42. compute epilogue & prologue
   43. finish - succeeded to schedule
*/

/* A limit on the number of cycles that resource conflicts can span.  ??? Should
   be provided by DFA, and be dependent on the type of insn scheduled.  Currently
   set to 0 to save compile time.  */
#define DFA_HISTORY SMS_DFA_HISTORY

/* Given the partial schedule PS, this function calculates and returns the
   cycles in which we can schedule the node with the given index I.
   NOTE: Here we do the backtracking in SMS, in some special cases. We have
   noticed that there are several cases in which we fail    to SMS the loop
   because the sched window of a node is empty    due to tight data-deps. In
   such cases we want to unschedule    some of the predecessors/successors
   until we get non-empty    scheduling window.  It returns -1 if the
   scheduling window is empty and zero otherwise.  */

static int
get_sched_window (partial_schedule_ptr ps, int *nodes_order, int i,
		  sbitmap sched_nodes, int ii, int *start_p, int *step_p, int *end_p)
{
  int start, step, end;
  ddg_edge_ptr e;
  int u = nodes_order [i];
  ddg_node_ptr u_node = &ps->g->nodes[u];
  sbitmap psp = sbitmap_alloc (ps->g->num_nodes);
  sbitmap pss = sbitmap_alloc (ps->g->num_nodes);
  sbitmap u_node_preds = NODE_PREDECESSORS (u_node);
  sbitmap u_node_succs = NODE_SUCCESSORS (u_node);
  int psp_not_empty;
  int pss_not_empty;

  /* 1. compute sched window for u (start, end, step).  */
  sbitmap_zero (psp);
  sbitmap_zero (pss);
  psp_not_empty = sbitmap_a_and_b_cg (psp, u_node_preds, sched_nodes);
  pss_not_empty = sbitmap_a_and_b_cg (pss, u_node_succs, sched_nodes);

  if (psp_not_empty && !pss_not_empty)
    {
      int early_start = INT_MIN;

      end = INT_MAX;
      for (e = u_node->in; e != 0; e = e->next_in)
	{
	  ddg_node_ptr v_node = e->src;
	  if (TEST_BIT (sched_nodes, v_node->cuid))
	    {
	      int node_st = SCHED_TIME (v_node)
	      		    + e->latency - (e->distance * ii);

	      early_start = MAX (early_start, node_st);

	      if (e->data_type == MEM_DEP)
		end = MIN (end, SCHED_TIME (v_node) + ii - 1);
	    }
	}
      start = early_start;
      end = MIN (end, early_start + ii);
      step = 1;
    }

  else if (!psp_not_empty && pss_not_empty)
    {
      int late_start = INT_MAX;

      end = INT_MIN;
      for (e = u_node->out; e != 0; e = e->next_out)
	{
	  ddg_node_ptr v_node = e->dest;
	  if (TEST_BIT (sched_nodes, v_node->cuid))
	    {
	      late_start = MIN (late_start,
				SCHED_TIME (v_node) - e->latency
				+ (e->distance * ii));
	      if (e->data_type == MEM_DEP)
		end = MAX (end, SCHED_TIME (v_node) - ii + 1);
	    }
	}
      start = late_start;
      end = MAX (end, late_start - ii);
      step = -1;
    }

  else if (psp_not_empty && pss_not_empty)
    {
      int early_start = INT_MIN;
      int late_start = INT_MAX;

      start = INT_MIN;
      end = INT_MAX;
      for (e = u_node->in; e != 0; e = e->next_in)
	{
	  ddg_node_ptr v_node = e->src;

	  if (TEST_BIT (sched_nodes, v_node->cuid))
	    {
	      early_start = MAX (early_start,
				 SCHED_TIME (v_node) + e->latency
				 - (e->distance * ii));
	      if (e->data_type == MEM_DEP)
		end = MIN (end, SCHED_TIME (v_node) + ii - 1);
	    }
	}
      for (e = u_node->out; e != 0; e = e->next_out)
	{
	  ddg_node_ptr v_node = e->dest;

	  if (TEST_BIT (sched_nodes, v_node->cuid))
	    {
	      late_start = MIN (late_start,
				SCHED_TIME (v_node) - e->latency
				+ (e->distance * ii));
	      if (e->data_type == MEM_DEP)
		start = MAX (start, SCHED_TIME (v_node) - ii + 1);
	    }
	}
      start = MAX (start, early_start);
      end = MIN (end, MIN (early_start + ii, late_start + 1));
      step = 1;
    }
  else /* psp is empty && pss is empty.  */
    {
      start = SCHED_ASAP (u_node);
      end = start + ii;
      step = 1;
    }

  *start_p = start;
  *step_p = step;
  *end_p = end;
  sbitmap_free (psp);
  sbitmap_free (pss);

  if ((start >= end && step == 1) || (start <= end && step == -1))
    return -1;
  else
    return 0;
}

/* This function implements the scheduling algorithm for SMS according to the
   above algorithm.  */
static partial_schedule_ptr
sms_schedule_by_order (ddg_ptr g, int mii, int maxii, int *nodes_order)
{
  int ii = mii;
  int i, c, success;
  int try_again_with_larger_ii = true;
  int num_nodes = g->num_nodes;
  ddg_edge_ptr e;
  int start, end, step; /* Place together into one struct?  */
  sbitmap sched_nodes = sbitmap_alloc (num_nodes);
  sbitmap must_precede = sbitmap_alloc (num_nodes);
  sbitmap must_follow = sbitmap_alloc (num_nodes);
  sbitmap tobe_scheduled = sbitmap_alloc (num_nodes);

  partial_schedule_ptr ps = create_partial_schedule (ii, g, DFA_HISTORY);

  sbitmap_ones (tobe_scheduled);
  sbitmap_zero (sched_nodes);

  while ((! sbitmap_equal (tobe_scheduled, sched_nodes)
	 || try_again_with_larger_ii ) && ii < maxii)
    {
      int j;
      bool unscheduled_nodes = false;

      if (dump_file)
	fprintf(dump_file, "Starting with ii=%d\n", ii);
      if (try_again_with_larger_ii)
	{
	  try_again_with_larger_ii = false;
	  sbitmap_zero (sched_nodes);
	}

      for (i = 0; i < num_nodes; i++)
	{
	  int u = nodes_order[i];
  	  ddg_node_ptr u_node = &ps->g->nodes[u];
	  rtx insn = u_node->insn;

	  if (!INSN_P (insn))
	    {
	      RESET_BIT (tobe_scheduled, u);
	      continue;
	    }

	  if (JUMP_P (insn)) /* Closing branch handled later.  */
	    {
	      RESET_BIT (tobe_scheduled, u);
	      continue;
	    }

	  if (TEST_BIT (sched_nodes, u))
	    continue;

	  /* Try to get non-empty scheduling window.  */
	  j = i;
	  while (get_sched_window (ps, nodes_order, i, sched_nodes, ii, &start, &step, &end) < 0
		 && j > 0)
	    {
	      unscheduled_nodes = true;
	      if (TEST_BIT (NODE_PREDECESSORS (u_node), nodes_order[j - 1])
		  || TEST_BIT (NODE_SUCCESSORS (u_node), nodes_order[j - 1]))
		{
		  ps_unschedule_node (ps, &ps->g->nodes[nodes_order[j - 1]]);
		  RESET_BIT (sched_nodes, nodes_order [j - 1]);
		}
	      j--;
	    }
	  if (j < 0)
	    {
	      /* ??? Try backtracking instead of immediately ii++?  */
	      ii++;
	      try_again_with_larger_ii = true;
	      reset_partial_schedule (ps, ii);
	      break;
	    }
	  /* 2. Try scheduling u in window.  */
	  if (dump_file)
	    fprintf(dump_file, "Trying to schedule node %d in (%d .. %d) step %d\n",
		    u, start, end, step);

          /* use must_follow & must_precede bitmaps to determine order
	     of nodes within the cycle.  */
          sbitmap_zero (must_precede);
          sbitmap_zero (must_follow);
      	  for (e = u_node->in; e != 0; e = e->next_in)
            if (TEST_BIT (sched_nodes, e->src->cuid)
	        && e->latency == (ii * e->distance)
		&& start == SCHED_TIME (e->src))
             SET_BIT (must_precede, e->src->cuid);

	  for (e = u_node->out; e != 0; e = e->next_out)
            if (TEST_BIT (sched_nodes, e->dest->cuid)
	        && e->latency == (ii * e->distance)
		&& end == SCHED_TIME (e->dest))
             SET_BIT (must_follow, e->dest->cuid);

	  success = 0;
	  if ((step > 0 && start < end) ||  (step < 0 && start > end))
	    for (c = start; c != end; c += step)
	      {
		ps_insn_ptr psi;

		psi = ps_add_node_check_conflicts (ps, u_node, c,
						   must_precede,
						   must_follow);

  		if (psi)
		  {
		    SCHED_TIME (u_node) = c;
		    SET_BIT (sched_nodes, u);
		    success = 1;
		    if (dump_file)
		      fprintf(dump_file, "Schedule in %d\n", c);
		    break;
		  }
	      }
	  if (!success)
	    {
	      /* ??? Try backtracking instead of immediately ii++?  */
	      ii++;
	      try_again_with_larger_ii = true;
	      reset_partial_schedule (ps, ii);
	      break;
	    }
	  if (unscheduled_nodes)
	    break;

	  /* ??? If (success), check register pressure estimates.  */
	} /* Continue with next node.  */
    } /* While try_again_with_larger_ii.  */

  sbitmap_free (sched_nodes);
  sbitmap_free (must_precede);
  sbitmap_free (must_follow);
  sbitmap_free (tobe_scheduled);

  if (ii >= maxii)
    {
      free_partial_schedule (ps);
      ps = NULL;
    }
  return ps;
}


/* This page implements the algorithm for ordering the nodes of a DDG
   for modulo scheduling, activated through the
   "int sms_order_nodes (ddg_ptr, int mii, int * result)" API.  */

#define ORDER_PARAMS(x) ((struct node_order_params *) (x)->aux.info)
#define ASAP(x) (ORDER_PARAMS ((x))->asap)
#define ALAP(x) (ORDER_PARAMS ((x))->alap)
#define HEIGHT(x) (ORDER_PARAMS ((x))->height)
#define MOB(x) (ALAP ((x)) - ASAP ((x)))
#define DEPTH(x) (ASAP ((x)))

typedef struct node_order_params * nopa;

static void order_nodes_of_sccs (ddg_all_sccs_ptr, int * result);
static int order_nodes_in_scc (ddg_ptr, sbitmap, sbitmap, int*, int);
static nopa  calculate_order_params (ddg_ptr, int mii);
static int find_max_asap (ddg_ptr, sbitmap);
static int find_max_hv_min_mob (ddg_ptr, sbitmap);
static int find_max_dv_min_mob (ddg_ptr, sbitmap);

enum sms_direction {BOTTOMUP, TOPDOWN};

struct node_order_params
{
  int asap;
  int alap;
  int height;
};

/* Check if NODE_ORDER contains a permutation of 0 .. NUM_NODES-1.  */
static void
check_nodes_order (int *node_order, int num_nodes)
{
  int i;
  sbitmap tmp = sbitmap_alloc (num_nodes);

  sbitmap_zero (tmp);

  for (i = 0; i < num_nodes; i++)
    {
      int u = node_order[i];

      gcc_assert (u < num_nodes && u >= 0 && !TEST_BIT (tmp, u));

      SET_BIT (tmp, u);
    }

  sbitmap_free (tmp);
}

/* Order the nodes of G for scheduling and pass the result in
   NODE_ORDER.  Also set aux.count of each node to ASAP.
   Return the recMII for the given DDG.  */
static int
sms_order_nodes (ddg_ptr g, int mii, int * node_order)
{
  int i;
  int rec_mii = 0;
  ddg_all_sccs_ptr sccs = create_ddg_all_sccs (g);

  nopa nops = calculate_order_params (g, mii);

  order_nodes_of_sccs (sccs, node_order);

  if (sccs->num_sccs > 0)
    /* First SCC has the largest recurrence_length.  */
    rec_mii = sccs->sccs[0]->recurrence_length;

  /* Save ASAP before destroying node_order_params.  */
  for (i = 0; i < g->num_nodes; i++)
    {
      ddg_node_ptr v = &g->nodes[i];
      v->aux.count = ASAP (v);
    }

  free (nops);
  free_ddg_all_sccs (sccs);
  check_nodes_order (node_order, g->num_nodes);

  return rec_mii;
}

static void
order_nodes_of_sccs (ddg_all_sccs_ptr all_sccs, int * node_order)
{
  int i, pos = 0;
  ddg_ptr g = all_sccs->ddg;
  int num_nodes = g->num_nodes;
  sbitmap prev_sccs = sbitmap_alloc (num_nodes);
  sbitmap on_path = sbitmap_alloc (num_nodes);
  sbitmap tmp = sbitmap_alloc (num_nodes);
  sbitmap ones = sbitmap_alloc (num_nodes);

  sbitmap_zero (prev_sccs);
  sbitmap_ones (ones);

  /* Perfrom the node ordering starting from the SCC with the highest recMII.
     For each SCC order the nodes according to their ASAP/ALAP/HEIGHT etc.  */
  for (i = 0; i < all_sccs->num_sccs; i++)
    {
      ddg_scc_ptr scc = all_sccs->sccs[i];

      /* Add nodes on paths from previous SCCs to the current SCC.  */
      find_nodes_on_paths (on_path, g, prev_sccs, scc->nodes);
      sbitmap_a_or_b (tmp, scc->nodes, on_path);

      /* Add nodes on paths from the current SCC to previous SCCs.  */
      find_nodes_on_paths (on_path, g, scc->nodes, prev_sccs);
      sbitmap_a_or_b (tmp, tmp, on_path);

      /* Remove nodes of previous SCCs from current extended SCC.  */
      sbitmap_difference (tmp, tmp, prev_sccs);

      pos = order_nodes_in_scc (g, prev_sccs, tmp, node_order, pos);
      /* Above call to order_nodes_in_scc updated prev_sccs |= tmp.  */
    }

  /* Handle the remaining nodes that do not belong to any scc.  Each call
     to order_nodes_in_scc handles a single connected component.  */
  while (pos < g->num_nodes)
    {
      sbitmap_difference (tmp, ones, prev_sccs);
      pos = order_nodes_in_scc (g, prev_sccs, tmp, node_order, pos);
    }
  sbitmap_free (prev_sccs);
  sbitmap_free (on_path);
  sbitmap_free (tmp);
  sbitmap_free (ones);
}

/* MII is needed if we consider backarcs (that do not close recursive cycles).  */
static struct node_order_params *
calculate_order_params (ddg_ptr g, int mii ATTRIBUTE_UNUSED)
{
  int u;
  int max_asap;
  int num_nodes = g->num_nodes;
  ddg_edge_ptr e;
  /* Allocate a place to hold ordering params for each node in the DDG.  */
  nopa node_order_params_arr;

  /* Initialize of ASAP/ALAP/HEIGHT to zero.  */
  node_order_params_arr = (nopa) xcalloc (num_nodes,
					  sizeof (struct node_order_params));

  /* Set the aux pointer of each node to point to its order_params structure.  */
  for (u = 0; u < num_nodes; u++)
    g->nodes[u].aux.info = &node_order_params_arr[u];

  /* Disregarding a backarc from each recursive cycle to obtain a DAG,
     calculate ASAP, ALAP, mobility, distance, and height for each node
     in the dependence (direct acyclic) graph.  */

  /* We assume that the nodes in the array are in topological order.  */

  max_asap = 0;
  for (u = 0; u < num_nodes; u++)
    {
      ddg_node_ptr u_node = &g->nodes[u];

      ASAP (u_node) = 0;
      for (e = u_node->in; e; e = e->next_in)
	if (e->distance == 0)
	  ASAP (u_node) = MAX (ASAP (u_node),
			       ASAP (e->src) + e->latency);
      max_asap = MAX (max_asap, ASAP (u_node));
    }

  for (u = num_nodes - 1; u > -1; u--)
    {
      ddg_node_ptr u_node = &g->nodes[u];

      ALAP (u_node) = max_asap;
      HEIGHT (u_node) = 0;
      for (e = u_node->out; e; e = e->next_out)
	if (e->distance == 0)
	  {
	    ALAP (u_node) = MIN (ALAP (u_node),
				 ALAP (e->dest) - e->latency);
	    HEIGHT (u_node) = MAX (HEIGHT (u_node),
				   HEIGHT (e->dest) + e->latency);
	  }
    }

  return node_order_params_arr;
}

static int
find_max_asap (ddg_ptr g, sbitmap nodes)
{
  unsigned int u = 0;
  int max_asap = -1;
  int result = -1;
  sbitmap_iterator sbi;

  EXECUTE_IF_SET_IN_SBITMAP (nodes, 0, u, sbi)
    {
      ddg_node_ptr u_node = &g->nodes[u];

      if (max_asap < ASAP (u_node))
	{
	  max_asap = ASAP (u_node);
	  result = u;
	}
    }
  return result;
}

static int
find_max_hv_min_mob (ddg_ptr g, sbitmap nodes)
{
  unsigned int u = 0;
  int max_hv = -1;
  int min_mob = INT_MAX;
  int result = -1;
  sbitmap_iterator sbi;

  EXECUTE_IF_SET_IN_SBITMAP (nodes, 0, u, sbi)
    {
      ddg_node_ptr u_node = &g->nodes[u];

      if (max_hv < HEIGHT (u_node))
	{
	  max_hv = HEIGHT (u_node);
	  min_mob = MOB (u_node);
	  result = u;
	}
      else if ((max_hv == HEIGHT (u_node))
	       && (min_mob > MOB (u_node)))
	{
	  min_mob = MOB (u_node);
	  result = u;
	}
    }
  return result;
}

static int
find_max_dv_min_mob (ddg_ptr g, sbitmap nodes)
{
  unsigned int u = 0;
  int max_dv = -1;
  int min_mob = INT_MAX;
  int result = -1;
  sbitmap_iterator sbi;

  EXECUTE_IF_SET_IN_SBITMAP (nodes, 0, u, sbi)
    {
      ddg_node_ptr u_node = &g->nodes[u];

      if (max_dv < DEPTH (u_node))
	{
	  max_dv = DEPTH (u_node);
	  min_mob = MOB (u_node);
	  result = u;
	}
      else if ((max_dv == DEPTH (u_node))
	       && (min_mob > MOB (u_node)))
	{
	  min_mob = MOB (u_node);
	  result = u;
	}
    }
  return result;
}

/* Places the nodes of SCC into the NODE_ORDER array starting
   at position POS, according to the SMS ordering algorithm.
   NODES_ORDERED (in&out parameter) holds the bitset of all nodes in
   the NODE_ORDER array, starting from position zero.  */
static int
order_nodes_in_scc (ddg_ptr g, sbitmap nodes_ordered, sbitmap scc,
		    int * node_order, int pos)
{
  enum sms_direction dir;
  int num_nodes = g->num_nodes;
  sbitmap workset = sbitmap_alloc (num_nodes);
  sbitmap tmp = sbitmap_alloc (num_nodes);
  sbitmap zero_bitmap = sbitmap_alloc (num_nodes);
  sbitmap predecessors = sbitmap_alloc (num_nodes);
  sbitmap successors = sbitmap_alloc (num_nodes);

  sbitmap_zero (predecessors);
  find_predecessors (predecessors, g, nodes_ordered);

  sbitmap_zero (successors);
  find_successors (successors, g, nodes_ordered);

  sbitmap_zero (tmp);
  if (sbitmap_a_and_b_cg (tmp, predecessors, scc))
    {
      sbitmap_copy (workset, tmp);
      dir = BOTTOMUP;
    }
  else if (sbitmap_a_and_b_cg (tmp, successors, scc))
    {
      sbitmap_copy (workset, tmp);
      dir = TOPDOWN;
    }
  else
    {
      int u;

      sbitmap_zero (workset);
      if ((u = find_max_asap (g, scc)) >= 0)
	SET_BIT (workset, u);
      dir = BOTTOMUP;
    }

  sbitmap_zero (zero_bitmap);
  while (!sbitmap_equal (workset, zero_bitmap))
    {
      int v;
      ddg_node_ptr v_node;
      sbitmap v_node_preds;
      sbitmap v_node_succs;

      if (dir == TOPDOWN)
	{
	  while (!sbitmap_equal (workset, zero_bitmap))
	    {
	      v = find_max_hv_min_mob (g, workset);
	      v_node = &g->nodes[v];
	      node_order[pos++] = v;
	      v_node_succs = NODE_SUCCESSORS (v_node);
	      sbitmap_a_and_b (tmp, v_node_succs, scc);

	      /* Don't consider the already ordered successors again.  */
	      sbitmap_difference (tmp, tmp, nodes_ordered);
	      sbitmap_a_or_b (workset, workset, tmp);
	      RESET_BIT (workset, v);
	      SET_BIT (nodes_ordered, v);
	    }
	  dir = BOTTOMUP;
	  sbitmap_zero (predecessors);
	  find_predecessors (predecessors, g, nodes_ordered);
	  sbitmap_a_and_b (workset, predecessors, scc);
	}
      else
	{
	  while (!sbitmap_equal (workset, zero_bitmap))
	    {
	      v = find_max_dv_min_mob (g, workset);
	      v_node = &g->nodes[v];
	      node_order[pos++] = v;
	      v_node_preds = NODE_PREDECESSORS (v_node);
	      sbitmap_a_and_b (tmp, v_node_preds, scc);

	      /* Don't consider the already ordered predecessors again.  */
	      sbitmap_difference (tmp, tmp, nodes_ordered);
	      sbitmap_a_or_b (workset, workset, tmp);
	      RESET_BIT (workset, v);
	      SET_BIT (nodes_ordered, v);
	    }
	  dir = TOPDOWN;
	  sbitmap_zero (successors);
	  find_successors (successors, g, nodes_ordered);
	  sbitmap_a_and_b (workset, successors, scc);
	}
    }
  sbitmap_free (tmp);
  sbitmap_free (workset);
  sbitmap_free (zero_bitmap);
  sbitmap_free (predecessors);
  sbitmap_free (successors);
  return pos;
}


/* This page contains functions for manipulating partial-schedules during
   modulo scheduling.  */

/* Create a partial schedule and allocate a memory to hold II rows.  */

static partial_schedule_ptr
create_partial_schedule (int ii, ddg_ptr g, int history)
{
  partial_schedule_ptr ps = XNEW (struct partial_schedule);
  ps->rows = (ps_insn_ptr *) xcalloc (ii, sizeof (ps_insn_ptr));
  ps->ii = ii;
  ps->history = history;
  ps->min_cycle = INT_MAX;
  ps->max_cycle = INT_MIN;
  ps->g = g;

  return ps;
}

/* Free the PS_INSNs in rows array of the given partial schedule.
   ??? Consider caching the PS_INSN's.  */
static void
free_ps_insns (partial_schedule_ptr ps)
{
  int i;

  for (i = 0; i < ps->ii; i++)
    {
      while (ps->rows[i])
	{
	  ps_insn_ptr ps_insn = ps->rows[i]->next_in_row;

	  free (ps->rows[i]);
	  ps->rows[i] = ps_insn;
	}
      ps->rows[i] = NULL;
    }
}

/* Free all the memory allocated to the partial schedule.  */

static void
free_partial_schedule (partial_schedule_ptr ps)
{
  if (!ps)
    return;
  free_ps_insns (ps);
  free (ps->rows);
  free (ps);
}

/* Clear the rows array with its PS_INSNs, and create a new one with
   NEW_II rows.  */

static void
reset_partial_schedule (partial_schedule_ptr ps, int new_ii)
{
  if (!ps)
    return;
  free_ps_insns (ps);
  if (new_ii == ps->ii)
    return;
  ps->rows = (ps_insn_ptr *) xrealloc (ps->rows, new_ii
						 * sizeof (ps_insn_ptr));
  memset (ps->rows, 0, new_ii * sizeof (ps_insn_ptr));
  ps->ii = new_ii;
  ps->min_cycle = INT_MAX;
  ps->max_cycle = INT_MIN;
}

/* Prints the partial schedule as an ii rows array, for each rows
   print the ids of the insns in it.  */
void
print_partial_schedule (partial_schedule_ptr ps, FILE *dump)
{
  int i;

  for (i = 0; i < ps->ii; i++)
    {
      ps_insn_ptr ps_i = ps->rows[i];

      fprintf (dump, "\n[CYCLE %d ]: ", i);
      while (ps_i)
	{
	  fprintf (dump, "%d, ",
		   INSN_UID (ps_i->node->insn));
	  ps_i = ps_i->next_in_row;
	}
    }
}

/* Creates an object of PS_INSN and initializes it to the given parameters.  */
static ps_insn_ptr
create_ps_insn (ddg_node_ptr node, int rest_count, int cycle)
{
  ps_insn_ptr ps_i = XNEW (struct ps_insn);

  ps_i->node = node;
  ps_i->next_in_row = NULL;
  ps_i->prev_in_row = NULL;
  ps_i->row_rest_count = rest_count;
  ps_i->cycle = cycle;

  return ps_i;
}


/* Removes the given PS_INSN from the partial schedule.  Returns false if the
   node is not found in the partial schedule, else returns true.  */
static bool
remove_node_from_ps (partial_schedule_ptr ps, ps_insn_ptr ps_i)
{
  int row;

  if (!ps || !ps_i)
    return false;

  row = SMODULO (ps_i->cycle, ps->ii);
  if (! ps_i->prev_in_row)
    {
      if (ps_i != ps->rows[row])
	return false;

      ps->rows[row] = ps_i->next_in_row;
      if (ps->rows[row])
	ps->rows[row]->prev_in_row = NULL;
    }
  else
    {
      ps_i->prev_in_row->next_in_row = ps_i->next_in_row;
      if (ps_i->next_in_row)
	ps_i->next_in_row->prev_in_row = ps_i->prev_in_row;
    }
  free (ps_i);
  return true;
}

/* Unlike what literature describes for modulo scheduling (which focuses
   on VLIW machines) the order of the instructions inside a cycle is
   important.  Given the bitmaps MUST_FOLLOW and MUST_PRECEDE we know
   where the current instruction should go relative to the already
   scheduled instructions in the given cycle.  Go over these
   instructions and find the first possible column to put it in.  */
static bool
ps_insn_find_column (partial_schedule_ptr ps, ps_insn_ptr ps_i,
		     sbitmap must_precede, sbitmap must_follow)
{
  ps_insn_ptr next_ps_i;
  ps_insn_ptr first_must_follow = NULL;
  ps_insn_ptr last_must_precede = NULL;
  int row;

  if (! ps_i)
    return false;

  row = SMODULO (ps_i->cycle, ps->ii);

  /* Find the first must follow and the last must precede
     and insert the node immediately after the must precede
     but make sure that it there is no must follow after it.  */
  for (next_ps_i = ps->rows[row];
       next_ps_i;
       next_ps_i = next_ps_i->next_in_row)
    {
      if (TEST_BIT (must_follow, next_ps_i->node->cuid)
	  && ! first_must_follow)
        first_must_follow = next_ps_i;
      if (TEST_BIT (must_precede, next_ps_i->node->cuid))
        {
          /* If we have already met a node that must follow, then
	     there is no possible column.  */
  	  if (first_must_follow)
            return false;
	  else
            last_must_precede = next_ps_i;
        }
    }

  /* Now insert the node after INSERT_AFTER_PSI.  */

  if (! last_must_precede)
    {
      ps_i->next_in_row = ps->rows[row];
      ps_i->prev_in_row = NULL;
      if (ps_i->next_in_row)
    	ps_i->next_in_row->prev_in_row = ps_i;
      ps->rows[row] = ps_i;
    }
  else
    {
      ps_i->next_in_row = last_must_precede->next_in_row;
      last_must_precede->next_in_row = ps_i;
      ps_i->prev_in_row = last_must_precede;
      if (ps_i->next_in_row)
        ps_i->next_in_row->prev_in_row = ps_i;
    }

  return true;
}

/* Advances the PS_INSN one column in its current row; returns false
   in failure and true in success.  Bit N is set in MUST_FOLLOW if 
   the node with cuid N must be come after the node pointed to by 
   PS_I when scheduled in the same cycle.  */
static int
ps_insn_advance_column (partial_schedule_ptr ps, ps_insn_ptr ps_i,
			sbitmap must_follow)
{
  ps_insn_ptr prev, next;
  int row;
  ddg_node_ptr next_node;

  if (!ps || !ps_i)
    return false;

  row = SMODULO (ps_i->cycle, ps->ii);

  if (! ps_i->next_in_row)
    return false;

  next_node = ps_i->next_in_row->node;

  /* Check if next_in_row is dependent on ps_i, both having same sched
     times (typically ANTI_DEP).  If so, ps_i cannot skip over it.  */
  if (TEST_BIT (must_follow, next_node->cuid))
    return false;

  /* Advance PS_I over its next_in_row in the doubly linked list.  */
  prev = ps_i->prev_in_row;
  next = ps_i->next_in_row;

  if (ps_i == ps->rows[row])
    ps->rows[row] = next;

  ps_i->next_in_row = next->next_in_row;

  if (next->next_in_row)
    next->next_in_row->prev_in_row = ps_i;

  next->next_in_row = ps_i;
  ps_i->prev_in_row = next;

  next->prev_in_row = prev;
  if (prev)
    prev->next_in_row = next;

  return true;
}

/* Inserts a DDG_NODE to the given partial schedule at the given cycle.
   Returns 0 if this is not possible and a PS_INSN otherwise.  Bit N is 
   set in MUST_PRECEDE/MUST_FOLLOW if the node with cuid N must be come 
   before/after (respectively) the node pointed to by PS_I when scheduled 
   in the same cycle.  */
static ps_insn_ptr
add_node_to_ps (partial_schedule_ptr ps, ddg_node_ptr node, int cycle,
		sbitmap must_precede, sbitmap must_follow)
{
  ps_insn_ptr ps_i;
  int rest_count = 1;
  int row = SMODULO (cycle, ps->ii);

  if (ps->rows[row]
      && ps->rows[row]->row_rest_count >= issue_rate)
    return NULL;

  if (ps->rows[row])
    rest_count += ps->rows[row]->row_rest_count;

  ps_i = create_ps_insn (node, rest_count, cycle);

  /* Finds and inserts PS_I according to MUST_FOLLOW and
     MUST_PRECEDE.  */
  if (! ps_insn_find_column (ps, ps_i, must_precede, must_follow))
    {
      free (ps_i);
      return NULL;
    }

  return ps_i;
}

/* Advance time one cycle.  Assumes DFA is being used.  */
static void
advance_one_cycle (void)
{
  if (targetm.sched.dfa_pre_cycle_insn)
    state_transition (curr_state,
		      targetm.sched.dfa_pre_cycle_insn ());

  state_transition (curr_state, NULL);

  if (targetm.sched.dfa_post_cycle_insn)
    state_transition (curr_state,
		      targetm.sched.dfa_post_cycle_insn ());
}

/* Given the kernel of a loop (from FIRST_INSN to LAST_INSN), finds
   the number of cycles according to DFA that the kernel fits in,
   we use this to check if we done well with SMS after we add
   register moves.  In some cases register moves overhead makes
   it even worse than the original loop.  We want SMS to be performed
   when it gives less cycles after register moves are added.  */
static int
kernel_number_of_cycles (rtx first_insn, rtx last_insn)
{
  int cycles = 0;
  rtx insn;
  int can_issue_more = issue_rate;

  state_reset (curr_state);

  for (insn = first_insn;
       insn != NULL_RTX && insn != last_insn;
       insn = NEXT_INSN (insn))
    {
      if (! INSN_P (insn) || GET_CODE (PATTERN (insn)) == USE)
	continue;

      /* Check if there is room for the current insn.  */
      if (!can_issue_more || state_dead_lock_p (curr_state))
	{
	  cycles ++;
	  advance_one_cycle ();
	  can_issue_more = issue_rate;
	}

	/* Update the DFA state and return with failure if the DFA found
	   recource conflicts.  */
      if (state_transition (curr_state, insn) >= 0)
	{
	  cycles ++;
	  advance_one_cycle ();
	  can_issue_more = issue_rate;
	}

      if (targetm.sched.variable_issue)
	can_issue_more =
	  targetm.sched.variable_issue (sched_dump, sched_verbose,
					insn, can_issue_more);
      /* A naked CLOBBER or USE generates no instruction, so don't
	 let them consume issue slots.  */
      else if (GET_CODE (PATTERN (insn)) != USE
	       && GET_CODE (PATTERN (insn)) != CLOBBER)
	can_issue_more--;
    }
  return cycles;
}

/* Checks if PS has resource conflicts according to DFA, starting from
   FROM cycle to TO cycle; returns true if there are conflicts and false
   if there are no conflicts.  Assumes DFA is being used.  */
static int
ps_has_conflicts (partial_schedule_ptr ps, int from, int to)
{
  int cycle;

  state_reset (curr_state);

  for (cycle = from; cycle <= to; cycle++)
    {
      ps_insn_ptr crr_insn;
      /* Holds the remaining issue slots in the current row.  */
      int can_issue_more = issue_rate;

      /* Walk through the DFA for the current row.  */
      for (crr_insn = ps->rows[SMODULO (cycle, ps->ii)];
	   crr_insn;
	   crr_insn = crr_insn->next_in_row)
	{
	  rtx insn = crr_insn->node->insn;

	  if (!INSN_P (insn))
	    continue;

	  /* Check if there is room for the current insn.  */
	  if (!can_issue_more || state_dead_lock_p (curr_state))
	    return true;

	  /* Update the DFA state and return with failure if the DFA found
	     recource conflicts.  */
	  if (state_transition (curr_state, insn) >= 0)
	    return true;

	  if (targetm.sched.variable_issue)
	    can_issue_more =
	      targetm.sched.variable_issue (sched_dump, sched_verbose,
					    insn, can_issue_more);
	  /* A naked CLOBBER or USE generates no instruction, so don't
	     let them consume issue slots.  */
	  else if (GET_CODE (PATTERN (insn)) != USE
		   && GET_CODE (PATTERN (insn)) != CLOBBER)
	    can_issue_more--;
	}

      /* Advance the DFA to the next cycle.  */
      advance_one_cycle ();
    }
  return false;
}

/* Checks if the given node causes resource conflicts when added to PS at
   cycle C.  If not the node is added to PS and returned; otherwise zero
   is returned.  Bit N is set in MUST_PRECEDE/MUST_FOLLOW if the node with 
   cuid N must be come before/after (respectively) the node pointed to by 
   PS_I when scheduled in the same cycle.  */
ps_insn_ptr
ps_add_node_check_conflicts (partial_schedule_ptr ps, ddg_node_ptr n,
   			     int c, sbitmap must_precede,
			     sbitmap must_follow)
{
  int has_conflicts = 0;
  ps_insn_ptr ps_i;

  /* First add the node to the PS, if this succeeds check for
     conflicts, trying different issue slots in the same row.  */
  if (! (ps_i = add_node_to_ps (ps, n, c, must_precede, must_follow)))
    return NULL; /* Failed to insert the node at the given cycle.  */

  has_conflicts = ps_has_conflicts (ps, c, c)
		  || (ps->history > 0
		      && ps_has_conflicts (ps,
					   c - ps->history,
					   c + ps->history));

  /* Try different issue slots to find one that the given node can be
     scheduled in without conflicts.  */
  while (has_conflicts)
    {
      if (! ps_insn_advance_column (ps, ps_i, must_follow))
	break;
      has_conflicts = ps_has_conflicts (ps, c, c)
		      || (ps->history > 0
			  && ps_has_conflicts (ps,
					       c - ps->history,
					       c + ps->history));
    }

  if (has_conflicts)
    {
      remove_node_from_ps (ps, ps_i);
      return NULL;
    }

  ps->min_cycle = MIN (ps->min_cycle, c);
  ps->max_cycle = MAX (ps->max_cycle, c);
  return ps_i;
}

/* Rotate the rows of PS such that insns scheduled at time
   START_CYCLE will appear in row 0.  Updates max/min_cycles.  */
void
rotate_partial_schedule (partial_schedule_ptr ps, int start_cycle)
{
  int i, row, backward_rotates;
  int last_row = ps->ii - 1;

  if (start_cycle == 0)
    return;

  backward_rotates = SMODULO (start_cycle, ps->ii);

  /* Revisit later and optimize this into a single loop.  */
  for (i = 0; i < backward_rotates; i++)
    {
      ps_insn_ptr first_row = ps->rows[0];

      for (row = 0; row < last_row; row++)
	ps->rows[row] = ps->rows[row+1];

      ps->rows[last_row] = first_row;
    }

  ps->max_cycle -= start_cycle;
  ps->min_cycle -= start_cycle;
}

/* Remove the node N from the partial schedule PS; because we restart the DFA
   each time we want to check for resource conflicts; this is equivalent to
   unscheduling the node N.  */
static bool
ps_unschedule_node (partial_schedule_ptr ps, ddg_node_ptr n)
{
  ps_insn_ptr ps_i;
  int row = SMODULO (SCHED_TIME (n), ps->ii);

  if (row < 0 || row > ps->ii)
    return false;

  for (ps_i = ps->rows[row];
       ps_i &&  ps_i->node != n;
       ps_i = ps_i->next_in_row);
  if (!ps_i)
    return false;

  return remove_node_from_ps (ps, ps_i);
}
#endif /* INSN_SCHEDULING */

static bool
gate_handle_sms (void)
{
  return (optimize > 0 && flag_modulo_sched);
}


/* Run instruction scheduler.  */
/* Perform SMS module scheduling.  */
static unsigned int
rest_of_handle_sms (void)
{
#ifdef INSN_SCHEDULING
  basic_block bb;

  /* We want to be able to create new pseudos.  */
  no_new_pseudos = 0;
  /* Collect loop information to be used in SMS.  */
  cfg_layout_initialize (CLEANUP_UPDATE_LIFE);
  sms_schedule ();

  /* Update the life information, because we add pseudos.  */
  max_regno = max_reg_num ();
  allocate_reg_info (max_regno, FALSE, FALSE);
  update_life_info (NULL, UPDATE_LIFE_GLOBAL_RM_NOTES,
                    (PROP_DEATH_NOTES
                     | PROP_REG_INFO
                     | PROP_KILL_DEAD_CODE
                     | PROP_SCAN_DEAD_CODE));

  no_new_pseudos = 1;

  /* Finalize layout changes.  */
  FOR_EACH_BB (bb)
    if (bb->next_bb != EXIT_BLOCK_PTR)
      bb->aux = bb->next_bb;
  cfg_layout_finalize ();
  free_dominance_info (CDI_DOMINATORS);
#endif /* INSN_SCHEDULING */
  return 0;
}

struct tree_opt_pass pass_sms =
{
  "sms",                                /* name */
  gate_handle_sms,                      /* gate */
  rest_of_handle_sms,                   /* execute */
  NULL,                                 /* sub */
  NULL,                                 /* next */
  0,                                    /* static_pass_number */
  TV_SMS,                               /* tv_id */
  0,                                    /* properties_required */
  0,                                    /* properties_provided */
  0,                                    /* properties_destroyed */
  TODO_dump_func,                       /* todo_flags_start */
  TODO_dump_func |
  TODO_ggc_collect,                     /* todo_flags_finish */
  'm'                                   /* letter */
};

