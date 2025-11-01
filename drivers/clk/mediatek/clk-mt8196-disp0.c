// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2025 MediaTek Inc.
 *                    Guangjie Song <guangjie.song@mediatek.com>
 * Copyright (c) 2025 Collabora Ltd.
 *                    Laura Nao <laura.nao@collabora.com>
 */
#include <dt-bindings/clock/mediatek,mt8196-clock.h>

#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>

#include "clk-gate.h"
#include "clk-mtk.h"

static const struct mtk_gate_regs mm0_cg_regs = {
	.set_ofs = 0x104,
	.clr_ofs = 0x108,
	.sta_ofs = 0x100,
};

static const struct mtk_gate_regs mm0_hwv_regs = {
	.set_ofs = 0x0020,
	.clr_ofs = 0x0024,
	.sta_ofs = 0x2c10,
};

static const struct mtk_gate_regs mm1_cg_regs = {
	.set_ofs = 0x114,
	.clr_ofs = 0x118,
	.sta_ofs = 0x110,
};

static const struct mtk_gate_regs mm1_hwv_regs = {
	.set_ofs = 0x0028,
	.clr_ofs = 0x002c,
	.sta_ofs = 0x2c14,
};

#define GATE_MM0(_id, _name, _parent, _shift) {	\
		.id = _id,			\
		.name = _name,			\
		.parent_name = _parent,		\
		.regs = &mm0_cg_regs,		\
		.shift = _shift,		\
		.flags = CLK_OPS_PARENT_ENABLE,	\
		.ops = &mtk_clk_gate_ops_setclr,\
	}

#define GATE_HWV_MM0(_id, _name, _parent, _shift) {	\
		.id = _id,				\
		.name = _name,				\
		.parent_name = _parent,			\
		.regs = &mm0_cg_regs,			\
		.hwv_regs = &mm0_hwv_regs,		\
		.shift = _shift,			\
		.ops = &mtk_clk_gate_hwv_ops_setclr,	\
		.flags =  CLK_OPS_PARENT_ENABLE		\
	}

#define GATE_MM1(_id, _name, _parent, _shift) {	\
		.id = _id,			\
		.name = _name,			\
		.parent_name = _parent,		\
		.regs = &mm1_cg_regs,		\
		.shift = _shift,		\
		.flags = CLK_OPS_PARENT_ENABLE,	\
		.ops = &mtk_clk_gate_ops_setclr,\
	}

#define GATE_HWV_MM1(_id, _name, _parent, _shift) {	\
		.id = _id,				\
		.name = _name,				\
		.parent_name = _parent,			\
		.regs = &mm1_cg_regs,			\
		.hwv_regs = &mm1_hwv_regs,		\
		.shift = _shift,			\
		.ops = &mtk_clk_gate_hwv_ops_setclr,	\
		.flags = CLK_OPS_PARENT_ENABLE,		\
	}

