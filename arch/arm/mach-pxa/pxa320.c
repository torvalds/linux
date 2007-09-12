/*
 * linux/arch/arm/mach-pxa/pxa320.c
 *
 * Code specific to PXA320
 *
 * Copyright (C) 2007 Marvell Internation Ltd.
 *
 * 2007-08-21: eric miao <eric.y.miao@gmail.com>
 *             initial version
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/kernel.h>

#include <asm/hardware.h>
#include <asm/arch/mfp.h>
#include <asm/arch/mfp-pxa320.h>

static struct pxa3xx_mfp_addr_map pxa320_mfp_addr_map[] __initdata = {

	MFP_ADDR_X(GPIO0,  GPIO4,   0x0124),
	MFP_ADDR_X(GPIO5,  GPIO26,  0x028C),
	MFP_ADDR_X(GPIO27, GPIO62,  0x0400),
	MFP_ADDR_X(GPIO63, GPIO73,  0x04B4),
	MFP_ADDR_X(GPIO74, GPIO98,  0x04F0),
	MFP_ADDR_X(GPIO99, GPIO127, 0x0600),
	MFP_ADDR_X(GPIO0_2,  GPIO5_2,   0x0674),
	MFP_ADDR_X(GPIO6_2,  GPIO13_2,  0x0494),
	MFP_ADDR_X(GPIO14_2, GPIO17_2, 0x04E0),

	MFP_ADDR(nXCVREN, 0x0138),
	MFP_ADDR(DF_CLE_nOE, 0x0204),
	MFP_ADDR(DF_nADV1_ALE, 0x0208),
	MFP_ADDR(DF_SCLK_S, 0x020C),
	MFP_ADDR(DF_SCLK_E, 0x0210),
	MFP_ADDR(nBE0, 0x0214),
	MFP_ADDR(nBE1, 0x0218),
	MFP_ADDR(DF_nADV2_ALE, 0x021C),
	MFP_ADDR(DF_INT_RnB, 0x0220),
	MFP_ADDR(DF_nCS0, 0x0224),
	MFP_ADDR(DF_nCS1, 0x0228),
	MFP_ADDR(DF_nWE, 0x022C),
	MFP_ADDR(DF_nRE_nOE, 0x0230),
	MFP_ADDR(nLUA, 0x0234),
	MFP_ADDR(nLLA, 0x0238),
	MFP_ADDR(DF_ADDR0, 0x023C),
	MFP_ADDR(DF_ADDR1, 0x0240),
	MFP_ADDR(DF_ADDR2, 0x0244),
	MFP_ADDR(DF_ADDR3, 0x0248),
	MFP_ADDR(DF_IO0, 0x024C),
	MFP_ADDR(DF_IO8, 0x0250),
	MFP_ADDR(DF_IO1, 0x0254),
	MFP_ADDR(DF_IO9, 0x0258),
	MFP_ADDR(DF_IO2, 0x025C),
	MFP_ADDR(DF_IO10, 0x0260),
	MFP_ADDR(DF_IO3, 0x0264),
	MFP_ADDR(DF_IO11, 0x0268),
	MFP_ADDR(DF_IO4, 0x026C),
	MFP_ADDR(DF_IO12, 0x0270),
	MFP_ADDR(DF_IO5, 0x0274),
	MFP_ADDR(DF_IO13, 0x0278),
	MFP_ADDR(DF_IO6, 0x027C),
	MFP_ADDR(DF_IO14, 0x0280),
	MFP_ADDR(DF_IO7, 0x0284),
	MFP_ADDR(DF_IO15, 0x0288),

	MFP_ADDR_END,
};

static void __init pxa320_init_mfp(void)
{
	pxa3xx_init_mfp();
	pxa3xx_mfp_init_addr(pxa320_mfp_addr_map);
}

static int __init pxa320_init(void)
{
	if (cpu_is_pxa320())
		pxa320_init_mfp();

	return 0;
}

core_initcall(pxa320_init);
