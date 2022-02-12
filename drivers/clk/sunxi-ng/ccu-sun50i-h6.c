// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2017 Icenowy Zheng <icenowy@aosc.io>
 */

#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include "ccu_common.h"
#include "ccu_reset.h"

#include "ccu_div.h"
#include "ccu_gate.h"
#include "ccu_mp.h"
#include "ccu_mult.h"
#include "ccu_nk.h"
#include "ccu_nkm.h"
#include "ccu_nkmp.h"
#include "ccu_nm.h"

#include "ccu-sun50i-h6.h"

/*
 * The CPU PLL is actually NP clock, with P being /1, /2 or /4. However
 * P should only be used for output frequencies lower than 288 MHz.
 *
 * For now we can just model it as a multiplier clock, and force P to /1.
 *
 * The M factor is present in the register's description, but not in the
 * frequency formula, and it's documented as "M is only used for backdoor
 * testing", so it's not modelled and then force to 0.
 */
#define SUN50I_H6_PLL_CPUX_REG		0x000
static struct ccu_mult pll_cpux_clk = {
	.enable		= BIT(31),
	.lock		= BIT(28),
	.mult		= _SUNXI_CCU_MULT_MIN(8, 8, 12),
	.common		= {
		.reg		= 0x000,
		.hw.init	= CLK_HW_INIT("pll-cpux", "osc24M",
					      &ccu_mult_ops,
					      CLK_SET_RATE_UNGATE),
	},
};

/* Some PLLs are input * N / div1 / P. Model them as NKMP with no K */
#define SUN50I_H6_PLL_DDR0_REG		0x010
static struct ccu_nkmp pll_ddr0_clk = {
	.enable		= BIT(31),
	.lock		= BIT(28),
	.n		= _SUNXI_CCU_MULT_MIN(8, 8, 12),
	.m		= _SUNXI_CCU_DIV(1, 1), /* input divider */
	.p		= _SUNXI_CCU_DIV(0, 1), /* output divider */
	.common		= {
		.reg		= 0x010,
		.hw.init	= CLK_HW_INIT("pll-ddr0", "osc24M",
					      &ccu_nkmp_ops,
					      CLK_SET_RATE_UNGATE),
	},
};

#define SUN50I_H6_PLL_PERIPH0_REG	0x020
static struct ccu_nkmp pll_periph0_clk = {
	.enable		= BIT(31),
	.lock		= BIT(28),
	.n		= _SUNXI_CCU_MULT_MIN(8, 8, 12),
	.m		= _SUNXI_CCU_DIV(1, 1), /* input divider */
	.p		= _SUNXI_CCU_DIV(0, 1), /* output divider */
	.fixed_post_div	= 4,
	.common		= {
		.reg		= 0x020,
		.features	= CCU_FEATURE_FIXED_POSTDIV,
		.hw.init	= CLK_HW_INIT("pll-periph0", "osc24M",
					      &ccu_nkmp_ops,
					      CLK_SET_RATE_UNGATE),
	},
};

#define SUN50I_H6_PLL_PERIPH1_REG	0x028
static struct ccu_nkmp pll_periph1_clk = {
	.enable		= BIT(31),
	.lock		= BIT(28),
	.n		= _SUNXI_CCU_MULT_MIN(8, 8, 12),
	.m		= _SUNXI_CCU_DIV(1, 1), /* input divider */
	.p		= _SUNXI_CCU_DIV(0, 1), /* output divider */
	.fixed_post_div	= 4,
	.common		= {
		.reg		= 0x028,
		.features	= CCU_FEATURE_FIXED_POSTDIV,
		.hw.init	= CLK_HW_INIT("pll-periph1", "osc24M",
					      &ccu_nkmp_ops,
					      CLK_SET_RATE_UNGATE),
	},
};

#define SUN50I_H6_PLL_GPU_REG		0x030
static struct ccu_nkmp pll_gpu_clk = {
	.enable		= BIT(31),
	.lock		= BIT(28),
	.n		= _SUNXI_CCU_MULT_MIN(8, 8, 12),
	.m		= _SUNXI_CCU_DIV(1, 1), /* input divider */
	.p		= _SUNXI_CCU_DIV(0, 1), /* output divider */
	.common		= {
		.reg		= 0x030,
		.hw.init	= CLK_HW_INIT("pll-gpu", "osc24M",
					      &ccu_nkmp_ops,
					      CLK_SET_RATE_UNGATE),
	},
};

/*
 * For Video PLLs, the output divider is described as "used for testing"
 * in the user manual. So it's not modelled and forced to 0.
 */
#define SUN50I_H6_PLL_VIDEO0_REG	0x040
static struct ccu_nm pll_video0_clk = {
	.enable		= BIT(31),
	.lock		= BIT(28),
	.n		= _SUNXI_CCU_MULT_MIN(8, 8, 12),
	.m		= _SUNXI_CCU_DIV(1, 1), /* input divider */
	.fixed_post_div	= 4,
	.min_rate	= 288000000,
	.max_rate	= 2400000000UL,
	.common		= {
		.reg		= 0x040,
		.features	= CCU_FEATURE_FIXED_POSTDIV,
		.hw.init	= CLK_HW_INIT("pll-video0", "osc24M",
					      &ccu_nm_ops,
					      CLK_SET_RATE_UNGATE),
	},
};

#define SUN50I_H6_PLL_VIDEO1_REG	0x048
static struct ccu_nm pll_video1_clk = {
	.enable		= BIT(31),
	.lock		= BIT(28),
	.n		= _SUNXI_CCU_MULT_MIN(8, 8, 12),
	.m		= _SUNXI_CCU_DIV(1, 1), /* input divider */
	.fixed_post_div	= 4,
	.min_rate	= 288000000,
	.max_rate	= 2400000000UL,
	.common		= {
		.reg		= 0x048,
		.features	= CCU_FEATURE_FIXED_POSTDIV,
		.hw.init	= CLK_HW_INIT("pll-video1", "osc24M",
					      &ccu_nm_ops,
					      CLK_SET_RATE_UNGATE),
	},
};

#define SUN50I_H6_PLL_VE_REG		0x058
static struct ccu_nkmp pll_ve_clk = {
	.enable		= BIT(31),
	.lock		= BIT(28),
	.n		= _SUNXI_CCU_MULT_MIN(8, 8, 12),
	.m		= _SUNXI_CCU_DIV(1, 1), /* input divider */
	.p		= _SUNXI_CCU_DIV(0, 1), /* output divider */
	.common		= {
		.reg		= 0x058,
		.hw.init	= CLK_HW_INIT("pll-ve", "osc24M",
					      &ccu_nkmp_ops,
					      CLK_SET_RATE_UNGATE),
	},
};

#define SUN50I_H6_PLL_DE_REG		0x060
static struct ccu_nkmp pll_de_clk = {
	.enable		= BIT(31),
	.lock		= BIT(28),
	.n		= _SUNXI_CCU_MULT_MIN(8, 8, 12),
	.m		= _SUNXI_CCU_DIV(1, 1), /* input divider */
	.p		= _SUNXI_CCU_DIV(0, 1), /* output divider */
	.common		= {
		.reg		= 0x060,
		.hw.init	= CLK_HW_INIT("pll-de", "osc24M",
					      &ccu_nkmp_ops,
					      CLK_SET_RATE_UNGATE),
	},
};

#define SUN50I_H6_PLL_HSIC_REG		0x070
static struct ccu_nkmp pll_hsic_clk = {
	.enable		= BIT(31),
	.lock		= BIT(28),
	.n		= _SUNXI_CCU_MULT_MIN(8, 8, 12),
	.m		= _SUNXI_CCU_DIV(1, 1), /* input divider */
	.p		= _SUNXI_CCU_DIV(0, 1), /* output divider */
	.common		= {
		.reg		= 0x070,
		.hw.init	= CLK_HW_INIT("pll-hsic", "osc24M",
					      &ccu_nkmp_ops,
					      CLK_SET_RATE_UNGATE),
	},
};

