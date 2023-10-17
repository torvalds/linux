// SPDX-License-Identifier: GPL-2.0-or-later
/* Lantiq cpu temperature sensor driver
 *
 * Copyright (C) 2017 Florian Eckert <fe@dev.tdt.de>
 */

#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/init.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include <lantiq_soc.h>

/* gphy1 configuration register contains cpu temperature */
#define CGU_GPHY1_CR   0x0040
#define CGU_TEMP_PD    BIT(19)

static void ltq_cputemp_enable(void)
{
	ltq_cgu_w32(ltq_cgu_r32(CGU_GPHY1_CR) | CGU_TEMP_PD, CGU_GPHY1_CR);
}

static void ltq_cputemp_disable(void *data)
{
	ltq_cgu_w32(ltq_cgu_r32(CGU_GPHY1_CR) & ~CGU_TEMP_PD, CGU_GPHY1_CR);
}

static int ltq_read(struct device *dev, enum hwmon_sensor_types type,
		    u32 attr, int channel, long *temp)
{
	int value;

	switch (attr) {
	case hwmon_temp_input:
		/* get the temperature including one decimal place */
		value = (ltq_cgu_r32(CGU_GPHY1_CR) >> 9) & 0x01FF;
		value = value * 5;
		/* range -38 to +154 °C, register value zero is -38.0 °C */
		value -= 380;
		/* scale temp to millidegree */
		value = value * 100;
		break;
	default:
		return -EOPNOTSUPP;
	}

	*temp = value;
	return 0;
}

static umode_t ltq_is_visible(const void *_data, enum hwmon_sensor_types type,
			      u32 attr, int channel)
{
	if (type != hwmon_temp)
		return 0;

	switch (attr) {
	case hwmon_temp_input:
		return 0444;
	default:
		return 0;
	}
}

static const struct hwmon_channel_info * const ltq_info[] = {
	HWMON_CHANNEL_INFO(chip,
			   HWMON_C_REGISTER_TZ),
	HWMON_CHANNEL_INFO(temp,
			   HWMON_T_INPUT),
	NULL
};

static const struct hwmon_ops ltq_hwmon_ops = {
	.is_visible = ltq_is_visible,
	.read = ltq_read,
};

static const struct hwmon_chip_info ltq_chip_info = {
	.ops = &ltq_hwmon_ops,
	.info = ltq_info,
};

static int ltq_cputemp_probe(struct platform_device *pdev)
{
	struct device *hwmon_dev;
	int err = 0;

	/* available on vr9 v1.2 SoCs only */
	if (ltq_soc_type() != SOC_TYPE_VR9_2)
		return -ENODEV;

	err = devm_add_action(&pdev->dev, ltq_cputemp_disable, NULL);
	if (err)
		return err;

	ltq_cputemp_enable();

	hwmon_dev = devm_hwmon_device_register_with_info(&pdev->dev,
							 "ltq_cputemp",
							 NULL,
							 &ltq_chip_info,
							 NULL);

	if (IS_ERR(hwmon_dev)) {
		dev_err(&pdev->dev, "Failed to register as hwmon device");
		return PTR_ERR(hwmon_dev);
	}

	return 0;
}

const struct of_device_id ltq_cputemp_match[] = {
	{ .compatible = "lantiq,cputemp" },
	{},
};
MODULE_DEVICE_TABLE(of, ltq_cputemp_match);

static struct platform_driver ltq_cputemp_driver = {
	.probe = ltq_cputemp_probe,
	.driver = {
		.name = "ltq-cputemp",
		.of_match_table = ltq_cputemp_match,
	},
};

module_platform_driver(ltq_cputemp_driver);

MODULE_AUTHOR("Florian Eckert <fe@dev.tdt.de>");
MODULE_DESCRIPTION("Lantiq cpu temperature sensor driver");
MODULE_LICENSE("GPL");
