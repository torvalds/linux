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
#include <asm/book3s/64/mmu-hash.h>

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
	bl	__init_tlb_power7
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
	bl	__init_tlb_power7
	mtlr	r11
	blr

_GLOBAL(__setup_cpu_power8)
	mflr	r11
	bl	__init_FSCR
	bl	__init_PMU
	bl	__init_PMU_ISA207
	bl	__init_hvmode_206
	mtlr	r11
	beqlr
	li	r0,0
	mtspr	SPRN_LPID,r0
	mfspr	r3,SPRN_LPCR
	ori	r3, r3, LPCR_PECEDH
	bl	__init_LPCR
	bl	__init_HFSCR
	bl	__init_tlb_power8
	bl	__init_PMU_HV
	bl	__init_PMU_HV_ISA207
	mtlr	r11
	blr

_GLOBAL(__restore_cpu_power8)
	mflr	r11
	bl	__init_FSCR
	bl	__init_PMU
	bl	__init_PMU_ISA207
	mfmsr	r3
	rldicl.	r0,r3,4,63
	mtlr	r11
	beqlr
	li	r0,0
	mtspr	SPRN_LPID,r0
	mfspr   r3,SPRN_LPCR
	ori	r3, r3, LPCR_PECEDH
	bl	__init_LPCR
	bl	__init_HFSCR
	bl	__init_tlb_power8
	bl	__init_PMU_HV
	bl	__init_PMU_HV_ISA207
	mtlr	r11
	blr

_GLOBAL(__setup_cpu_power9)
	mflr	r11
	bl	__init_FSCR
	bl	__init_PMU
	bl	__init_hvmode_206
	mtlr	r11
	beqlr
	li	r0,0
	mtspr	SPRN_LPID,r0
	mfspr	r3,SPRN_LPCR
	LOAD_REG_IMMEDIATE(r4, LPCR_PECEDH | LPCR_PECE_HVEE | LPCR_HVICE)
	or	r3, r3, r4
	LOAD_REG_IMMEDIATE(r4, LPCR_UPRT | LPCR_HR)
	andc	r3, r3, r4
	bl	__init_LPCR
	bl	__init_HFSCR
	bl	__init_tlb_power9
	bl	__init_PMU_HV
	mtlr	r11
	blr

_GLOBAL(__restore_cpu_power9)
	mflr	r11
	bl	__init_FSCR
	bl	__init_PMU
	mfmsr	r3
	rldicl.	r0,r3,4,63
	mtlr	r11
	beqlr
	li	r0,0
	mtspr	SPRN_LPID,r0
	mfspr   r3,SPRN_LPCR
	LOAD_REG_IMMEDIATE(r4, LPCR_PECEDH | LPCR_PECE_HVEE | LPCR_HVICE)
	or	r3, r3, r4
	LOAD_REG_IMMEDIATE(r4, LPCR_UPRT | LPCR_HR)
	andc	r3, r3, r4
	bl	__init_LPCR
	bl	__init_HFSCR
	bl	__init_tlb_power9
	bl	__init_PMU_HV
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

__init_FSCR:
	mfspr	r3,SPRN_FSCR
	ori	r3,r3,FSCR_TAR|FSCR_DSCR|FSCR_EBB
	mtspr	SPRN_FSCR,r3
	blr

__init_HFSCR:
	mfspr	r3,SPRN_HFSCR
	ori	r3,r3,HFSCR_TAR|HFSCR_TM|HFSCR_BHRB|HFSCR_PM|\
		      HFSCR_DSCR|HFSCR_VECVSX|HFSCR_FP|HFSCR_EBB
	mtspr	SPRN_HFSCR,r3
	blr

/*
 * Clear the TLB using the specified IS form of tlbiel instruction
 * (invalidate by congruence class). P7 has 128 CCs., P8 has 512.
 */
__init_tlb_power7:
	li	r6,POWER7_TLB_SETS
	mtctr	r6
	li	r7,0xc00	/* IS field = 0b11 */
	ptesync
2:	tlbiel	r7
	addi	r7,r7,0x1000
	bdnz	2b
	ptesync
1:	blr

__init_tlb_power8:
	li	r6,POWER8_TLB_SETS
	mtctr	r6
	li	r7,0xc00	/* IS field = 0b11 */
	ptesync
2:	tlbiel	r7
	addi	r7,r7,0x1000
	bdnz	2b
	ptesync
1:	blr

__init_tlb_power9:
	li	r6,POWER9_TLB_SETS_HASH
	mtctr	r6
	li	r7,0xc00	/* IS field = 0b11 */
	ptesync
2:	tlbiel	r7
	addi	r7,r7,0x1000
	bdnz	2b
	ptesync
1:	blr

__init_PMU_HV:
	li	r5,0
	mtspr	SPRN_MMCRC,r5
	blr

__init_PMU_HV_ISA207:
	li	r5,0
	mtspr	SPRN_MMCRH,r5
	blr

__init_PMU:
	li	r5,0
	mtspr	SPRN_MMCRA,r5
	mtspr	SPRN_MMCR0,r5
	mtspr	SPRN_MMCR1,r5
	mtspr	SPRN_MMCR2,r5
	blr

__init_PMU_ISA207:
	li	r5,0
	mtspr	SPRN_MMCRS,r5
	blr