/*
 * The Audio PLL is supposed to have 3 outputs: 2 fixed factors from
 * the base (2x and 4x), and one variable divider (the one true pll audio).
 *
 * We don't have any need for the variable divider for now, so we just
 * hardcode it to match with the clock names.
 */
#define SUN50I_H6_PLL_AUDIO_REG		0x078

static struct ccu_sdm_setting pll_audio_sdm_table[] = {
	{ .rate = 541900800, .pattern = 0xc001288d, .m = 1, .n = 22 },
	{ .rate = 589824000, .pattern = 0xc00126e9, .m = 1, .n = 24 },
};

static struct ccu_nm pll_audio_base_clk = {
	.enable		= BIT(31),
	.lock		= BIT(28),
	.n		= _SUNXI_CCU_MULT_MIN(8, 8, 12),
	.m		= _SUNXI_CCU_DIV(1, 1), /* input divider */
	.sdm		= _SUNXI_CCU_SDM(pll_audio_sdm_table,
					 BIT(24), 0x178, BIT(31)),
	.common		= {
		.features	= CCU_FEATURE_SIGMA_DELTA_MOD,
		.reg		= 0x078,
		.hw.init	= CLK_HW_INIT("pll-audio-base", "osc24M",
					      &ccu_nm_ops,
					      CLK_SET_RATE_UNGATE),
	},
};

static const char * const cpux_parents[] = { "osc24M", "osc32k",
					     "iosc", "pll-cpux" };
static SUNXI_CCU_MUX(cpux_clk, "cpux", cpux_parents,
		     0x500, 24, 2, CLK_SET_RATE_PARENT | CLK_IS_CRITICAL);
static SUNXI_CCU_M(axi_clk, "axi", "cpux", 0x500, 0, 2, 0);
static SUNXI_CCU_M(cpux_apb_clk, "cpux-apb", "cpux", 0x500, 8, 2, 0);

static const char * const psi_ahb1_ahb2_parents[] = { "osc24M", "osc32k",
						      "iosc", "pll-periph0" };
static SUNXI_CCU_MP_WITH_MUX(psi_ahb1_ahb2_clk, "psi-ahb1-ahb2",
			     psi_ahb1_ahb2_parents,
			     0x510,
			     0, 2,	/* M */
			     8, 2,	/* P */
			     24, 2,	/* mux */
			     0);

static const char * const ahb3_apb1_apb2_parents[] = { "osc24M", "osc32k",
						       "psi-ahb1-ahb2",
						       "pll-periph0" };
static SUNXI_CCU_MP_WITH_MUX(ahb3_clk, "ahb3", ahb3_apb1_apb2_parents, 0x51c,
			     0, 2,	/* M */
			     8, 2,	/* P */
			     24, 2,	/* mux */
			     0);

static SUNXI_CCU_MP_WITH_MUX(apb1_clk, "apb1", ahb3_apb1_apb2_parents, 0x520,
			     0, 2,	/* M */
			     8, 2,	/* P */
			     24, 2,	/* mux */
			     0);

static SUNXI_CCU_MP_WITH_MUX(apb2_clk, "apb2", ahb3_apb1_apb2_parents, 0x524,
			     0, 2,	/* M */
			     8, 2,	/* P */
			     24, 2,	/* mux */
			     0);

static const char * const mbus_parents[] = { "osc24M", "pll-periph0-2x",
					     "pll-ddr0", "pll-periph0-4x" };
static SUNXI_CCU_M_WITH_MUX_GATE(mbus_clk, "mbus", mbus_parents, 0x540,
				       0, 3,	/* M */
				       24, 2,	/* mux */
				       BIT(31),	/* gate */
				       CLK_IS_CRITICAL);

static const char * const de_parents[] = { "pll-de", "pll-periph0-2x" };
static SUNXI_CCU_M_WITH_MUX_GATE(de_clk, "de", de_parents, 0x600,
				       0, 4,	/* M */
				       24, 1,	/* mux */
				       BIT(31),	/* gate */
				       CLK_SET_RATE_PARENT);

static SUNXI_CCU_GATE(bus_de_clk, "bus-de", "psi-ahb1-ahb2",
		      0x60c, BIT(0), 0);

static const char * const deinterlace_parents[] = { "pll-periph0",
						    "pll-periph1" };
static SUNXI_CCU_M_WITH_MUX_GATE(deinterlace_clk, "deinterlace",
				       deinterlace_parents,
				       0x620,
				       0, 4,	/* M */
				       24, 1,	/* mux */
				       BIT(31),	/* gate */
				       0);

static SUNXI_CCU_GATE(bus_deinterlace_clk, "bus-deinterlace", "psi-ahb1-ahb2",
		      0x62c, BIT(0), 0);

static const char * const gpu_parents[] = { "pll-gpu" };
static SUNXI_CCU_M_WITH_MUX_GATE(gpu_clk, "gpu", gpu_parents, 0x670,
				       0, 3,	/* M */
				       24, 1,	/* mux */
				       BIT(31),	/* gate */
				       CLK_SET_RATE_PARENT);

static SUNXI_CCU_GATE(bus_gpu_clk, "bus-gpu", "psi-ahb1-ahb2",
		      0x67c, BIT(0), 0);

/* Also applies to EMCE */
static const char * const ce_parents[] = { "osc24M", "pll-periph0-2x" };
static SUNXI_CCU_MP_WITH_MUX_GATE(ce_clk, "ce", ce_parents, 0x680,
					0, 4,	/* M */
					8, 2,	/* N */
					24, 1,	/* mux */
					BIT(31),/* gate */
					0);

static SUNXI_CCU_GATE(bus_ce_clk, "bus-ce", "psi-ahb1-ahb2",
		      0x68c, BIT(0), 0);

static const char * const ve_parents[] = { "pll-ve" };
static SUNXI_CCU_M_WITH_MUX_GATE(ve_clk, "ve", ve_parents, 0x690,
				       0, 3,	/* M */
				       24, 1,	/* mux */
				       BIT(31),	/* gate */
				       CLK_SET_RATE_PARENT);

static SUNXI_CCU_GATE(bus_ve_clk, "bus-ve", "psi-ahb1-ahb2",
		      0x69c, BIT(0), 0);

static SUNXI_CCU_MP_WITH_MUX_GATE(emce_clk, "emce", ce_parents, 0x6b0,
					0, 4,	/* M */
					8, 2,	/* N */
					24, 1,	/* mux */
					BIT(31),/* gate */
					0);

static SUNXI_CCU_GATE(bus_emce_clk, "bus-emce", "psi-ahb1-ahb2",
		      0x6bc, BIT(0), 0);

static const char * const vp9_parents[] = { "pll-ve", "pll-periph0-2x" };
static SUNXI_CCU_M_WITH_MUX_GATE(vp9_clk, "vp9", vp9_parents, 0x6c0,
				       0, 3,	/* M */
				       24, 1,	/* mux */
				       BIT(31),	/* gate */
				       0);

static SUNXI_CCU_GATE(bus_vp9_clk, "bus-vp9", "psi-ahb1-ahb2",
		      0x6cc, BIT(0), 0);

static SUNXI_CCU_GATE(bus_dma_clk, "bus-dma", "psi-ahb1-ahb2",
		      0x70c, BIT(0), 0);

static SUNXI_CCU_GATE(bus_msgbox_clk, "bus-msgbox", "psi-ahb1-ahb2",
		      0x71c, BIT(0), 0);

static SUNXI_CCU_GATE(bus_spinlock_clk, "bus-spinlock", "psi-ahb1-ahb2",
		      0x72c, BIT(0), 0);

static SUNXI_CCU_GATE(bus_hstimer_clk, "bus-hstimer", "psi-ahb1-ahb2",
		      0x73c, BIT(0), 0);

static SUNXI_CCU_GATE(avs_clk, "avs", "osc24M", 0x740, BIT(31), 0);

static SUNXI_CCU_GATE(bus_dbg_clk, "bus-dbg", "psi-ahb1-ahb2",
		      0x78c, BIT(0), 0);

