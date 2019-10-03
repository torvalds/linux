/* SPDX-License-Identifier: GPL-2.0-or-later */
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
 */

#include <linux/errno.h>
#include <linux/err.h>
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
#include <asm/ptrace.h>
#include <asm/export.h>
#include <asm/asm-405.h>
#include <asm/feature-fixups.h>
#include <asm/barrier.h>
#include <asm/kup.h>
#include <asm/bug.h>

#include "head_32.h"

/*
 * Align to 4k in order to ensure that all functions modyfing srr0/srr1
 * fit into one page in order to not encounter a TLB miss between the
 * modification of srr0/srr1 and the associated rfi.
 */
	.align	12

#ifdef CONFIG_BOOKE
	.globl	mcheck_transfer_to_handler
mcheck_transfer_to_handler:
	mfspr	r0,SPRN_DSRR0
	stw	r0,_DSRR0(r11)
	mfspr	r0,SPRN_DSRR1
	stw	r0,_DSRR1(r11)
	/* fall through */

	.globl	debug_transfer_to_handler
debug_transfer_to_handler:
	mfspr	r0,SPRN_CSRR0
	stw	r0,_CSRR0(r11)
	mfspr	r0,SPRN_CSRR1
	stw	r0,_CSRR1(r11)
	/* fall through */

	.globl	crit_transfer_to_handler
crit_transfer_to_handler:
#ifdef CONFIG_PPC_BOOK3E_MMU
	mfspr	r0,SPRN_MAS0
	stw	r0,MAS0(r11)
	mfspr	r0,SPRN_MAS1
	stw	r0,MAS1(r11)
	mfspr	r0,SPRN_MAS2
	stw	r0,MAS2(r11)
	mfspr	r0,SPRN_MAS3
	stw	r0,MAS3(r11)
	mfspr	r0,SPRN_MAS6
	stw	r0,MAS6(r11)
#ifdef CONFIG_PHYS_64BIT
	mfspr	r0,SPRN_MAS7
	stw	r0,MAS7(r11)
#endif /* CONFIG_PHYS_64BIT */
#endif /* CONFIG_PPC_BOOK3E_MMU */
#ifdef CONFIG_44x
	mfspr	r0,SPRN_MMUCR
	stw	r0,MMUCR(r11)
#endif
	mfspr	r0,SPRN_SRR0
	stw	r0,_SRR0(r11)
	mfspr	r0,SPRN_SRR1
	stw	r0,_SRR1(r11)

	/* set the stack limit to the current stack */
	mfspr	r8,SPRN_SPRG_THREAD
	lwz	r0,KSP_LIMIT(r8)
	stw	r0,SAVED_KSP_LIMIT(r11)
	rlwinm	r0,r1,0,0,(31 - THREAD_SHIFT)
	stw	r0,KSP_LIMIT(r8)
	/* fall through */
#endif

#ifdef CONFIG_40x
	.globl	crit_transfer_to_handler
crit_transfer_to_handler:
	lwz	r0,crit_r10@l(0)
	stw	r0,GPR10(r11)
	lwz	r0,crit_r11@l(0)
	stw	r0,GPR11(r11)
	mfspr	r0,SPRN_SRR0
	stw	r0,crit_srr0@l(0)
	mfspr	r0,SPRN_SRR1
	stw	r0,crit_srr1@l(0)

	/* set the stack limit to the current stack */
	mfspr	r8,SPRN_SPRG_THREAD
	lwz	r0,KSP_LIMIT(r8)
	stw	r0,saved_ksp_limit@l(0)
	rlwinm	r0,r1,0,0,(31 - THREAD_SHIFT)
	stw	r0,KSP_LIMIT(r8)
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
	mfspr	r12,SPRN_SPRG_THREAD
	beq	2f			/* if from user, fix up THREAD.regs */
	addi	r2, r12, -THREAD
	addi	r11,r1,STACK_FRAME_OVERHEAD
	stw	r11,PT_REGS(r12)
#if defined(CONFIG_40x) || defined(CONFIG_BOOKE)
	/* Check to see if the dbcr0 register is set up to debug.  Use the
	   internal debug mode bit to do this. */
	lwz	r12,THREAD_DBCR0(r12)
	andis.	r12,r12,DBCR0_IDM@h
#endif
	ACCOUNT_CPU_USER_ENTRY(r2, r11, r12)
#ifdef CONFIG_PPC_BOOK3S_32
	kuep_lock r11, r12
#endif
#if defined(CONFIG_40x) || defined(CONFIG_BOOKE)
	beq+	3f
	/* From user and task is ptraced - load up global dbcr0 */
	li	r12,-1			/* clear all pending debug events */
	mtspr	SPRN_DBSR,r12
	lis	r11,global_dbcr0@ha
	tophys(r11,r11)
	addi	r11,r11,global_dbcr0@l
