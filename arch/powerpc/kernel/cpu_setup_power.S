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
	li	r0,0
	mtspr	SPRN_LPID,r0
	mfspr	r3,SPRN_LPCR
	bl	__init_LPCR
	bl	__init_TLB
	mtlr	r11
	blr

_GLOBAL(__restore_cpu_power7)
	mflr	r11
	mfmsr	r3
	rldicl.	r0,r3,4,63
	beqlr
	li	r0,0
	mtspr	SPRN_LPID,r0
	mfspr	r3,SPRN_LPCR
	bl	__init_LPCR
	bl	__init_TLB
	mtlr	r11
	blr

_GLOBAL(__setup_cpu_power8)
	mflr	r11
	bl	__init_hvmode_206
	mtlr	r11
	beqlr
	li	r0,0
	mtspr	SPRN_LPID,r0
	mfspr	r3,SPRN_LPCR
	oris	r3, r3, LPCR_AIL_3@h
	bl	__init_LPCR
	bl	__init_TLB
	mtlr	r11
	blr

_GLOBAL(__restore_cpu_power8)
	mflr	r11
	mfmsr	r3
	rldicl.	r0,r3,4,63
	beqlr
	li	r0,0
	mtspr	SPRN_LPID,r0
	mfspr   r3,SPRN_LPCR
	oris	r3, r3, LPCR_AIL_3@h
	bl	__init_LPCR
	bl	__init_TLB
	mtlr	r11
	blr

__init_hvmode_206:
	/* Disable CPU_FTR_HVMODE and exit if MSR:HV is not set */
	mfmsr	r3
	rldicl.	r0,r3,4,63
	bnelr
	ld	r5,CPU_SPEC_FEATURES(r4)
	LOAD_REG_IMMEDIATE(r6,CPU_FTR_HVMODE)
	xor	r5,r5,r6
	std	r5,CPU_SPEC_FEATURES(r4)
	blr

__init_LPCR:
	/* Setup a sane LPCR:
	 *   Called with initial LPCR in R3
	 *
	 *   LPES = 0b01 (HSRR0/1 used for 0x500)
	 *   PECE = 0b111
	 *   DPFD = 4
	 *   HDICE = 0
	 *   VC = 0b100 (VPM0=1, VPM1=0, ISL=0)
	 *   VRMASD = 0b10000 (L=1, LP=00)
	 *
	 * Other bits untouched for now
	 */
	li	r5,1
	rldimi	r3,r5, LPCR_LPES_SH, 64-LPCR_LPES_SH-2
	ori	r3,r3,(LPCR_PECE0|LPCR_PECE1|LPCR_PECE2)
	li	r5,4
	rldimi	r3,r5, LPCR_DPFD_SH, 64-LPCR_DPFD_SH-3
	clrrdi	r3,r3,1		/* clear HDICE */
	li	r5,4
	rldimi	r3,r5, LPCR_VC_SH, 0
	li	r5,0x10
	rldimi	r3,r5, LPCR_VRMASD_SH, 64-LPCR_VRMASD_SH-5
	mtspr	SPRN_LPCR,r3
	isync
	blr

__init_TLB:
	/* Clear the TLB */
	li	r6,128
	mtctr	r6
	li	r7,0xc00	/* IS field = 0b11 */
	ptesync
2:	tlbiel	r7
	addi	r7,r7,0x1000
	bdnz	2b
	ptesync
1:	blr
