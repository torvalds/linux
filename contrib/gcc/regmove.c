/* Move registers around to reduce number of move instructions needed.
   Copyright (C) 1987, 1988, 1989, 1992, 1993, 1994, 1995, 1996, 1997, 1998,
   1999, 2000, 2001, 2002, 2003, 2004, 2005 Free Software Foundation, Inc.

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


/* This module looks for cases where matching constraints would force
   an instruction to need a reload, and this reload would be a register
   to register move.  It then attempts to change the registers used by the
   instruction to avoid the move instruction.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "rtl.h" /* stdio.h must precede rtl.h for FFS.  */
#include "tm_p.h"
#include "insn-config.h"
#include "recog.h"
#include "output.h"
#include "regs.h"
#include "hard-reg-set.h"
#include "flags.h"
#include "function.h"
#include "expr.h"
#include "basic-block.h"
#include "except.h"
#include "toplev.h"
#include "reload.h"
#include "timevar.h"
#include "tree-pass.h"


/* Turn STACK_GROWS_DOWNWARD into a boolean.  */
#ifdef STACK_GROWS_DOWNWARD
#undef STACK_GROWS_DOWNWARD
#define STACK_GROWS_DOWNWARD 1
#else
#define STACK_GROWS_DOWNWARD 0
#endif

static int perhaps_ends_bb_p (rtx);
static int optimize_reg_copy_1 (rtx, rtx, rtx);
static void optimize_reg_copy_2 (rtx, rtx, rtx);
static void optimize_reg_copy_3 (rtx, rtx, rtx);
static void copy_src_to_dest (rtx, rtx, rtx, int);
static int *regmove_bb_head;

struct match {
  int with[MAX_RECOG_OPERANDS];
  enum { READ, WRITE, READWRITE } use[MAX_RECOG_OPERANDS];
  int commutative[MAX_RECOG_OPERANDS];
  int early_clobber[MAX_RECOG_OPERANDS];
};

static rtx discover_flags_reg (void);
static void mark_flags_life_zones (rtx);
static void flags_set_1 (rtx, rtx, void *);

static int try_auto_increment (rtx, rtx, rtx, rtx, HOST_WIDE_INT, int);
static int find_matches (rtx, struct match *);
static void replace_in_call_usage (rtx *, unsigned int, rtx, rtx);
static int fixup_match_1 (rtx, rtx, rtx, rtx, rtx, int, int, int);
static int stable_and_no_regs_but_for_p (rtx, rtx, rtx);
static int regclass_compatible_p (int, int);
static int replacement_quality (rtx);
static int fixup_match_2 (rtx, rtx, rtx, rtx);

/* Return nonzero if registers with CLASS1 and CLASS2 can be merged without
   causing too much register allocation problems.  */
static int
regclass_compatible_p (int class0, int class1)
{
  return (class0 == class1
	  || (reg_class_subset_p (class0, class1)
	      && ! CLASS_LIKELY_SPILLED_P (class0))
	  || (reg_class_subset_p (class1, class0)
	      && ! CLASS_LIKELY_SPILLED_P (class1)));
}

/* INC_INSN is an instruction that adds INCREMENT to REG.
   Try to fold INC_INSN as a post/pre in/decrement into INSN.
   Iff INC_INSN_SET is nonzero, inc_insn has a destination different from src.
   Return nonzero for success.  */
static int
try_auto_increment (rtx insn, rtx inc_insn, rtx inc_insn_set, rtx reg,
		    HOST_WIDE_INT increment, int pre)
{
  enum rtx_code inc_code;

  rtx pset = single_set (insn);
  if (pset)
    {
      /* Can't use the size of SET_SRC, we might have something like
	 (sign_extend:SI (mem:QI ...  */
      rtx use = find_use_as_address (pset, reg, 0);
      if (use != 0 && use != (rtx) (size_t) 1)
	{
	  int size = GET_MODE_SIZE (GET_MODE (use));
	  if (0
	      || (HAVE_POST_INCREMENT
		  && pre == 0 && (inc_code = POST_INC, increment == size))
	      || (HAVE_PRE_INCREMENT
		  && pre == 1 && (inc_code = PRE_INC, increment == size))
	      || (HAVE_POST_DECREMENT
		  && pre == 0 && (inc_code = POST_DEC, increment == -size))
	      || (HAVE_PRE_DECREMENT
		  && pre == 1 && (inc_code = PRE_DEC, increment == -size))
	  )
	    {
	      if (inc_insn_set)
		validate_change
		  (inc_insn,
		   &SET_SRC (inc_insn_set),
		   XEXP (SET_SRC (inc_insn_set), 0), 1);
	      validate_change (insn, &XEXP (use, 0),
			       gen_rtx_fmt_e (inc_code, Pmode, reg), 1);
	      if (apply_change_group ())
		{
		  /* If there is a REG_DEAD note on this insn, we must
		     change this not to REG_UNUSED meaning that the register
		     is set, but the value is dead.  Failure to do so will
		     result in a sched1 dieing -- when it recomputes lifetime
		     information, the number of REG_DEAD notes will have
		     changed.  */
		  rtx note = find_reg_note (insn, REG_DEAD, reg);
		  if (note)
		    PUT_MODE (note, REG_UNUSED);

		  REG_NOTES (insn)
		    = gen_rtx_EXPR_LIST (REG_INC,
					 reg, REG_NOTES (insn));
		  if (! inc_insn_set)
		    delete_insn (inc_insn);
		  return 1;
		}
	    }
	}
    }
  return 0;
}

/* Determine if the pattern generated by add_optab has a clobber,
   such as might be issued for a flags hard register.  To make the
   code elsewhere simpler, we handle cc0 in this same framework.

   Return the register if one was discovered.  Return NULL_RTX if
   if no flags were found.  Return pc_rtx if we got confused.  */

static rtx
discover_flags_reg (void)
{
  rtx tmp;
  tmp = gen_rtx_REG (word_mode, 10000);
  tmp = gen_add3_insn (tmp, tmp, const2_rtx);

  /* If we get something that isn't a simple set, or a
     [(set ..) (clobber ..)], this whole function will go wrong.  */
  if (GET_CODE (tmp) == SET)
    return NULL_RTX;
  else if (GET_CODE (tmp) == PARALLEL)
    {
      int found;

      if (XVECLEN (tmp, 0) != 2)
	return pc_rtx;
      tmp = XVECEXP (tmp, 0, 1);
      if (GET_CODE (tmp) != CLOBBER)
	return pc_rtx;
      tmp = XEXP (tmp, 0);

      /* Don't do anything foolish if the md wanted to clobber a
	 scratch or something.  We only care about hard regs.
	 Moreover we don't like the notion of subregs of hard regs.  */
      if (GET_CODE (tmp) == SUBREG
	  && REG_P (SUBREG_REG (tmp))
	  && REGNO (SUBREG_REG (tmp)) < FIRST_PSEUDO_REGISTER)
	return pc_rtx;
      found = (REG_P (tmp) && REGNO (tmp) < FIRST_PSEUDO_REGISTER);

      return (found ? tmp : NULL_RTX);
    }

  return pc_rtx;
}

/* It is a tedious task identifying when the flags register is live and
   when it is safe to optimize.  Since we process the instruction stream
   multiple times, locate and record these live zones by marking the
   mode of the instructions --

   QImode is used on the instruction at which the flags becomes live.

   HImode is used within the range (exclusive) that the flags are
   live.  Thus the user of the flags is not marked.

   All other instructions are cleared to VOIDmode.  */

/* Used to communicate with flags_set_1.  */
static rtx flags_set_1_rtx;
static int flags_set_1_set;

static void
mark_flags_life_zones (rtx flags)
{
  int flags_regno;
  int flags_nregs;
  basic_block block;

#ifdef HAVE_cc0
  /* If we found a flags register on a cc0 host, bail.  */
  if (flags == NULL_RTX)
    flags = cc0_rtx;
  else if (flags != cc0_rtx)
    flags = pc_rtx;
#endif

  /* Simple cases first: if no flags, clear all modes.  If confusing,
     mark the entire function as being in a flags shadow.  */
  if (flags == NULL_RTX || flags == pc_rtx)
    {
      enum machine_mode mode = (flags ? HImode : VOIDmode);
      rtx insn;
      for (insn = get_insns (); insn; insn = NEXT_INSN (insn))
	PUT_MODE (insn, mode);
      return;
    }

#ifdef HAVE_cc0
  flags_regno = -1;
  flags_nregs = 1;
#else
  flags_regno = REGNO (flags);
  flags_nregs = hard_regno_nregs[flags_regno][GET_MODE (flags)];
#endif
  flags_set_1_rtx = flags;

  /* Process each basic block.  */
  FOR_EACH_BB_REVERSE (block)
    {
      rtx insn, end;
      int live;

      insn = BB_HEAD (block);
      end = BB_END (block);

      /* Look out for the (unlikely) case of flags being live across
	 basic block boundaries.  */
      live = 0;
#ifndef HAVE_cc0
      {
	int i;
	for (i = 0; i < flags_nregs; ++i)
	  live |= REGNO_REG_SET_P (block->il.rtl->global_live_at_start,
				   flags_regno + i);
      }
#endif

      while (1)
	{
	  /* Process liveness in reverse order of importance --
	     alive, death, birth.  This lets more important info
	     overwrite the mode of lesser info.  */

	  if (INSN_P (insn))
	    {
#ifdef HAVE_cc0
	      /* In the cc0 case, death is not marked in reg notes,
		 but is instead the mere use of cc0 when it is alive.  */
	      if (live && reg_mentioned_p (cc0_rtx, PATTERN (insn)))
		live = 0;
#else
	      /* In the hard reg case, we watch death notes.  */
	      if (live && find_regno_note (insn, REG_DEAD, flags_regno))
		live = 0;
#endif
	      PUT_MODE (insn, (live ? HImode : VOIDmode));

	      /* In either case, birth is denoted simply by its presence
		 as the destination of a set.  */
	      flags_set_1_set = 0;
	      note_stores (PATTERN (insn), flags_set_1, NULL);
	      if (flags_set_1_set)
		{
		  live = 1;
		  PUT_MODE (insn, QImode);
		}
	    }
	  else
	    PUT_MODE (insn, (live ? HImode : VOIDmode));

	  if (insn == end)
	    break;
	  insn = NEXT_INSN (insn);
	}
    }
}

/* A subroutine of mark_flags_life_zones, called through note_stores.  */

static void
flags_set_1 (rtx x, rtx pat, void *data ATTRIBUTE_UNUSED)
{
  if (GET_CODE (pat) == SET
      && reg_overlap_mentioned_p (x, flags_set_1_rtx))
    flags_set_1_set = 1;
}

static int *regno_src_regno;

