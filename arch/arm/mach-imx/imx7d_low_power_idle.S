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

#define PM_INFO_VBASE_OFFSET			0x0
#define PM_INFO_PBASE_OFFSET			0x4
#define PM_INFO_RESUME_ADDR_OFFSET		0x8
#define PM_INFO_PM_INFO_SIZE_OFFSET		0xc
#define PM_INFO_PM_INFO_LAST_CPU_OFFSET		0x10
#define PM_INFO_PM_INFO_TTBR_OFFSET		0x14
#define PM_INFO_PM_INFO_CPU1_IN_WFI_OFFSET	0x18
#define PM_INFO_PM_INFO_LPI_ENTER_OFFSET	0x1c
#define PM_INFO_VAL_OFFSET			0x20
#define PM_INFO_FLAG0_OFFSET			0x24
#define PM_INFO_FLAG1_OFFSET			0x28
#define PM_INFO_MX7D_DDRC_P_OFFSET		0x2c
#define PM_INFO_MX7D_DDRC_V_OFFSET		0x30
#define PM_INFO_MX7D_CCM_P_OFFSET		0x34
#define PM_INFO_MX7D_CCM_V_OFFSET		0x38
#define PM_INFO_MX7D_ANATOP_P_OFFSET		0x3c
#define PM_INFO_MX7D_ANATOP_V_OFFSET		0x40
#define PM_INFO_MX7D_SRC_P_OFFSET		0x44
#define PM_INFO_MX7D_SRC_V_OFFSET		0x48
#define PM_INFO_MX7D_IOMUXC_GPR_P_OFFSET	0x4c
#define PM_INFO_MX7D_IOMUXC_GPR_V_OFFSET	0x50

#define MX7D_SRC_GPR1	0x74
#define MX7D_SRC_GPR2	0x78
#define MX7D_SRC_GPR3	0x7c
#define MX7D_SRC_GPR4	0x80
#define DDRC_STAT	0x4
#define DDRC_PWRCTL	0x30
#define DDRC_PSTAT	0x3fc

/*
 * imx_pen_lock
 *
 * The reference link of Peterson's algorithm:
 * http://en.wikipedia.org/wiki/Peterson's_algorithm
 *
 * val1 = r1 = !turn (inverted from Peterson's algorithm)
 * on cpu 0:
 * r2 = flag[0] (in flag0)
 * r3 = flag[1] (in flag1)
 * on cpu1:
 * r2 = flag[1] (in flag1)
 * r3 = flag[0] (in flag0)
 *
 */
	.macro	imx_pen_lock

	mov	r8, r0
	mrc	p15, 0, r5, c0, c0, 5
	and	r5, r5, #3
	add	r6, r8, #PM_INFO_VAL_OFFSET
	cmp	r5, #0
	addeq	r7, r8, #PM_INFO_FLAG0_OFFSET
	addeq	r8, r8, #PM_INFO_FLAG1_OFFSET
	addne	r7, r8, #PM_INFO_FLAG1_OFFSET
	addne	r8, r8, #PM_INFO_FLAG0_OFFSET

	mov	r9, #1
	str	r9, [r7]
	dsb
	str	r5, [r6]
1:
	dsb
	ldr	r9, [r8]
	cmp	r9, #1
	ldreq	r9, [r6]
	cmpeq	r9, r5
	beq	1b

	.endm

	.macro	imx_pen_unlock

	dsb
	mrc	p15, 0, r6, c0, c0, 5
	and	r6, r6, #3
	cmp	r6, #0
	addeq	r7, r0, #PM_INFO_FLAG0_OFFSET
	addne	r7, r0, #PM_INFO_FLAG1_OFFSET
	mov	r9, #0
	str	r9, [r7]

	.endm

	.macro  disable_l1_dcache

	push	{r0 - r12, lr}
	ldr	r7, =v7_flush_dcache_all
	mov	lr, pc
	mov	pc, r7
	pop	{r0 - r12, lr}

	/* disable d-cache */
	mrc	p15, 0, r7, c1, c0, 0
	bic	r7, r7, #(1 << 2)
	mcr	p15, 0, r7, c1, c0, 0
	dsb
	isb

	push	{r0 - r12, lr}
	ldr	r7, =v7_flush_dcache_all
	mov	lr, pc
	mov	pc, r7
	pop	{r0 - r12, lr}

