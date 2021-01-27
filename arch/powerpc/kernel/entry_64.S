/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  PowerPC version 
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *  Rewritten by Cort Dougan (cort@cs.nmt.edu) for PReP
 *    Copyright (C) 1996 Cort Dougan <cort@cs.nmt.edu>
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
#include <asm/cache.h>
#include <asm/unistd.h>
#include <asm/processor.h>
#include <asm/page.h>
#include <asm/mmu.h>
#include <asm/thread_info.h>
#include <asm/code-patching-asm.h>
#include <asm/ppc_asm.h>
#include <asm/asm-offsets.h>
#include <asm/cputable.h>
#include <asm/firmware.h>
#include <asm/bug.h>
#include <asm/ptrace.h>
#include <asm/irqflags.h>
#include <asm/hw_irq.h>
#include <asm/context_tracking.h>
#include <asm/tm.h>
#include <asm/ppc-opcode.h>
#include <asm/barrier.h>
#include <asm/export.h>
#include <asm/asm-compat.h>
#ifdef CONFIG_PPC_BOOK3S
#include <asm/exception-64s.h>
#else
#include <asm/exception-64e.h>
#endif
#include <asm/feature-fixups.h>
#include <asm/kup.h>

/*
 * System calls.
 */
	.section	".toc","aw"
SYS_CALL_TABLE:
	.tc sys_call_table[TC],sys_call_table

#ifdef CONFIG_COMPAT
COMPAT_SYS_CALL_TABLE:
	.tc compat_sys_call_table[TC],compat_sys_call_table
#endif

/* This value is used to mark exception frames on the stack. */
exception_marker:
	.tc	ID_EXC_MARKER[TC],STACK_FRAME_REGS_MARKER

	.section	".text"
	.align 7

#ifdef CONFIG_PPC_BOOK3S
.macro system_call_vectored name trapnr
	.globl system_call_vectored_\name
system_call_vectored_\name:
_ASM_NOKPROBE_SYMBOL(system_call_vectored_\name)
#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
BEGIN_FTR_SECTION
	extrdi.	r10, r12, 1, (63-MSR_TS_T_LG) /* transaction active? */
	bne	.Ltabort_syscall
END_FTR_SECTION_IFSET(CPU_FTR_TM)
#endif
	SCV_INTERRUPT_TO_KERNEL
	mr	r10,r1
	ld	r1,PACAKSAVE(r13)
	std	r10,0(r1)
	std	r11,_NIP(r1)
	std	r12,_MSR(r1)
	std	r0,GPR0(r1)
	std	r10,GPR1(r1)
	std	r2,GPR2(r1)
	ld	r2,PACATOC(r13)
	mfcr	r12
	li	r11,0
	/* Can we avoid saving r3-r8 in common case? */
	std	r3,GPR3(r1)
	std	r4,GPR4(r1)
	std	r5,GPR5(r1)
	std	r6,GPR6(r1)
	std	r7,GPR7(r1)
	std	r8,GPR8(r1)
	/* Zero r9-r12, this should only be required when restoring all GPRs */
	std	r11,GPR9(r1)
	std	r11,GPR10(r1)
	std	r11,GPR11(r1)
	std	r11,GPR12(r1)
	std	r9,GPR13(r1)
	SAVE_NVGPRS(r1)
	std	r11,_XER(r1)
	std	r11,_LINK(r1)
	std	r11,_CTR(r1)

	li	r11,\trapnr
	std	r11,_TRAP(r1)
	std	r12,_CCR(r1)
	std	r3,ORIG_GPR3(r1)
	addi	r10,r1,STACK_FRAME_OVERHEAD
	ld	r11,exception_marker@toc(r2)
	std	r11,-16(r10)		/* "regshere" marker */

BEGIN_FTR_SECTION
	HMT_MEDIUM
END_FTR_SECTION_IFSET(CPU_FTR_HAS_PPR)

	/*
	 * RECONCILE_IRQ_STATE without calling trace_hardirqs_off(), which
	 * would clobber syscall parameters. Also we always enter with IRQs
	 * enabled and nothing pending. system_call_exception() will call
	 * trace_hardirqs_off().
	 *
	 * scv enters with MSR[EE]=1, so don't set PACA_IRQ_HARD_DIS. The
	 * entry vector already sets PACAIRQSOFTMASK to IRQS_ALL_DISABLED.
	 */

	/* Calling convention has r9 = orig r0, r10 = regs */
	mr	r9,r0
	bl	system_call_exception

.Lsyscall_vectored_\name\()_exit:
	addi    r4,r1,STACK_FRAME_OVERHEAD
	li	r5,1 /* scv */
	bl	syscall_exit_prepare

	ld	r2,_CCR(r1)
	ld	r4,_NIP(r1)
	ld	r5,_MSR(r1)

BEGIN_FTR_SECTION
	stdcx.	r0,0,r1			/* to clear the reservation */
END_FTR_SECTION_IFCLR(CPU_FTR_STCX_CHECKS_ADDRESS)

BEGIN_FTR_SECTION
	HMT_MEDIUM_LOW
END_FTR_SECTION_IFSET(CPU_FTR_HAS_PPR)

	cmpdi	r3,0
	bne	.Lsyscall_vectored_\name\()_restore_regs

	/* rfscv returns with LR->NIA and CTR->MSR */
	mtlr	r4
	mtctr	r5

	/* Could zero these as per ABI, but we may consider a stricter ABI
	 * which preserves these if libc implementations can benefit, so
	 * restore them for now until further measurement is done. */
	ld	r0,GPR0(r1)
	ld	r4,GPR4(r1)
	ld	r5,GPR5(r1)
	ld	r6,GPR6(r1)
	ld	r7,GPR7(r1)
	ld	r8,GPR8(r1)
	/* Zero volatile regs that may contain sensitive kernel data */
	li	r9,0
	li	r10,0
	li	r11,0
	li	r12,0
	mtspr	SPRN_XER,r0

	/*
	 * We don't need to restore AMR on the way back to userspace for KUAP.
	 * The value of AMR only matters while we're in the kernel.
	 */
	mtcr	r2
	ld	r2,GPR2(r1)
	ld	r3,GPR3(r1)
	ld	r13,GPR13(r1)
	ld	r1,GPR1(r1)
	RFSCV_TO_USER
	b	.	/* prevent speculative execution */

.Lsyscall_vectored_\name\()_restore_regs:
	li	r3,0
	mtmsrd	r3,1
	mtspr	SPRN_SRR0,r4
	mtspr	SPRN_SRR1,r5

	ld	r3,_CTR(r1)
	ld	r4,_LINK(r1)
	ld	r5,_XER(r1)

	REST_NVGPRS(r1)
	ld	r0,GPR0(r1)
	mtcr	r2
	mtctr	r3
	mtlr	r4
	mtspr	SPRN_XER,r5
	REST_10GPRS(2, r1)
	REST_2GPRS(12, r1)
	ld	r1,GPR1(r1)
	RFI_TO_USER
.endm

system_call_vectored common 0x3000
/*
 * We instantiate another entry copy for the SIGILL variant, with TRAP=0x7ff0
 * which is tested by system_call_exception when r0 is -1 (as set by vector
 * entry code).
 */
system_call_vectored sigill 0x7ff0


/*
 * Entered via kernel return set up by kernel/sstep.c, must match entry regs
 */
	.globl system_call_vectored_emulate
system_call_vectored_emulate:
_ASM_NOKPROBE_SYMBOL(system_call_vectored_emulate)
	li	r10,IRQS_ALL_DISABLED
	stb	r10,PACAIRQSOFTMASK(r13)
	b	system_call_vectored_common
#endif

	.balign IFETCH_ALIGN_BYTES
	.globl system_call_common
system_call_common:
_ASM_NOKPROBE_SYMBOL(system_call_common)
#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
BEGIN_FTR_SECTION
	extrdi.	r10, r12, 1, (63-MSR_TS_T_LG) /* transaction active? */
	bne	.Ltabort_syscall
END_FTR_SECTION_IFSET(CPU_FTR_TM)
#endif
	mr	r10,r1
	ld	r1,PACAKSAVE(r13)
	std	r10,0(r1)
	std	r11,_NIP(r1)
	std	r12,_MSR(r1)
	std	r0,GPR0(r1)
	std	r10,GPR1(r1)
	std	r2,GPR2(r1)
#ifdef CONFIG_PPC_FSL_BOOK3E
START_BTB_FLUSH_SECTION
	BTB_FLUSH(r10)
