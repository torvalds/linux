/* Branch prediction routines for the GNU compiler.
   Copyright (C) 2000, 2001, 2002, 2003, 2004, 2005
   Free Software Foundation, Inc.

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

/* References:

   [1] "Branch Prediction for Free"
       Ball and Larus; PLDI '93.
   [2] "Static Branch Frequency and Program Profile Analysis"
       Wu and Larus; MICRO-27.
   [3] "Corpus-based Static Branch Prediction"
       Calder, Grunwald, Lindsay, Martin, Mozer, and Zorn; PLDI '95.  */


#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "tree.h"
#include "rtl.h"
#include "tm_p.h"
#include "hard-reg-set.h"
#include "basic-block.h"
#include "insn-config.h"
#include "regs.h"
#include "flags.h"
#include "output.h"
#include "function.h"
#include "except.h"
#include "toplev.h"
#include "recog.h"
#include "expr.h"
#include "predict.h"
#include "coverage.h"
#include "sreal.h"
#include "params.h"
#include "target.h"
#include "cfgloop.h"
#include "tree-flow.h"
#include "ggc.h"
#include "tree-dump.h"
#include "tree-pass.h"
#include "timevar.h"
#include "tree-scalar-evolution.h"
#include "cfgloop.h"

/* real constants: 0, 1, 1-1/REG_BR_PROB_BASE, REG_BR_PROB_BASE,
		   1/REG_BR_PROB_BASE, 0.5, BB_FREQ_MAX.  */
static sreal real_zero, real_one, real_almost_one, real_br_prob_base,
	     real_inv_br_prob_base, real_one_half, real_bb_freq_max;

/* Random guesstimation given names.  */
#define PROB_VERY_UNLIKELY	(REG_BR_PROB_BASE / 100 - 1)
#define PROB_EVEN		(REG_BR_PROB_BASE / 2)
#define PROB_VERY_LIKELY	(REG_BR_PROB_BASE - PROB_VERY_UNLIKELY)
#define PROB_ALWAYS		(REG_BR_PROB_BASE)

static void combine_predictions_for_insn (rtx, basic_block);
static void dump_prediction (FILE *, enum br_predictor, int, basic_block, int);
static void estimate_loops_at_level (struct loop *, bitmap);
static void propagate_freq (struct loop *, bitmap);
static void estimate_bb_frequencies (struct loops *);
static void predict_paths_leading_to (basic_block, int *, enum br_predictor, enum prediction);
static bool last_basic_block_p (basic_block);
static void compute_function_frequency (void);
static void choose_function_section (void);
static bool can_predict_insn_p (rtx);

/* Information we hold about each branch predictor.
   Filled using information from predict.def.  */

struct predictor_info
{
  const char *const name;	/* Name used in the debugging dumps.  */
  const int hitrate;		/* Expected hitrate used by
				   predict_insn_def call.  */
  const int flags;
};

/* Use given predictor without Dempster-Shaffer theory if it matches
   using first_match heuristics.  */
#define PRED_FLAG_FIRST_MATCH 1

/* Recompute hitrate in percent to our representation.  */

#define HITRATE(VAL) ((int) ((VAL) * REG_BR_PROB_BASE + 50) / 100)

#define DEF_PREDICTOR(ENUM, NAME, HITRATE, FLAGS) {NAME, HITRATE, FLAGS},
static const struct predictor_info predictor_info[]= {
#include "predict.def"

  /* Upper bound on predictors.  */
  {NULL, 0, 0}
};
#undef DEF_PREDICTOR

/* Return true in case BB can be CPU intensive and should be optimized
   for maximal performance.  */

bool
maybe_hot_bb_p (basic_block bb)
{
  if (profile_info && flag_branch_probabilities
      && (bb->count
	  < profile_info->sum_max / PARAM_VALUE (HOT_BB_COUNT_FRACTION)))
    return false;
  if (bb->frequency < BB_FREQ_MAX / PARAM_VALUE (HOT_BB_FREQUENCY_FRACTION))
    return false;
  return true;
}

/* Return true in case BB is cold and should be optimized for size.  */

bool
probably_cold_bb_p (basic_block bb)
{
  if (profile_info && flag_branch_probabilities
      && (bb->count
	  < profile_info->sum_max / PARAM_VALUE (HOT_BB_COUNT_FRACTION)))
    return true;
  if (bb->frequency < BB_FREQ_MAX / PARAM_VALUE (HOT_BB_FREQUENCY_FRACTION))
    return true;
  return false;
}

/* Return true in case BB is probably never executed.  */
bool
probably_never_executed_bb_p (basic_block bb)
{
  if (profile_info && flag_branch_probabilities)
    return ((bb->count + profile_info->runs / 2) / profile_info->runs) == 0;
  return false;
}

/* Return true if the one of outgoing edges is already predicted by
   PREDICTOR.  */

bool
rtl_predicted_by_p (basic_block bb, enum br_predictor predictor)
{
  rtx note;
  if (!INSN_P (BB_END (bb)))
    return false;
  for (note = REG_NOTES (BB_END (bb)); note; note = XEXP (note, 1))
    if (REG_NOTE_KIND (note) == REG_BR_PRED
	&& INTVAL (XEXP (XEXP (note, 0), 0)) == (int)predictor)
      return true;
  return false;
}

/* Return true if the one of outgoing edges is already predicted by
   PREDICTOR.  */

bool
tree_predicted_by_p (basic_block bb, enum br_predictor predictor)
{
  struct edge_prediction *i;
  for (i = bb->predictions; i; i = i->ep_next)
    if (i->ep_predictor == predictor)
      return true;
  return false;
}

/* Return true when the probability of edge is reliable.
  
   The profile guessing code is good at predicting branch outcome (ie.
   taken/not taken), that is predicted right slightly over 75% of time.
   It is however notoriously poor on predicting the probability itself.
   In general the profile appear a lot flatter (with probabilities closer
   to 50%) than the reality so it is bad idea to use it to drive optimization
   such as those disabling dynamic branch prediction for well predictable
   branches.

   There are two exceptions - edges leading to noreturn edges and edges
   predicted by number of iterations heuristics are predicted well.  This macro
   should be able to distinguish those, but at the moment it simply check for
   noreturn heuristic that is only one giving probability over 99% or bellow
   1%.  In future we might want to propagate reliability information across the
   CFG if we find this information useful on multiple places.   */
static bool
probability_reliable_p (int prob)
{
  return (profile_status == PROFILE_READ
	  || (profile_status == PROFILE_GUESSED
	      && (prob <= HITRATE (1) || prob >= HITRATE (99))));
}

/* Same predicate as above, working on edges.  */
bool
edge_probability_reliable_p (edge e)
{
  return probability_reliable_p (e->probability);
}

/* Same predicate as edge_probability_reliable_p, working on notes.  */
bool
br_prob_note_reliable_p (rtx note)
{
  gcc_assert (REG_NOTE_KIND (note) == REG_BR_PROB);
  return probability_reliable_p (INTVAL (XEXP (note, 0)));
}

static void
predict_insn (rtx insn, enum br_predictor predictor, int probability)
{
  gcc_assert (any_condjump_p (insn));
  if (!flag_guess_branch_prob)
    return;

  REG_NOTES (insn)
    = gen_rtx_EXPR_LIST (REG_BR_PRED,
			 gen_rtx_CONCAT (VOIDmode,
					 GEN_INT ((int) predictor),
					 GEN_INT ((int) probability)),
			 REG_NOTES (insn));
}

/* Predict insn by given predictor.  */

void
predict_insn_def (rtx insn, enum br_predictor predictor,
		  enum prediction taken)
{
   int probability = predictor_info[(int) predictor].hitrate;

   if (taken != TAKEN)
     probability = REG_BR_PROB_BASE - probability;

   predict_insn (insn, predictor, probability);
}

/* Predict edge E with given probability if possible.  */

void
rtl_predict_edge (edge e, enum br_predictor predictor, int probability)
{
  rtx last_insn;
  last_insn = BB_END (e->src);

  /* We can store the branch prediction information only about
     conditional jumps.  */
  if (!any_condjump_p (last_insn))
    return;

  /* We always store probability of branching.  */
  if (e->flags & EDGE_FALLTHRU)
    probability = REG_BR_PROB_BASE - probability;

  predict_insn (last_insn, predictor, probability);
}

