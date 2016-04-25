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
#include "hardware.h"

#define DDRC_MSTR		0x0
#define DDRC_STAT		0x4
#define DDRC_MRCTRL0		0x10
#define DDRC_MRCTRL1		0x14
#define DDRC_MRSTAT		0x18
#define DDRC_PWRCTL		0x30
#define DDRC_RFSHCTL3		0x60
#define DDRC_DBG1		0x304
#define DDRC_SWCTL		0x320
#define DDRC_SWSTAT		0x324
#define DDRC_PSTAT		0x3fc
#define DDRC_PCTRL_0		0x490
#define DDRC_ZQCTL0		0x180
#define DDRC_DFIMISC		0x1b0
#define DDRC_DBGCAM		0x308
#define DDRPHY_LP_CON0		0x18
#define IOMUXC_GPR8		0x20
#define DDRPHY_MDLL_CON0	0xb0
#define DDRPHY_MDLL_CON1	0xb4
#define DDRPHY_OFFSETD_CON0	0x50
#define DDRPHY_OFFSETR_CON0	0x20
#define DDRPHY_OFFSETR_CON1	0x24
#define DDRPHY_OFFSETR_CON2	0x28
#define DDRPHY_OFFSETW_CON0	0x30
#define DDRPHY_OFFSETW_CON1	0x34
#define DDRPHY_OFFSETW_CON2	0x38
#define DDRPHY_CA_WLDSKEW_CON0	0x6c
#define DDRPHY_CA_DSKEW_CON0	0x7c
#define DDRPHY_CA_DSKEW_CON1	0x80
#define DDRPHY_CA_DSKEW_CON2	0x84

