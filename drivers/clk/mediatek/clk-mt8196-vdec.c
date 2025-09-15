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

static const struct mtk_gate_regs vde20_cg_regs = {
	.set_ofs = 0x0,
	.clr_ofs = 0x4,
	.sta_ofs = 0x0,
};

static const struct mtk_gate_regs vde20_hwv_regs = {
	.set_ofs = 0x0088,
	.clr_ofs = 0x008c,
	.sta_ofs = 0x2c44,
};

static const struct mtk_gate_regs vde21_cg_regs = {
	.set_ofs = 0x200,
	.clr_ofs = 0x204,
	.sta_ofs = 0x200,
};

static const struct mtk_gate_regs vde21_hwv_regs = {
	.set_ofs = 0x0080,
	.clr_ofs = 0x0084,
	.sta_ofs = 0x2c40,
};

static const struct mtk_gate_regs vde22_cg_regs = {
	.set_ofs = 0x8,
	.clr_ofs = 0xc,
	.sta_ofs = 0x8,
};

static const struct mtk_gate_regs vde22_hwv_regs = {
	.set_ofs = 0x0078,
	.clr_ofs = 0x007c,
	.sta_ofs = 0x2c3c,
};

#define GATE_HWV_VDE20(_id, _name, _parent, _shift) {	\
		.id = _id,				\
		.name = _name,				\
		.parent_name = _parent,			\
		.regs = &vde20_cg_regs,			\
		.hwv_regs = &vde20_hwv_regs,		\
		.shift = _shift,			\
		.ops = &mtk_clk_gate_hwv_ops_setclr_inv,\
		.flags = CLK_OPS_PARENT_ENABLE,		\
	}

#define GATE_HWV_VDE21(_id, _name, _parent, _shift) {	\
		.id = _id,				\
		.name = _name,				\
		.parent_name = _parent,			\
		.regs = &vde21_cg_regs,			\
		.hwv_regs = &vde21_hwv_regs,		\
		.shift = _shift,			\
		.ops = &mtk_clk_gate_hwv_ops_setclr_inv,\
		.flags = CLK_OPS_PARENT_ENABLE,		\
	}

#define GATE_HWV_VDE22(_id, _name, _parent, _shift) {	\
		.id = _id,				\
		.name = _name,				\
		.parent_name = _parent,			\
		.regs = &vde22_cg_regs,			\
		.hwv_regs = &vde22_hwv_regs,		\
		.shift = _shift,			\
		.ops = &mtk_clk_gate_hwv_ops_setclr_inv,\
		.flags = CLK_OPS_PARENT_ENABLE |	\
			 CLK_IGNORE_UNUSED,		\
	}

static const struct mtk_gate vde2_clks[] = {
	/* VDE20 */
	GATE_HWV_VDE20(CLK_VDE2_VDEC_CKEN, "vde2_vdec_cken", "vdec", 0),
	GATE_HWV_VDE20(CLK_VDE2_VDEC_ACTIVE, "vde2_vdec_active", "vdec", 4),
	GATE_HWV_VDE20(CLK_VDE2_VDEC_CKEN_ENG, "vde2_vdec_cken_eng", "vdec", 8),
	/* VDE21 */
	GATE_HWV_VDE21(CLK_VDE2_LAT_CKEN, "vde2_lat_cken", "vdec", 0),
	GATE_HWV_VDE21(CLK_VDE2_LAT_ACTIVE, "vde2_lat_active", "vdec", 4),
	GATE_HWV_VDE21(CLK_VDE2_LAT_CKEN_ENG, "vde2_lat_cken_eng", "vdec", 8),
	/* VDE22 */
	GATE_HWV_VDE22(CLK_VDE2_LARB1_CKEN, "vde2_larb1_cken", "vdec", 0),
};

static const struct mtk_clk_desc vde2_mcd = {
	.clks = vde2_clks,
	.num_clks = ARRAY_SIZE(vde2_clks),
	.need_runtime_pm = true,
};

static const struct mtk_gate_regs vde10_hwv_regs = {
	.set_ofs = 0x00a0,
	.clr_ofs = 0x00a4,
	.sta_ofs = 0x2c50,
};

static const struct mtk_gate_regs vde11_cg_regs = {
	.set_ofs = 0x1e0,
	.clr_ofs = 0x1e0,
	.sta_ofs = 0x1e0,
};

static const struct mtk_gate_regs vde11_hwv_regs = {
	.set_ofs = 0x00b0,
	.clr_ofs = 0x00b4,
	.sta_ofs = 0x2c58,
};

static const struct mtk_gate_regs vde12_cg_regs = {
	.set_ofs = 0x1ec,
	.clr_ofs = 0x1ec,
	.sta_ofs = 0x1ec,
};

static const struct mtk_gate_regs vde12_hwv_regs = {
	.set_ofs = 0x00a8,
	.clr_ofs = 0x00ac,
	.sta_ofs = 0x2c54,
};

static const struct mtk_gate_regs vde13_cg_regs = {
	.set_ofs = 0x200,
	.clr_ofs = 0x204,
	.sta_ofs = 0x200,
};

static const struct mtk_gate_regs vde13_hwv_regs = {
	.set_ofs = 0x0098,
	.clr_ofs = 0x009c,
	.sta_ofs = 0x2c4c,
};

