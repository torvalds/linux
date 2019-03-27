/* Loop unrolling and peeling.
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
#include "params.h"
#include "output.h"
#include "expr.h"
#include "hashtab.h"
#include "recog.h"    

/* This pass performs loop unrolling and peeling.  We only perform these
   optimizations on innermost loops (with single exception) because
   the impact on performance is greatest here, and we want to avoid
   unnecessary code size growth.  The gain is caused by greater sequentiality
   of code, better code to optimize for further passes and in some cases
   by fewer testings of exit conditions.  The main problem is code growth,
   that impacts performance negatively due to effect of caches.

   What we do:

   -- complete peeling of once-rolling loops; this is the above mentioned
      exception, as this causes loop to be cancelled completely and
      does not cause code growth
   -- complete peeling of loops that roll (small) constant times.
   -- simple peeling of first iterations of loops that do not roll much
      (according to profile feedback)
   -- unrolling of loops that roll constant times; this is almost always
      win, as we get rid of exit condition tests.
   -- unrolling of loops that roll number of times that we can compute
      in runtime; we also get rid of exit condition tests here, but there
      is the extra expense for calculating the number of iterations
   -- simple unrolling of remaining loops; this is performed only if we
      are asked to, as the gain is questionable in this case and often
      it may even slow down the code
   For more detailed descriptions of each of those, see comments at
   appropriate function below.

   There is a lot of parameters (defined and described in params.def) that
   control how much we unroll/peel.

   ??? A great problem is that we don't have a good way how to determine
   how many times we should unroll the loop; the experiments I have made
   showed that this choice may affect performance in order of several %.
   */

/* Information about induction variables to split.  */

struct iv_to_split
{
  rtx insn;		/* The insn in that the induction variable occurs.  */
  rtx base_var;		/* The variable on that the values in the further
			   iterations are based.  */
  rtx step;		/* Step of the induction variable.  */
  unsigned n_loc;
  unsigned loc[3];	/* Location where the definition of the induction
			   variable occurs in the insn.  For example if
			   N_LOC is 2, the expression is located at
			   XEXP (XEXP (single_set, loc[0]), loc[1]).  */ 
};

/* Information about accumulators to expand.  */

struct var_to_expand
{
  rtx insn;		           /* The insn in that the variable expansion occurs.  */
  rtx reg;                         /* The accumulator which is expanded.  */
  VEC(rtx,heap) *var_expansions;   /* The copies of the accumulator which is expanded.  */ 
  enum rtx_code op;                /* The type of the accumulation - addition, subtraction 
                                      or multiplication.  */
  int expansion_count;             /* Count the number of expansions generated so far.  */
  int reuse_expansion;             /* The expansion we intend to reuse to expand
                                      the accumulator.  If REUSE_EXPANSION is 0 reuse 
                                      the original accumulator.  Else use 
                                      var_expansions[REUSE_EXPANSION - 1].  */
};

/* Information about optimization applied in
   the unrolled loop.  */

struct opt_info
{
  htab_t insns_to_split;           /* A hashtable of insns to split.  */
  htab_t insns_with_var_to_expand; /* A hashtable of insns with accumulators
                                      to expand.  */
  unsigned first_new_block;        /* The first basic block that was
                                      duplicated.  */
  basic_block loop_exit;           /* The loop exit basic block.  */
  basic_block loop_preheader;      /* The loop preheader basic block.  */
};

static void decide_unrolling_and_peeling (struct loops *, int);
static void peel_loops_completely (struct loops *, int);
static void decide_peel_simple (struct loop *, int);
static void decide_peel_once_rolling (struct loop *, int);
static void decide_peel_completely (struct loop *, int);
static void decide_unroll_stupid (struct loop *, int);
static void decide_unroll_constant_iterations (struct loop *, int);
static void decide_unroll_runtime_iterations (struct loop *, int);
static void peel_loop_simple (struct loops *, struct loop *);
static void peel_loop_completely (struct loops *, struct loop *);
static void unroll_loop_stupid (struct loops *, struct loop *);
static void unroll_loop_constant_iterations (struct loops *, struct loop *);
static void unroll_loop_runtime_iterations (struct loops *, struct loop *);
static struct opt_info *analyze_insns_in_loop (struct loop *);
static void opt_info_start_duplication (struct opt_info *);
static void apply_opt_in_copies (struct opt_info *, unsigned, bool, bool);
static void free_opt_info (struct opt_info *);
static struct var_to_expand *analyze_insn_to_expand_var (struct loop*, rtx);
static bool referenced_in_one_insn_in_loop_p (struct loop *, rtx);
static struct iv_to_split *analyze_iv_to_split_insn (rtx);
static void expand_var_during_unrolling (struct var_to_expand *, rtx);
static int insert_var_expansion_initialization (void **, void *);
static int combine_var_copies_in_loop_exit (void **, void *);
static int release_var_copies (void **, void *);
static rtx get_expansion (struct var_to_expand *);

/* Unroll and/or peel (depending on FLAGS) LOOPS.  */
void
unroll_and_peel_loops (struct loops *loops, int flags)
{
  struct loop *loop, *next;
  bool check;

  /* First perform complete loop peeling (it is almost surely a win,
     and affects parameters for further decision a lot).  */
  peel_loops_completely (loops, flags);

  /* Now decide rest of unrolling and peeling.  */
  decide_unrolling_and_peeling (loops, flags);

  loop = loops->tree_root;
  while (loop->inner)
    loop = loop->inner;

  /* Scan the loops, inner ones first.  */
  while (loop != loops->tree_root)
    {
      if (loop->next)
	{
	  next = loop->next;
	  while (next->inner)
	    next = next->inner;
	}
      else
	next = loop->outer;

      check = true;
      /* And perform the appropriate transformations.  */
      switch (loop->lpt_decision.decision)
	{
	case LPT_PEEL_COMPLETELY:
	  /* Already done.  */
	  gcc_unreachable ();
	case LPT_PEEL_SIMPLE:
	  peel_loop_simple (loops, loop);
	  break;
	case LPT_UNROLL_CONSTANT:
	  unroll_loop_constant_iterations (loops, loop);
	  break;
	case LPT_UNROLL_RUNTIME:
	  unroll_loop_runtime_iterations (loops, loop);
	  break;
	case LPT_UNROLL_STUPID:
	  unroll_loop_stupid (loops, loop);
	  break;
	case LPT_NONE:
	  check = false;
	  break;
	default:
	  gcc_unreachable ();
	}
      if (check)
	{
#ifdef ENABLE_CHECKING
	  verify_dominators (CDI_DOMINATORS);
	  verify_loop_structure (loops);
#endif
	}
      loop = next;
    }

  iv_analysis_done ();
}

/* Check whether exit of the LOOP is at the end of loop body.  */

static bool
loop_exit_at_end_p (struct loop *loop)
{
  struct niter_desc *desc = get_simple_loop_desc (loop);
  rtx insn;

  if (desc->in_edge->dest != loop->latch)
    return false;

  /* Check that the latch is empty.  */
  FOR_BB_INSNS (loop->latch, insn)
    {
      if (INSN_P (insn))
	return false;
    }

  return true;
}

/* Check whether to peel LOOPS (depending on FLAGS) completely and do so.  */
static void
peel_loops_completely (struct loops *loops, int flags)
{
  struct loop *loop;
  unsigned i;

  /* Scan the loops, the inner ones first.  */
  for (i = loops->num - 1; i > 0; i--)
    {
      loop = loops->parray[i];
      if (!loop)
	continue;

      loop->lpt_decision.decision = LPT_NONE;

      if (dump_file)
	fprintf (dump_file,
		 "\n;; *** Considering loop %d for complete peeling ***\n",
		 loop->num);

      loop->ninsns = num_loop_insns (loop);

      decide_peel_once_rolling (loop, flags);
      if (loop->lpt_decision.decision == LPT_NONE)
	decide_peel_completely (loop, flags);

      if (loop->lpt_decision.decision == LPT_PEEL_COMPLETELY)
	{
	  peel_loop_completely (loops, loop);
#ifdef ENABLE_CHECKING
	  verify_dominators (CDI_DOMINATORS);
	  verify_loop_structure (loops);
#endif
	}
    }
}

