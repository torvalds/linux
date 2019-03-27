/* DWARF2 EH unwinding support for PowerPC and PowerPC64 Linux.
   Copyright (C) 2004, 2005, 2006 Free Software Foundation, Inc.

   This file is part of GCC.

   GCC is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published
   by the Free Software Foundation; either version 2, or (at your
   option) any later version.

   In addition to the permissions in the GNU General Public License,
   the Free Software Foundation gives you unlimited permission to link
   the compiled version of this file with other programs, and to
   distribute those programs without any restriction coming from the
   use of this file.  (The General Public License restrictions do
   apply in other respects; for example, they cover modification of
   the file, and distribution when not linked into another program.)

   GCC is distributed in the hope that it will be useful, but WITHOUT
   ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
   or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
   License for more details.

   You should have received a copy of the GNU General Public License
   along with GCC; see the file COPYING.  If not, write to the
   Free Software Foundation, 51 Franklin Street, Fifth Floor, Boston,
   MA 02110-1301, USA.  */

/* This file defines our own versions of various kernel and user
   structs, so that system headers are not needed, which otherwise
   can make bootstrapping a new toolchain difficult.  Do not use
   these structs elsewhere;  Many fields are missing, particularly
   from the end of the structures.  */

struct gcc_vregs
{
  __attribute__ ((vector_size (16))) int vr[32];
#ifdef __powerpc64__
  unsigned int pad1[3];
  unsigned int vscr;
  unsigned int vsave;
  unsigned int pad2[3];
#else
  unsigned int vsave;
  unsigned int pad[2];
  unsigned int vscr;
#endif
};

struct gcc_regs
{
  unsigned long gpr[32];
  unsigned long nip;
  unsigned long msr;
  unsigned long orig_gpr3;
  unsigned long ctr;
  unsigned long link;
  unsigned long xer;
  unsigned long ccr;
  unsigned long softe;
  unsigned long trap;
  unsigned long dar;
  unsigned long dsisr;
  unsigned long result;
  unsigned long pad1[4];
  double fpr[32];
  unsigned int pad2;
  unsigned int fpscr;
#ifdef __powerpc64__
  struct gcc_vregs *vp;
#else
  unsigned int pad3[2];
#endif
  struct gcc_vregs vregs;
};

struct gcc_ucontext
{
#ifdef __powerpc64__
  unsigned long pad[28];
#else
  unsigned long pad[12];
#endif
  struct gcc_regs *regs;
  struct gcc_regs rsave;
};

#ifdef __powerpc64__

enum { SIGNAL_FRAMESIZE = 128 };

/* If PC is at a sigreturn trampoline, return a pointer to the
   regs.  Otherwise return NULL.  */

static struct gcc_regs *
get_regs (struct _Unwind_Context *context)
{
  const unsigned char *pc = context->ra;

  /* addi r1, r1, 128; li r0, 0x0077; sc  (sigreturn) */
  /* addi r1, r1, 128; li r0, 0x00AC; sc  (rt_sigreturn) */
  if (*(unsigned int *) (pc + 0) != 0x38210000 + SIGNAL_FRAMESIZE
      || *(unsigned int *) (pc + 8) != 0x44000002)
    return NULL;
  if (*(unsigned int *) (pc + 4) == 0x38000077)
    {
      struct sigframe {
	char gap[SIGNAL_FRAMESIZE];
	unsigned long pad[7];
	struct gcc_regs *regs;
      } *frame = (struct sigframe *) context->cfa;
      return frame->regs;
    }
  else if (*(unsigned int *) (pc + 4) == 0x380000AC)
    {
      /* This works for 2.4 kernels, but not for 2.6 kernels with vdso
	 because pc isn't pointing into the stack.  Can be removed when
	 no one is running 2.4.19 or 2.4.20, the first two ppc64
	 kernels released.  */
      struct rt_sigframe_24 {
	int tramp[6];
	void *pinfo;
	struct gcc_ucontext *puc;
      } *frame24 = (struct rt_sigframe_24 *) pc;

      /* Test for magic value in *puc of vdso.  */
      if ((long) frame24->puc != -21 * 8)
	return frame24->puc->regs;
      else
	{
	  /* This works for 2.4.21 and later kernels.  */
	  struct rt_sigframe {
	    char gap[SIGNAL_FRAMESIZE];
	    struct gcc_ucontext uc;
	    unsigned long pad[2];
	    int tramp[6];
	    void *pinfo;
	    struct gcc_ucontext *puc;
	  } *frame = (struct rt_sigframe *) context->cfa;
	  return frame->uc.regs;
	}
    }
  return NULL;
}

