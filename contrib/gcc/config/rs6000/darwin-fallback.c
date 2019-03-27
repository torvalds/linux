/* Fallback frame-state unwinder for Darwin.
   Copyright (C) 2004, 2005 Free Software Foundation, Inc.

   This file is part of GCC.

   GCC is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   In addition to the permissions in the GNU General Public License, the
   Free Software Foundation gives you unlimited permission to link the
   compiled version of this file into combinations with other programs,
   and to distribute those combinations without any restriction coming
   from the use of this file.  (The General Public License restrictions
   do apply in other respects; for example, they cover modification of
   the file, and distribution when not linked into a combined
   executable.)

   GCC is distributed in the hope that it will be useful, but WITHOUT
   ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
   or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
   License for more details.

   You should have received a copy of the GNU General Public License
   along with GCC; see the file COPYING.  If not, write to the Free
   Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
   02110-1301, USA.  */

#include "tconfig.h"
#include "tsystem.h"
#include "coretypes.h"
#include "tm.h"
#include "dwarf2.h"
#include "unwind.h"
#include "unwind-dw2.h"
#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>
#include <signal.h>

typedef unsigned long reg_unit;

/* Place in GPRS the parameters to the first 'sc' instruction that would
   have been executed if we were returning from this CONTEXT, or
   return false if an unexpected instruction is encountered.  */

