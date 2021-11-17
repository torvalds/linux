// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2014 Marvell Technology Group Ltd.
 *
 * Sebastian Hesselbarth <sebastian.hesselbarth@gmail.com>
 * Alexandre Belloni <alexandre.belloni@free-electrons.com>
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/slab.h>

#include <dt-bindings/clock/berlin2.h>

#include "berlin2-avpll.h"
#include "berlin2-div.h"
#include "berlin2-pll.h"
#include "common.h"

#define REG_PINMUX0		0x0000
#define REG_PINMUX1		0x0004
#define REG_SYSPLLCTL0		0x0014
#define REG_SYSPLLCTL4		0x0024
#define REG_MEMPLLCTL0		0x0028
#define REG_MEMPLLCTL4		0x0038
#define REG_CPUPLLCTL0		0x003c
#define REG_CPUPLLCTL4		0x004c
#define REG_AVPLLCTL0		0x0050
#define REG_AVPLLCTL31		0x00cc
#define REG_AVPLLCTL62		0x0148
#define REG_PLLSTATUS		0x014c
#define REG_CLKENABLE		0x0150
#define REG_CLKSELECT0		0x0154
#define REG_CLKSELECT1		0x0158
#define REG_CLKSELECT2		0x015c
#define REG_CLKSELECT3		0x0160
#define REG_CLKSWITCH0		0x0164
#define REG_CLKSWITCH1		0x0168
#define REG_RESET_TRIGGER	0x0178
#define REG_RESET_STATUS0	0x017c
#define REG_RESET_STATUS1	0x0180
#define REG_SW_GENERIC0		0x0184
#define REG_SW_GENERIC3		0x0190
#define REG_PRODUCTID		0x01cc
#define REG_PRODUCTID_EXT	0x01d0
#define REG_GFX3DCORE_CLKCTL	0x022c
#define REG_GFX3DSYS_CLKCTL	0x0230
#define REG_ARC_CLKCTL		0x0234
#define REG_VIP_CLKCTL		0x0238
#define REG_SDIO0XIN_CLKCTL	0x023c
#define REG_SDIO1XIN_CLKCTL	0x0240
#define REG_GFX3DEXTRA_CLKCTL	0x0244
#define REG_GFX3D_RESET		0x0248
#define REG_GC360_CLKCTL	0x024c
#define REG_SDIO_DLLMST_CLKCTL	0x0250

/*
 * BG2/BG2CD SoCs have the following audio/video I/O units:
 *
 * audiohd: HDMI TX audio
 * audio0:  7.1ch TX
 * audio1:  2ch TX
 * audio2:  2ch RX
 * audio3:  SPDIF TX
 * video0:  HDMI video
 * video1:  Secondary video
 * video2:  SD auxiliary video
 *
 * There are no external audio clocks (ACLKI0, ACLKI1) and
 * only one external video clock (VCLKI0).
 *
 * Currently missing bits and pieces:
 * - audio_fast_pll is unknown
 * - audiohd_pll is unknown
 * - video0_pll is unknown
 * - audio[023], audiohd parent pll is assumed to be audio_fast_pll
 *
 */

#define	MAX_CLKS 41
static struct clk_hw_onecell_data *clk_data;
static DEFINE_SPINLOCK(lock);
static void __iomem *gbase;

enum {
	REFCLK, VIDEO_EXT0,
	SYSPLL, MEMPLL, CPUPLL,
	AVPLL_A1, AVPLL_A2, AVPLL_A3, AVPLL_A4,
	AVPLL_A5, AVPLL_A6, AVPLL_A7, AVPLL_A8,
	AVPLL_B1, AVPLL_B2, AVPLL_B3, AVPLL_B4,
	AVPLL_B5, AVPLL_B6, AVPLL_B7, AVPLL_B8,
	AUDIO1_PLL, AUDIO_FAST_PLL,
	VIDEO0_PLL, VIDEO0_IN,
	VIDEO1_PLL, VIDEO1_IN,
	VIDEO2_PLL, VIDEO2_IN,
};