static SUNXI_CCU_GATE(bus_psi_clk, "bus-psi", "psi-ahb1-ahb2",
		      0x79c, BIT(0), 0);

static SUNXI_CCU_GATE(bus_pwm_clk, "bus-pwm", "apb1", 0x7ac, BIT(0), 0);

static SUNXI_CCU_GATE(bus_iommu_clk, "bus-iommu", "apb1", 0x7bc, BIT(0), 0);

static const char * const dram_parents[] = { "pll-ddr0" };
static struct ccu_div dram_clk = {
	.div		= _SUNXI_CCU_DIV(0, 2),
	.mux		= _SUNXI_CCU_MUX(24, 2),
	.common	= {
		.reg		= 0x800,
		.hw.init	= CLK_HW_INIT_PARENTS("dram",
						      dram_parents,
						      &ccu_div_ops,
						      CLK_IS_CRITICAL),
	},
};

static SUNXI_CCU_GATE(mbus_dma_clk, "mbus-dma", "mbus",
		      0x804, BIT(0), 0);
static SUNXI_CCU_GATE(mbus_ve_clk, "mbus-ve", "mbus",
		      0x804, BIT(1), 0);
static SUNXI_CCU_GATE(mbus_ce_clk, "mbus-ce", "mbus",
		      0x804, BIT(2), 0);
static SUNXI_CCU_GATE(mbus_ts_clk, "mbus-ts", "mbus",
		      0x804, BIT(3), 0);
static SUNXI_CCU_GATE(mbus_nand_clk, "mbus-nand", "mbus",
		      0x804, BIT(5), 0);
static SUNXI_CCU_GATE(mbus_csi_clk, "mbus-csi", "mbus",
		      0x804, BIT(8), 0);
static SUNXI_CCU_GATE(mbus_deinterlace_clk, "mbus-deinterlace", "mbus",
		      0x804, BIT(11), 0);

static SUNXI_CCU_GATE(bus_dram_clk, "bus-dram", "psi-ahb1-ahb2",
		      0x80c, BIT(0), CLK_IS_CRITICAL);

static const char * const nand_spi_parents[] = { "osc24M", "pll-periph0",
					     "pll-periph1", "pll-periph0-2x",
					     "pll-periph1-2x" };
static SUNXI_CCU_MP_WITH_MUX_GATE(nand0_clk, "nand0", nand_spi_parents, 0x810,
					0, 4,	/* M */
					8, 2,	/* N */
					24, 3,	/* mux */
					BIT(31),/* gate */
					0);

static SUNXI_CCU_MP_WITH_MUX_GATE(nand1_clk, "nand1", nand_spi_parents, 0x814,
					0, 4,	/* M */
					8, 2,	/* N */
					24, 3,	/* mux */
					BIT(31),/* gate */
					0);

static SUNXI_CCU_GATE(bus_nand_clk, "bus-nand", "ahb3", 0x82c, BIT(0), 0);

static const char * const mmc_parents[] = { "osc24M", "pll-periph0-2x",
					    "pll-periph1-2x" };
static SUNXI_CCU_MP_WITH_MUX_GATE_POSTDIV(mmc0_clk, "mmc0", mmc_parents, 0x830,
					  0, 4,		/* M */
					  8, 2,		/* N */
					  24, 2,	/* mux */
					  BIT(31),	/* gate */
					  2,		/* post-div */
					  0);

static SUNXI_CCU_MP_WITH_MUX_GATE_POSTDIV(mmc1_clk, "mmc1", mmc_parents, 0x834,
					  0, 4,		/* M */
					  8, 2,		/* N */
					  24, 2,	/* mux */
					  BIT(31),	/* gate */
					  2,		/* post-div */
					  0);

static SUNXI_CCU_MP_WITH_MUX_GATE_POSTDIV(mmc2_clk, "mmc2", mmc_parents, 0x838,
					  0, 4,		/* M */
					  8, 2,		/* N */
					  24, 2,	/* mux */
					  BIT(31),	/* gate */
					  2,		/* post-div */
					  0);

static SUNXI_CCU_GATE(bus_mmc0_clk, "bus-mmc0", "ahb3", 0x84c, BIT(0), 0);
static SUNXI_CCU_GATE(bus_mmc1_clk, "bus-mmc1", "ahb3", 0x84c, BIT(1), 0);
static SUNXI_CCU_GATE(bus_mmc2_clk, "bus-mmc2", "ahb3", 0x84c, BIT(2), 0);

static SUNXI_CCU_GATE(bus_uart0_clk, "bus-uart0", "apb2", 0x90c, BIT(0), 0);
static SUNXI_CCU_GATE(bus_uart1_clk, "bus-uart1", "apb2", 0x90c, BIT(1), 0);
static SUNXI_CCU_GATE(bus_uart2_clk, "bus-uart2", "apb2", 0x90c, BIT(2), 0);
static SUNXI_CCU_GATE(bus_uart3_clk, "bus-uart3", "apb2", 0x90c, BIT(3), 0);

static SUNXI_CCU_GATE(bus_i2c0_clk, "bus-i2c0", "apb2", 0x91c, BIT(0), 0);
static SUNXI_CCU_GATE(bus_i2c1_clk, "bus-i2c1", "apb2", 0x91c, BIT(1), 0);
static SUNXI_CCU_GATE(bus_i2c2_clk, "bus-i2c2", "apb2", 0x91c, BIT(2), 0);
static SUNXI_CCU_GATE(bus_i2c3_clk, "bus-i2c3", "apb2", 0x91c, BIT(3), 0);

static SUNXI_CCU_GATE(bus_scr0_clk, "bus-scr0", "apb2", 0x93c, BIT(0), 0);
static SUNXI_CCU_GATE(bus_scr1_clk, "bus-scr1", "apb2", 0x93c, BIT(1), 0);

static SUNXI_CCU_MP_WITH_MUX_GATE(spi0_clk, "spi0", nand_spi_parents, 0x940,
					0, 4,	/* M */
					8, 2,	/* N */
					24, 3,	/* mux */
					BIT(31),/* gate */
					0);

static SUNXI_CCU_MP_WITH_MUX_GATE(spi1_clk, "spi1", nand_spi_parents, 0x944,
					0, 4,	/* M */
					8, 2,	/* N */
					24, 3,	/* mux */
					BIT(31),/* gate */
					0);

static SUNXI_CCU_GATE(bus_spi0_clk, "bus-spi0", "ahb3", 0x96c, BIT(0), 0);
static SUNXI_CCU_GATE(bus_spi1_clk, "bus-spi1", "ahb3", 0x96c, BIT(1), 0);

static SUNXI_CCU_GATE(bus_emac_clk, "bus-emac", "ahb3", 0x97c, BIT(0), 0);

static const char * const ts_parents[] = { "osc24M", "pll-periph0" };
static SUNXI_CCU_MP_WITH_MUX_GATE(ts_clk, "ts", ts_parents, 0x9b0,
					0, 4,	/* M */
					8, 2,	/* N */
					24, 1,	/* mux */
					BIT(31),/* gate */
					0);

static SUNXI_CCU_GATE(bus_ts_clk, "bus-ts", "ahb3", 0x9bc, BIT(0), 0);

static const char * const ir_tx_parents[] = { "osc32k", "osc24M" };
static SUNXI_CCU_MP_WITH_MUX_GATE(ir_tx_clk, "ir-tx", ir_tx_parents, 0x9c0,
					0, 4,	/* M */
					8, 2,	/* N */
					24, 1,	/* mux */
					BIT(31),/* gate */
					0);

static SUNXI_CCU_GATE(bus_ir_tx_clk, "bus-ir-tx", "apb1", 0x9cc, BIT(0), 0);

static SUNXI_CCU_GATE(bus_ths_clk, "bus-ths", "apb1", 0x9fc, BIT(0), 0);

