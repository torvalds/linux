/*
 * Copyright (c) 2016, Fuzhou Rockchip Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include <linux/devfreq-event.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#define EVENT_BYTE		0x08
#define EVENT_CHAIN		0x10

#define START_EN		BIT(3)
#define GLOBAL_EN		BIT(0)
#define START_GO		BIT(0)

#define PROBE_MAINCTL		0x0008
#define PROBE_CFGCTL		0x000c
#define PROBE_STATPERIOD	0x0024
#define PROBE_STATGO		0x0028
#define PROBE_COUNTERS_0_SRC	0x0138
#define PROBE_COUNTERS_0_VAL	0x013c
#define PROBE_COUNTERS_1_SRC	0x014c
#define PROBE_COUNTERS_1_VAL	0x0150

struct rockchip_nocp {
	void __iomem *reg_base;
	struct device *dev;
	struct devfreq_event_dev *edev;
	struct devfreq_event_desc *desc;
	ktime_t time;
};

static int rockchip_nocp_enable(struct devfreq_event_dev *edev)
{
	struct rockchip_nocp *nocp = devfreq_event_get_drvdata(edev);
	void __iomem *reg_base = nocp->reg_base;

	writel_relaxed(GLOBAL_EN, reg_base + PROBE_CFGCTL);
	writel_relaxed(START_EN, reg_base + PROBE_MAINCTL);
	writel_relaxed(0, reg_base + PROBE_STATPERIOD);
	writel_relaxed(EVENT_BYTE, reg_base + PROBE_COUNTERS_0_SRC);
	writel_relaxed(EVENT_CHAIN, reg_base + PROBE_COUNTERS_1_SRC);
	writel_relaxed(START_GO, reg_base + PROBE_STATGO);

	nocp->time = ktime_get();

	return 0;
}

static int rockchip_nocp_disable(struct devfreq_event_dev *edev)
{
	struct rockchip_nocp *nocp = devfreq_event_get_drvdata(edev);
	void __iomem *reg_base = nocp->reg_base;

	writel_relaxed(0, reg_base + PROBE_STATGO);
	writel_relaxed(0, reg_base + PROBE_MAINCTL);
	writel_relaxed(0, reg_base + PROBE_CFGCTL);
	writel_relaxed(0, reg_base + PROBE_COUNTERS_0_SRC);
	writel_relaxed(0, reg_base + PROBE_COUNTERS_1_SRC);

	return 0;
}

static int rockchip_nocp_get_event(struct devfreq_event_dev *edev,
				   struct devfreq_event_data *edata)
{
	struct rockchip_nocp *nocp = devfreq_event_get_drvdata(edev);
	void __iomem *reg_base = nocp->reg_base;
	u32 counter = 0, counter0 = 0, counter1 = 0;
	int time_ms = 0;

	time_ms = ktime_to_ms(ktime_sub(ktime_get(), nocp->time));

	counter0 = readl_relaxed(reg_base + PROBE_COUNTERS_0_VAL);
	counter1 = readl_relaxed(reg_base + PROBE_COUNTERS_1_VAL);
	counter = (counter0 & 0xffff) | ((counter1 & 0xffff) << 16);
	counter = counter / 1000000;
	if (time_ms > 0)
		edata->load_count = (counter * 1000) / time_ms;

	writel_relaxed(START_GO, reg_base + PROBE_STATGO);
	nocp->time = ktime_get();

	return 0;
}

static int rockchip_nocp_set_event(struct devfreq_event_dev *edev)
{
	return 0;
}

static const struct devfreq_event_ops rockchip_nocp_ops = {
	.disable = rockchip_nocp_disable,
	.enable = rockchip_nocp_enable,
	.get_event = rockchip_nocp_get_event,
	.set_event = rockchip_nocp_set_event,
};

static const struct of_device_id rockchip_nocp_id_match[] = {
	{ .compatible = "rockchip,rk3288-nocp" },
	{ .compatible = "rockchip,rk3368-nocp" },
	{ .compatible = "rockchip,rk3399-nocp" },
	{ },
};

static int rockchip_nocp_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct rockchip_nocp *nocp;
	struct devfreq_event_desc *desc;
	struct device_node *np = pdev->dev.of_node;

	nocp = devm_kzalloc(&pdev->dev, sizeof(*nocp), GFP_KERNEL);
	if (!nocp)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	nocp->reg_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(nocp->reg_base))
		return PTR_ERR(nocp->reg_base);

	desc = devm_kzalloc(&pdev->dev, sizeof(*desc), GFP_KERNEL);
	if (!desc)
		return -ENOMEM;

	desc->ops = &rockchip_nocp_ops;
	desc->driver_data = nocp;
	desc->name = np->name;
	nocp->desc = desc;
	nocp->dev = &pdev->dev;
	nocp->edev = devm_devfreq_event_add_edev(&pdev->dev, desc);
	if (IS_ERR(nocp->edev)) {
		dev_err(&pdev->dev, "failed to add devfreq-event device\n");
		return PTR_ERR(nocp->edev);
	}

	platform_set_drvdata(pdev, nocp);

	return 0;
}

static struct platform_driver rockchip_nocp_driver = {
	.probe = rockchip_nocp_probe,
	.driver = {
		.name = "rockchip-nocp",
		.of_match_table = rockchip_nocp_id_match,
	},
};
module_platform_driver(rockchip_nocp_driver);

MODULE_DESCRIPTION("Rockchip NoC (Network on Chip) Probe driver");
MODULE_AUTHOR("Finley Xiao <finley.xiao@rock-chips.com>");
MODULE_LICENSE("GPL v2");
