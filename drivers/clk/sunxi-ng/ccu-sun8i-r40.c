// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017 Icenowy Zheng <icenowy@aosc.io>
 */

#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

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
#include "ccu_phase.h"

#include "ccu-sun8i-r40.h"

/* TODO: The result of N*K is required to be in [10, 88] range. */
static struct ccu_nkmp pll_cpu_clk = {
	.enable		= BIT(31),
	.lock		= BIT(28),
	.n		= _SUNXI_CCU_MULT(8, 5),
	.k		= _SUNXI_CCU_MULT(4, 2),
	.m		= _SUNXI_CCU_DIV(0, 2),
	.p		= _SUNXI_CCU_DIV_MAX(16, 2, 4),
	.common		= {
		.reg		= 0x000,
		.hw.init	= CLK_HW_INIT("pll-cpu",
					      "osc24M",
					      &ccu_nkmp_ops,
					      CLK_SET_RATE_UNGATE),
	},
};

/*
 * The Audio PLL is supposed to have 4 outputs: 3 fixed factors from
 * the base (2x, 4x and 8x), and one variable divider (the one true
 * pll audio).
 *
 * We don't have any need for the variable divider for now, so we just
 * hardcode it to match with the clock names
 */
#define SUN8I_R40_PLL_AUDIO_REG	0x008

static SUNXI_CCU_NM_WITH_GATE_LOCK(pll_audio_base_clk, "pll-audio-base",
				   "osc24M", 0x008,
				   8, 7,	/* N */
				   0, 5,	/* M */
				   BIT(31),	/* gate */
				   BIT(28),	/* lock */
				   CLK_SET_RATE_UNGATE);

static SUNXI_CCU_NM_WITH_FRAC_GATE_LOCK_MIN_MAX(pll_video0_clk, "pll-video0",
						"osc24M", 0x0010,
						192000000,  /* Minimum rate */
						1008000000, /* Maximum rate */
						8, 7,       /* N */
						0, 4,       /* M */
						BIT(24),    /* frac enable */
						BIT(25),    /* frac select */
						270000000,  /* frac rate 0 */
						297000000,  /* frac rate 1 */
						BIT(31),    /* gate */
						BIT(28),    /* lock */
						CLK_SET_RATE_UNGATE);

/* TODO: The result of N/M is required to be in [8, 25] range. */
static SUNXI_CCU_NM_WITH_FRAC_GATE_LOCK(pll_ve_clk, "pll-ve",
					"osc24M", 0x0018,
					8, 7,		/* N */
					0, 4,		/* M */
					BIT(24),	/* frac enable */
					BIT(25),	/* frac select */
					270000000,	/* frac rate 0 */
					297000000,	/* frac rate 1 */
					BIT(31),	/* gate */
					BIT(28),	/* lock */
					CLK_SET_RATE_UNGATE);

/* TODO: The result of N*K is required to be in [10, 77] range. */
static SUNXI_CCU_NKM_WITH_GATE_LOCK(pll_ddr0_clk, "pll-ddr0",
				    "osc24M", 0x020,
				    8, 5,	/* N */
				    4, 2,	/* K */
				    0, 2,	/* M */
				    BIT(31),	/* gate */
				    BIT(28),	/* lock */
				    CLK_SET_RATE_UNGATE);

/* TODO: The result of N*K is required to be in [21, 58] range. */
static struct ccu_nk pll_periph0_clk = {
	.enable		= BIT(31),
	.lock		= BIT(28),
	.n		= _SUNXI_CCU_MULT(8, 5),
	.k		= _SUNXI_CCU_MULT(4, 2),
	.fixed_post_div	= 2,
	.common		= {
		.reg		= 0x028,
		.features	= CCU_FEATURE_FIXED_POSTDIV,
		.hw.init	= CLK_HW_INIT("pll-periph0", "osc24M",
					      &ccu_nk_ops,
					      CLK_SET_RATE_UNGATE),
	},
};

static struct ccu_div pll_periph0_sata_clk = {
	.enable		= BIT(24),
	.div		= _SUNXI_CCU_DIV(0, 2),
	/*
	 * The formula of pll-periph0 (1x) is 24MHz*N*K/2, and the formula
	 * of pll-periph0-sata is 24MHz*N*K/M/6, so the postdiv here is
	 * 6/2 = 3.
	 */
	.fixed_post_div	= 3,
	.common		= {
		.reg		= 0x028,
		.features	= CCU_FEATURE_FIXED_POSTDIV,
		.hw.init	= CLK_HW_INIT("pll-periph0-sata",
					      "pll-periph0",
					      &ccu_div_ops, 0),
	},
};

/* TODO: The result of N*K is required to be in [21, 58] range. */
static struct ccu_nk pll_periph1_clk = {
	.enable		= BIT(31),
	.lock		= BIT(28),
	.n		= _SUNXI_CCU_MULT(8, 5),
	.k		= _SUNXI_CCU_MULT(4, 2),
	.fixed_post_div	= 2,
	.common		= {
		.reg		= 0x02c,
		.features	= CCU_FEATURE_FIXED_POSTDIV,
		.hw.init	= CLK_HW_INIT("pll-periph1", "osc24M",
					      &ccu_nk_ops,
					      CLK_SET_RATE_UNGATE),
	},
};

static SUNXI_CCU_NM_WITH_FRAC_GATE_LOCK_MIN_MAX(pll_video1_clk, "pll-video1",
						"osc24M", 0x030,
						192000000,  /* Minimum rate */
						1008000000, /* Maximum rate */
						8, 7,       /* N */
						0, 4,       /* M */
						BIT(24),    /* frac enable */
						BIT(25),    /* frac select */
						270000000,  /* frac rate 0 */
						297000000,  /* frac rate 1 */
						BIT(31),    /* gate */
						BIT(28),    /* lock */
						CLK_SET_RATE_UNGATE);

static struct ccu_nkm pll_sata_clk = {
	.enable		= BIT(31),
	.lock		= BIT(28),
	.n		= _SUNXI_CCU_MULT(8, 5),
	.k		= _SUNXI_CCU_MULT(4, 2),
	.m		= _SUNXI_CCU_DIV(0, 2),
	.fixed_post_div	= 6,
	.common		= {
		.reg		= 0x034,
		.features	= CCU_FEATURE_FIXED_POSTDIV,
		.hw.init	= CLK_HW_INIT("pll-sata", "osc24M",
					      &ccu_nkm_ops,
					      CLK_SET_RATE_UNGATE),
	},
};

static const char * const pll_sata_out_parents[] = { "pll-sata",
						     "pll-periph0-sata" };
static SUNXI_CCU_MUX_WITH_GATE(pll_sata_out_clk, "pll-sata-out",
			       pll_sata_out_parents, 0x034,
			       30, 1,	/* mux */
			       BIT(14),	/* gate */
			       CLK_SET_RATE_PARENT);

/* TODO: The result of N/M is required to be in [8, 25] range. */
static SUNXI_CCU_NM_WITH_FRAC_GATE_LOCK(pll_gpu_clk, "pll-gpu",
					"osc24M", 0x038,
					8, 7,		/* N */
					0, 4,		/* M */
					BIT(24),	/* frac enable */
					BIT(25),	/* frac select */
					270000000,	/* frac rate 0 */
					297000000,	/* frac rate 1 */
					BIT(31),	/* gate */
					BIT(28),	/* lock */
					CLK_SET_RATE_UNGATE);

/*
 * The MIPI PLL has 2 modes: "MIPI" and "HDMI".
 *
 * The MIPI mode is a standard NKM-style clock. The HDMI mode is an
 * integer / fractional clock with switchable multipliers and dividers.
 * This is not supported here. We hardcode the PLL to MIPI mode.
 *
 * TODO: In the MIPI mode, M/N is required to be equal or lesser than 3,
 * which cannot be implemented now.
 */
#define SUN8I_R40_PLL_MIPI_REG	0x040

static const char * const pll_mipi_parents[] = { "pll-video0" };
static struct ccu_nkm pll_mipi_clk = {
	.enable	= BIT(31) | BIT(23) | BIT(22),
	.lock	= BIT(28),
	.n	= _SUNXI_CCU_MULT(8, 4),
	.k	= _SUNXI_CCU_MULT_MIN(4, 2, 2),
	.m	= _SUNXI_CCU_DIV(0, 4),
	.mux	= _SUNXI_CCU_MUX(21, 1),
	.common	= {
		.reg		= 0x040,
		.hw.init	= CLK_HW_INIT_PARENTS("pll-mipi",
						      pll_mipi_parents,
						      &ccu_nkm_ops,
						      CLK_SET_RATE_UNGATE)
	},
};

/* TODO: The result of N/M is required to be in [8, 25] range. */
static SUNXI_CCU_NM_WITH_FRAC_GATE_LOCK(pll_de_clk, "pll-de",
					"osc24M", 0x048,
					8, 7,		/* N */
					0, 4,		/* M */
					BIT(24),	/* frac enable */
					BIT(25),	/* frac select */
					270000000,	/* frac rate 0 */
					297000000,	/* frac rate 1 */
					BIT(31),	/* gate */
					BIT(28),	/* lock */
					CLK_SET_RATE_UNGATE);

