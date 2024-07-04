// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 Yangtao Li <frank@allwinnertech.com>
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

#include "ccu-sun50i-a100.h"

#define SUN50I_A100_PLL_SDM_ENABLE		BIT(24)
#define SUN50I_A100_PLL_OUTPUT_ENABLE		BIT(27)
#define SUN50I_A100_PLL_LOCK			BIT(28)
#define SUN50I_A100_PLL_LOCK_ENABLE		BIT(29)
#define SUN50I_A100_PLL_ENABLE			BIT(31)

#define SUN50I_A100_PLL_PERIPH1_PATTERN0	0xd1303333

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
#define SUN50I_A100_PLL_CPUX_REG		0x000
static struct ccu_mult pll_cpux_clk = {
	.enable		= SUN50I_A100_PLL_OUTPUT_ENABLE,
	.lock		= SUN50I_A100_PLL_LOCK,
	.mult		= _SUNXI_CCU_MULT_MIN(8, 8, 12),
	.common		= {
		.reg		= 0x000,
		.hw.init	= CLK_HW_INIT("pll-cpux", "dcxo24M",
					      &ccu_mult_ops,
					      CLK_SET_RATE_UNGATE),
	},
};

/* Some PLLs are input * N / div1 / P. Model them as NKMP with no K */
#define SUN50I_A100_PLL_DDR0_REG		0x010
static struct ccu_nkmp pll_ddr0_clk = {
	.enable		= SUN50I_A100_PLL_OUTPUT_ENABLE,
	.lock		= SUN50I_A100_PLL_LOCK,
	.n		= _SUNXI_CCU_MULT_MIN(8, 8, 12),
	.m		= _SUNXI_CCU_DIV(1, 1), /* input divider */
	.p		= _SUNXI_CCU_DIV(0, 1), /* output divider */
	.common		= {
		.reg		= 0x010,
		.hw.init	= CLK_HW_INIT("pll-ddr0", "dcxo24M",
					      &ccu_nkmp_ops,
					      CLK_SET_RATE_UNGATE |
					      CLK_IS_CRITICAL),
	},
};

#define SUN50I_A100_PLL_PERIPH0_REG	0x020
static struct ccu_nkmp pll_periph0_clk = {
	.enable		= SUN50I_A100_PLL_OUTPUT_ENABLE,
	.lock		= SUN50I_A100_PLL_LOCK,
	.n		= _SUNXI_CCU_MULT_MIN(8, 8, 12),
	.m		= _SUNXI_CCU_DIV(1, 1), /* input divider */
	.p		= _SUNXI_CCU_DIV(0, 1), /* output divider */
	.fixed_post_div	= 2,
	.common		= {
		.reg		= 0x020,
		.features	= CCU_FEATURE_FIXED_POSTDIV,
		.hw.init	= CLK_HW_INIT("pll-periph0", "dcxo24M",
					      &ccu_nkmp_ops,
					      CLK_SET_RATE_UNGATE),
	},
};

#define SUN50I_A100_PLL_PERIPH1_REG	0x028
static struct ccu_nkmp pll_periph1_clk = {
	.enable		= SUN50I_A100_PLL_OUTPUT_ENABLE,
	.lock		= SUN50I_A100_PLL_LOCK,
	.n		= _SUNXI_CCU_MULT_MIN(8, 8, 12),
	.m		= _SUNXI_CCU_DIV(1, 1), /* input divider */
	.p		= _SUNXI_CCU_DIV(0, 1), /* output divider */
	.fixed_post_div	= 2,
	.common		= {
		.reg		= 0x028,
		.features	= CCU_FEATURE_FIXED_POSTDIV,
		.hw.init	= CLK_HW_INIT("pll-periph1", "dcxo24M",
					      &ccu_nkmp_ops,
					      CLK_SET_RATE_UNGATE),
	},
};
#define SUN50I_A100_PLL_PERIPH1_PATTERN0_REG	0x128

#define SUN50I_A100_PLL_GPU_REG		0x030
static struct ccu_nkmp pll_gpu_clk = {
	.enable		= SUN50I_A100_PLL_OUTPUT_ENABLE,
	.lock		= SUN50I_A100_PLL_LOCK,
	.n		= _SUNXI_CCU_MULT_MIN(8, 8, 12),
	.m		= _SUNXI_CCU_DIV(1, 1), /* input divider */
	.p		= _SUNXI_CCU_DIV(0, 1), /* output divider */
	.common		= {
		.reg		= 0x030,
		.hw.init	= CLK_HW_INIT("pll-gpu", "dcxo24M",
					      &ccu_nkmp_ops,
					      CLK_SET_RATE_UNGATE),
	},
};

/*
 * For Video PLLs, the output divider is described as "used for testing"
 * in the user manual. So it's not modelled and forced to 0.
 */
#define SUN50I_A100_PLL_VIDEO0_REG	0x040
static struct ccu_nm pll_video0_clk = {
	.enable		= SUN50I_A100_PLL_OUTPUT_ENABLE,
	.lock		= SUN50I_A100_PLL_LOCK,
	.n		= _SUNXI_CCU_MULT_MIN(8, 8, 12),
	.m		= _SUNXI_CCU_DIV(1, 1), /* input divider */
	.fixed_post_div	= 4,
	.common		= {
		.reg		= 0x040,
		.features	= CCU_FEATURE_FIXED_POSTDIV,
		.hw.init	= CLK_HW_INIT("pll-video0", "dcxo24M",
					      &ccu_nm_ops,
					      CLK_SET_RATE_UNGATE),
	},
};

#define SUN50I_A100_PLL_VIDEO1_REG	0x048
static struct ccu_nm pll_video1_clk = {
	.enable		= SUN50I_A100_PLL_OUTPUT_ENABLE,
	.lock		= SUN50I_A100_PLL_LOCK,
	.n		= _SUNXI_CCU_MULT_MIN(8, 8, 12),
	.m		= _SUNXI_CCU_DIV(1, 1), /* input divider */
	.fixed_post_div	= 4,
	.common		= {
		.reg		= 0x048,
		.features	= CCU_FEATURE_FIXED_POSTDIV,
		.hw.init	= CLK_HW_INIT("pll-video1", "dcxo24M",
					      &ccu_nm_ops,
					      CLK_SET_RATE_UNGATE),
	},
};

#define SUN50I_A100_PLL_VIDEO2_REG	0x050
static struct ccu_nm pll_video2_clk = {
	.enable		= SUN50I_A100_PLL_OUTPUT_ENABLE,
	.lock		= SUN50I_A100_PLL_LOCK,
	.n		= _SUNXI_CCU_MULT_MIN(8, 8, 12),
	.m		= _SUNXI_CCU_DIV(1, 1), /* input divider */
	.fixed_post_div	= 4,
	.common		= {
		.reg		= 0x050,
		.features	= CCU_FEATURE_FIXED_POSTDIV,
		.hw.init	= CLK_HW_INIT("pll-video2", "dcxo24M",
					      &ccu_nm_ops,
					      CLK_SET_RATE_UNGATE),
	},
};

#define SUN50I_A100_PLL_VE_REG		0x058
static struct ccu_nkmp pll_ve_clk = {
	.enable		= SUN50I_A100_PLL_OUTPUT_ENABLE,
	.lock		= SUN50I_A100_PLL_LOCK,
	.n		= _SUNXI_CCU_MULT_MIN(8, 8, 12),
	.m		= _SUNXI_CCU_DIV(1, 1), /* input divider */
	.p		= _SUNXI_CCU_DIV(0, 1), /* output divider */
	.common		= {
		.reg		= 0x058,
		.hw.init	= CLK_HW_INIT("pll-ve", "dcxo24M",
					      &ccu_nkmp_ops,
					      CLK_SET_RATE_UNGATE),
	},
};

