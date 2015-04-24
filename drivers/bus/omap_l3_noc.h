/*
 * OMAP L3 Interconnect  error handling driver header
 *
 * Copyright (C) 2011-2015 Texas Instruments Incorporated - http://www.ti.com/
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
#define L3_TARG_STDERRLOG_HDR		0x4c
#define L3_TARG_STDERRLOG_MSTADDR	0x50
#define L3_TARG_STDERRLOG_INFO		0x58
#define L3_TARG_STDERRLOG_SLVOFSLSB	0x5c
#define L3_TARG_STDERRLOG_CINFO_INFO	0x64
#define L3_TARG_STDERRLOG_CINFO_MSTADDR	0x68
#define L3_TARG_STDERRLOG_CINFO_OPCODE	0x6c
#define L3_FLAGMUX_REGERR0		0xc
#define L3_FLAGMUX_MASK0		0x8

#define L3_TARGET_NOT_SUPPORTED		NULL

#define L3_BASE_IS_SUBMODULE		((void __iomem *)(1 << 0))

static const char * const l3_transaction_type[] = {
	/* 0 0 0 */ "Idle",
	/* 0 0 1 */ "Write",
	/* 0 1 0 */ "Read",
	/* 0 1 1 */ "ReadEx",
	/* 1 0 0 */ "Read Link",
	/* 1 0 1 */ "Write Non-Posted",
	/* 1 1 0 */ "Write Conditional",
	/* 1 1 1 */ "Write Broadcast",
};

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
 * @l3_base:	base addresses of modules (populated runtime if 0)
 *		if set to L3_BASE_IS_SUBMODULE, then uses previous
 *		module index as the base address
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


static struct l3_target_data omap4_l3_target_data_clk3[] = {
	{0x0100, "DEBUGSS",},
};

static struct l3_flagmux_data omap4_l3_flagmux_clk3 = {
	.offset = 0x0200,
	.l3_targ = omap4_l3_target_data_clk3,
	.num_targ_data = ARRAY_SIZE(omap4_l3_target_data_clk3),
};

static struct l3_masters_data omap_l3_masters[] = {
	{ 0x00, "MPU"},
	{ 0x04, "CS_ADP"},
	{ 0x05, "xxx"},
	{ 0x08, "DSP"},
	{ 0x0C, "IVAHD"},
	{ 0x10, "ISS"},
	{ 0x11, "DucatiM3"},
	{ 0x12, "FaceDetect"},
	{ 0x14, "SDMA_Rd"},
	{ 0x15, "SDMA_Wr"},
	{ 0x16, "xxx"},
	{ 0x17, "xxx"},
	{ 0x18, "SGX"},
	{ 0x1C, "DSS"},
	{ 0x20, "C2C"},
	{ 0x22, "xxx"},
	{ 0x23, "xxx"},
	{ 0x24, "HSI"},
	{ 0x28, "MMC1"},
	{ 0x29, "MMC2"},
	{ 0x2A, "MMC6"},
	{ 0x2C, "UNIPRO1"},
	{ 0x30, "USBHOSTHS"},
	{ 0x31, "USBOTGHS"},
	{ 0x32, "USBHOSTFS"}
};

static struct l3_flagmux_data *omap4_l3_flagmux[] = {
	&omap_l3_flagmux_clk1,
	&omap_l3_flagmux_clk2,
	&omap4_l3_flagmux_clk3,
};

static const struct omap_l3 omap4_l3_data = {
	.l3_flagmux = omap4_l3_flagmux,
	.num_modules = ARRAY_SIZE(omap4_l3_flagmux),
	.l3_masters = omap_l3_masters,
	.num_masters = ARRAY_SIZE(omap_l3_masters),
	/* The 6 MSBs of register field used to distinguish initiator */
	.mst_addr_mask = 0xFC,
};

/* OMAP5 data */
static struct l3_target_data omap5_l3_target_data_clk3[] = {
	{0x0100, "L3INSTR",},
	{0x0300, "DEBUGSS",},
	{0x0,	 "HOSTCLK3",},
};

static struct l3_flagmux_data omap5_l3_flagmux_clk3 = {
	.offset = 0x0200,
	.l3_targ = omap5_l3_target_data_clk3,
	.num_targ_data = ARRAY_SIZE(omap5_l3_target_data_clk3),
};

static struct l3_flagmux_data *omap5_l3_flagmux[] = {
	&omap_l3_flagmux_clk1,
	&omap_l3_flagmux_clk2,
	&omap5_l3_flagmux_clk3,
};

static const struct omap_l3 omap5_l3_data = {
	.l3_flagmux = omap5_l3_flagmux,
	.num_modules = ARRAY_SIZE(omap5_l3_flagmux),
	.l3_masters = omap_l3_masters,
	.num_masters = ARRAY_SIZE(omap_l3_masters),
	/* The 6 MSBs of register field used to distinguish initiator */
	.mst_addr_mask = 0x7E0,
};