/* Indicate how good a choice REG (which appears as a source) is to replace
   a destination register with.  The higher the returned value, the better
   the choice.  The main objective is to avoid using a register that is
   a candidate for tying to a hard register, since the output might in
   turn be a candidate to be tied to a different hard register.  */
static int
replacement_quality (rtx reg)
{
  int src_regno;

  /* Bad if this isn't a register at all.  */
  if (!REG_P (reg))
    return 0;

  /* If this register is not meant to get a hard register,
     it is a poor choice.  */
  if (REG_LIVE_LENGTH (REGNO (reg)) < 0)
    return 0;

  src_regno = regno_src_regno[REGNO (reg)];

  /* If it was not copied from another register, it is fine.  */
  if (src_regno < 0)
    return 3;

  /* Copied from a hard register?  */
  if (src_regno < FIRST_PSEUDO_REGISTER)
    return 1;

  /* Copied from a pseudo register - not as bad as from a hard register,
     yet still cumbersome, since the register live length will be lengthened
     when the registers get tied.  */
  return 2;
}

/* Return 1 if INSN might end a basic block.  */

static int perhaps_ends_bb_p (rtx insn)
{
  switch (GET_CODE (insn))
    {
    case CODE_LABEL:
    case JUMP_INSN:
      /* These always end a basic block.  */
      return 1;

    case CALL_INSN:
      /* A CALL_INSN might be the last insn of a basic block, if it is inside
	 an EH region or if there are nonlocal gotos.  Note that this test is
	 very conservative.  */
      if (nonlocal_goto_handler_labels)
	return 1;
      /* Fall through.  */
    default:
      return can_throw_internal (insn);
    }
}

/* INSN is a copy from SRC to DEST, both registers, and SRC does not die
   in INSN.

   Search forward to see if SRC dies before either it or DEST is modified,
   but don't scan past the end of a basic block.  If so, we can replace SRC
   with DEST and let SRC die in INSN.

   This will reduce the number of registers live in that range and may enable
   DEST to be tied to SRC, thus often saving one register in addition to a
   register-register copy.  */

static int
optimize_reg_copy_1 (rtx insn, rtx dest, rtx src)
{
  rtx p, q;
  rtx note;
  rtx dest_death = 0;
  int sregno = REGNO (src);
  int dregno = REGNO (dest);

  /* We don't want to mess with hard regs if register classes are small.  */
  if (sregno == dregno
      || (SMALL_REGISTER_CLASSES
	  && (sregno < FIRST_PSEUDO_REGISTER
	      || dregno < FIRST_PSEUDO_REGISTER))
      /* We don't see all updates to SP if they are in an auto-inc memory
	 reference, so we must disallow this optimization on them.  */
      || sregno == STACK_POINTER_REGNUM || dregno == STACK_POINTER_REGNUM)
    return 0;

  for (p = NEXT_INSN (insn); p; p = NEXT_INSN (p))
    {
      /* ??? We can't scan past the end of a basic block without updating
	 the register lifetime info (REG_DEAD/basic_block_live_at_start).  */
      if (perhaps_ends_bb_p (p))
	break;
      else if (! INSN_P (p))
	continue;

      if (reg_set_p (src, p) || reg_set_p (dest, p)
	  /* If SRC is an asm-declared register, it must not be replaced
	     in any asm.  Unfortunately, the REG_EXPR tree for the asm
	     variable may be absent in the SRC rtx, so we can't check the
	     actual register declaration easily (the asm operand will have
	     it, though).  To avoid complicating the test for a rare case,
	     we just don't perform register replacement for a hard reg
	     mentioned in an asm.  */
	  || (sregno < FIRST_PSEUDO_REGISTER
	      && asm_noperands (PATTERN (p)) >= 0
	      && reg_overlap_mentioned_p (src, PATTERN (p)))
	  /* Don't change hard registers used by a call.  */
	  || (CALL_P (p) && sregno < FIRST_PSEUDO_REGISTER
	      && find_reg_fusage (p, USE, src))
	  /* Don't change a USE of a register.  */
	  || (GET_CODE (PATTERN (p)) == USE
	      && reg_overlap_mentioned_p (src, XEXP (PATTERN (p), 0))))
	break;

      /* See if all of SRC dies in P.  This test is slightly more
	 conservative than it needs to be.  */
      if ((note = find_regno_note (p, REG_DEAD, sregno)) != 0
	  && GET_MODE (XEXP (note, 0)) == GET_MODE (src))
	{
	  int failed = 0;
	  int d_length = 0;
	  int s_length = 0;
	  int d_n_calls = 0;
	  int s_n_calls = 0;

	  /* We can do the optimization.  Scan forward from INSN again,
	     replacing regs as we go.  Set FAILED if a replacement can't
	     be done.  In that case, we can't move the death note for SRC.
	     This should be rare.  */

	  /* Set to stop at next insn.  */
	  for (q = next_real_insn (insn);
	       q != next_real_insn (p);
	       q = next_real_insn (q))
	    {
	      if (reg_overlap_mentioned_p (src, PATTERN (q)))
		{
		  /* If SRC is a hard register, we might miss some
		     overlapping registers with validate_replace_rtx,
		     so we would have to undo it.  We can't if DEST is
		     present in the insn, so fail in that combination
		     of cases.  */
		  if (sregno < FIRST_PSEUDO_REGISTER
		      && reg_mentioned_p (dest, PATTERN (q)))
		    failed = 1;

		  /* Replace all uses and make sure that the register
		     isn't still present.  */
		  else if (validate_replace_rtx (src, dest, q)
			   && (sregno >= FIRST_PSEUDO_REGISTER
			       || ! reg_overlap_mentioned_p (src,
							     PATTERN (q))))
		    ;
		  else
		    {
		      validate_replace_rtx (dest, src, q);
		      failed = 1;
		    }
		}

	      /* For SREGNO, count the total number of insns scanned.
		 For DREGNO, count the total number of insns scanned after
		 passing the death note for DREGNO.  */
	      s_length++;
	      if (dest_death)
		d_length++;

	      /* If the insn in which SRC dies is a CALL_INSN, don't count it
		 as a call that has been crossed.  Otherwise, count it.  */
	      if (q != p && CALL_P (q))
		{
		  /* Similarly, total calls for SREGNO, total calls beyond
		     the death note for DREGNO.  */
		  s_n_calls++;
		  if (dest_death)
		    d_n_calls++;
		}

	      /* If DEST dies here, remove the death note and save it for
		 later.  Make sure ALL of DEST dies here; again, this is
		 overly conservative.  */
	      if (dest_death == 0
		  && (dest_death = find_regno_note (q, REG_DEAD, dregno)) != 0)
		{
		  if (GET_MODE (XEXP (dest_death, 0)) != GET_MODE (dest))
		    failed = 1, dest_death = 0;
		  else
		    remove_note (q, dest_death);
		}
	    }

	  if (! failed)
	    {
	      /* These counters need to be updated if and only if we are
		 going to move the REG_DEAD note.  */
	      if (sregno >= FIRST_PSEUDO_REGISTER)
		{
		  if (REG_LIVE_LENGTH (sregno) >= 0)
		    {
		      REG_LIVE_LENGTH (sregno) -= s_length;
		      /* REG_LIVE_LENGTH is only an approximation after
			 combine if sched is not run, so make sure that we
			 still have a reasonable value.  */
		      if (REG_LIVE_LENGTH (sregno) < 2)
			REG_LIVE_LENGTH (sregno) = 2;
		    }

		  REG_N_CALLS_CROSSED (sregno) -= s_n_calls;
		}

	      /* Move death note of SRC from P to INSN.  */
	      remove_note (p, note);
	      XEXP (note, 1) = REG_NOTES (insn);
	      REG_NOTES (insn) = note;
	    }

	  /* DEST is also dead if INSN has a REG_UNUSED note for DEST.  */
	  if (! dest_death
	      && (dest_death = find_regno_note (insn, REG_UNUSED, dregno)))
	    {
	      PUT_REG_NOTE_KIND (dest_death, REG_DEAD);
	      remove_note (insn, dest_death);
	    }

	  /* Put death note of DEST on P if we saw it die.  */
	  if (dest_death)
	    {
	      XEXP (dest_death, 1) = REG_NOTES (p);
	      REG_NOTES (p) = dest_death;

	      if (dregno >= FIRST_PSEUDO_REGISTER)
		{
		  /* If and only if we are moving the death note for DREGNO,
		     then we need to update its counters.  */
		  if (REG_LIVE_LENGTH (dregno) >= 0)
		    REG_LIVE_LENGTH (dregno) += d_length;
		  REG_N_CALLS_CROSSED (dregno) += d_n_calls;
		}
	    }

	  return ! failed;
	}

      /* If SRC is a hard register which is set or killed in some other
	 way, we can't do this optimization.  */
      else if (sregno < FIRST_PSEUDO_REGISTER
	       && dead_or_set_p (p, src))
	break;
    }
  return 0;
}

/* INSN is a copy of SRC to DEST, in which SRC dies.  See if we now have
   a sequence of insns that modify DEST followed by an insn that sets
   SRC to DEST in which DEST dies, with no prior modification of DEST.
   (There is no need to check if the insns in between actually modify
   DEST.  We should not have cases where DEST is not modified, but
   the optimization is safe if no such modification is detected.)
   In that case, we can replace all uses of DEST, starting with INSN and
   ending with the set of SRC to DEST, with SRC.  We do not do this
   optimization if a CALL_INSN is crossed unless SRC already crosses a
   call or if DEST dies before the copy back to SRC.

   It is assumed that DEST and SRC are pseudos; it is too complicated to do
   this for hard registers since the substitutions we may make might fail.  */

static void
optimize_reg_copy_2 (rtx insn, rtx dest, rtx src)
{
  rtx p, q;
  rtx set;
  int sregno = REGNO (src);
  int dregno = REGNO (dest);

  for (p = NEXT_INSN (insn); p; p = NEXT_INSN (p))
    {
      /* ??? We can't scan past the end of a basic block without updating
	 the register lifetime info (REG_DEAD/basic_block_live_at_start).  */
      if (perhaps_ends_bb_p (p))
	break;
      else if (! INSN_P (p))
	continue;

      set = single_set (p);
      if (set && SET_SRC (set) == dest && SET_DEST (set) == src
	  && find_reg_note (p, REG_DEAD, dest))
	{
	  /* We can do the optimization.  Scan forward from INSN again,
	     replacing regs as we go.  */

	  /* Set to stop at next insn.  */
	  for (q = insn; q != NEXT_INSN (p); q = NEXT_INSN (q))
	    if (INSN_P (q))
	      {
		if (reg_mentioned_p (dest, PATTERN (q)))
		  PATTERN (q) = replace_rtx (PATTERN (q), dest, src);


	      if (CALL_P (q))
		{
		  REG_N_CALLS_CROSSED (dregno)--;
		  REG_N_CALLS_CROSSED (sregno)++;
		}
	      }

	  remove_note (p, find_reg_note (p, REG_DEAD, dest));
	  REG_N_DEATHS (dregno)--;
	  remove_note (insn, find_reg_note (insn, REG_DEAD, src));
	  REG_N_DEATHS (sregno)--;
	  return;
	}

      if (reg_set_p (src, p)
	  || find_reg_note (p, REG_DEAD, dest)
	  || (CALL_P (p) && REG_N_CALLS_CROSSED (sregno) == 0))
	break;
    }
}
/* INSN is a ZERO_EXTEND or SIGN_EXTEND of SRC to DEST.
   Look if SRC dies there, and if it is only set once, by loading
   it from memory.  If so, try to incorporate the zero/sign extension
   into the memory read, change SRC to the mode of DEST, and alter
   the remaining accesses to use the appropriate SUBREG.  This allows
   SRC and DEST to be tied later.  */
