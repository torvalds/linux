/* RTL factoring (sequence abstraction).
   Copyright (C) 2004, 2005, 2006 Free Software Foundation, Inc.

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
#include "obstack.h"
#include "basic-block.h"
#include "resource.h"
#include "flags.h"
#include "ggc.h"
#include "regs.h"
#include "params.h"
#include "expr.h"
#include "tm_p.h"
#include "tree-pass.h"
#include "tree-flow.h"
#include "timevar.h"
#include "output.h"
#include "addresses.h"

/* Sequence abstraction:

   It is a size optimization method. The main idea of this technique is to
   find identical sequences of code, which can be turned into procedures and
   then replace all occurrences with calls to the newly created subroutine.
   It is kind of an opposite of function inlining.

   There are four major parts of this file:

   sequence fingerprint
     In order to avoid the comparison of every insn with every other, hash
     value will be designed for every insn by COMPUTE_HASH.
     These hash values are used for grouping the sequence candidates. So
     we only need to compare every insn with every other in same hash group.

     FILL_HASH_BUCKET creates all hash values and stores into HASH_BUCKETS.
     The result is used by COLLECT_PATTERN_SEQS.

   code matching
     In code matching the algorithm compares every two possible sequence
     candidates which last insns are in the same hash group. If these
     sequences are identical they will be stored and do further searches for
     finding more sequences which are identical with the first one.

     COLLECT_PATTERN_SEQS does the code matching and stores the results into
     PATTERN_SEQS.

   gain computation
     This part computes the gain of abstraction which could be archived when
     turning the pattern sequence into a pseudo-function and its matching
     sequences into pseudo-calls. After it the most effective sequences will
     be marked for abstraction.

     RECOMPUTE_GAIN does the gain computation. The sequences with the maximum
     gain is on the top of PATTERN_SEQS.

   abstract code
     This part turns the pattern sequence into a pseudo-function and its
     matching sequences into pseudo-calls.

     ABSTRACT_BEST_SEQ does the code merging.


   C code example:

   // Original source            // After sequence abstraction
   {                             {
                                   void *jump_label;
     ...                           ...
                                   jump_label = &&exit_0;
                                 entry_0:
     I0;                           I0;
     I1;                           I1;
     I2;                           I2;
     I3;                           I3;
                                   goto *jump_label;
                                 exit_0:
     ...                           ...
                                   jump_label = &&exit_1;
                                 goto entry_0;
     I0;
     I1;
     I2;
     I3;
                                 exit_1:
     ...                           ...
                                   jump_label = &&exit_2;
                                   goto entry_0;
     I0;
     I1;
     I2;
     I3;
                                 exit_2:
     ...                           ...
                                   jump_label = &&exit_3;
                                   goto entry_0;
     I0;
     I1;
     I2;
     I3;
                                exit_3:
     ...                           ...
   }                             }


   TODO:
   - Use REG_ALLOC_ORDER when choosing link register.
   - Handle JUMP_INSNs. Also handle volatile function calls (handle them
     similar to unconditional jumps.)
   - Test command line option -fpic.
*/

/* Predicate yielding nonzero iff X is an abstractable insn.  Non-jump insns are
   abstractable.  */
#define ABSTRACTABLE_INSN_P(X) (INSN_P (X) && !JUMP_P (X))

/* First parameter of the htab_create function call.  */
#define HASH_INIT 1023

/* Multiplier for cost of sequence call to avoid abstracting short
   sequences.  */
#ifndef SEQ_CALL_COST_MULTIPLIER
#define SEQ_CALL_COST_MULTIPLIER 2
#endif

/* Recomputes the cost of MSEQ pattern/matching sequence.  */
#define RECOMPUTE_COST(SEQ)                                 \
{                                                           \
  int l;                                                    \
  rtx x = SEQ->insn;                                        \
  SEQ->cost = 0;                                            \
  for (l = 0; l < SEQ->abstracted_length; l++)              \
    {                                                       \
      SEQ->cost += compute_rtx_cost (x);                    \
      x = prev_insn_in_block (x);                           \
    }                                                       \
}

/* A sequence matching a pattern sequence.  */
typedef struct matching_seq_def
{
  /* The last insn in the matching sequence.  */
  rtx insn;

  /* Index of INSN instruction.  */
  unsigned long idx;

  /* The number of insns matching in this sequence and the pattern sequence.
   */
  int matching_length;

  /* The number of insns selected to abstract from this sequence. Less than
     or equal to MATCHING_LENGTH.  */
  int abstracted_length;

  /* The cost of the sequence.  */
  int cost;

  /* The next sequence in the chain matching the same pattern.  */
  struct matching_seq_def *next_matching_seq;
} *matching_seq;


/* A pattern instruction sequence.  */
typedef struct pattern_seq_def
{
  /* The last insn in the pattern sequence.  */
  rtx insn;

  /* Index of INSN instruction.  */
  unsigned long idx;

  /* The gain of transforming the pattern sequence into a pseudo-function and
     the matching sequences into pseudo-calls.  */
  int gain;

  /* The maximum of the ABSTRACTED_LENGTH of the matching sequences.  */
  int abstracted_length;

  /* The cost of the sequence.  */
  int cost;

  /* The register used to hold the return address during the pseudo-call.  */
  rtx link_reg;

  /* The sequences matching this pattern.  */
  matching_seq matching_seqs;

  /* The next pattern sequence in the chain.  */
  struct pattern_seq_def *next_pattern_seq;
} *pattern_seq;