/* Decide whether unroll or peel LOOPS (depending on FLAGS) and how much.  */
static void
decide_unrolling_and_peeling (struct loops *loops, int flags)
{
  struct loop *loop = loops->tree_root, *next;

  while (loop->inner)
    loop = loop->inner;

  /* Scan the loops, inner ones first.  */
  while (loop != loops->tree_root)
    {
      if (loop->next)
	{
	  next = loop->next;
	  while (next->inner)
	    next = next->inner;
	}
      else
	next = loop->outer;

      loop->lpt_decision.decision = LPT_NONE;

      if (dump_file)
	fprintf (dump_file, "\n;; *** Considering loop %d ***\n", loop->num);

      /* Do not peel cold areas.  */
      if (!maybe_hot_bb_p (loop->header))
	{
	  if (dump_file)
	    fprintf (dump_file, ";; Not considering loop, cold area\n");
	  loop = next;
	  continue;
	}

      /* Can the loop be manipulated?  */
      if (!can_duplicate_loop_p (loop))
	{
	  if (dump_file)
	    fprintf (dump_file,
		     ";; Not considering loop, cannot duplicate\n");
	  loop = next;
	  continue;
	}

      /* Skip non-innermost loops.  */
      if (loop->inner)
	{
	  if (dump_file)
	    fprintf (dump_file, ";; Not considering loop, is not innermost\n");
	  loop = next;
	  continue;
	}

      loop->ninsns = num_loop_insns (loop);
      loop->av_ninsns = average_num_loop_insns (loop);

      /* Try transformations one by one in decreasing order of
	 priority.  */

      decide_unroll_constant_iterations (loop, flags);
      if (loop->lpt_decision.decision == LPT_NONE)
	decide_unroll_runtime_iterations (loop, flags);
      if (loop->lpt_decision.decision == LPT_NONE)
	decide_unroll_stupid (loop, flags);
      if (loop->lpt_decision.decision == LPT_NONE)
	decide_peel_simple (loop, flags);

      loop = next;
    }
}

/* Decide whether the LOOP is once rolling and suitable for complete
   peeling.  */
static void
decide_peel_once_rolling (struct loop *loop, int flags ATTRIBUTE_UNUSED)
{
  struct niter_desc *desc;

  if (dump_file)
    fprintf (dump_file, "\n;; Considering peeling once rolling loop\n");

  /* Is the loop small enough?  */
  if ((unsigned) PARAM_VALUE (PARAM_MAX_ONCE_PEELED_INSNS) < loop->ninsns)
    {
      if (dump_file)
	fprintf (dump_file, ";; Not considering loop, is too big\n");
      return;
    }

  /* Check for simple loops.  */
  desc = get_simple_loop_desc (loop);

  /* Check number of iterations.  */
  if (!desc->simple_p
      || desc->assumptions
      || desc->infinite
      || !desc->const_iter
      || desc->niter != 0)
    {
      if (dump_file)
	fprintf (dump_file,
		 ";; Unable to prove that the loop rolls exactly once\n");
      return;
    }

  /* Success.  */
  if (dump_file)
    fprintf (dump_file, ";; Decided to peel exactly once rolling loop\n");
  loop->lpt_decision.decision = LPT_PEEL_COMPLETELY;
}

/* Decide whether the LOOP is suitable for complete peeling.  */
static void
decide_peel_completely (struct loop *loop, int flags ATTRIBUTE_UNUSED)
{
  unsigned npeel;
  struct niter_desc *desc;

  if (dump_file)
    fprintf (dump_file, "\n;; Considering peeling completely\n");

  /* Skip non-innermost loops.  */
  if (loop->inner)
    {
      if (dump_file)
	fprintf (dump_file, ";; Not considering loop, is not innermost\n");
      return;
    }

  /* Do not peel cold areas.  */
  if (!maybe_hot_bb_p (loop->header))
    {
      if (dump_file)
	fprintf (dump_file, ";; Not considering loop, cold area\n");
      return;
    }

  /* Can the loop be manipulated?  */
  if (!can_duplicate_loop_p (loop))
    {
      if (dump_file)
	fprintf (dump_file,
		 ";; Not considering loop, cannot duplicate\n");
      return;
    }

  /* npeel = number of iterations to peel.  */
  npeel = PARAM_VALUE (PARAM_MAX_COMPLETELY_PEELED_INSNS) / loop->ninsns;
  if (npeel > (unsigned) PARAM_VALUE (PARAM_MAX_COMPLETELY_PEEL_TIMES))
    npeel = PARAM_VALUE (PARAM_MAX_COMPLETELY_PEEL_TIMES);

  /* Is the loop small enough?  */
  if (!npeel)
    {
      if (dump_file)
	fprintf (dump_file, ";; Not considering loop, is too big\n");
      return;
    }

  /* Check for simple loops.  */
  desc = get_simple_loop_desc (loop);

  /* Check number of iterations.  */
  if (!desc->simple_p
      || desc->assumptions
      || !desc->const_iter
      || desc->infinite)
    {
      if (dump_file)
	fprintf (dump_file,
		 ";; Unable to prove that the loop iterates constant times\n");
      return;
    }

  if (desc->niter > npeel - 1)
    {
      if (dump_file)
	{
	  fprintf (dump_file,
		   ";; Not peeling loop completely, rolls too much (");
	  fprintf (dump_file, HOST_WIDEST_INT_PRINT_DEC, desc->niter);
	  fprintf (dump_file, " iterations > %d [maximum peelings])\n", npeel);
	}
      return;
    }

  /* Success.  */
  if (dump_file)
    fprintf (dump_file, ";; Decided to peel loop completely\n");
  loop->lpt_decision.decision = LPT_PEEL_COMPLETELY;
}

/* Peel all iterations of LOOP, remove exit edges and cancel the loop
   completely.  The transformation done:

   for (i = 0; i < 4; i++)
     body;

   ==>

   i = 0;
   body; i++;
   body; i++;
   body; i++;
   body; i++;
   */
static void
peel_loop_completely (struct loops *loops, struct loop *loop)
{
  sbitmap wont_exit;
  unsigned HOST_WIDE_INT npeel;
  unsigned n_remove_edges, i;
  edge *remove_edges, ein;
  struct niter_desc *desc = get_simple_loop_desc (loop);
  struct opt_info *opt_info = NULL;
  
  npeel = desc->niter;

  if (npeel)
    {
      bool ok;
      
      wont_exit = sbitmap_alloc (npeel + 1);
      sbitmap_ones (wont_exit);
      RESET_BIT (wont_exit, 0);
      if (desc->noloop_assumptions)
	RESET_BIT (wont_exit, 1);

      remove_edges = XCNEWVEC (edge, npeel);
      n_remove_edges = 0;

      if (flag_split_ivs_in_unroller)
        opt_info = analyze_insns_in_loop (loop);
      
      opt_info_start_duplication (opt_info);
      ok = duplicate_loop_to_header_edge (loop, loop_preheader_edge (loop),
					  loops, npeel,
					  wont_exit, desc->out_edge,
					  remove_edges, &n_remove_edges,
					  DLTHE_FLAG_UPDATE_FREQ
					  | DLTHE_FLAG_COMPLETTE_PEEL
					  | (opt_info
					     ? DLTHE_RECORD_COPY_NUMBER : 0));
      gcc_assert (ok);

      free (wont_exit);
      
      if (opt_info)
 	{
 	  apply_opt_in_copies (opt_info, npeel, false, true);
 	  free_opt_info (opt_info);
 	}

      /* Remove the exit edges.  */
      for (i = 0; i < n_remove_edges; i++)
	remove_path (loops, remove_edges[i]);
      free (remove_edges);
    }

  ein = desc->in_edge;
  free_simple_loop_desc (loop);

  /* Now remove the unreachable part of the last iteration and cancel
     the loop.  */
  remove_path (loops, ein);

  if (dump_file)
    fprintf (dump_file, ";; Peeled loop completely, %d times\n", (int) npeel);
}

/* Decide whether to unroll LOOP iterating constant number of times
   and how much.  */

