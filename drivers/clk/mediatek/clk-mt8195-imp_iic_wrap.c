// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright (c) 2021 MediaTek Inc.
// Author: Chun-Jie Chen <chun-jie.chen@mediatek.com>

#include "clk-gate.h"
#include "clk-mtk.h"

#include <dt-bindings/clock/mt8195-clk.h>
#include <linux/clk-provider.h>
#include <linux/platform_device.h>

static const struct mtk_gate_regs imp_iic_wrap_cg_regs = {
	.set_ofs = 0xe08,
	.clr_ofs = 0xe04,
	.sta_ofs = 0xe00,
};

#define GATE_IMP_IIC_WRAP(_id, _name, _parent, _shift)				\
	GATE_MTK_FLAGS(_id, _name, _parent, &imp_iic_wrap_cg_regs, _shift,	\
		&mtk_clk_gate_ops_setclr, CLK_OPS_PARENT_ENABLE)

static const struct mtk_gate imp_iic_wrap_s_clks[] = {
	GATE_IMP_IIC_WRAP(CLK_IMP_IIC_WRAP_S_I2C5, "imp_iic_wrap_s_i2c5", "top_i2c", 0),
	GATE_IMP_IIC_WRAP(CLK_IMP_IIC_WRAP_S_I2C6, "imp_iic_wrap_s_i2c6", "top_i2c", 1),
	GATE_IMP_IIC_WRAP(CLK_IMP_IIC_WRAP_S_I2C7, "imp_iic_wrap_s_i2c7", "top_i2c", 2),
};

static const struct mtk_gate imp_iic_wrap_w_clks[] = {
	GATE_IMP_IIC_WRAP(CLK_IMP_IIC_WRAP_W_I2C0, "imp_iic_wrap_w_i2c0", "top_i2c", 0),
	GATE_IMP_IIC_WRAP(CLK_IMP_IIC_WRAP_W_I2C1, "imp_iic_wrap_w_i2c1", "top_i2c", 1),
	GATE_IMP_IIC_WRAP(CLK_IMP_IIC_WRAP_W_I2C2, "imp_iic_wrap_w_i2c2", "top_i2c", 2),
	GATE_IMP_IIC_WRAP(CLK_IMP_IIC_WRAP_W_I2C3, "imp_iic_wrap_w_i2c3", "top_i2c", 3),
	GATE_IMP_IIC_WRAP(CLK_IMP_IIC_WRAP_W_I2C4, "imp_iic_wrap_w_i2c4", "top_i2c", 4),
};

static const struct mtk_clk_desc imp_iic_wrap_s_desc = {
	.clks = imp_iic_wrap_s_clks,
	.num_clks = ARRAY_SIZE(imp_iic_wrap_s_clks),
};

static const struct mtk_clk_desc imp_iic_wrap_w_desc = {
	.clks = imp_iic_wrap_w_clks,
	.num_clks = ARRAY_SIZE(imp_iic_wrap_w_clks),
};

static const struct of_device_id of_match_clk_mt8195_imp_iic_wrap[] = {
	{
		.compatible = "mediatek,mt8195-imp_iic_wrap_s",
		.data = &imp_iic_wrap_s_desc,
	}, {
		.compatible = "mediatek,mt8195-imp_iic_wrap_w",
		.data = &imp_iic_wrap_w_desc,
	}, {
		/* sentinel */
	}
};

static struct platform_driver clk_mt8195_imp_iic_wrap_drv = {
	.probe = mtk_clk_simple_probe,
	.remove = mtk_clk_simple_remove,
	.driver = {
		.name = "clk-mt8195-imp_iic_wrap",
		.of_match_table = of_match_clk_mt8195_imp_iic_wrap,
	},
};
module_platform_driver(clk_mt8195_imp_iic_wrap_drv);
MODULE_LICENSE("GPL");