static bool
interpret_libc (reg_unit gprs[32], struct _Unwind_Context *context)
{
  uint32_t *pc = (uint32_t *)_Unwind_GetIP (context);
  uint32_t cr;
  reg_unit lr = (reg_unit) pc;
  reg_unit ctr = 0;
  uint32_t *invalid_address = NULL;

  int i;

  for (i = 0; i < 13; i++)
    gprs[i] = 1;
  gprs[1] = _Unwind_GetCFA (context);
  for (; i < 32; i++)
    gprs[i] = _Unwind_GetGR (context, i);
  cr = _Unwind_GetGR (context, CR2_REGNO);

  /* For each supported Libc, we have to track the code flow
     all the way back into the kernel.
  
     This code is believed to support all released Libc/Libsystem builds since
     Jaguar 6C115, including all the security updates.  To be precise,

     Libc	Libsystem	Build(s)
     262~1	60~37		6C115
     262~1	60.2~4		6D52
     262~1	61~3		6F21-6F22
     262~1	63~24		6G30-6G37
     262~1	63~32		6I34-6I35
     262~1	63~64		6L29-6L60
     262.4.1~1	63~84		6L123-6R172
     
     320~1	71~101		7B85-7D28
     320~1	71~266		7F54-7F56
     320~1	71~288		7F112
     320~1	71~289		7F113
     320.1.3~1	71.1.1~29	7H60-7H105
     320.1.3~1	71.1.1~30	7H110-7H113
     320.1.3~1	71.1.1~31	7H114
     
     That's a big table!  It would be insane to try to keep track of
     every little detail, so we just read the code itself and do what
     it would do.
  */

  for (;;)
    {
      uint32_t ins = *pc++;
      
      if ((ins & 0xFC000003) == 0x48000000)  /* b instruction */
	{
	  pc += ((((int32_t) ins & 0x3FFFFFC) ^ 0x2000000) - 0x2000004) / 4;
	  continue;
	}
      if ((ins & 0xFC600000) == 0x2C000000)  /* cmpwi */
	{
	  int32_t val1 = (int16_t) ins;
	  int32_t val2 = gprs[ins >> 16 & 0x1F];
	  /* Only beq and bne instructions are supported, so we only
	     need to set the EQ bit.  */
	  uint32_t mask = 0xF << ((ins >> 21 & 0x1C) ^ 0x1C);
	  if (val1 == val2)
	    cr |= mask;
	  else
	    cr &= ~mask;
	  continue;
	}
      if ((ins & 0xFEC38003) == 0x40820000)  /* forwards beq/bne */
	{
	  if ((cr >> ((ins >> 16 & 0x1F) ^ 0x1F) & 1) == (ins >> 24 & 1))
	    pc += (ins & 0x7FFC) / 4 - 1;
	  continue;
	}
      if ((ins & 0xFC0007FF) == 0x7C000378) /* or, including mr */
	{
	  gprs [ins >> 16 & 0x1F] = (gprs [ins >> 11 & 0x1F] 
				     | gprs [ins >> 21 & 0x1F]);
	  continue;
	}
      if (ins >> 26 == 0x0E)  /* addi, including li */
	{
	  reg_unit src = (ins >> 16 & 0x1F) == 0 ? 0 : gprs [ins >> 16 & 0x1F];
	  gprs [ins >> 21 & 0x1F] = src + (int16_t) ins;
	  continue;
	}
      if (ins >> 26 == 0x0F)  /* addis, including lis */
	{
	  reg_unit src = (ins >> 16 & 0x1F) == 0 ? 0 : gprs [ins >> 16 & 0x1F];
	  gprs [ins >> 21 & 0x1F] = src + ((int16_t) ins << 16);
	  continue;
	}
      if (ins >> 26 == 0x20)  /* lwz */
	{
	  reg_unit src = (ins >> 16 & 0x1F) == 0 ? 0 : gprs [ins >> 16 & 0x1F];
	  uint32_t *p = (uint32_t *)(src + (int16_t) ins);
	  if (p == invalid_address)
	    return false;
	  gprs [ins >> 21 & 0x1F] = *p;
	  continue;
	}
      if (ins >> 26 == 0x21)  /* lwzu */
	{
	  uint32_t *p = (uint32_t *)(gprs [ins >> 16 & 0x1F] += (int16_t) ins);
	  if (p == invalid_address)
	    return false;
	  gprs [ins >> 21 & 0x1F] = *p;
	  continue;
	}
      if (ins >> 26 == 0x24)  /* stw */
	/* What we hope this is doing is '--in_sigtramp'.  We don't want
	   to actually store to memory, so just make a note of the
	   address and refuse to load from it.  */
	{
	  reg_unit src = (ins >> 16 & 0x1F) == 0 ? 0 : gprs [ins >> 16 & 0x1F];
	  uint32_t *p = (uint32_t *)(src + (int16_t) ins);
	  if (p == NULL || invalid_address != NULL)
	    return false;
	  invalid_address = p;
	  continue;
	}
      if (ins >> 26 == 0x2E) /* lmw */
	{
	  reg_unit src = (ins >> 16 & 0x1F) == 0 ? 0 : gprs [ins >> 16 & 0x1F];
	  uint32_t *p = (uint32_t *)(src + (int16_t) ins);
	  int i;

	  for (i = (ins >> 21 & 0x1F); i < 32; i++)
	    {
	      if (p == invalid_address)
		return false;
	      gprs[i] = *p++;
	    }
	  continue;
	}
      if ((ins & 0xFC1FFFFF) == 0x7c0803a6)  /* mtlr */
	{
	  lr = gprs [ins >> 21 & 0x1F];
	  continue;
	}
      if ((ins & 0xFC1FFFFF) == 0x7c0802a6)  /* mflr */
	{
	  gprs [ins >> 21 & 0x1F] = lr;
	  continue;
	}
      if ((ins & 0xFC1FFFFF) == 0x7c0903a6)  /* mtctr */
	{
	  ctr = gprs [ins >> 21 & 0x1F];
	  continue;
	}
      /* The PowerPC User's Manual says that bit 11 of the mtcrf
	 instruction is reserved and should be set to zero, but it
	 looks like the Darwin assembler doesn't do that... */
      if ((ins & 0xFC000FFF) == 0x7c000120) /* mtcrf */
	{
	  int i;
	  uint32_t mask = 0;
	  for (i = 0; i < 8; i++)
	    mask |= ((-(ins >> (12 + i) & 1)) & 0xF) << 4 * i;
	  cr = (cr & ~mask) | (gprs [ins >> 21 & 0x1F] & mask);
	  continue;
	}
      if (ins == 0x429f0005)  /* bcl- 20,4*cr7+so,.+4, loads pc into LR */
	{
	  lr = (reg_unit) pc;
	  continue;
	}
      if (ins == 0x4e800420) /* bctr */
	{
	  pc = (uint32_t *) ctr;
	  continue;
	}
      if (ins == 0x44000002) /* sc */
	return true;

      return false;
    }
}

/* We used to include <ucontext.h> and <mach/thread_status.h>,
   but they change so much between different Darwin system versions
   that it's much easier to just write the structures involved here
   directly.  */

/* These defines are from the kernel's bsd/dev/ppc/unix_signal.c.  */
#define UC_TRAD                 1
#define UC_TRAD_VEC             6
#define UC_TRAD64               20
#define UC_TRAD64_VEC           25
#define UC_FLAVOR               30
#define UC_FLAVOR_VEC           35
#define UC_FLAVOR64             40
#define UC_FLAVOR64_VEC         45
#define UC_DUAL                 50
#define UC_DUAL_VEC             55