/* DRA7 data */
static struct l3_target_data dra_l3_target_data_clk1[] = {
	{0x2a00, "AES1",},
	{0x0200, "DMM_P1",},
	{0x0600, "DSP2_SDMA",},
	{0x0b00, "EVE2",},
	{0x1300, "DMM_P2",},
	{0x2c00, "AES2",},
	{0x0300, "DSP1_SDMA",},
	{0x0a00, "EVE1",},
	{0x0c00, "EVE3",},
	{0x0d00, "EVE4",},
	{0x2900, "DSS",},
	{0x0100, "GPMC",},
	{0x3700, "PCIE1",},
	{0x1600, "IVA_CONFIG",},
	{0x1800, "IVA_SL2IF",},
	{0x0500, "L4_CFG",},
	{0x1d00, "L4_WKUP",},
	{0x3800, "PCIE2",},
	{0x3300, "SHA2_1",},
	{0x1200, "GPU",},
	{0x1000, "IPU1",},
	{0x1100, "IPU2",},
	{0x2000, "TPCC_EDMA",},
	{0x2e00, "TPTC1_EDMA",},
	{0x2b00, "TPTC2_EDMA",},
	{0x0700, "VCP1",},
	{0x2500, "L4_PER2_P3",},
	{0x0e00, "L4_PER3_P3",},
	{0x2200, "MMU1",},
	{0x1400, "PRUSS1",},
	{0x1500, "PRUSS2"},
	{0x0800, "VCP1",},
};

static struct l3_flagmux_data dra_l3_flagmux_clk1 = {
	.offset = 0x803500,
	.l3_targ = dra_l3_target_data_clk1,
	.num_targ_data = ARRAY_SIZE(dra_l3_target_data_clk1),
};

static struct l3_target_data dra_l3_target_data_clk2[] = {
	{0x0,	"HOST CLK1",},
	{0x800000, "HOST CLK2",},
	{0xdead, L3_TARGET_NOT_SUPPORTED,},
	{0x3400, "SHA2_2",},
	{0x0900, "BB2D",},
	{0xdead, L3_TARGET_NOT_SUPPORTED,},
	{0x2100, "L4_PER1_P3",},
	{0x1c00, "L4_PER1_P1",},
	{0x1f00, "L4_PER1_P2",},
	{0x2300, "L4_PER2_P1",},
	{0x2400, "L4_PER2_P2",},
	{0x2600, "L4_PER3_P1",},
	{0x2700, "L4_PER3_P2",},
	{0x2f00, "MCASP1",},
	{0x3000, "MCASP2",},
	{0x3100, "MCASP3",},
	{0x2800, "MMU2",},
	{0x0f00, "OCMC_RAM1",},
	{0x1700, "OCMC_RAM2",},
	{0x1900, "OCMC_RAM3",},
	{0x1e00, "OCMC_ROM",},
	{0x3900, "QSPI",},
};

static struct l3_flagmux_data dra_l3_flagmux_clk2 = {
	.offset = 0x803600,
	.l3_targ = dra_l3_target_data_clk2,
	.num_targ_data = ARRAY_SIZE(dra_l3_target_data_clk2),
};

static struct l3_target_data dra_l3_target_data_clk3[] = {
	{0x0100, "L3_INSTR"},
	{0x0300, "DEBUGSS_CT_TBR"},
	{0x0,	 "HOST CLK3"},
};

static struct l3_flagmux_data dra_l3_flagmux_clk3 = {
	.offset = 0x200,
	.l3_targ = dra_l3_target_data_clk3,
	.num_targ_data = ARRAY_SIZE(dra_l3_target_data_clk3),
};

static struct l3_masters_data dra_l3_masters[] = {
	{ 0x0, "MPU" },
	{ 0x4, "CS_DAP" },
	{ 0x5, "IEEE1500_2_OCP" },
	{ 0x8, "DSP1_MDMA" },
	{ 0x9, "DSP1_CFG" },
	{ 0xA, "DSP1_DMA" },
	{ 0xB, "DSP2_MDMA" },
	{ 0xC, "DSP2_CFG" },
	{ 0xD, "DSP2_DMA" },
	{ 0xE, "IVA" },
	{ 0x10, "EVE1_P1" },
	{ 0x11, "EVE2_P1" },
	{ 0x12, "EVE3_P1" },
	{ 0x13, "EVE4_P1" },
	{ 0x14, "PRUSS1 PRU1" },
	{ 0x15, "PRUSS1 PRU2" },
	{ 0x16, "PRUSS2 PRU1" },
	{ 0x17, "PRUSS2 PRU2" },
	{ 0x18, "IPU1" },
	{ 0x19, "IPU2" },
	{ 0x1A, "SDMA" },
	{ 0x1B, "CDMA" },
	{ 0x1C, "TC1_EDMA" },
	{ 0x1D, "TC2_EDMA" },
	{ 0x20, "DSS" },
	{ 0x21, "MMU1" },
	{ 0x22, "PCIE1" },
	{ 0x23, "MMU2" },
	{ 0x24, "VIP1" },
	{ 0x25, "VIP2" },
	{ 0x26, "VIP3" },
	{ 0x27, "VPE" },
	{ 0x28, "GPU_P1" },
	{ 0x29, "BB2D" },
	{ 0x29, "GPU_P2" },
	{ 0x2B, "GMAC_SW" },
	{ 0x2C, "USB3" },
	{ 0x2D, "USB2_SS" },
	{ 0x2E, "USB2_ULPI_SS1" },
	{ 0x2F, "USB2_ULPI_SS2" },
	{ 0x30, "CSI2_1" },
	{ 0x31, "CSI2_2" },
	{ 0x33, "SATA" },
	{ 0x34, "EVE1_P2" },
	{ 0x35, "EVE2_P2" },
	{ 0x36, "EVE3_P2" },
	{ 0x37, "EVE4_P2" }
};

