// SPDX-License-Identifier: GPL-2.0
/*
 * max31827.c - Support for Maxim Low-Power Switch
 *
 * Copyright (c) 2023 Daniel Matyas <daniel.matyas@analog.com>
 */

#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/hwmon.h>
#include <linux/i2c.h>
#include <linux/mutex.h>
#include <linux/regmap.h>

#define MAX31827_T_REG			0x0
#define MAX31827_CONFIGURATION_REG	0x2
#define MAX31827_TH_REG			0x4
#define MAX31827_TL_REG			0x6
#define MAX31827_TH_HYST_REG		0x8
#define MAX31827_TL_HYST_REG		0xA

#define MAX31827_CONFIGURATION_1SHOT_MASK	BIT(0)
#define MAX31827_CONFIGURATION_CNV_RATE_MASK	GENMASK(3, 1)
#define MAX31827_CONFIGURATION_U_TEMP_STAT_MASK	BIT(14)
#define MAX31827_CONFIGURATION_O_TEMP_STAT_MASK	BIT(15)

#define MAX31827_12_BIT_CNV_TIME	141

#define MAX31827_CNV_1_DIV_64_HZ	0x1
#define MAX31827_CNV_1_DIV_32_HZ	0x2
#define MAX31827_CNV_1_DIV_16_HZ	0x3
#define MAX31827_CNV_1_DIV_4_HZ		0x4
#define MAX31827_CNV_1_HZ		0x5
#define MAX31827_CNV_4_HZ		0x6
#define MAX31827_CNV_8_HZ		0x7

#define MAX31827_16_BIT_TO_M_DGR(x)	(sign_extend32(x, 15) * 1000 / 16)
#define MAX31827_M_DGR_TO_16_BIT(x)	(((x) << 4) / 1000)
#define MAX31827_DEVICE_ENABLE(x)	((x) ? 0xA : 0x0)

struct max31827_state {
	/*
	 * Prevent simultaneous access to the i2c client.
	 */
	struct mutex lock;
	struct regmap *regmap;
	bool enable;
};

static const struct regmap_config max31827_regmap = {
	.reg_bits = 8,
	.val_bits = 16,
	.max_register = 0xA,
};

static int write_alarm_val(struct max31827_state *st, unsigned int reg,
			   long val)
{
	unsigned int cfg;
	unsigned int tmp;
	int ret;

	val = MAX31827_M_DGR_TO_16_BIT(val);

	/*
	 * Before the Temperature Threshold Alarm and Alarm Hysteresis Threshold
	 * register values are changed over I2C, the part must be in shutdown
	 * mode.
	 *
	 * Mutex is used to ensure, that some other process doesn't change the
	 * configuration register.
	 */
	mutex_lock(&st->lock);

	if (!st->enable) {
		ret = regmap_write(st->regmap, reg, val);
		goto unlock;
	}

	ret = regmap_read(st->regmap, MAX31827_CONFIGURATION_REG, &cfg);
	if (ret)
		goto unlock;

	tmp = cfg & ~(MAX31827_CONFIGURATION_1SHOT_MASK |
		      MAX31827_CONFIGURATION_CNV_RATE_MASK);
	ret = regmap_write(st->regmap, MAX31827_CONFIGURATION_REG, tmp);
	if (ret)
		goto unlock;

	ret = regmap_write(st->regmap, reg, val);
	if (ret)
		goto unlock;

	ret = regmap_write(st->regmap, MAX31827_CONFIGURATION_REG, cfg);

unlock:
	mutex_unlock(&st->lock);
	return ret;
}

static umode_t max31827_is_visible(const void *state,
				   enum hwmon_sensor_types type, u32 attr,
				   int channel)
{
	if (type == hwmon_temp) {
		switch (attr) {
		case hwmon_temp_enable:
		case hwmon_temp_max:
		case hwmon_temp_min:
		case hwmon_temp_max_hyst:
		case hwmon_temp_min_hyst:
			return 0644;
		case hwmon_temp_input:
		case hwmon_temp_min_alarm:
		case hwmon_temp_max_alarm:
			return 0444;
		default:
			return 0;
		}
	} else if (type == hwmon_chip) {
		if (attr == hwmon_chip_update_interval)
			return 0644;
	}

	return 0;
}

