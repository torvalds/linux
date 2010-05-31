/*
 * This file contains low-level functions for performing various
 * types of TLB invalidations on various processors with no hash
 * table.
 *
 * This file implements the following functions for all no-hash
 * processors. Some aren't implemented for some variants. Some
 * are inline in tlbflush.h
 *
 *	- tlbil_va
 *	- tlbil_pid
 *	- tlbil_all
 *	- tlbivax_bcast
 *
 * Code mostly moved over from misc_32.S
 *
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *
 * Partially rewritten by Cort Dougan (cort@cs.nmt.edu)
 * Paul Mackerras, Kumar Gala and Benjamin Herrenschmidt.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 */

#include <asm/reg.h>
#include <asm/page.h>
#include <asm/cputable.h>
#include <asm/mmu.h>
#include <asm/ppc_asm.h>
#include <asm/asm-offsets.h>
#include <asm/processor.h>
#include <asm/bug.h>

#if defined(CONFIG_40x)

/*
 * 40x implementation needs only tlbil_va
 */
_GLOBAL(__tlbil_va)
	/* We run the search with interrupts disabled because we have to change
	 * the PID and I don't want to preempt when that happens.
	 */
	mfmsr	r5
	mfspr	r6,SPRN_PID
	wrteei	0
	mtspr	SPRN_PID,r4
	tlbsx.	r3, 0, r3
	mtspr	SPRN_PID,r6
	wrtee	r5
	bne	1f
	sync
	/* There are only 64 TLB entries, so r3 < 64, which means bit 25 is
	 * clear. Since 25 is the V bit in the TLB_TAG, loading this value
	 * will invalidate the TLB entry. */
	tlbwe	r3, r3, TLB_TAG
	isync
1:	blr

#elif defined(CONFIG_8xx)

/*
 * Nothing to do for 8xx, everything is inline
 */

#elif defined(CONFIG_44x) /* Includes 47x */

/*
 * 440 implementation uses tlbsx/we for tlbil_va and a full sweep
 * of the TLB for everything else.
 */
_GLOBAL(__tlbil_va)
	mfspr	r5,SPRN_MMUCR
	mfmsr   r10

	/*
	 * We write 16 bits of STID since 47x supports that much, we
	 * will never be passed out of bounds values on 440 (hopefully)
	 */
	rlwimi  r5,r4,0,16,31

	/* We have to run the search with interrupts disabled, otherwise
	 * an interrupt which causes a TLB miss can clobber the MMUCR
	 * between the mtspr and the tlbsx.
	 *
	 * Critical and Machine Check interrupts take care of saving
	 * and restoring MMUCR, so only normal interrupts have to be
	 * taken care of.
	 */
	wrteei	0
	mtspr	SPRN_MMUCR,r5
	tlbsx.	r6,0,r3
	bne	10f
	sync
BEGIN_MMU_FTR_SECTION
	b	2f
END_MMU_FTR_SECTION_IFSET(MMU_FTR_TYPE_47x)
	/* On 440 There are only 64 TLB entries, so r3 < 64, which means bit
	 * 22, is clear.  Since 22 is the V bit in the TLB_PAGEID, loading this
	 * value will invalidate the TLB entry.
	 */
	tlbwe	r6,r6,PPC44x_TLB_PAGEID
	isync
10:	wrtee	r10
	blr
2:
#ifdef CONFIG_PPC_47x
	oris	r7,r6,0x8000	/* specify way explicitely */
	clrrwi	r4,r3,12	/* get an EPN for the hashing with V = 0 */
	ori	r4,r4,PPC47x_TLBE_SIZE
	tlbwe   r4,r7,0		/* write it */
	isync
	wrtee	r10
	blr
#else /* CONFIG_PPC_47x */
1:	trap
	EMIT_BUG_ENTRY 1b,__FILE__,__LINE__,0;
#endif /* !CONFIG_PPC_47x */

_GLOBAL(_tlbil_all)
_GLOBAL(_tlbil_pid)
BEGIN_MMU_FTR_SECTION
	b	2f
END_MMU_FTR_SECTION_IFSET(MMU_FTR_TYPE_47x)
	li	r3,0
	sync

	/* Load high watermark */
	lis	r4,tlb_44x_hwater@ha
	lwz	r5,tlb_44x_hwater@l(r4)

1:	tlbwe	r3,r3,PPC44x_TLB_PAGEID
	addi	r3,r3,1
	cmpw	0,r3,r5
	ble	1b

	isync
	blr