/* A block of a pattern sequence.  */
typedef struct seq_block_def
{
  /* The number of insns in the block.  */
  int length;

  /* The code_label of the block.  */
  rtx label;

  /* The sequences entering the pattern sequence at LABEL.  */
  matching_seq matching_seqs;

  /* The next block in the chain. The blocks are sorted by LENGTH in
     ascending order.  */
  struct seq_block_def *next_seq_block;
} *seq_block;

/* Contains same sequence candidates for further searching.  */
typedef struct hash_bucket_def
{
  /* The hash value of the group.  */
  unsigned int hash;

  /* List of sequence candidates.  */
  htab_t seq_candidates;
} *p_hash_bucket;

/* Contains the last insn of the sequence, and its index value.  */
typedef struct hash_elem_def
{
  /* Unique index; ordered by FILL_HASH_BUCKET.  */
  unsigned long idx;

  /* The last insn in the sequence.  */
  rtx insn;

  /* The cached length of the insn.  */
  int length;
} *p_hash_elem;

/* The list of same sequence candidates.  */
static htab_t hash_buckets;

/* The pattern sequences collected from the current functions.  */
static pattern_seq pattern_seqs;

/* The blocks of the current pattern sequence.  */
static seq_block seq_blocks;

/* Cost of calling sequence.  */
static int seq_call_cost;

/* Cost of jump.  */
static int seq_jump_cost;

/* Cost of returning.  */
static int seq_return_cost;

/* Returns the first insn preceding INSN for which INSN_P is true and belongs to
   the same basic block. Returns NULL_RTX if no such insn can be found.  */

static rtx
prev_insn_in_block (rtx insn)
{
  basic_block bb = BLOCK_FOR_INSN (insn);

  if (!bb)
    return NULL_RTX;

  while (insn != BB_HEAD (bb))
    {
      insn = PREV_INSN (insn);
      if (INSN_P (insn))
        return insn;
    }
  return NULL_RTX;
}

/* Returns the hash value of INSN.  */

static unsigned int
compute_hash (rtx insn)
{
  unsigned int hash = 0;
  rtx prev;

  hash = INSN_CODE (insn) * 100;

  prev = prev_insn_in_block (insn);
  if (prev)
    hash += INSN_CODE (prev);

  return hash;
}

/* Compute the cost of INSN rtx for abstraction.  */

static int
compute_rtx_cost (rtx insn)
{
  struct hash_bucket_def tmp_bucket;
  p_hash_bucket bucket;
  struct hash_elem_def tmp_elem;
  p_hash_elem elem = NULL;
  int cost = -1;

  /* Compute hash value for INSN.  */
  tmp_bucket.hash = compute_hash (insn);

  /* Select the hash group.  */
  bucket = htab_find (hash_buckets, &tmp_bucket);

  if (bucket)
  {
    tmp_elem.insn = insn;

    /* Select the insn.  */
    elem = htab_find (bucket->seq_candidates, &tmp_elem);

    /* If INSN is parsed the cost will be the cached length.  */
    if (elem)
      cost = elem->length;
  }

  /* If we can't parse the INSN cost will be the instruction length.  */
  if (cost == -1)
  {
    cost = get_attr_length (insn);

    /* Cache the length.  */
    if (elem)
      elem->length = cost;
  }

  /* If we can't get an accurate estimate for a complex instruction,
     assume that it has the same cost as a single fast instruction.  */
  return cost != 0 ? cost : COSTS_N_INSNS (1);
}

/* Determines the number of common insns in the sequences ending in INSN1 and
   INSN2. Returns with LEN number of common insns and COST cost of sequence.
*/

static void
matching_length (rtx insn1, rtx insn2, int* len, int* cost)
{
  rtx x1;
  rtx x2;

  x1 = insn1;
  x2 = insn2;
  *len = 0;
  *cost = 0;
  while (x1 && x2 && (x1 != insn2) && (x2 != insn1)
         && rtx_equal_p (PATTERN (x1), PATTERN (x2)))
    {
      (*len)++;
      (*cost) += compute_rtx_cost (x1);
      x1 = prev_insn_in_block (x1);
      x2 = prev_insn_in_block (x2);
    }
}

/* Adds E0 as a pattern sequence to PATTERN_SEQS with E1 as a matching
   sequence.  */

static void
match_seqs (p_hash_elem e0, p_hash_elem e1)
{
  int len;
  int cost;
  matching_seq mseq, p_prev, p_next;

  /* Determines the cost of the sequence and return without doing anything
     if it is too small to produce any gain.  */
  matching_length (e0->insn, e1->insn, &len, &cost);
  if (cost <= seq_call_cost)
    return;

  /* Prepend a new PATTERN_SEQ to PATTERN_SEQS if the last pattern sequence
     does not end in E0->INSN. This assumes that once the E0->INSN changes
     the old value will never appear again.  */
  if (!pattern_seqs || pattern_seqs->insn != e0->insn)
    {
      pattern_seq pseq =
        (pattern_seq) xmalloc (sizeof (struct pattern_seq_def));
      pseq->insn = e0->insn;
      pseq->idx = e0->idx;
      pseq->gain = 0;                 /* Set to zero to force recomputing.  */
      pseq->abstracted_length = 0;
      pseq->cost = 0;
      pseq->link_reg = NULL_RTX;
      pseq->matching_seqs = NULL;
      pseq->next_pattern_seq = pattern_seqs;
      pattern_seqs = pseq;
    }

  /* Find the position of E1 in the matching sequences list.  */
  p_prev = NULL;
  p_next = pattern_seqs->matching_seqs;
  while (p_next && p_next->idx < e1->idx)
    {
      p_prev = p_next;
      p_next = p_next->next_matching_seq;
    }

  /* Add a new E1 matching sequence to the pattern sequence. We know that
     it ends in E0->INSN.  */
  mseq = (matching_seq) xmalloc (sizeof (struct matching_seq_def));
  mseq->insn = e1->insn;
  mseq->idx = e1->idx;
  mseq->matching_length = len;
  mseq->abstracted_length = 0;
  mseq->cost = cost;

  if (p_prev == NULL)
    pattern_seqs->matching_seqs = mseq;
  else
    p_prev->next_matching_seq = mseq;
  mseq->next_matching_seq = p_next;
}