static void
optimize_reg_copy_3 (rtx insn, rtx dest, rtx src)
{
  rtx src_reg = XEXP (src, 0);
  int src_no = REGNO (src_reg);
  int dst_no = REGNO (dest);
  rtx p, set;
  enum machine_mode old_mode;

  if (src_no < FIRST_PSEUDO_REGISTER
      || dst_no < FIRST_PSEUDO_REGISTER
      || ! find_reg_note (insn, REG_DEAD, src_reg)
      || REG_N_DEATHS (src_no) != 1
      || REG_N_SETS (src_no) != 1)
    return;
  for (p = PREV_INSN (insn); p && ! reg_set_p (src_reg, p); p = PREV_INSN (p))
    /* ??? We can't scan past the end of a basic block without updating
       the register lifetime info (REG_DEAD/basic_block_live_at_start).  */
    if (perhaps_ends_bb_p (p))
      break;

  if (! p)
    return;

  if (! (set = single_set (p))
      || !MEM_P (SET_SRC (set))
      /* If there's a REG_EQUIV note, this must be an insn that loads an
	 argument.  Prefer keeping the note over doing this optimization.  */
      || find_reg_note (p, REG_EQUIV, NULL_RTX)
      || SET_DEST (set) != src_reg)
    return;

  /* Be conservative: although this optimization is also valid for
     volatile memory references, that could cause trouble in later passes.  */
  if (MEM_VOLATILE_P (SET_SRC (set)))
    return;

  /* Do not use a SUBREG to truncate from one mode to another if truncation
     is not a nop.  */
  if (GET_MODE_BITSIZE (GET_MODE (src_reg)) <= GET_MODE_BITSIZE (GET_MODE (src))
      && !TRULY_NOOP_TRUNCATION (GET_MODE_BITSIZE (GET_MODE (src)),
				 GET_MODE_BITSIZE (GET_MODE (src_reg))))
    return;

  old_mode = GET_MODE (src_reg);
  PUT_MODE (src_reg, GET_MODE (src));
  XEXP (src, 0) = SET_SRC (set);

  /* Include this change in the group so that it's easily undone if
     one of the changes in the group is invalid.  */
  validate_change (p, &SET_SRC (set), src, 1);

  /* Now walk forward making additional replacements.  We want to be able
     to undo all the changes if a later substitution fails.  */
  while (p = NEXT_INSN (p), p != insn)
    {
      if (! INSN_P (p))
	continue;

      /* Make a tentative change.  */
      validate_replace_rtx_group (src_reg,
				  gen_lowpart_SUBREG (old_mode, src_reg),
				  p);
    }

  validate_replace_rtx_group (src, src_reg, insn);

  /* Now see if all the changes are valid.  */
  if (! apply_change_group ())
    {
      /* One or more changes were no good.  Back out everything.  */
      PUT_MODE (src_reg, old_mode);
      XEXP (src, 0) = src_reg;
    }
  else
    {
      rtx note = find_reg_note (p, REG_EQUAL, NULL_RTX);
      if (note)
	remove_note (p, note);
    }
}


/* If we were not able to update the users of src to use dest directly, try
   instead moving the value to dest directly before the operation.  */

static void
copy_src_to_dest (rtx insn, rtx src, rtx dest, int old_max_uid)
{
  rtx seq;
  rtx link;
  rtx next;
  rtx set;
  rtx move_insn;
  rtx *p_insn_notes;
  rtx *p_move_notes;
  int src_regno;
  int dest_regno;
  int bb;
  int insn_uid;
  int move_uid;

  /* A REG_LIVE_LENGTH of -1 indicates the register is equivalent to a constant
     or memory location and is used infrequently; a REG_LIVE_LENGTH of -2 is
     parameter when there is no frame pointer that is not allocated a register.
     For now, we just reject them, rather than incrementing the live length.  */

  if (REG_P (src)
      && REG_LIVE_LENGTH (REGNO (src)) > 0
      && REG_P (dest)
      && REG_LIVE_LENGTH (REGNO (dest)) > 0
      && (set = single_set (insn)) != NULL_RTX
      && !reg_mentioned_p (dest, SET_SRC (set))
      && GET_MODE (src) == GET_MODE (dest))
    {
      int old_num_regs = reg_rtx_no;

      /* Generate the src->dest move.  */
      start_sequence ();
      emit_move_insn (dest, src);
      seq = get_insns ();
      end_sequence ();
      /* If this sequence uses new registers, we may not use it.  */
      if (old_num_regs != reg_rtx_no
	  || ! validate_replace_rtx (src, dest, insn))
	{
	  /* We have to restore reg_rtx_no to its old value, lest
	     recompute_reg_usage will try to compute the usage of the
	     new regs, yet reg_n_info is not valid for them.  */
	  reg_rtx_no = old_num_regs;
	  return;
	}
      emit_insn_before (seq, insn);
      move_insn = PREV_INSN (insn);
      p_move_notes = &REG_NOTES (move_insn);
      p_insn_notes = &REG_NOTES (insn);

      /* Move any notes mentioning src to the move instruction.  */
      for (link = REG_NOTES (insn); link != NULL_RTX; link = next)
	{
	  next = XEXP (link, 1);
	  if (XEXP (link, 0) == src)
	    {
	      *p_move_notes = link;
	      p_move_notes = &XEXP (link, 1);
	    }
	  else
	    {
	      *p_insn_notes = link;
	      p_insn_notes = &XEXP (link, 1);
	    }
	}

      *p_move_notes = NULL_RTX;
      *p_insn_notes = NULL_RTX;

      /* Is the insn the head of a basic block?  If so extend it.  */
      insn_uid = INSN_UID (insn);
      move_uid = INSN_UID (move_insn);
      if (insn_uid < old_max_uid)
	{
	  bb = regmove_bb_head[insn_uid];
	  if (bb >= 0)
	    {
	      BB_HEAD (BASIC_BLOCK (bb)) = move_insn;
	      regmove_bb_head[insn_uid] = -1;
	    }
	}

      /* Update the various register tables.  */
      dest_regno = REGNO (dest);
      REG_N_SETS (dest_regno) ++;
      REG_LIVE_LENGTH (dest_regno)++;
      if (REGNO_FIRST_UID (dest_regno) == insn_uid)
	REGNO_FIRST_UID (dest_regno) = move_uid;

      src_regno = REGNO (src);
      if (! find_reg_note (move_insn, REG_DEAD, src))
	REG_LIVE_LENGTH (src_regno)++;

      if (REGNO_FIRST_UID (src_regno) == insn_uid)
	REGNO_FIRST_UID (src_regno) = move_uid;

      if (REGNO_LAST_UID (src_regno) == insn_uid)
	REGNO_LAST_UID (src_regno) = move_uid;
    }
}

/* reg_set_in_bb[REGNO] points to basic block iff the register is set
   only once in the given block and has REG_EQUAL note.  */

basic_block *reg_set_in_bb;

/* Size of reg_set_in_bb array.  */
static unsigned int max_reg_computed;


/* Return whether REG is set in only one location, and is set to a
   constant, but is set in a different basic block from INSN (an
   instructions which uses REG).  In this case REG is equivalent to a
   constant, and we don't want to break that equivalence, because that
   may increase register pressure and make reload harder.  If REG is
   set in the same basic block as INSN, we don't worry about it,
   because we'll probably need a register anyhow (??? but what if REG
   is used in a different basic block as well as this one?).  FIRST is
   the first insn in the function.  */

static bool
reg_is_remote_constant_p (rtx reg, rtx insn)
{
  basic_block bb;
  rtx p;
  int max;

  if (!reg_set_in_bb)
    {
      max_reg_computed = max = max_reg_num ();
      reg_set_in_bb = xcalloc (max, sizeof (*reg_set_in_bb));

      FOR_EACH_BB (bb)
	for (p = BB_HEAD (bb); p != NEXT_INSN (BB_END (bb));
	     p = NEXT_INSN (p))
	{
	  rtx s;

	  if (!INSN_P (p))
	    continue;
	  s = single_set (p);
	  /* This is the instruction which sets REG.  If there is a
	     REG_EQUAL note, then REG is equivalent to a constant.  */
	  if (s != 0
	      && REG_P (SET_DEST (s))
	      && REG_N_SETS (REGNO (SET_DEST (s))) == 1
	      && find_reg_note (p, REG_EQUAL, NULL_RTX))
	    reg_set_in_bb[REGNO (SET_DEST (s))] = bb;
	}
    }
  gcc_assert (REGNO (reg) < max_reg_computed);
  if (reg_set_in_bb[REGNO (reg)] == NULL)
    return false;
  if (reg_set_in_bb[REGNO (reg)] != BLOCK_FOR_INSN (insn))
    return true;
  /* Look for the set.  */
  for (p = BB_HEAD (BLOCK_FOR_INSN (insn)); p != insn; p = NEXT_INSN (p))
    {
      rtx s;

      if (!INSN_P (p))
	continue;
      s = single_set (p);
      if (s != 0
	  && REG_P (SET_DEST (s)) && REGNO (SET_DEST (s)) == REGNO (reg))
	{
	  /* The register is set in the same basic block.  */
	  return false;
	}
    }
  return true;
}

/* INSN is adding a CONST_INT to a REG.  We search backwards looking for
   another add immediate instruction with the same source and dest registers,
   and if we find one, we change INSN to an increment, and return 1.  If
   no changes are made, we return 0.

   This changes
     (set (reg100) (plus reg1 offset1))
     ...
     (set (reg100) (plus reg1 offset2))
   to
     (set (reg100) (plus reg1 offset1))
     ...
     (set (reg100) (plus reg100 offset2-offset1))  */

/* ??? What does this comment mean?  */
/* cse disrupts preincrement / postdecrement sequences when it finds a
   hard register as ultimate source, like the frame pointer.  */

