// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2016 Maxime Ripard. All rights reserved.
 */

#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
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
#include "ccu_phase.h"
#include "ccu_sdm.h"

#include "ccu-sun8i-h3.h"

static SUNXI_CCU_NKMP_WITH_GATE_LOCK(pll_cpux_clk, "pll-cpux",
				     "osc24M", 0x000,
				     8, 5,	/* N */
				     4, 2,	/* K */
				     0, 2,	/* M */
				     16, 2,	/* P */
				     BIT(31),	/* gate */
				     BIT(28),	/* lock */
				     CLK_SET_RATE_UNGATE);

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
#define SUN8I_H3_PLL_AUDIO_REG	0x008

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

static SUNXI_CCU_NM_WITH_FRAC_GATE_LOCK_MIN_MAX(pll_video_clk, "pll-video",
						"osc24M", 0x0010,
						192000000, /* Minimum rate */
						912000000, /* Maximum rate */
						8, 7,      /* N */
						0, 4,	   /* M */
						BIT(24),   /* frac enable */
						BIT(25),   /* frac select */
						270000000, /* frac rate 0 */
						297000000, /* frac rate 1 */
						BIT(31),   /* gate */
						BIT(28),   /* lock */
						CLK_SET_RATE_UNGATE);

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

static SUNXI_CCU_NKM_WITH_GATE_LOCK(pll_ddr_clk, "pll-ddr",
				    "osc24M", 0x020,
				    8, 5,	/* N */
				    4, 2,	/* K */
				    0, 2,	/* M */
				    BIT(31),	/* gate */
				    BIT(28),	/* lock */
				    CLK_SET_RATE_UNGATE);

static SUNXI_CCU_NK_WITH_GATE_LOCK_POSTDIV(pll_periph0_clk, "pll-periph0",
					   "osc24M", 0x028,
					   8, 5,	/* N */
					   4, 2,	/* K */
					   BIT(31),	/* gate */
					   BIT(28),	/* lock */
					   2,		/* post-div */
					   CLK_SET_RATE_UNGATE);

static SUNXI_CCU_NM_WITH_FRAC_GATE_LOCK(pll_gpu_clk, "pll-gpu",
					"osc24M", 0x0038,
					8, 7,		/* N */
					0, 4,		/* M */
					BIT(24),	/* frac enable */
					BIT(25),	/* frac select */
					270000000,	/* frac rate 0 */
					297000000,	/* frac rate 1 */
					BIT(31),	/* gate */
					BIT(28),	/* lock */
					CLK_SET_RATE_UNGATE);

static SUNXI_CCU_NK_WITH_GATE_LOCK_POSTDIV(pll_periph1_clk, "pll-periph1",
					   "osc24M", 0x044,
					   8, 5,	/* N */
					   4, 2,	/* K */
					   BIT(31),	/* gate */
					   BIT(28),	/* lock */
					   2,		/* post-div */
					   CLK_SET_RATE_UNGATE);

static SUNXI_CCU_NM_WITH_FRAC_GATE_LOCK(pll_de_clk, "pll-de",
					"osc24M", 0x0048,
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
		     0x050, 16, 2, CLK_IS_CRITICAL | CLK_SET_RATE_PARENT);

static SUNXI_CCU_M(axi_clk, "axi", "cpux", 0x050, 0, 2, 0);

static const char * const ahb1_parents[] = { "osc32k", "osc24M",
					     "axi" , "pll-periph0" };
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
					     "pll-periph0" , "pll-periph0" };
static SUNXI_CCU_MP_WITH_MUX(apb2_clk, "apb2", apb2_parents, 0x058,
			     0, 5,	/* M */
			     16, 2,	/* P */
			     24, 2,	/* mux */
			     0);

static const char * const ahb2_parents[] = { "ahb1" , "pll-periph0" };
static const struct ccu_mux_fixed_prediv ahb2_fixed_predivs[] = {
	{ .index = 1, .div = 2 },
};
static struct ccu_mux ahb2_clk = {
	.mux		= {
		.shift	= 0,
		.width	= 1,
		.fixed_predivs	= ahb2_fixed_predivs,
		.n_predivs	= ARRAY_SIZE(ahb2_fixed_predivs),
	},

	.common		= {
		.reg		= 0x05c,
		.features	= CCU_FEATURE_FIXED_PREDIV,
		.hw.init	= CLK_HW_INIT_PARENTS("ahb2",
						      ahb2_parents,
						      &ccu_mux_ops,
						      0),
	},
};

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
static SUNXI_CCU_GATE(bus_nand_clk,	"bus-nand",	"ahb1",
		      0x060, BIT(13), 0);
static SUNXI_CCU_GATE(bus_dram_clk,	"bus-dram",	"ahb1",
		      0x060, BIT(14), 0);
static SUNXI_CCU_GATE(bus_emac_clk,	"bus-emac",	"ahb2",
		      0x060, BIT(17), 0);
static SUNXI_CCU_GATE(bus_ts_clk,	"bus-ts",	"ahb1",
		      0x060, BIT(18), 0);
static SUNXI_CCU_GATE(bus_hstimer_clk,	"bus-hstimer",	"ahb1",
		      0x060, BIT(19), 0);
static SUNXI_CCU_GATE(bus_spi0_clk,	"bus-spi0",	"ahb1",
		      0x060, BIT(20), 0);
static SUNXI_CCU_GATE(bus_spi1_clk,	"bus-spi1",	"ahb1",
		      0x060, BIT(21), 0);
static SUNXI_CCU_GATE(bus_otg_clk,	"bus-otg",	"ahb1",
		      0x060, BIT(23), 0);
static SUNXI_CCU_GATE(bus_ehci0_clk,	"bus-ehci0",	"ahb1",
		      0x060, BIT(24), 0);
static SUNXI_CCU_GATE(bus_ehci1_clk,	"bus-ehci1",	"ahb2",
		      0x060, BIT(25), 0);
static SUNXI_CCU_GATE(bus_ehci2_clk,	"bus-ehci2",	"ahb2",
		      0x060, BIT(26), 0);
static SUNXI_CCU_GATE(bus_ehci3_clk,	"bus-ehci3",	"ahb2",
		      0x060, BIT(27), 0);
static SUNXI_CCU_GATE(bus_ohci0_clk,	"bus-ohci0",	"ahb1",
		      0x060, BIT(28), 0);
static SUNXI_CCU_GATE(bus_ohci1_clk,	"bus-ohci1",	"ahb2",
		      0x060, BIT(29), 0);
static SUNXI_CCU_GATE(bus_ohci2_clk,	"bus-ohci2",	"ahb2",
		      0x060, BIT(30), 0);