static void
decide_unroll_constant_iterations (struct loop *loop, int flags)
{
  unsigned nunroll, nunroll_by_av, best_copies, best_unroll = 0, n_copies, i;
  struct niter_desc *desc;

  if (!(flags & UAP_UNROLL))
    {
      /* We were not asked to, just return back silently.  */
      return;
    }

  if (dump_file)
    fprintf (dump_file,
	     "\n;; Considering unrolling loop with constant "
	     "number of iterations\n");

  /* nunroll = total number of copies of the original loop body in
     unrolled loop (i.e. if it is 2, we have to duplicate loop body once.  */
  nunroll = PARAM_VALUE (PARAM_MAX_UNROLLED_INSNS) / loop->ninsns;
  nunroll_by_av
    = PARAM_VALUE (PARAM_MAX_AVERAGE_UNROLLED_INSNS) / loop->av_ninsns;
  if (nunroll > nunroll_by_av)
    nunroll = nunroll_by_av;
  if (nunroll > (unsigned) PARAM_VALUE (PARAM_MAX_UNROLL_TIMES))
    nunroll = PARAM_VALUE (PARAM_MAX_UNROLL_TIMES);

  /* Skip big loops.  */
  if (nunroll <= 1)
    {
      if (dump_file)
	fprintf (dump_file, ";; Not considering loop, is too big\n");
      return;
    }

  /* Check for simple loops.  */
  desc = get_simple_loop_desc (loop);

  /* Check number of iterations.  */
  if (!desc->simple_p || !desc->const_iter || desc->assumptions)
    {
      if (dump_file)
	fprintf (dump_file,
		 ";; Unable to prove that the loop iterates constant times\n");
      return;
    }

  /* Check whether the loop rolls enough to consider.  */
  if (desc->niter < 2 * nunroll)
    {
      if (dump_file)
	fprintf (dump_file, ";; Not unrolling loop, doesn't roll\n");
      return;
    }

  /* Success; now compute number of iterations to unroll.  We alter
     nunroll so that as few as possible copies of loop body are
     necessary, while still not decreasing the number of unrollings
     too much (at most by 1).  */
  best_copies = 2 * nunroll + 10;

  i = 2 * nunroll + 2;
  if (i - 1 >= desc->niter)
    i = desc->niter - 2;

  for (; i >= nunroll - 1; i--)
    {
      unsigned exit_mod = desc->niter % (i + 1);

      if (!loop_exit_at_end_p (loop))
	n_copies = exit_mod + i + 1;
      else if (exit_mod != (unsigned) i
	       || desc->noloop_assumptions != NULL_RTX)
	n_copies = exit_mod + i + 2;
      else
	n_copies = i + 1;

      if (n_copies < best_copies)
	{
	  best_copies = n_copies;
	  best_unroll = i;
	}
    }

  if (dump_file)
    fprintf (dump_file, ";; max_unroll %d (%d copies, initial %d).\n",
	     best_unroll + 1, best_copies, nunroll);

  loop->lpt_decision.decision = LPT_UNROLL_CONSTANT;
  loop->lpt_decision.times = best_unroll;
  
  if (dump_file)
    fprintf (dump_file,
	     ";; Decided to unroll the constant times rolling loop, %d times.\n",
	     loop->lpt_decision.times);
}

/* Unroll LOOP with constant number of iterations LOOP->LPT_DECISION.TIMES + 1
   times.  The transformation does this:

   for (i = 0; i < 102; i++)
     body;

   ==>

   i = 0;
   body; i++;
   body; i++;
   while (i < 102)
     {
       body; i++;
       body; i++;
       body; i++;
       body; i++;
     }
  */
static void
unroll_loop_constant_iterations (struct loops *loops, struct loop *loop)
{
  unsigned HOST_WIDE_INT niter;
  unsigned exit_mod;
  sbitmap wont_exit;
  unsigned n_remove_edges, i;
  edge *remove_edges;
  unsigned max_unroll = loop->lpt_decision.times;
  struct niter_desc *desc = get_simple_loop_desc (loop);
  bool exit_at_end = loop_exit_at_end_p (loop);
  struct opt_info *opt_info = NULL;
  bool ok;
  
  niter = desc->niter;

  /* Should not get here (such loop should be peeled instead).  */
  gcc_assert (niter > max_unroll + 1);

  exit_mod = niter % (max_unroll + 1);

  wont_exit = sbitmap_alloc (max_unroll + 1);
  sbitmap_ones (wont_exit);

  remove_edges = XCNEWVEC (edge, max_unroll + exit_mod + 1);
  n_remove_edges = 0;
  if (flag_split_ivs_in_unroller 
      || flag_variable_expansion_in_unroller)
    opt_info = analyze_insns_in_loop (loop);
  
  if (!exit_at_end)
    {
      /* The exit is not at the end of the loop; leave exit test
	 in the first copy, so that the loops that start with test
	 of exit condition have continuous body after unrolling.  */

      if (dump_file)
	fprintf (dump_file, ";; Condition on beginning of loop.\n");

      /* Peel exit_mod iterations.  */
      RESET_BIT (wont_exit, 0);
      if (desc->noloop_assumptions)
	RESET_BIT (wont_exit, 1);

      if (exit_mod)
	{
	  opt_info_start_duplication (opt_info);
          ok = duplicate_loop_to_header_edge (loop, loop_preheader_edge (loop),
					      loops, exit_mod,
					      wont_exit, desc->out_edge,
					      remove_edges, &n_remove_edges,
					      DLTHE_FLAG_UPDATE_FREQ
					      | (opt_info && exit_mod > 1
						 ? DLTHE_RECORD_COPY_NUMBER
						   : 0));
	  gcc_assert (ok);

          if (opt_info && exit_mod > 1)
 	    apply_opt_in_copies (opt_info, exit_mod, false, false); 
          
	  desc->noloop_assumptions = NULL_RTX;
	  desc->niter -= exit_mod;
	  desc->niter_max -= exit_mod;
	}

      SET_BIT (wont_exit, 1);
    }
  else
    {
      /* Leave exit test in last copy, for the same reason as above if
	 the loop tests the condition at the end of loop body.  */

      if (dump_file)
	fprintf (dump_file, ";; Condition on end of loop.\n");

      /* We know that niter >= max_unroll + 2; so we do not need to care of
	 case when we would exit before reaching the loop.  So just peel
	 exit_mod + 1 iterations.  */
      if (exit_mod != max_unroll
	  || desc->noloop_assumptions)
	{
	  RESET_BIT (wont_exit, 0);
	  if (desc->noloop_assumptions)
	    RESET_BIT (wont_exit, 1);
         
          opt_info_start_duplication (opt_info);
	  ok = duplicate_loop_to_header_edge (loop, loop_preheader_edge (loop),
					      loops, exit_mod + 1,
					      wont_exit, desc->out_edge,
					      remove_edges, &n_remove_edges,
					      DLTHE_FLAG_UPDATE_FREQ
					      | (opt_info && exit_mod > 0
						 ? DLTHE_RECORD_COPY_NUMBER
						   : 0));
	  gcc_assert (ok);
 
          if (opt_info && exit_mod > 0)
  	    apply_opt_in_copies (opt_info, exit_mod + 1, false, false);

	  desc->niter -= exit_mod + 1;
	  desc->niter_max -= exit_mod + 1;
	  desc->noloop_assumptions = NULL_RTX;

	  SET_BIT (wont_exit, 0);
	  SET_BIT (wont_exit, 1);
	}

      RESET_BIT (wont_exit, max_unroll);
    }

  /* Now unroll the loop.  */
  
  opt_info_start_duplication (opt_info);
  ok = duplicate_loop_to_header_edge (loop, loop_latch_edge (loop),
				      loops, max_unroll,
				      wont_exit, desc->out_edge,
				      remove_edges, &n_remove_edges,
				      DLTHE_FLAG_UPDATE_FREQ
				      | (opt_info
					 ? DLTHE_RECORD_COPY_NUMBER
					   : 0));
  gcc_assert (ok);

  if (opt_info)
    {
      apply_opt_in_copies (opt_info, max_unroll, true, true);
      free_opt_info (opt_info);
    }

  free (wont_exit);

  if (exit_at_end)
    {
      basic_block exit_block = get_bb_copy (desc->in_edge->src);
      /* Find a new in and out edge; they are in the last copy we have made.  */
      
      if (EDGE_SUCC (exit_block, 0)->dest == desc->out_edge->dest)
	{
	  desc->out_edge = EDGE_SUCC (exit_block, 0);
	  desc->in_edge = EDGE_SUCC (exit_block, 1);
	}
      else
	{
	  desc->out_edge = EDGE_SUCC (exit_block, 1);
	  desc->in_edge = EDGE_SUCC (exit_block, 0);
	}
    }

  desc->niter /= max_unroll + 1;
  desc->niter_max /= max_unroll + 1;
  desc->niter_expr = GEN_INT (desc->niter);

  /* Remove the edges.  */
  for (i = 0; i < n_remove_edges; i++)
    remove_path (loops, remove_edges[i]);
  free (remove_edges);

  if (dump_file)
    fprintf (dump_file,
	     ";; Unrolled loop %d times, constant # of iterations %i insns\n",
	     max_unroll, num_loop_insns (loop));
}

/* Decide whether to unroll LOOP iterating runtime computable number of times
   and how much.  */