static int
fixup_match_2 (rtx insn, rtx dst, rtx src, rtx offset)
{
  rtx p, dst_death = 0;
  int length, num_calls = 0;

  /* If SRC dies in INSN, we'd have to move the death note.  This is
     considered to be very unlikely, so we just skip the optimization
     in this case.  */
  if (find_regno_note (insn, REG_DEAD, REGNO (src)))
    return 0;

  /* Scan backward to find the first instruction that sets DST.  */

  for (length = 0, p = PREV_INSN (insn); p; p = PREV_INSN (p))
    {
      rtx pset;

      /* ??? We can't scan past the end of a basic block without updating
	 the register lifetime info (REG_DEAD/basic_block_live_at_start).  */
      if (perhaps_ends_bb_p (p))
	break;
      else if (! INSN_P (p))
	continue;

      if (find_regno_note (p, REG_DEAD, REGNO (dst)))
	dst_death = p;
      if (! dst_death)
	length++;

      pset = single_set (p);
      if (pset && SET_DEST (pset) == dst
	  && GET_CODE (SET_SRC (pset)) == PLUS
	  && XEXP (SET_SRC (pset), 0) == src
	  && GET_CODE (XEXP (SET_SRC (pset), 1)) == CONST_INT)
	{
	  HOST_WIDE_INT newconst
	    = INTVAL (offset) - INTVAL (XEXP (SET_SRC (pset), 1));
	  rtx add = gen_add3_insn (dst, dst, GEN_INT (newconst));

	  if (add && validate_change (insn, &PATTERN (insn), add, 0))
	    {
	      /* Remove the death note for DST from DST_DEATH.  */
	      if (dst_death)
		{
		  remove_death (REGNO (dst), dst_death);
		  REG_LIVE_LENGTH (REGNO (dst)) += length;
		  REG_N_CALLS_CROSSED (REGNO (dst)) += num_calls;
		}

	      if (dump_file)
		fprintf (dump_file,
			 "Fixed operand of insn %d.\n",
			  INSN_UID (insn));

#ifdef AUTO_INC_DEC
	      for (p = PREV_INSN (insn); p; p = PREV_INSN (p))
		{
		  if (LABEL_P (p)
		      || JUMP_P (p))
		    break;
		  if (! INSN_P (p))
		    continue;
		  if (reg_overlap_mentioned_p (dst, PATTERN (p)))
		    {
		      if (try_auto_increment (p, insn, 0, dst, newconst, 0))
			return 1;
		      break;
		    }
		}
	      for (p = NEXT_INSN (insn); p; p = NEXT_INSN (p))
		{
		  if (LABEL_P (p)
		      || JUMP_P (p))
		    break;
		  if (! INSN_P (p))
		    continue;
		  if (reg_overlap_mentioned_p (dst, PATTERN (p)))
		    {
		      try_auto_increment (p, insn, 0, dst, newconst, 1);
		      break;
		    }
		}
#endif
	      return 1;
	    }
	}

      if (reg_set_p (dst, PATTERN (p)))
	break;

      /* If we have passed a call instruction, and the
         pseudo-reg SRC is not already live across a call,
         then don't perform the optimization.  */
      /* reg_set_p is overly conservative for CALL_INSNS, thinks that all
	 hard regs are clobbered.  Thus, we only use it for src for
	 non-call insns.  */
      if (CALL_P (p))
	{
	  if (! dst_death)
	    num_calls++;

	  if (REG_N_CALLS_CROSSED (REGNO (src)) == 0)
	    break;

	  if (call_used_regs [REGNO (dst)]
	      || find_reg_fusage (p, CLOBBER, dst))
	    break;
	}
      else if (reg_set_p (src, PATTERN (p)))
	break;
    }

  return 0;
}

/* Main entry for the register move optimization.
   F is the first instruction.
   NREGS is one plus the highest pseudo-reg number used in the instruction.
   REGMOVE_DUMP_FILE is a stream for output of a trace of actions taken
   (or 0 if none should be output).  */

