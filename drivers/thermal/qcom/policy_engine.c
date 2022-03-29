// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2022, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/thermal.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/interrupt.h>
#include "thermal_zone_internal.h"

#define PE_SENS_DRIVER		"policy-engine-sensor"
#define PE_INT_ENABLE_OFFSET	0x530
#define PE_STATUS_OFFSET	0x590
#define PE_INT_STATUS_OFFSET	0x620
#define PE_INT_STATUS1_OFFSET	0x630
#define PE_INTR_CFG		0x11000
#define PE_INTR_CLEAR		0x11111
#define PE_STS_CLEAR		0xFFFF
#define PE_READ_MITIGATION_IDX(val) ((val >> 16) & 0x1F)

struct pe_sensor_data {
	struct device			*dev;
	struct thermal_zone_device	*tz_dev;
	int32_t				high_thresh;
	int32_t				low_thresh;
	int32_t				irq_num;
	void __iomem			*regmap;
	struct mutex			mutex;
};

static int pe_sensor_get_trend(void *data, int trip, enum thermal_trend *trend)
{
	struct pe_sensor_data *pe_sens = (struct pe_sensor_data *)data;
	struct thermal_zone_device *tz = pe_sens->tz_dev;
	int value, last_value;

	if (!tz)
		return -EINVAL;

	value = READ_ONCE(tz->temperature);
	last_value = READ_ONCE(tz->last_temperature);

	if (!value)
		*trend = THERMAL_TREND_DROPPING;
	else if (value > last_value)
		*trend = THERMAL_TREND_RAISING;
	else if (value < last_value)
		*trend = THERMAL_TREND_DROPPING;
	else
		*trend = THERMAL_TREND_STABLE;

	return 0;
}

static int fetch_mitigation_table_idx(struct pe_sensor_data *pe_sens, int *temp)
{
	u32 data = 0;

	data = readl_relaxed(pe_sens->regmap + PE_STATUS_OFFSET);
	*temp = PE_READ_MITIGATION_IDX(data);
	dev_dbg(pe_sens->dev, "PE data:%d index:%d\n", data, *temp);

	return 0;
}

static int pe_sensor_read(void *data, int *temp)
{
	struct pe_sensor_data *pe_sens = (struct pe_sensor_data *)data;

	return fetch_mitigation_table_idx(pe_sens, temp);
}

static int pe_sensor_set_trips(void *data, int low, int high)
{
	struct pe_sensor_data *pe_sens = (struct pe_sensor_data *)data;

	mutex_lock(&pe_sens->mutex);
	if (pe_sens->high_thresh == high &&
		pe_sens->low_thresh == low)
		goto unlock_exit;
	pe_sens->high_thresh = high;
	pe_sens->low_thresh = low;
	dev_dbg(pe_sens->dev, "PE rail set trip. high:%d low:%d\n",
				high, low);

unlock_exit:
	mutex_unlock(&pe_sens->mutex);
	return 0;
}

static struct thermal_zone_of_device_ops pe_sensor_ops = {
	.get_temp = pe_sensor_read,
	.set_trips = pe_sensor_set_trips,
	.get_trend = pe_sensor_get_trend,
};

static irqreturn_t pe_handle_irq(int irq, void *data)
{
	struct pe_sensor_data *pe_sens = (struct pe_sensor_data *)data;
	int val = 0, ret = 0;

	writel_relaxed(PE_INTR_CLEAR, pe_sens->regmap + PE_INT_STATUS_OFFSET);
	writel_relaxed(PE_STS_CLEAR, pe_sens->regmap + PE_INT_STATUS1_OFFSET);

	ret = fetch_mitigation_table_idx(pe_sens, &val);
	if (ret)
		return IRQ_HANDLED;

	mutex_lock(&pe_sens->mutex);
	dev_dbg(pe_sens->dev, "Policy Engine interrupt fired value:%d\n", val);
	if (pe_sens->tz_dev && (val >= pe_sens->high_thresh ||
			val <= pe_sens->low_thresh)) {
		mutex_unlock(&pe_sens->mutex);
		thermal_zone_device_update(pe_sens->tz_dev,
				THERMAL_TRIP_VIOLATED);
	} else
		mutex_unlock(&pe_sens->mutex);

	return IRQ_HANDLED;
}

