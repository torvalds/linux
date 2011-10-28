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

#ifdef DEBUG
/* Create space for syscalls counting. */
.section .data
.global syscall_debug_table
.align 4
syscall_debug_table:
	.space	(__NR_syscalls * 4)
#endif /* DEBUG */

#define C_ENTRY(name)	.globl name; .align 4; name

/*
 * Various ways of setting and clearing BIP in flags reg.
 * This is mucky, but necessary using microblaze version that
 * allows msr ops to write to BIP
 */
#if CONFIG_XILINX_MICROBLAZE0_USE_MSR_INSTR
	.macro	clear_bip
	msrclr	r0, MSR_BIP
	.endm

	.macro	set_bip
	msrset	r0, MSR_BIP
	.endm

	.macro	clear_eip
	msrclr	r0, MSR_EIP
	.endm

	.macro	set_ee
	msrset	r0, MSR_EE
	.endm

	.macro	disable_irq
	msrclr	r0, MSR_IE
	.endm

	.macro	enable_irq
	msrset	r0, MSR_IE
	.endm

	.macro	set_ums
	msrset	r0, MSR_UMS
	msrclr	r0, MSR_VMS
	.endm

	.macro	set_vms
	msrclr	r0, MSR_UMS
	msrset	r0, MSR_VMS
	.endm

	.macro	clear_ums
	msrclr	r0, MSR_UMS
	.endm

	.macro	clear_vms_ums
	msrclr	r0, MSR_VMS | MSR_UMS
	.endm
#else
	.macro	clear_bip
	mfs	r11, rmsr
	andi	r11, r11, ~MSR_BIP
	mts	rmsr, r11
	.endm

	.macro	set_bip
	mfs	r11, rmsr
	ori	r11, r11, MSR_BIP
	mts	rmsr, r11
	.endm

	.macro	clear_eip
	mfs	r11, rmsr
	andi	r11, r11, ~MSR_EIP
	mts	rmsr, r11
	.endm

	.macro	set_ee
	mfs	r11, rmsr
	ori	r11, r11, MSR_EE
	mts	rmsr, r11
	.endm

	.macro	disable_irq
	mfs	r11, rmsr
	andi	r11, r11, ~MSR_IE
	mts	rmsr, r11
	.endm

	.macro	enable_irq
	mfs	r11, rmsr
	ori	r11, r11, MSR_IE
	mts	rmsr, r11
	.endm

	.macro set_ums
	mfs	r11, rmsr
	ori	r11, r11, MSR_VMS
	andni	r11, r11, MSR_UMS
	mts	rmsr, r11
	.endm

	.macro	set_vms
	mfs	r11, rmsr
	ori	r11, r11, MSR_VMS
	andni	r11, r11, MSR_UMS
	mts	rmsr, r11
	.endm

	.macro	clear_ums
	mfs	r11, rmsr
	andni	r11, r11, MSR_UMS
	mts	rmsr,r11
	.endm

	.macro	clear_vms_ums
	mfs	r11, rmsr
	andni	r11, r11, (MSR_VMS|MSR_UMS)
	mts	rmsr,r11
	.endm
#endif

/* Define how to call high-level functions. With MMU, virtual mode must be
 * enabled when calling the high-level function. Clobbers R11.
 * VM_ON, VM_OFF, DO_JUMP_BIPCLR, DO_CALL
 */

/* turn on virtual protected mode save */
#define VM_ON		\
	set_ums;	\
	rted	r0, 2f;	\
	nop; \
2:

/* turn off virtual protected mode save and user mode save*/
#define VM_OFF			\
	clear_vms_ums;		\
	rted	r0, TOPHYS(1f);	\
	nop; \
1:

#define SAVE_REGS \
	swi	r2, r1, PT_R2;	/* Save SDA */			\
	swi	r3, r1, PT_R3;					\
	swi	r4, r1, PT_R4;					\
	swi	r5, r1, PT_R5;					\
	swi	r6, r1, PT_R6;					\
	swi	r7, r1, PT_R7;					\
	swi	r8, r1, PT_R8;					\
	swi	r9, r1, PT_R9;					\
	swi	r10, r1, PT_R10;					\
	swi	r11, r1, PT_R11;	/* save clobbered regs after rval */\
	swi	r12, r1, PT_R12;					\
	swi	r13, r1, PT_R13;	/* Save SDA2 */			\
	swi	r14, r1, PT_PC;	/* PC, before IRQ/trap */	\
	swi	r15, r1, PT_R15;	/* Save LP */			\
	swi	r16, r1, PT_R16;					\
	swi	r17, r1, PT_R17;					\
	swi	r18, r1, PT_R18;	/* Save asm scratch reg */	\
	swi	r19, r1, PT_R19;					\
	swi	r20, r1, PT_R20;					\
	swi	r21, r1, PT_R21;					\
	swi	r22, r1, PT_R22;					\
	swi	r23, r1, PT_R23;					\
	swi	r24, r1, PT_R24;					\
	swi	r25, r1, PT_R25;					\
	swi	r26, r1, PT_R26;					\
	swi	r27, r1, PT_R27;					\
	swi	r28, r1, PT_R28;					\
	swi	r29, r1, PT_R29;					\
	swi	r30, r1, PT_R30;					\
	swi	r31, r1, PT_R31;	/* Save current task reg */	\
	mfs	r11, rmsr;		/* save MSR */			\
	swi	r11, r1, PT_MSR;

#define RESTORE_REGS \
	lwi	r11, r1, PT_MSR;					\
	mts	rmsr , r11;						\
	lwi	r2, r1, PT_R2;	/* restore SDA */		\
	lwi	r3, r1, PT_R3;					\
	lwi	r4, r1, PT_R4;					\
	lwi	r5, r1, PT_R5;					\
	lwi	r6, r1, PT_R6;					\
	lwi	r7, r1, PT_R7;					\
	lwi	r8, r1, PT_R8;					\
	lwi	r9, r1, PT_R9;					\
	lwi	r10, r1, PT_R10;					\
	lwi	r11, r1, PT_R11;	/* restore clobbered regs after rval */\
	lwi	r12, r1, PT_R12;					\
	lwi	r13, r1, PT_R13;	/* restore SDA2 */		\
	lwi	r14, r1, PT_PC;	/* RESTORE_LINK PC, before IRQ/trap */\
	lwi	r15, r1, PT_R15;	/* restore LP */		\
	lwi	r16, r1, PT_R16;					\
	lwi	r17, r1, PT_R17;					\
	lwi	r18, r1, PT_R18;	/* restore asm scratch reg */	\
	lwi	r19, r1, PT_R19;					\
	lwi	r20, r1, PT_R20;					\
	lwi	r21, r1, PT_R21;					\
	lwi	r22, r1, PT_R22;					\
	lwi	r23, r1, PT_R23;					\
	lwi	r24, r1, PT_R24;					\
	lwi	r25, r1, PT_R25;					\
	lwi	r26, r1, PT_R26;					\
	lwi	r27, r1, PT_R27;					\
	lwi	r28, r1, PT_R28;					\
	lwi	r29, r1, PT_R29;					\
	lwi	r30, r1, PT_R30;					\
	lwi	r31, r1, PT_R31;	/* Restore cur task reg */

#define SAVE_STATE	\
	swi	r1, r0, TOPHYS(PER_CPU(ENTRY_SP)); /* save stack */	\
	/* See if already in kernel mode.*/				\
	mfs	r1, rmsr;						\
	andi	r1, r1, MSR_UMS;					\
	bnei	r1, 1f;						\
	/* Kernel-mode state save.  */					\
	/* Reload kernel stack-ptr. */					\
	lwi	r1, r0, TOPHYS(PER_CPU(ENTRY_SP));			\
	/* FIXME: I can add these two lines to one */			\
	/* tophys(r1,r1); */						\
	/* addik	r1, r1, -PT_SIZE; */				\
	addik	r1, r1, CONFIG_KERNEL_BASE_ADDR - CONFIG_KERNEL_START - PT_SIZE; \
	SAVE_REGS							\
	brid	2f;							\
	swi	r1, r1, PT_MODE; 	 				\
1:	/* User-mode state save.  */					\
	lwi	r1, r0, TOPHYS(PER_CPU(CURRENT_SAVE)); /* get saved current */\
	tophys(r1,r1);							\
	lwi	r1, r1, TS_THREAD_INFO;	/* get the thread info */	\
	/* MS these three instructions can be added to one */		\
	/* addik	r1, r1, THREAD_SIZE; */				\
	/* tophys(r1,r1); */						\
	/* addik	r1, r1, -PT_SIZE; */			\
	addik r1, r1, THREAD_SIZE + CONFIG_KERNEL_BASE_ADDR - CONFIG_KERNEL_START - PT_SIZE; \
	SAVE_REGS							\
	lwi	r11, r0, TOPHYS(PER_CPU(ENTRY_SP));			\
	swi	r11, r1, PT_R1; /* Store user SP.  */		\
	swi	r0, r1, PT_MODE; /* Was in user-mode.  */		\
	/* MS: I am clearing UMS even in case when I come from kernel space */ \
	clear_ums; 							\