static int max31827_read(struct device *dev, enum hwmon_sensor_types type,
			 u32 attr, int channel, long *val)
{
	struct max31827_state *st = dev_get_drvdata(dev);
	unsigned int uval;
	int ret = 0;

	switch (type) {
	case hwmon_temp:
		switch (attr) {
		case hwmon_temp_enable:
			ret = regmap_read(st->regmap,
					  MAX31827_CONFIGURATION_REG, &uval);
			if (ret)
				break;

			uval = FIELD_GET(MAX31827_CONFIGURATION_1SHOT_MASK |
					 MAX31827_CONFIGURATION_CNV_RATE_MASK,
					 uval);
			*val = !!uval;

			break;
		case hwmon_temp_input:
			mutex_lock(&st->lock);

			if (!st->enable) {
				/*
				 * This operation requires mutex protection,
				 * because the chip configuration should not
				 * be changed during the conversion process.
				 */

				ret = regmap_update_bits(st->regmap,
							 MAX31827_CONFIGURATION_REG,
							 MAX31827_CONFIGURATION_1SHOT_MASK,
							 1);
				if (ret) {
					mutex_unlock(&st->lock);
					return ret;
				}

				msleep(MAX31827_12_BIT_CNV_TIME);
			}
			ret = regmap_read(st->regmap, MAX31827_T_REG, &uval);

			mutex_unlock(&st->lock);

			if (ret)
				break;

			*val = MAX31827_16_BIT_TO_M_DGR(uval);

			break;
		case hwmon_temp_max:
			ret = regmap_read(st->regmap, MAX31827_TH_REG, &uval);
			if (ret)
				break;

			*val = MAX31827_16_BIT_TO_M_DGR(uval);
			break;
		case hwmon_temp_max_hyst:
			ret = regmap_read(st->regmap, MAX31827_TH_HYST_REG,
					  &uval);
			if (ret)
				break;

			*val = MAX31827_16_BIT_TO_M_DGR(uval);
			break;
		case hwmon_temp_max_alarm:
			ret = regmap_read(st->regmap,
					  MAX31827_CONFIGURATION_REG, &uval);
			if (ret)
				break;

			*val = FIELD_GET(MAX31827_CONFIGURATION_O_TEMP_STAT_MASK,
					 uval);
			break;
		case hwmon_temp_min:
			ret = regmap_read(st->regmap, MAX31827_TL_REG, &uval);
			if (ret)
				break;

			*val = MAX31827_16_BIT_TO_M_DGR(uval);
			break;
		case hwmon_temp_min_hyst:
			ret = regmap_read(st->regmap, MAX31827_TL_HYST_REG,
					  &uval);
			if (ret)
				break;

			*val = MAX31827_16_BIT_TO_M_DGR(uval);
			break;
		case hwmon_temp_min_alarm:
			ret = regmap_read(st->regmap,
					  MAX31827_CONFIGURATION_REG, &uval);
			if (ret)
				break;

			*val = FIELD_GET(MAX31827_CONFIGURATION_U_TEMP_STAT_MASK,
					 uval);
			break;
		default:
			ret = -EOPNOTSUPP;
			break;
		}

		break;

	case hwmon_chip:
		if (attr == hwmon_chip_update_interval) {
			ret = regmap_read(st->regmap,
					  MAX31827_CONFIGURATION_REG, &uval);
			if (ret)
				break;

			uval = FIELD_GET(MAX31827_CONFIGURATION_CNV_RATE_MASK,
					 uval);
			switch (uval) {
			case MAX31827_CNV_1_DIV_64_HZ:
				*val = 64000;
				break;
			case MAX31827_CNV_1_DIV_32_HZ:
				*val = 32000;
				break;
			case MAX31827_CNV_1_DIV_16_HZ:
				*val = 16000;
				break;
			case MAX31827_CNV_1_DIV_4_HZ:
				*val = 4000;
				break;
			case MAX31827_CNV_1_HZ:
				*val = 1000;
				break;
			case MAX31827_CNV_4_HZ:
				*val = 250;
				break;
			case MAX31827_CNV_8_HZ:
				*val = 125;
				break;
			default:
				*val = 0;
				break;
			}
		}
		break;

	default:
		ret = -EOPNOTSUPP;
		break;
	}

	return ret;
}

