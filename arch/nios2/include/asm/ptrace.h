/*
 * Copyright (C) 2013 Altera Corporation
 * Copyright (C) 2010 Tobias Klauser <tklauser@distanz.ch>
 * Copyright (C) 2004 Microtronix Datacom Ltd
 *
 * based on m68k asm/processor.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#ifndef _ASM_NIOS2_PTRACE_H
#define _ASM_NIOS2_PTRACE_H

#include <uapi/asm/ptrace.h>

/* This struct defines the way the registers are stored on the
   stack during a system call.  */

#ifndef __ASSEMBLY__
struct pt_regs {
	unsigned long  r8;	/* r8-r15 Caller-saved GP registers */
	unsigned long  r9;
	unsigned long  r10;
	unsigned long  r11;
	unsigned long  r12;
	unsigned long  r13;
	unsigned long  r14;
	unsigned long  r15;
	unsigned long  r1;	/* Assembler temporary */
	unsigned long  r2;	/* Retval LS 32bits */
	unsigned long  r3;	/* Retval MS 32bits */
	unsigned long  r4;	/* r4-r7 Register arguments */
	unsigned long  r5;
	unsigned long  r6;
	unsigned long  r7;
	unsigned long  orig_r2;	/* Copy of r2 ?? */
	unsigned long  ra;	/* Return address */
	unsigned long  fp;	/* Frame pointer */
	unsigned long  sp;	/* Stack pointer */
	unsigned long  gp;	/* Global pointer */
	unsigned long  estatus;
	unsigned long  ea;	/* Exception return address (pc) */
	unsigned long  orig_r7;
};

/*
 * This is the extended stack used by signal handlers and the context
 * switcher: it's pushed after the normal "struct pt_regs".
 */
struct switch_stack {
	unsigned long  r16;	/* r16-r23 Callee-saved GP registers */
	unsigned long  r17;
	unsigned long  r18;
	unsigned long  r19;
	unsigned long  r20;
	unsigned long  r21;
	unsigned long  r22;
	unsigned long  r23;
	unsigned long  fp;
	unsigned long  gp;
	unsigned long  ra;
};

#define user_mode(regs)	(((regs)->estatus & ESTATUS_EU))

#define instruction_pointer(regs)	((regs)->ra)
#define profile_pc(regs)		instruction_pointer(regs)
#define user_stack_pointer(regs)	((regs)->sp)
extern void show_regs(struct pt_regs *);

#define current_pt_regs() \
	((struct pt_regs *)((unsigned long)current_thread_info() + THREAD_SIZE)\
		- 1)

int do_syscall_trace_enter(void);
void do_syscall_trace_exit(void);
#endif /* __ASSEMBLY__ */
#endif /* _ASM_NIOS2_PTRACE_H */
