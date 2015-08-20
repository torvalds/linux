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
#include "hardware.h"

.globl imx6_up_ddr3_freq_change_start
.globl imx6_up_ddr3_freq_change_end

#define MMDC0_MDPDC		0x4
#define MMDC0_MDCF0		0xc
#define MMDC0_MDCF1		0x10
#define MMDC0_MDMISC		0x18
#define MMDC0_MDSCR		0x1c
#define MMDC0_MAPSR		0x404
#define MMDC0_MADPCR0		0x410
#define MMDC0_MPZQHWCTRL	0x800
#define MMDC0_MPODTCTRL		0x818
#define MMDC0_MPDGCTRL0		0x83c
#define MMDC0_MPMUR0		0x8b8

#define CCM_CBCDR		0x14
#define CCM_CBCMR		0x18
#define CCM_CSCMR1		0x1c
#define CCM_CDHIPR		0x48

#define L2_CACHE_SYNC		0x730

#define BUSFREQ_INFO_FREQ_OFFSET		0x0
#define BUSFREQ_INFO_DDR_SETTINGS_OFFSET	0x4
#define BUSFREQ_INFO_DLL_OFF_OFFSET		0x8
#define BUSFREQ_INFO_IOMUX_OFFSETS_OFFSET	0xc
#define BUSFREQ_INFO_MU_DELAY_OFFSET		0x10

.extern iram_tlb_phys_addr

	.align 3

	/* Check if the cpu is cortex-a7 */
	.macro is_ca7

	/* Read the primary cpu number is MPIDR */
	mrc	p15, 0, r7, c0, c0, 0
	ldr	r8, =0xfff0
	and	r7, r7, r8
	ldr	r8, =0xc070
	cmp	r7, r8

	.endm

	.macro do_delay

1:
	ldr	r9, =0
2:
	ldr	r10, [r4, r9]
	add	r9, r9, #4
	cmp	r9, #16
	bne	2b
	sub	r8, r8, #1
	cmp	r8, #0
	bgt	1b

	.endm

	.macro wait_for_ccm_handshake

