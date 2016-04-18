/*
 * Clock driver for Palmas device.
 *
 * Copyright (c) 2013, NVIDIA Corporation.
 * Copyright (c) 2013-2014 Texas Instruments, Inc.
 *
 * Author:	Laxman Dewangan <ldewangan@nvidia.com>
 *		Peter Ujfalusi <peter.ujfalusi@ti.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any kind,
 * whether express or implied; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/mfd/palmas.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#define PALMAS_CLOCK_DT_EXT_CONTROL_ENABLE1	1
#define PALMAS_CLOCK_DT_EXT_CONTROL_ENABLE2	2
#define PALMAS_CLOCK_DT_EXT_CONTROL_NSLEEP	3

struct palmas_clk32k_desc {
	const char *clk_name;
	unsigned int control_reg;
	unsigned int enable_mask;
	unsigned int sleep_mask;
	unsigned int sleep_reqstr_id;
	int delay;
};

struct palmas_clock_info {
	struct device *dev;
	struct clk *clk;
	struct clk_hw hw;
	struct palmas *palmas;
	const struct palmas_clk32k_desc *clk_desc;
	int ext_control_pin;
};

static inline struct palmas_clock_info *to_palmas_clks_info(struct clk_hw *hw)
{
	return container_of(hw, struct palmas_clock_info, hw);
}

static unsigned long palmas_clks_recalc_rate(struct clk_hw *hw,
					     unsigned long parent_rate)
{
	return 32768;
}

static int palmas_clks_prepare(struct clk_hw *hw)
{
	struct palmas_clock_info *cinfo = to_palmas_clks_info(hw);
	int ret;

	ret = palmas_update_bits(cinfo->palmas, PALMAS_RESOURCE_BASE,
				 cinfo->clk_desc->control_reg,
				 cinfo->clk_desc->enable_mask,
				 cinfo->clk_desc->enable_mask);
	if (ret < 0)
		dev_err(cinfo->dev, "Reg 0x%02x update failed, %d\n",
			cinfo->clk_desc->control_reg, ret);
	else if (cinfo->clk_desc->delay)
		udelay(cinfo->clk_desc->delay);

	return ret;
}

static void palmas_clks_unprepare(struct clk_hw *hw)
{
	struct palmas_clock_info *cinfo = to_palmas_clks_info(hw);
	int ret;

	/*
	 * Clock can be disabled through external pin if it is externally
	 * controlled.
	 */
	if (cinfo->ext_control_pin)
		return;

	ret = palmas_update_bits(cinfo->palmas, PALMAS_RESOURCE_BASE,
				 cinfo->clk_desc->control_reg,
				 cinfo->clk_desc->enable_mask, 0);
	if (ret < 0)
		dev_err(cinfo->dev, "Reg 0x%02x update failed, %d\n",
			cinfo->clk_desc->control_reg, ret);
}

static int palmas_clks_is_prepared(struct clk_hw *hw)
{
	struct palmas_clock_info *cinfo = to_palmas_clks_info(hw);
	int ret;
	u32 val;

	if (cinfo->ext_control_pin)
		return 1;

	ret = palmas_read(cinfo->palmas, PALMAS_RESOURCE_BASE,
			  cinfo->clk_desc->control_reg, &val);
	if (ret < 0) {
		dev_err(cinfo->dev, "Reg 0x%02x read failed, %d\n",
			cinfo->clk_desc->control_reg, ret);
		return ret;
	}
	return !!(val & cinfo->clk_desc->enable_mask);
}

static struct clk_ops palmas_clks_ops = {
	.prepare	= palmas_clks_prepare,
	.unprepare	= palmas_clks_unprepare,
	.is_prepared	= palmas_clks_is_prepared,
	.recalc_rate	= palmas_clks_recalc_rate,
};

struct palmas_clks_of_match_data {
	struct clk_init_data init;
	const struct palmas_clk32k_desc desc;
};

static const struct palmas_clks_of_match_data palmas_of_clk32kg = {
	.init = {
		.name = "clk32kg",
		.ops = &palmas_clks_ops,
		.flags = CLK_IS_ROOT | CLK_IGNORE_UNUSED,
	},
	.desc = {
		.clk_name = "clk32kg",
		.control_reg = PALMAS_CLK32KG_CTRL,
		.enable_mask = PALMAS_CLK32KG_CTRL_MODE_ACTIVE,
		.sleep_mask = PALMAS_CLK32KG_CTRL_MODE_SLEEP,
		.sleep_reqstr_id = PALMAS_EXTERNAL_REQSTR_ID_CLK32KG,
		.delay = 200,
	},
};

static const struct palmas_clks_of_match_data palmas_of_clk32kgaudio = {
	.init = {
		.name = "clk32kgaudio",
		.ops = &palmas_clks_ops,
		.flags = CLK_IS_ROOT | CLK_IGNORE_UNUSED,
	},
	.desc = {
		.clk_name = "clk32kgaudio",
		.control_reg = PALMAS_CLK32KGAUDIO_CTRL,
		.enable_mask = PALMAS_CLK32KG_CTRL_MODE_ACTIVE,
		.sleep_mask = PALMAS_CLK32KG_CTRL_MODE_SLEEP,
		.sleep_reqstr_id = PALMAS_EXTERNAL_REQSTR_ID_CLK32KGAUDIO,
		.delay = 200,
	},
};

