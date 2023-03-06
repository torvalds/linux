// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright (c) 2021 MediaTek Inc.
// Author: Chun-Jie Chen <chun-jie.chen@mediatek.com>

#include "clk-gate.h"
#include "clk-mtk.h"

#include <dt-bindings/clock/mt8195-clk.h>
#include <linux/clk-provider.h>
#include <linux/platform_device.h>

static const struct mtk_gate_regs vdo1_0_cg_regs = {
	.set_ofs = 0x104,
	.clr_ofs = 0x108,
	.sta_ofs = 0x100,
};

static const struct mtk_gate_regs vdo1_1_cg_regs = {
	.set_ofs = 0x124,
	.clr_ofs = 0x128,
	.sta_ofs = 0x120,
};

static const struct mtk_gate_regs vdo1_2_cg_regs = {
	.set_ofs = 0x134,
	.clr_ofs = 0x138,
	.sta_ofs = 0x130,
};

static const struct mtk_gate_regs vdo1_3_cg_regs = {
	.set_ofs = 0x144,
	.clr_ofs = 0x148,
	.sta_ofs = 0x140,
};

static const struct mtk_gate_regs vdo1_4_cg_regs = {
	.set_ofs = 0x400,
	.clr_ofs = 0x400,
	.sta_ofs = 0x400,
};

#define GATE_VDO1_0(_id, _name, _parent, _shift)			\
	GATE_MTK(_id, _name, _parent, &vdo1_0_cg_regs, _shift, &mtk_clk_gate_ops_setclr)

#define GATE_VDO1_1(_id, _name, _parent, _shift)			\
	GATE_MTK(_id, _name, _parent, &vdo1_1_cg_regs, _shift, &mtk_clk_gate_ops_setclr)

#define GATE_VDO1_2(_id, _name, _parent, _shift)			\
	GATE_MTK(_id, _name, _parent, &vdo1_2_cg_regs, _shift, &mtk_clk_gate_ops_setclr)

#define GATE_VDO1_2_FLAGS(_id, _name, _parent, _shift, _flags)		\
	GATE_MTK_FLAGS(_id, _name, _parent, &vdo1_2_cg_regs, _shift,	\
		       &mtk_clk_gate_ops_setclr, _flags)

#define GATE_VDO1_3(_id, _name, _parent, _shift)			\
	GATE_MTK(_id, _name, _parent, &vdo1_3_cg_regs, _shift, &mtk_clk_gate_ops_setclr)

#define GATE_VDO1_4(_id, _name, _parent, _shift)			\
	GATE_MTK(_id, _name, _parent, &vdo1_4_cg_regs, _shift, &mtk_clk_gate_ops_no_setclr_inv)