2:
#ifdef CONFIG_PPC_47x
	/* 476 variant. There's not simple way to do this, hopefully we'll
	 * try to limit the amount of such full invalidates
	 */
	mfmsr	r11		/* Interrupts off */
	wrteei	0
	li	r3,-1		/* Current set */
	lis	r10,tlb_47x_boltmap@h
	ori	r10,r10,tlb_47x_boltmap@l
	lis	r7,0x8000	/* Specify way explicitely */

	b	9f		/* For each set */

1:	li	r9,4		/* Number of ways */
	li	r4,0		/* Current way */
	li	r6,0		/* Default entry value 0 */
	andi.	r0,r8,1		/* Check if way 0 is bolted */
	mtctr	r9		/* Load way counter */
	bne-	3f		/* Bolted, skip loading it */

2:	/* For each way */
	or	r5,r3,r4	/* Make way|index for tlbre */
	rlwimi	r5,r5,16,8,15	/* Copy index into position */
	tlbre	r6,r5,0		/* Read entry */
3:	addis	r4,r4,0x2000	/* Next way */
	andi.	r0,r6,PPC47x_TLB0_VALID /* Valid entry ? */
	beq	4f		/* Nope, skip it */
	rlwimi	r7,r5,0,1,2	/* Insert way number */
	rlwinm	r6,r6,0,21,19	/* Clear V */
	tlbwe   r6,r7,0		/* Write it */
4:	bdnz	2b		/* Loop for each way */
	srwi	r8,r8,1		/* Next boltmap bit */
9:	cmpwi	cr1,r3,255	/* Last set done ? */
	addi	r3,r3,1		/* Next set */
	beq	cr1,1f		/* End of loop */
	andi.	r0,r3,0x1f	/* Need to load a new boltmap word ? */
	bne	1b		/* No, loop */
	lwz	r8,0(r10)	/* Load boltmap entry */
	addi	r10,r10,4	/* Next word */
	b	1b		/* Then loop */
1:	isync			/* Sync shadows */
	wrtee	r11
#else /* CONFIG_PPC_47x */
1:	trap
	EMIT_BUG_ENTRY 1b,__FILE__,__LINE__,0;
#endif /* !CONFIG_PPC_47x */
	blr

#ifdef CONFIG_PPC_47x
/*
 * _tlbivax_bcast is only on 47x. We don't bother doing a runtime
 * check though, it will blow up soon enough if we mistakenly try
 * to use it on a 440.
 */
_GLOBAL(_tlbivax_bcast)
	mfspr	r5,SPRN_MMUCR
	mfmsr	r10
	rlwimi	r5,r4,0,16,31
	wrteei	0
	mtspr	SPRN_MMUCR,r5
/*	tlbivax	0,r3 - use .long to avoid binutils deps */
	.long 0x7c000624 | (r3 << 11)
	isync
	eieio
	tlbsync
	sync
	wrtee	r10
	blr
#endif /* CONFIG_PPC_47x */

#elif defined(CONFIG_FSL_BOOKE)
/*
 * FSL BookE implementations.
 *
 * Since feature sections are using _SECTION_ELSE we need
 * to have the larger code path before the _SECTION_ELSE
 */

/*
 * Flush MMU TLB on the local processor
 */
_GLOBAL(_tlbil_all)
BEGIN_MMU_FTR_SECTION
	li	r3,(MMUCSR0_TLBFI)@l
	mtspr	SPRN_MMUCSR0, r3
1:
	mfspr	r3,SPRN_MMUCSR0
	andi.	r3,r3,MMUCSR0_TLBFI@l
	bne	1b
MMU_FTR_SECTION_ELSE
	PPC_TLBILX_ALL(0,0)
ALT_MMU_FTR_SECTION_END_IFCLR(MMU_FTR_USE_TLBILX)
	msync
	isync
	blr

_GLOBAL(_tlbil_pid)
BEGIN_MMU_FTR_SECTION
	slwi	r3,r3,16
	mfmsr	r10
	wrteei	0
	mfspr	r4,SPRN_MAS6	/* save MAS6 */
	mtspr	SPRN_MAS6,r3
	PPC_TLBILX_PID(0,0)
	mtspr	SPRN_MAS6,r4	/* restore MAS6 */
	wrtee	r10
MMU_FTR_SECTION_ELSE
	li	r3,(MMUCSR0_TLBFI)@l
	mtspr	SPRN_MMUCSR0, r3
1:
	mfspr	r3,SPRN_MMUCSR0
	andi.	r3,r3,MMUCSR0_TLBFI@l
	bne	1b
ALT_MMU_FTR_SECTION_END_IFSET(MMU_FTR_USE_TLBILX)
	msync
	isync
	blr

/*
 * Flush MMU TLB for a particular address, but only on the local processor
 * (no broadcast)
 */
