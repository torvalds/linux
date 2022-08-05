// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright (c) 2021 MediaTek Inc.
// Author: Chun-Jie Chen <chun-jie.chen@mediatek.com>

#include <linux/clk-provider.h>
#include <linux/platform_device.h>

#include "clk-mtk.h"
#include "clk-gate.h"

#include <dt-bindings/clock/mt8192-clk.h>

static const struct mtk_gate_regs mm0_cg_regs = {
	.set_ofs = 0x104,
	.clr_ofs = 0x108,
	.sta_ofs = 0x100,
};

static const struct mtk_gate_regs mm1_cg_regs = {
	.set_ofs = 0x114,
	.clr_ofs = 0x118,
	.sta_ofs = 0x110,
};

static const struct mtk_gate_regs mm2_cg_regs = {
	.set_ofs = 0x1a4,
	.clr_ofs = 0x1a8,
	.sta_ofs = 0x1a0,
};

#define GATE_MM0(_id, _name, _parent, _shift)	\
	GATE_MTK(_id, _name, _parent, &mm0_cg_regs, _shift, &mtk_clk_gate_ops_setclr)

#define GATE_MM1(_id, _name, _parent, _shift)	\
	GATE_MTK(_id, _name, _parent, &mm1_cg_regs, _shift, &mtk_clk_gate_ops_setclr)

#define GATE_MM2(_id, _name, _parent, _shift)	\
	GATE_MTK(_id, _name, _parent, &mm2_cg_regs, _shift, &mtk_clk_gate_ops_setclr)

static const struct mtk_gate mm_clks[] = {
	/* MM0 */
	GATE_MM0(CLK_MM_DISP_MUTEX0, "mm_disp_mutex0", "disp_sel", 0),
	GATE_MM0(CLK_MM_DISP_CONFIG, "mm_disp_config", "disp_sel", 1),
	GATE_MM0(CLK_MM_DISP_OVL0, "mm_disp_ovl0", "disp_sel", 2),
	GATE_MM0(CLK_MM_DISP_RDMA0, "mm_disp_rdma0", "disp_sel", 3),
	GATE_MM0(CLK_MM_DISP_OVL0_2L, "mm_disp_ovl0_2l", "disp_sel", 4),
	GATE_MM0(CLK_MM_DISP_WDMA0, "mm_disp_wdma0", "disp_sel", 5),
	GATE_MM0(CLK_MM_DISP_UFBC_WDMA0, "mm_disp_ufbc_wdma0", "disp_sel", 6),
	GATE_MM0(CLK_MM_DISP_RSZ0, "mm_disp_rsz0", "disp_sel", 7),
	GATE_MM0(CLK_MM_DISP_AAL0, "mm_disp_aal0", "disp_sel", 8),
	GATE_MM0(CLK_MM_DISP_CCORR0, "mm_disp_ccorr0", "disp_sel", 9),
	GATE_MM0(CLK_MM_DISP_DITHER0, "mm_disp_dither0", "disp_sel", 10),
	GATE_MM0(CLK_MM_SMI_INFRA, "mm_smi_infra", "disp_sel", 11),
	GATE_MM0(CLK_MM_DISP_GAMMA0, "mm_disp_gamma0", "disp_sel", 12),
	GATE_MM0(CLK_MM_DISP_POSTMASK0, "mm_disp_postmask0", "disp_sel", 13),
	GATE_MM0(CLK_MM_DISP_DSC_WRAP0, "mm_disp_dsc_wrap0", "disp_sel", 14),
	GATE_MM0(CLK_MM_DSI0, "mm_dsi0", "disp_sel", 15),
	GATE_MM0(CLK_MM_DISP_COLOR0, "mm_disp_color0", "disp_sel", 16),
	GATE_MM0(CLK_MM_SMI_COMMON, "mm_smi_common", "disp_sel", 17),
	GATE_MM0(CLK_MM_DISP_FAKE_ENG0, "mm_disp_fake_eng0", "disp_sel", 18),
	GATE_MM0(CLK_MM_DISP_FAKE_ENG1, "mm_disp_fake_eng1", "disp_sel", 19),
	GATE_MM0(CLK_MM_MDP_TDSHP4, "mm_mdp_tdshp4", "disp_sel", 20),
	GATE_MM0(CLK_MM_MDP_RSZ4, "mm_mdp_rsz4", "disp_sel", 21),
	GATE_MM0(CLK_MM_MDP_AAL4, "mm_mdp_aal4", "disp_sel", 22),
	GATE_MM0(CLK_MM_MDP_HDR4, "mm_mdp_hdr4", "disp_sel", 23),
	GATE_MM0(CLK_MM_MDP_RDMA4, "mm_mdp_rdma4", "disp_sel", 24),
	GATE_MM0(CLK_MM_MDP_COLOR4, "mm_mdp_color4", "disp_sel", 25),
	GATE_MM0(CLK_MM_DISP_Y2R0, "mm_disp_y2r0", "disp_sel", 26),
	GATE_MM0(CLK_MM_SMI_GALS, "mm_smi_gals", "disp_sel", 27),
	GATE_MM0(CLK_MM_DISP_OVL2_2L, "mm_disp_ovl2_2l", "disp_sel", 28),
	GATE_MM0(CLK_MM_DISP_RDMA4, "mm_disp_rdma4", "disp_sel", 29),
	GATE_MM0(CLK_MM_DISP_DPI0, "mm_disp_dpi0", "disp_sel", 30),
	/* MM1 */
	GATE_MM1(CLK_MM_SMI_IOMMU, "mm_smi_iommu", "disp_sel", 0),
	/* MM2 */
	GATE_MM2(CLK_MM_DSI_DSI0, "mm_dsi_dsi0", "disp_sel", 0),
	GATE_MM2(CLK_MM_DPI_DPI0, "mm_dpi_dpi0", "dpi_sel", 8),
	GATE_MM2(CLK_MM_26MHZ, "mm_26mhz", "clk26m", 24),
	GATE_MM2(CLK_MM_32KHZ, "mm_32khz", "clk32k", 25),
};

static int clk_mt8192_mm_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->parent->of_node;
	struct clk_hw_onecell_data *clk_data;
	int r;

	clk_data = mtk_alloc_clk_data(CLK_MM_NR_CLK);
	if (!clk_data)
		return -ENOMEM;

	r = mtk_clk_register_gates(node, mm_clks, ARRAY_SIZE(mm_clks), clk_data);
	if (r)
		return r;

	return of_clk_add_hw_provider(node, of_clk_hw_onecell_get, clk_data);
}

static struct platform_driver clk_mt8192_mm_drv = {
	.probe = clk_mt8192_mm_probe,
	.driver = {
		.name = "clk-mt8192-mm",
	},
};

builtin_platform_driver(clk_mt8192_mm_drv);