static int pe_sens_device_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	int ret = 0;
	struct pe_sensor_data *pe_sens;
	struct resource *res;

	pe_sens = devm_kzalloc(dev, sizeof(*pe_sens), GFP_KERNEL);
	if (!pe_sens)
		return -ENOMEM;
	pe_sens->dev = dev;
	pe_sens->high_thresh = INT_MAX;
	pe_sens->low_thresh = INT_MIN;
	mutex_init(&pe_sens->mutex);

	dev_set_drvdata(dev, pe_sens);
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(dev, "Couldn't get MEM resource\n");
		return -EINVAL;
	}
	dev_dbg(dev, "pe@0x%x size:%d\n", res->start,
			resource_size(res));

	pe_sens->regmap = devm_ioremap_resource(dev, res);
	if (!pe_sens->regmap) {
		dev_err(dev, "Couldn't get regmap\n");
		return -EINVAL;
	}

	pe_sens->irq_num = platform_get_irq(pdev, 0);
	if (pe_sens->irq_num < 0) {
		dev_err(dev, "Couldn't get irq number\n");
		return pe_sens->irq_num;
	}
	pe_sens->tz_dev = devm_thermal_zone_of_sensor_register(
				dev, 0, pe_sens, &pe_sensor_ops);
	if (IS_ERR_OR_NULL(pe_sens->tz_dev)) {
		ret = PTR_ERR(pe_sens->tz_dev);
		if (ret != -ENODEV)
			dev_err(dev, "sensor register failed. ret:%d\n", ret);
		pe_sens->tz_dev = NULL;
		return ret;
	}
	qti_update_tz_ops(pe_sens->tz_dev, true);

	writel_relaxed(PE_INTR_CFG, pe_sens->regmap + PE_INT_ENABLE_OFFSET);
	writel_relaxed(PE_INTR_CLEAR, pe_sens->regmap + PE_INT_STATUS_OFFSET);
	writel_relaxed(PE_STS_CLEAR, pe_sens->regmap + PE_INT_STATUS1_OFFSET);
	ret = devm_request_threaded_irq(dev, pe_sens->irq_num, NULL,
				pe_handle_irq,
				IRQF_TRIGGER_HIGH | IRQF_ONESHOT,
				dev_name(dev), pe_sens);
	if (ret) {
		dev_err(dev, "Couldn't get irq registered\n");
		thermal_zone_of_sensor_unregister(pe_sens->dev,
				pe_sens->tz_dev);
		return ret;
	}
	enable_irq_wake(pe_sens->irq_num);

	dev_dbg(dev, "PE sensor register success\n");

	return 0;
}

static int pe_sens_device_remove(struct platform_device *pdev)
{
	struct pe_sensor_data *pe_sens =
		(struct pe_sensor_data *)dev_get_drvdata(&pdev->dev);

	thermal_zone_of_sensor_unregister(pe_sens->dev, pe_sens->tz_dev);
	qti_update_tz_ops(pe_sens->tz_dev, false);

	return 0;
}

static const struct of_device_id pe_sens_device_match[] = {
	{.compatible = "qcom,policy-engine"},
	{}
};

static struct platform_driver pe_sens_device_driver = {
	.probe          = pe_sens_device_probe,
	.remove         = pe_sens_device_remove,
	.driver         = {
		.name   = PE_SENS_DRIVER,
		.of_match_table = pe_sens_device_match,
	},
};

module_platform_driver(pe_sens_device_driver);
MODULE_LICENSE("GPL");