/*
 * The COM PLL has m0 dividers in addition to the usual N, M
 * factors. Since we only need 1 frequencies from this PLL: 45.1584 MHz,
 * ignore it for now.
 */
#define SUN50I_A100_PLL_COM_REG		0x060
static struct ccu_sdm_setting pll_com_sdm_table[] = {
	{ .rate = 451584000, .pattern = 0xc0014396, .m = 2, .n = 37 },
};

static struct ccu_nm pll_com_clk = {
	.enable		= SUN50I_A100_PLL_OUTPUT_ENABLE,
	.lock		= SUN50I_A100_PLL_LOCK,
	.n		= _SUNXI_CCU_MULT_MIN(8, 8, 12),
	.m		= _SUNXI_CCU_DIV(0, 1),
	.sdm		= _SUNXI_CCU_SDM(pll_com_sdm_table, BIT(24),
					 0x160, BIT(31)),
	.common		= {
		.reg		= 0x060,
		.features	= CCU_FEATURE_SIGMA_DELTA_MOD,
		.hw.init	= CLK_HW_INIT("pll-com", "dcxo24M",
					      &ccu_nm_ops,
					      CLK_SET_RATE_UNGATE),
	},
};

#define SUN50I_A100_PLL_VIDEO3_REG	0x068
static struct ccu_nm pll_video3_clk = {
	.enable		= SUN50I_A100_PLL_OUTPUT_ENABLE,
	.lock		= SUN50I_A100_PLL_LOCK,
	.n		= _SUNXI_CCU_MULT_MIN(8, 8, 12),
	.m		= _SUNXI_CCU_DIV(1, 1), /* input divider */
	.fixed_post_div	= 4,
	.common		= {
		.reg		= 0x068,
		.features	= CCU_FEATURE_FIXED_POSTDIV,
		.hw.init	= CLK_HW_INIT("pll-video3", "dcxo24M",
					      &ccu_nm_ops,
					      CLK_SET_RATE_UNGATE),
	},
};

/*
 * The Audio PLL has m0, m1 dividers in addition to the usual N, M
 * factors. Since we only need 4 frequencies from this PLL: 22.5792 MHz,
 * 24.576 MHz, 90.3168MHz and 98.304MHz ignore them for now.
 * Enforce the default for them, which is m0 = 1, m1 = 0.
 */
#define SUN50I_A100_PLL_AUDIO_REG		0x078
static struct ccu_sdm_setting pll_audio_sdm_table[] = {
	{ .rate = 45158400, .pattern = 0xc001bcd3, .m = 18, .n = 33 },
	{ .rate = 49152000, .pattern = 0xc001eb85, .m = 20, .n = 40 },
	{ .rate = 180633600, .pattern = 0xc001288d, .m = 3, .n = 22 },
	{ .rate = 196608000, .pattern = 0xc001eb85, .m = 5, .n = 40 },
};

static struct ccu_nm pll_audio_clk = {
	.enable		= SUN50I_A100_PLL_OUTPUT_ENABLE,
	.lock		= SUN50I_A100_PLL_LOCK,
	.n		= _SUNXI_CCU_MULT_MIN(8, 8, 12),
	.m		= _SUNXI_CCU_DIV(16, 6),
	.fixed_post_div	= 2,
	.sdm		= _SUNXI_CCU_SDM(pll_audio_sdm_table, BIT(24),
					 0x178, BIT(31)),
	.common		= {
		.reg		= 0x078,
		.features	= CCU_FEATURE_FIXED_POSTDIV |
				  CCU_FEATURE_SIGMA_DELTA_MOD,
		.hw.init	= CLK_HW_INIT("pll-audio", "dcxo24M",
					      &ccu_nm_ops,
					      CLK_SET_RATE_UNGATE),
	},
};

static const char * const cpux_parents[] = { "dcxo24M", "osc32k",
					     "iosc", "pll-cpux",
					      "pll-periph0" };
static SUNXI_CCU_MUX(cpux_clk, "cpux", cpux_parents,
		     0x500, 24, 3, CLK_SET_RATE_PARENT | CLK_IS_CRITICAL);
static SUNXI_CCU_M(axi_clk, "axi", "cpux", 0x500, 0, 2, 0);
static SUNXI_CCU_M(cpux_apb_clk, "cpux-apb", "cpux", 0x500, 8, 2, 0);

static const char * const psi_ahb1_ahb2_parents[] = { "dcxo24M", "osc32k",
						      "iosc", "pll-periph0",
						      "pll-periph0-2x" };
static SUNXI_CCU_MP_WITH_MUX(psi_ahb1_ahb2_clk, "psi-ahb1-ahb2",
			     psi_ahb1_ahb2_parents, 0x510,
			     0, 2,	/* M */
			     8, 2,	/* P */
			     24, 3,	/* mux */
			     0);

static const char * const ahb3_apb1_apb2_parents[] = { "dcxo24M", "osc32k",
						       "psi-ahb1-ahb2",
						       "pll-periph0",
						       "pll-periph0-2x" };
static SUNXI_CCU_MP_WITH_MUX(ahb3_clk, "ahb3", ahb3_apb1_apb2_parents, 0x51c,
			     0, 2,	/* M */
			     8, 2,	/* P */
			     24, 3,	/* mux */
			     0);

static SUNXI_CCU_MP_WITH_MUX(apb1_clk, "apb1", ahb3_apb1_apb2_parents, 0x520,
			     0, 2,	/* M */
			     8, 2,	/* P */
			     24, 3,	/* mux */
			     0);

static SUNXI_CCU_MP_WITH_MUX(apb2_clk, "apb2", ahb3_apb1_apb2_parents, 0x524,
			     0, 2,	/* M */
			     8, 2,	/* P */
			     24, 3,	/* mux */
			     0);

static const char * const mbus_parents[] = { "dcxo24M", "pll-ddr0",
					     "pll-periph0",
					     "pll-periph0-2x" };
static SUNXI_CCU_M_WITH_MUX_GATE(mbus_clk, "mbus", mbus_parents, 0x540,
				 0, 3,		/* M */
				 24, 2,		/* mux */
				 BIT(31),	/* gate */
				 CLK_IS_CRITICAL);

static const char * const de_parents[] = { "pll-com", "pll-periph0-2x" };
static SUNXI_CCU_M_WITH_MUX_GATE(de_clk, "de0", de_parents, 0x600,
				 0, 4,		/* M */
				 24, 1,		/* mux */
				 BIT(31),	/* gate */
				 CLK_SET_RATE_PARENT);

static SUNXI_CCU_GATE(bus_de_clk, "bus-de", "psi-ahb1-ahb2",
		      0x60c, BIT(0), 0);

static const char * const g2d_parents[] = { "pll-com", "pll-periph0-2x",
					     "pll-video0-2x", "pll-video1-2x",
					     "pll-video2-2x"};
static SUNXI_CCU_M_WITH_MUX_GATE(g2d_clk, "g2d",
				 g2d_parents,
				 0x630,
				 0, 4,		/* M */
				 24, 3,		/* mux */
				 BIT(31),	/* gate */
				 0);

static SUNXI_CCU_GATE(bus_g2d_clk, "bus-g2d", "psi-ahb1-ahb2",
		      0x63c, BIT(0), 0);

