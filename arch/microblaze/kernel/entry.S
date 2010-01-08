/*
 * Low-level system-call handling, trap handlers and context-switching
 *
 * Copyright (C) 2008-2009 Michal Simek <monstr@monstr.eu>
 * Copyright (C) 2008-2009 PetaLogix
 * Copyright (C) 2003		John Williams <jwilliams@itee.uq.edu.au>
 * Copyright (C) 2001,2002	NEC Corporation
 * Copyright (C) 2001,2002	Miles Bader <miles@gnu.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License. See the file COPYING in the main directory of this
 * archive for more details.
 *
 * Written by Miles Bader <miles@gnu.org>
 * Heavily modified by John Williams for Microblaze
 */

#include <linux/sys.h>
#include <linux/linkage.h>

#include <asm/entry.h>
#include <asm/current.h>
#include <asm/processor.h>
#include <asm/exceptions.h>
#include <asm/asm-offsets.h>
#include <asm/thread_info.h>

#include <asm/page.h>
#include <asm/unistd.h>

#include <linux/errno.h>
#include <asm/signal.h>

#undef DEBUG

/* The size of a state save frame. */
#define STATE_SAVE_SIZE		(PT_SIZE + STATE_SAVE_ARG_SPACE)

/* The offset of the struct pt_regs in a `state save frame' on the stack. */
#define PTO	STATE_SAVE_ARG_SPACE /* 24 the space for args */

#define C_ENTRY(name)	.globl name; .align 4; name

/*
 * Various ways of setting and clearing BIP in flags reg.
 * This is mucky, but necessary using microblaze version that
 * allows msr ops to write to BIP
 */
#if CONFIG_XILINX_MICROBLAZE0_USE_MSR_INSTR
	.macro	clear_bip
	msrclr	r11, MSR_BIP
	nop
	.endm

	.macro	set_bip
	msrset	r11, MSR_BIP
	nop
	.endm

	.macro	clear_eip
	msrclr	r11, MSR_EIP
	nop
	.endm

	.macro	set_ee
	msrset	r11, MSR_EE
	nop
	.endm

	.macro	disable_irq
	msrclr	r11, MSR_IE
	nop
	.endm

	.macro	enable_irq
	msrset	r11, MSR_IE
	nop
	.endm

	.macro	set_ums
	msrset	r11, MSR_UMS
	nop
	msrclr	r11, MSR_VMS
	nop
	.endm

	.macro	set_vms
	msrclr	r11, MSR_UMS
	nop
	msrset	r11, MSR_VMS
	nop
	.endm

	.macro	clear_vms_ums
	msrclr	r11, MSR_VMS
	nop
	msrclr	r11, MSR_UMS
	nop
	.endm
#else
	.macro	clear_bip
	mfs	r11, rmsr
	nop
	andi	r11, r11, ~MSR_BIP
	mts	rmsr, r11
	nop
	.endm

	.macro	set_bip
	mfs	r11, rmsr
	nop
	ori	r11, r11, MSR_BIP
	mts	rmsr, r11
	nop
	.endm

	.macro	clear_eip
	mfs	r11, rmsr
	nop
	andi	r11, r11, ~MSR_EIP
	mts	rmsr, r11
	nop
	.endm

	.macro	set_ee
	mfs	r11, rmsr
	nop
	ori	r11, r11, MSR_EE
	mts	rmsr, r11
	nop
	.endm

	.macro	disable_irq
	mfs	r11, rmsr
	nop
	andi	r11, r11, ~MSR_IE
	mts	rmsr, r11
	nop
	.endm

	.macro	enable_irq
	mfs	r11, rmsr
	nop
	ori	r11, r11, MSR_IE
	mts	rmsr, r11
	nop
	.endm

	.macro set_ums
	mfs	r11, rmsr
	nop
	ori	r11, r11, MSR_VMS
	andni	r11, r11, MSR_UMS
	mts	rmsr, r11
	nop
	.endm

	.macro	set_vms
	mfs	r11, rmsr
	nop
	ori	r11, r11, MSR_VMS
	andni	r11, r11, MSR_UMS
	mts	rmsr, r11
	nop
	.endm

	.macro	clear_vms_ums
	mfs	r11, rmsr
	nop
	andni	r11, r11, (MSR_VMS|MSR_UMS)
	mts	rmsr,r11
	nop
	.endm
#endif

/* Define how to call high-level functions. With MMU, virtual mode must be
 * enabled when calling the high-level function. Clobbers R11.
 * VM_ON, VM_OFF, DO_JUMP_BIPCLR, DO_CALL
 */

/* turn on virtual protected mode save */
#define VM_ON		\
	set_ums;		\
	rted	r0, 2f;	\
2: nop;

/* turn off virtual protected mode save and user mode save*/
#define VM_OFF			\
	clear_vms_ums;			\
	rted	r0, TOPHYS(1f);	\
1: nop;

#define SAVE_REGS \
	swi	r2, r1, PTO+PT_R2;	/* Save SDA */			\
	swi	r5, r1, PTO+PT_R5;					\
	swi	r6, r1, PTO+PT_R6;					\
	swi	r7, r1, PTO+PT_R7;					\
	swi	r8, r1, PTO+PT_R8;					\
	swi	r9, r1, PTO+PT_R9;					\
	swi	r10, r1, PTO+PT_R10;					\
	swi	r11, r1, PTO+PT_R11;	/* save clobbered regs after rval */\
	swi	r12, r1, PTO+PT_R12;					\
	swi	r13, r1, PTO+PT_R13;	/* Save SDA2 */			\
	swi	r14, r1, PTO+PT_PC;	/* PC, before IRQ/trap */	\
	swi	r15, r1, PTO+PT_R15;	/* Save LP */			\
	swi	r18, r1, PTO+PT_R18;	/* Save asm scratch reg */	\
	swi	r19, r1, PTO+PT_R19;					\
	swi	r20, r1, PTO+PT_R20;					\
	swi	r21, r1, PTO+PT_R21;					\
	swi	r22, r1, PTO+PT_R22;					\
	swi	r23, r1, PTO+PT_R23;					\
	swi	r24, r1, PTO+PT_R24;					\
	swi	r25, r1, PTO+PT_R25;					\
	swi	r26, r1, PTO+PT_R26;					\
	swi	r27, r1, PTO+PT_R27;					\
	swi	r28, r1, PTO+PT_R28;					\
	swi	r29, r1, PTO+PT_R29;					\
	swi	r30, r1, PTO+PT_R30;					\
	swi	r31, r1, PTO+PT_R31;	/* Save current task reg */	\
	mfs	r11, rmsr;		/* save MSR */			\
	nop;								\
	swi	r11, r1, PTO+PT_MSR;