#ifdef CONFIG_SMP
	lwz	r9,TASK_CPU(r2)
	slwi	r9,r9,3
	add	r11,r11,r9
#endif
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
	kuap_save_and_lock r11, r12, r9, r2, r0
	addi	r2, r12, -THREAD
	lwz	r9,KSP_LIMIT(r12)
	cmplw	r1,r9			/* if r1 <= ksp_limit */
	ble-	stack_ovf		/* then the kernel stack overflowed */
5:
#if defined(CONFIG_PPC_BOOK3S_32) || defined(CONFIG_E500)
	lwz	r12,TI_LOCAL_FLAGS(r2)
	mtcrf	0x01,r12
	bt-	31-TLF_NAPPING,4f
	bt-	31-TLF_SLEEPING,7f
#endif /* CONFIG_PPC_BOOK3S_32 || CONFIG_E500 */
	.globl transfer_to_handler_cont
transfer_to_handler_cont:
3:
	mflr	r9
	tovirt(r2, r2)			/* set r2 to current */
	lwz	r11,0(r9)		/* virtual address of handler */
	lwz	r9,4(r9)		/* where to go when done */
#if defined(CONFIG_PPC_8xx) && defined(CONFIG_PERF_EVENTS)
	mtspr	SPRN_NRI, r0
#endif
#ifdef CONFIG_TRACE_IRQFLAGS
	/*
	 * When tracing IRQ state (lockdep) we enable the MMU before we call
	 * the IRQ tracing functions as they might access vmalloc space or
	 * perform IOs for console output.
	 *
	 * To speed up the syscall path where interrupts stay on, let's check
	 * first if we are changing the MSR value at all.
	 */
	tophys(r12, r1)
	lwz	r12,_MSR(r12)
	andi.	r12,r12,MSR_EE
	bne	1f

	/* MSR isn't changing, just transition directly */
#endif
	mtspr	SPRN_SRR0,r11
	mtspr	SPRN_SRR1,r10
	mtlr	r9
	SYNC
	RFI				/* jump to handler, enable MMU */

#ifdef CONFIG_TRACE_IRQFLAGS
1:	/* MSR is changing, re-enable MMU so we can notify lockdep. We need to
	 * keep interrupts disabled at this point otherwise we might risk
	 * taking an interrupt before we tell lockdep they are enabled.
	 */
	lis	r12,reenable_mmu@h
	ori	r12,r12,reenable_mmu@l
	LOAD_REG_IMMEDIATE(r0, MSR_KERNEL)
	mtspr	SPRN_SRR0,r12
	mtspr	SPRN_SRR1,r0
	SYNC
	RFI

reenable_mmu:
	/*
	 * We save a bunch of GPRs,
	 * r3 can be different from GPR3(r1) at this point, r9 and r11
	 * contains the old MSR and handler address respectively,
	 * r4 & r5 can contain page fault arguments that need to be passed
	 * along as well. r12, CCR, CTR, XER etc... are left clobbered as
	 * they aren't useful past this point (aren't syscall arguments),
	 * the rest is restored from the exception frame.
	 */

	stwu	r1,-32(r1)
	stw	r9,8(r1)
	stw	r11,12(r1)
	stw	r3,16(r1)
	stw	r4,20(r1)
	stw	r5,24(r1)

	/* If we are disabling interrupts (normal case), simply log it with
	 * lockdep
	 */
1:	bl	trace_hardirqs_off
2:	lwz	r5,24(r1)
	lwz	r4,20(r1)
	lwz	r3,16(r1)
	lwz	r11,12(r1)
	lwz	r9,8(r1)
	addi	r1,r1,32
	lwz	r0,GPR0(r1)
	lwz	r6,GPR6(r1)
	lwz	r7,GPR7(r1)
	lwz	r8,GPR8(r1)
	mtctr	r11
	mtlr	r9
	bctr				/* jump to handler */
#endif /* CONFIG_TRACE_IRQFLAGS */

#if defined (CONFIG_PPC_BOOK3S_32) || defined(CONFIG_E500)
4:	rlwinm	r12,r12,0,~_TLF_NAPPING
	stw	r12,TI_LOCAL_FLAGS(r2)
	b	power_save_ppc32_restore

7:	rlwinm	r12,r12,0,~_TLF_SLEEPING
	stw	r12,TI_LOCAL_FLAGS(r2)
	lwz	r9,_MSR(r11)		/* if sleeping, clear MSR.EE */
	rlwinm	r9,r9,0,~MSR_EE
	lwz	r12,_LINK(r11)		/* and return to address in LR */
	kuap_restore r11, r2, r3, r4, r5
	b	fast_exception_return
#endif