#else  /* !__powerpc64__ */

enum { SIGNAL_FRAMESIZE = 64 };

static struct gcc_regs *
get_regs (struct _Unwind_Context *context)
{
  const unsigned char *pc = context->ra;

  /* li r0, 0x7777; sc  (sigreturn old)  */
  /* li r0, 0x0077; sc  (sigreturn new)  */
  /* li r0, 0x6666; sc  (rt_sigreturn old)  */
  /* li r0, 0x00AC; sc  (rt_sigreturn new)  */
  if (*(unsigned int *) (pc + 4) != 0x44000002)
    return NULL;
  if (*(unsigned int *) (pc + 0) == 0x38007777
      || *(unsigned int *) (pc + 0) == 0x38000077)
    {
      struct sigframe {
	char gap[SIGNAL_FRAMESIZE];
	unsigned long pad[7];
	struct gcc_regs *regs;
      } *frame = (struct sigframe *) context->cfa;
      return frame->regs;
    }
  else if (*(unsigned int *) (pc + 0) == 0x38006666
	   || *(unsigned int *) (pc + 0) == 0x380000AC)
    {
      struct rt_sigframe {
	char gap[SIGNAL_FRAMESIZE + 16];
	char siginfo[128];
	struct gcc_ucontext uc;
      } *frame = (struct rt_sigframe *) context->cfa;
      return frame->uc.regs;
    }
  return NULL;
}
#endif

/* Find an entry in the process auxiliary vector.  The canonical way to
   test for VMX is to look at AT_HWCAP.  */

static long
ppc_linux_aux_vector (long which)
{
  /* __libc_stack_end holds the original stack passed to a process.  */
  extern long *__libc_stack_end;
  long argc;
  char **argv;
  char **envp;
  struct auxv
  {
    long a_type;
    long a_val;
  } *auxp;

  /* The Linux kernel puts argc first on the stack.  */
  argc = __libc_stack_end[0];
  /* Followed by argv, NULL terminated.  */
  argv = (char **) __libc_stack_end + 1;
  /* Followed by environment string pointers, NULL terminated. */
  envp = argv + argc + 1;
  while (*envp++)
    continue;
  /* Followed by the aux vector, zero terminated.  */
  for (auxp = (struct auxv *) envp; auxp->a_type != 0; ++auxp)
    if (auxp->a_type == which)
      return auxp->a_val;
  return 0;
}

/* Do code reading to identify a signal frame, and set the frame
   state data appropriately.  See unwind-dw2.c for the structs.  */

#define MD_FALLBACK_FRAME_STATE_FOR ppc_fallback_frame_state