#define RESTORE_REGS \
	lwi	r11, r1, PTO+PT_MSR;					\
	mts	rmsr , r11;						\
	nop;								\
	lwi	r2, r1, PTO+PT_R2;	/* restore SDA */		\
	lwi	r5, r1, PTO+PT_R5;					\
	lwi	r6, r1, PTO+PT_R6;					\
	lwi	r7, r1, PTO+PT_R7;					\
	lwi	r8, r1, PTO+PT_R8;					\
	lwi	r9, r1, PTO+PT_R9;					\
	lwi	r10, r1, PTO+PT_R10;					\
	lwi	r11, r1, PTO+PT_R11;	/* restore clobbered regs after rval */\
	lwi	r12, r1, PTO+PT_R12;					\
	lwi	r13, r1, PTO+PT_R13;	/* restore SDA2 */		\
	lwi	r14, r1, PTO+PT_PC;	/* RESTORE_LINK PC, before IRQ/trap */\
	lwi	r15, r1, PTO+PT_R15;	/* restore LP */		\
	lwi	r18, r1, PTO+PT_R18;	/* restore asm scratch reg */	\
	lwi	r19, r1, PTO+PT_R19;					\
	lwi	r20, r1, PTO+PT_R20;					\
	lwi	r21, r1, PTO+PT_R21;					\
	lwi	r22, r1, PTO+PT_R22;					\
	lwi	r23, r1, PTO+PT_R23;					\
	lwi	r24, r1, PTO+PT_R24;					\
	lwi	r25, r1, PTO+PT_R25;					\
	lwi	r26, r1, PTO+PT_R26;					\
	lwi	r27, r1, PTO+PT_R27;					\
	lwi	r28, r1, PTO+PT_R28;					\
	lwi	r29, r1, PTO+PT_R29;					\
	lwi	r30, r1, PTO+PT_R30;					\
	lwi	r31, r1, PTO+PT_R31;	/* Restore cur task reg */

.text

/*
 * User trap.
 *
 * System calls are handled here.
 *
 * Syscall protocol:
 * Syscall number in r12, args in r5-r10
 * Return value in r3
 *
 * Trap entered via brki instruction, so BIP bit is set, and interrupts
 * are masked. This is nice, means we don't have to CLI before state save
 */
C_ENTRY(_user_exception):
	swi	r1, r0, TOPHYS(PER_CPU(ENTRY_SP)) /* save stack */
	addi	r14, r14, 4	/* return address is 4 byte after call */
	swi	r11, r0, TOPHYS(r0_ram + PTO + PT_R11);	/* Save r11 */

	lwi	r11, r0, TOPHYS(PER_CPU(KM));/* See if already in kernel mode.*/
	beqi	r11, 1f;		/* Jump ahead if coming from user */
/* Kernel-mode state save. */
	lwi	r11, r0, TOPHYS(PER_CPU(ENTRY_SP)); /* Reload kernel stack-ptr*/
	tophys(r1,r11);
	swi	r11, r1, (PT_R1-PT_SIZE); /* Save original SP. */
	lwi	r11, r0, TOPHYS(r0_ram + PTO + PT_R11); /* restore r11 */

	addik	r1, r1, -STATE_SAVE_SIZE; /* Make room on the stack. */
	SAVE_REGS

	addi	r11, r0, 1; 		/* Was in kernel-mode. */
	swi	r11, r1, PTO+PT_MODE; /* pt_regs -> kernel mode */
	brid	2f;
	nop;				/* Fill delay slot */

/* User-mode state save.  */
1:
	lwi	r11, r0, TOPHYS(r0_ram + PTO + PT_R11);	/* restore r11 */
	lwi	r1, r0, TOPHYS(PER_CPU(CURRENT_SAVE)); /* get saved current */
	tophys(r1,r1);
	lwi	r1, r1, TS_THREAD_INFO;	/* get stack from task_struct */
/* calculate kernel stack pointer from task struct 8k */
	addik	r1, r1, THREAD_SIZE;
	tophys(r1,r1);

	addik	r1, r1, -STATE_SAVE_SIZE; /* Make room on the stack.  */
	SAVE_REGS

	swi	r0, r1, PTO+PT_MODE;			/* Was in user-mode. */
	lwi	r11, r0, TOPHYS(PER_CPU(ENTRY_SP));
	swi	r11, r1, PTO+PT_R1;		/* Store user SP.  */
	addi	r11, r0, 1;
	swi	r11, r0, TOPHYS(PER_CPU(KM));	/* Now we're in kernel-mode.  */
2:	lwi	r31, r0, TOPHYS(PER_CPU(CURRENT_SAVE));	/* get saved current */
	/* Save away the syscall number.  */
	swi	r12, r1, PTO+PT_R0;
	tovirt(r1,r1)

/* where the trap should return need -8 to adjust for rtsd r15, 8*/
/* Jump to the appropriate function for the system call number in r12
 * (r12 is not preserved), or return an error if r12 is not valid. The LP
 * register should point to the location where
 * the called function should return.  [note that MAKE_SYS_CALL uses label 1] */

	# Step into virtual mode.
	set_vms;
	addik	r11, r0, 3f
	rtid	r11, 0
	nop
