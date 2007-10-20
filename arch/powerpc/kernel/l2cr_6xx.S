/*
	L2CR functions
	Copyright © 1997-1998 by PowerLogix R & D, Inc.

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/
/*
	Thur, Dec. 12, 1998.
	- First public release, contributed by PowerLogix.
	***********
	Sat, Aug. 7, 1999.
	- Terry: Made sure code disabled interrupts before running. (Previously
			it was assumed interrupts were already disabled).
	- Terry: Updated for tentative G4 support.  4MB of memory is now flushed
			instead of 2MB.  (Prob. only 3 is necessary).
	- Terry: Updated for workaround to HID0[DPM] processor bug
			during global invalidates.
	***********
	Thu, July 13, 2000.
	- Terry: Added isync to correct for an errata.

	22 August 2001.
	- DanM: Finally added the 7450 patch I've had for the past
		several months.  The L2CR is similar, but I'm going
		to assume the user of this functions knows what they
		are doing.

	Author:	Terry Greeniaus (tgree@phys.ualberta.ca)
	Please e-mail updates to this file to me, thanks!
*/
#include <asm/processor.h>
#include <asm/cputable.h>
#include <asm/ppc_asm.h>
#include <asm/cache.h>
#include <asm/page.h>

/* Usage:

	When setting the L2CR register, you must do a few special
	things.  If you are enabling the cache, you must perform a
	global invalidate.  If you are disabling the cache, you must
	flush the cache contents first.  This routine takes care of
	doing these things.  When first enabling the cache, make sure
	you pass in the L2CR you want, as well as passing in the
	global invalidate bit set.  A global invalidate will only be
	performed if the L2I bit is set in applyThis.  When enabling
	the cache, you should also set the L2E bit in applyThis.  If
	you want to modify the L2CR contents after the cache has been
	enabled, the recommended procedure is to first call
	__setL2CR(0) to disable the cache and then call it again with
	the new values for L2CR.  Examples:

	_setL2CR(0)		- disables the cache
	_setL2CR(0xB3A04000)	- enables my G3 upgrade card:
				- L2E set to turn on the cache
				- L2SIZ set to 1MB
				- L2CLK set to 1:1
				- L2RAM set to pipelined synchronous late-write
				- L2I set to perform a global invalidation
				- L2OH set to 0.5 nS
				- L2DF set because this upgrade card
				  requires it

	A similar call should work for your card.  You need to know
	the correct setting for your card and then place them in the
	fields I have outlined above.  Other fields support optional
	features, such as L2DO which caches only data, or L2TS which
	causes cache pushes from the L1 cache to go to the L2 cache
	instead of to main memory.

IMPORTANT:
	Starting with the 7450, the bits in this register have moved
	or behave differently.  The Enable, Parity Enable, Size,
	and L2 Invalidate are the only bits that have not moved.
	The size is read-only for these processors with internal L2
	cache, and the invalidate is a control as well as status.
		-- Dan

*/
/*
 * Summary: this procedure ignores the L2I bit in the value passed in,
 * flushes the cache if it was already enabled, always invalidates the
 * cache, then enables the cache if the L2E bit is set in the value
 * passed in.
 *   -- paulus.
 */
_GLOBAL(_set_L2CR)
	/* Make sure this is a 750 or 7400 chip */
BEGIN_FTR_SECTION
	li	r3,-1
	blr
END_FTR_SECTION_IFCLR(CPU_FTR_L2CR)

	mflr	r9

	/* Stop DST streams */
BEGIN_FTR_SECTION
	DSSALL
	sync
END_FTR_SECTION_IFSET(CPU_FTR_ALTIVEC)

	/* Turn off interrupts and data relocation. */
	mfmsr	r7		/* Save MSR in r7 */
	rlwinm	r4,r7,0,17,15
	rlwinm	r4,r4,0,28,26	/* Turn off DR bit */
	sync
	mtmsr	r4
	isync

	/* Before we perform the global invalidation, we must disable dynamic
	 * power management via HID0[DPM] to work around a processor bug where
	 * DPM can possibly interfere with the state machine in the processor
	 * that invalidates the L2 cache tags.
	 */
	mfspr	r8,SPRN_HID0		/* Save HID0 in r8 */
	rlwinm	r4,r8,0,12,10		/* Turn off HID0[DPM] */
	sync
	mtspr	SPRN_HID0,r4		/* Disable DPM */
	sync

	/* Get the current enable bit of the L2CR into r4 */
	mfspr	r4,SPRN_L2CR

	/* Tweak some bits */
	rlwinm	r5,r3,0,0,0		/* r5 contains the new enable bit */
	rlwinm	r3,r3,0,11,9		/* Turn off the invalidate bit */
	rlwinm	r3,r3,0,1,31		/* Turn off the enable bit */

	/* Check to see if we need to flush */
	rlwinm.	r4,r4,0,0,0
	beq	2f

	/* Flush the cache. First, read the first 4MB of memory (physical) to
	 * put new data in the cache.  (Actually we only need
	 * the size of the L2 cache plus the size of the L1 cache, but 4MB will
	 * cover everything just to be safe).
	 */

	 /**** Might be a good idea to set L2DO here - to prevent instructions
	       from getting into the cache.  But since we invalidate
	       the next time we enable the cache it doesn't really matter.
	       Don't do this unless you accomodate all processor variations.
	       The bit moved on the 7450.....
	  ****/

BEGIN_FTR_SECTION
	/* Disable L2 prefetch on some 745x and try to ensure
	 * L2 prefetch engines are idle. As explained by errata
	 * text, we can't be sure they are, we just hope very hard
	 * that well be enough (sic !). At least I noticed Apple
	 * doesn't even bother doing the dcbf's here...
	 */
	mfspr	r4,SPRN_MSSCR0
	rlwinm	r4,r4,0,0,29
	sync
	mtspr	SPRN_MSSCR0,r4
	sync
	isync
	lis	r4,KERNELBASE@h
	dcbf	0,r4
	dcbf	0,r4
	dcbf	0,r4
	dcbf	0,r4
END_FTR_SECTION_IFSET(CPU_FTR_SPEC7450)

	/* TODO: use HW flush assist when available */

	lis	r4,0x0002
	mtctr	r4
	li	r4,0
1:
	lwzx	r0,r0,r4
	addi	r4,r4,32		/* Go to start of next cache line */
	bdnz	1b
	isync

	/* Now, flush the first 4MB of memory */
	lis	r4,0x0002
	mtctr	r4
	li	r4,0
	sync
1:
	dcbf	0,r4
	addi	r4,r4,32		/* Go to start of next cache line */
	bdnz	1b

2:
	/* Set up the L2CR configuration bits (and switch L2 off) */
	/* CPU errata: Make sure the mtspr below is already in the
	 * L1 icache
	 */
	b	20f
	.balign	L1_CACHE_BYTES
22:
	sync
	mtspr	SPRN_L2CR,r3
	sync
	b	23f
20:
	b	21f
21:	sync
	isync
	b	22b

23:
	/* Perform a global invalidation */
	oris	r3,r3,0x0020
	sync
	mtspr	SPRN_L2CR,r3
	sync
	isync				/* For errata */

BEGIN_FTR_SECTION
	/* On the 7450, we wait for the L2I bit to clear......
	*/
10:	mfspr	r3,SPRN_L2CR
	andis.	r4,r3,0x0020
	bne	10b
	b	11f
END_FTR_SECTION_IFSET(CPU_FTR_SPEC7450)

	/* Wait for the invalidation to complete */
3:	mfspr	r3,SPRN_L2CR
	rlwinm.	r4,r3,0,31,31
	bne	3b

11:	rlwinm	r3,r3,0,11,9		/* Turn off the L2I bit */
	sync
	mtspr	SPRN_L2CR,r3
	sync

	/* See if we need to enable the cache */
	cmplwi	r5,0
	beq	4f

	/* Enable the cache */
	oris	r3,r3,0x8000
	mtspr	SPRN_L2CR,r3
	sync
	
	/* Enable L2 HW prefetch on 744x/745x */
BEGIN_FTR_SECTION
	mfspr	r3,SPRN_MSSCR0
	ori	r3,r3,3
	sync
	mtspr	SPRN_MSSCR0,r3
	sync
	isync
END_FTR_SECTION_IFSET(CPU_FTR_SPEC7450)
4:

	/* Restore HID0[DPM] to whatever it was before */
	sync
	mtspr	1008,r8
	sync

	/* Restore MSR (restores EE and DR bits to original state) */
	SYNC
	mtmsr	r7
	isync

	mtlr	r9
	blr

_GLOBAL(_get_L2CR)
	/* Return the L2CR contents */
	li	r3,0
BEGIN_FTR_SECTION
	mfspr	r3,SPRN_L2CR
END_FTR_SECTION_IFSET(CPU_FTR_L2CR)
	blr


/*
 * Here is a similar routine for dealing with the L3 cache
 * on the 745x family of chips
 */

_GLOBAL(_set_L3CR)
	/* Make sure this is a 745x chip */
BEGIN_FTR_SECTION
	li	r3,-1
	blr