/*
 * On kernel stack overflow, load up an initial stack pointer
 * and call StackOverflow(regs), which should not return.
 */
stack_ovf:
	/* sometimes we use a statically-allocated stack, which is OK. */
	lis	r12,_end@h
	ori	r12,r12,_end@l
	cmplw	r1,r12
	ble	5b			/* r1 <= &_end is OK */
	SAVE_NVGPRS(r11)
	addi	r3,r1,STACK_FRAME_OVERHEAD
	lis	r1,init_thread_union@ha
	addi	r1,r1,init_thread_union@l
	addi	r1,r1,THREAD_SIZE-STACK_FRAME_OVERHEAD
	lis	r9,StackOverflow@ha
	addi	r9,r9,StackOverflow@l
	LOAD_REG_IMMEDIATE(r10,MSR_KERNEL)
#if defined(CONFIG_PPC_8xx) && defined(CONFIG_PERF_EVENTS)
	mtspr	SPRN_NRI, r0
#endif
	mtspr	SPRN_SRR0,r9
	mtspr	SPRN_SRR1,r10
	SYNC
	RFI

#ifdef CONFIG_TRACE_IRQFLAGS
trace_syscall_entry_irq_off:
	/*
	 * Syscall shouldn't happen while interrupts are disabled,
	 * so let's do a warning here.
	 */
0:	trap
	EMIT_BUG_ENTRY 0b,__FILE__,__LINE__, BUGFLAG_WARNING
	bl	trace_hardirqs_on

	/* Now enable for real */
	LOAD_REG_IMMEDIATE(r10, MSR_KERNEL | MSR_EE)
	mtmsr	r10

	REST_GPR(0, r1)
	REST_4GPRS(3, r1)
	REST_2GPRS(7, r1)
	b	DoSyscall
#endif /* CONFIG_TRACE_IRQFLAGS */

	.globl	transfer_to_syscall
transfer_to_syscall:
#ifdef CONFIG_TRACE_IRQFLAGS
	andi.	r12,r9,MSR_EE
	beq-	trace_syscall_entry_irq_off
#endif /* CONFIG_TRACE_IRQFLAGS */

/*
 * Handle a system call.
 */
	.stabs	"arch/powerpc/kernel/",N_SO,0,0,0f
	.stabs	"entry_32.S",N_SO,0,0,0f
0:

_GLOBAL(DoSyscall)
	stw	r3,ORIG_GPR3(r1)
	li	r12,0
	stw	r12,RESULT(r1)
#ifdef CONFIG_TRACE_IRQFLAGS
	/* Make sure interrupts are enabled */
	mfmsr	r11
	andi.	r12,r11,MSR_EE
	/* We came in with interrupts disabled, we WARN and mark them enabled
	 * for lockdep now */
0:	tweqi	r12, 0
	EMIT_BUG_ENTRY 0b,__FILE__,__LINE__, BUGFLAG_WARNING
#endif /* CONFIG_TRACE_IRQFLAGS */
	lwz	r11,TI_FLAGS(r2)
	andi.	r11,r11,_TIF_SYSCALL_DOTRACE
	bne-	syscall_dotrace
syscall_dotrace_cont:
	cmplwi	0,r0,NR_syscalls
	lis	r10,sys_call_table@h
	ori	r10,r10,sys_call_table@l
	slwi	r0,r0,2
	bge-	66f

	barrier_nospec_asm
	/*
	 * Prevent the load of the handler below (based on the user-passed
	 * system call number) being speculatively executed until the test
	 * against NR_syscalls and branch to .66f above has
	 * committed.
	 */

	lwzx	r10,r10,r0	/* Fetch system call handler [ptr] */
	mtlr	r10
	addi	r9,r1,STACK_FRAME_OVERHEAD
	PPC440EP_ERR42
	blrl			/* Call handler */
	.globl	ret_from_syscall
ret_from_syscall:
#ifdef CONFIG_DEBUG_RSEQ
	/* Check whether the syscall is issued inside a restartable sequence */
	stw	r3,GPR3(r1)
	addi    r3,r1,STACK_FRAME_OVERHEAD
	bl      rseq_syscall
	lwz	r3,GPR3(r1)
#endif
	mr	r6,r3
	/* disable interrupts so current_thread_info()->flags can't change */
	LOAD_REG_IMMEDIATE(r10,MSR_KERNEL)	/* doesn't include MSR_EE */
	/* Note: We don't bother telling lockdep about it */
	SYNC
	MTMSRD(r10)
	lwz	r9,TI_FLAGS(r2)
	li	r8,-MAX_ERRNO
	andi.	r0,r9,(_TIF_SYSCALL_DOTRACE|_TIF_SINGLESTEP|_TIF_USER_WORK_MASK|_TIF_PERSYSCALL_MASK)
	bne-	syscall_exit_work
	cmplw	0,r3,r8
	blt+	syscall_exit_cont
	lwz	r11,_CCR(r1)			/* Load CR */
	neg	r3,r3
	oris	r11,r11,0x1000	/* Set SO bit in CR */
	stw	r11,_CCR(r1)