3:
	add	r11, r0, CURRENT_TASK	 /* Get current task ptr into r11 */
	lwi	r11, r11, TS_THREAD_INFO /* get thread info */
	lwi	r11, r11, TI_FLAGS	 /* get flags in thread info */
	andi	r11, r11, _TIF_WORK_SYSCALL_MASK
	beqi	r11, 4f

	addik	r3, r0, -ENOSYS
	swi	r3, r1, PTO + PT_R3
	brlid	r15, do_syscall_trace_enter
	addik	r5, r1, PTO + PT_R0

	# do_syscall_trace_enter returns the new syscall nr.
	addk	r12, r0, r3
	lwi	r5, r1, PTO+PT_R5;
	lwi	r6, r1, PTO+PT_R6;
	lwi	r7, r1, PTO+PT_R7;
	lwi	r8, r1, PTO+PT_R8;
	lwi	r9, r1, PTO+PT_R9;
	lwi	r10, r1, PTO+PT_R10;
4:
/* Jump to the appropriate function for the system call number in r12
 * (r12 is not preserved), or return an error if r12 is not valid.
 * The LP register should point to the location where the called function
 * should return.  [note that MAKE_SYS_CALL uses label 1] */
	/* See if the system call number is valid */
	addi	r11, r12, -__NR_syscalls;
	bgei	r11,5f;
	/* Figure out which function to use for this system call.  */
	/* Note Microblaze barrel shift is optional, so don't rely on it */
	add	r12, r12, r12;			/* convert num -> ptr */
	add	r12, r12, r12;

#ifdef DEBUG
	/* Trac syscalls and stored them to r0_ram */
	lwi	r3, r12, 0x400 + r0_ram
	addi	r3, r3, 1
	swi	r3, r12, 0x400 + r0_ram
#endif

	# Find and jump into the syscall handler.
	lwi	r12, r12, sys_call_table
	/* where the trap should return need -8 to adjust for rtsd r15, 8 */
	la	r15, r0, ret_from_trap-8
	bra	r12

	/* The syscall number is invalid, return an error.  */
5:
	addi	r3, r0, -ENOSYS;
	rtsd	r15,8;		/* looks like a normal subroutine return */
	or 	r0, r0, r0


/* Entry point used to return from a syscall/trap */
/* We re-enable BIP bit before state restore */
C_ENTRY(ret_from_trap):
	set_bip;			/*  Ints masked for state restore*/
	lwi	r11, r1, PTO+PT_MODE;
/* See if returning to kernel mode, if so, skip resched &c.  */
	bnei	r11, 2f;

	/* We're returning to user mode, so check for various conditions that
	 * trigger rescheduling. */
	# FIXME: Restructure all these flag checks.
	add	r11, r0, CURRENT_TASK;	/* Get current task ptr into r11 */
	lwi	r11, r11, TS_THREAD_INFO;	/* get thread info */
	lwi	r11, r11, TI_FLAGS;		/* get flags in thread info */
	andi	r11, r11, _TIF_WORK_SYSCALL_MASK
	beqi	r11, 1f

	swi	r3, r1, PTO + PT_R3
	swi	r4, r1, PTO + PT_R4
	brlid	r15, do_syscall_trace_leave
	addik	r5, r1, PTO + PT_R0
	lwi	r3, r1, PTO + PT_R3
	lwi	r4, r1, PTO + PT_R4
1:

	/* We're returning to user mode, so check for various conditions that
	 * trigger rescheduling. */
	/* Get current task ptr into r11 */
	add	r11, r0, CURRENT_TASK;	/* Get current task ptr into r11 */
	lwi	r11, r11, TS_THREAD_INFO;	/* get thread info */
	lwi	r11, r11, TI_FLAGS;		/* get flags in thread info */
	andi	r11, r11, _TIF_NEED_RESCHED;
	beqi	r11, 5f;

	swi	r3, r1, PTO + PT_R3; /* store syscall result */
	swi	r4, r1, PTO + PT_R4;
	bralid	r15, schedule;	/* Call scheduler */
	nop;				/* delay slot */
	lwi	r3, r1, PTO + PT_R3; /* restore syscall result */
	lwi	r4, r1, PTO + PT_R4;

	/* Maybe handle a signal */
5:	add	r11, r0, CURRENT_TASK; /* Get current task ptr into r11 */
	lwi	r11, r11, TS_THREAD_INFO;	/* get thread info */
	lwi	r11, r11, TI_FLAGS;	/* get flags in thread info */
	andi	r11, r11, _TIF_SIGPENDING;
	beqi	r11, 1f;		/* Signals to handle, handle them */

	swi	r3, r1, PTO + PT_R3; /* store syscall result */
	swi	r4, r1, PTO + PT_R4;
	la	r5, r1, PTO;		/* Arg 1: struct pt_regs *regs */
	add	r6, r0, r0;		/* Arg 2: sigset_t *oldset */
	addi	r7, r0, 1;		/* Arg 3: int in_syscall */
	bralid	r15, do_signal;	/* Handle any signals */
	nop;
	lwi	r3, r1, PTO + PT_R3; /* restore syscall result */
	lwi	r4, r1, PTO + PT_R4;

/* Finally, return to user state.  */
1:	swi	r0, r0, PER_CPU(KM);	/* Now officially in user state. */
	add	r11, r0, CURRENT_TASK;	/* Get current task ptr into r11 */
	swi	r11, r0, PER_CPU(CURRENT_SAVE); /* save current */
	VM_OFF;
	tophys(r1,r1);
	RESTORE_REGS;
	addik	r1, r1, STATE_SAVE_SIZE		/* Clean up stack space.  */
	lwi	r1, r1, PT_R1 - PT_SIZE;/* Restore user stack pointer. */
	bri	6f;

/* Return to kernel state.  */
2:	VM_OFF;
	tophys(r1,r1);
	RESTORE_REGS;
	addik	r1, r1, STATE_SAVE_SIZE		/* Clean up stack space.  */
	tovirt(r1,r1);
6:
TRAP_return:		/* Make global symbol for debugging */
	rtbd	r14, 0;	/* Instructions to return from an IRQ */
	nop;


