/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  This file contains low-level assembler routines for managing
 *  the PowerPC 603 tlb invalidation.
 */

#include <asm/page.h>
#include <asm/ppc_asm.h>
#include <asm/asm-offsets.h>

/*
 * Flush an entry from the TLB
 */
#ifdef CONFIG_SMP
_GLOBAL(_tlbie)
	lwz	r8,TASK_CPU(r2)
	oris	r8,r8,11
	mfmsr	r10
	rlwinm	r0,r10,0,17,15		/* clear bit 16 (MSR_EE) */
	rlwinm	r0,r0,0,28,26		/* clear DR */
	mtmsr	r0
	isync
	lis	r9,mmu_hash_lock@h
	ori	r9,r9,mmu_hash_lock@l
	tophys(r9,r9)
10:	lwarx	r7,0,r9
	cmpwi	0,r7,0
	bne-	10b
	stwcx.	r8,0,r9
	bne-	10b
	eieio
	tlbie	r3
	sync
	TLBSYNC
	li	r0,0
	stw	r0,0(r9)		/* clear mmu_hash_lock */
	mtmsr	r10
	isync
	blr
_ASM_NOKPROBE_SYMBOL(_tlbie)
#endif /* CONFIG_SMP */

/*
 * Flush the entire TLB. 603/603e only
 */
_GLOBAL(_tlbia)
#if defined(CONFIG_SMP)
	lwz	r8,TASK_CPU(r2)
	oris	r8,r8,10
	mfmsr	r10
	rlwinm	r0,r10,0,17,15		/* clear bit 16 (MSR_EE) */
	rlwinm	r0,r0,0,28,26		/* clear DR */
	mtmsr	r0
	isync
	lis	r9,mmu_hash_lock@h
	ori	r9,r9,mmu_hash_lock@l
	tophys(r9,r9)
10:	lwarx	r7,0,r9
	cmpwi	0,r7,0
	bne-	10b
	stwcx.	r8,0,r9
	bne-	10b
#endif /* CONFIG_SMP */
	li	r5, 32
	lis	r4, KERNELBASE@h
	mtctr	r5
	sync
0:	tlbie	r4
	addi	r4, r4, 0x1000
	bdnz	0b
	sync
#ifdef CONFIG_SMP
	TLBSYNC
	li	r0,0
	stw	r0,0(r9)		/* clear mmu_hash_lock */
	mtmsr	r10
	isync
#endif /* CONFIG_SMP */
	blr
_ASM_NOKPROBE_SYMBOL(_tlbia)
