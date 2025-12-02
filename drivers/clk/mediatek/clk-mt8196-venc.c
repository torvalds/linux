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

static const struct mtk_gate_regs ven10_cg_regs = {
	.set_ofs = 0x4,
	.clr_ofs = 0x8,
	.sta_ofs = 0x0,
};

static const struct mtk_gate_regs ven10_hwv_regs = {
	.set_ofs = 0x00b8,
	.clr_ofs = 0x00bc,
	.sta_ofs = 0x2c5c,
};

static const struct mtk_gate_regs ven11_cg_regs = {
	.set_ofs = 0x10,
	.clr_ofs = 0x14,
	.sta_ofs = 0x10,
};

static const struct mtk_gate_regs ven11_hwv_regs = {
	.set_ofs = 0x00c0,
	.clr_ofs = 0x00c4,
	.sta_ofs = 0x2c60,
};

#define GATE_VEN10(_id, _name, _parent, _shift) {	\
		.id = _id,				\
		.name = _name,				\
		.parent_name = _parent,			\
		.regs = &ven10_cg_regs,			\
		.shift = _shift,			\
		.flags = CLK_OPS_PARENT_ENABLE,		\
		.ops = &mtk_clk_gate_ops_setclr_inv,	\
	}

#define GATE_HWV_VEN10_FLAGS(_id, _name, _parent, _shift, _flags) {	\
		.id = _id,						\
		.name = _name,						\
		.parent_name = _parent,					\
		.regs = &ven10_cg_regs,					\
		.hwv_regs = &ven10_hwv_regs,				\
		.shift = _shift,					\
		.ops = &mtk_clk_gate_hwv_ops_setclr_inv,		\
		.flags = (_flags) |					\
			 CLK_OPS_PARENT_ENABLE,				\
	}

#define GATE_HWV_VEN10(_id, _name, _parent, _shift)	\
	GATE_HWV_VEN10_FLAGS(_id, _name, _parent, _shift, 0)

#define GATE_HWV_VEN11(_id, _name, _parent, _shift) {	\
		.id = _id,				\
		.name = _name,				\
		.parent_name = _parent,			\
		.regs = &ven11_cg_regs,			\
		.hwv_regs = &ven11_hwv_regs,		\
		.shift = _shift,			\
		.ops = &mtk_clk_gate_hwv_ops_setclr_inv,\
		.flags = CLK_OPS_PARENT_ENABLE		\
	}

static const struct mtk_gate ven1_clks[] = {
	/* VEN10 */
	GATE_HWV_VEN10(CLK_VEN1_CKE0_LARB, "ven1_larb", "venc", 0),
	GATE_HWV_VEN10(CLK_VEN1_CKE1_VENC, "ven1_venc", "venc", 4),
	GATE_VEN10(CLK_VEN1_CKE2_JPGENC, "ven1_jpgenc", "venc", 8),
	GATE_VEN10(CLK_VEN1_CKE3_JPGDEC, "ven1_jpgdec", "venc", 12),
	GATE_VEN10(CLK_VEN1_CKE4_JPGDEC_C1, "ven1_jpgdec_c1", "venc", 16),
	GATE_HWV_VEN10(CLK_VEN1_CKE5_GALS, "ven1_gals", "venc", 28),
	GATE_HWV_VEN10(CLK_VEN1_CKE29_VENC_ADAB_CTRL, "ven1_venc_adab_ctrl",
			"venc", 29),
	GATE_HWV_VEN10_FLAGS(CLK_VEN1_CKE29_VENC_XPC_CTRL,
			      "ven1_venc_xpc_ctrl", "venc", 30,
			      CLK_IGNORE_UNUSED),
	GATE_HWV_VEN10(CLK_VEN1_CKE6_GALS_SRAM, "ven1_gals_sram", "venc", 31),
	/* VEN11 */
	GATE_HWV_VEN11(CLK_VEN1_RES_FLAT, "ven1_res_flat", "venc", 0),
};

static const struct mtk_clk_desc ven1_mcd = {
	.clks = ven1_clks,
	.num_clks = ARRAY_SIZE(ven1_clks),
	.need_runtime_pm = true,
};

static const struct mtk_gate_regs ven20_hwv_regs = {
	.set_ofs = 0x00c8,
	.clr_ofs = 0x00cc,
	.sta_ofs = 0x2c64,
};

static const struct mtk_gate_regs ven21_hwv_regs = {
	.set_ofs = 0x00d0,
	.clr_ofs = 0x00d4,
	.sta_ofs = 0x2c68,
};

#define GATE_VEN20(_id, _name, _parent, _shift) {	\
		.id = _id,				\
		.name = _name,				\
		.parent_name = _parent,			\
		.regs = &ven10_cg_regs,			\
		.shift = _shift,			\
		.flags = CLK_OPS_PARENT_ENABLE,		\
		.ops = &mtk_clk_gate_ops_setclr_inv,	\
	}

#define GATE_HWV_VEN20(_id, _name, _parent, _shift) {	\
		.id = _id,				\
		.name = _name,				\
		.parent_name = _parent,			\
		.regs = &ven10_cg_regs,			\
		.hwv_regs = &ven20_hwv_regs,		\
		.shift = _shift,			\
		.ops = &mtk_clk_gate_hwv_ops_setclr_inv,\
		.flags = CLK_OPS_PARENT_ENABLE,		\
	}

