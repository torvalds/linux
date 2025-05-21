// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for Sophgo CV1800 series SoC RTC subsystem
 *
 * The RTC module comprises a 32kHz oscillator, Power-on-Reset (PoR) sub-module,
 * HW state machine to control chip power-on, power-off and reset. Furthermore,
 * the 8051 subsystem is located within RTCSYS including associated SRAM block.
 *
 * Copyright (C) 2025 Alexander Sverdlin <alexander.sverdlin@gmail.com>
 *
 */

#include <linux/mfd/core.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/property.h>

static struct resource cv1800_rtcsys_irq_resources[] = {
	DEFINE_RES_IRQ_NAMED(0, "alarm"),
};

static const struct mfd_cell cv1800_rtcsys_subdev[] = {
	{
		.name = "cv1800b-rtc",
		.num_resources = 1,
		.resources = &cv1800_rtcsys_irq_resources[0],
	},
};

static int cv1800_rtcsys_probe(struct platform_device *pdev)
{
	int irq;

	irq = platform_get_irq_byname(pdev, "alarm");
	if (irq < 0)
		return irq;
	cv1800_rtcsys_irq_resources[0].start = irq;
	cv1800_rtcsys_irq_resources[0].end = irq;

	return devm_mfd_add_devices(&pdev->dev, PLATFORM_DEVID_AUTO,
				    cv1800_rtcsys_subdev,
				    ARRAY_SIZE(cv1800_rtcsys_subdev),
				    NULL, 0, NULL);
}

static const struct of_device_id cv1800_rtcsys_of_match[] = {
	{ .compatible = "sophgo,cv1800b-rtc" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, cv1800_rtcsys_of_match);

static struct platform_driver cv1800_rtcsys_mfd = {
	.probe	= cv1800_rtcsys_probe,
	.driver	= {
		.name		= "cv1800_rtcsys",
		.of_match_table	= cv1800_rtcsys_of_match,
	},
};
module_platform_driver(cv1800_rtcsys_mfd);

MODULE_AUTHOR("Alexander Sverdlin <alexander.sverdlin@gmail.com>");
MODULE_DESCRIPTION("Sophgo CV1800 series SoC RTC subsystem driver");
MODULE_LICENSE("GPL");
