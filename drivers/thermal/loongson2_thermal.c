// SPDX-License-Identifier: GPL-2.0+
/*
 * Author: zhanghongchen <zhanghongchen@loongson.cn>
 *         Yinbo Zhu <zhuyinbo@loongson.cn>
 * Copyright (C) 2022-2023 Loongson Technology Corporation Limited
 */

#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/minmax.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/thermal.h>
#include <linux/units.h>

#include "thermal_hwmon.h"

#define LOONGSON2_MAX_SENSOR_SEL_NUM	3

#define LOONGSON2_THSENS_CTRL_HI_REG	0x0
#define LOONGSON2_THSENS_CTRL_LOW_REG	0x8
#define LOONGSON2_THSENS_STATUS_REG	0x10
#define LOONGSON2_THSENS_OUT_REG	0x14

#define LOONGSON2_THSENS_INT_LO		BIT(0)
#define LOONGSON2_THSENS_INT_HIGH	BIT(1)
#define LOONGSON2_THSENS_INT_EN		(LOONGSON2_THSENS_INT_LO | \
					 LOONGSON2_THSENS_INT_HIGH)
#define LOONGSON2_THSENS_OUT_MASK	0xFF

/*
 * This flag is used to indicate the temperature reading
 * method of the Loongson-2K2000
 */
#define LS2K2000_THSENS_OUT_FLAG	BIT(0)

struct loongson2_thermal_chip_data {
	unsigned int thermal_sensor_sel;
	unsigned int flags;
};

struct loongson2_thermal_data {
	void __iomem *ctrl_reg;
	void __iomem *temp_reg;
	const struct loongson2_thermal_chip_data *chip_data;
};

static void loongson2_set_ctrl_regs(struct loongson2_thermal_data *data,
				    int ctrl_data, bool low, bool enable)
{
	int reg_ctrl = 0;
	int reg_off  = data->chip_data->thermal_sensor_sel * 2;
	int ctrl_reg = low ? LOONGSON2_THSENS_CTRL_LOW_REG : LOONGSON2_THSENS_CTRL_HI_REG;

	reg_ctrl = ctrl_data + HECTO;
	reg_ctrl |= enable ? 0x100 : 0;
	writew(reg_ctrl, data->ctrl_reg + ctrl_reg + reg_off);
}

static int loongson2_thermal_set(struct loongson2_thermal_data *data,
				 int low, int high, bool enable)
{
	/* Set low temperature threshold */
	loongson2_set_ctrl_regs(data, clamp(-40, low, high), true, enable);

	/* Set high temperature threshold */
	loongson2_set_ctrl_regs(data, clamp(125, low, high), false, enable);

	return 0;
}

static int loongson2_2k1000_get_temp(struct thermal_zone_device *tz, int *temp)
{
	int val;
	struct loongson2_thermal_data *data = thermal_zone_device_priv(tz);

	val = readl(data->ctrl_reg + LOONGSON2_THSENS_OUT_REG);
	*temp = ((val & LOONGSON2_THSENS_OUT_MASK) - HECTO) * KILO;

	return 0;
}

static int loongson2_2k2000_get_temp(struct thermal_zone_device *tz, int *temp)
{
	int val;
	struct loongson2_thermal_data *data = thermal_zone_device_priv(tz);

	val = readl(data->temp_reg);
	*temp = ((val & 0xffff) * 820 / 0x4000 - 311) * KILO;

	return 0;
}

static irqreturn_t loongson2_thermal_irq_thread(int irq, void *dev)
{
	struct thermal_zone_device *tzd = dev;
	struct loongson2_thermal_data *data = thermal_zone_device_priv(tzd);

	writeb(LOONGSON2_THSENS_INT_EN, data->ctrl_reg + LOONGSON2_THSENS_STATUS_REG);

	thermal_zone_device_update(tzd, THERMAL_EVENT_UNSPECIFIED);

	return IRQ_HANDLED;
}