static void
decide_unroll_runtime_iterations (struct loop *loop, int flags)
{
  unsigned nunroll, nunroll_by_av, i;
  struct niter_desc *desc;

  if (!(flags & UAP_UNROLL))
    {
      /* We were not asked to, just return back silently.  */
      return;
    }

  if (dump_file)
    fprintf (dump_file,
	     "\n;; Considering unrolling loop with runtime "
	     "computable number of iterations\n");

  /* nunroll = total number of copies of the original loop body in
     unrolled loop (i.e. if it is 2, we have to duplicate loop body once.  */
  nunroll = PARAM_VALUE (PARAM_MAX_UNROLLED_INSNS) / loop->ninsns;
  nunroll_by_av = PARAM_VALUE (PARAM_MAX_AVERAGE_UNROLLED_INSNS) / loop->av_ninsns;
  if (nunroll > nunroll_by_av)
    nunroll = nunroll_by_av;
  if (nunroll > (unsigned) PARAM_VALUE (PARAM_MAX_UNROLL_TIMES))
    nunroll = PARAM_VALUE (PARAM_MAX_UNROLL_TIMES);

  /* Skip big loops.  */
  if (nunroll <= 1)
    {
      if (dump_file)
	fprintf (dump_file, ";; Not considering loop, is too big\n");
      return;
    }

  /* Check for simple loops.  */
  desc = get_simple_loop_desc (loop);

  /* Check simpleness.  */
  if (!desc->simple_p || desc->assumptions)
    {
      if (dump_file)
	fprintf (dump_file,
		 ";; Unable to prove that the number of iterations "
		 "can be counted in runtime\n");
      return;
    }

  if (desc->const_iter)
    {
      if (dump_file)
	fprintf (dump_file, ";; Loop iterates constant times\n");
      return;
    }

  /* If we have profile feedback, check whether the loop rolls.  */
  if (loop->header->count && expected_loop_iterations (loop) < 2 * nunroll)
    {
      if (dump_file)
	fprintf (dump_file, ";; Not unrolling loop, doesn't roll\n");
      return;
    }

  /* Success; now force nunroll to be power of 2, as we are unable to
     cope with overflows in computation of number of iterations.  */
  for (i = 1; 2 * i <= nunroll; i *= 2)
    continue;

  loop->lpt_decision.decision = LPT_UNROLL_RUNTIME;
  loop->lpt_decision.times = i - 1;
  
  if (dump_file)
    fprintf (dump_file,
	     ";; Decided to unroll the runtime computable "
	     "times rolling loop, %d times.\n",
	     loop->lpt_decision.times);
}

/* Unroll LOOP for that we are able to count number of iterations in runtime
   LOOP->LPT_DECISION.TIMES + 1 times.  The transformation does this (with some
   extra care for case n < 0):

   for (i = 0; i < n; i++)
     body;

   ==>

   i = 0;
   mod = n % 4;

   switch (mod)
     {
       case 3:
         body; i++;
       case 2:
         body; i++;
       case 1:
         body; i++;
       case 0: ;
     }

   while (i < n)
     {
       body; i++;
       body; i++;
       body; i++;
       body; i++;
     }
   */
static void
unroll_loop_runtime_iterations (struct loops *loops, struct loop *loop)
{
  rtx old_niter, niter, init_code, branch_code, tmp;
  unsigned i, j, p;
  basic_block preheader, *body, *dom_bbs, swtch, ezc_swtch;
  unsigned n_dom_bbs;
  sbitmap wont_exit;
  int may_exit_copy;
  unsigned n_peel, n_remove_edges;
  edge *remove_edges, e;
  bool extra_zero_check, last_may_exit;
  unsigned max_unroll = loop->lpt_decision.times;
  struct niter_desc *desc = get_simple_loop_desc (loop);
  bool exit_at_end = loop_exit_at_end_p (loop);
  struct opt_info *opt_info = NULL;
  bool ok;
  
  if (flag_split_ivs_in_unroller
      || flag_variable_expansion_in_unroller)
    opt_info = analyze_insns_in_loop (loop);
  
  /* Remember blocks whose dominators will have to be updated.  */
  dom_bbs = XCNEWVEC (basic_block, n_basic_blocks);
  n_dom_bbs = 0;

  body = get_loop_body (loop);
  for (i = 0; i < loop->num_nodes; i++)
    {
      unsigned nldom;
      basic_block *ldom;

      nldom = get_dominated_by (CDI_DOMINATORS, body[i], &ldom);
      for (j = 0; j < nldom; j++)
	if (!flow_bb_inside_loop_p (loop, ldom[j]))
	  dom_bbs[n_dom_bbs++] = ldom[j];

      free (ldom);
    }
  free (body);

  if (!exit_at_end)
    {
      /* Leave exit in first copy (for explanation why see comment in
	 unroll_loop_constant_iterations).  */
      may_exit_copy = 0;
      n_peel = max_unroll - 1;
      extra_zero_check = true;
      last_may_exit = false;
    }
  else
    {
      /* Leave exit in last copy (for explanation why see comment in
	 unroll_loop_constant_iterations).  */
      may_exit_copy = max_unroll;
      n_peel = max_unroll;
      extra_zero_check = false;
      last_may_exit = true;
    }

  /* Get expression for number of iterations.  */
  start_sequence ();
  old_niter = niter = gen_reg_rtx (desc->mode);
  tmp = force_operand (copy_rtx (desc->niter_expr), niter);
  if (tmp != niter)
    emit_move_insn (niter, tmp);

  /* Count modulo by ANDing it with max_unroll; we use the fact that
     the number of unrollings is a power of two, and thus this is correct
     even if there is overflow in the computation.  */
  niter = expand_simple_binop (desc->mode, AND,
			       niter,
			       GEN_INT (max_unroll),
			       NULL_RTX, 0, OPTAB_LIB_WIDEN);

  init_code = get_insns ();
  end_sequence ();

  /* Precondition the loop.  */
  loop_split_edge_with (loop_preheader_edge (loop), init_code);

  remove_edges = XCNEWVEC (edge, max_unroll + n_peel + 1);
  n_remove_edges = 0;

  wont_exit = sbitmap_alloc (max_unroll + 2);

  /* Peel the first copy of loop body (almost always we must leave exit test
     here; the only exception is when we have extra zero check and the number
     of iterations is reliable.  Also record the place of (possible) extra
     zero check.  */
  sbitmap_zero (wont_exit);
  if (extra_zero_check
      && !desc->noloop_assumptions)
    SET_BIT (wont_exit, 1);
  ezc_swtch = loop_preheader_edge (loop)->src;
  ok = duplicate_loop_to_header_edge (loop, loop_preheader_edge (loop),
				      loops, 1,
				      wont_exit, desc->out_edge,
				      remove_edges, &n_remove_edges,
				      DLTHE_FLAG_UPDATE_FREQ);
  gcc_assert (ok);

  /* Record the place where switch will be built for preconditioning.  */
  swtch = loop_split_edge_with (loop_preheader_edge (loop),
				NULL_RTX);

  for (i = 0; i < n_peel; i++)
    {
      /* Peel the copy.  */
      sbitmap_zero (wont_exit);
      if (i != n_peel - 1 || !last_may_exit)
	SET_BIT (wont_exit, 1);
      ok = duplicate_loop_to_header_edge (loop, loop_preheader_edge (loop),
					  loops, 1,
					  wont_exit, desc->out_edge,
					  remove_edges, &n_remove_edges,
					  DLTHE_FLAG_UPDATE_FREQ);
      gcc_assert (ok);

      /* Create item for switch.  */
      j = n_peel - i - (extra_zero_check ? 0 : 1);
      p = REG_BR_PROB_BASE / (i + 2);

      preheader = loop_split_edge_with (loop_preheader_edge (loop), NULL_RTX);
      branch_code = compare_and_jump_seq (copy_rtx (niter), GEN_INT (j), EQ,
					  block_label (preheader), p,
					  NULL_RTX);

      swtch = loop_split_edge_with (single_pred_edge (swtch), branch_code);
      set_immediate_dominator (CDI_DOMINATORS, preheader, swtch);
      single_pred_edge (swtch)->probability = REG_BR_PROB_BASE - p;
      e = make_edge (swtch, preheader,
		     single_succ_edge (swtch)->flags & EDGE_IRREDUCIBLE_LOOP);
      e->probability = p;
    }

  if (extra_zero_check)
    {
      /* Add branch for zero iterations.  */
      p = REG_BR_PROB_BASE / (max_unroll + 1);
      swtch = ezc_swtch;
      preheader = loop_split_edge_with (loop_preheader_edge (loop), NULL_RTX);
      branch_code = compare_and_jump_seq (copy_rtx (niter), const0_rtx, EQ,
					  block_label (preheader), p,
					  NULL_RTX);

      swtch = loop_split_edge_with (single_succ_edge (swtch), branch_code);
      set_immediate_dominator (CDI_DOMINATORS, preheader, swtch);
      single_succ_edge (swtch)->probability = REG_BR_PROB_BASE - p;
      e = make_edge (swtch, preheader,
		     single_succ_edge (swtch)->flags & EDGE_IRREDUCIBLE_LOOP);
      e->probability = p;
    }

  /* Recount dominators for outer blocks.  */
  iterate_fix_dominators (CDI_DOMINATORS, dom_bbs, n_dom_bbs);

  /* And unroll loop.  */

  sbitmap_ones (wont_exit);
  RESET_BIT (wont_exit, may_exit_copy);
  opt_info_start_duplication (opt_info);
  
  ok = duplicate_loop_to_header_edge (loop, loop_latch_edge (loop),
				      loops, max_unroll,
				      wont_exit, desc->out_edge,
				      remove_edges, &n_remove_edges,
				      DLTHE_FLAG_UPDATE_FREQ
				      | (opt_info
					 ? DLTHE_RECORD_COPY_NUMBER
					   : 0));
  gcc_assert (ok);
  
  if (opt_info)
    {
      apply_opt_in_copies (opt_info, max_unroll, true, true);
      free_opt_info (opt_info);
    }

  free (wont_exit);

  if (exit_at_end)
    {
      basic_block exit_block = get_bb_copy (desc->in_edge->src);
      /* Find a new in and out edge; they are in the last copy we have
	 made.  */
      
      if (EDGE_SUCC (exit_block, 0)->dest == desc->out_edge->dest)
	{
	  desc->out_edge = EDGE_SUCC (exit_block, 0);
	  desc->in_edge = EDGE_SUCC (exit_block, 1);
	}
      else
	{
	  desc->out_edge = EDGE_SUCC (exit_block, 1);
	  desc->in_edge = EDGE_SUCC (exit_block, 0);
	}
    }

  /* Remove the edges.  */
  for (i = 0; i < n_remove_edges; i++)
    remove_path (loops, remove_edges[i]);
  free (remove_edges);

  /* We must be careful when updating the number of iterations due to
     preconditioning and the fact that the value must be valid at entry
     of the loop.  After passing through the above code, we see that
     the correct new number of iterations is this:  */
  gcc_assert (!desc->const_iter);
  desc->niter_expr =
    simplify_gen_binary (UDIV, desc->mode, old_niter,
			 GEN_INT (max_unroll + 1));
  desc->niter_max /= max_unroll + 1;
  if (exit_at_end)
    {
      desc->niter_expr =
	simplify_gen_binary (MINUS, desc->mode, desc->niter_expr, const1_rtx);
      desc->noloop_assumptions = NULL_RTX;
      desc->niter_max--;
    }

  if (dump_file)
    fprintf (dump_file,
	     ";; Unrolled loop %d times, counting # of iterations "
	     "in runtime, %i insns\n",
	     max_unroll, num_loop_insns (loop));

  if (dom_bbs)
    free (dom_bbs);
}