/* Collects all pattern sequences and their matching sequences and puts them
   into PATTERN_SEQS.  */

static void
collect_pattern_seqs (void)
{
  htab_iterator hti0, hti1, hti2;
  p_hash_bucket hash_bucket;
  p_hash_elem e0, e1;
#ifdef STACK_REGS
  basic_block bb;
  bitmap_head stack_reg_live;

  /* Extra initialization step to ensure that no stack registers (if present)
     are live across abnormal edges. Set a flag in STACK_REG_LIVE for an insn
     if a stack register is live after the insn.  */
  bitmap_initialize (&stack_reg_live, NULL);

  FOR_EACH_BB (bb)
  {
    regset_head live;
    struct propagate_block_info *pbi;
    rtx insn;

    /* Initialize liveness propagation.  */
    INIT_REG_SET (&live);
    COPY_REG_SET (&live, bb->il.rtl->global_live_at_end);
    pbi = init_propagate_block_info (bb, &live, NULL, NULL, 0);

    /* Propagate liveness info and mark insns where a stack reg is live.  */
    insn = BB_END (bb);
    while (1)
      {
        int reg;
        for (reg = FIRST_STACK_REG; reg <= LAST_STACK_REG; reg++)
          {
            if (REGNO_REG_SET_P (&live, reg))
              {
                bitmap_set_bit (&stack_reg_live, INSN_UID (insn));
                break;
              }
          }

        if (insn == BB_HEAD (bb))
          break;
        insn = propagate_one_insn (pbi, insn);
      }

    /* Free unused data.  */
    CLEAR_REG_SET (&live);
    free_propagate_block_info (pbi);
  }
#endif

  /* Initialize PATTERN_SEQS to empty.  */
  pattern_seqs = 0;

  /* Try to match every abstractable insn with every other insn in the same
     HASH_BUCKET.  */

  FOR_EACH_HTAB_ELEMENT (hash_buckets, hash_bucket, p_hash_bucket, hti0)
    if (htab_elements (hash_bucket->seq_candidates) > 1)
      FOR_EACH_HTAB_ELEMENT (hash_bucket->seq_candidates, e0, p_hash_elem, hti1)
        FOR_EACH_HTAB_ELEMENT (hash_bucket->seq_candidates, e1, p_hash_elem,
                               hti2)
          if (e0 != e1
#ifdef STACK_REGS
              && !bitmap_bit_p (&stack_reg_live, INSN_UID (e0->insn))
              && !bitmap_bit_p (&stack_reg_live, INSN_UID (e1->insn))
#endif
             )
            match_seqs (e0, e1);
#ifdef STACK_REGS
  /* Free unused data.  */
  bitmap_clear (&stack_reg_live);
#endif
}

/* Transforms a regset to a HARD_REG_SET. Every hard register in REGS is added
   to hregs. Additionally, the hard counterpart of every renumbered pseudo
   register is also added.  */

static void
renumbered_reg_set_to_hard_reg_set (HARD_REG_SET * hregs, regset regs)
{
  int r;

  REG_SET_TO_HARD_REG_SET (*hregs, regs);
  for (r = FIRST_PSEUDO_REGISTER; r < max_regno; r++)
    if (REGNO_REG_SET_P (regs, r) && reg_renumber[r] >= 0)
      SET_HARD_REG_BIT (*hregs, reg_renumber[r]);
}

/* Clears the bits in REGS for all registers, which are live in the sequence
   give by its last INSN and its LENGTH.  */

static void
clear_regs_live_in_seq (HARD_REG_SET * regs, rtx insn, int length)
{
  basic_block bb;
  regset_head live;
  HARD_REG_SET hlive;
  struct propagate_block_info *pbi;
  rtx x;
  int i;

  /* Initialize liveness propagation.  */
  bb = BLOCK_FOR_INSN (insn);
  INIT_REG_SET (&live);
  COPY_REG_SET (&live, bb->il.rtl->global_live_at_end);
  pbi = init_propagate_block_info (bb, &live, NULL, NULL, 0);

  /* Propagate until INSN if found.  */
  for (x = BB_END (bb); x != insn;)
    x = propagate_one_insn (pbi, x);

  /* Clear registers live after INSN.  */
  renumbered_reg_set_to_hard_reg_set (&hlive, &live);
  AND_COMPL_HARD_REG_SET (*regs, hlive);

  /* Clear registers live in and before the sequence.  */
  for (i = 0; i < length;)
    {
      rtx prev = propagate_one_insn (pbi, x);

      if (INSN_P (x))
        {
          renumbered_reg_set_to_hard_reg_set (&hlive, &live);
          AND_COMPL_HARD_REG_SET (*regs, hlive);
          i++;
        }

      x = prev;
    }

  /* Free unused data.  */
  free_propagate_block_info (pbi);
  CLEAR_REG_SET (&live);
}

/* Computes the gain of turning PSEQ into a pseudo-function and its matching
   sequences into pseudo-calls. Also computes and caches the number of insns to
   abstract from  the matching sequences.  */