static const struct of_device_id palmas_clks_of_match[] = {
	{
		.compatible = "ti,palmas-clk32kg",
		.data = &palmas_of_clk32kg,
	},
	{
		.compatible = "ti,palmas-clk32kgaudio",
		.data = &palmas_of_clk32kgaudio,
	},
	{ },
};
MODULE_DEVICE_TABLE(of, palmas_clks_of_match);

static void palmas_clks_get_clk_data(struct platform_device *pdev,
				     struct palmas_clock_info *cinfo)
{
	struct device_node *node = pdev->dev.of_node;
	unsigned int prop;
	int ret;

	ret = of_property_read_u32(node, "ti,external-sleep-control",
				   &prop);
	if (ret)
		return;

	switch (prop) {
	case PALMAS_CLOCK_DT_EXT_CONTROL_ENABLE1:
		prop = PALMAS_EXT_CONTROL_ENABLE1;
		break;
	case PALMAS_CLOCK_DT_EXT_CONTROL_ENABLE2:
		prop = PALMAS_EXT_CONTROL_ENABLE2;
		break;
	case PALMAS_CLOCK_DT_EXT_CONTROL_NSLEEP:
		prop = PALMAS_EXT_CONTROL_NSLEEP;
		break;
	default:
		dev_warn(&pdev->dev, "%s: Invalid ext control option: %u\n",
			 node->name, prop);
		prop = 0;
		break;
	}
	cinfo->ext_control_pin = prop;
}

static int palmas_clks_init_configure(struct palmas_clock_info *cinfo)
{
	int ret;

	ret = palmas_update_bits(cinfo->palmas, PALMAS_RESOURCE_BASE,
				 cinfo->clk_desc->control_reg,
				 cinfo->clk_desc->sleep_mask, 0);
	if (ret < 0) {
		dev_err(cinfo->dev, "Reg 0x%02x update failed, %d\n",
			cinfo->clk_desc->control_reg, ret);
		return ret;
	}

	if (cinfo->ext_control_pin) {
		ret = clk_prepare(cinfo->clk);
		if (ret < 0) {
			dev_err(cinfo->dev, "Clock prep failed, %d\n", ret);
			return ret;
		}

		ret = palmas_ext_control_req_config(cinfo->palmas,
					cinfo->clk_desc->sleep_reqstr_id,
					cinfo->ext_control_pin, true);
		if (ret < 0) {
			dev_err(cinfo->dev, "Ext config for %s failed, %d\n",
				cinfo->clk_desc->clk_name, ret);
			return ret;
		}
	}

	return ret;
}
static int palmas_clks_probe(struct platform_device *pdev)
{
	struct palmas *palmas = dev_get_drvdata(pdev->dev.parent);
	struct device_node *node = pdev->dev.of_node;
	const struct palmas_clks_of_match_data *match_data;
	struct palmas_clock_info *cinfo;
	struct clk *clk;
	int ret;

	match_data = of_device_get_match_data(&pdev->dev);
	if (!match_data)
		return 1;

	cinfo = devm_kzalloc(&pdev->dev, sizeof(*cinfo), GFP_KERNEL);
	if (!cinfo)
		return -ENOMEM;

	palmas_clks_get_clk_data(pdev, cinfo);
	platform_set_drvdata(pdev, cinfo);

	cinfo->dev = &pdev->dev;
	cinfo->palmas = palmas;

	cinfo->clk_desc = &match_data->desc;
	cinfo->hw.init = &match_data->init;
	clk = devm_clk_register(&pdev->dev, &cinfo->hw);
	if (IS_ERR(clk)) {
		ret = PTR_ERR(clk);
		dev_err(&pdev->dev, "Fail to register clock %s, %d\n",
			match_data->desc.clk_name, ret);
		return ret;
	}

	cinfo->clk = clk;
	ret = palmas_clks_init_configure(cinfo);
	if (ret < 0) {
		dev_err(&pdev->dev, "Clock config failed, %d\n", ret);
		return ret;
	}

	ret = of_clk_add_provider(node, of_clk_src_simple_get, cinfo->clk);
	if (ret < 0)
		dev_err(&pdev->dev, "Fail to add clock driver, %d\n", ret);
	return ret;
}

static int palmas_clks_remove(struct platform_device *pdev)
{
	of_clk_del_provider(pdev->dev.of_node);
	return 0;
}

static struct platform_driver palmas_clks_driver = {
	.driver = {
		.name = "palmas-clk",
		.of_match_table = palmas_clks_of_match,
	},
	.probe = palmas_clks_probe,
	.remove = palmas_clks_remove,
};

module_platform_driver(palmas_clks_driver);

MODULE_DESCRIPTION("Clock driver for Palmas Series Devices");
MODULE_ALIAS("platform:palmas-clk");
MODULE_AUTHOR("Peter Ujfalusi <peter.ujfalusi@ti.com>");
MODULE_LICENSE("GPL v2");
