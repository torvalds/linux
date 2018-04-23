// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018, Fuzhou Rockchip Electronics Co., Ltd.
 * Author: Tony Xie <tony.xie@rock-chips.com>
 */

#include <linux/arm-smccc.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/rockchip/rockchip_sip.h>
#include <linux/slab.h>
#include <linux/string.h>

static int rockchip_sip_soc_bus_div(u32 bus_id, u32 timer, u32 enable_msk)
{
	struct arm_smccc_res res;

	res = sip_smc_soc_bus_div(bus_id, timer, enable_msk);

	return res.a0;
}

static const struct of_device_id rockchip_busfreq_of_match[] = {
	{ .compatible = "rockchip,px30-bus", },
	{ },
};

MODULE_DEVICE_TABLE(of, rockchip_busfreq_of_match);

static int rockchip_busfreq_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct device_node *child;
	int ret, enable_msk, bus_id, sip_timer;

	for_each_available_child_of_node(np, child) {
		ret = of_property_read_u32_index(child, "bus-id", 0,
						 &bus_id);
		if (ret)
			continue;

		ret = of_property_read_u32_index(child, "timer-us", 0,
						 &sip_timer);
		if (ret) {
			dev_info(dev, "get timer_us error\n");
			continue;
		}

		if (!sip_timer) {
			dev_info(dev, "timer_us invalid\n");
			continue;
		}

		ret = of_property_read_u32_index(child, "enable-msk", 0,
						 &enable_msk);
		if (ret) {
			dev_info(dev, "get enable_msk error\n");
			continue;
		}

		ret = rockchip_sip_soc_bus_div(bus_id, sip_timer,
					       enable_msk);
		if (ret == -2) {
			dev_info(dev, "smc sip not support! %x\n", ret);
			break;
		}
	}

	return 0;
}

static struct platform_driver rockchip_busfreq_driver = {
	.probe	= rockchip_busfreq_probe,
	.driver = {
		.name	= "rockchip,soc_bus",
		.of_match_table = rockchip_busfreq_of_match,
	},
};

module_platform_driver(rockchip_busfreq_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Tony Xie <tony.xie@rock-chips.com>");
MODULE_DESCRIPTION("rockchip soc bus driver with devfreq framework");