struct gcc_ucontext 
{
  int onstack;
  sigset_t sigmask;
  void * stack_sp;
  size_t stack_sz;
  int stack_flags;
  struct gcc_ucontext *link;
  size_t mcsize;
  struct gcc_mcontext32 *mcontext;
};

struct gcc_float_vector_state 
{
  double fpregs[32];
  uint32_t fpscr_pad;
  uint32_t fpscr;
  uint32_t save_vr[32][4];
  uint32_t save_vscr[4];
};

struct gcc_mcontext32 {
  uint32_t dar;
  uint32_t dsisr;
  uint32_t exception;
  uint32_t padding1[5];
  uint32_t srr0;
  uint32_t srr1;
  uint32_t gpr[32];
  uint32_t cr;
  uint32_t xer;
  uint32_t lr;
  uint32_t ctr;
  uint32_t mq;
  uint32_t vrsave;
  struct gcc_float_vector_state fvs;
};

/* These are based on /usr/include/ppc/ucontext.h and
   /usr/include/mach/ppc/thread_status.h, but rewritten to be more
   convenient, to compile on Jaguar, and to work around Radar 3712064
   on Panther, which is that the 'es' field of 'struct mcontext64' has
   the wrong type (doh!).  */

struct gcc_mcontext64 {
  uint64_t dar;
  uint32_t dsisr;
  uint32_t exception;
  uint32_t padding1[4];
  uint64_t srr0;
  uint64_t srr1;
  uint32_t gpr[32][2];
  uint32_t cr;
  uint32_t xer[2];  /* These are arrays because the original structure has them misaligned.  */
  uint32_t lr[2];
  uint32_t ctr[2];
  uint32_t vrsave;
  struct gcc_float_vector_state fvs;
};

#define UC_FLAVOR_SIZE \
  (sizeof (struct gcc_mcontext32) - 33*16)

#define UC_FLAVOR_VEC_SIZE (sizeof (struct gcc_mcontext32))

#define UC_FLAVOR64_SIZE \
  (sizeof (struct gcc_mcontext64) - 33*16)

#define UC_FLAVOR64_VEC_SIZE (sizeof (struct gcc_mcontext64))

/* Given GPRS as input to a 'sc' instruction, and OLD_CFA, update FS
   to represent the execution of a signal return; or, if not a signal
   return, return false.  */

