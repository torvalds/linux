/*
 * Copyright (c) 2016 Maxime Ripard. All rights reserved.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/clk-provider.h>
#include <linux/of_address.h>

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
#include "ccu_sdm.h"

#include "ccu-sun8i-a23-a33.h"


static struct ccu_nkmp pll_cpux_clk = {
	.enable = BIT(31),
	.lock	= BIT(28),

	.n	= _SUNXI_CCU_MULT(8, 5),
	.k	= _SUNXI_CCU_MULT(4, 2),
	.m	= _SUNXI_CCU_DIV(0, 2),
	.p	= _SUNXI_CCU_DIV_MAX(16, 2, 4),

	.common	= {
		.reg		= 0x000,
		.hw.init	= CLK_HW_INIT("pll-cpux", "osc24M",
					      &ccu_nkmp_ops,
					      0),
	},
};

/*
 * The Audio PLL is supposed to have 4 outputs: 3 fixed factors from
 * the base (2x, 4x and 8x), and one variable divider (the one true
 * pll audio).
 *
 * With sigma-delta modulation for fractional-N on the audio PLL,
 * we have to use specific dividers. This means the variable divider
 * can no longer be used, as the audio codec requests the exact clock
 * rates we support through this mechanism. So we now hard code the
 * variable divider to 1. This means the clock rates will no longer
 * match the clock names.
 */
#define SUN8I_A23_PLL_AUDIO_REG	0x008

static struct ccu_sdm_setting pll_audio_sdm_table[] = {
	{ .rate = 22579200, .pattern = 0xc0010d84, .m = 8, .n = 7 },
	{ .rate = 24576000, .pattern = 0xc000ac02, .m = 14, .n = 14 },
};

static SUNXI_CCU_NM_WITH_SDM_GATE_LOCK(pll_audio_base_clk, "pll-audio-base",
				       "osc24M", 0x008,
				       8, 7,	/* N */
				       0, 5,	/* M */
				       pll_audio_sdm_table, BIT(24),
				       0x284, BIT(31),
				       BIT(31),	/* gate */
				       BIT(28),	/* lock */
				       CLK_SET_RATE_UNGATE);

static SUNXI_CCU_NM_WITH_FRAC_GATE_LOCK(pll_video_clk, "pll-video",
					"osc24M", 0x010,
					8, 7,		/* N */
					0, 4,		/* M */
					BIT(24),	/* frac enable */
					BIT(25),	/* frac select */
					270000000,	/* frac rate 0 */
					297000000,	/* frac rate 1 */
					BIT(31),	/* gate */
					BIT(28),	/* lock */
					CLK_SET_RATE_UNGATE);

static SUNXI_CCU_NM_WITH_FRAC_GATE_LOCK(pll_ve_clk, "pll-ve",
					"osc24M", 0x018,
					8, 7,		/* N */
					0, 4,		/* M */
					BIT(24),	/* frac enable */
					BIT(25),	/* frac select */
					270000000,	/* frac rate 0 */
					297000000,	/* frac rate 1 */
					BIT(31),	/* gate */
					BIT(28),	/* lock */
					CLK_SET_RATE_UNGATE);

static SUNXI_CCU_NKM_WITH_GATE_LOCK(pll_ddr_clk, "pll-ddr",
				    "osc24M", 0x020,
				    8, 5,		/* N */
				    4, 2,		/* K */
				    0, 2,		/* M */
				    BIT(31),		/* gate */
				    BIT(28),		/* lock */
				    0);

static SUNXI_CCU_NK_WITH_GATE_LOCK_POSTDIV(pll_periph_clk, "pll-periph",
					   "osc24M", 0x028,
					   8, 5,	/* N */
					   4, 2,	/* K */
					   BIT(31),	/* gate */
					   BIT(28),	/* lock */
					   2,		/* post-div */
					   CLK_SET_RATE_UNGATE);

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
 */
#define SUN8I_A23_PLL_MIPI_REG	0x040
static SUNXI_CCU_NKM_WITH_GATE_LOCK(pll_mipi_clk, "pll-mipi",
				    "pll-video", 0x040,
				    8, 4,		/* N */
				    4, 2,		/* K */
				    0, 4,		/* M */
				    BIT(31) | BIT(23) | BIT(22), /* gate */
				    BIT(28),		/* lock */
				    CLK_SET_RATE_UNGATE);