/* These syscalls need access to the struct pt_regs on the stack, so we
   implement them in assembly (they're basically all wrappers anyway).  */

C_ENTRY(sys_fork_wrapper):
	addi	r5, r0, SIGCHLD			/* Arg 0: flags */
	lwi	r6, r1, PTO+PT_R1	/* Arg 1: child SP (use parent's) */
	la	r7, r1, PTO			/* Arg 2: parent context */
	add	r8. r0, r0			/* Arg 3: (unused) */
	add	r9, r0, r0;			/* Arg 4: (unused) */
	add	r10, r0, r0;			/* Arg 5: (unused) */
	brid	do_fork		/* Do real work (tail-call) */
	nop;

/* This the initial entry point for a new child thread, with an appropriate
   stack in place that makes it look the the child is in the middle of an
   syscall.  This function is actually `returned to' from switch_thread
   (copy_thread makes ret_from_fork the return address in each new thread's
   saved context).  */
C_ENTRY(ret_from_fork):
	bralid	r15, schedule_tail; /* ...which is schedule_tail's arg */
	add	r3, r5, r0;	/* switch_thread returns the prev task */
				/* ( in the delay slot ) */
	add	r3, r0, r0;	/* Child's fork call should return 0. */
	brid	ret_from_trap;	/* Do normal trap return */
	nop;

C_ENTRY(sys_vfork):
	brid	microblaze_vfork	/* Do real work (tail-call) */
	la	r5, r1, PTO

C_ENTRY(sys_clone):
	bnei	r6, 1f;			/* See if child SP arg (arg 1) is 0. */
	lwi	r6, r1, PTO+PT_R1;	/* If so, use paret's stack ptr */
1:	la	r7, r1, PTO;			/* Arg 2: parent context */
	add	r8, r0, r0;			/* Arg 3: (unused) */
	add	r9, r0, r0;			/* Arg 4: (unused) */
	add	r10, r0, r0;			/* Arg 5: (unused) */
	brid	do_fork		/* Do real work (tail-call) */
	nop;

C_ENTRY(sys_execve):
	la	r8, r1, PTO;		/* add user context as 4th arg */
	brid	microblaze_execve;	/* Do real work (tail-call).*/
	nop;

C_ENTRY(sys_rt_sigreturn_wrapper):
	swi	r3, r1, PTO+PT_R3; /* restore saved r3, r4 registers */
	swi	r4, r1, PTO+PT_R4;
	la	r5, r1, PTO;		/* add user context as 1st arg */
	brlid	r15, sys_rt_sigreturn	/* Do real work */
	nop;
	lwi	r3, r1, PTO+PT_R3; /* restore saved r3, r4 registers */
	lwi	r4, r1, PTO+PT_R4;
	bri ret_from_trap /* fall through will not work here due to align */
	nop;

/*
 * HW EXCEPTION rutine start
 */

#define SAVE_STATE	\
	swi	r11, r0, TOPHYS(r0_ram + PTO + PT_R11); /* Save r11 */	\
	set_bip;	/*equalize initial state for all possible entries*/\
	clear_eip;							\
	enable_irq;							\
	set_ee;								\
	/* See if already in kernel mode.*/				\
	lwi	r11, r0, TOPHYS(PER_CPU(KM));				\
	beqi	r11, 1f;		/* Jump ahead if coming from user */\
	/* Kernel-mode state save.  */					\
	/* Reload kernel stack-ptr. */					\
	lwi	r11, r0, TOPHYS(PER_CPU(ENTRY_SP));			\
	tophys(r1,r11);							\
	swi	r11, r1, (PT_R1-PT_SIZE); /* Save original SP. */	\
	lwi	r11, r0, TOPHYS(r0_ram + PTO + PT_R11); /* restore r11 */\
	addik	r1, r1, -STATE_SAVE_SIZE; /* Make room on the stack.  */\
	/* store return registers separately because			\
	 * this macros is use for others exceptions */			\
	swi	r3, r1, PTO + PT_R3;					\
	swi	r4, r1, PTO + PT_R4;					\
	SAVE_REGS							\
	/* PC, before IRQ/trap - this is one instruction above */	\
	swi	r17, r1, PTO+PT_PC;					\
									\
	addi	r11, r0, 1; 		/* Was in kernel-mode.  */	\
	swi	r11, r1, PTO+PT_MODE; 	 				\
	brid	2f;							\
	nop;				/* Fill delay slot */		\
1:	/* User-mode state save.  */					\
	lwi	r11, r0, TOPHYS(r0_ram + PTO + PT_R11); /* restore r11 */\
	lwi	r1, r0, TOPHYS(PER_CPU(CURRENT_SAVE)); /* get saved current */\
	tophys(r1,r1);							\
	lwi	r1, r1, TS_THREAD_INFO;	/* get the thread info */	\
	addik	r1, r1, THREAD_SIZE;	/* calculate kernel stack pointer */\
	tophys(r1,r1);							\
									\
	addik	r1, r1, -STATE_SAVE_SIZE; /* Make room on the stack.  */\
	/* store return registers separately because this macros	\
	 * is use for others exceptions */				\
	swi	r3, r1, PTO + PT_R3; 					\
	swi	r4, r1, PTO + PT_R4;					\
	SAVE_REGS							\
	/* PC, before IRQ/trap - this is one instruction above FIXME*/	\
	swi	r17, r1, PTO+PT_PC;					\
									\
	swi	r0, r1, PTO+PT_MODE; /* Was in user-mode.  */		\
	lwi	r11, r0, TOPHYS(PER_CPU(ENTRY_SP));			\
	swi	r11, r1, PTO+PT_R1; /* Store user SP.  */		\
	addi	r11, r0, 1;						\
	swi	r11, r0, TOPHYS(PER_CPU(KM)); /* Now we're in kernel-mode.*/\
2:	lwi	r31, r0, TOPHYS(PER_CPU(CURRENT_SAVE)); /* get saved current */\
	/* Save away the syscall number.  */				\
	swi	r0, r1, PTO+PT_R0;					\
	tovirt(r1,r1)