syscall_exit_cont:
	lwz	r8,_MSR(r1)
#ifdef CONFIG_TRACE_IRQFLAGS
	/* If we are going to return from the syscall with interrupts
	 * off, we trace that here. It shouldn't normally happen.
	 */
	andi.	r10,r8,MSR_EE
	bne+	1f
	stw	r3,GPR3(r1)
	bl      trace_hardirqs_off
	lwz	r3,GPR3(r1)
1:
#endif /* CONFIG_TRACE_IRQFLAGS */
#if defined(CONFIG_4xx) || defined(CONFIG_BOOKE)
	/* If the process has its own DBCR0 value, load it up.  The internal
	   debug mode bit tells us that dbcr0 should be loaded. */
	lwz	r0,THREAD+THREAD_DBCR0(r2)
	andis.	r10,r0,DBCR0_IDM@h
	bnel-	load_dbcr0
#endif
#ifdef CONFIG_44x
BEGIN_MMU_FTR_SECTION
	lis	r4,icache_44x_need_flush@ha
	lwz	r5,icache_44x_need_flush@l(r4)
	cmplwi	cr0,r5,0
	bne-	2f
1:
END_MMU_FTR_SECTION_IFCLR(MMU_FTR_TYPE_47x)
#endif /* CONFIG_44x */
BEGIN_FTR_SECTION
	lwarx	r7,0,r1
END_FTR_SECTION_IFSET(CPU_FTR_NEED_PAIRED_STWCX)
	stwcx.	r0,0,r1			/* to clear the reservation */
	ACCOUNT_CPU_USER_EXIT(r2, r5, r7)
#ifdef CONFIG_PPC_BOOK3S_32
	kuep_unlock r5, r7
#endif
	kuap_check r2, r4
	lwz	r4,_LINK(r1)
	lwz	r5,_CCR(r1)
	mtlr	r4
	mtcr	r5
	lwz	r7,_NIP(r1)
	lwz	r2,GPR2(r1)
	lwz	r1,GPR1(r1)
#if defined(CONFIG_PPC_8xx) && defined(CONFIG_PERF_EVENTS)
	mtspr	SPRN_NRI, r0
#endif
	mtspr	SPRN_SRR0,r7
	mtspr	SPRN_SRR1,r8
	SYNC
	RFI
#ifdef CONFIG_44x
2:	li	r7,0
	iccci	r0,r0
	stw	r7,icache_44x_need_flush@l(r4)
	b	1b
#endif  /* CONFIG_44x */

66:	li	r3,-ENOSYS
	b	ret_from_syscall

	.globl	ret_from_fork
ret_from_fork:
	REST_NVGPRS(r1)
	bl	schedule_tail
	li	r3,0
	b	ret_from_syscall

	.globl	ret_from_kernel_thread
ret_from_kernel_thread:
	REST_NVGPRS(r1)
	bl	schedule_tail
	mtlr	r14
	mr	r3,r15
	PPC440EP_ERR42
	blrl
	li	r3,0
	b	ret_from_syscall

/* Traced system call support */
syscall_dotrace:
	SAVE_NVGPRS(r1)
	li	r0,0xc00
	stw	r0,_TRAP(r1)
	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	do_syscall_trace_enter
	/*
	 * Restore argument registers possibly just changed.
	 * We use the return value of do_syscall_trace_enter
	 * for call number to look up in the table (r0).
	 */
	mr	r0,r3
	lwz	r3,GPR3(r1)
	lwz	r4,GPR4(r1)
	lwz	r5,GPR5(r1)
	lwz	r6,GPR6(r1)
	lwz	r7,GPR7(r1)
	lwz	r8,GPR8(r1)
	REST_NVGPRS(r1)

	cmplwi	r0,NR_syscalls
	/* Return code is already in r3 thanks to do_syscall_trace_enter() */
	bge-	ret_from_syscall
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
	addi	r12,r2,TI_FLAGS
3:	lwarx	r8,0,r12
	andc	r8,r8,r11
#ifdef CONFIG_IBM405_ERR77
	dcbt	0,r12
#endif
	stwcx.	r8,0,r12
	bne-	3b
	
4:	/* Anything which requires enabling interrupts? */
	andi.	r0,r9,(_TIF_SYSCALL_DOTRACE|_TIF_SINGLESTEP)
	beq	ret_from_except

	/* Re-enable interrupts. There is no need to trace that with
	 * lockdep as we are supposed to have IRQs on at this point
	 */
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

	.globl	ppc_clone3