static SUNXI_CCU_NM_WITH_FRAC_GATE_LOCK(pll_hsic_clk, "pll-hsic",
					"osc24M", 0x044,
					8, 7,		/* N */
					0, 4,		/* M */
					BIT(24),	/* frac enable */
					BIT(25),	/* frac select */
					270000000,	/* frac rate 0 */
					297000000,	/* frac rate 1 */
					BIT(31),	/* gate */
					BIT(28),	/* lock */
					CLK_SET_RATE_UNGATE);

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

static const char * const cpux_parents[] = { "osc32k", "osc24M",
					     "pll-cpux" , "pll-cpux" };
static SUNXI_CCU_MUX(cpux_clk, "cpux", cpux_parents,
		     0x050, 16, 2, CLK_IS_CRITICAL);

static SUNXI_CCU_M(axi_clk, "axi", "cpux", 0x050, 0, 2, 0);

static const char * const ahb1_parents[] = { "osc32k", "osc24M",
					     "axi" , "pll-periph" };
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
					     "pll-periph" , "pll-periph" };
static SUNXI_CCU_MP_WITH_MUX(apb2_clk, "apb2", apb2_parents, 0x058,
			     0, 5,	/* M */
			     16, 2,	/* P */
			     24, 2,	/* mux */
			     0);

static SUNXI_CCU_GATE(bus_mipi_dsi_clk,	"bus-mipi-dsi",	"ahb1",
		      0x060, BIT(1), 0);
static SUNXI_CCU_GATE(bus_dma_clk,	"bus-dma",	"ahb1",
		      0x060, BIT(6), 0);
static SUNXI_CCU_GATE(bus_mmc0_clk,	"bus-mmc0",	"ahb1",
		      0x060, BIT(8), 0);
static SUNXI_CCU_GATE(bus_mmc1_clk,	"bus-mmc1",	"ahb1",
		      0x060, BIT(9), 0);
static SUNXI_CCU_GATE(bus_mmc2_clk,	"bus-mmc2",	"ahb1",
		      0x060, BIT(10), 0);
static SUNXI_CCU_GATE(bus_nand_clk,	"bus-nand",	"ahb1",
		      0x060, BIT(13), 0);
static SUNXI_CCU_GATE(bus_dram_clk,	"bus-dram",	"ahb1",
		      0x060, BIT(14), 0);
static SUNXI_CCU_GATE(bus_hstimer_clk,	"bus-hstimer",	"ahb1",
		      0x060, BIT(19), 0);
static SUNXI_CCU_GATE(bus_spi0_clk,	"bus-spi0",	"ahb1",
		      0x060, BIT(20), 0);
static SUNXI_CCU_GATE(bus_spi1_clk,	"bus-spi1",	"ahb1",
		      0x060, BIT(21), 0);
static SUNXI_CCU_GATE(bus_otg_clk,	"bus-otg",	"ahb1",
		      0x060, BIT(24), 0);
static SUNXI_CCU_GATE(bus_ehci_clk,	"bus-ehci",	"ahb1",
		      0x060, BIT(26), 0);
static SUNXI_CCU_GATE(bus_ohci_clk,	"bus-ohci",	"ahb1",
		      0x060, BIT(29), 0);

static SUNXI_CCU_GATE(bus_ve_clk,	"bus-ve",	"ahb1",
		      0x064, BIT(0), 0);
static SUNXI_CCU_GATE(bus_lcd_clk,	"bus-lcd",	"ahb1",
		      0x064, BIT(4), 0);
static SUNXI_CCU_GATE(bus_csi_clk,	"bus-csi",	"ahb1",
		      0x064, BIT(8), 0);
static SUNXI_CCU_GATE(bus_de_be_clk,	"bus-de-be",	"ahb1",
		      0x064, BIT(12), 0);
static SUNXI_CCU_GATE(bus_de_fe_clk,	"bus-de-fe",	"ahb1",
		      0x064, BIT(14), 0);
static SUNXI_CCU_GATE(bus_gpu_clk,	"bus-gpu",	"ahb1",
		      0x064, BIT(20), 0);
static SUNXI_CCU_GATE(bus_msgbox_clk,	"bus-msgbox",	"ahb1",
		      0x064, BIT(21), 0);
static SUNXI_CCU_GATE(bus_spinlock_clk,	"bus-spinlock",	"ahb1",
		      0x064, BIT(22), 0);
