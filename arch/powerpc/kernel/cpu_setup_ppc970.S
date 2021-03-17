/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * This file contains low level CPU setup functions.
 *    Copyright (C) 2003 Benjamin Herrenschmidt (benh@kernel.crashing.org)
 */

#include <asm/processor.h>
#include <asm/page.h>
#include <asm/cputable.h>
#include <asm/ppc_asm.h>
#include <asm/asm-offsets.h>
#include <asm/cache.h>

_GLOBAL(__cpu_preinit_ppc970)
	/* Do nothing if not running in HV mode */
	mfmsr	r0
	rldicl.	r0,r0,4,63
	beqlr

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


_GLOBAL(__setup_cpu_ppc970)
	/* Do nothing if not running in HV mode */
	mfmsr	r0
	rldicl.	r0,r0,4,63
	beq	no_hv_mode

	mfspr	r0,SPRN_HID0
	li	r11,5			/* clear DOZE and SLEEP */
	rldimi	r0,r11,52,8		/* set NAP and DPM */
	li	r11,0
	rldimi	r0,r11,32,31		/* clear EN_ATTN */
	b	load_hids		/* Jump to shared code */


_GLOBAL(__setup_cpu_ppc970MP)
	/* Do nothing if not running in HV mode */
	mfmsr	r0
	rldicl.	r0,r0,4,63
	beq	no_hv_mode

	mfspr	r0,SPRN_HID0
	li	r11,0x15		/* clear DOZE and SLEEP */
	rldimi	r0,r11,52,6		/* set DEEPNAP, NAP and DPM */
	li	r11,0
	rldimi	r0,r11,32,31		/* clear EN_ATTN */

load_hids:
	mtspr	SPRN_HID0,r0
	mfspr	r0,SPRN_HID0
	mfspr	r0,SPRN_HID0
	mfspr	r0,SPRN_HID0
	mfspr	r0,SPRN_HID0
	mfspr	r0,SPRN_HID0
	mfspr	r0,SPRN_HID0
	sync
	isync

	/* Try to set LPES = 01 in HID4 */
	mfspr	r0,SPRN_HID4
	clrldi	r0,r0,1			/* clear LPES0 */
	ori	r0,r0,HID4_LPES1	/* set LPES1 */
	sync
	mtspr	SPRN_HID4,r0
	isync

	/* Save away cpu state */
	LOAD_REG_ADDR(r5,cpu_state_storage)

	/* Save HID0,1,4 and 5 */
	mfspr	r3,SPRN_HID0
	std	r3,CS_HID0(r5)
	mfspr	r3,SPRN_HID1
	std	r3,CS_HID1(r5)
	mfspr	r4,SPRN_HID4
	std	r4,CS_HID4(r5)
	mfspr	r3,SPRN_HID5
	std	r3,CS_HID5(r5)

	/* See if we successfully set LPES1 to 1; if not we are in Apple mode */
	andi.	r4,r4,HID4_LPES1
	bnelr

no_hv_mode:
	/* Disable CPU_FTR_HVMODE and exit, since we don't have HV mode */
	ld	r5,CPU_SPEC_FEATURES(r4)
	LOAD_REG_IMMEDIATE(r6,CPU_FTR_HVMODE)
	andc	r5,r5,r6
	std	r5,CPU_SPEC_FEATURES(r4)
	blr

/* Called with no MMU context (typically MSR:IR/DR off) to
 * restore CPU state as backed up by the previous
 * function. This does not include cache setting
 */
_GLOBAL(__restore_cpu_ppc970)
	/* Do nothing if not running in HV mode */
	mfmsr	r0
	rldicl.	r0,r0,4,63
	beqlr

	LOAD_REG_ADDR(r5,cpu_state_storage)
	/* Before accessing memory, we make sure rm_ci is clear */
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

