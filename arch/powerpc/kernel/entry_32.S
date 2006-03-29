/*
 *  PowerPC version
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *  Rewritten by Cort Dougan (cort@fsmlabs.com) for PReP
 *    Copyright (C) 1996 Cort Dougan <cort@fsmlabs.com>
 *  Adapted for Power Macintosh by Paul Mackerras.
 *  Low-level exception handlers and MMU support
 *  rewritten by Paul Mackerras.
 *    Copyright (C) 1996 Paul Mackerras.
 *  MPC8xx modifications Copyright (C) 1997 Dan Malek (dmalek@jlc.net).
 *
 *  This file contains the system call entry code, context switch
 *  code, and exception/interrupt return code for PowerPC.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 */

#include <linux/config.h>
#include <linux/errno.h>
#include <linux/sys.h>
#include <linux/threads.h>
#include <asm/reg.h>
#include <asm/page.h>
#include <asm/mmu.h>
#include <asm/cputable.h>
#include <asm/thread_info.h>
#include <asm/ppc_asm.h>
#include <asm/asm-offsets.h>
#include <asm/unistd.h>

#undef SHOW_SYSCALLS
#undef SHOW_SYSCALLS_TASK

/*
 * MSR_KERNEL is > 0x10000 on 4xx/Book-E since it include MSR_CE.
 */
#if MSR_KERNEL >= 0x10000
#define LOAD_MSR_KERNEL(r, x)	lis r,(x)@h; ori r,r,(x)@l
#else
#define LOAD_MSR_KERNEL(r, x)	li r,(x)
#endif

#ifdef CONFIG_BOOKE
#include "head_booke.h"
#define TRANSFER_TO_HANDLER_EXC_LEVEL(exc_level)	\
	mtspr	exc_level##_SPRG,r8;			\
	BOOKE_LOAD_EXC_LEVEL_STACK(exc_level);		\
	lwz	r0,GPR10-INT_FRAME_SIZE(r8);		\
	stw	r0,GPR10(r11);				\
	lwz	r0,GPR11-INT_FRAME_SIZE(r8);		\
	stw	r0,GPR11(r11);				\
	mfspr	r8,exc_level##_SPRG

	.globl	mcheck_transfer_to_handler
mcheck_transfer_to_handler:
	TRANSFER_TO_HANDLER_EXC_LEVEL(MCHECK)
	b	transfer_to_handler_full

	.globl	debug_transfer_to_handler
debug_transfer_to_handler:
	TRANSFER_TO_HANDLER_EXC_LEVEL(DEBUG)
	b	transfer_to_handler_full

	.globl	crit_transfer_to_handler
crit_transfer_to_handler:
	TRANSFER_TO_HANDLER_EXC_LEVEL(CRIT)
	/* fall through */
#endif

#ifdef CONFIG_40x
	.globl	crit_transfer_to_handler
crit_transfer_to_handler:
	lwz	r0,crit_r10@l(0)
	stw	r0,GPR10(r11)
	lwz	r0,crit_r11@l(0)
	stw	r0,GPR11(r11)
	/* fall through */
#endif

/*
 * This code finishes saving the registers to the exception frame
 * and jumps to the appropriate handler for the exception, turning
 * on address translation.
 * Note that we rely on the caller having set cr0.eq iff the exception
 * occurred in kernel mode (i.e. MSR:PR = 0).
 */
	.globl	transfer_to_handler_full
transfer_to_handler_full:
	SAVE_NVGPRS(r11)
	/* fall through */

	.globl	transfer_to_handler
transfer_to_handler:
	stw	r2,GPR2(r11)
	stw	r12,_NIP(r11)
	stw	r9,_MSR(r11)
	andi.	r2,r9,MSR_PR
	mfctr	r12
	mfspr	r2,SPRN_XER
	stw	r12,_CTR(r11)
	stw	r2,_XER(r11)
	mfspr	r12,SPRN_SPRG3
	addi	r2,r12,-THREAD
	tovirt(r2,r2)			/* set r2 to current */
	beq	2f			/* if from user, fix up THREAD.regs */
	addi	r11,r1,STACK_FRAME_OVERHEAD
	stw	r11,PT_REGS(r12)
#if defined(CONFIG_40x) || defined(CONFIG_BOOKE)
	/* Check to see if the dbcr0 register is set up to debug.  Use the
	   single-step bit to do this. */
	lwz	r12,THREAD_DBCR0(r12)
	andis.	r12,r12,DBCR0_IC@h
	beq+	3f
	/* From user and task is ptraced - load up global dbcr0 */
	li	r12,-1			/* clear all pending debug events */
	mtspr	SPRN_DBSR,r12
	lis	r11,global_dbcr0@ha
	tophys(r11,r11)
	addi	r11,r11,global_dbcr0@l
	lwz	r12,0(r11)
	mtspr	SPRN_DBCR0,r12
	lwz	r12,4(r11)
	addi	r12,r12,-1
	stw	r12,4(r11)