static int max31827_write(struct device *dev, enum hwmon_sensor_types type,
			  u32 attr, int channel, long val)
{
	struct max31827_state *st = dev_get_drvdata(dev);
	int ret;

	switch (type) {
	case hwmon_temp:
		switch (attr) {
		case hwmon_temp_enable:
			if (val >> 1)
				return -EINVAL;

			mutex_lock(&st->lock);
			/**
			 * The chip should not be enabled while a conversion is
			 * performed. Neither should the chip be enabled when
			 * the alarm values are changed.
			 */

			st->enable = val;

			ret = regmap_update_bits(st->regmap,
						 MAX31827_CONFIGURATION_REG,
						 MAX31827_CONFIGURATION_1SHOT_MASK |
						 MAX31827_CONFIGURATION_CNV_RATE_MASK,
						 MAX31827_DEVICE_ENABLE(val));

			mutex_unlock(&st->lock);

			return ret;

		case hwmon_temp_max:
			return write_alarm_val(st, MAX31827_TH_REG, val);

		case hwmon_temp_max_hyst:
			return write_alarm_val(st, MAX31827_TH_HYST_REG, val);

		case hwmon_temp_min:
			return write_alarm_val(st, MAX31827_TL_REG, val);

		case hwmon_temp_min_hyst:
			return write_alarm_val(st, MAX31827_TL_HYST_REG, val);

		default:
			return -EOPNOTSUPP;
		}

	case hwmon_chip:
		if (attr == hwmon_chip_update_interval) {
			if (!st->enable)
				return -EINVAL;

			switch (val) {
			case 125:
				val = MAX31827_CNV_8_HZ;
				break;
			case 250:
				val = MAX31827_CNV_4_HZ;
				break;
			case 1000:
				val = MAX31827_CNV_1_HZ;
				break;
			case 4000:
				val = MAX31827_CNV_1_DIV_4_HZ;
				break;
			case 16000:
				val = MAX31827_CNV_1_DIV_16_HZ;
				break;
			case 32000:
				val = MAX31827_CNV_1_DIV_32_HZ;
				break;
			case 64000:
				val = MAX31827_CNV_1_DIV_64_HZ;
				break;
			default:
				return -EINVAL;
			}

			val = FIELD_PREP(MAX31827_CONFIGURATION_CNV_RATE_MASK,
					 val);

			return regmap_update_bits(st->regmap,
						  MAX31827_CONFIGURATION_REG,
						  MAX31827_CONFIGURATION_CNV_RATE_MASK,
						  val);
		}
		break;

	default:
		return -EOPNOTSUPP;
	}

	return -EOPNOTSUPP;
}

static int max31827_init_client(struct max31827_state *st)
{
	st->enable = true;

	return regmap_update_bits(st->regmap, MAX31827_CONFIGURATION_REG,
				  MAX31827_CONFIGURATION_1SHOT_MASK |
					  MAX31827_CONFIGURATION_CNV_RATE_MASK,
				  MAX31827_DEVICE_ENABLE(1));
}

static const struct hwmon_channel_info *max31827_info[] = {
	HWMON_CHANNEL_INFO(temp, HWMON_T_ENABLE | HWMON_T_INPUT | HWMON_T_MIN |
					 HWMON_T_MIN_HYST | HWMON_T_MIN_ALARM |
					 HWMON_T_MAX | HWMON_T_MAX_HYST |
					 HWMON_T_MAX_ALARM),
	HWMON_CHANNEL_INFO(chip, HWMON_C_UPDATE_INTERVAL),
	NULL,
};

static const struct hwmon_ops max31827_hwmon_ops = {
	.is_visible = max31827_is_visible,
	.read = max31827_read,
	.write = max31827_write,
};

static const struct hwmon_chip_info max31827_chip_info = {
	.ops = &max31827_hwmon_ops,
	.info = max31827_info,
};

static int max31827_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct device *hwmon_dev;
	struct max31827_state *st;
	int err;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_WORD_DATA))
		return -EOPNOTSUPP;

	st = devm_kzalloc(dev, sizeof(*st), GFP_KERNEL);
	if (!st)
		return -ENOMEM;

	mutex_init(&st->lock);

	st->regmap = devm_regmap_init_i2c(client, &max31827_regmap);
	if (IS_ERR(st->regmap))
		return dev_err_probe(dev, PTR_ERR(st->regmap),
				     "Failed to allocate regmap.\n");

	err = max31827_init_client(st);
	if (err)
		return err;

	hwmon_dev = devm_hwmon_device_register_with_info(dev, client->name, st,
							 &max31827_chip_info,
							 NULL);

	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static const struct i2c_device_id max31827_i2c_ids[] = {
	{ "max31827", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, max31827_i2c_ids);

static const struct of_device_id max31827_of_match[] = {
	{ .compatible = "adi,max31827" },
	{ }
};
MODULE_DEVICE_TABLE(of, max31827_of_match);

static struct i2c_driver max31827_driver = {
	.class = I2C_CLASS_HWMON,
	.driver = {
		.name = "max31827",
		.of_match_table = max31827_of_match,
	},
	.probe = max31827_probe,
	.id_table = max31827_i2c_ids,
};
module_i2c_driver(max31827_driver);

MODULE_AUTHOR("Daniel Matyas <daniel.matyas@analog.com>");
MODULE_DESCRIPTION("Maxim MAX31827 low-power temperature switch driver");
MODULE_LICENSE("GPL");