static _Unwind_Reason_Code
ppc_fallback_frame_state (struct _Unwind_Context *context,
			  _Unwind_FrameState *fs)
{
  static long hwcap = 0;
  struct gcc_regs *regs = get_regs (context);
  long new_cfa;
  int i;

  if (regs == NULL)
    return _URC_END_OF_STACK;

  new_cfa = regs->gpr[STACK_POINTER_REGNUM];
  fs->cfa_how = CFA_REG_OFFSET;
  fs->cfa_reg = STACK_POINTER_REGNUM;
  fs->cfa_offset = new_cfa - (long) context->cfa;

  for (i = 0; i < 32; i++)
    if (i != STACK_POINTER_REGNUM)
      {
	fs->regs.reg[i].how = REG_SAVED_OFFSET;
	fs->regs.reg[i].loc.offset = (long) &regs->gpr[i] - new_cfa;
      }

  fs->regs.reg[CR2_REGNO].how = REG_SAVED_OFFSET;
  fs->regs.reg[CR2_REGNO].loc.offset = (long) &regs->ccr - new_cfa;

  fs->regs.reg[LINK_REGISTER_REGNUM].how = REG_SAVED_OFFSET;
  fs->regs.reg[LINK_REGISTER_REGNUM].loc.offset = (long) &regs->link - new_cfa;

  fs->regs.reg[ARG_POINTER_REGNUM].how = REG_SAVED_OFFSET;
  fs->regs.reg[ARG_POINTER_REGNUM].loc.offset = (long) &regs->nip - new_cfa;
  fs->retaddr_column = ARG_POINTER_REGNUM;
  fs->signal_frame = 1;

  if (hwcap == 0)
    {
      hwcap = ppc_linux_aux_vector (16);
      /* These will already be set if we found AT_HWCAP.  A nonzero
	 value stops us looking again if for some reason we couldn't
	 find AT_HWCAP.  */
#ifdef __powerpc64__
      hwcap |= 0xc0000000;
#else
      hwcap |= 0x80000000;
#endif
    }

  /* If we have a FPU...  */
  if (hwcap & 0x08000000)
    for (i = 0; i < 32; i++)
      {
	fs->regs.reg[i + 32].how = REG_SAVED_OFFSET;
	fs->regs.reg[i + 32].loc.offset = (long) &regs->fpr[i] - new_cfa;
      }

  /* If we have a VMX unit...  */
  if (hwcap & 0x10000000)
    {
      struct gcc_vregs *vregs;
#ifdef __powerpc64__
      vregs = regs->vp;
#else
      vregs = &regs->vregs;
#endif
      if (regs->msr & (1 << 25))
	{
	  for (i = 0; i < 32; i++)
	    {
	      fs->regs.reg[i + FIRST_ALTIVEC_REGNO].how = REG_SAVED_OFFSET;
	      fs->regs.reg[i + FIRST_ALTIVEC_REGNO].loc.offset
		= (long) &vregs[i] - new_cfa;
	    }

	  fs->regs.reg[VSCR_REGNO].how = REG_SAVED_OFFSET;
	  fs->regs.reg[VSCR_REGNO].loc.offset = (long) &vregs->vscr - new_cfa;
	}

      fs->regs.reg[VRSAVE_REGNO].how = REG_SAVED_OFFSET;
      fs->regs.reg[VRSAVE_REGNO].loc.offset = (long) &vregs->vsave - new_cfa;
    }

  return _URC_NO_REASON;
}

#define MD_FROB_UPDATE_CONTEXT frob_update_context

static void
frob_update_context (struct _Unwind_Context *context, _Unwind_FrameState *fs ATTRIBUTE_UNUSED)
{
  const unsigned int *pc = (const unsigned int *) context->ra;

  /* Fix up for 2.6.12 - 2.6.16 Linux kernels that have vDSO, but don't
     have S flag in it.  */
#ifdef __powerpc64__
  /* addi r1, r1, 128; li r0, 0x0077; sc  (sigreturn) */
  /* addi r1, r1, 128; li r0, 0x00AC; sc  (rt_sigreturn) */
  if (pc[0] == 0x38210000 + SIGNAL_FRAMESIZE
      && (pc[1] == 0x38000077 || pc[1] == 0x380000AC)
      && pc[2] == 0x44000002)
    _Unwind_SetSignalFrame (context, 1);
#else
  /* li r0, 0x7777; sc  (sigreturn old)  */
  /* li r0, 0x0077; sc  (sigreturn new)  */
  /* li r0, 0x6666; sc  (rt_sigreturn old)  */
  /* li r0, 0x00AC; sc  (rt_sigreturn new)  */
  if ((pc[0] == 0x38007777 || pc[0] == 0x38000077
       || pc[0] == 0x38006666 || pc[0] == 0x380000AC)
      && pc[1] == 0x44000002)
    _Unwind_SetSignalFrame (context, 1);
#endif

#ifdef __powerpc64__
  if (fs->regs.reg[2].how == REG_UNSAVED)
    {
      /* If the current unwind info (FS) does not contain explicit info
	 saving R2, then we have to do a minor amount of code reading to
	 figure out if it was saved.  The big problem here is that the
	 code that does the save/restore is generated by the linker, so
	 we have no good way to determine at compile time what to do.  */
      unsigned int *insn
	= (unsigned int *) _Unwind_GetGR (context, LINK_REGISTER_REGNUM);
      if (*insn == 0xE8410028)
	_Unwind_SetGRPtr (context, 2, context->cfa + 40);
    }
#endif
}