static const char * const gpu_parents[] = { "pll-gpu" };
static SUNXI_CCU_M_WITH_MUX_GATE(gpu_clk, "gpu", gpu_parents, 0x670,
				       0, 2,	/* M */
				       24, 1,	/* mux */
				       BIT(31),	/* gate */
				       0);

static SUNXI_CCU_GATE(bus_gpu_clk, "bus-gpu", "psi-ahb1-ahb2",
		      0x67c, BIT(0), 0);

static const char * const ce_parents[] = { "dcxo24M", "pll-periph0-2x" };
static SUNXI_CCU_MP_WITH_MUX_GATE(ce_clk, "ce", ce_parents, 0x680,
				  0, 4,		/* M */
				  8, 2,		/* P */
				  24, 1,	/* mux */
				  BIT(31),	/* gate */
				  0);

static SUNXI_CCU_GATE(bus_ce_clk, "bus-ce", "psi-ahb1-ahb2",
		      0x68c, BIT(0), 0);

static const char * const ve_parents[] = { "pll-ve" };
static SUNXI_CCU_M_WITH_MUX_GATE(ve_clk, "ve", ve_parents, 0x690,
				 0, 3,		/* M */
				 24, 1,		/* mux */
				 BIT(31),	/* gate */
				 CLK_SET_RATE_PARENT);

static SUNXI_CCU_GATE(bus_ve_clk, "bus-ve", "psi-ahb1-ahb2",
		      0x69c, BIT(0), 0);

static SUNXI_CCU_GATE(bus_dma_clk, "bus-dma", "psi-ahb1-ahb2",
		      0x70c, BIT(0), 0);

static SUNXI_CCU_GATE(bus_msgbox_clk, "bus-msgbox", "psi-ahb1-ahb2",
		      0x71c, BIT(0), 0);

static SUNXI_CCU_GATE(bus_spinlock_clk, "bus-spinlock", "psi-ahb1-ahb2",
		      0x72c, BIT(0), 0);

static SUNXI_CCU_GATE(bus_hstimer_clk, "bus-hstimer", "psi-ahb1-ahb2",
		      0x73c, BIT(0), 0);

static SUNXI_CCU_GATE(avs_clk, "avs", "dcxo24M", 0x740, BIT(31), 0);

static SUNXI_CCU_GATE(bus_dbg_clk, "bus-dbg", "psi-ahb1-ahb2",
		      0x78c, BIT(0), 0);

static SUNXI_CCU_GATE(bus_psi_clk, "bus-psi", "psi-ahb1-ahb2",
		      0x79c, BIT(0), 0);

static SUNXI_CCU_GATE(bus_pwm_clk, "bus-pwm", "apb1", 0x7ac, BIT(0), 0);

static SUNXI_CCU_GATE(bus_iommu_clk, "bus-iommu", "apb1", 0x7bc, BIT(0), 0);

static SUNXI_CCU_GATE(mbus_dma_clk, "mbus-dma", "mbus",
		      0x804, BIT(0), 0);
static SUNXI_CCU_GATE(mbus_ve_clk, "mbus-ve", "mbus",
		      0x804, BIT(1), 0);
static SUNXI_CCU_GATE(mbus_ce_clk, "mbus-ce", "mbus",
		      0x804, BIT(2), 0);
static SUNXI_CCU_GATE(mbus_nand_clk, "mbus-nand", "mbus",
		      0x804, BIT(5), 0);
static SUNXI_CCU_GATE(mbus_csi_clk, "mbus-csi", "mbus",
		      0x804, BIT(8), 0);
static SUNXI_CCU_GATE(mbus_isp_clk, "mbus-isp", "mbus",
		      0x804, BIT(9), 0);
static SUNXI_CCU_GATE(mbus_g2d_clk, "mbus-g2d", "mbus",
		      0x804, BIT(10), 0);

static SUNXI_CCU_GATE(bus_dram_clk, "bus-dram", "psi-ahb1-ahb2",
		      0x80c, BIT(0), CLK_IS_CRITICAL);

static const char * const nand_spi_parents[] = { "dcxo24M",
						 "pll-periph0",
						 "pll-periph1",
						 "pll-periph0-2x",
						 "pll-periph1-2x" };
static SUNXI_CCU_MP_WITH_MUX_GATE(nand0_clk, "nand0", nand_spi_parents, 0x810,
				  0, 4,		/* M */
				  8, 2,		/* P */
				  24, 3,	/* mux */
				  BIT(31),	/* gate */
				  0);

static SUNXI_CCU_MP_WITH_MUX_GATE(nand1_clk, "nand1", nand_spi_parents, 0x814,
				  0, 4,		/* M */
				  8, 2,		/* P */
				  24, 3,	/* mux */
				  BIT(31),	/* gate */
				  0);

static SUNXI_CCU_GATE(bus_nand_clk, "bus-nand", "ahb3", 0x82c, BIT(0), 0);

static const char * const mmc_parents[] = { "dcxo24M", "pll-periph0-2x",
					    "pll-periph1-2x" };
static SUNXI_CCU_MP_WITH_MUX_GATE_POSTDIV(mmc0_clk, "mmc0", mmc_parents, 0x830,
					  0, 4,		/* M */
					  8, 2,		/* P */
					  24, 2,	/* mux */
					  BIT(31),	/* gate */
					  2,		/* post-div */
					  CLK_SET_RATE_NO_REPARENT);

static SUNXI_CCU_MP_WITH_MUX_GATE_POSTDIV(mmc1_clk, "mmc1", mmc_parents, 0x834,
					  0, 4,		/* M */
					  8, 2,		/* P */
					  24, 2,	/* mux */
					  BIT(31),	/* gate */
					  2,		/* post-div */
					  CLK_SET_RATE_NO_REPARENT);

static SUNXI_CCU_MP_WITH_MUX_GATE_POSTDIV(mmc2_clk, "mmc2", mmc_parents, 0x838,
					  0, 4,		/* M */
					  8, 2,		/* P */
					  24, 2,	/* mux */
					  BIT(31),	/* gate */
					  2,		/* post-div */
					  CLK_SET_RATE_NO_REPARENT);

static SUNXI_CCU_GATE(bus_mmc0_clk, "bus-mmc0", "ahb3", 0x84c, BIT(0), 0);
static SUNXI_CCU_GATE(bus_mmc1_clk, "bus-mmc1", "ahb3", 0x84c, BIT(1), 0);
static SUNXI_CCU_GATE(bus_mmc2_clk, "bus-mmc2", "ahb3", 0x84c, BIT(2), 0);

static SUNXI_CCU_GATE(bus_uart0_clk, "bus-uart0", "apb2", 0x90c, BIT(0), 0);
static SUNXI_CCU_GATE(bus_uart1_clk, "bus-uart1", "apb2", 0x90c, BIT(1), 0);
static SUNXI_CCU_GATE(bus_uart2_clk, "bus-uart2", "apb2", 0x90c, BIT(2), 0);
static SUNXI_CCU_GATE(bus_uart3_clk, "bus-uart3", "apb2", 0x90c, BIT(3), 0);
static SUNXI_CCU_GATE(bus_uart4_clk, "bus-uart4", "apb2", 0x90c, BIT(4), 0);