/* Predict edge E with the given PROBABILITY.  */
void
tree_predict_edge (edge e, enum br_predictor predictor, int probability)
{
  gcc_assert (profile_status != PROFILE_GUESSED);
  if ((e->src != ENTRY_BLOCK_PTR && EDGE_COUNT (e->src->succs) > 1)
      && flag_guess_branch_prob && optimize)
    {
      struct edge_prediction *i = ggc_alloc (sizeof (struct edge_prediction));

      i->ep_next = e->src->predictions;
      e->src->predictions = i;
      i->ep_probability = probability;
      i->ep_predictor = predictor;
      i->ep_edge = e;
    }
}

/* Remove all predictions on given basic block that are attached
   to edge E.  */
void
remove_predictions_associated_with_edge (edge e)
{
  if (e->src->predictions)
    {
      struct edge_prediction **prediction = &e->src->predictions;
      while (*prediction)
	{
	  if ((*prediction)->ep_edge == e)
	    *prediction = (*prediction)->ep_next;
	  else
	    prediction = &((*prediction)->ep_next);
	}
    }
}

/* Return true when we can store prediction on insn INSN.
   At the moment we represent predictions only on conditional
   jumps, not at computed jump or other complicated cases.  */
static bool
can_predict_insn_p (rtx insn)
{
  return (JUMP_P (insn)
	  && any_condjump_p (insn)
	  && EDGE_COUNT (BLOCK_FOR_INSN (insn)->succs) >= 2);
}

/* Predict edge E by given predictor if possible.  */

void
predict_edge_def (edge e, enum br_predictor predictor,
		  enum prediction taken)
{
   int probability = predictor_info[(int) predictor].hitrate;

   if (taken != TAKEN)
     probability = REG_BR_PROB_BASE - probability;

   predict_edge (e, predictor, probability);
}

/* Invert all branch predictions or probability notes in the INSN.  This needs
   to be done each time we invert the condition used by the jump.  */

void
invert_br_probabilities (rtx insn)
{
  rtx note;

  for (note = REG_NOTES (insn); note; note = XEXP (note, 1))
    if (REG_NOTE_KIND (note) == REG_BR_PROB)
      XEXP (note, 0) = GEN_INT (REG_BR_PROB_BASE - INTVAL (XEXP (note, 0)));
    else if (REG_NOTE_KIND (note) == REG_BR_PRED)
      XEXP (XEXP (note, 0), 1)
	= GEN_INT (REG_BR_PROB_BASE - INTVAL (XEXP (XEXP (note, 0), 1)));
}

/* Dump information about the branch prediction to the output file.  */

static void
dump_prediction (FILE *file, enum br_predictor predictor, int probability,
		 basic_block bb, int used)
{
  edge e;
  edge_iterator ei;

  if (!file)
    return;

  FOR_EACH_EDGE (e, ei, bb->succs)
    if (! (e->flags & EDGE_FALLTHRU))
      break;

  fprintf (file, "  %s heuristics%s: %.1f%%",
	   predictor_info[predictor].name,
	   used ? "" : " (ignored)", probability * 100.0 / REG_BR_PROB_BASE);

  if (bb->count)
    {
      fprintf (file, "  exec ");
      fprintf (file, HOST_WIDEST_INT_PRINT_DEC, bb->count);
      if (e)
	{
	  fprintf (file, " hit ");
	  fprintf (file, HOST_WIDEST_INT_PRINT_DEC, e->count);
	  fprintf (file, " (%.1f%%)", e->count * 100.0 / bb->count);
	}
    }

  fprintf (file, "\n");
}

/* We can not predict the probabilities of outgoing edges of bb.  Set them
   evenly and hope for the best.  */
static void
set_even_probabilities (basic_block bb)
{
  int nedges = 0;
  edge e;
  edge_iterator ei;

  FOR_EACH_EDGE (e, ei, bb->succs)
    if (!(e->flags & (EDGE_EH | EDGE_FAKE)))
      nedges ++;
  FOR_EACH_EDGE (e, ei, bb->succs)
    if (!(e->flags & (EDGE_EH | EDGE_FAKE)))
      e->probability = (REG_BR_PROB_BASE + nedges / 2) / nedges;
    else
      e->probability = 0;
}

/* Combine all REG_BR_PRED notes into single probability and attach REG_BR_PROB
   note if not already present.  Remove now useless REG_BR_PRED notes.  */

static void
combine_predictions_for_insn (rtx insn, basic_block bb)
{
  rtx prob_note;
  rtx *pnote;
  rtx note;
  int best_probability = PROB_EVEN;
  int best_predictor = END_PREDICTORS;
  int combined_probability = REG_BR_PROB_BASE / 2;
  int d;
  bool first_match = false;
  bool found = false;

  if (!can_predict_insn_p (insn))
    {
      set_even_probabilities (bb);
      return;
    }

  prob_note = find_reg_note (insn, REG_BR_PROB, 0);
  pnote = &REG_NOTES (insn);
  if (dump_file)
    fprintf (dump_file, "Predictions for insn %i bb %i\n", INSN_UID (insn),
	     bb->index);

  /* We implement "first match" heuristics and use probability guessed
     by predictor with smallest index.  */
  for (note = REG_NOTES (insn); note; note = XEXP (note, 1))
    if (REG_NOTE_KIND (note) == REG_BR_PRED)
      {
	int predictor = INTVAL (XEXP (XEXP (note, 0), 0));
	int probability = INTVAL (XEXP (XEXP (note, 0), 1));

	found = true;
	if (best_predictor > predictor)
	  best_probability = probability, best_predictor = predictor;

	d = (combined_probability * probability
	     + (REG_BR_PROB_BASE - combined_probability)
	     * (REG_BR_PROB_BASE - probability));

	/* Use FP math to avoid overflows of 32bit integers.  */
	if (d == 0)
	  /* If one probability is 0% and one 100%, avoid division by zero.  */
	  combined_probability = REG_BR_PROB_BASE / 2;
	else
	  combined_probability = (((double) combined_probability) * probability
				  * REG_BR_PROB_BASE / d + 0.5);
      }

  /* Decide which heuristic to use.  In case we didn't match anything,
     use no_prediction heuristic, in case we did match, use either
     first match or Dempster-Shaffer theory depending on the flags.  */

  if (predictor_info [best_predictor].flags & PRED_FLAG_FIRST_MATCH)
    first_match = true;

  if (!found)
    dump_prediction (dump_file, PRED_NO_PREDICTION,
		     combined_probability, bb, true);
  else
    {
      dump_prediction (dump_file, PRED_DS_THEORY, combined_probability,
		       bb, !first_match);
      dump_prediction (dump_file, PRED_FIRST_MATCH, best_probability,
		       bb, first_match);
    }

  if (first_match)
    combined_probability = best_probability;
  dump_prediction (dump_file, PRED_COMBINED, combined_probability, bb, true);

  while (*pnote)
    {
      if (REG_NOTE_KIND (*pnote) == REG_BR_PRED)
	{
	  int predictor = INTVAL (XEXP (XEXP (*pnote, 0), 0));
	  int probability = INTVAL (XEXP (XEXP (*pnote, 0), 1));

	  dump_prediction (dump_file, predictor, probability, bb,
			   !first_match || best_predictor == predictor);
	  *pnote = XEXP (*pnote, 1);
	}
      else
	pnote = &XEXP (*pnote, 1);
    }

  if (!prob_note)
    {
      REG_NOTES (insn)
	= gen_rtx_EXPR_LIST (REG_BR_PROB,
			     GEN_INT (combined_probability), REG_NOTES (insn));

      /* Save the prediction into CFG in case we are seeing non-degenerated
	 conditional jump.  */
      if (!single_succ_p (bb))
	{
	  BRANCH_EDGE (bb)->probability = combined_probability;
	  FALLTHRU_EDGE (bb)->probability
	    = REG_BR_PROB_BASE - combined_probability;
	}
    }
  else if (!single_succ_p (bb))
    {
      int prob = INTVAL (XEXP (prob_note, 0));

      BRANCH_EDGE (bb)->probability = prob;
      FALLTHRU_EDGE (bb)->probability = REG_BR_PROB_BASE - prob;
    }
  else
    single_succ_edge (bb)->probability = REG_BR_PROB_BASE;
}

