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

#define GATE_MM0(_id, _name, _parent, _shift)	\
	GATE_MTK(_id, _name, _parent, &mm0_cg_regs, _shift, &mtk_clk_gate_ops_setclr)

#define GATE_MM1(_id, _name, _parent, _shift)	\
	GATE_MTK(_id, _name, _parent, &mm1_cg_regs, _shift, &mtk_clk_gate_ops_setclr)

static const struct mtk_gate mm_clks[] = {
	/* MM0 */
	GATE_MM0(CLK_MM_SMI_COMMON, "mm_smi_common", "smi_mm", 0),
	GATE_MM0(CLK_MM_SMI_LARB0, "mm_smi_larb0", "smi_mm", 1),
	GATE_MM0(CLK_MM_CAM_MDP, "mm_cam_mdp", "smi_mm", 2),
	GATE_MM0(CLK_MM_MDP_RDMA, "mm_mdp_rdma", "smi_mm", 3),
	GATE_MM0(CLK_MM_MDP_RSZ0, "mm_mdp_rsz0", "smi_mm", 4),
	GATE_MM0(CLK_MM_MDP_RSZ1, "mm_mdp_rsz1", "smi_mm", 5),
	GATE_MM0(CLK_MM_MDP_TDSHP, "mm_mdp_tdshp", "smi_mm", 6),
	GATE_MM0(CLK_MM_MDP_WDMA, "mm_mdp_wdma", "smi_mm", 7),
	GATE_MM0(CLK_MM_MDP_WROT, "mm_mdp_wrot", "smi_mm", 8),
	GATE_MM0(CLK_MM_FAKE_ENG, "mm_fake_eng", "smi_mm", 9),
	GATE_MM0(CLK_MM_DISP_OVL0, "mm_disp_ovl0", "smi_mm", 10),
	GATE_MM0(CLK_MM_DISP_RDMA0, "mm_disp_rdma0", "smi_mm", 11),
	GATE_MM0(CLK_MM_DISP_RDMA1, "mm_disp_rdma1", "smi_mm", 12),
	GATE_MM0(CLK_MM_DISP_WDMA, "mm_disp_wdma", "smi_mm", 13),
	GATE_MM0(CLK_MM_DISP_COLOR, "mm_disp_color", "smi_mm", 14),
	GATE_MM0(CLK_MM_DISP_CCORR, "mm_disp_ccorr", "smi_mm", 15),
	GATE_MM0(CLK_MM_DISP_AAL, "mm_disp_aal", "smi_mm", 16),
	GATE_MM0(CLK_MM_DISP_GAMMA, "mm_disp_gamma", "smi_mm", 17),
	GATE_MM0(CLK_MM_DISP_DITHER, "mm_disp_dither", "smi_mm", 18),
	GATE_MM0(CLK_MM_DISP_UFOE, "mm_disp_ufoe", "smi_mm", 19),
	/* MM1 */
	GATE_MM1(CLK_MM_DISP_PWM_MM, "mm_disp_pwm_mm", "smi_mm", 0),
	GATE_MM1(CLK_MM_DISP_PWM_26M, "mm_disp_pwm_26m", "smi_mm", 1),
	GATE_MM1(CLK_MM_DSI_ENGINE, "mm_dsi_engine", "smi_mm", 2),
	GATE_MM1(CLK_MM_DSI_DIGITAL, "mm_dsi_digital", "dsi0_lntc_dsick", 3),
	GATE_MM1(CLK_MM_DPI0_ENGINE, "mm_dpi0_engine", "smi_mm", 4),
	GATE_MM1(CLK_MM_DPI0_PXL, "mm_dpi0_pxl", "rg_fdpi0", 5),
	GATE_MM1(CLK_MM_LVDS_PXL, "mm_lvds_pxl", "vpll_dpix", 14),
	GATE_MM1(CLK_MM_LVDS_CTS, "mm_lvds_cts", "lvdstx_dig_cts", 15),
	GATE_MM1(CLK_MM_DPI1_ENGINE, "mm_dpi1_engine", "smi_mm", 16),
	GATE_MM1(CLK_MM_DPI1_PXL, "mm_dpi1_pxl", "rg_fdpi1", 17),
	GATE_MM1(CLK_MM_HDMI_PXL, "mm_hdmi_pxl", "rg_fdpi1", 18),
	GATE_MM1(CLK_MM_HDMI_SPDIF, "mm_hdmi_spdif", "apll12_div6", 19),
	GATE_MM1(CLK_MM_HDMI_ADSP_BCK, "mm_hdmi_adsp_b", "apll12_div4b", 20),
	GATE_MM1(CLK_MM_HDMI_PLL, "mm_hdmi_pll", "hdmtx_dig_cts", 21),
};

static const struct mtk_clk_desc mm_desc = {
	.clks = mm_clks,
	.num_clks = ARRAY_SIZE(mm_clks),
};

static const struct platform_device_id clk_mt8167_mm_id_table[] = {
	{ .name = "clk-mt8167-mm", .driver_data = (kernel_ulong_t)&mm_desc },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(platform, clk_mt8167_mm_id_table);

static struct platform_driver clk_mt8167_mm_drv = {
	.probe = mtk_clk_pdev_probe,
	.remove_new = mtk_clk_pdev_remove,
	.driver = {
		.name = "clk-mt8167-mm",
	},
	.id_table = clk_mt8167_mm_id_table,
};
module_platform_driver(clk_mt8167_mm_drv);
MODULE_LICENSE("GPL");
