/*
 * Copyright (C) 2012-2016 Freescale Semiconductor, Inc. All Rights Reserved.
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

.globl imx6_lpddr2_freq_change_start
.globl imx6_lpddr2_freq_change_end

	.macro    mx6sl_switch_to_24MHz

	/*
	 * Set MMDC clock to be sourced from PLL3.
	 * Ensure first periph2_clk2 is sourced from PLL3.
	 * Set the PERIPH2_CLK2_PODF to divide by 2.
	 */
	ldr	r6, [r2, #0x14]
	bic	r6, r6, #0x7
	orr	r6, r6, #0x1
	str	r6, [r2, #0x14]

	/* Select PLL3 to source MMDC. */
	ldr	r6, [r2, #0x18]
	bic	r6, r6, #0x100000
	str	r6, [r2, #0x18]

	/* Swtich periph2_clk_sel to run from PLL3. */
	ldr	r6, [r2, #0x14]
	orr	r6, r6, #0x4000000
	str	r6, [r2, #0x14]

periph2_clk_switch1:
	ldr	r6, [r2, #0x48]
	cmp	r6, #0
	bne	periph2_clk_switch1

	/*
	 * Need to clock gate the 528 PFDs before
	 * powering down PLL2.
	 * Only the PLL2_PFD2_400M should be ON
	 * at this time, so only clock gate that one.
	 */
	ldr	r6, [r3, #0x100]
	orr	r6, r6, #0x800000
	str	r6, [r3, #0x100]

	/*
	 * Set PLL2 to bypass state. We should be here
	 * only if MMDC is not sourced from PLL2.
	 */
	ldr	r6, [r3, #0x30]
	orr	r6, r6, #0x10000
	str	r6, [r3, #0x30]

	ldr	r6, [r3, #0x30]
	orr	r6, r6, #0x1000
	str	r6, [r3, #0x30]

	/* Ensure pre_periph2_clk_mux is set to pll2 */
	ldr	r6, [r2, #0x18]
	bic	r6, r6, #0x600000
	str	r6, [r2, #0x18]

	/* Set MMDC clock to be sourced from the bypassed PLL2. */
	ldr	r6, [r2, #0x14]
	bic	r6, r6, #0x4000000
	str	r6, [r2, #0x14]

periph2_clk_switch2:
	ldr	r6, [r2, #0x48]
	cmp	r6, #0
	bne	periph2_clk_switch2

	/*
	 * Now move MMDC back to periph2_clk2 source.
	 * after selecting PLL2 as the option.
	 * Select PLL2 as the source.
	 */
	ldr	r6, [r2, #0x18]
	orr	r6, r6, #0x100000
	str	r6, [r2, #0x18]

	/* set periph2_clk2_podf to divide by 1. */
	ldr	r6, [r2, #0x14]
	bic	r6, r6, #0x7
	str	r6, [r2, #0x14]

	/* Now move periph2_clk to periph2_clk2 source */
	ldr	r6, [r2, #0x14]
	orr	r6, r6, #0x4000000
	str	r6, [r2, #0x14]

periph2_clk_switch3:
	ldr	r6, [r2, #0x48]
	cmp	r6, #0
	bne	periph2_clk_switch3

	/* Now set the MMDC PODF back to 1.*/
	ldr	r6, [r2, #0x14]
	bic	r6, r6, #0x38
	str	r6, [r2, #0x14]

mmdc_podf0:
	ldr	r6, [r2, #0x48]
	cmp	r6, #0
	bne	mmdc_podf0

	.endm

	  .macro	ddr_switch_400MHz

	/* Set MMDC divider first, in case PLL3 is at 480MHz. */
	ldr	r6, [r3, #0x10]
	and	r6, r6, #0x10000
	cmp	r6, #0x10000
	beq	pll3_in_bypass

	/* Set MMDC divder to divide by 2. */
	ldr	r6, [r2, #0x14]
	bic	r6, r6, #0x38
	orr	r6, r6, #0x8
	str	r6, [r2, #0x14]

mmdc_podf:
	ldr	r6, [r2, #0x48]
	cmp	r6, #0
	bne	mmdc_podf

pll3_in_bypass:
	/*
	 * Check if we are switching between
	 * 400Mhz <-> 100MHz.If so, we should
	 * try to source MMDC from PLL2_200M.
	 */
	cmp	r1, #0
	beq	not_low_bus_freq

	/* Ensure that MMDC is sourced from PLL2 mux first. */
	ldr	r6, [r2, #0x14]
	bic	r6, r6, #0x4000000
	str	r6, [r2, #0x14]

periph2_clk_switch4:
	ldr	r6, [r2, #0x48]
	cmp	r6, #0
	bne	periph2_clk_switch4

not_low_bus_freq:
	/* Now ensure periph2_clk2_sel mux is set to PLL3 */
	ldr	r6, [r2, #0x18]
	bic	r6, r6, #0x100000
	str	r6, [r2, #0x18]

	/* Now switch MMDC to PLL3. */
	ldr	r6, [r2, #0x14]
	orr	r6, r6, #0x4000000
	str	r6, [r2, #0x14]

periph2_clk_switch5:
	ldr	r6, [r2, #0x48]
	cmp	r6, #0
	bne	periph2_clk_switch5

	/*
	 * Check if PLL2 is already unlocked.
	 * If so do nothing with PLL2.
	 */
	cmp	r1, #0
	beq	pll2_already_on

	/* Now power up PLL2 and unbypass it. */
	ldr	r6, [r3, #0x30]
	bic	r6, r6, #0x1000
	str	r6, [r3, #0x30]

	/* Make sure PLL2 has locked.*/
wait_for_pll_lock:
	ldr	r6, [r3, #0x30]
	and	r6, r6, #0x80000000
	cmp	r6, #0x80000000
	bne	wait_for_pll_lock

	ldr	r6, [r3, #0x30]
	bic	r6, r6, #0x10000
	str	r6, [r3, #0x30]

	/*
	 * Need to enable the 528 PFDs after
	 * powering up PLL2.
	 * Only the PLL2_PFD2_400M should be ON
	 * as it feeds the MMDC. Rest should have
	 * been managed by clock code.
	 */
	ldr	r6, [r3, #0x100]
	bic	r6, r6, #0x800000
	str	r6, [r3, #0x100]

pll2_already_on:
	/*
	 * Now switch MMDC clk back to pll2_mux option.
	 * Ensure pre_periph2_clk2 is set to pll2_pfd_400M.
	 * If switching to audio DDR freq, set the
	 * pre_periph2_clk2 to PLL2_PFD_200M
	 */
	ldr	r6, =400000000
	cmp	r6, r0
	bne	use_pll2_pfd_200M

	ldr	r6, [r2, #0x18]
	bic	r6, r6, #0x600000
	orr	r6, r6, #0x200000
	str	r6, [r2, #0x18]
	ldr	r6, =400000000
	b       cont2

use_pll2_pfd_200M:
	ldr	r6, [r2, #0x18]
	orr	r6, r6, #0x600000
	str	r6, [r2, #0x18]
	ldr	r6, =200000000

cont2:
	ldr	r4, [r2, #0x14]
	bic	r4, r4, #0x4000000
	str	r4, [r2, #0x14]

periph2_clk_switch6:
	ldr	r4, [r2, #0x48]
	cmp	r4, #0
	bne	periph2_clk_switch6

change_divider_only:
	/*
	 * Calculate the MMDC divider
	 * based on the requested freq.
	 */
	ldr	r4, =0
Loop2:
	sub	r6, r6, r0
	cmp	r6, r0
	blt	Div_Found
	add	r4, r4, #1
	bgt	Loop2

	/* Shift divider into correct offset. */
	lsl	r4, r4, #3
Div_Found:
	/* Set the MMDC PODF. */
	ldr	r6, [r2, #0x14]
	bic	r6, r6, #0x38
	orr	r6, r6, r4
	str	r6, [r2, #0x14]

mmdc_podf1:
	ldr	r6, [r2, #0x48]
	cmp	r6, #0
	bne	mmdc_podf1

	.endm

	.macro	mmdc_clk_lower_100MHz

	/*
	 * Prior to reducing the DDR frequency (at 528/400 MHz),
	 * read the Measure unit count bits (MU_UNIT_DEL_NUM)
	 */
	ldr	r5, =0x8B8
	ldr	r6, [r8, r5]
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
	ldr	r6, [r8, r5]
	orr	r6, r6, #0x400
	str	r6, [r8, r5]
	/*
	 * Double the measure count value read in step 1 and program it in the
	 * measurement bypass bits (MU_BYP_VAL) of the MMDC PHY Measure Unit
	 * Register for the reduced frequency operation below 100 MHz
	 */
	ldr	r6, [r8, r5]
	ldr	r4, =0x3FF
	bic	r6, r6, r4
	orr	r6, r6, r7
	str	r6, [r8, r5]

	.endm

	.macro	mmdc_clk_above_100MHz

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
	.endm

/*
 *  mx6_lpddr2_freq_change
 *
 *  Make sure DDR is in self-refresh.
 *  IRQs are already disabled.
 * r0 : DDR freq.
 * r1: low_bus_freq_mode flag
 */
	.align 3
ENTRY(mx6_lpddr2_freq_change)
imx6_lpddr2_freq_change_start:
	push {r4-r10}

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

	/* Disable Branch Prediction, Z bit in SCTLR. */
	mrc	p15, 0, r6, c1, c0, 0
	bic	r6, r6, #0x800
	mcr	p15, 0, r6, c1, c0, 0

	/* Flush the Branch Target Address Cache (BTAC) */
	ldr	r6, =0x0
	mcr	p15, 0, r6, c7, c1, 6

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

	/* Disable Automatic power savings. */
	ldr	r6, [r8, #0x404]
	orr	r6, r6, #0x01
	str	r6, [r8, #0x404]

	/* MMDC0_MDPDC disable power down timer */
	ldr	r6, [r8, #0x4]
	bic	r6, r6, #0xff00
	str	r6, [r8, #0x4]

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

	ldr	r10, =100000000
	cmp	r0, r10
	bgt	set_ddr_mu_above_100
	mmdc_clk_lower_100MHz

set_ddr_mu_above_100:
	ldr	r10, =24000000
	cmp	r0, r10
	beq	set_to_24MHz

	ddr_switch_400MHz

	ldr	r10,=100000000
	cmp	r0, r10
	blt	done
	mmdc_clk_above_100MHz

	b	done

set_to_24MHz:
	mx6sl_switch_to_24MHz

done:
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

	ldr	r10, =24000000
	cmp	r0, r10
	beq	skip_power_down

	/* Enable MMDC power down timer. */
	ldr	r6, [r8, #0x4]
	orr	r6, r6, #0x5500
	str	r6, [r8, #0x4]

skip_power_down:
	/* clear SBS - unblock DDR accesses */
	ldr	r6, [r8, #0x410]
	bic	r6, r6, #0x100
	str	r6, [r8, #0x410]

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

	pop {r4-r10}

	/* Restore registers */
	mov	pc, lr

	/*
	 * Add ltorg here to ensure that all
	 * literals are stored here and are
	 * within the text space.
	 */
	.ltorg
imx6_lpddr2_freq_change_end:
