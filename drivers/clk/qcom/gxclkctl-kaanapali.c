// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/clk-provider.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include <dt-bindings/clock/qcom,kaanapali-gxclkctl.h>

#include "common.h"
#include "gdsc.h"

enum {
	DT_BI_TCXO,
};

static struct gdsc gx_clkctl_gx_gdsc = {
	.gdscr = 0x4024,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0xf,
	.pd = {
		.name = "gx_clkctl_gx_gdsc",
		.power_on = gdsc_gx_do_nothing_enable,
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = POLL_CFG_GDSCR | RETAIN_FF_ENABLE,
};

static struct gdsc *gx_clkctl_gdscs[] = {
	[GX_CLKCTL_GX_GDSC] = &gx_clkctl_gx_gdsc,
};

static const struct regmap_config gx_clkctl_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x4038,
	.fast_io = true,
};

static const struct qcom_cc_desc gx_clkctl_kaanapali_desc = {
	.config = &gx_clkctl_regmap_config,
	.gdscs = gx_clkctl_gdscs,
	.num_gdscs = ARRAY_SIZE(gx_clkctl_gdscs),
	.use_rpm = true,
};

static const struct of_device_id gx_clkctl_kaanapali_match_table[] = {
	{ .compatible = "qcom,kaanapali-gxclkctl" },
	{ }
};
MODULE_DEVICE_TABLE(of, gx_clkctl_kaanapali_match_table);

static int gx_clkctl_kaanapali_probe(struct platform_device *pdev)
{
	return qcom_cc_probe(pdev, &gx_clkctl_kaanapali_desc);
}

static struct platform_driver gx_clkctl_kaanapali_driver = {
	.probe = gx_clkctl_kaanapali_probe,
	.driver = {
		.name = "gxclkctl-kaanapali",
		.of_match_table = gx_clkctl_kaanapali_match_table,
	},
};

module_platform_driver(gx_clkctl_kaanapali_driver);

MODULE_DESCRIPTION("QTI GXCLKCTL Kaanapali Driver");
MODULE_LICENSE("GPL");