ppc_clone3:
	SAVE_NVGPRS(r1)
	lwz	r0,_TRAP(r1)
	rlwinm	r0,r0,0,0,30		/* clear LSB to indicate full */
	stw	r0,_TRAP(r1)		/* register set saved */
	b	sys_clone3

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
#ifdef CONFIG_PPC_BOOK3S_32
	andis.  r0,r5,DSISR_DABRMATCH@h
	bne-    handle_dabr_fault
#endif
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

#ifdef CONFIG_PPC_BOOK3S_32
	/* We have a data breakpoint exception - handle it */
handle_dabr_fault:
	SAVE_NVGPRS(r1)
	lwz	r0,_TRAP(r1)
	clrrwi	r0,r0,1
	stw	r0,_TRAP(r1)
	bl      do_break
	b	ret_from_except_full
#endif

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
BEGIN_FTR_SECTION
	oris	r0,r0,MSR_SPE@h	 /* Disable SPE */
	mfspr	r12,SPRN_SPEFSCR /* save spefscr register value */
	stw	r12,THREAD+THREAD_SPEFSCR(r2)
END_FTR_SECTION_IFSET(CPU_FTR_SPE)
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

	kuap_check r2, r4
#ifdef CONFIG_SMP
	/* We need a sync somewhere here to make sure that if the
	 * previous task gets rescheduled on another CPU, it sees all
	 * stores it has performed on this one.
	 */
	sync
#endif /* CONFIG_SMP */

	tophys(r0,r4)
	mtspr	SPRN_SPRG_THREAD,r0	/* Update current THREAD phys addr */
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
BEGIN_FTR_SECTION
	lwz	r0,THREAD+THREAD_SPEFSCR(r2)
	mtspr	SPRN_SPEFSCR,r0		/* restore SPEFSCR reg */
END_FTR_SECTION_IFSET(CPU_FTR_SPE)
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
	/* Clear the exception_marker on the stack to avoid confusing stacktrace */
	li	r10, 0
	stw	r10, 8(r11)
	REST_GPR(10, r11)
#if defined(CONFIG_PPC_8xx) && defined(CONFIG_PERF_EVENTS)
	mtspr	SPRN_NRI, r0
#endif
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
#if CONFIG_PPC_BOOK3S_601
	bge	2b
#else
	bge	3f
#endif
	lis	r4,exc_exit_restart@ha
	addi	r4,r4,exc_exit_restart@l
	cmplw	r12,r4
#if CONFIG_PPC_BOOK3S_601
	blt	2b
#else
	blt	3f
#endif
	lis	r3,fee_restarts@ha
	tophys(r3,r3)
	lwz	r5,fee_restarts@l(r3)
	addi	r5,r5,1
	stw	r5,fee_restarts@l(r3)
	mr	r12,r4		/* restart at exc_exit_restart */
	b	2b

	.section .bss
	.align	2
fee_restarts:
	.space	4
	.previous

/* aargh, a nonrecoverable interrupt, panic */
/* aargh, we don't know which trap this is */
/* but the 601 doesn't implement the RI bit, so assume it's OK */
3:
	li	r10,-1
	stw	r10,_TRAP(r11)
	addi	r3,r1,STACK_FRAME_OVERHEAD
	lis	r10,MSR_KERNEL@h
	ori	r10,r10,MSR_KERNEL@l
	bl	transfer_to_handler_full
	.long	unrecoverable_exception
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
	/* Note: We don't bother telling lockdep about it */
	LOAD_REG_IMMEDIATE(r10,MSR_KERNEL)
	SYNC			/* Some chip revs have problems here... */
	MTMSRD(r10)		/* disable interrupts */

	lwz	r3,_MSR(r1)	/* Returning to user mode? */
	andi.	r0,r3,MSR_PR
	beq	resume_kernel

user_exc_return:		/* r10 contains MSR_KERNEL here */
	/* Check current_thread_info()->flags */
	lwz	r9,TI_FLAGS(r2)
	andi.	r0,r9,_TIF_USER_WORK_MASK
	bne	do_work

restore_user:
#if defined(CONFIG_4xx) || defined(CONFIG_BOOKE)
	/* Check whether this process has its own DBCR0 value.  The internal
	   debug mode bit tells us that dbcr0 should be loaded. */
	lwz	r0,THREAD+THREAD_DBCR0(r2)
	andis.	r10,r0,DBCR0_IDM@h
	bnel-	load_dbcr0
#endif
	ACCOUNT_CPU_USER_EXIT(r2, r10, r11)
#ifdef CONFIG_PPC_BOOK3S_32
	kuep_unlock	r10, r11
#endif

	b	restore