#endif
	b	3f
2:	/* if from kernel, check interrupted DOZE/NAP mode and
         * check for stack overflow
         */
#ifdef CONFIG_6xx
	mfspr	r11,SPRN_HID0
	mtcr	r11
BEGIN_FTR_SECTION
	bt-	8,4f			/* Check DOZE */
END_FTR_SECTION_IFSET(CPU_FTR_CAN_DOZE)
BEGIN_FTR_SECTION
	bt-	9,4f			/* Check NAP */
END_FTR_SECTION_IFSET(CPU_FTR_CAN_NAP)
#endif /* CONFIG_6xx */
	.globl transfer_to_handler_cont
transfer_to_handler_cont:
	lwz	r11,THREAD_INFO-THREAD(r12)
	cmplw	r1,r11			/* if r1 <= current->thread_info */
	ble-	stack_ovf		/* then the kernel stack overflowed */
3:
	mflr	r9
	lwz	r11,0(r9)		/* virtual address of handler */
	lwz	r9,4(r9)		/* where to go when done */
	FIX_SRR1(r10,r12)
	mtspr	SPRN_SRR0,r11
	mtspr	SPRN_SRR1,r10
	mtlr	r9
	SYNC
	RFI				/* jump to handler, enable MMU */

#ifdef CONFIG_6xx	
4:	b	power_save_6xx_restore
#endif

/*
 * On kernel stack overflow, load up an initial stack pointer
 * and call StackOverflow(regs), which should not return.
 */
stack_ovf:
	/* sometimes we use a statically-allocated stack, which is OK. */
	lis	r11,_end@h
	ori	r11,r11,_end@l
	cmplw	r1,r11
	ble	3b			/* r1 <= &_end is OK */
	SAVE_NVGPRS(r11)
	addi	r3,r1,STACK_FRAME_OVERHEAD
	lis	r1,init_thread_union@ha
	addi	r1,r1,init_thread_union@l
	addi	r1,r1,THREAD_SIZE-STACK_FRAME_OVERHEAD
	lis	r9,StackOverflow@ha
	addi	r9,r9,StackOverflow@l
	LOAD_MSR_KERNEL(r10,MSR_KERNEL)
	FIX_SRR1(r10,r12)
	mtspr	SPRN_SRR0,r9
	mtspr	SPRN_SRR1,r10
	SYNC
	RFI

/*
 * Handle a system call.
 */
	.stabs	"arch/powerpc/kernel/",N_SO,0,0,0f
	.stabs	"entry_32.S",N_SO,0,0,0f
0:

_GLOBAL(DoSyscall)
	stw	r0,THREAD+LAST_SYSCALL(r2)
	stw	r3,ORIG_GPR3(r1)
	li	r12,0
	stw	r12,RESULT(r1)
	lwz	r11,_CCR(r1)	/* Clear SO bit in CR */
	rlwinm	r11,r11,0,4,2
	stw	r11,_CCR(r1)
#ifdef SHOW_SYSCALLS
	bl	do_show_syscall
#endif /* SHOW_SYSCALLS */
	rlwinm	r10,r1,0,0,(31-THREAD_SHIFT)	/* current_thread_info() */
	lwz	r11,TI_FLAGS(r10)
	andi.	r11,r11,_TIF_SYSCALL_T_OR_A
	bne-	syscall_dotrace
syscall_dotrace_cont:
	cmplwi	0,r0,NR_syscalls
	lis	r10,sys_call_table@h
	ori	r10,r10,sys_call_table@l
	slwi	r0,r0,2
	bge-	66f
	lwzx	r10,r10,r0	/* Fetch system call handler [ptr] */
	mtlr	r10
	addi	r9,r1,STACK_FRAME_OVERHEAD
	PPC440EP_ERR42
	blrl			/* Call handler */
	.globl	ret_from_syscall
ret_from_syscall:
#ifdef SHOW_SYSCALLS
	bl	do_show_syscall_exit
#endif
	mr	r6,r3
	rlwinm	r12,r1,0,0,(31-THREAD_SHIFT)	/* current_thread_info() */
	/* disable interrupts so current_thread_info()->flags can't change */
	LOAD_MSR_KERNEL(r10,MSR_KERNEL)	/* doesn't include MSR_EE */
	SYNC
	MTMSRD(r10)
	lwz	r9,TI_FLAGS(r12)
	li	r8,-_LAST_ERRNO
	andi.	r0,r9,(_TIF_SYSCALL_T_OR_A|_TIF_SINGLESTEP|_TIF_USER_WORK_MASK|_TIF_PERSYSCALL_MASK)
	bne-	syscall_exit_work
	cmplw	0,r3,r8
	blt+	syscall_exit_cont
	lwz	r11,_CCR(r1)			/* Load CR */
	neg	r3,r3
	oris	r11,r11,0x1000	/* Set SO bit in CR */
	stw	r11,_CCR(r1)