static SUNXI_CCU_GATE(bus_i2c0_clk, "bus-i2c0", "apb2", 0x91c, BIT(0), 0);
static SUNXI_CCU_GATE(bus_i2c1_clk, "bus-i2c1", "apb2", 0x91c, BIT(1), 0);
static SUNXI_CCU_GATE(bus_i2c2_clk, "bus-i2c2", "apb2", 0x91c, BIT(2), 0);
static SUNXI_CCU_GATE(bus_i2c3_clk, "bus-i2c3", "apb2", 0x91c, BIT(3), 0);

static SUNXI_CCU_MP_WITH_MUX_GATE(spi0_clk, "spi0", nand_spi_parents, 0x940,
				  0, 4,		/* M */
				  8, 2,		/* P */
				  24, 3,	/* mux */
				  BIT(31),	/* gate */
				  0);

static SUNXI_CCU_MP_WITH_MUX_GATE(spi1_clk, "spi1", nand_spi_parents, 0x944,
				  0, 4,		/* M */
				  8, 2,		/* P */
				  24, 3,	/* mux */
				  BIT(31),	/* gate */
				  0);

static SUNXI_CCU_MP_WITH_MUX_GATE(spi2_clk, "spi2", nand_spi_parents, 0x948,
				  0, 4,		/* M */
				  8, 2,		/* P */
				  24, 3,	/* mux */
				  BIT(31),	/* gate */
				  0);

static SUNXI_CCU_GATE(bus_spi0_clk, "bus-spi0", "ahb3", 0x96c, BIT(0), 0);
static SUNXI_CCU_GATE(bus_spi1_clk, "bus-spi1", "ahb3", 0x96c, BIT(1), 0);
static SUNXI_CCU_GATE(bus_spi2_clk, "bus-spi2", "ahb3", 0x96c, BIT(2), 0);

static SUNXI_CCU_GATE(emac_25m_clk, "emac-25m", "ahb3", 0x970,
		      BIT(31) | BIT(30), 0);

static SUNXI_CCU_GATE(bus_emac_clk, "bus-emac", "ahb3", 0x97c, BIT(0), 0);

static const char * const ir_parents[] = { "osc32k", "iosc",
					   "pll-periph0", "pll-periph1" };
static SUNXI_CCU_MP_WITH_MUX_GATE(ir_rx_clk, "ir-rx", ir_parents, 0x990,
				  0, 4,		/* M */
				  8, 2,		/* P */
				  24, 3,	/* mux */
				  BIT(31),	/* gate */
				  0);

static SUNXI_CCU_GATE(bus_ir_rx_clk, "bus-ir-rx", "ahb3", 0x99c, BIT(0), 0);

static SUNXI_CCU_MP_WITH_MUX_GATE(ir_tx_clk, "ir-tx", ir_parents, 0x9c0,
				  0, 4,		/* M */
				  8, 2,		/* P */
				  24, 3,	/* mux */
				  BIT(31),	/* gate */
				  0);

static SUNXI_CCU_GATE(bus_ir_tx_clk, "bus-ir-tx", "apb1", 0x9cc, BIT(0), 0);

static SUNXI_CCU_GATE(bus_gpadc_clk, "bus-gpadc", "apb1", 0x9ec, BIT(0), 0);

static SUNXI_CCU_GATE(bus_ths_clk, "bus-ths", "apb1", 0x9fc, BIT(0), 0);

static const char * const audio_parents[] = { "pll-audio", "pll-com-audio" };
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

static struct ccu_div i2s3_clk = {
	.enable		= BIT(31),
	.div		= _SUNXI_CCU_DIV_FLAGS(8, 2, CLK_DIVIDER_POWER_OF_TWO),
	.mux		= _SUNXI_CCU_MUX(24, 2),
	.common		= {
		.reg		= 0xa1c,
		.hw.init	= CLK_HW_INIT_PARENTS("i2s3",
						      audio_parents,
						      &ccu_div_ops,
						      CLK_SET_RATE_PARENT),
	},
};

static SUNXI_CCU_GATE(bus_i2s0_clk, "bus-i2s0", "apb1", 0xa20, BIT(0), 0);
static SUNXI_CCU_GATE(bus_i2s1_clk, "bus-i2s1", "apb1", 0xa20, BIT(1), 0);
static SUNXI_CCU_GATE(bus_i2s2_clk, "bus-i2s2", "apb1", 0xa20, BIT(2), 0);
static SUNXI_CCU_GATE(bus_i2s3_clk, "bus-i2s3", "apb1", 0xa20, BIT(3), 0);