static void
recompute_gain_for_pattern_seq (pattern_seq pseq)
{
  matching_seq mseq;
  rtx x;
  int i;
  int hascall;
  HARD_REG_SET linkregs;

  /* Initialize data.  */
  SET_HARD_REG_SET (linkregs);
  pseq->link_reg = NULL_RTX;
  pseq->abstracted_length = 0;

  pseq->gain = -(seq_call_cost - seq_jump_cost + seq_return_cost);

  /* Determine ABSTRACTED_LENGTH and COST for matching sequences of PSEQ.
     ABSTRACTED_LENGTH may be less than MATCHING_LENGTH if sequences in the
     same block overlap. */

  for (mseq = pseq->matching_seqs; mseq; mseq = mseq->next_matching_seq)
    {
      /* Determine ABSTRACTED_LENGTH.  */
      if (mseq->next_matching_seq)
        mseq->abstracted_length = (int)(mseq->next_matching_seq->idx -
                                        mseq->idx);
      else
        mseq->abstracted_length = mseq->matching_length;

      if (mseq->abstracted_length > mseq->matching_length)
        mseq->abstracted_length = mseq->matching_length;

      /* Compute the cost of sequence.  */
      RECOMPUTE_COST (mseq);

      /* If COST is big enough registers live in this matching sequence
         should not be used as a link register. Also set ABSTRACTED_LENGTH
         of PSEQ.  */
      if (mseq->cost > seq_call_cost)
        {
          clear_regs_live_in_seq (&linkregs, mseq->insn,
                                  mseq->abstracted_length);
          if (mseq->abstracted_length > pseq->abstracted_length)
            pseq->abstracted_length = mseq->abstracted_length;
        }
    }

  /* Modify ABSTRACTED_LENGTH of PSEQ if pattern sequence overlaps with one
     of the matching sequences.  */
  for (mseq = pseq->matching_seqs; mseq; mseq = mseq->next_matching_seq)
    {
      x = pseq->insn;
      for (i = 0; (i < pseq->abstracted_length) && (x != mseq->insn); i++)
        x = prev_insn_in_block (x);
      pseq->abstracted_length = i;
    }

  /* Compute the cost of pattern sequence.  */
  RECOMPUTE_COST (pseq);

  /* No gain if COST is too small.  */
  if (pseq->cost <= seq_call_cost)
  {
    pseq->gain = -1;
    return;
  }

  /* Ensure that no matching sequence is longer than the pattern sequence.  */
  for (mseq = pseq->matching_seqs; mseq; mseq = mseq->next_matching_seq)
    {
      if (mseq->abstracted_length > pseq->abstracted_length)
        {
          mseq->abstracted_length = pseq->abstracted_length;
          RECOMPUTE_COST (mseq);
        }
      /* Once the length is stabilizing the gain can be calculated.  */
      if (mseq->cost > seq_call_cost)
        pseq->gain += mseq->cost - seq_call_cost;
    }

  /* No need to do further work if there is no gain.  */
  if (pseq->gain <= 0)
    return;

  /* Should not use registers live in the pattern sequence as link register.
   */
  clear_regs_live_in_seq (&linkregs, pseq->insn, pseq->abstracted_length);

  /* Determine whether pattern sequence contains a call_insn.  */
  hascall = 0;
  x = pseq->insn;
  for (i = 0; i < pseq->abstracted_length; i++)
    {
      if (CALL_P (x))
        {
          hascall = 1;
          break;
        }
      x = prev_insn_in_block (x);
    }

  /* Should not use a register as a link register if - it is a fixed
     register, or - the sequence contains a call insn and the register is a
     call used register, or - the register needs to be saved if used in a
     function but was not used before (since saving it can invalidate already
     computed frame pointer offsets), or - the register cannot be used as a
     base register.  */

  for (i = 0; i < FIRST_PSEUDO_REGISTER; i++)
    if (fixed_regs[i]
#ifdef REGNO_OK_FOR_INDIRECT_JUMP_P
        || (!REGNO_OK_FOR_INDIRECT_JUMP_P (i, Pmode))
#else
        || (!ok_for_base_p_1 (i, Pmode, MEM, SCRATCH))
        || (!reg_class_subset_p (REGNO_REG_CLASS (i),
				 base_reg_class (VOIDmode, MEM, SCRATCH)))
#endif
        || (hascall && call_used_regs[i])
        || (!call_used_regs[i] && !regs_ever_live[i]))
      CLEAR_HARD_REG_BIT (linkregs, i);

  /* Find an appropriate register to be used as the link register.  */
  for (i = 0; i < FIRST_PSEUDO_REGISTER; i++)
    if (TEST_HARD_REG_BIT (linkregs, i))
      {
        pseq->link_reg = gen_rtx_REG (Pmode, i);
        break;
      }

  /* Abstraction is not possible if no link register is available, so set
     gain to 0.  */
  if (!pseq->link_reg)
    pseq->gain = 0;
}

/* Deallocates memory occupied by PSEQ and its matching seqs.  */

static void
free_pattern_seq (pattern_seq pseq)
{
  while (pseq->matching_seqs)
    {
      matching_seq mseq = pseq->matching_seqs;
      pseq->matching_seqs = mseq->next_matching_seq;
      free (mseq);
    }
  free (pseq);
}


/* Computes the gain for pattern sequences. Pattern sequences producing no gain
   are deleted. The pattern sequence with the biggest gain is moved to the first
   place of PATTERN_SEQS.  */