C_ENTRY(full_exception_trap):
	swi	r1, r0, TOPHYS(PER_CPU(ENTRY_SP)) /* save stack */
	/* adjust exception address for privileged instruction
	 * for finding where is it */
	addik	r17, r17, -4
	SAVE_STATE /* Save registers */
	/* FIXME this can be store directly in PT_ESR reg.
	 * I tested it but there is a fault */
	/* where the trap should return need -8 to adjust for rtsd r15, 8 */
	la	r15, r0, ret_from_exc - 8
	la	r5, r1, PTO		 /* parameter struct pt_regs * regs */
	mfs	r6, resr
	nop
	mfs	r7, rfsr;		/* save FSR */
	nop
	mts	rfsr, r0;	/* Clear sticky fsr */
	nop
	la	r12, r0, full_exception
	set_vms;
	rtbd	r12, 0;
	nop;

/*
 * Unaligned data trap.
 *
 * Unaligned data trap last on 4k page is handled here.
 *
 * Trap entered via exception, so EE bit is set, and interrupts
 * are masked.  This is nice, means we don't have to CLI before state save
 *
 * The assembler routine is in "arch/microblaze/kernel/hw_exception_handler.S"
 */
C_ENTRY(unaligned_data_trap):
	swi	r1, r0, TOPHYS(PER_CPU(ENTRY_SP)) /* save stack */
	SAVE_STATE		/* Save registers.*/
	/* where the trap should return need -8 to adjust for rtsd r15, 8 */
	la	r15, r0, ret_from_exc-8
	mfs	r3, resr		/* ESR */
	nop
	mfs	r4, rear		/* EAR */
	nop
	la	r7, r1, PTO		/* parameter struct pt_regs * regs */
	la	r12, r0, _unaligned_data_exception
	set_vms;
	rtbd	r12, 0;	/* interrupts enabled */
	nop;

/*
 * Page fault traps.
 *
 * If the real exception handler (from hw_exception_handler.S) didn't find
 * the mapping for the process, then we're thrown here to handle such situation.
 *
 * Trap entered via exceptions, so EE bit is set, and interrupts
 * are masked.  This is nice, means we don't have to CLI before state save
 *
 * Build a standard exception frame for TLB Access errors.  All TLB exceptions
 * will bail out to this point if they can't resolve the lightweight TLB fault.
 *
 * The C function called is in "arch/microblaze/mm/fault.c", declared as:
 * void do_page_fault(struct pt_regs *regs,
 *				unsigned long address,
 *				unsigned long error_code)
 */
/* data and intruction trap - which is choose is resolved int fault.c */
C_ENTRY(page_fault_data_trap):
	swi	r1, r0, TOPHYS(PER_CPU(ENTRY_SP)) /* save stack */
	SAVE_STATE		/* Save registers.*/
	/* where the trap should return need -8 to adjust for rtsd r15, 8 */
	la	r15, r0, ret_from_exc-8
	la	r5, r1, PTO		/* parameter struct pt_regs * regs */
	mfs	r6, rear		/* parameter unsigned long address */
	nop
	mfs	r7, resr		/* parameter unsigned long error_code */
	nop
	la	r12, r0, do_page_fault
	set_vms;
	rtbd	r12, 0;	/* interrupts enabled */
	nop;

C_ENTRY(page_fault_instr_trap):
	swi	r1, r0, TOPHYS(PER_CPU(ENTRY_SP)) /* save stack */
	SAVE_STATE		/* Save registers.*/
	/* where the trap should return need -8 to adjust for rtsd r15, 8 */
	la	r15, r0, ret_from_exc-8
	la	r5, r1, PTO		/* parameter struct pt_regs * regs */
	mfs	r6, rear		/* parameter unsigned long address */
	nop
	ori	r7, r0, 0		/* parameter unsigned long error_code */
	la	r12, r0, do_page_fault
	set_vms;
	rtbd	r12, 0;	/* interrupts enabled */
	nop;

/* Entry point used to return from an exception.  */
C_ENTRY(ret_from_exc):
	set_bip;			/*  Ints masked for state restore*/
	lwi	r11, r1, PTO+PT_MODE;
	bnei	r11, 2f;		/* See if returning to kernel mode, */
					/* ... if so, skip resched &c.  */

	/* We're returning to user mode, so check for various conditions that
	   trigger rescheduling. */
	/* Get current task ptr into r11 */
	add	r11, r0, CURRENT_TASK; /* Get current task ptr into r11 */
	lwi	r11, r11, TS_THREAD_INFO;	/* get thread info */
	lwi	r11, r11, TI_FLAGS;	/* get flags in thread info */
	andi	r11, r11, _TIF_NEED_RESCHED;
	beqi	r11, 5f;

/* Call the scheduler before returning from a syscall/trap. */
	bralid	r15, schedule;	/* Call scheduler */
	nop;				/* delay slot */

	/* Maybe handle a signal */
5:	add	r11, r0, CURRENT_TASK; /* Get current task ptr into r11 */
	lwi	r11, r11, TS_THREAD_INFO;	/* get thread info */
	lwi	r11, r11, TI_FLAGS;	/* get flags in thread info */
	andi	r11, r11, _TIF_SIGPENDING;
	beqi	r11, 1f;		/* Signals to handle, handle them */

	/*
	 * Handle a signal return; Pending signals should be in r18.
	 *
	 * Not all registers are saved by the normal trap/interrupt entry
	 * points (for instance, call-saved registers (because the normal
	 * C-compiler calling sequence in the kernel makes sure they're
	 * preserved), and call-clobbered registers in the case of
	 * traps), but signal handlers may want to examine or change the
	 * complete register state.  Here we save anything not saved by
	 * the normal entry sequence, so that it may be safely restored
	 * (in a possibly modified form) after do_signal returns.
	 * store return registers separately because this macros is use
	 * for others exceptions */
	la	r5, r1, PTO;		/* Arg 1: struct pt_regs *regs */
	add	r6, r0, r0;		/* Arg 2: sigset_t *oldset */
	addi	r7, r0, 0;		/* Arg 3: int in_syscall */
	bralid	r15, do_signal;	/* Handle any signals */
	nop;