/* Decide whether to simply peel LOOP and how much.  */
static void
decide_peel_simple (struct loop *loop, int flags)
{
  unsigned npeel;
  struct niter_desc *desc;

  if (!(flags & UAP_PEEL))
    {
      /* We were not asked to, just return back silently.  */
      return;
    }

  if (dump_file)
    fprintf (dump_file, "\n;; Considering simply peeling loop\n");

  /* npeel = number of iterations to peel.  */
  npeel = PARAM_VALUE (PARAM_MAX_PEELED_INSNS) / loop->ninsns;
  if (npeel > (unsigned) PARAM_VALUE (PARAM_MAX_PEEL_TIMES))
    npeel = PARAM_VALUE (PARAM_MAX_PEEL_TIMES);

  /* Skip big loops.  */
  if (!npeel)
    {
      if (dump_file)
	fprintf (dump_file, ";; Not considering loop, is too big\n");
      return;
    }

  /* Check for simple loops.  */
  desc = get_simple_loop_desc (loop);

  /* Check number of iterations.  */
  if (desc->simple_p && !desc->assumptions && desc->const_iter)
    {
      if (dump_file)
	fprintf (dump_file, ";; Loop iterates constant times\n");
      return;
    }

  /* Do not simply peel loops with branches inside -- it increases number
     of mispredicts.  */
  if (num_loop_branches (loop) > 1)
    {
      if (dump_file)
	fprintf (dump_file, ";; Not peeling, contains branches\n");
      return;
    }

  if (loop->header->count)
    {
      unsigned niter = expected_loop_iterations (loop);
      if (niter + 1 > npeel)
	{
	  if (dump_file)
	    {
	      fprintf (dump_file, ";; Not peeling loop, rolls too much (");
	      fprintf (dump_file, HOST_WIDEST_INT_PRINT_DEC,
		       (HOST_WIDEST_INT) (niter + 1));
	      fprintf (dump_file, " iterations > %d [maximum peelings])\n",
		       npeel);
	    }
	  return;
	}
      npeel = niter + 1;
    }
  else
    {
      /* For now we have no good heuristics to decide whether loop peeling
         will be effective, so disable it.  */
      if (dump_file)
	fprintf (dump_file,
		 ";; Not peeling loop, no evidence it will be profitable\n");
      return;
    }

  /* Success.  */
  loop->lpt_decision.decision = LPT_PEEL_SIMPLE;
  loop->lpt_decision.times = npeel;
      
  if (dump_file)
    fprintf (dump_file, ";; Decided to simply peel the loop, %d times.\n",
	     loop->lpt_decision.times);
}

/* Peel a LOOP LOOP->LPT_DECISION.TIMES times.  The transformation:
   while (cond)
     body;

   ==>

   if (!cond) goto end;
   body;
   if (!cond) goto end;
   body;
   while (cond)
     body;
   end: ;
   */
static void
peel_loop_simple (struct loops *loops, struct loop *loop)
{
  sbitmap wont_exit;
  unsigned npeel = loop->lpt_decision.times;
  struct niter_desc *desc = get_simple_loop_desc (loop);
  struct opt_info *opt_info = NULL;
  bool ok;
  
  if (flag_split_ivs_in_unroller && npeel > 1)
    opt_info = analyze_insns_in_loop (loop);
  
  wont_exit = sbitmap_alloc (npeel + 1);
  sbitmap_zero (wont_exit);
  
  opt_info_start_duplication (opt_info);
  
  ok = duplicate_loop_to_header_edge (loop, loop_preheader_edge (loop),
				      loops, npeel, wont_exit,
				      NULL, NULL,
				      NULL, DLTHE_FLAG_UPDATE_FREQ
				      | (opt_info
					 ? DLTHE_RECORD_COPY_NUMBER
					   : 0));
  gcc_assert (ok);

  free (wont_exit);
  
  if (opt_info)
    {
      apply_opt_in_copies (opt_info, npeel, false, false);
      free_opt_info (opt_info);
    }

  if (desc->simple_p)
    {
      if (desc->const_iter)
	{
	  desc->niter -= npeel;
	  desc->niter_expr = GEN_INT (desc->niter);
	  desc->noloop_assumptions = NULL_RTX;
	}
      else
	{
	  /* We cannot just update niter_expr, as its value might be clobbered
	     inside loop.  We could handle this by counting the number into
	     temporary just like we do in runtime unrolling, but it does not
	     seem worthwhile.  */
	  free_simple_loop_desc (loop);
	}
    }
  if (dump_file)
    fprintf (dump_file, ";; Peeling loop %d times\n", npeel);
}

/* Decide whether to unroll LOOP stupidly and how much.  */
static void
decide_unroll_stupid (struct loop *loop, int flags)
{
  unsigned nunroll, nunroll_by_av, i;
  struct niter_desc *desc;

  if (!(flags & UAP_UNROLL_ALL))
    {
      /* We were not asked to, just return back silently.  */
      return;
    }

  if (dump_file)
    fprintf (dump_file, "\n;; Considering unrolling loop stupidly\n");

  /* nunroll = total number of copies of the original loop body in
     unrolled loop (i.e. if it is 2, we have to duplicate loop body once.  */
  nunroll = PARAM_VALUE (PARAM_MAX_UNROLLED_INSNS) / loop->ninsns;
  nunroll_by_av
    = PARAM_VALUE (PARAM_MAX_AVERAGE_UNROLLED_INSNS) / loop->av_ninsns;
  if (nunroll > nunroll_by_av)
    nunroll = nunroll_by_av;
  if (nunroll > (unsigned) PARAM_VALUE (PARAM_MAX_UNROLL_TIMES))
    nunroll = PARAM_VALUE (PARAM_MAX_UNROLL_TIMES);

  /* Skip big loops.  */
  if (nunroll <= 1)
    {
      if (dump_file)
	fprintf (dump_file, ";; Not considering loop, is too big\n");
      return;
    }

  /* Check for simple loops.  */
  desc = get_simple_loop_desc (loop);

  /* Check simpleness.  */
  if (desc->simple_p && !desc->assumptions)
    {
      if (dump_file)
	fprintf (dump_file, ";; The loop is simple\n");
      return;
    }

  /* Do not unroll loops with branches inside -- it increases number
     of mispredicts.  */
  if (num_loop_branches (loop) > 1)
    {
      if (dump_file)
	fprintf (dump_file, ";; Not unrolling, contains branches\n");
      return;
    }

  /* If we have profile feedback, check whether the loop rolls.  */
  if (loop->header->count
      && expected_loop_iterations (loop) < 2 * nunroll)
    {
      if (dump_file)
	fprintf (dump_file, ";; Not unrolling loop, doesn't roll\n");
      return;
    }

  /* Success.  Now force nunroll to be power of 2, as it seems that this
     improves results (partially because of better alignments, partially
     because of some dark magic).  */
  for (i = 1; 2 * i <= nunroll; i *= 2)
    continue;

  loop->lpt_decision.decision = LPT_UNROLL_STUPID;
  loop->lpt_decision.times = i - 1;
      
  if (dump_file)
    fprintf (dump_file,
	     ";; Decided to unroll the loop stupidly, %d times.\n",
	     loop->lpt_decision.times);
}