static void
regmove_optimize (rtx f, int nregs)
{
  int old_max_uid = get_max_uid ();
  rtx insn;
  struct match match;
  int pass;
  int i;
  rtx copy_src, copy_dst;
  basic_block bb;

  /* ??? Hack.  Regmove doesn't examine the CFG, and gets mightily
     confused by non-call exceptions ending blocks.  */
  if (flag_non_call_exceptions)
    return;

  /* Find out where a potential flags register is live, and so that we
     can suppress some optimizations in those zones.  */
  mark_flags_life_zones (discover_flags_reg ());

  regno_src_regno = XNEWVEC (int, nregs);
  for (i = nregs; --i >= 0; ) regno_src_regno[i] = -1;

  regmove_bb_head = XNEWVEC (int, old_max_uid + 1);
  for (i = old_max_uid; i >= 0; i--) regmove_bb_head[i] = -1;
  FOR_EACH_BB (bb)
    regmove_bb_head[INSN_UID (BB_HEAD (bb))] = bb->index;

  /* A forward/backward pass.  Replace output operands with input operands.  */

  for (pass = 0; pass <= 2; pass++)
    {
      if (! flag_regmove && pass >= flag_expensive_optimizations)
	goto done;

      if (dump_file)
	fprintf (dump_file, "Starting %s pass...\n",
		 pass ? "backward" : "forward");

      for (insn = pass ? get_last_insn () : f; insn;
	   insn = pass ? PREV_INSN (insn) : NEXT_INSN (insn))
	{
	  rtx set;
	  int op_no, match_no;

	  set = single_set (insn);
	  if (! set)
	    continue;

	  if (flag_expensive_optimizations && ! pass
	      && (GET_CODE (SET_SRC (set)) == SIGN_EXTEND
		  || GET_CODE (SET_SRC (set)) == ZERO_EXTEND)
	      && REG_P (XEXP (SET_SRC (set), 0))
	      && REG_P (SET_DEST (set)))
	    optimize_reg_copy_3 (insn, SET_DEST (set), SET_SRC (set));

	  if (flag_expensive_optimizations && ! pass
	      && REG_P (SET_SRC (set))
	      && REG_P (SET_DEST (set)))
	    {
	      /* If this is a register-register copy where SRC is not dead,
		 see if we can optimize it.  If this optimization succeeds,
		 it will become a copy where SRC is dead.  */
	      if ((find_reg_note (insn, REG_DEAD, SET_SRC (set))
		   || optimize_reg_copy_1 (insn, SET_DEST (set), SET_SRC (set)))
		  && REGNO (SET_DEST (set)) >= FIRST_PSEUDO_REGISTER)
		{
		  /* Similarly for a pseudo-pseudo copy when SRC is dead.  */
		  if (REGNO (SET_SRC (set)) >= FIRST_PSEUDO_REGISTER)
		    optimize_reg_copy_2 (insn, SET_DEST (set), SET_SRC (set));
		  if (regno_src_regno[REGNO (SET_DEST (set))] < 0
		      && SET_SRC (set) != SET_DEST (set))
		    {
		      int srcregno = REGNO (SET_SRC (set));
		      if (regno_src_regno[srcregno] >= 0)
			srcregno = regno_src_regno[srcregno];
		      regno_src_regno[REGNO (SET_DEST (set))] = srcregno;
		    }
		}
	    }
	  if (! flag_regmove)
	    continue;

	  if (! find_matches (insn, &match))
	    continue;

	  /* Now scan through the operands looking for a source operand
	     which is supposed to match the destination operand.
	     Then scan forward for an instruction which uses the dest
	     operand.
	     If it dies there, then replace the dest in both operands with
	     the source operand.  */

	  for (op_no = 0; op_no < recog_data.n_operands; op_no++)
	    {
	      rtx src, dst, src_subreg;
	      enum reg_class src_class, dst_class;

	      match_no = match.with[op_no];

	      /* Nothing to do if the two operands aren't supposed to match.  */
	      if (match_no < 0)
		continue;

	      src = recog_data.operand[op_no];
	      dst = recog_data.operand[match_no];

	      if (!REG_P (src))
		continue;

	      src_subreg = src;
	      if (GET_CODE (dst) == SUBREG
		  && GET_MODE_SIZE (GET_MODE (dst))
		     >= GET_MODE_SIZE (GET_MODE (SUBREG_REG (dst))))
		{
		  dst = SUBREG_REG (dst);
		  src_subreg = lowpart_subreg (GET_MODE (dst),
					       src, GET_MODE (src));
		  if (!src_subreg)
		    continue;
		}
	      if (!REG_P (dst)
		  || REGNO (dst) < FIRST_PSEUDO_REGISTER)
		continue;

	      if (REGNO (src) < FIRST_PSEUDO_REGISTER)
		{
		  if (match.commutative[op_no] < op_no)
		    regno_src_regno[REGNO (dst)] = REGNO (src);
		  continue;
		}

	      if (REG_LIVE_LENGTH (REGNO (src)) < 0)
		continue;

	      /* op_no/src must be a read-only operand, and
		 match_operand/dst must be a write-only operand.  */
	      if (match.use[op_no] != READ
		  || match.use[match_no] != WRITE)
		continue;

	      if (match.early_clobber[match_no]
		  && count_occurrences (PATTERN (insn), src, 0) > 1)
		continue;

	      /* Make sure match_operand is the destination.  */
	      if (recog_data.operand[match_no] != SET_DEST (set))
		continue;

	      /* If the operands already match, then there is nothing to do.  */
	      if (operands_match_p (src, dst))
		continue;

	      /* But in the commutative case, we might find a better match.  */
	      if (match.commutative[op_no] >= 0)
		{
		  rtx comm = recog_data.operand[match.commutative[op_no]];
		  if (operands_match_p (comm, dst)
		      && (replacement_quality (comm)
			  >= replacement_quality (src)))
		    continue;
		}

	      src_class = reg_preferred_class (REGNO (src));
	      dst_class = reg_preferred_class (REGNO (dst));
	      if (! regclass_compatible_p (src_class, dst_class))
		continue;

	      if (GET_MODE (src) != GET_MODE (dst))
		continue;

	      if (fixup_match_1 (insn, set, src, src_subreg, dst, pass,
				 op_no, match_no))
		break;
	    }
	}
    }

  /* A backward pass.  Replace input operands with output operands.  */

  if (dump_file)
    fprintf (dump_file, "Starting backward pass...\n");

  for (insn = get_last_insn (); insn; insn = PREV_INSN (insn))
    {
      if (INSN_P (insn))
	{
	  int op_no, match_no;
	  int success = 0;

	  if (! find_matches (insn, &match))
	    continue;

	  /* Now scan through the operands looking for a destination operand
	     which is supposed to match a source operand.
	     Then scan backward for an instruction which sets the source
	     operand.  If safe, then replace the source operand with the
	     dest operand in both instructions.  */

	  copy_src = NULL_RTX;
	  copy_dst = NULL_RTX;
	  for (op_no = 0; op_no < recog_data.n_operands; op_no++)
	    {
	      rtx set, p, src, dst;
	      rtx src_note, dst_note;
	      int num_calls = 0;
	      enum reg_class src_class, dst_class;
	      int length;

	      match_no = match.with[op_no];

	      /* Nothing to do if the two operands aren't supposed to match.  */
	      if (match_no < 0)
		continue;

	      dst = recog_data.operand[match_no];
	      src = recog_data.operand[op_no];

	      if (!REG_P (src))
		continue;

	      if (!REG_P (dst)
		  || REGNO (dst) < FIRST_PSEUDO_REGISTER
		  || REG_LIVE_LENGTH (REGNO (dst)) < 0
		  || GET_MODE (src) != GET_MODE (dst))
		continue;

	      /* If the operands already match, then there is nothing to do.  */
	      if (operands_match_p (src, dst))
		continue;

	      if (match.commutative[op_no] >= 0)
		{
		  rtx comm = recog_data.operand[match.commutative[op_no]];
		  if (operands_match_p (comm, dst))
		    continue;
		}

	      set = single_set (insn);
	      if (! set)
		continue;

	      /* Note that single_set ignores parts of a parallel set for
		 which one of the destinations is REG_UNUSED.  We can't
		 handle that here, since we can wind up rewriting things
		 such that a single register is set twice within a single
		 parallel.  */
	      if (reg_set_p (src, insn))
		continue;

	      /* match_no/dst must be a write-only operand, and
		 operand_operand/src must be a read-only operand.  */
	      if (match.use[op_no] != READ
		  || match.use[match_no] != WRITE)
		continue;

	      if (match.early_clobber[match_no]
		  && count_occurrences (PATTERN (insn), src, 0) > 1)
		continue;

	      /* Make sure match_no is the destination.  */
	      if (recog_data.operand[match_no] != SET_DEST (set))
		continue;

	      if (REGNO (src) < FIRST_PSEUDO_REGISTER)
		{
		  if (GET_CODE (SET_SRC (set)) == PLUS
		      && GET_CODE (XEXP (SET_SRC (set), 1)) == CONST_INT
		      && XEXP (SET_SRC (set), 0) == src
		      && fixup_match_2 (insn, dst, src,
					XEXP (SET_SRC (set), 1)))
		    break;
		  continue;
		}
	      src_class = reg_preferred_class (REGNO (src));
	      dst_class = reg_preferred_class (REGNO (dst));

	      if (! (src_note = find_reg_note (insn, REG_DEAD, src)))
		{
		  /* We used to force the copy here like in other cases, but
		     it produces worse code, as it eliminates no copy
		     instructions and the copy emitted will be produced by
		     reload anyway.  On patterns with multiple alternatives,
		     there may be better solution available.

		     In particular this change produced slower code for numeric
		     i387 programs.  */

		  continue;
		}

	      if (! regclass_compatible_p (src_class, dst_class))
		{
		  if (!copy_src)
		    {
		      copy_src = src;
		      copy_dst = dst;
		    }
		  continue;
		}

	      /* Can not modify an earlier insn to set dst if this insn
		 uses an old value in the source.  */
	      if (reg_overlap_mentioned_p (dst, SET_SRC (set)))
		{
		  if (!copy_src)
		    {
		      copy_src = src;
		      copy_dst = dst;
		    }
		  continue;
		}

	      /* If src is set once in a different basic block,
		 and is set equal to a constant, then do not use
		 it for this optimization, as this would make it
		 no longer equivalent to a constant.  */

	      if (reg_is_remote_constant_p (src, insn))
		{
		  if (!copy_src)
		    {
		      copy_src = src;
		      copy_dst = dst;
		    }
		  continue;
		}


	      if (dump_file)
		fprintf (dump_file,
			 "Could fix operand %d of insn %d matching operand %d.\n",
			 op_no, INSN_UID (insn), match_no);

	      /* Scan backward to find the first instruction that uses
		 the input operand.  If the operand is set here, then
		 replace it in both instructions with match_no.  */

	      for (length = 0, p = PREV_INSN (insn); p; p = PREV_INSN (p))
		{
		  rtx pset;

		  /* ??? We can't scan past the end of a basic block without
		     updating the register lifetime info
		     (REG_DEAD/basic_block_live_at_start).  */
		  if (perhaps_ends_bb_p (p))
		    break;
		  else if (! INSN_P (p))
		    continue;

		  length++;

		  /* ??? See if all of SRC is set in P.  This test is much
		     more conservative than it needs to be.  */
		  pset = single_set (p);
		  if (pset && SET_DEST (pset) == src)
		    {
		      /* We use validate_replace_rtx, in case there
			 are multiple identical source operands.  All of
			 them have to be changed at the same time.  */
		      if (validate_replace_rtx (src, dst, insn))
			{
			  if (validate_change (p, &SET_DEST (pset),
					       dst, 0))
			    success = 1;
			  else
			    {
			      /* Change all source operands back.
				 This modifies the dst as a side-effect.  */
			      validate_replace_rtx (dst, src, insn);
			      /* Now make sure the dst is right.  */
			      validate_change (insn,
					       recog_data.operand_loc[match_no],
					       dst, 0);
			    }
			}
		      break;
		    }

		  if (reg_overlap_mentioned_p (src, PATTERN (p))
		      || reg_overlap_mentioned_p (dst, PATTERN (p)))
		    break;

		  /* If we have passed a call instruction, and the
		     pseudo-reg DST is not already live across a call,
		     then don't perform the optimization.  */
		  if (CALL_P (p))
		    {
		      num_calls++;

		      if (REG_N_CALLS_CROSSED (REGNO (dst)) == 0)
			break;
		    }
		}

	      if (success)
		{
		  int dstno, srcno;

		  /* Remove the death note for SRC from INSN.  */
		  remove_note (insn, src_note);
		  /* Move the death note for SRC to P if it is used
		     there.  */
		  if (reg_overlap_mentioned_p (src, PATTERN (p)))
		    {
		      XEXP (src_note, 1) = REG_NOTES (p);
		      REG_NOTES (p) = src_note;
		    }
		  /* If there is a REG_DEAD note for DST on P, then remove
		     it, because DST is now set there.  */
		  if ((dst_note = find_reg_note (p, REG_DEAD, dst)))
		    remove_note (p, dst_note);

		  dstno = REGNO (dst);
		  srcno = REGNO (src);

		  REG_N_SETS (dstno)++;
		  REG_N_SETS (srcno)--;

		  REG_N_CALLS_CROSSED (dstno) += num_calls;
		  REG_N_CALLS_CROSSED (srcno) -= num_calls;

		  REG_LIVE_LENGTH (dstno) += length;
		  if (REG_LIVE_LENGTH (srcno) >= 0)
		    {
		      REG_LIVE_LENGTH (srcno) -= length;
		      /* REG_LIVE_LENGTH is only an approximation after
			 combine if sched is not run, so make sure that we
			 still have a reasonable value.  */
		      if (REG_LIVE_LENGTH (srcno) < 2)
			REG_LIVE_LENGTH (srcno) = 2;
		    }

		  if (dump_file)
		    fprintf (dump_file,
			     "Fixed operand %d of insn %d matching operand %d.\n",
			     op_no, INSN_UID (insn), match_no);

		  break;
		}
	    }

	  /* If we weren't able to replace any of the alternatives, try an
	     alternative approach of copying the source to the destination.  */
	  if (!success && copy_src != NULL_RTX)
	    copy_src_to_dest (insn, copy_src, copy_dst, old_max_uid);

	}
    }

  /* In fixup_match_1, some insns may have been inserted after basic block
     ends.  Fix that here.  */
  FOR_EACH_BB (bb)
    {
      rtx end = BB_END (bb);
      rtx new = end;
      rtx next = NEXT_INSN (new);
      while (next != 0 && INSN_UID (next) >= old_max_uid
	     && (bb->next_bb == EXIT_BLOCK_PTR || BB_HEAD (bb->next_bb) != next))
	new = next, next = NEXT_INSN (new);
      BB_END (bb) = new;
    }

 done:
  /* Clean up.  */
  free (regno_src_regno);
  free (regmove_bb_head);
  if (reg_set_in_bb)
    {
      free (reg_set_in_bb);
      reg_set_in_bb = NULL;
    }
}

/* Returns nonzero if INSN's pattern has matching constraints for any operand.
   Returns 0 if INSN can't be recognized, or if the alternative can't be
   determined.

   Initialize the info in MATCHP based on the constraints.  */

static int
find_matches (rtx insn, struct match *matchp)
{
  int likely_spilled[MAX_RECOG_OPERANDS];
  int op_no;
  int any_matches = 0;

  extract_insn (insn);
  if (! constrain_operands (0))
    return 0;

  /* Must initialize this before main loop, because the code for
     the commutative case may set matches for operands other than
     the current one.  */
  for (op_no = recog_data.n_operands; --op_no >= 0; )
    matchp->with[op_no] = matchp->commutative[op_no] = -1;

  for (op_no = 0; op_no < recog_data.n_operands; op_no++)
    {
      const char *p;
      char c;
      int i = 0;

      p = recog_data.constraints[op_no];

      likely_spilled[op_no] = 0;
      matchp->use[op_no] = READ;
      matchp->early_clobber[op_no] = 0;
      if (*p == '=')
	matchp->use[op_no] = WRITE;
      else if (*p == '+')
	matchp->use[op_no] = READWRITE;

      for (;*p && i < which_alternative; p++)
	if (*p == ',')
	  i++;

      while ((c = *p) != '\0' && c != ',')
	{
	  switch (c)
	    {
	    case '=':
	      break;
	    case '+':
	      break;
	    case '&':
	      matchp->early_clobber[op_no] = 1;
	      break;
	    case '%':
	      matchp->commutative[op_no] = op_no + 1;
	      matchp->commutative[op_no + 1] = op_no;
	      break;

	    case '0': case '1': case '2': case '3': case '4':
	    case '5': case '6': case '7': case '8': case '9':
	      {
		char *end;
		unsigned long match_ul = strtoul (p, &end, 10);
		int match = match_ul;

		p = end;

		if (match < op_no && likely_spilled[match])
		  continue;
		matchp->with[op_no] = match;
		any_matches = 1;
		if (matchp->commutative[op_no] >= 0)
		  matchp->with[matchp->commutative[op_no]] = match;
	      }
	    continue;

	  case 'a': case 'b': case 'c': case 'd': case 'e': case 'f': case 'h':
	  case 'j': case 'k': case 'l': case 'p': case 'q': case 't': case 'u':
	  case 'v': case 'w': case 'x': case 'y': case 'z': case 'A': case 'B':
	  case 'C': case 'D': case 'W': case 'Y': case 'Z':
	    if (CLASS_LIKELY_SPILLED_P (REG_CLASS_FROM_CONSTRAINT ((unsigned char) c, p) ))
	      likely_spilled[op_no] = 1;
	    break;
	  }
	  p += CONSTRAINT_LEN (c, p);
	}
    }
  return any_matches;
}