/* Combine predictions into single probability and store them into CFG.
   Remove now useless prediction entries.  */

static void
combine_predictions_for_bb (basic_block bb)
{
  int best_probability = PROB_EVEN;
  int best_predictor = END_PREDICTORS;
  int combined_probability = REG_BR_PROB_BASE / 2;
  int d;
  bool first_match = false;
  bool found = false;
  struct edge_prediction *pred;
  int nedges = 0;
  edge e, first = NULL, second = NULL;
  edge_iterator ei;

  FOR_EACH_EDGE (e, ei, bb->succs)
    if (!(e->flags & (EDGE_EH | EDGE_FAKE)))
      {
	nedges ++;
	if (first && !second)
	  second = e;
	if (!first)
	  first = e;
      }

  /* When there is no successor or only one choice, prediction is easy. 

     We are lazy for now and predict only basic blocks with two outgoing
     edges.  It is possible to predict generic case too, but we have to
     ignore first match heuristics and do more involved combining.  Implement
     this later.  */
  if (nedges != 2)
    {
      if (!bb->count)
	set_even_probabilities (bb);
      bb->predictions = NULL;
      if (dump_file)
	fprintf (dump_file, "%i edges in bb %i predicted to even probabilities\n",
		 nedges, bb->index);
      return;
    }

  if (dump_file)
    fprintf (dump_file, "Predictions for bb %i\n", bb->index);

  /* We implement "first match" heuristics and use probability guessed
     by predictor with smallest index.  */
  for (pred = bb->predictions; pred; pred = pred->ep_next)
    {
      int predictor = pred->ep_predictor;
      int probability = pred->ep_probability;

      if (pred->ep_edge != first)
	probability = REG_BR_PROB_BASE - probability;

      found = true;
      if (best_predictor > predictor)
	best_probability = probability, best_predictor = predictor;

      d = (combined_probability * probability
	   + (REG_BR_PROB_BASE - combined_probability)
	   * (REG_BR_PROB_BASE - probability));

      /* Use FP math to avoid overflows of 32bit integers.  */
      if (d == 0)
	/* If one probability is 0% and one 100%, avoid division by zero.  */
	combined_probability = REG_BR_PROB_BASE / 2;
      else
	combined_probability = (((double) combined_probability) * probability
				* REG_BR_PROB_BASE / d + 0.5);
    }

  /* Decide which heuristic to use.  In case we didn't match anything,
     use no_prediction heuristic, in case we did match, use either
     first match or Dempster-Shaffer theory depending on the flags.  */

  if (predictor_info [best_predictor].flags & PRED_FLAG_FIRST_MATCH)
    first_match = true;

  if (!found)
    dump_prediction (dump_file, PRED_NO_PREDICTION, combined_probability, bb, true);
  else
    {
      dump_prediction (dump_file, PRED_DS_THEORY, combined_probability, bb,
		       !first_match);
      dump_prediction (dump_file, PRED_FIRST_MATCH, best_probability, bb,
		       first_match);
    }

  if (first_match)
    combined_probability = best_probability;
  dump_prediction (dump_file, PRED_COMBINED, combined_probability, bb, true);

  for (pred = bb->predictions; pred; pred = pred->ep_next)
    {
      int predictor = pred->ep_predictor;
      int probability = pred->ep_probability;

      if (pred->ep_edge != EDGE_SUCC (bb, 0))
	probability = REG_BR_PROB_BASE - probability;
      dump_prediction (dump_file, predictor, probability, bb,
		       !first_match || best_predictor == predictor);
    }
  bb->predictions = NULL;

  if (!bb->count)
    {
      first->probability = combined_probability;
      second->probability = REG_BR_PROB_BASE - combined_probability;
    }
}

/* Predict edge probabilities by exploiting loop structure.
   When RTLSIMPLELOOPS is set, attempt to count number of iterations by analyzing
   RTL otherwise use tree based approach.  */
static void
predict_loops (struct loops *loops_info, bool rtlsimpleloops)
{
  unsigned i;

  if (!rtlsimpleloops)
    scev_initialize (loops_info);

  /* Try to predict out blocks in a loop that are not part of a
     natural loop.  */
  for (i = 1; i < loops_info->num; i++)
    {
      basic_block bb, *bbs;
      unsigned j;
      unsigned n_exits;
      struct loop *loop = loops_info->parray[i];
      struct niter_desc desc;
      unsigned HOST_WIDE_INT niter;
      edge *exits;

      exits = get_loop_exit_edges (loop, &n_exits);

      if (rtlsimpleloops)
	{
	  iv_analysis_loop_init (loop);
	  find_simple_exit (loop, &desc);

	  if (desc.simple_p && desc.const_iter)
	    {
	      int prob;
	      niter = desc.niter + 1;
	      if (niter == 0)        /* We might overflow here.  */
		niter = desc.niter;
	      if (niter
		  > (unsigned int) PARAM_VALUE (PARAM_MAX_PREDICTED_ITERATIONS))
		niter = PARAM_VALUE (PARAM_MAX_PREDICTED_ITERATIONS);

	      prob = (REG_BR_PROB_BASE
		      - (REG_BR_PROB_BASE + niter /2) / niter);
	      /* Branch prediction algorithm gives 0 frequency for everything
		 after the end of loop for loop having 0 probability to finish.  */
	      if (prob == REG_BR_PROB_BASE)
		prob = REG_BR_PROB_BASE - 1;
	      predict_edge (desc.in_edge, PRED_LOOP_ITERATIONS,
			    prob);
	    }
	}
      else
	{
	  struct tree_niter_desc niter_desc;

	  for (j = 0; j < n_exits; j++)
	    {
	      tree niter = NULL;

	      if (number_of_iterations_exit (loop, exits[j], &niter_desc, false))
		niter = niter_desc.niter;
	      if (!niter || TREE_CODE (niter_desc.niter) != INTEGER_CST)
	        niter = loop_niter_by_eval (loop, exits[j]);

	      if (TREE_CODE (niter) == INTEGER_CST)
		{
		  int probability;
		  int max = PARAM_VALUE (PARAM_MAX_PREDICTED_ITERATIONS);
		  if (host_integerp (niter, 1)
		      && tree_int_cst_lt (niter,
				          build_int_cstu (NULL_TREE, max - 1)))
		    {
		      HOST_WIDE_INT nitercst = tree_low_cst (niter, 1) + 1;
		      probability = ((REG_BR_PROB_BASE + nitercst / 2)
				     / nitercst);
		    }
		  else
		    probability = ((REG_BR_PROB_BASE + max / 2) / max);

		  predict_edge (exits[j], PRED_LOOP_ITERATIONS, probability);
		}
	    }

	}
      free (exits);

      bbs = get_loop_body (loop);

      for (j = 0; j < loop->num_nodes; j++)
	{
	  int header_found = 0;
	  edge e;
	  edge_iterator ei;

	  bb = bbs[j];

	  /* Bypass loop heuristics on continue statement.  These
	     statements construct loops via "non-loop" constructs
	     in the source language and are better to be handled
	     separately.  */
	  if ((rtlsimpleloops && !can_predict_insn_p (BB_END (bb)))
	      || predicted_by_p (bb, PRED_CONTINUE))
	    continue;

	  /* Loop branch heuristics - predict an edge back to a
	     loop's head as taken.  */
	  if (bb == loop->latch)
	    {
	      e = find_edge (loop->latch, loop->header);
	      if (e)
		{
		  header_found = 1;
		  predict_edge_def (e, PRED_LOOP_BRANCH, TAKEN);
		}
	    }

	  /* Loop exit heuristics - predict an edge exiting the loop if the
	     conditional has no loop header successors as not taken.  */
	  if (!header_found)
	    {
	      /* For loop with many exits we don't want to predict all exits
	         with the pretty large probability, because if all exits are
		 considered in row, the loop would be predicted to iterate
		 almost never.  The code to divide probability by number of
		 exits is very rough.  It should compute the number of exits
		 taken in each patch through function (not the overall number
		 of exits that might be a lot higher for loops with wide switch
		 statements in them) and compute n-th square root.

		 We limit the minimal probability by 2% to avoid
		 EDGE_PROBABILITY_RELIABLE from trusting the branch prediction
		 as this was causing regression in perl benchmark containing such
		 a wide loop.  */
	        
	      int probability = ((REG_BR_PROB_BASE
		                  - predictor_info [(int) PRED_LOOP_EXIT].hitrate)
				 / n_exits);
	      if (probability < HITRATE (2))
		probability = HITRATE (2);
	      FOR_EACH_EDGE (e, ei, bb->succs)
		if (e->dest->index < NUM_FIXED_BLOCKS
		    || !flow_bb_inside_loop_p (loop, e->dest))
		  predict_edge (e, PRED_LOOP_EXIT, probability);
	    }
	}
      
      /* Free basic blocks from get_loop_body.  */
      free (bbs);
    }

  if (!rtlsimpleloops)
    {
      scev_finalize ();
      current_loops = NULL;
    }
}

