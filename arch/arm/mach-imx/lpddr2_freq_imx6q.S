/*
 * Copyright (C) 2015-2016 Freescale Semiconductor, Inc. All Rights Reserved.
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
#include <asm/smp_scu.h>
#include "hardware.h"

#define CCM_CBCDR 		0x14
#define CCM_CBCMR 		0x18
#define CCM_CSCMR1 		0x1c
#define CCM_CDHIPR 		0x48

.globl mx6q_lpddr2_freq_change_start
.globl mx6q_lpddr2_freq_change_end

	.macro	wait_for_ccm_handshake
	/* wait for div update */
1:
	ldr	r9, [r2, #CCM_CDHIPR]
	cmp	r9, #0
	bne	1b

	.endm

	.macro	switch_to_400MHz
	/* set the MMDC_DIV=1, AXI_DIV=2, AHB_DIV=3 */
	ldr	r9, [r2, #CCM_CBCDR]
	ldr	r6, =0x3f1f00
	bic	r9, r9, r6
	orr	r9, r9, #(0x9 << 8)
	orr	r9, r9, #(1 << 16)
	str	r9, [r2, #CCM_CBCDR]

	wait_for_ccm_handshake

	/* check periph_clk_sel */
	ldr	r9, [r2, #CCM_CBCDR]
	and	r9, r9, #(1 << 25)
	cmp	r9, #(1 << 25)
	bne	skip_periph_clk_switch_400m

	/* now switch periph_clk back. */
	ldr	r9, [r2, #CCM_CBCDR]
	bic	r9, r9, #(1 << 25)
	str	r9, [r2, #CCM_CBCDR]

	wait_for_ccm_handshake

skip_periph_clk_switch_400m:

	.endm

	.macro	switch_to_100MHz
	/* set the MMDC_DIV=4, AXI_DIV=8, AHB_DIV=8 */
	ldr	r9, [r2, #CCM_CBCDR]
	ldr	r6, =0x3f1f00
	bic	r9, r9, r6
	orr	r9, r9, #(0x1F << 16)
	orr	r9, r9, #(0x1D << 8)
	str	r9, [r2, #CCM_CBCDR]

	wait_for_ccm_handshake

	/* check if periph_clk_sel is already set. */
	ldr	r9, [r2, #CCM_CBCDR]
	and	r9, r9, #(1 << 25)
	cmp	r9, #(1 << 25)
	bne	skip_periph_clk_switch_100m

	/* now switch periph_clk back. */
	ldr	r9, [r2, #CCM_CBCDR]
	bic	r9, r9, #(1 << 25)
	str	r9, [r2, #CCM_CBCDR]

	wait_for_ccm_handshake

skip_periph_clk_switch_100m:

	.endm

	.macro	switch_to_24MHz
	/*
	 * change the freq now try setting DDR to 24MHz.
	 * source it from the periph_clk2 ensure the
	 * periph_clk2 is sourced from 24MHz and the
	 * divider is 1.
	 */

	ldr	r9, [r2, #CCM_CBCMR]
	bic	r9, r9, #(0x3 << 12)
	orr	r9, r9, #(1 << 12)
	str	r9, [r2, #CCM_CBCMR]

	ldr	r9, [r2, #CCM_CBCDR]
	bic	r9, r9, #(0x7 << 27)
	str	r9, [r2, #CCM_CBCDR]

	/* now switch periph_clk to 24MHz. */
	ldr	r9, [r2, #CCM_CBCDR]
	orr	r9, r9, #(1 << 25)
	str	r9, [r2, #CCM_CBCDR]

	wait_for_ccm_handshake

	/* change all the dividers to 1. */
	ldr	r9, [r2, #CCM_CBCDR]
	ldr	r6, =0x3f1f00
	bic	r9, r9, r6
	orr	r9, r9, #(1 << 8)
	str	r9, [r2, #CCM_CBCDR]

	/* Wait for the divider to change. */
	wait_for_ccm_handshake

	.endm

	.macro 	switch_to_24MHZ_from_pll2
	/* Change DDR freq settings from pll2_pfd2 (div 2) */

	ldr	r9, [r2, #CCM_CBCMR]
	bic	r9, r9, #(0x3 << 18)
	orr	r9, r9, #(0x3 << 18)
	str	r9, [r2, #CCM_CBCMR]

	ldr	r9, [r2, #CCM_CBCDR]
	bic	r9, r9, #(1 << 25)
	str	r9, [r2, #CCM_CBCDR]

	wait_for_ccm_handshake

	ldr	r9, [r2, #CCM_CBCDR]
	ldr	r6, =0x3f1f00
	bic	r9, r9, r6
	orr	r9, r9, #(1 << 8)
	orr	r9, r9, #(0x7 << 19)
	str	r9, [r2, #CCM_CBCDR]

	wait_for_ccm_handshake

	.endm

	.macro	set_timings_below_100MHz_operation
	/* Set MMDCx_MISC[RALAT] = 2 cycles */
	ldr	r6, [r8, #0x18]
	bic	r6, r6, #(0x7 << 6)
	orr	r6, r6, #(0x2 << 6)
	str	r6, [r8, #0x18]

	/* Adjust LPDDR2 timings for 24Mhz operation */
	ldr	r5, =0x03162073
	str	r5, [r8, #0xC]  	/* MMDC0_MDCFG0 */
	ldr	r7, =0x00020482
	str	r7, [r8, #0x10] 	/* MMDC0_MDCFG1 */
	ldr	r9, =0x00000049
	str	r9, [r8, #0x14] 	/* MMDC0_MDCFG2 */
	ldr	r10, =0x00020333
	str	r10, [r8, #0x38] 	/* MMDC0_MDCFG3LP */

	/* Check if lpddr2 channel 1 is enabled */
	ldr	r6, [r8, #0x18]
	ands	r6, r6, #(1 << 2)
	beq	skip_below_100Mhz_ch1_timings

	ldr	r6, [r4, #0x18]
	bic	r6, r6, #(0x7 << 6)
	orr	r6, r6, #(0x2 << 6)
	str	r6, [r4, #0x18]

	str	r5, [r4, #0xC]  	/* MMDC1_MDCFG0 */
	str	r7, [r4, #0x10] 	/* MMDC1_MDCFG1 */
	str	r9, [r4, #0x14] 	/* MMDC1_MDCFG2 */
	str	r10, [r4, #0x38] 	/* MMDC1_MDCFG3LP */

skip_below_100Mhz_ch1_timings:

	.endm

	.macro	set_timings_above_100MHz_operation
	/* restore timing from mmdc_settings_info */
	ldr	r6, [r1, #0x0]
	ldr	r7, [r1, #0x4]
tloop:
	ldr	r9, [r7], #0x4
	ldr	r10, [r7], #0x4
	str	r10, [r8, r9]
	subs	r6, r6, #0x1
	bne	tloop

	/* Check if lpddr2 channel 1 is enabled */
	ldr	r6, [r8, #0x18]
	ands	r6, r6, #(1 << 2)
	beq	skip_above_100Mhz_ch1_timings

	ldr	r6, [r1, #0x0]
	ldr	r7, [r1, #0x4]
tloop2:
	ldr	r9, [r7], #0x4
	ldr	r10, [r7], #0x4
	str	r10, [r4, r9]
	subs	r6, r6, #0x1
	bne	tloop2

skip_above_100Mhz_ch1_timings:

	.endm

	.macro	mmdc_clk_lower_100MHz

	set_timings_below_100MHz_operation
	/*
	 * Prior to reducing the DDR frequency (at 528/400 MHz),
	 * read the Measure unit count bits (MU_UNIT_DEL_NUM)
	 */
	ldr	r5, =0x8B8
	ldr	r6, [r8, r5]
	/* Original MU unit count */
	mov	r6, r6, LSR #16
	ldr	r9, =0x3FF
	and	r6, r6, r9
	/* Original MU unit count * 2 */
	mov	r7, r6, LSL #1
	/*
	 * Bypass the automatic measure unit when below 100 MHz
	 * by setting the Measure unit bypass enable bit (MU_BYP_EN)
	 */
	ldr	r6, [r8, r5]
	orr	r6, r6, #0x400
	str	r6, [r8, r5]
	/*
	 * Double the measure count value read in step 1 and program it in the
	 * measurement bypass bits (MU_BYP_VAL) of the MMDC PHY Measure Unit
	 * Register for the reduced frequency operation below 100 MHz
	 */
	ldr	r6, [r8, r5]
	ldr	r9, =0x3FF
	bic	r6, r6, r9
	orr	r6, r6, r7
	str	r6, [r8, r5]
	/* Now perform a Force Measurement. */
	ldr	r6, [r8, r5]
	orr	r6, r6, #0x800
	str	r6, [r8, r5]
	/* Wait for FRC_MSR to clear. */
force_measure:
	ldr	r6, [r8, r5]
	and	r6, r6, #0x800
	cmp	r6, #0x0
	bne	force_measure

	/* Check if lpddr2 channel 2 is enabled */
	ldr	r6, [r8, #0x18]
	ands	r6, r6, #(1 << 2)
	beq	skip_lower_force_measure_ch1

	ldr	r5, =0x8B8
	ldr	r6, [r4, r5]
	/* Original MU unit count */
	mov	r6, r6, LSR #16
	ldr	r9, =0x3FF
	and	r6, r6, r9
	/* Original MU unit count * 2 */
	mov	r7, r6, LSL #1
	/*
	 * Bypass the automatic measure unit when below 100 MHz
	 * by setting the Measure unit bypass enable bit (MU_BYP_EN)
	 */
	ldr	r6, [r4, r5]
	orr	r6, r6, #0x400
	str	r6, [r4, r5]
	/*
	 * Double the measure count value read in step 1 and program it in the
	 * measurement bypass bits (MU_BYP_VAL) of the MMDC PHY Measure Unit
	 * Register for the reduced frequency operation below 100 MHz
	 */
	ldr	r6, [r4, r5]
	ldr	r9, =0x3FF
	bic	r6, r6, r9
	orr	r6, r6, r7
	str	r6, [r4, r5]
	/* Now perform a Force Measurement. */
	ldr	r6, [r4, r5]
	orr	r6, r6, #0x800
	str	r6, [r4, r5]
	/* Wait for FRC_MSR to clear. */
force_measure_ch1:
	ldr	r6, [r4, r5]
	and	r6, r6, #0x800
	cmp	r6, #0x0
	bne	force_measure_ch1

skip_lower_force_measure_ch1:

	.endm

	.macro	mmdc_clk_above_100MHz

	set_timings_above_100MHz_operation

	/* Make sure that the PHY measurement unit is NOT in bypass mode */
	ldr	r5, =0x8B8
	ldr	r6, [r8, r5]
	bic	r6, r6, #0x400
	str	r6, [r8, r5]
	/* Now perform a Force Measurement. */
	ldr	r6, [r8, r5]
	orr	r6, r6, #0x800
	str	r6, [r8, r5]
	/* Wait for FRC_MSR to clear. */
force_measure1:
	ldr	r6, [r8, r5]
	and	r6, r6, #0x800
	cmp	r6, #0x0
	bne	force_measure1

	/* Check if lpddr2 channel 2 is enabled */
	ldr	r6, [r8, #0x18]
	ands	r6, r6, #(1 << 2)
	beq	skip_above_force_measure_ch1

	ldr	r5, =0x8B8
	ldr	r6, [r4, r5]
	bic	r6, r6, #0x400
	str	r6, [r4, r5]
	/* Now perform a Force Measurement. */
	ldr	r6, [r4, r5]
	orr	r6, r6, #0x800
	str	r6, [r4, r5]
	/* Wait for FRC_MSR to clear. */
force_measure1_ch1:
	ldr	r6, [r4, r5]
	and	r6, r6, #0x800
	cmp	r6, #0x0
	bne	force_measure1_ch1

skip_above_force_measure_ch1:

	.endm

/*
 *  mx6_lpddr2_freq_change
 *
 *  Make sure DDR is in self-refresh.
 *  IRQs are already disabled.
 * r0 : DDR freq.
 * r1 : mmdc_settings_info
 */
	.align 3
ENTRY(mx6q_lpddr2_freq_change)
mx6q_lpddr2_freq_change_start:
	push	{r2-r10}

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

	/* Wait for background operations to complete. */
wait_for_l2_to_idle:
	ldr	r6, [r7, #0x730]
	cmp	r6, #0x0
	bne	wait_for_l2_to_idle

	mov	r6, #0x0
	str	r6, [r7, #0x730]

	/*
	 * The second dsb might be needed to keep cache sync (device write)
	 * ordering with the memory accesses before it.
	 */
	dsb
	isb

	/* Disable L2. */
	str	r6, [r7, #0x100]
#endif

	ldr	r3, =IMX_IO_P2V(MX6Q_ANATOP_BASE_ADDR)
	ldr	r2, =IMX_IO_P2V(MX6Q_CCM_BASE_ADDR)
	ldr	r8, =IMX_IO_P2V(MX6Q_MMDC_P0_BASE_ADDR)
	ldr r4, =IMX_IO_P2V(MX6Q_MMDC_P1_BASE_ADDR)

	/* Disable Automatic power savings. */
	ldr	r6, [r8, #0x404]
	orr	r6, r6, #0x01
	str	r6, [r8, #0x404]

	/* MMDC0_MDPDC disable power down timer */
	ldr	r6, [r8, #0x4]
	bic	r6, r6, #0xff00
	str	r6, [r8, #0x4]

	/* Check if lpddr2 channel 2 is enabled */
	ldr	r6, [r8, #0x18]
	ands	r6, r6, #(1 << 2)
	beq	skip_psd_ch1

	ldr	r6, [r4, #0x404]
	orr	r6, r6, #0x01
	str	r6, [r4, #0x404]

	ldr	r6, [r4, #0x4]
	bic	r6, r6, #0xff00
	str	r6, [r4, #0x4]

skip_psd_ch1:
	/* Delay for a while */
	ldr	r10, =10
delay1:
	ldr	r7, =0
cont1:
	ldr	r6, [r8, r7]
	add	r7, r7, #4
	cmp	r7, #16
	bne	cont1
	sub	r10, r10, #1
	cmp	r10, #0
	bgt	delay1

	/* Make the DDR explicitly enter self-refresh. */
	ldr	r6, [r8, #0x404]
	orr	r6, r6, #0x200000
	str	r6, [r8, #0x404]

poll_dvfs_set_1:
	ldr	r6, [r8, #0x404]
	and	r6, r6, #0x2000000
	cmp	r6, #0x2000000
	bne	poll_dvfs_set_1

	/* set SBS step-by-step mode */
	ldr	r6, [r8, #0x410]
	orr	r6, r6, #0x100
	str	r6, [r8, #0x410]

	/* Check if lpddr2 channel 2 is enabled */
	ldr	r6, [r8, #0x18]
	ands	r6, r6, #(1 << 2)
	beq	skip_sbs_ch1

	ldr	r6, [r4, #0x404]
	orr	r6, r6, #0x200000
	str	r6, [r4, #0x404]

poll_dvfs_set_2:
	ldr	r6, [r4, #0x404]
	and	r6, r6, #0x2000000
	cmp	r6, #0x2000000
	bne	poll_dvfs_set_2

	ldr	r6, [r4, #0x410]
	orr	r6, r6, #0x100
	str	r6, [r4, #0x410]

skip_sbs_ch1:
	ldr	r10, =100000000
	cmp	r0, r10
	bgt	set_ddr_mu_above_100
	mmdc_clk_lower_100MHz

set_ddr_mu_above_100:
	ldr	r10, =24000000
	cmp	r0, r10
	beq	set_to_24MHz

	ldr	r10, =100000000
	cmp	r0, r10
	beq set_to_100MHz

	ldr r10, =400000000
	cmp r0, r10
	switch_to_400MHz
	b	done

set_to_24MHz:
/*
	switch_to_24MHZ_from_pll2
*/
	switch_to_24MHz
	b	done

set_to_100MHz:
	switch_to_100MHz

done:

	ldr	r10,=100000000
	cmp	r0, r10
	ble	skip_mmdc_clk_check
	mmdc_clk_above_100MHz

skip_mmdc_clk_check:

	/* clear DVFS - exit from self refresh mode */
	ldr	r6, [r8, #0x404]
	bic	r6, r6, #0x200000
	str	r6, [r8, #0x404]

poll_dvfs_clear_1:
	ldr	r6, [r8, #0x404]
	and	r6, r6, #0x2000000
	cmp	r6, #0x2000000
	beq	poll_dvfs_clear_1

	/* Enable Automatic power savings. */
	ldr	r6, [r8, #0x404]
	bic	r6, r6, #0x01
	str	r6, [r8, #0x404]

	/* Check if lpddr2 channel 2 is enabled */
	ldr	r6, [r8, #0x18]
	ands	r6, r6, #(1 << 2)
	beq	skip_enable_psd_ch1

	ldr	r6, [r4, #0x404]
	bic	r6, r6, #0x200000
	str	r6, [r4, #0x404]

poll_dvfs_clear_2:
	ldr	r6, [r4, #0x404]
	and	r6, r6, #0x2000000
	cmp	r6, #0x2000000
	beq	poll_dvfs_clear_2

	ldr	r6, [r4, #0x404]
	bic	r6, r6, #0x01
	str	r6, [r4, #0x404]

skip_enable_psd_ch1:
	ldr	r10, =24000000
	cmp	r0, r10
	beq	skip_power_down

	/* Enable MMDC power down timer. */
	ldr	r6, [r8, #0x4]
	orr	r6, r6, #0x5500
	str	r6, [r8, #0x4]

	/* Check if lpddr2 channel 2 is enabled */
	ldr	r6, [r8, #0x18]
	ands	r6, r6, #(1 << 2)
	beq	skip_power_down

	ldr	r6, [r4, #0x4]
	orr	r6, r6, #0x5500
	str	r6, [r4, #0x4]

skip_power_down:
	/* clear SBS - unblock DDR accesses */
	ldr	r6, [r8, #0x410]
	bic	r6, r6, #0x100
	str	r6, [r8, #0x410]

	/* Check if lpddr2 channel 2 is enabled */
	ldr	r6, [r8, #0x18]
	ands	r6, r6, #(1 << 2)
	beq	skip_disable_sbs_ch1

	ldr	r6, [r4, #0x410]
	bic	r6, r6, #0x100
	str	r6, [r4, #0x410]

skip_disable_sbs_ch1:
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

	pop	{r2-r10}

	/* Restore registers */
	mov	pc, lr

	/*
	 * Add ltorg here to ensure that all
	 * literals are stored here and are
	 * within the text space.
	 */
	.ltorg
mx6q_lpddr2_freq_change_end:
