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

static const struct mtk_gate_regs jpgdec_cg_regs = {
	.set_ofs = 0x4,
	.clr_ofs = 0x8,
	.sta_ofs = 0x0,
};

#define GATE_JPGDEC(_id, _name, _parent, _shift)			\
	GATE_MTK(_id, _name, _parent, &jpgdec_cg_regs, _shift, &mtk_clk_gate_ops_setclr_inv)

static const struct mtk_gate jpgdec_clks[] = {
	GATE_JPGDEC(CLK_JPGDEC_JPGDEC1, "jpgdec_jpgdec1", "jpgdec_sel", 0),
	GATE_JPGDEC(CLK_JPGDEC_JPGDEC, "jpgdec_jpgdec", "jpgdec_sel", 4),
};

static const struct mtk_clk_desc jpgdec_desc = {
	.clks = jpgdec_clks,
	.num_clks = ARRAY_SIZE(jpgdec_clks),
};

static const struct of_device_id of_match_clk_mt2712_jpgdec[] = {
	{
		.compatible = "mediatek,mt2712-jpgdecsys",
		.data = &jpgdec_desc,
	}, {
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(of, of_match_clk_mt2712_jpgdec);

static struct platform_driver clk_mt2712_jpgdec_drv = {
	.probe = mtk_clk_simple_probe,
	.remove = mtk_clk_simple_remove,
	.driver = {
		.name = "clk-mt2712-jpgdec",
		.of_match_table = of_match_clk_mt2712_jpgdec,
	},
};
module_platform_driver(clk_mt2712_jpgdec_drv);

MODULE_DESCRIPTION("MediaTek MT2712 JPEG Decoder clocks driver");
MODULE_LICENSE("GPL");