static const struct mtk_gate vdo1_clks[] = {
	/* VDO1_0 */
	GATE_VDO1_0(CLK_VDO1_SMI_LARB2, "vdo1_smi_larb2", "top_vpp", 0),
	GATE_VDO1_0(CLK_VDO1_SMI_LARB3, "vdo1_smi_larb3", "top_vpp", 1),
	GATE_VDO1_0(CLK_VDO1_GALS, "vdo1_gals", "top_vpp", 2),
	GATE_VDO1_0(CLK_VDO1_FAKE_ENG0, "vdo1_fake_eng0", "top_vpp", 3),
	GATE_VDO1_0(CLK_VDO1_FAKE_ENG, "vdo1_fake_eng", "top_vpp", 4),
	GATE_VDO1_0(CLK_VDO1_MDP_RDMA0, "vdo1_mdp_rdma0", "top_vpp", 5),
	GATE_VDO1_0(CLK_VDO1_MDP_RDMA1, "vdo1_mdp_rdma1", "top_vpp", 6),
	GATE_VDO1_0(CLK_VDO1_MDP_RDMA2, "vdo1_mdp_rdma2", "top_vpp", 7),
	GATE_VDO1_0(CLK_VDO1_MDP_RDMA3, "vdo1_mdp_rdma3", "top_vpp", 8),
	GATE_VDO1_0(CLK_VDO1_VPP_MERGE0, "vdo1_vpp_merge0", "top_vpp", 9),
	GATE_VDO1_0(CLK_VDO1_VPP_MERGE1, "vdo1_vpp_merge1", "top_vpp", 10),
	GATE_VDO1_0(CLK_VDO1_VPP_MERGE2, "vdo1_vpp_merge2", "top_vpp", 11),
	GATE_VDO1_0(CLK_VDO1_VPP_MERGE3, "vdo1_vpp_merge3", "top_vpp", 12),
	GATE_VDO1_0(CLK_VDO1_VPP_MERGE4, "vdo1_vpp_merge4", "top_vpp", 13),
	GATE_VDO1_0(CLK_VDO1_VPP2_TO_VDO1_DL_ASYNC, "vdo1_vpp2_to_vdo1_dl_async", "top_vpp", 14),
	GATE_VDO1_0(CLK_VDO1_VPP3_TO_VDO1_DL_ASYNC, "vdo1_vpp3_to_vdo1_dl_async", "top_vpp", 15),
	GATE_VDO1_0(CLK_VDO1_DISP_MUTEX, "vdo1_disp_mutex", "top_vpp", 16),
	GATE_VDO1_0(CLK_VDO1_MDP_RDMA4, "vdo1_mdp_rdma4", "top_vpp", 17),
	GATE_VDO1_0(CLK_VDO1_MDP_RDMA5, "vdo1_mdp_rdma5", "top_vpp", 18),
	GATE_VDO1_0(CLK_VDO1_MDP_RDMA6, "vdo1_mdp_rdma6", "top_vpp", 19),
	GATE_VDO1_0(CLK_VDO1_MDP_RDMA7, "vdo1_mdp_rdma7", "top_vpp", 20),
	GATE_VDO1_0(CLK_VDO1_DP_INTF0_MM, "vdo1_dp_intf0_mm", "top_vpp", 21),
	GATE_VDO1_0(CLK_VDO1_DPI0_MM, "vdo1_dpi0_mm", "top_vpp", 22),
	GATE_VDO1_0(CLK_VDO1_DPI1_MM, "vdo1_dpi1_mm", "top_vpp", 23),
	GATE_VDO1_0(CLK_VDO1_DISP_MONITOR, "vdo1_disp_monitor", "top_vpp", 24),
	GATE_VDO1_0(CLK_VDO1_MERGE0_DL_ASYNC, "vdo1_merge0_dl_async", "top_vpp", 25),
	GATE_VDO1_0(CLK_VDO1_MERGE1_DL_ASYNC, "vdo1_merge1_dl_async", "top_vpp", 26),
	GATE_VDO1_0(CLK_VDO1_MERGE2_DL_ASYNC, "vdo1_merge2_dl_async", "top_vpp", 27),
	GATE_VDO1_0(CLK_VDO1_MERGE3_DL_ASYNC, "vdo1_merge3_dl_async", "top_vpp", 28),
	GATE_VDO1_0(CLK_VDO1_MERGE4_DL_ASYNC, "vdo1_merge4_dl_async", "top_vpp", 29),
	GATE_VDO1_0(CLK_VDO1_VDO0_DSC_TO_VDO1_DL_ASYNC, "vdo1_vdo0_dsc_to_vdo1_dl_async",
		    "top_vpp", 30),
	GATE_VDO1_0(CLK_VDO1_VDO0_MERGE_TO_VDO1_DL_ASYNC, "vdo1_vdo0_merge_to_vdo1_dl_async",
		    "top_vpp", 31),
	/* VDO1_1 */
	GATE_VDO1_1(CLK_VDO1_HDR_VDO_FE0, "vdo1_hdr_vdo_fe0", "top_vpp", 0),
	GATE_VDO1_1(CLK_VDO1_HDR_GFX_FE0, "vdo1_hdr_gfx_fe0", "top_vpp", 1),
	GATE_VDO1_1(CLK_VDO1_HDR_VDO_BE, "vdo1_hdr_vdo_be", "top_vpp", 2),
	GATE_VDO1_1(CLK_VDO1_HDR_VDO_FE1, "vdo1_hdr_vdo_fe1", "top_vpp", 16),
	GATE_VDO1_1(CLK_VDO1_HDR_GFX_FE1, "vdo1_hdr_gfx_fe1", "top_vpp", 17),
	GATE_VDO1_1(CLK_VDO1_DISP_MIXER, "vdo1_disp_mixer", "top_vpp", 18),
	GATE_VDO1_1(CLK_VDO1_HDR_VDO_FE0_DL_ASYNC, "vdo1_hdr_vdo_fe0_dl_async", "top_vpp", 19),
	GATE_VDO1_1(CLK_VDO1_HDR_VDO_FE1_DL_ASYNC, "vdo1_hdr_vdo_fe1_dl_async", "top_vpp", 20),
	GATE_VDO1_1(CLK_VDO1_HDR_GFX_FE0_DL_ASYNC, "vdo1_hdr_gfx_fe0_dl_async", "top_vpp", 21),
	GATE_VDO1_1(CLK_VDO1_HDR_GFX_FE1_DL_ASYNC, "vdo1_hdr_gfx_fe1_dl_async", "top_vpp", 22),
	GATE_VDO1_1(CLK_VDO1_HDR_VDO_BE_DL_ASYNC, "vdo1_hdr_vdo_be_dl_async", "top_vpp", 23),
	/* VDO1_2 */
	GATE_VDO1_2(CLK_VDO1_DPI0, "vdo1_dpi0", "top_vpp", 0),
	GATE_VDO1_2(CLK_VDO1_DISP_MONITOR_DPI0, "vdo1_disp_monitor_dpi0", "top_vpp", 1),
	GATE_VDO1_2(CLK_VDO1_DPI1, "vdo1_dpi1", "top_vpp", 8),
	GATE_VDO1_2(CLK_VDO1_DISP_MONITOR_DPI1, "vdo1_disp_monitor_dpi1", "top_vpp", 9),
	GATE_VDO1_2_FLAGS(CLK_VDO1_DPINTF, "vdo1_dpintf", "top_dp", 16, CLK_SET_RATE_PARENT),
	GATE_VDO1_2(CLK_VDO1_DISP_MONITOR_DPINTF, "vdo1_disp_monitor_dpintf", "top_vpp", 17),
	/* VDO1_3 */
	GATE_VDO1_3(CLK_VDO1_26M_SLOW, "vdo1_26m_slow", "clk26m", 8),
	/* VDO1_4 */
	GATE_VDO1_4(CLK_VDO1_DPI1_HDMI, "vdo1_dpi1_hdmi", "hdmi_txpll", 0),
};

static const struct mtk_clk_desc vdo1_desc = {
	.clks = vdo1_clks,
	.num_clks = ARRAY_SIZE(vdo1_clks),
};

static const struct platform_device_id clk_mt8195_vdo1_id_table[] = {
	{ .name = "clk-mt8195-vdo1", .driver_data = (kernel_ulong_t)&vdo1_desc },
	{ /* sentinel */ }
};

static struct platform_driver clk_mt8195_vdo1_drv = {
	.probe = mtk_clk_pdev_probe,
	.remove = mtk_clk_pdev_remove,
	.driver = {
		.name = "clk-mt8195-vdo1",
	},
	.id_table = clk_mt8195_vdo1_id_table,
};
builtin_platform_driver(clk_mt8195_vdo1_drv);