static SUNXI_CCU_GATE(bus_drc_clk,	"bus-drc",	"ahb1",
		      0x064, BIT(25), 0);

static SUNXI_CCU_GATE(bus_codec_clk,	"bus-codec",	"apb1",
		      0x068, BIT(0), 0);
static SUNXI_CCU_GATE(bus_pio_clk,	"bus-pio",	"apb1",
		      0x068, BIT(5), 0);
static SUNXI_CCU_GATE(bus_i2s0_clk,	"bus-i2s0",	"apb1",
		      0x068, BIT(12), 0);
static SUNXI_CCU_GATE(bus_i2s1_clk,	"bus-i2s1",	"apb1",
		      0x068, BIT(13), 0);

static SUNXI_CCU_GATE(bus_i2c0_clk,	"bus-i2c0",	"apb2",
		      0x06c, BIT(0), 0);
static SUNXI_CCU_GATE(bus_i2c1_clk,	"bus-i2c1",	"apb2",
		      0x06c, BIT(1), 0);
static SUNXI_CCU_GATE(bus_i2c2_clk,	"bus-i2c2",	"apb2",
		      0x06c, BIT(2), 0);
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

static const char * const mod0_default_parents[] = { "osc24M", "pll-periph" };
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

static SUNXI_CCU_PHASE(mmc0_sample_clk, "mmc0_sample", "mmc0",
		       0x088, 20, 3, 0);
static SUNXI_CCU_PHASE(mmc0_output_clk, "mmc0_output", "mmc0",
		       0x088, 8, 3, 0);

static SUNXI_CCU_MP_WITH_MUX_GATE(mmc1_clk, "mmc1", mod0_default_parents, 0x08c,
				  0, 4,		/* M */
				  16, 2,	/* P */
				  24, 2,	/* mux */
				  BIT(31),	/* gate */
				  0);

static SUNXI_CCU_PHASE(mmc1_sample_clk, "mmc1_sample", "mmc1",
		       0x08c, 20, 3, 0);
static SUNXI_CCU_PHASE(mmc1_output_clk, "mmc1_output", "mmc1",
		       0x08c, 8, 3, 0);

static SUNXI_CCU_MP_WITH_MUX_GATE(mmc2_clk, "mmc2", mod0_default_parents, 0x090,
				  0, 4,		/* M */
				  16, 2,	/* P */
				  24, 2,	/* mux */
				  BIT(31),	/* gate */
				  0);

static SUNXI_CCU_PHASE(mmc2_sample_clk, "mmc2_sample", "mmc2",
		       0x090, 20, 3, 0);
static SUNXI_CCU_PHASE(mmc2_output_clk, "mmc2_output", "mmc2",
		       0x090, 8, 3, 0);

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

static const char * const i2s_parents[] = { "pll-audio-8x", "pll-audio-4x",
					    "pll-audio-2x", "pll-audio" };
static SUNXI_CCU_MUX_WITH_GATE(i2s0_clk, "i2s0", i2s_parents,
			       0x0b0, 16, 2, BIT(31), CLK_SET_RATE_PARENT);

static SUNXI_CCU_MUX_WITH_GATE(i2s1_clk, "i2s1", i2s_parents,
			       0x0b4, 16, 2, BIT(31), CLK_SET_RATE_PARENT);

/* TODO: the parent for most of the USB clocks is not known */
static SUNXI_CCU_GATE(usb_phy0_clk,	"usb-phy0",	"osc24M",
		      0x0cc, BIT(8), 0);
static SUNXI_CCU_GATE(usb_phy1_clk,	"usb-phy1",	"osc24M",
		      0x0cc, BIT(9), 0);
static SUNXI_CCU_GATE(usb_hsic_clk,	"usb-hsic",	"pll-hsic",
		      0x0cc, BIT(10), 0);
static SUNXI_CCU_GATE(usb_hsic_12M_clk,	"usb-hsic-12M",	"osc24M",
		      0x0cc, BIT(11), 0);
static SUNXI_CCU_GATE(usb_ohci_clk,	"usb-ohci",	"osc24M",
		      0x0cc, BIT(16), 0);

static SUNXI_CCU_GATE(dram_ve_clk,	"dram-ve",	"pll-ddr",
		      0x100, BIT(0), 0);
static SUNXI_CCU_GATE(dram_csi_clk,	"dram-csi",	"pll-ddr",
		      0x100, BIT(1), 0);