/* Attempt to predict probabilities of BB outgoing edges using local
   properties.  */
static void
bb_estimate_probability_locally (basic_block bb)
{
  rtx last_insn = BB_END (bb);
  rtx cond;

  if (! can_predict_insn_p (last_insn))
    return;
  cond = get_condition (last_insn, NULL, false, false);
  if (! cond)
    return;

  /* Try "pointer heuristic."
     A comparison ptr == 0 is predicted as false.
     Similarly, a comparison ptr1 == ptr2 is predicted as false.  */
  if (COMPARISON_P (cond)
      && ((REG_P (XEXP (cond, 0)) && REG_POINTER (XEXP (cond, 0)))
	  || (REG_P (XEXP (cond, 1)) && REG_POINTER (XEXP (cond, 1)))))
    {
      if (GET_CODE (cond) == EQ)
	predict_insn_def (last_insn, PRED_POINTER, NOT_TAKEN);
      else if (GET_CODE (cond) == NE)
	predict_insn_def (last_insn, PRED_POINTER, TAKEN);
    }
  else

  /* Try "opcode heuristic."
     EQ tests are usually false and NE tests are usually true. Also,
     most quantities are positive, so we can make the appropriate guesses
     about signed comparisons against zero.  */
    switch (GET_CODE (cond))
      {
      case CONST_INT:
	/* Unconditional branch.  */
	predict_insn_def (last_insn, PRED_UNCONDITIONAL,
			  cond == const0_rtx ? NOT_TAKEN : TAKEN);
	break;

      case EQ:
      case UNEQ:
	/* Floating point comparisons appears to behave in a very
	   unpredictable way because of special role of = tests in
	   FP code.  */
	if (FLOAT_MODE_P (GET_MODE (XEXP (cond, 0))))
	  ;
	/* Comparisons with 0 are often used for booleans and there is
	   nothing useful to predict about them.  */
	else if (XEXP (cond, 1) == const0_rtx
		 || XEXP (cond, 0) == const0_rtx)
	  ;
	else
	  predict_insn_def (last_insn, PRED_OPCODE_NONEQUAL, NOT_TAKEN);
	break;

      case NE:
      case LTGT:
	/* Floating point comparisons appears to behave in a very
	   unpredictable way because of special role of = tests in
	   FP code.  */
	if (FLOAT_MODE_P (GET_MODE (XEXP (cond, 0))))
	  ;
	/* Comparisons with 0 are often used for booleans and there is
	   nothing useful to predict about them.  */
	else if (XEXP (cond, 1) == const0_rtx
		 || XEXP (cond, 0) == const0_rtx)
	  ;
	else
	  predict_insn_def (last_insn, PRED_OPCODE_NONEQUAL, TAKEN);
	break;

      case ORDERED:
	predict_insn_def (last_insn, PRED_FPOPCODE, TAKEN);
	break;

      case UNORDERED:
	predict_insn_def (last_insn, PRED_FPOPCODE, NOT_TAKEN);
	break;

      case LE:
      case LT:
	if (XEXP (cond, 1) == const0_rtx || XEXP (cond, 1) == const1_rtx
	    || XEXP (cond, 1) == constm1_rtx)
	  predict_insn_def (last_insn, PRED_OPCODE_POSITIVE, NOT_TAKEN);
	break;

      case GE:
      case GT:
	if (XEXP (cond, 1) == const0_rtx || XEXP (cond, 1) == const1_rtx
	    || XEXP (cond, 1) == constm1_rtx)
	  predict_insn_def (last_insn, PRED_OPCODE_POSITIVE, TAKEN);
	break;

      default:
	break;
      }
}

/* Set edge->probability for each successor edge of BB.  */
void
guess_outgoing_edge_probabilities (basic_block bb)
{
  bb_estimate_probability_locally (bb);
  combine_predictions_for_insn (BB_END (bb), bb);
}

/* Return constant EXPR will likely have at execution time, NULL if unknown. 
   The function is used by builtin_expect branch predictor so the evidence
   must come from this construct and additional possible constant folding.
  
   We may want to implement more involved value guess (such as value range
   propagation based prediction), but such tricks shall go to new
   implementation.  */

static tree
expr_expected_value (tree expr, bitmap visited)
{
  if (TREE_CONSTANT (expr))
    return expr;
  else if (TREE_CODE (expr) == SSA_NAME)
    {
      tree def = SSA_NAME_DEF_STMT (expr);

      /* If we were already here, break the infinite cycle.  */
      if (bitmap_bit_p (visited, SSA_NAME_VERSION (expr)))
	return NULL;
      bitmap_set_bit (visited, SSA_NAME_VERSION (expr));

      if (TREE_CODE (def) == PHI_NODE)
	{
	  /* All the arguments of the PHI node must have the same constant
	     length.  */
	  int i;
	  tree val = NULL, new_val;

	  for (i = 0; i < PHI_NUM_ARGS (def); i++)
	    {
	      tree arg = PHI_ARG_DEF (def, i);

	      /* If this PHI has itself as an argument, we cannot
		 determine the string length of this argument.  However,
		 if we can find an expected constant value for the other
		 PHI args then we can still be sure that this is
		 likely a constant.  So be optimistic and just
		 continue with the next argument.  */
	      if (arg == PHI_RESULT (def))
		continue;

	      new_val = expr_expected_value (arg, visited);
	      if (!new_val)
		return NULL;
	      if (!val)
		val = new_val;
	      else if (!operand_equal_p (val, new_val, false))
		return NULL;
	    }
	  return val;
	}
      if (TREE_CODE (def) != MODIFY_EXPR || TREE_OPERAND (def, 0) != expr)
	return NULL;
      return expr_expected_value (TREE_OPERAND (def, 1), visited);
    }
  else if (TREE_CODE (expr) == CALL_EXPR)
    {
      tree decl = get_callee_fndecl (expr);
      if (!decl)
	return NULL;
      if (DECL_BUILT_IN_CLASS (decl) == BUILT_IN_NORMAL
	  && DECL_FUNCTION_CODE (decl) == BUILT_IN_EXPECT)
	{
	  tree arglist = TREE_OPERAND (expr, 1);
	  tree val;

	  if (arglist == NULL_TREE
	      || TREE_CHAIN (arglist) == NULL_TREE)
	    return NULL; 
	  val = TREE_VALUE (TREE_CHAIN (TREE_OPERAND (expr, 1)));
	  if (TREE_CONSTANT (val))
	    return val;
	  return TREE_VALUE (TREE_CHAIN (TREE_OPERAND (expr, 1)));
	}
    }
  if (BINARY_CLASS_P (expr) || COMPARISON_CLASS_P (expr))
    {
      tree op0, op1, res;
      op0 = expr_expected_value (TREE_OPERAND (expr, 0), visited);
      if (!op0)
	return NULL;
      op1 = expr_expected_value (TREE_OPERAND (expr, 1), visited);
      if (!op1)
	return NULL;
      res = fold_build2 (TREE_CODE (expr), TREE_TYPE (expr), op0, op1);
      if (TREE_CONSTANT (res))
	return res;
      return NULL;
    }
  if (UNARY_CLASS_P (expr))
    {
      tree op0, res;
      op0 = expr_expected_value (TREE_OPERAND (expr, 0), visited);
      if (!op0)
	return NULL;
      res = fold_build1 (TREE_CODE (expr), TREE_TYPE (expr), op0);
      if (TREE_CONSTANT (res))
	return res;
      return NULL;
    }
  return NULL;
}

