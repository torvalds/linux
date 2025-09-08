/* SPDX-License-Identifier: GPL-2.0 */
/*
 * R-Car Gen2 Clock Pulse Generator
 *
 * Copyright (C) 2016 Cogent Embedded Inc.
 */

#ifndef __CLK_RENESAS_RCAR_GEN2_CPG_H__
#define __CLK_RENESAS_RCAR_GEN2_CPG_H__

enum rcar_gen2_clk_types {
	CLK_TYPE_GEN2_MAIN = CLK_TYPE_CUSTOM,
	CLK_TYPE_GEN2_PLL0,
	CLK_TYPE_GEN2_PLL1,
	CLK_TYPE_GEN2_PLL3,
	CLK_TYPE_GEN2_Z,
	CLK_TYPE_GEN2_LB,
	CLK_TYPE_GEN2_ADSP,
	CLK_TYPE_GEN2_SDH,
	CLK_TYPE_GEN2_SD0,
	CLK_TYPE_GEN2_SD1,
	CLK_TYPE_GEN2_QSPI,
	CLK_TYPE_GEN2_RCAN,
};

struct rcar_gen2_cpg_pll_config {
	u8 extal_div;
	u8 pll1_mult;
	u8 pll3_mult;
	u8 pll0_mult;		/* leave as zero if PLL0CR exists */
};

struct clk *rcar_gen2_cpg_clk_register(struct device *dev,
	const struct cpg_core_clk *core, const struct cpg_mssr_info *info,
	struct cpg_mssr_pub *pub);
int rcar_gen2_cpg_init(const struct rcar_gen2_cpg_pll_config *config,
		       unsigned int pll0_div, u32 mode);

#endif