END_BTB_FLUSH_SECTION
#endif
	ld	r2,PACATOC(r13)
	mfcr	r12
	li	r11,0
	/* Can we avoid saving r3-r8 in common case? */
	std	r3,GPR3(r1)
	std	r4,GPR4(r1)
	std	r5,GPR5(r1)
	std	r6,GPR6(r1)
	std	r7,GPR7(r1)
	std	r8,GPR8(r1)
	/* Zero r9-r12, this should only be required when restoring all GPRs */
	std	r11,GPR9(r1)
	std	r11,GPR10(r1)
	std	r11,GPR11(r1)
	std	r11,GPR12(r1)
	std	r9,GPR13(r1)
	SAVE_NVGPRS(r1)
	std	r11,_XER(r1)
	std	r11,_CTR(r1)
	mflr	r10

	/*
	 * This clears CR0.SO (bit 28), which is the error indication on
	 * return from this system call.
	 */
	rldimi	r12,r11,28,(63-28)
	li	r11,0xc00
	std	r10,_LINK(r1)
	std	r11,_TRAP(r1)
	std	r12,_CCR(r1)
	std	r3,ORIG_GPR3(r1)
	addi	r10,r1,STACK_FRAME_OVERHEAD
	ld	r11,exception_marker@toc(r2)
	std	r11,-16(r10)		/* "regshere" marker */

	/*
	 * RECONCILE_IRQ_STATE without calling trace_hardirqs_off(), which
	 * would clobber syscall parameters. Also we always enter with IRQs
	 * enabled and nothing pending. system_call_exception() will call
	 * trace_hardirqs_off().
	 */
	li	r11,IRQS_ALL_DISABLED
	li	r12,PACA_IRQ_HARD_DIS
	stb	r11,PACAIRQSOFTMASK(r13)
	stb	r12,PACAIRQHAPPENED(r13)

	/* Calling convention has r9 = orig r0, r10 = regs */
	mr	r9,r0
	bl	system_call_exception

.Lsyscall_exit:
	addi    r4,r1,STACK_FRAME_OVERHEAD
	li	r5,0 /* !scv */
	bl	syscall_exit_prepare

	ld	r2,_CCR(r1)
	ld	r4,_NIP(r1)
	ld	r5,_MSR(r1)
	ld	r6,_LINK(r1)

BEGIN_FTR_SECTION
	stdcx.	r0,0,r1			/* to clear the reservation */
END_FTR_SECTION_IFCLR(CPU_FTR_STCX_CHECKS_ADDRESS)

	mtspr	SPRN_SRR0,r4
	mtspr	SPRN_SRR1,r5
	mtlr	r6

	cmpdi	r3,0
	bne	.Lsyscall_restore_regs
	/* Zero volatile regs that may contain sensitive kernel data */
	li	r0,0
	li	r4,0
	li	r5,0
	li	r6,0
	li	r7,0
	li	r8,0
	li	r9,0
	li	r10,0
	li	r11,0
	li	r12,0
	mtctr	r0
	mtspr	SPRN_XER,r0
.Lsyscall_restore_regs_cont:

BEGIN_FTR_SECTION
	HMT_MEDIUM_LOW
END_FTR_SECTION_IFSET(CPU_FTR_HAS_PPR)

	/*
	 * We don't need to restore AMR on the way back to userspace for KUAP.
	 * The value of AMR only matters while we're in the kernel.
	 */
	mtcr	r2
	ld	r2,GPR2(r1)
	ld	r3,GPR3(r1)
	ld	r13,GPR13(r1)
	ld	r1,GPR1(r1)
	RFI_TO_USER
	b	.	/* prevent speculative execution */

.Lsyscall_restore_regs:
	ld	r3,_CTR(r1)
	ld	r4,_XER(r1)
	REST_NVGPRS(r1)
	mtctr	r3
	mtspr	SPRN_XER,r4
	ld	r0,GPR0(r1)
	REST_8GPRS(4, r1)
	ld	r12,GPR12(r1)
	b	.Lsyscall_restore_regs_cont

#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
.Ltabort_syscall:
	/* Firstly we need to enable TM in the kernel */
	mfmsr	r10
	li	r9, 1
	rldimi	r10, r9, MSR_TM_LG, 63-MSR_TM_LG
	mtmsrd	r10, 0

	/* tabort, this dooms the transaction, nothing else */
	li	r9, (TM_CAUSE_SYSCALL|TM_CAUSE_PERSISTENT)
	TABORT(R9)

	/*
	 * Return directly to userspace. We have corrupted user register state,
	 * but userspace will never see that register state. Execution will
	 * resume after the tbegin of the aborted transaction with the
	 * checkpointed register state.
	 */
	li	r9, MSR_RI
	andc	r10, r10, r9
	mtmsrd	r10, 1
	mtspr	SPRN_SRR0, r11
	mtspr	SPRN_SRR1, r12
	RFI_TO_USER
	b	.	/* prevent speculative execution */