2:	lwi	CURRENT_TASK, r0, TOPHYS(PER_CPU(CURRENT_SAVE));

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

	mfs	r1, rmsr
	nop
	andi	r1, r1, MSR_UMS
	bnei	r1, 1f

/* Kernel-mode state save - kernel execve */
	lwi	r1, r0, TOPHYS(PER_CPU(ENTRY_SP)); /* Reload kernel stack-ptr*/
	tophys(r1,r1);

	addik	r1, r1, -PT_SIZE; /* Make room on the stack. */
	SAVE_REGS

	swi	r1, r1, PT_MODE; /* pt_regs -> kernel mode */
	brid	2f;
	nop;				/* Fill delay slot */

/* User-mode state save.  */
1:
	lwi	r1, r0, TOPHYS(PER_CPU(CURRENT_SAVE)); /* get saved current */
	tophys(r1,r1);
	lwi	r1, r1, TS_THREAD_INFO;	/* get stack from task_struct */
/* calculate kernel stack pointer from task struct 8k */
	addik	r1, r1, THREAD_SIZE;
	tophys(r1,r1);

	addik	r1, r1, -PT_SIZE; /* Make room on the stack.  */
	SAVE_REGS
	swi	r0, r1, PT_R3
	swi	r0, r1, PT_R4

	swi	r0, r1, PT_MODE;			/* Was in user-mode. */
	lwi	r11, r0, TOPHYS(PER_CPU(ENTRY_SP));
	swi	r11, r1, PT_R1;		/* Store user SP.  */
	clear_ums;
2:	lwi	CURRENT_TASK, r0, TOPHYS(PER_CPU(CURRENT_SAVE));
	/* Save away the syscall number.  */
	swi	r12, r1, PT_R0;
	tovirt(r1,r1)

/* where the trap should return need -8 to adjust for rtsd r15, 8*/
/* Jump to the appropriate function for the system call number in r12
 * (r12 is not preserved), or return an error if r12 is not valid. The LP
 * register should point to the location where
 * the called function should return.  [note that MAKE_SYS_CALL uses label 1] */

	/* Step into virtual mode */
	rtbd	r0, 3f
	nop
3:
	lwi	r11, CURRENT_TASK, TS_THREAD_INFO /* get thread info */
	lwi	r11, r11, TI_FLAGS	 /* get flags in thread info */
	andi	r11, r11, _TIF_WORK_SYSCALL_MASK
	beqi	r11, 4f

	addik	r3, r0, -ENOSYS
	swi	r3, r1, PT_R3
	brlid	r15, do_syscall_trace_enter
	addik	r5, r1, PT_R0

	# do_syscall_trace_enter returns the new syscall nr.
	addk	r12, r0, r3
	lwi	r5, r1, PT_R5;
	lwi	r6, r1, PT_R6;
	lwi	r7, r1, PT_R7;
	lwi	r8, r1, PT_R8;
	lwi	r9, r1, PT_R9;
	lwi	r10, r1, PT_R10;
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
	/* Trac syscalls and stored them to syscall_debug_table */
	/* The first syscall location stores total syscall number */
	lwi	r3, r0, syscall_debug_table
	addi	r3, r3, 1
	swi	r3, r0, syscall_debug_table
	lwi	r3, r12, syscall_debug_table
	addi	r3, r3, 1
	swi	r3, r12, syscall_debug_table
#endif

	# Find and jump into the syscall handler.
	lwi	r12, r12, sys_call_table
	/* where the trap should return need -8 to adjust for rtsd r15, 8 */
	addi	r15, r0, ret_from_trap-8
	bra	r12

	/* The syscall number is invalid, return an error.  */
5:
	rtsd	r15, 8;		/* looks like a normal subroutine return */
	addi	r3, r0, -ENOSYS;

/* Entry point used to return from a syscall/trap */
/* We re-enable BIP bit before state restore */
C_ENTRY(ret_from_trap):
	swi	r3, r1, PT_R3
	swi	r4, r1, PT_R4

	lwi	r11, r1, PT_MODE;
/* See if returning to kernel mode, if so, skip resched &c.  */
	bnei	r11, 2f;
	/* We're returning to user mode, so check for various conditions that
	 * trigger rescheduling. */
	/* FIXME: Restructure all these flag checks. */
	lwi	r11, CURRENT_TASK, TS_THREAD_INFO;	/* get thread info */
	lwi	r11, r11, TI_FLAGS;		/* get flags in thread info */
	andi	r11, r11, _TIF_WORK_SYSCALL_MASK
	beqi	r11, 1f

	brlid	r15, do_syscall_trace_leave
	addik	r5, r1, PT_R0