#ifdef CONFIG_SMP
	clrex

	/* Turn off SMP bit. */
	mrc	p15, 0, r8, c1, c0, 1
	bic	r8, r8, #0x40
	mcr	p15, 0, r8, c1, c0, 1

	isb
	dsb
#endif

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

	/* Disable Branch Prediction, Z bit in SCTLR. */
	mrc	p15, 0, r6, c1, c0, 0
	bic	r6, r6, #0x800
	mcr	p15, 0, r6, c1, c0, 0

	/* Flush the BTAC. */
	ldr	r6, =0x0
	mcr	p15, 0, r6, c7, c1, 6

	ldr	r6, =iram_tlb_phys_addr
	ldr	r7, [r6]

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

	/* Read TTBCR and set PD0=0, N = 0 */
	mrc	p15, 0, r6, c2, c0, 2
	bic	r6, r6, #0x11
	mcr	p15, 0, r6, c2, c0, 2

	dsb
	isb

	/* flush the TLB */
	ldr	r6, =0x0
	mcr	p15, 0, r6, c8, c3, 0

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

	/* r10 must be DDRC base address */
	.macro ddrc_enter_self_refresh

	ldr	r10, [r0, #PM_INFO_MX7D_DDRC_V_OFFSET]

	/* let DDR out of self-refresh */
	ldr	r7, =0x0
	str	r7, [r10, #DDRC_PWRCTL]

	/* wait rw port_busy clear */
	ldr	r6, =(0x1 << 16)
	orr	r6, r6, #0x1
2:
	ldr	r7, [r10, #DDRC_PSTAT]
	ands	r7, r7, r6
	bne	2b

	/* enter self-refresh bit 5 */
	ldr	r7, =(0x1 << 5)
	str	r7, [r10, #DDRC_PWRCTL]

	/* wait until self-refresh mode entered */
3:
	ldr	r7, [r10, #DDRC_STAT]
	and	r7, r7, #0x3
	cmp	r7, #0x3
	bne	3b
4:
	ldr	r7, [r10, #DDRC_STAT]
	ands	r7, r7, #0x20
	beq	4b

	/* disable dram clk */
	ldr	r7, [r10, #DDRC_PWRCTL]
	orr	r7, r7, #(1 << 3)
	str	r7, [r10, #DDRC_PWRCTL]

	ldr	r10, [r0, #PM_INFO_MX7D_IOMUXC_GPR_V_OFFSET]
	ldr	r7, =0xf0000
	str	r7, [r10]

	ldr	r7, =0x10000
11:
	subs	r7, r7, #0x1
	bne	11b

	.endm

	/* r10 must be DDRC base address */
	.macro ddrc_exit_self_refresh

	cmp	r5, #0x1
	ldreq	r10, [r0, #PM_INFO_MX7D_IOMUXC_GPR_P_OFFSET]
	ldrne	r10, [r0, #PM_INFO_MX7D_IOMUXC_GPR_V_OFFSET]
	ldr	r7, =0x0
	str	r7, [r10]

	ldr	r7, =0x10000
12:
	subs	r7, r7, #0x1
	bne	12b

	cmp	r5, #0x1
	ldreq	r10, [r0, #PM_INFO_MX7D_DDRC_P_OFFSET]
	ldrne	r10, [r0, #PM_INFO_MX7D_DDRC_V_OFFSET]

	/* let DDR out of self-refresh */
	ldr	r7, =0x0
	str	r7, [r10, #DDRC_PWRCTL]

	/* wait until self-refresh mode exited */
5:
	ldr	r7, [r10, #DDRC_STAT]
	and	r7, r7, #0x3
	cmp	r7, #0x3
	beq	5b

	/* enable auto self-refresh */
	ldr	r7, [r10, #DDRC_PWRCTL]
	orr	r7, r7, #(1 << 0)
	str	r7, [r10, #DDRC_PWRCTL]

	.endm

	.macro	pll_do_wait_lock
6:
	ldr	r7, [r10, r8]
	ands	r7, #0x80000000
	beq	6b

	.endm

	.macro ccm_enter_idle

	ldr	r10, [r0, #PM_INFO_MX7D_ANATOP_V_OFFSET]

	/* ungate pfd1 332m for lower axi */
	ldr	r7, =0x8000
	str	r7, [r10, #0xc8]

	ldr	r10, [r0, #PM_INFO_MX7D_CCM_V_OFFSET]

	/* switch ARM CLK to OSC */
	ldr	r8, =0x8000
	ldr	r7, [r10, r8]
	bic	r7, r7, #0x7000000
	str	r7, [r10, r8]

	/* lower AXI clk from 24MHz to 3MHz */
	ldr	r8, =0x8800
	ldr	r7, [r10, r8]
	orr	r7, r7, #0x7
	str	r7, [r10, r8]

	/* lower AHB clk from 24MHz to 3MHz */
	ldr	r8, =0x9000
	ldr	r7, [r10, r8]
	orr	r7, r7, #0x7
	str	r7, [r10, r8]

	/* gate dram clk */
	ldr	r8, =0x9880
	ldr	r7, [r10, r8]
	bic	r7, r7, #0x10000000
	str	r7, [r10, r8]

	ldr	r10, [r0, #PM_INFO_MX7D_ANATOP_V_OFFSET]

	/* gate pfd1 332m */
	ldr	r7, =0x8000
	str	r7, [r10, #0xc4]

	/* gate system pll pfd div 1 */
	ldr	r7, =0x10
	str	r7, [r10, #0xb4]
	/* power down ARM, 480 and DRAM PLL */
	ldr	r7, =0x1000
	str	r7, [r10, #0x64]
	str	r7, [r10, #0xb4]
	ldr	r7, =0x100000
	str	r7, [r10, #0x74]

	.endm

	.macro ccm_exit_idle

	cmp	r5, #0x1
	ldreq	r10, [r0, #PM_INFO_MX7D_ANATOP_P_OFFSET]
	ldrne	r10, [r0, #PM_INFO_MX7D_ANATOP_V_OFFSET]

	/* power up ARM, 480 and DRAM PLL */
	ldr	r7, =0x1000
	str	r7, [r10, #0x68]
	ldr	r8, =0x60
	pll_do_wait_lock

	ldr	r7, =0x1000
	str	r7, [r10, #0xb8]
	ldr	r8, =0xb0
	pll_do_wait_lock

	ldr	r7, =0x100000
	str	r7, [r10, #0x78]
	ldr	r8, =0x70
	pll_do_wait_lock

	/* ungate pfd1 332m for lower axi */
	ldr	r7, =0x8000
	str	r7, [r10, #0xc8]

	/* ungate system pll pfd div 1 */
	ldr	r7, =0x10
	str	r7, [r10, #0xb8]

	cmp	r5, #0x1
	ldreq	r10, [r0, #PM_INFO_MX7D_CCM_P_OFFSET]
	ldrne	r10, [r0, #PM_INFO_MX7D_CCM_V_OFFSET]

	/* switch ARM CLK to PLL */
	ldr	r8, =0x8000
	ldr	r7, [r10, r8]
	orr	r7, r7, #0x1000000
	str	r7, [r10, r8]

	/* restore AXI clk from 3MHz to 24MHz */
	ldr	r8, =0x8800
	ldr	r7, [r10, r8]
	bic	r7, r7, #0x7
	str	r7, [r10, r8]

	/* restore AHB clk from 3MHz to 24MHz */
	ldr	r8, =0x9000
	ldr	r7, [r10, r8]
	bic	r7, r7, #0x7
	str	r7, [r10, r8]

	/* ungate dram clk */
	ldr	r8, =0x9880
	ldr	r7, [r10, r8]
	orr	r7, r7, #0x10000000
	str	r7, [r10, r8]

	cmp	r5, #0x1
	ldreq	r10, [r0, #PM_INFO_MX7D_ANATOP_P_OFFSET]
	ldrne	r10, [r0, #PM_INFO_MX7D_ANATOP_V_OFFSET]

	/* gate pfd1 332m for lower axi */
	ldr	r7, =0x8000
	str	r7, [r10, #0xc4]

	.endm

	.macro anatop_enter_idle

	ldr	r10, [r0, #PM_INFO_MX7D_ANATOP_V_OFFSET]

	/* XTAL to RC-OSC switch */
	ldr	r7, [r10]
	orr	r7, r7, #0x1000
	str	r7, [r10]
	/* power down XTAL */
	ldr	r7, [r10]
	orr	r7, r7, #0x1
	str	r7, [r10]

	/* enable weak 1P0A */
	ldr	r7, [r10, #0x200]
	orr	r7, r7, #0x40000
	str	r7, [r10, #0x200]

	/* disable LDO 1P0A */
	ldr	r7, [r10, #0x200]
	bic	r7, r7, #0x1
	str	r7, [r10, #0x200]

	/* disable LDO 1P0D */
	ldr	r7, [r10, #0x210]
	bic	r7, r7, #0x1
	str	r7, [r10, #0x210]

	/* disable LDO 1P2 */
	ldr	r7, [r10, #0x220]
	bic	r7, r7, #0x1
	str	r7, [r10, #0x220]

	/* switch to low power bandgap */
	ldr	r7, [r10, #0x270]
	orr	r7, r7, #0x400
	str	r7, [r10, #0x270]
	/* power down normal bandgap */
	orr	r7, r7, #0x1
	str	r7, [r10, #0x270]

	.endm

	.macro anatop_exit_idle

	cmp	r5, #0x1
	ldreq	r10, [r0, #PM_INFO_MX7D_ANATOP_P_OFFSET]
	ldrne	r10, [r0, #PM_INFO_MX7D_ANATOP_V_OFFSET]

	/* power on normal bandgap */
	ldr	r7, [r10, #0x270]
	bic	r7, r7, #0x1
	str	r7, [r10, #0x270]
	/* switch to normal bandgap */
	bic	r7, r7, #0x400
	str	r7, [r10, #0x270]

	/* enable LDO 1P2 */
	ldr	r7, [r10, #0x220]
	orr	r7, r7, #0x1
	str	r7, [r10, #0x220]
7:
	ldr	r7, [r10, #0x220]
	ands	r7, #0x20000
	beq	7b

	/* enable LDO 1P0D */
	ldr	r7, [r10, #0x210]
	orr	r7, r7, #0x1
	str	r7, [r10, #0x210]
8:
	ldr	r7, [r10, #0x210]
	ands	r7, #0x20000
	beq	8b

	/* enable LDO 1P0A */
	ldr	r7, [r10, #0x200]
	orr	r7, r7, #0x1
	str	r7, [r10, #0x200]
9:
	ldr	r7, [r10, #0x200]
	ands	r7, #0x20000
	beq	9b
	/* disable weak 1P0A */
	ldr	r7, [r10, #0x200]
	bic	r7, r7, #0x40000
	str	r7, [r10, #0x200]

	/* power up XTAL and wait */
	ldr	r7, [r10]
	bic	r7, r7, #0x1
	str	r7, [r10]
10:
	ldr	r7, [r10]
	ands	r7, r7, #0x4
	beq	10b
	/* RC-OSC to XTAL switch */
	ldr	r7, [r10]
	bic	r7, r7, #0x1000
	str	r7, [r10]

	.endm

.extern iram_tlb_phys_addr

	.align 3
ENTRY(imx7d_low_power_idle)
	push	{r0 - r12}

	/* get necessary info from pm_info */
	ldr	r1, [r0, #PM_INFO_PBASE_OFFSET]
	ldr	r2, [r0, #PM_INFO_PM_INFO_SIZE_OFFSET]

	/*
	 * counting the resume address in iram
	 * to set it in SRC register.
	 */
	ldr	r5, =imx7d_low_power_idle
	ldr     r6, =wakeup
	sub	r6, r6, r5
	add     r8, r1, r2
	add	r3, r8, r6

	/* r11 is cpu id */
	mrc	p15, 0, r11, c0, c0, 5
	and	r11, r11, #3
	cmp	r11, #0x0
	ldreq	r6, =MX7D_SRC_GPR1
	ldreq	r7, =MX7D_SRC_GPR2
	ldrne	r6, =MX7D_SRC_GPR3
	ldrne	r7, =MX7D_SRC_GPR4
	/* store physical resume addr and pm_info address. */
	ldr	r10, [r0, #PM_INFO_MX7D_SRC_V_OFFSET]
	str	r3, [r10, r6]
	str	r1, [r10, r7]

	disable_l1_dcache

	tlb_set_to_ocram

	imx_pen_lock

	ldr	r6, [r0, #PM_INFO_PM_INFO_LAST_CPU_OFFSET]
	cmp	r11, r6
	bne	first_cpu
last_cpu:
	ldr	r7, [r0, #PM_INFO_PM_INFO_CPU1_IN_WFI_OFFSET]
	cmp	r7, #0x0
	beq	lpi_enter_done

	ddrc_enter_self_refresh
	ccm_enter_idle
	anatop_enter_idle
	/* set low power idle enter flag */
	ldr	r7, =0x1
	str	r7, [r0, #PM_INFO_PM_INFO_LPI_ENTER_OFFSET]
	b	lpi_enter_done
first_cpu:
	/* set first cpu wfi flag */
	ldr	r7, =0x1
	str	r7, [r0, #PM_INFO_PM_INFO_CPU1_IN_WFI_OFFSET]
lpi_enter_done:

	imx_pen_unlock

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

	imx_pen_lock

	ldr	r6, [r0, #PM_INFO_PM_INFO_LAST_CPU_OFFSET]
	cmp	r11, r6
	beq	do_exit_wfi

	/* clear first cpu wfi flag */
	ldr	r7, =0x0
	str	r7, [r0, #PM_INFO_PM_INFO_CPU1_IN_WFI_OFFSET]
do_exit_wfi:
	ldr	r7, [r0, #PM_INFO_PM_INFO_LPI_ENTER_OFFSET]
	cmp	r7, #0
	beq	skip_lpi_flow

	ldr	r5, =0x0
	anatop_exit_idle
	ccm_exit_idle
	ddrc_exit_self_refresh
	/* clear lpi enter flag */
	ldr	r7, =0x0
	str	r7, [r0, #PM_INFO_PM_INFO_LPI_ENTER_OFFSET]
skip_lpi_flow:

	imx_pen_unlock

	tlb_back_to_ddr

#ifdef CONFIG_SMP
	/* Turn on SMP bit. */
	mrc	p15, 0, r7, c1, c0, 1
	orr	r7, r7, #0x40
	mcr	p15, 0, r7, c1, c0, 1

	isb
#endif

	/* enable d-cache */
	mrc	p15, 0, r7, c1, c0, 0
	orr	r7, r7, #(1 << 2)
	mcr	p15, 0, r7, c1, c0, 0
	dsb
	isb

	/* Restore registers */
	pop	{r0 - r12}
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

	/* r11 is cpu id */
	mrc	p15, 0, r11, c0, c0, 5
	and	r11, r11, #3
	cmp	r11, #0x0

	imx_pen_lock

	ldr	r6, [r0, #PM_INFO_PM_INFO_LAST_CPU_OFFSET]
	cmp	r11, r6
	beq	do_exit_lp_idle

	/* clear first wfi flag */
	ldr	r7, =0x0
	str	r7, [r0, #PM_INFO_PM_INFO_CPU1_IN_WFI_OFFSET]
do_exit_lp_idle:
	ldr	r7, [r0, #PM_INFO_PM_INFO_LPI_ENTER_OFFSET]
	cmp	r7, #0
	beq	wakeup_skip_lpi_flow

	ldr	r5, =0x1
	anatop_exit_idle
	ccm_exit_idle
	ddrc_exit_self_refresh
	/* clear lpi enter flag */
	ldr	r7, =0x0
	str	r7, [r0, #PM_INFO_PM_INFO_LPI_ENTER_OFFSET]
wakeup_skip_lpi_flow:

	imx_pen_unlock

	/* get physical resume address from pm_info. */
	ldr	lr, [r0, #PM_INFO_RESUME_ADDR_OFFSET]

	/* Restore registers */
	mov	pc, lr
	.ltorg
ENDPROC(imx7d_low_power_idle)
