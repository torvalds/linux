// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright (c) 2022 MediaTek Inc.
// Author: Chun-Jie Chen <chun-jie.chen@mediatek.com>

#include <linux/clk-provider.h>
#include <linux/platform_device.h>
#include <dt-bindings/clock/mt8186-clk.h>

#include "clk-gate.h"
#include "clk-mtk.h"

static const struct mtk_gate_regs mdp0_cg_regs = {
	.set_ofs = 0x104,
	.clr_ofs = 0x108,
	.sta_ofs = 0x100,
};

static const struct mtk_gate_regs mdp2_cg_regs = {
	.set_ofs = 0x124,
	.clr_ofs = 0x128,
	.sta_ofs = 0x120,
};

#define GATE_MDP0(_id, _name, _parent, _shift)			\
	GATE_MTK(_id, _name, _parent, &mdp0_cg_regs, _shift, &mtk_clk_gate_ops_setclr)

#define GATE_MDP2(_id, _name, _parent, _shift)			\
	GATE_MTK(_id, _name, _parent, &mdp2_cg_regs, _shift, &mtk_clk_gate_ops_setclr)

static const struct mtk_gate mdp_clks[] = {
	/* MDP0 */
	GATE_MDP0(CLK_MDP_RDMA0, "mdp_rdma0", "top_mdp", 0),
	GATE_MDP0(CLK_MDP_TDSHP0, "mdp_tdshp0", "top_mdp", 1),
	GATE_MDP0(CLK_MDP_IMG_DL_ASYNC0, "mdp_img_dl_async0", "top_mdp", 2),
	GATE_MDP0(CLK_MDP_IMG_DL_ASYNC1, "mdp_img_dl_async1", "top_mdp", 3),
	GATE_MDP0(CLK_MDP_DISP_RDMA, "mdp_disp_rdma", "top_mdp", 4),
	GATE_MDP0(CLK_MDP_HMS, "mdp_hms", "top_mdp", 5),
	GATE_MDP0(CLK_MDP_SMI0, "mdp_smi0", "top_mdp", 6),
	GATE_MDP0(CLK_MDP_APB_BUS, "mdp_apb_bus", "top_mdp", 7),
	GATE_MDP0(CLK_MDP_WROT0, "mdp_wrot0", "top_mdp", 8),
	GATE_MDP0(CLK_MDP_RSZ0, "mdp_rsz0", "top_mdp", 9),
	GATE_MDP0(CLK_MDP_HDR0, "mdp_hdr0", "top_mdp", 10),
	GATE_MDP0(CLK_MDP_MUTEX0, "mdp_mutex0", "top_mdp", 11),
	GATE_MDP0(CLK_MDP_WROT1, "mdp_wrot1", "top_mdp", 12),
	GATE_MDP0(CLK_MDP_RSZ1, "mdp_rsz1", "top_mdp", 13),
	GATE_MDP0(CLK_MDP_FAKE_ENG0, "mdp_fake_eng0", "top_mdp", 14),
	GATE_MDP0(CLK_MDP_AAL0, "mdp_aal0", "top_mdp", 15),
	GATE_MDP0(CLK_MDP_DISP_WDMA, "mdp_disp_wdma", "top_mdp", 16),
	GATE_MDP0(CLK_MDP_COLOR, "mdp_color", "top_mdp", 17),
	GATE_MDP0(CLK_MDP_IMG_DL_ASYNC2, "mdp_img_dl_async2", "top_mdp", 18),
	/* MDP2 */
	GATE_MDP2(CLK_MDP_IMG_DL_RELAY0_ASYNC0, "mdp_img_dl_rel0_as0", "top_mdp", 0),
	GATE_MDP2(CLK_MDP_IMG_DL_RELAY1_ASYNC1, "mdp_img_dl_rel1_as1", "top_mdp", 8),
	GATE_MDP2(CLK_MDP_IMG_DL_RELAY2_ASYNC2, "mdp_img_dl_rel2_as2", "top_mdp", 24),
};

static const struct mtk_clk_desc mdp_desc = {
	.clks = mdp_clks,
	.num_clks = ARRAY_SIZE(mdp_clks),
};

static const struct of_device_id of_match_clk_mt8186_mdp[] = {
	{
		.compatible = "mediatek,mt8186-mdpsys",
		.data = &mdp_desc,
	}, {
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(of, of_match_clk_mt8186_mdp);

static struct platform_driver clk_mt8186_mdp_drv = {
	.probe = mtk_clk_simple_probe,
	.remove_new = mtk_clk_simple_remove,
	.driver = {
		.name = "clk-mt8186-mdp",
		.of_match_table = of_match_clk_mt8186_mdp,
	},
};
module_platform_driver(clk_mt8186_mdp_drv);

MODULE_DESCRIPTION("MediaTek MT8186 Multimedia Data Path clocks driver");
MODULE_LICENSE("GPL");