static SUNXI_CCU_GATE(dram_drc_clk,	"dram-drc",	"pll-ddr",
		      0x100, BIT(16), 0);
static SUNXI_CCU_GATE(dram_de_fe_clk,	"dram-de-fe",	"pll-ddr",
		      0x100, BIT(24), 0);
static SUNXI_CCU_GATE(dram_de_be_clk,	"dram-de-be",	"pll-ddr",
		      0x100, BIT(26), 0);

static const char * const de_parents[] = { "pll-video", "pll-periph-2x",
					   "pll-gpu", "pll-de" };
static const u8 de_table[] = { 0, 2, 3, 5 };
static SUNXI_CCU_M_WITH_MUX_TABLE_GATE(de_be_clk, "de-be",
				       de_parents, de_table,
				       0x104, 0, 4, 24, 3, BIT(31), 0);

static SUNXI_CCU_M_WITH_MUX_TABLE_GATE(de_fe_clk, "de-fe",
				       de_parents, de_table,
				       0x10c, 0, 4, 24, 3, BIT(31), 0);

static const char * const lcd_ch0_parents[] = { "pll-video", "pll-video-2x",
						"pll-mipi" };
static const u8 lcd_ch0_table[] = { 0, 2, 4 };
static SUNXI_CCU_MUX_TABLE_WITH_GATE(lcd_ch0_clk, "lcd-ch0",
				     lcd_ch0_parents, lcd_ch0_table,
				     0x118, 24, 3, BIT(31),
				     CLK_SET_RATE_PARENT);

static const char * const lcd_ch1_parents[] = { "pll-video", "pll-video-2x" };
static const u8 lcd_ch1_table[] = { 0, 2 };
static SUNXI_CCU_M_WITH_MUX_TABLE_GATE(lcd_ch1_clk, "lcd-ch1",
				       lcd_ch1_parents, lcd_ch1_table,
				       0x12c, 0, 4, 24, 2, BIT(31), 0);

static const char * const csi_sclk_parents[] = { "pll-video", "pll-de",
						 "pll-mipi", "pll-ve" };
static const u8 csi_sclk_table[] = { 0, 3, 4, 5 };
static SUNXI_CCU_M_WITH_MUX_TABLE_GATE(csi_sclk_clk, "csi-sclk",
				       csi_sclk_parents, csi_sclk_table,
				       0x134, 16, 4, 24, 3, BIT(31), 0);

static const char * const csi_mclk_parents[] = { "pll-video", "pll-de",
						 "osc24M" };
static const u8 csi_mclk_table[] = { 0, 3, 5 };
static SUNXI_CCU_M_WITH_MUX_TABLE_GATE(csi_mclk_clk, "csi-mclk",
				       csi_mclk_parents, csi_mclk_table,
				       0x134, 0, 5, 8, 3, BIT(15), 0);

static SUNXI_CCU_M_WITH_GATE(ve_clk, "ve", "pll-ve",
			     0x13c, 16, 3, BIT(31), CLK_SET_RATE_PARENT);

static SUNXI_CCU_GATE(ac_dig_clk,	"ac-dig",	"pll-audio",
		      0x140, BIT(31), CLK_SET_RATE_PARENT);
static SUNXI_CCU_GATE(avs_clk,		"avs",		"osc24M",
		      0x144, BIT(31), 0);

static const char * const mbus_parents[] = { "osc24M", "pll-periph-2x",
					     "pll-ddr" };
static SUNXI_CCU_M_WITH_MUX_GATE(mbus_clk, "mbus", mbus_parents,
				 0x15c, 0, 3, 24, 2, BIT(31), CLK_IS_CRITICAL);

static const char * const dsi_sclk_parents[] = { "pll-video", "pll-video-2x" };
static const u8 dsi_sclk_table[] = { 0, 2 };
static SUNXI_CCU_M_WITH_MUX_TABLE_GATE(dsi_sclk_clk, "dsi-sclk",
				       dsi_sclk_parents, dsi_sclk_table,
				       0x168, 16, 4, 24, 2, BIT(31), 0);

static const char * const dsi_dphy_parents[] = { "pll-video", "pll-periph" };
static const u8 dsi_dphy_table[] = { 0, 2 };
static SUNXI_CCU_M_WITH_MUX_TABLE_GATE(dsi_dphy_clk, "dsi-dphy",
				       dsi_dphy_parents, dsi_dphy_table,
				       0x168, 0, 4, 8, 2, BIT(15), 0);

