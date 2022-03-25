// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2021 ASPEED Technology Inc.
 *
 * CHASSIS driver for the Aspeed SoC
 */
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/sysfs.h>
#include <linux/interrupt.h>

/* #define USE_INTERRUPTS */
/******************************************************************************/
union chassis_ctrl_register {
	u32 value;
	struct {
		uint32_t intrusion_status_clear : 1; /*[0]*/
		uint32_t intrusion_int_enable : 1; /*[1]*/
		uint32_t intrusion_status : 1; /*[2]*/
		uint32_t battery_power_good : 1; /*[3]*/
		uint32_t chassis_raw_status : 1; /*[4]*/
		uint32_t reserved0 : 3; /*[5-7]*/
		uint32_t io_power_status_clear : 1; /*[8]*/
		uint32_t io_power_int_enable : 1; /*[9]*/
		uint32_t core_power_status : 1; /*[10]*/
		uint32_t reserved1 : 5; /*[11-15]*/
		uint32_t core_power_status_clear : 1; /*[16]*/
		uint32_t core_power_int_enable : 1; /*[17]*/
		uint32_t io_power_status : 1; /*[18]*/
		uint32_t reserved2 : 13; /*[19-31]*/
	} fields;
};

struct aspeed_chassis {
	struct device *dev;
	void __iomem *base;
	int irq;
	/* for hwmon */
	const struct attribute_group *groups[2];
};

static ssize_t
intrusion_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long val;
	struct aspeed_chassis *chassis = dev_get_drvdata(dev);
	union chassis_ctrl_register chassis_ctrl;

	if (kstrtoul(buf, 10, &val) < 0 || val != 0)
		return -EINVAL;

	chassis_ctrl.value = readl(chassis->base);
	chassis_ctrl.fields.intrusion_status_clear = 1;
	writel(chassis_ctrl.value, chassis->base);
	chassis_ctrl.fields.intrusion_status_clear = 0;
	writel(chassis_ctrl.value, chassis->base);
	return count;
}

static ssize_t intrusion_show(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int index = sensor_attr->index;
	struct aspeed_chassis *chassis = dev_get_drvdata(dev);
	union chassis_ctrl_register chassis_ctrl;
	u8 ret;

	chassis_ctrl.value = readl(chassis->base);

	switch (index) {
	case 0:
		ret = chassis_ctrl.fields.core_power_status;
		break;
	case 1:
		ret = chassis_ctrl.fields.io_power_status;
		break;
	case 2:
		ret = chassis_ctrl.fields.intrusion_status;
		break;
	}

	return sprintf(buf, "%d\n", ret);
}

static SENSOR_DEVICE_ATTR_RO(core_power, intrusion, 0);
static SENSOR_DEVICE_ATTR_RO(io_power, intrusion, 1);
static SENSOR_DEVICE_ATTR_RW(intrusion0_alarm, intrusion, 2);

static struct attribute *intrusion_dev_attrs[] = {
	&sensor_dev_attr_core_power.dev_attr.attr,
	&sensor_dev_attr_io_power.dev_attr.attr,
	&sensor_dev_attr_intrusion0_alarm.dev_attr.attr, NULL
};

static const struct attribute_group intrusion_dev_group = {
	.attrs = intrusion_dev_attrs,
	.is_visible = NULL,
};

#ifdef USE_INTERRUPTS
static void aspeed_chassis_status_check(struct aspeed_chassis *chassis)
{
	union chassis_ctrl_register chassis_ctrl;

	chassis_ctrl.value = readl(chassis->base);
	if (chassis_ctrl.fields.intrusion_status) {
		dev_info(chassis->dev, "CHASI# pin has been pulled low");
		chassis_ctrl.fields.intrusion_status_clear = 1;
		writel(chassis_ctrl.value, chassis->base);
		chassis_ctrl.fields.intrusion_status_clear = 0;
		writel(chassis_ctrl.value, chassis->base);
	}

	if (chassis_ctrl.fields.core_power_status) {
		dev_info(chassis->dev, "Core power has been pulled low");
		chassis_ctrl.fields.core_power_status_clear = 1;
		writel(chassis_ctrl.value, chassis->base);
		chassis_ctrl.fields.core_power_status_clear = 0;
		writel(chassis_ctrl.value, chassis->base);
	}

	if (chassis_ctrl.fields.io_power_status) {
		dev_info(chassis->dev, "IO power has been pulled low");
		chassis_ctrl.fields.io_power_status_clear = 1;
		writel(chassis_ctrl.value, chassis->base);
		chassis_ctrl.fields.io_power_status_clear = 0;
		writel(chassis_ctrl.value, chassis->base);
	}
}

static irqreturn_t aspeed_chassis_isr(int this_irq, void *dev_id)
{
	struct aspeed_chassis *chassis = dev_id;

	aspeed_chassis_status_check(chassis);
	return IRQ_HANDLED;
}
#endif

static void aspeed_chassis_int_ctrl(struct aspeed_chassis *chassis, bool ctrl)
{
	union chassis_ctrl_register chassis_ctrl;

	chassis_ctrl.value = readl(chassis->base);
	chassis_ctrl.fields.intrusion_int_enable = ctrl;
	chassis_ctrl.fields.io_power_int_enable = ctrl;
	chassis_ctrl.fields.core_power_int_enable = ctrl;
	writel(chassis_ctrl.value, chassis->base);
}

static const struct of_device_id aspeed_chassis_of_table[] = {
	{ .compatible = "aspeed,ast2600-chassis" },
	{}
};
MODULE_DEVICE_TABLE(of, aspeed_chassis_of_table);

static int aspeed_chassis_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct aspeed_chassis *priv;
	struct device *hwmon;
	int __maybe_unused ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = dev;
	priv->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);
#ifdef USE_INTERRUPTS
	priv->irq = platform_get_irq(pdev, 0);
	if (priv->irq < 0) {
		dev_err(dev, "no irq specified\n");
		return -ENOENT;
	}

	ret = devm_request_irq(dev, priv->irq, aspeed_chassis_isr, 0,
			       dev_name(dev), priv);
	if (ret) {
		dev_err(dev, "Chassis Unable to get IRQ");
		return ret;
	}
	aspeed_chassis_int_ctrl(priv, true);
#else
	aspeed_chassis_int_ctrl(priv, false);
#endif

	priv->groups[0] = &intrusion_dev_group;
	priv->groups[1] = NULL;

	hwmon = devm_hwmon_device_register_with_groups(dev, "aspeed_chassis",
						       priv, priv->groups);

	return PTR_ERR_OR_ZERO(hwmon);
}

static struct platform_driver aspeed_chassis_driver = {
	.probe		= aspeed_chassis_probe,
	.driver		= {
		.name	= KBUILD_MODNAME,
		.of_match_table = aspeed_chassis_of_table,
	},
};

module_platform_driver(aspeed_chassis_driver);

MODULE_AUTHOR("Billy Tsai<billy_tsai@aspeedtech.com>");
MODULE_DESCRIPTION("ASPEED CHASSIS Driver");
MODULE_LICENSE("GPL");
