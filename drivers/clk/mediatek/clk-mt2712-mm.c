// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017 MediaTek Inc.
 * Author: Weiyi Lu <weiyi.lu@mediatek.com>
 */

#include <linux/clk-provider.h>
#include <linux/platform_device.h>

#include "clk-mtk.h"
#include "clk-gate.h"

#include <dt-bindings/clock/mt2712-clk.h>

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
	.set_ofs = 0x224,
	.clr_ofs = 0x228,
	.sta_ofs = 0x220,
};

#define GATE_MM0(_id, _name, _parent, _shift)	\
	GATE_MTK(_id, _name, _parent, &mm0_cg_regs, _shift, &mtk_clk_gate_ops_setclr)

#define GATE_MM1(_id, _name, _parent, _shift)	\
	GATE_MTK(_id, _name, _parent, &mm1_cg_regs, _shift, &mtk_clk_gate_ops_setclr)

#define GATE_MM2(_id, _name, _parent, _shift)	\
	GATE_MTK(_id, _name, _parent, &mm2_cg_regs, _shift, &mtk_clk_gate_ops_setclr)

static const struct mtk_gate mm_clks[] = {
	/* MM0 */
	GATE_MM0(CLK_MM_SMI_COMMON, "mm_smi_common", "mm_sel", 0),
	GATE_MM0(CLK_MM_SMI_LARB0, "mm_smi_larb0", "mm_sel", 1),
	GATE_MM0(CLK_MM_CAM_MDP, "mm_cam_mdp", "mm_sel", 2),
	GATE_MM0(CLK_MM_MDP_RDMA0, "mm_mdp_rdma0", "mm_sel", 3),
	GATE_MM0(CLK_MM_MDP_RDMA1, "mm_mdp_rdma1", "mm_sel", 4),
	GATE_MM0(CLK_MM_MDP_RSZ0, "mm_mdp_rsz0", "mm_sel", 5),
	GATE_MM0(CLK_MM_MDP_RSZ1, "mm_mdp_rsz1", "mm_sel", 6),
	GATE_MM0(CLK_MM_MDP_RSZ2, "mm_mdp_rsz2", "mm_sel", 7),
	GATE_MM0(CLK_MM_MDP_TDSHP0, "mm_mdp_tdshp0", "mm_sel", 8),
	GATE_MM0(CLK_MM_MDP_TDSHP1, "mm_mdp_tdshp1", "mm_sel", 9),
	GATE_MM0(CLK_MM_MDP_CROP, "mm_mdp_crop", "mm_sel", 10),
	GATE_MM0(CLK_MM_MDP_WDMA, "mm_mdp_wdma", "mm_sel", 11),
	GATE_MM0(CLK_MM_MDP_WROT0, "mm_mdp_wrot0", "mm_sel", 12),
	GATE_MM0(CLK_MM_MDP_WROT1, "mm_mdp_wrot1", "mm_sel", 13),
	GATE_MM0(CLK_MM_FAKE_ENG, "mm_fake_eng", "mm_sel", 14),
	GATE_MM0(CLK_MM_MUTEX_32K, "mm_mutex_32k", "clk32k", 15),
	GATE_MM0(CLK_MM_DISP_OVL0, "mm_disp_ovl0", "mm_sel", 16),
	GATE_MM0(CLK_MM_DISP_OVL1, "mm_disp_ovl1", "mm_sel", 17),
	GATE_MM0(CLK_MM_DISP_RDMA0, "mm_disp_rdma0", "mm_sel", 18),
	GATE_MM0(CLK_MM_DISP_RDMA1, "mm_disp_rdma1", "mm_sel", 19),
	GATE_MM0(CLK_MM_DISP_RDMA2, "mm_disp_rdma2", "mm_sel", 20),
	GATE_MM0(CLK_MM_DISP_WDMA0, "mm_disp_wdma0", "mm_sel", 21),
	GATE_MM0(CLK_MM_DISP_WDMA1, "mm_disp_wdma1", "mm_sel", 22),
	GATE_MM0(CLK_MM_DISP_COLOR0, "mm_disp_color0", "mm_sel", 23),
	GATE_MM0(CLK_MM_DISP_COLOR1, "mm_disp_color1", "mm_sel", 24),
	GATE_MM0(CLK_MM_DISP_AAL, "mm_disp_aal", "mm_sel", 25),
	GATE_MM0(CLK_MM_DISP_GAMMA, "mm_disp_gamma", "mm_sel", 26),
	GATE_MM0(CLK_MM_DISP_UFOE, "mm_disp_ufoe", "mm_sel", 27),
	GATE_MM0(CLK_MM_DISP_SPLIT0, "mm_disp_split0", "mm_sel", 28),
	GATE_MM0(CLK_MM_DISP_OD, "mm_disp_od", "mm_sel", 31),
	/* MM1 */
	GATE_MM1(CLK_MM_DISP_PWM0_MM, "mm_pwm0_mm", "mm_sel", 0),
	GATE_MM1(CLK_MM_DISP_PWM0_26M, "mm_pwm0_26m", "pwm_sel", 1),
	GATE_MM1(CLK_MM_DISP_PWM1_MM, "mm_pwm1_mm", "mm_sel", 2),
	GATE_MM1(CLK_MM_DISP_PWM1_26M, "mm_pwm1_26m", "pwm_sel", 3),
	GATE_MM1(CLK_MM_DSI0_ENGINE, "mm_dsi0_engine", "mm_sel", 4),
	GATE_MM1(CLK_MM_DSI0_DIGITAL, "mm_dsi0_digital", "dsi0_lntc", 5),
	GATE_MM1(CLK_MM_DSI1_ENGINE, "mm_dsi1_engine", "mm_sel", 6),
	GATE_MM1(CLK_MM_DSI1_DIGITAL, "mm_dsi1_digital", "dsi1_lntc", 7),
	GATE_MM1(CLK_MM_DPI_PIXEL, "mm_dpi_pixel", "vpll_dpix", 8),
	GATE_MM1(CLK_MM_DPI_ENGINE, "mm_dpi_engine", "mm_sel", 9),
	GATE_MM1(CLK_MM_DPI1_PIXEL, "mm_dpi1_pixel", "vpll3_dpix", 10),
	GATE_MM1(CLK_MM_DPI1_ENGINE, "mm_dpi1_engine", "mm_sel", 11),
	GATE_MM1(CLK_MM_LVDS_PIXEL, "mm_lvds_pixel", "vpll_dpix", 16),
	GATE_MM1(CLK_MM_LVDS_CTS, "mm_lvds_cts", "lvdstx", 17),
	GATE_MM1(CLK_MM_SMI_LARB4, "mm_smi_larb4", "mm_sel", 18),
	GATE_MM1(CLK_MM_SMI_COMMON1, "mm_smi_common1", "mm_sel", 21),
	GATE_MM1(CLK_MM_SMI_LARB5, "mm_smi_larb5", "mm_sel", 22),
	GATE_MM1(CLK_MM_MDP_RDMA2, "mm_mdp_rdma2", "mm_sel", 23),
	GATE_MM1(CLK_MM_MDP_TDSHP2, "mm_mdp_tdshp2", "mm_sel", 24),
	GATE_MM1(CLK_MM_DISP_OVL2, "mm_disp_ovl2", "mm_sel", 25),
	GATE_MM1(CLK_MM_DISP_WDMA2, "mm_disp_wdma2", "mm_sel", 26),
	GATE_MM1(CLK_MM_DISP_COLOR2, "mm_disp_color2", "mm_sel", 27),
	GATE_MM1(CLK_MM_DISP_AAL1, "mm_disp_aal1", "mm_sel", 28),
	GATE_MM1(CLK_MM_DISP_OD1, "mm_disp_od1", "mm_sel", 29),
	GATE_MM1(CLK_MM_LVDS1_PIXEL, "mm_lvds1_pixel", "vpll3_dpix", 30),
	GATE_MM1(CLK_MM_LVDS1_CTS, "mm_lvds1_cts", "lvdstx3", 31),
	/* MM2 */
	GATE_MM2(CLK_MM_SMI_LARB7, "mm_smi_larb7", "mm_sel", 0),
	GATE_MM2(CLK_MM_MDP_RDMA3, "mm_mdp_rdma3", "mm_sel", 1),
	GATE_MM2(CLK_MM_MDP_WROT2, "mm_mdp_wrot2", "mm_sel", 2),
	GATE_MM2(CLK_MM_DSI2, "mm_dsi2", "mm_sel", 3),
	GATE_MM2(CLK_MM_DSI2_DIGITAL, "mm_dsi2_digital", "dsi0_lntc", 4),
	GATE_MM2(CLK_MM_DSI3, "mm_dsi3", "mm_sel", 5),
	GATE_MM2(CLK_MM_DSI3_DIGITAL, "mm_dsi3_digital", "dsi1_lntc", 6),
};

static const struct mtk_clk_desc mm_desc = {
	.clks = mm_clks,
	.num_clks = ARRAY_SIZE(mm_clks),
};

static const struct platform_device_id clk_mt2712_mm_id_table[] = {
	{ .name = "clk-mt2712-mm", .driver_data = (kernel_ulong_t)&mm_desc },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(platform, clk_mt2712_mm_id_table);

static struct platform_driver clk_mt2712_mm_drv = {
	.probe = mtk_clk_pdev_probe,
	.remove = mtk_clk_pdev_remove,
	.driver = {
		.name = "clk-mt2712-mm",
	},
	.id_table = clk_mt2712_mm_id_table,
};
module_platform_driver(clk_mt2712_mm_drv);
MODULE_LICENSE("GPL");
