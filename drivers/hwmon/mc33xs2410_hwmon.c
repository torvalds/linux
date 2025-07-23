// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2025 Liebherr-Electronics and Drives GmbH
 */

#include <linux/auxiliary_bus.h>
#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/hwmon.h>
#include <linux/mc33xs2410.h>
#include <linux/module.h>

/* ctrl registers */

#define MC33XS2410_TEMP_WT			0x29
#define MC33XS2410_TEMP_WT_MASK			GENMASK(7, 0)

/* diag registers */

/* chan in { 1 ... 4 } */
#define MC33XS2410_OUT_STA(chan)		(0x02 + (chan) - 1)
#define MC33XS2410_OUT_STA_OTW			BIT(8)

#define MC33XS2410_TS_TEMP_DIE			0x26
#define MC33XS2410_TS_TEMP_MASK			GENMASK(9, 0)

/* chan in { 1 ... 4 } */
#define MC33XS2410_TS_TEMP(chan)		(0x2f + (chan) - 1)

static const struct hwmon_channel_info * const mc33xs2410_hwmon_info[] = {
	HWMON_CHANNEL_INFO(temp,
			   HWMON_T_LABEL | HWMON_T_INPUT,
			   HWMON_T_LABEL | HWMON_T_INPUT | HWMON_T_MAX |
			   HWMON_T_ALARM,
			   HWMON_T_LABEL | HWMON_T_INPUT | HWMON_T_MAX |
			   HWMON_T_ALARM,
			   HWMON_T_LABEL | HWMON_T_INPUT | HWMON_T_MAX |
			   HWMON_T_ALARM,
			   HWMON_T_LABEL | HWMON_T_INPUT | HWMON_T_MAX |
			   HWMON_T_ALARM),
	NULL,
};

static umode_t mc33xs2410_hwmon_is_visible(const void *data,
					   enum hwmon_sensor_types type,
					   u32 attr, int channel)
{
	switch (attr) {
	case hwmon_temp_input:
	case hwmon_temp_alarm:
	case hwmon_temp_label:
		return 0444;
	case hwmon_temp_max:
		return 0644;
	default:
		return 0;
	}
}

static int mc33xs2410_hwmon_read(struct device *dev,
				 enum hwmon_sensor_types type,
				 u32 attr, int channel, long *val)
{
	struct spi_device *spi = dev_get_drvdata(dev);
	u16 reg_val;
	int ret;
	u8 reg;

	switch (attr) {
	case hwmon_temp_input:
		reg = (channel == 0) ? MC33XS2410_TS_TEMP_DIE :
				       MC33XS2410_TS_TEMP(channel);
		ret = mc33xs2410_read_reg_diag(spi, reg, &reg_val);
		if (ret < 0)
			return ret;

		/* LSB is 0.25 degree celsius */
		*val = FIELD_GET(MC33XS2410_TS_TEMP_MASK, reg_val) * 250 - 40000;
		return 0;
	case hwmon_temp_alarm:
		ret = mc33xs2410_read_reg_diag(spi, MC33XS2410_OUT_STA(channel),
					       &reg_val);
		if (ret < 0)
			return ret;

		*val = FIELD_GET(MC33XS2410_OUT_STA_OTW, reg_val);
		return 0;
	case hwmon_temp_max:
		ret = mc33xs2410_read_reg_ctrl(spi, MC33XS2410_TEMP_WT, &reg_val);
		if (ret < 0)
			return ret;

		/* LSB is 1 degree celsius */
		*val = FIELD_GET(MC33XS2410_TEMP_WT_MASK, reg_val) * 1000 - 40000;
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static int mc33xs2410_hwmon_write(struct device *dev,
				  enum hwmon_sensor_types type, u32 attr,
				  int channel, long val)
{
	struct spi_device *spi = dev_get_drvdata(dev);

	switch (attr) {
	case hwmon_temp_max:
		val = clamp_val(val, -40000, 215000);

		/* LSB is 1 degree celsius */
		val = (val / 1000) + 40;
		return mc33xs2410_modify_reg(spi, MC33XS2410_TEMP_WT,
					     MC33XS2410_TEMP_WT_MASK, val);
	default:
		return -EOPNOTSUPP;
	}
}

static const char *const mc33xs2410_temp_label[] = {
	"Central die temperature",
	"Channel 1 temperature",
	"Channel 2 temperature",
	"Channel 3 temperature",
	"Channel 4 temperature",
};

static int mc33xs2410_read_string(struct device *dev,
				  enum hwmon_sensor_types type,
				  u32 attr, int channel, const char **str)
{
	*str = mc33xs2410_temp_label[channel];

	return 0;
}

static const struct hwmon_ops mc33xs2410_hwmon_hwmon_ops = {
	.is_visible = mc33xs2410_hwmon_is_visible,
	.read = mc33xs2410_hwmon_read,
	.read_string = mc33xs2410_read_string,
	.write = mc33xs2410_hwmon_write,
};

static const struct hwmon_chip_info mc33xs2410_hwmon_chip_info = {
	.ops = &mc33xs2410_hwmon_hwmon_ops,
	.info = mc33xs2410_hwmon_info,
};

static int mc33xs2410_hwmon_probe(struct auxiliary_device *adev,
				  const struct auxiliary_device_id *id)
{
	struct device *dev = &adev->dev;
	struct spi_device *spi = container_of(dev->parent, struct spi_device, dev);
	struct device *hwmon;

	hwmon = devm_hwmon_device_register_with_info(dev, NULL, spi,
						     &mc33xs2410_hwmon_chip_info,
						     NULL);
	return PTR_ERR_OR_ZERO(hwmon);
}

static const struct auxiliary_device_id mc33xs2410_hwmon_ids[] = {
	{
		.name = "pwm_mc33xs2410.hwmon",
	},
	{ }
};
MODULE_DEVICE_TABLE(auxiliary, mc33xs2410_hwmon_ids);

static struct auxiliary_driver mc33xs2410_hwmon_driver = {
	.probe = mc33xs2410_hwmon_probe,
	.id_table = mc33xs2410_hwmon_ids,
};
module_auxiliary_driver(mc33xs2410_hwmon_driver);

MODULE_DESCRIPTION("NXP MC33XS2410 hwmon driver");
MODULE_AUTHOR("Dimitri Fedrau <dimitri.fedrau@liebherr.com>");
MODULE_LICENSE("GPL");