syscall_exit_cont:
#if defined(CONFIG_4xx) || defined(CONFIG_BOOKE)
	/* If the process has its own DBCR0 value, load it up.  The single
	   step bit tells us that dbcr0 should be loaded. */
	lwz	r0,THREAD+THREAD_DBCR0(r2)
	andis.	r10,r0,DBCR0_IC@h
	bnel-	load_dbcr0
#endif
	stwcx.	r0,0,r1			/* to clear the reservation */
	lwz	r4,_LINK(r1)
	lwz	r5,_CCR(r1)
	mtlr	r4
	mtcr	r5
	lwz	r7,_NIP(r1)
	lwz	r8,_MSR(r1)
	FIX_SRR1(r8, r0)
	lwz	r2,GPR2(r1)
	lwz	r1,GPR1(r1)
	mtspr	SPRN_SRR0,r7
	mtspr	SPRN_SRR1,r8
	SYNC
	RFI

66:	li	r3,-ENOSYS
	b	ret_from_syscall

	.globl	ret_from_fork
ret_from_fork:
	REST_NVGPRS(r1)
	bl	schedule_tail
	li	r3,0
	b	ret_from_syscall

/* Traced system call support */
syscall_dotrace:
	SAVE_NVGPRS(r1)
	li	r0,0xc00
	stw	r0,_TRAP(r1)
	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	do_syscall_trace_enter
	lwz	r0,GPR0(r1)	/* Restore original registers */
	lwz	r3,GPR3(r1)
	lwz	r4,GPR4(r1)
	lwz	r5,GPR5(r1)
	lwz	r6,GPR6(r1)
	lwz	r7,GPR7(r1)
	lwz	r8,GPR8(r1)
	REST_NVGPRS(r1)
	b	syscall_dotrace_cont

syscall_exit_work:
	andi.	r0,r9,_TIF_RESTOREALL
	beq+	0f
	REST_NVGPRS(r1)
	b	2f
0:	cmplw	0,r3,r8
	blt+	1f
	andi.	r0,r9,_TIF_NOERROR
	bne-	1f
	lwz	r11,_CCR(r1)			/* Load CR */
	neg	r3,r3
	oris	r11,r11,0x1000	/* Set SO bit in CR */
	stw	r11,_CCR(r1)

1:	stw	r6,RESULT(r1)	/* Save result */
	stw	r3,GPR3(r1)	/* Update return value */
2:	andi.	r0,r9,(_TIF_PERSYSCALL_MASK)
	beq	4f

	/* Clear per-syscall TIF flags if any are set.  */

	li	r11,_TIF_PERSYSCALL_MASK
	addi	r12,r12,TI_FLAGS
3:	lwarx	r8,0,r12
	andc	r8,r8,r11
#ifdef CONFIG_IBM405_ERR77
	dcbt	0,r12
#endif
	stwcx.	r8,0,r12
	bne-	3b
	subi	r12,r12,TI_FLAGS
	
4:	/* Anything which requires enabling interrupts? */
	andi.	r0,r9,(_TIF_SYSCALL_T_OR_A|_TIF_SINGLESTEP)
	beq	ret_from_except

	/* Re-enable interrupts */
	ori	r10,r10,MSR_EE
	SYNC
	MTMSRD(r10)

	/* Save NVGPRS if they're not saved already */
	lwz	r4,_TRAP(r1)
	andi.	r4,r4,1
	beq	5f
	SAVE_NVGPRS(r1)
	li	r4,0xc00
	stw	r4,_TRAP(r1)
5:
	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	do_syscall_trace_leave
	b	ret_from_except_full

#ifdef SHOW_SYSCALLS
do_show_syscall:
#ifdef SHOW_SYSCALLS_TASK
	lis	r11,show_syscalls_task@ha
	lwz	r11,show_syscalls_task@l(r11)
	cmp	0,r2,r11
	bnelr
#endif
	stw	r31,GPR31(r1)
	mflr	r31
	lis	r3,7f@ha
	addi	r3,r3,7f@l
	lwz	r4,GPR0(r1)
	lwz	r5,GPR3(r1)
	lwz	r6,GPR4(r1)
	lwz	r7,GPR5(r1)
	lwz	r8,GPR6(r1)
	lwz	r9,GPR7(r1)
	bl	printk
	lis	r3,77f@ha
	addi	r3,r3,77f@l
	lwz	r4,GPR8(r1)
	mr	r5,r2
	bl	printk
	lwz	r0,GPR0(r1)
	lwz	r3,GPR3(r1)
	lwz	r4,GPR4(r1)
	lwz	r5,GPR5(r1)
	lwz	r6,GPR6(r1)
	lwz	r7,GPR7(r1)
	lwz	r8,GPR8(r1)
	mtlr	r31
	lwz	r31,GPR31(r1)
	blr

