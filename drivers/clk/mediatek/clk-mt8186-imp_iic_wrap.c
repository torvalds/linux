// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright (c) 2022 MediaTek Inc.
// Author: Chun-Jie Chen <chun-jie.chen@mediatek.com>

#include <linux/clk-provider.h>
#include <linux/platform_device.h>
#include <dt-bindings/clock/mt8186-clk.h>

#include "clk-gate.h"
#include "clk-mtk.h"

static const struct mtk_gate_regs imp_iic_wrap_cg_regs = {
	.set_ofs = 0xe08,
	.clr_ofs = 0xe04,
	.sta_ofs = 0xe00,
};

#define GATE_IMP_IIC_WRAP(_id, _name, _parent, _shift)			\
	GATE_MTK(_id, _name, _parent, &imp_iic_wrap_cg_regs, _shift, &mtk_clk_gate_ops_setclr)

static const struct mtk_gate imp_iic_wrap_clks[] = {
	GATE_IMP_IIC_WRAP(CLK_IMP_IIC_WRAP_AP_CLOCK_I2C0,
		"imp_iic_wrap_ap_clock_i2c0", "infra_ao_i2c_ap", 0),
	GATE_IMP_IIC_WRAP(CLK_IMP_IIC_WRAP_AP_CLOCK_I2C1,
		"imp_iic_wrap_ap_clock_i2c1", "infra_ao_i2c_ap", 1),
	GATE_IMP_IIC_WRAP(CLK_IMP_IIC_WRAP_AP_CLOCK_I2C2,
		"imp_iic_wrap_ap_clock_i2c2", "infra_ao_i2c_ap", 2),
	GATE_IMP_IIC_WRAP(CLK_IMP_IIC_WRAP_AP_CLOCK_I2C3,
		"imp_iic_wrap_ap_clock_i2c3", "infra_ao_i2c_ap", 3),
	GATE_IMP_IIC_WRAP(CLK_IMP_IIC_WRAP_AP_CLOCK_I2C4,
		"imp_iic_wrap_ap_clock_i2c4", "infra_ao_i2c_ap", 4),
	GATE_IMP_IIC_WRAP(CLK_IMP_IIC_WRAP_AP_CLOCK_I2C5,
		"imp_iic_wrap_ap_clock_i2c5", "infra_ao_i2c_ap", 5),
	GATE_IMP_IIC_WRAP(CLK_IMP_IIC_WRAP_AP_CLOCK_I2C6,
		"imp_iic_wrap_ap_clock_i2c6", "infra_ao_i2c_ap", 6),
	GATE_IMP_IIC_WRAP(CLK_IMP_IIC_WRAP_AP_CLOCK_I2C7,
		"imp_iic_wrap_ap_clock_i2c7", "infra_ao_i2c_ap", 7),
	GATE_IMP_IIC_WRAP(CLK_IMP_IIC_WRAP_AP_CLOCK_I2C8,
		"imp_iic_wrap_ap_clock_i2c8", "infra_ao_i2c_ap", 8),
	GATE_IMP_IIC_WRAP(CLK_IMP_IIC_WRAP_AP_CLOCK_I2C9,
		"imp_iic_wrap_ap_clock_i2c9", "infra_ao_i2c_ap", 9),
};

static const struct mtk_clk_desc imp_iic_wrap_desc = {
	.clks = imp_iic_wrap_clks,
	.num_clks = ARRAY_SIZE(imp_iic_wrap_clks),
};

static const struct of_device_id of_match_clk_mt8186_imp_iic_wrap[] = {
	{
		.compatible = "mediatek,mt8186-imp_iic_wrap",
		.data = &imp_iic_wrap_desc,
	}, {
		/* sentinel */
	}
};

static struct platform_driver clk_mt8186_imp_iic_wrap_drv = {
	.probe = mtk_clk_simple_probe,
	.remove = mtk_clk_simple_remove,
	.driver = {
		.name = "clk-mt8186-imp_iic_wrap",
		.of_match_table = of_match_clk_mt8186_imp_iic_wrap,
	},
};
module_platform_driver(clk_mt8186_imp_iic_wrap_drv);