#endif

#ifdef CONFIG_PPC_BOOK3S
_GLOBAL(ret_from_fork_scv)
	bl	schedule_tail
	REST_NVGPRS(r1)
	li	r3,0	/* fork() return value */
	b	.Lsyscall_vectored_common_exit
#endif

_GLOBAL(ret_from_fork)
	bl	schedule_tail
	REST_NVGPRS(r1)
	li	r3,0	/* fork() return value */
	b	.Lsyscall_exit

_GLOBAL(ret_from_kernel_thread)
	bl	schedule_tail
	REST_NVGPRS(r1)
	mtctr	r14
	mr	r3,r15
#ifdef PPC64_ELF_ABI_v2
	mr	r12,r14
#endif
	bctrl
	li	r3,0
	b	.Lsyscall_exit

#ifdef CONFIG_PPC_BOOK3E
/* Save non-volatile GPRs, if not already saved. */
_GLOBAL(save_nvgprs)
	ld	r11,_TRAP(r1)
	andi.	r0,r11,1
	beqlr-
	SAVE_NVGPRS(r1)
	clrrdi	r0,r11,1
	std	r0,_TRAP(r1)
	blr
_ASM_NOKPROBE_SYMBOL(save_nvgprs);
#endif

#ifdef CONFIG_PPC_BOOK3S_64

#define FLUSH_COUNT_CACHE	\
1:	nop;			\
	patch_site 1b, patch__call_flush_branch_caches1; \
1:	nop;			\
	patch_site 1b, patch__call_flush_branch_caches2; \
1:	nop;			\
	patch_site 1b, patch__call_flush_branch_caches3

.macro nops number
	.rept \number
	nop
	.endr
.endm

.balign 32
.global flush_branch_caches
flush_branch_caches:
	/* Save LR into r9 */
	mflr	r9

	// Flush the link stack
	.rept 64
	bl	.+4
	.endr
	b	1f
	nops	6

	.balign 32
	/* Restore LR */
1:	mtlr	r9

	// If we're just flushing the link stack, return here
3:	nop
	patch_site 3b patch__flush_link_stack_return

	li	r9,0x7fff
	mtctr	r9

	PPC_BCCTR_FLUSH

2:	nop
	patch_site 2b patch__flush_count_cache_return

	nops	3

	.rept 278
	.balign 32
	PPC_BCCTR_FLUSH
	nops	7
	.endr

	blr
#else
#define FLUSH_COUNT_CACHE
#endif /* CONFIG_PPC_BOOK3S_64 */

/*
 * This routine switches between two different tasks.  The process
 * state of one is saved on its kernel stack.  Then the state
 * of the other is restored from its kernel stack.  The memory
 * management hardware is updated to the second process's state.
 * Finally, we can return to the second process, via interrupt_return.
 * On entry, r3 points to the THREAD for the current task, r4
 * points to the THREAD for the new task.
 *
 * Note: there are two ways to get to the "going out" portion
 * of this code; either by coming in via the entry (_switch)
 * or via "fork" which must set up an environment equivalent
 * to the "_switch" path.  If you change this you'll have to change
 * the fork code also.
 *
 * The code which creates the new task context is in 'copy_thread'
 * in arch/powerpc/kernel/process.c 
 */
	.align	7
_GLOBAL(_switch)
	mflr	r0
	std	r0,16(r1)
	stdu	r1,-SWITCH_FRAME_SIZE(r1)
	/* r3-r13 are caller saved -- Cort */
	SAVE_NVGPRS(r1)
	std	r0,_NIP(r1)	/* Return to switch caller */
	mfcr	r23
	std	r23,_CCR(r1)
	std	r1,KSP(r3)	/* Set old stack pointer */

	kuap_check_amr r9, r10

	FLUSH_COUNT_CACHE	/* Clobbers r9, ctr */

	/*
	 * On SMP kernels, care must be taken because a task may be
	 * scheduled off CPUx and on to CPUy. Memory ordering must be
	 * considered.
	 *
	 * Cacheable stores on CPUx will be visible when the task is
	 * scheduled on CPUy by virtue of the core scheduler barriers
	 * (see "Notes on Program-Order guarantees on SMP systems." in
	 * kernel/sched/core.c).
	 *
	 * Uncacheable stores in the case of involuntary preemption must
	 * be taken care of. The smp_mb__after_spinlock() in __schedule()
	 * is implemented as hwsync on powerpc, which orders MMIO too. So
	 * long as there is an hwsync in the context switch path, it will
	 * be executed on the source CPU after the task has performed
	 * all MMIO ops on that CPU, and on the destination CPU before the
	 * task performs any MMIO ops there.
	 */

	/*
	 * The kernel context switch path must contain a spin_lock,
	 * which contains larx/stcx, which will clear any reservation
	 * of the task being switched.
	 */
