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

static const struct mtk_gate_regs img_cg_regs = {
	.set_ofs = 0x4,
	.clr_ofs = 0x8,
	.sta_ofs = 0x0,
};

#define GATE_IMG(_id, _name, _parent, _shift)			\
	GATE_MTK(_id, _name, _parent, &img_cg_regs, _shift, &mtk_clk_gate_ops_setclr)

static const struct mtk_gate img_clks[] = {
	GATE_IMG(CLK_IMG_LARB1_SMI, "img_larb1_smi", "smi_mm", 0),
	GATE_IMG(CLK_IMG_CAM_SMI, "img_cam_smi", "smi_mm", 5),
	GATE_IMG(CLK_IMG_CAM_CAM, "img_cam_cam", "smi_mm", 6),
	GATE_IMG(CLK_IMG_SEN_TG, "img_sen_tg", "cam_mm", 7),
	GATE_IMG(CLK_IMG_SEN_CAM, "img_sen_cam", "smi_mm", 8),
	GATE_IMG(CLK_IMG_VENC, "img_venc", "smi_mm", 9),
};

static const struct mtk_clk_desc img_desc = {
	.clks = img_clks,
	.num_clks = ARRAY_SIZE(img_clks),
};

static const struct of_device_id of_match_clk_mt8167_imgsys[] = {
	{ .compatible = "mediatek,mt8167-imgsys", .data = &img_desc },
	{ /* sentinel */ }
};

static struct platform_driver clk_mt8167_imgsys_drv = {
	.probe = mtk_clk_simple_probe,
	.remove = mtk_clk_simple_remove,
	.driver = {
		.name = "clk-mt8167-imgsys",
		.of_match_table = of_match_clk_mt8167_imgsys,
	},
};
module_platform_driver(clk_mt8167_imgsys_drv);
MODULE_LICENSE("GPL");
