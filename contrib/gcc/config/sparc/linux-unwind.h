/* DWARF2 EH unwinding support for SPARC Linux.
   Copyright 2004, 2005 Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

In addition to the permissions in the GNU General Public License, the
Free Software Foundation gives you unlimited permission to link the
compiled version of this file with other programs, and to distribute
those programs without any restriction coming from the use of this
file.  (The General Public License restrictions do apply in other
respects; for example, they cover modification of the file, and
distribution when not linked into another program.)

GCC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to
the Free Software Foundation, 51 Franklin Street, Fifth Floor,
Boston, MA 02110-1301, USA.  */

/* Do code reading to identify a signal frame, and set the frame
   state data appropriately.  See unwind-dw2.c for the structs.  */

/* Handle multilib correctly.  */
#if defined(__arch64__)

/* 64-bit SPARC version */
#define MD_FALLBACK_FRAME_STATE_FOR sparc64_fallback_frame_state

static _Unwind_Reason_Code
sparc64_fallback_frame_state (struct _Unwind_Context *context,
			      _Unwind_FrameState *fs)
{
  unsigned int *pc = context->ra;
  long new_cfa, i;
  long regs_off, fpu_save_off;
  long this_cfa, fpu_save;

  if (pc[0] != 0x82102065		/* mov NR_rt_sigreturn, %g1 */
      || pc[1] != 0x91d0206d)		/* ta 0x6d */
    return _URC_END_OF_STACK;
  regs_off = 192 + 128;
  fpu_save_off = regs_off + (16 * 8) + (3 * 8) + (2 * 4);
  this_cfa = (long) context->cfa;
  new_cfa = *(long *)((context->cfa) + (regs_off + (14 * 8)));
  new_cfa += 2047; /* Stack bias */
  fpu_save = *(long *)((this_cfa) + (fpu_save_off));
  fs->cfa_how = CFA_REG_OFFSET;
  fs->cfa_reg = 14;
  fs->cfa_offset = new_cfa - (long) context->cfa;
  for (i = 1; i < 16; ++i)
    {
      fs->regs.reg[i].how = REG_SAVED_OFFSET;
      fs->regs.reg[i].loc.offset =
	this_cfa + (regs_off + (i * 8)) - new_cfa;
    }
  for (i = 0; i < 16; ++i)
    {
      fs->regs.reg[i + 16].how = REG_SAVED_OFFSET;
      fs->regs.reg[i + 16].loc.offset =
	this_cfa + (i * 8) - new_cfa;
    }
  if (fpu_save)
    {
      for (i = 0; i < 64; ++i)
	{
	  if (i > 32 && (i & 0x1))
	    continue;
	  fs->regs.reg[i + 32].how = REG_SAVED_OFFSET;
	  fs->regs.reg[i + 32].loc.offset =
	    (fpu_save + (i * 4)) - new_cfa;
	}
    }
  /* Stick return address into %g0, same trick Alpha uses.  */
  fs->regs.reg[0].how = REG_SAVED_OFFSET;
  fs->regs.reg[0].loc.offset =
    this_cfa + (regs_off + (16 * 8) + 8) - new_cfa;
  fs->retaddr_column = 0;
  return _URC_NO_REASON;
}

#else

/* 32-bit SPARC version */
#define MD_FALLBACK_FRAME_STATE_FOR sparc_fallback_frame_state

static _Unwind_Reason_Code
sparc_fallback_frame_state (struct _Unwind_Context *context,
			    _Unwind_FrameState *fs)
{
  unsigned int *pc = context->ra;
  int new_cfa, i, oldstyle;
  int regs_off, fpu_save_off;
  int fpu_save, this_cfa;

  if (pc[1] != 0x91d02010)		/* ta 0x10 */
    return _URC_END_OF_STACK;
  if (pc[0] == 0x821020d8)		/* mov NR_sigreturn, %g1 */
    oldstyle = 1;
  else if (pc[0] == 0x82102065)	/* mov NR_rt_sigreturn, %g1 */
    oldstyle = 0;
  else
    return _URC_END_OF_STACK;
  if (oldstyle)
    {
      regs_off = 96;
      fpu_save_off = regs_off + (4 * 4) + (16 * 4);
    }
  else
    {
      regs_off = 96 + 128;
      fpu_save_off = regs_off + (4 * 4) + (16 * 4) + (2 * 4);
    }
  this_cfa = (int) context->cfa;
  new_cfa = *(int *)((context->cfa) + (regs_off+(4*4)+(14 * 4)));
  fpu_save = *(int *)((this_cfa) + (fpu_save_off));
  fs->cfa_how = CFA_REG_OFFSET;
  fs->cfa_reg = 14;
  fs->cfa_offset = new_cfa - (int) context->cfa;
  for (i = 1; i < 16; ++i)
    {
      if (i == 14)
	continue;
      fs->regs.reg[i].how = REG_SAVED_OFFSET;
      fs->regs.reg[i].loc.offset =
	this_cfa + (regs_off+(4 * 4)+(i * 4)) - new_cfa;
    }
  for (i = 0; i < 16; ++i)
    {
      fs->regs.reg[i + 16].how = REG_SAVED_OFFSET;
      fs->regs.reg[i + 16].loc.offset =
	this_cfa + (i * 4) - new_cfa;
    }
  if (fpu_save)
    {
      for (i = 0; i < 32; ++i)
	{
	  fs->regs.reg[i + 32].how = REG_SAVED_OFFSET;
	  fs->regs.reg[i + 32].loc.offset =
	    (fpu_save + (i * 4)) - new_cfa;
	}
    }
  /* Stick return address into %g0, same trick Alpha uses.  */
  fs->regs.reg[0].how = REG_SAVED_OFFSET;
  fs->regs.reg[0].loc.offset = this_cfa+(regs_off+4)-new_cfa;
  fs->retaddr_column = 0;
  return _URC_NO_REASON;
}

#endif
