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

#include <asm/processor.h>
#include <asm/page.h>
#include <asm/cputable.h>
#include <asm/ppc_asm.h>
#include <asm/asm-offsets.h>
#include <asm/cache.h>

/* Entry: r3 = crap, r4 = ptr to cputable entry
 *
 * Note that we can be called twice for pseudo-PVRs
 */
_GLOBAL(__setup_cpu_power7)
	mflr	r11
	bl	__init_hvmode_206
	mtlr	r11
	beqlr
	bl	__init_LPCR
	mtlr	r11
	blr

_GLOBAL(__restore_cpu_power7)
	mflr	r11
	mfmsr	r3
	rldicl.	r0,r3,4,63
	beqlr
	bl	__init_LPCR
	mtlr	r11
	blr

__init_hvmode_206:
	/* Disable CPU_FTR_HVMODE_206 and exit if MSR:HV is not set */
	mfmsr	r3
	rldicl.	r0,r3,4,63
	bnelr
	ld	r5,CPU_SPEC_FEATURES(r4)
	LOAD_REG_IMMEDIATE(r6,CPU_FTR_HVMODE_206)
	xor	r5,r5,r6
	std	r5,CPU_SPEC_FEATURES(r4)
	blr

__init_LPCR:
	/* Setup a sane LPCR:
	 *
	 *   LPES = 0b01 (HSRR0/1 used for 0x500)
	 *   PECE = 0b111
	 *
	 * Other bits untouched for now
	 */
	mfspr	r3,SPRN_LPCR
	ori	r3,r3,(LPCR_LPES0|LPCR_LPES1)
	xori	r3,r3, LPCR_LPES0
	ori	r3,r3,(LPCR_PECE0|LPCR_PECE1|LPCR_PECE2)
	mtspr	SPRN_LPCR,r3
	isync
	blr