END_FTR_SECTION_IFCLR(CPU_FTR_L3CR)

	/* Turn off interrupts and data relocation. */
	mfmsr	r7		/* Save MSR in r7 */
	rlwinm	r4,r7,0,17,15
	rlwinm	r4,r4,0,28,26	/* Turn off DR bit */
	sync
	mtmsr	r4
	isync

	/* Stop DST streams */
	DSSALL
	sync

	/* Get the current enable bit of the L3CR into r4 */
	mfspr	r4,SPRN_L3CR

	/* Tweak some bits */
	rlwinm	r5,r3,0,0,0		/* r5 contains the new enable bit */
	rlwinm	r3,r3,0,22,20		/* Turn off the invalidate bit */
	rlwinm	r3,r3,0,2,31		/* Turn off the enable & PE bits */
	rlwinm	r3,r3,0,5,3		/* Turn off the clken bit */
	/* Check to see if we need to flush */
	rlwinm.	r4,r4,0,0,0
	beq	2f

	/* Flush the cache.
	 */

	/* TODO: use HW flush assist */

	lis	r4,0x0008
	mtctr	r4
	li	r4,0
1:
	lwzx	r0,r0,r4
	dcbf	0,r4
	addi	r4,r4,32		/* Go to start of next cache line */
	bdnz	1b

2:
	/* Set up the L3CR configuration bits (and switch L3 off) */
	sync
	mtspr	SPRN_L3CR,r3
	sync

	oris	r3,r3,L3CR_L3RES@h		/* Set reserved bit 5 */
	mtspr	SPRN_L3CR,r3
	sync
	oris	r3,r3,L3CR_L3CLKEN@h		/* Set clken */
	mtspr	SPRN_L3CR,r3
	sync

	/* Wait for stabilize */
	li	r0,256
	mtctr	r0
1:	bdnz	1b

	/* Perform a global invalidation */
	ori	r3,r3,0x0400
	sync
	mtspr	SPRN_L3CR,r3
	sync
	isync

	/* We wait for the L3I bit to clear...... */
10:	mfspr	r3,SPRN_L3CR
	andi.	r4,r3,0x0400
	bne	10b

	/* Clear CLKEN */
	rlwinm	r3,r3,0,5,3		/* Turn off the clken bit */
	mtspr	SPRN_L3CR,r3
	sync

	/* Wait for stabilize */
	li	r0,256
	mtctr	r0
1:	bdnz	1b

	/* See if we need to enable the cache */
	cmplwi	r5,0
	beq	4f

	/* Enable the cache */
	oris	r3,r3,(L3CR_L3E | L3CR_L3CLKEN)@h
	mtspr	SPRN_L3CR,r3
	sync

	/* Wait for stabilize */
	li	r0,256
	mtctr	r0
1:	bdnz	1b

	/* Restore MSR (restores EE and DR bits to original state) */
4:	SYNC
	mtmsr	r7
	isync
	blr

_GLOBAL(_get_L3CR)
	/* Return the L3CR contents */
	li	r3,0
BEGIN_FTR_SECTION
	mfspr	r3,SPRN_L3CR
END_FTR_SECTION_IFSET(CPU_FTR_L3CR)
	blr

/* --- End of PowerLogix code ---
 */


/* flush_disable_L1()	- Flush and disable L1 cache
 *
 * clobbers r0, r3, ctr, cr0
 * Must be called with interrupts disabled and MMU enabled.
 */
_GLOBAL(__flush_disable_L1)
	/* Stop pending alitvec streams and memory accesses */
BEGIN_FTR_SECTION
	DSSALL
END_FTR_SECTION_IFSET(CPU_FTR_ALTIVEC)
 	sync

	/* Load counter to 0x4000 cache lines (512k) and
	 * load cache with datas
	 */
	li	r3,0x4000	/* 512kB / 32B */
	mtctr	r3
	lis	r3,KERNELBASE@h
1:
	lwz	r0,0(r3)
	addi	r3,r3,0x0020	/* Go to start of next cache line */
	bdnz	1b
	isync
	sync

	/* Now flush those cache lines */
	li	r3,0x4000	/* 512kB / 32B */
	mtctr	r3
	lis	r3,KERNELBASE@h
1:
	dcbf	0,r3
	addi	r3,r3,0x0020	/* Go to start of next cache line */
	bdnz	1b
	sync

	/* We can now disable the L1 cache (HID0:DCE, HID0:ICE) */
	mfspr	r3,SPRN_HID0
	rlwinm	r3,r3,0,18,15
	mtspr	SPRN_HID0,r3
	sync
	isync
 	blr

/* inval_enable_L1	- Invalidate and enable L1 cache
 *
 * Assumes L1 is already disabled and MSR:EE is off
 *
 * clobbers r3
 */
_GLOBAL(__inval_enable_L1)
	/* Enable and then Flash inval the instruction & data cache */
	mfspr	r3,SPRN_HID0
	ori	r3,r3, HID0_ICE|HID0_ICFI|HID0_DCE|HID0_DCI
	sync
	isync
	mtspr	SPRN_HID0,r3
	xori	r3,r3, HID0_ICFI|HID0_DCI
	mtspr	SPRN_HID0,r3
	sync

 	blr