/* Get rid of all builtin_expect calls we no longer need.  */
static void
strip_builtin_expect (void)
{
  basic_block bb;
  FOR_EACH_BB (bb)
    {
      block_stmt_iterator bi;
      for (bi = bsi_start (bb); !bsi_end_p (bi); bsi_next (&bi))
	{
	  tree stmt = bsi_stmt (bi);
	  tree fndecl;
	  tree arglist;

	  if (TREE_CODE (stmt) == MODIFY_EXPR
	      && TREE_CODE (TREE_OPERAND (stmt, 1)) == CALL_EXPR
	      && (fndecl = get_callee_fndecl (TREE_OPERAND (stmt, 1)))
	      && DECL_BUILT_IN_CLASS (fndecl) == BUILT_IN_NORMAL
	      && DECL_FUNCTION_CODE (fndecl) == BUILT_IN_EXPECT
	      && (arglist = TREE_OPERAND (TREE_OPERAND (stmt, 1), 1))
	      && TREE_CHAIN (arglist))
	    {
	      TREE_OPERAND (stmt, 1) = TREE_VALUE (arglist);
	      update_stmt (stmt);
	    }
	}
    }
}

/* Predict using opcode of the last statement in basic block.  */
static void
tree_predict_by_opcode (basic_block bb)
{
  tree stmt = last_stmt (bb);
  edge then_edge;
  tree cond;
  tree op0;
  tree type;
  tree val;
  bitmap visited;
  edge_iterator ei;

  if (!stmt || TREE_CODE (stmt) != COND_EXPR)
    return;
  FOR_EACH_EDGE (then_edge, ei, bb->succs)
    if (then_edge->flags & EDGE_TRUE_VALUE)
      break;
  cond = TREE_OPERAND (stmt, 0);
  if (!COMPARISON_CLASS_P (cond))
    return;
  op0 = TREE_OPERAND (cond, 0);
  type = TREE_TYPE (op0);
  visited = BITMAP_ALLOC (NULL);
  val = expr_expected_value (cond, visited);
  BITMAP_FREE (visited);
  if (val)
    {
      if (integer_zerop (val))
	predict_edge_def (then_edge, PRED_BUILTIN_EXPECT, NOT_TAKEN);
      else
	predict_edge_def (then_edge, PRED_BUILTIN_EXPECT, TAKEN);
      return;
    }
  /* Try "pointer heuristic."
     A comparison ptr == 0 is predicted as false.
     Similarly, a comparison ptr1 == ptr2 is predicted as false.  */
  if (POINTER_TYPE_P (type))
    {
      if (TREE_CODE (cond) == EQ_EXPR)
	predict_edge_def (then_edge, PRED_TREE_POINTER, NOT_TAKEN);
      else if (TREE_CODE (cond) == NE_EXPR)
	predict_edge_def (then_edge, PRED_TREE_POINTER, TAKEN);
    }
  else

  /* Try "opcode heuristic."
     EQ tests are usually false and NE tests are usually true. Also,
     most quantities are positive, so we can make the appropriate guesses
     about signed comparisons against zero.  */
    switch (TREE_CODE (cond))
      {
      case EQ_EXPR:
      case UNEQ_EXPR:
	/* Floating point comparisons appears to behave in a very
	   unpredictable way because of special role of = tests in
	   FP code.  */
	if (FLOAT_TYPE_P (type))
	  ;
	/* Comparisons with 0 are often used for booleans and there is
	   nothing useful to predict about them.  */
	else if (integer_zerop (op0)
		 || integer_zerop (TREE_OPERAND (cond, 1)))
	  ;
	else
	  predict_edge_def (then_edge, PRED_TREE_OPCODE_NONEQUAL, NOT_TAKEN);
	break;

      case NE_EXPR:
      case LTGT_EXPR:
	/* Floating point comparisons appears to behave in a very
	   unpredictable way because of special role of = tests in
	   FP code.  */
	if (FLOAT_TYPE_P (type))
	  ;
	/* Comparisons with 0 are often used for booleans and there is
	   nothing useful to predict about them.  */
	else if (integer_zerop (op0)
		 || integer_zerop (TREE_OPERAND (cond, 1)))
	  ;
	else
	  predict_edge_def (then_edge, PRED_TREE_OPCODE_NONEQUAL, TAKEN);
	break;

      case ORDERED_EXPR:
	predict_edge_def (then_edge, PRED_TREE_FPOPCODE, TAKEN);
	break;

      case UNORDERED_EXPR:
	predict_edge_def (then_edge, PRED_TREE_FPOPCODE, NOT_TAKEN);
	break;

      case LE_EXPR:
      case LT_EXPR:
	if (integer_zerop (TREE_OPERAND (cond, 1))
	    || integer_onep (TREE_OPERAND (cond, 1))
	    || integer_all_onesp (TREE_OPERAND (cond, 1))
	    || real_zerop (TREE_OPERAND (cond, 1))
	    || real_onep (TREE_OPERAND (cond, 1))
	    || real_minus_onep (TREE_OPERAND (cond, 1)))
	  predict_edge_def (then_edge, PRED_TREE_OPCODE_POSITIVE, NOT_TAKEN);
	break;

      case GE_EXPR:
      case GT_EXPR:
	if (integer_zerop (TREE_OPERAND (cond, 1))
	    || integer_onep (TREE_OPERAND (cond, 1))
	    || integer_all_onesp (TREE_OPERAND (cond, 1))
	    || real_zerop (TREE_OPERAND (cond, 1))
	    || real_onep (TREE_OPERAND (cond, 1))
	    || real_minus_onep (TREE_OPERAND (cond, 1)))
	  predict_edge_def (then_edge, PRED_TREE_OPCODE_POSITIVE, TAKEN);
	break;

      default:
	break;
      }
}

/* Try to guess whether the value of return means error code.  */
static enum br_predictor
return_prediction (tree val, enum prediction *prediction)
{
  /* VOID.  */
  if (!val)
    return PRED_NO_PREDICTION;
  /* Different heuristics for pointers and scalars.  */
  if (POINTER_TYPE_P (TREE_TYPE (val)))
    {
      /* NULL is usually not returned.  */
      if (integer_zerop (val))
	{
	  *prediction = NOT_TAKEN;
	  return PRED_NULL_RETURN;
	}
    }
  else if (INTEGRAL_TYPE_P (TREE_TYPE (val)))
    {
      /* Negative return values are often used to indicate
         errors.  */
      if (TREE_CODE (val) == INTEGER_CST
	  && tree_int_cst_sgn (val) < 0)
	{
	  *prediction = NOT_TAKEN;
	  return PRED_NEGATIVE_RETURN;
	}
      /* Constant return values seems to be commonly taken.
         Zero/one often represent booleans so exclude them from the
	 heuristics.  */
      if (TREE_CONSTANT (val)
	  && (!integer_zerop (val) && !integer_onep (val)))
	{
	  *prediction = TAKEN;
	  return PRED_NEGATIVE_RETURN;
	}
    }
  return PRED_NO_PREDICTION;
}

/* Find the basic block with return expression and look up for possible
   return value trying to apply RETURN_PREDICTION heuristics.  */
static void
apply_return_prediction (int *heads)
{
  tree return_stmt = NULL;
  tree return_val;
  edge e;
  tree phi;
  int phi_num_args, i;
  enum br_predictor pred;
  enum prediction direction;
  edge_iterator ei;

  FOR_EACH_EDGE (e, ei, EXIT_BLOCK_PTR->preds)
    {
      return_stmt = last_stmt (e->src);
      if (TREE_CODE (return_stmt) == RETURN_EXPR)
	break;
    }
  if (!e)
    return;
  return_val = TREE_OPERAND (return_stmt, 0);
  if (!return_val)
    return;
  if (TREE_CODE (return_val) == MODIFY_EXPR)
    return_val = TREE_OPERAND (return_val, 1);
  if (TREE_CODE (return_val) != SSA_NAME
      || !SSA_NAME_DEF_STMT (return_val)
      || TREE_CODE (SSA_NAME_DEF_STMT (return_val)) != PHI_NODE)
    return;
  for (phi = SSA_NAME_DEF_STMT (return_val); phi; phi = PHI_CHAIN (phi))
    if (PHI_RESULT (phi) == return_val)
      break;
  if (!phi)
    return;
  phi_num_args = PHI_NUM_ARGS (phi);
  pred = return_prediction (PHI_ARG_DEF (phi, 0), &direction);

  /* Avoid the degenerate case where all return values form the function
     belongs to same category (ie they are all positive constants)
     so we can hardly say something about them.  */
  for (i = 1; i < phi_num_args; i++)
    if (pred != return_prediction (PHI_ARG_DEF (phi, i), &direction))
      break;
  if (i != phi_num_args)
    for (i = 0; i < phi_num_args; i++)
      {
	pred = return_prediction (PHI_ARG_DEF (phi, i), &direction);
	if (pred != PRED_NO_PREDICTION)
	  predict_paths_leading_to (PHI_ARG_EDGE (phi, i)->src, heads, pred,
				    direction);
      }
}

