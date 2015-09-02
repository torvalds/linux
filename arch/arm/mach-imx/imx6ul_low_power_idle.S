/*
 * Copyright (C) 2015 Freescale Semiconductor, Inc. All Rights Reserved.
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

#define PM_INFO_PBASE_OFFSET			0x0
#define PM_INFO_RESUME_ADDR_OFFSET		0x4
#define PM_INFO_PM_INFO_SIZE_OFFSET		0x8
#define PM_INFO_PM_INFO_TTBR_OFFSET		0xc
#define PM_INFO_MX6Q_MMDC_P_OFFSET		0x10
#define PM_INFO_MX6Q_MMDC_V_OFFSET		0x14
#define PM_INFO_MX6Q_IOMUXC_P_OFFSET		0x18
#define PM_INFO_MX6Q_IOMUXC_V_OFFSET		0x1c
#define PM_INFO_MX6Q_CCM_P_OFFSET		0x20
#define PM_INFO_MX6Q_CCM_V_OFFSET		0x24
#define PM_INFO_MX6Q_GPC_P_OFFSET		0x28
#define PM_INFO_MX6Q_GPC_V_OFFSET		0x2c
#define PM_INFO_MX6Q_ANATOP_P_OFFSET		0x30
#define PM_INFO_MX6Q_ANATOP_V_OFFSET		0x34
#define PM_INFO_MX6Q_SRC_P_OFFSET		0x38
#define PM_INFO_MX6Q_SRC_V_OFFSET		0x3c
#define PM_INFO_MMDC_IO_NUM_OFFSET		0x40
#define PM_INFO_MMDC_IO_VAL_OFFSET		0x44

#define	MX6Q_MMDC_MAPSR		0x404
#define MX6Q_MMDC_MPDGCTRL0	0x83c
#define MX6Q_SRC_GPR1	0x20
#define MX6Q_SRC_GPR2	0x24
#define MX6Q_GPC_IMR1	0x08
#define MX6Q_GPC_IMR2	0x0c
#define MX6Q_GPC_IMR3	0x10
#define MX6Q_GPC_IMR4	0x14
#define MX6Q_CCM_CCR	0x0

.globl mx6ul_lpm_wfi_start
.globl mx6ul_lpm_wfi_end

	.macro	pll_do_wait_lock
1:
	ldr	r7, [r10, r8]
	ands	r7, #0x80000000
	beq	1b

	.endm

	.macro	ccm_do_wait
2:
	ldr	r7, [r10, #0x48]
	cmp	r7, #0x0
	bne	2b

	.endm

	.macro	ccm_enter_idle

	ldr	r10, [r0, #PM_INFO_MX6Q_CCM_V_OFFSET]

	/* set ahb to 3MHz */
	ldr	r7, [r10, #0x14]
	orr	r7, r7, #0x1c00
	str	r7, [r10, #0x14]

	/* set perclk to 6MHz */
	ldr	r7, [r10, #0x1c]
	bic	r7, r7, #0x3f
	orr	r7, r7, #0x3
	str	r7, [r10, #0x1c]

	/* set mmdc to 1MHz, periph2_clk2 need to be @8MHz */
	ldr	r7, [r10, #0x14]
	orr     r7, r7, #0x2
	orr	r7, r7, #(0x7 << 3)
	str	r7, [r10, #0x14]

	ccm_do_wait

	ldr	r10, [r0, #PM_INFO_MX6Q_ANATOP_V_OFFSET]

	/* bypass PLL1 output to OSC */
	ldr	r7, [r10]
	orr	r7, r7, #(0x1 << 16)
	str	r7, [r10]

	ldr	r10, [r0, #PM_INFO_MX6Q_CCM_V_OFFSET]

	/* set pll1_sw to from pll1 main */
	ldr	r7, [r10, #0xc]
	bic	r7, r7, #0x4
	str	r7, [r10, #0xc]

	/* set step from osc */
	ldr	r7, [r10, #0xc]
	bic	r7, r7, #0x100
	str	r7, [r10, #0xc]

	/* set pll1_sw to from step */
	ldr	r7, [r10, #0xc]
	orr	r7, r7, #0x4
	str	r7, [r10, #0xc]

	ldr	r10, [r0, #PM_INFO_MX6Q_ANATOP_V_OFFSET]

	/* Disable PLL1 bypass output */
	ldr	r7, [r10]
	bic	r7, r7, #0x12000
	str	r7, [r10]

	/*
	 * disable pll2, suppose when system enter low
	 * power idle mode, only 396MHz pfd needs pll2,
	 * now we switch arm clock to OSC, we can disable
	 * pll2 now, gate pll2_pfd2 first.
	 */
	ldr	r7, [r10, #0x100]
	orr	r7, #0x800000
	str	r7, [r10, #0x100]

	ldr	r7, [r10, #0x30]
	orr	r7, r7, #0x1000
	bic	r7, r7, #0x2000
	str	r7, [r10, #0x30]

	.endm

	.macro	ccm_exit_idle

	cmp	r5, #0x0
	ldreq	r10, [r0, #PM_INFO_MX6Q_ANATOP_V_OFFSET]
	ldrne	r10, [r0, #PM_INFO_MX6Q_ANATOP_P_OFFSET]

	/* enable pll2 and pll2_pfd2 */
	ldr	r7, [r10, #0x30]
	bic	r7, r7, #0x1000
	orr	r7, r7, #0x2000
	str	r7, [r10, #0x30]

	ldr	r8, =0x30
	pll_do_wait_lock

	ldr	r7, [r10, #0x100]
	bic	r7, #0x800000
	str	r7, [r10, #0x100]

	/* enable PLL1 bypass output */
	ldr	r7, [r10]
	orr	r7, r7, #0x12000
	str	r7, [r10]

	cmp	r5, #0x0
	ldreq	r10, [r0, #PM_INFO_MX6Q_CCM_V_OFFSET]
	ldrne	r10, [r0, #PM_INFO_MX6Q_CCM_P_OFFSET]

	/* set perclk back to 24MHz */
	ldr	r7, [r10, #0x1c]
	bic	r7, r7, #0x3f
	str	r7, [r10, #0x1c]

	/* set mmdc back to 24MHz */
	ldr	r7, [r10, #0x14]
	bic	r7, r7, #0x7
	bic	r7, r7, #(0x7 << 3)
	str	r7, [r10, #0x14]

	/* set ahb div back to 24MHz */
	ldr	r7, [r10, #0x14]
	bic	r7, r7, #0x1c00
	str	r7, [r10, #0x14]

	ccm_do_wait

	/* set pll1_sw to from pll1 main */
	ldr	r7, [r10, #0xc]
	bic	r7, r7, #0x4
	str	r7, [r10, #0xc]

	/* set step from pll2_pfd2 */
	ldr	r7, [r10, #0xc]
	orr	r7, r7, #0x100
	str	r7, [r10, #0xc]

	/* set pll1_sw to from step */
	ldr	r7, [r10, #0xc]
	orr	r7, r7, #0x4
	str	r7, [r10, #0xc]

	cmp	r5, #0x0
	ldreq	r10, [r0, #PM_INFO_MX6Q_ANATOP_V_OFFSET]
	ldrne	r10, [r0, #PM_INFO_MX6Q_ANATOP_P_OFFSET]

	/* Unbypass PLL1 */
	ldr	r7, [r10]
	bic	r7, r7, #(0x1 << 16)
	str	r7, [r10]

	.endm

	.macro	anatop_enter_idle

	ldr	r10, [r0, #PM_INFO_MX6Q_ANATOP_V_OFFSET]

	/*
	 * check whether any PLL is enabled, as only when
	 * there is no PLLs enabled, 2P5 and 1P1 can be
	 * off and only enable weak ones.
	 */

	/* arm pll1 */
	ldr	r7, [r10, #0]
	ands	r7, r7, #(1 << 31)
	bne	10f

	/* sys pll2 */
	ldr	r7, [r10, #0x30]
	ands	r7, r7, #(1 << 31)
	bne	10f

	/* usb pll3 */
	ldr	r7, [r10, #0x10]
	ands	r7, r7, #(1 << 31)
	bne	10f

	/* audio pll4 */
	ldr	r7, [r10, #0x70]
	ands	r7, r7, #(1 << 31)
	bne	10f

	/* vidio pll5 */
	ldr	r7, [r10, #0xa0]
	ands	r7, r7, #(1 << 31)
	bne	10f

	/* enet pll6 */
	ldr	r7, [r10, #0xe0]
	ands	r7, r7, #(1 << 31)
	bne	10f

	/* usb host pll7 */
	ldr	r7, [r10, #0x20]
	ands	r7, r7, #(1 << 31)
	bne	10f

	/* enable weak 2P5 and turn off regular 2P5 */
	ldr	r7, [r10, #0x130]
	orr	r7, r7, #0x40000
	str	r7, [r10, #0x130]
	bic	r7, r7, #0x1
	str	r7, [r10, #0x130]

	/* enable weak 1p1 and turn off regular 1P1 */
	ldr	r7, [r10, #0x110]
	orr	r7, r7, #0x40000
	str	r7, [r10, #0x110]
	bic	r7, r7, #0x1
	str	r7, [r10, #0x110]

	/* check whether ARM LDO is bypassed */
	ldr	r7, [r10, #0x140]
	and	r7, r7, #0x1f
	cmp	r7, #0x1f
	bne	10f

	/* low power band gap enable */
	ldr	r7, [r10, #0x270]
	orr	r7, r7, #0x20
	str	r7, [r10, #0x270]

	/* turn off the bias current from the regular bandgap */
	ldr	r7, [r10, #0x270]
	orr	r7, r7, #0x80
	str	r7, [r10, #0x270]

	/*
	 * clear the REFTOP_SELFBIASOFF,
	 * self-bias circuit of the band gap.
	 * Per RM, should be cleared when
	 * band gap is powered down.
	 */
	ldr	r7, [r10, #0x150]
	bic	r7, r7, #0x8
	str	r7, [r10, #0x150]

	/* turn off regular bandgap */
	ldr	r7, [r10, #0x150]
	orr	r7, r7, #0x1
	str	r7, [r10, #0x150]

	/* switch to RC-OSC */
	ldr	r7, [r10, #0x270]
	orr	r7, r7, #0x10
	str	r7, [r10, #0x270]

	/* turn off XTAL-OSC */
	ldr	r7, [r10, #0x150]
	orr	r7, r7, #0x40000000
	str	r7, [r10, #0x150]
10:
	/* lower OSC current by 37.5% */
	ldr	r7, [r10, #0x150]
	orr	r7, r7, #0x6000
	str	r7, [r10, #0x150]

	/* disconnect vdd_high_in and vdd_snvs_in */
	ldr	r7, [r10, #0x150]
	orr	r7, r7, #0x1000
	str	r7, [r10, #0x150]

	.endm

	.macro anatop_exit_idle

	cmp	r5, #0x0
	ldreq	r10, [r0, #PM_INFO_MX6Q_ANATOP_V_OFFSET]
	ldrne	r10, [r0, #PM_INFO_MX6Q_ANATOP_P_OFFSET]

	/* increase OSC current to normal */
	ldr	r7, [r10, #0x150]
	bic	r7, r7, #0x6000
	str	r7, [r10, #0x150]

	/* turn on XTAL-OSC and detector */
	ldr	r7, [r10, #0x150]
	bic	r7, r7, #0x40000000
	orr	r7, r7, #0x10000
	str	r7, [r10, #0x150]

	/* wait for XTAL stable */
14:
	ldr	r7, [r10, #0x150]
	ands	r7, r7, #0x8000
	beq	14b

	/* switch to XTAL-OSC */
	ldr	r7, [r10, #0x270]
	bic	r7, r7, #0x10
	str	r7, [r10, #0x270]

	/* turn off XTAL-OSC detector */
	ldr	r7, [r10, #0x150]
	bic	r7, r7, #0x10000
	str	r7, [r10, #0x150]
15:
	/* check whether we need to enable 2P5/1P1 */
	ldr	r7, [r10, #0x110]
	ands	r7, r7, #0x40000
	beq	11f

	/* check whether ARM LDO is bypassed */
	ldr	r7, [r10, #0x140]
	and	r7, r7, #0x1f
	cmp	r7, #0x1f
	bne	12f

	/* turn on regular bandgap and wait for stable */
	ldr	r7, [r10, #0x150]
	bic	r7, r7, #0x1
	str	r7, [r10, #0x150]
13:
	ldr	r7, [r10, #0x150]
	ands	r7, #0x80
	beq	13b

	/*
	 * set the REFTOP_SELFBIASOFF,
	 * self-bias circuit of the band gap.
	 */
	ldr     r7, [r10, #0x150]
	orr     r7, r7, #0x8
	str     r7, [r10, #0x150]

	/* turn on the bias current from the regular bandgap */
	ldr	r7, [r10, #0x270]
	bic	r7, r7, #0x80
	str	r7, [r10, #0x270]

	/* low power band gap disable */
	ldr	r7, [r10, #0x270]
	bic	r7, r7, #0x20
	str	r7, [r10, #0x270]
12:
	/* enable regular 2P5 and turn off weak 2P5 */
	ldr	r7, [r10, #0x130]
	orr	r7, r7, #0x1
	str	r7, [r10, #0x130]

	/* Ensure the 2P5 is up. */
3:
	ldr	r7, [r10, #0x130]
	ands	r7, r7, #0x20000
	beq	3b
	ldr	r7, [r10, #0x130]
	bic	r7, r7, #0x40000
	str	r7, [r10, #0x130]

	/* enable regular 1p1 and turn off weak 1P1 */
	ldr	r7, [r10, #0x110]
	orr	r7, r7, #0x1
	str	r7, [r10, #0x110]
4:
	ldr	r7, [r10, #0x110]
	ands	r7, r7, #0x20000
	beq	4b
	ldr	r7, [r10, #0x110]
	bic	r7, r7, #0x40000
	str	r7, [r10, #0x110]
11:
	.endm

	.macro	disable_l1_dcache

	/*
	 * Flush all data from the L1 data cache before disabling
	 * SCTLR.C bit.
	 */
	push	{r0 - r10, lr}
	ldr	r7, =v7_flush_dcache_all
	mov	lr, pc
	mov	pc, r7
	pop	{r0 - r10, lr}

	/* disable d-cache */
	mrc	p15, 0, r7, c1, c0, 0
	bic	r7, r7, #(1 << 2)
	mcr	p15, 0, r7, c1, c0, 0
	dsb
	isb

	push	{r0 - r10, lr}
	ldr	r7, =v7_flush_dcache_all
	mov	lr, pc
	mov	pc, r7
	pop	{r0 - r10, lr}

	.endm

	.macro mmdc_enter_dvfs_mode

	/* disable automatic power savings. */
	ldr	r7, [r10, #MX6Q_MMDC_MAPSR]
	orr	r7, r7, #0x1
	str	r7, [r10, #MX6Q_MMDC_MAPSR]

	/* disable power down timer */
	ldr	r7, [r10, #0x4]
	bic	r7, r7, #0xff00
	str	r7, [r10, #0x4]

	/* make the DDR explicitly enter self-refresh. */
	ldr	r7, [r10, #MX6Q_MMDC_MAPSR]
	orr	r7, r7, #(1 << 21)
	str	r7, [r10, #MX6Q_MMDC_MAPSR]
5:
	ldr	r7, [r10, #MX6Q_MMDC_MAPSR]
	ands	r7, r7, #(1 << 25)
	beq	5b

	.endm

	.macro	resume_mmdc

	/* restore MMDC IO */
	cmp	r5, #0x0
	ldreq	r10, [r0, #PM_INFO_MX6Q_IOMUXC_V_OFFSET]
	ldrne	r10, [r0, #PM_INFO_MX6Q_IOMUXC_P_OFFSET]

	ldr	r6, [r0, #PM_INFO_MMDC_IO_NUM_OFFSET]
	ldr	r7, =PM_INFO_MMDC_IO_VAL_OFFSET
	add	r7, r7, r0
6:
	ldr	r8, [r7], #0x4
	ldr	r9, [r7], #0x4
	str	r9, [r10, r8]
	subs	r6, r6, #0x1
	bne	6b

	cmp	r5, #0x0
	ldreq	r10, [r0, #PM_INFO_MX6Q_MMDC_V_OFFSET]
	ldrne	r10, [r0, #PM_INFO_MX6Q_MMDC_P_OFFSET]

	/* reset read FIFO, RST_RD_FIFO */
	ldr	r7, =MX6Q_MMDC_MPDGCTRL0
	ldr	r6, [r10, r7]
	orr	r6, r6, #(1 << 31)
	str	r6, [r10, r7]
7:
	ldr	r6, [r10, r7]
	ands	r6, r6, #(1 << 31)
	bne	7b

	/* reset FIFO a second time */
	ldr	r6, [r10, r7]
	orr	r6, r6, #(1 << 31)
	str	r6, [r10, r7]
8:
	ldr	r6, [r10, r7]
	ands	r6, r6, #(1 << 31)
	bne	8b

	/* let DDR out of self-refresh */
	ldr	r7, [r10, #MX6Q_MMDC_MAPSR]
	bic	r7, r7, #(1 << 21)
	str	r7, [r10, #MX6Q_MMDC_MAPSR]
9:
	ldr	r7, [r10, #MX6Q_MMDC_MAPSR]
	ands	r7, r7, #(1 << 25)
	bne	9b

	/* enable power down timer */
	ldr	r7, [r10, #0x4]
	orr	r7, r7, #0x5500
	str	r7, [r10, #0x4]

	/* enable DDR auto power saving */
	ldr	r7, [r10, #MX6Q_MMDC_MAPSR]
	bic	r7, r7, #0x1
	str	r7, [r10, #MX6Q_MMDC_MAPSR]

	.endm

	.macro	tlb_set_to_ocram

	/* save ttbr */
	mrc	p15, 0, r7, c2, c0, 1
	str	r7, [r0, #PM_INFO_PM_INFO_TTBR_OFFSET]

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

	/* Flush the BTAC. */
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

	.endm

	.macro	tlb_back_to_ddr

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

	/* restore ttbr */
	ldr	r7, [r0, #PM_INFO_PM_INFO_TTBR_OFFSET]
	mcr	p15, 0, r7, c2, c0, 1

	.endm

.extern iram_tlb_phys_addr

/* imx6ul_low_power_idle */

	.align 3
ENTRY(imx6ul_low_power_idle)
mx6ul_lpm_wfi_start:
	push	{r4 - r10}

	/* get necessary info from pm_info */
	ldr	r1, [r0, #PM_INFO_PBASE_OFFSET]
	ldr	r2, [r0, #PM_INFO_PM_INFO_SIZE_OFFSET]

	/*
	 * counting the resume address in iram
	 * to set it in SRC register.
	 */
	ldr	r5, =imx6ul_low_power_idle
	ldr     r6, =wakeup
	sub	r6, r6, r5
	add     r8, r1, r2
	add	r3, r8, r6

	/* store physical resume addr and pm_info address. */
	ldr	r10, [r0, #PM_INFO_MX6Q_SRC_V_OFFSET]
	str	r3, [r10, #0x20]
	str	r1, [r10, #0x24]

	/* set ARM power to be gated */
	ldr	r10, [r0, #PM_INFO_MX6Q_GPC_V_OFFSET]
	ldr	r7, =0x1
	str	r7, [r10, #0x2a0]

	disable_l1_dcache

	tlb_set_to_ocram

	/* make sure MMDC in self-refresh */
	ldr	r10, [r0, #PM_INFO_MX6Q_MMDC_V_OFFSET]
	mmdc_enter_dvfs_mode

	/* save DDR IO settings */
	ldr     r10, [r0, #PM_INFO_MX6Q_IOMUXC_V_OFFSET]
	ldr     r6, =0x0
	ldr     r7, [r0, #PM_INFO_MMDC_IO_NUM_OFFSET]
	ldr     r8, =PM_INFO_MMDC_IO_VAL_OFFSET
	add     r8, r8, r0
save_and_set_mmdc_io_lpm:
	ldr	r9, [r8], #0x4
	ldr	r5, [r10, r9]
	str	r6, [r10, r9]
	str	r5, [r8], #0x4
	subs	r7, r7, #0x1
	bne	save_and_set_mmdc_io_lpm

	mov	r5, #0x0
	ccm_enter_idle
	anatop_enter_idle

	/*
	 * mask all GPC interrupts before
	 * enabling the RBC counters to
	 * avoid the counter starting too
	 * early if an interupt is already
	 * pending.
	 */
	ldr     r10, [r0, #PM_INFO_MX6Q_GPC_V_OFFSET]
	ldr	r4, [r10, #MX6Q_GPC_IMR1]
	ldr	r5, [r10, #MX6Q_GPC_IMR2]
	ldr	r6, [r10, #MX6Q_GPC_IMR3]
	ldr	r7, [r10, #MX6Q_GPC_IMR4]

	ldr	r3, =0xffffffff
	str	r3, [r10, #MX6Q_GPC_IMR1]
	str	r3, [r10, #MX6Q_GPC_IMR2]
	str	r3, [r10, #MX6Q_GPC_IMR3]
	str	r3, [r10, #MX6Q_GPC_IMR4]

	/*
	 * enable the RBC bypass counter here
	 * to hold off the interrupts. RBC counter
	 * = 4 (120us). With this setting, the latency
	 * from wakeup interrupt to ARM power up
	 * is ~130uS.
	 */
	ldr     r10, [r0, #PM_INFO_MX6Q_CCM_V_OFFSET]
	ldr	r3, [r10, #MX6Q_CCM_CCR]
	bic	r3, r3, #(0x3f << 21)
	orr	r3, r3, #(0x4 << 21)
	str	r3, [r10, #MX6Q_CCM_CCR]

	/* enable the counter. */
	ldr	r3, [r10, #MX6Q_CCM_CCR]
	orr	r3, r3, #(0x1 << 27)
	str	r3, [r10, #MX6Q_CCM_CCR]

	/* unmask all the GPC interrupts. */
	ldr     r10, [r0, #PM_INFO_MX6Q_GPC_V_OFFSET]
	str	r4, [r10, #MX6Q_GPC_IMR1]
	str	r5, [r10, #MX6Q_GPC_IMR2]
	str	r6, [r10, #MX6Q_GPC_IMR3]
	str	r7, [r10, #MX6Q_GPC_IMR4]

	/*
	 * now delay for a short while (3usec)
	 * ARM is at 24MHz at this point
	 * so a short loop should be enough.
	 * this delay is required to ensure that
	 * the RBC counter can start counting in
	 * case an interrupt is already pending
	 * or in case an interrupt arrives just
	 * as ARM is about to assert DSM_request.
	 */
	ldr	r4, =50
rbc_loop:
	subs	r4, r4, #0x1
	bne	rbc_loop

	wfi

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

	mov	r5, #0x0
	anatop_exit_idle
	ccm_exit_idle

	/* clear ARM power gate setting */
	ldr	r10, [r0, #PM_INFO_MX6Q_GPC_V_OFFSET]
	ldr	r7, =0x0
	str	r7, [r10, #0x2a0]

	resume_mmdc
	/* enable d-cache */
	mrc	p15, 0, r7, c1, c0, 0
	orr	r7, r7, #(1 << 2)
	mcr	p15, 0, r7, c1, c0, 0

	tlb_back_to_ddr

	/* Restore registers */
	pop	{r4 - r10}
	mov	pc, lr

wakeup:
	/* invalidate L1 I-cache first */
	mov	r1, #0x0
	mcr	p15, 0, r1, c7, c5, 0
	mcr	p15, 0, r1, c7, c5, 0
	mcr	p15, 0, r1, c7, c5, 6
	/* enable the Icache and branch prediction */
	mov	r1, #0x1800
	mcr	p15, 0, r1, c1, c0, 0
	isb

	/* get physical resume address from pm_info. */
	ldr	lr, [r0, #PM_INFO_RESUME_ADDR_OFFSET]
	/* clear core0's entry and parameter */
	ldr	r10, [r0, #PM_INFO_MX6Q_SRC_P_OFFSET]
	mov	r7, #0x0
	str	r7, [r10, #MX6Q_SRC_GPR1]
	str	r7, [r10, #MX6Q_SRC_GPR2]

	/* clear ARM power gate setting */
	ldr	r10, [r0, #PM_INFO_MX6Q_GPC_P_OFFSET]
	ldr	r7, =0x0
	str	r7, [r10, #0x2a0]

	mov	r5, #0x1
	anatop_exit_idle
	ccm_exit_idle
	resume_mmdc

	/* Restore registers */
	mov	pc, lr
	.ltorg
mx6ul_lpm_wfi_end:
