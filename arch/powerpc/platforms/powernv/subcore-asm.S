/*
 * Copyright 2013, Michael (Ellerman|Neuling), IBM Corporation.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <asm/asm-offsets.h>
#include <asm/ppc_asm.h>
#include <asm/reg.h>

#include "subcore.h"


_GLOBAL(split_core_secondary_loop)
	/*
	 * r3 = u8 *state, used throughout the routine
	 * r4 = temp
	 * r5 = temp
	 * ..
	 * r12 = MSR
	 */
	mfmsr	r12

	/* Disable interrupts so SRR0/1 don't get trashed */
	li	r4,0
	ori	r4,r4,MSR_EE|MSR_SE|MSR_BE|MSR_RI
	andc	r4,r12,r4
	sync
	mtmsrd	r4

	/* Switch to real mode and leave interrupts off */
	li	r5, MSR_IR|MSR_DR
	andc	r5, r4, r5

	LOAD_REG_ADDR(r4, real_mode)

	mtspr	SPRN_SRR0,r4
	mtspr	SPRN_SRR1,r5
	rfid
	b	.	/* prevent speculative execution */

real_mode:
	/* Grab values from unsplit SPRs */
	mfspr	r6,  SPRN_LDBAR
	mfspr	r7,  SPRN_PMMAR
	mfspr	r8,  SPRN_PMCR
	mfspr	r9,  SPRN_RPR
	mfspr	r10, SPRN_SDR1

	/* Order reading the SPRs vs telling the primary we are ready to split */
	sync

	/* Tell thread 0 we are in real mode */
	li	r4, SYNC_STEP_REAL_MODE
	stb	r4, 0(r3)

	li	r5, (HID0_POWER8_4LPARMODE | HID0_POWER8_2LPARMODE)@highest
	sldi	r5, r5, 48

	/* Loop until we see the split happen in HID0 */
1:	mfspr	r4, SPRN_HID0
	and.	r4, r4, r5
	beq	1b

	/*
	 * We only need to initialise the below regs once for each subcore,
	 * but it's simpler and harmless to do it on each thread.
	 */

	/* Make sure various SPRS have sane values */
	li	r4, 0
	mtspr	SPRN_LPID, r4
	mtspr	SPRN_PCR, r4
	mtspr	SPRN_HDEC, r4

	/* Restore SPR values now we are split */
	mtspr	SPRN_LDBAR, r6
	mtspr	SPRN_PMMAR, r7
	mtspr	SPRN_PMCR, r8
	mtspr	SPRN_RPR, r9
	mtspr	SPRN_SDR1, r10

	LOAD_REG_ADDR(r5, virtual_mode)

	/* Get out of real mode */
	mtspr	SPRN_SRR0,r5
	mtspr	SPRN_SRR1,r12
	rfid
	b	.	/* prevent speculative execution */

virtual_mode:
	blr
