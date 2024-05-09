// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014, The Linux Foundation. All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/bitops.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/clk-provider.h>
#include <linux/regmap.h>

#include <dt-bindings/clock/qcom,lcc-ipq806x.h>

#include "common.h"
#include "clk-regmap.h"
#include "clk-pll.h"
#include "clk-rcg.h"
#include "clk-branch.h"
#include "clk-regmap-divider.h"
#include "clk-regmap-mux.h"
#include "reset.h"

static struct clk_pll pll4 = {
	.l_reg = 0x4,
	.m_reg = 0x8,
	.n_reg = 0xc,
	.config_reg = 0x14,
	.mode_reg = 0x0,
	.status_reg = 0x18,
	.status_bit = 16,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "pll4",
		.parent_data = &(const struct clk_parent_data) {
			.fw_name = "pxo", .name = "pxo_board",
		},
		.num_parents = 1,
		.ops = &clk_pll_ops,
	},
};

static const struct pll_config pll4_config = {
	.l = 0xf,
	.m = 0x91,
	.n = 0xc7,
	.vco_val = 0x0,
	.vco_mask = BIT(17) | BIT(16),
	.pre_div_val = 0x0,
	.pre_div_mask = BIT(19),
	.post_div_val = 0x0,
	.post_div_mask = BIT(21) | BIT(20),
	.mn_ena_mask = BIT(22),
	.main_output_mask = BIT(23),
};

enum {
	P_PXO,
	P_PLL4,
};

static const struct parent_map lcc_pxo_pll4_map[] = {
	{ P_PXO, 0 },
	{ P_PLL4, 2 }
};

static const struct clk_parent_data lcc_pxo_pll4[] = {
	{ .fw_name = "pxo", .name = "pxo_board" },
	{ .fw_name = "pll4_vote", .name = "pll4_vote" },
};

static struct freq_tbl clk_tbl_aif_mi2s[] = {
	{  1024000, P_PLL4, 4,  1,  96 },
	{  1411200, P_PLL4, 4,  2, 139 },
	{  1536000, P_PLL4, 4,  1,  64 },
	{  2048000, P_PLL4, 4,  1,  48 },
	{  2116800, P_PLL4, 4,  2,  93 },
	{  2304000, P_PLL4, 4,  2,  85 },
	{  2822400, P_PLL4, 4,  6, 209 },
	{  3072000, P_PLL4, 4,  1,  32 },
	{  3175200, P_PLL4, 4,  1,  31 },
	{  4096000, P_PLL4, 4,  1,  24 },
	{  4233600, P_PLL4, 4,  9, 209 },
	{  4608000, P_PLL4, 4,  3,  64 },
	{  5644800, P_PLL4, 4, 12, 209 },
	{  6144000, P_PLL4, 4,  1,  16 },
	{  6350400, P_PLL4, 4,  2,  31 },
	{  8192000, P_PLL4, 4,  1,  12 },
	{  8467200, P_PLL4, 4, 18, 209 },
	{  9216000, P_PLL4, 4,  3,  32 },
	{ 11289600, P_PLL4, 4, 24, 209 },
	{ 12288000, P_PLL4, 4,  1,   8 },
	{ 12700800, P_PLL4, 4, 27, 209 },
	{ 13824000, P_PLL4, 4,  9,  64 },
	{ 16384000, P_PLL4, 4,  1,   6 },
	{ 16934400, P_PLL4, 4, 41, 238 },
	{ 18432000, P_PLL4, 4,  3,  16 },
	{ 22579200, P_PLL4, 2, 24, 209 },
	{ 24576000, P_PLL4, 4,  1,   4 },
	{ 27648000, P_PLL4, 4,  9,  32 },
	{ 33868800, P_PLL4, 4, 41, 119 },
	{ 36864000, P_PLL4, 4,  3,   8 },
	{ 45158400, P_PLL4, 1, 24, 209 },
	{ 49152000, P_PLL4, 4,  1,   2 },
	{ 50803200, P_PLL4, 1, 27, 209 },
	{ }
};