#ifdef CONFIG_PPC_BOOK3S
/* Cancel all explict user streams as they will have no use after context
 * switch and will stop the HW from creating streams itself
 */
	DCBT_BOOK3S_STOP_ALL_STREAM_IDS(r6)
#endif

	addi	r6,r4,-THREAD	/* Convert THREAD to 'current' */
	std	r6,PACACURRENT(r13)	/* Set new 'current' */
#if defined(CONFIG_STACKPROTECTOR)
	ld	r6, TASK_CANARY(r6)
	std	r6, PACA_CANARY(r13)
#endif

	ld	r8,KSP(r4)	/* new stack pointer */
#ifdef CONFIG_PPC_BOOK3S_64
BEGIN_MMU_FTR_SECTION
	b	2f
END_MMU_FTR_SECTION_IFSET(MMU_FTR_TYPE_RADIX)
BEGIN_FTR_SECTION
	clrrdi	r6,r8,28	/* get its ESID */
	clrrdi	r9,r1,28	/* get current sp ESID */
FTR_SECTION_ELSE
	clrrdi	r6,r8,40	/* get its 1T ESID */
	clrrdi	r9,r1,40	/* get current sp 1T ESID */
ALT_MMU_FTR_SECTION_END_IFCLR(MMU_FTR_1T_SEGMENT)
	clrldi.	r0,r6,2		/* is new ESID c00000000? */
	cmpd	cr1,r6,r9	/* or is new ESID the same as current ESID? */
	cror	eq,4*cr1+eq,eq
	beq	2f		/* if yes, don't slbie it */

	/* Bolt in the new stack SLB entry */
	ld	r7,KSP_VSID(r4)	/* Get new stack's VSID */
	oris	r0,r6,(SLB_ESID_V)@h
	ori	r0,r0,(SLB_NUM_BOLTED-1)@l
BEGIN_FTR_SECTION
	li	r9,MMU_SEGSIZE_1T	/* insert B field */
	oris	r6,r6,(MMU_SEGSIZE_1T << SLBIE_SSIZE_SHIFT)@h
	rldimi	r7,r9,SLB_VSID_SSIZE_SHIFT,0
END_MMU_FTR_SECTION_IFSET(MMU_FTR_1T_SEGMENT)

	/* Update the last bolted SLB.  No write barriers are needed
	 * here, provided we only update the current CPU's SLB shadow
	 * buffer.
	 */
	ld	r9,PACA_SLBSHADOWPTR(r13)
	li	r12,0
	std	r12,SLBSHADOW_STACKESID(r9)	/* Clear ESID */
	li	r12,SLBSHADOW_STACKVSID
	STDX_BE	r7,r12,r9			/* Save VSID */
	li	r12,SLBSHADOW_STACKESID
	STDX_BE	r0,r12,r9			/* Save ESID */

	/* No need to check for MMU_FTR_NO_SLBIE_B here, since when
	 * we have 1TB segments, the only CPUs known to have the errata
	 * only support less than 1TB of system memory and we'll never
	 * actually hit this code path.
	 */

	isync
	slbie	r6
BEGIN_FTR_SECTION
	slbie	r6		/* Workaround POWER5 < DD2.1 issue */
END_FTR_SECTION_IFCLR(CPU_FTR_ARCH_207S)
	slbmte	r7,r0
	isync
2:
#endif /* CONFIG_PPC_BOOK3S_64 */

	clrrdi	r7, r8, THREAD_SHIFT	/* base of new stack */
	/* Note: this uses SWITCH_FRAME_SIZE rather than INT_FRAME_SIZE
	   because we don't need to leave the 288-byte ABI gap at the
	   top of the kernel stack. */
	addi	r7,r7,THREAD_SIZE-SWITCH_FRAME_SIZE

	/*
	 * PMU interrupts in radix may come in here. They will use r1, not
	 * PACAKSAVE, so this stack switch will not cause a problem. They
	 * will store to the process stack, which may then be migrated to
	 * another CPU. However the rq lock release on this CPU paired with
	 * the rq lock acquire on the new CPU before the stack becomes
	 * active on the new CPU, will order those stores.
	 */
	mr	r1,r8		/* start using new stack pointer */
	std	r7,PACAKSAVE(r13)

	ld	r6,_CCR(r1)
	mtcrf	0xFF,r6

	/* r3-r13 are destroyed -- Cort */
	REST_NVGPRS(r1)

	/* convert old thread to its task_struct for return value */
	addi	r3,r3,-THREAD
	ld	r7,_NIP(r1)	/* Return to _switch caller in new task */
	mtlr	r7
	addi	r1,r1,SWITCH_FRAME_SIZE
	blr