static const char * const audio_parents[] = { "pll-audio", "pll-audio-2x", "pll-audio-4x" };
static struct ccu_div i2s3_clk = {
	.enable		= BIT(31),
	.div		= _SUNXI_CCU_DIV_FLAGS(8, 2, CLK_DIVIDER_POWER_OF_TWO),
	.mux		= _SUNXI_CCU_MUX(24, 2),
	.common		= {
		.reg		= 0xa0c,
		.hw.init	= CLK_HW_INIT_PARENTS("i2s3",
						      audio_parents,
						      &ccu_div_ops,
						      CLK_SET_RATE_PARENT),
	},
};

static struct ccu_div i2s0_clk = {
	.enable		= BIT(31),
	.div		= _SUNXI_CCU_DIV_FLAGS(8, 2, CLK_DIVIDER_POWER_OF_TWO),
	.mux		= _SUNXI_CCU_MUX(24, 2),
	.common		= {
		.reg		= 0xa10,
		.hw.init	= CLK_HW_INIT_PARENTS("i2s0",
						      audio_parents,
						      &ccu_div_ops,
						      CLK_SET_RATE_PARENT),
	},
};

static struct ccu_div i2s1_clk = {
	.enable		= BIT(31),
	.div		= _SUNXI_CCU_DIV_FLAGS(8, 2, CLK_DIVIDER_POWER_OF_TWO),
	.mux		= _SUNXI_CCU_MUX(24, 2),
	.common		= {
		.reg		= 0xa14,
		.hw.init	= CLK_HW_INIT_PARENTS("i2s1",
						      audio_parents,
						      &ccu_div_ops,
						      CLK_SET_RATE_PARENT),
	},
};

static struct ccu_div i2s2_clk = {
	.enable		= BIT(31),
	.div		= _SUNXI_CCU_DIV_FLAGS(8, 2, CLK_DIVIDER_POWER_OF_TWO),
	.mux		= _SUNXI_CCU_MUX(24, 2),
	.common		= {
		.reg		= 0xa18,
		.hw.init	= CLK_HW_INIT_PARENTS("i2s2",
						      audio_parents,
						      &ccu_div_ops,
						      CLK_SET_RATE_PARENT),
	},
};

static SUNXI_CCU_GATE(bus_i2s0_clk, "bus-i2s0", "apb1", 0xa1c, BIT(0), 0);
static SUNXI_CCU_GATE(bus_i2s1_clk, "bus-i2s1", "apb1", 0xa1c, BIT(1), 0);
static SUNXI_CCU_GATE(bus_i2s2_clk, "bus-i2s2", "apb1", 0xa1c, BIT(2), 0);
static SUNXI_CCU_GATE(bus_i2s3_clk, "bus-i2s3", "apb1", 0xa1c, BIT(3), 0);

static struct ccu_div spdif_clk = {
	.enable		= BIT(31),
	.div		= _SUNXI_CCU_DIV_FLAGS(8, 2, CLK_DIVIDER_POWER_OF_TWO),
	.mux		= _SUNXI_CCU_MUX(24, 2),
	.common		= {
		.reg		= 0xa20,
		.hw.init	= CLK_HW_INIT_PARENTS("spdif",
						      audio_parents,
						      &ccu_div_ops,
						      0),
	},
};

static SUNXI_CCU_GATE(bus_spdif_clk, "bus-spdif", "apb1", 0xa2c, BIT(0), 0);

static struct ccu_div dmic_clk = {
	.enable		= BIT(31),
	.div		= _SUNXI_CCU_DIV_FLAGS(8, 2, CLK_DIVIDER_POWER_OF_TWO),
	.mux		= _SUNXI_CCU_MUX(24, 2),
	.common		= {
		.reg		= 0xa40,
		.hw.init	= CLK_HW_INIT_PARENTS("dmic",
						      audio_parents,
						      &ccu_div_ops,
						      0),
	},
};

static SUNXI_CCU_GATE(bus_dmic_clk, "bus-dmic", "apb1", 0xa4c, BIT(0), 0);

static struct ccu_div audio_hub_clk = {
	.enable		= BIT(31),
	.div		= _SUNXI_CCU_DIV_FLAGS(8, 2, CLK_DIVIDER_POWER_OF_TWO),
	.mux		= _SUNXI_CCU_MUX(24, 2),
	.common		= {
		.reg		= 0xa60,
		.hw.init	= CLK_HW_INIT_PARENTS("audio-hub",
						      audio_parents,
						      &ccu_div_ops,
						      0),
	},
};

static SUNXI_CCU_GATE(bus_audio_hub_clk, "bus-audio-hub", "apb1", 0xa6c, BIT(0), 0);

/*
 * There are OHCI 12M clock source selection bits for 2 USB 2.0 ports.
 * We will force them to 0 (12M divided from 48M).
 */
#define SUN50I_H6_USB0_CLK_REG		0xa70
#define SUN50I_H6_USB3_CLK_REG		0xa7c

static SUNXI_CCU_GATE(usb_ohci0_clk, "usb-ohci0", "osc12M", 0xa70, BIT(31), 0);
static SUNXI_CCU_GATE(usb_phy0_clk, "usb-phy0", "osc24M", 0xa70, BIT(29), 0);

static SUNXI_CCU_GATE(usb_phy1_clk, "usb-phy1", "osc24M", 0xa74, BIT(29), 0);

static SUNXI_CCU_GATE(usb_ohci3_clk, "usb-ohci3", "osc12M", 0xa7c, BIT(31), 0);
static SUNXI_CCU_GATE(usb_phy3_clk, "usb-phy3", "osc12M", 0xa7c, BIT(29), 0);
static SUNXI_CCU_GATE(usb_hsic_12m_clk, "usb-hsic-12M", "osc12M", 0xa7c, BIT(27), 0);
static SUNXI_CCU_GATE(usb_hsic_clk, "usb-hsic", "pll-hsic", 0xa7c, BIT(26), 0);

static SUNXI_CCU_GATE(bus_ohci0_clk, "bus-ohci0", "ahb3", 0xa8c, BIT(0), 0);
static SUNXI_CCU_GATE(bus_ohci3_clk, "bus-ohci3", "ahb3", 0xa8c, BIT(3), 0);
static SUNXI_CCU_GATE(bus_ehci0_clk, "bus-ehci0", "ahb3", 0xa8c, BIT(4), 0);
static SUNXI_CCU_GATE(bus_xhci_clk, "bus-xhci", "ahb3", 0xa8c, BIT(5), 0);
static SUNXI_CCU_GATE(bus_ehci3_clk, "bus-ehci3", "ahb3", 0xa8c, BIT(7), 0);
static SUNXI_CCU_GATE(bus_otg_clk, "bus-otg", "ahb3", 0xa8c, BIT(8), 0);

static struct clk_fixed_factor pll_periph0_4x_clk;
static CLK_FIXED_FACTOR_HW(pcie_ref_100m_clk, "pcie-ref-100M",
			   &pll_periph0_4x_clk.hw, 24, 1, 0);
static SUNXI_CCU_GATE(pcie_ref_clk, "pcie-ref", "pcie-ref-100M",
		      0xab0, BIT(31), 0);
static SUNXI_CCU_GATE(pcie_ref_out_clk, "pcie-ref-out", "pcie-ref",
		      0xab0, BIT(30), 0);

static SUNXI_CCU_M_WITH_GATE(pcie_maxi_clk, "pcie-maxi",
			     "pll-periph0", 0xab4,
			     0, 4,	/* M */
			     BIT(31),	/* gate */
			     0);

static SUNXI_CCU_M_WITH_GATE(pcie_aux_clk, "pcie-aux", "osc24M", 0xab8,
			     0, 5,	/* M */
			     BIT(31),	/* gate */
			     0);

static SUNXI_CCU_GATE(bus_pcie_clk, "bus-pcie", "psi-ahb1-ahb2",
		      0xabc, BIT(0), 0);

static const char * const hdmi_parents[] = { "pll-video0", "pll-video1",
					      "pll-video1-4x" };
static SUNXI_CCU_M_WITH_MUX_GATE(hdmi_clk, "hdmi", hdmi_parents, 0xb00,
				 0, 4,		/* M */
				 24, 2,		/* mux */
				 BIT(31),	/* gate */
				 0);

