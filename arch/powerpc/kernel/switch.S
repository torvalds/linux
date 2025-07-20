/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <linux/objtool.h>
#include <asm/asm-offsets.h>
#include <asm/code-patching-asm.h>
#include <asm/mmu.h>
#include <asm/ppc_asm.h>
#include <asm/kup.h>
#include <asm/thread_info.h>

.section ".text","ax",@progbits

#ifdef CONFIG_PPC_BOOK3S_64
/*
 * Cancel all explict user streams as they will have no use after context
 * switch and will stop the HW from creating streams itself
 */
#define STOP_STREAMS		\
	DCBT_BOOK3S_STOP_ALL_STREAM_IDS(r6)

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

#ifdef CONFIG_PPC_64S_HASH_MMU
.balign 32
/*
 * New stack pointer in r8, old stack pointer in r1, must not clobber r3
 */
pin_stack_slb:
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
2:	blr
	.size pin_stack_slb,.-pin_stack_slb
#endif /* CONFIG_PPC_64S_HASH_MMU */

#else
#define STOP_STREAMS
#define FLUSH_COUNT_CACHE
#endif /* CONFIG_PPC_BOOK3S_64 */

/*
 * do_switch_32/64 have the same calling convention as _switch, i.e., r3,r4
 * are prev and next thread_struct *, and returns prev task_struct * in r3.

 * This switches the stack, current, and does other task switch housekeeping.
 */
.macro do_switch_32
	tophys(r0,r4)
	mtspr	SPRN_SPRG_THREAD,r0	/* Update current THREAD phys addr */
	lwz	r1,KSP(r4)	/* Load new stack pointer */

	/* save the old current 'last' for return value */
	mr	r3,r2
	addi	r2,r4,-THREAD	/* Update current */
.endm

.macro do_switch_64
	ld	r8,KSP(r4)	/* Load new stack pointer */

	kuap_check_amr r9, r10

	FLUSH_COUNT_CACHE	/* Clobbers r9, ctr */

	STOP_STREAMS		/* Clobbers r6 */

	addi	r3,r3,-THREAD	/* old thread -> task_struct for return value */
	addi	r6,r4,-THREAD	/* new thread -> task_struct */
	std	r6,PACACURRENT(r13)	/* Set new task_struct to 'current' */
#if defined(CONFIG_STACKPROTECTOR)
	ld	r6, TASK_CANARY(r6)
	std	r6, PACA_CANARY(r13)
#endif
	/* Set new PACAKSAVE */
	clrrdi	r7,r8,THREAD_SHIFT	/* base of new stack */
	addi	r7,r7,THREAD_SIZE-SWITCH_FRAME_SIZE
	std	r7,PACAKSAVE(r13)

#ifdef CONFIG_PPC_64S_HASH_MMU
BEGIN_MMU_FTR_SECTION
	bl	pin_stack_slb
END_MMU_FTR_SECTION_IFCLR(MMU_FTR_TYPE_RADIX)
#endif
	/*
	 * PMU interrupts in radix may come in here. They will use r1, not
	 * PACAKSAVE, so this stack switch will not cause a problem. They
	 * will store to the process stack, which may then be migrated to
	 * another CPU. However the rq lock release on this CPU paired with
	 * the rq lock acquire on the new CPU before the stack becomes
	 * active on the new CPU, will order those stores.
	 */
	mr	r1,r8		/* start using new stack pointer */
.endm

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
 *
 * Note: this uses SWITCH_FRAME_SIZE rather than USER_INT_FRAME_SIZE
 * because we don't need to leave the redzone ABI gap at the top of
 * the kernel stack.
 */
_GLOBAL(_switch)
	PPC_CREATE_STACK_FRAME(SWITCH_FRAME_SIZE)
	PPC_STL		r1,KSP(r3)	/* Set old stack pointer */
	SAVE_NVGPRS(r1)			/* volatiles are caller-saved -- Cort */
	PPC_STL		r0,_NIP(r1)	/* Return to switch caller */
	mfcr		r0
	stw		r0,_CCR(r1)

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

#ifdef CONFIG_PPC32
	do_switch_32
#else
	do_switch_64
#endif

	lwz	r0,_CCR(r1)
	mtcrf	0xFF,r0
	REST_NVGPRS(r1)		/* volatiles are destroyed -- Cort */
	PPC_LL	r0,_NIP(r1)	/* Return to _switch caller in new task */
	mtlr	r0
	addi	r1,r1,SWITCH_FRAME_SIZE
	blr