#ifdef CONFIG_PPC_BOOK3S
	/*
	 * If MSR EE/RI was never enabled, IRQs not reconciled, NVGPRs not
	 * touched, no exit work created, then this can be used.
	 */
	.balign IFETCH_ALIGN_BYTES
	.globl fast_interrupt_return
fast_interrupt_return:
_ASM_NOKPROBE_SYMBOL(fast_interrupt_return)
	kuap_check_amr r3, r4
	ld	r5,_MSR(r1)
	andi.	r0,r5,MSR_PR
	bne	.Lfast_user_interrupt_return
	kuap_restore_amr r3, r4
	andi.	r0,r5,MSR_RI
	li	r3,0 /* 0 return value, no EMULATE_STACK_STORE */
	bne+	.Lfast_kernel_interrupt_return
	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	unrecoverable_exception
	b	. /* should not get here */

	.balign IFETCH_ALIGN_BYTES
	.globl interrupt_return
interrupt_return:
_ASM_NOKPROBE_SYMBOL(interrupt_return)
	ld	r4,_MSR(r1)
	andi.	r0,r4,MSR_PR
	beq	.Lkernel_interrupt_return
	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	interrupt_exit_user_prepare
	cmpdi	r3,0
	bne-	.Lrestore_nvgprs

.Lfast_user_interrupt_return:
	ld	r11,_NIP(r1)
	ld	r12,_MSR(r1)
BEGIN_FTR_SECTION
	ld	r10,_PPR(r1)
	mtspr	SPRN_PPR,r10
END_FTR_SECTION_IFSET(CPU_FTR_HAS_PPR)
	mtspr	SPRN_SRR0,r11
	mtspr	SPRN_SRR1,r12

BEGIN_FTR_SECTION
	stdcx.	r0,0,r1		/* to clear the reservation */
FTR_SECTION_ELSE
	ldarx	r0,0,r1
ALT_FTR_SECTION_END_IFCLR(CPU_FTR_STCX_CHECKS_ADDRESS)

	ld	r3,_CCR(r1)
	ld	r4,_LINK(r1)
	ld	r5,_CTR(r1)
	ld	r6,_XER(r1)
	li	r0,0

	REST_4GPRS(7, r1)
	REST_2GPRS(11, r1)
	REST_GPR(13, r1)

	mtcr	r3
	mtlr	r4
	mtctr	r5
	mtspr	SPRN_XER,r6

	REST_4GPRS(2, r1)
	REST_GPR(6, r1)
	REST_GPR(0, r1)
	REST_GPR(1, r1)
	RFI_TO_USER
	b	.	/* prevent speculative execution */

.Lrestore_nvgprs:
	REST_NVGPRS(r1)
	b	.Lfast_user_interrupt_return

	.balign IFETCH_ALIGN_BYTES
.Lkernel_interrupt_return:
	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	interrupt_exit_kernel_prepare

.Lfast_kernel_interrupt_return:
	cmpdi	cr1,r3,0
	ld	r11,_NIP(r1)
	ld	r12,_MSR(r1)
	mtspr	SPRN_SRR0,r11
	mtspr	SPRN_SRR1,r12

BEGIN_FTR_SECTION
	stdcx.	r0,0,r1		/* to clear the reservation */
FTR_SECTION_ELSE
	ldarx	r0,0,r1
ALT_FTR_SECTION_END_IFCLR(CPU_FTR_STCX_CHECKS_ADDRESS)

	ld	r3,_LINK(r1)
	ld	r4,_CTR(r1)
	ld	r5,_XER(r1)
	ld	r6,_CCR(r1)
	li	r0,0

	REST_4GPRS(7, r1)
	REST_2GPRS(11, r1)

	mtlr	r3
	mtctr	r4
	mtspr	SPRN_XER,r5

	/*
	 * Leaving a stale exception_marker on the stack can confuse
	 * the reliable stack unwinder later on. Clear it.
	 */
	std	r0,STACK_FRAME_OVERHEAD-16(r1)

	REST_4GPRS(2, r1)

	bne-	cr1,1f /* emulate stack store */
	mtcr	r6
	REST_GPR(6, r1)
	REST_GPR(0, r1)
	REST_GPR(1, r1)
	RFI_TO_KERNEL
	b	.	/* prevent speculative execution */