/* Finally, return to user state.  */
1:	swi	r0, r0, PER_CPU(KM);	/* Now officially in user state. */
	add	r11, r0, CURRENT_TASK; /* Get current task ptr into r11 */
	swi	r11, r0, PER_CPU(CURRENT_SAVE); /* save current */
	VM_OFF;
	tophys(r1,r1);

	lwi	r3, r1, PTO+PT_R3; /* restore saved r3, r4 registers */
	lwi	r4, r1, PTO+PT_R4;
	RESTORE_REGS;
	addik	r1, r1, STATE_SAVE_SIZE		/* Clean up stack space.  */

	lwi	r1, r1, PT_R1 - PT_SIZE; /* Restore user stack pointer. */
	bri	6f;
/* Return to kernel state.  */
2:	VM_OFF;
	tophys(r1,r1);
	lwi	r3, r1, PTO+PT_R3; /* restore saved r3, r4 registers */
	lwi	r4, r1, PTO+PT_R4;
	RESTORE_REGS;
	addik	r1, r1, STATE_SAVE_SIZE		/* Clean up stack space.  */

	tovirt(r1,r1);
6:
EXC_return:		/* Make global symbol for debugging */
	rtbd	r14, 0;	/* Instructions to return from an IRQ */
	nop;

/*
 * HW EXCEPTION rutine end
 */

/*
 * Hardware maskable interrupts.
 *
 * The stack-pointer (r1) should have already been saved to the memory
 * location PER_CPU(ENTRY_SP).
 */
C_ENTRY(_interrupt):
/* MS: we are in physical address */
/* Save registers, switch to proper stack, convert SP to virtual.*/
	swi	r1, r0, TOPHYS(PER_CPU(ENTRY_SP))
	swi	r11, r0, TOPHYS(PER_CPU(R11_SAVE));
	/* MS: See if already in kernel mode. */
	lwi	r11, r0, TOPHYS(PER_CPU(KM));
	beqi	r11, 1f; /* MS: Jump ahead if coming from user */

/* Kernel-mode state save. */
	or	r11, r1, r0
	tophys(r1,r11); /* MS: I have in r1 physical address where stack is */
/* MS: Save original SP - position PT_R1 to next stack frame 4 *1 - 152*/
	swi	r11, r1, (PT_R1 - PT_SIZE);
/* MS: restore r11 because of saving in SAVE_REGS */
	lwi	r11, r0, TOPHYS(PER_CPU(R11_SAVE));
	/* save registers */
/* MS: Make room on the stack -> activation record */
	addik	r1, r1, -STATE_SAVE_SIZE;
/* MS: store return registers separately because
 * this macros is use for others exceptions */
	swi	r3, r1, PTO + PT_R3;
	swi	r4, r1, PTO + PT_R4;
	SAVE_REGS
	/* MS: store mode */
	addi	r11, r0, 1; /* MS: Was in kernel-mode. */
	swi	r11, r1, PTO + PT_MODE; /* MS: and save it */
	brid	2f;
	nop; /* MS: Fill delay slot */

1:
/* User-mode state save. */
/* MS: restore r11 -> FIXME move before SAVE_REG */
	lwi	r11, r0, TOPHYS(PER_CPU(R11_SAVE));
 /* MS: get the saved current */
	lwi	r1, r0, TOPHYS(PER_CPU(CURRENT_SAVE));
	tophys(r1,r1);
	lwi	r1, r1, TS_THREAD_INFO;
	addik	r1, r1, THREAD_SIZE;
	tophys(r1,r1);
	/* save registers */
	addik	r1, r1, -STATE_SAVE_SIZE;
	swi	r3, r1, PTO+PT_R3;
	swi	r4, r1, PTO+PT_R4;
	SAVE_REGS
	/* calculate mode */
	swi	r0, r1, PTO + PT_MODE;
	lwi	r11, r0, TOPHYS(PER_CPU(ENTRY_SP));
	swi	r11, r1, PTO+PT_R1;
	/* setup kernel mode to KM */
	addi	r11, r0, 1;
	swi	r11, r0, TOPHYS(PER_CPU(KM));

2:
	lwi	r31, r0, TOPHYS(PER_CPU(CURRENT_SAVE));
	swi	r0, r1, PTO + PT_R0;
	tovirt(r1,r1)
	la	r5, r1, PTO;
	set_vms;
	la	r11, r0, do_IRQ;
	la	r15, r0, irq_call;
irq_call:rtbd	r11, 0;
	nop;

/* MS: we are in virtual mode */
ret_from_irq:
	lwi	r11, r1, PTO + PT_MODE;
	bnei	r11, 2f;

	add	r11, r0, CURRENT_TASK;
	lwi	r11, r11, TS_THREAD_INFO;
	lwi	r11, r11, TI_FLAGS; /* MS: get flags from thread info */
	andi	r11, r11, _TIF_NEED_RESCHED;
	beqi	r11, 5f
	bralid	r15, schedule;
	nop; /* delay slot */

    /* Maybe handle a signal */
5:	add	r11, r0, CURRENT_TASK;
	lwi	r11, r11, TS_THREAD_INFO; /* MS: get thread info */
	lwi	r11, r11, TI_FLAGS; /* get flags in thread info */
	andi	r11, r11, _TIF_SIGPENDING;
	beqid	r11, no_intr_resched
/* Handle a signal return; Pending signals should be in r18. */
	addi	r7, r0, 0; /* Arg 3: int in_syscall */
	la	r5, r1, PTO; /* Arg 1: struct pt_regs *regs */
	bralid	r15, do_signal;	/* Handle any signals */
	add	r6, r0, r0; /* Arg 2: sigset_t *oldset */