/* TODO: The N factor is required to be in [16, 75] range. */
static SUNXI_CCU_NM_WITH_GATE_LOCK(pll_ddr1_clk, "pll-ddr1",
				   "osc24M", 0x04c,
				   8, 7,	/* N */
				   0, 2,	/* M */
				   BIT(31),	/* gate */
				   BIT(28),	/* lock */
				   CLK_SET_RATE_UNGATE);

static const char * const cpu_parents[] = { "osc32k", "osc24M",
					     "pll-cpu", "pll-cpu" };
static SUNXI_CCU_MUX(cpu_clk, "cpu", cpu_parents,
		     0x050, 16, 2, CLK_IS_CRITICAL | CLK_SET_RATE_PARENT);

static SUNXI_CCU_M(axi_clk, "axi", "cpu", 0x050, 0, 2, 0);

static const char * const ahb1_parents[] = { "osc32k", "osc24M",
					     "axi", "pll-periph0" };
static const struct ccu_mux_var_prediv ahb1_predivs[] = {
	{ .index = 3, .shift = 6, .width = 2 },
};
static struct ccu_div ahb1_clk = {
	.div		= _SUNXI_CCU_DIV_FLAGS(4, 2, CLK_DIVIDER_POWER_OF_TWO),

	.mux		= {
		.shift	= 12,
		.width	= 2,

		.var_predivs	= ahb1_predivs,
		.n_var_predivs	= ARRAY_SIZE(ahb1_predivs),
	},

	.common		= {
		.reg		= 0x054,
		.features	= CCU_FEATURE_VARIABLE_PREDIV,
		.hw.init	= CLK_HW_INIT_PARENTS("ahb1",
						      ahb1_parents,
						      &ccu_div_ops,
						      0),
	},
};

static struct clk_div_table apb1_div_table[] = {
	{ .val = 0, .div = 2 },
	{ .val = 1, .div = 2 },
	{ .val = 2, .div = 4 },
	{ .val = 3, .div = 8 },
	{ /* Sentinel */ },
};
static SUNXI_CCU_DIV_TABLE(apb1_clk, "apb1", "ahb1",
			   0x054, 8, 2, apb1_div_table, 0);

static const char * const apb2_parents[] = { "osc32k", "osc24M",
					     "pll-periph0-2x",
					     "pll-periph0-2x" };
static SUNXI_CCU_MP_WITH_MUX(apb2_clk, "apb2", apb2_parents, 0x058,
			     0, 5,	/* M */
			     16, 2,	/* P */
			     24, 2,	/* mux */
			     0);

static SUNXI_CCU_GATE(bus_mipi_dsi_clk,	"bus-mipi-dsi",	"ahb1",
		      0x060, BIT(1), 0);
static SUNXI_CCU_GATE(bus_ce_clk,	"bus-ce",	"ahb1",
		      0x060, BIT(5), 0);
static SUNXI_CCU_GATE(bus_dma_clk,	"bus-dma",	"ahb1",
		      0x060, BIT(6), 0);
static SUNXI_CCU_GATE(bus_mmc0_clk,	"bus-mmc0",	"ahb1",
		      0x060, BIT(8), 0);
static SUNXI_CCU_GATE(bus_mmc1_clk,	"bus-mmc1",	"ahb1",
		      0x060, BIT(9), 0);
static SUNXI_CCU_GATE(bus_mmc2_clk,	"bus-mmc2",	"ahb1",
		      0x060, BIT(10), 0);
static SUNXI_CCU_GATE(bus_mmc3_clk,	"bus-mmc3",	"ahb1",
		      0x060, BIT(11), 0);
static SUNXI_CCU_GATE(bus_nand_clk,	"bus-nand",	"ahb1",
		      0x060, BIT(13), 0);
static SUNXI_CCU_GATE(bus_dram_clk,	"bus-dram",	"ahb1",
		      0x060, BIT(14), 0);
static SUNXI_CCU_GATE(bus_emac_clk,	"bus-emac",	"ahb1",
		      0x060, BIT(17), 0);
static SUNXI_CCU_GATE(bus_ts_clk,	"bus-ts",	"ahb1",
		      0x060, BIT(18), 0);
static SUNXI_CCU_GATE(bus_hstimer_clk,	"bus-hstimer",	"ahb1",
		      0x060, BIT(19), 0);
static SUNXI_CCU_GATE(bus_spi0_clk,	"bus-spi0",	"ahb1",
		      0x060, BIT(20), 0);
static SUNXI_CCU_GATE(bus_spi1_clk,	"bus-spi1",	"ahb1",
		      0x060, BIT(21), 0);
static SUNXI_CCU_GATE(bus_spi2_clk,	"bus-spi2",	"ahb1",
		      0x060, BIT(22), 0);
static SUNXI_CCU_GATE(bus_spi3_clk,	"bus-spi3",	"ahb1",
		      0x060, BIT(23), 0);
static SUNXI_CCU_GATE(bus_sata_clk,	"bus-sata",	"ahb1",
		      0x060, BIT(24), 0);
static SUNXI_CCU_GATE(bus_otg_clk,	"bus-otg",	"ahb1",
		      0x060, BIT(25), 0);
static SUNXI_CCU_GATE(bus_ehci0_clk,	"bus-ehci0",	"ahb1",
		      0x060, BIT(26), 0);
static SUNXI_CCU_GATE(bus_ehci1_clk,	"bus-ehci1",	"ahb1",
		      0x060, BIT(27), 0);
static SUNXI_CCU_GATE(bus_ehci2_clk,	"bus-ehci2",	"ahb1",
		      0x060, BIT(28), 0);
static SUNXI_CCU_GATE(bus_ohci0_clk,	"bus-ohci0",	"ahb1",
		      0x060, BIT(29), 0);
static SUNXI_CCU_GATE(bus_ohci1_clk,	"bus-ohci1",	"ahb1",
		      0x060, BIT(30), 0);
static SUNXI_CCU_GATE(bus_ohci2_clk,	"bus-ohci2",	"ahb1",
		      0x060, BIT(31), 0);

static SUNXI_CCU_GATE(bus_ve_clk,	"bus-ve",	"ahb1",
		      0x064, BIT(0), 0);
static SUNXI_CCU_GATE(bus_mp_clk,	"bus-mp",	"ahb1",
		      0x064, BIT(2), 0);
static SUNXI_CCU_GATE(bus_deinterlace_clk,	"bus-deinterlace",	"ahb1",
		      0x064, BIT(5), 0);
static SUNXI_CCU_GATE(bus_csi0_clk,	"bus-csi0",	"ahb1",
		      0x064, BIT(8), 0);
static SUNXI_CCU_GATE(bus_csi1_clk,	"bus-csi1",	"ahb1",
		      0x064, BIT(9), 0);
static SUNXI_CCU_GATE(bus_hdmi0_clk,	"bus-hdmi0",	"ahb1",
		      0x064, BIT(10), 0);
static SUNXI_CCU_GATE(bus_hdmi1_clk,	"bus-hdmi1",	"ahb1",
		      0x064, BIT(11), 0);
static SUNXI_CCU_GATE(bus_de_clk,	"bus-de",	"ahb1",
		      0x064, BIT(12), 0);
static SUNXI_CCU_GATE(bus_tve0_clk,	"bus-tve0",	"ahb1",
		      0x064, BIT(13), 0);
static SUNXI_CCU_GATE(bus_tve1_clk,	"bus-tve1",	"ahb1",
		      0x064, BIT(14), 0);
static SUNXI_CCU_GATE(bus_tve_top_clk,	"bus-tve-top",	"ahb1",
		      0x064, BIT(15), 0);
static SUNXI_CCU_GATE(bus_gmac_clk,	"bus-gmac",	"ahb1",
		      0x064, BIT(17), 0);
static SUNXI_CCU_GATE(bus_gpu_clk,	"bus-gpu",	"ahb1",
		      0x064, BIT(20), 0);
static SUNXI_CCU_GATE(bus_tvd0_clk,	"bus-tvd0",	"ahb1",
		      0x064, BIT(21), 0);
static SUNXI_CCU_GATE(bus_tvd1_clk,	"bus-tvd1",	"ahb1",
		      0x064, BIT(22), 0);
static SUNXI_CCU_GATE(bus_tvd2_clk,	"bus-tvd2",	"ahb1",
		      0x064, BIT(23), 0);
static SUNXI_CCU_GATE(bus_tvd3_clk,	"bus-tvd3",	"ahb1",
		      0x064, BIT(24), 0);
static SUNXI_CCU_GATE(bus_tvd_top_clk,	"bus-tvd-top",	"ahb1",
		      0x064, BIT(25), 0);