do_show_syscall_exit:
#ifdef SHOW_SYSCALLS_TASK
	lis	r11,show_syscalls_task@ha
	lwz	r11,show_syscalls_task@l(r11)
	cmp	0,r2,r11
	bnelr
#endif
	stw	r31,GPR31(r1)
	mflr	r31
	stw	r3,RESULT(r1)	/* Save result */
	mr	r4,r3
	lis	r3,79f@ha
	addi	r3,r3,79f@l
	bl	printk
	lwz	r3,RESULT(r1)
	mtlr	r31
	lwz	r31,GPR31(r1)
	blr

7:	.string	"syscall %d(%x, %x, %x, %x, %x, "
77:	.string	"%x), current=%p\n"
79:	.string	" -> %x\n"
	.align	2,0

#ifdef SHOW_SYSCALLS_TASK
	.data
	.globl	show_syscalls_task
show_syscalls_task:
	.long	-1
	.text
#endif
#endif /* SHOW_SYSCALLS */

/*
 * The fork/clone functions need to copy the full register set into
 * the child process. Therefore we need to save all the nonvolatile
 * registers (r13 - r31) before calling the C code.
 */
	.globl	ppc_fork
ppc_fork:
	SAVE_NVGPRS(r1)
	lwz	r0,_TRAP(r1)
	rlwinm	r0,r0,0,0,30		/* clear LSB to indicate full */
	stw	r0,_TRAP(r1)		/* register set saved */
	b	sys_fork

	.globl	ppc_vfork
ppc_vfork:
	SAVE_NVGPRS(r1)
	lwz	r0,_TRAP(r1)
	rlwinm	r0,r0,0,0,30		/* clear LSB to indicate full */
	stw	r0,_TRAP(r1)		/* register set saved */
	b	sys_vfork

	.globl	ppc_clone
ppc_clone:
	SAVE_NVGPRS(r1)
	lwz	r0,_TRAP(r1)
	rlwinm	r0,r0,0,0,30		/* clear LSB to indicate full */
	stw	r0,_TRAP(r1)		/* register set saved */
	b	sys_clone

	.globl	ppc_swapcontext
ppc_swapcontext:
	SAVE_NVGPRS(r1)
	lwz	r0,_TRAP(r1)
	rlwinm	r0,r0,0,0,30		/* clear LSB to indicate full */
	stw	r0,_TRAP(r1)		/* register set saved */
	b	sys_swapcontext

/*
 * Top-level page fault handling.
 * This is in assembler because if do_page_fault tells us that
 * it is a bad kernel page fault, we want to save the non-volatile
 * registers before calling bad_page_fault.
 */
	.globl	handle_page_fault
handle_page_fault:
	stw	r4,_DAR(r1)
	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	do_page_fault
	cmpwi	r3,0
	beq+	ret_from_except
	SAVE_NVGPRS(r1)
	lwz	r0,_TRAP(r1)
	clrrwi	r0,r0,1
	stw	r0,_TRAP(r1)
	mr	r5,r3
	addi	r3,r1,STACK_FRAME_OVERHEAD
	lwz	r4,_DAR(r1)
	bl	bad_page_fault
	b	ret_from_except_full

/*
 * This routine switches between two different tasks.  The process
 * state of one is saved on its kernel stack.  Then the state
 * of the other is restored from its kernel stack.  The memory
 * management hardware is updated to the second process's state.
 * Finally, we can return to the second process.
 * On entry, r3 points to the THREAD for the current task, r4
 * points to the THREAD for the new task.
 *
 * This routine is always called with interrupts disabled.
 *
 * Note: there are two ways to get to the "going out" portion
 * of this code; either by coming in via the entry (_switch)
 * or via "fork" which must set up an environment equivalent
 * to the "_switch" path.  If you change this , you'll have to
 * change the fork code also.
 *
 * The code which creates the new task context is in 'copy_thread'
 * in arch/ppc/kernel/process.c
 */
_GLOBAL(_switch)
	stwu	r1,-INT_FRAME_SIZE(r1)
	mflr	r0
	stw	r0,INT_FRAME_SIZE+4(r1)
	/* r3-r12 are caller saved -- Cort */
	SAVE_NVGPRS(r1)
	stw	r0,_NIP(r1)	/* Return to switch caller */
	mfmsr	r11
	li	r0,MSR_FP	/* Disable floating-point */
#ifdef CONFIG_ALTIVEC
BEGIN_FTR_SECTION
	oris	r0,r0,MSR_VEC@h	/* Disable altivec */
	mfspr	r12,SPRN_VRSAVE	/* save vrsave register value */
	stw	r12,THREAD+THREAD_VRSAVE(r2)