/* Try to replace all occurrences of DST_REG with SRC in LOC, that is
   assumed to be in INSN.  */

static void
replace_in_call_usage (rtx *loc, unsigned int dst_reg, rtx src, rtx insn)
{
  rtx x = *loc;
  enum rtx_code code;
  const char *fmt;
  int i, j;

  if (! x)
    return;

  code = GET_CODE (x);
  if (code == REG)
    {
      if (REGNO (x) != dst_reg)
	return;

      validate_change (insn, loc, src, 1);

      return;
    }

  /* Process each of our operands recursively.  */
  fmt = GET_RTX_FORMAT (code);
  for (i = 0; i < GET_RTX_LENGTH (code); i++, fmt++)
    if (*fmt == 'e')
      replace_in_call_usage (&XEXP (x, i), dst_reg, src, insn);
    else if (*fmt == 'E')
      for (j = 0; j < XVECLEN (x, i); j++)
	replace_in_call_usage (& XVECEXP (x, i, j), dst_reg, src, insn);
}

/* Try to replace output operand DST in SET, with input operand SRC.  SET is
   the only set in INSN.  INSN has just been recognized and constrained.
   SRC is operand number OPERAND_NUMBER in INSN.
   DST is operand number MATCH_NUMBER in INSN.
   If BACKWARD is nonzero, we have been called in a backward pass.
   Return nonzero for success.  */

static int
fixup_match_1 (rtx insn, rtx set, rtx src, rtx src_subreg, rtx dst,
	       int backward, int operand_number, int match_number)
{
  rtx p;
  rtx post_inc = 0, post_inc_set = 0, search_end = 0;
  int success = 0;
  int num_calls = 0, s_num_calls = 0;
  enum rtx_code code = NOTE;
  HOST_WIDE_INT insn_const = 0, newconst = 0;
  rtx overlap = 0; /* need to move insn ? */
  rtx src_note = find_reg_note (insn, REG_DEAD, src), dst_note = NULL_RTX;
  int length, s_length;

  if (! src_note)
    {
      /* Look for (set (regX) (op regA constX))
		  (set (regY) (op regA constY))
	 and change that to
		  (set (regA) (op regA constX)).
		  (set (regY) (op regA constY-constX)).
	 This works for add and shift operations, if
	 regA is dead after or set by the second insn.  */

      code = GET_CODE (SET_SRC (set));
      if ((code == PLUS || code == LSHIFTRT
	   || code == ASHIFT || code == ASHIFTRT)
	  && XEXP (SET_SRC (set), 0) == src
	  && GET_CODE (XEXP (SET_SRC (set), 1)) == CONST_INT)
	insn_const = INTVAL (XEXP (SET_SRC (set), 1));
      else if (! stable_and_no_regs_but_for_p (SET_SRC (set), src, dst))
	return 0;
      else
	/* We might find a src_note while scanning.  */
	code = NOTE;
    }

  if (dump_file)
    fprintf (dump_file,
	     "Could fix operand %d of insn %d matching operand %d.\n",
	     operand_number, INSN_UID (insn), match_number);

  /* If SRC is equivalent to a constant set in a different basic block,
     then do not use it for this optimization.  We want the equivalence
     so that if we have to reload this register, we can reload the
     constant, rather than extending the lifespan of the register.  */
  if (reg_is_remote_constant_p (src, insn))
    return 0;

  /* Scan forward to find the next instruction that
     uses the output operand.  If the operand dies here,
     then replace it in both instructions with
     operand_number.  */

  for (length = s_length = 0, p = NEXT_INSN (insn); p; p = NEXT_INSN (p))
    {
      if (CALL_P (p))
	replace_in_call_usage (& CALL_INSN_FUNCTION_USAGE (p),
			       REGNO (dst), src, p);

      /* ??? We can't scan past the end of a basic block without updating
	 the register lifetime info (REG_DEAD/basic_block_live_at_start).  */
      if (perhaps_ends_bb_p (p))
	break;
      else if (! INSN_P (p))
	continue;

      length++;
      if (src_note)
	s_length++;

      if (reg_set_p (src, p) || reg_set_p (dst, p)
	  || (GET_CODE (PATTERN (p)) == USE
	      && reg_overlap_mentioned_p (src, XEXP (PATTERN (p), 0))))
	break;

      /* See if all of DST dies in P.  This test is
	 slightly more conservative than it needs to be.  */
      if ((dst_note = find_regno_note (p, REG_DEAD, REGNO (dst)))
	  && (GET_MODE (XEXP (dst_note, 0)) == GET_MODE (dst)))
	{
	  /* If we would be moving INSN, check that we won't move it
	     into the shadow of a live a live flags register.  */
	  /* ??? We only try to move it in front of P, although
		 we could move it anywhere between OVERLAP and P.  */
	  if (overlap && GET_MODE (PREV_INSN (p)) != VOIDmode)
	    break;

	  if (! src_note)
	    {
	      rtx q;
	      rtx set2 = NULL_RTX;

	      /* If an optimization is done, the value of SRC while P
		 is executed will be changed.  Check that this is OK.  */
	      if (reg_overlap_mentioned_p (src, PATTERN (p)))
		break;
	      for (q = p; q; q = NEXT_INSN (q))
		{
		  /* ??? We can't scan past the end of a basic block without
		     updating the register lifetime info
		     (REG_DEAD/basic_block_live_at_start).  */
		  if (perhaps_ends_bb_p (q))
		    {
		      q = 0;
		      break;
		    }
		  else if (! INSN_P (q))
		    continue;
		  else if (reg_overlap_mentioned_p (src, PATTERN (q))
			   || reg_set_p (src, q))
		    break;
		}
	      if (q)
		set2 = single_set (q);
	      if (! q || ! set2 || GET_CODE (SET_SRC (set2)) != code
		  || XEXP (SET_SRC (set2), 0) != src
		  || GET_CODE (XEXP (SET_SRC (set2), 1)) != CONST_INT
		  || (SET_DEST (set2) != src
		      && ! find_reg_note (q, REG_DEAD, src)))
		{
		  /* If this is a PLUS, we can still save a register by doing
		     src += insn_const;
		     P;
		     src -= insn_const; .
		     This also gives opportunities for subsequent
		     optimizations in the backward pass, so do it there.  */
		  if (code == PLUS && backward
		      /* Don't do this if we can likely tie DST to SET_DEST
			 of P later; we can't do this tying here if we got a
			 hard register.  */
		      && ! (dst_note && ! REG_N_CALLS_CROSSED (REGNO (dst))
			    && single_set (p)
			    && REG_P (SET_DEST (single_set (p)))
			    && (REGNO (SET_DEST (single_set (p)))
				< FIRST_PSEUDO_REGISTER))
		      /* We may only emit an insn directly after P if we
			 are not in the shadow of a live flags register.  */
		      && GET_MODE (p) == VOIDmode)
		    {
		      search_end = q;
		      q = insn;
		      set2 = set;
		      newconst = -insn_const;
		      code = MINUS;
		    }
		  else
		    break;
		}
	      else
		{
		  newconst = INTVAL (XEXP (SET_SRC (set2), 1)) - insn_const;
		  /* Reject out of range shifts.  */
		  if (code != PLUS
		      && (newconst < 0
			  || ((unsigned HOST_WIDE_INT) newconst
			      >= (GET_MODE_BITSIZE (GET_MODE
						    (SET_SRC (set2)))))))
		    break;
		  if (code == PLUS)
		    {
		      post_inc = q;
		      if (SET_DEST (set2) != src)
			post_inc_set = set2;
		    }
		}
	      /* We use 1 as last argument to validate_change so that all
		 changes are accepted or rejected together by apply_change_group
		 when it is called by validate_replace_rtx .  */
	      validate_change (q, &XEXP (SET_SRC (set2), 1),
			       GEN_INT (newconst), 1);
	    }
	  validate_change (insn, recog_data.operand_loc[match_number], src, 1);
	  if (validate_replace_rtx (dst, src_subreg, p))
	    success = 1;
	  break;
	}

      if (reg_overlap_mentioned_p (dst, PATTERN (p)))
	break;
      if (! src_note && reg_overlap_mentioned_p (src, PATTERN (p)))
	{
	  /* INSN was already checked to be movable wrt. the registers that it
	     sets / uses when we found no REG_DEAD note for src on it, but it
	     still might clobber the flags register.  We'll have to check that
	     we won't insert it into the shadow of a live flags register when
	     we finally know where we are to move it.  */
	  overlap = p;
	  src_note = find_reg_note (p, REG_DEAD, src);
	}

      /* If we have passed a call instruction, and the pseudo-reg SRC is not
	 already live across a call, then don't perform the optimization.  */
      if (CALL_P (p))
	{
	  if (REG_N_CALLS_CROSSED (REGNO (src)) == 0)
	    break;

	  num_calls++;

	  if (src_note)
	    s_num_calls++;

	}
    }

  if (! success)
    return 0;

  /* Remove the death note for DST from P.  */
  remove_note (p, dst_note);
  if (code == MINUS)
    {
      post_inc = emit_insn_after (copy_rtx (PATTERN (insn)), p);
      if ((HAVE_PRE_INCREMENT || HAVE_PRE_DECREMENT)
	  && search_end
	  && try_auto_increment (search_end, post_inc, 0, src, newconst, 1))
	post_inc = 0;
      validate_change (insn, &XEXP (SET_SRC (set), 1), GEN_INT (insn_const), 0);
      REG_N_SETS (REGNO (src))++;
      REG_LIVE_LENGTH (REGNO (src))++;
    }
  if (overlap)
    {
      /* The lifetime of src and dest overlap,
	 but we can change this by moving insn.  */
      rtx pat = PATTERN (insn);
      if (src_note)
	remove_note (overlap, src_note);
      if ((HAVE_POST_INCREMENT || HAVE_POST_DECREMENT)
	  && code == PLUS
	  && try_auto_increment (overlap, insn, 0, src, insn_const, 0))
	insn = overlap;
      else
	{
	  rtx notes = REG_NOTES (insn);

	  emit_insn_after_with_line_notes (pat, PREV_INSN (p), insn);
	  delete_insn (insn);
	  /* emit_insn_after_with_line_notes has no
	     return value, so search for the new insn.  */
	  insn = p;
	  while (! INSN_P (insn) || PATTERN (insn) != pat)
	    insn = PREV_INSN (insn);

	  REG_NOTES (insn) = notes;
	}
    }
  /* Sometimes we'd generate src = const; src += n;
     if so, replace the instruction that set src
     in the first place.  */

  if (! overlap && (code == PLUS || code == MINUS))
    {
      rtx note = find_reg_note (insn, REG_EQUAL, NULL_RTX);
      rtx q, set2 = NULL_RTX;
      int num_calls2 = 0, s_length2 = 0;

      if (note && CONSTANT_P (XEXP (note, 0)))
	{
	  for (q = PREV_INSN (insn); q; q = PREV_INSN (q))
	    {
	      /* ??? We can't scan past the end of a basic block without
		 updating the register lifetime info
		 (REG_DEAD/basic_block_live_at_start).  */
	      if (perhaps_ends_bb_p (q))
		{
		  q = 0;
		  break;
		}
	      else if (! INSN_P (q))
		continue;

	      s_length2++;
	      if (reg_set_p (src, q))
		{
		  set2 = single_set (q);
		  break;
		}
	      if (reg_overlap_mentioned_p (src, PATTERN (q)))
		{
		  q = 0;
		  break;
		}
	      if (CALL_P (p))
		num_calls2++;
	    }
	  if (q && set2 && SET_DEST (set2) == src && CONSTANT_P (SET_SRC (set2))
	      && validate_change (insn, &SET_SRC (set), XEXP (note, 0), 0))
	    {
	      delete_insn (q);
	      REG_N_SETS (REGNO (src))--;
	      REG_N_CALLS_CROSSED (REGNO (src)) -= num_calls2;
	      REG_LIVE_LENGTH (REGNO (src)) -= s_length2;
	      insn_const = 0;
	    }
	}
    }

  if ((HAVE_PRE_INCREMENT || HAVE_PRE_DECREMENT)
	   && (code == PLUS || code == MINUS) && insn_const
	   && try_auto_increment (p, insn, 0, src, insn_const, 1))
    insn = p;
  else if ((HAVE_POST_INCREMENT || HAVE_POST_DECREMENT)
	   && post_inc
	   && try_auto_increment (p, post_inc, post_inc_set, src, newconst, 0))
    post_inc = 0;
  /* If post_inc still prevails, try to find an
     insn where it can be used as a pre-in/decrement.
     If code is MINUS, this was already tried.  */
  if (post_inc && code == PLUS
  /* Check that newconst is likely to be usable
     in a pre-in/decrement before starting the search.  */
      && ((HAVE_PRE_INCREMENT && newconst > 0 && newconst <= MOVE_MAX)
	  || (HAVE_PRE_DECREMENT && newconst < 0 && newconst >= -MOVE_MAX))
      && exact_log2 (newconst))
    {
      rtx q, inc_dest;

      inc_dest = post_inc_set ? SET_DEST (post_inc_set) : src;
      for (q = post_inc; (q = NEXT_INSN (q)); )
	{
	  /* ??? We can't scan past the end of a basic block without updating
	     the register lifetime info
	     (REG_DEAD/basic_block_live_at_start).  */
	  if (perhaps_ends_bb_p (q))
	    break;
	  else if (! INSN_P (q))
	    continue;
	  else if (src != inc_dest
		   && (reg_overlap_mentioned_p (src, PATTERN (q))
		       || reg_set_p (src, q)))
	    break;
	  else if (reg_set_p (inc_dest, q))
	    break;
	  else if (reg_overlap_mentioned_p (inc_dest, PATTERN (q)))
	    {
	      try_auto_increment (q, post_inc,
				  post_inc_set, inc_dest, newconst, 1);
	      break;
	    }
	}
    }

  /* Move the death note for DST to INSN if it is used
     there.  */
  if (reg_overlap_mentioned_p (dst, PATTERN (insn)))
    {
      XEXP (dst_note, 1) = REG_NOTES (insn);
      REG_NOTES (insn) = dst_note;
    }

  if (src_note)
    {
      /* Move the death note for SRC from INSN to P.  */
      if (! overlap)
	remove_note (insn, src_note);
      XEXP (src_note, 1) = REG_NOTES (p);
      REG_NOTES (p) = src_note;

      REG_N_CALLS_CROSSED (REGNO (src)) += s_num_calls;
    }

  REG_N_SETS (REGNO (src))++;
  REG_N_SETS (REGNO (dst))--;

  REG_N_CALLS_CROSSED (REGNO (dst)) -= num_calls;

  REG_LIVE_LENGTH (REGNO (src)) += s_length;
  if (REG_LIVE_LENGTH (REGNO (dst)) >= 0)
    {
      REG_LIVE_LENGTH (REGNO (dst)) -= length;
      /* REG_LIVE_LENGTH is only an approximation after
	 combine if sched is not run, so make sure that we
	 still have a reasonable value.  */
      if (REG_LIVE_LENGTH (REGNO (dst)) < 2)
	REG_LIVE_LENGTH (REGNO (dst)) = 2;
    }
  if (dump_file)
    fprintf (dump_file,
	     "Fixed operand %d of insn %d matching operand %d.\n",
	     operand_number, INSN_UID (insn), match_number);
  return 1;
}


