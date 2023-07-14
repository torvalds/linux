/* SPDX-License-Identifier: GPL-2.0 */
/*
 * R-Car Gen4 Clock Pulse Generator
 *
 * Copyright (C) 2021 Renesas Electronics Corp.
 *
 */

#ifndef __CLK_RENESAS_RCAR_GEN4_CPG_H__
#define __CLK_RENESAS_RCAR_GEN4_CPG_H__

enum rcar_gen4_clk_types {
	CLK_TYPE_GEN4_MAIN = CLK_TYPE_CUSTOM,
	CLK_TYPE_GEN4_PLL1,
	CLK_TYPE_GEN4_PLL2,
	CLK_TYPE_GEN4_PLL2_VAR,
	CLK_TYPE_GEN4_PLL2X_3X,	/* r8a779a0 only */
	CLK_TYPE_GEN4_PLL3,
	CLK_TYPE_GEN4_PLL4,
	CLK_TYPE_GEN4_PLL5,
	CLK_TYPE_GEN4_PLL6,
	CLK_TYPE_GEN4_SDSRC,
	CLK_TYPE_GEN4_SDH,
	CLK_TYPE_GEN4_SD,
	CLK_TYPE_GEN4_MDSEL,	/* Select parent/divider using mode pin */
	CLK_TYPE_GEN4_Z,
	CLK_TYPE_GEN4_OSC,	/* OSC EXTAL predivider and fixed divider */
	CLK_TYPE_GEN4_RPCSRC,
	CLK_TYPE_GEN4_RPC,
	CLK_TYPE_GEN4_RPCD2,

	/* SoC specific definitions start here */
	CLK_TYPE_GEN4_SOC_BASE,
};

#define DEF_GEN4_SDH(_name, _id, _parent, _offset)	\
	DEF_BASE(_name, _id, CLK_TYPE_GEN4_SDH, _parent, .offset = _offset)

#define DEF_GEN4_SD(_name, _id, _parent, _offset)	\
	DEF_BASE(_name, _id, CLK_TYPE_GEN4_SD, _parent, .offset = _offset)

#define DEF_GEN4_MDSEL(_name, _id, _md, _parent0, _div0, _parent1, _div1) \
	DEF_BASE(_name, _id, CLK_TYPE_GEN4_MDSEL,	\
		 (_parent0) << 16 | (_parent1),		\
		 .div = (_div0) << 16 | (_div1), .offset = _md)

#define DEF_GEN4_OSC(_name, _id, _parent, _div)		\
	DEF_BASE(_name, _id, CLK_TYPE_GEN4_OSC, _parent, .div = _div)

#define DEF_GEN4_Z(_name, _id, _type, _parent, _div, _offset)	\
	DEF_BASE(_name, _id, _type, _parent, .div = _div, .offset = _offset)

struct rcar_gen4_cpg_pll_config {
	u8 extal_div;
	u8 pll1_mult;
	u8 pll1_div;
	u8 pll2_mult;
	u8 pll2_div;
	u8 pll3_mult;
	u8 pll3_div;
	u8 pll4_mult;
	u8 pll4_div;
	u8 pll5_mult;
	u8 pll5_div;
	u8 pll6_mult;
	u8 pll6_div;
	u8 osc_prediv;
};

#define CPG_RPCCKCR	0x874
#define SD0CKCR1	0x8a4

struct clk *rcar_gen4_cpg_clk_register(struct device *dev,
	const struct cpg_core_clk *core, const struct cpg_mssr_info *info,
	struct clk **clks, void __iomem *base,
	struct raw_notifier_head *notifiers);
int rcar_gen4_cpg_init(const struct rcar_gen4_cpg_pll_config *config,
		       unsigned int clk_extalr, u32 mode);

#endif