#define ANADIG_DIGPROG		0x800

	.align 3

	.macro	switch_to_below_100m

	ldr	r7, =0x2
	str	r7, [r4, #DDRC_DBG1]

	ldr	r6, =0x36000000
1:
	ldr	r7, [r4, #DDRC_DBGCAM]
	and	r7, r7, r6
	cmp	r7, r6
	bne	1b

	ldr	r6, =0x1
2:
	ldr	r7, [r4, #DDRC_MRSTAT]
	and	r7, r7, r6
	cmp	r7, r6
	beq	2b

	ldr	r7, =0x10f0
	str	r7, [r4, #DDRC_MRCTRL0]
	ldr	r7, =0x0
	str	r7, [r4, #DDRC_MRCTRL1]
	ldr	r7, =0x800010f0
	str	r7, [r4, #DDRC_MRCTRL0]

	ldr	r6, =0x1
3:
	ldr	r7, [r4, #DDRC_MRSTAT]
	and	r7, r7, r6
	cmp	r7, r6
	beq	3b

	ldr	r7, =0x20f0
	str	r7, [r4, #DDRC_MRCTRL0]
	ldr	r7, =0x8
	str	r7, [r4, #DDRC_MRCTRL1]
	ldr	r7, =0x800020f0
	str	r7, [r4, #DDRC_MRCTRL0]

	ldr	r6, =0x1
4:
	ldr	r7, [r4, #DDRC_MRSTAT]
	and	r7, r7, r6
	cmp	r7, r6
	beq	4b

	ldr	r7, =0x10f0
	str	r7, [r4, #DDRC_MRCTRL0]
	ldr	r7, =0x1
	str	r7, [r4, #DDRC_MRCTRL1]
	ldr	r7, =0x800010f0
	str	r7, [r4, #DDRC_MRCTRL0]

	ldr	r7, =0x20
	str	r7, [r4, #DDRC_PWRCTL]

	ldr	r6, =0x23
5:
	ldr	r7, [r4, #DDRC_STAT]
	and	r7, r7, r6
	cmp	r7, r6
	bne	5b

	ldr	r7, =0x0
	str	r7, [r4, #DDRC_SWCTL]

	ldr	r7, =0x03048001
	str	r7, [r4, #DDRC_MSTR]

	ldr	r7, =0x1
	str	r7, [r4, #DDRC_SWCTL]

	ldr	r6, =0x1
6:
	ldr	r7, [r4, #DDRC_SWSTAT]
	and	r7, r7, r6
	cmp	r7, r6
	bne	6b

	ldr	r7, =0x10010100
	str	r7, [r5, #0x4]

	ldr	r6, =24000000
	cmp	r0, r6
	bne	7f

	/* dram alt sel set to OSC */
	ldr	r7, =0x10000000
	ldr	r8, =0xa080
	str	r7, [r2, r8]
	/* dram root set to from dram alt, div by 1 */
	ldr	r7, =0x11000000
	ldr	r8, =0x9880
	str	r7, [r2, r8]
	b	8f
7:
	/* dram alt sel set to pfd0_392m */
	ldr	r7, =0x15000000
	ldr	r8, =0xa080
	str	r7, [r2, r8]
	/* dram root set to from dram alt, div by 4 */
	ldr	r7, =0x11000003
	ldr	r8, =0x9880
	str	r7, [r2, r8]
8:
	ldr	r7, =0x202ffd0
	str	r7, [r5, #DDRPHY_MDLL_CON0]

	ldr	r7, =0x1000007f
	str	r7, [r5, #DDRPHY_OFFSETD_CON0]

	ldr	r7, =0x7f7f7f7f
	str	r7, [r5, #DDRPHY_OFFSETR_CON0]
	str	r7, [r5, #DDRPHY_OFFSETR_CON1]
	ldr	r7, =0x7f
	str	r7, [r5, #DDRPHY_OFFSETR_CON2]

	ldr	r7, =0x7f7f7f7f
	str	r7, [r5, #DDRPHY_OFFSETW_CON0]
	str	r7, [r5, #DDRPHY_OFFSETW_CON1]
	ldr	r7, =0x7f
	str	r7, [r5, #DDRPHY_OFFSETW_CON2]

	ldr	r7, [r9, #ANADIG_DIGPROG]
	and	r7, r7, #0x11
	cmp	r7, #0x11
	bne	20f

	ldr	r7, =0x0
	str	r7, [r5, #DDRPHY_CA_WLDSKEW_CON0]
	ldr	r7, =0x60606060
	str	r7, [r5, #DDRPHY_CA_DSKEW_CON0]
	str	r7, [r5, #DDRPHY_CA_DSKEW_CON1]
	ldr	r7, =0x00006060
	str	r7, [r5, #DDRPHY_CA_DSKEW_CON2]
	b	21f
20:
	ldr	r7, =0x0
	str	r7, [r5, #DDRPHY_CA_DSKEW_CON0]
	str	r7, [r5, #DDRPHY_CA_DSKEW_CON1]
	str	r7, [r5, #DDRPHY_CA_DSKEW_CON2]
21:
	ldr	r7, =0x1100007f
	str	r7, [r5, #DDRPHY_OFFSETD_CON0]
	ldr	r7, =0x1000007f
	str	r7, [r5, #DDRPHY_OFFSETD_CON0]

	ldr	r7, =0x0
	str	r7, [r4, #DDRC_PWRCTL]

	ldr	r6, =0x1
9:
	ldr	r7, [r4, #DDRC_MRSTAT]
	and	r7, r7, r6
	cmp	r7, r6
	beq	9b

	ldr	r7, =0xf0
	str	r7, [r4, #DDRC_MRCTRL0]
	ldr	r7, =0x820
	str	r7, [r4, #DDRC_MRCTRL1]
	ldr	r7, =0x800000f0
	str	r7, [r4, #DDRC_MRCTRL0]

	ldr	r6, =0x1
10:
	ldr	r7, [r4, #DDRC_MRSTAT]
	and	r7, r7, r6
	cmp	r7, r6
	beq	10b

	ldr	r7, =0x800020
	str	r7, [r4, #DDRC_ZQCTL0]

	ldr	r7, =0x0
	str	r7, [r4, #DDRC_DBG1]

	/* enable auto self-refresh */
	ldr	r7, [r4, #DDRC_PWRCTL]
	orr	r7, r7, #(1 << 0)
	str	r7, [r4, #DDRC_PWRCTL]

	.endm

	.macro	switch_to_533m

	ldr	r7, =0x2
	str	r7, [r4, #DDRC_DBG1]

	ldr	r7, =0x78
	str	r7, [r3, #IOMUXC_GPR8]
	orr	r7, r7, #0x100
	str	r7, [r3, #IOMUXC_GPR8]

	ldr	r6, =0x30000000
11:
	ldr	r7, [r4, #DDRC_DBGCAM]
	and	r7, r7, r6
	cmp	r7, r6
	bne	11b

	ldr	r6, =0x1
12:
	ldr	r7, [r4, #DDRC_MRSTAT]
	and	r7, r7, r6
	cmp	r7, r6
	beq	12b

	ldr	r7, =0x10f0
	str	r7, [r4, #DDRC_MRCTRL0]
	ldr	r7, =0x1
	str	r7, [r4, #DDRC_MRCTRL1]
	ldr	r7, =0x800010f0
	str	r7, [r4, #DDRC_MRCTRL0]

	ldr	r7, =0x20
	str	r7, [r4, #DDRC_PWRCTL]

	ldr	r6, =0x23
13:
	ldr	r7, [r4, #DDRC_STAT]
	and	r7, r7, r6
	cmp	r7, r6
	bne	13b

	ldr	r7, =0x03040001
	str	r7, [r4, #DDRC_MSTR]

	ldr	r7, =0x40800020
	str	r7, [r4, #DDRC_ZQCTL0]


	ldr	r7, =0x10210100
	str	r7, [r5, #0x4]

	/* dram root set to from dram main, div by 2 */
	ldr	r7, =0x10000001
	ldr	r8, =0x9880
	str	r7, [r2, r8]

	ldr	r7, =0x02020070
	str	r7, [r5, #DDRPHY_MDLL_CON0]

	ldr	r7, =0x10000008
	str	r7, [r5, #DDRPHY_OFFSETD_CON0]

	ldr	r7, =0x08080808
	str	r7, [r5, #DDRPHY_OFFSETR_CON0]
	str	r7, [r5, #DDRPHY_OFFSETR_CON1]
	ldr	r7, =0x8
	str	r7, [r5, #DDRPHY_OFFSETR_CON2]

	ldr	r7, =0x08080808
	str	r7, [r5, #DDRPHY_OFFSETW_CON0]
	str	r7, [r5, #DDRPHY_OFFSETW_CON1]
	ldr	r7, =0x8
	str	r7, [r5, #DDRPHY_OFFSETW_CON2]

	ldr	r7, [r9, #ANADIG_DIGPROG]
	and	r7, r7, #0x11
	cmp	r7, #0x11
	bne	22f

	ldr	r7, =0x40404040
	str	r7, [r5, #DDRPHY_CA_WLDSKEW_CON0]
	ldr	r7, =0x18181818
	str	r7, [r5, #DDRPHY_CA_DSKEW_CON0]
	str	r7, [r5, #DDRPHY_CA_DSKEW_CON1]
	ldr	r7, =0x40401818
	str	r7, [r5, #DDRPHY_CA_DSKEW_CON2]
	b	23f
22:
	ldr	r7, =0x0
	str	r7, [r5, #DDRPHY_CA_DSKEW_CON0]
	str	r7, [r5, #DDRPHY_CA_DSKEW_CON1]
	str	r7, [r5, #DDRPHY_CA_DSKEW_CON2]
23:
	ldr	r7, =0x11000008
	str	r7, [r5, #DDRPHY_OFFSETD_CON0]
	ldr	r7, =0x10000008
	str	r7, [r5, #DDRPHY_OFFSETD_CON0]

	ldr	r6, =0x4
14:
	ldr	r7, [r5, #DDRPHY_MDLL_CON1]
	and	r7, r7, r6
	cmp	r7, r6
	bne	14b

	ldr	r7, =0x1
	str	r7, [r4, #DDRC_RFSHCTL3]
	ldr	r7, =0x3
	str	r7, [r4, #DDRC_RFSHCTL3]

	ldr	r7, =0x0
	str	r7, [r4, #DDRC_PWRCTL]

	ldr	r6, =0x1
15:
	ldr	r7, [r4, #DDRC_MRSTAT]
	and	r7, r7, r6
	cmp	r7, r6
	beq	15b

	ldr	r7, =0x10f0
	str	r7, [r4, #DDRC_MRCTRL0]
	ldr	r7, =0x0
	str	r7, [r4, #DDRC_MRCTRL1]
	ldr	r7, =0x800010f0
	str	r7, [r4, #DDRC_MRCTRL0]

	ldr	r6, =0x1
16:
	ldr	r7, [r4, #DDRC_MRSTAT]
	and	r7, r7, r6
	cmp	r7, r6
	beq	16b

	ldr	r7, =0xf0
	str	r7, [r4, #DDRC_MRCTRL0]
	ldr	r7, =0x930
	str	r7, [r4, #DDRC_MRCTRL1]
	ldr	r7, =0x800000f0
	str	r7, [r4, #DDRC_MRCTRL0]

	ldr	r7, =0x0
	str	r7, [r4, #DDRC_RFSHCTL3]
	ldr	r7, =0x2
	str	r7, [r4, #DDRC_RFSHCTL3]

	ldr	r6, =0x1
17:
	ldr	r7, [r4, #DDRC_MRSTAT]
	and	r7, r7, r6
	cmp	r7, r6
	beq	17b

	ldr	r7, =0xf0
	str	r7, [r4, #DDRC_MRCTRL0]
	ldr	r7, =0x930
	str	r7, [r4, #DDRC_MRCTRL1]
	ldr	r7, =0x800000f0
	str	r7, [r4, #DDRC_MRCTRL0]

	ldr	r6, =0x1
18:
	ldr	r7, [r4, #DDRC_MRSTAT]
	and	r7, r7, r6
	cmp	r7, r6
	beq	18b

	ldr	r7, =0x20f0
	str	r7, [r4, #DDRC_MRCTRL0]
	ldr	r7, =0x408
	str	r7, [r4, #DDRC_MRCTRL1]
	ldr	r7, =0x800020f0
	str	r7, [r4, #DDRC_MRCTRL0]

	ldr	r6, =0x1
19:
	ldr	r7, [r4, #DDRC_MRSTAT]
	and	r7, r7, r6
	cmp	r7, r6
	beq	19b

	ldr	r7, =0x10f0
	str	r7, [r4, #DDRC_MRCTRL0]
	ldr	r7, =0x4
	str	r7, [r4, #DDRC_MRCTRL1]
	ldr	r7, =0x800010f0
	str	r7, [r4, #DDRC_MRCTRL0]

	ldr	r7, =0x0
	str	r7, [r4, #DDRC_DBG1]

	/* enable auto self-refresh */
	ldr	r7, [r4, #DDRC_PWRCTL]
	orr	r7, r7, #(1 << 0)
	str	r7, [r4, #DDRC_PWRCTL]

	.endm

ENTRY(imx7d_ddr3_freq_change)
	push	{r2 - r9}

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
	ldr	r6, =0x0
	mcr	p15, 0, r6, c8, c3, 0

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

	ldr	r2, =IMX_IO_P2V(MX7D_CCM_BASE_ADDR)
	ldr	r3, =IMX_IO_P2V(MX7D_IOMUXC_GPR_BASE_ADDR)
	ldr	r4, =IMX_IO_P2V(MX7D_DDRC_BASE_ADDR)
	ldr	r5, =IMX_IO_P2V(MX7D_DDRC_PHY_BASE_ADDR)
	ldr	r9, =IMX_IO_P2V(MX7D_ANATOP_BASE_ADDR)

	ldr	r6, =100000000
	cmp	r0, r6
	bgt	set_to_533m

set_to_below_100m:
	switch_to_below_100m
	b	done

set_to_533m:
	switch_to_533m
	b	done

done:
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

	dsb
	isb

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
	pop	{r2 - r9}
	mov	pc, lr
	.ltorg
ENDPROC(imx7d_ddr3_freq_change)