/* Return nonzero if X is stable and mentions no registers but for
   mentioning SRC or mentioning / changing DST .  If in doubt, presume
   it is unstable.
   The rationale is that we want to check if we can move an insn easily
   while just paying attention to SRC and DST.  */
static int
stable_and_no_regs_but_for_p (rtx x, rtx src, rtx dst)
{
  RTX_CODE code = GET_CODE (x);
  switch (GET_RTX_CLASS (code))
    {
    case RTX_UNARY:
    case RTX_BIN_ARITH:
    case RTX_COMM_ARITH:
    case RTX_COMPARE:
    case RTX_COMM_COMPARE:
    case RTX_TERNARY:
    case RTX_BITFIELD_OPS:
      {
	int i;
	const char *fmt = GET_RTX_FORMAT (code);
	for (i = GET_RTX_LENGTH (code) - 1; i >= 0; i--)
	  if (fmt[i] == 'e'
	      && ! stable_and_no_regs_but_for_p (XEXP (x, i), src, dst))
	      return 0;
	return 1;
      }
    case RTX_OBJ:
      if (code == REG)
	return x == src || x == dst;
      /* If this is a MEM, look inside - there might be a register hidden in
	 the address of an unchanging MEM.  */
      if (code == MEM
	  && ! stable_and_no_regs_but_for_p (XEXP (x, 0), src, dst))
	return 0;
      /* Fall through.  */
    default:
      return ! rtx_unstable_p (x);
    }
}

/* Track stack adjustments and stack memory references.  Attempt to
   reduce the number of stack adjustments by back-propagating across
   the memory references.

   This is intended primarily for use with targets that do not define
   ACCUMULATE_OUTGOING_ARGS.  It is of significantly more value to
   targets that define PREFERRED_STACK_BOUNDARY more aligned than
   STACK_BOUNDARY (e.g. x86), or if not all registers can be pushed
   (e.g. x86 fp regs) which would ordinarily have to be implemented
   as a sub/mov pair due to restrictions in calls.c.

   Propagation stops when any of the insns that need adjusting are
   (a) no longer valid because we've exceeded their range, (b) a
   non-trivial push instruction, or (c) a call instruction.

   Restriction B is based on the assumption that push instructions
   are smaller or faster.  If a port really wants to remove all
   pushes, it should have defined ACCUMULATE_OUTGOING_ARGS.  The
   one exception that is made is for an add immediately followed
   by a push.  */

/* This structure records stack memory references between stack adjusting
   instructions.  */

struct csa_memlist
{
  HOST_WIDE_INT sp_offset;
  rtx insn, *mem;
  struct csa_memlist *next;
};

static int stack_memref_p (rtx);
static rtx single_set_for_csa (rtx);
static void free_csa_memlist (struct csa_memlist *);
static struct csa_memlist *record_one_stack_memref (rtx, rtx *,
						    struct csa_memlist *);
static int try_apply_stack_adjustment (rtx, struct csa_memlist *,
				       HOST_WIDE_INT, HOST_WIDE_INT);
static void combine_stack_adjustments_for_block (basic_block);
static int record_stack_memrefs (rtx *, void *);


/* Main entry point for stack adjustment combination.  */

static void
combine_stack_adjustments (void)
{
  basic_block bb;

  FOR_EACH_BB (bb)
    combine_stack_adjustments_for_block (bb);
}

/* Recognize a MEM of the form (sp) or (plus sp const).  */

static int
stack_memref_p (rtx x)
{
  if (!MEM_P (x))
    return 0;
  x = XEXP (x, 0);

  if (x == stack_pointer_rtx)
    return 1;
  if (GET_CODE (x) == PLUS
      && XEXP (x, 0) == stack_pointer_rtx
      && GET_CODE (XEXP (x, 1)) == CONST_INT)
    return 1;

  return 0;
}

/* Recognize either normal single_set or the hack in i386.md for
   tying fp and sp adjustments.  */

static rtx
single_set_for_csa (rtx insn)
{
  int i;
  rtx tmp = single_set (insn);
  if (tmp)
    return tmp;

  if (!NONJUMP_INSN_P (insn)
      || GET_CODE (PATTERN (insn)) != PARALLEL)
    return NULL_RTX;

  tmp = PATTERN (insn);
  if (GET_CODE (XVECEXP (tmp, 0, 0)) != SET)
    return NULL_RTX;

  for (i = 1; i < XVECLEN (tmp, 0); ++i)
    {
      rtx this = XVECEXP (tmp, 0, i);

      /* The special case is allowing a no-op set.  */
      if (GET_CODE (this) == SET
	  && SET_SRC (this) == SET_DEST (this))
	;
      else if (GET_CODE (this) != CLOBBER
	       && GET_CODE (this) != USE)
	return NULL_RTX;
    }

  return XVECEXP (tmp, 0, 0);
}

/* Free the list of csa_memlist nodes.  */

static void
free_csa_memlist (struct csa_memlist *memlist)
{
  struct csa_memlist *next;
  for (; memlist ; memlist = next)
    {
      next = memlist->next;
      free (memlist);
    }
}

/* Create a new csa_memlist node from the given memory reference.
   It is already known that the memory is stack_memref_p.  */

static struct csa_memlist *
record_one_stack_memref (rtx insn, rtx *mem, struct csa_memlist *next_memlist)
{
  struct csa_memlist *ml;

  ml = XNEW (struct csa_memlist);

  if (XEXP (*mem, 0) == stack_pointer_rtx)
    ml->sp_offset = 0;
  else
    ml->sp_offset = INTVAL (XEXP (XEXP (*mem, 0), 1));

  ml->insn = insn;
  ml->mem = mem;
  ml->next = next_memlist;

  return ml;
}

/* Attempt to apply ADJUST to the stack adjusting insn INSN, as well
   as each of the memories in MEMLIST.  Return true on success.  */

