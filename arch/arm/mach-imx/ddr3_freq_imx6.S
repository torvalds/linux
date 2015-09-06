/*
 * Copyright (C) 2011-2015 Freescale Semiconductor, Inc. All Rights Reserved.
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

#define MMDC0_MDPDC		0x4
#define MMDC0_MDCF0			0x0c
#define MMDC0_MDCF1			0x10
#define MMDC0_MDMISC		0x18
#define MMDC0_MDSCR		0x1c
#define MMDC0_MAARCR		0x400
#define MMDC0_MAPSR			0x404
#define MMDC0_MADPCR0		0x410
#define MMDC0_MPZQHWCTRL	0x800
#define MMDC1_MPZQHWCTRL	0x4800
#define MMDC0_MPODTCTRL	0x818
#define MMDC1_MPODTCTRL	0x4818
#define MMDC0_MPDGCTRL0	0x83c
#define MMDC1_MPDGCTRL0	0x483c
#define MMDC0_MPMUR0		0x8b8
#define MMDC1_MPMUR0		0x48b8

#define CCM_CBCDR			0x14
#define CCM_CBCMR			0x18
#define CCM_CSCMR1			0x1c
#define CCM_CDHIPR			0x48

#define L2_CACHE_SYNC		0x730

#define IMX6QP_REVISION_ID 0x630100
#define ANADIG_DIGPROG     0x260

.extern iram_tlb_phys_addr
#ifdef CONFIG_SMP
.extern imx_scu_base
#endif

.globl mx6_ddr3_freq_change_start
.globl mx6_ddr3_freq_change_end
.globl wfe_ddr3_freq_change_start
.globl wfe_ddr3_freq_change_end

	.align 3

	.macro is_mx6qp

	/* check if the SOC is i.MX6QP */
	ldr	r0, =IMX_IO_P2V(MX6Q_ANATOP_BASE_ADDR)
	ldr	r1, [r0, #ANADIG_DIGPROG]
	ldr	r2, =IMX6QP_REVISION_ID
	cmp	r1, r2

	.endm

	.macro	switch_to_528MHz

	/* check if periph_clk_sel is already set */
	ldr	r0, [r6, #CCM_CBCDR]
	and	r0, r0, #(1 << 25)
	cmp	r0, #(1 << 25)
	beq	set_ahb_podf_before_switch

	/* change periph_clk to be sourced from pll3_clk. */
	ldr	r0, [r6, #CCM_CBCMR]
	bic	r0, r0, #(3 << 12)
	str	r0, [r6, #CCM_CBCMR]

	ldr	r0, [r6, #CCM_CBCDR]
	bic	r0, r0, #(0x38 << 20)
	str	r0, [r6, #CCM_CBCDR]

	/*
	 * set the AHB dividers before the switch,
	 * don't change AXI clock divider,
	 * set the MMDC_DIV=1, AXI_DIV = 2, AHB_DIV=4,
	 */
	ldr	r0, [r6, #CCM_CBCDR]
	ldr	r2, =0x3f1f00
	bic	r0, r0, r2
	orr	r0, r0, #0xd00
	orr	r0, r0, #(1 << 16)
	str	r0, [r6, #CCM_CBCDR]

wait_div_update528:
	ldr	r0, [r6, #CCM_CDHIPR]
	cmp	r0, #0
	bne	wait_div_update528

	/* now switch periph_clk to pll3_main_clk. */
	ldr	r0, [r6, #CCM_CBCDR]
	orr	r0, r0, #(1 << 25)
	str	r0, [r6, #CCM_CBCDR]

periph_clk_switch3:
	ldr	r0, [r6, #CCM_CDHIPR]
	cmp	r0, #0
	bne	periph_clk_switch3

	b	switch_pre_periph_clk_528

set_ahb_podf_before_switch:
	/*
	 * set the MMDC_DIV=1, AXI_DIV = 2, AHB_DIV=4,
	 */
	ldr	r0, [r6, #CCM_CBCDR]
	ldr	r2, =0x3f1f00
	bic	r0, r0, r2
	orr	r0, r0, #0xd00
	orr	r0, r0, #(1 << 16)
	str	r0, [r6, #CCM_CBCDR]

wait_div_update528_1:
	ldr	r0, [r6, #CCM_CDHIPR]
	cmp	r0, #0
	bne	wait_div_update528_1

switch_pre_periph_clk_528:

	/* now switch pre_periph_clk to PLL2_528MHz. */
	ldr	r0, [r6, #CCM_CBCMR]
	bic	r0, r0, #(0xc << 16)
	str	r0, [r6, #CCM_CBCMR]

	/* now switch periph_clk back. */
	ldr	r0, [r6, #CCM_CBCDR]
	bic	r0, r0, #(1 << 25)
	str	r0, [r6, #CCM_CBCDR]

periph_clk_switch4:
	ldr	r0, [r6, #CCM_CDHIPR]
	cmp	r0, #0
	bne	periph_clk_switch4

	.endm

	.macro	switch_to_400MHz

	/* check if periph_clk_sel is already set. */
	ldr	r0, [r6, #CCM_CBCDR]
	and	r0, r0, #(1 << 25)
	cmp	r0, #(1 << 25)
	beq	set_ahb_podf_before_switch1

	/* change periph_clk to be sourced from pll3_clk. */
	ldr	r0, [r6, #CCM_CBCMR]
	bic	r0, r0, #(3 << 12)
	str	r0, [r6, #CCM_CBCMR]

	ldr	r0, [r6, #CCM_CBCDR]
	bic	r0, r0, #(0x38 << 24)
	str	r0, [r6, #CCM_CBCDR]

	/* now switch periph_clk to pll3_main_clk. */
	ldr	r0, [r6, #CCM_CBCDR]
	orr	r0, r0, #(1 << 25)
	str	r0, [r6, #CCM_CBCDR]

periph_clk_switch5:
	ldr	r0, [r6, #CCM_CDHIPR]
	cmp	r0, #0
	bne	periph_clk_switch5

	b	switch_pre_periph_clk_400

set_ahb_podf_before_switch1:
	/*
	 * set the MMDC_DIV=1, AXI_DIV = 2, AHB_DIV=4,
	 */
	ldr	r0, [r6, #CCM_CBCDR]
	ldr	r2, =0x3f1f00
	bic	r0, r0, r2
	orr	r0, r0, #(0x9 << 8)
	orr	r0, r0, #(1 << 16)
	str	r0, [r6, #CCM_CBCDR]

wait_div_update400_1:
	ldr	r0, [r6, #CCM_CDHIPR]
	cmp	r0, #0
	bne	wait_div_update400_1

switch_pre_periph_clk_400:

	/* now switch pre_periph_clk to PFD_400MHz. */
	ldr	r0, [r6, #CCM_CBCMR]
	bic	r0, r0, #(0xc << 16)
	orr	r0, r0, #(0x4 << 16)
	str	r0, [r6, #CCM_CBCMR]

	/* now switch periph_clk back. */
	ldr	r0, [r6, #CCM_CBCDR]
	bic	r0, r0, #(1 << 25)
	str	r0, [r6, #CCM_CBCDR]

periph_clk_switch6:
	ldr	r0, [r6, #CCM_CDHIPR]
	cmp	r0, #0
	bne	periph_clk_switch6

	/*
	 * change AHB divider so that we are at 400/3=133MHz.
	 * don't change AXI clock divider.
	 * set the MMDC_DIV=1, AXI_DIV=2, AHB_DIV=3,
	 */
	ldr	r0, [r6, #CCM_CBCDR]
	ldr	r2, =0x3f1f00
	bic	r0, r0, r2
	orr	r0, r0, #(0x9 << 8)
	orr	r0, r0, #(1 << 16)
	str	r0, [r6, #CCM_CBCDR]

wait_div_update400_2:
	ldr	r0, [r6, #CCM_CDHIPR]
	cmp	r0, #0
	bne	wait_div_update400_2

	.endm

	.macro	switch_to_50MHz

	/* check if periph_clk_sel is already set. */
	ldr	r0, [r6, #CCM_CBCDR]
	and	r0, r0, #(1 << 25)
	cmp	r0, #(1 << 25)
	beq	switch_pre_periph_clk_50

	/*
	 * set the periph_clk to be sourced from PLL2_PFD_200M
	 * change periph_clk to be sourced from pll3_clk.
	 * ensure PLL3 is the source and set the divider to 1.
	 */
	ldr	r0, [r6, #CCM_CBCMR]
	bic	r0, r0, #(0x3 << 12)
	str	r0, [r6, #CCM_CBCMR]

	ldr	r0, [r6, #CCM_CBCDR]
	bic	r0, r0, #(0x38 << 24)
	str	r0, [r6, #CCM_CBCDR]

	/* now switch periph_clk to pll3_main_clk. */
	ldr	r0, [r6, #CCM_CBCDR]
	orr	r0, r0, #(1 << 25)
	str	r0, [r6, #CCM_CBCDR]

periph_clk_switch_50:
	ldr	r0, [r6, #CCM_CDHIPR]
	cmp	r0, #0
	bne	periph_clk_switch_50

switch_pre_periph_clk_50:

	/* now switch pre_periph_clk to PFD_200MHz. */
	ldr	r0, [r6, #CCM_CBCMR]
	orr	r0, r0, #(0xc << 16)
	str	r0, [r6, #CCM_CBCMR]

	/*
	 * set the MMDC_DIV=4, AXI_DIV = 4, AHB_DIV=8,
	 */
	ldr	r0, [r6, #CCM_CBCDR]
	ldr	r2, =0x3f1f00
	bic	r0, r0, r2
	orr	r0, r0, #(0x18 << 16)
	orr	r0, r0, #(0x3 << 16)

	/*
	 * if changing AHB divider remember to change
	 * the IPGPER divider too below.
	 */
	orr	r0, r0, #0x1d00
	str	r0, [r6, #CCM_CBCDR]

wait_div_update_50:
	ldr	r0, [r6, #CCM_CDHIPR]
	cmp	r0, #0
	bne	wait_div_update_50

	/* now switch periph_clk back. */
	ldr	r0, [r6, #CCM_CBCDR]
	bic	r0, r0, #(1 << 25)
	str	r0, [r6, #CCM_CBCDR]

periph_clk_switch2:
	ldr	r0, [r6, #CCM_CDHIPR]
	cmp	r0, #0
	bne	periph_clk_switch2

	.endm

	.macro	switch_to_24MHz
	/*
	 * change the freq now try setting DDR to 24MHz.
	 * source it from the periph_clk2 ensure the
	 * periph_clk2 is sourced from 24MHz and the
	 * divider is 1.
	 */

	ldr	r0, [r6, #CCM_CBCMR]
	bic	r0, r0, #(0x3 << 12)
	orr	r0, r0, #(1 << 12)
	str	r0, [r6, #CCM_CBCMR]

	ldr	r0, [r6, #CCM_CBCDR]
	bic	r0, r0, #(0x38 << 24)
	str	r0, [r6, #CCM_CBCDR]

	/* now switch periph_clk to 24MHz. */
	ldr	r0, [r6, #CCM_CBCDR]
	orr	r0, r0, #(1 << 25)
	str	r0, [r6, #CCM_CBCDR]

periph_clk_switch1:
	ldr	r0, [r6, #CCM_CDHIPR]
	cmp	r0, #0
	bne	periph_clk_switch1

	/* change all the dividers to 1. */
	ldr	r0, [r6, #CCM_CBCDR]
	ldr	r2, =0x3f1f00
	bic	r0, r0, r2
	orr	r0, r0, #(1 << 8)
	str	r0, [r6, #CCM_CBCDR]

	/* Wait for the divider to change. */
wait_div_update:
	ldr	r0, [r6, #CCM_CDHIPR]
	cmp	r0, #0
	bne	wait_div_update

	.endm

	.macro	disable_l1_dcache

	/*
	 * Flush all data from the L1 data cache before disabling
	 * SCTLR.C bit.
	 */
	push	{r0 - r11, lr}

	ldr	r7, =v7_flush_kern_cache_all
	mov	lr, pc
	mov	pc, r7
	pop	{r0 - r11, lr}

	/* disable d-cache */
	mrc	p15, 0, r6, c1, c0, 0
	bic	r6, r6, #0x4
	mcr	p15, 0, r6, c1, c0, 0
	dsb
	isb

	push	{r0 - r11, lr}

	ldr	r7, =v7_flush_kern_cache_all
	mov	lr, pc
	mov	pc, r7
	pop	{r0 - r11, lr}

	.endm

/*
 *  mx6_ddr3_freq_change
 *
 *  idle the processor (eg, wait for interrupt).
 *  make sure DDR is in self-refresh.
 *  IRQs are already disabled.
 */
ENTRY(mx6_ddr3_freq_change)

mx6_ddr3_freq_change_start:
	stmfd	sp!, {r4-r12}

	/*
	 * r5 -> mmdc_base
	 * r6 -> ccm_base
	 * r7 -> iomux_base
	 * r12 -> l2_base
	 */
	mov	r4, r0
	mov	r8, r1
	mov	r9, r2
	mov	r11, r3

	/* flush the TLB */
	ldr	r6, =0x0
	mcr	p15, 0, r6, c8, c3, 0

	ldr	r6, =iram_tlb_phys_addr
	ldr	r7, [r6]

	/*
	  * Need to flush and disable L1 before
	  * disabling L2, we need data to
	  * coherent. Flushing L1 pushes
	  * everyhting to L2. We sync L2 later, but
	  * it can still have dirty lines.
	  * While exiting, we need to enable L2 first
	  * and then L1.
	  */
	disable_l1_dcache

#ifdef CONFIG_CACHE_L2X0
	/*
	 * Make sure the L2 buffers are drained.
	 * Sync operation on L2 drains the buffers.
	 */
	ldr	r12, =IMX_IO_P2V(MX6Q_L2_BASE_ADDR)

	/* Wait for background operations to complete. */
wait_for_l2_to_idle:
	ldr	r1, [r12, #L2_CACHE_SYNC]
	cmp	r1, #0x0
	bne	wait_for_l2_to_idle

	mov	r1, #0x0
	str	r1, [r12, #L2_CACHE_SYNC]

	/* Disable L2. */
	str	r1, [r12, #0x100]

	dsb
	isb
#endif

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


	/* Now switch the TTBR. */
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

	dsb
	isb


	ldr	r5, =IMX_IO_P2V(MX6Q_MMDC_P0_BASE_ADDR)
	ldr	r6, =IMX_IO_P2V(MX6Q_CCM_BASE_ADDR)
	ldr	r7, =IMX_IO_P2V(MX6Q_IOMUXC_BASE_ADDR)

	/* Read the Original MU delay value */
	ldr	r1, [r5, #MMDC0_MPMUR0]
	mov	r10, r1, lsr #16
	ldr	r1, =0x3ff
	and	r10, r10, r1

	/* disable automatic power saving. */
	ldr	r0, [r5, #MMDC0_MAPSR]
	orr	r0, r0, #0x01
	str	r0, [r5, #MMDC0_MAPSR]

	/* disable MMDC power down timer. */
	ldr	r0, [r5, #MMDC0_MDPDC]
	bic	r0, r0, #(0xff << 8)
	str	r0, [r5, #MMDC0_MDPDC]

	/* delay for a while */
	ldr	r1, =4
delay1:
	ldr	r2, =0
cont1:
	ldr	r0, [r5, r2]
	add	r2, r2, #4
	cmp	r2, #16
	bne	cont1
	sub	r1, r1, #1
	cmp	r1, #0
	bgt	delay1

	/* set CON_REG */
	ldr	r0, =0x8000
	str	r0, [r5, #MMDC0_MDSCR]
poll_conreq_set_1:
	ldr	r0, [r5, #MMDC0_MDSCR]
	and	r0, r0, #(0x4 << 12)
	cmp	r0, #(0x4 << 12)
	bne	poll_conreq_set_1

	/*
	 * if requested frequency is great than
	 * 300MHz, skip setting bypass adopt mode.
	 */
	ldr	r1, =300000000
	cmp	r4, r1
	bge	1f

	is_mx6qp
	bne	1f
	/* Switch to adopt mode, set MMDC0_MAARCR bit25~26 to 2b'01 */
	ldr	r0, [r5, #MMDC0_MAARCR]
	bic	r0, r0, #(0x3 << 25)
	orr	r0, #(0x01 << 25)
	str	r0 , [r5, #MMDC0_MAARCR]
1:
	ldr	r0, =0x00008050
	str	r0, [r5, #MMDC0_MDSCR]
	ldr	r0, =0x00008058
	str	r0, [r5, #MMDC0_MDSCR]

	/*
	 * if requested frequency is greater than
	 * 300MHz go to DLL on mode.
	 */
	ldr	r1, =300000000
	cmp	r4, r1
	bge	dll_on_mode

dll_off_mode:

	/* if DLL is currently on, turn it off. */
	cmp	r9, #1
	beq	continue_dll_off_1

	ldr	r0, =0x00018031
	str	r0, [r5, #MMDC0_MDSCR]

	ldr	r0, =0x00018039
	str	r0, [r5, #MMDC0_MDSCR]

	ldr	r1, =10
delay1a:
	ldr	r2, =0
cont1a:
	ldr	r0, [r5, r2]
	add	r2, r2, #4
	cmp	r2, #16
	bne	cont1a
	sub	r1, r1, #1
	cmp	r1, #0
	bgt	delay1a

continue_dll_off_1:
	/* set DVFS - enter self refresh mode */
	ldr	r0, [r5, #MMDC0_MAPSR]
	orr	r0, r0, #(1 << 21)
	str	r0, [r5, #MMDC0_MAPSR]

	/* de-assert con_req */
	mov	r0, #0x0
	str	r0, [r5, #MMDC0_MDSCR]

poll_dvfs_set_1:
	ldr	r0, [r5, #MMDC0_MAPSR]
	and	r0, r0, #(1 << 25)
	cmp	r0, #(1 << 25)
	bne	poll_dvfs_set_1

	ldr	r1, =24000000
	cmp	r4, r1
	beq	switch_freq_24

	switch_to_50MHz
	b	continue_dll_off_2

switch_freq_24:
	switch_to_24MHz

continue_dll_off_2:

	/* set SBS - block ddr accesses */
	ldr	r0, [r5, #MMDC0_MADPCR0]
	orr	r0, r0, #(1 << 8)
	str	r0, [r5, #MMDC0_MADPCR0]

	/* clear DVFS - exit from self refresh mode */
	ldr	r0, [r5, #MMDC0_MAPSR]
	bic	r0, r0, #(1 << 21)
	str	r0, [r5, #MMDC0_MAPSR]

poll_dvfs_clear_1:
	ldr	r0, [r5, #MMDC0_MAPSR]
	and	r0, r0, #(1 << 25)
	cmp	r0, #(1 << 25)
	beq	poll_dvfs_clear_1

	/* if DLL was previously on, continue DLL off routine. */
	cmp    	r9, #1
	beq 	continue_dll_off_3

	ldr	r0, =0x00018031
	str	r0, [r5, #MMDC0_MDSCR]

	ldr	r0, =0x00018039
	str	r0, [r5, #MMDC0_MDSCR]

	ldr	r0, =0x08208030
	str	r0, [r5, #MMDC0_MDSCR]

	ldr	r0, =0x08208038
	str	r0, [r5, #MMDC0_MDSCR]

	ldr	r0, =0x00088032
	str	r0, [r5, #MMDC0_MDSCR]

	ldr	r0, =0x0008803A
	str	r0, [r5, #MMDC0_MDSCR]

	/* delay for a while. */
	ldr	r1, =4
delay_1:
	ldr	r2, =0
cont_1:
	ldr	r0, [r5, r2]
	add	r2, r2, #4
	cmp	r2, #16
	bne	cont_1
	sub	r1, r1, #1
	cmp	r1, #0
	bgt	delay_1

	ldr	r0, [r5, #MMDC0_MDCF0]
	bic	r0, r0, #0xf
	orr	r0, r0, #0x3
	str	r0, [r5, #MMDC0_MDCF0]

	ldr	r0, [r5, #MMDC0_MDCF1]
	bic	r0, r0, #0x7
	orr	r0, r0, #0x4
	str	r0, [r5, #MMDC0_MDCF1]

	ldr	r0, =0x00011680
	str	r0, [r5, #MMDC0_MDMISC]

	/* enable dqs pull down in the IOMUX. */
	ldr	r1, [r11]
	add	r11, r11, #8
	ldr	r2, =0x3028
update_iomux:
	ldr	r0, [r11, #0x0]
	ldr	r3, [r7, r0]
	bic	r3, r3, r2
	orr	r3, r3, #(0x3 << 12)
	orr	r3, r3, #0x28
	str	r3, [r7, r0]
	add	r11, r11, #8
	sub	r1, r1, #1
	cmp	r1, #0
	bgt	update_iomux

	/*  ODT disabled. */
	ldr	r0, =0x0
	ldr	r2, =MMDC0_MPODTCTRL
	str	r0, [r5, r2]
	ldr	r2, =MMDC1_MPODTCTRL
	str	r0, [r5, r2]

	/* DQS gating disabled. */
	ldr	r2, =MMDC0_MPDGCTRL0
	ldr	r0, [r5, r2]
	orr	r0, r0, #(1 << 29)
	str	r0, [r5, r2]

	ldr	r2, =MMDC1_MPDGCTRL0
	ldr	r0, [r5, r2]
	orr	r0, r0, #(0x1 << 29)
	str	r0, [r5, r2]

	/* Add workaround for ERR005778.*/
	/* double the original MU_UNIT_DEL_NUM. */
	lsl	r10, r10, #1

	/* Bypass the automatic MU by setting the mu_byp_en */
	ldr	r2, [r5, #MMDC0_MPMUR0]
	orr	r2, r2, #0x400
	orr	r2, r2, r10
	str	r2, [r5, #MMDC0_MPMUR0]
	ldr	r0, =MMDC1_MPMUR0
	str	r2, [r5, r0]

	/* Now perform a force measure */
	ldr	r0, [r5, #MMDC0_MPMUR0]
	orr	r0, r0, #0x800
	str	r0, [r5, #MMDC0_MPMUR0]
	ldr	r2, =MMDC1_MPMUR0
	str	r0, [r5, r2]
	/* Wait for FRC_MSR to clear. */
1:
	ldr	r0, [r5, #MMDC0_MPMUR0]
	and	r0, r0, #0x800
	ldr	r1, [r5, r2]
	and	r1, r1, #0x800
	orr	r0, r0, r1
	cmp	r0, #0x0
	bne	1b

continue_dll_off_3:
	/* clear SBS - unblock accesses to DDR. */
	ldr	r0, [r5, #MMDC0_MADPCR0]
	bic	r0, r0, #(0x1 << 8)
	str	r0, [r5, #MMDC0_MADPCR0]

	mov	r0, #0x0
	str	r0, [r5, #MMDC0_MDSCR]
poll_conreq_clear_1:
	ldr	r0, [r5, #MMDC0_MDSCR]
	and	r0, r0, #(0x4 << 12)
	cmp	r0, #(0x4 << 12)
	beq	poll_conreq_clear_1

	b	done

dll_on_mode:
	/* assert DVFS - enter self refresh mode. */
	ldr	r0, [r5, #MMDC0_MAPSR]
	orr	r0, r0, #(1 << 21)
	str	r0, [r5, #MMDC0_MAPSR]

	/* de-assert CON_REQ. */
	mov	r0, #0x0
	str	r0, [r5, #MMDC0_MDSCR]

	/* poll DVFS ack. */
poll_dvfs_set_2:
	ldr	r0, [r5, #MMDC0_MAPSR]
	and	r0, r0, #(1 << 25)
	cmp	r0, #(1 << 25)
	bne	poll_dvfs_set_2

	ldr	r1, =528000000
	cmp	r4, r1
	beq	switch_freq_528

	switch_to_400MHz

	b	continue_dll_on

switch_freq_528:
	switch_to_528MHz

continue_dll_on:

	/* set SBS step-by-step mode. */
	ldr	r0, [r5, #MMDC0_MADPCR0]
	orr	r0, r0, #( 1 << 8)
	str	r0, [r5, #MMDC0_MADPCR0]

	/* clear DVFS - exit self refresh mode. */
	ldr	r0, [r5, #MMDC0_MAPSR]
	bic	r0, r0, #(1 << 21)
	str	r0, [r5, #MMDC0_MAPSR]

poll_dvfs_clear_2:
	ldr	r0, [r5, #MMDC0_MAPSR]
	and	r0, r0, #(1 << 25)
	cmp	r0, #(1 << 25)
	beq	poll_dvfs_clear_2

	/* if DLL is currently off, turn it back on. */
	cmp	r9, #0
	beq	update_calibration_only

	ldr	r0, =0xa1390003
	str	r0, [r5, #MMDC0_MPZQHWCTRL]
	ldr	r2, =MMDC1_MPZQHWCTRL
	str	r0, [r5, r2]

	/* enable DQS gating. */
	ldr	r2, =MMDC0_MPDGCTRL0
	ldr	r0, [r5, r2]
	bic	r0, r0, #(1 << 29)
	str	r0, [r5, r2]

	ldr	r2, =MMDC1_MPDGCTRL0
	ldr	r0, [r5, r2]
	bic	r0, r0, #(1 << 29)
	str	r0, [r5, r2]

	/* force measure. */
	ldr	r0, =0x00000800
	str	r0, [r5, #MMDC0_MPMUR0]
	ldr	r2, =MMDC1_MPMUR0
	str	r0, [r5, r2]

	/* Wait for FRC_MSR to clear. */
1:
	ldr	r0, [r5, #MMDC0_MPMUR0]
	and	r0, r0, #0x800
	ldr	r1, [r5, r2]
	and	r1, r1, #0x800
	orr	r0, r0, r1
	cmp	r0, #0x0
	bne	1b

	/* disable dqs pull down in the IOMUX. */
	ldr	r1, [r11]
	add	r11, r11, #8
update_iomux1:
	ldr	r0, [r11, #0x0]
	ldr	r3, [r11, #0x4]
	str	r3, [r7, r0]
	add	r11, r11, #8
	sub	r1, r1, #1
	cmp	r1, #0
	bgt	update_iomux1

	/* config MMDC timings to 528MHz. */
	ldr	r9, [r8]
	add	r8, r8, #8
	ldr	r0, [r8, #0x0]
	ldr	r3, [r8, #0x4]
	str	r3, [r5, r0]
	add	r8, r8, #8

	ldr	r0, [r8, #0x0]
	ldr	r3, [r8, #0x4]
	str	r3, [r5, r0]
	add	r8, r8, #8

	/* update MISC register: WALAT, RALAT */
	ldr	r0, =0x00001740
	str	r0, [r5, #MMDC0_MDMISC]

	/* configure ddr devices to dll on, odt. */
	ldr	r0, =0x00048031
	str	r0, [r5, #MMDC0_MDSCR]

	ldr	r0, =0x00048039
	str	r0, [r5, #MMDC0_MDSCR]

	/* delay for while. */
	ldr	r1, =4
delay7:
	ldr	r2, =0
cont7:
	ldr	r0, [r5, r2]
	add	r2, r2, #4
	cmp	r2, #16
	bne	cont7
	sub	r1, r1, #1
	cmp	r1, #0
	bgt	delay7

	/* reset dll. */
	ldr	r0, =0x09408030
	str	r0, [r5, #MMDC0_MDSCR]

	ldr	r0, =0x09408038
	str	r0, [r5, #MMDC0_MDSCR]

	/* delay for while. */
	ldr	r1, =100
delay8:
	ldr	r2, =0
cont8:
	ldr	r0, [r5, r2]
	add	r2, r2, #4
	cmp	r2, #16
	bne	cont8
	sub	r1, r1, #1
	cmp	r1, #0
	bgt	delay8

	ldr	r0, [r8, #0x0]
	ldr	r3, [r8, #0x4]
	str	r3, [r5, r0]
	add	r8, r8, #8

	ldr	r0, [r8, #0x0]
	ldr	r3, [r8, #0x4]
	str	r3, [r5, r0]
	add	r8, r8, #8

	ldr	r0, =0x00428031
	str	r0, [r5, #MMDC0_MDSCR]

	ldr	r0, =0x00428039
	str	r0, [r5, #MMDC0_MDSCR]

	ldr	r0, [r8, #0x0]
	ldr	r3, [r8, #0x4]
	str	r3, [r5, r0]
	add	r8, r8, #8

	ldr	r0, [r8, #0x0]
	ldr	r3, [r8, #0x4]
	str	r3, [r5, r0]
	add	r8, r8, #8

	/* issue a zq command. */
	ldr	r0, =0x04008040
	str	r0, [r5, #MMDC0_MDSCR]

	ldr	r0, =0x04008048
	str	r0, [r5, #MMDC0_MDSCR]

	/* MMDC ODT enable. */
	ldr	r0, [r8, #0x0]
	ldr	r3, [r8, #0x4]
	str	r3, [r5, r0]
	add	r8, r8, #8

	ldr	r2, =0x4818
	str	r3, [r5, r2]

	/* delay for while. */
	ldr	r1, =40
delay15:
	ldr	r2, =0
cont15:
	ldr	r0, [r5, r2]
	add	r2, r2, #4
	cmp	r2, #16
	bne	cont15
	sub	r1, r1, #1
	cmp	r1, #0
	bgt	delay15

	/* enable MMDC power down timer. */
	ldr	r0, [r5, #MMDC0_MDPDC]
	orr	r0, r0, #(0x55 << 8)
	str	r0, [r5, #MMDC0_MDPDC]

	b	update_calibration

update_calibration_only:
	ldr	r1, [r8]
	sub	r1, r1, #7
	add	r8, r8, #64
	b	update_calib

update_calibration:
	/* write the new calibration values. */
	mov	r1, r9
	sub	r1, r1, #7

update_calib:
	ldr	r0, [r8, #0x0]
	ldr	r3, [r8, #0x4]
	str	r3, [r5, r0]
	add	r8, r8, #8
	sub	r1, r1, #1
	cmp	r1, #0
	bgt	update_calib

	/* perform a force measurement. */
	ldr	r0, =0x800
	str	r0, [r5, #MMDC0_MPMUR0]
	ldr	r2, =MMDC1_MPMUR0
	str	r0, [r5, r2]

	/* Wait for FRC_MSR to clear. */
1:
	ldr	r0, [r5, #MMDC0_MPMUR0]
	and	r0, r0, #0x800
	ldr	r1, [r5, r2]
	and	r1, r1, #0x800
	orr	r0, r0, r1
	cmp	r0, #0x0
	bne	1b

	/* clear SBS - unblock DDR accesses. */
	ldr	r0, [r5, #MMDC0_MADPCR0]
	bic	r0, r0, #(1 << 8)
	str	r0, [r5, #MMDC0_MADPCR0]

	is_mx6qp
	bne	3f
	/*
	 * Switch back to adopt_bp mode, set MMDC0_MAARCR
	 * bit25~26 to 2b'10.
	 */
	ldr	r0, [r5, #MMDC0_MAARCR]
	bic	r0, r0, #(0x3 << 25)
	orr	r0, r0, #(0x2 << 25)
	str	r0, [r5, #MMDC0_MAARCR]
3:
	mov	r0, #0x0
	str	r0, [r5, #MMDC0_MDSCR]
poll_conreq_clear_2:
	ldr	r0, [r5, #MMDC0_MDSCR]
	and	r0, r0, #(0x4 << 12)
	cmp	r0, #(0x4 << 12)
	beq	poll_conreq_clear_2

done:
	/* MMDC0_MAPSR adopt power down enable. */
	ldr	r0, [r5, #MMDC0_MAPSR]
	bic	r0, r0, #0x01
	str	r0, [r5, #MMDC0_MAPSR]

#ifdef CONFIG_CACHE_L2X0
	/* Enable L2. */
	ldr	r7, =IMX_IO_P2V(MX6Q_L2_BASE_ADDR)
	ldr	r6, =0x1
	str	r6, [r7, #0x100]
	isb
	dsb
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

	isb

	/* Flush the Branch Target Address Cache (BTAC) */
	ldr	r6, =0x0
	mcr	p15, 0, r6, c7, c1, 6
	isb
	dsb

	/* restore registers */
	ldmfd	sp!, {r4-r12}
	mov	pc, lr

	/*
	 * Add ltorg here to ensure that all
	 * literals are stored here and are
	 * within the text space.
	 */
	.ltorg
mx6_ddr3_freq_change_end:

#ifdef CONFIG_SMP
	.align 3

ENTRY(wfe_ddr3_freq_change)
wfe_ddr3_freq_change_start:
	push	{r4 - r11, lr}

	mov	r6, r0
	mov	r7, r1

	dsb
	isb

	disable_l1_dcache

	isb

	/* Turn off SMP bit. */
	mrc	p15, 0, r8, c1, c0, 1
	bic	r8, r8, #0x40
	mcr	p15, 0, r8, c1, c0, 1

	isb

	/* Inform the SCU we are going to enter WFE. */
	push	{r0 - r11, lr}

	ldr	r0,=imx_scu_base
	ldr	r0, [r0]
	mov	r1, #SCU_PM_DORMANT
	ldr	r3, =scu_power_mode
	mov	lr, pc
	mov	pc, r3

	pop	{r0 - r11, lr}

go_back_wfe:
	wfe

	ldr	r3, [r7]
	cmp	r3, #1
	beq	go_back_wfe

	/* Turn ON SMP bit. */
	mrc	p15, 0, r8, c1, c0, 1
	orr	r8, r8, #0x40
	mcr	p15, 0, r8, c1, c0, 1

	isb
	/* Enable L1 data cache. */
	mrc	p15, 0, r8, c1, c0, 0
	orr	r8, r8, #0x4
	mcr	p15, 0, r8, c1, c0, 0
	isb

	/* Inform the SCU we have exited WFE. */
	push	{r0 - r11, lr}

	ldr	r0,=imx_scu_base
	ldr	r0, [r0]
	mov	r1, #SCU_PM_NORMAL
	ldr	r3, =scu_power_mode
	mov	lr, pc
	mov	pc, r3

	pop	{r0 - r11, lr}

	/* Pop all saved registers. */
	pop	{r4 - r11, lr}
	mov	pc, lr
	.ltorg
wfe_ddr3_freq_change_end:
#endif