_GLOBAL(__tlbil_va)
	mfmsr	r10
	wrteei	0
	slwi	r4,r4,16
	ori	r4,r4,(MAS6_ISIZE(BOOK3E_PAGESZ_4K))@l
	mtspr	SPRN_MAS6,r4		/* assume AS=0 for now */
BEGIN_MMU_FTR_SECTION
	tlbsx	0,r3
	mfspr	r4,SPRN_MAS1		/* check valid */
	andis.	r3,r4,MAS1_VALID@h
	beq	1f
	rlwinm	r4,r4,0,1,31
	mtspr	SPRN_MAS1,r4
	tlbwe
MMU_FTR_SECTION_ELSE
	PPC_TLBILX_VA(0,r3)
ALT_MMU_FTR_SECTION_END_IFCLR(MMU_FTR_USE_TLBILX)
	msync
	isync
1:	wrtee	r10
	blr
#elif defined(CONFIG_PPC_BOOK3E)
/*
 * New Book3E (>= 2.06) implementation
 *
 * Note: We may be able to get away without the interrupt masking stuff
 * if we save/restore MAS6 on exceptions that might modify it
 */
_GLOBAL(_tlbil_pid)
	slwi	r4,r3,MAS6_SPID_SHIFT
	mfmsr	r10
	wrteei	0
	mtspr	SPRN_MAS6,r4
	PPC_TLBILX_PID(0,0)
	wrtee	r10
	msync
	isync
	blr

_GLOBAL(_tlbil_pid_noind)
	slwi	r4,r3,MAS6_SPID_SHIFT
	mfmsr	r10
	ori	r4,r4,MAS6_SIND
	wrteei	0
	mtspr	SPRN_MAS6,r4
	PPC_TLBILX_PID(0,0)
	wrtee	r10
	msync
	isync
	blr

_GLOBAL(_tlbil_all)
	PPC_TLBILX_ALL(0,0)
	msync
	isync
	blr

_GLOBAL(_tlbil_va)
	mfmsr	r10
	wrteei	0
	cmpwi	cr0,r6,0
	slwi	r4,r4,MAS6_SPID_SHIFT
	rlwimi	r4,r5,MAS6_ISIZE_SHIFT,MAS6_ISIZE_MASK
	beq	1f
	rlwimi	r4,r6,MAS6_SIND_SHIFT,MAS6_SIND
1:	mtspr	SPRN_MAS6,r4		/* assume AS=0 for now */
	PPC_TLBILX_VA(0,r3)
	msync
	isync
	wrtee	r10
	blr

_GLOBAL(_tlbivax_bcast)
	mfmsr	r10
	wrteei	0
	cmpwi	cr0,r6,0
	slwi	r4,r4,MAS6_SPID_SHIFT
	rlwimi	r4,r5,MAS6_ISIZE_SHIFT,MAS6_ISIZE_MASK
	beq	1f
	rlwimi	r4,r6,MAS6_SIND_SHIFT,MAS6_SIND
1:	mtspr	SPRN_MAS6,r4		/* assume AS=0 for now */
	PPC_TLBIVAX(0,r3)
	eieio
	tlbsync
	sync
	wrtee	r10
	blr

_GLOBAL(set_context)
#ifdef CONFIG_BDI_SWITCH
	/* Context switch the PTE pointer for the Abatron BDI2000.
	 * The PGDIR is the second parameter.
	 */
	lis	r5, abatron_pteptrs@h
	ori	r5, r5, abatron_pteptrs@l
	stw	r4, 0x4(r5)
#endif
	mtspr	SPRN_PID,r3
	isync			/* Force context change */
	blr
#else
#error Unsupported processor type !
#endif

#if defined(CONFIG_FSL_BOOKE)
/*
 * extern void loadcam_entry(unsigned int index)
 *
 * Load TLBCAM[index] entry in to the L2 CAM MMU
 */
_GLOBAL(loadcam_entry)
	LOAD_REG_ADDR(r4, TLBCAM)
	mulli	r5,r3,TLBCAM_SIZE
	add	r3,r5,r4
	lwz	r4,TLBCAM_MAS0(r3)
	mtspr	SPRN_MAS0,r4
	lwz	r4,TLBCAM_MAS1(r3)
	mtspr	SPRN_MAS1,r4
	PPC_LL	r4,TLBCAM_MAS2(r3)
	mtspr	SPRN_MAS2,r4
	lwz	r4,TLBCAM_MAS3(r3)
	mtspr	SPRN_MAS3,r4
BEGIN_MMU_FTR_SECTION
	lwz	r4,TLBCAM_MAS7(r3)
	mtspr	SPRN_MAS7,r4
END_MMU_FTR_SECTION_IFSET(MMU_FTR_BIG_PHYS)
	isync
	tlbwe
	isync
	blr
#endif
