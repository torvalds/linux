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

static const struct mtk_gate_regs mm_v_cg_regs = {
	.set_ofs = 0x104,
	.clr_ofs = 0x108,
	.sta_ofs = 0x100,
};

static const struct mtk_gate_regs mm_v_hwv_regs = {
	.set_ofs = 0x0030,
	.clr_ofs = 0x0034,
	.sta_ofs = 0x2c18,
};

#define GATE_MM_AO_V(_id, _name, _parent, _shift) {	\
		.id = _id,				\
		.name = _name,				\
		.parent_name = _parent,			\
		.regs = &mm_v_cg_regs,			\
		.shift = _shift,			\
		.ops = &mtk_clk_gate_ops_setclr,	\
		.flags = CLK_OPS_PARENT_ENABLE |	\
			 CLK_IS_CRITICAL,		\
	}

#define GATE_HWV_MM_V(_id, _name, _parent, _shift) {	\
		.id = _id,				\
		.name = _name,				\
		.parent_name = _parent,			\
		.regs = &mm_v_cg_regs,			\
		.hwv_regs = &mm_v_hwv_regs,		\
		.shift = _shift,			\
		.ops = &mtk_clk_gate_hwv_ops_setclr,	\
		.flags = CLK_OPS_PARENT_ENABLE,		\
	}

static const struct mtk_gate mm_v_clks[] = {
	GATE_HWV_MM_V(CLK_MM_V_DISP_VDISP_AO_CONFIG, "mm_v_disp_vdisp_ao_config", "disp", 0),
	GATE_HWV_MM_V(CLK_MM_V_DISP_DPC, "mm_v_disp_dpc", "disp", 16),
	GATE_MM_AO_V(CLK_MM_V_SMI_SUB_SOMM0, "mm_v_smi_sub_somm0", "disp", 2),
};

static const struct mtk_clk_desc mm_v_mcd = {
	.clks = mm_v_clks,
	.num_clks = ARRAY_SIZE(mm_v_clks),
};

static const struct of_device_id of_match_clk_mt8196_vdisp_ao[] = {
	{ .compatible = "mediatek,mt8196-vdisp-ao", .data = &mm_v_mcd },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, of_match_clk_mt8196_vdisp_ao);

static struct platform_driver clk_mt8196_vdisp_ao_drv = {
	.probe = mtk_clk_pdev_probe,
	.remove = mtk_clk_pdev_remove,
	.driver = {
		.name = "clk-mt8196-vdisp-ao",
		.of_match_table = of_match_clk_mt8196_vdisp_ao,
	},
};
module_platform_driver(clk_mt8196_vdisp_ao_drv);

MODULE_DESCRIPTION("MediaTek MT8196 vdisp_ao clocks driver");
MODULE_LICENSE("GPL");
