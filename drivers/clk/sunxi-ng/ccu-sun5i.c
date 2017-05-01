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

#include "ccu-sun5i.h"

static struct ccu_nkmp pll_core_clk = {
	.enable		= BIT(31),
	.n		= _SUNXI_CCU_MULT_OFFSET(8, 5, 0),
	.k		= _SUNXI_CCU_MULT(4, 2),
	.m		= _SUNXI_CCU_DIV(0, 2),
	.p		= _SUNXI_CCU_DIV(16, 2),
	.common		= {
		.reg		= 0x000,
		.hw.init	= CLK_HW_INIT("pll-core",
					      "hosc",
					      &ccu_nkmp_ops,
					      0),
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
#define SUN5I_PLL_AUDIO_REG	0x008

static struct ccu_nm pll_audio_base_clk = {
	.enable		= BIT(31),
	.n		= _SUNXI_CCU_MULT_OFFSET(8, 7, 0),

	/*
	 * The datasheet is wrong here, this doesn't have any
	 * offset
	 */
	.m		= _SUNXI_CCU_DIV_OFFSET(0, 5, 0),
	.common		= {
		.reg		= 0x008,
		.hw.init	= CLK_HW_INIT("pll-audio-base",
					      "hosc",
					      &ccu_nm_ops,
					      0),
	},
};

static struct ccu_mult pll_video0_clk = {
	.enable		= BIT(31),
	.mult		= _SUNXI_CCU_MULT_OFFSET_MIN_MAX(0, 7, 0, 9, 127),
	.frac		= _SUNXI_CCU_FRAC(BIT(15), BIT(14),
					  270000000, 297000000),
	.common		= {
		.reg		= 0x010,
		.features	= (CCU_FEATURE_FRACTIONAL |
				   CCU_FEATURE_ALL_PREDIV),
		.prediv		= 8,
		.hw.init	= CLK_HW_INIT("pll-video0",
					      "hosc",
					      &ccu_mult_ops,
					      0),
	},
};

static struct ccu_nkmp pll_ve_clk = {
	.enable		= BIT(31),
	.n		= _SUNXI_CCU_MULT_OFFSET(8, 5, 0),
	.k		= _SUNXI_CCU_MULT(4, 2),
	.m		= _SUNXI_CCU_DIV(0, 2),
	.p		= _SUNXI_CCU_DIV(16, 2),
	.common		= {
		.reg		= 0x018,
		.hw.init	= CLK_HW_INIT("pll-ve",
					      "hosc",
					      &ccu_nkmp_ops,
					      0),
	},
};

static struct ccu_nk pll_ddr_base_clk = {
	.enable		= BIT(31),
	.n		= _SUNXI_CCU_MULT_OFFSET(8, 5, 0),
	.k		= _SUNXI_CCU_MULT(4, 2),
	.common		= {
		.reg		= 0x020,
		.hw.init	= CLK_HW_INIT("pll-ddr-base",
					      "hosc",
					      &ccu_nk_ops,
					      0),
	},
};

static SUNXI_CCU_M(pll_ddr_clk, "pll-ddr", "pll-ddr-base", 0x020, 0, 2,
		   CLK_IS_CRITICAL);

static struct ccu_div pll_ddr_other_clk = {
	.div		= _SUNXI_CCU_DIV_FLAGS(16, 2, CLK_DIVIDER_POWER_OF_TWO),

	.common		= {
		.reg		= 0x020,
		.hw.init	= CLK_HW_INIT("pll-ddr-other", "pll-ddr-base",
					      &ccu_div_ops,
					      0),
	},
};

static struct ccu_nk pll_periph_clk = {
	.enable		= BIT(31),
	.n		= _SUNXI_CCU_MULT_OFFSET(8, 5, 0),
	.k		= _SUNXI_CCU_MULT(4, 2),
	.fixed_post_div	= 2,
	.common		= {
		.reg		= 0x028,
		.features	= CCU_FEATURE_FIXED_POSTDIV,
		.hw.init	= CLK_HW_INIT("pll-periph",
					      "hosc",
					      &ccu_nk_ops,
					      0),
	},
};

static struct ccu_mult pll_video1_clk = {
	.enable		= BIT(31),
	.mult		= _SUNXI_CCU_MULT_OFFSET_MIN_MAX(0, 7, 0, 9, 127),
	.frac		= _SUNXI_CCU_FRAC(BIT(15), BIT(14),
				  270000000, 297000000),
	.common		= {
		.reg		= 0x030,
		.features	= (CCU_FEATURE_FRACTIONAL |
				   CCU_FEATURE_ALL_PREDIV),
		.prediv		= 8,
		.hw.init	= CLK_HW_INIT("pll-video1",
					      "hosc",
					      &ccu_mult_ops,
					      0),
	},
};

static SUNXI_CCU_GATE(hosc_clk,	"hosc",	"osc24M", 0x050, BIT(0), 0);

#define SUN5I_AHB_REG	0x054
static const char * const cpu_parents[] = { "osc32k", "hosc",
					    "pll-core" , "pll-periph" };
static const struct ccu_mux_fixed_prediv cpu_predivs[] = {
	{ .index = 3, .div = 3, },
};
static struct ccu_mux cpu_clk = {
	.mux		= {
		.shift		= 16,
		.width		= 2,
		.fixed_predivs	= cpu_predivs,
		.n_predivs	= ARRAY_SIZE(cpu_predivs),
	},
	.common		= {
		.reg		= 0x054,
		.features	= CCU_FEATURE_FIXED_PREDIV,
		.hw.init	= CLK_HW_INIT_PARENTS("cpu",
						      cpu_parents,
						      &ccu_mux_ops,
						      CLK_IS_CRITICAL),
	}
};

static SUNXI_CCU_M(axi_clk, "axi", "cpu", 0x054, 0, 2, 0);

static const char * const ahb_parents[] = { "axi" , "cpu", "pll-periph" };
static const struct ccu_mux_fixed_prediv ahb_predivs[] = {
	{ .index = 2, .div = 2, },
};
static struct ccu_div ahb_clk = {
	.div		= _SUNXI_CCU_DIV_FLAGS(4, 2, CLK_DIVIDER_POWER_OF_TWO),
	.mux		= {
		.shift		= 6,
		.width		= 2,
		.fixed_predivs	= ahb_predivs,
		.n_predivs	= ARRAY_SIZE(ahb_predivs),
	},

	.common		= {
		.reg		= 0x054,
		.hw.init	= CLK_HW_INIT_PARENTS("ahb",
						      ahb_parents,
						      &ccu_div_ops,
						      0),
	},
};

static struct clk_div_table apb0_div_table[] = {
	{ .val = 0, .div = 2 },
	{ .val = 1, .div = 2 },
	{ .val = 2, .div = 4 },
	{ .val = 3, .div = 8 },
	{ /* Sentinel */ },
};
static SUNXI_CCU_DIV_TABLE(apb0_clk, "apb0", "ahb",
			   0x054, 8, 2, apb0_div_table, 0);

static const char * const apb1_parents[] = { "hosc", "pll-periph", "osc32k" };
static SUNXI_CCU_MP_WITH_MUX(apb1_clk, "apb1", apb1_parents, 0x058,
			     0, 5,	/* M */
			     16, 2,	/* P */
			     24, 2,	/* mux */
			     0);

static SUNXI_CCU_GATE(axi_dram_clk,	"axi-dram",	"axi",
		      0x05c, BIT(0), 0);

static SUNXI_CCU_GATE(ahb_otg_clk,	"ahb-otg",	"ahb",
		      0x060, BIT(0), 0);
static SUNXI_CCU_GATE(ahb_ehci_clk,	"ahb-ehci",	"ahb",
		      0x060, BIT(1), 0);
static SUNXI_CCU_GATE(ahb_ohci_clk,	"ahb-ohci",	"ahb",
		      0x060, BIT(2), 0);
static SUNXI_CCU_GATE(ahb_ss_clk,	"ahb-ss",	"ahb",
		      0x060, BIT(5), 0);
static SUNXI_CCU_GATE(ahb_dma_clk,	"ahb-dma",	"ahb",
		      0x060, BIT(6), 0);
static SUNXI_CCU_GATE(ahb_bist_clk,	"ahb-bist",	"ahb",
		      0x060, BIT(6), 0);
static SUNXI_CCU_GATE(ahb_mmc0_clk,	"ahb-mmc0",	"ahb",
		      0x060, BIT(8), 0);
static SUNXI_CCU_GATE(ahb_mmc1_clk,	"ahb-mmc1",	"ahb",
		      0x060, BIT(9), 0);
static SUNXI_CCU_GATE(ahb_mmc2_clk,	"ahb-mmc2",	"ahb",
		      0x060, BIT(10), 0);
static SUNXI_CCU_GATE(ahb_nand_clk,	"ahb-nand",	"ahb",
		      0x060, BIT(13), 0);
static SUNXI_CCU_GATE(ahb_sdram_clk,	"ahb-sdram",	"ahb",
		      0x060, BIT(14), CLK_IS_CRITICAL);
static SUNXI_CCU_GATE(ahb_emac_clk,	"ahb-emac",	"ahb",
		      0x060, BIT(17), 0);
static SUNXI_CCU_GATE(ahb_ts_clk,	"ahb-ts",	"ahb",
		      0x060, BIT(18), 0);
static SUNXI_CCU_GATE(ahb_spi0_clk,	"ahb-spi0",	"ahb",
		      0x060, BIT(20), 0);
static SUNXI_CCU_GATE(ahb_spi1_clk,	"ahb-spi1",	"ahb",
		      0x060, BIT(21), 0);
static SUNXI_CCU_GATE(ahb_spi2_clk,	"ahb-spi2",	"ahb",
		      0x060, BIT(22), 0);
static SUNXI_CCU_GATE(ahb_gps_clk,	"ahb-gps",	"ahb",
		      0x060, BIT(26), 0);
static SUNXI_CCU_GATE(ahb_hstimer_clk,	"ahb-hstimer",	"ahb",
		      0x060, BIT(28), 0);

static SUNXI_CCU_GATE(ahb_ve_clk,	"ahb-ve",	"ahb",
		      0x064, BIT(0), 0);
static SUNXI_CCU_GATE(ahb_tve_clk,	"ahb-tve",	"ahb",
		      0x064, BIT(2), 0);
static SUNXI_CCU_GATE(ahb_lcd_clk,	"ahb-lcd",	"ahb",
		      0x064, BIT(4), 0);
static SUNXI_CCU_GATE(ahb_csi_clk,	"ahb-csi",	"ahb",
		      0x064, BIT(8), 0);
static SUNXI_CCU_GATE(ahb_hdmi_clk,	"ahb-hdmi",	"ahb",
		      0x064, BIT(11), 0);
static SUNXI_CCU_GATE(ahb_de_be_clk,	"ahb-de-be",	"ahb",
		      0x064, BIT(12), 0);
static SUNXI_CCU_GATE(ahb_de_fe_clk,	"ahb-de-fe",	"ahb",
		      0x064, BIT(14), 0);
static SUNXI_CCU_GATE(ahb_iep_clk,	"ahb-iep",	"ahb",
		      0x064, BIT(19), 0);
static SUNXI_CCU_GATE(ahb_gpu_clk,	"ahb-gpu",	"ahb",
		      0x064, BIT(20), 0);

static SUNXI_CCU_GATE(apb0_codec_clk,	"apb0-codec",	"apb0",
		      0x068, BIT(0), 0);
static SUNXI_CCU_GATE(apb0_spdif_clk,	"apb0-spdif",	"apb0",
		      0x068, BIT(1), 0);
static SUNXI_CCU_GATE(apb0_i2s_clk,	"apb0-i2s",	"apb0",
		      0x068, BIT(3), 0);
static SUNXI_CCU_GATE(apb0_pio_clk,	"apb0-pio",	"apb0",
		      0x068, BIT(5), 0);
static SUNXI_CCU_GATE(apb0_ir_clk,	"apb0-ir",	"apb0",
		      0x068, BIT(6), 0);
static SUNXI_CCU_GATE(apb0_keypad_clk,	"apb0-keypad",	"apb0",
		      0x068, BIT(10), 0);

static SUNXI_CCU_GATE(apb1_i2c0_clk,	"apb1-i2c0",	"apb1",
		      0x06c, BIT(0), 0);
static SUNXI_CCU_GATE(apb1_i2c1_clk,	"apb1-i2c1",	"apb1",
		      0x06c, BIT(1), 0);
static SUNXI_CCU_GATE(apb1_i2c2_clk,	"apb1-i2c2",	"apb1",
		      0x06c, BIT(2), 0);
static SUNXI_CCU_GATE(apb1_uart0_clk,	"apb1-uart0",	"apb1",
		      0x06c, BIT(16), 0);
static SUNXI_CCU_GATE(apb1_uart1_clk,	"apb1-uart1",	"apb1",
		      0x06c, BIT(17), 0);
static SUNXI_CCU_GATE(apb1_uart2_clk,	"apb1-uart2",	"apb1",
		      0x06c, BIT(18), 0);
static SUNXI_CCU_GATE(apb1_uart3_clk,	"apb1-uart3",	"apb1",
		      0x06c, BIT(19), 0);

static const char * const mod0_default_parents[] = { "hosc", "pll-periph",
						     "pll-ddr-other" };
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

static SUNXI_CCU_MP_WITH_MUX_GATE(ts_clk, "ts", mod0_default_parents, 0x098,
				  0, 4,		/* M */
				  16, 2,	/* P */
				  24, 2,	/* mux */
				  BIT(31),	/* gate */
				  0);

static SUNXI_CCU_MP_WITH_MUX_GATE(ss_clk, "ss", mod0_default_parents, 0x09c,
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

static SUNXI_CCU_MP_WITH_MUX_GATE(ir_clk, "ir", mod0_default_parents, 0x0b0,
				  0, 4,		/* M */
				  16, 2,	/* P */
				  24, 2,	/* mux */
				  BIT(31),	/* gate */
				  0);

static const char * const i2s_parents[] = { "pll-audio-8x", "pll-audio-4x",
					    "pll-audio-2x", "pll-audio" };
static SUNXI_CCU_MUX_WITH_GATE(i2s_clk, "i2s", i2s_parents,
			       0x0b8, 16, 2, BIT(31), CLK_SET_RATE_PARENT);

static const char * const spdif_parents[] = { "pll-audio-8x", "pll-audio-4x",
					    "pll-audio-2x", "pll-audio" };
static SUNXI_CCU_MUX_WITH_GATE(spdif_clk, "spdif", spdif_parents,
			       0x0c0, 16, 2, BIT(31), CLK_SET_RATE_PARENT);

static const char * const keypad_parents[] = { "hosc", "losc"};
static const u8 keypad_table[] = { 0, 2 };
static struct ccu_mp keypad_clk = {
	.enable		= BIT(31),
	.m		= _SUNXI_CCU_DIV(8, 5),
	.p		= _SUNXI_CCU_DIV(20, 2),
	.mux		= _SUNXI_CCU_MUX_TABLE(24, 2, keypad_table),

	.common		= {
		.reg		= 0x0c4,
		.hw.init	= CLK_HW_INIT_PARENTS("keypad",
						      keypad_parents,
						      &ccu_mp_ops,
						      0),
	},
};

static SUNXI_CCU_GATE(usb_ohci_clk,	"usb-ohci",	"pll-periph",
		      0x0cc, BIT(6), 0);
static SUNXI_CCU_GATE(usb_phy0_clk,	"usb-phy0",	"pll-periph",
		      0x0cc, BIT(8), 0);
static SUNXI_CCU_GATE(usb_phy1_clk,	"usb-phy1",	"pll-periph",
		      0x0cc, BIT(9), 0);

static const char * const gps_parents[] = { "hosc", "pll-periph",
					    "pll-video1", "pll-ve" };
static SUNXI_CCU_M_WITH_MUX_GATE(gps_clk, "gps", gps_parents,
				 0x0d0, 0, 3, 24, 2, BIT(31), 0);

static SUNXI_CCU_GATE(dram_ve_clk,	"dram-ve",	"pll-ddr",
		      0x100, BIT(0), 0);
static SUNXI_CCU_GATE(dram_csi_clk,	"dram-csi",	"pll-ddr",
		      0x100, BIT(1), 0);
static SUNXI_CCU_GATE(dram_ts_clk,	"dram-ts",	"pll-ddr",
		      0x100, BIT(3), 0);
static SUNXI_CCU_GATE(dram_tve_clk,	"dram-tve",	"pll-ddr",
		      0x100, BIT(5), 0);
static SUNXI_CCU_GATE(dram_de_fe_clk,	"dram-de-fe",	"pll-ddr",
		      0x100, BIT(25), 0);
static SUNXI_CCU_GATE(dram_de_be_clk,	"dram-de-be",	"pll-ddr",
		      0x100, BIT(26), 0);
static SUNXI_CCU_GATE(dram_ace_clk,	"dram-ace",	"pll-ddr",
		      0x100, BIT(29), 0);
static SUNXI_CCU_GATE(dram_iep_clk,	"dram-iep",	"pll-ddr",
		      0x100, BIT(31), 0);

static const char * const de_parents[] = { "pll-video0", "pll-video1",
					   "pll-ddr-other" };
static SUNXI_CCU_M_WITH_MUX_GATE(de_be_clk, "de-be", de_parents,
				 0x104, 0, 4, 24, 2, BIT(31), 0);

static SUNXI_CCU_M_WITH_MUX_GATE(de_fe_clk, "de-fe", de_parents,
				 0x10c, 0, 4, 24, 2, BIT(31), 0);

static const char * const tcon_parents[] = { "pll-video0", "pll-video1",
					     "pll-video0-2x", "pll-video1-2x" };
static SUNXI_CCU_MUX_WITH_GATE(tcon_ch0_clk, "tcon-ch0-sclk", tcon_parents,
			       0x118, 24, 2, BIT(31), CLK_SET_RATE_PARENT);

static SUNXI_CCU_M_WITH_MUX_GATE(tcon_ch1_sclk2_clk, "tcon-ch1-sclk2",
				 tcon_parents,
				 0x12c, 0, 4, 24, 2, BIT(31), CLK_SET_RATE_PARENT);

static SUNXI_CCU_M_WITH_GATE(tcon_ch1_sclk1_clk, "tcon-ch1-sclk1", "tcon-ch1-sclk2",
			     0x12c, 11, 1, BIT(15), CLK_SET_RATE_PARENT);

static const char * const csi_parents[] = { "hosc", "pll-video0", "pll-video1",
					    "pll-video0-2x", "pll-video1-2x" };
static const u8 csi_table[] = { 0, 1, 2, 5, 6 };
static SUNXI_CCU_M_WITH_MUX_TABLE_GATE(csi_clk, "csi",
				       csi_parents, csi_table,
				       0x134, 0, 5, 24, 2, BIT(31), 0);

static SUNXI_CCU_GATE(ve_clk,		"ve",		"pll-ve",
		      0x13c, BIT(31), CLK_SET_RATE_PARENT);

static SUNXI_CCU_GATE(codec_clk,	"codec",	"pll-audio",
		      0x140, BIT(31), CLK_SET_RATE_PARENT);

static SUNXI_CCU_GATE(avs_clk,		"avs",		"hosc",
		      0x144, BIT(31), 0);

static const char * const hdmi_parents[] = { "pll-video0", "pll-video0-2x" };
static const u8 hdmi_table[] = { 0, 2 };
static SUNXI_CCU_M_WITH_MUX_TABLE_GATE(hdmi_clk, "hdmi",
				       hdmi_parents, hdmi_table,
				       0x150, 0, 4, 24, 2, BIT(31),
				       CLK_SET_RATE_PARENT);

static const char * const gpu_parents[] = { "pll-video0", "pll-ve",
					    "pll-ddr-other", "pll-video1",
					    "pll-video1-2x" };
static SUNXI_CCU_M_WITH_MUX_GATE(gpu_clk, "gpu", gpu_parents,
				 0x154, 0, 4, 24, 3, BIT(31), 0);

static const char * const mbus_parents[] = { "hosc", "pll-periph", "pll-ddr" };
static SUNXI_CCU_MP_WITH_MUX_GATE(mbus_clk, "mbus", mbus_parents,
				  0x15c, 0, 4, 16, 2, 24, 2, BIT(31), CLK_IS_CRITICAL);

static SUNXI_CCU_GATE(iep_clk,		"iep",		"de-be",
		      0x160, BIT(31), 0);

static struct ccu_common *sun5i_a10s_ccu_clks[] = {
	&hosc_clk.common,
	&pll_core_clk.common,
	&pll_audio_base_clk.common,
	&pll_video0_clk.common,
	&pll_ve_clk.common,
	&pll_ddr_base_clk.common,
	&pll_ddr_clk.common,
	&pll_ddr_other_clk.common,
	&pll_periph_clk.common,
	&pll_video1_clk.common,
	&cpu_clk.common,
	&axi_clk.common,
	&ahb_clk.common,
	&apb0_clk.common,
	&apb1_clk.common,
	&axi_dram_clk.common,
	&ahb_otg_clk.common,
	&ahb_ehci_clk.common,
	&ahb_ohci_clk.common,
	&ahb_ss_clk.common,
	&ahb_dma_clk.common,
	&ahb_bist_clk.common,
	&ahb_mmc0_clk.common,
	&ahb_mmc1_clk.common,
	&ahb_mmc2_clk.common,
	&ahb_nand_clk.common,
	&ahb_sdram_clk.common,
	&ahb_emac_clk.common,
	&ahb_ts_clk.common,
	&ahb_spi0_clk.common,
	&ahb_spi1_clk.common,
	&ahb_spi2_clk.common,
	&ahb_gps_clk.common,
	&ahb_hstimer_clk.common,
	&ahb_ve_clk.common,
	&ahb_tve_clk.common,
	&ahb_lcd_clk.common,
	&ahb_csi_clk.common,
	&ahb_hdmi_clk.common,
	&ahb_de_be_clk.common,
	&ahb_de_fe_clk.common,
	&ahb_iep_clk.common,
	&ahb_gpu_clk.common,
	&apb0_codec_clk.common,
	&apb0_spdif_clk.common,
	&apb0_i2s_clk.common,
	&apb0_pio_clk.common,
	&apb0_ir_clk.common,
	&apb0_keypad_clk.common,
	&apb1_i2c0_clk.common,
	&apb1_i2c1_clk.common,
	&apb1_i2c2_clk.common,
	&apb1_uart0_clk.common,
	&apb1_uart1_clk.common,
	&apb1_uart2_clk.common,
	&apb1_uart3_clk.common,
	&nand_clk.common,
	&mmc0_clk.common,
	&mmc1_clk.common,
	&mmc2_clk.common,
	&ts_clk.common,
	&ss_clk.common,
	&spi0_clk.common,
	&spi1_clk.common,
	&spi2_clk.common,
	&ir_clk.common,
	&i2s_clk.common,
	&spdif_clk.common,
	&keypad_clk.common,
	&usb_ohci_clk.common,
	&usb_phy0_clk.common,
	&usb_phy1_clk.common,
	&gps_clk.common,
	&dram_ve_clk.common,
	&dram_csi_clk.common,
	&dram_ts_clk.common,
	&dram_tve_clk.common,
	&dram_de_fe_clk.common,
	&dram_de_be_clk.common,
	&dram_ace_clk.common,
	&dram_iep_clk.common,
	&de_be_clk.common,
	&de_fe_clk.common,
	&tcon_ch0_clk.common,
	&tcon_ch1_sclk2_clk.common,
	&tcon_ch1_sclk1_clk.common,
	&csi_clk.common,
	&ve_clk.common,
	&codec_clk.common,
	&avs_clk.common,
	&hdmi_clk.common,
	&gpu_clk.common,
	&mbus_clk.common,
	&iep_clk.common,
};

/* We hardcode the divider to 4 for now */
static CLK_FIXED_FACTOR(pll_audio_clk, "pll-audio",
			"pll-audio-base", 4, 1, CLK_SET_RATE_PARENT);
static CLK_FIXED_FACTOR(pll_audio_2x_clk, "pll-audio-2x",
			"pll-audio-base", 2, 1, CLK_SET_RATE_PARENT);
static CLK_FIXED_FACTOR(pll_audio_4x_clk, "pll-audio-4x",
			"pll-audio-base", 1, 1, CLK_SET_RATE_PARENT);
static CLK_FIXED_FACTOR(pll_audio_8x_clk, "pll-audio-8x",
			"pll-audio-base", 1, 2, CLK_SET_RATE_PARENT);
static CLK_FIXED_FACTOR(pll_video0_2x_clk, "pll-video0-2x",
			"pll-video0", 1, 2, CLK_SET_RATE_PARENT);
static CLK_FIXED_FACTOR(pll_video1_2x_clk, "pll-video1-2x",
			"pll-video1", 1, 2, CLK_SET_RATE_PARENT);

static struct clk_hw_onecell_data sun5i_a10s_hw_clks = {
	.hws	= {
		[CLK_HOSC]		= &hosc_clk.common.hw,
		[CLK_PLL_CORE]		= &pll_core_clk.common.hw,
		[CLK_PLL_AUDIO_BASE]	= &pll_audio_base_clk.common.hw,
		[CLK_PLL_AUDIO]		= &pll_audio_clk.hw,
		[CLK_PLL_AUDIO_2X]	= &pll_audio_2x_clk.hw,
		[CLK_PLL_AUDIO_4X]	= &pll_audio_4x_clk.hw,
		[CLK_PLL_AUDIO_8X]	= &pll_audio_8x_clk.hw,
		[CLK_PLL_VIDEO0]	= &pll_video0_clk.common.hw,
		[CLK_PLL_VIDEO0_2X]	= &pll_video0_2x_clk.hw,
		[CLK_PLL_VE]		= &pll_ve_clk.common.hw,
		[CLK_PLL_DDR_BASE]	= &pll_ddr_base_clk.common.hw,
		[CLK_PLL_DDR]		= &pll_ddr_clk.common.hw,
		[CLK_PLL_DDR_OTHER]	= &pll_ddr_other_clk.common.hw,
		[CLK_PLL_PERIPH]	= &pll_periph_clk.common.hw,
		[CLK_PLL_VIDEO1]	= &pll_video1_clk.common.hw,
		[CLK_PLL_VIDEO1_2X]	= &pll_video1_2x_clk.hw,
		[CLK_CPU]		= &cpu_clk.common.hw,
		[CLK_AXI]		= &axi_clk.common.hw,
		[CLK_AHB]		= &ahb_clk.common.hw,
		[CLK_APB0]		= &apb0_clk.common.hw,
		[CLK_APB1]		= &apb1_clk.common.hw,
		[CLK_DRAM_AXI]		= &axi_dram_clk.common.hw,
		[CLK_AHB_OTG]		= &ahb_otg_clk.common.hw,
		[CLK_AHB_EHCI]		= &ahb_ehci_clk.common.hw,
		[CLK_AHB_OHCI]		= &ahb_ohci_clk.common.hw,
		[CLK_AHB_SS]		= &ahb_ss_clk.common.hw,
		[CLK_AHB_DMA]		= &ahb_dma_clk.common.hw,
		[CLK_AHB_BIST]		= &ahb_bist_clk.common.hw,
		[CLK_AHB_MMC0]		= &ahb_mmc0_clk.common.hw,
		[CLK_AHB_MMC1]		= &ahb_mmc1_clk.common.hw,
		[CLK_AHB_MMC2]		= &ahb_mmc2_clk.common.hw,
		[CLK_AHB_NAND]		= &ahb_nand_clk.common.hw,
		[CLK_AHB_SDRAM]		= &ahb_sdram_clk.common.hw,
		[CLK_AHB_EMAC]		= &ahb_emac_clk.common.hw,
		[CLK_AHB_TS]		= &ahb_ts_clk.common.hw,
		[CLK_AHB_SPI0]		= &ahb_spi0_clk.common.hw,
		[CLK_AHB_SPI1]		= &ahb_spi1_clk.common.hw,
		[CLK_AHB_SPI2]		= &ahb_spi2_clk.common.hw,
		[CLK_AHB_GPS]		= &ahb_gps_clk.common.hw,
		[CLK_AHB_HSTIMER]	= &ahb_hstimer_clk.common.hw,
		[CLK_AHB_VE]		= &ahb_ve_clk.common.hw,
		[CLK_AHB_TVE]		= &ahb_tve_clk.common.hw,
		[CLK_AHB_LCD]		= &ahb_lcd_clk.common.hw,
		[CLK_AHB_CSI]		= &ahb_csi_clk.common.hw,
		[CLK_AHB_HDMI]		= &ahb_hdmi_clk.common.hw,
		[CLK_AHB_DE_BE]		= &ahb_de_be_clk.common.hw,
		[CLK_AHB_DE_FE]		= &ahb_de_fe_clk.common.hw,
		[CLK_AHB_IEP]		= &ahb_iep_clk.common.hw,
		[CLK_AHB_GPU]		= &ahb_gpu_clk.common.hw,
		[CLK_APB0_CODEC]	= &apb0_codec_clk.common.hw,
		[CLK_APB0_I2S]		= &apb0_i2s_clk.common.hw,
		[CLK_APB0_PIO]		= &apb0_pio_clk.common.hw,
		[CLK_APB0_IR]		= &apb0_ir_clk.common.hw,
		[CLK_APB0_KEYPAD]	= &apb0_keypad_clk.common.hw,
		[CLK_APB1_I2C0]		= &apb1_i2c0_clk.common.hw,
		[CLK_APB1_I2C1]		= &apb1_i2c1_clk.common.hw,
		[CLK_APB1_I2C2]		= &apb1_i2c2_clk.common.hw,
		[CLK_APB1_UART0]	= &apb1_uart0_clk.common.hw,
		[CLK_APB1_UART1]	= &apb1_uart1_clk.common.hw,
		[CLK_APB1_UART2]	= &apb1_uart2_clk.common.hw,
		[CLK_APB1_UART3]	= &apb1_uart3_clk.common.hw,
		[CLK_NAND]		= &nand_clk.common.hw,
		[CLK_MMC0]		= &mmc0_clk.common.hw,
		[CLK_MMC1]		= &mmc1_clk.common.hw,
		[CLK_MMC2]		= &mmc2_clk.common.hw,
		[CLK_TS]		= &ts_clk.common.hw,
		[CLK_SS]		= &ss_clk.common.hw,
		[CLK_SPI0]		= &spi0_clk.common.hw,
		[CLK_SPI1]		= &spi1_clk.common.hw,
		[CLK_SPI2]		= &spi2_clk.common.hw,
		[CLK_IR]		= &ir_clk.common.hw,
		[CLK_I2S]		= &i2s_clk.common.hw,
		[CLK_KEYPAD]		= &keypad_clk.common.hw,
		[CLK_USB_OHCI]		= &usb_ohci_clk.common.hw,
		[CLK_USB_PHY0]		= &usb_phy0_clk.common.hw,
		[CLK_USB_PHY1]		= &usb_phy1_clk.common.hw,
		[CLK_GPS]		= &gps_clk.common.hw,
		[CLK_DRAM_VE]		= &dram_ve_clk.common.hw,
		[CLK_DRAM_CSI]		= &dram_csi_clk.common.hw,
		[CLK_DRAM_TS]		= &dram_ts_clk.common.hw,
		[CLK_DRAM_TVE]		= &dram_tve_clk.common.hw,
		[CLK_DRAM_DE_FE]	= &dram_de_fe_clk.common.hw,
		[CLK_DRAM_DE_BE]	= &dram_de_be_clk.common.hw,
		[CLK_DRAM_ACE]		= &dram_ace_clk.common.hw,
		[CLK_DRAM_IEP]		= &dram_iep_clk.common.hw,
		[CLK_DE_BE]		= &de_be_clk.common.hw,
		[CLK_DE_FE]		= &de_fe_clk.common.hw,
		[CLK_TCON_CH0]		= &tcon_ch0_clk.common.hw,
		[CLK_TCON_CH1_SCLK]	= &tcon_ch1_sclk2_clk.common.hw,
		[CLK_TCON_CH1]		= &tcon_ch1_sclk1_clk.common.hw,
		[CLK_CSI]		= &csi_clk.common.hw,
		[CLK_VE]		= &ve_clk.common.hw,
		[CLK_CODEC]		= &codec_clk.common.hw,
		[CLK_AVS]		= &avs_clk.common.hw,
		[CLK_HDMI]		= &hdmi_clk.common.hw,
		[CLK_GPU]		= &gpu_clk.common.hw,
		[CLK_MBUS]		= &mbus_clk.common.hw,
		[CLK_IEP]		= &iep_clk.common.hw,
	},
	.num	= CLK_NUMBER,
};

static struct ccu_reset_map sun5i_a10s_ccu_resets[] = {
	[RST_USB_PHY0]		=  { 0x0cc, BIT(0) },
	[RST_USB_PHY1]		=  { 0x0cc, BIT(1) },

	[RST_GPS]		=  { 0x0d0, BIT(30) },

	[RST_DE_BE]		=  { 0x104, BIT(30) },

	[RST_DE_FE]		=  { 0x10c, BIT(30) },

	[RST_TVE]		=  { 0x118, BIT(29) },
	[RST_LCD]		=  { 0x118, BIT(30) },

	[RST_CSI]		=  { 0x134, BIT(30) },

	[RST_VE]		=  { 0x13c, BIT(0) },

	[RST_GPU]		=  { 0x154, BIT(30) },

	[RST_IEP]		=  { 0x160, BIT(30) },
};

static const struct sunxi_ccu_desc sun5i_a10s_ccu_desc = {
	.ccu_clks	= sun5i_a10s_ccu_clks,
	.num_ccu_clks	= ARRAY_SIZE(sun5i_a10s_ccu_clks),

	.hw_clks	= &sun5i_a10s_hw_clks,

	.resets		= sun5i_a10s_ccu_resets,
	.num_resets	= ARRAY_SIZE(sun5i_a10s_ccu_resets),
};

/*
 * The A13 is the A10s minus the TS, GPS, HDMI, I2S and the keypad
 */
static struct clk_hw_onecell_data sun5i_a13_hw_clks = {
	.hws	= {
		[CLK_HOSC]		= &hosc_clk.common.hw,
		[CLK_PLL_CORE]		= &pll_core_clk.common.hw,
		[CLK_PLL_AUDIO_BASE]	= &pll_audio_base_clk.common.hw,
		[CLK_PLL_AUDIO]		= &pll_audio_clk.hw,
		[CLK_PLL_AUDIO_2X]	= &pll_audio_2x_clk.hw,
		[CLK_PLL_AUDIO_4X]	= &pll_audio_4x_clk.hw,
		[CLK_PLL_AUDIO_8X]	= &pll_audio_8x_clk.hw,
		[CLK_PLL_VIDEO0]	= &pll_video0_clk.common.hw,
		[CLK_PLL_VIDEO0_2X]	= &pll_video0_2x_clk.hw,
		[CLK_PLL_VE]		= &pll_ve_clk.common.hw,
		[CLK_PLL_DDR_BASE]	= &pll_ddr_base_clk.common.hw,
		[CLK_PLL_DDR]		= &pll_ddr_clk.common.hw,
		[CLK_PLL_DDR_OTHER]	= &pll_ddr_other_clk.common.hw,
		[CLK_PLL_PERIPH]	= &pll_periph_clk.common.hw,
		[CLK_PLL_VIDEO1]	= &pll_video1_clk.common.hw,
		[CLK_PLL_VIDEO1_2X]	= &pll_video1_2x_clk.hw,
		[CLK_CPU]		= &cpu_clk.common.hw,
		[CLK_AXI]		= &axi_clk.common.hw,
		[CLK_AHB]		= &ahb_clk.common.hw,
		[CLK_APB0]		= &apb0_clk.common.hw,
		[CLK_APB1]		= &apb1_clk.common.hw,
		[CLK_DRAM_AXI]		= &axi_dram_clk.common.hw,
		[CLK_AHB_OTG]		= &ahb_otg_clk.common.hw,
		[CLK_AHB_EHCI]		= &ahb_ehci_clk.common.hw,
		[CLK_AHB_OHCI]		= &ahb_ohci_clk.common.hw,
		[CLK_AHB_SS]		= &ahb_ss_clk.common.hw,
		[CLK_AHB_DMA]		= &ahb_dma_clk.common.hw,
		[CLK_AHB_BIST]		= &ahb_bist_clk.common.hw,
		[CLK_AHB_MMC0]		= &ahb_mmc0_clk.common.hw,
		[CLK_AHB_MMC1]		= &ahb_mmc1_clk.common.hw,
		[CLK_AHB_MMC2]		= &ahb_mmc2_clk.common.hw,
		[CLK_AHB_NAND]		= &ahb_nand_clk.common.hw,
		[CLK_AHB_SDRAM]		= &ahb_sdram_clk.common.hw,
		[CLK_AHB_EMAC]		= &ahb_emac_clk.common.hw,
		[CLK_AHB_SPI0]		= &ahb_spi0_clk.common.hw,
		[CLK_AHB_SPI1]		= &ahb_spi1_clk.common.hw,
		[CLK_AHB_SPI2]		= &ahb_spi2_clk.common.hw,
		[CLK_AHB_HSTIMER]	= &ahb_hstimer_clk.common.hw,
		[CLK_AHB_VE]		= &ahb_ve_clk.common.hw,
		[CLK_AHB_TVE]		= &ahb_tve_clk.common.hw,
		[CLK_AHB_LCD]		= &ahb_lcd_clk.common.hw,
		[CLK_AHB_CSI]		= &ahb_csi_clk.common.hw,
		[CLK_AHB_DE_BE]		= &ahb_de_be_clk.common.hw,
		[CLK_AHB_DE_FE]		= &ahb_de_fe_clk.common.hw,
		[CLK_AHB_IEP]		= &ahb_iep_clk.common.hw,
		[CLK_AHB_GPU]		= &ahb_gpu_clk.common.hw,
		[CLK_APB0_CODEC]	= &apb0_codec_clk.common.hw,
		[CLK_APB0_PIO]		= &apb0_pio_clk.common.hw,
		[CLK_APB0_IR]		= &apb0_ir_clk.common.hw,
		[CLK_APB1_I2C0]		= &apb1_i2c0_clk.common.hw,
		[CLK_APB1_I2C1]		= &apb1_i2c1_clk.common.hw,
		[CLK_APB1_I2C2]		= &apb1_i2c2_clk.common.hw,
		[CLK_APB1_UART0]	= &apb1_uart0_clk.common.hw,
		[CLK_APB1_UART1]	= &apb1_uart1_clk.common.hw,
		[CLK_APB1_UART2]	= &apb1_uart2_clk.common.hw,
		[CLK_APB1_UART3]	= &apb1_uart3_clk.common.hw,
		[CLK_NAND]		= &nand_clk.common.hw,
		[CLK_MMC0]		= &mmc0_clk.common.hw,
		[CLK_MMC1]		= &mmc1_clk.common.hw,
		[CLK_MMC2]		= &mmc2_clk.common.hw,
		[CLK_SS]		= &ss_clk.common.hw,
		[CLK_SPI0]		= &spi0_clk.common.hw,
		[CLK_SPI1]		= &spi1_clk.common.hw,
		[CLK_SPI2]		= &spi2_clk.common.hw,
		[CLK_IR]		= &ir_clk.common.hw,
		[CLK_USB_OHCI]		= &usb_ohci_clk.common.hw,
		[CLK_USB_PHY0]		= &usb_phy0_clk.common.hw,
		[CLK_USB_PHY1]		= &usb_phy1_clk.common.hw,
		[CLK_DRAM_VE]		= &dram_ve_clk.common.hw,
		[CLK_DRAM_CSI]		= &dram_csi_clk.common.hw,
		[CLK_DRAM_TVE]		= &dram_tve_clk.common.hw,
		[CLK_DRAM_DE_FE]	= &dram_de_fe_clk.common.hw,
		[CLK_DRAM_DE_BE]	= &dram_de_be_clk.common.hw,
		[CLK_DRAM_ACE]		= &dram_ace_clk.common.hw,
		[CLK_DRAM_IEP]		= &dram_iep_clk.common.hw,
		[CLK_DE_BE]		= &de_be_clk.common.hw,
		[CLK_DE_FE]		= &de_fe_clk.common.hw,
		[CLK_TCON_CH0]		= &tcon_ch0_clk.common.hw,
		[CLK_TCON_CH1_SCLK]	= &tcon_ch1_sclk2_clk.common.hw,
		[CLK_TCON_CH1]		= &tcon_ch1_sclk1_clk.common.hw,
		[CLK_CSI]		= &csi_clk.common.hw,
		[CLK_VE]		= &ve_clk.common.hw,
		[CLK_CODEC]		= &codec_clk.common.hw,
		[CLK_AVS]		= &avs_clk.common.hw,
		[CLK_GPU]		= &gpu_clk.common.hw,
		[CLK_MBUS]		= &mbus_clk.common.hw,
		[CLK_IEP]		= &iep_clk.common.hw,
	},
	.num	= CLK_NUMBER,
};

static const struct sunxi_ccu_desc sun5i_a13_ccu_desc = {
	.ccu_clks	= sun5i_a10s_ccu_clks,
	.num_ccu_clks	= ARRAY_SIZE(sun5i_a10s_ccu_clks),

	.hw_clks	= &sun5i_a13_hw_clks,

	.resets		= sun5i_a10s_ccu_resets,
	.num_resets	= ARRAY_SIZE(sun5i_a10s_ccu_resets),
};

/*
 * The GR8 is the A10s CCU minus the HDMI and keypad, plus SPDIF
 */
static struct clk_hw_onecell_data sun5i_gr8_hw_clks = {
	.hws	= {
		[CLK_HOSC]		= &hosc_clk.common.hw,
		[CLK_PLL_CORE]		= &pll_core_clk.common.hw,
		[CLK_PLL_AUDIO_BASE]	= &pll_audio_base_clk.common.hw,
		[CLK_PLL_AUDIO]		= &pll_audio_clk.hw,
		[CLK_PLL_AUDIO_2X]	= &pll_audio_2x_clk.hw,
		[CLK_PLL_AUDIO_4X]	= &pll_audio_4x_clk.hw,
		[CLK_PLL_AUDIO_8X]	= &pll_audio_8x_clk.hw,
		[CLK_PLL_VIDEO0]	= &pll_video0_clk.common.hw,
		[CLK_PLL_VIDEO0_2X]	= &pll_video0_2x_clk.hw,
		[CLK_PLL_VE]		= &pll_ve_clk.common.hw,
		[CLK_PLL_DDR_BASE]	= &pll_ddr_base_clk.common.hw,
		[CLK_PLL_DDR]		= &pll_ddr_clk.common.hw,
		[CLK_PLL_DDR_OTHER]	= &pll_ddr_other_clk.common.hw,
		[CLK_PLL_PERIPH]	= &pll_periph_clk.common.hw,
		[CLK_PLL_VIDEO1]	= &pll_video1_clk.common.hw,
		[CLK_PLL_VIDEO1_2X]	= &pll_video1_2x_clk.hw,
		[CLK_CPU]		= &cpu_clk.common.hw,
		[CLK_AXI]		= &axi_clk.common.hw,
		[CLK_AHB]		= &ahb_clk.common.hw,
		[CLK_APB0]		= &apb0_clk.common.hw,
		[CLK_APB1]		= &apb1_clk.common.hw,
		[CLK_DRAM_AXI]		= &axi_dram_clk.common.hw,
		[CLK_AHB_OTG]		= &ahb_otg_clk.common.hw,
		[CLK_AHB_EHCI]		= &ahb_ehci_clk.common.hw,
		[CLK_AHB_OHCI]		= &ahb_ohci_clk.common.hw,
		[CLK_AHB_SS]		= &ahb_ss_clk.common.hw,
		[CLK_AHB_DMA]		= &ahb_dma_clk.common.hw,
		[CLK_AHB_BIST]		= &ahb_bist_clk.common.hw,
		[CLK_AHB_MMC0]		= &ahb_mmc0_clk.common.hw,
		[CLK_AHB_MMC1]		= &ahb_mmc1_clk.common.hw,
		[CLK_AHB_MMC2]		= &ahb_mmc2_clk.common.hw,
		[CLK_AHB_NAND]		= &ahb_nand_clk.common.hw,
		[CLK_AHB_SDRAM]		= &ahb_sdram_clk.common.hw,
		[CLK_AHB_EMAC]		= &ahb_emac_clk.common.hw,
		[CLK_AHB_TS]		= &ahb_ts_clk.common.hw,
		[CLK_AHB_SPI0]		= &ahb_spi0_clk.common.hw,
		[CLK_AHB_SPI1]		= &ahb_spi1_clk.common.hw,
		[CLK_AHB_SPI2]		= &ahb_spi2_clk.common.hw,
		[CLK_AHB_GPS]		= &ahb_gps_clk.common.hw,
		[CLK_AHB_HSTIMER]	= &ahb_hstimer_clk.common.hw,
		[CLK_AHB_VE]		= &ahb_ve_clk.common.hw,
		[CLK_AHB_TVE]		= &ahb_tve_clk.common.hw,
		[CLK_AHB_LCD]		= &ahb_lcd_clk.common.hw,
		[CLK_AHB_CSI]		= &ahb_csi_clk.common.hw,
		[CLK_AHB_DE_BE]		= &ahb_de_be_clk.common.hw,
		[CLK_AHB_DE_FE]		= &ahb_de_fe_clk.common.hw,
		[CLK_AHB_IEP]		= &ahb_iep_clk.common.hw,
		[CLK_AHB_GPU]		= &ahb_gpu_clk.common.hw,
		[CLK_APB0_CODEC]	= &apb0_codec_clk.common.hw,
		[CLK_APB0_SPDIF]	= &apb0_spdif_clk.common.hw,
		[CLK_APB0_I2S]		= &apb0_i2s_clk.common.hw,
		[CLK_APB0_PIO]		= &apb0_pio_clk.common.hw,
		[CLK_APB0_IR]		= &apb0_ir_clk.common.hw,
		[CLK_APB1_I2C0]		= &apb1_i2c0_clk.common.hw,
		[CLK_APB1_I2C1]		= &apb1_i2c1_clk.common.hw,
		[CLK_APB1_I2C2]		= &apb1_i2c2_clk.common.hw,
		[CLK_APB1_UART0]	= &apb1_uart0_clk.common.hw,
		[CLK_APB1_UART1]	= &apb1_uart1_clk.common.hw,
		[CLK_APB1_UART2]	= &apb1_uart2_clk.common.hw,
		[CLK_APB1_UART3]	= &apb1_uart3_clk.common.hw,
		[CLK_NAND]		= &nand_clk.common.hw,
		[CLK_MMC0]		= &mmc0_clk.common.hw,
		[CLK_MMC1]		= &mmc1_clk.common.hw,
		[CLK_MMC2]		= &mmc2_clk.common.hw,
		[CLK_TS]		= &ts_clk.common.hw,
		[CLK_SS]		= &ss_clk.common.hw,
		[CLK_SPI0]		= &spi0_clk.common.hw,
		[CLK_SPI1]		= &spi1_clk.common.hw,
		[CLK_SPI2]		= &spi2_clk.common.hw,
		[CLK_IR]		= &ir_clk.common.hw,
		[CLK_I2S]		= &i2s_clk.common.hw,
		[CLK_SPDIF]		= &spdif_clk.common.hw,
		[CLK_USB_OHCI]		= &usb_ohci_clk.common.hw,
		[CLK_USB_PHY0]		= &usb_phy0_clk.common.hw,
		[CLK_USB_PHY1]		= &usb_phy1_clk.common.hw,
		[CLK_GPS]		= &gps_clk.common.hw,
		[CLK_DRAM_VE]		= &dram_ve_clk.common.hw,
		[CLK_DRAM_CSI]		= &dram_csi_clk.common.hw,
		[CLK_DRAM_TS]		= &dram_ts_clk.common.hw,
		[CLK_DRAM_TVE]		= &dram_tve_clk.common.hw,
		[CLK_DRAM_DE_FE]	= &dram_de_fe_clk.common.hw,
		[CLK_DRAM_DE_BE]	= &dram_de_be_clk.common.hw,
		[CLK_DRAM_ACE]		= &dram_ace_clk.common.hw,
		[CLK_DRAM_IEP]		= &dram_iep_clk.common.hw,
		[CLK_DE_BE]		= &de_be_clk.common.hw,
		[CLK_DE_FE]		= &de_fe_clk.common.hw,
		[CLK_TCON_CH0]		= &tcon_ch0_clk.common.hw,
		[CLK_TCON_CH1_SCLK]	= &tcon_ch1_sclk2_clk.common.hw,
		[CLK_TCON_CH1]		= &tcon_ch1_sclk1_clk.common.hw,
		[CLK_CSI]		= &csi_clk.common.hw,
		[CLK_VE]		= &ve_clk.common.hw,
		[CLK_CODEC]		= &codec_clk.common.hw,
		[CLK_AVS]		= &avs_clk.common.hw,
		[CLK_GPU]		= &gpu_clk.common.hw,
		[CLK_MBUS]		= &mbus_clk.common.hw,
		[CLK_IEP]		= &iep_clk.common.hw,
	},
	.num	= CLK_NUMBER,
};

static const struct sunxi_ccu_desc sun5i_gr8_ccu_desc = {
	.ccu_clks	= sun5i_a10s_ccu_clks,
	.num_ccu_clks	= ARRAY_SIZE(sun5i_a10s_ccu_clks),

	.hw_clks	= &sun5i_gr8_hw_clks,

	.resets		= sun5i_a10s_ccu_resets,
	.num_resets	= ARRAY_SIZE(sun5i_a10s_ccu_resets),
};

static void __init sun5i_ccu_init(struct device_node *node,
				  const struct sunxi_ccu_desc *desc)
{
	void __iomem *reg;
	u32 val;

	reg = of_io_request_and_map(node, 0, of_node_full_name(node));
	if (IS_ERR(reg)) {
		pr_err("%s: Could not map the clock registers\n",
		       of_node_full_name(node));
		return;
	}

	/* Force the PLL-Audio-1x divider to 4 */
	val = readl(reg + SUN5I_PLL_AUDIO_REG);
	val &= ~GENMASK(19, 16);
	writel(val | (3 << 16), reg + SUN5I_PLL_AUDIO_REG);

	/*
	 * Use the peripheral PLL as the AHB parent, instead of CPU /
	 * AXI which have rate changes due to cpufreq.
	 *
	 * This is especially a big deal for the HS timer whose parent
	 * clock is AHB.
	 */
	val = readl(reg + SUN5I_AHB_REG);
	val &= ~GENMASK(7, 6);
	writel(val | (2 << 6), reg + SUN5I_AHB_REG);

	sunxi_ccu_probe(node, reg, desc);
}

static void __init sun5i_a10s_ccu_setup(struct device_node *node)
{
	sun5i_ccu_init(node, &sun5i_a10s_ccu_desc);
}
CLK_OF_DECLARE(sun5i_a10s_ccu, "allwinner,sun5i-a10s-ccu",
	       sun5i_a10s_ccu_setup);

static void __init sun5i_a13_ccu_setup(struct device_node *node)
{
	sun5i_ccu_init(node, &sun5i_a13_ccu_desc);
}
CLK_OF_DECLARE(sun5i_a13_ccu, "allwinner,sun5i-a13-ccu",
	       sun5i_a13_ccu_setup);

static void __init sun5i_gr8_ccu_setup(struct device_node *node)
{
	sun5i_ccu_init(node, &sun5i_gr8_ccu_desc);
}
CLK_OF_DECLARE(sun5i_gr8_ccu, "nextthing,gr8-ccu",
	       sun5i_gr8_ccu_setup);
