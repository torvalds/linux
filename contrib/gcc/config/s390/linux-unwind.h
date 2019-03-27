/* DWARF2 EH unwinding support for S/390 Linux.
   Copyright (C) 2004, 2005, 2006 Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 2, or (at your option) any later
version.

In addition to the permissions in the GNU General Public License, the
Free Software Foundation gives you unlimited permission to link the
compiled version of this file with other programs, and to distribute
those programs without any restriction coming from the use of this
file.  (The General Public License restrictions do apply in other
respects; for example, they cover modification of the file, and
distribution when not linked into another program.)

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to the Free
Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
02110-1301, USA.  */

/* Do code reading to identify a signal frame, and set the frame
   state data appropriately.  See unwind-dw2.c for the structs.  */

#define MD_FALLBACK_FRAME_STATE_FOR s390_fallback_frame_state

static _Unwind_Reason_Code
s390_fallback_frame_state (struct _Unwind_Context *context,
			   _Unwind_FrameState *fs)
{
  unsigned char *pc = context->ra;
  long new_cfa;
  int i;

  typedef struct
  {
    unsigned long psw_mask;
    unsigned long psw_addr;
    unsigned long gprs[16];
    unsigned int  acrs[16];
    unsigned int  fpc;
    unsigned int  __pad;
    double        fprs[16];
  } __attribute__ ((__aligned__ (8))) sigregs_;

  sigregs_ *regs;
  int *signo;

  /* svc $__NR_sigreturn or svc $__NR_rt_sigreturn  */
  if (pc[0] != 0x0a || (pc[1] != 119 && pc[1] != 173))
    return _URC_END_OF_STACK;

  /* Legacy frames:
       old signal mask (8 bytes)
       pointer to sigregs (8 bytes) - points always to next location
       sigregs
       retcode
     This frame layout was used on kernels < 2.6.9 for non-RT frames,
     and on kernels < 2.4.13 for RT frames as well.  Note that we need
     to look at RA to detect this layout -- this means that if you use
     sa_restorer to install a different signal restorer on a legacy
     kernel, unwinding from signal frames will not work.  */
  if (context->ra == context->cfa + 16 + sizeof (sigregs_))
    {
      regs = (sigregs_ *)(context->cfa + 16);
      signo = NULL;
    }

  /* New-style RT frame:
     retcode + alignment (8 bytes)
     siginfo (128 bytes)
     ucontext (contains sigregs)  */
  else if (pc[1] == 173 /* __NR_rt_sigreturn */)
    {
      struct ucontext_
      {
	unsigned long     uc_flags;
	struct ucontext_ *uc_link;
	unsigned long     uc_stack[3];
	sigregs_          uc_mcontext;
      } *uc = context->cfa + 8 + 128;

      regs = &uc->uc_mcontext;
      signo = context->cfa + sizeof(long);
    }

  /* New-style non-RT frame:
     old signal mask (8 bytes)
     pointer to sigregs (followed by signal number)  */
  else
    {
      regs = *(sigregs_ **)(context->cfa + 8);
      signo = (int *)(regs + 1);
    }

  new_cfa = regs->gprs[15] + 16*sizeof(long) + 32;
  fs->cfa_how = CFA_REG_OFFSET;
  fs->cfa_reg = 15;
  fs->cfa_offset =
    new_cfa - (long) context->cfa + 16*sizeof(long) + 32;

  for (i = 0; i < 16; i++)
    {
      fs->regs.reg[i].how = REG_SAVED_OFFSET;
      fs->regs.reg[i].loc.offset =
	(long)&regs->gprs[i] - new_cfa;
    }
  for (i = 0; i < 16; i++)
    {
      fs->regs.reg[16+i].how = REG_SAVED_OFFSET;
      fs->regs.reg[16+i].loc.offset =
	(long)&regs->fprs[i] - new_cfa;
    }

  /* Load return addr from PSW into dummy register 32.  */

  fs->regs.reg[32].how = REG_SAVED_OFFSET;
  fs->regs.reg[32].loc.offset = (long)&regs->psw_addr - new_cfa;
  fs->retaddr_column = 32;
  /* SIGILL, SIGFPE and SIGTRAP are delivered with psw_addr
     after the faulting instruction rather than before it.
     Don't set FS->signal_frame in that case.  */
  if (!signo || (*signo != 4 && *signo != 5 && *signo != 8))
    fs->signal_frame = 1;

  return _URC_NO_REASON;
}