static const struct mtk_gate_regs vde14_hwv_regs = {
	.set_ofs = 0x0090,
	.clr_ofs = 0x0094,
	.sta_ofs = 0x2c48,
};

#define GATE_HWV_VDE10(_id, _name, _parent, _shift) {	\
		.id = _id,				\
		.name = _name,				\
		.parent_name = _parent,			\
		.regs = &vde20_cg_regs,			\
		.hwv_regs = &vde10_hwv_regs,		\
		.shift = _shift,			\
		.ops = &mtk_clk_gate_hwv_ops_setclr_inv,\
		.flags = CLK_OPS_PARENT_ENABLE,		\
	}

#define GATE_HWV_VDE11(_id, _name, _parent, _shift) {		\
		.id = _id,					\
		.name = _name,					\
		.parent_name = _parent,				\
		.regs = &vde11_cg_regs,				\
		.hwv_regs = &vde11_hwv_regs,			\
		.shift = _shift,				\
		.ops = &mtk_clk_gate_hwv_ops_setclr_inv,	\
		.flags = CLK_OPS_PARENT_ENABLE,			\
	}

#define GATE_HWV_VDE12(_id, _name, _parent, _shift) {		\
		.id = _id,					\
		.name = _name,					\
		.parent_name = _parent,				\
		.regs = &vde12_cg_regs,				\
		.hwv_regs = &vde12_hwv_regs,			\
		.shift = _shift,				\
		.ops = &mtk_clk_gate_hwv_ops_setclr_inv,	\
		.flags = CLK_OPS_PARENT_ENABLE			\
	}

#define GATE_HWV_VDE13(_id, _name, _parent, _shift) {	\
		.id = _id,				\
		.name = _name,				\
		.parent_name = _parent,			\
		.regs = &vde13_cg_regs,			\
		.hwv_regs = &vde13_hwv_regs,		\
		.shift = _shift,			\
		.ops = &mtk_clk_gate_hwv_ops_setclr_inv,\
		.flags = CLK_OPS_PARENT_ENABLE,		\
	}

#define GATE_HWV_VDE14(_id, _name, _parent, _shift) {	\
		.id = _id,				\
		.name = _name,				\
		.parent_name = _parent,			\
		.regs = &vde22_cg_regs,			\
		.hwv_regs = &vde14_hwv_regs,		\
		.shift = _shift,			\
		.ops = &mtk_clk_gate_hwv_ops_setclr_inv,\
		.flags = CLK_OPS_PARENT_ENABLE |	\
			 CLK_IGNORE_UNUSED,		\
	}

static const struct mtk_gate vde1_clks[] = {
	/* VDE10 */
	GATE_HWV_VDE10(CLK_VDE1_VDEC_CKEN, "vde1_vdec_cken", "vdec", 0),
	GATE_HWV_VDE10(CLK_VDE1_VDEC_ACTIVE, "vde1_vdec_active", "vdec", 4),
	GATE_HWV_VDE10(CLK_VDE1_VDEC_CKEN_ENG, "vde1_vdec_cken_eng", "vdec", 8),
	/* VDE11 */
	GATE_HWV_VDE11(CLK_VDE1_VDEC_SOC_IPS_EN, "vde1_vdec_soc_ips_en", "vdec", 0),
	/* VDE12 */
	GATE_HWV_VDE12(CLK_VDE1_VDEC_SOC_APTV_EN, "vde1_aptv_en", "ck_tck_26m_mx9_ck", 0),
	GATE_HWV_VDE12(CLK_VDE1_VDEC_SOC_APTV_TOP_EN, "vde1_aptv_topen", "ck_tck_26m_mx9_ck", 1),
	/* VDE13 */
	GATE_HWV_VDE13(CLK_VDE1_LAT_CKEN, "vde1_lat_cken", "vdec", 0),
	GATE_HWV_VDE13(CLK_VDE1_LAT_ACTIVE, "vde1_lat_active", "vdec", 4),
	GATE_HWV_VDE13(CLK_VDE1_LAT_CKEN_ENG, "vde1_lat_cken_eng", "vdec", 8),
	/* VDE14 */
	GATE_HWV_VDE14(CLK_VDE1_LARB1_CKEN, "vde1_larb1_cken", "vdec", 0),
};

static const struct mtk_clk_desc vde1_mcd = {
	.clks = vde1_clks,
	.num_clks = ARRAY_SIZE(vde1_clks),
	.need_runtime_pm = true,
};

static const struct of_device_id of_match_clk_mt8196_vdec[] = {
	{ .compatible = "mediatek,mt8196-vdecsys", .data = &vde2_mcd },
	{ .compatible = "mediatek,mt8196-vdecsys-soc", .data = &vde1_mcd },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, of_match_clk_mt8196_vdec);

static struct platform_driver clk_mt8196_vdec_drv = {
	.probe = mtk_clk_simple_probe,
	.remove = mtk_clk_simple_remove,
	.driver = {
		.name = "clk-mt8196-vdec",
		.of_match_table = of_match_clk_mt8196_vdec,
	},
};
module_platform_driver(clk_mt8196_vdec_drv);

MODULE_DESCRIPTION("MediaTek MT8196 Video Decoders clocks driver");
MODULE_LICENSE("GPL");