static const char *clk_names[] = {
	[REFCLK]		= "refclk",
	[VIDEO_EXT0]		= "video_ext0",
	[SYSPLL]		= "syspll",
	[MEMPLL]		= "mempll",
	[CPUPLL]		= "cpupll",
	[AVPLL_A1]		= "avpll_a1",
	[AVPLL_A2]		= "avpll_a2",
	[AVPLL_A3]		= "avpll_a3",
	[AVPLL_A4]		= "avpll_a4",
	[AVPLL_A5]		= "avpll_a5",
	[AVPLL_A6]		= "avpll_a6",
	[AVPLL_A7]		= "avpll_a7",
	[AVPLL_A8]		= "avpll_a8",
	[AVPLL_B1]		= "avpll_b1",
	[AVPLL_B2]		= "avpll_b2",
	[AVPLL_B3]		= "avpll_b3",
	[AVPLL_B4]		= "avpll_b4",
	[AVPLL_B5]		= "avpll_b5",
	[AVPLL_B6]		= "avpll_b6",
	[AVPLL_B7]		= "avpll_b7",
	[AVPLL_B8]		= "avpll_b8",
	[AUDIO1_PLL]		= "audio1_pll",
	[AUDIO_FAST_PLL]	= "audio_fast_pll",
	[VIDEO0_PLL]		= "video0_pll",
	[VIDEO0_IN]		= "video0_in",
	[VIDEO1_PLL]		= "video1_pll",
	[VIDEO1_IN]		= "video1_in",
	[VIDEO2_PLL]		= "video2_pll",
	[VIDEO2_IN]		= "video2_in",
};

static const struct berlin2_pll_map bg2_pll_map __initconst = {
	.vcodiv		= {10, 15, 20, 25, 30, 40, 50, 60, 80},
	.mult		= 10,
	.fbdiv_shift	= 6,
	.rfdiv_shift	= 1,
	.divsel_shift	= 7,
};

static const u8 default_parent_ids[] = {
	SYSPLL, AVPLL_B4, AVPLL_A5, AVPLL_B6, AVPLL_B7, SYSPLL
};