static int loongson2_thermal_set_trips(struct thermal_zone_device *tz, int low, int high)
{
	struct loongson2_thermal_data *data = thermal_zone_device_priv(tz);

	return loongson2_thermal_set(data, low/MILLI, high/MILLI, true);
}

static const struct thermal_zone_device_ops loongson2_2k1000_of_thermal_ops = {
	.get_temp = loongson2_2k1000_get_temp,
	.set_trips = loongson2_thermal_set_trips,
};

static const struct thermal_zone_device_ops loongson2_2k2000_of_thermal_ops = {
	.get_temp = loongson2_2k2000_get_temp,
	.set_trips = loongson2_thermal_set_trips,
};

static int loongson2_thermal_probe(struct platform_device *pdev)
{
	const struct thermal_zone_device_ops *thermal_ops;
	struct device *dev = &pdev->dev;
	struct loongson2_thermal_data *data;
	struct thermal_zone_device *tzd;
	int ret, irq, i;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->chip_data = device_get_match_data(dev);

	data->ctrl_reg = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(data->ctrl_reg))
		return PTR_ERR(data->ctrl_reg);

	/* The temperature output register is separate for Loongson-2K2000 */
	if (data->chip_data->flags & LS2K2000_THSENS_OUT_FLAG) {
		data->temp_reg = devm_platform_ioremap_resource(pdev, 1);
		if (IS_ERR(data->temp_reg))
			return PTR_ERR(data->temp_reg);

		thermal_ops = &loongson2_2k2000_of_thermal_ops;
	} else {
		thermal_ops = &loongson2_2k1000_of_thermal_ops;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	writeb(LOONGSON2_THSENS_INT_EN, data->ctrl_reg + LOONGSON2_THSENS_STATUS_REG);

	loongson2_thermal_set(data, 0, 0, false);

	for (i = 0; i <= LOONGSON2_MAX_SENSOR_SEL_NUM; i++) {
		tzd = devm_thermal_of_zone_register(dev, i, data, thermal_ops);

		if (!IS_ERR(tzd))
			break;

		if (PTR_ERR(tzd) != -ENODEV)
			continue;

		return dev_err_probe(dev, PTR_ERR(tzd), "failed to register");
	}

	ret = devm_request_threaded_irq(dev, irq, NULL, loongson2_thermal_irq_thread,
					IRQF_ONESHOT, "loongson2_thermal", tzd);
	if (ret < 0)
		return dev_err_probe(dev, ret, "failed to request alarm irq\n");

	devm_thermal_add_hwmon_sysfs(dev, tzd);

	return 0;
}

static const struct loongson2_thermal_chip_data loongson2_thermal_ls2k1000_data = {
	.thermal_sensor_sel = 0,
	.flags = 0,
};

static const struct loongson2_thermal_chip_data loongson2_thermal_ls2k2000_data = {
	.thermal_sensor_sel = 0,
	.flags = LS2K2000_THSENS_OUT_FLAG,
};

static const struct of_device_id of_loongson2_thermal_match[] = {
	{
		.compatible = "loongson,ls2k1000-thermal",
		.data = &loongson2_thermal_ls2k1000_data,
	},
	{
		.compatible = "loongson,ls2k2000-thermal",
		.data = &loongson2_thermal_ls2k2000_data,
	},
	{ /* end */ }
};
MODULE_DEVICE_TABLE(of, of_loongson2_thermal_match);

static struct platform_driver loongson2_thermal_driver = {
	.driver = {
		.name		= "loongson2_thermal",
		.of_match_table = of_loongson2_thermal_match,
	},
	.probe	= loongson2_thermal_probe,
};
module_platform_driver(loongson2_thermal_driver);

MODULE_DESCRIPTION("Loongson2 thermal driver");
MODULE_AUTHOR("Loongson Technology Corporation Limited");
MODULE_LICENSE("GPL");
