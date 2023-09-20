// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017 MediaTek Inc.
 * Author: Kevin Chen <kevin-cw.chen@mediatek.com>
 */

#include <linux/clk-provider.h>
#include <linux/platform_device.h>
#include <dt-bindings/clock/mt6797-clk.h>

#include "clk-mtk.h"
#include "clk-gate.h"

static const struct mtk_gate_regs mm0_cg_regs = {
	.set_ofs = 0x0104,
	.clr_ofs = 0x0108,
	.sta_ofs = 0x0100,
};

static const struct mtk_gate_regs mm1_cg_regs = {
	.set_ofs = 0x0114,
	.clr_ofs = 0x0118,
	.sta_ofs = 0x0110,
};

#define GATE_MM0(_id, _name, _parent, _shift)	\
	GATE_MTK(_id, _name, _parent, &mm0_cg_regs, _shift, &mtk_clk_gate_ops_setclr)

#define GATE_MM1(_id, _name, _parent, _shift)	\
	GATE_MTK(_id, _name, _parent, &mm1_cg_regs, _shift, &mtk_clk_gate_ops_setclr)

static const struct mtk_gate mm_clks[] = {
	GATE_MM0(CLK_MM_SMI_COMMON, "mm_smi_common", "mm_sel", 0),
	GATE_MM0(CLK_MM_SMI_LARB0, "mm_smi_larb0", "mm_sel", 1),
	GATE_MM0(CLK_MM_SMI_LARB5, "mm_smi_larb5", "mm_sel", 2),
	GATE_MM0(CLK_MM_CAM_MDP, "mm_cam_mdp", "mm_sel", 3),
	GATE_MM0(CLK_MM_MDP_RDMA0, "mm_mdp_rdma0", "mm_sel", 4),
	GATE_MM0(CLK_MM_MDP_RDMA1, "mm_mdp_rdma1", "mm_sel", 5),
	GATE_MM0(CLK_MM_MDP_RSZ0, "mm_mdp_rsz0", "mm_sel", 6),
	GATE_MM0(CLK_MM_MDP_RSZ1, "mm_mdp_rsz1", "mm_sel", 7),
	GATE_MM0(CLK_MM_MDP_RSZ2, "mm_mdp_rsz2", "mm_sel", 8),
	GATE_MM0(CLK_MM_MDP_TDSHP, "mm_mdp_tdshp", "mm_sel", 9),
	GATE_MM0(CLK_MM_MDP_COLOR, "mm_mdp_color", "mm_sel", 10),
	GATE_MM0(CLK_MM_MDP_WDMA, "mm_mdp_wdma", "mm_sel", 11),
	GATE_MM0(CLK_MM_MDP_WROT0, "mm_mdp_wrot0", "mm_sel", 12),
	GATE_MM0(CLK_MM_MDP_WROT1, "mm_mdp_wrot1", "mm_sel", 13),
	GATE_MM0(CLK_MM_FAKE_ENG, "mm_fake_eng", "mm_sel", 14),
	GATE_MM0(CLK_MM_DISP_OVL0, "mm_disp_ovl0", "mm_sel", 15),
	GATE_MM0(CLK_MM_DISP_OVL1, "mm_disp_ovl1", "mm_sel", 16),
	GATE_MM0(CLK_MM_DISP_OVL0_2L, "mm_disp_ovl0_2l", "mm_sel", 17),
	GATE_MM0(CLK_MM_DISP_OVL1_2L, "mm_disp_ovl1_2l", "mm_sel", 18),
	GATE_MM0(CLK_MM_DISP_RDMA0, "mm_disp_rdma0", "mm_sel", 19),
	GATE_MM0(CLK_MM_DISP_RDMA1, "mm_disp_rdma1", "mm_sel", 20),
	GATE_MM0(CLK_MM_DISP_WDMA0, "mm_disp_wdma0", "mm_sel", 21),
	GATE_MM0(CLK_MM_DISP_WDMA1, "mm_disp_wdma1", "mm_sel", 22),
	GATE_MM0(CLK_MM_DISP_COLOR, "mm_disp_color", "mm_sel", 23),
	GATE_MM0(CLK_MM_DISP_CCORR, "mm_disp_ccorr", "mm_sel", 24),
	GATE_MM0(CLK_MM_DISP_AAL, "mm_disp_aal", "mm_sel", 25),
	GATE_MM0(CLK_MM_DISP_GAMMA, "mm_disp_gamma", "mm_sel", 26),
	GATE_MM0(CLK_MM_DISP_OD, "mm_disp_od", "mm_sel", 27),
	GATE_MM0(CLK_MM_DISP_DITHER, "mm_disp_dither", "mm_sel", 28),
	GATE_MM0(CLK_MM_DISP_UFOE, "mm_disp_ufoe", "mm_sel", 29),
	GATE_MM0(CLK_MM_DISP_DSC, "mm_disp_dsc", "mm_sel", 30),
	GATE_MM0(CLK_MM_DISP_SPLIT, "mm_disp_split", "mm_sel", 31),
	GATE_MM1(CLK_MM_DSI0_MM_CLOCK, "mm_dsi0_mm_clock", "mm_sel", 0),
	GATE_MM1(CLK_MM_DSI1_MM_CLOCK, "mm_dsi1_mm_clock", "mm_sel", 2),
	GATE_MM1(CLK_MM_DPI_MM_CLOCK, "mm_dpi_mm_clock", "mm_sel", 4),
	GATE_MM1(CLK_MM_DPI_INTERFACE_CLOCK, "mm_dpi_interface_clock",
		 "dpi0_sel", 5),
	GATE_MM1(CLK_MM_LARB4_AXI_ASIF_MM_CLOCK, "mm_larb4_axi_asif_mm_clock",
		 "mm_sel", 6),
	GATE_MM1(CLK_MM_LARB4_AXI_ASIF_MJC_CLOCK, "mm_larb4_axi_asif_mjc_clock",
		 "mjc_sel", 7),
	GATE_MM1(CLK_MM_DISP_OVL0_MOUT_CLOCK, "mm_disp_ovl0_mout_clock",
		 "mm_sel", 8),
	GATE_MM1(CLK_MM_FAKE_ENG2, "mm_fake_eng2", "mm_sel", 9),
	GATE_MM1(CLK_MM_DSI0_INTERFACE_CLOCK, "mm_dsi0_interface_clock",
		 "clk26m", 1),
	GATE_MM1(CLK_MM_DSI1_INTERFACE_CLOCK, "mm_dsi1_interface_clock",
		 "clk26m", 3),
};

static const struct mtk_clk_desc mm_desc = {
	.clks = mm_clks,
	.num_clks = ARRAY_SIZE(mm_clks),
};

static const struct platform_device_id clk_mt6797_mm_id_table[] = {
	{ .name = "clk-mt6797-mm", .driver_data = (kernel_ulong_t)&mm_desc },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(platform, clk_mt6797_mm_id_table);

static struct platform_driver clk_mt6797_mm_drv = {
	.probe = mtk_clk_pdev_probe,
	.remove_new = mtk_clk_pdev_remove,
	.driver = {
		.name = "clk-mt6797-mm",
	},
	.id_table = clk_mt6797_mm_id_table,
};
module_platform_driver(clk_mt6797_mm_drv);
MODULE_LICENSE("GPL");
