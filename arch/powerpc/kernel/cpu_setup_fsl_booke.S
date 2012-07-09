/*
 * This file contains low level CPU setup functions.
 * Kumar Gala <galak@kernel.crashing.org>
 * Copyright 2009 Freescale Semiconductor, Inc.
 *
 * Based on cpu_setup_6xx code by
 * Benjamin Herrenschmidt <benh@kernel.crashing.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 */

#include <asm/processor.h>
#include <asm/cputable.h>
#include <asm/ppc_asm.h>
#include <asm/mmu-book3e.h>
#include <asm/asm-offsets.h>

_GLOBAL(__e500_icache_setup)
	mfspr	r0, SPRN_L1CSR1
	andi.	r3, r0, L1CSR1_ICE
	bnelr				/* Already enabled */
	oris	r0, r0, L1CSR1_CPE@h
	ori	r0, r0, (L1CSR1_ICFI | L1CSR1_ICLFR |  L1CSR1_ICE)
	mtspr	SPRN_L1CSR1, r0		/* Enable I-Cache */
	isync
	blr

_GLOBAL(__e500_dcache_setup)
	mfspr	r0, SPRN_L1CSR0
	andi.	r3, r0, L1CSR0_DCE
	bnelr				/* Already enabled */
	msync
	isync
	li	r0, 0
	mtspr	SPRN_L1CSR0, r0		/* Disable */
	msync
	isync
	li	r0, (L1CSR0_DCFI | L1CSR0_CLFC)
	mtspr	SPRN_L1CSR0, r0		/* Invalidate */
	isync
1:	mfspr	r0, SPRN_L1CSR0
	andi.	r3, r0, L1CSR0_CLFC
	bne+	1b			/* Wait for lock bits reset */
	oris	r0, r0, L1CSR0_CPE@h
	ori	r0, r0, L1CSR0_DCE
	msync
	isync
	mtspr	SPRN_L1CSR0, r0		/* Enable */
	isync
	blr

#ifdef CONFIG_PPC32
_GLOBAL(__setup_cpu_e200)
	/* enable dedicated debug exception handling resources (Debug APU) */
	mfspr	r3,SPRN_HID0
	ori	r3,r3,HID0_DAPUEN@l
	mtspr	SPRN_HID0,r3
	b	__setup_e200_ivors
_GLOBAL(__setup_cpu_e500v1)
_GLOBAL(__setup_cpu_e500v2)
	mflr	r4
	bl	__e500_icache_setup
	bl	__e500_dcache_setup
	bl	__setup_e500_ivors
#ifdef CONFIG_FSL_RIO
	/* Ensure that RFXE is set */
	mfspr	r3,SPRN_HID1
	oris	r3,r3,HID1_RFXE@h
	mtspr	SPRN_HID1,r3
#endif
	mtlr	r4
	blr
_GLOBAL(__setup_cpu_e500mc)
	mflr	r5
	bl	__e500_icache_setup
	bl	__e500_dcache_setup
	bl	__setup_e500mc_ivors
	/*
	 * We only want to touch IVOR38-41 if we're running on hardware
	 * that supports category E.HV.  The architectural way to determine
	 * this is MMUCFG[LPIDSIZE].
	 */
	mfspr	r3, SPRN_MMUCFG
	rlwinm.	r3, r3, 0, MMUCFG_LPIDSIZE
	beq	1f
	bl	__setup_ehv_ivors
	b	2f
1:
	lwz	r3, CPU_SPEC_FEATURES(r4)
	/* We need this check as cpu_setup is also called for
	 * the secondary cores. So, if we have already cleared
	 * the feature on the primary core, avoid doing it on the
	 * secondary core.
	 */
	andis.	r6, r3, CPU_FTR_EMB_HV@h
	beq	2f
	rlwinm	r3, r3, 0, ~CPU_FTR_EMB_HV
	stw	r3, CPU_SPEC_FEATURES(r4)
2:
	mtlr	r5
	blr
#endif
/* Right now, restore and setup are the same thing */
_GLOBAL(__restore_cpu_e5500)
_GLOBAL(__setup_cpu_e5500)
	mflr	r4
	bl	__e500_icache_setup
	bl	__e500_dcache_setup
#ifdef CONFIG_PPC_BOOK3E_64
	bl	.__setup_base_ivors
	bl	.setup_perfmon_ivor
	bl	.setup_doorbell_ivors
	bl	.setup_ehv_ivors
#else
	bl	__setup_e500mc_ivors
#endif
	mtlr	r4
	blr