/* N.B. the only way to get here is from the beq following ret_from_except. */
resume_kernel:
	/* check current_thread_info, _TIF_EMULATE_STACK_STORE */
	lwz	r8,TI_FLAGS(r2)
	andis.	r0,r8,_TIF_EMULATE_STACK_STORE@h
	beq+	1f

	addi	r8,r1,INT_FRAME_SIZE	/* Get the kprobed function entry */

	lwz	r3,GPR1(r1)
	subi	r3,r3,INT_FRAME_SIZE	/* dst: Allocate a trampoline exception frame */
	mr	r4,r1			/* src:  current exception frame */
	mr	r1,r3			/* Reroute the trampoline frame to r1 */

	/* Copy from the original to the trampoline. */
	li	r5,INT_FRAME_SIZE/4	/* size: INT_FRAME_SIZE */
	li	r6,0			/* start offset: 0 */
	mtctr	r5
2:	lwzx	r0,r6,r4
	stwx	r0,r6,r3
	addi	r6,r6,4
	bdnz	2b

	/* Do real store operation to complete stwu */
	lwz	r5,GPR1(r1)
	stw	r8,0(r5)

	/* Clear _TIF_EMULATE_STACK_STORE flag */
	lis	r11,_TIF_EMULATE_STACK_STORE@h
	addi	r5,r2,TI_FLAGS
0:	lwarx	r8,0,r5
	andc	r8,r8,r11
#ifdef CONFIG_IBM405_ERR77
	dcbt	0,r5
#endif
	stwcx.	r8,0,r5
	bne-	0b
1:

#ifdef CONFIG_PREEMPT
	/* check current_thread_info->preempt_count */
	lwz	r0,TI_PREEMPT(r2)
	cmpwi	0,r0,0		/* if non-zero, just restore regs and return */
	bne	restore_kuap
	andi.	r8,r8,_TIF_NEED_RESCHED
	beq+	restore_kuap
	lwz	r3,_MSR(r1)
	andi.	r0,r3,MSR_EE	/* interrupts off? */
	beq	restore_kuap	/* don't schedule if so */
#ifdef CONFIG_TRACE_IRQFLAGS
	/* Lockdep thinks irqs are enabled, we need to call
	 * preempt_schedule_irq with IRQs off, so we inform lockdep
	 * now that we -did- turn them off already
	 */
	bl	trace_hardirqs_off
#endif
	bl	preempt_schedule_irq
#ifdef CONFIG_TRACE_IRQFLAGS
	/* And now, to properly rebalance the above, we tell lockdep they
	 * are being turned back on, which will happen when we return
	 */
	bl	trace_hardirqs_on
#endif
#endif /* CONFIG_PREEMPT */
restore_kuap:
	kuap_restore r1, r2, r9, r10, r0

	/* interrupts are hard-disabled at this point */
restore:
#ifdef CONFIG_44x
BEGIN_MMU_FTR_SECTION
	b	1f
END_MMU_FTR_SECTION_IFSET(MMU_FTR_TYPE_47x)
	lis	r4,icache_44x_need_flush@ha
	lwz	r5,icache_44x_need_flush@l(r4)
	cmplwi	cr0,r5,0
	beq+	1f
	li	r6,0
	iccci	r0,r0
	stw	r6,icache_44x_need_flush@l(r4)
1:
#endif  /* CONFIG_44x */

	lwz	r9,_MSR(r1)
#ifdef CONFIG_TRACE_IRQFLAGS
	/* Lockdep doesn't know about the fact that IRQs are temporarily turned
	 * off in this assembly code while peeking at TI_FLAGS() and such. However
	 * we need to inform it if the exception turned interrupts off, and we
	 * are about to trun them back on.
	 */
	andi.	r10,r9,MSR_EE
	beq	1f
	stwu	r1,-32(r1)
	mflr	r0
	stw	r0,4(r1)
	bl	trace_hardirqs_on
	addi	r1, r1, 32
	lwz	r9,_MSR(r1)
1:
#endif /* CONFIG_TRACE_IRQFLAGS */

	lwz	r0,GPR0(r1)
	lwz	r2,GPR2(r1)
	REST_4GPRS(3, r1)
	REST_2GPRS(7, r1)

	lwz	r10,_XER(r1)
	lwz	r11,_CTR(r1)
	mtspr	SPRN_XER,r10
	mtctr	r11

	PPC405_ERR77(0,r1)
BEGIN_FTR_SECTION
	lwarx	r11,0,r1
END_FTR_SECTION_IFSET(CPU_FTR_NEED_PAIRED_STWCX)
	stwcx.	r0,0,r1			/* to clear the reservation */