/* Finally, return to user state. */
no_intr_resched:
    /* Disable interrupts, we are now committed to the state restore */
	disable_irq
	swi	r0, r0, PER_CPU(KM); /* MS: Now officially in user state. */
	add	r11, r0, CURRENT_TASK;
	swi	r11, r0, PER_CPU(CURRENT_SAVE);
	VM_OFF;
	tophys(r1,r1);
	lwi	r3, r1, PTO + PT_R3; /* MS: restore saved r3, r4 registers */
	lwi	r4, r1, PTO + PT_R4;
	RESTORE_REGS
	addik	r1, r1, STATE_SAVE_SIZE /* MS: Clean up stack space. */
	lwi	r1, r1, PT_R1 - PT_SIZE;
	bri	6f;
/* MS: Return to kernel state. */
2:	VM_OFF /* MS: turn off MMU */
	tophys(r1,r1)
	lwi	r3, r1, PTO + PT_R3; /* MS: restore saved r3, r4 registers */
	lwi	r4, r1, PTO + PT_R4;
	RESTORE_REGS
	addik	r1, r1, STATE_SAVE_SIZE	/* MS: Clean up stack space. */
	tovirt(r1,r1);
6:
IRQ_return: /* MS: Make global symbol for debugging */
	rtid	r14, 0
	nop

/*
 * `Debug' trap
 *  We enter dbtrap in "BIP" (breakpoint) mode.
 *  So we exit the breakpoint mode with an 'rtbd' and proceed with the
 *  original dbtrap.
 *  however, wait to save state first
 */
C_ENTRY(_debug_exception):
	/* BIP bit is set on entry, no interrupts can occur */
	swi	r1, r0, TOPHYS(PER_CPU(ENTRY_SP))

	swi	r11, r0, TOPHYS(r0_ram + PTO + PT_R11); /* Save r11 */
	set_bip;	/*equalize initial state for all possible entries*/
	clear_eip;
	enable_irq;
	lwi	r11, r0, TOPHYS(PER_CPU(KM));/* See if already in kernel mode.*/
	beqi	r11, 1f;		/* Jump ahead if coming from user */
	/* Kernel-mode state save.  */
	lwi	r11, r0, TOPHYS(PER_CPU(ENTRY_SP)); /* Reload kernel stack-ptr*/
	tophys(r1,r11);
	swi	r11, r1, (PT_R1-PT_SIZE); /* Save original SP. */
	lwi	r11, r0, TOPHYS(r0_ram + PTO + PT_R11); /* restore r11 */

	addik	r1, r1, -STATE_SAVE_SIZE; /* Make room on the stack.  */
	swi	r3, r1, PTO + PT_R3;
	swi	r4, r1, PTO + PT_R4;
	SAVE_REGS;

	addi	r11, r0, 1; 		/* Was in kernel-mode.  */
	swi	r11, r1, PTO + PT_MODE;
	brid	2f;
	nop;				/* Fill delay slot */
1:      /* User-mode state save.  */
	lwi	r11, r0, TOPHYS(r0_ram + PTO + PT_R11); /* restore r11 */
	lwi	r1, r0, TOPHYS(PER_CPU(CURRENT_SAVE)); /* get saved current */
	tophys(r1,r1);
	lwi	r1, r1, TS_THREAD_INFO;	/* get the thread info */
	addik	r1, r1, THREAD_SIZE;	/* calculate kernel stack pointer */
	tophys(r1,r1);

	addik	r1, r1, -STATE_SAVE_SIZE; /* Make room on the stack.  */
	swi	r3, r1, PTO + PT_R3;
	swi	r4, r1, PTO + PT_R4;
	SAVE_REGS;

	swi	r0, r1, PTO+PT_MODE; /* Was in user-mode.  */
	lwi	r11, r0, TOPHYS(PER_CPU(ENTRY_SP));
	swi	r11, r1, PTO+PT_R1; /* Store user SP.  */
	addi	r11, r0, 1;
	swi	r11, r0, TOPHYS(PER_CPU(KM));	/* Now we're in kernel-mode.  */
2:	lwi	r31, r0, TOPHYS(PER_CPU(CURRENT_SAVE)); /* get saved current */
	/* Save away the syscall number.  */
	swi	r0, r1, PTO+PT_R0;
	tovirt(r1,r1)

	addi	r5, r0, SIGTRAP		     /* send the trap signal */
	add	r6, r0, CURRENT_TASK; /* Get current task ptr into r11 */
	addk	r7, r0, r0		     /* 3rd param zero */

	set_vms;
	la	r11, r0, send_sig;
	la	r15, r0, dbtrap_call;
dbtrap_call:	rtbd	r11, 0;
	nop;

	set_bip;			/*  Ints masked for state restore*/
	lwi	r11, r1, PTO+PT_MODE;
	bnei	r11, 2f;

	/* Get current task ptr into r11 */
	add	r11, r0, CURRENT_TASK; /* Get current task ptr into r11 */
	lwi	r11, r11, TS_THREAD_INFO;	/* get thread info */
	lwi	r11, r11, TI_FLAGS;	/* get flags in thread info */
	andi	r11, r11, _TIF_NEED_RESCHED;
	beqi	r11, 5f;

/* Call the scheduler before returning from a syscall/trap. */

	bralid	r15, schedule;	/* Call scheduler */
	nop;				/* delay slot */
	/* XXX Is PT_DTRACE handling needed here? */
	/* XXX m68knommu also checks TASK_STATE & TASK_COUNTER here.  */

	/* Maybe handle a signal */
5:	add	r11, r0, CURRENT_TASK; /* Get current task ptr into r11 */
	lwi	r11, r11, TS_THREAD_INFO;	/* get thread info */
	lwi	r11, r11, TI_FLAGS;	/* get flags in thread info */
	andi	r11, r11, _TIF_SIGPENDING;
	beqi	r11, 1f;		/* Signals to handle, handle them */