static SUNXI_CCU_GATE(hdmi_slow_clk, "hdmi-slow", "osc24M", 0xb04, BIT(31), 0);

static const char * const hdmi_cec_parents[] = { "osc32k", "pll-periph0-2x" };
static const struct ccu_mux_fixed_prediv hdmi_cec_predivs[] = {
	{ .index = 1, .div = 36621 },
};

#define SUN50I_H6_HDMI_CEC_CLK_REG		0xb10
static struct ccu_mux hdmi_cec_clk = {
	.enable		= BIT(31),

	.mux		= {
		.shift	= 24,
		.width	= 2,

		.fixed_predivs	= hdmi_cec_predivs,
		.n_predivs	= ARRAY_SIZE(hdmi_cec_predivs),
	},

	.common		= {
		.reg		= 0xb10,
		.features	= CCU_FEATURE_FIXED_PREDIV,
		.hw.init	= CLK_HW_INIT_PARENTS("hdmi-cec",
						      hdmi_cec_parents,
						      &ccu_mux_ops,
						      0),
	},
};

static SUNXI_CCU_GATE(bus_hdmi_clk, "bus-hdmi", "ahb3", 0xb1c, BIT(0), 0);

static SUNXI_CCU_GATE(bus_tcon_top_clk, "bus-tcon-top", "ahb3",
		      0xb5c, BIT(0), 0);

static const char * const tcon_lcd0_parents[] = { "pll-video0",
						  "pll-video0-4x",
						  "pll-video1" };
static SUNXI_CCU_MUX_WITH_GATE(tcon_lcd0_clk, "tcon-lcd0",
			       tcon_lcd0_parents, 0xb60,
			       24, 3,	/* mux */
			       BIT(31),	/* gate */
			       CLK_SET_RATE_PARENT);

static SUNXI_CCU_GATE(bus_tcon_lcd0_clk, "bus-tcon-lcd0", "ahb3",
		      0xb7c, BIT(0), 0);

static const char * const tcon_tv0_parents[] = { "pll-video0",
						 "pll-video0-4x",
						 "pll-video1",
						 "pll-video1-4x" };
static SUNXI_CCU_MP_WITH_MUX_GATE(tcon_tv0_clk, "tcon-tv0",
				  tcon_tv0_parents, 0xb80,
				  0, 4,		/* M */
				  8, 2,		/* P */
				  24, 3,	/* mux */
				  BIT(31),	/* gate */
				  CLK_SET_RATE_PARENT);

static SUNXI_CCU_GATE(bus_tcon_tv0_clk, "bus-tcon-tv0", "ahb3",
		      0xb9c, BIT(0), 0);

static SUNXI_CCU_GATE(csi_cci_clk, "csi-cci", "osc24M", 0xc00, BIT(0), 0);

static const char * const csi_top_parents[] = { "pll-video0", "pll-ve",
					      "pll-periph0" };
static const u8 csi_top_table[] = { 0, 2, 3 };
static SUNXI_CCU_M_WITH_MUX_TABLE_GATE(csi_top_clk, "csi-top",
				       csi_top_parents, csi_top_table, 0xc04,
				       0, 4,	/* M */
				       24, 3,	/* mux */
				       BIT(31),	/* gate */
				       0);

static const char * const csi_mclk_parents[] = { "osc24M", "pll-video0",
					       "pll-periph0", "pll-periph1" };
static SUNXI_CCU_M_WITH_MUX_GATE(csi_mclk_clk, "csi-mclk",
				 csi_mclk_parents, 0xc08,
				 0, 5,		/* M */
				 24, 3,		/* mux */
				 BIT(31),	/* gate */
				 0);

static SUNXI_CCU_GATE(bus_csi_clk, "bus-csi", "ahb3", 0xc2c, BIT(0), 0);

static const char * const hdcp_parents[] = { "pll-periph0", "pll-periph1" };
static SUNXI_CCU_M_WITH_MUX_GATE(hdcp_clk, "hdcp", hdcp_parents, 0xc40,
				 0, 4,		/* M */
				 24, 2,		/* mux */
				 BIT(31),	/* gate */
				 0);

static SUNXI_CCU_GATE(bus_hdcp_clk, "bus-hdcp", "ahb3", 0xc4c, BIT(0), 0);

/* Fixed factor clocks */
static CLK_FIXED_FACTOR_FW_NAME(osc12M_clk, "osc12M", "hosc", 2, 1, 0);

static const struct clk_hw *clk_parent_pll_audio[] = {
	&pll_audio_base_clk.common.hw
};

/*
 * The divider of pll-audio is fixed to 24 for now, so 24576000 and 22579200
 * rates can be set exactly in conjunction with sigma-delta modulation.
 */
static CLK_FIXED_FACTOR_HWS(pll_audio_clk, "pll-audio",
			    clk_parent_pll_audio,
			    24, 1, CLK_SET_RATE_PARENT);
static CLK_FIXED_FACTOR_HWS(pll_audio_2x_clk, "pll-audio-2x",
			    clk_parent_pll_audio,
			    4, 1, CLK_SET_RATE_PARENT);
static CLK_FIXED_FACTOR_HWS(pll_audio_4x_clk, "pll-audio-4x",
			    clk_parent_pll_audio,
			    2, 1, CLK_SET_RATE_PARENT);

static const struct clk_hw *pll_periph0_parents[] = {
	&pll_periph0_clk.common.hw
};
static CLK_FIXED_FACTOR_HWS(pll_periph0_4x_clk, "pll-periph0-4x",
			    pll_periph0_parents,
			    1, 4, 0);
static CLK_FIXED_FACTOR_HWS(pll_periph0_2x_clk, "pll-periph0-2x",
			    pll_periph0_parents,
			    1, 2, 0);

static const struct clk_hw *pll_periph1_parents[] = {
	&pll_periph1_clk.common.hw
};
static CLK_FIXED_FACTOR_HWS(pll_periph1_4x_clk, "pll-periph1-4x",
			    pll_periph1_parents,
			    1, 4, 0);
static CLK_FIXED_FACTOR_HWS(pll_periph1_2x_clk, "pll-periph1-2x",
			    pll_periph1_parents,
			    1, 2, 0);

static CLK_FIXED_FACTOR_HW(pll_video0_4x_clk, "pll-video0-4x",
			   &pll_video0_clk.common.hw,
			   1, 4, CLK_SET_RATE_PARENT);
static CLK_FIXED_FACTOR_HW(pll_video1_4x_clk, "pll-video1-4x",
			   &pll_video1_clk.common.hw,
			   1, 4, CLK_SET_RATE_PARENT);