static void
recompute_gain (void)
{
  pattern_seq *pseq;
  int maxgain;

  maxgain = 0;
  for (pseq = &pattern_seqs; *pseq;)
    {
      if ((*pseq)->gain <= 0)
        recompute_gain_for_pattern_seq (*pseq);

      if ((*pseq)->gain > 0)
        {
          if ((*pseq)->gain > maxgain)
            {
              pattern_seq temp = *pseq;
              (*pseq) = temp->next_pattern_seq;
              temp->next_pattern_seq = pattern_seqs;
              pattern_seqs = temp;
              maxgain = pattern_seqs->gain;
            }
          else
            {
              pseq = &(*pseq)->next_pattern_seq;
            }
        }
      else
        {
          pattern_seq temp = *pseq;
          *pseq = temp->next_pattern_seq;
          free_pattern_seq (temp);
        }
    }
}

/* Updated those pattern sequences and matching sequences, which overlap with
   the sequence given by INSN and LEN. Deletes sequences shrinking below a
   limit.  */

static void
erase_from_pattern_seqs (rtx insn, int len)
{
  pattern_seq *pseq;
  matching_seq *mseq;
  rtx x;
  int plen, mlen;
  int pcost, mcost;

  while (len > 0)
    {
      for (pseq = &pattern_seqs; *pseq;)
        {
          plen = 0;
          pcost = 0;
          for (x = (*pseq)->insn; x && (x != insn);
               x = prev_insn_in_block (x))
            {
              plen++;
              pcost += compute_rtx_cost (x);
            }

          if (pcost <= seq_call_cost)
            {
              pattern_seq temp = *pseq;
              *pseq = temp->next_pattern_seq;
              free_pattern_seq (temp);
            }
          else
            {
              for (mseq = &(*pseq)->matching_seqs; *mseq;)
                {
                  mlen = 0;
                  mcost = 0;
                  for (x = (*mseq)->insn;
                       x && (x != insn) && (mlen < plen)
                       && (mlen < (*mseq)->matching_length);
                       x = prev_insn_in_block (x))
                    {
                      mlen++;
                      mcost += compute_rtx_cost (x);
                    }

                  if (mcost <= seq_call_cost)
                    {
                      matching_seq temp = *mseq;
                      *mseq = temp->next_matching_seq;
                      free (temp);
                      /* Set to 0 to force gain recomputation.  */
                      (*pseq)->gain = 0;
                    }
                  else
                    {
                      if (mlen < (*mseq)->matching_length)
                        {
                          (*mseq)->cost = mcost;
                          (*mseq)->matching_length = mlen;
                          /* Set to 0 to force gain recomputation.  */
                          (*pseq)->gain = 0;
                        }
                      mseq = &(*mseq)->next_matching_seq;
                    }
                }

              pseq = &(*pseq)->next_pattern_seq;
            }
        }

      len--;
      insn = prev_insn_in_block (insn);
    }
}

/* Updates those pattern sequences and matching sequences, which overlap with
   the pattern sequence with the biggest gain and its matching sequences.  */

static void
update_pattern_seqs (void)
{
  pattern_seq bestpseq;
  matching_seq mseq;

  bestpseq = pattern_seqs;
  pattern_seqs = bestpseq->next_pattern_seq;

  for (mseq = bestpseq->matching_seqs; mseq; mseq = mseq->next_matching_seq)
    if (mseq->cost > seq_call_cost)
      erase_from_pattern_seqs (mseq->insn, mseq->abstracted_length);
  erase_from_pattern_seqs (bestpseq->insn, bestpseq->abstracted_length);

  bestpseq->next_pattern_seq = pattern_seqs;
  pattern_seqs = bestpseq;
}

/* Groups together those matching sequences of the best pattern sequence, which
   have the same ABSTRACTED_LENGTH and puts these groups in ascending order.
   SEQ_BLOCKS contains the result.  */

static void
determine_seq_blocks (void)
{
  seq_block sb;
  matching_seq *mseq;
  matching_seq m;

  /* Initialize SEQ_BLOCKS to empty.  */
  seq_blocks = 0;

  /* Process all matching sequences.  */
  for (mseq = &pattern_seqs->matching_seqs; *mseq;)
    {
      /* Deal only with matching sequences being long enough. */
      if ((*mseq)->cost <= seq_call_cost)
        {
          mseq = &(*mseq)->next_matching_seq;
          continue;
        }

      /* Ensure that SB contains a seq_block with the appropriate length.
         Insert a new seq_block if necessary.  */
      if (!seq_blocks || ((*mseq)->abstracted_length < seq_blocks->length))
        {
          sb = (seq_block) xmalloc (sizeof (struct seq_block_def));
          sb->length = (*mseq)->abstracted_length;
          sb->label = NULL_RTX;
          sb->matching_seqs = 0;
          sb->next_seq_block = seq_blocks;
          seq_blocks = sb;
        }
      else
        {
          for (sb = seq_blocks; sb; sb = sb->next_seq_block)
            {
              if ((*mseq)->abstracted_length == sb->length)
                break;
              if (!sb->next_seq_block
                  || ((*mseq)->abstracted_length <
                      sb->next_seq_block->length))
                {
                  seq_block temp =
                    (seq_block) xmalloc (sizeof (struct seq_block_def));
                  temp->length = (*mseq)->abstracted_length;
                  temp->label = NULL_RTX;
                  temp->matching_seqs = 0;
                  temp->next_seq_block = sb->next_seq_block;
                  sb->next_seq_block = temp;
                }
            }
        }

      /* Remove the matching sequence from the linked list of the pattern
         sequence and link it to SB.  */
      m = *mseq;
      *mseq = m->next_matching_seq;
      m->next_matching_seq = sb->matching_seqs;
      sb->matching_seqs = m;
    }
}

/* Builds a symbol_ref for LABEL.  */

