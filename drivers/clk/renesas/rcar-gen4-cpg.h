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
	CLK_TYPE_GEN4_PLL2X_3X,	/* r8a779a0 only */
	CLK_TYPE_GEN4_PLL5,
	CLK_TYPE_GEN4_PLL_F8_25,	/* Fixed fractional 8.25 PLL */
	CLK_TYPE_GEN4_PLL_V8_25,	/* Variable fractional 8.25 PLL */
	CLK_TYPE_GEN4_PLL_F9_24,	/* Fixed fractional 9.24 PLL */
	CLK_TYPE_GEN4_PLL_V9_24,	/* Variable fractional 9.24 PLL */
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

#define DEF_GEN4_PLL_F8_25(_name, _idx, _id, _parent)	\
	DEF_BASE(_name, _id, CLK_TYPE_GEN4_PLL_F8_25, _parent, .offset = _idx)

#define DEF_GEN4_PLL_V8_25(_name, _idx, _id, _parent)	\
	DEF_BASE(_name, _id, CLK_TYPE_GEN4_PLL_V8_25, _parent, .offset = _idx)

#define DEF_GEN4_PLL_F9_24(_name, _idx, _id, _parent)	\
	DEF_BASE(_name, _id, CLK_TYPE_GEN4_PLL_F9_24, _parent, .offset = _idx)

#define DEF_GEN4_PLL_V9_24(_name, _idx, _id, _parent)	\
	DEF_BASE(_name, _id, CLK_TYPE_GEN4_PLL_V9_24, _parent, .offset = _idx)

#define DEF_GEN4_Z(_name, _id, _type, _parent, _div, _offset)	\
	DEF_BASE(_name, _id, _type, _parent, .div = _div, .offset = _offset)

struct rcar_gen4_cpg_pll_config {
	u8 extal_div;
	u8 pll1_mult;
	u8 pll1_div;
	u8 pll5_mult;
	u8 pll5_div;
	u8 osc_prediv;
};

#define CPG_SD0CKCR	0x870	/* SD-IF0 Clock Frequency Control Register */
#define CPG_CANFDCKCR	0x878	/* CAN-FD Clock Frequency Control Register */
#define CPG_MSOCKCR	0x87c	/* MSIOF Clock Frequency Control Register */
#define CPG_CSICKCR	0x880	/* CSI Clock Frequency Control Register */
#define CPG_DSIEXTCKCR	0x884	/* DSI Clock Frequency Control Register */

struct clk *rcar_gen4_cpg_clk_register(struct device *dev,
	const struct cpg_core_clk *core, const struct cpg_mssr_info *info,
	struct cpg_mssr_pub *pub);
int rcar_gen4_cpg_init(const struct rcar_gen4_cpg_pll_config *config,
		       unsigned int clk_extalr, u32 mode);

#endif