static SUNXI_CCU_GATE(bus_tcon_lcd0_clk,	"bus-tcon-lcd0",	"ahb1",
		      0x064, BIT(26), 0);
static SUNXI_CCU_GATE(bus_tcon_lcd1_clk,	"bus-tcon-lcd1",	"ahb1",
		      0x064, BIT(27), 0);
static SUNXI_CCU_GATE(bus_tcon_tv0_clk,	"bus-tcon-tv0",	"ahb1",
		      0x064, BIT(28), 0);
static SUNXI_CCU_GATE(bus_tcon_tv1_clk,	"bus-tcon-tv1",	"ahb1",
		      0x064, BIT(29), 0);
static SUNXI_CCU_GATE(bus_tcon_top_clk,	"bus-tcon-top",	"ahb1",
		      0x064, BIT(30), 0);

static SUNXI_CCU_GATE(bus_codec_clk,	"bus-codec",	"apb1",
		      0x068, BIT(0), 0);
static SUNXI_CCU_GATE(bus_spdif_clk,	"bus-spdif",	"apb1",
		      0x068, BIT(1), 0);
static SUNXI_CCU_GATE(bus_ac97_clk,	"bus-ac97",	"apb1",
		      0x068, BIT(2), 0);
static SUNXI_CCU_GATE(bus_pio_clk,	"bus-pio",	"apb1",
		      0x068, BIT(5), 0);
static SUNXI_CCU_GATE(bus_ir0_clk,	"bus-ir0",	"apb1",
		      0x068, BIT(6), 0);
static SUNXI_CCU_GATE(bus_ir1_clk,	"bus-ir1",	"apb1",
		      0x068, BIT(7), 0);
static SUNXI_CCU_GATE(bus_ths_clk,	"bus-ths",	"apb1",
		      0x068, BIT(8), 0);
static SUNXI_CCU_GATE(bus_keypad_clk,	"bus-keypad",	"apb1",
		      0x068, BIT(10), 0);
static SUNXI_CCU_GATE(bus_i2s0_clk,	"bus-i2s0",	"apb1",
		      0x068, BIT(12), 0);
static SUNXI_CCU_GATE(bus_i2s1_clk,	"bus-i2s1",	"apb1",
		      0x068, BIT(13), 0);
static SUNXI_CCU_GATE(bus_i2s2_clk,	"bus-i2s2",	"apb1",
		      0x068, BIT(14), 0);

static SUNXI_CCU_GATE(bus_i2c0_clk,	"bus-i2c0",	"apb2",
		      0x06c, BIT(0), 0);
static SUNXI_CCU_GATE(bus_i2c1_clk,	"bus-i2c1",	"apb2",
		      0x06c, BIT(1), 0);
static SUNXI_CCU_GATE(bus_i2c2_clk,	"bus-i2c2",	"apb2",
		      0x06c, BIT(2), 0);
static SUNXI_CCU_GATE(bus_i2c3_clk,	"bus-i2c3",	"apb2",
		      0x06c, BIT(3), 0);
/*
 * In datasheet here's "Reserved", however the gate exists in BSP soucre
 * code.
 */
static SUNXI_CCU_GATE(bus_can_clk,	"bus-can",	"apb2",
		      0x06c, BIT(4), 0);
static SUNXI_CCU_GATE(bus_scr_clk,	"bus-scr",	"apb2",
		      0x06c, BIT(5), 0);
static SUNXI_CCU_GATE(bus_ps20_clk,	"bus-ps20",	"apb2",
		      0x06c, BIT(6), 0);
static SUNXI_CCU_GATE(bus_ps21_clk,	"bus-ps21",	"apb2",
		      0x06c, BIT(7), 0);
static SUNXI_CCU_GATE(bus_i2c4_clk,	"bus-i2c4",	"apb2",
		      0x06c, BIT(15), 0);
static SUNXI_CCU_GATE(bus_uart0_clk,	"bus-uart0",	"apb2",
		      0x06c, BIT(16), 0);
static SUNXI_CCU_GATE(bus_uart1_clk,	"bus-uart1",	"apb2",
		      0x06c, BIT(17), 0);
static SUNXI_CCU_GATE(bus_uart2_clk,	"bus-uart2",	"apb2",
		      0x06c, BIT(18), 0);
static SUNXI_CCU_GATE(bus_uart3_clk,	"bus-uart3",	"apb2",
		      0x06c, BIT(19), 0);
static SUNXI_CCU_GATE(bus_uart4_clk,	"bus-uart4",	"apb2",
		      0x06c, BIT(20), 0);
static SUNXI_CCU_GATE(bus_uart5_clk,	"bus-uart5",	"apb2",
		      0x06c, BIT(21), 0);
static SUNXI_CCU_GATE(bus_uart6_clk,	"bus-uart6",	"apb2",
		      0x06c, BIT(22), 0);
static SUNXI_CCU_GATE(bus_uart7_clk,	"bus-uart7",	"apb2",
		      0x06c, BIT(23), 0);

static SUNXI_CCU_GATE(bus_dbg_clk,	"bus-dbg",	"ahb1",
		      0x070, BIT(7), 0);

static const char * const ths_parents[] = { "osc24M" };
static struct ccu_div ths_clk = {
	.enable	= BIT(31),
	.div	= _SUNXI_CCU_DIV_FLAGS(0, 2, CLK_DIVIDER_POWER_OF_TWO),
	.mux	= _SUNXI_CCU_MUX(24, 2),
	.common	= {
		.reg		= 0x074,
		.hw.init	= CLK_HW_INIT_PARENTS("ths",
						      ths_parents,
						      &ccu_div_ops,
						      0),
	},
};

static const char * const mod0_default_parents[] = { "osc24M", "pll-periph0",
						     "pll-periph1" };
static SUNXI_CCU_MP_WITH_MUX_GATE(nand_clk, "nand", mod0_default_parents, 0x080,
				  0, 4,		/* M */
				  16, 2,	/* P */
				  24, 2,	/* mux */
				  BIT(31),	/* gate */
				  0);

static SUNXI_CCU_MP_WITH_MUX_GATE(mmc0_clk, "mmc0", mod0_default_parents, 0x088,
				  0, 4,		/* M */
				  16, 2,	/* P */
				  24, 2,	/* mux */
				  BIT(31),	/* gate */
				  0);

static SUNXI_CCU_MP_WITH_MUX_GATE(mmc1_clk, "mmc1", mod0_default_parents, 0x08c,
				  0, 4,		/* M */
				  16, 2,	/* P */
				  24, 2,	/* mux */
				  BIT(31),	/* gate */
				  0);

static SUNXI_CCU_MP_WITH_MUX_GATE(mmc2_clk, "mmc2", mod0_default_parents, 0x090,
				  0, 4,		/* M */
				  16, 2,	/* P */
				  24, 2,	/* mux */
				  BIT(31),	/* gate */
				  0);

static SUNXI_CCU_MP_WITH_MUX_GATE(mmc3_clk, "mmc3", mod0_default_parents, 0x094,
				  0, 4,		/* M */
				  16, 2,	/* P */
				  24, 2,	/* mux */
				  BIT(31),	/* gate */
				  0);

static const char * const ts_parents[] = { "osc24M", "pll-periph0", };
static SUNXI_CCU_MP_WITH_MUX_GATE(ts_clk, "ts", ts_parents, 0x098,
				  0, 4,		/* M */
				  16, 2,	/* P */
				  24, 4,	/* mux */
				  BIT(31),	/* gate */
				  0);

static const char * const ce_parents[] = { "osc24M", "pll-periph0-2x",
					   "pll-periph1-2x" };
static SUNXI_CCU_MP_WITH_MUX_GATE(ce_clk, "ce", ce_parents, 0x09c,
				  0, 4,		/* M */
				  16, 2,	/* P */
				  24, 2,	/* mux */
				  BIT(31),	/* gate */
				  0);

static SUNXI_CCU_MP_WITH_MUX_GATE(spi0_clk, "spi0", mod0_default_parents, 0x0a0,
				  0, 4,		/* M */
				  16, 2,	/* P */
				  24, 2,	/* mux */
				  BIT(31),	/* gate */
				  0);

static SUNXI_CCU_MP_WITH_MUX_GATE(spi1_clk, "spi1", mod0_default_parents, 0x0a4,
				  0, 4,		/* M */
				  16, 2,	/* P */
				  24, 2,	/* mux */
				  BIT(31),	/* gate */
				  0);

static SUNXI_CCU_MP_WITH_MUX_GATE(spi2_clk, "spi2", mod0_default_parents, 0x0a8,
				  0, 4,		/* M */
				  16, 2,	/* P */
				  24, 2,	/* mux */
				  BIT(31),	/* gate */
				  0);

static SUNXI_CCU_MP_WITH_MUX_GATE(spi3_clk, "spi3", mod0_default_parents, 0x0ac,
				  0, 4,		/* M */
				  16, 2,	/* P */
				  24, 2,	/* mux */
				  BIT(31),	/* gate */
				  0);