3:
	ldr	r8, [r5, #CCM_CDHIPR]
	cmp	r8, #0
	bne	3b

	.endm

	.macro	switch_to_400MHz

	/* check whether periph2_clk is already from top path */
	ldr	r8, [r5, #CCM_CBCDR]
	ands	r8, #(1 << 26)
	beq	skip_periph2_clk2_switch_400m

	/* now switch periph2_clk back. */
	ldr	r8, [r5, #CCM_CBCDR]
	bic	r8, r8, #(1 << 26)
	str	r8, [r5, #CCM_CBCDR]

	wait_for_ccm_handshake

	/*
	 * on i.MX6SX, pre_periph2_clk will be always from
	 * pll2_pfd2, so no need to set pre_periph2_clk
	 * parent, just set the mmdc divider directly.
	 */
skip_periph2_clk2_switch_400m:

	/* fabric_mmdc_podf to 0 */
	ldr	r8, [r5, #CCM_CBCDR]
	bic	r8, r8, #(0x7 << 3)
	str	r8, [r5, #CCM_CBCDR]

	wait_for_ccm_handshake

	.endm

	.macro	switch_to_50MHz

	/* check whether periph2_clk is already from top path */
	ldr	r8, [r5, #CCM_CBCDR]
	ands	r8, #(1 << 26)
	beq	skip_periph2_clk2_switch_50m

	/* now switch periph2_clk back. */
	ldr	r8, [r5, #CCM_CBCDR]
	bic	r8, r8, #(1 << 26)
	str	r8, [r5, #CCM_CBCDR]

	wait_for_ccm_handshake

	/*
	 * on i.MX6SX, pre_periph2_clk will be always from
	 * pll2_pfd2, so no need to set pre_periph2_clk
	 * parent, just set the mmdc divider directly.
	 */
skip_periph2_clk2_switch_50m:

	/* fabric_mmdc_podf to 7 so that mmdc is 400 / 8 = 50MHz */
	ldr	r8, [r5, #CCM_CBCDR]
	orr	r8, r8, #(0x7 << 3)
	str	r8, [r5, #CCM_CBCDR]

	wait_for_ccm_handshake

	.endm

	.macro	switch_to_24MHz

	/* periph2_clk2 sel to OSC_CLK */
	ldr	r8, [r5, #CCM_CBCMR]
	orr	r8, r8, #(1 << 20)
	str	r8, [r5, #CCM_CBCMR]

	/* periph2_clk2_podf to 0 */
	ldr	r8, [r5, #CCM_CBCDR]
	bic	r8, r8, #0x7
	str	r8, [r5, #CCM_CBCDR]

	/* periph2_clk sel to periph2_clk2 */
	ldr	r8, [r5, #CCM_CBCDR]
	orr	r8, r8, #(0x1 << 26)
	str	r8, [r5, #CCM_CBCDR]

	wait_for_ccm_handshake

	/* fabric_mmdc_podf to 0 */
	ldr	r8, [r5, #CCM_CBCDR]
	bic	r8, r8, #(0x7 << 3)
	str	r8, [r5, #CCM_CBCDR]

	wait_for_ccm_handshake

	.endm

/*
 *  imx6_up_ddr3_freq_change
 *  Below code can be used by i.MX6SX and i.MX6UL.
 *
 *  idle the processor (eg, wait for interrupt).
 *  make sure DDR is in self-refresh.
 *  IRQs are already disabled.
 */
ENTRY(imx6_up_ddr3_freq_change)

imx6_up_ddr3_freq_change_start:
	stmfd	sp!, {r4 - r11}

	ldr	r1, [r0, #BUSFREQ_INFO_DDR_SETTINGS_OFFSET]
	ldr	r2, [r0, #BUSFREQ_INFO_DLL_OFF_OFFSET]
	ldr	r3, [r0, #BUSFREQ_INFO_IOMUX_OFFSETS_OFFSET]

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
	 *    and 2-4G is translated by TTBR1.
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

	dsb
	isb

	/* Disable L1 data cache. */
	mrc	p15, 0, r6, c1, c0, 0
	bic	r6, r6, #0x4
	mcr	p15, 0, r6, c1, c0, 0

	ldr	r4, =IMX_IO_P2V(MX6Q_MMDC_P0_BASE_ADDR)
	ldr	r5, =IMX_IO_P2V(MX6Q_CCM_BASE_ADDR)
	ldr	r6, =IMX_IO_P2V(MX6Q_IOMUXC_BASE_ADDR)

	is_ca7
	beq	skip_disable_l2

#ifdef CONFIG_CACHE_L2X0
	/*
	 * make sure the L2 buffers are drained,
	 * sync operation on L2 drains the buffers.
	 */
	ldr	r8, =IMX_IO_P2V(MX6Q_L2_BASE_ADDR)

	/* Wait for background operations to complete. */
wait_for_l2_to_idle:
	ldr	r7, [r8, #0x730]
	cmp	r7, #0x0
	bne	wait_for_l2_to_idle

	mov	r7, #0x0
	str	r7, [r8, #L2_CACHE_SYNC]

	/* Disable L2. */
	mov	r7, #0x0
	str	r7, [r8, #0x100]

	/*
	 * The second dsb might be needed to keep cache sync (device write)
	 * ordering with the memory accesses before it.
	 */
	dsb
	isb
#endif

skip_disable_l2:
	/* disable automatic power saving. */
	ldr	r8, [r4, #MMDC0_MAPSR]
	orr	r8, r8, #0x1
	str	r8, [r4, #MMDC0_MAPSR]

	/* disable MMDC power down timer. */
	ldr	r8, [r4, #MMDC0_MDPDC]
	bic	r8, r8, #(0xff << 8)
	str	r8, [r4, #MMDC0_MDPDC]

	/* delay for a while */
	ldr	r8, =4
	do_delay

	/* set CON_REG */
	ldr	r8, =0x8000
	str	r8, [r4, #MMDC0_MDSCR]
poll_conreq_set_1:
	ldr	r8, [r4, #MMDC0_MDSCR]
	and	r8, r8, #(0x4 << 12)
	cmp	r8, #(0x4 << 12)
	bne	poll_conreq_set_1

	/*
	 * if requested frequency is greater than
	 * 300MHz go to DLL on mode.
	 */
	ldr	r8, [r0, #BUSFREQ_INFO_FREQ_OFFSET]
	ldr	r9, =300000000
	cmp	r8, r9
	bge	dll_on_mode

dll_off_mode:
	/* if DLL is currently on, turn it off. */
	cmp	r2, #1
	beq	continue_dll_off_1

	ldr	r8, =0x00018031
	str	r8, [r4, #MMDC0_MDSCR]

	ldr	r8, =0x00018039
	str	r8, [r4, #MMDC0_MDSCR]

	ldr	r8, =10
	do_delay

continue_dll_off_1:
	/* set DVFS - enter self refresh mode */
	ldr	r8, [r4, #MMDC0_MAPSR]
	orr	r8, r8, #(1 << 21)
	str	r8, [r4, #MMDC0_MAPSR]

	/* de-assert con_req */
	mov	r8, #0x0
	str	r8, [r4, #MMDC0_MDSCR]

poll_dvfs_set_1:
	ldr	r8, [r4, #MMDC0_MAPSR]
	and	r8, r8, #(1 << 25)
	cmp	r8, #(1 << 25)
	bne	poll_dvfs_set_1

	ldr	r8, [r0, #BUSFREQ_INFO_FREQ_OFFSET]
	ldr	r9, =24000000
	cmp	r8, r9
	beq	switch_freq_24

	switch_to_50MHz
	b	continue_dll_off_2

switch_freq_24:
	switch_to_24MHz

continue_dll_off_2:
	/* set SBS - block ddr accesses */
	ldr	r8, [r4, #MMDC0_MADPCR0]
	orr	r8, r8, #(1 << 8)
	str	r8, [r4, #MMDC0_MADPCR0]

	/* clear DVFS - exit from self refresh mode */
	ldr	r8, [r4, #MMDC0_MAPSR]
	bic	r8, r8, #(1 << 21)
	str	r8, [r4, #MMDC0_MAPSR]

poll_dvfs_clear_1:
	ldr	r8, [r4, #MMDC0_MAPSR]
	and	r8, r8, #(1 << 25)
	cmp	r8, #(1 << 25)
	beq	poll_dvfs_clear_1

	/* if DLL was previously on, continue DLL off routine. */
	cmp    	r2, #1
	beq 	continue_dll_off_3

	ldr	r8, =0x00018031
	str	r8, [r4, #MMDC0_MDSCR]

	ldr	r8, =0x00018039
	str	r8, [r4, #MMDC0_MDSCR]

	ldr	r8, =0x04208030
	str	r8, [r4, #MMDC0_MDSCR]

	ldr	r8, =0x04208038
	str	r8, [r4, #MMDC0_MDSCR]

	ldr	r8, =0x00088032
	str	r8, [r4, #MMDC0_MDSCR]

	ldr	r8, =0x0008803A
	str	r8, [r4, #MMDC0_MDSCR]

	/* delay for a while. */
	ldr	r8, =4
	do_delay

	ldr	r8, [r4, #MMDC0_MDCF0]
	bic	r8, r8, #0xf
	orr	r8, r8, #0x3
	str	r8, [r4, #MMDC0_MDCF0]

	ldr	r8, [r4, #MMDC0_MDCF1]
	bic	r8, r8, #0x7
	orr	r8, r8, #0x4
	str	r8, [r4, #MMDC0_MDCF1]

	ldr	r8, =0x00091680
	str	r8, [r4, #MMDC0_MDMISC]

	/* enable dqs pull down in the IOMUX. */
	ldr	r8, [r3]
	add	r3, r3, #8
	ldr	r9, =0x3028
update_iomux:
	ldr	r10, [r3]
	ldr	r11, [r6, r10]
	bic	r11, r11, r9
	orr	r11, r11, #(0x3 << 12)
	orr	r11, r11, #0x28
	str	r11, [r6, r10]
	add	r3, r3, #8
	sub	r8, r8, #1
	cmp	r8, #0
	bgt	update_iomux

	/*  ODT disabled. */
	ldr	r8, =0x0
	str	r8, [r4, #MMDC0_MPODTCTRL]

	/* DQS gating disabled. */
	ldr	r8, [r4, #MMDC0_MPDGCTRL0]
	orr	r8, r8, #(1 << 29)
	str	r8, [r4, #MMDC0_MPDGCTRL0]

	/* Add workaround for ERR005778.*/
	/* double the original MU_UNIT_DEL_NUM. */
	ldr	r8, [r0, #BUSFREQ_INFO_MU_DELAY_OFFSET]
	lsl	r8, r8, #1

	/* Bypass the automatic MU by setting the mu_byp_en */
	ldr	r10, [r4, #MMDC0_MPMUR0]
	orr	r10, r10, #0x400
	/* Set the MU_BYP_VAL */
	orr	r10, r10, r8
	str	r10, [r4, #MMDC0_MPMUR0]

	/* Now perform a force measure */
	ldr	r8, [r4, #MMDC0_MPMUR0]
	orr	r8, r8, #0x800
	str	r8, [r4, #MMDC0_MPMUR0]
	/* Wait for FRC_MSR to clear. */
1:
	ldr	r8, [r4, #MMDC0_MPMUR0]
	and	r8, r8, #0x800
	cmp	r8, #0x0
	bne	1b

continue_dll_off_3:
	/* clear SBS - unblock accesses to DDR. */
	ldr	r8, [r4, #MMDC0_MADPCR0]
	bic	r8, r8, #(0x1 << 8)
	str	r8, [r4, #MMDC0_MADPCR0]

	mov	r8, #0x0
	str	r8, [r4, #MMDC0_MDSCR]
poll_conreq_clear_1:
	ldr	r8, [r4, #MMDC0_MDSCR]
	and	r8, r8, #(0x4 << 12)
	cmp	r8, #(0x4 << 12)
	beq	poll_conreq_clear_1

	b	done

dll_on_mode:
	/* assert DVFS - enter self refresh mode. */
	ldr	r8, [r4, #MMDC0_MAPSR]
	orr	r8, r8, #(1 << 21)
	str	r8, [r4, #MMDC0_MAPSR]

	/* de-assert CON_REQ. */
	mov	r8, #0x0
	str	r8, [r4, #MMDC0_MDSCR]

	/* poll DVFS ack. */
poll_dvfs_set_2:
	ldr	r8, [r4, #MMDC0_MAPSR]
	and	r8, r8, #(1 << 25)
	cmp	r8, #(1 << 25)
	bne	poll_dvfs_set_2

	switch_to_400MHz

	/* set SBS step-by-step mode. */
	ldr	r8, [r4, #MMDC0_MADPCR0]
	orr	r8, r8, #(1 << 8)
	str	r8, [r4, #MMDC0_MADPCR0]

	/* clear DVFS - exit self refresh mode. */
	ldr	r8, [r4, #MMDC0_MAPSR]
	bic	r8, r8, #(1 << 21)
	str	r8, [r4, #MMDC0_MAPSR]

poll_dvfs_clear_2:
	ldr	r8, [r4, #MMDC0_MAPSR]
	ands	r8, r8, #(1 << 25)
	bne	poll_dvfs_clear_2

	/* if DLL is currently off, turn it back on. */
	cmp	r2, #0
	beq	update_calibration_only

	ldr	r8, =0xa5390003
	str	r8, [r4, #MMDC0_MPZQHWCTRL]

	/* enable DQS gating. */
	ldr	r10, =MMDC0_MPDGCTRL0
	ldr	r8, [r4, r10]
	bic	r8, r8, #(1 << 29)
	str	r8, [r4, r10]

	/* Now perform a force measure */
	ldr	r8, =0x00000800
	str	r8, [r4, #MMDC0_MPMUR0]
	/* Wait for FRC_MSR to clear. */
1:
	ldr	r8, [r4, #MMDC0_MPMUR0]
	and	r8, r8, #0x800
	cmp	r8, #0x0
	bne	1b

	/* disable dqs pull down in the IOMUX. */
	ldr	r8, [r3]
	add	r3, r3, #8
update_iomux1:
	ldr	r10, [r3, #0x0]
	ldr	r11, [r3, #0x4]
	str	r11, [r6, r10]
	add	r3, r3, #8
	sub	r8, r8, #1
	cmp	r8, #0
	bgt	update_iomux1

	/* config MMDC timings to 400MHz. */
	ldr	r1, [r0, #BUSFREQ_INFO_DDR_SETTINGS_OFFSET]
	ldr	r7, [r1]
	add	r1, r1, #8
	ldr	r10, [r1, #0x0]
	ldr	r11, [r1, #0x4]
	str	r11, [r4, r10]
	add	r1, r1, #8

	ldr	r10, [r1, #0x0]
	ldr	r11, [r1, #0x4]
	str	r11, [r4, r10]
	add	r1, r1, #8

	/* update MISC register: WALAT, RALAT */
	ldr	r8, =0x00081740
	str	r8, [r4, #MMDC0_MDMISC]

	/* configure ddr devices to dll on, odt. */
	ldr	r8, =0x00028031
	str	r8, [r4, #MMDC0_MDSCR]

	ldr	r8, =0x00028039
	str	r8, [r4, #MMDC0_MDSCR]

	/* delay for while. */
	ldr	r8, =4
	do_delay

	/* reset dll. */
	ldr	r8, =0x09208030
	str	r8, [r4, #MMDC0_MDSCR]

	ldr	r8, =0x09208038
	str	r8, [r4, #MMDC0_MDSCR]

	/* delay for while. */
	ldr	r8, =100
	do_delay

	ldr	r10, [r1, #0x0]
	ldr	r11, [r1, #0x4]
	str	r11, [r4, r10]
	add	r1, r1, #8

	ldr	r10, [r1, #0x0]
	ldr	r11, [r1, #0x4]
	str	r11, [r4, r10]
	add	r1, r1, #8

	ldr	r8, =0x00428031
	str	r8, [r4, #MMDC0_MDSCR]

	ldr	r8, =0x00428039
	str	r8, [r4, #MMDC0_MDSCR]

	ldr	r10, [r1, #0x0]
	ldr	r11, [r1, #0x4]
	str	r11, [r4, r10]
	add	r1, r1, #8

	ldr	r10, [r1, #0x0]
	ldr	r11, [r1, #0x4]
	str	r11, [r4, r10]
	add	r1, r1, #8

	/* issue a zq command. */
	ldr	r8, =0x04008040
	str	r8, [r4, #MMDC0_MDSCR]

	ldr	r8, =0x04008048
	str	r8, [r4, #MMDC0_MDSCR]

	/* MMDC ODT enable. */
	ldr	r10, [r1, #0x0]
	ldr	r11, [r1, #0x4]
	str	r11, [r4, r10]
	add	r1, r1, #8

	/* delay for while. */
	ldr	r8, =40
	do_delay

	/* enable MMDC power down timer. */
	ldr	r8, [r4, #MMDC0_MDPDC]
	orr	r8, r8, #(0x55 << 8)
	str	r8, [r4, #MMDC0_MDPDC]

	b	update_calibration

update_calibration_only:
	ldr	r8, [r1]
	sub	r8, r8, #7
	add	r1, r1, #64
	b	update_calib

update_calibration:
	/* write the new calibration values. */
	mov	r8, r7
	sub	r8, r8, #7

update_calib:
	ldr	r10, [r1, #0x0]
	ldr	r11, [r1, #0x4]
	str	r11, [r4, r10]
	add	r1, r1, #8
	sub	r8, r8, #1
	cmp	r8, #0
	bgt	update_calib

	/* perform a force measurement. */
	ldr	r8, =0x800
	str	r8, [r4, #MMDC0_MPMUR0]
	/* Wait for FRC_MSR to clear. */
1:
	ldr	r8, [r4, #MMDC0_MPMUR0]
	and	r8, r8, #0x800
	cmp	r8, #0x0
	bne	1b

	/* clear SBS - unblock DDR accesses. */
	ldr	r8, [r4, #MMDC0_MADPCR0]
	bic	r8, r8, #(1 << 8)
	str	r8, [r4, #MMDC0_MADPCR0]

	mov	r8, #0x0
	str	r8, [r4, #MMDC0_MDSCR]
poll_conreq_clear_2:
	ldr	r8, [r4, #MMDC0_MDSCR]
	and	r8, r8, #(0x4 << 12)
	cmp	r8, #(0x4 << 12)
	beq	poll_conreq_clear_2

done:

	/* MMDC0_MAPSR adopt power down enable. */
	ldr	r8, [r4, #MMDC0_MAPSR]
	bic	r8, r8, #0x01
	str	r8, [r4, #MMDC0_MAPSR]

	is_ca7
	beq	skip_enable_l2

#ifdef CONFIG_CACHE_L2X0
	/* Enable L2. */
	ldr	r8, =IMX_IO_P2V(MX6Q_L2_BASE_ADDR)
	ldr	r7, =0x1
	str	r7, [r8, #0x100]
#endif

skip_enable_l2:
	/* Enable L1 data cache. */
	mrc	p15, 0, r7, c1, c0, 0
	orr	r7, r7, #0x4
	mcr	p15, 0, r7, c1, c0, 0

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
	mrc	p15, 0, r7, c1, c0, 0
	orr	r7, r7, #0x800
	mcr	p15, 0, r7, c1, c0, 0

	/* Flush the Branch Target Address Cache (BTAC) */
	ldr	r7, =0x0
	mcr	p15, 0, r7, c7, c1, 6

	/* restore registers */
	ldmfd	sp!, {r4 - r11}
	mov	pc, lr

	/*
	 * Add ltorg here to ensure that all
	 * literals are stored here and are
	 * within the text space.
	 */
	.ltorg
imx6_up_ddr3_freq_change_end:
ENDPROC(imx6_up_ddr3_freq_change)
