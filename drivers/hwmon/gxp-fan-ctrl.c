// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2022 Hewlett-Packard Enterprise Development Company, L.P. */

#include <linux/bits.h>
#include <linux/err.h>
#include <linux/hwmon.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>

#define OFS_FAN_INST	0 /* Is 0 because plreg base will be set at INST */
#define OFS_FAN_FAIL	2 /* Is 2 bytes after base */
#define OFS_SEVSTAT	0 /* Is 0 because fn2 base will be set at SEVSTAT */
#define POWER_BIT	24

struct gxp_fan_ctrl_drvdata {
	void __iomem	*base;
	void __iomem	*plreg;
	void __iomem	*fn2;
};

static bool fan_installed(struct device *dev, int fan)
{
	struct gxp_fan_ctrl_drvdata *drvdata = dev_get_drvdata(dev);
	u8 val;

	val = readb(drvdata->plreg + OFS_FAN_INST);

	return !!(val & BIT(fan));
}

static long fan_failed(struct device *dev, int fan)
{
	struct gxp_fan_ctrl_drvdata *drvdata = dev_get_drvdata(dev);
	u8 val;

	val = readb(drvdata->plreg + OFS_FAN_FAIL);

	return !!(val & BIT(fan));
}

static long fan_enabled(struct device *dev, int fan)
{
	struct gxp_fan_ctrl_drvdata *drvdata = dev_get_drvdata(dev);
	u32 val;

	/*
	 * Check the power status as if the platform is off the value
	 * reported for the PWM will be incorrect. Report fan as
	 * disabled.
	 */
	val = readl(drvdata->fn2 + OFS_SEVSTAT);

	return !!((val & BIT(POWER_BIT)) && fan_installed(dev, fan));
}

static int gxp_pwm_write(struct device *dev, u32 attr, int channel, long val)
{
	struct gxp_fan_ctrl_drvdata *drvdata = dev_get_drvdata(dev);

	switch (attr) {
	case hwmon_pwm_input:
		if (val > 255 || val < 0)
			return -EINVAL;
		writeb(val, drvdata->base + channel);
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static int gxp_fan_ctrl_write(struct device *dev, enum hwmon_sensor_types type,
			      u32 attr, int channel, long val)
{
	switch (type) {
	case hwmon_pwm:
		return gxp_pwm_write(dev, attr, channel, val);
	default:
		return -EOPNOTSUPP;
	}
}

static int gxp_fan_read(struct device *dev, u32 attr, int channel, long *val)
{
	switch (attr) {
	case hwmon_fan_enable:
		*val = fan_enabled(dev, channel);
		return 0;
	case hwmon_fan_fault:
		*val = fan_failed(dev, channel);
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static int gxp_pwm_read(struct device *dev, u32 attr, int channel, long *val)
{
	struct gxp_fan_ctrl_drvdata *drvdata = dev_get_drvdata(dev);
	u32 reg;

	/*
	 * Check the power status of the platform. If the platform is off
	 * the value reported for the PWM will be incorrect. In this case
	 * report a PWM of zero.
	 */

	reg = readl(drvdata->fn2 + OFS_SEVSTAT);

	if (reg & BIT(POWER_BIT))
		*val = fan_installed(dev, channel) ? readb(drvdata->base + channel) : 0;
	else
		*val = 0;

	return 0;
}

static int gxp_fan_ctrl_read(struct device *dev, enum hwmon_sensor_types type,
			     u32 attr, int channel, long *val)
{
	switch (type) {
	case hwmon_fan:
		return gxp_fan_read(dev, attr, channel, val);
	case hwmon_pwm:
		return gxp_pwm_read(dev, attr, channel, val);
	default:
		return -EOPNOTSUPP;
	}
}

static umode_t gxp_fan_ctrl_is_visible(const void *_data,
				       enum hwmon_sensor_types type,
				       u32 attr, int channel)
{
	umode_t mode = 0;

	switch (type) {
	case hwmon_fan:
		switch (attr) {
		case hwmon_fan_enable:
		case hwmon_fan_fault:
			mode = 0444;
			break;
		default:
			break;
		}
		break;
	case hwmon_pwm:
		switch (attr) {
		case hwmon_pwm_input:
			mode = 0644;
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}

	return mode;
}

static const struct hwmon_ops gxp_fan_ctrl_ops = {
	.is_visible = gxp_fan_ctrl_is_visible,
	.read = gxp_fan_ctrl_read,
	.write = gxp_fan_ctrl_write,
};

static const struct hwmon_channel_info *gxp_fan_ctrl_info[] = {
	HWMON_CHANNEL_INFO(fan,
			   HWMON_F_FAULT | HWMON_F_ENABLE,
			   HWMON_F_FAULT | HWMON_F_ENABLE,
			   HWMON_F_FAULT | HWMON_F_ENABLE,
			   HWMON_F_FAULT | HWMON_F_ENABLE,
			   HWMON_F_FAULT | HWMON_F_ENABLE,
			   HWMON_F_FAULT | HWMON_F_ENABLE,
			   HWMON_F_FAULT | HWMON_F_ENABLE,
			   HWMON_F_FAULT | HWMON_F_ENABLE),
	HWMON_CHANNEL_INFO(pwm,
			   HWMON_PWM_INPUT,
			   HWMON_PWM_INPUT,
			   HWMON_PWM_INPUT,
			   HWMON_PWM_INPUT,
			   HWMON_PWM_INPUT,
			   HWMON_PWM_INPUT,
			   HWMON_PWM_INPUT,
			   HWMON_PWM_INPUT),
	NULL
};

static const struct hwmon_chip_info gxp_fan_ctrl_chip_info = {
	.ops = &gxp_fan_ctrl_ops,
	.info = gxp_fan_ctrl_info,

};

static int gxp_fan_ctrl_probe(struct platform_device *pdev)
{
	struct gxp_fan_ctrl_drvdata *drvdata;
	struct device *dev = &pdev->dev;
	struct device *hwmon_dev;

	drvdata = devm_kzalloc(dev, sizeof(struct gxp_fan_ctrl_drvdata),
			       GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;

	drvdata->base = devm_platform_get_and_ioremap_resource(pdev, 0, NULL);
	if (IS_ERR(drvdata->base))
		return dev_err_probe(dev, PTR_ERR(drvdata->base),
				     "failed to map base\n");

	drvdata->plreg = devm_platform_ioremap_resource_byname(pdev,
							       "pl");
	if (IS_ERR(drvdata->plreg))
		return dev_err_probe(dev, PTR_ERR(drvdata->plreg),
				     "failed to map plreg\n");

	drvdata->fn2 = devm_platform_ioremap_resource_byname(pdev,
							     "fn2");
	if (IS_ERR(drvdata->fn2))
		return dev_err_probe(dev, PTR_ERR(drvdata->fn2),
				     "failed to map fn2\n");

	hwmon_dev = devm_hwmon_device_register_with_info(&pdev->dev,
							 "hpe_gxp_fan_ctrl",
							 drvdata,
							 &gxp_fan_ctrl_chip_info,
							 NULL);

	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static const struct of_device_id gxp_fan_ctrl_of_match[] = {
	{ .compatible = "hpe,gxp-fan-ctrl", },
	{},
};
MODULE_DEVICE_TABLE(of, gxp_fan_ctrl_of_match);

static struct platform_driver gxp_fan_ctrl_driver = {
	.probe		= gxp_fan_ctrl_probe,
	.driver = {
		.name	= "gxp-fan-ctrl",
		.of_match_table = gxp_fan_ctrl_of_match,
	},
};
module_platform_driver(gxp_fan_ctrl_driver);

MODULE_AUTHOR("Nick Hawkins <nick.hawkins@hpe.com>");
MODULE_DESCRIPTION("HPE GXP fan controller");
MODULE_LICENSE("GPL");
