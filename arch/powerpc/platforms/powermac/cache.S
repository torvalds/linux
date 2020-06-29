/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * This file contains low-level cache management functions
 * used for sleep and CPU speed changes on Apple machines.
 * (In fact the only thing that is Apple-specific is that we assume
 * that we can read from ROM at physical address 0xfff00000.)
 *
 *    Copyright (C) 2004 Paul Mackerras (paulus@samba.org) and
 *                       Benjamin Herrenschmidt (benh@kernel.crashing.org)
 */

#include <asm/processor.h>
#include <asm/ppc_asm.h>
#include <asm/cputable.h>
#include <asm/feature-fixups.h>

/*
 * Flush and disable all data caches (dL1, L2, L3). This is used
 * when going to sleep, when doing a PMU based cpufreq transition,
 * or when "offlining" a CPU on SMP machines. This code is over
 * paranoid, but I've had enough issues with various CPU revs and
 * bugs that I decided it was worth being over cautious
 */

_GLOBAL(flush_disable_caches)
#ifndef CONFIG_PPC_BOOK3S_32
	blr
#else
BEGIN_FTR_SECTION
	b	flush_disable_745x
END_FTR_SECTION_IFSET(CPU_FTR_SPEC7450)
BEGIN_FTR_SECTION
	b	flush_disable_75x
END_FTR_SECTION_IFSET(CPU_FTR_L2CR)
	b	__flush_disable_L1

/* This is the code for G3 and 74[01]0 */
flush_disable_75x:
	mflr	r10

	/* Turn off EE and DR in MSR */
	mfmsr	r11
	rlwinm	r0,r11,0,~MSR_EE
	rlwinm	r0,r0,0,~MSR_DR
	sync
	mtmsr	r0
	isync

	/* Stop DST streams */
BEGIN_FTR_SECTION
	DSSALL
	sync
END_FTR_SECTION_IFSET(CPU_FTR_ALTIVEC)

	/* Stop DPM */
	mfspr	r8,SPRN_HID0		/* Save SPRN_HID0 in r8 */
	rlwinm	r4,r8,0,12,10		/* Turn off HID0[DPM] */
	sync
	mtspr	SPRN_HID0,r4		/* Disable DPM */
	sync

	/* Disp-flush L1. We have a weird problem here that I never
	 * totally figured out. On 750FX, using the ROM for the flush
	 * results in a non-working flush. We use that workaround for
	 * now until I finally understand what's going on. --BenH
	 */

	/* ROM base by default */
	lis	r4,0xfff0
	mfpvr	r3
	srwi	r3,r3,16
	cmplwi	cr0,r3,0x7000
	bne+	1f
	/* RAM base on 750FX */
	li	r4,0
1:	li	r4,0x4000
	mtctr	r4
1:	lwz	r0,0(r4)
	addi	r4,r4,32
	bdnz	1b
	sync
	isync

	/* Disable / invalidate / enable L1 data */
	mfspr	r3,SPRN_HID0
	rlwinm	r3,r3,0,~(HID0_DCE | HID0_ICE)
	mtspr	SPRN_HID0,r3
	sync
	isync
	ori	r3,r3,(HID0_DCE|HID0_DCI|HID0_ICE|HID0_ICFI)
	sync
	isync
	mtspr	SPRN_HID0,r3
	xori	r3,r3,(HID0_DCI|HID0_ICFI)
	mtspr	SPRN_HID0,r3
	sync

	/* Get the current enable bit of the L2CR into r4 */
	mfspr	r5,SPRN_L2CR
	/* Set to data-only (pre-745x bit) */
	oris	r3,r5,L2CR_L2DO@h
	b	2f
	/* When disabling L2, code must be in L1 */
	.balign 32
1:	mtspr	SPRN_L2CR,r3
3:	sync
	isync
	b	1f
2:	b	3f
3:	sync
	isync
	b	1b
1:	/* disp-flush L2. The interesting thing here is that the L2 can be
	 * up to 2Mb ... so using the ROM, we'll end up wrapping back to memory
	 * but that is probbaly fine. We disp-flush over 4Mb to be safe
	 */
	lis	r4,2
	mtctr	r4
	lis	r4,0xfff0
1:	lwz	r0,0(r4)
	addi	r4,r4,32
	bdnz	1b
	sync
	isync
	lis	r4,2
	mtctr	r4
	lis	r4,0xfff0
1:	dcbf	0,r4
	addi	r4,r4,32
	bdnz	1b
	sync
	isync

	/* now disable L2 */
	rlwinm	r5,r5,0,~L2CR_L2E
	b	2f
	/* When disabling L2, code must be in L1 */
	.balign 32
1:	mtspr	SPRN_L2CR,r5
3:	sync
	isync
	b	1f
2:	b	3f
3:	sync
	isync
	b	1b
1:	sync
	isync
	/* Invalidate L2. This is pre-745x, we clear the L2I bit ourselves */
	oris	r4,r5,L2CR_L2I@h
	mtspr	SPRN_L2CR,r4
	sync
	isync

	/* Wait for the invalidation to complete */
1:	mfspr	r3,SPRN_L2CR
	rlwinm.	r0,r3,0,31,31
	bne	1b

	/* Clear L2I */
	xoris	r4,r4,L2CR_L2I@h
	sync
	mtspr	SPRN_L2CR,r4
	sync

	/* now disable the L1 data cache */
	mfspr	r0,SPRN_HID0
	rlwinm	r0,r0,0,~(HID0_DCE|HID0_ICE)
	mtspr	SPRN_HID0,r0
	sync
	isync

	/* Restore HID0[DPM] to whatever it was before */
	sync
	mfspr	r0,SPRN_HID0
	rlwimi	r0,r8,0,11,11		/* Turn back HID0[DPM] */
	mtspr	SPRN_HID0,r0
	sync

	/* restore DR and EE */
	sync
	mtmsr	r11
	isync

	mtlr	r10
	blr
_ASM_NOKPROBE_SYMBOL(flush_disable_75x)

/* This code is for 745x processors */
flush_disable_745x:
	/* Turn off EE and DR in MSR */
	mfmsr	r11
	rlwinm	r0,r11,0,~MSR_EE
	rlwinm	r0,r0,0,~MSR_DR
	sync
	mtmsr	r0
	isync

	/* Stop prefetch streams */
	DSSALL
	sync

	/* Disable L2 prefetching */
	mfspr	r0,SPRN_MSSCR0
	rlwinm	r0,r0,0,0,29
	mtspr	SPRN_MSSCR0,r0
	sync
	isync
	lis	r4,0
	dcbf	0,r4
	dcbf	0,r4
	dcbf	0,r4
	dcbf	0,r4
	dcbf	0,r4
	dcbf	0,r4
	dcbf	0,r4
	dcbf	0,r4

	/* Due to a bug with the HW flush on some CPU revs, we occasionally
	 * experience data corruption. I'm adding a displacement flush along
	 * with a dcbf loop over a few Mb to "help". The problem isn't totally
	 * fixed by this in theory, but at least, in practice, I couldn't reproduce
	 * it even with a big hammer...
	 */

        lis     r4,0x0002
        mtctr   r4
 	li      r4,0
1:
        lwz     r0,0(r4)
        addi    r4,r4,32                /* Go to start of next cache line */
        bdnz    1b
        isync

        /* Now, flush the first 4MB of memory */
        lis     r4,0x0002
        mtctr   r4
	li      r4,0
        sync
1:
        dcbf    0,r4
        addi    r4,r4,32                /* Go to start of next cache line */
        bdnz    1b

	/* Flush and disable the L1 data cache */
	mfspr	r6,SPRN_LDSTCR
	lis	r3,0xfff0	/* read from ROM for displacement flush */
	li	r4,0xfe		/* start with only way 0 unlocked */
	li	r5,128		/* 128 lines in each way */
1:	mtctr	r5
	rlwimi	r6,r4,0,24,31
	mtspr	SPRN_LDSTCR,r6
	sync
	isync
2:	lwz	r0,0(r3)	/* touch each cache line */
	addi	r3,r3,32
	bdnz	2b
	rlwinm	r4,r4,1,24,30	/* move on to the next way */
	ori	r4,r4,1
	cmpwi	r4,0xff		/* all done? */
	bne	1b
	/* now unlock the L1 data cache */
	li	r4,0
	rlwimi	r6,r4,0,24,31
	sync
	mtspr	SPRN_LDSTCR,r6
	sync
	isync

	/* Flush the L2 cache using the hardware assist */
	mfspr	r3,SPRN_L2CR
	cmpwi	r3,0		/* check if it is enabled first */
	bge	4f
	oris	r0,r3,(L2CR_L2IO_745x|L2CR_L2DO_745x)@h
	b	2f
	/* When disabling/locking L2, code must be in L1 */
	.balign 32
1:	mtspr	SPRN_L2CR,r0	/* lock the L2 cache */
3:	sync
	isync
	b	1f
2:	b	3f
3:	sync
	isync
	b	1b
1:	sync
	isync
	ori	r0,r3,L2CR_L2HWF_745x
	sync
	mtspr	SPRN_L2CR,r0	/* set the hardware flush bit */
3:	mfspr	r0,SPRN_L2CR	/* wait for it to go to 0 */
	andi.	r0,r0,L2CR_L2HWF_745x
	bne	3b
	sync
	rlwinm	r3,r3,0,~L2CR_L2E
	b	2f
	/* When disabling L2, code must be in L1 */
	.balign 32
1:	mtspr	SPRN_L2CR,r3	/* disable the L2 cache */
3:	sync
	isync
	b	1f
2:	b	3f
3:	sync
	isync
	b	1b
1:	sync
	isync
	oris	r4,r3,L2CR_L2I@h
	mtspr	SPRN_L2CR,r4
	sync
	isync
1:	mfspr	r4,SPRN_L2CR
	andis.	r0,r4,L2CR_L2I@h
	bne	1b
	sync

BEGIN_FTR_SECTION
	/* Flush the L3 cache using the hardware assist */
4:	mfspr	r3,SPRN_L3CR
	cmpwi	r3,0		/* check if it is enabled */
	bge	6f
	oris	r0,r3,L3CR_L3IO@h
	ori	r0,r0,L3CR_L3DO
	sync
	mtspr	SPRN_L3CR,r0	/* lock the L3 cache */
	sync
	isync
	ori	r0,r0,L3CR_L3HWF
	sync
	mtspr	SPRN_L3CR,r0	/* set the hardware flush bit */
5:	mfspr	r0,SPRN_L3CR	/* wait for it to go to zero */
	andi.	r0,r0,L3CR_L3HWF
	bne	5b
	rlwinm	r3,r3,0,~L3CR_L3E
	sync
	mtspr	SPRN_L3CR,r3	/* disable the L3 cache */
	sync
	ori	r4,r3,L3CR_L3I
	mtspr	SPRN_L3CR,r4
1:	mfspr	r4,SPRN_L3CR
	andi.	r0,r4,L3CR_L3I
	bne	1b
	sync
END_FTR_SECTION_IFSET(CPU_FTR_L3CR)

6:	mfspr	r0,SPRN_HID0	/* now disable the L1 data cache */
	rlwinm	r0,r0,0,~HID0_DCE
	mtspr	SPRN_HID0,r0
	sync
	isync
	mtmsr	r11		/* restore DR and EE */
	isync
	blr
_ASM_NOKPROBE_SYMBOL(flush_disable_745x)
#endif	/* CONFIG_PPC_BOOK3S_32 */
