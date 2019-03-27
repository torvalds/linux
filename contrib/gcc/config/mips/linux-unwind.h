/* DWARF2 EH unwinding support for MIPS Linux.
   Copyright (C) 2004, 2005 Free Software Foundation, Inc.

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

#ifndef inhibit_libc
/* Do code reading to identify a signal frame, and set the frame
   state data appropriately.  See unwind-dw2.c for the structs.  */

#include <signal.h>
#include <asm/unistd.h>

/* The third parameter to the signal handler points to something with
 * this structure defined in asm/ucontext.h, but the name clashes with
 * struct ucontext from sys/ucontext.h so this private copy is used.  */
typedef struct _sig_ucontext {
    unsigned long         uc_flags;
    struct _sig_ucontext  *uc_link;
    stack_t               uc_stack;
    struct sigcontext uc_mcontext;
    sigset_t      uc_sigmask;
} _sig_ucontext_t;

#define MD_FALLBACK_FRAME_STATE_FOR mips_fallback_frame_state

static _Unwind_Reason_Code
mips_fallback_frame_state (struct _Unwind_Context *context,
			   _Unwind_FrameState *fs)
{
  u_int32_t *pc = (u_int32_t *) context->ra;
  struct sigcontext *sc;
  _Unwind_Ptr new_cfa;
  int i;

  /* 24021061 li v0, 0x1061 (rt_sigreturn)*/
  /* 0000000c syscall    */
  /*    or */
  /* 24021017 li v0, 0x1017 (sigreturn) */
  /* 0000000c syscall  */
  if (pc[1] != 0x0000000c)
    return _URC_END_OF_STACK;
#if _MIPS_SIM == _ABIO32
  if (pc[0] == (0x24020000 | __NR_sigreturn))
    {
      struct sigframe {
	u_int32_t trampoline[2];
	struct sigcontext sigctx;
      } *rt_ = context->ra;
      sc = &rt_->sigctx;
    }
  else
#endif
  if (pc[0] == (0x24020000 | __NR_rt_sigreturn))
    {
      struct rt_sigframe {
	u_int32_t trampoline[2];
	struct siginfo info;
	_sig_ucontext_t uc;
      } *rt_ = context->ra;
      sc = &rt_->uc.uc_mcontext;
    }
  else
    return _URC_END_OF_STACK;

  new_cfa = (_Unwind_Ptr)sc;
  fs->cfa_how = CFA_REG_OFFSET;
  fs->cfa_reg = STACK_POINTER_REGNUM;
  fs->cfa_offset = new_cfa - (_Unwind_Ptr) context->cfa;

#if _MIPS_SIM == _ABIO32 && defined __MIPSEB__
  /* On o32 Linux, the register save slots in the sigcontext are
     eight bytes.  We need the lower half of each register slot,
     so slide our view of the structure back four bytes.  */
  new_cfa -= 4;
#endif

  for (i = 0; i < 32; i++) {
    fs->regs.reg[i].how = REG_SAVED_OFFSET;
    fs->regs.reg[i].loc.offset
      = (_Unwind_Ptr)&(sc->sc_regs[i]) - new_cfa;
  }
  fs->regs.reg[SIGNAL_UNWIND_RETURN_COLUMN].how = REG_SAVED_OFFSET;
  fs->regs.reg[SIGNAL_UNWIND_RETURN_COLUMN].loc.offset
    = (_Unwind_Ptr)&(sc->sc_pc) - new_cfa;
  fs->retaddr_column = SIGNAL_UNWIND_RETURN_COLUMN;

  return _URC_NO_REASON;
}
#endif