END_FTR_SECTION_IFSET(CPU_FTR_ALTIVEC)
#endif /* CONFIG_ALTIVEC */
#ifdef CONFIG_SPE
	oris	r0,r0,MSR_SPE@h	 /* Disable SPE */
	mfspr	r12,SPRN_SPEFSCR /* save spefscr register value */
	stw	r12,THREAD+THREAD_SPEFSCR(r2)
#endif /* CONFIG_SPE */
	and.	r0,r0,r11	/* FP or altivec or SPE enabled? */
	beq+	1f
	andc	r11,r11,r0
	MTMSRD(r11)
	isync
1:	stw	r11,_MSR(r1)
	mfcr	r10
	stw	r10,_CCR(r1)
	stw	r1,KSP(r3)	/* Set old stack pointer */

#ifdef CONFIG_SMP
	/* We need a sync somewhere here to make sure that if the
	 * previous task gets rescheduled on another CPU, it sees all
	 * stores it has performed on this one.
	 */
	sync
#endif /* CONFIG_SMP */

	tophys(r0,r4)
	CLR_TOP32(r0)
	mtspr	SPRN_SPRG3,r0	/* Update current THREAD phys addr */
	lwz	r1,KSP(r4)	/* Load new stack pointer */

	/* save the old current 'last' for return value */
	mr	r3,r2
	addi	r2,r4,-THREAD	/* Update current */

#ifdef CONFIG_ALTIVEC
BEGIN_FTR_SECTION
	lwz	r0,THREAD+THREAD_VRSAVE(r2)
	mtspr	SPRN_VRSAVE,r0		/* if G4, restore VRSAVE reg */
END_FTR_SECTION_IFSET(CPU_FTR_ALTIVEC)
#endif /* CONFIG_ALTIVEC */
#ifdef CONFIG_SPE
	lwz	r0,THREAD+THREAD_SPEFSCR(r2)
	mtspr	SPRN_SPEFSCR,r0		/* restore SPEFSCR reg */
#endif /* CONFIG_SPE */

	lwz	r0,_CCR(r1)
	mtcrf	0xFF,r0
	/* r3-r12 are destroyed -- Cort */
	REST_NVGPRS(r1)

	lwz	r4,_NIP(r1)	/* Return to _switch caller in new task */
	mtlr	r4
	addi	r1,r1,INT_FRAME_SIZE
	blr

	.globl	fast_exception_return
fast_exception_return:
#if !(defined(CONFIG_4xx) || defined(CONFIG_BOOKE))
	andi.	r10,r9,MSR_RI		/* check for recoverable interrupt */
	beq	1f			/* if not, we've got problems */
#endif

2:	REST_4GPRS(3, r11)
	lwz	r10,_CCR(r11)
	REST_GPR(1, r11)
	mtcr	r10
	lwz	r10,_LINK(r11)
	mtlr	r10
	REST_GPR(10, r11)
	mtspr	SPRN_SRR1,r9
	mtspr	SPRN_SRR0,r12
	REST_GPR(9, r11)
	REST_GPR(12, r11)
	lwz	r11,GPR11(r11)
	SYNC
	RFI

#if !(defined(CONFIG_4xx) || defined(CONFIG_BOOKE))
/* check if the exception happened in a restartable section */
1:	lis	r3,exc_exit_restart_end@ha
	addi	r3,r3,exc_exit_restart_end@l
	cmplw	r12,r3
	bge	3f
	lis	r4,exc_exit_restart@ha
	addi	r4,r4,exc_exit_restart@l
	cmplw	r12,r4
	blt	3f
	lis	r3,fee_restarts@ha
	tophys(r3,r3)
	lwz	r5,fee_restarts@l(r3)
	addi	r5,r5,1
	stw	r5,fee_restarts@l(r3)
	mr	r12,r4		/* restart at exc_exit_restart */
	b	2b

	.comm	fee_restarts,4

/* aargh, a nonrecoverable interrupt, panic */
/* aargh, we don't know which trap this is */
/* but the 601 doesn't implement the RI bit, so assume it's OK */
3:
BEGIN_FTR_SECTION
	b	2b
END_FTR_SECTION_IFSET(CPU_FTR_601)
	li	r10,-1
	stw	r10,_TRAP(r11)
	addi	r3,r1,STACK_FRAME_OVERHEAD
	lis	r10,MSR_KERNEL@h
	ori	r10,r10,MSR_KERNEL@l
	bl	transfer_to_handler_full
	.long	nonrecoverable_exception
	.long	ret_from_except
#endif

	.globl	ret_from_except_full
ret_from_except_full:
	REST_NVGPRS(r1)
	/* fall through */

	.globl	ret_from_except