/* Look for basic block that contains unlikely to happen events
   (such as noreturn calls) and mark all paths leading to execution
   of this basic blocks as unlikely.  */

static void
tree_bb_level_predictions (void)
{
  basic_block bb;
  int *heads;

  heads = XNEWVEC (int, last_basic_block);
  memset (heads, ENTRY_BLOCK, sizeof (int) * last_basic_block);
  heads[ENTRY_BLOCK_PTR->next_bb->index] = last_basic_block;

  apply_return_prediction (heads);

  FOR_EACH_BB (bb)
    {
      block_stmt_iterator bsi = bsi_last (bb);

      for (bsi = bsi_start (bb); !bsi_end_p (bsi); bsi_next (&bsi))
	{
	  tree stmt = bsi_stmt (bsi);
	  switch (TREE_CODE (stmt))
	    {
	      case MODIFY_EXPR:
		if (TREE_CODE (TREE_OPERAND (stmt, 1)) == CALL_EXPR)
		  {
		    stmt = TREE_OPERAND (stmt, 1);
		    goto call_expr;
		  }
		break;
	      case CALL_EXPR:
call_expr:;
		if (call_expr_flags (stmt) & ECF_NORETURN)
		  predict_paths_leading_to (bb, heads, PRED_NORETURN,
		      			    NOT_TAKEN);
		break;
	      default:
		break;
	    }
	}
    }

  free (heads);
}

/* Predict branch probabilities and estimate profile of the tree CFG.  */
static unsigned int
tree_estimate_probability (void)
{
  basic_block bb;
  struct loops loops_info;

  flow_loops_find (&loops_info);
  if (dump_file && (dump_flags & TDF_DETAILS))
    flow_loops_dump (&loops_info, dump_file, NULL, 0);

  add_noreturn_fake_exit_edges ();
  connect_infinite_loops_to_exit ();
  calculate_dominance_info (CDI_DOMINATORS);
  calculate_dominance_info (CDI_POST_DOMINATORS);

  tree_bb_level_predictions ();

  mark_irreducible_loops (&loops_info);
  predict_loops (&loops_info, false);

  FOR_EACH_BB (bb)
    {
      edge e;
      edge_iterator ei;

      FOR_EACH_EDGE (e, ei, bb->succs)
	{
	  /* Predict early returns to be probable, as we've already taken
	     care for error returns and other cases are often used for
	     fast paths through function.  */
	  if (e->dest == EXIT_BLOCK_PTR
	      && TREE_CODE (last_stmt (bb)) == RETURN_EXPR
	      && !single_pred_p (bb))
	    {
	      edge e1;
	      edge_iterator ei1;

	      FOR_EACH_EDGE (e1, ei1, bb->preds)
	      	if (!predicted_by_p (e1->src, PRED_NULL_RETURN)
		    && !predicted_by_p (e1->src, PRED_CONST_RETURN)
		    && !predicted_by_p (e1->src, PRED_NEGATIVE_RETURN)
		    && !last_basic_block_p (e1->src))
		  predict_edge_def (e1, PRED_TREE_EARLY_RETURN, NOT_TAKEN);
	    }

	  /* Look for block we are guarding (ie we dominate it,
	     but it doesn't postdominate us).  */
	  if (e->dest != EXIT_BLOCK_PTR && e->dest != bb
	      && dominated_by_p (CDI_DOMINATORS, e->dest, e->src)
	      && !dominated_by_p (CDI_POST_DOMINATORS, e->src, e->dest))
	    {
	      block_stmt_iterator bi;

	      /* The call heuristic claims that a guarded function call
		 is improbable.  This is because such calls are often used
		 to signal exceptional situations such as printing error
		 messages.  */
	      for (bi = bsi_start (e->dest); !bsi_end_p (bi);
		   bsi_next (&bi))
		{
		  tree stmt = bsi_stmt (bi);
		  if ((TREE_CODE (stmt) == CALL_EXPR
		       || (TREE_CODE (stmt) == MODIFY_EXPR
			   && TREE_CODE (TREE_OPERAND (stmt, 1)) == CALL_EXPR))
		      /* Constant and pure calls are hardly used to signalize
			 something exceptional.  */
		      && TREE_SIDE_EFFECTS (stmt))
		    {
		      predict_edge_def (e, PRED_CALL, NOT_TAKEN);
		      break;
		    }
		}
	    }
	}
      tree_predict_by_opcode (bb);
    }
  FOR_EACH_BB (bb)
    combine_predictions_for_bb (bb);

  strip_builtin_expect ();
  estimate_bb_frequencies (&loops_info);
  free_dominance_info (CDI_POST_DOMINATORS);
  remove_fake_exit_edges ();
  flow_loops_free (&loops_info);
  if (dump_file && (dump_flags & TDF_DETAILS))
    dump_tree_cfg (dump_file, dump_flags);
  if (profile_status == PROFILE_ABSENT)
    profile_status = PROFILE_GUESSED;
  return 0;
}

/* __builtin_expect dropped tokens into the insn stream describing expected
   values of registers.  Generate branch probabilities based off these
   values.  */

void
expected_value_to_br_prob (void)
{
  rtx insn, cond, ev = NULL_RTX, ev_reg = NULL_RTX;

  for (insn = get_insns (); insn ; insn = NEXT_INSN (insn))
    {
      switch (GET_CODE (insn))
	{
	case NOTE:
	  /* Look for expected value notes.  */
	  if (NOTE_LINE_NUMBER (insn) == NOTE_INSN_EXPECTED_VALUE)
	    {
	      ev = NOTE_EXPECTED_VALUE (insn);
	      ev_reg = XEXP (ev, 0);
	      delete_insn (insn);
	    }
	  continue;

	case CODE_LABEL:
	  /* Never propagate across labels.  */
	  ev = NULL_RTX;
	  continue;

	case JUMP_INSN:
	  /* Look for simple conditional branches.  If we haven't got an
	     expected value yet, no point going further.  */
	  if (!JUMP_P (insn) || ev == NULL_RTX
	      || ! any_condjump_p (insn))
	    continue;
	  break;

	default:
	  /* Look for insns that clobber the EV register.  */
	  if (ev && reg_set_p (ev_reg, insn))
	    ev = NULL_RTX;
	  continue;
	}

      /* Collect the branch condition, hopefully relative to EV_REG.  */
      /* ???  At present we'll miss things like
		(expected_value (eq r70 0))
		(set r71 -1)
		(set r80 (lt r70 r71))
		(set pc (if_then_else (ne r80 0) ...))
	 as canonicalize_condition will render this to us as
		(lt r70, r71)
	 Could use cselib to try and reduce this further.  */
      cond = XEXP (SET_SRC (pc_set (insn)), 0);
      cond = canonicalize_condition (insn, cond, 0, NULL, ev_reg,
				     false, false);
      if (! cond || XEXP (cond, 0) != ev_reg
	  || GET_CODE (XEXP (cond, 1)) != CONST_INT)
	continue;

      /* Substitute and simplify.  Given that the expression we're
	 building involves two constants, we should wind up with either
	 true or false.  */
      cond = gen_rtx_fmt_ee (GET_CODE (cond), VOIDmode,
			     XEXP (ev, 1), XEXP (cond, 1));
      cond = simplify_rtx (cond);

      /* Turn the condition into a scaled branch probability.  */
      gcc_assert (cond == const_true_rtx || cond == const0_rtx);
      predict_insn_def (insn, PRED_BUILTIN_EXPECT,
		        cond == const_true_rtx ? TAKEN : NOT_TAKEN);
    }
}