1:
	/* We're returning to user mode, so check for various conditions that
	 * trigger rescheduling. */
	/* get thread info from current task */
	lwi	r11, CURRENT_TASK, TS_THREAD_INFO;
	lwi	r11, r11, TI_FLAGS;		/* get flags in thread info */
	andi	r11, r11, _TIF_NEED_RESCHED;
	beqi	r11, 5f;

	bralid	r15, schedule;	/* Call scheduler */
	nop;				/* delay slot */

	/* Maybe handle a signal */
5:	/* get thread info from current task*/
	lwi	r11, CURRENT_TASK, TS_THREAD_INFO;
	lwi	r11, r11, TI_FLAGS;	/* get flags in thread info */
	andi	r11, r11, _TIF_SIGPENDING;
	beqi	r11, 1f;		/* Signals to handle, handle them */

	addik	r5, r1, 0;		/* Arg 1: struct pt_regs *regs */
	addi	r7, r0, 1;		/* Arg 3: int in_syscall */
	bralid	r15, do_signal;	/* Handle any signals */
	add	r6, r0, r0;		/* Arg 2: sigset_t *oldset */

/* Finally, return to user state.  */
1:	set_bip;			/*  Ints masked for state restore */
	swi	CURRENT_TASK, r0, PER_CPU(CURRENT_SAVE); /* save current */
	VM_OFF;
	tophys(r1,r1);
	RESTORE_REGS;
	addik	r1, r1, PT_SIZE		/* Clean up stack space.  */
	lwi	r1, r1, PT_R1 - PT_SIZE;/* Restore user stack pointer. */
	bri	6f;

/* Return to kernel state.  */
2:	set_bip;			/*  Ints masked for state restore */
	VM_OFF;
	tophys(r1,r1);
	RESTORE_REGS;
	addik	r1, r1, PT_SIZE		/* Clean up stack space.  */
	tovirt(r1,r1);
6:
TRAP_return:		/* Make global symbol for debugging */
	rtbd	r14, 0;	/* Instructions to return from an IRQ */
	nop;


/* These syscalls need access to the struct pt_regs on the stack, so we
   implement them in assembly (they're basically all wrappers anyway).  */

C_ENTRY(sys_fork_wrapper):
	addi	r5, r0, SIGCHLD			/* Arg 0: flags */
	lwi	r6, r1, PT_R1	/* Arg 1: child SP (use parent's) */
	addik	r7, r1, 0			/* Arg 2: parent context */
	add	r8. r0, r0			/* Arg 3: (unused) */
	add	r9, r0, r0;			/* Arg 4: (unused) */
	brid	do_fork		/* Do real work (tail-call) */
	add	r10, r0, r0;			/* Arg 5: (unused) */

/* This the initial entry point for a new child thread, with an appropriate
   stack in place that makes it look the the child is in the middle of an
   syscall.  This function is actually `returned to' from switch_thread
   (copy_thread makes ret_from_fork the return address in each new thread's
   saved context).  */
C_ENTRY(ret_from_fork):
	bralid	r15, schedule_tail; /* ...which is schedule_tail's arg */
	add	r3, r5, r0;	/* switch_thread returns the prev task */
				/* ( in the delay slot ) */
	brid	ret_from_trap;	/* Do normal trap return */
	add	r3, r0, r0;	/* Child's fork call should return 0. */

C_ENTRY(sys_vfork):
	brid	microblaze_vfork	/* Do real work (tail-call) */
	addik	r5, r1, 0

C_ENTRY(sys_clone):
	bnei	r6, 1f;			/* See if child SP arg (arg 1) is 0. */
	lwi	r6, r1, PT_R1;	/* If so, use paret's stack ptr */
1:	addik	r7, r1, 0;			/* Arg 2: parent context */
	add	r8, r0, r0;			/* Arg 3: (unused) */
	add	r9, r0, r0;			/* Arg 4: (unused) */
	brid	do_fork		/* Do real work (tail-call) */
	add	r10, r0, r0;			/* Arg 5: (unused) */

C_ENTRY(sys_execve):
	brid	microblaze_execve;	/* Do real work (tail-call).*/
	addik	r8, r1, 0;		/* add user context as 4th arg */

C_ENTRY(sys_rt_sigreturn_wrapper):
	brid	sys_rt_sigreturn	/* Do real work */
	addik	r5, r1, 0;		/* add user context as 1st arg */