static struct l3_flagmux_data *dra_l3_flagmux[] = {
	&dra_l3_flagmux_clk1,
	&dra_l3_flagmux_clk2,
	&dra_l3_flagmux_clk3,
};

static const struct omap_l3 dra_l3_data = {
	.l3_base = { [1] = L3_BASE_IS_SUBMODULE },
	.l3_flagmux = dra_l3_flagmux,
	.num_modules = ARRAY_SIZE(dra_l3_flagmux),
	.l3_masters = dra_l3_masters,
	.num_masters = ARRAY_SIZE(dra_l3_masters),
	/* The 6 MSBs of register field used to distinguish initiator */
	.mst_addr_mask = 0xFC,
};

/* AM4372 data */
static struct l3_target_data am4372_l3_target_data_200f[] = {
	{0xf00,  "EMIF",},
	{0x1200, "DES",},
	{0x400,  "OCMCRAM",},
	{0x700,  "TPTC0",},
	{0x800,  "TPTC1",},
	{0x900,  "TPTC2"},
	{0xb00,  "TPCC",},
	{0xd00,  "DEBUGSS",},
	{0xdead, L3_TARGET_NOT_SUPPORTED,},
	{0x200,  "SHA",},
	{0xc00,  "SGX530",},
	{0x500,  "AES0",},
	{0xa00,  "L4_FAST",},
	{0x300,  "MPUSS_L2_RAM",},
	{0x100,  "ICSS",},
};

static struct l3_flagmux_data am4372_l3_flagmux_200f = {
	.offset = 0x1000,
	.l3_targ = am4372_l3_target_data_200f,
	.num_targ_data = ARRAY_SIZE(am4372_l3_target_data_200f),
};

static struct l3_target_data am4372_l3_target_data_100s[] = {
	{0x100, "L4_PER_0",},
	{0x200, "L4_PER_1",},
	{0x300, "L4_PER_2",},
	{0x400, "L4_PER_3",},
	{0x800, "McASP0",},
	{0x900, "McASP1",},
	{0xC00, "MMCHS2",},
	{0x700, "GPMC",},
	{0xD00, "L4_FW",},
	{0xdead, L3_TARGET_NOT_SUPPORTED,},
	{0x500, "ADCTSC",},
	{0xE00, "L4_WKUP",},
	{0xA00, "MAG_CARD",},
};

static struct l3_flagmux_data am4372_l3_flagmux_100s = {
	.offset = 0x600,
	.l3_targ = am4372_l3_target_data_100s,
	.num_targ_data = ARRAY_SIZE(am4372_l3_target_data_100s),
};

static struct l3_masters_data am4372_l3_masters[] = {
	{ 0x0, "M1 (128-bit)"},
	{ 0x1, "M2 (64-bit)"},
	{ 0x4, "DAP"},
	{ 0x5, "P1500"},
	{ 0xC, "ICSS0"},
	{ 0xD, "ICSS1"},
	{ 0x14, "Wakeup Processor"},
	{ 0x18, "TPTC0 Read"},
	{ 0x19, "TPTC0 Write"},
	{ 0x1A, "TPTC1 Read"},
	{ 0x1B, "TPTC1 Write"},
	{ 0x1C, "TPTC2 Read"},
	{ 0x1D, "TPTC2 Write"},
	{ 0x20, "SGX530"},
	{ 0x21, "OCP WP Traffic Probe"},
	{ 0x22, "OCP WP DMA Profiling"},
	{ 0x23, "OCP WP Event Trace"},
	{ 0x25, "DSS"},
	{ 0x28, "Crypto DMA RD"},
	{ 0x29, "Crypto DMA WR"},
	{ 0x2C, "VPFE0"},
	{ 0x2D, "VPFE1"},
	{ 0x30, "GEMAC"},
	{ 0x34, "USB0 RD"},
	{ 0x35, "USB0 WR"},
	{ 0x36, "USB1 RD"},
	{ 0x37, "USB1 WR"},
};

static struct l3_flagmux_data *am4372_l3_flagmux[] = {
	&am4372_l3_flagmux_200f,
	&am4372_l3_flagmux_100s,
};

static const struct omap_l3 am4372_l3_data = {
	.l3_flagmux = am4372_l3_flagmux,
	.num_modules = ARRAY_SIZE(am4372_l3_flagmux),
	.l3_masters = am4372_l3_masters,
	.num_masters = ARRAY_SIZE(am4372_l3_masters),
	/* All 6 bits of register field used to distinguish initiator */
	.mst_addr_mask = 0x3F,
};

#endif	/* __OMAP_L3_NOC_H */
