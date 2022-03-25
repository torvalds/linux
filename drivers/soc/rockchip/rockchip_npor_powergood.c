// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2022 Rockchip Electronics Co., Ltd.
 */

#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mfd/syscon.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

struct rknpor_powergood_info {
	int		irq;
	irq_handler_t	isr;
	struct regmap	*grf;
	u32	grf_offset;
	u8	status_bits_shift;
};

static irqreturn_t rv1106_npor_powergood_isr(int irq, void *data)
{
	struct rknpor_powergood_info *powergood = data;
	u32 voltage = 0;

	while (!voltage) {
		regmap_read(powergood->grf, powergood->grf_offset, &voltage);
		voltage = (voltage >> powergood->status_bits_shift) & 0x1;
	}

	pr_err("%s voltage jitter detected\n", __func__);

	return IRQ_HANDLED;
}

static struct rknpor_powergood_info rv1106_soc_data = {
	.grf_offset = 0x20020,
	.status_bits_shift = 4,
	.isr = rv1106_npor_powergood_isr,
};

static const struct of_device_id rockchip_npor_powergood_dt_match[] = {
	{
		.compatible = "rockchip,rv1106-npor-powergood",
		.data = &rv1106_soc_data
	},
	{},
};

static int rockchip_npor_powergood_probe(struct platform_device *pdev)
{
	struct rknpor_powergood_info *powergood;
	const struct of_device_id *match;
	struct device *dev = &pdev->dev;
	int ret;

	match = of_match_node(rockchip_npor_powergood_dt_match, pdev->dev.of_node);
	powergood = (struct rknpor_powergood_info *)match->data;

	if (!powergood)
		return -EINVAL;

	if (dev->parent && dev->parent->of_node) {
		powergood->grf = syscon_node_to_regmap(dev->parent->of_node);
		if (IS_ERR(powergood->grf))
			return dev_err_probe(&pdev->dev, PTR_ERR(powergood->grf), "fail to find grf\n");
	}

	powergood->irq = platform_get_irq(pdev, 0);
	if (powergood->irq < 0)
		return powergood->irq;

	ret = devm_request_irq(&pdev->dev, powergood->irq, powergood->isr, 0, "rknpor_powergood", powergood);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "request irq fail\n");

	dev_info(&pdev->dev, "probe success!\n");

	return 0;
}

static struct platform_driver rockchip_npor_powergood_driver = {
	.probe = rockchip_npor_powergood_probe,
	.driver		= {
		.name	= "rockchip,rknpor-powergood",
		.of_match_table = rockchip_npor_powergood_dt_match,
	},
};

static int __init rockchip_npor_powergood_init(void)
{
	return platform_driver_register(&rockchip_npor_powergood_driver);
}
subsys_initcall_sync(rockchip_npor_powergood_init);
MODULE_DESCRIPTION("Rockchip NPOR Powergood");
MODULE_LICENSE("GPL");