static struct clk_rcg mi2s_osr_src = {
	.ns_reg = 0x48,
	.md_reg = 0x4c,
	.mn = {
		.mnctr_en_bit = 8,
		.mnctr_reset_bit = 7,
		.mnctr_mode_shift = 5,
		.n_val_shift = 24,
		.m_val_shift = 8,
		.width = 8,
	},
	.p = {
		.pre_div_shift = 3,
		.pre_div_width = 2,
	},
	.s = {
		.src_sel_shift = 0,
		.parent_map = lcc_pxo_pll4_map,
	},
	.freq_tbl = clk_tbl_aif_mi2s,
	.clkr = {
		.enable_reg = 0x48,
		.enable_mask = BIT(9),
		.hw.init = &(struct clk_init_data){
			.name = "mi2s_osr_src",
			.parent_data = lcc_pxo_pll4,
			.num_parents = ARRAY_SIZE(lcc_pxo_pll4),
			.ops = &clk_rcg_ops,
			.flags = CLK_SET_RATE_GATE,
		},
	},
};

static struct clk_branch mi2s_osr_clk = {
	.halt_reg = 0x50,
	.halt_bit = 1,
	.halt_check = BRANCH_HALT_ENABLE,
	.clkr = {
		.enable_reg = 0x48,
		.enable_mask = BIT(17),
		.hw.init = &(struct clk_init_data){
			.name = "mi2s_osr_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&mi2s_osr_src.clkr.hw,
			},
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_regmap_div mi2s_div_clk = {
	.reg = 0x48,
	.shift = 10,
	.width = 4,
	.clkr = {
		.hw.init = &(struct clk_init_data){
			.name = "mi2s_div_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&mi2s_osr_src.clkr.hw,
			},
			.num_parents = 1,
			.ops = &clk_regmap_div_ops,
		},
	},
};

static struct clk_branch mi2s_bit_div_clk = {
	.halt_reg = 0x50,
	.halt_bit = 0,
	.halt_check = BRANCH_HALT_ENABLE,
	.clkr = {
		.enable_reg = 0x48,
		.enable_mask = BIT(15),
		.hw.init = &(struct clk_init_data){
			.name = "mi2s_bit_div_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&mi2s_div_clk.clkr.hw,
			},
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static const struct clk_parent_data lcc_mi2s_bit_div_codec_clk[] = {
	{ .hw = &mi2s_bit_div_clk.clkr.hw, },
	{ .fw_name = "mi2s_codec", .name = "mi2s_codec_clk" },
};

static struct clk_regmap_mux mi2s_bit_clk = {
	.reg = 0x48,
	.shift = 14,
	.width = 1,
	.clkr = {
		.hw.init = &(struct clk_init_data){
			.name = "mi2s_bit_clk",
			.parent_data = lcc_mi2s_bit_div_codec_clk,
			.num_parents = ARRAY_SIZE(lcc_mi2s_bit_div_codec_clk),
			.ops = &clk_regmap_mux_closest_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct freq_tbl clk_tbl_pcm[] = {
	{   64000, P_PLL4, 4, 1, 1536 },
	{  128000, P_PLL4, 4, 1,  768 },
	{  256000, P_PLL4, 4, 1,  384 },
	{  512000, P_PLL4, 4, 1,  192 },
	{ 1024000, P_PLL4, 4, 1,   96 },
	{ 2048000, P_PLL4, 4, 1,   48 },
	{ },
};

static struct clk_rcg pcm_src = {
	.ns_reg = 0x54,
	.md_reg = 0x58,
	.mn = {
		.mnctr_en_bit = 8,
		.mnctr_reset_bit = 7,
		.mnctr_mode_shift = 5,
		.n_val_shift = 16,
		.m_val_shift = 16,
		.width = 16,
	},
	.p = {
		.pre_div_shift = 3,
		.pre_div_width = 2,
	},
	.s = {
		.src_sel_shift = 0,
		.parent_map = lcc_pxo_pll4_map,
	},
	.freq_tbl = clk_tbl_pcm,
	.clkr = {
		.enable_reg = 0x54,
		.enable_mask = BIT(9),
		.hw.init = &(struct clk_init_data){
			.name = "pcm_src",
			.parent_data = lcc_pxo_pll4,
			.num_parents = ARRAY_SIZE(lcc_pxo_pll4),
			.ops = &clk_rcg_ops,
			.flags = CLK_SET_RATE_GATE,
		},
	},
};

static struct clk_branch pcm_clk_out = {
	.halt_reg = 0x5c,
	.halt_bit = 0,
	.halt_check = BRANCH_HALT_ENABLE,
	.clkr = {
		.enable_reg = 0x54,
		.enable_mask = BIT(11),
		.hw.init = &(struct clk_init_data){
			.name = "pcm_clk_out",
			.parent_hws = (const struct clk_hw*[]) {
				&pcm_src.clkr.hw,
			},
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static const struct clk_parent_data lcc_pcm_clk_out_codec_clk[] = {
	{ .hw = &pcm_clk_out.clkr.hw, },
	{ .fw_name = "pcm_codec_clk", .name = "pcm_codec_clk" },
};

static struct clk_regmap_mux pcm_clk = {
	.reg = 0x54,
	.shift = 10,
	.width = 1,
	.clkr = {
		.hw.init = &(struct clk_init_data){
			.name = "pcm_clk",
			.parent_data = lcc_pcm_clk_out_codec_clk,
			.num_parents = ARRAY_SIZE(lcc_pcm_clk_out_codec_clk),
			.ops = &clk_regmap_mux_closest_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct freq_tbl clk_tbl_aif_osr[] = {
	{  2822400, P_PLL4, 1, 147, 20480 },
	{  4096000, P_PLL4, 1,   1,    96 },
	{  5644800, P_PLL4, 1, 147, 10240 },
	{  6144000, P_PLL4, 1,   1,    64 },
	{ 11289600, P_PLL4, 1, 147,  5120 },
	{ 12288000, P_PLL4, 1,   1,    32 },
	{ 22579200, P_PLL4, 1, 147,  2560 },
	{ 24576000, P_PLL4, 1,   1,    16 },
	{ },
};

static struct clk_rcg spdif_src = {
	.ns_reg = 0xcc,
	.md_reg = 0xd0,
	.mn = {
		.mnctr_en_bit = 8,
		.mnctr_reset_bit = 7,
		.mnctr_mode_shift = 5,
		.n_val_shift = 16,
		.m_val_shift = 16,
		.width = 8,
	},
	.p = {
		.pre_div_shift = 3,
		.pre_div_width = 2,
	},
	.s = {
		.src_sel_shift = 0,
		.parent_map = lcc_pxo_pll4_map,
	},
	.freq_tbl = clk_tbl_aif_osr,
	.clkr = {
		.enable_reg = 0xcc,
		.enable_mask = BIT(9),
		.hw.init = &(struct clk_init_data){
			.name = "spdif_src",
			.parent_data = lcc_pxo_pll4,
			.num_parents = ARRAY_SIZE(lcc_pxo_pll4),
			.ops = &clk_rcg_ops,
			.flags = CLK_SET_RATE_GATE,
		},
	},
};

static struct clk_branch spdif_clk = {
	.halt_reg = 0xd4,
	.halt_bit = 1,
	.halt_check = BRANCH_HALT_ENABLE,
	.clkr = {
		.enable_reg = 0xcc,
		.enable_mask = BIT(12),
		.hw.init = &(struct clk_init_data){
			.name = "spdif_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&spdif_src.clkr.hw,
			},
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct freq_tbl clk_tbl_ahbix[] = {
	{ 131072000, P_PLL4, 1, 1, 3 },
	{ },
};

static struct clk_rcg ahbix_clk = {
	.ns_reg = 0x38,
	.md_reg = 0x3c,
	.mn = {
		.mnctr_en_bit = 8,
		.mnctr_reset_bit = 7,
		.mnctr_mode_shift = 5,
		.n_val_shift = 24,
		.m_val_shift = 8,
		.width = 8,
	},
	.p = {
		.pre_div_shift = 3,
		.pre_div_width = 2,
	},
	.s = {
		.src_sel_shift = 0,
		.parent_map = lcc_pxo_pll4_map,
	},
	.freq_tbl = clk_tbl_ahbix,
	.clkr = {
		.enable_reg = 0x38,
		.enable_mask = BIT(11),
		.hw.init = &(struct clk_init_data){
			.name = "ahbix",
			.parent_data = lcc_pxo_pll4,
			.num_parents = ARRAY_SIZE(lcc_pxo_pll4),
			.ops = &clk_rcg_lcc_ops,
		},
	},
};

static struct clk_regmap *lcc_ipq806x_clks[] = {
	[PLL4] = &pll4.clkr,
	[MI2S_OSR_SRC] = &mi2s_osr_src.clkr,
	[MI2S_OSR_CLK] = &mi2s_osr_clk.clkr,
	[MI2S_DIV_CLK] = &mi2s_div_clk.clkr,
	[MI2S_BIT_DIV_CLK] = &mi2s_bit_div_clk.clkr,
	[MI2S_BIT_CLK] = &mi2s_bit_clk.clkr,
	[PCM_SRC] = &pcm_src.clkr,
	[PCM_CLK_OUT] = &pcm_clk_out.clkr,
	[PCM_CLK] = &pcm_clk.clkr,
	[SPDIF_SRC] = &spdif_src.clkr,
	[SPDIF_CLK] = &spdif_clk.clkr,
	[AHBIX_CLK] = &ahbix_clk.clkr,
};

static const struct qcom_reset_map lcc_ipq806x_resets[] = {
	[LCC_PCM_RESET] = { 0x54, 13 },
};

static const struct regmap_config lcc_ipq806x_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= 0xfc,
	.fast_io	= true,
};

static const struct qcom_cc_desc lcc_ipq806x_desc = {
	.config = &lcc_ipq806x_regmap_config,
	.clks = lcc_ipq806x_clks,
	.num_clks = ARRAY_SIZE(lcc_ipq806x_clks),
	.resets = lcc_ipq806x_resets,
	.num_resets = ARRAY_SIZE(lcc_ipq806x_resets),
};

static const struct of_device_id lcc_ipq806x_match_table[] = {
	{ .compatible = "qcom,lcc-ipq8064" },
	{ }
};
MODULE_DEVICE_TABLE(of, lcc_ipq806x_match_table);

static int lcc_ipq806x_probe(struct platform_device *pdev)
{
	u32 val;
	struct regmap *regmap;

	regmap = qcom_cc_map(pdev, &lcc_ipq806x_desc);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	/* Configure the rate of PLL4 if the bootloader hasn't already */
	regmap_read(regmap, 0x0, &val);
	if (!val)
		clk_pll_configure_sr(&pll4, regmap, &pll4_config, true);
	/* Enable PLL4 source on the LPASS Primary PLL Mux */
	regmap_write(regmap, 0xc4, 0x1);

	return qcom_cc_really_probe(pdev, &lcc_ipq806x_desc, regmap);
}

static struct platform_driver lcc_ipq806x_driver = {
	.probe		= lcc_ipq806x_probe,
	.driver		= {
		.name	= "lcc-ipq806x",
		.of_match_table = lcc_ipq806x_match_table,
	},
};
module_platform_driver(lcc_ipq806x_driver);

MODULE_DESCRIPTION("QCOM LCC IPQ806x Driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:lcc-ipq806x");