static const char * const i2s_parents[] = { "pll-audio-8x", "pll-audio-4x",
					    "pll-audio-2x", "pll-audio" };
static SUNXI_CCU_MUX_WITH_GATE(i2s0_clk, "i2s0", i2s_parents,
			       0x0b0, 16, 2, BIT(31), CLK_SET_RATE_PARENT);

static SUNXI_CCU_MUX_WITH_GATE(i2s1_clk, "i2s1", i2s_parents,
			       0x0b4, 16, 2, BIT(31), CLK_SET_RATE_PARENT);

static SUNXI_CCU_MUX_WITH_GATE(i2s2_clk, "i2s2", i2s_parents,
			       0x0b8, 16, 2, BIT(31), CLK_SET_RATE_PARENT);

static SUNXI_CCU_MUX_WITH_GATE(ac97_clk, "ac97", i2s_parents,
			       0x0bc, 16, 2, BIT(31), CLK_SET_RATE_PARENT);

static SUNXI_CCU_MUX_WITH_GATE(spdif_clk, "spdif", i2s_parents,
			       0x0c0, 16, 2, BIT(31), CLK_SET_RATE_PARENT);

static const char * const keypad_parents[] = { "osc24M", "osc32k" };
static const u8 keypad_table[] = { 0, 2 };
static struct ccu_mp keypad_clk = {
	.enable	= BIT(31),
	.m	= _SUNXI_CCU_DIV(0, 5),
	.p	= _SUNXI_CCU_DIV(16, 2),
	.mux	= _SUNXI_CCU_MUX_TABLE(24, 2, keypad_table),
	.common	= {
		.reg		= 0x0c4,
		.hw.init	= CLK_HW_INIT_PARENTS("keypad",
						      keypad_parents,
						      &ccu_mp_ops,
						      0),
	}
};

static const char * const sata_parents[] = { "pll-sata-out", "sata-ext" };
static SUNXI_CCU_MUX_WITH_GATE(sata_clk, "sata", sata_parents,
			       0x0c8, 24, 1, BIT(31), CLK_SET_RATE_PARENT);

/*
 * There are 3 OHCI 12M clock source selection bits in this register.
 * We will force them to 0 (12M divided from 48M).
 */
#define SUN8I_R40_USB_CLK_REG	0x0cc

static SUNXI_CCU_GATE(usb_phy0_clk,	"usb-phy0",	"osc24M",
		      0x0cc, BIT(8), 0);
static SUNXI_CCU_GATE(usb_phy1_clk,	"usb-phy1",	"osc24M",
		      0x0cc, BIT(9), 0);
static SUNXI_CCU_GATE(usb_phy2_clk,	"usb-phy2",	"osc24M",
		      0x0cc, BIT(10), 0);
static SUNXI_CCU_GATE(usb_ohci0_clk,	"usb-ohci0",	"osc12M",
		      0x0cc, BIT(16), 0);
static SUNXI_CCU_GATE(usb_ohci1_clk,	"usb-ohci1",	"osc12M",
		      0x0cc, BIT(17), 0);
static SUNXI_CCU_GATE(usb_ohci2_clk,	"usb-ohci2",	"osc12M",
		      0x0cc, BIT(18), 0);

static const char * const ir_parents[] = { "osc24M", "pll-periph0",
					   "pll-periph1", "osc32k" };
static SUNXI_CCU_MP_WITH_MUX_GATE(ir0_clk, "ir0", ir_parents, 0x0d0,
				  0, 4,		/* M */
				  16, 2,	/* P */
				  24, 2,	/* mux */
				  BIT(31),	/* gate */
				  0);

static SUNXI_CCU_MP_WITH_MUX_GATE(ir1_clk, "ir1", ir_parents, 0x0d4,
				  0, 4,		/* M */
				  16, 2,	/* P */
				  24, 2,	/* mux */
				  BIT(31),	/* gate */
				  0);

static const char * const dram_parents[] = { "pll-ddr0", "pll-ddr1" };
static SUNXI_CCU_M_WITH_MUX(dram_clk, "dram", dram_parents,
			    0x0f4, 0, 2, 20, 2, CLK_IS_CRITICAL);

static SUNXI_CCU_GATE(dram_ve_clk,	"dram-ve",	"dram",
		      0x100, BIT(0), 0);
static SUNXI_CCU_GATE(dram_csi0_clk,	"dram-csi0",	"dram",
		      0x100, BIT(1), 0);
static SUNXI_CCU_GATE(dram_csi1_clk,	"dram-csi1",	"dram",
		      0x100, BIT(2), 0);
static SUNXI_CCU_GATE(dram_ts_clk,	"dram-ts",	"dram",
		      0x100, BIT(3), 0);
static SUNXI_CCU_GATE(dram_tvd_clk,	"dram-tvd",	"dram",
		      0x100, BIT(4), 0);
static SUNXI_CCU_GATE(dram_mp_clk,	"dram-mp",	"dram",
		      0x100, BIT(5), 0);
static SUNXI_CCU_GATE(dram_deinterlace_clk,	"dram-deinterlace",	"dram",
		      0x100, BIT(6), 0);

static const char * const de_parents[] = { "pll-periph0-2x", "pll-de" };
static SUNXI_CCU_M_WITH_MUX_GATE(de_clk, "de", de_parents,
				 0x104, 0, 4, 24, 3, BIT(31),
				 CLK_SET_RATE_PARENT);
static SUNXI_CCU_M_WITH_MUX_GATE(mp_clk, "mp", de_parents,
				 0x108, 0, 4, 24, 3, BIT(31), 0);

static const char * const tcon_parents[] = { "pll-video0", "pll-video1",
					     "pll-video0-2x", "pll-video1-2x",
					     "pll-mipi" };
static SUNXI_CCU_MUX_WITH_GATE(tcon_lcd0_clk, "tcon-lcd0", tcon_parents,
			       0x110, 24, 3, BIT(31), CLK_SET_RATE_PARENT);
static SUNXI_CCU_MUX_WITH_GATE(tcon_lcd1_clk, "tcon-lcd1", tcon_parents,
			       0x114, 24, 3, BIT(31), CLK_SET_RATE_PARENT);
static SUNXI_CCU_M_WITH_MUX_GATE(tcon_tv0_clk, "tcon-tv0", tcon_parents,
				 0x118, 0, 4, 24, 3, BIT(31),
				 CLK_SET_RATE_PARENT);
static SUNXI_CCU_M_WITH_MUX_GATE(tcon_tv1_clk, "tcon-tv1", tcon_parents,
				 0x11c, 0, 4, 24, 3, BIT(31),
				 CLK_SET_RATE_PARENT);

static const char * const deinterlace_parents[] = { "pll-periph0",
						    "pll-periph1" };
static SUNXI_CCU_M_WITH_MUX_GATE(deinterlace_clk, "deinterlace",
				 deinterlace_parents, 0x124, 0, 4, 24, 3,
				 BIT(31), 0);

static const char * const csi_mclk_parents[] = { "osc24M", "pll-video1",
						 "pll-periph1" };
static SUNXI_CCU_M_WITH_MUX_GATE(csi1_mclk_clk, "csi1-mclk", csi_mclk_parents,
				 0x130, 0, 5, 8, 3, BIT(15), 0);

static const char * const csi_sclk_parents[] = { "pll-periph0", "pll-periph1" };
static SUNXI_CCU_M_WITH_MUX_GATE(csi_sclk_clk, "csi-sclk", csi_sclk_parents,
				 0x134, 16, 4, 24, 3, BIT(31), 0);

static SUNXI_CCU_M_WITH_MUX_GATE(csi0_mclk_clk, "csi0-mclk", csi_mclk_parents,
				 0x134, 0, 5, 8, 3, BIT(15), 0);

static SUNXI_CCU_M_WITH_GATE(ve_clk, "ve", "pll-ve",
			     0x13c, 16, 3, BIT(31), CLK_SET_RATE_PARENT);

static SUNXI_CCU_GATE(codec_clk,	"codec",	"pll-audio",
		      0x140, BIT(31), CLK_SET_RATE_PARENT);
static SUNXI_CCU_GATE(avs_clk,		"avs",		"osc24M",
		      0x144, BIT(31), 0);

static const char * const hdmi_parents[] = { "pll-video0", "pll-video1" };
static SUNXI_CCU_M_WITH_MUX_GATE(hdmi_clk, "hdmi", hdmi_parents,
				 0x150, 0, 4, 24, 2, BIT(31),
				 CLK_SET_RATE_PARENT);

static SUNXI_CCU_GATE(hdmi_slow_clk,	"hdmi-slow",	"osc24M",
		      0x154, BIT(31), 0);

/*
 * In the SoC's user manual, the P factor is mentioned, but not used in
 * the frequency formula.
 *
 * Here the factor is included, according to the BSP kernel source,
 * which contains the P factor of this clock.
 */