ret_from_except:
	/* Hard-disable interrupts so that current_thread_info()->flags
	 * can't change between when we test it and when we return
	 * from the interrupt. */
	LOAD_MSR_KERNEL(r10,MSR_KERNEL)
	SYNC			/* Some chip revs have problems here... */
	MTMSRD(r10)		/* disable interrupts */

	lwz	r3,_MSR(r1)	/* Returning to user mode? */
	andi.	r0,r3,MSR_PR
	beq	resume_kernel

user_exc_return:		/* r10 contains MSR_KERNEL here */
	/* Check current_thread_info()->flags */
	rlwinm	r9,r1,0,0,(31-THREAD_SHIFT)
	lwz	r9,TI_FLAGS(r9)
	andi.	r0,r9,(_TIF_SIGPENDING|_TIF_RESTORE_SIGMASK|_TIF_NEED_RESCHED)
	bne	do_work

restore_user:
#if defined(CONFIG_4xx) || defined(CONFIG_BOOKE)
	/* Check whether this process has its own DBCR0 value.  The single
	   step bit tells us that dbcr0 should be loaded. */
	lwz	r0,THREAD+THREAD_DBCR0(r2)
	andis.	r10,r0,DBCR0_IC@h
	bnel-	load_dbcr0
#endif

#ifdef CONFIG_PREEMPT
	b	restore

/* N.B. the only way to get here is from the beq following ret_from_except. */
resume_kernel:
	/* check current_thread_info->preempt_count */
	rlwinm	r9,r1,0,0,(31-THREAD_SHIFT)
	lwz	r0,TI_PREEMPT(r9)
	cmpwi	0,r0,0		/* if non-zero, just restore regs and return */
	bne	restore
	lwz	r0,TI_FLAGS(r9)
	andi.	r0,r0,_TIF_NEED_RESCHED
	beq+	restore
	andi.	r0,r3,MSR_EE	/* interrupts off? */
	beq	restore		/* don't schedule if so */
1:	bl	preempt_schedule_irq
	rlwinm	r9,r1,0,0,(31-THREAD_SHIFT)
	lwz	r3,TI_FLAGS(r9)
	andi.	r0,r3,_TIF_NEED_RESCHED
	bne-	1b
#else
resume_kernel:
#endif /* CONFIG_PREEMPT */

	/* interrupts are hard-disabled at this point */
restore:
	lwz	r0,GPR0(r1)
	lwz	r2,GPR2(r1)
	REST_4GPRS(3, r1)
	REST_2GPRS(7, r1)

	lwz	r10,_XER(r1)
	lwz	r11,_CTR(r1)
	mtspr	SPRN_XER,r10
	mtctr	r11

	PPC405_ERR77(0,r1)
	stwcx.	r0,0,r1			/* to clear the reservation */

#if !(defined(CONFIG_4xx) || defined(CONFIG_BOOKE))
	lwz	r9,_MSR(r1)
	andi.	r10,r9,MSR_RI		/* check if this exception occurred */
	beql	nonrecoverable		/* at a bad place (MSR:RI = 0) */

	lwz	r10,_CCR(r1)
	lwz	r11,_LINK(r1)
	mtcrf	0xFF,r10
	mtlr	r11

	/*
	 * Once we put values in SRR0 and SRR1, we are in a state
	 * where exceptions are not recoverable, since taking an
	 * exception will trash SRR0 and SRR1.  Therefore we clear the
	 * MSR:RI bit to indicate this.  If we do take an exception,
	 * we can't return to the point of the exception but we
	 * can restart the exception exit path at the label
	 * exc_exit_restart below.  -- paulus
	 */
	LOAD_MSR_KERNEL(r10,MSR_KERNEL & ~MSR_RI)
	SYNC
	MTMSRD(r10)		/* clear the RI bit */
	.globl exc_exit_restart
exc_exit_restart:
	lwz	r9,_MSR(r1)
	lwz	r12,_NIP(r1)
	FIX_SRR1(r9,r10)
	mtspr	SPRN_SRR0,r12
	mtspr	SPRN_SRR1,r9
	REST_4GPRS(9, r1)
	lwz	r1,GPR1(r1)
	.globl exc_exit_restart_end
exc_exit_restart_end:
	SYNC
	RFI

#else /* !(CONFIG_4xx || CONFIG_BOOKE) */
	/*
	 * This is a bit different on 4xx/Book-E because it doesn't have
	 * the RI bit in the MSR.
	 * The TLB miss handler checks if we have interrupted
	 * the exception exit path and restarts it if so
	 * (well maybe one day it will... :).
	 */
	lwz	r11,_LINK(r1)
	mtlr	r11
	lwz	r10,_CCR(r1)
	mtcrf	0xff,r10
	REST_2GPRS(9, r1)
	.globl exc_exit_restart
exc_exit_restart:
	lwz	r11,_NIP(r1)
	lwz	r12,_MSR(r1)
