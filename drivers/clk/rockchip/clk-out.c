// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2023 Rockchip Electronics Co., Ltd
 */

#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

static DEFINE_SPINLOCK(clk_out_lock);

static int rockchip_clk_out_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = pdev->dev.of_node;
	struct clk_hw *hw;
	struct resource *res;
	const char *clk_name = node->name;
	const char *parent_name;
	void __iomem *reg;
	u32 shift = 0;
	u8 clk_gate_flags = CLK_GATE_HIWORD_MASK;
	unsigned long flags = CLK_SET_RATE_PARENT;
	int ret;

	ret = device_property_read_string(dev, "clock-output-names", &clk_name);
	if (ret)
		return ret;

	ret = device_property_read_u32(dev, "rockchip,bit-shift", &shift);
	if (ret)
		return ret;

	if (device_property_read_bool(dev, "rockchip,bit-set-to-disable"))
		clk_gate_flags |= CLK_GATE_SET_TO_DISABLE;

	if (device_property_read_bool(dev, "rockchip,clk-ignore-unused"))
		flags |= CLK_IGNORE_UNUSED;

	ret = of_clk_parent_fill(node, &parent_name, 1);
	if (ret != 1)
		return -EINVAL;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENOMEM;

	reg = devm_ioremap(dev, res->start, resource_size(res));
	if (!reg)
		return -ENOMEM;

	pm_runtime_enable(dev);


	hw = clk_hw_register_gate(dev, clk_name, parent_name, flags,
				  reg, shift, clk_gate_flags, &clk_out_lock);
	if (IS_ERR(hw)) {
		ret = -EINVAL;
		goto err_disable_pm_runtime;
	}

	of_clk_add_hw_provider(node, of_clk_hw_simple_get, hw);

	return 0;

err_disable_pm_runtime:
	pm_runtime_disable(dev);

	return ret;
}

static int rockchip_clk_out_remove(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;

	of_clk_del_provider(node);
	pm_runtime_disable(&pdev->dev);

	return 0;
}

static const struct of_device_id rockchip_clk_out_match[] = {
	{ .compatible = "rockchip,clk-out", },
	{},
};

static struct platform_driver rockchip_clk_out_driver = {
	.driver = {
		.name = "rockchip-clk-out",
		.of_match_table = rockchip_clk_out_match,
	},
	.probe = rockchip_clk_out_probe,
	.remove = rockchip_clk_out_remove,
};

module_platform_driver(rockchip_clk_out_driver);

MODULE_DESCRIPTION("Rockchip Clock Input-Output-Switch");
MODULE_AUTHOR("Sugar Zhang <sugar.zhang@rock-chips.com>");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(of, rockchip_clk_out_match);