/* Unroll a LOOP LOOP->LPT_DECISION.TIMES times.  The transformation:
   while (cond)
     body;

   ==>

   while (cond)
     {
       body;
       if (!cond) break;
       body;
       if (!cond) break;
       body;
       if (!cond) break;
       body;
     }
   */
static void
unroll_loop_stupid (struct loops *loops, struct loop *loop)
{
  sbitmap wont_exit;
  unsigned nunroll = loop->lpt_decision.times;
  struct niter_desc *desc = get_simple_loop_desc (loop);
  struct opt_info *opt_info = NULL;
  bool ok;
  
  if (flag_split_ivs_in_unroller
      || flag_variable_expansion_in_unroller)
    opt_info = analyze_insns_in_loop (loop);
  
  
  wont_exit = sbitmap_alloc (nunroll + 1);
  sbitmap_zero (wont_exit);
  opt_info_start_duplication (opt_info);
  
  ok = duplicate_loop_to_header_edge (loop, loop_latch_edge (loop),
				      loops, nunroll, wont_exit,
				      NULL, NULL, NULL,
				      DLTHE_FLAG_UPDATE_FREQ
				      | (opt_info
					 ? DLTHE_RECORD_COPY_NUMBER
					   : 0));
  gcc_assert (ok);
  
  if (opt_info)
    {
      apply_opt_in_copies (opt_info, nunroll, true, true);
      free_opt_info (opt_info);
    }

  free (wont_exit);

  if (desc->simple_p)
    {
      /* We indeed may get here provided that there are nontrivial assumptions
	 for a loop to be really simple.  We could update the counts, but the
	 problem is that we are unable to decide which exit will be taken
	 (not really true in case the number of iterations is constant,
	 but noone will do anything with this information, so we do not
	 worry about it).  */
      desc->simple_p = false;
    }

  if (dump_file)
    fprintf (dump_file, ";; Unrolled loop %d times, %i insns\n",
	     nunroll, num_loop_insns (loop));
}

/* A hash function for information about insns to split.  */

static hashval_t
si_info_hash (const void *ivts)
{
  return (hashval_t) INSN_UID (((struct iv_to_split *) ivts)->insn);
}

/* An equality functions for information about insns to split.  */

static int
si_info_eq (const void *ivts1, const void *ivts2)
{
  const struct iv_to_split *i1 = ivts1;
  const struct iv_to_split *i2 = ivts2;

  return i1->insn == i2->insn;
}

/* Return a hash for VES, which is really a "var_to_expand *".  */

static hashval_t
ve_info_hash (const void *ves)
{
  return (hashval_t) INSN_UID (((struct var_to_expand *) ves)->insn);
}

/* Return true if IVTS1 and IVTS2 (which are really both of type 
   "var_to_expand *") refer to the same instruction.  */

static int
ve_info_eq (const void *ivts1, const void *ivts2)
{
  const struct var_to_expand *i1 = ivts1;
  const struct var_to_expand *i2 = ivts2;
  
  return i1->insn == i2->insn;
}

/* Returns true if REG is referenced in one insn in LOOP.  */

bool
referenced_in_one_insn_in_loop_p (struct loop *loop, rtx reg)
{
  basic_block *body, bb;
  unsigned i;
  int count_ref = 0;
  rtx insn;
  
  body = get_loop_body (loop); 
  for (i = 0; i < loop->num_nodes; i++)
    {
      bb = body[i];
      
      FOR_BB_INSNS (bb, insn)
      {
        if (rtx_referenced_p (reg, insn))
          count_ref++;
      }
    }
  return (count_ref  == 1);
}

/* Determine whether INSN contains an accumulator
   which can be expanded into separate copies, 
   one for each copy of the LOOP body.
   
   for (i = 0 ; i < n; i++)
     sum += a[i];
   
   ==>
     
   sum += a[i]
   ....
   i = i+1;
   sum1 += a[i]
   ....
   i = i+1
   sum2 += a[i];
   ....

   Return NULL if INSN contains no opportunity for expansion of accumulator.  
   Otherwise, allocate a VAR_TO_EXPAND structure, fill it with the relevant 
   information and return a pointer to it.
*/

static struct var_to_expand *
analyze_insn_to_expand_var (struct loop *loop, rtx insn)
{
  rtx set, dest, src, op1;
  struct var_to_expand *ves;
  enum machine_mode mode1, mode2;
  
  set = single_set (insn);
  if (!set)
    return NULL;
  
  dest = SET_DEST (set);
  src = SET_SRC (set);
  
  if (GET_CODE (src) != PLUS
      && GET_CODE (src) != MINUS
      && GET_CODE (src) != MULT)
    return NULL;

  /* Hmm, this is a bit paradoxical.  We know that INSN is a valid insn
     in MD.  But if there is no optab to generate the insn, we can not
     perform the variable expansion.  This can happen if an MD provides
     an insn but not a named pattern to generate it, for example to avoid
     producing code that needs additional mode switches like for x87/mmx.

     So we check have_insn_for which looks for an optab for the operation
     in SRC.  If it doesn't exist, we can't perform the expansion even
     though INSN is valid.  */
  if (!have_insn_for (GET_CODE (src), GET_MODE (src)))
    return NULL;

  if (!XEXP (src, 0))
    return NULL;
  
  op1 = XEXP (src, 0);
  
  if (!REG_P (dest)
      && !(GET_CODE (dest) == SUBREG
           && REG_P (SUBREG_REG (dest))))
    return NULL;
  
  if (!rtx_equal_p (dest, op1))
    return NULL;      
  
  if (!referenced_in_one_insn_in_loop_p (loop, dest))
    return NULL;
  
  if (rtx_referenced_p (dest, XEXP (src, 1)))
    return NULL;
  
  mode1 = GET_MODE (dest); 
  mode2 = GET_MODE (XEXP (src, 1));
  if ((FLOAT_MODE_P (mode1) 
       || FLOAT_MODE_P (mode2)) 
      && !flag_unsafe_math_optimizations) 
    return NULL;
  
  /* Record the accumulator to expand.  */
  ves = XNEW (struct var_to_expand);
  ves->insn = insn;
  ves->var_expansions = VEC_alloc (rtx, heap, 1);
  ves->reg = copy_rtx (dest);
  ves->op = GET_CODE (src);
  ves->expansion_count = 0;
  ves->reuse_expansion = 0;
  return ves; 
}

/* Determine whether there is an induction variable in INSN that
   we would like to split during unrolling.  

   I.e. replace

   i = i + 1;
   ...
   i = i + 1;
   ...
   i = i + 1;
   ...

   type chains by

   i0 = i + 1
   ...
   i = i0 + 1
   ...
   i = i0 + 2
   ...

   Return NULL if INSN contains no interesting IVs.  Otherwise, allocate 
   an IV_TO_SPLIT structure, fill it with the relevant information and return a
   pointer to it.  */

