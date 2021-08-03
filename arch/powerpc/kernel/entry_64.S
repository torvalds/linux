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
	.section	".text"

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
	LOAD_REG_IMMEDIATE(r12, MSR_SF | MSR_LE)
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
