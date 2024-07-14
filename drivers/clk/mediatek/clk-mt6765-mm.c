// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018 MediaTek Inc.
 * Author: Owen Chen <owen.chen@mediatek.com>
 */

#include <linux/clk-provider.h>
#include <linux/platform_device.h>

#include "clk-mtk.h"
#include "clk-gate.h"

#include <dt-bindings/clock/mt6765-clk.h>

static const struct mtk_gate_regs mm_cg_regs = {
	.set_ofs = 0x104,
	.clr_ofs = 0x108,
	.sta_ofs = 0x100,
};

#define GATE_MM(_id, _name, _parent, _shift)	\
	GATE_MTK(_id, _name, _parent, &mm_cg_regs, _shift, &mtk_clk_gate_ops_setclr)

static const struct mtk_gate mm_clks[] = {
	/* MM */
	GATE_MM(CLK_MM_MDP_RDMA0, "mm_mdp_rdma0", "mm_ck", 0),
	GATE_MM(CLK_MM_MDP_CCORR0, "mm_mdp_ccorr0", "mm_ck", 1),
	GATE_MM(CLK_MM_MDP_RSZ0, "mm_mdp_rsz0", "mm_ck", 2),
	GATE_MM(CLK_MM_MDP_RSZ1, "mm_mdp_rsz1", "mm_ck", 3),
	GATE_MM(CLK_MM_MDP_TDSHP0, "mm_mdp_tdshp0", "mm_ck", 4),
	GATE_MM(CLK_MM_MDP_WROT0, "mm_mdp_wrot0", "mm_ck", 5),
	GATE_MM(CLK_MM_MDP_WDMA0, "mm_mdp_wdma0", "mm_ck", 6),
	GATE_MM(CLK_MM_DISP_OVL0, "mm_disp_ovl0", "mm_ck", 7),
	GATE_MM(CLK_MM_DISP_OVL0_2L, "mm_disp_ovl0_2l", "mm_ck", 8),
	GATE_MM(CLK_MM_DISP_RSZ0, "mm_disp_rsz0", "mm_ck", 9),
	GATE_MM(CLK_MM_DISP_RDMA0, "mm_disp_rdma0", "mm_ck", 10),
	GATE_MM(CLK_MM_DISP_WDMA0, "mm_disp_wdma0", "mm_ck", 11),
	GATE_MM(CLK_MM_DISP_COLOR0, "mm_disp_color0", "mm_ck", 12),
	GATE_MM(CLK_MM_DISP_CCORR0, "mm_disp_ccorr0", "mm_ck", 13),
	GATE_MM(CLK_MM_DISP_AAL0, "mm_disp_aal0", "mm_ck", 14),
	GATE_MM(CLK_MM_DISP_GAMMA0, "mm_disp_gamma0", "mm_ck", 15),
	GATE_MM(CLK_MM_DISP_DITHER0, "mm_disp_dither0", "mm_ck", 16),
	GATE_MM(CLK_MM_DSI0, "mm_dsi0", "mm_ck", 17),
	GATE_MM(CLK_MM_FAKE_ENG, "mm_fake_eng", "mm_ck", 18),
	GATE_MM(CLK_MM_SMI_COMMON, "mm_smi_common", "mm_ck", 19),
	GATE_MM(CLK_MM_SMI_LARB0, "mm_smi_larb0", "mm_ck", 20),
	GATE_MM(CLK_MM_SMI_COMM0, "mm_smi_comm0", "mm_ck", 21),
	GATE_MM(CLK_MM_SMI_COMM1, "mm_smi_comm1", "mm_ck", 22),
	GATE_MM(CLK_MM_CAM_MDP, "mm_cam_mdp_ck", "mm_ck", 23),
	GATE_MM(CLK_MM_SMI_IMG, "mm_smi_img_ck", "mm_ck", 24),
	GATE_MM(CLK_MM_SMI_CAM, "mm_smi_cam_ck", "mm_ck", 25),
	GATE_MM(CLK_MM_IMG_DL_RELAY, "mm_img_dl_relay", "mm_ck", 26),
	GATE_MM(CLK_MM_IMG_DL_ASYNC_TOP, "mm_imgdl_async", "mm_ck", 27),
	GATE_MM(CLK_MM_DIG_DSI, "mm_dig_dsi_ck", "mm_ck", 28),
	GATE_MM(CLK_MM_F26M_HRTWT, "mm_hrtwt", "f_f26m_ck", 29),
};

static const struct mtk_clk_desc mm_desc = {
	.clks = mm_clks,
	.num_clks = ARRAY_SIZE(mm_clks),
};

static const struct of_device_id of_match_clk_mt6765_mm[] = {
	{
		.compatible = "mediatek,mt6765-mmsys",
		.data = &mm_desc,
	}, {
		/* sentinel */
	}
};

static struct platform_driver clk_mt6765_mm_drv = {
	.probe = mtk_clk_simple_probe,
	.remove = mtk_clk_simple_remove,
	.driver = {
		.name = "clk-mt6765-mm",
		.of_match_table = of_match_clk_mt6765_mm,
	},
};
module_platform_driver(clk_mt6765_mm_drv);
MODULE_LICENSE("GPL");