static const struct berlin2_div_data bg2_divs[] __initconst = {
	{
		.name = "sys",
		.parent_ids = (const u8 []){
			SYSPLL, AVPLL_B4, AVPLL_B5, AVPLL_B6, AVPLL_B7, SYSPLL
		},
		.num_parents = 6,
		.map = {
			BERLIN2_DIV_GATE(REG_CLKENABLE, 0),
			BERLIN2_PLL_SELECT(REG_CLKSELECT0, 0),
			BERLIN2_DIV_SELECT(REG_CLKSELECT0, 3),
			BERLIN2_PLL_SWITCH(REG_CLKSWITCH0, 3),
			BERLIN2_DIV_SWITCH(REG_CLKSWITCH0, 4),
			BERLIN2_DIV_D3SWITCH(REG_CLKSWITCH0, 5),
		},
		.div_flags = BERLIN2_DIV_HAS_GATE | BERLIN2_DIV_HAS_MUX,
		.flags = CLK_IGNORE_UNUSED,
	},
	{
		.name = "cpu",
		.parent_ids = (const u8 []){
			CPUPLL, MEMPLL, MEMPLL, MEMPLL, MEMPLL
		},
		.num_parents = 5,
		.map = {
			BERLIN2_PLL_SELECT(REG_CLKSELECT0, 6),
			BERLIN2_DIV_SELECT(REG_CLKSELECT0, 9),
			BERLIN2_PLL_SWITCH(REG_CLKSWITCH0, 6),
			BERLIN2_DIV_SWITCH(REG_CLKSWITCH0, 7),
			BERLIN2_DIV_D3SWITCH(REG_CLKSWITCH0, 8),
		},
		.div_flags = BERLIN2_DIV_HAS_MUX,
		.flags = 0,
	},
	{
		.name = "drmfigo",
		.parent_ids = default_parent_ids,
		.num_parents = ARRAY_SIZE(default_parent_ids),
		.map = {
			BERLIN2_DIV_GATE(REG_CLKENABLE, 16),
			BERLIN2_PLL_SELECT(REG_CLKSELECT0, 17),
			BERLIN2_DIV_SELECT(REG_CLKSELECT0, 20),
			BERLIN2_PLL_SWITCH(REG_CLKSWITCH0, 12),
			BERLIN2_DIV_SWITCH(REG_CLKSWITCH0, 13),
			BERLIN2_DIV_D3SWITCH(REG_CLKSWITCH0, 14),
		},
		.div_flags = BERLIN2_DIV_HAS_GATE | BERLIN2_DIV_HAS_MUX,
		.flags = 0,
	},
	{
		.name = "cfg",
		.parent_ids = default_parent_ids,
		.num_parents = ARRAY_SIZE(default_parent_ids),
		.map = {
			BERLIN2_DIV_GATE(REG_CLKENABLE, 1),
			BERLIN2_PLL_SELECT(REG_CLKSELECT0, 23),
			BERLIN2_DIV_SELECT(REG_CLKSELECT0, 26),
			BERLIN2_PLL_SWITCH(REG_CLKSWITCH0, 15),
			BERLIN2_DIV_SWITCH(REG_CLKSWITCH0, 16),
			BERLIN2_DIV_D3SWITCH(REG_CLKSWITCH0, 17),
		},
		.div_flags = BERLIN2_DIV_HAS_GATE | BERLIN2_DIV_HAS_MUX,
		.flags = 0,
	},
	{
		.name = "gfx",
		.parent_ids = default_parent_ids,
		.num_parents = ARRAY_SIZE(default_parent_ids),
		.map = {
			BERLIN2_DIV_GATE(REG_CLKENABLE, 4),
			BERLIN2_PLL_SELECT(REG_CLKSELECT0, 29),
			BERLIN2_DIV_SELECT(REG_CLKSELECT1, 0),
			BERLIN2_PLL_SWITCH(REG_CLKSWITCH0, 18),
			BERLIN2_DIV_SWITCH(REG_CLKSWITCH0, 19),
			BERLIN2_DIV_D3SWITCH(REG_CLKSWITCH0, 20),
		},
		.div_flags = BERLIN2_DIV_HAS_GATE | BERLIN2_DIV_HAS_MUX,
		.flags = 0,
	},
	{
		.name = "zsp",
		.parent_ids = default_parent_ids,
		.num_parents = ARRAY_SIZE(default_parent_ids),
		.map = {
			BERLIN2_DIV_GATE(REG_CLKENABLE, 5),
			BERLIN2_PLL_SELECT(REG_CLKSELECT1, 3),
			BERLIN2_DIV_SELECT(REG_CLKSELECT1, 6),
			BERLIN2_PLL_SWITCH(REG_CLKSWITCH0, 21),
			BERLIN2_DIV_SWITCH(REG_CLKSWITCH0, 22),
			BERLIN2_DIV_D3SWITCH(REG_CLKSWITCH0, 23),
		},
		.div_flags = BERLIN2_DIV_HAS_GATE | BERLIN2_DIV_HAS_MUX,
		.flags = 0,
	},
	{
		.name = "perif",
		.parent_ids = default_parent_ids,
		.num_parents = ARRAY_SIZE(default_parent_ids),
		.map = {
			BERLIN2_DIV_GATE(REG_CLKENABLE, 6),
			BERLIN2_PLL_SELECT(REG_CLKSELECT1, 9),
			BERLIN2_DIV_SELECT(REG_CLKSELECT1, 12),
			BERLIN2_PLL_SWITCH(REG_CLKSWITCH0, 24),
			BERLIN2_DIV_SWITCH(REG_CLKSWITCH0, 25),
			BERLIN2_DIV_D3SWITCH(REG_CLKSWITCH0, 26),
		},
		.div_flags = BERLIN2_DIV_HAS_GATE | BERLIN2_DIV_HAS_MUX,
		.flags = CLK_IGNORE_UNUSED,
	},
	{
		.name = "pcube",
		.parent_ids = default_parent_ids,
		.num_parents = ARRAY_SIZE(default_parent_ids),
		.map = {
			BERLIN2_DIV_GATE(REG_CLKENABLE, 2),
			BERLIN2_PLL_SELECT(REG_CLKSELECT1, 15),
			BERLIN2_DIV_SELECT(REG_CLKSELECT1, 18),
			BERLIN2_PLL_SWITCH(REG_CLKSWITCH0, 27),
			BERLIN2_DIV_SWITCH(REG_CLKSWITCH0, 28),
			BERLIN2_DIV_D3SWITCH(REG_CLKSWITCH0, 29),
		},
		.div_flags = BERLIN2_DIV_HAS_GATE | BERLIN2_DIV_HAS_MUX,
		.flags = 0,
	},
	{
		.name = "vscope",
		.parent_ids = default_parent_ids,
		.num_parents = ARRAY_SIZE(default_parent_ids),
		.map = {
			BERLIN2_DIV_GATE(REG_CLKENABLE, 3),
			BERLIN2_PLL_SELECT(REG_CLKSELECT1, 21),
			BERLIN2_DIV_SELECT(REG_CLKSELECT1, 24),
			BERLIN2_PLL_SWITCH(REG_CLKSWITCH0, 30),
			BERLIN2_DIV_SWITCH(REG_CLKSWITCH0, 31),
			BERLIN2_DIV_D3SWITCH(REG_CLKSWITCH1, 0),
		},
		.div_flags = BERLIN2_DIV_HAS_GATE | BERLIN2_DIV_HAS_MUX,
		.flags = 0,
	},
	{
		.name = "nfc_ecc",
		.parent_ids = default_parent_ids,
		.num_parents = ARRAY_SIZE(default_parent_ids),
		.map = {
			BERLIN2_DIV_GATE(REG_CLKENABLE, 18),
			BERLIN2_PLL_SELECT(REG_CLKSELECT1, 27),
			BERLIN2_DIV_SELECT(REG_CLKSELECT2, 0),
			BERLIN2_PLL_SWITCH(REG_CLKSWITCH1, 1),
			BERLIN2_DIV_SWITCH(REG_CLKSWITCH1, 2),
			BERLIN2_DIV_D3SWITCH(REG_CLKSWITCH1, 3),
		},
		.div_flags = BERLIN2_DIV_HAS_GATE | BERLIN2_DIV_HAS_MUX,
		.flags = 0,
	},
	{
		.name = "vpp",
		.parent_ids = default_parent_ids,
		.num_parents = ARRAY_SIZE(default_parent_ids),
		.map = {
			BERLIN2_DIV_GATE(REG_CLKENABLE, 21),
			BERLIN2_PLL_SELECT(REG_CLKSELECT2, 3),
			BERLIN2_DIV_SELECT(REG_CLKSELECT2, 6),
			BERLIN2_PLL_SWITCH(REG_CLKSWITCH1, 4),
			BERLIN2_DIV_SWITCH(REG_CLKSWITCH1, 5),
			BERLIN2_DIV_D3SWITCH(REG_CLKSWITCH1, 6),
		},
		.div_flags = BERLIN2_DIV_HAS_GATE | BERLIN2_DIV_HAS_MUX,
		.flags = 0,
	},
	{
		.name = "app",
		.parent_ids = default_parent_ids,
		.num_parents = ARRAY_SIZE(default_parent_ids),
		.map = {
			BERLIN2_DIV_GATE(REG_CLKENABLE, 20),
			BERLIN2_PLL_SELECT(REG_CLKSELECT2, 9),
			BERLIN2_DIV_SELECT(REG_CLKSELECT2, 12),
			BERLIN2_PLL_SWITCH(REG_CLKSWITCH1, 7),
			BERLIN2_DIV_SWITCH(REG_CLKSWITCH1, 8),
			BERLIN2_DIV_D3SWITCH(REG_CLKSWITCH1, 9),
		},
		.div_flags = BERLIN2_DIV_HAS_GATE | BERLIN2_DIV_HAS_MUX,
		.flags = 0,
	},
	{
		.name = "audio0",
		.parent_ids = (const u8 []){ AUDIO_FAST_PLL },
		.num_parents = 1,
		.map = {
			BERLIN2_DIV_GATE(REG_CLKENABLE, 22),
			BERLIN2_DIV_SELECT(REG_CLKSELECT2, 17),
			BERLIN2_DIV_SWITCH(REG_CLKSWITCH1, 10),
			BERLIN2_DIV_D3SWITCH(REG_CLKSWITCH1, 11),
		},
		.div_flags = BERLIN2_DIV_HAS_GATE,
		.flags = 0,
	},
	{
		.name = "audio2",
		.parent_ids = (const u8 []){ AUDIO_FAST_PLL },
		.num_parents = 1,
		.map = {
			BERLIN2_DIV_GATE(REG_CLKENABLE, 24),
			BERLIN2_DIV_SELECT(REG_CLKSELECT2, 20),
			BERLIN2_DIV_SWITCH(REG_CLKSWITCH1, 14),
			BERLIN2_DIV_D3SWITCH(REG_CLKSWITCH1, 15),
		},
		.div_flags = BERLIN2_DIV_HAS_GATE,
		.flags = 0,
	},
	{
		.name = "audio3",
		.parent_ids = (const u8 []){ AUDIO_FAST_PLL },
		.num_parents = 1,
		.map = {
			BERLIN2_DIV_GATE(REG_CLKENABLE, 25),
			BERLIN2_DIV_SELECT(REG_CLKSELECT2, 23),
			BERLIN2_DIV_SWITCH(REG_CLKSWITCH1, 16),
			BERLIN2_DIV_D3SWITCH(REG_CLKSWITCH1, 17),
		},
		.div_flags = BERLIN2_DIV_HAS_GATE,
		.flags = 0,
	},
	{
		.name = "audio1",
		.parent_ids = (const u8 []){ AUDIO1_PLL },
		.num_parents = 1,
		.map = {
			BERLIN2_DIV_GATE(REG_CLKENABLE, 23),
			BERLIN2_DIV_SELECT(REG_CLKSELECT3, 0),
			BERLIN2_DIV_SWITCH(REG_CLKSWITCH1, 12),
			BERLIN2_DIV_D3SWITCH(REG_CLKSWITCH1, 13),
		},
		.div_flags = BERLIN2_DIV_HAS_GATE,
		.flags = 0,
	},
	{
		.name = "gfx3d_core",
		.parent_ids = default_parent_ids,
		.num_parents = ARRAY_SIZE(default_parent_ids),
		.map = {
			BERLIN2_SINGLE_DIV(REG_GFX3DCORE_CLKCTL),
		},
		.div_flags = BERLIN2_DIV_HAS_GATE | BERLIN2_DIV_HAS_MUX,
		.flags = 0,
	},
	{
		.name = "gfx3d_sys",
		.parent_ids = default_parent_ids,
		.num_parents = ARRAY_SIZE(default_parent_ids),
		.map = {
			BERLIN2_SINGLE_DIV(REG_GFX3DSYS_CLKCTL),
		},
		.div_flags = BERLIN2_DIV_HAS_GATE | BERLIN2_DIV_HAS_MUX,
		.flags = 0,
	},
	{
		.name = "arc",
		.parent_ids = default_parent_ids,
		.num_parents = ARRAY_SIZE(default_parent_ids),
		.map = {
			BERLIN2_SINGLE_DIV(REG_ARC_CLKCTL),
		},
		.div_flags = BERLIN2_DIV_HAS_GATE | BERLIN2_DIV_HAS_MUX,
		.flags = 0,
	},
	{
		.name = "vip",
		.parent_ids = default_parent_ids,
		.num_parents = ARRAY_SIZE(default_parent_ids),
		.map = {
			BERLIN2_SINGLE_DIV(REG_VIP_CLKCTL),
		},
		.div_flags = BERLIN2_DIV_HAS_GATE | BERLIN2_DIV_HAS_MUX,
		.flags = 0,
	},
	{
		.name = "sdio0xin",
		.parent_ids = default_parent_ids,
		.num_parents = ARRAY_SIZE(default_parent_ids),
		.map = {
			BERLIN2_SINGLE_DIV(REG_SDIO0XIN_CLKCTL),
		},
		.div_flags = BERLIN2_DIV_HAS_GATE | BERLIN2_DIV_HAS_MUX,
		.flags = 0,
	},
	{
		.name = "sdio1xin",
		.parent_ids = default_parent_ids,
		.num_parents = ARRAY_SIZE(default_parent_ids),
		.map = {
			BERLIN2_SINGLE_DIV(REG_SDIO1XIN_CLKCTL),
		},
		.div_flags = BERLIN2_DIV_HAS_GATE | BERLIN2_DIV_HAS_MUX,
		.flags = 0,
	},
	{
		.name = "gfx3d_extra",
		.parent_ids = default_parent_ids,
		.num_parents = ARRAY_SIZE(default_parent_ids),
		.map = {
			BERLIN2_SINGLE_DIV(REG_GFX3DEXTRA_CLKCTL),
		},
		.div_flags = BERLIN2_DIV_HAS_GATE | BERLIN2_DIV_HAS_MUX,
		.flags = 0,
	},
	{
		.name = "gc360",
		.parent_ids = default_parent_ids,
		.num_parents = ARRAY_SIZE(default_parent_ids),
		.map = {
			BERLIN2_SINGLE_DIV(REG_GC360_CLKCTL),
		},
		.div_flags = BERLIN2_DIV_HAS_GATE | BERLIN2_DIV_HAS_MUX,
		.flags = 0,
	},
	{
		.name = "sdio_dllmst",
		.parent_ids = default_parent_ids,
		.num_parents = ARRAY_SIZE(default_parent_ids),
		.map = {
			BERLIN2_SINGLE_DIV(REG_SDIO_DLLMST_CLKCTL),
		},
		.div_flags = BERLIN2_DIV_HAS_GATE | BERLIN2_DIV_HAS_MUX,
		.flags = 0,
	},
};