static const char * const mbus_parents[] = { "osc24M", "pll-periph0-2x",
					     "pll-ddr0" };
static SUNXI_CCU_MP_WITH_MUX_GATE(mbus_clk, "mbus", mbus_parents, 0x15c,
				  0, 4,		/* M */
				  16, 2,	/* P */
				  24, 2,	/* mux */
				  BIT(31),	/* gate */
				  CLK_IS_CRITICAL);

static const char * const dsi_dphy_parents[] = { "pll-video0", "pll-video1",
						 "pll-periph0" };
static SUNXI_CCU_M_WITH_MUX_GATE(dsi_dphy_clk, "dsi-dphy", dsi_dphy_parents,
				 0x168, 0, 4, 8, 2, BIT(15), 0);

static SUNXI_CCU_M_WITH_MUX_GATE(tve0_clk, "tve0", tcon_parents,
				 0x180, 0, 4, 24, 3, BIT(31), 0);
static SUNXI_CCU_M_WITH_MUX_GATE(tve1_clk, "tve1", tcon_parents,
				 0x184, 0, 4, 24, 3, BIT(31), 0);

static const char * const tvd_parents[] = { "pll-video0", "pll-video1",
					    "pll-video0-2x", "pll-video1-2x" };
static SUNXI_CCU_M_WITH_MUX_GATE(tvd0_clk, "tvd0", tvd_parents,
				 0x188, 0, 4, 24, 3, BIT(31), 0);
static SUNXI_CCU_M_WITH_MUX_GATE(tvd1_clk, "tvd1", tvd_parents,
				 0x18c, 0, 4, 24, 3, BIT(31), 0);
static SUNXI_CCU_M_WITH_MUX_GATE(tvd2_clk, "tvd2", tvd_parents,
				 0x190, 0, 4, 24, 3, BIT(31), 0);
static SUNXI_CCU_M_WITH_MUX_GATE(tvd3_clk, "tvd3", tvd_parents,
				 0x194, 0, 4, 24, 3, BIT(31), 0);

static SUNXI_CCU_M_WITH_GATE(gpu_clk, "gpu", "pll-gpu",
			     0x1a0, 0, 3, BIT(31), CLK_SET_RATE_PARENT);

static const char * const out_parents[] = { "osc24M", "osc32k", "osc24M" };
static const struct ccu_mux_fixed_prediv out_predivs[] = {
	{ .index = 0, .div = 750, },
};

static struct ccu_mp outa_clk = {
	.enable	= BIT(31),
	.m	= _SUNXI_CCU_DIV(8, 5),
	.p	= _SUNXI_CCU_DIV(20, 2),
	.mux	= {
		.shift		= 24,
		.width		= 2,
		.fixed_predivs	= out_predivs,
		.n_predivs	= ARRAY_SIZE(out_predivs),
	},
	.common	= {
		.reg		= 0x1f0,
		.features	= CCU_FEATURE_FIXED_PREDIV,
		.hw.init	= CLK_HW_INIT_PARENTS("outa", out_parents,
						      &ccu_mp_ops, 0),
	}
};

static struct ccu_mp outb_clk = {
	.enable	= BIT(31),
	.m	= _SUNXI_CCU_DIV(8, 5),
	.p	= _SUNXI_CCU_DIV(20, 2),
	.mux	= {
		.shift		= 24,
		.width		= 2,
		.fixed_predivs	= out_predivs,
		.n_predivs	= ARRAY_SIZE(out_predivs),
	},
	.common	= {
		.reg		= 0x1f4,
		.features	= CCU_FEATURE_FIXED_PREDIV,
		.hw.init	= CLK_HW_INIT_PARENTS("outb", out_parents,
						      &ccu_mp_ops, 0),
	}
};

static struct ccu_common *sun8i_r40_ccu_clks[] = {
	&pll_cpu_clk.common,
	&pll_audio_base_clk.common,
	&pll_video0_clk.common,
	&pll_ve_clk.common,
	&pll_ddr0_clk.common,
	&pll_periph0_clk.common,
	&pll_periph0_sata_clk.common,
	&pll_periph1_clk.common,
	&pll_video1_clk.common,
	&pll_sata_clk.common,
	&pll_sata_out_clk.common,
	&pll_gpu_clk.common,
	&pll_mipi_clk.common,
	&pll_de_clk.common,
	&pll_ddr1_clk.common,
	&cpu_clk.common,
	&axi_clk.common,
	&ahb1_clk.common,
	&apb1_clk.common,
	&apb2_clk.common,
	&bus_mipi_dsi_clk.common,
	&bus_ce_clk.common,
	&bus_dma_clk.common,
	&bus_mmc0_clk.common,
	&bus_mmc1_clk.common,
	&bus_mmc2_clk.common,
	&bus_mmc3_clk.common,
	&bus_nand_clk.common,
	&bus_dram_clk.common,
	&bus_emac_clk.common,
	&bus_ts_clk.common,
	&bus_hstimer_clk.common,
	&bus_spi0_clk.common,
	&bus_spi1_clk.common,
	&bus_spi2_clk.common,
	&bus_spi3_clk.common,
	&bus_sata_clk.common,
	&bus_otg_clk.common,
	&bus_ehci0_clk.common,
	&bus_ehci1_clk.common,
	&bus_ehci2_clk.common,
	&bus_ohci0_clk.common,
	&bus_ohci1_clk.common,
	&bus_ohci2_clk.common,
	&bus_ve_clk.common,
	&bus_mp_clk.common,
	&bus_deinterlace_clk.common,
	&bus_csi0_clk.common,
	&bus_csi1_clk.common,
	&bus_hdmi0_clk.common,
	&bus_hdmi1_clk.common,
	&bus_de_clk.common,
	&bus_tve0_clk.common,
	&bus_tve1_clk.common,
	&bus_tve_top_clk.common,
	&bus_gmac_clk.common,
	&bus_gpu_clk.common,
	&bus_tvd0_clk.common,
	&bus_tvd1_clk.common,
	&bus_tvd2_clk.common,
	&bus_tvd3_clk.common,
	&bus_tvd_top_clk.common,
	&bus_tcon_lcd0_clk.common,
	&bus_tcon_lcd1_clk.common,
	&bus_tcon_tv0_clk.common,
	&bus_tcon_tv1_clk.common,
	&bus_tcon_top_clk.common,
	&bus_codec_clk.common,
	&bus_spdif_clk.common,
	&bus_ac97_clk.common,
	&bus_pio_clk.common,
	&bus_ir0_clk.common,
	&bus_ir1_clk.common,
	&bus_ths_clk.common,
	&bus_keypad_clk.common,
	&bus_i2s0_clk.common,
	&bus_i2s1_clk.common,
	&bus_i2s2_clk.common,
	&bus_i2c0_clk.common,
	&bus_i2c1_clk.common,
	&bus_i2c2_clk.common,
	&bus_i2c3_clk.common,
	&bus_can_clk.common,
	&bus_scr_clk.common,
	&bus_ps20_clk.common,
	&bus_ps21_clk.common,
	&bus_i2c4_clk.common,
	&bus_uart0_clk.common,
	&bus_uart1_clk.common,
	&bus_uart2_clk.common,
	&bus_uart3_clk.common,
	&bus_uart4_clk.common,
	&bus_uart5_clk.common,
	&bus_uart6_clk.common,
	&bus_uart7_clk.common,
	&bus_dbg_clk.common,
	&ths_clk.common,
	&nand_clk.common,
	&mmc0_clk.common,
	&mmc1_clk.common,
	&mmc2_clk.common,
	&mmc3_clk.common,
	&ts_clk.common,
	&ce_clk.common,
	&spi0_clk.common,
	&spi1_clk.common,
	&spi2_clk.common,
	&spi3_clk.common,
	&i2s0_clk.common,
	&i2s1_clk.common,
	&i2s2_clk.common,
	&ac97_clk.common,
	&spdif_clk.common,
	&keypad_clk.common,
	&sata_clk.common,
	&usb_phy0_clk.common,
	&usb_phy1_clk.common,
	&usb_phy2_clk.common,
	&usb_ohci0_clk.common,
	&usb_ohci1_clk.common,
	&usb_ohci2_clk.common,
	&ir0_clk.common,
	&ir1_clk.common,
	&dram_clk.common,
	&dram_ve_clk.common,
	&dram_csi0_clk.common,
	&dram_csi1_clk.common,
	&dram_ts_clk.common,
	&dram_tvd_clk.common,
	&dram_mp_clk.common,
	&dram_deinterlace_clk.common,
	&de_clk.common,
	&mp_clk.common,
	&tcon_lcd0_clk.common,
	&tcon_lcd1_clk.common,
	&tcon_tv0_clk.common,
	&tcon_tv1_clk.common,
	&deinterlace_clk.common,
	&csi1_mclk_clk.common,
	&csi_sclk_clk.common,
	&csi0_mclk_clk.common,
	&ve_clk.common,
	&codec_clk.common,
	&avs_clk.common,
	&hdmi_clk.common,
	&hdmi_slow_clk.common,
	&mbus_clk.common,
	&dsi_dphy_clk.common,
	&tve0_clk.common,
	&tve1_clk.common,
	&tvd0_clk.common,
	&tvd1_clk.common,
	&tvd2_clk.common,
	&tvd3_clk.common,
	&gpu_clk.common,
	&outa_clk.common,
	&outb_clk.common,
};

