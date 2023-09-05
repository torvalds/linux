// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright (c) 2022 MediaTek Inc.
// Author: Chun-Jie Chen <chun-jie.chen@mediatek.com>

#include <linux/clk-provider.h>
#include <linux/platform_device.h>
#include <dt-bindings/clock/mt8186-clk.h>

#include "clk-gate.h"
#include "clk-mtk.h"

static const struct mtk_gate_regs mm0_cg_regs = {
	.set_ofs = 0x104,
	.clr_ofs = 0x108,
	.sta_ofs = 0x100,
};

static const struct mtk_gate_regs mm1_cg_regs = {
	.set_ofs = 0x1a4,
	.clr_ofs = 0x1a8,
	.sta_ofs = 0x1a0,
};

#define GATE_MM0(_id, _name, _parent, _shift)			\
	GATE_MTK(_id, _name, _parent, &mm0_cg_regs, _shift, &mtk_clk_gate_ops_setclr)

#define GATE_MM1(_id, _name, _parent, _shift)			\
	GATE_MTK(_id, _name, _parent, &mm1_cg_regs, _shift, &mtk_clk_gate_ops_setclr)

static const struct mtk_gate mm_clks[] = {
	/* MM0 */
	GATE_MM0(CLK_MM_DISP_MUTEX0, "mm_disp_mutex0", "top_disp", 0),
	GATE_MM0(CLK_MM_APB_MM_BUS, "mm_apb_mm_bus", "top_disp", 1),
	GATE_MM0(CLK_MM_DISP_OVL0, "mm_disp_ovl0", "top_disp", 2),
	GATE_MM0(CLK_MM_DISP_RDMA0, "mm_disp_rdma0", "top_disp", 3),
	GATE_MM0(CLK_MM_DISP_OVL0_2L, "mm_disp_ovl0_2l", "top_disp", 4),
	GATE_MM0(CLK_MM_DISP_WDMA0, "mm_disp_wdma0", "top_disp", 5),
	GATE_MM0(CLK_MM_DISP_RSZ0, "mm_disp_rsz0", "top_disp", 7),
	GATE_MM0(CLK_MM_DISP_AAL0, "mm_disp_aal0", "top_disp", 8),
	GATE_MM0(CLK_MM_DISP_CCORR0, "mm_disp_ccorr0", "top_disp", 9),
	GATE_MM0(CLK_MM_DISP_COLOR0, "mm_disp_color0", "top_disp", 10),
	GATE_MM0(CLK_MM_SMI_INFRA, "mm_smi_infra", "top_disp", 11),
	GATE_MM0(CLK_MM_DISP_DSC_WRAP0, "mm_disp_dsc_wrap0", "top_disp", 12),
	GATE_MM0(CLK_MM_DISP_GAMMA0, "mm_disp_gamma0", "top_disp", 13),
	GATE_MM0(CLK_MM_DISP_POSTMASK0, "mm_disp_postmask0", "top_disp", 14),
	GATE_MM0(CLK_MM_DISP_DITHER0, "mm_disp_dither0", "top_disp", 16),
	GATE_MM0(CLK_MM_SMI_COMMON, "mm_smi_common", "top_disp", 17),
	GATE_MM0(CLK_MM_DSI0, "mm_dsi0", "top_disp", 19),
	GATE_MM0(CLK_MM_DISP_FAKE_ENG0, "mm_disp_fake_eng0", "top_disp", 20),
	GATE_MM0(CLK_MM_DISP_FAKE_ENG1, "mm_disp_fake_eng1", "top_disp", 21),
	GATE_MM0(CLK_MM_SMI_GALS, "mm_smi_gals", "top_disp", 22),
	GATE_MM0(CLK_MM_SMI_IOMMU, "mm_smi_iommu", "top_disp", 24),
	GATE_MM0(CLK_MM_DISP_RDMA1, "mm_disp_rdma1", "top_disp", 25),
	GATE_MM0(CLK_MM_DISP_DPI, "mm_disp_dpi", "top_disp", 26),
	/* MM1 */
	GATE_MM1(CLK_MM_DSI0_DSI_CK_DOMAIN, "mm_dsi0_dsi_domain", "top_disp", 0),
	GATE_MM1(CLK_MM_DISP_26M, "mm_disp_26m_ck", "top_disp", 10),
};

static const struct mtk_clk_desc mm_desc = {
	.clks = mm_clks,
	.num_clks = ARRAY_SIZE(mm_clks),
};

static const struct platform_device_id clk_mt8186_mm_id_table[] = {
	{ .name = "clk-mt8186-mm", .driver_data = (kernel_ulong_t)&mm_desc },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(platform, clk_mt8186_mm_id_table);

static struct platform_driver clk_mt8186_mm_drv = {
	.probe = mtk_clk_pdev_probe,
	.remove_new = mtk_clk_pdev_remove,
	.driver = {
		.name = "clk-mt8186-mm",
	},
	.id_table = clk_mt8186_mm_id_table,
};
module_platform_driver(clk_mt8186_mm_drv);
MODULE_LICENSE("GPL");