static struct ccu_common *sun50i_h6_ccu_clks[] = {
	&pll_cpux_clk.common,
	&pll_ddr0_clk.common,
	&pll_periph0_clk.common,
	&pll_periph1_clk.common,
	&pll_gpu_clk.common,
	&pll_video0_clk.common,
	&pll_video1_clk.common,
	&pll_ve_clk.common,
	&pll_de_clk.common,
	&pll_hsic_clk.common,
	&pll_audio_base_clk.common,
	&cpux_clk.common,
	&axi_clk.common,
	&cpux_apb_clk.common,
	&psi_ahb1_ahb2_clk.common,
	&ahb3_clk.common,
	&apb1_clk.common,
	&apb2_clk.common,
	&mbus_clk.common,
	&de_clk.common,
	&bus_de_clk.common,
	&deinterlace_clk.common,
	&bus_deinterlace_clk.common,
	&gpu_clk.common,
	&bus_gpu_clk.common,
	&ce_clk.common,
	&bus_ce_clk.common,
	&ve_clk.common,
	&bus_ve_clk.common,
	&emce_clk.common,
	&bus_emce_clk.common,
	&vp9_clk.common,
	&bus_vp9_clk.common,
	&bus_dma_clk.common,
	&bus_msgbox_clk.common,
	&bus_spinlock_clk.common,
	&bus_hstimer_clk.common,
	&avs_clk.common,
	&bus_dbg_clk.common,
	&bus_psi_clk.common,
	&bus_pwm_clk.common,
	&bus_iommu_clk.common,
	&dram_clk.common,
	&mbus_dma_clk.common,
	&mbus_ve_clk.common,
	&mbus_ce_clk.common,
	&mbus_ts_clk.common,
	&mbus_nand_clk.common,
	&mbus_csi_clk.common,
	&mbus_deinterlace_clk.common,
	&bus_dram_clk.common,
	&nand0_clk.common,
	&nand1_clk.common,
	&bus_nand_clk.common,
	&mmc0_clk.common,
	&mmc1_clk.common,
	&mmc2_clk.common,
	&bus_mmc0_clk.common,
	&bus_mmc1_clk.common,
	&bus_mmc2_clk.common,
	&bus_uart0_clk.common,
	&bus_uart1_clk.common,
	&bus_uart2_clk.common,
	&bus_uart3_clk.common,
	&bus_i2c0_clk.common,
	&bus_i2c1_clk.common,
	&bus_i2c2_clk.common,
	&bus_i2c3_clk.common,
	&bus_scr0_clk.common,
	&bus_scr1_clk.common,
	&spi0_clk.common,
	&spi1_clk.common,
	&bus_spi0_clk.common,
	&bus_spi1_clk.common,
	&bus_emac_clk.common,
	&ts_clk.common,
	&bus_ts_clk.common,
	&ir_tx_clk.common,
	&bus_ir_tx_clk.common,
	&bus_ths_clk.common,
	&i2s3_clk.common,
	&i2s0_clk.common,
	&i2s1_clk.common,
	&i2s2_clk.common,
	&bus_i2s0_clk.common,
	&bus_i2s1_clk.common,
	&bus_i2s2_clk.common,
	&bus_i2s3_clk.common,
	&spdif_clk.common,
	&bus_spdif_clk.common,
	&dmic_clk.common,
	&bus_dmic_clk.common,
	&audio_hub_clk.common,
	&bus_audio_hub_clk.common,
	&usb_ohci0_clk.common,
	&usb_phy0_clk.common,
	&usb_phy1_clk.common,
	&usb_ohci3_clk.common,
	&usb_phy3_clk.common,
	&usb_hsic_12m_clk.common,
	&usb_hsic_clk.common,
	&bus_ohci0_clk.common,
	&bus_ohci3_clk.common,
	&bus_ehci0_clk.common,
	&bus_xhci_clk.common,
	&bus_ehci3_clk.common,
	&bus_otg_clk.common,
	&pcie_ref_clk.common,
	&pcie_ref_out_clk.common,
	&pcie_maxi_clk.common,
	&pcie_aux_clk.common,
	&bus_pcie_clk.common,
	&hdmi_clk.common,
	&hdmi_slow_clk.common,
	&hdmi_cec_clk.common,
	&bus_hdmi_clk.common,
	&bus_tcon_top_clk.common,
	&tcon_lcd0_clk.common,
	&bus_tcon_lcd0_clk.common,
	&tcon_tv0_clk.common,
	&bus_tcon_tv0_clk.common,
	&csi_cci_clk.common,
	&csi_top_clk.common,
	&csi_mclk_clk.common,
	&bus_csi_clk.common,
	&hdcp_clk.common,
	&bus_hdcp_clk.common,
};