#if !(defined(CONFIG_4xx) || defined(CONFIG_BOOKE))
	andi.	r10,r9,MSR_RI		/* check if this exception occurred */
	beql	nonrecoverable		/* at a bad place (MSR:RI = 0) */

	lwz	r10,_CCR(r1)
	lwz	r11,_LINK(r1)
	mtcrf	0xFF,r10
	mtlr	r11

	/* Clear the exception_marker on the stack to avoid confusing stacktrace */
	li	r10, 0
	stw	r10, 8(r1)
	/*
	 * Once we put values in SRR0 and SRR1, we are in a state
	 * where exceptions are not recoverable, since taking an
	 * exception will trash SRR0 and SRR1.  Therefore we clear the
	 * MSR:RI bit to indicate this.  If we do take an exception,
	 * we can't return to the point of the exception but we
	 * can restart the exception exit path at the label
	 * exc_exit_restart below.  -- paulus
	 */
	LOAD_REG_IMMEDIATE(r10,MSR_KERNEL & ~MSR_RI)
	SYNC
	MTMSRD(r10)		/* clear the RI bit */
	.globl exc_exit_restart
exc_exit_restart:
	lwz	r12,_NIP(r1)
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
	/* Clear the exception_marker on the stack to avoid confusing stacktrace */
	li	r10, 0
	stw	r10, 8(r1)
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
	LOAD_REG_IMMEDIATE(r10,MSR_KERNEL);				\
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

#define	RESTORE_xSRR(exc_lvl_srr0, exc_lvl_srr1)			\
	lwz	r9,_##exc_lvl_srr0(r1);					\
	lwz	r10,_##exc_lvl_srr1(r1);				\
	mtspr	SPRN_##exc_lvl_srr0,r9;					\
	mtspr	SPRN_##exc_lvl_srr1,r10;

#if defined(CONFIG_PPC_BOOK3E_MMU)
#ifdef CONFIG_PHYS_64BIT
#define	RESTORE_MAS7							\
	lwz	r11,MAS7(r1);						\
	mtspr	SPRN_MAS7,r11;
#else
#define	RESTORE_MAS7
#endif /* CONFIG_PHYS_64BIT */
#define RESTORE_MMU_REGS						\
	lwz	r9,MAS0(r1);						\
	lwz	r10,MAS1(r1);						\
	lwz	r11,MAS2(r1);						\
	mtspr	SPRN_MAS0,r9;						\
	lwz	r9,MAS3(r1);						\
	mtspr	SPRN_MAS1,r10;						\
	lwz	r10,MAS6(r1);						\
	mtspr	SPRN_MAS2,r11;						\
	mtspr	SPRN_MAS3,r9;						\
	mtspr	SPRN_MAS6,r10;						\
	RESTORE_MAS7;
#elif defined(CONFIG_44x)
#define RESTORE_MMU_REGS						\
	lwz	r9,MMUCR(r1);						\
	mtspr	SPRN_MMUCR,r9;
#else
#define RESTORE_MMU_REGS
#endif

#ifdef CONFIG_40x
	.globl	ret_from_crit_exc
ret_from_crit_exc:
	mfspr	r9,SPRN_SPRG_THREAD
	lis	r10,saved_ksp_limit@ha;
	lwz	r10,saved_ksp_limit@l(r10);
	tovirt(r9,r9);
	stw	r10,KSP_LIMIT(r9)
	lis	r9,crit_srr0@ha;
	lwz	r9,crit_srr0@l(r9);
	lis	r10,crit_srr1@ha;
	lwz	r10,crit_srr1@l(r10);
	mtspr	SPRN_SRR0,r9;
	mtspr	SPRN_SRR1,r10;
	RET_FROM_EXC_LEVEL(SPRN_CSRR0, SPRN_CSRR1, PPC_RFCI)
#endif /* CONFIG_40x */

#ifdef CONFIG_BOOKE
	.globl	ret_from_crit_exc
ret_from_crit_exc:
	mfspr	r9,SPRN_SPRG_THREAD
	lwz	r10,SAVED_KSP_LIMIT(r1)
	stw	r10,KSP_LIMIT(r9)
	RESTORE_xSRR(SRR0,SRR1);
	RESTORE_MMU_REGS;
	RET_FROM_EXC_LEVEL(SPRN_CSRR0, SPRN_CSRR1, PPC_RFCI)

	.globl	ret_from_debug_exc
ret_from_debug_exc:
	mfspr	r9,SPRN_SPRG_THREAD
	lwz	r10,SAVED_KSP_LIMIT(r1)
	stw	r10,KSP_LIMIT(r9)
	RESTORE_xSRR(SRR0,SRR1);
	RESTORE_xSRR(CSRR0,CSRR1);
	RESTORE_MMU_REGS;
	RET_FROM_EXC_LEVEL(SPRN_DSRR0, SPRN_DSRR1, PPC_RFDI)

	.globl	ret_from_mcheck_exc
