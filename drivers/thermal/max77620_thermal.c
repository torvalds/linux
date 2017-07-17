/*
 * Junction temperature thermal driver for Maxim Max77620.
 *
 * Copyright (c) 2016, NVIDIA CORPORATION.  All rights reserved.
 *
 * Author: Laxman Dewangan <ldewangan@nvidia.com>
 *	   Mallikarjun Kasoju <mkasoju@nvidia.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 */

#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/mfd/max77620.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/thermal.h>

#define MAX77620_NORMAL_OPERATING_TEMP	100000
#define MAX77620_TJALARM1_TEMP		120000
#define MAX77620_TJALARM2_TEMP		140000

struct max77620_therm_info {
	struct device			*dev;
	struct regmap			*rmap;
	struct thermal_zone_device	*tz_device;
	int				irq_tjalarm1;
	int				irq_tjalarm2;
};

/**
 * max77620_thermal_read_temp: Read PMIC die temperatue.
 * @data:	Device specific data.
 * temp:	Temperature in millidegrees Celsius
 *
 * The actual temperature of PMIC die is not available from PMIC.
 * PMIC only tells the status if it has crossed or not the threshold level
 * of 120degC or 140degC.
 * If threshold has not been crossed then assume die temperature as 100degC
 * else 120degC or 140deG based on the PMIC die temp threshold status.
 *
 * Return 0 on success otherwise error number to show reason of failure.
 */

static int max77620_thermal_read_temp(void *data, int *temp)
{
	struct max77620_therm_info *mtherm = data;
	unsigned int val;
	int ret;

	ret = regmap_read(mtherm->rmap, MAX77620_REG_STATLBT, &val);
	if (ret < 0) {
		dev_err(mtherm->dev, "Failed to read STATLBT: %d\n", ret);
		return ret;
	}

	if (val & MAX77620_IRQ_TJALRM2_MASK)
		*temp = MAX77620_TJALARM2_TEMP;
	else if (val & MAX77620_IRQ_TJALRM1_MASK)
		*temp = MAX77620_TJALARM1_TEMP;
	else
		*temp = MAX77620_NORMAL_OPERATING_TEMP;

	return 0;
}

static const struct thermal_zone_of_device_ops max77620_thermal_ops = {
	.get_temp = max77620_thermal_read_temp,
};

static irqreturn_t max77620_thermal_irq(int irq, void *data)
{
	struct max77620_therm_info *mtherm = data;

	if (irq == mtherm->irq_tjalarm1)
		dev_warn(mtherm->dev, "Junction Temp Alarm1(120C) occurred\n");
	else if (irq == mtherm->irq_tjalarm2)
		dev_crit(mtherm->dev, "Junction Temp Alarm2(140C) occurred\n");

	thermal_zone_device_update(mtherm->tz_device,
				   THERMAL_EVENT_UNSPECIFIED);

	return IRQ_HANDLED;
}

static int max77620_thermal_probe(struct platform_device *pdev)
{
	struct max77620_therm_info *mtherm;
	int ret;

	mtherm = devm_kzalloc(&pdev->dev, sizeof(*mtherm), GFP_KERNEL);
	if (!mtherm)
		return -ENOMEM;

	mtherm->irq_tjalarm1 = platform_get_irq(pdev, 0);
	mtherm->irq_tjalarm2 = platform_get_irq(pdev, 1);
	if ((mtherm->irq_tjalarm1 < 0) || (mtherm->irq_tjalarm2 < 0)) {
		dev_err(&pdev->dev, "Alarm irq number not available\n");
		return -EINVAL;
	}

	mtherm->dev = &pdev->dev;
	mtherm->rmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!mtherm->rmap) {
		dev_err(&pdev->dev, "Failed to get parent regmap\n");
		return -ENODEV;
	}

	/*
	 * The reference taken to the parent's node which will be balanced on
	 * reprobe or on platform-device release.
	 */
	device_set_of_node_from_dev(&pdev->dev, pdev->dev.parent);

	mtherm->tz_device = devm_thermal_zone_of_sensor_register(&pdev->dev, 0,
				mtherm, &max77620_thermal_ops);
	if (IS_ERR(mtherm->tz_device)) {
		ret = PTR_ERR(mtherm->tz_device);
		dev_err(&pdev->dev, "Failed to register thermal zone: %d\n",
			ret);
		return ret;
	}

	ret = devm_request_threaded_irq(&pdev->dev, mtherm->irq_tjalarm1, NULL,
					max77620_thermal_irq,
					IRQF_ONESHOT | IRQF_SHARED,
					dev_name(&pdev->dev), mtherm);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to request irq1: %d\n", ret);
		return ret;
	}

	ret = devm_request_threaded_irq(&pdev->dev, mtherm->irq_tjalarm2, NULL,
					max77620_thermal_irq,
					IRQF_ONESHOT | IRQF_SHARED,
					dev_name(&pdev->dev), mtherm);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to request irq2: %d\n", ret);
		return ret;
	}

	platform_set_drvdata(pdev, mtherm);

	return 0;
}

static struct platform_device_id max77620_thermal_devtype[] = {
	{ .name = "max77620-thermal", },
	{},
};
MODULE_DEVICE_TABLE(platform, max77620_thermal_devtype);

static struct platform_driver max77620_thermal_driver = {
	.driver = {
		.name = "max77620-thermal",
	},
	.probe = max77620_thermal_probe,
	.id_table = max77620_thermal_devtype,
};

module_platform_driver(max77620_thermal_driver);

MODULE_DESCRIPTION("Max77620 Junction temperature Thermal driver");
MODULE_AUTHOR("Laxman Dewangan <ldewangan@nvidia.com>");
MODULE_AUTHOR("Mallikarjun Kasoju <mkasoju@nvidia.com>");
MODULE_LICENSE("GPL v2");