static SUNXI_CCU_M_WITH_MUX_TABLE_GATE(drc_clk, "drc",
				       de_parents, de_table,
				       0x180, 0, 4, 24, 3, BIT(31), 0);

static SUNXI_CCU_M_WITH_GATE(gpu_clk, "gpu", "pll-gpu",
			     0x1a0, 0, 3, BIT(31), 0);

static const char * const ats_parents[] = { "osc24M", "pll-periph" };
static SUNXI_CCU_M_WITH_MUX_GATE(ats_clk, "ats", ats_parents,
				 0x1b0, 0, 3, 24, 2, BIT(31), 0);

static struct ccu_common *sun8i_a23_ccu_clks[] = {
	&pll_cpux_clk.common,
	&pll_audio_base_clk.common,
	&pll_video_clk.common,
	&pll_ve_clk.common,
	&pll_ddr_clk.common,
	&pll_periph_clk.common,
	&pll_gpu_clk.common,
	&pll_mipi_clk.common,
	&pll_hsic_clk.common,
	&pll_de_clk.common,
	&cpux_clk.common,
	&axi_clk.common,
	&ahb1_clk.common,
	&apb1_clk.common,
	&apb2_clk.common,
	&bus_mipi_dsi_clk.common,
	&bus_dma_clk.common,
	&bus_mmc0_clk.common,
	&bus_mmc1_clk.common,
	&bus_mmc2_clk.common,
	&bus_nand_clk.common,
	&bus_dram_clk.common,
	&bus_hstimer_clk.common,
	&bus_spi0_clk.common,
	&bus_spi1_clk.common,
	&bus_otg_clk.common,
	&bus_ehci_clk.common,
	&bus_ohci_clk.common,
	&bus_ve_clk.common,
	&bus_lcd_clk.common,
	&bus_csi_clk.common,
	&bus_de_fe_clk.common,
	&bus_de_be_clk.common,
	&bus_gpu_clk.common,
	&bus_msgbox_clk.common,
	&bus_spinlock_clk.common,
	&bus_drc_clk.common,
	&bus_codec_clk.common,
	&bus_pio_clk.common,
	&bus_i2s0_clk.common,
	&bus_i2s1_clk.common,
	&bus_i2c0_clk.common,
	&bus_i2c1_clk.common,
	&bus_i2c2_clk.common,
	&bus_uart0_clk.common,
	&bus_uart1_clk.common,
	&bus_uart2_clk.common,
	&bus_uart3_clk.common,
	&bus_uart4_clk.common,
	&nand_clk.common,
	&mmc0_clk.common,
	&mmc0_sample_clk.common,
	&mmc0_output_clk.common,
	&mmc1_clk.common,
	&mmc1_sample_clk.common,
	&mmc1_output_clk.common,
	&mmc2_clk.common,
	&mmc2_sample_clk.common,
	&mmc2_output_clk.common,
	&spi0_clk.common,
	&spi1_clk.common,
	&i2s0_clk.common,
	&i2s1_clk.common,
	&usb_phy0_clk.common,
	&usb_phy1_clk.common,
	&usb_hsic_clk.common,
	&usb_hsic_12M_clk.common,
	&usb_ohci_clk.common,
	&dram_ve_clk.common,
	&dram_csi_clk.common,
	&dram_drc_clk.common,
	&dram_de_fe_clk.common,
	&dram_de_be_clk.common,
	&de_be_clk.common,
	&de_fe_clk.common,
	&lcd_ch0_clk.common,
	&lcd_ch1_clk.common,
	&csi_sclk_clk.common,
	&csi_mclk_clk.common,
	&ve_clk.common,
	&ac_dig_clk.common,
	&avs_clk.common,
	&mbus_clk.common,
	&dsi_sclk_clk.common,
	&dsi_dphy_clk.common,
	&drc_clk.common,
	&gpu_clk.common,
	&ats_clk.common,
};

/* We hardcode the divider to 1 for now */
static CLK_FIXED_FACTOR(pll_audio_clk, "pll-audio",
			"pll-audio-base", 1, 1, CLK_SET_RATE_PARENT);
static CLK_FIXED_FACTOR(pll_audio_2x_clk, "pll-audio-2x",
			"pll-audio-base", 2, 1, CLK_SET_RATE_PARENT);
static CLK_FIXED_FACTOR(pll_audio_4x_clk, "pll-audio-4x",
			"pll-audio-base", 1, 1, CLK_SET_RATE_PARENT);