static SUNXI_CCU_GATE(bus_ohci3_clk,	"bus-ohci3",	"ahb2",
		      0x060, BIT(31), 0);

static SUNXI_CCU_GATE(bus_ve_clk,	"bus-ve",	"ahb1",
		      0x064, BIT(0), 0);
static SUNXI_CCU_GATE(bus_tcon0_clk,	"bus-tcon0",	"ahb1",
		      0x064, BIT(3), 0);
static SUNXI_CCU_GATE(bus_tcon1_clk,	"bus-tcon1",	"ahb1",
		      0x064, BIT(4), 0);
static SUNXI_CCU_GATE(bus_deinterlace_clk,	"bus-deinterlace",	"ahb1",
		      0x064, BIT(5), 0);
static SUNXI_CCU_GATE(bus_csi_clk,	"bus-csi",	"ahb1",
		      0x064, BIT(8), 0);
static SUNXI_CCU_GATE(bus_tve_clk,	"bus-tve",	"ahb1",
		      0x064, BIT(9), 0);
static SUNXI_CCU_GATE(bus_hdmi_clk,	"bus-hdmi",	"ahb1",
		      0x064, BIT(11), 0);
static SUNXI_CCU_GATE(bus_de_clk,	"bus-de",	"ahb1",
		      0x064, BIT(12), 0);
static SUNXI_CCU_GATE(bus_gpu_clk,	"bus-gpu",	"ahb1",
		      0x064, BIT(20), 0);
static SUNXI_CCU_GATE(bus_msgbox_clk,	"bus-msgbox",	"ahb1",
		      0x064, BIT(21), 0);
static SUNXI_CCU_GATE(bus_spinlock_clk,	"bus-spinlock",	"ahb1",
		      0x064, BIT(22), 0);

static SUNXI_CCU_GATE(bus_codec_clk,	"bus-codec",	"apb1",
		      0x068, BIT(0), 0);
static SUNXI_CCU_GATE(bus_spdif_clk,	"bus-spdif",	"apb1",
		      0x068, BIT(1), 0);
static SUNXI_CCU_GATE(bus_pio_clk,	"bus-pio",	"apb1",
		      0x068, BIT(5), 0);
static SUNXI_CCU_GATE(bus_ths_clk,	"bus-ths",	"apb1",
		      0x068, BIT(8), 0);
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
static SUNXI_CCU_GATE(bus_uart0_clk,	"bus-uart0",	"apb2",
		      0x06c, BIT(16), 0);
static SUNXI_CCU_GATE(bus_uart1_clk,	"bus-uart1",	"apb2",
		      0x06c, BIT(17), 0);
static SUNXI_CCU_GATE(bus_uart2_clk,	"bus-uart2",	"apb2",
		      0x06c, BIT(18), 0);
static SUNXI_CCU_GATE(bus_uart3_clk,	"bus-uart3",	"apb2",
		      0x06c, BIT(19), 0);
static SUNXI_CCU_GATE(bus_scr0_clk,	"bus-scr0",	"apb2",
		      0x06c, BIT(20), 0);
static SUNXI_CCU_GATE(bus_scr1_clk,	"bus-scr1",	"apb2",
		      0x06c, BIT(21), 0);

static SUNXI_CCU_GATE(bus_ephy_clk,	"bus-ephy",	"ahb1",
		      0x070, BIT(0), 0);
static SUNXI_CCU_GATE(bus_dbg_clk,	"bus-dbg",	"ahb1",
		      0x070, BIT(7), 0);

static struct clk_div_table ths_div_table[] = {
	{ .val = 0, .div = 1 },
	{ .val = 1, .div = 2 },
	{ .val = 2, .div = 4 },
	{ .val = 3, .div = 6 },
	{ /* Sentinel */ },
};
static SUNXI_CCU_DIV_TABLE_WITH_GATE(ths_clk, "ths", "osc24M",
				     0x074, 0, 2, ths_div_table, BIT(31), 0);

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

static const char * const ts_parents[] = { "osc24M", "pll-periph0", };
static SUNXI_CCU_MP_WITH_MUX_GATE(ts_clk, "ts", ts_parents, 0x098,
				  0, 4,		/* M */
				  16, 2,	/* P */
				  24, 2,	/* mux */
				  BIT(31),	/* gate */
				  0);

