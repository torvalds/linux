/*
 * Copyright (c) 2015 MediaTek Inc.
 * Author: Hongzhou.Yang <hongzhou.yang@mediatek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/mfd/mt6397/core.h>

#include "pinctrl-mtk-common.h"
#include "pinctrl-mtk-mt6397.h"

#define MT6397_PIN_REG_BASE  0xc000

static const struct mtk_pinctrl_devdata mt6397_pinctrl_data = {
	.pins = mtk_pins_mt6397,
	.npins = ARRAY_SIZE(mtk_pins_mt6397),
	.dir_offset = (MT6397_PIN_REG_BASE + 0x000),
	.ies_offset = MTK_PINCTRL_NOT_SUPPORT,
	.smt_offset = MTK_PINCTRL_NOT_SUPPORT,
	.pullen_offset = (MT6397_PIN_REG_BASE + 0x020),
	.pullsel_offset = (MT6397_PIN_REG_BASE + 0x040),
	.dout_offset = (MT6397_PIN_REG_BASE + 0x080),
	.din_offset = (MT6397_PIN_REG_BASE + 0x0a0),
	.pinmux_offset = (MT6397_PIN_REG_BASE + 0x0c0),
	.type1_start = 41,
	.type1_end = 41,
	.port_shf = 3,
	.port_mask = 0x3,
	.port_align = 2,
};

static int mt6397_pinctrl_probe(struct platform_device *pdev)
{
	struct mt6397_chip *mt6397;

	mt6397 = dev_get_drvdata(pdev->dev.parent);
	return mtk_pctrl_init(pdev, &mt6397_pinctrl_data, mt6397->regmap);
}

static const struct of_device_id mt6397_pctrl_match[] = {
	{ .compatible = "mediatek,mt6397-pinctrl", },
	{ }
};
MODULE_DEVICE_TABLE(of, mt6397_pctrl_match);

static struct platform_driver mtk_pinctrl_driver = {
	.probe = mt6397_pinctrl_probe,
	.driver = {
		.name = "mediatek-mt6397-pinctrl",
		.of_match_table = mt6397_pctrl_match,
	},
};

static int __init mtk_pinctrl_init(void)
{
	return platform_driver_register(&mtk_pinctrl_driver);
}

module_init(mtk_pinctrl_init);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("MediaTek MT6397 Pinctrl Driver");
MODULE_AUTHOR("Hongzhou Yang <hongzhou.yang@mediatek.com>");