/* Fixed Factor clocks */
static CLK_FIXED_FACTOR(osc12M_clk, "osc12M", "osc24M", 2, 1, 0);

/* We hardcode the divider to 4 for now */
static CLK_FIXED_FACTOR(pll_audio_clk, "pll-audio",
			"pll-audio-base", 4, 1, CLK_SET_RATE_PARENT);
static CLK_FIXED_FACTOR(pll_audio_2x_clk, "pll-audio-2x",
			"pll-audio-base", 2, 1, CLK_SET_RATE_PARENT);
static CLK_FIXED_FACTOR(pll_audio_4x_clk, "pll-audio-4x",
			"pll-audio-base", 1, 1, CLK_SET_RATE_PARENT);
static CLK_FIXED_FACTOR(pll_audio_8x_clk, "pll-audio-8x",
			"pll-audio-base", 1, 2, CLK_SET_RATE_PARENT);
static CLK_FIXED_FACTOR(pll_periph0_2x_clk, "pll-periph0-2x",
			"pll-periph0", 1, 2, 0);
static CLK_FIXED_FACTOR(pll_periph1_2x_clk, "pll-periph1-2x",
			"pll-periph1", 1, 2, 0);
static CLK_FIXED_FACTOR(pll_video0_2x_clk, "pll-video0-2x",
			"pll-video0", 1, 2, 0);
static CLK_FIXED_FACTOR(pll_video1_2x_clk, "pll-video1-2x",
			"pll-video1", 1, 2, 0);

static struct clk_hw_onecell_data sun8i_r40_hw_clks = {
	.hws	= {
		[CLK_OSC_12M]		= &osc12M_clk.hw,
		[CLK_PLL_CPU]		= &pll_cpu_clk.common.hw,
		[CLK_PLL_AUDIO_BASE]	= &pll_audio_base_clk.common.hw,
		[CLK_PLL_AUDIO]		= &pll_audio_clk.hw,
		[CLK_PLL_AUDIO_2X]	= &pll_audio_2x_clk.hw,
		[CLK_PLL_AUDIO_4X]	= &pll_audio_4x_clk.hw,
		[CLK_PLL_AUDIO_8X]	= &pll_audio_8x_clk.hw,
		[CLK_PLL_VIDEO0]	= &pll_video0_clk.common.hw,
		[CLK_PLL_VIDEO0_2X]	= &pll_video0_2x_clk.hw,
		[CLK_PLL_VE]		= &pll_ve_clk.common.hw,
		[CLK_PLL_DDR0]		= &pll_ddr0_clk.common.hw,
		[CLK_PLL_PERIPH0]	= &pll_periph0_clk.common.hw,
		[CLK_PLL_PERIPH0_SATA]	= &pll_periph0_sata_clk.common.hw,
		[CLK_PLL_PERIPH0_2X]	= &pll_periph0_2x_clk.hw,
		[CLK_PLL_PERIPH1]	= &pll_periph1_clk.common.hw,
		[CLK_PLL_PERIPH1_2X]	= &pll_periph1_2x_clk.hw,
		[CLK_PLL_VIDEO1]	= &pll_video1_clk.common.hw,
		[CLK_PLL_VIDEO1_2X]	= &pll_video1_2x_clk.hw,
		[CLK_PLL_SATA]		= &pll_sata_clk.common.hw,
		[CLK_PLL_SATA_OUT]	= &pll_sata_out_clk.common.hw,
		[CLK_PLL_GPU]		= &pll_gpu_clk.common.hw,
		[CLK_PLL_MIPI]		= &pll_mipi_clk.common.hw,
		[CLK_PLL_DE]		= &pll_de_clk.common.hw,
		[CLK_PLL_DDR1]		= &pll_ddr1_clk.common.hw,
		[CLK_CPU]		= &cpu_clk.common.hw,
		[CLK_AXI]		= &axi_clk.common.hw,
		[CLK_AHB1]		= &ahb1_clk.common.hw,
		[CLK_APB1]		= &apb1_clk.common.hw,
		[CLK_APB2]		= &apb2_clk.common.hw,
		[CLK_BUS_MIPI_DSI]	= &bus_mipi_dsi_clk.common.hw,
		[CLK_BUS_CE]		= &bus_ce_clk.common.hw,
		[CLK_BUS_DMA]		= &bus_dma_clk.common.hw,
		[CLK_BUS_MMC0]		= &bus_mmc0_clk.common.hw,
		[CLK_BUS_MMC1]		= &bus_mmc1_clk.common.hw,
		[CLK_BUS_MMC2]		= &bus_mmc2_clk.common.hw,
		[CLK_BUS_MMC3]		= &bus_mmc3_clk.common.hw,
		[CLK_BUS_NAND]		= &bus_nand_clk.common.hw,
		[CLK_BUS_DRAM]		= &bus_dram_clk.common.hw,
		[CLK_BUS_EMAC]		= &bus_emac_clk.common.hw,
		[CLK_BUS_TS]		= &bus_ts_clk.common.hw,
		[CLK_BUS_HSTIMER]	= &bus_hstimer_clk.common.hw,
		[CLK_BUS_SPI0]		= &bus_spi0_clk.common.hw,
		[CLK_BUS_SPI1]		= &bus_spi1_clk.common.hw,
		[CLK_BUS_SPI2]		= &bus_spi2_clk.common.hw,
		[CLK_BUS_SPI3]		= &bus_spi3_clk.common.hw,
		[CLK_BUS_SATA]		= &bus_sata_clk.common.hw,
		[CLK_BUS_OTG]		= &bus_otg_clk.common.hw,
		[CLK_BUS_EHCI0]		= &bus_ehci0_clk.common.hw,
		[CLK_BUS_EHCI1]		= &bus_ehci1_clk.common.hw,
		[CLK_BUS_EHCI2]		= &bus_ehci2_clk.common.hw,
		[CLK_BUS_OHCI0]		= &bus_ohci0_clk.common.hw,
		[CLK_BUS_OHCI1]		= &bus_ohci1_clk.common.hw,
		[CLK_BUS_OHCI2]		= &bus_ohci2_clk.common.hw,
		[CLK_BUS_VE]		= &bus_ve_clk.common.hw,
		[CLK_BUS_MP]		= &bus_mp_clk.common.hw,
		[CLK_BUS_DEINTERLACE]	= &bus_deinterlace_clk.common.hw,
		[CLK_BUS_CSI0]		= &bus_csi0_clk.common.hw,
		[CLK_BUS_CSI1]		= &bus_csi1_clk.common.hw,
		[CLK_BUS_HDMI0]		= &bus_hdmi0_clk.common.hw,
		[CLK_BUS_HDMI1]		= &bus_hdmi1_clk.common.hw,
		[CLK_BUS_DE]		= &bus_de_clk.common.hw,
		[CLK_BUS_TVE0]		= &bus_tve0_clk.common.hw,
		[CLK_BUS_TVE1]		= &bus_tve1_clk.common.hw,
		[CLK_BUS_TVE_TOP]	= &bus_tve_top_clk.common.hw,
		[CLK_BUS_GMAC]		= &bus_gmac_clk.common.hw,
		[CLK_BUS_GPU]		= &bus_gpu_clk.common.hw,
		[CLK_BUS_TVD0]		= &bus_tvd0_clk.common.hw,
		[CLK_BUS_TVD1]		= &bus_tvd1_clk.common.hw,
		[CLK_BUS_TVD2]		= &bus_tvd2_clk.common.hw,
		[CLK_BUS_TVD3]		= &bus_tvd3_clk.common.hw,
		[CLK_BUS_TVD_TOP]	= &bus_tvd_top_clk.common.hw,
		[CLK_BUS_TCON_LCD0]	= &bus_tcon_lcd0_clk.common.hw,
		[CLK_BUS_TCON_LCD1]	= &bus_tcon_lcd1_clk.common.hw,
		[CLK_BUS_TCON_TV0]	= &bus_tcon_tv0_clk.common.hw,
		[CLK_BUS_TCON_TV1]	= &bus_tcon_tv1_clk.common.hw,
		[CLK_BUS_TCON_TOP]	= &bus_tcon_top_clk.common.hw,
		[CLK_BUS_CODEC]		= &bus_codec_clk.common.hw,
		[CLK_BUS_SPDIF]		= &bus_spdif_clk.common.hw,
		[CLK_BUS_AC97]		= &bus_ac97_clk.common.hw,
		[CLK_BUS_PIO]		= &bus_pio_clk.common.hw,
		[CLK_BUS_IR0]		= &bus_ir0_clk.common.hw,
		[CLK_BUS_IR1]		= &bus_ir1_clk.common.hw,
		[CLK_BUS_THS]		= &bus_ths_clk.common.hw,
		[CLK_BUS_KEYPAD]	= &bus_keypad_clk.common.hw,
		[CLK_BUS_I2S0]		= &bus_i2s0_clk.common.hw,
		[CLK_BUS_I2S1]		= &bus_i2s1_clk.common.hw,
		[CLK_BUS_I2S2]		= &bus_i2s2_clk.common.hw,
		[CLK_BUS_I2C0]		= &bus_i2c0_clk.common.hw,
		[CLK_BUS_I2C1]		= &bus_i2c1_clk.common.hw,
		[CLK_BUS_I2C2]		= &bus_i2c2_clk.common.hw,
		[CLK_BUS_I2C3]		= &bus_i2c3_clk.common.hw,
		[CLK_BUS_CAN]		= &bus_can_clk.common.hw,
		[CLK_BUS_SCR]		= &bus_scr_clk.common.hw,
		[CLK_BUS_PS20]		= &bus_ps20_clk.common.hw,
		[CLK_BUS_PS21]		= &bus_ps21_clk.common.hw,
		[CLK_BUS_I2C4]		= &bus_i2c4_clk.common.hw,
		[CLK_BUS_UART0]		= &bus_uart0_clk.common.hw,
		[CLK_BUS_UART1]		= &bus_uart1_clk.common.hw,
		[CLK_BUS_UART2]		= &bus_uart2_clk.common.hw,
		[CLK_BUS_UART3]		= &bus_uart3_clk.common.hw,
		[CLK_BUS_UART4]		= &bus_uart4_clk.common.hw,
		[CLK_BUS_UART5]		= &bus_uart5_clk.common.hw,
		[CLK_BUS_UART6]		= &bus_uart6_clk.common.hw,
		[CLK_BUS_UART7]		= &bus_uart7_clk.common.hw,
		[CLK_BUS_DBG]		= &bus_dbg_clk.common.hw,
		[CLK_THS]		= &ths_clk.common.hw,
		[CLK_NAND]		= &nand_clk.common.hw,
		[CLK_MMC0]		= &mmc0_clk.common.hw,
		[CLK_MMC1]		= &mmc1_clk.common.hw,
		[CLK_MMC2]		= &mmc2_clk.common.hw,
		[CLK_MMC3]		= &mmc3_clk.common.hw,
		[CLK_TS]		= &ts_clk.common.hw,
		[CLK_CE]		= &ce_clk.common.hw,
		[CLK_SPI0]		= &spi0_clk.common.hw,
		[CLK_SPI1]		= &spi1_clk.common.hw,
		[CLK_SPI2]		= &spi2_clk.common.hw,
		[CLK_SPI3]		= &spi3_clk.common.hw,
		[CLK_I2S0]		= &i2s0_clk.common.hw,
		[CLK_I2S1]		= &i2s1_clk.common.hw,
		[CLK_I2S2]		= &i2s2_clk.common.hw,
		[CLK_AC97]		= &ac97_clk.common.hw,
		[CLK_SPDIF]		= &spdif_clk.common.hw,
		[CLK_KEYPAD]		= &keypad_clk.common.hw,
		[CLK_SATA]		= &sata_clk.common.hw,
		[CLK_USB_PHY0]		= &usb_phy0_clk.common.hw,
		[CLK_USB_PHY1]		= &usb_phy1_clk.common.hw,
		[CLK_USB_PHY2]		= &usb_phy2_clk.common.hw,
		[CLK_USB_OHCI0]		= &usb_ohci0_clk.common.hw,
		[CLK_USB_OHCI1]		= &usb_ohci1_clk.common.hw,
		[CLK_USB_OHCI2]		= &usb_ohci2_clk.common.hw,
		[CLK_IR0]		= &ir0_clk.common.hw,
		[CLK_IR1]		= &ir1_clk.common.hw,
		[CLK_DRAM]		= &dram_clk.common.hw,
		[CLK_DRAM_VE]		= &dram_ve_clk.common.hw,
		[CLK_DRAM_CSI0]		= &dram_csi0_clk.common.hw,
		[CLK_DRAM_CSI1]		= &dram_csi1_clk.common.hw,
		[CLK_DRAM_TS]		= &dram_ts_clk.common.hw,
		[CLK_DRAM_TVD]		= &dram_tvd_clk.common.hw,
		[CLK_DRAM_MP]		= &dram_mp_clk.common.hw,
		[CLK_DRAM_DEINTERLACE]	= &dram_deinterlace_clk.common.hw,
		[CLK_DE]		= &de_clk.common.hw,
		[CLK_MP]		= &mp_clk.common.hw,
		[CLK_TCON_LCD0]		= &tcon_lcd0_clk.common.hw,
		[CLK_TCON_LCD1]		= &tcon_lcd1_clk.common.hw,
		[CLK_TCON_TV0]		= &tcon_tv0_clk.common.hw,
		[CLK_TCON_TV1]		= &tcon_tv1_clk.common.hw,
		[CLK_DEINTERLACE]	= &deinterlace_clk.common.hw,
		[CLK_CSI1_MCLK]		= &csi1_mclk_clk.common.hw,
		[CLK_CSI_SCLK]		= &csi_sclk_clk.common.hw,
		[CLK_CSI0_MCLK]		= &csi0_mclk_clk.common.hw,
		[CLK_VE]		= &ve_clk.common.hw,
		[CLK_CODEC]		= &codec_clk.common.hw,
		[CLK_AVS]		= &avs_clk.common.hw,
		[CLK_HDMI]		= &hdmi_clk.common.hw,
		[CLK_HDMI_SLOW]		= &hdmi_slow_clk.common.hw,
		[CLK_MBUS]		= &mbus_clk.common.hw,
		[CLK_DSI_DPHY]		= &dsi_dphy_clk.common.hw,
		[CLK_TVE0]		= &tve0_clk.common.hw,
		[CLK_TVE1]		= &tve1_clk.common.hw,
		[CLK_TVD0]		= &tvd0_clk.common.hw,
		[CLK_TVD1]		= &tvd1_clk.common.hw,
		[CLK_TVD2]		= &tvd2_clk.common.hw,
		[CLK_TVD3]		= &tvd3_clk.common.hw,
		[CLK_GPU]		= &gpu_clk.common.hw,
		[CLK_OUTA]		= &outa_clk.common.hw,
		[CLK_OUTB]		= &outb_clk.common.hw,
	},
	.num	= CLK_NUMBER,
};