static const struct mtk_gate mm_clks[] = {
	/* MM0 */
	GATE_HWV_MM0(CLK_MM_CONFIG, "mm_config", "disp", 0),
	GATE_HWV_MM0(CLK_MM_DISP_MUTEX0, "mm_disp_mutex0", "disp", 1),
	GATE_HWV_MM0(CLK_MM_DISP_AAL0, "mm_disp_aal0", "disp", 2),
	GATE_HWV_MM0(CLK_MM_DISP_AAL1, "mm_disp_aal1", "disp", 3),
	GATE_MM0(CLK_MM_DISP_C3D0, "mm_disp_c3d0", "disp", 4),
	GATE_MM0(CLK_MM_DISP_C3D1, "mm_disp_c3d1", "disp", 5),
	GATE_MM0(CLK_MM_DISP_C3D2, "mm_disp_c3d2", "disp", 6),
	GATE_MM0(CLK_MM_DISP_C3D3, "mm_disp_c3d3", "disp", 7),
	GATE_MM0(CLK_MM_DISP_CCORR0, "mm_disp_ccorr0", "disp", 8),
	GATE_MM0(CLK_MM_DISP_CCORR1, "mm_disp_ccorr1", "disp", 9),
	GATE_MM0(CLK_MM_DISP_CCORR2, "mm_disp_ccorr2", "disp", 10),
	GATE_MM0(CLK_MM_DISP_CCORR3, "mm_disp_ccorr3", "disp", 11),
	GATE_MM0(CLK_MM_DISP_CHIST0, "mm_disp_chist0", "disp", 12),
	GATE_MM0(CLK_MM_DISP_CHIST1, "mm_disp_chist1", "disp", 13),
	GATE_MM0(CLK_MM_DISP_COLOR0, "mm_disp_color0", "disp", 14),
	GATE_MM0(CLK_MM_DISP_COLOR1, "mm_disp_color1", "disp", 15),
	GATE_MM0(CLK_MM_DISP_DITHER0, "mm_disp_dither0", "disp", 16),
	GATE_MM0(CLK_MM_DISP_DITHER1, "mm_disp_dither1", "disp", 17),
	GATE_HWV_MM0(CLK_MM_DISP_DLI_ASYNC0, "mm_disp_dli_async0", "disp", 18),
	GATE_HWV_MM0(CLK_MM_DISP_DLI_ASYNC1, "mm_disp_dli_async1", "disp", 19),
	GATE_HWV_MM0(CLK_MM_DISP_DLI_ASYNC2, "mm_disp_dli_async2", "disp", 20),
	GATE_HWV_MM0(CLK_MM_DISP_DLI_ASYNC3, "mm_disp_dli_async3", "disp", 21),
	GATE_HWV_MM0(CLK_MM_DISP_DLI_ASYNC4, "mm_disp_dli_async4", "disp", 22),
	GATE_HWV_MM0(CLK_MM_DISP_DLI_ASYNC5, "mm_disp_dli_async5", "disp", 23),
	GATE_HWV_MM0(CLK_MM_DISP_DLI_ASYNC6, "mm_disp_dli_async6", "disp", 24),
	GATE_HWV_MM0(CLK_MM_DISP_DLI_ASYNC7, "mm_disp_dli_async7", "disp", 25),
	GATE_HWV_MM0(CLK_MM_DISP_DLI_ASYNC8, "mm_disp_dli_async8", "disp", 26),
	GATE_HWV_MM0(CLK_MM_DISP_DLI_ASYNC9, "mm_disp_dli_async9", "disp", 27),
	GATE_HWV_MM0(CLK_MM_DISP_DLI_ASYNC10, "mm_disp_dli_async10", "disp", 28),
	GATE_HWV_MM0(CLK_MM_DISP_DLI_ASYNC11, "mm_disp_dli_async11", "disp", 29),
	GATE_HWV_MM0(CLK_MM_DISP_DLI_ASYNC12, "mm_disp_dli_async12", "disp", 30),
	GATE_HWV_MM0(CLK_MM_DISP_DLI_ASYNC13, "mm_disp_dli_async13", "disp", 31),
	/* MM1 */
	GATE_HWV_MM1(CLK_MM_DISP_DLI_ASYNC14, "mm_disp_dli_async14", "disp", 0),
	GATE_HWV_MM1(CLK_MM_DISP_DLI_ASYNC15, "mm_disp_dli_async15", "disp", 1),
	GATE_HWV_MM1(CLK_MM_DISP_DLO_ASYNC0, "mm_disp_dlo_async0", "disp", 2),
	GATE_HWV_MM1(CLK_MM_DISP_DLO_ASYNC1, "mm_disp_dlo_async1", "disp", 3),
	GATE_HWV_MM1(CLK_MM_DISP_DLO_ASYNC2, "mm_disp_dlo_async2", "disp", 4),
	GATE_HWV_MM1(CLK_MM_DISP_DLO_ASYNC3, "mm_disp_dlo_async3", "disp", 5),
	GATE_HWV_MM1(CLK_MM_DISP_DLO_ASYNC4, "mm_disp_dlo_async4", "disp", 6),
	GATE_HWV_MM1(CLK_MM_DISP_DLO_ASYNC5, "mm_disp_dlo_async5", "disp", 7),
	GATE_HWV_MM1(CLK_MM_DISP_DLO_ASYNC6, "mm_disp_dlo_async6", "disp", 8),
	GATE_HWV_MM1(CLK_MM_DISP_DLO_ASYNC7, "mm_disp_dlo_async7", "disp", 9),
	GATE_HWV_MM1(CLK_MM_DISP_DLO_ASYNC8, "mm_disp_dlo_async8", "disp", 10),
	GATE_MM1(CLK_MM_DISP_GAMMA0, "mm_disp_gamma0", "disp", 11),
	GATE_MM1(CLK_MM_DISP_GAMMA1, "mm_disp_gamma1", "disp", 12),
	GATE_MM1(CLK_MM_MDP_AAL0, "mm_mdp_aal0", "disp", 13),
	GATE_MM1(CLK_MM_MDP_AAL1, "mm_mdp_aal1", "disp", 14),
	GATE_HWV_MM1(CLK_MM_MDP_RDMA0, "mm_mdp_rdma0", "disp", 15),
	GATE_HWV_MM1(CLK_MM_DISP_POSTMASK0, "mm_disp_postmask0", "disp", 16),
	GATE_HWV_MM1(CLK_MM_DISP_POSTMASK1, "mm_disp_postmask1", "disp", 17),
	GATE_HWV_MM1(CLK_MM_MDP_RSZ0, "mm_mdp_rsz0", "disp", 18),
	GATE_HWV_MM1(CLK_MM_MDP_RSZ1, "mm_mdp_rsz1", "disp", 19),
	GATE_HWV_MM1(CLK_MM_DISP_SPR0, "mm_disp_spr0", "disp", 20),
	GATE_MM1(CLK_MM_DISP_TDSHP0, "mm_disp_tdshp0", "disp", 21),
	GATE_MM1(CLK_MM_DISP_TDSHP1, "mm_disp_tdshp1", "disp", 22),
	GATE_HWV_MM1(CLK_MM_DISP_WDMA0, "mm_disp_wdma0", "disp", 23),
	GATE_HWV_MM1(CLK_MM_DISP_Y2R0, "mm_disp_y2r0", "disp", 24),
	GATE_HWV_MM1(CLK_MM_SMI_SUB_COMM0, "mm_ssc", "disp", 25),
	GATE_HWV_MM1(CLK_MM_DISP_FAKE_ENG0, "mm_disp_fake_eng0", "disp", 26),
};

static const struct mtk_clk_desc mm_mcd = {
	.clks = mm_clks,
	.num_clks = ARRAY_SIZE(mm_clks),
};

static const struct platform_device_id clk_mt8196_disp0_id_table[] = {
	{ .name = "clk-mt8196-disp0", .driver_data = (kernel_ulong_t)&mm_mcd },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(platform, clk_mt8196_disp0_id_table);

static struct platform_driver clk_mt8196_disp0_drv = {
	.probe = mtk_clk_pdev_probe,
	.remove = mtk_clk_pdev_remove,
	.driver = {
		.name = "clk-mt8196-disp0",
	},
	.id_table = clk_mt8196_disp0_id_table,
};
module_platform_driver(clk_mt8196_disp0_drv);

MODULE_DESCRIPTION("MediaTek MT8196 disp0 clocks driver");
MODULE_LICENSE("GPL");