/*
 * HW EXCEPTION rutine start
 */
C_ENTRY(full_exception_trap):
	/* adjust exception address for privileged instruction
	 * for finding where is it */
	addik	r17, r17, -4
	SAVE_STATE /* Save registers */
	/* PC, before IRQ/trap - this is one instruction above */
	swi	r17, r1, PT_PC;
	tovirt(r1,r1)
	/* FIXME this can be store directly in PT_ESR reg.
	 * I tested it but there is a fault */
	/* where the trap should return need -8 to adjust for rtsd r15, 8 */
	addik	r15, r0, ret_from_exc - 8
	mfs	r6, resr
	mfs	r7, rfsr;		/* save FSR */
	mts	rfsr, r0;	/* Clear sticky fsr */
	rted	r0, full_exception
	addik	r5, r1, 0		 /* parameter struct pt_regs * regs */

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
	/* MS: I have to save r11 value and then restore it because
	 * set_bit, clear_eip, set_ee use r11 as temp register if MSR
	 * instructions are not used. We don't need to do if MSR instructions
	 * are used and they use r0 instead of r11.
	 * I am using ENTRY_SP which should be primary used only for stack
	 * pointer saving. */
	swi	r11, r0, TOPHYS(PER_CPU(ENTRY_SP));
	set_bip;        /* equalize initial state for all possible entries */
	clear_eip;
	set_ee;
	lwi	r11, r0, TOPHYS(PER_CPU(ENTRY_SP));
	SAVE_STATE		/* Save registers.*/
	/* PC, before IRQ/trap - this is one instruction above */
	swi	r17, r1, PT_PC;
	tovirt(r1,r1)
	/* where the trap should return need -8 to adjust for rtsd r15, 8 */
	addik	r15, r0, ret_from_exc-8
	mfs	r3, resr		/* ESR */
	mfs	r4, rear		/* EAR */
	rtbd	r0, _unaligned_data_exception
	addik	r7, r1, 0		/* parameter struct pt_regs * regs */

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
	SAVE_STATE		/* Save registers.*/
	/* PC, before IRQ/trap - this is one instruction above */
	swi	r17, r1, PT_PC;
	tovirt(r1,r1)
	/* where the trap should return need -8 to adjust for rtsd r15, 8 */
	addik	r15, r0, ret_from_exc-8
	mfs	r6, rear		/* parameter unsigned long address */
	mfs	r7, resr		/* parameter unsigned long error_code */
	rted	r0, do_page_fault
	addik	r5, r1, 0		/* parameter struct pt_regs * regs */

C_ENTRY(page_fault_instr_trap):
	SAVE_STATE		/* Save registers.*/
	/* PC, before IRQ/trap - this is one instruction above */
	swi	r17, r1, PT_PC;
	tovirt(r1,r1)
	/* where the trap should return need -8 to adjust for rtsd r15, 8 */
	addik	r15, r0, ret_from_exc-8
	mfs	r6, rear		/* parameter unsigned long address */
	ori	r7, r0, 0		/* parameter unsigned long error_code */
	rted	r0, do_page_fault
	addik	r5, r1, 0		/* parameter struct pt_regs * regs */

/* Entry point used to return from an exception.  */
C_ENTRY(ret_from_exc):
	lwi	r11, r1, PT_MODE;
	bnei	r11, 2f;		/* See if returning to kernel mode, */
					/* ... if so, skip resched &c.  */

	/* We're returning to user mode, so check for various conditions that
	   trigger rescheduling. */
	lwi	r11, CURRENT_TASK, TS_THREAD_INFO;	/* get thread info */
	lwi	r11, r11, TI_FLAGS;	/* get flags in thread info */
	andi	r11, r11, _TIF_NEED_RESCHED;
	beqi	r11, 5f;

/* Call the scheduler before returning from a syscall/trap. */
	bralid	r15, schedule;	/* Call scheduler */
	nop;				/* delay slot */

	/* Maybe handle a signal */
5:	lwi	r11, CURRENT_TASK, TS_THREAD_INFO;	/* get thread info */
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
	 * (in a possibly modified form) after do_signal returns. */
	addik	r5, r1, 0;		/* Arg 1: struct pt_regs *regs */
	addi	r7, r0, 0;		/* Arg 3: int in_syscall */
	bralid	r15, do_signal;	/* Handle any signals */
	add	r6, r0, r0;		/* Arg 2: sigset_t *oldset */