/* Handle a signal return; Pending signals should be in r18.  */
	/* Not all registers are saved by the normal trap/interrupt entry
	   points (for instance, call-saved registers (because the normal
	   C-compiler calling sequence in the kernel makes sure they're
	   preserved), and call-clobbered registers in the case of
	   traps), but signal handlers may want to examine or change the
	   complete register state.  Here we save anything not saved by
	   the normal entry sequence, so that it may be safely restored
	   (in a possibly modified form) after do_signal returns.  */

	la	r5, r1, PTO;		/* Arg 1: struct pt_regs *regs */
	add	r6, r0, r0;		/* Arg 2: sigset_t *oldset */
	addi  r7, r0, 0;	/* Arg 3: int in_syscall */
	bralid	r15, do_signal;	/* Handle any signals */
	nop;


/* Finally, return to user state.  */
1:	swi	r0, r0, PER_CPU(KM);	/* Now officially in user state. */
	add	r11, r0, CURRENT_TASK; /* Get current task ptr into r11 */
	swi	r11, r0, PER_CPU(CURRENT_SAVE); /* save current */
	VM_OFF;
	tophys(r1,r1);

	lwi	r3, r1, PTO+PT_R3; /* restore saved r3, r4 registers */
	lwi	r4, r1, PTO+PT_R4;
	RESTORE_REGS
	addik	r1, r1, STATE_SAVE_SIZE		/* Clean up stack space.  */


	lwi	r1, r1, PT_R1 - PT_SIZE;
					/* Restore user stack pointer. */
	bri	6f;

/* Return to kernel state.  */
2:	VM_OFF;
	tophys(r1,r1);
	lwi	r3, r1, PTO+PT_R3; /* restore saved r3, r4 registers */
	lwi	r4, r1, PTO+PT_R4;
	RESTORE_REGS
	addik	r1, r1, STATE_SAVE_SIZE		/* Clean up stack space.  */

	tovirt(r1,r1);
6:
DBTRAP_return:		/* Make global symbol for debugging */
	rtbd	r14, 0;	/* Instructions to return from an IRQ */
	nop;



ENTRY(_switch_to)
	/* prepare return value */
	addk	r3, r0, r31

	/* save registers in cpu_context */
	/* use r11 and r12, volatile registers, as temp register */
	/* give start of cpu_context for previous process */
	addik	r11, r5, TI_CPU_CONTEXT
	swi	r1, r11, CC_R1
	swi	r2, r11, CC_R2
	/* skip volatile registers.
	 * they are saved on stack when we jumped to _switch_to() */
	/* dedicated registers */
	swi	r13, r11, CC_R13
	swi	r14, r11, CC_R14
	swi	r15, r11, CC_R15
	swi	r16, r11, CC_R16
	swi	r17, r11, CC_R17
	swi	r18, r11, CC_R18
	/* save non-volatile registers */
	swi	r19, r11, CC_R19
	swi	r20, r11, CC_R20
	swi	r21, r11, CC_R21
	swi	r22, r11, CC_R22
	swi	r23, r11, CC_R23
	swi	r24, r11, CC_R24
	swi	r25, r11, CC_R25
	swi	r26, r11, CC_R26
	swi	r27, r11, CC_R27
	swi	r28, r11, CC_R28
	swi	r29, r11, CC_R29
	swi	r30, r11, CC_R30
	/* special purpose registers */
	mfs	r12, rmsr
	nop
	swi	r12, r11, CC_MSR
	mfs	r12, rear
	nop
	swi	r12, r11, CC_EAR
	mfs	r12, resr
	nop
	swi	r12, r11, CC_ESR
	mfs	r12, rfsr
	nop
	swi	r12, r11, CC_FSR

	/* update r31, the current */
	lwi	r31, r6, TI_TASK/* give me pointer to task which will be next */
	/* stored it to current_save too */
	swi	r31, r0, PER_CPU(CURRENT_SAVE)

	/* get new process' cpu context and restore */
	/* give me start where start context of next task */
	addik	r11, r6, TI_CPU_CONTEXT

	/* non-volatile registers */
	lwi	r30, r11, CC_R30
	lwi	r29, r11, CC_R29
	lwi	r28, r11, CC_R28
	lwi	r27, r11, CC_R27
	lwi	r26, r11, CC_R26
	lwi	r25, r11, CC_R25
	lwi	r24, r11, CC_R24
	lwi	r23, r11, CC_R23
	lwi	r22, r11, CC_R22
	lwi	r21, r11, CC_R21
	lwi	r20, r11, CC_R20
	lwi	r19, r11, CC_R19
	/* dedicated registers */
	lwi	r18, r11, CC_R18
	lwi	r17, r11, CC_R17
	lwi	r16, r11, CC_R16
	lwi	r15, r11, CC_R15
	lwi	r14, r11, CC_R14
	lwi	r13, r11, CC_R13
	/* skip volatile registers */
	lwi	r2, r11, CC_R2
	lwi	r1, r11, CC_R1

	/* special purpose registers */
	lwi	r12, r11, CC_FSR
	mts	rfsr, r12
	nop
	lwi	r12, r11, CC_MSR
	mts	rmsr, r12
	nop

	rtsd	r15, 8
	nop

ENTRY(_reset)
	brai	0x70; /* Jump back to FS-boot */

ENTRY(_break)
	mfs	r5, rmsr
	nop
	swi	r5, r0, 0x250 + TOPHYS(r0_ram)
	mfs	r5, resr
	nop
	swi	r5, r0, 0x254 + TOPHYS(r0_ram)
	bri	0

	/* These are compiled and loaded into high memory, then
	 * copied into place in mach_early_setup */
	.section	.init.ivt, "ax"
	.org	0x0
	/* this is very important - here is the reset vector */
	/* in current MMU branch you don't care what is here - it is
	 * used from bootloader site - but this is correct for FS-BOOT */
	brai	0x70
	nop
	brai	TOPHYS(_user_exception); /* syscall handler */
	brai	TOPHYS(_interrupt);	/* Interrupt handler */
	brai	TOPHYS(_break);		/* nmi trap handler */
	brai	TOPHYS(_hw_exception_handler);	/* HW exception handler */

	.org	0x60
	brai	TOPHYS(_debug_exception);	/* debug trap handler*/

.section .rodata,"a"
#include "syscall_table.S"

syscall_table_size=(.-sys_call_table)

