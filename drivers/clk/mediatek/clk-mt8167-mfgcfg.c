// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 MediaTek Inc.
 * Copyright (c) 2020 BayLibre, SAS
 * Author: James Liao <jamesjj.liao@mediatek.com>
 *         Fabien Parent <fparent@baylibre.com>
 */

#include <linux/clk-provider.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>

#include "clk-mtk.h"
#include "clk-gate.h"

#include <dt-bindings/clock/mt8167-clk.h>

static const struct mtk_gate_regs mfg_cg_regs = {
	.set_ofs = 0x4,
	.clr_ofs = 0x8,
	.sta_ofs = 0x0,
};

#define GATE_MFG(_id, _name, _parent, _shift)			\
	GATE_MTK(_id, _name, _parent, &mfg_cg_regs, _shift, &mtk_clk_gate_ops_setclr)

static const struct mtk_gate mfg_clks[] __initconst = {
	GATE_MFG(CLK_MFG_BAXI, "mfg_baxi", "ahb_infra_sel", 0),
	GATE_MFG(CLK_MFG_BMEM, "mfg_bmem", "gfmux_emi1x_sel", 1),
	GATE_MFG(CLK_MFG_BG3D, "mfg_bg3d", "mfg_mm", 2),
	GATE_MFG(CLK_MFG_B26M, "mfg_b26m", "clk26m_ck", 3),
};

static void __init mtk_mfgcfg_init(struct device_node *node)
{
	struct clk_hw_onecell_data *clk_data;
	int r;

	clk_data = mtk_alloc_clk_data(CLK_MFG_NR_CLK);

	mtk_clk_register_gates(node, mfg_clks, ARRAY_SIZE(mfg_clks), clk_data);

	r = of_clk_add_hw_provider(node, of_clk_hw_onecell_get, clk_data);

	if (r)
		pr_err("%s(): could not register clock provider: %d\n",
			__func__, r);

}
CLK_OF_DECLARE(mtk_mfgcfg, "mediatek,mt8167-mfgcfg", mtk_mfgcfg_init);
