/*
 * OMAP4XXX L3 Interconnect  error handling driver header
 *
 * Copyright (C) 2011 Texas Corporation
 *	Santosh Shilimkar <santosh.shilimkar@ti.com>
 *	sricharan <r.sricharan@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */
#ifndef __ARCH_ARM_MACH_OMAP2_L3_INTERCONNECT_3XXX_H
#define __ARCH_ARM_MACH_OMAP2_L3_INTERCONNECT_3XXX_H

#define L3_MODULES			3
#define CLEAR_STDERR_LOG		(1 << 31)
#define CUSTOM_ERROR			0x2
#define STANDARD_ERROR			0x0
#define INBAND_ERROR			0x0
#define L3_APPLICATION_ERROR		0x0
#define L3_DEBUG_ERROR			0x1

/* L3 TARG register offsets */
#define L3_TARG_STDERRLOG_MAIN		0x48
#define L3_TARG_STDERRLOG_SLVOFSLSB	0x5c
#define L3_TARG_STDERRLOG_MSTADDR	0x68
#define L3_FLAGMUX_REGERR0		0xc

#define NUM_OF_L3_MASTERS	(sizeof(l3_masters)/sizeof(l3_masters[0]))

static u32 l3_flagmux[L3_MODULES] = {
	0x500,
	0x1000,
	0X0200
};

/* L3 Target standard Error register offsets */
static u32 l3_targ_inst_clk1[] = {
	0x100, /* DMM1 */
	0x200, /* DMM2 */
	0x300, /* ABE */
	0x400, /* L4CFG */
	0x600  /* CLK2 PWR DISC */
};

static u32 l3_targ_inst_clk2[] = {
	0x500, /* CORTEX M3 */
	0x300, /* DSS */
	0x100, /* GPMC */
	0x400, /* ISS */
	0x700, /* IVAHD */
	0xD00, /* missing in TRM  corresponds to AES1*/
	0x900, /* L4 PER0*/
	0x200, /* OCMRAM */
	0x100, /* missing in TRM corresponds to GPMC sERROR*/
	0x600, /* SGX */
	0x800, /* SL2 */
	0x1600, /* C2C */
	0x1100,	/* missing in TRM corresponds PWR DISC CLK1*/
	0xF00, /* missing in TRM corrsponds to SHA1*/
	0xE00, /* missing in TRM corresponds to AES2*/
	0xC00, /* L4 PER3 */
	0xA00, /* L4 PER1*/
	0xB00 /* L4 PER2*/
};

static u32 l3_targ_inst_clk3[] = {
	0x0100	/* EMUSS */
};

static struct l3_masters_data {
	u32 id;
	char name[10];
} l3_masters[] = {
	{ 0x0 , "MPU"},
	{ 0x10, "CS_ADP"},
	{ 0x14, "xxx"},
	{ 0x20, "DSP"},
	{ 0x30, "IVAHD"},
	{ 0x40, "ISS"},
	{ 0x44, "DucatiM3"},
	{ 0x48, "FaceDetect"},
	{ 0x50, "SDMA_Rd"},
	{ 0x54, "SDMA_Wr"},
	{ 0x58, "xxx"},
	{ 0x5C, "xxx"},
	{ 0x60, "SGX"},
	{ 0x70, "DSS"},
	{ 0x80, "C2C"},
	{ 0x88, "xxx"},
	{ 0x8C, "xxx"},
	{ 0x90, "HSI"},
	{ 0xA0, "MMC1"},
	{ 0xA4, "MMC2"},
	{ 0xA8, "MMC6"},
	{ 0xB0, "UNIPRO1"},
	{ 0xC0, "USBHOSTHS"},
	{ 0xC4, "USBOTGHS"},
	{ 0xC8, "USBHOSTFS"}
};

static char *l3_targ_inst_name[L3_MODULES][18] = {
	{
		"DMM1",
		"DMM2",
		"ABE",
		"L4CFG",
		"CLK2 PWR DISC",
	},
	{
		"CORTEX M3" ,
		"DSS ",
		"GPMC ",
		"ISS ",
		"IVAHD ",
		"AES1",
		"L4 PER0",
		"OCMRAM ",
		"GPMC sERROR",
		"SGX ",
		"SL2 ",
		"C2C ",
		"PWR DISC CLK1",
		"SHA1",
		"AES2",
		"L4 PER3",
		"L4 PER1",
		"L4 PER2",
	},
	{
		"EMUSS",
	},
};

static u32 *l3_targ[L3_MODULES] = {
	l3_targ_inst_clk1,
	l3_targ_inst_clk2,
	l3_targ_inst_clk3,
};

struct omap4_l3 {
	struct device *dev;
	struct clk *ick;

	/* memory base */
	void __iomem *l3_base[L3_MODULES];

	int debug_irq;
	int app_irq;
};
#endif
