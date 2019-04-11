/* SPDX-License-Identifier: GPL-2.0
 *
 * R-Car Gen3 Clock Pulse Generator
 *
 * Copyright (C) 2015-2018 Glider bvba
 * Copyright (C) 2018 Renesas Electronics Corp.
 *
 */

#ifndef __CLK_RENESAS_RCAR_GEN3_CPG_H__
#define __CLK_RENESAS_RCAR_GEN3_CPG_H__

enum rcar_gen3_clk_types {
	CLK_TYPE_GEN3_MAIN = CLK_TYPE_CUSTOM,
	CLK_TYPE_GEN3_PLL0,
	CLK_TYPE_GEN3_PLL1,
	CLK_TYPE_GEN3_PLL2,
	CLK_TYPE_GEN3_PLL3,
	CLK_TYPE_GEN3_PLL4,
	CLK_TYPE_GEN3_SD,
	CLK_TYPE_GEN3_R,
	CLK_TYPE_GEN3_MDSEL,	/* Select parent/divider using mode pin */
	CLK_TYPE_GEN3_Z,
	CLK_TYPE_GEN3_OSC,	/* OSC EXTAL predivider and fixed divider */
	CLK_TYPE_GEN3_RCKSEL,	/* Select parent/divider using RCKCR.CKSEL */
	CLK_TYPE_GEN3_RPCSRC,
	CLK_TYPE_GEN3_RPC,
	CLK_TYPE_GEN3_RPCD2,

	/* SoC specific definitions start here */
	CLK_TYPE_GEN3_SOC_BASE,
};

#define DEF_GEN3_SD(_name, _id, _parent, _offset)	\
	DEF_BASE(_name, _id, CLK_TYPE_GEN3_SD, _parent, .offset = _offset)

#define DEF_GEN3_MDSEL(_name, _id, _md, _parent0, _div0, _parent1, _div1) \
	DEF_BASE(_name, _id, CLK_TYPE_GEN3_MDSEL,	\
		 (_parent0) << 16 | (_parent1),		\
		 .div = (_div0) << 16 | (_div1), .offset = _md)

#define DEF_GEN3_PE(_name, _id, _parent_sscg, _div_sscg, _parent_clean, \
		    _div_clean) \
	DEF_GEN3_MDSEL(_name, _id, 12, _parent_sscg, _div_sscg,	\
		       _parent_clean, _div_clean)

#define DEF_GEN3_OSC(_name, _id, _parent, _div)		\
	DEF_BASE(_name, _id, CLK_TYPE_GEN3_OSC, _parent, .div = _div)

#define DEF_GEN3_RCKSEL(_name, _id, _parent0, _div0, _parent1, _div1) \
	DEF_BASE(_name, _id, CLK_TYPE_GEN3_RCKSEL,	\
		 (_parent0) << 16 | (_parent1),	.div = (_div0) << 16 | (_div1))

#define DEF_GEN3_Z(_name, _id, _type, _parent, _div, _offset)	\
	DEF_BASE(_name, _id, _type, _parent, .div = _div, .offset = _offset)

struct rcar_gen3_cpg_pll_config {
	u8 extal_div;
	u8 pll1_mult;
	u8 pll1_div;
	u8 pll3_mult;
	u8 pll3_div;
	u8 osc_prediv;
};

#define CPG_RPCCKCR	0x238
#define CPG_RCKCR	0x240

struct clk *rcar_gen3_cpg_clk_register(struct device *dev,
	const struct cpg_core_clk *core, const struct cpg_mssr_info *info,
	struct clk **clks, void __iomem *base,
	struct raw_notifier_head *notifiers);
int rcar_gen3_cpg_init(const struct rcar_gen3_cpg_pll_config *config,
		       unsigned int clk_extalr, u32 mode);

#endif