static struct iv_to_split *
analyze_iv_to_split_insn (rtx insn)
{
  rtx set, dest;
  struct rtx_iv iv;
  struct iv_to_split *ivts;
  bool ok;

  /* For now we just split the basic induction variables.  Later this may be
     extended for example by selecting also addresses of memory references.  */
  set = single_set (insn);
  if (!set)
    return NULL;

  dest = SET_DEST (set);
  if (!REG_P (dest))
    return NULL;

  if (!biv_p (insn, dest))
    return NULL;

  ok = iv_analyze_result (insn, dest, &iv);

  /* This used to be an assert under the assumption that if biv_p returns
     true that iv_analyze_result must also return true.  However, that
     assumption is not strictly correct as evidenced by pr25569.

     Returning NULL when iv_analyze_result returns false is safe and
     avoids the problems in pr25569 until the iv_analyze_* routines
     can be fixed, which is apparently hard and time consuming
     according to their author.  */
  if (! ok)
    return NULL;

  if (iv.step == const0_rtx
      || iv.mode != iv.extend_mode)
    return NULL;

  /* Record the insn to split.  */
  ivts = XNEW (struct iv_to_split);
  ivts->insn = insn;
  ivts->base_var = NULL_RTX;
  ivts->step = iv.step;
  ivts->n_loc = 1;
  ivts->loc[0] = 1;
  
  return ivts;
}

/* Determines which of insns in LOOP can be optimized.
   Return a OPT_INFO struct with the relevant hash tables filled
   with all insns to be optimized.  The FIRST_NEW_BLOCK field
   is undefined for the return value.  */

static struct opt_info *
analyze_insns_in_loop (struct loop *loop)
{
  basic_block *body, bb;
  unsigned i, num_edges = 0;
  struct opt_info *opt_info = XCNEW (struct opt_info);
  rtx insn;
  struct iv_to_split *ivts = NULL;
  struct var_to_expand *ves = NULL;
  PTR *slot1;
  PTR *slot2;
  edge *edges = get_loop_exit_edges (loop, &num_edges);
  bool can_apply = false;
  
  iv_analysis_loop_init (loop);

  body = get_loop_body (loop);

  if (flag_split_ivs_in_unroller)
    opt_info->insns_to_split = htab_create (5 * loop->num_nodes,
                                            si_info_hash, si_info_eq, free);
  
  /* Record the loop exit bb and loop preheader before the unrolling.  */
  if (!loop_preheader_edge (loop)->src)
    {
      loop_split_edge_with (loop_preheader_edge (loop), NULL_RTX);
      opt_info->loop_preheader = loop_split_edge_with (loop_preheader_edge (loop), NULL_RTX);
    }
  else
    opt_info->loop_preheader = loop_preheader_edge (loop)->src;
  
  if (num_edges == 1
      && !(edges[0]->flags & EDGE_COMPLEX))
    {
      opt_info->loop_exit = loop_split_edge_with (edges[0], NULL_RTX);
      can_apply = true;
    }
  
  if (flag_variable_expansion_in_unroller
      && can_apply)
    opt_info->insns_with_var_to_expand = htab_create (5 * loop->num_nodes,
						      ve_info_hash, ve_info_eq, free);
  
  for (i = 0; i < loop->num_nodes; i++)
    {
      bb = body[i];
      if (!dominated_by_p (CDI_DOMINATORS, loop->latch, bb))
	continue;

      FOR_BB_INSNS (bb, insn)
      {
        if (!INSN_P (insn))
          continue;
        
        if (opt_info->insns_to_split)
          ivts = analyze_iv_to_split_insn (insn);
        
        if (ivts)
          {
            slot1 = htab_find_slot (opt_info->insns_to_split, ivts, INSERT);
            *slot1 = ivts;
            continue;
          }
        
        if (opt_info->insns_with_var_to_expand)
          ves = analyze_insn_to_expand_var (loop, insn);
        
        if (ves)
          {
            slot2 = htab_find_slot (opt_info->insns_with_var_to_expand, ves, INSERT);
            *slot2 = ves;
          }
      }
    }
  
  free (edges);
  free (body);
  return opt_info;
}

/* Called just before loop duplication.  Records start of duplicated area
   to OPT_INFO.  */

static void 
opt_info_start_duplication (struct opt_info *opt_info)
{
  if (opt_info)
    opt_info->first_new_block = last_basic_block;
}

/* Determine the number of iterations between initialization of the base
   variable and the current copy (N_COPY).  N_COPIES is the total number
   of newly created copies.  UNROLLING is true if we are unrolling
   (not peeling) the loop.  */

static unsigned
determine_split_iv_delta (unsigned n_copy, unsigned n_copies, bool unrolling)
{
  if (unrolling)
    {
      /* If we are unrolling, initialization is done in the original loop
	 body (number 0).  */
      return n_copy;
    }
  else
    {
      /* If we are peeling, the copy in that the initialization occurs has
	 number 1.  The original loop (number 0) is the last.  */
      if (n_copy)
	return n_copy - 1;
      else
	return n_copies;
    }
}

/* Locate in EXPR the expression corresponding to the location recorded
   in IVTS, and return a pointer to the RTX for this location.  */

static rtx *
get_ivts_expr (rtx expr, struct iv_to_split *ivts)
{
  unsigned i;
  rtx *ret = &expr;

  for (i = 0; i < ivts->n_loc; i++)
    ret = &XEXP (*ret, ivts->loc[i]);

  return ret;
}

/* Allocate basic variable for the induction variable chain.  Callback for
   htab_traverse.  */

static int
allocate_basic_variable (void **slot, void *data ATTRIBUTE_UNUSED)
{
  struct iv_to_split *ivts = *slot;
  rtx expr = *get_ivts_expr (single_set (ivts->insn), ivts);

  ivts->base_var = gen_reg_rtx (GET_MODE (expr));

  return 1;
}

/* Insert initialization of basic variable of IVTS before INSN, taking
   the initial value from INSN.  */

static void
insert_base_initialization (struct iv_to_split *ivts, rtx insn)
{
  rtx expr = copy_rtx (*get_ivts_expr (single_set (insn), ivts));
  rtx seq;

  start_sequence ();
  expr = force_operand (expr, ivts->base_var);
  if (expr != ivts->base_var)
    emit_move_insn (ivts->base_var, expr);
  seq = get_insns ();
  end_sequence ();

  emit_insn_before (seq, insn);
}

/* Replace the use of induction variable described in IVTS in INSN
   by base variable + DELTA * step.  */

static void
split_iv (struct iv_to_split *ivts, rtx insn, unsigned delta)
{
  rtx expr, *loc, seq, incr, var;
  enum machine_mode mode = GET_MODE (ivts->base_var);
  rtx src, dest, set;

  /* Construct base + DELTA * step.  */
  if (!delta)
    expr = ivts->base_var;
  else
    {
      incr = simplify_gen_binary (MULT, mode,
				  ivts->step, gen_int_mode (delta, mode));
      expr = simplify_gen_binary (PLUS, GET_MODE (ivts->base_var),
				  ivts->base_var, incr);
    }

  /* Figure out where to do the replacement.  */
  loc = get_ivts_expr (single_set (insn), ivts);

  /* If we can make the replacement right away, we're done.  */
  if (validate_change (insn, loc, expr, 0))
    return;

  /* Otherwise, force EXPR into a register and try again.  */
  start_sequence ();
  var = gen_reg_rtx (mode);
  expr = force_operand (expr, var);
  if (expr != var)
    emit_move_insn (var, expr);
  seq = get_insns ();
  end_sequence ();
  emit_insn_before (seq, insn);
      
  if (validate_change (insn, loc, var, 0))
    return;

  /* The last chance.  Try recreating the assignment in insn
     completely from scratch.  */
  set = single_set (insn);
  gcc_assert (set);

  start_sequence ();
  *loc = var;
  src = copy_rtx (SET_SRC (set));
  dest = copy_rtx (SET_DEST (set));
  src = force_operand (src, dest);
  if (src != dest)
    emit_move_insn (dest, src);
  seq = get_insns ();
  end_sequence ();
     
  emit_insn_before (seq, insn);
  delete_insn (insn);
}


/* Return one expansion of the accumulator recorded in struct VE.  */

static rtx
get_expansion (struct var_to_expand *ve)
{
  rtx reg;
  
  if (ve->reuse_expansion == 0)
    reg = ve->reg;
  else
    reg = VEC_index (rtx, ve->var_expansions, ve->reuse_expansion - 1);
  
  if (VEC_length (rtx, ve->var_expansions) == (unsigned) ve->reuse_expansion)
    ve->reuse_expansion = 0;
  else 
    ve->reuse_expansion++;
  
  return reg;
}


/* Given INSN replace the uses of the accumulator recorded in VE 
   with a new register.  */

static void
expand_var_during_unrolling (struct var_to_expand *ve, rtx insn)
{
  rtx new_reg, set;
  bool really_new_expansion = false;
  
  set = single_set (insn);
  gcc_assert (set);
  
  /* Generate a new register only if the expansion limit has not been
     reached.  Else reuse an already existing expansion.  */
  if (PARAM_VALUE (PARAM_MAX_VARIABLE_EXPANSIONS) > ve->expansion_count)
    {
      really_new_expansion = true;
      new_reg = gen_reg_rtx (GET_MODE (ve->reg));
    }
  else
    new_reg = get_expansion (ve);

  validate_change (insn, &SET_DEST (set), new_reg, 1);
  validate_change (insn, &XEXP (SET_SRC (set), 0), new_reg, 1);
  
  if (apply_change_group ())
    if (really_new_expansion)
      {
        VEC_safe_push (rtx, heap, ve->var_expansions, new_reg);
        ve->expansion_count++;
      }
}