#define GATE_HWV_VEN21(_id, _name, _parent, _shift) {	\
		.id = _id,				\
		.name = _name,				\
		.parent_name = _parent,			\
		.regs = &ven11_cg_regs,			\
		.hwv_regs = &ven21_hwv_regs,		\
		.shift = _shift,			\
		.ops = &mtk_clk_gate_hwv_ops_setclr,	\
		.flags = CLK_OPS_PARENT_ENABLE		\
	}

static const struct mtk_gate ven2_clks[] = {
	/* VEN20 */
	GATE_HWV_VEN20(CLK_VEN2_CKE0_LARB, "ven2_larb", "venc", 0),
	GATE_HWV_VEN20(CLK_VEN2_CKE1_VENC, "ven2_venc", "venc", 4),
	GATE_VEN20(CLK_VEN2_CKE2_JPGENC, "ven2_jpgenc", "venc", 8),
	GATE_VEN20(CLK_VEN2_CKE3_JPGDEC, "ven2_jpgdec", "venc", 12),
	GATE_HWV_VEN20(CLK_VEN2_CKE5_GALS, "ven2_gals", "venc", 28),
	GATE_HWV_VEN20(CLK_VEN2_CKE29_VENC_XPC_CTRL, "ven2_venc_xpc_ctrl", "venc", 30),
	GATE_HWV_VEN20(CLK_VEN2_CKE6_GALS_SRAM, "ven2_gals_sram", "venc", 31),
	/* VEN21 */
	GATE_HWV_VEN21(CLK_VEN2_RES_FLAT, "ven2_res_flat", "venc", 0),
};

static const struct mtk_clk_desc ven2_mcd = {
	.clks = ven2_clks,
	.num_clks = ARRAY_SIZE(ven2_clks),
	.need_runtime_pm = true,
};

static const struct mtk_gate_regs ven_c20_hwv_regs = {
	.set_ofs = 0x00d8,
	.clr_ofs = 0x00dc,
	.sta_ofs = 0x2c6c,
};

static const struct mtk_gate_regs ven_c21_hwv_regs = {
	.set_ofs = 0x00e0,
	.clr_ofs = 0x00e4,
	.sta_ofs = 0x2c70,
};

#define GATE_HWV_VEN_C20(_id, _name, _parent, _shift) {\
		.id = _id,				\
		.name = _name,				\
		.parent_name = _parent,			\
		.regs = &ven10_cg_regs,		\
		.hwv_regs = &ven_c20_hwv_regs,		\
		.shift = _shift,			\
		.ops = &mtk_clk_gate_hwv_ops_setclr_inv,\
		.flags = CLK_OPS_PARENT_ENABLE,		\
	}

#define GATE_HWV_VEN_C21(_id, _name, _parent, _shift) {\
		.id = _id,				\
		.name = _name,				\
		.parent_name = _parent,			\
		.regs = &ven11_cg_regs,		\
		.hwv_regs = &ven_c21_hwv_regs,		\
		.shift = _shift,			\
		.ops = &mtk_clk_gate_hwv_ops_setclr,	\
		.flags = CLK_OPS_PARENT_ENABLE,		\
	}

static const struct mtk_gate ven_c2_clks[] = {
	/* VEN_C20 */
	GATE_HWV_VEN_C20(CLK_VEN_C2_CKE0_LARB, "ven_c2_larb", "venc", 0),
	GATE_HWV_VEN_C20(CLK_VEN_C2_CKE1_VENC, "ven_c2_venc", "venc", 4),
	GATE_HWV_VEN_C20(CLK_VEN_C2_CKE5_GALS, "ven_c2_gals", "venc", 28),
	GATE_HWV_VEN_C20(CLK_VEN_C2_CKE29_VENC_XPC_CTRL, "ven_c2_venc_xpc_ctrl",
			  "venc", 30),
	GATE_HWV_VEN_C20(CLK_VEN_C2_CKE6_GALS_SRAM, "ven_c2_gals_sram", "venc", 31),
	/* VEN_C21 */
	GATE_HWV_VEN_C21(CLK_VEN_C2_RES_FLAT, "ven_c2_res_flat", "venc", 0),
};

static const struct mtk_clk_desc ven_c2_mcd = {
	.clks = ven_c2_clks,
	.num_clks = ARRAY_SIZE(ven_c2_clks),
	.need_runtime_pm = true,
};

static const struct of_device_id of_match_clk_mt8196_venc[] = {
	{ .compatible = "mediatek,mt8196-vencsys", .data = &ven1_mcd },
	{ .compatible = "mediatek,mt8196-vencsys-c1", .data = &ven2_mcd },
	{ .compatible = "mediatek,mt8196-vencsys-c2", .data = &ven_c2_mcd },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, of_match_clk_mt8196_venc);

static struct platform_driver clk_mt8196_venc_drv = {
	.probe = mtk_clk_simple_probe,
	.remove = mtk_clk_simple_remove,
	.driver = {
		.name = "clk-mt8196-venc",
		.of_match_table = of_match_clk_mt8196_venc,
	},
};
module_platform_driver(clk_mt8196_venc_drv);

MODULE_DESCRIPTION("MediaTek MT8196 Video Encoders clocks driver");
MODULE_LICENSE("GPL");
