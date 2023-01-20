// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2021 MediaTek Inc.
 * Author: Sam Shih <sam.shih@mediatek.com>
 * Author: Wenzhen Yu <wenzhen.yu@mediatek.com>
 */

#include <linux/clk-provider.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>

#include "clk-mtk.h"
#include "clk-gate.h"

#include <dt-bindings/clock/mt7986-clk.h>

static const struct mtk_gate_regs sgmii0_cg_regs = {
	.set_ofs = 0xe4,
	.clr_ofs = 0xe4,
	.sta_ofs = 0xe4,
};

#define GATE_SGMII0(_id, _name, _parent, _shift)                               \
	{                                                                      \
		.id = _id, .name = _name, .parent_name = _parent,              \
		.regs = &sgmii0_cg_regs, .shift = _shift,                      \
		.ops = &mtk_clk_gate_ops_no_setclr_inv,                        \
	}

static const struct mtk_gate sgmii0_clks[] __initconst = {
	GATE_SGMII0(CLK_SGMII0_TX250M_EN, "sgmii0_tx250m_en", "top_xtal", 2),
	GATE_SGMII0(CLK_SGMII0_RX250M_EN, "sgmii0_rx250m_en", "top_xtal", 3),
	GATE_SGMII0(CLK_SGMII0_CDR_REF, "sgmii0_cdr_ref", "top_xtal", 4),
	GATE_SGMII0(CLK_SGMII0_CDR_FB, "sgmii0_cdr_fb", "top_xtal", 5),
};

static const struct mtk_gate_regs sgmii1_cg_regs = {
	.set_ofs = 0xe4,
	.clr_ofs = 0xe4,
	.sta_ofs = 0xe4,
};

#define GATE_SGMII1(_id, _name, _parent, _shift)                               \
	{                                                                      \
		.id = _id, .name = _name, .parent_name = _parent,              \
		.regs = &sgmii1_cg_regs, .shift = _shift,                      \
		.ops = &mtk_clk_gate_ops_no_setclr_inv,                        \
	}

static const struct mtk_gate sgmii1_clks[] __initconst = {
	GATE_SGMII1(CLK_SGMII1_TX250M_EN, "sgmii1_tx250m_en", "top_xtal", 2),
	GATE_SGMII1(CLK_SGMII1_RX250M_EN, "sgmii1_rx250m_en", "top_xtal", 3),
	GATE_SGMII1(CLK_SGMII1_CDR_REF, "sgmii1_cdr_ref", "top_xtal", 4),
	GATE_SGMII1(CLK_SGMII1_CDR_FB, "sgmii1_cdr_fb", "top_xtal", 5),
};

static const struct mtk_gate_regs eth_cg_regs = {
	.set_ofs = 0x30,
	.clr_ofs = 0x30,
	.sta_ofs = 0x30,
};

#define GATE_ETH(_id, _name, _parent, _shift)                                  \
	{                                                                      \
		.id = _id, .name = _name, .parent_name = _parent,              \
		.regs = &eth_cg_regs, .shift = _shift,                         \
		.ops = &mtk_clk_gate_ops_no_setclr_inv,                        \
	}

static const struct mtk_gate eth_clks[] __initconst = {
	GATE_ETH(CLK_ETH_FE_EN, "eth_fe_en", "netsys_2x_sel", 6),
	GATE_ETH(CLK_ETH_GP2_EN, "eth_gp2_en", "sgm_325m_sel", 7),
	GATE_ETH(CLK_ETH_GP1_EN, "eth_gp1_en", "sgm_325m_sel", 8),
	GATE_ETH(CLK_ETH_WOCPU1_EN, "eth_wocpu1_en", "netsys_mcu_sel", 14),
	GATE_ETH(CLK_ETH_WOCPU0_EN, "eth_wocpu0_en", "netsys_mcu_sel", 15),
};

static void __init mtk_sgmiisys_0_init(struct device_node *node)
{
	struct clk_hw_onecell_data *clk_data;
	int r;

	clk_data = mtk_alloc_clk_data(ARRAY_SIZE(sgmii0_clks));

	mtk_clk_register_gates(NULL, node, sgmii0_clks,
			       ARRAY_SIZE(sgmii0_clks), clk_data);

	r = of_clk_add_hw_provider(node, of_clk_hw_onecell_get, clk_data);
	if (r)
		pr_err("%s(): could not register clock provider: %d\n",
		       __func__, r);
}
CLK_OF_DECLARE(mtk_sgmiisys_0, "mediatek,mt7986-sgmiisys_0",
	       mtk_sgmiisys_0_init);

static void __init mtk_sgmiisys_1_init(struct device_node *node)
{
	struct clk_hw_onecell_data *clk_data;
	int r;

	clk_data = mtk_alloc_clk_data(ARRAY_SIZE(sgmii1_clks));

	mtk_clk_register_gates(NULL, node, sgmii1_clks,
			       ARRAY_SIZE(sgmii1_clks), clk_data);

	r = of_clk_add_hw_provider(node, of_clk_hw_onecell_get, clk_data);

	if (r)
		pr_err("%s(): could not register clock provider: %d\n",
		       __func__, r);
}
CLK_OF_DECLARE(mtk_sgmiisys_1, "mediatek,mt7986-sgmiisys_1",
	       mtk_sgmiisys_1_init);

static void __init mtk_ethsys_init(struct device_node *node)
{
	struct clk_hw_onecell_data *clk_data;
	int r;

	clk_data = mtk_alloc_clk_data(ARRAY_SIZE(eth_clks));

	mtk_clk_register_gates(NULL, node, eth_clks, ARRAY_SIZE(eth_clks), clk_data);

	r = of_clk_add_hw_provider(node, of_clk_hw_onecell_get, clk_data);

	if (r)
		pr_err("%s(): could not register clock provider: %d\n",
		       __func__, r);
}
CLK_OF_DECLARE(mtk_ethsys, "mediatek,mt7986-ethsys", mtk_ethsys_init);