static CLK_FIXED_FACTOR(pll_audio_8x_clk, "pll-audio-8x",
			"pll-audio-base", 1, 2, CLK_SET_RATE_PARENT);
static CLK_FIXED_FACTOR(pll_periph_2x_clk, "pll-periph-2x",
			"pll-periph", 1, 2, 0);
static CLK_FIXED_FACTOR(pll_video_2x_clk, "pll-video-2x",
			"pll-video", 1, 2, 0);

static struct clk_hw_onecell_data sun8i_a23_hw_clks = {
	.hws	= {
		[CLK_PLL_CPUX]		= &pll_cpux_clk.common.hw,
		[CLK_PLL_AUDIO_BASE]	= &pll_audio_base_clk.common.hw,
		[CLK_PLL_AUDIO]		= &pll_audio_clk.hw,
		[CLK_PLL_AUDIO_2X]	= &pll_audio_2x_clk.hw,
		[CLK_PLL_AUDIO_4X]	= &pll_audio_4x_clk.hw,
		[CLK_PLL_AUDIO_8X]	= &pll_audio_8x_clk.hw,
		[CLK_PLL_VIDEO]		= &pll_video_clk.common.hw,
		[CLK_PLL_VIDEO_2X]	= &pll_video_2x_clk.hw,
		[CLK_PLL_VE]		= &pll_ve_clk.common.hw,
		[CLK_PLL_DDR0]		= &pll_ddr_clk.common.hw,
		[CLK_PLL_PERIPH]	= &pll_periph_clk.common.hw,
		[CLK_PLL_PERIPH_2X]	= &pll_periph_2x_clk.hw,
		[CLK_PLL_GPU]		= &pll_gpu_clk.common.hw,
		[CLK_PLL_MIPI]		= &pll_mipi_clk.common.hw,
		[CLK_PLL_HSIC]		= &pll_hsic_clk.common.hw,
		[CLK_PLL_DE]		= &pll_de_clk.common.hw,
		[CLK_CPUX]		= &cpux_clk.common.hw,
		[CLK_AXI]		= &axi_clk.common.hw,
		[CLK_AHB1]		= &ahb1_clk.common.hw,
		[CLK_APB1]		= &apb1_clk.common.hw,
		[CLK_APB2]		= &apb2_clk.common.hw,
		[CLK_BUS_MIPI_DSI]	= &bus_mipi_dsi_clk.common.hw,
		[CLK_BUS_DMA]		= &bus_dma_clk.common.hw,
		[CLK_BUS_MMC0]		= &bus_mmc0_clk.common.hw,
		[CLK_BUS_MMC1]		= &bus_mmc1_clk.common.hw,
		[CLK_BUS_MMC2]		= &bus_mmc2_clk.common.hw,
		[CLK_BUS_NAND]		= &bus_nand_clk.common.hw,
		[CLK_BUS_DRAM]		= &bus_dram_clk.common.hw,
		[CLK_BUS_HSTIMER]	= &bus_hstimer_clk.common.hw,
		[CLK_BUS_SPI0]		= &bus_spi0_clk.common.hw,
		[CLK_BUS_SPI1]		= &bus_spi1_clk.common.hw,
		[CLK_BUS_OTG]		= &bus_otg_clk.common.hw,
		[CLK_BUS_EHCI]		= &bus_ehci_clk.common.hw,
		[CLK_BUS_OHCI]		= &bus_ohci_clk.common.hw,
		[CLK_BUS_VE]		= &bus_ve_clk.common.hw,
		[CLK_BUS_LCD]		= &bus_lcd_clk.common.hw,
		[CLK_BUS_CSI]		= &bus_csi_clk.common.hw,
		[CLK_BUS_DE_BE]		= &bus_de_be_clk.common.hw,
		[CLK_BUS_DE_FE]		= &bus_de_fe_clk.common.hw,
		[CLK_BUS_GPU]		= &bus_gpu_clk.common.hw,
		[CLK_BUS_MSGBOX]	= &bus_msgbox_clk.common.hw,
		[CLK_BUS_SPINLOCK]	= &bus_spinlock_clk.common.hw,
		[CLK_BUS_DRC]		= &bus_drc_clk.common.hw,
		[CLK_BUS_CODEC]		= &bus_codec_clk.common.hw,
		[CLK_BUS_PIO]		= &bus_pio_clk.common.hw,
		[CLK_BUS_I2S0]		= &bus_i2s0_clk.common.hw,
		[CLK_BUS_I2S1]		= &bus_i2s1_clk.common.hw,
		[CLK_BUS_I2C0]		= &bus_i2c0_clk.common.hw,
		[CLK_BUS_I2C1]		= &bus_i2c1_clk.common.hw,
		[CLK_BUS_I2C2]		= &bus_i2c2_clk.common.hw,
		[CLK_BUS_UART0]		= &bus_uart0_clk.common.hw,
		[CLK_BUS_UART1]		= &bus_uart1_clk.common.hw,
		[CLK_BUS_UART2]		= &bus_uart2_clk.common.hw,
		[CLK_BUS_UART3]		= &bus_uart3_clk.common.hw,
		[CLK_BUS_UART4]		= &bus_uart4_clk.common.hw,
		[CLK_NAND]		= &nand_clk.common.hw,
		[CLK_MMC0]		= &mmc0_clk.common.hw,
		[CLK_MMC0_SAMPLE]	= &mmc0_sample_clk.common.hw,
		[CLK_MMC0_OUTPUT]	= &mmc0_output_clk.common.hw,
		[CLK_MMC1]		= &mmc1_clk.common.hw,
		[CLK_MMC1_SAMPLE]	= &mmc1_sample_clk.common.hw,
		[CLK_MMC1_OUTPUT]	= &mmc1_output_clk.common.hw,
		[CLK_MMC2]		= &mmc2_clk.common.hw,
		[CLK_MMC2_SAMPLE]	= &mmc2_sample_clk.common.hw,
		[CLK_MMC2_OUTPUT]	= &mmc2_output_clk.common.hw,
		[CLK_SPI0]		= &spi0_clk.common.hw,
		[CLK_SPI1]		= &spi1_clk.common.hw,
		[CLK_I2S0]		= &i2s0_clk.common.hw,
		[CLK_I2S1]		= &i2s1_clk.common.hw,
		[CLK_USB_PHY0]		= &usb_phy0_clk.common.hw,
		[CLK_USB_PHY1]		= &usb_phy1_clk.common.hw,
		[CLK_USB_HSIC]		= &usb_hsic_clk.common.hw,
		[CLK_USB_HSIC_12M]	= &usb_hsic_12M_clk.common.hw,
		[CLK_USB_OHCI]		= &usb_ohci_clk.common.hw,
		[CLK_DRAM_VE]		= &dram_ve_clk.common.hw,
		[CLK_DRAM_CSI]		= &dram_csi_clk.common.hw,
		[CLK_DRAM_DRC]		= &dram_drc_clk.common.hw,
		[CLK_DRAM_DE_FE]	= &dram_de_fe_clk.common.hw,
		[CLK_DRAM_DE_BE]	= &dram_de_be_clk.common.hw,
		[CLK_DE_BE]		= &de_be_clk.common.hw,
		[CLK_DE_FE]		= &de_fe_clk.common.hw,
		[CLK_LCD_CH0]		= &lcd_ch0_clk.common.hw,
		[CLK_LCD_CH1]		= &lcd_ch1_clk.common.hw,
		[CLK_CSI_SCLK]		= &csi_sclk_clk.common.hw,
		[CLK_CSI_MCLK]		= &csi_mclk_clk.common.hw,
		[CLK_VE]		= &ve_clk.common.hw,
		[CLK_AC_DIG]		= &ac_dig_clk.common.hw,
		[CLK_AVS]		= &avs_clk.common.hw,
		[CLK_MBUS]		= &mbus_clk.common.hw,
		[CLK_DSI_SCLK]		= &dsi_sclk_clk.common.hw,
		[CLK_DSI_DPHY]		= &dsi_dphy_clk.common.hw,
		[CLK_DRC]		= &drc_clk.common.hw,
		[CLK_GPU]		= &gpu_clk.common.hw,
		[CLK_ATS]		= &ats_clk.common.hw,
	},
	.num	= CLK_NUMBER,
};