static int
try_apply_stack_adjustment (rtx insn, struct csa_memlist *memlist, HOST_WIDE_INT new_adjust,
			    HOST_WIDE_INT delta)
{
  struct csa_memlist *ml;
  rtx set;

  set = single_set_for_csa (insn);
  validate_change (insn, &XEXP (SET_SRC (set), 1), GEN_INT (new_adjust), 1);

  for (ml = memlist; ml ; ml = ml->next)
    validate_change
      (ml->insn, ml->mem,
       replace_equiv_address_nv (*ml->mem,
				 plus_constant (stack_pointer_rtx,
						ml->sp_offset - delta)), 1);

  if (apply_change_group ())
    {
      /* Succeeded.  Update our knowledge of the memory references.  */
      for (ml = memlist; ml ; ml = ml->next)
	ml->sp_offset -= delta;

      return 1;
    }
  else
    return 0;
}

/* Called via for_each_rtx and used to record all stack memory references in
   the insn and discard all other stack pointer references.  */
struct record_stack_memrefs_data
{
  rtx insn;
  struct csa_memlist *memlist;
};

static int
record_stack_memrefs (rtx *xp, void *data)
{
  rtx x = *xp;
  struct record_stack_memrefs_data *d =
    (struct record_stack_memrefs_data *) data;
  if (!x)
    return 0;
  switch (GET_CODE (x))
    {
    case MEM:
      if (!reg_mentioned_p (stack_pointer_rtx, x))
	return -1;
      /* We are not able to handle correctly all possible memrefs containing
         stack pointer, so this check is necessary.  */
      if (stack_memref_p (x))
	{
	  d->memlist = record_one_stack_memref (d->insn, xp, d->memlist);
	  return -1;
	}
      return 1;
    case REG:
      /* ??? We want be able to handle non-memory stack pointer
	 references later.  For now just discard all insns referring to
	 stack pointer outside mem expressions.  We would probably
	 want to teach validate_replace to simplify expressions first.

	 We can't just compare with STACK_POINTER_RTX because the
	 reference to the stack pointer might be in some other mode.
	 In particular, an explicit clobber in an asm statement will
	 result in a QImode clobber.  */
      if (REGNO (x) == STACK_POINTER_REGNUM)
	return 1;
      break;
    default:
      break;
    }
  return 0;
}

/* Subroutine of combine_stack_adjustments, called for each basic block.  */

static void
combine_stack_adjustments_for_block (basic_block bb)
{
  HOST_WIDE_INT last_sp_adjust = 0;
  rtx last_sp_set = NULL_RTX;
  struct csa_memlist *memlist = NULL;
  rtx insn, next, set;
  struct record_stack_memrefs_data data;
  bool end_of_block = false;

  for (insn = BB_HEAD (bb); !end_of_block ; insn = next)
    {
      end_of_block = insn == BB_END (bb);
      next = NEXT_INSN (insn);

      if (! INSN_P (insn))
	continue;

      set = single_set_for_csa (insn);
      if (set)
	{
	  rtx dest = SET_DEST (set);
	  rtx src = SET_SRC (set);

	  /* Find constant additions to the stack pointer.  */
	  if (dest == stack_pointer_rtx
	      && GET_CODE (src) == PLUS
	      && XEXP (src, 0) == stack_pointer_rtx
	      && GET_CODE (XEXP (src, 1)) == CONST_INT)
	    {
	      HOST_WIDE_INT this_adjust = INTVAL (XEXP (src, 1));

	      /* If we've not seen an adjustment previously, record
		 it now and continue.  */
	      if (! last_sp_set)
		{
		  last_sp_set = insn;
		  last_sp_adjust = this_adjust;
		  continue;
		}

	      /* If not all recorded memrefs can be adjusted, or the
		 adjustment is now too large for a constant addition,
		 we cannot merge the two stack adjustments.

		 Also we need to be careful to not move stack pointer
		 such that we create stack accesses outside the allocated
		 area.  We can combine an allocation into the first insn,
		 or a deallocation into the second insn.  We can not
		 combine an allocation followed by a deallocation.

		 The only somewhat frequent occurrence of the later is when
		 a function allocates a stack frame but does not use it.
		 For this case, we would need to analyze rtl stream to be
		 sure that allocated area is really unused.  This means not
		 only checking the memory references, but also all registers
		 or global memory references possibly containing a stack
		 frame address.

		 Perhaps the best way to address this problem is to teach
		 gcc not to allocate stack for objects never used.  */

	      /* Combine an allocation into the first instruction.  */
	      if (STACK_GROWS_DOWNWARD ? this_adjust <= 0 : this_adjust >= 0)
		{
		  if (try_apply_stack_adjustment (last_sp_set, memlist,
						  last_sp_adjust + this_adjust,
						  this_adjust))
		    {
		      /* It worked!  */
		      delete_insn (insn);
		      last_sp_adjust += this_adjust;
		      continue;
		    }
		}

	      /* Otherwise we have a deallocation.  Do not combine with
		 a previous allocation.  Combine into the second insn.  */
	      else if (STACK_GROWS_DOWNWARD
		       ? last_sp_adjust >= 0 : last_sp_adjust <= 0)
		{
		  if (try_apply_stack_adjustment (insn, memlist,
						  last_sp_adjust + this_adjust,
						  -last_sp_adjust))
		    {
		      /* It worked!  */
		      delete_insn (last_sp_set);
		      last_sp_set = insn;
		      last_sp_adjust += this_adjust;
		      free_csa_memlist (memlist);
		      memlist = NULL;
		      continue;
		    }
		}

	      /* Combination failed.  Restart processing from here.  If
		 deallocation+allocation conspired to cancel, we can
		 delete the old deallocation insn.  */
	      if (last_sp_set && last_sp_adjust == 0)
		delete_insn (insn);
	      free_csa_memlist (memlist);
	      memlist = NULL;
	      last_sp_set = insn;
	      last_sp_adjust = this_adjust;
	      continue;
	    }

	  /* Find a predecrement of exactly the previous adjustment and
	     turn it into a direct store.  Obviously we can't do this if
	     there were any intervening uses of the stack pointer.  */
	  if (memlist == NULL
	      && MEM_P (dest)
	      && ((GET_CODE (XEXP (dest, 0)) == PRE_DEC
		   && (last_sp_adjust
		       == (HOST_WIDE_INT) GET_MODE_SIZE (GET_MODE (dest))))
		  || (GET_CODE (XEXP (dest, 0)) == PRE_MODIFY
		      && GET_CODE (XEXP (XEXP (dest, 0), 1)) == PLUS
		      && XEXP (XEXP (XEXP (dest, 0), 1), 0) == stack_pointer_rtx
		      && (GET_CODE (XEXP (XEXP (XEXP (dest, 0), 1), 1))
		          == CONST_INT)
		      && (INTVAL (XEXP (XEXP (XEXP (dest, 0), 1), 1))
		          == -last_sp_adjust)))
	      && XEXP (XEXP (dest, 0), 0) == stack_pointer_rtx
	      && ! reg_mentioned_p (stack_pointer_rtx, src)
	      && memory_address_p (GET_MODE (dest), stack_pointer_rtx)
	      && validate_change (insn, &SET_DEST (set),
				  replace_equiv_address (dest,
							 stack_pointer_rtx),
				  0))
	    {
	      delete_insn (last_sp_set);
	      free_csa_memlist (memlist);
	      memlist = NULL;
	      last_sp_set = NULL_RTX;
	      last_sp_adjust = 0;
	      continue;
	    }
	}

      data.insn = insn;
      data.memlist = memlist;
      if (!CALL_P (insn) && last_sp_set
	  && !for_each_rtx (&PATTERN (insn), record_stack_memrefs, &data))
	{
	   memlist = data.memlist;
	   continue;
	}
      memlist = data.memlist;

      /* Otherwise, we were not able to process the instruction.
	 Do not continue collecting data across such a one.  */
      if (last_sp_set
	  && (CALL_P (insn)
	      || reg_mentioned_p (stack_pointer_rtx, PATTERN (insn))))
	{
	  if (last_sp_set && last_sp_adjust == 0)
	    delete_insn (last_sp_set);
	  free_csa_memlist (memlist);
	  memlist = NULL;
	  last_sp_set = NULL_RTX;
	  last_sp_adjust = 0;
	}
    }

  if (last_sp_set && last_sp_adjust == 0)
    delete_insn (last_sp_set);

  if (memlist)
    free_csa_memlist (memlist);
}

static bool
gate_handle_regmove (void)
{
  return (optimize > 0 && flag_regmove);
}


/* Register allocation pre-pass, to reduce number of moves necessary
   for two-address machines.  */
static unsigned int
rest_of_handle_regmove (void)
{
  regmove_optimize (get_insns (), max_reg_num ());
  cleanup_cfg (CLEANUP_EXPENSIVE | CLEANUP_UPDATE_LIFE);
  return 0;
}

struct tree_opt_pass pass_regmove =
{
  "regmove",                            /* name */
  gate_handle_regmove,                  /* gate */
  rest_of_handle_regmove,               /* execute */
  NULL,                                 /* sub */
  NULL,                                 /* next */
  0,                                    /* static_pass_number */
  TV_REGMOVE,                           /* tv_id */
  0,                                    /* properties_required */
  0,                                    /* properties_provided */
  0,                                    /* properties_destroyed */
  0,                                    /* todo_flags_start */
  TODO_dump_func |
  TODO_ggc_collect,                     /* todo_flags_finish */
  'N'                                   /* letter */
};


static bool
gate_handle_stack_adjustments (void)
{
  return (optimize > 0);
}

static unsigned int
rest_of_handle_stack_adjustments (void)
{
  life_analysis (PROP_POSTRELOAD);
  cleanup_cfg (CLEANUP_EXPENSIVE | CLEANUP_UPDATE_LIFE
               | (flag_crossjumping ? CLEANUP_CROSSJUMP : 0));

  /* This is kind of a heuristic.  We need to run combine_stack_adjustments
     even for machines with possibly nonzero RETURN_POPS_ARGS
     and ACCUMULATE_OUTGOING_ARGS.  We expect that only ports having
     push instructions will have popping returns.  */
#ifndef PUSH_ROUNDING
  if (!ACCUMULATE_OUTGOING_ARGS)
#endif
    combine_stack_adjustments ();
  return 0;
}

struct tree_opt_pass pass_stack_adjustments =
{
  "csa",                                /* name */
  gate_handle_stack_adjustments,        /* gate */
  rest_of_handle_stack_adjustments,     /* execute */
  NULL,                                 /* sub */
  NULL,                                 /* next */
  0,                                    /* static_pass_number */
  0,                                    /* tv_id */
  0,                                    /* properties_required */
  0,                                    /* properties_provided */
  0,                                    /* properties_destroyed */
  0,                                    /* todo_flags_start */
  TODO_dump_func |
  TODO_ggc_collect,                     /* todo_flags_finish */
  0                                     /* letter */
};