static struct ccu_reset_map sun8i_r40_ccu_resets[] = {
	[RST_USB_PHY0]		=  { 0x0cc, BIT(0) },
	[RST_USB_PHY1]		=  { 0x0cc, BIT(1) },
	[RST_USB_PHY2]		=  { 0x0cc, BIT(2) },

	[RST_DRAM]		=  { 0x0f4, BIT(31) },
	[RST_MBUS]		=  { 0x0fc, BIT(31) },

	[RST_BUS_MIPI_DSI]	=  { 0x2c0, BIT(1) },
	[RST_BUS_CE]		=  { 0x2c0, BIT(5) },
	[RST_BUS_DMA]		=  { 0x2c0, BIT(6) },
	[RST_BUS_MMC0]		=  { 0x2c0, BIT(8) },
	[RST_BUS_MMC1]		=  { 0x2c0, BIT(9) },
	[RST_BUS_MMC2]		=  { 0x2c0, BIT(10) },
	[RST_BUS_MMC3]		=  { 0x2c0, BIT(11) },
	[RST_BUS_NAND]		=  { 0x2c0, BIT(13) },
	[RST_BUS_DRAM]		=  { 0x2c0, BIT(14) },
	[RST_BUS_EMAC]		=  { 0x2c0, BIT(17) },
	[RST_BUS_TS]		=  { 0x2c0, BIT(18) },
	[RST_BUS_HSTIMER]	=  { 0x2c0, BIT(19) },
	[RST_BUS_SPI0]		=  { 0x2c0, BIT(20) },
	[RST_BUS_SPI1]		=  { 0x2c0, BIT(21) },
	[RST_BUS_SPI2]		=  { 0x2c0, BIT(22) },
	[RST_BUS_SPI3]		=  { 0x2c0, BIT(23) },
	[RST_BUS_SATA]		=  { 0x2c0, BIT(24) },
	[RST_BUS_OTG]		=  { 0x2c0, BIT(25) },
	[RST_BUS_EHCI0]		=  { 0x2c0, BIT(26) },
	[RST_BUS_EHCI1]		=  { 0x2c0, BIT(27) },
	[RST_BUS_EHCI2]		=  { 0x2c0, BIT(28) },
	[RST_BUS_OHCI0]		=  { 0x2c0, BIT(29) },
	[RST_BUS_OHCI1]		=  { 0x2c0, BIT(30) },
	[RST_BUS_OHCI2]		=  { 0x2c0, BIT(31) },