1:	/*
	 * Emulate stack store with update. New r1 value was already calculated
	 * and updated in our interrupt regs by emulate_loadstore, but we can't
	 * store the previous value of r1 to the stack before re-loading our
	 * registers from it, otherwise they could be clobbered.  Use
	 * PACA_EXGEN as temporary storage to hold the store data, as
	 * interrupts are disabled here so it won't be clobbered.
	 */
	mtcr	r6
	std	r9,PACA_EXGEN+0(r13)
	addi	r9,r1,INT_FRAME_SIZE /* get original r1 */
	REST_GPR(6, r1)
	REST_GPR(0, r1)
	REST_GPR(1, r1)
	std	r9,0(r1) /* perform store component of stdu */
	ld	r9,PACA_EXGEN+0(r13)

	RFI_TO_KERNEL
	b	.	/* prevent speculative execution */
#endif /* CONFIG_PPC_BOOK3S */

#ifdef CONFIG_PPC_RTAS
/*
 * On CHRP, the Run-Time Abstraction Services (RTAS) have to be
 * called with the MMU off.
 *
 * In addition, we need to be in 32b mode, at least for now.
 * 
 * Note: r3 is an input parameter to rtas, so don't trash it...
 */
_GLOBAL(enter_rtas)
	mflr	r0
	std	r0,16(r1)
        stdu	r1,-SWITCH_FRAME_SIZE(r1) /* Save SP and create stack space. */

	/* Because RTAS is running in 32b mode, it clobbers the high order half
	 * of all registers that it saves.  We therefore save those registers
	 * RTAS might touch to the stack.  (r0, r3-r13 are caller saved)
   	 */
	SAVE_GPR(2, r1)			/* Save the TOC */
	SAVE_GPR(13, r1)		/* Save paca */
	SAVE_NVGPRS(r1)			/* Save the non-volatiles */

	mfcr	r4
	std	r4,_CCR(r1)
	mfctr	r5
	std	r5,_CTR(r1)
	mfspr	r6,SPRN_XER
	std	r6,_XER(r1)
	mfdar	r7
	std	r7,_DAR(r1)
	mfdsisr	r8
	std	r8,_DSISR(r1)

	/* Temporary workaround to clear CR until RTAS can be modified to
	 * ignore all bits.
	 */
	li	r0,0
	mtcr	r0

#ifdef CONFIG_BUG
	/* There is no way it is acceptable to get here with interrupts enabled,
	 * check it with the asm equivalent of WARN_ON
	 */
	lbz	r0,PACAIRQSOFTMASK(r13)
1:	tdeqi	r0,IRQS_ENABLED
	EMIT_BUG_ENTRY 1b,__FILE__,__LINE__,BUGFLAG_WARNING
#endif

	/* Hard-disable interrupts */
	mfmsr	r6
	rldicl	r7,r6,48,1
	rotldi	r7,r7,16
	mtmsrd	r7,1

	/* Unfortunately, the stack pointer and the MSR are also clobbered,
	 * so they are saved in the PACA which allows us to restore
	 * our original state after RTAS returns.
         */
	std	r1,PACAR1(r13)
        std	r6,PACASAVEDMSR(r13)

	/* Setup our real return addr */	
	LOAD_REG_ADDR(r4,rtas_return_loc)
	clrldi	r4,r4,2			/* convert to realmode address */
       	mtlr	r4

	li	r0,0
	ori	r0,r0,MSR_EE|MSR_SE|MSR_BE|MSR_RI
	andc	r0,r6,r0
	
        li      r9,1
        rldicr  r9,r9,MSR_SF_LG,(63-MSR_SF_LG)
	ori	r9,r9,MSR_IR|MSR_DR|MSR_FE0|MSR_FE1|MSR_FP|MSR_RI|MSR_LE
	andc	r6,r0,r9