static const struct berlin2_gate_data bg2_gates[] __initconst = {
	{ "geth0",	"perif",	7 },
	{ "geth1",	"perif",	8 },
	{ "sata",	"perif",	9 },
	{ "ahbapb",	"perif",	10, CLK_IGNORE_UNUSED },
	{ "usb0",	"perif",	11 },
	{ "usb1",	"perif",	12 },
	{ "pbridge",	"perif",	13, CLK_IGNORE_UNUSED },
	{ "sdio0",	"perif",	14 },
	{ "sdio1",	"perif",	15 },
	{ "nfc",	"perif",	17 },
	{ "smemc",	"perif",	19 },
	{ "audiohd",	"audiohd_pll",	26 },
	{ "video0",	"video0_in",	27 },
	{ "video1",	"video1_in",	28 },
	{ "video2",	"video2_in",	29 },
};

static void __init berlin2_clock_setup(struct device_node *np)
{
	struct device_node *parent_np = of_get_parent(np);
	const char *parent_names[9];
	struct clk *clk;
	struct clk_hw *hw;
	struct clk_hw **hws;
	u8 avpll_flags = 0;
	int n, ret;

	clk_data = kzalloc(struct_size(clk_data, hws, MAX_CLKS), GFP_KERNEL);
	if (!clk_data)
		return;
	clk_data->num = MAX_CLKS;
	hws = clk_data->hws;

	gbase = of_iomap(parent_np, 0);
	if (!gbase)
		return;

	/* overwrite default clock names with DT provided ones */
	clk = of_clk_get_by_name(np, clk_names[REFCLK]);
	if (!IS_ERR(clk)) {
		clk_names[REFCLK] = __clk_get_name(clk);
		clk_put(clk);
	}

	clk = of_clk_get_by_name(np, clk_names[VIDEO_EXT0]);
	if (!IS_ERR(clk)) {
		clk_names[VIDEO_EXT0] = __clk_get_name(clk);
		clk_put(clk);
	}

	/* simple register PLLs */
	ret = berlin2_pll_register(&bg2_pll_map, gbase + REG_SYSPLLCTL0,
				   clk_names[SYSPLL], clk_names[REFCLK], 0);
	if (ret)
		goto bg2_fail;

	ret = berlin2_pll_register(&bg2_pll_map, gbase + REG_MEMPLLCTL0,
				   clk_names[MEMPLL], clk_names[REFCLK], 0);
	if (ret)
		goto bg2_fail;

	ret = berlin2_pll_register(&bg2_pll_map, gbase + REG_CPUPLLCTL0,
				   clk_names[CPUPLL], clk_names[REFCLK], 0);
	if (ret)
		goto bg2_fail;

	if (of_device_is_compatible(np, "marvell,berlin2-global-register"))
		avpll_flags |= BERLIN2_AVPLL_SCRAMBLE_QUIRK;

	/* audio/video VCOs */
	ret = berlin2_avpll_vco_register(gbase + REG_AVPLLCTL0, "avpll_vcoA",
			 clk_names[REFCLK], avpll_flags, 0);
	if (ret)
		goto bg2_fail;

	for (n = 0; n < 8; n++) {
		ret = berlin2_avpll_channel_register(gbase + REG_AVPLLCTL0,
			     clk_names[AVPLL_A1 + n], n, "avpll_vcoA",
			     avpll_flags, 0);
		if (ret)
			goto bg2_fail;
	}

	ret = berlin2_avpll_vco_register(gbase + REG_AVPLLCTL31, "avpll_vcoB",
				 clk_names[REFCLK], BERLIN2_AVPLL_BIT_QUIRK |
				 avpll_flags, 0);
	if (ret)
		goto bg2_fail;

	for (n = 0; n < 8; n++) {
		ret = berlin2_avpll_channel_register(gbase + REG_AVPLLCTL31,
			     clk_names[AVPLL_B1 + n], n, "avpll_vcoB",
			     BERLIN2_AVPLL_BIT_QUIRK | avpll_flags, 0);
		if (ret)
			goto bg2_fail;
	}

	/* reference clock bypass switches */
	parent_names[0] = clk_names[SYSPLL];
	parent_names[1] = clk_names[REFCLK];
	hw = clk_hw_register_mux(NULL, "syspll_byp", parent_names, 2,
			       0, gbase + REG_CLKSWITCH0, 0, 1, 0, &lock);
	if (IS_ERR(hw))
		goto bg2_fail;
	clk_names[SYSPLL] = clk_hw_get_name(hw);

	parent_names[0] = clk_names[MEMPLL];
	parent_names[1] = clk_names[REFCLK];
	hw = clk_hw_register_mux(NULL, "mempll_byp", parent_names, 2,
			       0, gbase + REG_CLKSWITCH0, 1, 1, 0, &lock);
	if (IS_ERR(hw))
		goto bg2_fail;
	clk_names[MEMPLL] = clk_hw_get_name(hw);

	parent_names[0] = clk_names[CPUPLL];
	parent_names[1] = clk_names[REFCLK];
	hw = clk_hw_register_mux(NULL, "cpupll_byp", parent_names, 2,
			       0, gbase + REG_CLKSWITCH0, 2, 1, 0, &lock);
	if (IS_ERR(hw))
		goto bg2_fail;
	clk_names[CPUPLL] = clk_hw_get_name(hw);

	/* clock muxes */
	parent_names[0] = clk_names[AVPLL_B3];
	parent_names[1] = clk_names[AVPLL_A3];
	hw = clk_hw_register_mux(NULL, clk_names[AUDIO1_PLL], parent_names, 2,
			       0, gbase + REG_CLKSELECT2, 29, 1, 0, &lock);
	if (IS_ERR(hw))
		goto bg2_fail;

	parent_names[0] = clk_names[VIDEO0_PLL];
	parent_names[1] = clk_names[VIDEO_EXT0];
	hw = clk_hw_register_mux(NULL, clk_names[VIDEO0_IN], parent_names, 2,
			       0, gbase + REG_CLKSELECT3, 4, 1, 0, &lock);
	if (IS_ERR(hw))
		goto bg2_fail;

	parent_names[0] = clk_names[VIDEO1_PLL];
	parent_names[1] = clk_names[VIDEO_EXT0];
	hw = clk_hw_register_mux(NULL, clk_names[VIDEO1_IN], parent_names, 2,
			       0, gbase + REG_CLKSELECT3, 6, 1, 0, &lock);
	if (IS_ERR(hw))
		goto bg2_fail;

	parent_names[0] = clk_names[AVPLL_A2];
	parent_names[1] = clk_names[AVPLL_B2];
	hw = clk_hw_register_mux(NULL, clk_names[VIDEO1_PLL], parent_names, 2,
			       0, gbase + REG_CLKSELECT3, 7, 1, 0, &lock);
	if (IS_ERR(hw))
		goto bg2_fail;

	parent_names[0] = clk_names[VIDEO2_PLL];
	parent_names[1] = clk_names[VIDEO_EXT0];
	hw = clk_hw_register_mux(NULL, clk_names[VIDEO2_IN], parent_names, 2,
			       0, gbase + REG_CLKSELECT3, 9, 1, 0, &lock);
	if (IS_ERR(hw))
		goto bg2_fail;

	parent_names[0] = clk_names[AVPLL_B1];
	parent_names[1] = clk_names[AVPLL_A5];
	hw = clk_hw_register_mux(NULL, clk_names[VIDEO2_PLL], parent_names, 2,
			       0, gbase + REG_CLKSELECT3, 10, 1, 0, &lock);
	if (IS_ERR(hw))
		goto bg2_fail;

	/* clock divider cells */
	for (n = 0; n < ARRAY_SIZE(bg2_divs); n++) {
		const struct berlin2_div_data *dd = &bg2_divs[n];
		int k;

		for (k = 0; k < dd->num_parents; k++)
			parent_names[k] = clk_names[dd->parent_ids[k]];

		hws[CLKID_SYS + n] = berlin2_div_register(&dd->map, gbase,
				dd->name, dd->div_flags, parent_names,
				dd->num_parents, dd->flags, &lock);
	}

	/* clock gate cells */
	for (n = 0; n < ARRAY_SIZE(bg2_gates); n++) {
		const struct berlin2_gate_data *gd = &bg2_gates[n];

		hws[CLKID_GETH0 + n] = clk_hw_register_gate(NULL, gd->name,
			    gd->parent_name, gd->flags, gbase + REG_CLKENABLE,
			    gd->bit_idx, 0, &lock);
	}

	/* twdclk is derived from cpu/3 */
	hws[CLKID_TWD] =
		clk_hw_register_fixed_factor(NULL, "twd", "cpu", 0, 1, 3);

	/* check for errors on leaf clocks */
	for (n = 0; n < MAX_CLKS; n++) {
		if (!IS_ERR(hws[n]))
			continue;

		pr_err("%pOF: Unable to register leaf clock %d\n", np, n);
		goto bg2_fail;
	}

	/* register clk-provider */
	of_clk_add_hw_provider(np, of_clk_hw_onecell_get, clk_data);

	return;

bg2_fail:
	iounmap(gbase);
}
CLK_OF_DECLARE(berlin2_clk, "marvell,berlin2-clk",
	       berlin2_clock_setup);
