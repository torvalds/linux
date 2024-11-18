// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright (c) 2021 MediaTek Inc.
// Author: Chun-Jie Chen <chun-jie.chen@mediatek.com>

#include <linux/clk-provider.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>

#include "clk-mtk.h"
#include "clk-gate.h"

#include <dt-bindings/clock/mt8192-clk.h>

static const struct mtk_gate_regs mdp0_cg_regs = {
	.set_ofs = 0x104,
	.clr_ofs = 0x108,
	.sta_ofs = 0x100,
};

static const struct mtk_gate_regs mdp1_cg_regs = {
	.set_ofs = 0x124,
	.clr_ofs = 0x128,
	.sta_ofs = 0x120,
};

#define GATE_MDP0(_id, _name, _parent, _shift)	\
	GATE_MTK(_id, _name, _parent, &mdp0_cg_regs, _shift, &mtk_clk_gate_ops_setclr)

#define GATE_MDP1(_id, _name, _parent, _shift)	\
	GATE_MTK(_id, _name, _parent, &mdp1_cg_regs, _shift, &mtk_clk_gate_ops_setclr)

static const struct mtk_gate mdp_clks[] = {
	/* MDP0 */
	GATE_MDP0(CLK_MDP_RDMA0, "mdp_mdp_rdma0", "mdp_sel", 0),
	GATE_MDP0(CLK_MDP_TDSHP0, "mdp_mdp_tdshp0", "mdp_sel", 1),
	GATE_MDP0(CLK_MDP_IMG_DL_ASYNC0, "mdp_img_dl_async0", "mdp_sel", 2),
	GATE_MDP0(CLK_MDP_IMG_DL_ASYNC1, "mdp_img_dl_async1", "mdp_sel", 3),
	GATE_MDP0(CLK_MDP_RDMA1, "mdp_mdp_rdma1", "mdp_sel", 4),
	GATE_MDP0(CLK_MDP_TDSHP1, "mdp_mdp_tdshp1", "mdp_sel", 5),
	GATE_MDP0(CLK_MDP_SMI0, "mdp_smi0", "mdp_sel", 6),
	GATE_MDP0(CLK_MDP_APB_BUS, "mdp_apb_bus", "mdp_sel", 7),
	GATE_MDP0(CLK_MDP_WROT0, "mdp_mdp_wrot0", "mdp_sel", 8),
	GATE_MDP0(CLK_MDP_RSZ0, "mdp_mdp_rsz0", "mdp_sel", 9),
	GATE_MDP0(CLK_MDP_HDR0, "mdp_mdp_hdr0", "mdp_sel", 10),
	GATE_MDP0(CLK_MDP_MUTEX0, "mdp_mdp_mutex0", "mdp_sel", 11),
	GATE_MDP0(CLK_MDP_WROT1, "mdp_mdp_wrot1", "mdp_sel", 12),
	GATE_MDP0(CLK_MDP_RSZ1, "mdp_mdp_rsz1", "mdp_sel", 13),
	GATE_MDP0(CLK_MDP_HDR1, "mdp_mdp_hdr1", "mdp_sel", 14),
	GATE_MDP0(CLK_MDP_FAKE_ENG0, "mdp_mdp_fake_eng0", "mdp_sel", 15),
	GATE_MDP0(CLK_MDP_AAL0, "mdp_mdp_aal0", "mdp_sel", 16),
	GATE_MDP0(CLK_MDP_AAL1, "mdp_mdp_aal1", "mdp_sel", 17),
	GATE_MDP0(CLK_MDP_COLOR0, "mdp_mdp_color0", "mdp_sel", 18),
	GATE_MDP0(CLK_MDP_COLOR1, "mdp_mdp_color1", "mdp_sel", 19),
	/* MDP1 */
	GATE_MDP1(CLK_MDP_IMG_DL_RELAY0_ASYNC0, "mdp_img_dl_relay0_async0", "mdp_sel", 0),
	GATE_MDP1(CLK_MDP_IMG_DL_RELAY1_ASYNC1, "mdp_img_dl_relay1_async1", "mdp_sel", 8),
};

static const struct mtk_clk_desc mdp_desc = {
	.clks = mdp_clks,
	.num_clks = ARRAY_SIZE(mdp_clks),
};

static const struct of_device_id of_match_clk_mt8192_mdp[] = {
	{
		.compatible = "mediatek,mt8192-mdpsys",
		.data = &mdp_desc,
	}, {
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(of, of_match_clk_mt8192_mdp);

static struct platform_driver clk_mt8192_mdp_drv = {
	.probe = mtk_clk_simple_probe,
	.remove = mtk_clk_simple_remove,
	.driver = {
		.name = "clk-mt8192-mdp",
		.of_match_table = of_match_clk_mt8192_mdp,
	},
};
module_platform_driver(clk_mt8192_mdp_drv);

MODULE_DESCRIPTION("MediaTek MT8192 Multimedia Data Path clocks driver");
MODULE_LICENSE("GPL");