static bool
handle_syscall (_Unwind_FrameState *fs, const reg_unit gprs[32],
		_Unwind_Ptr old_cfa)
{
  struct gcc_ucontext *uctx;
  bool is_64, is_vector;
  struct gcc_float_vector_state * float_vector_state;
  _Unwind_Ptr new_cfa;
  int i;
  static _Unwind_Ptr return_addr;
  
  /* Yay!  We're in a Libc that we understand, and it's made a
     system call.  It'll be one of two kinds: either a Jaguar-style
     SYS_sigreturn, or a Panther-style 'syscall' call with 184, which 
     is also SYS_sigreturn.  */
  
  if (gprs[0] == 0x67 /* SYS_SIGRETURN */)
    {
      uctx = (struct gcc_ucontext *) gprs[3];
      is_vector = (uctx->mcsize == UC_FLAVOR64_VEC_SIZE
		   || uctx->mcsize == UC_FLAVOR_VEC_SIZE);
      is_64 = (uctx->mcsize == UC_FLAVOR64_VEC_SIZE
	       || uctx->mcsize == UC_FLAVOR64_SIZE);
    }
  else if (gprs[0] == 0 && gprs[3] == 184)
    {
      int ctxstyle = gprs[5];
      uctx = (struct gcc_ucontext *) gprs[4];
      is_vector = (ctxstyle == UC_FLAVOR_VEC || ctxstyle == UC_FLAVOR64_VEC
		   || ctxstyle == UC_TRAD_VEC || ctxstyle == UC_TRAD64_VEC);
      is_64 = (ctxstyle == UC_FLAVOR64_VEC || ctxstyle == UC_TRAD64_VEC
	       || ctxstyle == UC_FLAVOR64 || ctxstyle == UC_TRAD64);
    }
  else
    return false;

#define set_offset(r, addr)					\
  (fs->regs.reg[r].how = REG_SAVED_OFFSET,			\
   fs->regs.reg[r].loc.offset = (_Unwind_Ptr)(addr) - new_cfa)

  /* Restore even the registers that are not call-saved, since they
     might be being used in the prologue to save other registers,
     for instance GPR0 is sometimes used to save LR.  */

  /* Handle the GPRs, and produce the information needed to do the rest.  */
  if (is_64)
    {
      /* The context is 64-bit, but it doesn't carry any extra information
	 for us because only the low 32 bits of the registers are
	 call-saved.  */
      struct gcc_mcontext64 *m64 = (struct gcc_mcontext64 *)uctx->mcontext;
      int i;

      float_vector_state = &m64->fvs;

      new_cfa = m64->gpr[1][1];
      
      set_offset (CR2_REGNO, &m64->cr);
      for (i = 0; i < 32; i++)
	set_offset (i, m64->gpr[i] + 1);
      set_offset (XER_REGNO, m64->xer + 1);
      set_offset (LINK_REGISTER_REGNUM, m64->lr + 1);
      set_offset (COUNT_REGISTER_REGNUM, m64->ctr + 1);
      if (is_vector)
	set_offset (VRSAVE_REGNO, &m64->vrsave);
      
      /* Sometimes, srr0 points to the instruction that caused the exception,
	 and sometimes to the next instruction to be executed; we want
	 the latter.  */
      if (m64->exception == 3 || m64->exception == 4
	  || m64->exception == 6
	  || (m64->exception == 7 && !(m64->srr1 & 0x10000)))
	return_addr = m64->srr0 + 4;
      else
	return_addr = m64->srr0;
    }
  else
    {
      struct gcc_mcontext32 *m = uctx->mcontext;
      int i;

      float_vector_state = &m->fvs;
      
      new_cfa = m->gpr[1];

      set_offset (CR2_REGNO, &m->cr);
      for (i = 0; i < 32; i++)
	set_offset (i, m->gpr + i);
      set_offset (XER_REGNO, &m->xer);
      set_offset (LINK_REGISTER_REGNUM, &m->lr);
      set_offset (COUNT_REGISTER_REGNUM, &m->ctr);

      if (is_vector)
	set_offset (VRSAVE_REGNO, &m->vrsave);

      /* Sometimes, srr0 points to the instruction that caused the exception,
	 and sometimes to the next instruction to be executed; we want
	 the latter.  */
      if (m->exception == 3 || m->exception == 4
	  || m->exception == 6
	  || (m->exception == 7 && !(m->srr1 & 0x10000)))
	return_addr = m->srr0 + 4;
      else
	return_addr = m->srr0;
    }

  fs->cfa_how = CFA_REG_OFFSET;
  fs->cfa_reg = STACK_POINTER_REGNUM;
  fs->cfa_offset = new_cfa - old_cfa;;
  
  /* The choice of column for the return address is somewhat tricky.
     Fortunately, the actual choice is private to this file, and
     the space it's reserved from is the GCC register space, not the
     DWARF2 numbering.  So any free element of the right size is an OK
     choice.  Thus: */
  fs->retaddr_column = ARG_POINTER_REGNUM;
  /* FIXME: this should really be done using a DWARF2 location expression,
     not using a static variable.  In fact, this entire file should
     be implemented in DWARF2 expressions.  */
  set_offset (ARG_POINTER_REGNUM, &return_addr);

  for (i = 0; i < 32; i++)
    set_offset (32 + i, float_vector_state->fpregs + i);
  set_offset (SPEFSCR_REGNO, &float_vector_state->fpscr);
  
  if (is_vector)
    {
      for (i = 0; i < 32; i++)
	set_offset (FIRST_ALTIVEC_REGNO + i, float_vector_state->save_vr + i);
      set_offset (VSCR_REGNO, float_vector_state->save_vscr);
    }

  return true;
}

/* This is also prototyped in rs6000/darwin.h, inside the
   MD_FALLBACK_FRAME_STATE_FOR macro.  */
extern bool _Unwind_fallback_frame_state_for (struct _Unwind_Context *context,
					      _Unwind_FrameState *fs);

/* Implement the MD_FALLBACK_FRAME_STATE_FOR macro,
   returning true iff the frame was a sigreturn() frame that we
   can understand.  */

bool
_Unwind_fallback_frame_state_for (struct _Unwind_Context *context,
				  _Unwind_FrameState *fs)
{
  reg_unit gprs[32];

  if (!interpret_libc (gprs, context))
    return false;
  return handle_syscall (fs, gprs, _Unwind_GetCFA (context));
}
