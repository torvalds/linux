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

#include <asm/page.h>
#include <asm/processor.h>
#include <asm/cputable.h>
#include <asm/ppc_asm.h>
#include <asm/mmu-book3e.h>
#include <asm/asm-offsets.h>
#include <asm/mpc85xx.h>

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

/*
 * FIXME - we haven't yet done testing to determine a reasonable default
 * value for PW20_WAIT_IDLE_BIT.
 */
#define PW20_WAIT_IDLE_BIT		50 /* 1ms, TB frequency is 41.66MHZ */
_GLOBAL(setup_pw20_idle)
	mfspr	r3, SPRN_PWRMGTCR0

	/* Set PW20_WAIT bit, enable pw20 state*/
	ori	r3, r3, PWRMGTCR0_PW20_WAIT
	li	r11, PW20_WAIT_IDLE_BIT

	/* Set Automatic PW20 Core Idle Count */
	rlwimi	r3, r11, PWRMGTCR0_PW20_ENT_SHIFT, PWRMGTCR0_PW20_ENT

	mtspr	SPRN_PWRMGTCR0, r3

	blr

/*
 * FIXME - we haven't yet done testing to determine a reasonable default
 * value for AV_WAIT_IDLE_BIT.
 */
#define AV_WAIT_IDLE_BIT		50 /* 1ms, TB frequency is 41.66MHZ */
_GLOBAL(setup_altivec_idle)
	mfspr	r3, SPRN_PWRMGTCR0

	/* Enable Altivec Idle */
	oris	r3, r3, PWRMGTCR0_AV_IDLE_PD_EN@h
	li	r11, AV_WAIT_IDLE_BIT

	/* Set Automatic AltiVec Idle Count */
	rlwimi	r3, r11, PWRMGTCR0_AV_IDLE_CNT_SHIFT, PWRMGTCR0_AV_IDLE_CNT

	mtspr	SPRN_PWRMGTCR0, r3

	blr

#ifdef CONFIG_PPC_E500MC
_GLOBAL(__setup_cpu_e6500)
	mflr	r6
#ifdef CONFIG_PPC64
	bl	setup_altivec_ivors
	/* Touch IVOR42 only if the CPU supports E.HV category */
	mfspr	r10,SPRN_MMUCFG
	rlwinm.	r10,r10,0,MMUCFG_LPIDSIZE
	beq	1f
	bl	setup_lrat_ivor
1:
#endif
	bl	setup_pw20_idle
	bl	setup_altivec_idle
	bl	__setup_cpu_e5500
	mtlr	r6
	blr
#endif /* CONFIG_PPC_E500MC */

#ifdef CONFIG_PPC32
#ifdef CONFIG_E200
_GLOBAL(__setup_cpu_e200)
	/* enable dedicated debug exception handling resources (Debug APU) */
	mfspr	r3,SPRN_HID0
	ori	r3,r3,HID0_DAPUEN@l
	mtspr	SPRN_HID0,r3
	b	__setup_e200_ivors
#endif /* CONFIG_E200 */

#ifdef CONFIG_E500
#ifndef CONFIG_PPC_E500MC
_GLOBAL(__setup_cpu_e500v1)
_GLOBAL(__setup_cpu_e500v2)
	mflr	r4
	bl	__e500_icache_setup
	bl	__e500_dcache_setup
	bl	__setup_e500_ivors
#if defined(CONFIG_FSL_RIO) || defined(CONFIG_FSL_PCI)
	/* Ensure that RFXE is set */
	mfspr	r3,SPRN_HID1
	oris	r3,r3,HID1_RFXE@h
	mtspr	SPRN_HID1,r3
#endif
	mtlr	r4
	blr
#else /* CONFIG_PPC_E500MC */
_GLOBAL(__setup_cpu_e500mc)
_GLOBAL(__setup_cpu_e5500)
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
#endif /* CONFIG_PPC_E500MC */
#endif /* CONFIG_E500 */
#endif /* CONFIG_PPC32 */

#ifdef CONFIG_PPC_BOOK3E_64
_GLOBAL(__restore_cpu_e6500)
	mflr	r5
	bl	setup_altivec_ivors
	/* Touch IVOR42 only if the CPU supports E.HV category */
	mfspr	r10,SPRN_MMUCFG
	rlwinm.	r10,r10,0,MMUCFG_LPIDSIZE
	beq	1f
	bl	setup_lrat_ivor
1:
	bl	setup_pw20_idle
	bl	setup_altivec_idle
	bl	__restore_cpu_e5500
	mtlr	r5
	blr

