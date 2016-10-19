/*
 * Copyright (C) 2016 Freescale Semiconductor, Inc. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/linkage.h>
#include "hardware.h"

#define CCM_CBCDR	0x14
#define CCM_CBCMR	0x18
#define CCM_CSCMR1	0x1c
#define CCM_CDHIPR	0x48

#define L2_CACHE_SYNC	0x730

#define MMDC0_MDPDC	0x4
#define MMDC0_MAPSR	0x404
#define MMDC0_MADPCR0	0x410

#define HIGH_BUS_MODE	0x0

	.macro	wait_for_ccm_handshake

1:
	ldr	r8, [r2, #CCM_CDHIPR]
	cmp	r8, #0
	bne	1b

	.endm

	.macro	switch_to_24MHz

	/* periph2_clk2 sel to OSC_CLK */
	ldr	r8, [r2, #CCM_CBCMR]
	orr	r8, r8, #(1 << 20)
	str	r8, [r2, #CCM_CBCMR]

	/* periph2_clk2_podf to 0 */
	ldr	r8, [r2, #CCM_CBCDR]
	bic	r8, r8, #0x7
	str	r8, [r2, #CCM_CBCDR]

	/* periph2_clk sel to periph2_clk2 */
	ldr	r8, [r2, #CCM_CBCDR]
	orr	r8, r8, #(0x1 << 26)
	str	r8, [r2, #CCM_CBCDR]

	wait_for_ccm_handshake

	/* fabric_mmdc_podf to 0 */
	ldr	r8, [r2, #CCM_CBCDR]
	bic	r8, r8, #(0x7 << 3)
	str	r8, [r2, #CCM_CBCDR]

	wait_for_ccm_handshake

	.endm

	.macro	switch_to_100MHz

	/* check whether periph2_clk is from top path */
	ldr	r8, [r2, #CCM_CBCDR]
	ands	r8, #(1 << 26)
	beq	skip_periph2_clk2_switch_100m

	/* now switch periph2_clk back. */
	ldr	r8, [r2, #CCM_CBCDR]
	bic	r8, r8, #(1 << 26)
	str	r8, [r2, #CCM_CBCDR]

	wait_for_ccm_handshake

	/*
	 * on i.MX6SLL, pre_periph2_clk will be always from
	 * pll2_pfd2, so no need to set pre_periph2_clk
	 * parent, just set the mmdc divider directly.
	 */
skip_periph2_clk2_switch_100m:

	/* fabric_mmdc_podf to 3 so that mmdc is 400 / 4 = 100MHz */
	ldr	r8, [r2, #CCM_CBCDR]
	bic	r8, r8, #(0x7 << 3)
	orr	r8, r8, #(0x3 << 3)
	str	r8, [r2, #CCM_CBCDR]

	wait_for_ccm_handshake

	.endm

	.macro	switch_to_400MHz

	/* check whether periph2_clk is from top path */
	ldr	r8, [r2, #CCM_CBCDR]
	ands	r8, #(1 << 26)
	beq	skip_periph2_clk2_switch_400m

	/* now switch periph2_clk back. */
	ldr	r8, [r2, #CCM_CBCDR]
	bic	r8, r8, #(1 << 26)
	str	r8, [r2, #CCM_CBCDR]

	wait_for_ccm_handshake

	/*
	 * on i.MX6SLL, pre_periph2_clk will be always from
	 * pll2_pfd2, so no need to set pre_periph2_clk
	 * parent, just set the mmdc divider directly.
	 */
skip_periph2_clk2_switch_400m:

	/* fabric_mmdc_podf to 0 */
	ldr	r8, [r2, #CCM_CBCDR]
	bic	r8, r8, #(0x7 << 3)
	str	r8, [r2, #CCM_CBCDR]

	wait_for_ccm_handshake

	.endm

	.macro	mmdc_clk_lower_100MHz
	/* if MMDC is not in 400MHz mode, skip double mu count */
	cmp	r1, #HIGH_BUS_MODE
	bne	1f

	/*
	 * Prior to reducing the DDR frequency (at 528/400 MHz),
	 * read the Measure unit count bits (MU_UNIT_DEL_NUM)
	 */
	ldr	r8, =0x8B8
	ldr	r6, [r5, r8]
	/* Original MU unit count */
	mov	r6, r6, LSR #16
	ldr	r4, =0x3FF
	and	r6, r6, r4
	/* Original MU unit count * 2 */
	mov	r7, r6, LSL #1
	/*
	 * Bypass the automatic measure unit when below 100 MHz
	 * by setting the Measure unit bypass enable bit (MU_BYP_EN)
	 */
	ldr	r6, [r5, r8]
	orr	r6, r6, #0x400
	str	r6, [r5, r8]
	/*
	 * Double the measure count value read in step 1 and program it in the
	 * measurement bypass bits (MU_BYP_VAL) of the MMDC PHY Measure Unit
	 * Register for the reduced frequency operation below 100 MHz
	 */
	ldr	r6, [r5, r8]
	ldr	r4, =0x3FF
	bic	r6, r6, r4
	orr	r6, r6, r7
	str	r6, [r5, r8]

	/* For freq lower than 100MHz, need to set RALAT to 2 */
	ldr	r6, [r5, #0x18]
	bic	r6, r6, #(0x7 << 6)
	orr	r6, r6, #(0x2 << 6)
	str	r6, [r5, #0x18]
1:
	.endm

	.macro	mmdc_clk_above_100MHz

	/* Make sure that the PHY measurement unit is NOT in bypass mode */
	ldr	r8, =0x8B8
	ldr	r6, [r5, r8]
	bic	r6, r6, #0x400
	str	r6, [r5, r8]
	/* Now perform a Force Measurement. */
	ldr	r6, [r5, r8]
	orr	r6, r6, #0x800
	str	r6, [r5, r8]
	/* Wait for FRC_MSR to clear. */
force_measure1:
	ldr	r6, [r5, r8]
	and	r6, r6, #0x800
	cmp	r6, #0x0
	bne	force_measure1

	/* For freq higher than 100MHz, need to set RALAT to 5 */
	ldr	r6, [r5, #0x18]
	bic	r6, r6, #(0x7 << 6)
	orr	r6, r6, #(0x5 << 6)
	str	r6, [r5, #0x18]

	.endm

	.align 3
/*
 * Below code can be used by i.MX6SLL when changing the
 * frequency of MMDC. the MMDC is the same on these two SOCs.
 */
ENTRY(imx6sll_lpddr2_freq_change)
	push	{r2 - r8}

	/*
	 * To ensure no page table walks occur in DDR, we
	 * have a another page table stored in IRAM that only
	 * contains entries pointing to IRAM, AIPS1 and AIPS2.
	 * We need to set the TTBR1 to the new IRAM TLB.
	 * Do the following steps:
	 * 1. Flush the Branch Target Address Cache (BTAC)
	 * 2. Set TTBR1 to point to IRAM page table.
	 * 3. Disable page table walks in TTBR0 (PD0 = 1)
	 * 4. Set TTBR0.N=1, implying 0-2G is translated by TTBR0
	 *     and 2-4G is translated by TTBR1.
	 */

	ldr	r6, =iram_tlb_phys_addr
	ldr	r7, [r6]

	/* Flush the Branch Target Address Cache (BTAC) */
	ldr	r6, =0x0
	mcr	p15, 0, r6, c7, c1, 6

	/* Disable Branch Prediction, Z bit in SCTLR. */
	mrc	p15, 0, r6, c1, c0, 0
	bic	r6, r6, #0x800
	mcr	p15, 0, r6, c1, c0, 0

	dsb
	isb
	/* Store the IRAM table in TTBR1 */
	mcr	p15, 0, r7, c2, c0, 1

	/* Read TTBCR and set PD0=1, N = 1 */
	mrc	p15, 0, r6, c2, c0, 2
	orr	r6, r6, #0x11
	mcr	p15, 0, r6, c2, c0, 2

	dsb
	isb

	/* flush the TLB */
	ldr	r6, =0x0
	mcr	p15, 0, r6, c8, c3, 0

	/* Disable L1 data cache. */
	mrc	p15, 0, r6, c1, c0, 0
	bic	r6, r6, #0x4
	mcr	p15, 0, r6, c1, c0, 0

	dsb
	isb

#ifdef CONFIG_CACHE_L2X0
	/*
	 * Need to make sure the buffers in L2 are drained.
	 * Performing a sync operation does this.
	 */
	ldr	r7, =IMX_IO_P2V(MX6Q_L2_BASE_ADDR)
	mov	r6, #0x0
	str	r6, [r7, #L2_CACHE_SYNC]

	/*
	 * The second dsb might be needed to keep cache sync (device write)
	 * ordering with the memory accesses before it.
	 */
	dsb
	isb

	/* Disable L2. */
	str	r6, [r7, #0x100]
#endif

	ldr	r2, =IMX_IO_P2V(MX6Q_CCM_BASE_ADDR)
	ldr	r3, =IMX_IO_P2V(MX6Q_ANATOP_BASE_ADDR)
	ldr	r5, =IMX_IO_P2V(MX6Q_MMDC_P0_BASE_ADDR)

	/* Disable Automatic power savings. */
	ldr	r6, [r5, #MMDC0_MAPSR]
	orr	r6, r6, #0x1
	str	r6, [r5, #MMDC0_MAPSR]

	/* Delay for a while */
	ldr	r8, =10
delay:
	ldr	r7, =0
cont:
	ldr	r6, [r5, r7]
	add	r7, r7, #4
	cmp	r7, #16
	bne	cont
	sub	r8, r8, #1
	cmp	r8, #0
	bgt	delay

	/* Make the DDR explicitly enter self-refresh. */
	ldr	r6, [r5, #MMDC0_MAPSR]
	orr	r6, r6, #0x200000
	str	r6, [r5, #MMDC0_MAPSR]

poll_dvfs_set_1:
	ldr	r6, [r5, #MMDC0_MAPSR]
	and	r6, r6, #0x2000000
	cmp	r6, #0x2000000
	bne	poll_dvfs_set_1

	/* set SBS step-by-step mode */
	ldr	r6, [r5, #MMDC0_MADPCR0]
	orr	r6, r6, #0x100
	str	r6, [r5, #MMDC0_MADPCR0]

	ldr	r6, =100000000
	cmp	r0, r6
	bgt	set_ddr_mu_above_100
	mmdc_clk_lower_100MHz

set_ddr_mu_above_100:
	ldr	r6, =24000000
	cmp	r0, r6
	beq	set_to_24MHz

	ldr     r6, =100000000
	cmp	r0, r6
	beq	set_to_100MHz

	switch_to_400MHz

	mmdc_clk_above_100MHz

	b	done

set_to_24MHz:
	switch_to_24MHz
	b	done
set_to_100MHz:
	switch_to_100MHz
done:
	/* clear DVFS - exit from self refresh mode */
	ldr	r6, [r5, #MMDC0_MAPSR]
	bic	r6, r6, #0x200000
	str	r6, [r5, #MMDC0_MAPSR]

poll_dvfs_clear_1:
	ldr	r6, [r5, #MMDC0_MAPSR]
	and	r6, r6, #0x2000000
	cmp	r6, #0x2000000
	beq	poll_dvfs_clear_1

	/* Enable Automatic power savings. */
	ldr	r6, [r5, #MMDC0_MAPSR]
	bic	r6, r6, #0x1
	str	r6, [r5, #MMDC0_MAPSR]

	/* clear SBS - unblock DDR accesses */
	ldr	r6, [r5, #MMDC0_MADPCR0]
	bic	r6, r6, #0x100
	str	r6, [r5, #MMDC0_MADPCR0]

	ldr	r6,  =0xa0000000
	str	r6, [r5, #0x83c]


#ifdef CONFIG_CACHE_L2X0
	/* Enable L2. */
	ldr	r7, =IMX_IO_P2V(MX6Q_L2_BASE_ADDR)
	ldr	r6, =0x1
	str	r6, [r7, #0x100]
#endif

	/* Enable L1 data cache. */
	mrc	p15, 0, r6, c1, c0, 0
	orr	r6, r6, #0x4
	mcr	p15, 0, r6, c1, c0, 0

	/* Restore the TTBCR */
	dsb
	isb

	/* Read TTBCR and set PD0=0, N = 0 */
	mrc	p15, 0, r6, c2, c0, 2
	bic	r6, r6, #0x11
	mcr	p15, 0, r6, c2, c0, 2
	dsb
	isb

	/* flush the TLB */
	ldr	r6, =0x0
	mcr	p15, 0, r6, c8, c3, 0

	dsb
	isb

	/* Enable Branch Prediction, Z bit in SCTLR. */
	mrc	p15, 0, r6, c1, c0, 0
	orr	r6, r6, #0x800
	mcr	p15, 0, r6, c1, c0, 0

	/* Flush the Branch Target Address Cache (BTAC) */
	ldr	r6, =0x0
	mcr	p15, 0, r6, c7, c1, 6

	nop
	nop
	nop
	nop
	nop

	nop
	nop
	nop
	nop
	nop

	nop
	nop
	nop
	nop
	nop

	nop
	nop
	nop
	nop
	nop

	nop
	nop
	nop
	nop
	nop

	/* Restore registers */
	pop	{r2 - r8}
	mov	pc, lr