static struct clk_hw_onecell_data sun50i_h6_hw_clks = {
	.hws	= {
		[CLK_OSC12M]		= &osc12M_clk.hw,
		[CLK_PLL_CPUX]		= &pll_cpux_clk.common.hw,
		[CLK_PLL_DDR0]		= &pll_ddr0_clk.common.hw,
		[CLK_PLL_PERIPH0]	= &pll_periph0_clk.common.hw,
		[CLK_PLL_PERIPH0_2X]	= &pll_periph0_2x_clk.hw,
		[CLK_PLL_PERIPH0_4X]	= &pll_periph0_4x_clk.hw,
		[CLK_PLL_PERIPH1]	= &pll_periph1_clk.common.hw,
		[CLK_PLL_PERIPH1_2X]	= &pll_periph1_2x_clk.hw,
		[CLK_PLL_PERIPH1_4X]	= &pll_periph1_4x_clk.hw,
		[CLK_PLL_GPU]		= &pll_gpu_clk.common.hw,
		[CLK_PLL_VIDEO0]	= &pll_video0_clk.common.hw,
		[CLK_PLL_VIDEO0_4X]	= &pll_video0_4x_clk.hw,
		[CLK_PLL_VIDEO1]	= &pll_video1_clk.common.hw,
		[CLK_PLL_VIDEO1_4X]	= &pll_video1_4x_clk.hw,
		[CLK_PLL_VE]		= &pll_ve_clk.common.hw,
		[CLK_PLL_DE]		= &pll_de_clk.common.hw,
		[CLK_PLL_HSIC]		= &pll_hsic_clk.common.hw,
		[CLK_PLL_AUDIO_BASE]	= &pll_audio_base_clk.common.hw,
		[CLK_PLL_AUDIO]		= &pll_audio_clk.hw,
		[CLK_PLL_AUDIO_2X]	= &pll_audio_2x_clk.hw,
		[CLK_PLL_AUDIO_4X]	= &pll_audio_4x_clk.hw,
		[CLK_CPUX]		= &cpux_clk.common.hw,
		[CLK_AXI]		= &axi_clk.common.hw,
		[CLK_CPUX_APB]		= &cpux_apb_clk.common.hw,
		[CLK_PSI_AHB1_AHB2]	= &psi_ahb1_ahb2_clk.common.hw,
		[CLK_AHB3]		= &ahb3_clk.common.hw,
		[CLK_APB1]		= &apb1_clk.common.hw,
		[CLK_APB2]		= &apb2_clk.common.hw,
		[CLK_MBUS]		= &mbus_clk.common.hw,
		[CLK_DE]		= &de_clk.common.hw,
		[CLK_BUS_DE]		= &bus_de_clk.common.hw,
		[CLK_DEINTERLACE]	= &deinterlace_clk.common.hw,
		[CLK_BUS_DEINTERLACE]	= &bus_deinterlace_clk.common.hw,
		[CLK_GPU]		= &gpu_clk.common.hw,
		[CLK_BUS_GPU]		= &bus_gpu_clk.common.hw,
		[CLK_CE]		= &ce_clk.common.hw,
		[CLK_BUS_CE]		= &bus_ce_clk.common.hw,
		[CLK_VE]		= &ve_clk.common.hw,
		[CLK_BUS_VE]		= &bus_ve_clk.common.hw,
		[CLK_EMCE]		= &emce_clk.common.hw,
		[CLK_BUS_EMCE]		= &bus_emce_clk.common.hw,
		[CLK_VP9]		= &vp9_clk.common.hw,
		[CLK_BUS_VP9]		= &bus_vp9_clk.common.hw,
		[CLK_BUS_DMA]		= &bus_dma_clk.common.hw,
		[CLK_BUS_MSGBOX]	= &bus_msgbox_clk.common.hw,
		[CLK_BUS_SPINLOCK]	= &bus_spinlock_clk.common.hw,
		[CLK_BUS_HSTIMER]	= &bus_hstimer_clk.common.hw,
		[CLK_AVS]		= &avs_clk.common.hw,
		[CLK_BUS_DBG]		= &bus_dbg_clk.common.hw,
		[CLK_BUS_PSI]		= &bus_psi_clk.common.hw,
		[CLK_BUS_PWM]		= &bus_pwm_clk.common.hw,
		[CLK_BUS_IOMMU]		= &bus_iommu_clk.common.hw,
		[CLK_DRAM]		= &dram_clk.common.hw,
		[CLK_MBUS_DMA]		= &mbus_dma_clk.common.hw,
		[CLK_MBUS_VE]		= &mbus_ve_clk.common.hw,
		[CLK_MBUS_CE]		= &mbus_ce_clk.common.hw,
		[CLK_MBUS_TS]		= &mbus_ts_clk.common.hw,
		[CLK_MBUS_NAND]		= &mbus_nand_clk.common.hw,
		[CLK_MBUS_CSI]		= &mbus_csi_clk.common.hw,
		[CLK_MBUS_DEINTERLACE]	= &mbus_deinterlace_clk.common.hw,
		[CLK_BUS_DRAM]		= &bus_dram_clk.common.hw,
		[CLK_NAND0]		= &nand0_clk.common.hw,
		[CLK_NAND1]		= &nand1_clk.common.hw,
		[CLK_BUS_NAND]		= &bus_nand_clk.common.hw,
		[CLK_MMC0]		= &mmc0_clk.common.hw,
		[CLK_MMC1]		= &mmc1_clk.common.hw,
		[CLK_MMC2]		= &mmc2_clk.common.hw,
		[CLK_BUS_MMC0]		= &bus_mmc0_clk.common.hw,
		[CLK_BUS_MMC1]		= &bus_mmc1_clk.common.hw,
		[CLK_BUS_MMC2]		= &bus_mmc2_clk.common.hw,
		[CLK_BUS_UART0]		= &bus_uart0_clk.common.hw,
		[CLK_BUS_UART1]		= &bus_uart1_clk.common.hw,
		[CLK_BUS_UART2]		= &bus_uart2_clk.common.hw,
		[CLK_BUS_UART3]		= &bus_uart3_clk.common.hw,
		[CLK_BUS_I2C0]		= &bus_i2c0_clk.common.hw,
		[CLK_BUS_I2C1]		= &bus_i2c1_clk.common.hw,
		[CLK_BUS_I2C2]		= &bus_i2c2_clk.common.hw,
		[CLK_BUS_I2C3]		= &bus_i2c3_clk.common.hw,
		[CLK_BUS_SCR0]		= &bus_scr0_clk.common.hw,
		[CLK_BUS_SCR1]		= &bus_scr1_clk.common.hw,
		[CLK_SPI0]		= &spi0_clk.common.hw,
		[CLK_SPI1]		= &spi1_clk.common.hw,
		[CLK_BUS_SPI0]		= &bus_spi0_clk.common.hw,
		[CLK_BUS_SPI1]		= &bus_spi1_clk.common.hw,
		[CLK_BUS_EMAC]		= &bus_emac_clk.common.hw,
		[CLK_TS]		= &ts_clk.common.hw,
		[CLK_BUS_TS]		= &bus_ts_clk.common.hw,
		[CLK_IR_TX]		= &ir_tx_clk.common.hw,
		[CLK_BUS_IR_TX]		= &bus_ir_tx_clk.common.hw,
		[CLK_BUS_THS]		= &bus_ths_clk.common.hw,
		[CLK_I2S3]		= &i2s3_clk.common.hw,
		[CLK_I2S0]		= &i2s0_clk.common.hw,
		[CLK_I2S1]		= &i2s1_clk.common.hw,
		[CLK_I2S2]		= &i2s2_clk.common.hw,
		[CLK_BUS_I2S0]		= &bus_i2s0_clk.common.hw,
		[CLK_BUS_I2S1]		= &bus_i2s1_clk.common.hw,
		[CLK_BUS_I2S2]		= &bus_i2s2_clk.common.hw,
		[CLK_BUS_I2S3]		= &bus_i2s3_clk.common.hw,
		[CLK_SPDIF]		= &spdif_clk.common.hw,
		[CLK_BUS_SPDIF]		= &bus_spdif_clk.common.hw,
		[CLK_DMIC]		= &dmic_clk.common.hw,
		[CLK_BUS_DMIC]		= &bus_dmic_clk.common.hw,
		[CLK_AUDIO_HUB]		= &audio_hub_clk.common.hw,
		[CLK_BUS_AUDIO_HUB]	= &bus_audio_hub_clk.common.hw,
		[CLK_USB_OHCI0]		= &usb_ohci0_clk.common.hw,
		[CLK_USB_PHY0]		= &usb_phy0_clk.common.hw,
		[CLK_USB_PHY1]		= &usb_phy1_clk.common.hw,
		[CLK_USB_OHCI3]		= &usb_ohci3_clk.common.hw,
		[CLK_USB_PHY3]		= &usb_phy3_clk.common.hw,
		[CLK_USB_HSIC_12M]	= &usb_hsic_12m_clk.common.hw,
		[CLK_USB_HSIC]		= &usb_hsic_clk.common.hw,
		[CLK_BUS_OHCI0]		= &bus_ohci0_clk.common.hw,
		[CLK_BUS_OHCI3]		= &bus_ohci3_clk.common.hw,
		[CLK_BUS_EHCI0]		= &bus_ehci0_clk.common.hw,
		[CLK_BUS_XHCI]		= &bus_xhci_clk.common.hw,
		[CLK_BUS_EHCI3]		= &bus_ehci3_clk.common.hw,
		[CLK_BUS_OTG]		= &bus_otg_clk.common.hw,
		[CLK_PCIE_REF_100M]	= &pcie_ref_100m_clk.hw,
		[CLK_PCIE_REF]		= &pcie_ref_clk.common.hw,
		[CLK_PCIE_REF_OUT]	= &pcie_ref_out_clk.common.hw,
		[CLK_PCIE_MAXI]		= &pcie_maxi_clk.common.hw,
		[CLK_PCIE_AUX]		= &pcie_aux_clk.common.hw,
		[CLK_BUS_PCIE]		= &bus_pcie_clk.common.hw,
		[CLK_HDMI]		= &hdmi_clk.common.hw,
		[CLK_HDMI_SLOW]		= &hdmi_slow_clk.common.hw,
		[CLK_HDMI_CEC]		= &hdmi_cec_clk.common.hw,
		[CLK_BUS_HDMI]		= &bus_hdmi_clk.common.hw,
		[CLK_BUS_TCON_TOP]	= &bus_tcon_top_clk.common.hw,
		[CLK_TCON_LCD0]		= &tcon_lcd0_clk.common.hw,
		[CLK_BUS_TCON_LCD0]	= &bus_tcon_lcd0_clk.common.hw,
		[CLK_TCON_TV0]		= &tcon_tv0_clk.common.hw,
		[CLK_BUS_TCON_TV0]	= &bus_tcon_tv0_clk.common.hw,
		[CLK_CSI_CCI]		= &csi_cci_clk.common.hw,
		[CLK_CSI_TOP]		= &csi_top_clk.common.hw,
		[CLK_CSI_MCLK]		= &csi_mclk_clk.common.hw,
		[CLK_BUS_CSI]		= &bus_csi_clk.common.hw,
		[CLK_HDCP]		= &hdcp_clk.common.hw,
		[CLK_BUS_HDCP]		= &bus_hdcp_clk.common.hw,
	},
	.num = CLK_NUMBER,
};

static struct ccu_reset_map sun50i_h6_ccu_resets[] = {
	[RST_MBUS]		= { 0x540, BIT(30) },