/* Finally, return to user state.  */
1:	set_bip;			/* Ints masked for state restore */
	swi	CURRENT_TASK, r0, PER_CPU(CURRENT_SAVE); /* save current */
	VM_OFF;
	tophys(r1,r1);

	RESTORE_REGS;
	addik	r1, r1, PT_SIZE		/* Clean up stack space.  */

	lwi	r1, r1, PT_R1 - PT_SIZE; /* Restore user stack pointer. */
	bri	6f;
/* Return to kernel state.  */
2:	set_bip;			/* Ints masked for state restore */
	VM_OFF;
	tophys(r1,r1);
	RESTORE_REGS;
	addik	r1, r1, PT_SIZE		/* Clean up stack space.  */

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
	/* MS: See if already in kernel mode. */
	mfs	r1, rmsr
	nop
	andi	r1, r1, MSR_UMS
	bnei	r1, 1f

/* Kernel-mode state save. */
	lwi	r1, r0, TOPHYS(PER_CPU(ENTRY_SP))
	tophys(r1,r1); /* MS: I have in r1 physical address where stack is */
	/* save registers */
/* MS: Make room on the stack -> activation record */
	addik	r1, r1, -PT_SIZE;
	SAVE_REGS
	brid	2f;
	swi	r1, r1, PT_MODE; /* 0 - user mode, 1 - kernel mode */
1:
/* User-mode state save. */
 /* MS: get the saved current */
	lwi	r1, r0, TOPHYS(PER_CPU(CURRENT_SAVE));
	tophys(r1,r1);
	lwi	r1, r1, TS_THREAD_INFO;
	addik	r1, r1, THREAD_SIZE;
	tophys(r1,r1);
	/* save registers */
	addik	r1, r1, -PT_SIZE;
	SAVE_REGS
	/* calculate mode */
	swi	r0, r1, PT_MODE;
	lwi	r11, r0, TOPHYS(PER_CPU(ENTRY_SP));
	swi	r11, r1, PT_R1;
	clear_ums;
2:
	lwi	CURRENT_TASK, r0, TOPHYS(PER_CPU(CURRENT_SAVE));
	tovirt(r1,r1)
	addik	r15, r0, irq_call;
irq_call:rtbd	r0, do_IRQ;
	addik	r5, r1, 0;

/* MS: we are in virtual mode */
ret_from_irq:
	lwi	r11, r1, PT_MODE;
	bnei	r11, 2f;

	lwi	r11, CURRENT_TASK, TS_THREAD_INFO;
	lwi	r11, r11, TI_FLAGS; /* MS: get flags from thread info */
	andi	r11, r11, _TIF_NEED_RESCHED;
	beqi	r11, 5f
	bralid	r15, schedule;
	nop; /* delay slot */

    /* Maybe handle a signal */
5:	lwi	r11, CURRENT_TASK, TS_THREAD_INFO; /* MS: get thread info */
	lwi	r11, r11, TI_FLAGS; /* get flags in thread info */
	andi	r11, r11, _TIF_SIGPENDING;
	beqid	r11, no_intr_resched
/* Handle a signal return; Pending signals should be in r18. */
	addi	r7, r0, 0; /* Arg 3: int in_syscall */
	addik	r5, r1, 0; /* Arg 1: struct pt_regs *regs */
	bralid	r15, do_signal;	/* Handle any signals */
	add	r6, r0, r0; /* Arg 2: sigset_t *oldset */

/* Finally, return to user state. */
no_intr_resched:
    /* Disable interrupts, we are now committed to the state restore */
	disable_irq
	swi	CURRENT_TASK, r0, PER_CPU(CURRENT_SAVE);
	VM_OFF;
	tophys(r1,r1);
	RESTORE_REGS
	addik	r1, r1, PT_SIZE /* MS: Clean up stack space. */
	lwi	r1, r1, PT_R1 - PT_SIZE;
	bri	6f;
/* MS: Return to kernel state. */
2:
#ifdef CONFIG_PREEMPT
	lwi	r11, CURRENT_TASK, TS_THREAD_INFO;
	/* MS: get preempt_count from thread info */
	lwi	r5, r11, TI_PREEMPT_COUNT;
	bgti	r5, restore;

	lwi	r5, r11, TI_FLAGS;		/* get flags in thread info */
	andi	r5, r5, _TIF_NEED_RESCHED;
	beqi	r5, restore /* if zero jump over */