static rtx
gen_symbol_ref_rtx_for_label (rtx label)
{
  char name[20];
  rtx sym;

  ASM_GENERATE_INTERNAL_LABEL (name, "L", CODE_LABEL_NUMBER (label));
  sym = gen_rtx_SYMBOL_REF (Pmode, ggc_strdup (name));
  SYMBOL_REF_FLAGS (sym) = SYMBOL_FLAG_LOCAL;
  return sym;
}

/* Ensures that INSN is the last insn in its block and returns the block label
   of the next block.  */

static rtx
block_label_after (rtx insn)
{
  basic_block bb = BLOCK_FOR_INSN (insn);
  if ((insn == BB_END (bb)) && (bb->next_bb != EXIT_BLOCK_PTR))
    return block_label (bb->next_bb);
  else
    return block_label (split_block (bb, insn)->dest);
}

/* Ensures that the last insns of the best pattern and its matching sequences
   are the last insns in their block. Additionally, extends the live set at the
   end of the pattern sequence with the live sets at the end of the matching
   sequences.  */

static void
split_blocks_after_seqs (void)
{
  seq_block sb;
  matching_seq mseq;

  block_label_after (pattern_seqs->insn);
  for (sb = seq_blocks; sb; sb = sb->next_seq_block)
    {
      for (mseq = sb->matching_seqs; mseq; mseq = mseq->next_matching_seq)
        {
          block_label_after (mseq->insn);
          IOR_REG_SET (BLOCK_FOR_INSN (pattern_seqs->insn)->
                       il.rtl->global_live_at_end,
                       BLOCK_FOR_INSN (mseq->insn)->il.rtl->global_live_at_end);
        }
    }
}

/* Splits the best pattern sequence according to SEQ_BLOCKS. Emits pseudo-call
   and -return insns before and after the sequence.  */

static void
split_pattern_seq (void)
{
  rtx insn;
  basic_block bb;
  rtx retlabel, retjmp, saveinsn;
  int i;
  seq_block sb;

  insn = pattern_seqs->insn;
  bb = BLOCK_FOR_INSN (insn);

  /* Get the label after the sequence. This will be the return address. The
     label will be referenced using a symbol_ref so protect it from
     deleting.  */
  retlabel = block_label_after (insn);
  LABEL_PRESERVE_P (retlabel) = 1;

  /* Emit an indirect jump via the link register after the sequence acting
     as the return insn.  Also emit a barrier and update the basic block.  */
  retjmp = emit_jump_insn_after (gen_indirect_jump (pattern_seqs->link_reg),
                                 BB_END (bb));
  emit_barrier_after (BB_END (bb));

  /* Replace all outgoing edges with a new one to the block of RETLABEL.  */
  while (EDGE_COUNT (bb->succs) != 0)
    remove_edge (EDGE_SUCC (bb, 0));
  make_edge (bb, BLOCK_FOR_INSN (retlabel), EDGE_ABNORMAL);

  /* Split the sequence according to SEQ_BLOCKS and cache the label of the
     resulting basic blocks.  */
  i = 0;
  for (sb = seq_blocks; sb; sb = sb->next_seq_block)
    {
      for (; i < sb->length; i++)
        insn = prev_insn_in_block (insn);

      sb->label = block_label (split_block (bb, insn)->dest);
    }

  /* Emit an insn saving the return address to the link register before the
     sequence.  */
  saveinsn = emit_insn_after (gen_move_insn (pattern_seqs->link_reg,
                              gen_symbol_ref_rtx_for_label
                              (retlabel)), BB_END (bb));
  /* Update liveness info.  */
  SET_REGNO_REG_SET (bb->il.rtl->global_live_at_end,
                     REGNO (pattern_seqs->link_reg));
}

/* Deletes the insns of the matching sequences of the best pattern sequence and
   replaces them with pseudo-calls to the pattern sequence.  */

static void
erase_matching_seqs (void)
{
  seq_block sb;
  matching_seq mseq;
  rtx insn;
  basic_block bb;
  rtx retlabel, saveinsn, callinsn;
  int i;

  for (sb = seq_blocks; sb; sb = sb->next_seq_block)
    {
      for (mseq = sb->matching_seqs; mseq; mseq = mseq->next_matching_seq)
        {
          insn = mseq->insn;
          bb = BLOCK_FOR_INSN (insn);

          /* Get the label after the sequence. This will be the return
             address. The label will be referenced using a symbol_ref so
             protect it from deleting.  */
          retlabel = block_label_after (insn);
          LABEL_PRESERVE_P (retlabel) = 1;

          /* Delete the insns of the sequence.  */
          for (i = 0; i < sb->length; i++)
            insn = prev_insn_in_block (insn);
          delete_basic_block (split_block (bb, insn)->dest);

          /* Emit an insn saving the return address to the link register
             before the deleted sequence.  */
          saveinsn = emit_insn_after (gen_move_insn (pattern_seqs->link_reg,
                                      gen_symbol_ref_rtx_for_label
                                      (retlabel)),
                                      BB_END (bb));
          BLOCK_FOR_INSN (saveinsn) = bb;

          /* Emit a jump to the appropriate part of the pattern sequence
             after the save insn. Also update the basic block.  */
          callinsn = emit_jump_insn_after (gen_jump (sb->label), saveinsn);
          JUMP_LABEL (callinsn) = sb->label;
          LABEL_NUSES (sb->label)++;
          BLOCK_FOR_INSN (callinsn) = bb;
          BB_END (bb) = callinsn;

          /* Maintain control flow and liveness information.  */
          SET_REGNO_REG_SET (bb->il.rtl->global_live_at_end,
                             REGNO (pattern_seqs->link_reg));
          emit_barrier_after (BB_END (bb));
          make_single_succ_edge (bb, BLOCK_FOR_INSN (sb->label), 0);
          IOR_REG_SET (bb->il.rtl->global_live_at_end,
            BLOCK_FOR_INSN (sb->label)->il.rtl->global_live_at_start);

          make_edge (BLOCK_FOR_INSN (seq_blocks->label),
                     BLOCK_FOR_INSN (retlabel), EDGE_ABNORMAL);
        }
    }
}