	[RST_BUS_DE]		= { 0x60c, BIT(16) },
	[RST_BUS_DEINTERLACE]	= { 0x62c, BIT(16) },
	[RST_BUS_GPU]		= { 0x67c, BIT(16) },
	[RST_BUS_CE]		= { 0x68c, BIT(16) },
	[RST_BUS_VE]		= { 0x69c, BIT(16) },
	[RST_BUS_EMCE]		= { 0x6bc, BIT(16) },
	[RST_BUS_VP9]		= { 0x6cc, BIT(16) },
	[RST_BUS_DMA]		= { 0x70c, BIT(16) },
	[RST_BUS_MSGBOX]	= { 0x71c, BIT(16) },
	[RST_BUS_SPINLOCK]	= { 0x72c, BIT(16) },
	[RST_BUS_HSTIMER]	= { 0x73c, BIT(16) },
	[RST_BUS_DBG]		= { 0x78c, BIT(16) },
	[RST_BUS_PSI]		= { 0x79c, BIT(16) },
	[RST_BUS_PWM]		= { 0x7ac, BIT(16) },
	[RST_BUS_IOMMU]		= { 0x7bc, BIT(16) },
	[RST_BUS_DRAM]		= { 0x80c, BIT(16) },
	[RST_BUS_NAND]		= { 0x82c, BIT(16) },
	[RST_BUS_MMC0]		= { 0x84c, BIT(16) },
	[RST_BUS_MMC1]		= { 0x84c, BIT(17) },
	[RST_BUS_MMC2]		= { 0x84c, BIT(18) },
	[RST_BUS_UART0]		= { 0x90c, BIT(16) },
	[RST_BUS_UART1]		= { 0x90c, BIT(17) },
	[RST_BUS_UART2]		= { 0x90c, BIT(18) },
	[RST_BUS_UART3]		= { 0x90c, BIT(19) },
	[RST_BUS_I2C0]		= { 0x91c, BIT(16) },
	[RST_BUS_I2C1]		= { 0x91c, BIT(17) },
	[RST_BUS_I2C2]		= { 0x91c, BIT(18) },
	[RST_BUS_I2C3]		= { 0x91c, BIT(19) },
	[RST_BUS_SCR0]		= { 0x93c, BIT(16) },
	[RST_BUS_SCR1]		= { 0x93c, BIT(17) },
	[RST_BUS_SPI0]		= { 0x96c, BIT(16) },
	[RST_BUS_SPI1]		= { 0x96c, BIT(17) },
	[RST_BUS_EMAC]		= { 0x97c, BIT(16) },
	[RST_BUS_TS]		= { 0x9bc, BIT(16) },
	[RST_BUS_IR_TX]		= { 0x9cc, BIT(16) },
	[RST_BUS_THS]		= { 0x9fc, BIT(16) },
	[RST_BUS_I2S0]		= { 0xa1c, BIT(16) },
	[RST_BUS_I2S1]		= { 0xa1c, BIT(17) },
	[RST_BUS_I2S2]		= { 0xa1c, BIT(18) },
	[RST_BUS_I2S3]		= { 0xa1c, BIT(19) },
	[RST_BUS_SPDIF]		= { 0xa2c, BIT(16) },
	[RST_BUS_DMIC]		= { 0xa4c, BIT(16) },
	[RST_BUS_AUDIO_HUB]	= { 0xa6c, BIT(16) },

	[RST_USB_PHY0]		= { 0xa70, BIT(30) },
	[RST_USB_PHY1]		= { 0xa74, BIT(30) },
	[RST_USB_PHY3]		= { 0xa7c, BIT(30) },
	[RST_USB_HSIC]		= { 0xa7c, BIT(28) },

	[RST_BUS_OHCI0]		= { 0xa8c, BIT(16) },
	[RST_BUS_OHCI3]		= { 0xa8c, BIT(19) },
	[RST_BUS_EHCI0]		= { 0xa8c, BIT(20) },
	[RST_BUS_XHCI]		= { 0xa8c, BIT(21) },
	[RST_BUS_EHCI3]		= { 0xa8c, BIT(23) },
	[RST_BUS_OTG]		= { 0xa8c, BIT(24) },
	[RST_BUS_PCIE]		= { 0xabc, BIT(16) },

	[RST_PCIE_POWERUP]	= { 0xabc, BIT(17) },

	[RST_BUS_HDMI]		= { 0xb1c, BIT(16) },
	[RST_BUS_HDMI_SUB]	= { 0xb1c, BIT(17) },
	[RST_BUS_TCON_TOP]	= { 0xb5c, BIT(16) },
	[RST_BUS_TCON_LCD0]	= { 0xb7c, BIT(16) },
	[RST_BUS_TCON_TV0]	= { 0xb9c, BIT(16) },
	[RST_BUS_CSI]		= { 0xc2c, BIT(16) },
	[RST_BUS_HDCP]		= { 0xc4c, BIT(16) },
};

static const struct sunxi_ccu_desc sun50i_h6_ccu_desc = {
	.ccu_clks	= sun50i_h6_ccu_clks,
	.num_ccu_clks	= ARRAY_SIZE(sun50i_h6_ccu_clks),

	.hw_clks	= &sun50i_h6_hw_clks,

	.resets		= sun50i_h6_ccu_resets,
	.num_resets	= ARRAY_SIZE(sun50i_h6_ccu_resets),
};

static const u32 pll_regs[] = {
	SUN50I_H6_PLL_CPUX_REG,
	SUN50I_H6_PLL_DDR0_REG,
	SUN50I_H6_PLL_PERIPH0_REG,
	SUN50I_H6_PLL_PERIPH1_REG,
	SUN50I_H6_PLL_GPU_REG,
	SUN50I_H6_PLL_VIDEO0_REG,
	SUN50I_H6_PLL_VIDEO1_REG,
	SUN50I_H6_PLL_VE_REG,
	SUN50I_H6_PLL_DE_REG,
	SUN50I_H6_PLL_HSIC_REG,
	SUN50I_H6_PLL_AUDIO_REG,
};

static const u32 pll_video_regs[] = {
	SUN50I_H6_PLL_VIDEO0_REG,
	SUN50I_H6_PLL_VIDEO1_REG,
};

static const u32 usb2_clk_regs[] = {
	SUN50I_H6_USB0_CLK_REG,
	SUN50I_H6_USB3_CLK_REG,
};

static int sun50i_h6_ccu_probe(struct platform_device *pdev)
{
	void __iomem *reg;
	u32 val;
	int i;

	reg = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(reg))
		return PTR_ERR(reg);

	/* Enable the lock bits on all PLLs */
	for (i = 0; i < ARRAY_SIZE(pll_regs); i++) {
		val = readl(reg + pll_regs[i]);
		val |= BIT(29);
		writel(val, reg + pll_regs[i]);
	}

	/*
	 * Force the output divider of video PLLs to 0.
	 *
	 * See the comment before pll-video0 definition for the reason.
	 */
	for (i = 0; i < ARRAY_SIZE(pll_video_regs); i++) {
		val = readl(reg + pll_video_regs[i]);
		val &= ~BIT(0);
		writel(val, reg + pll_video_regs[i]);
	}

	/*
	 * Force OHCI 12M clock sources to 00 (12MHz divided from 48MHz)
	 *
	 * This clock mux is still mysterious, and the code just enforces
	 * it to have a valid clock parent.
	 */
	for (i = 0; i < ARRAY_SIZE(usb2_clk_regs); i++) {
		val = readl(reg + usb2_clk_regs[i]);
		val &= ~GENMASK(25, 24);
		writel (val, reg + usb2_clk_regs[i]);
	}

	/*
	 * Force the post-divider of pll-audio to 12 and the output divider
	 * of it to 2, so 24576000 and 22579200 rates can be set exactly.
	 */
	val = readl(reg + SUN50I_H6_PLL_AUDIO_REG);
	val &= ~(GENMASK(21, 16) | BIT(0));
	writel(val | (11 << 16) | BIT(0), reg + SUN50I_H6_PLL_AUDIO_REG);

	/*
	 * First clock parent (osc32K) is unusable for CEC. But since there
	 * is no good way to force parent switch (both run with same frequency),
	 * just set second clock parent here.
	 */
	val = readl(reg + SUN50I_H6_HDMI_CEC_CLK_REG);
	val |= BIT(24);
	writel(val, reg + SUN50I_H6_HDMI_CEC_CLK_REG);

	return devm_sunxi_ccu_probe(&pdev->dev, reg, &sun50i_h6_ccu_desc);
}

static const struct of_device_id sun50i_h6_ccu_ids[] = {
	{ .compatible = "allwinner,sun50i-h6-ccu" },
	{ }
};

static struct platform_driver sun50i_h6_ccu_driver = {
	.probe	= sun50i_h6_ccu_probe,
	.driver	= {
		.name	= "sun50i-h6-ccu",
		.suppress_bind_attrs = true,
		.of_match_table	= sun50i_h6_ccu_ids,
	},
};
module_platform_driver(sun50i_h6_ccu_driver);

MODULE_IMPORT_NS(SUNXI_CCU);
MODULE_LICENSE("GPL");