preempt:
	/* interrupts are off that's why I am calling preempt_chedule_irq */
	bralid	r15, preempt_schedule_irq
	nop
	lwi	r11, CURRENT_TASK, TS_THREAD_INFO;	/* get thread info */
	lwi	r5, r11, TI_FLAGS;		/* get flags in thread info */
	andi	r5, r5, _TIF_NEED_RESCHED;
	bnei	r5, preempt /* if non zero jump to resched */
restore:
#endif
	VM_OFF /* MS: turn off MMU */
	tophys(r1,r1)
	RESTORE_REGS
	addik	r1, r1, PT_SIZE	/* MS: Clean up stack space. */
	tovirt(r1,r1);
6:
IRQ_return: /* MS: Make global symbol for debugging */
	rtid	r14, 0
	nop

/*
 * Debug trap for KGDB. Enter to _debug_exception by brki r16, 0x18
 * and call handling function with saved pt_regs
 */
C_ENTRY(_debug_exception):
	/* BIP bit is set on entry, no interrupts can occur */
	swi	r1, r0, TOPHYS(PER_CPU(ENTRY_SP))

	mfs	r1, rmsr
	nop
	andi	r1, r1, MSR_UMS
	bnei	r1, 1f
/* MS: Kernel-mode state save - kgdb */
	lwi	r1, r0, TOPHYS(PER_CPU(ENTRY_SP)); /* Reload kernel stack-ptr*/

	/* BIP bit is set on entry, no interrupts can occur */
	addik   r1, r1, CONFIG_KERNEL_BASE_ADDR - CONFIG_KERNEL_START - PT_SIZE;
	SAVE_REGS;
	/* save all regs to pt_reg structure */
	swi	r0, r1, PT_R0;	/* R0 must be saved too */
	swi	r14, r1, PT_R14	/* rewrite saved R14 value */
	swi	r16, r1, PT_PC; /* PC and r16 are the same */
	/* save special purpose registers to pt_regs */
	mfs	r11, rear;
	swi	r11, r1, PT_EAR;
	mfs	r11, resr;
	swi	r11, r1, PT_ESR;
	mfs	r11, rfsr;
	swi	r11, r1, PT_FSR;

	/* stack pointer is in physical address at it is decrease
	 * by PT_SIZE but we need to get correct R1 value */
	addik   r11, r1, CONFIG_KERNEL_START - CONFIG_KERNEL_BASE_ADDR + PT_SIZE;
	swi	r11, r1, PT_R1
	/* MS: r31 - current pointer isn't changed */
	tovirt(r1,r1)
#ifdef CONFIG_KGDB
	addi	r5, r1, 0 /* pass pt_reg address as the first arg */
	addik	r15, r0, dbtrap_call; /* return address */
	rtbd	r0, microblaze_kgdb_break
	nop;
#endif
	/* MS: Place handler for brki from kernel space if KGDB is OFF.
	 * It is very unlikely that another brki instruction is called. */
	bri 0

/* MS: User-mode state save - gdb */
1:	lwi	r1, r0, TOPHYS(PER_CPU(CURRENT_SAVE)); /* get saved current */
	tophys(r1,r1);
	lwi	r1, r1, TS_THREAD_INFO;	/* get the thread info */
	addik	r1, r1, THREAD_SIZE;	/* calculate kernel stack pointer */
	tophys(r1,r1);

	addik	r1, r1, -PT_SIZE; /* Make room on the stack.  */
	SAVE_REGS;
	swi	r16, r1, PT_PC;	/* Save LP */
	swi	r0, r1, PT_MODE; /* Was in user-mode.  */
	lwi	r11, r0, TOPHYS(PER_CPU(ENTRY_SP));
	swi	r11, r1, PT_R1; /* Store user SP.  */
	lwi	CURRENT_TASK, r0, TOPHYS(PER_CPU(CURRENT_SAVE));
	tovirt(r1,r1)
	set_vms;
	addik	r5, r1, 0;
	addik	r15, r0, dbtrap_call;
dbtrap_call: /* Return point for kernel/user entry + 8 because of rtsd r15, 8 */
	rtbd	r0, sw_exception
	nop

	/* MS: The first instruction for the second part of the gdb/kgdb */
	set_bip; /* Ints masked for state restore */
	lwi	r11, r1, PT_MODE;
	bnei	r11, 2f;
/* MS: Return to user space - gdb */
	/* Get current task ptr into r11 */
	lwi	r11, CURRENT_TASK, TS_THREAD_INFO;	/* get thread info */
	lwi	r11, r11, TI_FLAGS;	/* get flags in thread info */
	andi	r11, r11, _TIF_NEED_RESCHED;
	beqi	r11, 5f;

	/* Call the scheduler before returning from a syscall/trap. */
	bralid	r15, schedule;	/* Call scheduler */
	nop;				/* delay slot */

	/* Maybe handle a signal */