/* Deallocates SEQ_BLOCKS and all the matching sequences.  */

static void
free_seq_blocks (void)
{
  while (seq_blocks)
    {
      seq_block sb = seq_blocks;
      while (sb->matching_seqs)
        {
          matching_seq mseq = sb->matching_seqs;
          sb->matching_seqs = mseq->next_matching_seq;
          free (mseq);
        }
      seq_blocks = sb->next_seq_block;
      free (sb);
    }
}

/* Transforms the best pattern sequence into a pseudo-function and its matching
   sequences to pseudo-calls. Afterwards the best pattern sequence is removed
   from PATTERN_SEQS.  */

static void
abstract_best_seq (void)
{
  pattern_seq bestpseq;

  /* Do the abstraction.  */
  determine_seq_blocks ();
  split_blocks_after_seqs ();
  split_pattern_seq ();
  erase_matching_seqs ();
  free_seq_blocks ();

  /* Record the usage of the link register.  */
  regs_ever_live[REGNO (pattern_seqs->link_reg)] = 1;

  /* Remove the best pattern sequence.  */
  bestpseq = pattern_seqs;
  pattern_seqs = bestpseq->next_pattern_seq;
  free_pattern_seq (bestpseq);
}

/* Prints info on the pattern sequences to the dump file.  */

static void
dump_pattern_seqs (void)
{
  pattern_seq pseq;
  matching_seq mseq;

  if (!dump_file)
    return;

  fprintf (dump_file, ";; Pattern sequences\n");
  for (pseq = pattern_seqs; pseq; pseq = pseq->next_pattern_seq)
    {
      fprintf (dump_file, "Pattern sequence at insn %d matches sequences at",
               INSN_UID (pseq->insn));
      for (mseq = pseq->matching_seqs; mseq; mseq = mseq->next_matching_seq)
        {
          fprintf (dump_file, " insn %d (length %d)", INSN_UID (mseq->insn),
                   mseq->matching_length);
          if (mseq->next_matching_seq)
            fprintf (dump_file, ",");
        }
      fprintf (dump_file, ".\n");
    }
  fprintf (dump_file, "\n");
}

/* Prints info on the best pattern sequence transformed in the ITER-th
   iteration to the dump file.  */

static void
dump_best_pattern_seq (int iter)
{
  matching_seq mseq;

  if (!dump_file)
    return;

  fprintf (dump_file, ";; Iteration %d\n", iter);
  fprintf (dump_file,
           "Best pattern sequence with %d gain is at insn %d (length %d).\n",
           pattern_seqs->gain, INSN_UID (pattern_seqs->insn),
           pattern_seqs->abstracted_length);
  fprintf (dump_file, "Matching sequences are at");
  for (mseq = pattern_seqs->matching_seqs; mseq;
       mseq = mseq->next_matching_seq)
    {
      fprintf (dump_file, " insn %d (length %d)", INSN_UID (mseq->insn),
               mseq->abstracted_length);
      if (mseq->next_matching_seq)
        fprintf (dump_file, ",");
    }
  fprintf (dump_file, ".\n");
  fprintf (dump_file, "Using reg %d as link register.\n\n",
           REGNO (pattern_seqs->link_reg));
}

/* Htab hash function for hash_bucket_def structure.  */

static unsigned int
htab_hash_bucket (const void *p)
{
  p_hash_bucket bucket = (p_hash_bucket) p;
  return bucket->hash;
}

/* Htab equal function for hash_bucket_def structure.  */

static int
htab_eq_bucket (const void *p0, const void *p1)
{
  return htab_hash_bucket (p0) == htab_hash_bucket (p1);
}

/* Htab delete function for hash_bucket_def structure.  */

static void
htab_del_bucket (void *p)
{
  p_hash_bucket bucket = (p_hash_bucket) p;

  if (bucket->seq_candidates)
    htab_delete (bucket->seq_candidates);

  free (bucket);
}

/* Htab hash function for hash_bucket_def structure.  */

static unsigned int
htab_hash_elem (const void *p)
{
  p_hash_elem elem = (p_hash_elem) p;
  return htab_hash_pointer (elem->insn);
}

/* Htab equal function for hash_bucket_def structure.  */

static int
htab_eq_elem (const void *p0, const void *p1)
{
  return htab_hash_elem (p0) == htab_hash_elem (p1);
}

/* Htab delete function for hash_bucket_def structure.  */

static void
htab_del_elem (void *p)
{
  p_hash_elem elem = (p_hash_elem) p;
  free (elem);
}

/* Creates a hash value for each sequence candidate and saves them
   in HASH_BUCKET.  */

