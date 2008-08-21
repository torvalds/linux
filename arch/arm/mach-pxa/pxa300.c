/*
 * linux/arch/arm/mach-pxa/pxa300.c
 *
 * Code specific to PXA300/PXA310
 *
 * Copyright (C) 2007 Marvell Internation Ltd.
 *
 * 2007-08-21: eric miao <eric.miao@marvell.com>
 *             initial version
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>

#include <mach/hardware.h>
#include <mach/pxa3xx-regs.h>
#include <mach/mfp-pxa300.h>

#include "generic.h"
#include "devices.h"
#include "clock.h"

static struct pxa3xx_mfp_addr_map pxa300_mfp_addr_map[] __initdata = {

	MFP_ADDR_X(GPIO0,   GPIO2,   0x00b4),
	MFP_ADDR_X(GPIO3,   GPIO26,  0x027c),
	MFP_ADDR_X(GPIO27,  GPIO98,  0x0400),
	MFP_ADDR_X(GPIO99,  GPIO127, 0x0600),
	MFP_ADDR_X(GPIO0_2, GPIO1_2, 0x0674),
	MFP_ADDR_X(GPIO2_2, GPIO6_2, 0x02dc),

	MFP_ADDR(nBE0, 0x0204),
	MFP_ADDR(nBE1, 0x0208),

	MFP_ADDR(nLUA, 0x0244),
	MFP_ADDR(nLLA, 0x0254),

	MFP_ADDR(DF_CLE_nOE, 0x0240),
	MFP_ADDR(DF_nRE_nOE, 0x0200),
	MFP_ADDR(DF_ALE_nWE, 0x020C),
	MFP_ADDR(DF_INT_RnB, 0x00C8),
	MFP_ADDR(DF_nCS0, 0x0248),
	MFP_ADDR(DF_nCS1, 0x0278),
	MFP_ADDR(DF_nWE, 0x00CC),

	MFP_ADDR(DF_ADDR0, 0x0210),
	MFP_ADDR(DF_ADDR1, 0x0214),
	MFP_ADDR(DF_ADDR2, 0x0218),
	MFP_ADDR(DF_ADDR3, 0x021C),

	MFP_ADDR(DF_IO0, 0x0220),
	MFP_ADDR(DF_IO1, 0x0228),
	MFP_ADDR(DF_IO2, 0x0230),
	MFP_ADDR(DF_IO3, 0x0238),
	MFP_ADDR(DF_IO4, 0x0258),
	MFP_ADDR(DF_IO5, 0x0260),
	MFP_ADDR(DF_IO6, 0x0268),
	MFP_ADDR(DF_IO7, 0x0270),
	MFP_ADDR(DF_IO8, 0x0224),
	MFP_ADDR(DF_IO9, 0x022C),
	MFP_ADDR(DF_IO10, 0x0234),
	MFP_ADDR(DF_IO11, 0x023C),
	MFP_ADDR(DF_IO12, 0x025C),
	MFP_ADDR(DF_IO13, 0x0264),
	MFP_ADDR(DF_IO14, 0x026C),
	MFP_ADDR(DF_IO15, 0x0274),

	MFP_ADDR_END,
};

/* override pxa300 MFP register addresses */
static struct pxa3xx_mfp_addr_map pxa310_mfp_addr_map[] __initdata = {
	MFP_ADDR_X(GPIO30,  GPIO98,   0x0418),
	MFP_ADDR_X(GPIO7_2, GPIO12_2, 0x052C),

	MFP_ADDR(ULPI_STP, 0x040C),
	MFP_ADDR(ULPI_NXT, 0x0410),
	MFP_ADDR(ULPI_DIR, 0x0414),

	MFP_ADDR_END,
};

static struct clk common_clks[] = {
	PXA3xx_CKEN("NANDCLK", NAND, 156000000, 0, &pxa3xx_device_nand.dev),
};

static struct clk pxa310_clks[] = {
#ifdef CONFIG_CPU_PXA310
	PXA3xx_CKEN("MMCCLK", MMC3, 19500000, 0, &pxa3xx_device_mci3.dev),
#endif
};

static int __init pxa300_init(void)
{
	if (cpu_is_pxa300() || cpu_is_pxa310()) {
		pxa3xx_init_mfp();
		pxa3xx_mfp_init_addr(pxa300_mfp_addr_map);
		clks_register(ARRAY_AND_SIZE(common_clks));
	}

	if (cpu_is_pxa310()) {
		pxa3xx_mfp_init_addr(pxa310_mfp_addr_map);
		clks_register(ARRAY_AND_SIZE(pxa310_clks));
	}

	return 0;
}

core_initcall(pxa300_init);