static SUNXI_CCU_MP_WITH_MUX_GATE(ce_clk, "ce", mod0_default_parents, 0x09c,
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

static const char * const i2s_parents[] = { "pll-audio-8x", "pll-audio-4x",
					    "pll-audio-2x", "pll-audio" };
static SUNXI_CCU_MUX_WITH_GATE(i2s0_clk, "i2s0", i2s_parents,
			       0x0b0, 16, 2, BIT(31), CLK_SET_RATE_PARENT);

static SUNXI_CCU_MUX_WITH_GATE(i2s1_clk, "i2s1", i2s_parents,
			       0x0b4, 16, 2, BIT(31), CLK_SET_RATE_PARENT);

static SUNXI_CCU_MUX_WITH_GATE(i2s2_clk, "i2s2", i2s_parents,
			       0x0b8, 16, 2, BIT(31), CLK_SET_RATE_PARENT);

static SUNXI_CCU_M_WITH_GATE(spdif_clk, "spdif", "pll-audio",
			     0x0c0, 0, 4, BIT(31), CLK_SET_RATE_PARENT);

static SUNXI_CCU_GATE(usb_phy0_clk,	"usb-phy0",	"osc24M",
		      0x0cc, BIT(8), 0);
static SUNXI_CCU_GATE(usb_phy1_clk,	"usb-phy1",	"osc24M",
		      0x0cc, BIT(9), 0);
static SUNXI_CCU_GATE(usb_phy2_clk,	"usb-phy2",	"osc24M",
		      0x0cc, BIT(10), 0);
static SUNXI_CCU_GATE(usb_phy3_clk,	"usb-phy3",	"osc24M",
		      0x0cc, BIT(11), 0);
static SUNXI_CCU_GATE(usb_ohci0_clk,	"usb-ohci0",	"osc24M",
		      0x0cc, BIT(16), 0);
static SUNXI_CCU_GATE(usb_ohci1_clk,	"usb-ohci1",	"osc24M",
		      0x0cc, BIT(17), 0);
static SUNXI_CCU_GATE(usb_ohci2_clk,	"usb-ohci2",	"osc24M",
		      0x0cc, BIT(18), 0);
static SUNXI_CCU_GATE(usb_ohci3_clk,	"usb-ohci3",	"osc24M",
		      0x0cc, BIT(19), 0);

/* H3 has broken MDFS hardware, so the mux/divider cannot be changed. */
static CLK_FIXED_FACTOR_HW(h3_dram_clk, "dram",
			   &pll_ddr_clk.common.hw,
			   1, 1, CLK_SET_RATE_PARENT | CLK_IS_CRITICAL);

static const char * const h5_dram_parents[] = { "pll-ddr", "pll-periph0-2x" };
static SUNXI_CCU_M_WITH_MUX(h5_dram_clk, "dram", h5_dram_parents,
			    0x0f4, 0, 4, 20, 2, CLK_IS_CRITICAL);

static SUNXI_CCU_GATE(dram_ve_clk,	"dram-ve",	"dram",
		      0x100, BIT(0), 0);
static SUNXI_CCU_GATE(dram_csi_clk,	"dram-csi",	"dram",
		      0x100, BIT(1), 0);
static SUNXI_CCU_GATE(dram_deinterlace_clk,	"dram-deinterlace",	"dram",
		      0x100, BIT(2), 0);
static SUNXI_CCU_GATE(dram_ts_clk,	"dram-ts",	"dram",
		      0x100, BIT(3), 0);

static const char * const de_parents[] = { "pll-periph0-2x", "pll-de" };
static SUNXI_CCU_M_WITH_MUX_GATE(de_clk, "de", de_parents,
				 0x104, 0, 4, 24, 3, BIT(31),
				 CLK_SET_RATE_PARENT);

static const char * const tcon_parents[] = { "pll-video" };
static SUNXI_CCU_M_WITH_MUX_GATE(tcon_clk, "tcon", tcon_parents,
				 0x118, 0, 4, 24, 3, BIT(31),
				 CLK_SET_RATE_PARENT);

static const char * const tve_parents[] = { "pll-de", "pll-periph1" };
static SUNXI_CCU_M_WITH_MUX_GATE(tve_clk, "tve", tve_parents,
				 0x120, 0, 4, 24, 3, BIT(31), 0);

static const char * const deinterlace_parents[] = { "pll-periph0", "pll-periph1" };
static SUNXI_CCU_M_WITH_MUX_GATE(deinterlace_clk, "deinterlace", deinterlace_parents,
				 0x124, 0, 4, 24, 3, BIT(31), 0);

static SUNXI_CCU_GATE(csi_misc_clk,	"csi-misc",	"osc24M",
		      0x130, BIT(31), 0);

static const char * const csi_sclk_parents[] = { "pll-periph0", "pll-periph1" };
static SUNXI_CCU_M_WITH_MUX_GATE(csi_sclk_clk, "csi-sclk", csi_sclk_parents,
				 0x134, 16, 4, 24, 3, BIT(31), 0);

static const char * const csi_mclk_parents[] = { "osc24M", "pll-video", "pll-periph1" };
static SUNXI_CCU_M_WITH_MUX_GATE(csi_mclk_clk, "csi-mclk", csi_mclk_parents,
				 0x134, 0, 5, 8, 3, BIT(15), 0);

static SUNXI_CCU_M_WITH_GATE(ve_clk, "ve", "pll-ve",
			     0x13c, 16, 3, BIT(31), CLK_SET_RATE_PARENT);

static SUNXI_CCU_GATE(ac_dig_clk,	"ac-dig",	"pll-audio",
		      0x140, BIT(31), CLK_SET_RATE_PARENT);
static SUNXI_CCU_GATE(avs_clk,		"avs",		"osc24M",
		      0x144, BIT(31), 0);

static const char * const hdmi_parents[] = { "pll-video" };
static SUNXI_CCU_M_WITH_MUX_GATE(hdmi_clk, "hdmi", hdmi_parents,
				 0x150, 0, 4, 24, 2, BIT(31),
				 CLK_SET_RATE_PARENT);

static SUNXI_CCU_GATE(hdmi_ddc_clk,	"hdmi-ddc",	"osc24M",
		      0x154, BIT(31), 0);

static const char * const mbus_parents[] = { "osc24M", "pll-periph0-2x", "pll-ddr" };
static SUNXI_CCU_M_WITH_MUX_GATE(mbus_clk, "mbus", mbus_parents,
				 0x15c, 0, 3, 24, 2, BIT(31), CLK_IS_CRITICAL);

static SUNXI_CCU_M_WITH_GATE(gpu_clk, "gpu", "pll-gpu",
			     0x1a0, 0, 3, BIT(31), CLK_SET_RATE_PARENT);

static struct ccu_common *sun8i_h3_ccu_clks[] = {
	&pll_cpux_clk.common,
	&pll_audio_base_clk.common,
	&pll_video_clk.common,
	&pll_ve_clk.common,
	&pll_ddr_clk.common,
	&pll_periph0_clk.common,
	&pll_gpu_clk.common,
	&pll_periph1_clk.common,
	&pll_de_clk.common,
	&cpux_clk.common,
	&axi_clk.common,
	&ahb1_clk.common,
	&apb1_clk.common,
	&apb2_clk.common,
	&ahb2_clk.common,
	&bus_ce_clk.common,
	&bus_dma_clk.common,
	&bus_mmc0_clk.common,
	&bus_mmc1_clk.common,
	&bus_mmc2_clk.common,
	&bus_nand_clk.common,
	&bus_dram_clk.common,
	&bus_emac_clk.common,
	&bus_ts_clk.common,
	&bus_hstimer_clk.common,
	&bus_spi0_clk.common,
	&bus_spi1_clk.common,
	&bus_otg_clk.common,
	&bus_ehci0_clk.common,
	&bus_ehci1_clk.common,
	&bus_ehci2_clk.common,
	&bus_ehci3_clk.common,
	&bus_ohci0_clk.common,
	&bus_ohci1_clk.common,
	&bus_ohci2_clk.common,
	&bus_ohci3_clk.common,
	&bus_ve_clk.common,
	&bus_tcon0_clk.common,
	&bus_tcon1_clk.common,
	&bus_deinterlace_clk.common,
	&bus_csi_clk.common,
	&bus_tve_clk.common,
	&bus_hdmi_clk.common,
	&bus_de_clk.common,
	&bus_gpu_clk.common,
	&bus_msgbox_clk.common,
	&bus_spinlock_clk.common,
	&bus_codec_clk.common,
	&bus_spdif_clk.common,
	&bus_pio_clk.common,
	&bus_ths_clk.common,
	&bus_i2s0_clk.common,
	&bus_i2s1_clk.common,
	&bus_i2s2_clk.common,
	&bus_i2c0_clk.common,
	&bus_i2c1_clk.common,
	&bus_i2c2_clk.common,
	&bus_uart0_clk.common,
	&bus_uart1_clk.common,
	&bus_uart2_clk.common,
	&bus_uart3_clk.common,
	&bus_scr0_clk.common,
	&bus_scr1_clk.common,
	&bus_ephy_clk.common,
	&bus_dbg_clk.common,
	&ths_clk.common,
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
	&ts_clk.common,
	&ce_clk.common,
	&spi0_clk.common,
	&spi1_clk.common,
	&i2s0_clk.common,
	&i2s1_clk.common,
	&i2s2_clk.common,
	&spdif_clk.common,
	&usb_phy0_clk.common,
	&usb_phy1_clk.common,
	&usb_phy2_clk.common,
	&usb_phy3_clk.common,
	&usb_ohci0_clk.common,
	&usb_ohci1_clk.common,
	&usb_ohci2_clk.common,
	&usb_ohci3_clk.common,
	&h5_dram_clk.common,
	&dram_ve_clk.common,
	&dram_csi_clk.common,
	&dram_deinterlace_clk.common,
	&dram_ts_clk.common,
	&de_clk.common,
	&tcon_clk.common,
	&tve_clk.common,
	&deinterlace_clk.common,
	&csi_misc_clk.common,
	&csi_sclk_clk.common,
	&csi_mclk_clk.common,
	&ve_clk.common,
	&ac_dig_clk.common,
	&avs_clk.common,
	&hdmi_clk.common,
	&hdmi_ddc_clk.common,
	&mbus_clk.common,
	&gpu_clk.common,
};

static const struct clk_hw *clk_parent_pll_audio[] = {
	&pll_audio_base_clk.common.hw
};

/* We hardcode the divider to 1 for now */
static CLK_FIXED_FACTOR_HWS(pll_audio_clk, "pll-audio",
			    clk_parent_pll_audio,
			    1, 1, CLK_SET_RATE_PARENT);
static CLK_FIXED_FACTOR_HWS(pll_audio_2x_clk, "pll-audio-2x",
			    clk_parent_pll_audio,
			    2, 1, CLK_SET_RATE_PARENT);
static CLK_FIXED_FACTOR_HWS(pll_audio_4x_clk, "pll-audio-4x",
			    clk_parent_pll_audio,
			    1, 1, CLK_SET_RATE_PARENT);
static CLK_FIXED_FACTOR_HWS(pll_audio_8x_clk, "pll-audio-8x",
			    clk_parent_pll_audio,
			    1, 2, CLK_SET_RATE_PARENT);
static CLK_FIXED_FACTOR_HW(pll_periph0_2x_clk, "pll-periph0-2x",
			   &pll_periph0_clk.common.hw,
			   1, 2, 0);

static struct clk_hw_onecell_data sun8i_h3_hw_clks = {
	.hws	= {
		[CLK_PLL_CPUX]		= &pll_cpux_clk.common.hw,
		[CLK_PLL_AUDIO_BASE]	= &pll_audio_base_clk.common.hw,
		[CLK_PLL_AUDIO]		= &pll_audio_clk.hw,
		[CLK_PLL_AUDIO_2X]	= &pll_audio_2x_clk.hw,
		[CLK_PLL_AUDIO_4X]	= &pll_audio_4x_clk.hw,
		[CLK_PLL_AUDIO_8X]	= &pll_audio_8x_clk.hw,
		[CLK_PLL_VIDEO]		= &pll_video_clk.common.hw,
		[CLK_PLL_VE]		= &pll_ve_clk.common.hw,
		[CLK_PLL_DDR]		= &pll_ddr_clk.common.hw,
		[CLK_PLL_PERIPH0]	= &pll_periph0_clk.common.hw,
		[CLK_PLL_PERIPH0_2X]	= &pll_periph0_2x_clk.hw,
		[CLK_PLL_GPU]		= &pll_gpu_clk.common.hw,
		[CLK_PLL_PERIPH1]	= &pll_periph1_clk.common.hw,
		[CLK_PLL_DE]		= &pll_de_clk.common.hw,
		[CLK_CPUX]		= &cpux_clk.common.hw,
		[CLK_AXI]		= &axi_clk.common.hw,
		[CLK_AHB1]		= &ahb1_clk.common.hw,
		[CLK_APB1]		= &apb1_clk.common.hw,
		[CLK_APB2]		= &apb2_clk.common.hw,
		[CLK_AHB2]		= &ahb2_clk.common.hw,
		[CLK_BUS_CE]		= &bus_ce_clk.common.hw,
		[CLK_BUS_DMA]		= &bus_dma_clk.common.hw,
		[CLK_BUS_MMC0]		= &bus_mmc0_clk.common.hw,
		[CLK_BUS_MMC1]		= &bus_mmc1_clk.common.hw,
		[CLK_BUS_MMC2]		= &bus_mmc2_clk.common.hw,
		[CLK_BUS_NAND]		= &bus_nand_clk.common.hw,
		[CLK_BUS_DRAM]		= &bus_dram_clk.common.hw,
		[CLK_BUS_EMAC]		= &bus_emac_clk.common.hw,
		[CLK_BUS_TS]		= &bus_ts_clk.common.hw,
		[CLK_BUS_HSTIMER]	= &bus_hstimer_clk.common.hw,
		[CLK_BUS_SPI0]		= &bus_spi0_clk.common.hw,
		[CLK_BUS_SPI1]		= &bus_spi1_clk.common.hw,
		[CLK_BUS_OTG]		= &bus_otg_clk.common.hw,
		[CLK_BUS_EHCI0]		= &bus_ehci0_clk.common.hw,
		[CLK_BUS_EHCI1]		= &bus_ehci1_clk.common.hw,
		[CLK_BUS_EHCI2]		= &bus_ehci2_clk.common.hw,
		[CLK_BUS_EHCI3]		= &bus_ehci3_clk.common.hw,
		[CLK_BUS_OHCI0]		= &bus_ohci0_clk.common.hw,
		[CLK_BUS_OHCI1]		= &bus_ohci1_clk.common.hw,
		[CLK_BUS_OHCI2]		= &bus_ohci2_clk.common.hw,
		[CLK_BUS_OHCI3]		= &bus_ohci3_clk.common.hw,
		[CLK_BUS_VE]		= &bus_ve_clk.common.hw,
		[CLK_BUS_TCON0]		= &bus_tcon0_clk.common.hw,
		[CLK_BUS_TCON1]		= &bus_tcon1_clk.common.hw,
		[CLK_BUS_DEINTERLACE]	= &bus_deinterlace_clk.common.hw,
		[CLK_BUS_CSI]		= &bus_csi_clk.common.hw,
		[CLK_BUS_TVE]		= &bus_tve_clk.common.hw,
		[CLK_BUS_HDMI]		= &bus_hdmi_clk.common.hw,
		[CLK_BUS_DE]		= &bus_de_clk.common.hw,
		[CLK_BUS_GPU]		= &bus_gpu_clk.common.hw,
		[CLK_BUS_MSGBOX]	= &bus_msgbox_clk.common.hw,
		[CLK_BUS_SPINLOCK]	= &bus_spinlock_clk.common.hw,
		[CLK_BUS_CODEC]		= &bus_codec_clk.common.hw,
		[CLK_BUS_SPDIF]		= &bus_spdif_clk.common.hw,
		[CLK_BUS_PIO]		= &bus_pio_clk.common.hw,
		[CLK_BUS_THS]		= &bus_ths_clk.common.hw,
		[CLK_BUS_I2S0]		= &bus_i2s0_clk.common.hw,
		[CLK_BUS_I2S1]		= &bus_i2s1_clk.common.hw,
		[CLK_BUS_I2S2]		= &bus_i2s2_clk.common.hw,
		[CLK_BUS_I2C0]		= &bus_i2c0_clk.common.hw,
		[CLK_BUS_I2C1]		= &bus_i2c1_clk.common.hw,
		[CLK_BUS_I2C2]		= &bus_i2c2_clk.common.hw,
		[CLK_BUS_UART0]		= &bus_uart0_clk.common.hw,
		[CLK_BUS_UART1]		= &bus_uart1_clk.common.hw,
		[CLK_BUS_UART2]		= &bus_uart2_clk.common.hw,
		[CLK_BUS_UART3]		= &bus_uart3_clk.common.hw,
		[CLK_BUS_SCR0]		= &bus_scr0_clk.common.hw,
		[CLK_BUS_EPHY]		= &bus_ephy_clk.common.hw,
		[CLK_BUS_DBG]		= &bus_dbg_clk.common.hw,
		[CLK_THS]		= &ths_clk.common.hw,
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
		[CLK_TS]		= &ts_clk.common.hw,
		[CLK_CE]		= &ce_clk.common.hw,
		[CLK_SPI0]		= &spi0_clk.common.hw,
		[CLK_SPI1]		= &spi1_clk.common.hw,
		[CLK_I2S0]		= &i2s0_clk.common.hw,
		[CLK_I2S1]		= &i2s1_clk.common.hw,
		[CLK_I2S2]		= &i2s2_clk.common.hw,
		[CLK_SPDIF]		= &spdif_clk.common.hw,
		[CLK_USB_PHY0]		= &usb_phy0_clk.common.hw,
		[CLK_USB_PHY1]		= &usb_phy1_clk.common.hw,
		[CLK_USB_PHY2]		= &usb_phy2_clk.common.hw,
		[CLK_USB_PHY3]		= &usb_phy3_clk.common.hw,
		[CLK_USB_OHCI0]		= &usb_ohci0_clk.common.hw,
		[CLK_USB_OHCI1]		= &usb_ohci1_clk.common.hw,
		[CLK_USB_OHCI2]		= &usb_ohci2_clk.common.hw,
		[CLK_USB_OHCI3]		= &usb_ohci3_clk.common.hw,
		[CLK_DRAM]		= &h3_dram_clk.hw,
		[CLK_DRAM_VE]		= &dram_ve_clk.common.hw,
		[CLK_DRAM_CSI]		= &dram_csi_clk.common.hw,
		[CLK_DRAM_DEINTERLACE]	= &dram_deinterlace_clk.common.hw,
		[CLK_DRAM_TS]		= &dram_ts_clk.common.hw,
		[CLK_DE]		= &de_clk.common.hw,
		[CLK_TCON0]		= &tcon_clk.common.hw,
		[CLK_TVE]		= &tve_clk.common.hw,
		[CLK_DEINTERLACE]	= &deinterlace_clk.common.hw,
		[CLK_CSI_MISC]		= &csi_misc_clk.common.hw,
		[CLK_CSI_SCLK]		= &csi_sclk_clk.common.hw,
		[CLK_CSI_MCLK]		= &csi_mclk_clk.common.hw,
		[CLK_VE]		= &ve_clk.common.hw,
		[CLK_AC_DIG]		= &ac_dig_clk.common.hw,
		[CLK_AVS]		= &avs_clk.common.hw,
		[CLK_HDMI]		= &hdmi_clk.common.hw,
		[CLK_HDMI_DDC]		= &hdmi_ddc_clk.common.hw,
		[CLK_MBUS]		= &mbus_clk.common.hw,
		[CLK_GPU]		= &gpu_clk.common.hw,
	},
	.num	= CLK_NUMBER_H3,
};

static struct clk_hw_onecell_data sun50i_h5_hw_clks = {
	.hws	= {
		[CLK_PLL_CPUX]		= &pll_cpux_clk.common.hw,
		[CLK_PLL_AUDIO_BASE]	= &pll_audio_base_clk.common.hw,
		[CLK_PLL_AUDIO]		= &pll_audio_clk.hw,
		[CLK_PLL_AUDIO_2X]	= &pll_audio_2x_clk.hw,
		[CLK_PLL_AUDIO_4X]	= &pll_audio_4x_clk.hw,
		[CLK_PLL_AUDIO_8X]	= &pll_audio_8x_clk.hw,
		[CLK_PLL_VIDEO]		= &pll_video_clk.common.hw,
		[CLK_PLL_VE]		= &pll_ve_clk.common.hw,
		[CLK_PLL_DDR]		= &pll_ddr_clk.common.hw,
		[CLK_PLL_PERIPH0]	= &pll_periph0_clk.common.hw,
		[CLK_PLL_PERIPH0_2X]	= &pll_periph0_2x_clk.hw,
		[CLK_PLL_GPU]		= &pll_gpu_clk.common.hw,
		[CLK_PLL_PERIPH1]	= &pll_periph1_clk.common.hw,
		[CLK_PLL_DE]		= &pll_de_clk.common.hw,
		[CLK_CPUX]		= &cpux_clk.common.hw,
		[CLK_AXI]		= &axi_clk.common.hw,
		[CLK_AHB1]		= &ahb1_clk.common.hw,
		[CLK_APB1]		= &apb1_clk.common.hw,
		[CLK_APB2]		= &apb2_clk.common.hw,
		[CLK_AHB2]		= &ahb2_clk.common.hw,
		[CLK_BUS_CE]		= &bus_ce_clk.common.hw,
		[CLK_BUS_DMA]		= &bus_dma_clk.common.hw,
		[CLK_BUS_MMC0]		= &bus_mmc0_clk.common.hw,
		[CLK_BUS_MMC1]		= &bus_mmc1_clk.common.hw,
		[CLK_BUS_MMC2]		= &bus_mmc2_clk.common.hw,
		[CLK_BUS_NAND]		= &bus_nand_clk.common.hw,
		[CLK_BUS_DRAM]		= &bus_dram_clk.common.hw,
		[CLK_BUS_EMAC]		= &bus_emac_clk.common.hw,
		[CLK_BUS_TS]		= &bus_ts_clk.common.hw,
		[CLK_BUS_HSTIMER]	= &bus_hstimer_clk.common.hw,
		[CLK_BUS_SPI0]		= &bus_spi0_clk.common.hw,
		[CLK_BUS_SPI1]		= &bus_spi1_clk.common.hw,
		[CLK_BUS_OTG]		= &bus_otg_clk.common.hw,
		[CLK_BUS_EHCI0]		= &bus_ehci0_clk.common.hw,
		[CLK_BUS_EHCI1]		= &bus_ehci1_clk.common.hw,
		[CLK_BUS_EHCI2]		= &bus_ehci2_clk.common.hw,
		[CLK_BUS_EHCI3]		= &bus_ehci3_clk.common.hw,
		[CLK_BUS_OHCI0]		= &bus_ohci0_clk.common.hw,
		[CLK_BUS_OHCI1]		= &bus_ohci1_clk.common.hw,
		[CLK_BUS_OHCI2]		= &bus_ohci2_clk.common.hw,
		[CLK_BUS_OHCI3]		= &bus_ohci3_clk.common.hw,
		[CLK_BUS_VE]		= &bus_ve_clk.common.hw,
		[CLK_BUS_TCON0]		= &bus_tcon0_clk.common.hw,
		[CLK_BUS_TCON1]		= &bus_tcon1_clk.common.hw,
		[CLK_BUS_DEINTERLACE]	= &bus_deinterlace_clk.common.hw,
		[CLK_BUS_CSI]		= &bus_csi_clk.common.hw,
		[CLK_BUS_TVE]		= &bus_tve_clk.common.hw,
		[CLK_BUS_HDMI]		= &bus_hdmi_clk.common.hw,
		[CLK_BUS_DE]		= &bus_de_clk.common.hw,
		[CLK_BUS_GPU]		= &bus_gpu_clk.common.hw,
		[CLK_BUS_MSGBOX]	= &bus_msgbox_clk.common.hw,
		[CLK_BUS_SPINLOCK]	= &bus_spinlock_clk.common.hw,
		[CLK_BUS_CODEC]		= &bus_codec_clk.common.hw,
		[CLK_BUS_SPDIF]		= &bus_spdif_clk.common.hw,
		[CLK_BUS_PIO]		= &bus_pio_clk.common.hw,
		[CLK_BUS_THS]		= &bus_ths_clk.common.hw,
		[CLK_BUS_I2S0]		= &bus_i2s0_clk.common.hw,
		[CLK_BUS_I2S1]		= &bus_i2s1_clk.common.hw,
		[CLK_BUS_I2S2]		= &bus_i2s2_clk.common.hw,
		[CLK_BUS_I2C0]		= &bus_i2c0_clk.common.hw,
		[CLK_BUS_I2C1]		= &bus_i2c1_clk.common.hw,
		[CLK_BUS_I2C2]		= &bus_i2c2_clk.common.hw,
		[CLK_BUS_UART0]		= &bus_uart0_clk.common.hw,
		[CLK_BUS_UART1]		= &bus_uart1_clk.common.hw,
		[CLK_BUS_UART2]		= &bus_uart2_clk.common.hw,
		[CLK_BUS_UART3]		= &bus_uart3_clk.common.hw,
		[CLK_BUS_SCR0]		= &bus_scr0_clk.common.hw,
		[CLK_BUS_SCR1]		= &bus_scr1_clk.common.hw,
		[CLK_BUS_EPHY]		= &bus_ephy_clk.common.hw,
		[CLK_BUS_DBG]		= &bus_dbg_clk.common.hw,
		[CLK_THS]		= &ths_clk.common.hw,
		[CLK_NAND]		= &nand_clk.common.hw,
		[CLK_MMC0]		= &mmc0_clk.common.hw,
		[CLK_MMC1]		= &mmc1_clk.common.hw,
		[CLK_MMC2]		= &mmc2_clk.common.hw,
		[CLK_TS]		= &ts_clk.common.hw,
		[CLK_CE]		= &ce_clk.common.hw,
		[CLK_SPI0]		= &spi0_clk.common.hw,
		[CLK_SPI1]		= &spi1_clk.common.hw,
		[CLK_I2S0]		= &i2s0_clk.common.hw,
		[CLK_I2S1]		= &i2s1_clk.common.hw,
		[CLK_I2S2]		= &i2s2_clk.common.hw,
		[CLK_SPDIF]		= &spdif_clk.common.hw,
		[CLK_USB_PHY0]		= &usb_phy0_clk.common.hw,
		[CLK_USB_PHY1]		= &usb_phy1_clk.common.hw,
		[CLK_USB_PHY2]		= &usb_phy2_clk.common.hw,
		[CLK_USB_PHY3]		= &usb_phy3_clk.common.hw,
		[CLK_USB_OHCI0]		= &usb_ohci0_clk.common.hw,
		[CLK_USB_OHCI1]		= &usb_ohci1_clk.common.hw,
		[CLK_USB_OHCI2]		= &usb_ohci2_clk.common.hw,
		[CLK_USB_OHCI3]		= &usb_ohci3_clk.common.hw,
		[CLK_DRAM]		= &h5_dram_clk.common.hw,
		[CLK_DRAM_VE]		= &dram_ve_clk.common.hw,
		[CLK_DRAM_CSI]		= &dram_csi_clk.common.hw,
		[CLK_DRAM_DEINTERLACE]	= &dram_deinterlace_clk.common.hw,
		[CLK_DRAM_TS]		= &dram_ts_clk.common.hw,
		[CLK_DE]		= &de_clk.common.hw,
		[CLK_TCON0]		= &tcon_clk.common.hw,
		[CLK_TVE]		= &tve_clk.common.hw,
		[CLK_DEINTERLACE]	= &deinterlace_clk.common.hw,
		[CLK_CSI_MISC]		= &csi_misc_clk.common.hw,
		[CLK_CSI_SCLK]		= &csi_sclk_clk.common.hw,
		[CLK_CSI_MCLK]		= &csi_mclk_clk.common.hw,
		[CLK_VE]		= &ve_clk.common.hw,
		[CLK_AC_DIG]		= &ac_dig_clk.common.hw,
		[CLK_AVS]		= &avs_clk.common.hw,
		[CLK_HDMI]		= &hdmi_clk.common.hw,
		[CLK_HDMI_DDC]		= &hdmi_ddc_clk.common.hw,
		[CLK_MBUS]		= &mbus_clk.common.hw,
		[CLK_GPU]		= &gpu_clk.common.hw,
	},
	.num	= CLK_NUMBER_H5,
};

static struct ccu_reset_map sun8i_h3_ccu_resets[] = {
	[RST_USB_PHY0]		=  { 0x0cc, BIT(0) },
	[RST_USB_PHY1]		=  { 0x0cc, BIT(1) },
	[RST_USB_PHY2]		=  { 0x0cc, BIT(2) },
	[RST_USB_PHY3]		=  { 0x0cc, BIT(3) },

	[RST_MBUS]		=  { 0x0fc, BIT(31) },

	[RST_BUS_CE]		=  { 0x2c0, BIT(5) },
	[RST_BUS_DMA]		=  { 0x2c0, BIT(6) },
	[RST_BUS_MMC0]		=  { 0x2c0, BIT(8) },
	[RST_BUS_MMC1]		=  { 0x2c0, BIT(9) },
	[RST_BUS_MMC2]		=  { 0x2c0, BIT(10) },
	[RST_BUS_NAND]		=  { 0x2c0, BIT(13) },
	[RST_BUS_DRAM]		=  { 0x2c0, BIT(14) },
	[RST_BUS_EMAC]		=  { 0x2c0, BIT(17) },
	[RST_BUS_TS]		=  { 0x2c0, BIT(18) },
	[RST_BUS_HSTIMER]	=  { 0x2c0, BIT(19) },
	[RST_BUS_SPI0]		=  { 0x2c0, BIT(20) },
	[RST_BUS_SPI1]		=  { 0x2c0, BIT(21) },
	[RST_BUS_OTG]		=  { 0x2c0, BIT(23) },
	[RST_BUS_EHCI0]		=  { 0x2c0, BIT(24) },
	[RST_BUS_EHCI1]		=  { 0x2c0, BIT(25) },
	[RST_BUS_EHCI2]		=  { 0x2c0, BIT(26) },
	[RST_BUS_EHCI3]		=  { 0x2c0, BIT(27) },
	[RST_BUS_OHCI0]		=  { 0x2c0, BIT(28) },
	[RST_BUS_OHCI1]		=  { 0x2c0, BIT(29) },
	[RST_BUS_OHCI2]		=  { 0x2c0, BIT(30) },
	[RST_BUS_OHCI3]		=  { 0x2c0, BIT(31) },

	[RST_BUS_VE]		=  { 0x2c4, BIT(0) },
	[RST_BUS_TCON0]		=  { 0x2c4, BIT(3) },
	[RST_BUS_TCON1]		=  { 0x2c4, BIT(4) },
	[RST_BUS_DEINTERLACE]	=  { 0x2c4, BIT(5) },
	[RST_BUS_CSI]		=  { 0x2c4, BIT(8) },
	[RST_BUS_TVE]		=  { 0x2c4, BIT(9) },
	[RST_BUS_HDMI0]		=  { 0x2c4, BIT(10) },
	[RST_BUS_HDMI1]		=  { 0x2c4, BIT(11) },
	[RST_BUS_DE]		=  { 0x2c4, BIT(12) },
	[RST_BUS_GPU]		=  { 0x2c4, BIT(20) },
	[RST_BUS_MSGBOX]	=  { 0x2c4, BIT(21) },
	[RST_BUS_SPINLOCK]	=  { 0x2c4, BIT(22) },
	[RST_BUS_DBG]		=  { 0x2c4, BIT(31) },

	[RST_BUS_EPHY]		=  { 0x2c8, BIT(2) },

	[RST_BUS_CODEC]		=  { 0x2d0, BIT(0) },
	[RST_BUS_SPDIF]		=  { 0x2d0, BIT(1) },
	[RST_BUS_THS]		=  { 0x2d0, BIT(8) },
	[RST_BUS_I2S0]		=  { 0x2d0, BIT(12) },
	[RST_BUS_I2S1]		=  { 0x2d0, BIT(13) },
	[RST_BUS_I2S2]		=  { 0x2d0, BIT(14) },

	[RST_BUS_I2C0]		=  { 0x2d8, BIT(0) },
	[RST_BUS_I2C1]		=  { 0x2d8, BIT(1) },
	[RST_BUS_I2C2]		=  { 0x2d8, BIT(2) },
	[RST_BUS_UART0]		=  { 0x2d8, BIT(16) },
	[RST_BUS_UART1]		=  { 0x2d8, BIT(17) },
	[RST_BUS_UART2]		=  { 0x2d8, BIT(18) },
	[RST_BUS_UART3]		=  { 0x2d8, BIT(19) },
	[RST_BUS_SCR0]		=  { 0x2d8, BIT(20) },
};

static struct ccu_reset_map sun50i_h5_ccu_resets[] = {
	[RST_USB_PHY0]		=  { 0x0cc, BIT(0) },
	[RST_USB_PHY1]		=  { 0x0cc, BIT(1) },
	[RST_USB_PHY2]		=  { 0x0cc, BIT(2) },
	[RST_USB_PHY3]		=  { 0x0cc, BIT(3) },

	[RST_MBUS]		=  { 0x0fc, BIT(31) },

	[RST_BUS_CE]		=  { 0x2c0, BIT(5) },
	[RST_BUS_DMA]		=  { 0x2c0, BIT(6) },
	[RST_BUS_MMC0]		=  { 0x2c0, BIT(8) },
	[RST_BUS_MMC1]		=  { 0x2c0, BIT(9) },
	[RST_BUS_MMC2]		=  { 0x2c0, BIT(10) },
	[RST_BUS_NAND]		=  { 0x2c0, BIT(13) },
	[RST_BUS_DRAM]		=  { 0x2c0, BIT(14) },
	[RST_BUS_EMAC]		=  { 0x2c0, BIT(17) },
	[RST_BUS_TS]		=  { 0x2c0, BIT(18) },
	[RST_BUS_HSTIMER]	=  { 0x2c0, BIT(19) },
	[RST_BUS_SPI0]		=  { 0x2c0, BIT(20) },
	[RST_BUS_SPI1]		=  { 0x2c0, BIT(21) },
	[RST_BUS_OTG]		=  { 0x2c0, BIT(23) },
	[RST_BUS_EHCI0]		=  { 0x2c0, BIT(24) },
	[RST_BUS_EHCI1]		=  { 0x2c0, BIT(25) },
	[RST_BUS_EHCI2]		=  { 0x2c0, BIT(26) },
	[RST_BUS_EHCI3]		=  { 0x2c0, BIT(27) },
	[RST_BUS_OHCI0]		=  { 0x2c0, BIT(28) },
	[RST_BUS_OHCI1]		=  { 0x2c0, BIT(29) },
	[RST_BUS_OHCI2]		=  { 0x2c0, BIT(30) },
	[RST_BUS_OHCI3]		=  { 0x2c0, BIT(31) },

	[RST_BUS_VE]		=  { 0x2c4, BIT(0) },
	[RST_BUS_TCON0]		=  { 0x2c4, BIT(3) },
	[RST_BUS_TCON1]		=  { 0x2c4, BIT(4) },
	[RST_BUS_DEINTERLACE]	=  { 0x2c4, BIT(5) },
	[RST_BUS_CSI]		=  { 0x2c4, BIT(8) },
	[RST_BUS_TVE]		=  { 0x2c4, BIT(9) },
	[RST_BUS_HDMI0]		=  { 0x2c4, BIT(10) },
	[RST_BUS_HDMI1]		=  { 0x2c4, BIT(11) },
	[RST_BUS_DE]		=  { 0x2c4, BIT(12) },
	[RST_BUS_GPU]		=  { 0x2c4, BIT(20) },
	[RST_BUS_MSGBOX]	=  { 0x2c4, BIT(21) },
	[RST_BUS_SPINLOCK]	=  { 0x2c4, BIT(22) },
	[RST_BUS_DBG]		=  { 0x2c4, BIT(31) },

	[RST_BUS_EPHY]		=  { 0x2c8, BIT(2) },

	[RST_BUS_CODEC]		=  { 0x2d0, BIT(0) },
	[RST_BUS_SPDIF]		=  { 0x2d0, BIT(1) },
	[RST_BUS_THS]		=  { 0x2d0, BIT(8) },
	[RST_BUS_I2S0]		=  { 0x2d0, BIT(12) },
	[RST_BUS_I2S1]		=  { 0x2d0, BIT(13) },
	[RST_BUS_I2S2]		=  { 0x2d0, BIT(14) },

	[RST_BUS_I2C0]		=  { 0x2d8, BIT(0) },
	[RST_BUS_I2C1]		=  { 0x2d8, BIT(1) },
	[RST_BUS_I2C2]		=  { 0x2d8, BIT(2) },
	[RST_BUS_UART0]		=  { 0x2d8, BIT(16) },
	[RST_BUS_UART1]		=  { 0x2d8, BIT(17) },
	[RST_BUS_UART2]		=  { 0x2d8, BIT(18) },
	[RST_BUS_UART3]		=  { 0x2d8, BIT(19) },
	[RST_BUS_SCR0]		=  { 0x2d8, BIT(20) },
	[RST_BUS_SCR1]		=  { 0x2d8, BIT(20) },
};

static const struct sunxi_ccu_desc sun8i_h3_ccu_desc = {
	.ccu_clks	= sun8i_h3_ccu_clks,
	.num_ccu_clks	= ARRAY_SIZE(sun8i_h3_ccu_clks),

	.hw_clks	= &sun8i_h3_hw_clks,

	.resets		= sun8i_h3_ccu_resets,
	.num_resets	= ARRAY_SIZE(sun8i_h3_ccu_resets),
};

static const struct sunxi_ccu_desc sun50i_h5_ccu_desc = {
	.ccu_clks	= sun8i_h3_ccu_clks,
	.num_ccu_clks	= ARRAY_SIZE(sun8i_h3_ccu_clks),

	.hw_clks	= &sun50i_h5_hw_clks,

	.resets		= sun50i_h5_ccu_resets,
	.num_resets	= ARRAY_SIZE(sun50i_h5_ccu_resets),
};

static struct ccu_pll_nb sun8i_h3_pll_cpu_nb = {
	.common	= &pll_cpux_clk.common,
	/* copy from pll_cpux_clk */
	.enable	= BIT(31),
	.lock	= BIT(28),
};

static struct ccu_mux_nb sun8i_h3_cpu_nb = {
	.common		= &cpux_clk.common,
	.cm		= &cpux_clk.mux,
	.delay_us	= 1, /* > 8 clock cycles at 24 MHz */
	.bypass_index	= 1, /* index of 24 MHz oscillator */
};

static int sun8i_h3_ccu_probe(struct platform_device *pdev)
{
	const struct sunxi_ccu_desc *desc;
	void __iomem *reg;
	int ret;
	u32 val;

	desc = of_device_get_match_data(&pdev->dev);
	if (!desc)
		return -EINVAL;

	reg = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(reg))
		return PTR_ERR(reg);

	/* Force the PLL-Audio-1x divider to 1 */
	val = readl(reg + SUN8I_H3_PLL_AUDIO_REG);
	val &= ~GENMASK(19, 16);
	writel(val | (0 << 16), reg + SUN8I_H3_PLL_AUDIO_REG);

	ret = devm_sunxi_ccu_probe(&pdev->dev, reg, desc);
	if (ret)
		return ret;

	/* Gate then ungate PLL CPU after any rate changes */
	ccu_pll_notifier_register(&sun8i_h3_pll_cpu_nb);

	/* Reparent CPU during PLL CPU rate changes */
	ccu_mux_notifier_register(pll_cpux_clk.common.hw.clk,
				  &sun8i_h3_cpu_nb);

	return 0;
}

static const struct of_device_id sun8i_h3_ccu_ids[] = {
	{
		.compatible = "allwinner,sun8i-h3-ccu",
		.data = &sun8i_h3_ccu_desc,
	},
	{
		.compatible = "allwinner,sun50i-h5-ccu",
		.data = &sun50i_h5_ccu_desc,
	},
	{ }
};
MODULE_DEVICE_TABLE(of, sun8i_h3_ccu_ids);

static struct platform_driver sun8i_h3_ccu_driver = {
	.probe	= sun8i_h3_ccu_probe,
	.driver	= {
		.name			= "sun8i-h3-ccu",
		.suppress_bind_attrs	= true,
		.of_match_table		= sun8i_h3_ccu_ids,
	},
};
module_platform_driver(sun8i_h3_ccu_driver);

MODULE_IMPORT_NS(SUNXI_CCU);
MODULE_LICENSE("GPL");