/* Check whether this is the last basic block of function.  Commonly
   there is one extra common cleanup block.  */
static bool
last_basic_block_p (basic_block bb)
{
  if (bb == EXIT_BLOCK_PTR)
    return false;

  return (bb->next_bb == EXIT_BLOCK_PTR
	  || (bb->next_bb->next_bb == EXIT_BLOCK_PTR
	      && single_succ_p (bb)
	      && single_succ (bb)->next_bb == EXIT_BLOCK_PTR));
}

/* Sets branch probabilities according to PREDiction and
   FLAGS. HEADS[bb->index] should be index of basic block in that we
   need to alter branch predictions (i.e. the first of our dominators
   such that we do not post-dominate it) (but we fill this information
   on demand, so -1 may be there in case this was not needed yet).  */

static void
predict_paths_leading_to (basic_block bb, int *heads, enum br_predictor pred,
			  enum prediction taken)
{
  edge e;
  edge_iterator ei;
  int y;

  if (heads[bb->index] == ENTRY_BLOCK)
    {
      /* This is first time we need this field in heads array; so
         find first dominator that we do not post-dominate (we are
         using already known members of heads array).  */
      basic_block ai = bb;
      basic_block next_ai = get_immediate_dominator (CDI_DOMINATORS, bb);
      int head;

      while (heads[next_ai->index] == ENTRY_BLOCK)
	{
	  if (!dominated_by_p (CDI_POST_DOMINATORS, next_ai, bb))
	    break;
	  heads[next_ai->index] = ai->index;
	  ai = next_ai;
	  next_ai = get_immediate_dominator (CDI_DOMINATORS, next_ai);
	}
      if (!dominated_by_p (CDI_POST_DOMINATORS, next_ai, bb))
	head = next_ai->index;
      else
	head = heads[next_ai->index];
      while (next_ai != bb)
	{
	  next_ai = ai;
	  ai = BASIC_BLOCK (heads[ai->index]);
	  heads[next_ai->index] = head;
	}
    }
  y = heads[bb->index];

  /* Now find the edge that leads to our branch and aply the prediction.  */

  if (y == last_basic_block)
    return;
  FOR_EACH_EDGE (e, ei, BASIC_BLOCK (y)->succs)
    if (e->dest->index >= NUM_FIXED_BLOCKS
	&& dominated_by_p (CDI_POST_DOMINATORS, e->dest, bb))
      predict_edge_def (e, pred, taken);
}

/* This is used to carry information about basic blocks.  It is
   attached to the AUX field of the standard CFG block.  */

typedef struct block_info_def
{
  /* Estimated frequency of execution of basic_block.  */
  sreal frequency;

  /* To keep queue of basic blocks to process.  */
  basic_block next;

  /* Number of predecessors we need to visit first.  */
  int npredecessors;
} *block_info;

/* Similar information for edges.  */
typedef struct edge_info_def
{
  /* In case edge is a loopback edge, the probability edge will be reached
     in case header is.  Estimated number of iterations of the loop can be
     then computed as 1 / (1 - back_edge_prob).  */
  sreal back_edge_prob;
  /* True if the edge is a loopback edge in the natural loop.  */
  unsigned int back_edge:1;
} *edge_info;

#define BLOCK_INFO(B)	((block_info) (B)->aux)
#define EDGE_INFO(E)	((edge_info) (E)->aux)

/* Helper function for estimate_bb_frequencies.
   Propagate the frequencies for LOOP.  */

static void
propagate_freq (struct loop *loop, bitmap tovisit)
{
  basic_block head = loop->header;
  basic_block bb;
  basic_block last;
  unsigned i;
  edge e;
  basic_block nextbb;
  bitmap_iterator bi;

  /* For each basic block we need to visit count number of his predecessors
     we need to visit first.  */
  EXECUTE_IF_SET_IN_BITMAP (tovisit, 0, i, bi)
    {
      edge_iterator ei;
      int count = 0;

       /* The outermost "loop" includes the exit block, which we can not
	  look up via BASIC_BLOCK.  Detect this and use EXIT_BLOCK_PTR
	  directly.  Do the same for the entry block.  */
      bb = BASIC_BLOCK (i);

      FOR_EACH_EDGE (e, ei, bb->preds)
	{
	  bool visit = bitmap_bit_p (tovisit, e->src->index);

	  if (visit && !(e->flags & EDGE_DFS_BACK))
	    count++;
	  else if (visit && dump_file && !EDGE_INFO (e)->back_edge)
	    fprintf (dump_file,
		     "Irreducible region hit, ignoring edge to %i->%i\n",
		     e->src->index, bb->index);
	}
      BLOCK_INFO (bb)->npredecessors = count;
    }

  memcpy (&BLOCK_INFO (head)->frequency, &real_one, sizeof (real_one));
  last = head;
  for (bb = head; bb; bb = nextbb)
    {
      edge_iterator ei;
      sreal cyclic_probability, frequency;

      memcpy (&cyclic_probability, &real_zero, sizeof (real_zero));
      memcpy (&frequency, &real_zero, sizeof (real_zero));

      nextbb = BLOCK_INFO (bb)->next;
      BLOCK_INFO (bb)->next = NULL;

      /* Compute frequency of basic block.  */
      if (bb != head)
	{
#ifdef ENABLE_CHECKING
	  FOR_EACH_EDGE (e, ei, bb->preds)
	    gcc_assert (!bitmap_bit_p (tovisit, e->src->index)
			|| (e->flags & EDGE_DFS_BACK));
#endif

	  FOR_EACH_EDGE (e, ei, bb->preds)
	    if (EDGE_INFO (e)->back_edge)
	      {
		sreal_add (&cyclic_probability, &cyclic_probability,
			   &EDGE_INFO (e)->back_edge_prob);
	      }
	    else if (!(e->flags & EDGE_DFS_BACK))
	      {
		sreal tmp;

		/*  frequency += (e->probability
				  * BLOCK_INFO (e->src)->frequency /
				  REG_BR_PROB_BASE);  */

		sreal_init (&tmp, e->probability, 0);
		sreal_mul (&tmp, &tmp, &BLOCK_INFO (e->src)->frequency);
		sreal_mul (&tmp, &tmp, &real_inv_br_prob_base);
		sreal_add (&frequency, &frequency, &tmp);
	      }

	  if (sreal_compare (&cyclic_probability, &real_zero) == 0)
	    {
	      memcpy (&BLOCK_INFO (bb)->frequency, &frequency,
		      sizeof (frequency));
	    }
	  else
	    {
	      if (sreal_compare (&cyclic_probability, &real_almost_one) > 0)
		{
		  memcpy (&cyclic_probability, &real_almost_one,
			  sizeof (real_almost_one));
		}

	      /* BLOCK_INFO (bb)->frequency = frequency
					      / (1 - cyclic_probability) */

	      sreal_sub (&cyclic_probability, &real_one, &cyclic_probability);
	      sreal_div (&BLOCK_INFO (bb)->frequency,
			 &frequency, &cyclic_probability);
	    }
	}

      bitmap_clear_bit (tovisit, bb->index);

      e = find_edge (bb, head);
      if (e)
	{
	  sreal tmp;
	    
	  /* EDGE_INFO (e)->back_edge_prob
	     = ((e->probability * BLOCK_INFO (bb)->frequency)
	     / REG_BR_PROB_BASE); */
	    
	  sreal_init (&tmp, e->probability, 0);
	  sreal_mul (&tmp, &tmp, &BLOCK_INFO (bb)->frequency);
	  sreal_mul (&EDGE_INFO (e)->back_edge_prob,
		     &tmp, &real_inv_br_prob_base);
	}

      /* Propagate to successor blocks.  */
      FOR_EACH_EDGE (e, ei, bb->succs)
	if (!(e->flags & EDGE_DFS_BACK)
	    && BLOCK_INFO (e->dest)->npredecessors)
	  {
	    BLOCK_INFO (e->dest)->npredecessors--;
	    if (!BLOCK_INFO (e->dest)->npredecessors)
	      {
		if (!nextbb)
		  nextbb = e->dest;
		else
		  BLOCK_INFO (last)->next = e->dest;
		
		last = e->dest;
	      }
	  }
    }
}

