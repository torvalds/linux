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
#include <linux/of_device.h>
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

struct nocp_info {
	u32 counter0_src;
	u32 counter0_val;
	u32 counter1_src;
	u32 counter1_val;
};

struct rockchip_nocp {
	void __iomem *reg_base;
	struct device *dev;
	struct devfreq_event_dev *edev;
	struct devfreq_event_desc *desc;
	const struct nocp_info *info;
	ktime_t time;
};

static int rockchip_nocp_enable(struct devfreq_event_dev *edev)
{
	struct rockchip_nocp *nocp = devfreq_event_get_drvdata(edev);
	const struct nocp_info *info = nocp->info;
	void __iomem *reg_base = nocp->reg_base;

	writel_relaxed(GLOBAL_EN, reg_base + PROBE_CFGCTL);
	writel_relaxed(START_EN, reg_base + PROBE_MAINCTL);
	writel_relaxed(0, reg_base + PROBE_STATPERIOD);
	writel_relaxed(EVENT_BYTE, reg_base + info->counter0_src);
	writel_relaxed(EVENT_CHAIN, reg_base + info->counter1_src);
	writel_relaxed(START_GO, reg_base + PROBE_STATGO);

	nocp->time = ktime_get();

	return 0;
}

static int rockchip_nocp_disable(struct devfreq_event_dev *edev)
{
	struct rockchip_nocp *nocp = devfreq_event_get_drvdata(edev);
	const struct nocp_info *info = nocp->info;
	void __iomem *reg_base = nocp->reg_base;

	writel_relaxed(0, reg_base + PROBE_STATGO);
	writel_relaxed(0, reg_base + PROBE_MAINCTL);
	writel_relaxed(0, reg_base + PROBE_CFGCTL);
	writel_relaxed(0, reg_base + info->counter0_src);
	writel_relaxed(0, reg_base + info->counter1_src);

	return 0;
}

static int rockchip_nocp_get_event(struct devfreq_event_dev *edev,
				   struct devfreq_event_data *edata)
{
	struct rockchip_nocp *nocp = devfreq_event_get_drvdata(edev);
	const struct nocp_info *info = nocp->info;
	void __iomem *reg_base = nocp->reg_base;
	u32 counter = 0, counter0 = 0, counter1 = 0;
	int time_ms = 0;

	time_ms = ktime_to_ms(ktime_sub(ktime_get(), nocp->time));

	counter0 = readl_relaxed(reg_base + info->counter0_val);
	counter1 = readl_relaxed(reg_base + info->counter1_val);
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

static const struct nocp_info rk3288_nocp = {
	.counter0_src = 0x138,
	.counter0_val = 0x13c,
	.counter1_src = 0x14c,
	.counter1_val = 0x150,
};

static const struct nocp_info rk3568_nocp = {
	.counter0_src = 0x204,
	.counter0_val = 0x20c,
	.counter1_src = 0x214,
	.counter1_val = 0x21c,
};

static const struct of_device_id rockchip_nocp_id_match[] = {
	{
		.compatible = "rockchip,rk3288-nocp",
		.data = (void *)&rk3288_nocp,
	},
	{
		.compatible = "rockchip,rk3368-nocp",
		.data = (void *)&rk3288_nocp,
	},
	{
		.compatible = "rockchip,rk3399-nocp",
		.data = (void *)&rk3288_nocp,
	},
	{
		.compatible = "rockchip,rk3568-nocp",
		.data = (void *)&rk3568_nocp,
	},
	{ },
};

static int rockchip_nocp_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct rockchip_nocp *nocp;
	struct devfreq_event_desc *desc;
	struct device_node *np = pdev->dev.of_node;
	const struct of_device_id *match;

	match = of_match_device(rockchip_nocp_id_match, &pdev->dev);
	if (!match || !match->data) {
		dev_err(&pdev->dev, "missing nocp data\n");
		return -ENODEV;
	}

	nocp = devm_kzalloc(&pdev->dev, sizeof(*nocp), GFP_KERNEL);
	if (!nocp)
		return -ENOMEM;

	nocp->info = match->data;

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