5:	lwi	r11, CURRENT_TASK, TS_THREAD_INFO;	/* get thread info */
	lwi	r11, r11, TI_FLAGS;	/* get flags in thread info */
	andi	r11, r11, _TIF_SIGPENDING;
	beqi	r11, 1f;		/* Signals to handle, handle them */

	addik	r5, r1, 0;		/* Arg 1: struct pt_regs *regs */
	addi  r7, r0, 0;	/* Arg 3: int in_syscall */
	bralid	r15, do_signal;	/* Handle any signals */
	add	r6, r0, r0;		/* Arg 2: sigset_t *oldset */

/* Finally, return to user state.  */
1:	swi	CURRENT_TASK, r0, PER_CPU(CURRENT_SAVE); /* save current */
	VM_OFF;
	tophys(r1,r1);
	/* MS: Restore all regs */
	RESTORE_REGS
	addik	r1, r1, PT_SIZE	 /* Clean up stack space */
	lwi	r1, r1, PT_R1 - PT_SIZE; /* Restore user stack pointer */
DBTRAP_return_user: /* MS: Make global symbol for debugging */
	rtbd	r16, 0; /* MS: Instructions to return from a debug trap */
	nop;

/* MS: Return to kernel state - kgdb */
2:	VM_OFF;
	tophys(r1,r1);
	/* MS: Restore all regs */
	RESTORE_REGS
	lwi	r14, r1, PT_R14;
	lwi	r16, r1, PT_PC;
	addik	r1, r1, PT_SIZE; /* MS: Clean up stack space */
	tovirt(r1,r1);
DBTRAP_return_kernel: /* MS: Make global symbol for debugging */
	rtbd	r16, 0; /* MS: Instructions to return from a debug trap */
	nop;


ENTRY(_switch_to)
	/* prepare return value */
	addk	r3, r0, CURRENT_TASK

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
	swi	r12, r11, CC_MSR
	mfs	r12, rear
	swi	r12, r11, CC_EAR
	mfs	r12, resr
	swi	r12, r11, CC_ESR
	mfs	r12, rfsr
	swi	r12, r11, CC_FSR

	/* update r31, the current-give me pointer to task which will be next */
	lwi	CURRENT_TASK, r6, TI_TASK
	/* stored it to current_save too */
	swi	CURRENT_TASK, r0, PER_CPU(CURRENT_SAVE)

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
	lwi	r12, r11, CC_MSR
	mts	rmsr, r12

	rtsd	r15, 8
	nop

ENTRY(_reset)
	brai	0; /* Jump to reset vector */

	/* These are compiled and loaded into high memory, then
	 * copied into place in mach_early_setup */
	.section	.init.ivt, "ax"
#if CONFIG_MANUAL_RESET_VECTOR
	.org	0x0
	brai	CONFIG_MANUAL_RESET_VECTOR
#endif
	.org	0x8
	brai	TOPHYS(_user_exception); /* syscall handler */
	.org	0x10
	brai	TOPHYS(_interrupt);	/* Interrupt handler */
	.org	0x18
	brai	TOPHYS(_debug_exception);	/* debug trap handler */
	.org	0x20
	brai	TOPHYS(_hw_exception_handler);	/* HW exception handler */

.section .rodata,"a"
#include "syscall_table.S"

syscall_table_size=(.-sys_call_table)

type_SYSCALL:
	.ascii "SYSCALL\0"
type_IRQ:
	.ascii "IRQ\0"
type_IRQ_PREEMPT:
	.ascii "IRQ (PREEMPTED)\0"
type_SYSCALL_PREEMPT:
	.ascii " SYSCALL (PREEMPTED)\0"

	/*
	 * Trap decoding for stack unwinder
	 * Tuples are (start addr, end addr, string)
	 * If return address lies on [start addr, end addr],
	 * unwinder displays 'string'
	 */

	.align 4
.global microblaze_trap_handlers
microblaze_trap_handlers:
	/* Exact matches come first */
	.word ret_from_trap; .word ret_from_trap   ; .word type_SYSCALL
	.word ret_from_irq ; .word ret_from_irq    ; .word type_IRQ
	/* Fuzzy matches go here */
	.word ret_from_irq ; .word no_intr_resched ; .word type_IRQ_PREEMPT
	.word ret_from_trap; .word TRAP_return     ; .word type_SYSCALL_PREEMPT
	/* End of table */
	.word 0               ; .word 0               ; .word 0