exc_exit_start:
	mtspr	SPRN_SRR0,r11
	mtspr	SPRN_SRR1,r12
	REST_2GPRS(11, r1)
	lwz	r1,GPR1(r1)
	.globl exc_exit_restart_end
exc_exit_restart_end:
	PPC405_ERR77_SYNC
	rfi
	b	.			/* prevent prefetch past rfi */

/*
 * Returning from a critical interrupt in user mode doesn't need
 * to be any different from a normal exception.  For a critical
 * interrupt in the kernel, we just return (without checking for
 * preemption) since the interrupt may have happened at some crucial
 * place (e.g. inside the TLB miss handler), and because we will be
 * running with r1 pointing into critical_stack, not the current
 * process's kernel stack (and therefore current_thread_info() will
 * give the wrong answer).
 * We have to restore various SPRs that may have been in use at the
 * time of the critical interrupt.
 *
 */
#ifdef CONFIG_40x
#define PPC_40x_TURN_OFF_MSR_DR						    \
	/* avoid any possible TLB misses here by turning off MSR.DR, we	    \
	 * assume the instructions here are mapped by a pinned TLB entry */ \
	li	r10,MSR_IR;						    \
	mtmsr	r10;							    \
	isync;								    \
	tophys(r1, r1);
#else
#define PPC_40x_TURN_OFF_MSR_DR
#endif

#define RET_FROM_EXC_LEVEL(exc_lvl_srr0, exc_lvl_srr1, exc_lvl_rfi)	\
	REST_NVGPRS(r1);						\
	lwz	r3,_MSR(r1);						\
	andi.	r3,r3,MSR_PR;						\
	LOAD_MSR_KERNEL(r10,MSR_KERNEL);				\
	bne	user_exc_return;					\
	lwz	r0,GPR0(r1);						\
	lwz	r2,GPR2(r1);						\
	REST_4GPRS(3, r1);						\
	REST_2GPRS(7, r1);						\
	lwz	r10,_XER(r1);						\
	lwz	r11,_CTR(r1);						\
	mtspr	SPRN_XER,r10;						\
	mtctr	r11;							\
	PPC405_ERR77(0,r1);						\
	stwcx.	r0,0,r1;		/* to clear the reservation */	\
	lwz	r11,_LINK(r1);						\
	mtlr	r11;							\
	lwz	r10,_CCR(r1);						\
	mtcrf	0xff,r10;						\
	PPC_40x_TURN_OFF_MSR_DR;					\
	lwz	r9,_DEAR(r1);						\
	lwz	r10,_ESR(r1);						\
	mtspr	SPRN_DEAR,r9;						\
	mtspr	SPRN_ESR,r10;						\
	lwz	r11,_NIP(r1);						\
	lwz	r12,_MSR(r1);						\
	mtspr	exc_lvl_srr0,r11;					\
	mtspr	exc_lvl_srr1,r12;					\
	lwz	r9,GPR9(r1);						\
	lwz	r12,GPR12(r1);						\
	lwz	r10,GPR10(r1);						\
	lwz	r11,GPR11(r1);						\
	lwz	r1,GPR1(r1);						\
	PPC405_ERR77_SYNC;						\
	exc_lvl_rfi;							\
	b	.;		/* prevent prefetch past exc_lvl_rfi */

	.globl	ret_from_crit_exc
ret_from_crit_exc:
	RET_FROM_EXC_LEVEL(SPRN_CSRR0, SPRN_CSRR1, RFCI)

#ifdef CONFIG_BOOKE
	.globl	ret_from_debug_exc
ret_from_debug_exc:
	RET_FROM_EXC_LEVEL(SPRN_DSRR0, SPRN_DSRR1, RFDI)

	.globl	ret_from_mcheck_exc
ret_from_mcheck_exc:
	RET_FROM_EXC_LEVEL(SPRN_MCSRR0, SPRN_MCSRR1, RFMCI)
#endif /* CONFIG_BOOKE */

/*
 * Load the DBCR0 value for a task that is being ptraced,
 * having first saved away the global DBCR0.  Note that r0
 * has the dbcr0 value to set upon entry to this.
 */
load_dbcr0:
	mfmsr	r10		/* first disable debug exceptions */
	rlwinm	r10,r10,0,~MSR_DE
	mtmsr	r10
	isync
	mfspr	r10,SPRN_DBCR0
	lis	r11,global_dbcr0@ha
	addi	r11,r11,global_dbcr0@l
	stw	r10,0(r11)
	mtspr	SPRN_DBCR0,r0
	lwz	r10,4(r11)
	addi	r10,r10,1
	stw	r10,4(r11)
	li	r11,-1
	mtspr	SPRN_DBSR,r11	/* clear all pending debug events */
	blr

	.comm	global_dbcr0,8
#endif /* !(CONFIG_4xx || CONFIG_BOOKE) */