static struct ccu_reset_map sun8i_a23_ccu_resets[] = {
	[RST_USB_PHY0]		=  { 0x0cc, BIT(0) },
	[RST_USB_PHY1]		=  { 0x0cc, BIT(1) },
	[RST_USB_HSIC]		=  { 0x0cc, BIT(2) },

	[RST_MBUS]		=  { 0x0fc, BIT(31) },

	[RST_BUS_MIPI_DSI]	=  { 0x2c0, BIT(1) },
	[RST_BUS_DMA]		=  { 0x2c0, BIT(6) },
	[RST_BUS_MMC0]		=  { 0x2c0, BIT(8) },
	[RST_BUS_MMC1]		=  { 0x2c0, BIT(9) },
	[RST_BUS_MMC2]		=  { 0x2c0, BIT(10) },
	[RST_BUS_NAND]		=  { 0x2c0, BIT(13) },
	[RST_BUS_DRAM]		=  { 0x2c0, BIT(14) },
	[RST_BUS_HSTIMER]	=  { 0x2c0, BIT(19) },
	[RST_BUS_SPI0]		=  { 0x2c0, BIT(20) },
	[RST_BUS_SPI1]		=  { 0x2c0, BIT(21) },
	[RST_BUS_OTG]		=  { 0x2c0, BIT(24) },
	[RST_BUS_EHCI]		=  { 0x2c0, BIT(26) },
	[RST_BUS_OHCI]		=  { 0x2c0, BIT(29) },

	[RST_BUS_VE]		=  { 0x2c4, BIT(0) },
	[RST_BUS_LCD]		=  { 0x2c4, BIT(4) },
	[RST_BUS_CSI]		=  { 0x2c4, BIT(8) },
	[RST_BUS_DE_BE]		=  { 0x2c4, BIT(12) },
	[RST_BUS_DE_FE]		=  { 0x2c4, BIT(14) },
	[RST_BUS_GPU]		=  { 0x2c4, BIT(20) },
	[RST_BUS_MSGBOX]	=  { 0x2c4, BIT(21) },
	[RST_BUS_SPINLOCK]	=  { 0x2c4, BIT(22) },
	[RST_BUS_DRC]		=  { 0x2c4, BIT(25) },

	[RST_BUS_LVDS]		=  { 0x2c8, BIT(0) },

	[RST_BUS_CODEC]		=  { 0x2d0, BIT(0) },
	[RST_BUS_I2S0]		=  { 0x2d0, BIT(12) },
	[RST_BUS_I2S1]		=  { 0x2d0, BIT(13) },

	[RST_BUS_I2C0]		=  { 0x2d8, BIT(0) },
	[RST_BUS_I2C1]		=  { 0x2d8, BIT(1) },
	[RST_BUS_I2C2]		=  { 0x2d8, BIT(2) },
	[RST_BUS_UART0]		=  { 0x2d8, BIT(16) },
	[RST_BUS_UART1]		=  { 0x2d8, BIT(17) },
	[RST_BUS_UART2]		=  { 0x2d8, BIT(18) },
	[RST_BUS_UART3]		=  { 0x2d8, BIT(19) },
	[RST_BUS_UART4]		=  { 0x2d8, BIT(20) },
};

static const struct sunxi_ccu_desc sun8i_a23_ccu_desc = {
	.ccu_clks	= sun8i_a23_ccu_clks,
	.num_ccu_clks	= ARRAY_SIZE(sun8i_a23_ccu_clks),

	.hw_clks	= &sun8i_a23_hw_clks,

	.resets		= sun8i_a23_ccu_resets,
	.num_resets	= ARRAY_SIZE(sun8i_a23_ccu_resets),
};

static void __init sun8i_a23_ccu_setup(struct device_node *node)
{
	void __iomem *reg;
	u32 val;

	reg = of_io_request_and_map(node, 0, of_node_full_name(node));
	if (IS_ERR(reg)) {
		pr_err("%pOF: Could not map the clock registers\n", node);
		return;
	}

	/* Force the PLL-Audio-1x divider to 1 */
	val = readl(reg + SUN8I_A23_PLL_AUDIO_REG);
	val &= ~GENMASK(19, 16);
	writel(val | (0 << 16), reg + SUN8I_A23_PLL_AUDIO_REG);

	/* Force PLL-MIPI to MIPI mode */
	val = readl(reg + SUN8I_A23_PLL_MIPI_REG);
	val &= ~BIT(16);
	writel(val, reg + SUN8I_A23_PLL_MIPI_REG);

	sunxi_ccu_probe(node, reg, &sun8i_a23_ccu_desc);
}
CLK_OF_DECLARE(sun8i_a23_ccu, "allwinner,sun8i-a23-ccu",
	       sun8i_a23_ccu_setup);