__enter_rtas:
	sync				/* disable interrupts so SRR0/1 */
	mtmsrd	r0			/* don't get trashed */

	LOAD_REG_ADDR(r4, rtas)
	ld	r5,RTASENTRY(r4)	/* get the rtas->entry value */
	ld	r4,RTASBASE(r4)		/* get the rtas->base value */
	
	mtspr	SPRN_SRR0,r5
	mtspr	SPRN_SRR1,r6
	RFI_TO_KERNEL
	b	.	/* prevent speculative execution */

rtas_return_loc:
	FIXUP_ENDIAN

	/*
	 * Clear RI and set SF before anything.
	 */
	mfmsr   r6
	li	r0,MSR_RI
	andc	r6,r6,r0
	sldi	r0,r0,(MSR_SF_LG - MSR_RI_LG)
	or	r6,r6,r0
	sync
	mtmsrd  r6

	/* relocation is off at this point */
	GET_PACA(r4)
	clrldi	r4,r4,2			/* convert to realmode address */

	bcl	20,31,$+4
0:	mflr	r3
	ld	r3,(1f-0b)(r3)		/* get &rtas_restore_regs */

        ld	r1,PACAR1(r4)           /* Restore our SP */
        ld	r4,PACASAVEDMSR(r4)     /* Restore our MSR */

	mtspr	SPRN_SRR0,r3
	mtspr	SPRN_SRR1,r4
	RFI_TO_KERNEL
	b	.	/* prevent speculative execution */
_ASM_NOKPROBE_SYMBOL(__enter_rtas)
_ASM_NOKPROBE_SYMBOL(rtas_return_loc)

	.align	3
1:	.8byte	rtas_restore_regs

rtas_restore_regs:
	/* relocation is on at this point */
	REST_GPR(2, r1)			/* Restore the TOC */
	REST_GPR(13, r1)		/* Restore paca */
	REST_NVGPRS(r1)			/* Restore the non-volatiles */

	GET_PACA(r13)

	ld	r4,_CCR(r1)
	mtcr	r4
	ld	r5,_CTR(r1)
	mtctr	r5
	ld	r6,_XER(r1)
	mtspr	SPRN_XER,r6
	ld	r7,_DAR(r1)
	mtdar	r7
	ld	r8,_DSISR(r1)
	mtdsisr	r8

        addi	r1,r1,SWITCH_FRAME_SIZE	/* Unstack our frame */
	ld	r0,16(r1)		/* get return address */

	mtlr    r0
        blr				/* return to caller */

#endif /* CONFIG_PPC_RTAS */

_GLOBAL(enter_prom)
	mflr	r0
	std	r0,16(r1)
        stdu	r1,-SWITCH_FRAME_SIZE(r1) /* Save SP and create stack space */

	/* Because PROM is running in 32b mode, it clobbers the high order half
	 * of all registers that it saves.  We therefore save those registers
	 * PROM might touch to the stack.  (r0, r3-r13 are caller saved)
   	 */
	SAVE_GPR(2, r1)
	SAVE_GPR(13, r1)
	SAVE_NVGPRS(r1)
	mfcr	r10
	mfmsr	r11
	std	r10,_CCR(r1)
	std	r11,_MSR(r1)

	/* Put PROM address in SRR0 */
	mtsrr0	r4

	/* Setup our trampoline return addr in LR */
	bcl	20,31,$+4
0:	mflr	r4
	addi	r4,r4,(1f - 0b)
       	mtlr	r4

	/* Prepare a 32-bit mode big endian MSR
	 */
#ifdef CONFIG_PPC_BOOK3E
	rlwinm	r11,r11,0,1,31
	mtsrr1	r11
	rfi
#else /* CONFIG_PPC_BOOK3E */
	LOAD_REG_IMMEDIATE(r12, MSR_SF | MSR_ISF | MSR_LE)
	andc	r11,r11,r12
	mtsrr1	r11
	RFI_TO_KERNEL
#endif /* CONFIG_PPC_BOOK3E */

1:	/* Return from OF */
	FIXUP_ENDIAN

	/* Just make sure that r1 top 32 bits didn't get
	 * corrupt by OF
	 */
	rldicl	r1,r1,0,32

	/* Restore the MSR (back to 64 bits) */
	ld	r0,_MSR(r1)
	MTMSRD(r0)
        isync

	/* Restore other registers */
	REST_GPR(2, r1)
	REST_GPR(13, r1)
	REST_NVGPRS(r1)
	ld	r4,_CCR(r1)
	mtcr	r4

        addi	r1,r1,SWITCH_FRAME_SIZE
	ld	r0,16(r1)
	mtlr    r0
        blr