static struct ccu_div spdif_clk = {
	.enable		= BIT(31),
	.div		= _SUNXI_CCU_DIV_FLAGS(8, 2, CLK_DIVIDER_POWER_OF_TWO),
	.mux		= _SUNXI_CCU_MUX(24, 2),
	.common		= {
		.reg		= 0xa24,
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

static SUNXI_CCU_M_WITH_MUX_GATE(audio_codec_dac_clk, "audio-codec-dac",
				 audio_parents, 0xa50,
				 0, 4,		/* M */
				 24, 2,		/* mux */
				 BIT(31),	/* gate */
				 0);

static SUNXI_CCU_M_WITH_MUX_GATE(audio_codec_adc_clk, "audio-codec-adc",
				 audio_parents, 0xa54,
				 0, 4,		/* M */
				 24, 2,		/* mux */
				 BIT(31),	/* gate */
				 0);

static SUNXI_CCU_M_WITH_MUX_GATE(audio_codec_4x_clk, "audio-codec-4x",
				 audio_parents, 0xa58,
				 0, 4,		/* M */
				 24, 2,		/* mux */
				 BIT(31),	/* gate */
				 0);

static SUNXI_CCU_GATE(bus_audio_codec_clk, "bus-audio-codec", "apb1", 0xa5c,
		      BIT(0), 0);

/*
 * There are OHCI 12M clock source selection bits for 2 USB 2.0 ports.
 * We will force them to 0 (12M divided from 48M).
 */
#define SUN50I_A100_USB0_CLK_REG		0xa70
#define SUN50I_A100_USB1_CLK_REG		0xa74

static SUNXI_CCU_GATE(usb_ohci0_clk, "usb-ohci0", "osc12M", 0xa70, BIT(31), 0);
static SUNXI_CCU_GATE(usb_phy0_clk, "usb-phy0", "dcxo24M", 0xa70, BIT(29), 0);

static SUNXI_CCU_GATE(usb_ohci1_clk, "usb-ohci1", "osc12M", 0xa74, BIT(31), 0);
static SUNXI_CCU_GATE(usb_phy1_clk, "usb-phy1", "dcxo24M", 0xa74, BIT(29), 0);

static SUNXI_CCU_GATE(bus_ohci0_clk, "bus-ohci0", "ahb3", 0xa8c, BIT(0), 0);
static SUNXI_CCU_GATE(bus_ohci1_clk, "bus-ohci1", "ahb3", 0xa8c, BIT(1), 0);
static SUNXI_CCU_GATE(bus_ehci0_clk, "bus-ehci0", "ahb3", 0xa8c, BIT(4), 0);
static SUNXI_CCU_GATE(bus_ehci1_clk, "bus-ehci1", "ahb3", 0xa8c, BIT(5), 0);
static SUNXI_CCU_GATE(bus_otg_clk, "bus-otg", "ahb3", 0xa8c, BIT(8), 0);

static SUNXI_CCU_GATE(bus_lradc_clk, "bus-lradc", "ahb3", 0xa9c, BIT(0), 0);

static SUNXI_CCU_GATE(bus_dpss_top0_clk, "bus-dpss-top0", "ahb3",
		      0xabc, BIT(0), 0);

static SUNXI_CCU_GATE(bus_dpss_top1_clk, "bus-dpss-top1", "ahb3",
		      0xacc, BIT(0), 0);

static const char * const mipi_dsi_parents[] = { "dcxo24M", "pll-periph0-2x",
						 "pll-periph0" };
static SUNXI_CCU_M_WITH_MUX_GATE(mipi_dsi_clk, "mipi-dsi",
				 mipi_dsi_parents,
				 0xb24,
				 0, 4,		/* M */
				 24, 2,		/* mux */
				 BIT(31),	/* gate */
				 0);

static SUNXI_CCU_GATE(bus_mipi_dsi_clk, "bus-mipi-dsi", "ahb3",
		      0xb4c, BIT(0), 0);

static const char * const tcon_lcd_parents[] = { "pll-video0-4x",
						  "pll-video1-4x",
						  "pll-video2-4x",
						  "pll-video3-4x",
						  "pll-periph0-2x" };
static SUNXI_CCU_MP_WITH_MUX_GATE(tcon_lcd_clk, "tcon-lcd0",
				  tcon_lcd_parents, 0xb60,
				  0, 4,		/* M */
				  8, 2,		/* P */
				  24, 3,	/* mux */
				  BIT(31),	/* gate */
				  0);

static SUNXI_CCU_GATE(bus_tcon_lcd_clk, "bus-tcon-lcd0", "ahb3",
		      0xb7c, BIT(0), 0);

static const char * const ledc_parents[] = { "dcxo24M",
					     "pll-periph0" };
static SUNXI_CCU_MP_WITH_MUX_GATE(ledc_clk, "ledc",
				  ledc_parents, 0xbf0,
				  0, 4,		/* M */
				  8, 2,		/* P */
				  24, 3,	/* mux */
				  BIT(31),	/* gate */
				  0);

static SUNXI_CCU_GATE(bus_ledc_clk, "bus-ledc", "ahb3", 0xbfc, BIT(0), 0);

static const char * const csi_top_parents[] = { "pll-periph0-2x",
						"pll-video0-2x",
						"pll-video1-2x",
						"pll-video2-2x",
						"pll-video3-2x" };
static SUNXI_CCU_M_WITH_MUX_GATE(csi_top_clk, "csi-top",
				 csi_top_parents, 0xc04,
				 0, 4,		/* M */
				 24, 3,		/* mux */
				 BIT(31),	/* gate */
				 0);

static const char * const csi0_mclk_parents[] = { "dcxo24M", "pll-video2",
						  "pll-video3", "pll-video0",
						  "pll-video1" };
static SUNXI_CCU_M_WITH_MUX_GATE(csi0_mclk_clk, "csi0-mclk",
				 csi0_mclk_parents, 0xc08,
				 0, 5,		/* M */
				 24, 3,		/* mux */
				 BIT(31),	/* gate */
				 0);

static const char * const csi1_mclk_parents[] = { "dcxo24M", "pll-video3",
						  "pll-video0", "pll-video1",
						  "pll-video2" };
static SUNXI_CCU_M_WITH_MUX_GATE(csi1_mclk_clk, "csi1-mclk",
				 csi1_mclk_parents, 0xc0c,
				 0, 5,		/* M */
				 24, 3,		/* mux */
				 BIT(31),	/* gate */
				 0);

static SUNXI_CCU_GATE(bus_csi_clk, "bus-csi", "ahb3", 0xc1c, BIT(0), 0);

static const char * const csi_isp_parents[] = { "pll-periph0-2x",
						"pll-video0-2x",
						"pll-video1-2x",
						"pll-video2-2x",
						"pll-video3-2x" };
static SUNXI_CCU_M_WITH_MUX_GATE(csi_isp_clk, "csi-isp",
				 csi_isp_parents, 0xc20,
				 0, 5,		/* M */
				 24, 3,		/* mux */
				 BIT(31),	/* gate */
				 0);

/* Fixed factor clocks */
static CLK_FIXED_FACTOR_FW_NAME(osc12M_clk, "osc12M", "hosc", 2, 1, 0);

static CLK_FIXED_FACTOR_HW(pll_com_audio_clk, "pll-com-audio",
			   &pll_com_clk.common.hw,
			   5, 1, CLK_SET_RATE_PARENT);

static CLK_FIXED_FACTOR_HW(pll_periph0_2x_clk, "pll-periph0-2x",
			   &pll_periph0_clk.common.hw,
			   1, 2, 0);

static CLK_FIXED_FACTOR_HW(pll_periph1_2x_clk, "pll-periph1-2x",
			   &pll_periph1_clk.common.hw,
			   1, 2, 0);

static const struct clk_hw *pll_video0_parents[] = {
	&pll_video0_clk.common.hw
};
static CLK_FIXED_FACTOR_HWS(pll_video0_4x_clk, "pll-video0-4x",
			    pll_video0_parents,
			    1, 4, CLK_SET_RATE_PARENT);
static CLK_FIXED_FACTOR_HWS(pll_video0_2x_clk, "pll-video0-2x",
			    pll_video0_parents,
			    1, 2, CLK_SET_RATE_PARENT);

static const struct clk_hw *pll_video1_parents[] = {
	&pll_video1_clk.common.hw
};
static CLK_FIXED_FACTOR_HWS(pll_video1_4x_clk, "pll-video1-4x",
			    pll_video1_parents,
			    1, 4, CLK_SET_RATE_PARENT);
static CLK_FIXED_FACTOR_HWS(pll_video1_2x_clk, "pll-video1-2x",
			    pll_video1_parents,
			    1, 2, CLK_SET_RATE_PARENT);

static const struct clk_hw *pll_video2_parents[] = {
	&pll_video2_clk.common.hw
};
static CLK_FIXED_FACTOR_HWS(pll_video2_4x_clk, "pll-video2-4x",
			    pll_video2_parents,
			    1, 4, CLK_SET_RATE_PARENT);
static CLK_FIXED_FACTOR_HWS(pll_video2_2x_clk, "pll-video2-2x",
			    pll_video2_parents,
			    1, 2, CLK_SET_RATE_PARENT);

static const struct clk_hw *pll_video3_parents[] = {
	&pll_video3_clk.common.hw
};
static CLK_FIXED_FACTOR_HWS(pll_video3_4x_clk, "pll-video3-4x",
			    pll_video3_parents,
			    1, 4, CLK_SET_RATE_PARENT);
static CLK_FIXED_FACTOR_HWS(pll_video3_2x_clk, "pll-video3-2x",
			    pll_video3_parents,
			    1, 2, CLK_SET_RATE_PARENT);

static struct ccu_common *sun50i_a100_ccu_clks[] = {
	&pll_cpux_clk.common,
	&pll_ddr0_clk.common,
	&pll_periph0_clk.common,
	&pll_periph1_clk.common,
	&pll_gpu_clk.common,
	&pll_video0_clk.common,
	&pll_video1_clk.common,
	&pll_video2_clk.common,
	&pll_video3_clk.common,
	&pll_ve_clk.common,
	&pll_com_clk.common,
	&pll_audio_clk.common,
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
	&g2d_clk.common,
	&bus_g2d_clk.common,
	&gpu_clk.common,
	&bus_gpu_clk.common,
	&ce_clk.common,
	&bus_ce_clk.common,
	&ve_clk.common,
	&bus_ve_clk.common,
	&bus_dma_clk.common,
	&bus_msgbox_clk.common,
	&bus_spinlock_clk.common,
	&bus_hstimer_clk.common,
	&avs_clk.common,
	&bus_dbg_clk.common,
	&bus_psi_clk.common,
	&bus_pwm_clk.common,
	&bus_iommu_clk.common,
	&mbus_dma_clk.common,
	&mbus_ve_clk.common,
	&mbus_ce_clk.common,
	&mbus_nand_clk.common,
	&mbus_csi_clk.common,
	&mbus_isp_clk.common,
	&mbus_g2d_clk.common,
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
	&bus_uart4_clk.common,
	&bus_i2c0_clk.common,
	&bus_i2c1_clk.common,
	&bus_i2c2_clk.common,
	&bus_i2c3_clk.common,
	&spi0_clk.common,
	&spi1_clk.common,
	&spi2_clk.common,
	&bus_spi0_clk.common,
	&bus_spi1_clk.common,
	&bus_spi2_clk.common,
	&emac_25m_clk.common,
	&bus_emac_clk.common,
	&ir_rx_clk.common,
	&bus_ir_rx_clk.common,
	&ir_tx_clk.common,
	&bus_ir_tx_clk.common,
	&bus_gpadc_clk.common,
	&bus_ths_clk.common,
	&i2s0_clk.common,
	&i2s1_clk.common,
	&i2s2_clk.common,
	&i2s3_clk.common,
	&bus_i2s0_clk.common,
	&bus_i2s1_clk.common,
	&bus_i2s2_clk.common,
	&bus_i2s3_clk.common,
	&spdif_clk.common,
	&bus_spdif_clk.common,
	&dmic_clk.common,
	&bus_dmic_clk.common,
	&audio_codec_dac_clk.common,
	&audio_codec_adc_clk.common,
	&audio_codec_4x_clk.common,
	&bus_audio_codec_clk.common,
	&usb_ohci0_clk.common,
	&usb_phy0_clk.common,
	&usb_ohci1_clk.common,
	&usb_phy1_clk.common,
	&bus_ohci0_clk.common,
	&bus_ohci1_clk.common,
	&bus_ehci0_clk.common,
	&bus_ehci1_clk.common,
	&bus_otg_clk.common,
	&bus_lradc_clk.common,
	&bus_dpss_top0_clk.common,
	&bus_dpss_top1_clk.common,
	&mipi_dsi_clk.common,
	&bus_mipi_dsi_clk.common,
	&tcon_lcd_clk.common,
	&bus_tcon_lcd_clk.common,
	&ledc_clk.common,
	&bus_ledc_clk.common,
	&csi_top_clk.common,
	&csi0_mclk_clk.common,
	&csi1_mclk_clk.common,
	&bus_csi_clk.common,
	&csi_isp_clk.common,
};

static struct clk_hw_onecell_data sun50i_a100_hw_clks = {
	.hws	= {
		[CLK_OSC12M]		= &osc12M_clk.hw,
		[CLK_PLL_CPUX]		= &pll_cpux_clk.common.hw,
		[CLK_PLL_DDR0]		= &pll_ddr0_clk.common.hw,
		[CLK_PLL_PERIPH0]	= &pll_periph0_clk.common.hw,
		[CLK_PLL_PERIPH0_2X]	= &pll_periph0_2x_clk.hw,
		[CLK_PLL_PERIPH1]	= &pll_periph1_clk.common.hw,
		[CLK_PLL_PERIPH1_2X]	= &pll_periph1_2x_clk.hw,
		[CLK_PLL_GPU]		= &pll_gpu_clk.common.hw,
		[CLK_PLL_VIDEO0]	= &pll_video0_clk.common.hw,
		[CLK_PLL_VIDEO0_2X]	= &pll_video0_2x_clk.hw,
		[CLK_PLL_VIDEO0_4X]	= &pll_video0_4x_clk.hw,
		[CLK_PLL_VIDEO1]	= &pll_video1_clk.common.hw,
		[CLK_PLL_VIDEO1_2X]	= &pll_video1_2x_clk.hw,
		[CLK_PLL_VIDEO1_4X]	= &pll_video1_4x_clk.hw,
		[CLK_PLL_VIDEO2]	= &pll_video2_clk.common.hw,
		[CLK_PLL_VIDEO2_2X]	= &pll_video2_2x_clk.hw,
		[CLK_PLL_VIDEO2_4X]	= &pll_video2_4x_clk.hw,
		[CLK_PLL_VIDEO3]	= &pll_video3_clk.common.hw,
		[CLK_PLL_VIDEO3_2X]	= &pll_video3_2x_clk.hw,
		[CLK_PLL_VIDEO3_4X]	= &pll_video3_4x_clk.hw,
		[CLK_PLL_VE]		= &pll_ve_clk.common.hw,
		[CLK_PLL_COM]		= &pll_com_clk.common.hw,
		[CLK_PLL_COM_AUDIO]	= &pll_com_audio_clk.hw,
		[CLK_PLL_AUDIO]		= &pll_audio_clk.common.hw,
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
		[CLK_G2D]		= &g2d_clk.common.hw,
		[CLK_BUS_G2D]		= &bus_g2d_clk.common.hw,
		[CLK_GPU]		= &gpu_clk.common.hw,
		[CLK_BUS_GPU]		= &bus_gpu_clk.common.hw,
		[CLK_CE]		= &ce_clk.common.hw,
		[CLK_BUS_CE]		= &bus_ce_clk.common.hw,
		[CLK_VE]		= &ve_clk.common.hw,
		[CLK_BUS_VE]		= &bus_ve_clk.common.hw,
		[CLK_BUS_DMA]		= &bus_dma_clk.common.hw,
		[CLK_BUS_MSGBOX]	= &bus_msgbox_clk.common.hw,
		[CLK_BUS_SPINLOCK]	= &bus_spinlock_clk.common.hw,
		[CLK_BUS_HSTIMER]	= &bus_hstimer_clk.common.hw,
		[CLK_AVS]		= &avs_clk.common.hw,
		[CLK_BUS_DBG]		= &bus_dbg_clk.common.hw,
		[CLK_BUS_PSI]		= &bus_psi_clk.common.hw,
		[CLK_BUS_PWM]		= &bus_pwm_clk.common.hw,
		[CLK_BUS_IOMMU]		= &bus_iommu_clk.common.hw,
		[CLK_MBUS_DMA]		= &mbus_dma_clk.common.hw,
		[CLK_MBUS_VE]		= &mbus_ve_clk.common.hw,
		[CLK_MBUS_CE]		= &mbus_ce_clk.common.hw,
		[CLK_MBUS_NAND]		= &mbus_nand_clk.common.hw,
		[CLK_MBUS_CSI]		= &mbus_csi_clk.common.hw,
		[CLK_MBUS_ISP]		= &mbus_isp_clk.common.hw,
		[CLK_MBUS_G2D]		= &mbus_g2d_clk.common.hw,
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
		[CLK_BUS_UART4]		= &bus_uart4_clk.common.hw,
		[CLK_BUS_I2C0]		= &bus_i2c0_clk.common.hw,
		[CLK_BUS_I2C1]		= &bus_i2c1_clk.common.hw,
		[CLK_BUS_I2C2]		= &bus_i2c2_clk.common.hw,
		[CLK_BUS_I2C3]		= &bus_i2c3_clk.common.hw,
		[CLK_SPI0]		= &spi0_clk.common.hw,
		[CLK_SPI1]		= &spi1_clk.common.hw,
		[CLK_SPI2]		= &spi2_clk.common.hw,
		[CLK_BUS_SPI0]		= &bus_spi0_clk.common.hw,
		[CLK_BUS_SPI1]		= &bus_spi1_clk.common.hw,
		[CLK_BUS_SPI2]		= &bus_spi2_clk.common.hw,
		[CLK_EMAC_25M]		= &emac_25m_clk.common.hw,
		[CLK_BUS_EMAC]		= &bus_emac_clk.common.hw,
		[CLK_IR_RX]		= &ir_rx_clk.common.hw,
		[CLK_BUS_IR_RX]		= &bus_ir_rx_clk.common.hw,
		[CLK_IR_TX]		= &ir_tx_clk.common.hw,
		[CLK_BUS_IR_TX]		= &bus_ir_tx_clk.common.hw,
		[CLK_BUS_GPADC]		= &bus_gpadc_clk.common.hw,
		[CLK_BUS_THS]		= &bus_ths_clk.common.hw,
		[CLK_I2S0]		= &i2s0_clk.common.hw,
		[CLK_I2S1]		= &i2s1_clk.common.hw,
		[CLK_I2S2]		= &i2s2_clk.common.hw,
		[CLK_I2S3]		= &i2s3_clk.common.hw,
		[CLK_BUS_I2S0]		= &bus_i2s0_clk.common.hw,
		[CLK_BUS_I2S1]		= &bus_i2s1_clk.common.hw,
		[CLK_BUS_I2S2]		= &bus_i2s2_clk.common.hw,
		[CLK_BUS_I2S3]		= &bus_i2s3_clk.common.hw,
		[CLK_SPDIF]		= &spdif_clk.common.hw,
		[CLK_BUS_SPDIF]		= &bus_spdif_clk.common.hw,
		[CLK_DMIC]		= &dmic_clk.common.hw,
		[CLK_BUS_DMIC]		= &bus_dmic_clk.common.hw,
		[CLK_AUDIO_DAC]		= &audio_codec_dac_clk.common.hw,
		[CLK_AUDIO_ADC]		= &audio_codec_adc_clk.common.hw,
		[CLK_AUDIO_4X]		= &audio_codec_4x_clk.common.hw,
		[CLK_BUS_AUDIO_CODEC]	= &bus_audio_codec_clk.common.hw,
		[CLK_USB_OHCI0]		= &usb_ohci0_clk.common.hw,
		[CLK_USB_PHY0]		= &usb_phy0_clk.common.hw,
		[CLK_USB_OHCI1]		= &usb_ohci1_clk.common.hw,
		[CLK_USB_PHY1]		= &usb_phy1_clk.common.hw,
		[CLK_BUS_OHCI0]		= &bus_ohci0_clk.common.hw,
		[CLK_BUS_OHCI1]		= &bus_ohci1_clk.common.hw,
		[CLK_BUS_EHCI0]		= &bus_ehci0_clk.common.hw,
		[CLK_BUS_EHCI1]		= &bus_ehci1_clk.common.hw,
		[CLK_BUS_OTG]		= &bus_otg_clk.common.hw,
		[CLK_BUS_LRADC]		= &bus_lradc_clk.common.hw,
		[CLK_BUS_DPSS_TOP0]	= &bus_dpss_top0_clk.common.hw,
		[CLK_BUS_DPSS_TOP1]	= &bus_dpss_top1_clk.common.hw,
		[CLK_MIPI_DSI]		= &mipi_dsi_clk.common.hw,
		[CLK_BUS_MIPI_DSI]	= &bus_mipi_dsi_clk.common.hw,
		[CLK_TCON_LCD]		= &tcon_lcd_clk.common.hw,
		[CLK_BUS_TCON_LCD]	= &bus_tcon_lcd_clk.common.hw,
		[CLK_LEDC]		= &ledc_clk.common.hw,
		[CLK_BUS_LEDC]		= &bus_ledc_clk.common.hw,
		[CLK_CSI_TOP]		= &csi_top_clk.common.hw,
		[CLK_CSI0_MCLK]		= &csi0_mclk_clk.common.hw,
		[CLK_CSI1_MCLK]		= &csi1_mclk_clk.common.hw,
		[CLK_BUS_CSI]		= &bus_csi_clk.common.hw,
		[CLK_CSI_ISP]		= &csi_isp_clk.common.hw,
	},
	.num = CLK_NUMBER,
};

static struct ccu_reset_map sun50i_a100_ccu_resets[] = {
	[RST_MBUS]		= { 0x540, BIT(30) },

	[RST_BUS_DE]		= { 0x60c, BIT(16) },
	[RST_BUS_G2D]		= { 0x63c, BIT(16) },
	[RST_BUS_GPU]		= { 0x67c, BIT(16) },
	[RST_BUS_CE]		= { 0x68c, BIT(16) },
	[RST_BUS_VE]		= { 0x69c, BIT(16) },
	[RST_BUS_DMA]		= { 0x70c, BIT(16) },
	[RST_BUS_MSGBOX]	= { 0x71c, BIT(16) },
	[RST_BUS_SPINLOCK]	= { 0x72c, BIT(16) },
	[RST_BUS_HSTIMER]	= { 0x73c, BIT(16) },
	[RST_BUS_DBG]		= { 0x78c, BIT(16) },
	[RST_BUS_PSI]		= { 0x79c, BIT(16) },
	[RST_BUS_PWM]		= { 0x7ac, BIT(16) },
	[RST_BUS_DRAM]		= { 0x80c, BIT(16) },
	[RST_BUS_NAND]		= { 0x82c, BIT(16) },
	[RST_BUS_MMC0]		= { 0x84c, BIT(16) },
	[RST_BUS_MMC1]		= { 0x84c, BIT(17) },
	[RST_BUS_MMC2]		= { 0x84c, BIT(18) },
	[RST_BUS_UART0]		= { 0x90c, BIT(16) },
	[RST_BUS_UART1]		= { 0x90c, BIT(17) },
	[RST_BUS_UART2]		= { 0x90c, BIT(18) },
	[RST_BUS_UART3]		= { 0x90c, BIT(19) },
	[RST_BUS_UART4]		= { 0x90c, BIT(20) },
	[RST_BUS_I2C0]		= { 0x91c, BIT(16) },
	[RST_BUS_I2C1]		= { 0x91c, BIT(17) },
	[RST_BUS_I2C2]		= { 0x91c, BIT(18) },
	[RST_BUS_I2C3]		= { 0x91c, BIT(19) },
	[RST_BUS_SPI0]		= { 0x96c, BIT(16) },
	[RST_BUS_SPI1]		= { 0x96c, BIT(17) },
	[RST_BUS_SPI2]		= { 0x96c, BIT(18) },
	[RST_BUS_EMAC]		= { 0x97c, BIT(16) },
	[RST_BUS_IR_RX]		= { 0x99c, BIT(16) },
	[RST_BUS_IR_TX]		= { 0x9cc, BIT(16) },
	[RST_BUS_GPADC]		= { 0x9ec, BIT(16) },
	[RST_BUS_THS]		= { 0x9fc, BIT(16) },
	[RST_BUS_I2S0]		= { 0xa20, BIT(16) },
	[RST_BUS_I2S1]		= { 0xa20, BIT(17) },
	[RST_BUS_I2S2]		= { 0xa20, BIT(18) },
	[RST_BUS_I2S3]		= { 0xa20, BIT(19) },
	[RST_BUS_SPDIF]		= { 0xa2c, BIT(16) },
	[RST_BUS_DMIC]		= { 0xa4c, BIT(16) },
	[RST_BUS_AUDIO_CODEC]	= { 0xa5c, BIT(16) },

	[RST_USB_PHY0]		= { 0xa70, BIT(30) },
	[RST_USB_PHY1]		= { 0xa74, BIT(30) },

	[RST_BUS_OHCI0]		= { 0xa8c, BIT(16) },
	[RST_BUS_OHCI1]		= { 0xa8c, BIT(17) },
	[RST_BUS_EHCI0]		= { 0xa8c, BIT(20) },
	[RST_BUS_EHCI1]		= { 0xa8c, BIT(21) },
	[RST_BUS_OTG]		= { 0xa8c, BIT(24) },

	[RST_BUS_LRADC]		= { 0xa9c, BIT(16) },
	[RST_BUS_DPSS_TOP0]	= { 0xabc, BIT(16) },
	[RST_BUS_DPSS_TOP1]	= { 0xacc, BIT(16) },
	[RST_BUS_MIPI_DSI]	= { 0xb4c, BIT(16) },
	[RST_BUS_TCON_LCD]	= { 0xb7c, BIT(16) },
	[RST_BUS_LVDS]		= { 0xbac, BIT(16) },
	[RST_BUS_LEDC]		= { 0xbfc, BIT(16) },
	[RST_BUS_CSI]		= { 0xc1c, BIT(16) },
	[RST_BUS_CSI_ISP]	= { 0xc2c, BIT(16) },
};

static const struct sunxi_ccu_desc sun50i_a100_ccu_desc = {
	.ccu_clks	= sun50i_a100_ccu_clks,
	.num_ccu_clks	= ARRAY_SIZE(sun50i_a100_ccu_clks),

	.hw_clks	= &sun50i_a100_hw_clks,

	.resets		= sun50i_a100_ccu_resets,
	.num_resets	= ARRAY_SIZE(sun50i_a100_ccu_resets),
};

static const u32 sun50i_a100_pll_regs[] = {
	SUN50I_A100_PLL_CPUX_REG,
	SUN50I_A100_PLL_DDR0_REG,
	SUN50I_A100_PLL_PERIPH0_REG,
	SUN50I_A100_PLL_PERIPH1_REG,
	SUN50I_A100_PLL_GPU_REG,
	SUN50I_A100_PLL_VIDEO0_REG,
	SUN50I_A100_PLL_VIDEO1_REG,
	SUN50I_A100_PLL_VIDEO2_REG,
	SUN50I_A100_PLL_VIDEO3_REG,
	SUN50I_A100_PLL_VE_REG,
	SUN50I_A100_PLL_COM_REG,
	SUN50I_A100_PLL_AUDIO_REG,
};

static const u32 sun50i_a100_pll_video_regs[] = {
	SUN50I_A100_PLL_VIDEO0_REG,
	SUN50I_A100_PLL_VIDEO1_REG,
	SUN50I_A100_PLL_VIDEO2_REG,
	SUN50I_A100_PLL_VIDEO3_REG,
};

static const u32 sun50i_a100_usb2_clk_regs[] = {
	SUN50I_A100_USB0_CLK_REG,
	SUN50I_A100_USB1_CLK_REG,
};

static struct ccu_pll_nb sun50i_a100_pll_cpu_nb = {
	.common = &pll_cpux_clk.common,
	/* copy from pll_cpux_clk */
	.enable = BIT(27),
	.lock   = BIT(28),
};

static struct ccu_mux_nb sun50i_a100_cpu_nb = {
	.common         = &cpux_clk.common,
	.cm             = &cpux_clk.mux,
	.delay_us       = 1,
	.bypass_index   = 4, /* index of pll periph0 */
};

static int sun50i_a100_ccu_probe(struct platform_device *pdev)
{
	void __iomem *reg;
	u32 val;
	int i, ret;

	reg = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(reg))
		return PTR_ERR(reg);

	/*
	 * Enable lock and enable bits on all PLLs.
	 *
	 * Due to the current design, multiple PLLs share one power switch,
	 * so switching PLL is easy to cause stability problems.
	 * When initializing, we enable them by default. When disable,
	 * we only turn off the output of PLL.
	 */
	for (i = 0; i < ARRAY_SIZE(sun50i_a100_pll_regs); i++) {
		val = readl(reg + sun50i_a100_pll_regs[i]);
		val |= SUN50I_A100_PLL_LOCK_ENABLE | SUN50I_A100_PLL_ENABLE;
		writel(val, reg + sun50i_a100_pll_regs[i]);
	}

	/*
	 * In order to pass the EMI certification, the SDM function of
	 * the peripheral 1 bus is enabled, and the frequency is still
	 * calculated using the previous division factor.
	 */
	writel(SUN50I_A100_PLL_PERIPH1_PATTERN0,
	       reg + SUN50I_A100_PLL_PERIPH1_PATTERN0_REG);

	val = readl(reg + SUN50I_A100_PLL_PERIPH1_REG);
	val |= SUN50I_A100_PLL_SDM_ENABLE;
	writel(val, reg + SUN50I_A100_PLL_PERIPH1_REG);

	/*
	 * Force the output divider of video PLLs to 0.
	 *
	 * See the comment before pll-video0 definition for the reason.
	 */
	for (i = 0; i < ARRAY_SIZE(sun50i_a100_pll_video_regs); i++) {
		val = readl(reg + sun50i_a100_pll_video_regs[i]);
		val &= ~BIT(0);
		writel(val, reg + sun50i_a100_pll_video_regs[i]);
	}

	/*
	 * Enforce m1 = 0, m0 = 1 for Audio PLL
	 *
	 * See the comment before pll-audio definition for the reason.
	 */
	val = readl(reg + SUN50I_A100_PLL_AUDIO_REG);
	val &= ~BIT(1);
	val |= BIT(0);
	writel(val, reg + SUN50I_A100_PLL_AUDIO_REG);

	/*
	 * Force OHCI 12M clock sources to 00 (12MHz divided from 48MHz)
	 *
	 * This clock mux is still mysterious, and the code just enforces
	 * it to have a valid clock parent.
	 */
	for (i = 0; i < ARRAY_SIZE(sun50i_a100_usb2_clk_regs); i++) {
		val = readl(reg + sun50i_a100_usb2_clk_regs[i]);
		val &= ~GENMASK(25, 24);
		writel(val, reg + sun50i_a100_usb2_clk_regs[i]);
	}

	ret = devm_sunxi_ccu_probe(&pdev->dev, reg, &sun50i_a100_ccu_desc);
	if (ret)
		return ret;

	/* Gate then ungate PLL CPU after any rate changes */
	ccu_pll_notifier_register(&sun50i_a100_pll_cpu_nb);

	/* Reparent CPU during PLL CPU rate changes */
	ccu_mux_notifier_register(pll_cpux_clk.common.hw.clk,
				  &sun50i_a100_cpu_nb);

	return 0;
}

static const struct of_device_id sun50i_a100_ccu_ids[] = {
	{ .compatible = "allwinner,sun50i-a100-ccu" },
	{ }
};
MODULE_DEVICE_TABLE(of, sun50i_a100_ccu_ids);

static struct platform_driver sun50i_a100_ccu_driver = {
	.probe	= sun50i_a100_ccu_probe,
	.driver	= {
		.name	= "sun50i-a100-ccu",
		.suppress_bind_attrs = true,
		.of_match_table	= sun50i_a100_ccu_ids,
	},
};
module_platform_driver(sun50i_a100_ccu_driver);

MODULE_IMPORT_NS(SUNXI_CCU);
MODULE_LICENSE("GPL");
