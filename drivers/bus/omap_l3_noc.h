/*
 * OMAP L3 Interconnect  error handling driver header
 *
 * Copyright (C) 2011-2014 Texas Instruments Incorporated - http://www.ti.com/
 *	Santosh Shilimkar <santosh.shilimkar@ti.com>
 *	sricharan <r.sricharan@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#ifndef __OMAP_L3_NOC_H
#define __OMAP_L3_NOC_H

#define MAX_L3_MODULES			3
#define MAX_CLKDM_TARGETS		31

#define CLEAR_STDERR_LOG		(1 << 31)
#define CUSTOM_ERROR			0x2
#define STANDARD_ERROR			0x0
#define INBAND_ERROR			0x0
#define L3_APPLICATION_ERROR		0x0
#define L3_DEBUG_ERROR			0x1

/* L3 TARG register offsets */
#define L3_TARG_STDERRLOG_MAIN		0x48
#define L3_TARG_STDERRLOG_MSTADDR	0x50
#define L3_TARG_STDERRLOG_SLVOFSLSB	0x5c
#define L3_TARG_STDERRLOG_CINFO_MSTADDR	0x68
#define L3_FLAGMUX_REGERR0		0xc
#define L3_FLAGMUX_MASK0		0x8

#define L3_TARGET_NOT_SUPPORTED		NULL

/**
 * struct l3_masters_data - L3 Master information
 * @id:		ID of the L3 Master
 * @name:	master name
 */
struct l3_masters_data {
	u32 id;
	char *name;
};

/**
 * struct l3_target_data - L3 Target information
 * @offset:	Offset from base for L3 Target
 * @name:	Target name
 *
 * Target information is organized indexed by bit field definitions.
 */
struct l3_target_data {
	u32 offset;
	char *name;
};

/**
 * struct l3_flagmux_data - Flag Mux information
 * @offset:	offset from base for flagmux register
 * @l3_targ:	array indexed by flagmux index (bit offset) pointing to the
 *		target data. unsupported ones are marked with
 *		L3_TARGET_NOT_SUPPORTED
 * @num_targ_data: number of entries in target data
 * @mask_app_bits: ignore these from raw application irq status
 * @mask_dbg_bits: ignore these from raw debug irq status
 */
struct l3_flagmux_data {
	u32 offset;
	struct l3_target_data *l3_targ;
	u8 num_targ_data;
	u32 mask_app_bits;
	u32 mask_dbg_bits;
};


/**
 * struct omap_l3 - Description of data relevant for L3 bus.
 * @dev:	device representing the bus (populated runtime)
 * @l3_base:	base addresses of modules (populated runtime)
 * @l3_flag_mux: array containing flag mux data per module
 *		 offset from corresponding module base indexed per
 *		 module.
 * @num_modules: number of clock domains / modules.
 * @l3_masters:	array pointing to master data containing name and register
 *		offset for the master.
 * @num_master: number of masters
 * @mst_addr_mask: Mask representing MSTADDR information of NTTP packet
 * @debug_irq:	irq number of the debug interrupt (populated runtime)
 * @app_irq:	irq number of the application interrupt (populated runtime)
 */
struct omap_l3 {
	struct device *dev;

	void __iomem *l3_base[MAX_L3_MODULES];
	struct l3_flagmux_data **l3_flagmux;
	int num_modules;

	struct l3_masters_data *l3_masters;
	int num_masters;
	u32 mst_addr_mask;

	int debug_irq;
	int app_irq;
};

static struct l3_target_data omap_l3_target_data_clk1[] = {
	{0x100,	"DMM1",},
	{0x200,	"DMM2",},
	{0x300,	"ABE",},
	{0x400,	"L4CFG",},
	{0x600,	"CLK2PWRDISC",},
	{0x0,	"HOSTCLK1",},
	{0x900,	"L4WAKEUP",},
};

static struct l3_flagmux_data omap_l3_flagmux_clk1 = {
	.offset = 0x500,
	.l3_targ = omap_l3_target_data_clk1,
	.num_targ_data = ARRAY_SIZE(omap_l3_target_data_clk1),
};


static struct l3_target_data omap_l3_target_data_clk2[] = {
	{0x500,	"CORTEXM3",},
	{0x300,	"DSS",},
	{0x100,	"GPMC",},
	{0x400,	"ISS",},
	{0x700,	"IVAHD",},
	{0xD00,	"AES1",},
	{0x900,	"L4PER0",},
	{0x200,	"OCMRAM",},
	{0x100,	"GPMCsERROR",},
	{0x600,	"SGX",},
	{0x800,	"SL2",},
	{0x1600, "C2C",},
	{0x1100, "PWRDISCCLK1",},
	{0xF00,	"SHA1",},
	{0xE00,	"AES2",},
	{0xC00,	"L4PER3",},
	{0xA00,	"L4PER1",},
	{0xB00,	"L4PER2",},
	{0x0,	"HOSTCLK2",},
	{0x1800, "CAL",},
	{0x1700, "LLI",},
};

static struct l3_flagmux_data omap_l3_flagmux_clk2 = {
	.offset = 0x1000,
	.l3_targ = omap_l3_target_data_clk2,
	.num_targ_data = ARRAY_SIZE(omap_l3_target_data_clk2),
};


static struct l3_target_data omap_l3_target_data_clk3[] = {
	{0x0100, "EMUSS",},
	{0x0300, "DEBUG SOURCE",},
	{0x0,	"HOST CLK3",},
};

static struct l3_flagmux_data omap_l3_flagmux_clk3 = {
	.offset = 0x0200,
	.l3_targ = omap_l3_target_data_clk3,
	.num_targ_data = ARRAY_SIZE(omap_l3_target_data_clk3),
};

static struct l3_masters_data omap_l3_masters[] = {
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

static struct l3_flagmux_data *omap_l3_flagmux[] = {
	&omap_l3_flagmux_clk1,
	&omap_l3_flagmux_clk2,
	&omap_l3_flagmux_clk3,
};

static const struct omap_l3 omap_l3_data = {
	.l3_flagmux = omap_l3_flagmux,
	.num_modules = ARRAY_SIZE(omap_l3_flagmux),
	.l3_masters = omap_l3_masters,
	.num_masters = ARRAY_SIZE(omap_l3_masters),
	/* The 6 MSBs of register field used to distinguish initiator */
	.mst_addr_mask = 0xFC,
};

#endif	/* __OMAP_L3_NOC_H */