static void
fill_hash_bucket (void)
{
  basic_block bb;
  rtx insn;
  void **slot;
  p_hash_bucket bucket;
  struct hash_bucket_def tmp_bucket;
  p_hash_elem elem;
  unsigned long insn_idx;

  insn_idx = 0;
  FOR_EACH_BB (bb)
    {
      FOR_BB_INSNS_REVERSE (bb, insn)
        {
          if (!ABSTRACTABLE_INSN_P (insn))
            continue;

          /* Compute hash value for INSN.  */
          tmp_bucket.hash = compute_hash (insn);

          /* Select the hash group.  */
          bucket = htab_find (hash_buckets, &tmp_bucket);

          if (!bucket)
            {
              /* Create a new hash group.  */
              bucket = (p_hash_bucket) xcalloc (1,
                                        sizeof (struct hash_bucket_def));
              bucket->hash = tmp_bucket.hash;
              bucket->seq_candidates = NULL;

              slot = htab_find_slot (hash_buckets, &tmp_bucket, INSERT);
              *slot = bucket;
            }

          /* Create new list for storing sequence candidates.  */
          if (!bucket->seq_candidates)
              bucket->seq_candidates = htab_create (HASH_INIT,
                                                    htab_hash_elem,
                                                    htab_eq_elem,
                                                    htab_del_elem);

          elem = (p_hash_elem) xcalloc (1, sizeof (struct hash_elem_def));
          elem->insn = insn;
          elem->idx = insn_idx;
          elem->length = get_attr_length (insn);

          /* Insert INSN into BUCKET hash bucket.  */
          slot = htab_find_slot (bucket->seq_candidates, elem, INSERT);
          *slot = elem;

          insn_idx++;
        }
    }
}

/* Computes the cost of calling sequence and the cost of return.  */

static void
compute_init_costs (void)
{
  rtx rtx_jump, rtx_store, rtx_return, reg, label;
  basic_block bb;

  FOR_EACH_BB (bb)
    if (BB_HEAD (bb))
      break;

  label = block_label (bb);
  reg = gen_rtx_REG (Pmode, 0);

  /* Pattern for indirect jump.  */
  rtx_jump = gen_indirect_jump (reg);

  /* Pattern for storing address.  */
  rtx_store = gen_rtx_SET (VOIDmode, reg, gen_symbol_ref_rtx_for_label (label));

  /* Pattern for return insn.  */
  rtx_return = gen_jump (label);

  /* The cost of jump.  */
  seq_jump_cost = compute_rtx_cost (make_jump_insn_raw (rtx_jump));

  /* The cost of calling sequence.  */
  seq_call_cost = seq_jump_cost + compute_rtx_cost (make_insn_raw (rtx_store));

  /* The cost of return.  */
  seq_return_cost = compute_rtx_cost (make_jump_insn_raw (rtx_return));

  /* Simple heuristic for minimal sequence cost.  */
  seq_call_cost   = (int)(seq_call_cost * (double)SEQ_CALL_COST_MULTIPLIER);
}

/* Finds equivalent insn sequences in the current function and retains only one
   instance of them which is turned into a pseudo-function. The additional
   copies are erased and replaced by pseudo-calls to the retained sequence.  */

static void
rtl_seqabstr (void)
{
  int iter;

  /* Create a hash list for COLLECT_PATTERN_SEQS.  */
  hash_buckets = htab_create (HASH_INIT, htab_hash_bucket , htab_eq_bucket ,
                              htab_del_bucket);
  fill_hash_bucket ();

  /* Compute the common cost of abstraction.  */
  compute_init_costs ();

  /* Build an initial set of pattern sequences from the current function.  */
  collect_pattern_seqs ();
  dump_pattern_seqs ();

  /* Iterate until there are no sequences to abstract.  */
  for (iter = 1;; iter++)
    {
      /* Recompute gain for sequences if necessary and select sequence with
         biggest gain.  */
      recompute_gain ();
      if (!pattern_seqs)
        break;
      dump_best_pattern_seq (iter);
      /* Update the cached info of the other sequences and force gain
         recomputation where needed.  */
      update_pattern_seqs ();
      /* Turn best sequences into pseudo-functions and -calls.  */
      abstract_best_seq ();
    }

  /* Cleanup hash tables.  */
  htab_delete (hash_buckets);

  if (iter > 1)
    {
      /* Update notes.  */
      count_or_remove_death_notes (NULL, 1);

      life_analysis (PROP_DEATH_NOTES | PROP_SCAN_DEAD_CODE
		     | PROP_KILL_DEAD_CODE);

      /* Extra cleanup.  */
      cleanup_cfg (CLEANUP_EXPENSIVE |
                   CLEANUP_UPDATE_LIFE |
                   (flag_crossjumping ? CLEANUP_CROSSJUMP : 0));
    }
}

/* The gate function for TREE_OPT_PASS.  */

static bool
gate_rtl_seqabstr (void)
{
  return flag_rtl_seqabstr;
}

/* The entry point of the sequence abstraction algorithm.  */

static unsigned int
rest_of_rtl_seqabstr (void)
{
  life_analysis (PROP_DEATH_NOTES | PROP_SCAN_DEAD_CODE | PROP_KILL_DEAD_CODE);

  cleanup_cfg (CLEANUP_EXPENSIVE |
               CLEANUP_UPDATE_LIFE |
               (flag_crossjumping ? CLEANUP_CROSSJUMP : 0));

  /* Abstract out common insn sequences. */
  rtl_seqabstr ();
  return 0;
}

struct tree_opt_pass pass_rtl_seqabstr = {
  "seqabstr",                           /* name */
  gate_rtl_seqabstr,                    /* gate */
  rest_of_rtl_seqabstr,                 /* execute */
  NULL,                                 /* sub */
  NULL,                                 /* next */
  0,                                    /* static_pass_number */
  TV_SEQABSTR,                          /* tv_id */
  0,                                    /* properties_required */
  0,                                    /* properties_provided */
  0,                                    /* properties_destroyed */
  0,                                    /* todo_flags_start */
  TODO_dump_func |
  TODO_ggc_collect,                     /* todo_flags_finish */
  'Q'                                   /* letter */
};