_GLOBAL(__restore_cpu_e5500)
	mflr	r4
	bl	__e500_icache_setup
	bl	__e500_dcache_setup
	bl	__setup_base_ivors
	bl	setup_perfmon_ivor
	bl	setup_doorbell_ivors
	/*
	 * We only want to touch IVOR38-41 if we're running on hardware
	 * that supports category E.HV.  The architectural way to determine
	 * this is MMUCFG[LPIDSIZE].
	 */
	mfspr	r10,SPRN_MMUCFG
	rlwinm.	r10,r10,0,MMUCFG_LPIDSIZE
	beq	1f
	bl	setup_ehv_ivors
1:
	mtlr	r4
	blr

_GLOBAL(__setup_cpu_e5500)
	mflr	r5
	bl	__e500_icache_setup
	bl	__e500_dcache_setup
	bl	__setup_base_ivors
	bl	setup_perfmon_ivor
	bl	setup_doorbell_ivors
	/*
	 * We only want to touch IVOR38-41 if we're running on hardware
	 * that supports category E.HV.  The architectural way to determine
	 * this is MMUCFG[LPIDSIZE].
	 */
	mfspr	r10,SPRN_MMUCFG
	rlwinm.	r10,r10,0,MMUCFG_LPIDSIZE
	beq	1f
	bl	setup_ehv_ivors
	b	2f
1:
	ld	r10,CPU_SPEC_FEATURES(r4)
	LOAD_REG_IMMEDIATE(r9,CPU_FTR_EMB_HV)
	andc	r10,r10,r9
	std	r10,CPU_SPEC_FEATURES(r4)
2:
	mtlr	r5
	blr
#endif

/* flush L1 date cache, it can apply to e500v2, e500mc and e5500 */
_GLOBAL(flush_dcache_L1)
	mfmsr	r10
	wrteei	0

	mfspr	r3,SPRN_L1CFG0
	rlwinm	r5,r3,9,3	/* Extract cache block size */
	twlgti	r5,1		/* Only 32 and 64 byte cache blocks
				 * are currently defined.
				 */
	li	r4,32
	subfic	r6,r5,2		/* r6 = log2(1KiB / cache block size) -
				 *      log2(number of ways)
				 */
	slw	r5,r4,r5	/* r5 = cache block size */

	rlwinm	r7,r3,0,0xff	/* Extract number of KiB in the cache */
	mulli	r7,r7,13	/* An 8-way cache will require 13
				 * loads per set.
				 */
	slw	r7,r7,r6

	/* save off HID0 and set DCFA */
	mfspr	r8,SPRN_HID0
	ori	r9,r8,HID0_DCFA@l
	mtspr	SPRN_HID0,r9
	isync

	LOAD_REG_IMMEDIATE(r6, KERNELBASE)
	mr	r4, r6
	mtctr	r7

1:	lwz	r3,0(r4)	/* Load... */
	add	r4,r4,r5
	bdnz	1b

	msync
	mr	r4, r6
	mtctr	r7

1:	dcbf	0,r4		/* ...and flush. */
	add	r4,r4,r5
	bdnz	1b

	/* restore HID0 */
	mtspr	SPRN_HID0,r8
	isync

	wrtee r10

	blr

has_L2_cache:
	/* skip L2 cache on P2040/P2040E as they have no L2 cache */
	mfspr	r3, SPRN_SVR
	/* shift right by 8 bits and clear E bit of SVR */
	rlwinm	r4, r3, 24, ~0x800

	lis	r3, SVR_P2040@h
	ori	r3, r3, SVR_P2040@l
	cmpw	r4, r3
	beq	1f

	li	r3, 1
	blr
1:
	li	r3, 0
	blr

/* flush backside L2 cache */
flush_backside_L2_cache:
	mflr	r10
	bl	has_L2_cache
	mtlr	r10
	cmpwi	r3, 0
	beq	2f

	/* Flush the L2 cache */
	mfspr	r3, SPRN_L2CSR0
	ori	r3, r3, L2CSR0_L2FL@l
	msync
	isync
	mtspr	SPRN_L2CSR0,r3
	isync

	/* check if it is complete */
1:	mfspr	r3,SPRN_L2CSR0
	andi.	r3, r3, L2CSR0_L2FL@l
	bne	1b
2:
	blr

_GLOBAL(cpu_down_flush_e500v2)
	mflr r0
	bl	flush_dcache_L1
	mtlr r0
	blr

_GLOBAL(cpu_down_flush_e500mc)
_GLOBAL(cpu_down_flush_e5500)
	mflr r0
	bl	flush_dcache_L1
	bl	flush_backside_L2_cache
	mtlr r0
	blr

/* L1 Data Cache of e6500 contains no modified data, no flush is required */
_GLOBAL(cpu_down_flush_e6500)
	blr