/* Estimate probabilities of loopback edges in loops at same nest level.  */

static void
estimate_loops_at_level (struct loop *first_loop, bitmap tovisit)
{
  struct loop *loop;

  for (loop = first_loop; loop; loop = loop->next)
    {
      edge e;
      basic_block *bbs;
      unsigned i;

      estimate_loops_at_level (loop->inner, tovisit);

      /* Do not do this for dummy function loop.  */
      if (EDGE_COUNT (loop->latch->succs) > 0)
	{
	  /* Find current loop back edge and mark it.  */
	  e = loop_latch_edge (loop);
	  EDGE_INFO (e)->back_edge = 1;
       }

      bbs = get_loop_body (loop);
      for (i = 0; i < loop->num_nodes; i++)
	bitmap_set_bit (tovisit, bbs[i]->index);
      free (bbs);
      propagate_freq (loop, tovisit);
    }
}

/* Convert counts measured by profile driven feedback to frequencies.
   Return nonzero iff there was any nonzero execution count.  */

int
counts_to_freqs (void)
{
  gcov_type count_max, true_count_max = 0;
  basic_block bb;

  FOR_EACH_BB (bb)
    true_count_max = MAX (bb->count, true_count_max);

  count_max = MAX (true_count_max, 1);
  FOR_BB_BETWEEN (bb, ENTRY_BLOCK_PTR, NULL, next_bb)
    bb->frequency = (bb->count * BB_FREQ_MAX + count_max / 2) / count_max;
  return true_count_max;
}

/* Return true if function is likely to be expensive, so there is no point to
   optimize performance of prologue, epilogue or do inlining at the expense
   of code size growth.  THRESHOLD is the limit of number of instructions
   function can execute at average to be still considered not expensive.  */

bool
expensive_function_p (int threshold)
{
  unsigned int sum = 0;
  basic_block bb;
  unsigned int limit;

  /* We can not compute accurately for large thresholds due to scaled
     frequencies.  */
  gcc_assert (threshold <= BB_FREQ_MAX);

  /* Frequencies are out of range.  This either means that function contains
     internal loop executing more than BB_FREQ_MAX times or profile feedback
     is available and function has not been executed at all.  */
  if (ENTRY_BLOCK_PTR->frequency == 0)
    return true;

  /* Maximally BB_FREQ_MAX^2 so overflow won't happen.  */
  limit = ENTRY_BLOCK_PTR->frequency * threshold;
  FOR_EACH_BB (bb)
    {
      rtx insn;

      for (insn = BB_HEAD (bb); insn != NEXT_INSN (BB_END (bb));
	   insn = NEXT_INSN (insn))
	if (active_insn_p (insn))
	  {
	    sum += bb->frequency;
	    if (sum > limit)
	      return true;
	}
    }

  return false;
}

/* Estimate basic blocks frequency by given branch probabilities.  */

static void
estimate_bb_frequencies (struct loops *loops)
{
  basic_block bb;
  sreal freq_max;

  if (!flag_branch_probabilities || !counts_to_freqs ())
    {
      static int real_values_initialized = 0;
      bitmap tovisit;

      if (!real_values_initialized)
        {
	  real_values_initialized = 1;
	  sreal_init (&real_zero, 0, 0);
	  sreal_init (&real_one, 1, 0);
	  sreal_init (&real_br_prob_base, REG_BR_PROB_BASE, 0);
	  sreal_init (&real_bb_freq_max, BB_FREQ_MAX, 0);
	  sreal_init (&real_one_half, 1, -1);
	  sreal_div (&real_inv_br_prob_base, &real_one, &real_br_prob_base);
	  sreal_sub (&real_almost_one, &real_one, &real_inv_br_prob_base);
	}

      mark_dfs_back_edges ();

      single_succ_edge (ENTRY_BLOCK_PTR)->probability = REG_BR_PROB_BASE;

      /* Set up block info for each basic block.  */
      tovisit = BITMAP_ALLOC (NULL);
      alloc_aux_for_blocks (sizeof (struct block_info_def));
      alloc_aux_for_edges (sizeof (struct edge_info_def));
      FOR_BB_BETWEEN (bb, ENTRY_BLOCK_PTR, NULL, next_bb)
	{
	  edge e;
	  edge_iterator ei;

	  FOR_EACH_EDGE (e, ei, bb->succs)
	    {
	      sreal_init (&EDGE_INFO (e)->back_edge_prob, e->probability, 0);
	      sreal_mul (&EDGE_INFO (e)->back_edge_prob,
			 &EDGE_INFO (e)->back_edge_prob,
			 &real_inv_br_prob_base);
	    }
	}

      /* First compute probabilities locally for each loop from innermost
         to outermost to examine probabilities for back edges.  */
      estimate_loops_at_level (loops->tree_root, tovisit);

      memcpy (&freq_max, &real_zero, sizeof (real_zero));
      FOR_EACH_BB (bb)
	if (sreal_compare (&freq_max, &BLOCK_INFO (bb)->frequency) < 0)
	  memcpy (&freq_max, &BLOCK_INFO (bb)->frequency, sizeof (freq_max));

      sreal_div (&freq_max, &real_bb_freq_max, &freq_max);
      FOR_BB_BETWEEN (bb, ENTRY_BLOCK_PTR, NULL, next_bb)
	{
	  sreal tmp;

	  sreal_mul (&tmp, &BLOCK_INFO (bb)->frequency, &freq_max);
	  sreal_add (&tmp, &tmp, &real_one_half);
	  bb->frequency = sreal_to_int (&tmp);
	}

      free_aux_for_blocks ();
      free_aux_for_edges ();
      BITMAP_FREE (tovisit);
    }
  compute_function_frequency ();
  if (flag_reorder_functions)
    choose_function_section ();
}

/* Decide whether function is hot, cold or unlikely executed.  */
static void
compute_function_frequency (void)
{
  basic_block bb;

  if (!profile_info || !flag_branch_probabilities)
    return;
  cfun->function_frequency = FUNCTION_FREQUENCY_UNLIKELY_EXECUTED;
  FOR_EACH_BB (bb)
    {
      if (maybe_hot_bb_p (bb))
	{
	  cfun->function_frequency = FUNCTION_FREQUENCY_HOT;
	  return;
	}
      if (!probably_never_executed_bb_p (bb))
	cfun->function_frequency = FUNCTION_FREQUENCY_NORMAL;
    }
}

/* Choose appropriate section for the function.  */
static void
choose_function_section (void)
{
  if (DECL_SECTION_NAME (current_function_decl)
      || !targetm.have_named_sections
      /* Theoretically we can split the gnu.linkonce text section too,
	 but this requires more work as the frequency needs to match
	 for all generated objects so we need to merge the frequency
	 of all instances.  For now just never set frequency for these.  */
      || DECL_ONE_ONLY (current_function_decl))
    return;

  /* If we are doing the partitioning optimization, let the optimization
     choose the correct section into which to put things.  */

  if (flag_reorder_blocks_and_partition)
    return;

  if (cfun->function_frequency == FUNCTION_FREQUENCY_HOT)
    DECL_SECTION_NAME (current_function_decl) =
      build_string (strlen (HOT_TEXT_SECTION_NAME), HOT_TEXT_SECTION_NAME);
  if (cfun->function_frequency == FUNCTION_FREQUENCY_UNLIKELY_EXECUTED)
    DECL_SECTION_NAME (current_function_decl) =
      build_string (strlen (UNLIKELY_EXECUTED_TEXT_SECTION_NAME),
		    UNLIKELY_EXECUTED_TEXT_SECTION_NAME);
}

static bool
gate_estimate_probability (void)
{
  return flag_guess_branch_prob;
}

struct tree_opt_pass pass_profile = 
{
  "profile",				/* name */
  gate_estimate_probability,		/* gate */
  tree_estimate_probability,		/* execute */
  NULL,					/* sub */
  NULL,					/* next */
  0,					/* static_pass_number */
  TV_BRANCH_PROB,			/* tv_id */
  PROP_cfg,				/* properties_required */
  0,					/* properties_provided */
  0,					/* properties_destroyed */
  0,					/* todo_flags_start */
  TODO_ggc_collect | TODO_verify_ssa,			/* todo_flags_finish */
  0					/* letter */
};