do_work:			/* r10 contains MSR_KERNEL here */
	andi.	r0,r9,_TIF_NEED_RESCHED
	beq	do_user_signal

do_resched:			/* r10 contains MSR_KERNEL here */
	ori	r10,r10,MSR_EE
	SYNC
	MTMSRD(r10)		/* hard-enable interrupts */
	bl	schedule
recheck:
	LOAD_MSR_KERNEL(r10,MSR_KERNEL)
	SYNC
	MTMSRD(r10)		/* disable interrupts */
	rlwinm	r9,r1,0,0,(31-THREAD_SHIFT)
	lwz	r9,TI_FLAGS(r9)
	andi.	r0,r9,_TIF_NEED_RESCHED
	bne-	do_resched
	andi.	r0,r9,_TIF_SIGPENDING|_TIF_RESTORE_SIGMASK
	beq	restore_user
do_user_signal:			/* r10 contains MSR_KERNEL here */
	ori	r10,r10,MSR_EE
	SYNC
	MTMSRD(r10)		/* hard-enable interrupts */
	/* save r13-r31 in the exception frame, if not already done */
	lwz	r3,_TRAP(r1)
	andi.	r0,r3,1
	beq	2f
	SAVE_NVGPRS(r1)
	rlwinm	r3,r3,0,0,30
	stw	r3,_TRAP(r1)
2:	li	r3,0
	addi	r4,r1,STACK_FRAME_OVERHEAD
	bl	do_signal
	REST_NVGPRS(r1)
	b	recheck

/*
 * We come here when we are at the end of handling an exception
 * that occurred at a place where taking an exception will lose
 * state information, such as the contents of SRR0 and SRR1.
 */
nonrecoverable:
	lis	r10,exc_exit_restart_end@ha
	addi	r10,r10,exc_exit_restart_end@l
	cmplw	r12,r10
	bge	3f
	lis	r11,exc_exit_restart@ha
	addi	r11,r11,exc_exit_restart@l
	cmplw	r12,r11
	blt	3f
	lis	r10,ee_restarts@ha
	lwz	r12,ee_restarts@l(r10)
	addi	r12,r12,1
	stw	r12,ee_restarts@l(r10)
	mr	r12,r11		/* restart at exc_exit_restart */
	blr
3:	/* OK, we can't recover, kill this process */
	/* but the 601 doesn't implement the RI bit, so assume it's OK */
BEGIN_FTR_SECTION
	blr
END_FTR_SECTION_IFSET(CPU_FTR_601)
	lwz	r3,_TRAP(r1)
	andi.	r0,r3,1
	beq	4f
	SAVE_NVGPRS(r1)
	rlwinm	r3,r3,0,0,30
	stw	r3,_TRAP(r1)
4:	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	nonrecoverable_exception
	/* shouldn't return */
	b	4b

	.comm	ee_restarts,4

/*
 * PROM code for specific machines follows.  Put it
 * here so it's easy to add arch-specific sections later.
 * -- Cort
 */
#ifdef CONFIG_PPC_RTAS
/*
 * On CHRP, the Run-Time Abstraction Services (RTAS) have to be
 * called with the MMU off.
 */
_GLOBAL(enter_rtas)
	stwu	r1,-INT_FRAME_SIZE(r1)
	mflr	r0
	stw	r0,INT_FRAME_SIZE+4(r1)
	LOAD_REG_ADDR(r4, rtas)
	lis	r6,1f@ha	/* physical return address for rtas */
	addi	r6,r6,1f@l
	tophys(r6,r6)
	tophys(r7,r1)
	lwz	r8,RTASENTRY(r4)
	lwz	r4,RTASBASE(r4)
	mfmsr	r9
	stw	r9,8(r1)
	LOAD_MSR_KERNEL(r0,MSR_KERNEL)
	SYNC			/* disable interrupts so SRR0/1 */
	MTMSRD(r0)		/* don't get trashed */
	li	r9,MSR_KERNEL & ~(MSR_IR|MSR_DR)
	mtlr	r6
	mtspr	SPRN_SPRG2,r7
	mtspr	SPRN_SRR0,r8
	mtspr	SPRN_SRR1,r9
	RFI
1:	tophys(r9,r1)
	lwz	r8,INT_FRAME_SIZE+4(r9)	/* get return address */
	lwz	r9,8(r9)	/* original msr value */
	FIX_SRR1(r9,r0)
	addi	r1,r1,INT_FRAME_SIZE
	li	r0,0
	mtspr	SPRN_SPRG2,r0
	mtspr	SPRN_SRR0,r8
	mtspr	SPRN_SRR1,r9
	RFI			/* return to caller */

	.globl	machine_check_in_rtas
machine_check_in_rtas:
	twi	31,0,0
	/* XXX load up BATs and panic */

#endif /* CONFIG_PPC_RTAS */