/* Initialize the variable expansions in loop preheader.  
   Callbacks for htab_traverse.  PLACE_P is the loop-preheader 
   basic block where the initialization of the expansions 
   should take place.  */

static int
insert_var_expansion_initialization (void **slot, void *place_p)
{
  struct var_to_expand *ve = *slot;
  basic_block place = (basic_block)place_p;
  rtx seq, var, zero_init, insn;
  unsigned i;
  
  if (VEC_length (rtx, ve->var_expansions) == 0)
    return 1;
  
  start_sequence ();
  if (ve->op == PLUS || ve->op == MINUS) 
    for (i = 0; VEC_iterate (rtx, ve->var_expansions, i, var); i++)
      {
        zero_init =  CONST0_RTX (GET_MODE (var));
        emit_move_insn (var, zero_init);
      }
  else if (ve->op == MULT)
    for (i = 0; VEC_iterate (rtx, ve->var_expansions, i, var); i++)
      {
        zero_init =  CONST1_RTX (GET_MODE (var));
        emit_move_insn (var, zero_init);
      }
  
  seq = get_insns ();
  end_sequence ();
  
  insn = BB_HEAD (place);
  while (!NOTE_INSN_BASIC_BLOCK_P (insn))
    insn = NEXT_INSN (insn);
  
  emit_insn_after (seq, insn); 
  /* Continue traversing the hash table.  */
  return 1;   
}

/*  Combine the variable expansions at the loop exit.  
    Callbacks for htab_traverse.  PLACE_P is the loop exit
    basic block where the summation of the expansions should 
    take place.  */

static int
combine_var_copies_in_loop_exit (void **slot, void *place_p)
{
  struct var_to_expand *ve = *slot;
  basic_block place = (basic_block)place_p;
  rtx sum = ve->reg;
  rtx expr, seq, var, insn;
  unsigned i;

  if (VEC_length (rtx, ve->var_expansions) == 0)
    return 1;
  
  start_sequence ();
  if (ve->op == PLUS || ve->op == MINUS)
    for (i = 0; VEC_iterate (rtx, ve->var_expansions, i, var); i++)
      {
        sum = simplify_gen_binary (PLUS, GET_MODE (ve->reg),
                                   var, sum);
      }
  else if (ve->op == MULT)
    for (i = 0; VEC_iterate (rtx, ve->var_expansions, i, var); i++)
      {
        sum = simplify_gen_binary (MULT, GET_MODE (ve->reg),
                                   var, sum);
      }
  
  expr = force_operand (sum, ve->reg);
  if (expr != ve->reg)
    emit_move_insn (ve->reg, expr);
  seq = get_insns ();
  end_sequence ();
  
  insn = BB_HEAD (place);
  while (!NOTE_INSN_BASIC_BLOCK_P (insn))
    insn = NEXT_INSN (insn);

  emit_insn_after (seq, insn);
  
  /* Continue traversing the hash table.  */
  return 1;
}

/* Apply loop optimizations in loop copies using the 
   data which gathered during the unrolling.  Structure 
   OPT_INFO record that data.
   
   UNROLLING is true if we unrolled (not peeled) the loop.
   REWRITE_ORIGINAL_BODY is true if we should also rewrite the original body of
   the loop (as it should happen in complete unrolling, but not in ordinary
   peeling of the loop).  */

static void
apply_opt_in_copies (struct opt_info *opt_info, 
                     unsigned n_copies, bool unrolling, 
                     bool rewrite_original_loop)
{
  unsigned i, delta;
  basic_block bb, orig_bb;
  rtx insn, orig_insn, next;
  struct iv_to_split ivts_templ, *ivts;
  struct var_to_expand ve_templ, *ves;
  
  /* Sanity check -- we need to put initialization in the original loop
     body.  */
  gcc_assert (!unrolling || rewrite_original_loop);
  
  /* Allocate the basic variables (i0).  */
  if (opt_info->insns_to_split)
    htab_traverse (opt_info->insns_to_split, allocate_basic_variable, NULL);
  
  for (i = opt_info->first_new_block; i < (unsigned) last_basic_block; i++)
    {
      bb = BASIC_BLOCK (i);
      orig_bb = get_bb_original (bb);
      
      /* bb->aux holds position in copy sequence initialized by
	 duplicate_loop_to_header_edge.  */
      delta = determine_split_iv_delta ((size_t)bb->aux, n_copies,
					unrolling);
      bb->aux = 0;
      orig_insn = BB_HEAD (orig_bb);
      for (insn = BB_HEAD (bb); insn != NEXT_INSN (BB_END (bb)); insn = next)
        {
          next = NEXT_INSN (insn);
          if (!INSN_P (insn))
            continue;
          
          while (!INSN_P (orig_insn))
            orig_insn = NEXT_INSN (orig_insn);
          
          ivts_templ.insn = orig_insn;
          ve_templ.insn = orig_insn;
          
          /* Apply splitting iv optimization.  */
          if (opt_info->insns_to_split)
            {
              ivts = htab_find (opt_info->insns_to_split, &ivts_templ);
              
              if (ivts)
                {
		  gcc_assert (GET_CODE (PATTERN (insn))
			      == GET_CODE (PATTERN (orig_insn)));
                  
                  if (!delta)
                    insert_base_initialization (ivts, insn);
                  split_iv (ivts, insn, delta);
                }
            }
          /* Apply variable expansion optimization.  */
          if (unrolling && opt_info->insns_with_var_to_expand)
            {
              ves = htab_find (opt_info->insns_with_var_to_expand, &ve_templ);
              if (ves)
                { 
		  gcc_assert (GET_CODE (PATTERN (insn))
			      == GET_CODE (PATTERN (orig_insn)));
                  expand_var_during_unrolling (ves, insn);
                }
            }
          orig_insn = NEXT_INSN (orig_insn);
        }
    }

  if (!rewrite_original_loop)
    return;
  
  /* Initialize the variable expansions in the loop preheader
     and take care of combining them at the loop exit.  */ 
  if (opt_info->insns_with_var_to_expand)
    {
      htab_traverse (opt_info->insns_with_var_to_expand, 
                     insert_var_expansion_initialization, 
                     opt_info->loop_preheader);
      htab_traverse (opt_info->insns_with_var_to_expand, 
                     combine_var_copies_in_loop_exit, 
                     opt_info->loop_exit);
    }
  
  /* Rewrite also the original loop body.  Find them as originals of the blocks
     in the last copied iteration, i.e. those that have
     get_bb_copy (get_bb_original (bb)) == bb.  */
  for (i = opt_info->first_new_block; i < (unsigned) last_basic_block; i++)
    {
      bb = BASIC_BLOCK (i);
      orig_bb = get_bb_original (bb);
      if (get_bb_copy (orig_bb) != bb)
	continue;
      
      delta = determine_split_iv_delta (0, n_copies, unrolling);
      for (orig_insn = BB_HEAD (orig_bb);
           orig_insn != NEXT_INSN (BB_END (bb));
           orig_insn = next)
        {
          next = NEXT_INSN (orig_insn);
          
          if (!INSN_P (orig_insn))
 	    continue;
          
          ivts_templ.insn = orig_insn;
          if (opt_info->insns_to_split)
            {
              ivts = htab_find (opt_info->insns_to_split, &ivts_templ);
              if (ivts)
                {
                  if (!delta)
                    insert_base_initialization (ivts, orig_insn);
                  split_iv (ivts, orig_insn, delta);
                  continue;
                }
            }
          
        }
    }
}

/*  Release the data structures used for the variable expansion
    optimization.  Callbacks for htab_traverse.  */

static int
release_var_copies (void **slot, void *data ATTRIBUTE_UNUSED)
{
  struct var_to_expand *ve = *slot;
  
  VEC_free (rtx, heap, ve->var_expansions);
  
  /* Continue traversing the hash table.  */
  return 1;
}

/* Release OPT_INFO.  */

static void
free_opt_info (struct opt_info *opt_info)
{
  if (opt_info->insns_to_split)
    htab_delete (opt_info->insns_to_split);
  if (opt_info->insns_with_var_to_expand)
    {
      htab_traverse (opt_info->insns_with_var_to_expand, 
                     release_var_copies, NULL);
      htab_delete (opt_info->insns_with_var_to_expand);
    }
  free (opt_info);
}