ret_from_mcheck_exc:
	mfspr	r9,SPRN_SPRG_THREAD
	lwz	r10,SAVED_KSP_LIMIT(r1)
	stw	r10,KSP_LIMIT(r9)
	RESTORE_xSRR(SRR0,SRR1);
	RESTORE_xSRR(CSRR0,CSRR1);
	RESTORE_xSRR(DSRR0,DSRR1);
	RESTORE_MMU_REGS;
	RET_FROM_EXC_LEVEL(SPRN_MCSRR0, SPRN_MCSRR1, PPC_RFMCI)
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
#ifdef CONFIG_SMP
	lwz	r9,TASK_CPU(r2)
	slwi	r9,r9,3
	add	r11,r11,r9
#endif
	stw	r10,0(r11)
	mtspr	SPRN_DBCR0,r0
	lwz	r10,4(r11)
	addi	r10,r10,1
	stw	r10,4(r11)
	li	r11,-1
	mtspr	SPRN_DBSR,r11	/* clear all pending debug events */
	blr

	.section .bss
	.align	4
	.global global_dbcr0
global_dbcr0:
	.space	8*NR_CPUS
	.previous
#endif /* !(CONFIG_4xx || CONFIG_BOOKE) */

do_work:			/* r10 contains MSR_KERNEL here */
	andi.	r0,r9,_TIF_NEED_RESCHED
	beq	do_user_signal

do_resched:			/* r10 contains MSR_KERNEL here */
#ifdef CONFIG_TRACE_IRQFLAGS
	bl	trace_hardirqs_on
	mfmsr	r10
#endif
	ori	r10,r10,MSR_EE
	SYNC
	MTMSRD(r10)		/* hard-enable interrupts */
	bl	schedule
recheck:
	/* Note: And we don't tell it we are disabling them again
	 * neither. Those disable/enable cycles used to peek at
	 * TI_FLAGS aren't advertised.
	 */
	LOAD_REG_IMMEDIATE(r10,MSR_KERNEL)
	SYNC
	MTMSRD(r10)		/* disable interrupts */
	lwz	r9,TI_FLAGS(r2)
	andi.	r0,r9,_TIF_NEED_RESCHED
	bne-	do_resched
	andi.	r0,r9,_TIF_USER_WORK_MASK
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
2:	addi	r3,r1,STACK_FRAME_OVERHEAD
	mr	r4,r9
	bl	do_notify_resume
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
#ifdef CONFIG_PPC_BOOK3S_601
	bgelr
#else
	bge	3f
#endif
	lis	r11,exc_exit_restart@ha
	addi	r11,r11,exc_exit_restart@l
	cmplw	r12,r11
#ifdef CONFIG_PPC_BOOK3S_601
	bltlr
#else
	blt	3f
#endif
	lis	r10,ee_restarts@ha
	lwz	r12,ee_restarts@l(r10)
	addi	r12,r12,1
	stw	r12,ee_restarts@l(r10)
	mr	r12,r11		/* restart at exc_exit_restart */
	blr
3:	/* OK, we can't recover, kill this process */
	/* but the 601 doesn't implement the RI bit, so assume it's OK */
	lwz	r3,_TRAP(r1)
	andi.	r0,r3,1
	beq	5f
	SAVE_NVGPRS(r1)
	rlwinm	r3,r3,0,0,30
	stw	r3,_TRAP(r1)
5:	mfspr	r2,SPRN_SPRG_THREAD
	addi	r2,r2,-THREAD
	tovirt(r2,r2)			/* set back r2 to current */
4:	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	unrecoverable_exception
	/* shouldn't return */
	b	4b

	.section .bss
	.align	2
ee_restarts:
	.space	4
	.previous

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
	LOAD_REG_IMMEDIATE(r0,MSR_KERNEL)
	SYNC			/* disable interrupts so SRR0/1 */
	MTMSRD(r0)		/* don't get trashed */
	li	r9,MSR_KERNEL & ~(MSR_IR|MSR_DR)
	mtlr	r6
	stw	r7, THREAD + RTAS_SP(r2)
	mtspr	SPRN_SRR0,r8
	mtspr	SPRN_SRR1,r9
	RFI
1:	tophys(r9,r1)
	lwz	r8,INT_FRAME_SIZE+4(r9)	/* get return address */
	lwz	r9,8(r9)	/* original msr value */
	addi	r1,r1,INT_FRAME_SIZE
	li	r0,0
	tophys(r7, r2)
	stw	r0, THREAD + RTAS_SP(r7)
	mtspr	SPRN_SRR0,r8
	mtspr	SPRN_SRR1,r9
	RFI			/* return to caller */

	.globl	machine_check_in_rtas
machine_check_in_rtas:
	twi	31,0,0
	/* XXX load up BATs and panic */

#endif /* CONFIG_PPC_RTAS */