	[RST_BUS_VE]		=  { 0x2c4, BIT(0) },
	[RST_BUS_MP]		=  { 0x2c4, BIT(2) },
	[RST_BUS_DEINTERLACE]	=  { 0x2c4, BIT(5) },
	[RST_BUS_CSI0]		=  { 0x2c4, BIT(8) },
	[RST_BUS_CSI1]		=  { 0x2c4, BIT(9) },
	[RST_BUS_HDMI0]		=  { 0x2c4, BIT(10) },
	[RST_BUS_HDMI1]		=  { 0x2c4, BIT(11) },
	[RST_BUS_DE]		=  { 0x2c4, BIT(12) },
	[RST_BUS_TVE0]		=  { 0x2c4, BIT(13) },
	[RST_BUS_TVE1]		=  { 0x2c4, BIT(14) },
	[RST_BUS_TVE_TOP]	=  { 0x2c4, BIT(15) },
	[RST_BUS_GMAC]		=  { 0x2c4, BIT(17) },
	[RST_BUS_GPU]		=  { 0x2c4, BIT(20) },
	[RST_BUS_TVD0]		=  { 0x2c4, BIT(21) },
	[RST_BUS_TVD1]		=  { 0x2c4, BIT(22) },
	[RST_BUS_TVD2]		=  { 0x2c4, BIT(23) },
	[RST_BUS_TVD3]		=  { 0x2c4, BIT(24) },
	[RST_BUS_TVD_TOP]	=  { 0x2c4, BIT(25) },
	[RST_BUS_TCON_LCD0]	=  { 0x2c4, BIT(26) },
	[RST_BUS_TCON_LCD1]	=  { 0x2c4, BIT(27) },
	[RST_BUS_TCON_TV0]	=  { 0x2c4, BIT(28) },
	[RST_BUS_TCON_TV1]	=  { 0x2c4, BIT(29) },
	[RST_BUS_TCON_TOP]	=  { 0x2c4, BIT(30) },
	[RST_BUS_DBG]		=  { 0x2c4, BIT(31) },

	[RST_BUS_LVDS]		=  { 0x2c8, BIT(0) },

	[RST_BUS_CODEC]		=  { 0x2d0, BIT(0) },
	[RST_BUS_SPDIF]		=  { 0x2d0, BIT(1) },
	[RST_BUS_AC97]		=  { 0x2d0, BIT(2) },
	[RST_BUS_IR0]		=  { 0x2d0, BIT(6) },
	[RST_BUS_IR1]		=  { 0x2d0, BIT(7) },
	[RST_BUS_THS]		=  { 0x2d0, BIT(8) },
	[RST_BUS_KEYPAD]	=  { 0x2d0, BIT(10) },
	[RST_BUS_I2S0]		=  { 0x2d0, BIT(12) },
	[RST_BUS_I2S1]		=  { 0x2d0, BIT(13) },
	[RST_BUS_I2S2]		=  { 0x2d0, BIT(14) },

	[RST_BUS_I2C0]		=  { 0x2d8, BIT(0) },
	[RST_BUS_I2C1]		=  { 0x2d8, BIT(1) },
	[RST_BUS_I2C2]		=  { 0x2d8, BIT(2) },
	[RST_BUS_I2C3]		=  { 0x2d8, BIT(3) },
	[RST_BUS_CAN]		=  { 0x2d8, BIT(4) },
	[RST_BUS_SCR]		=  { 0x2d8, BIT(5) },
	[RST_BUS_PS20]		=  { 0x2d8, BIT(6) },
	[RST_BUS_PS21]		=  { 0x2d8, BIT(7) },
	[RST_BUS_I2C4]		=  { 0x2d8, BIT(15) },
	[RST_BUS_UART0]		=  { 0x2d8, BIT(16) },
	[RST_BUS_UART1]		=  { 0x2d8, BIT(17) },
	[RST_BUS_UART2]		=  { 0x2d8, BIT(18) },
	[RST_BUS_UART3]		=  { 0x2d8, BIT(19) },
	[RST_BUS_UART4]		=  { 0x2d8, BIT(20) },
	[RST_BUS_UART5]		=  { 0x2d8, BIT(21) },
	[RST_BUS_UART6]		=  { 0x2d8, BIT(22) },
	[RST_BUS_UART7]		=  { 0x2d8, BIT(23) },
};

static const struct sunxi_ccu_desc sun8i_r40_ccu_desc = {
	.ccu_clks	= sun8i_r40_ccu_clks,
	.num_ccu_clks	= ARRAY_SIZE(sun8i_r40_ccu_clks),

	.hw_clks	= &sun8i_r40_hw_clks,

	.resets		= sun8i_r40_ccu_resets,
	.num_resets	= ARRAY_SIZE(sun8i_r40_ccu_resets),
};

static struct ccu_pll_nb sun8i_r40_pll_cpu_nb = {
	.common	= &pll_cpu_clk.common,
	/* copy from pll_cpu_clk */
	.enable	= BIT(31),
	.lock	= BIT(28),
};

static struct ccu_mux_nb sun8i_r40_cpu_nb = {
	.common		= &cpu_clk.common,
	.cm		= &cpu_clk.mux,
	.delay_us	= 1, /* > 8 clock cycles at 24 MHz */
	.bypass_index	= 1, /* index of 24 MHz oscillator */
};

/*
 * Add a regmap for the GMAC driver (dwmac-sun8i) to access the
 * GMAC configuration register.
 * Only this register is allowed to be written, in order to
 * prevent overriding critical clock configuration.
 */

#define SUN8I_R40_GMAC_CFG_REG 0x164
static bool sun8i_r40_ccu_regmap_accessible_reg(struct device *dev,
						unsigned int reg)
{
	if (reg == SUN8I_R40_GMAC_CFG_REG)
		return true;
	return false;
}

static struct regmap_config sun8i_r40_ccu_regmap_config = {
	.reg_bits	= 32,
	.val_bits	= 32,
	.reg_stride	= 4,
	.max_register	= 0x320, /* PLL_LOCK_CTRL_REG */

	/* other devices have no business accessing other registers */
	.readable_reg	= sun8i_r40_ccu_regmap_accessible_reg,
	.writeable_reg	= sun8i_r40_ccu_regmap_accessible_reg,
};

#define SUN8I_R40_SYS_32K_CLK_REG 0x310
#define SUN8I_R40_SYS_32K_CLK_KEY (0x16AA << 16)

static int sun8i_r40_ccu_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct regmap *regmap;
	void __iomem *reg;
	u32 val;
	int ret;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	reg = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(reg))
		return PTR_ERR(reg);

	/* Force the PLL-Audio-1x divider to 4 */
	val = readl(reg + SUN8I_R40_PLL_AUDIO_REG);
	val &= ~GENMASK(19, 16);
	writel(val | (3 << 16), reg + SUN8I_R40_PLL_AUDIO_REG);

	/* Force PLL-MIPI to MIPI mode */
	val = readl(reg + SUN8I_R40_PLL_MIPI_REG);
	val &= ~BIT(16);
	writel(val, reg + SUN8I_R40_PLL_MIPI_REG);

	/* Force OHCI 12M parent to 12M divided from 48M */
	val = readl(reg + SUN8I_R40_USB_CLK_REG);
	val &= ~GENMASK(25, 20);
	writel(val, reg + SUN8I_R40_USB_CLK_REG);

	/*
	 * Force SYS 32k (otherwise known as LOSC throughout the CCU)
	 * clock parent to LOSC output from RTC module instead of the
	 * CCU's internal RC oscillator divided output.
	 */
	writel(SUN8I_R40_SYS_32K_CLK_KEY | BIT(8),
	       reg + SUN8I_R40_SYS_32K_CLK_REG);

	regmap = devm_regmap_init_mmio(&pdev->dev, reg,
				       &sun8i_r40_ccu_regmap_config);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	ret = sunxi_ccu_probe(pdev->dev.of_node, reg, &sun8i_r40_ccu_desc);
	if (ret)
		return ret;

	/* Gate then ungate PLL CPU after any rate changes */
	ccu_pll_notifier_register(&sun8i_r40_pll_cpu_nb);

	/* Reparent CPU during PLL CPU rate changes */
	ccu_mux_notifier_register(pll_cpu_clk.common.hw.clk,
				  &sun8i_r40_cpu_nb);

	return 0;
}

static const struct of_device_id sun8i_r40_ccu_ids[] = {
	{ .compatible = "allwinner,sun8i-r40-ccu" },
	{ }
};

static struct platform_driver sun8i_r40_ccu_driver = {
	.probe	= sun8i_r40_ccu_probe,
	.driver	= {
		.name	= "sun8i-r40-ccu",
		.of_match_table	= sun8i_r40_ccu_ids,
	},
};
builtin_platform_driver(sun8i_r40_ccu_driver);
