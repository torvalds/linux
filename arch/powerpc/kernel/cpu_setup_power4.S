/*
 * This file contains low level CPU setup functions.
 *    Copyright (C) 2003 Benjamin Herrenschmidt (benh@kernel.crashing.org)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 */

#include <linux/config.h>
#include <asm/processor.h>
#include <asm/page.h>
#include <asm/cputable.h>
#include <asm/ppc_asm.h>
#include <asm/asm-offsets.h>
#include <asm/cache.h>

_GLOBAL(__970_cpu_preinit)
	/*
	 * Do nothing if not running in HV mode
	 */
	mfmsr	r0
	rldicl.	r0,r0,4,63
	beqlr

	/*
	 * Deal only with PPC970 and PPC970FX.
	 */
	mfspr	r0,SPRN_PVR
	srwi	r0,r0,16
	cmpwi	r0,0x39
	beq	1f
	cmpwi	r0,0x3c
	beq	1f
	cmpwi	r0,0x44
	bnelr
1:

	/* Make sure HID4:rm_ci is off before MMU is turned off, that large
	 * pages are enabled with HID4:61 and clear HID5:DCBZ_size and
	 * HID5:DCBZ32_ill
	 */
	li	r0,0
	mfspr	r3,SPRN_HID4
	rldimi	r3,r0,40,23	/* clear bit 23 (rm_ci) */
	rldimi	r3,r0,2,61	/* clear bit 61 (lg_pg_en) */
	sync
	mtspr	SPRN_HID4,r3
	isync
	sync
	mfspr	r3,SPRN_HID5
	rldimi	r3,r0,6,56	/* clear bits 56 & 57 (DCBZ*) */
	sync
	mtspr	SPRN_HID5,r3
	isync
	sync

	/* Setup some basic HID1 features */
	mfspr	r0,SPRN_HID1
	li	r3,0x1200		/* enable i-fetch cacheability */
	sldi	r3,r3,44		/* and prefetch */
	or	r0,r0,r3
	mtspr	SPRN_HID1,r0
	mtspr	SPRN_HID1,r0
	isync

	/* Clear HIOR */
	li	r0,0
	sync
	mtspr	SPRN_HIOR,0		/* Clear interrupt prefix */
	isync
	blr

_GLOBAL(__setup_cpu_power4)
	blr

_GLOBAL(__setup_cpu_be)
        /* Set large page sizes LP=0: 16MB, LP=1: 64KB */
        addi    r3, 0,  0
        ori     r3, r3, HID6_LB
        sldi    r3, r3, 32
        nor     r3, r3, r3
        mfspr   r4, SPRN_HID6
        and     r4, r4, r3
        addi    r3, 0, 0x02000
        sldi    r3, r3, 32
        or      r4, r4, r3
        mtspr   SPRN_HID6, r4
	blr

_GLOBAL(__setup_cpu_ppc970)
	mfspr	r0,SPRN_HID0
	li	r11,5			/* clear DOZE and SLEEP */
	rldimi	r0,r11,52,8		/* set NAP and DPM */
	mtspr	SPRN_HID0,r0
	mfspr	r0,SPRN_HID0
	mfspr	r0,SPRN_HID0
	mfspr	r0,SPRN_HID0
	mfspr	r0,SPRN_HID0
	mfspr	r0,SPRN_HID0
	mfspr	r0,SPRN_HID0
	sync
	isync
	blr

/* Definitions for the table use to save CPU states */
#define CS_HID0		0
#define CS_HID1		8
#define	CS_HID4		16
#define CS_HID5		24
#define CS_SIZE		32

	.data
	.balign	L1_CACHE_BYTES,0
cpu_state_storage:
	.space	CS_SIZE
	.balign	L1_CACHE_BYTES,0
	.text

/* Called in normal context to backup CPU 0 state. This
 * does not include cache settings. This function is also
 * called for machine sleep. This does not include the MMU
 * setup, BATs, etc... but rather the "special" registers
 * like HID0, HID1, HID4, etc...
 */
_GLOBAL(__save_cpu_setup)
	/* Some CR fields are volatile, we back it up all */
	mfcr	r7

	/* Get storage ptr */
	LOADADDR(r5,cpu_state_storage)

	/* We only deal with 970 for now */
	mfspr	r0,SPRN_PVR
	srwi	r0,r0,16
	cmpwi	r0,0x39
	beq	1f
	cmpwi	r0,0x3c
	beq	1f
	cmpwi	r0,0x44
	bne	2f

1:	/* Save HID0,1,4 and 5 */
	mfspr	r3,SPRN_HID0
	std	r3,CS_HID0(r5)
	mfspr	r3,SPRN_HID1
	std	r3,CS_HID1(r5)
	mfspr	r3,SPRN_HID4
	std	r3,CS_HID4(r5)
	mfspr	r3,SPRN_HID5
	std	r3,CS_HID5(r5)

2:
	mtcr	r7
	blr

/* Called with no MMU context (typically MSR:IR/DR off) to
 * restore CPU state as backed up by the previous
 * function. This does not include cache setting
 */
_GLOBAL(__restore_cpu_setup)
	/* Get storage ptr (FIXME when using anton reloc as we
	 * are running with translation disabled here
	 */
	LOADADDR(r5,cpu_state_storage)

	/* We only deal with 970 for now */
	mfspr	r0,SPRN_PVR
	srwi	r0,r0,16
	cmpwi	r0,0x39
	beq	1f
	cmpwi	r0,0x3c
	beq	1f
	cmpwi	r0,0x44
	bnelr

1:	/* Before accessing memory, we make sure rm_ci is clear */
	li	r0,0
	mfspr	r3,SPRN_HID4
	rldimi	r3,r0,40,23	/* clear bit 23 (rm_ci) */
	sync
	mtspr	SPRN_HID4,r3
	isync
	sync

	/* Clear interrupt prefix */
	li	r0,0
	sync
	mtspr	SPRN_HIOR,0
	isync

	/* Restore HID0 */
	ld	r3,CS_HID0(r5)
	sync
	isync
	mtspr	SPRN_HID0,r3
	mfspr	r3,SPRN_HID0
	mfspr	r3,SPRN_HID0
	mfspr	r3,SPRN_HID0
	mfspr	r3,SPRN_HID0
	mfspr	r3,SPRN_HID0
	mfspr	r3,SPRN_HID0
	sync
	isync

	/* Restore HID1 */
	ld	r3,CS_HID1(r5)
	sync
	isync
	mtspr	SPRN_HID1,r3
	mtspr	SPRN_HID1,r3
	sync
	isync

	/* Restore HID4 */
	ld	r3,CS_HID4(r5)
	sync
	isync
	mtspr	SPRN_HID4,r3
	sync
	isync

	/* Restore HID5 */
	ld	r3,CS_HID5(r5)
	sync
	isync
	mtspr	SPRN_HID5,r3
	sync
	isync
	blr

