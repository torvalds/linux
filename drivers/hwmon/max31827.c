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
#include <linux/of_device.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>

#define MAX31827_T_REG			0x0
#define MAX31827_CONFIGURATION_REG	0x2
#define MAX31827_TH_REG			0x4
#define MAX31827_TL_REG			0x6
#define MAX31827_TH_HYST_REG		0x8
#define MAX31827_TL_HYST_REG		0xA

#define MAX31827_CONFIGURATION_1SHOT_MASK	BIT(0)
#define MAX31827_CONFIGURATION_CNV_RATE_MASK	GENMASK(3, 1)
#define MAX31827_CONFIGURATION_PEC_EN_MASK	BIT(4)
#define MAX31827_CONFIGURATION_TIMEOUT_MASK	BIT(5)
#define MAX31827_CONFIGURATION_RESOLUTION_MASK	GENMASK(7, 6)
#define MAX31827_CONFIGURATION_ALRM_POL_MASK	BIT(8)
#define MAX31827_CONFIGURATION_COMP_INT_MASK	BIT(9)
#define MAX31827_CONFIGURATION_FLT_Q_MASK	GENMASK(11, 10)
#define MAX31827_CONFIGURATION_U_TEMP_STAT_MASK	BIT(14)
#define MAX31827_CONFIGURATION_O_TEMP_STAT_MASK	BIT(15)

#define MAX31827_ALRM_POL_LOW	0x0
#define MAX31827_ALRM_POL_HIGH	0x1
#define MAX31827_FLT_Q_1	0x0
#define MAX31827_FLT_Q_4	0x2

#define MAX31827_8_BIT_CNV_TIME		9
#define MAX31827_9_BIT_CNV_TIME		18
#define MAX31827_10_BIT_CNV_TIME	35
#define MAX31827_12_BIT_CNV_TIME	140

#define MAX31827_16_BIT_TO_M_DGR(x)	(sign_extend32(x, 15) * 1000 / 16)
#define MAX31827_M_DGR_TO_16_BIT(x)	(((x) << 4) / 1000)
#define MAX31827_DEVICE_ENABLE(x)	((x) ? 0xA : 0x0)

/*
 * The enum passed in the .data pointer of struct of_device_id must
 * start with a value != 0 since that is a requirement for using
 * device_get_match_data().
 */
enum chips { max31827 = 1, max31828, max31829 };

enum max31827_cnv {
	MAX31827_CNV_1_DIV_64_HZ = 1,
	MAX31827_CNV_1_DIV_32_HZ,
	MAX31827_CNV_1_DIV_16_HZ,
	MAX31827_CNV_1_DIV_4_HZ,
	MAX31827_CNV_1_HZ,
	MAX31827_CNV_4_HZ,
	MAX31827_CNV_8_HZ,
};

static const u16 max31827_conversions[] = {
	[MAX31827_CNV_1_DIV_64_HZ] = 64000,
	[MAX31827_CNV_1_DIV_32_HZ] = 32000,
	[MAX31827_CNV_1_DIV_16_HZ] = 16000,
	[MAX31827_CNV_1_DIV_4_HZ] = 4000,
	[MAX31827_CNV_1_HZ] = 1000,
	[MAX31827_CNV_4_HZ] = 250,
	[MAX31827_CNV_8_HZ] = 125,
};

enum max31827_resolution {
	MAX31827_RES_8_BIT = 0,
	MAX31827_RES_9_BIT,
	MAX31827_RES_10_BIT,
	MAX31827_RES_12_BIT,
};

static const u16 max31827_resolutions[] = {
	[MAX31827_RES_8_BIT] = 1000,
	[MAX31827_RES_9_BIT] = 500,
	[MAX31827_RES_10_BIT] = 250,
	[MAX31827_RES_12_BIT] = 62,
};

static const u16 max31827_conv_times[] = {
	[MAX31827_RES_8_BIT] = MAX31827_8_BIT_CNV_TIME,
	[MAX31827_RES_9_BIT] = MAX31827_9_BIT_CNV_TIME,
	[MAX31827_RES_10_BIT] = MAX31827_10_BIT_CNV_TIME,
	[MAX31827_RES_12_BIT] = MAX31827_12_BIT_CNV_TIME,
};

struct max31827_state {
	/*
	 * Prevent simultaneous access to the i2c client.
	 */
	struct mutex lock;
	struct regmap *regmap;
	bool enable;
	unsigned int resolution;
	unsigned int update_interval;
};

static const struct regmap_config max31827_regmap = {
	.reg_bits = 8,
	.val_bits = 16,
	.max_register = 0xA,
};

static int shutdown_write(struct max31827_state *st, unsigned int reg,
			  unsigned int mask, unsigned int val)
{
	unsigned int cfg;
	unsigned int cnv_rate;
	int ret;

	/*
	 * Before the Temperature Threshold Alarm, Alarm Hysteresis Threshold
	 * and Resolution bits from Configuration register are changed over I2C,
	 * the part must be in shutdown mode.
	 *
	 * Mutex is used to ensure, that some other process doesn't change the
	 * configuration register.
	 */
	mutex_lock(&st->lock);

	if (!st->enable) {
		if (!mask)
			ret = regmap_write(st->regmap, reg, val);
		else
			ret = regmap_update_bits(st->regmap, reg, mask, val);
		goto unlock;
	}

	ret = regmap_read(st->regmap, MAX31827_CONFIGURATION_REG, &cfg);
	if (ret)
		goto unlock;

	cnv_rate = MAX31827_CONFIGURATION_CNV_RATE_MASK & cfg;
	cfg = cfg & ~(MAX31827_CONFIGURATION_1SHOT_MASK |
		      MAX31827_CONFIGURATION_CNV_RATE_MASK);
	ret = regmap_write(st->regmap, MAX31827_CONFIGURATION_REG, cfg);
	if (ret)
		goto unlock;

	if (!mask)
		ret = regmap_write(st->regmap, reg, val);
	else
		ret = regmap_update_bits(st->regmap, reg, mask, val);

	if (ret)
		goto unlock;

	ret = regmap_update_bits(st->regmap, MAX31827_CONFIGURATION_REG,
				 MAX31827_CONFIGURATION_CNV_RATE_MASK,
				 cnv_rate);

unlock:
	mutex_unlock(&st->lock);
	return ret;
}

static int write_alarm_val(struct max31827_state *st, unsigned int reg,
			   long val)
{
	val = MAX31827_M_DGR_TO_16_BIT(val);

	return shutdown_write(st, reg, 0, val);
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
				msleep(max31827_conv_times[st->resolution]);
			}

			/*
			 * For 12-bit resolution the conversion time is 140 ms,
			 * thus an additional 15 ms is needed to complete the
			 * conversion: 125 ms + 15 ms = 140 ms
			 */
			if (max31827_resolutions[st->resolution] == 12 &&
			    st->update_interval == 125)
				usleep_range(15000, 20000);

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
			*val = max31827_conversions[uval];
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
	int res = 1;
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
		switch (attr) {
		case hwmon_chip_update_interval:
			if (!st->enable)
				return -EINVAL;

			/*
			 * Convert the desired conversion rate into register
			 * bits. res is already initialized with 1.
			 *
			 * This was inspired by lm73 driver.
			 */
			while (res < ARRAY_SIZE(max31827_conversions) &&
			       val < max31827_conversions[res])
				res++;

			if (res == ARRAY_SIZE(max31827_conversions))
				res = ARRAY_SIZE(max31827_conversions) - 1;

			res = FIELD_PREP(MAX31827_CONFIGURATION_CNV_RATE_MASK,
					 res);

			ret = regmap_update_bits(st->regmap,
						 MAX31827_CONFIGURATION_REG,
						 MAX31827_CONFIGURATION_CNV_RATE_MASK,
						 res);
			if (ret)
				return ret;

			st->update_interval = val;

			return 0;
		case hwmon_chip_pec:
			return regmap_update_bits(st->regmap, MAX31827_CONFIGURATION_REG,
						  MAX31827_CONFIGURATION_PEC_EN_MASK,
						  val ? MAX31827_CONFIGURATION_PEC_EN_MASK : 0);
		default:
			return -EOPNOTSUPP;
		}
	default:
		return -EOPNOTSUPP;
	}
}

static ssize_t temp1_resolution_show(struct device *dev,
				     struct device_attribute *devattr,
				     char *buf)
{
	struct max31827_state *st = dev_get_drvdata(dev);
	unsigned int val;
	int ret;

	ret = regmap_read(st->regmap, MAX31827_CONFIGURATION_REG, &val);
	if (ret)
		return ret;

	val = FIELD_GET(MAX31827_CONFIGURATION_RESOLUTION_MASK, val);

	return sysfs_emit(buf, "%u\n", max31827_resolutions[val]);
}

static ssize_t temp1_resolution_store(struct device *dev,
				      struct device_attribute *devattr,
				      const char *buf, size_t count)
{
	struct max31827_state *st = dev_get_drvdata(dev);
	unsigned int idx = 0;
	unsigned int val;
	int ret;

	ret = kstrtouint(buf, 10, &val);
	if (ret)
		return ret;

	/*
	 * Convert the desired resolution into register
	 * bits. idx is already initialized with 0.
	 *
	 * This was inspired by lm73 driver.
	 */
	while (idx < ARRAY_SIZE(max31827_resolutions) &&
	       val < max31827_resolutions[idx])
		idx++;

	if (idx == ARRAY_SIZE(max31827_resolutions))
		idx = ARRAY_SIZE(max31827_resolutions) - 1;

	st->resolution = idx;

	ret = shutdown_write(st, MAX31827_CONFIGURATION_REG,
			     MAX31827_CONFIGURATION_RESOLUTION_MASK,
			     FIELD_PREP(MAX31827_CONFIGURATION_RESOLUTION_MASK,
					idx));

	return ret ? ret : count;
}

static DEVICE_ATTR_RW(temp1_resolution);

static struct attribute *max31827_attrs[] = {
	&dev_attr_temp1_resolution.attr,
	NULL
};
ATTRIBUTE_GROUPS(max31827);

static const struct i2c_device_id max31827_i2c_ids[] = {
	{ "max31827", max31827 },
	{ "max31828", max31828 },
	{ "max31829", max31829 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, max31827_i2c_ids);

static int max31827_init_client(struct max31827_state *st,
				struct device *dev)
{
	struct fwnode_handle *fwnode;
	unsigned int res = 0;
	u32 data, lsb_idx;
	enum chips type;
	bool prop;
	int ret;

	fwnode = dev_fwnode(dev);

	st->enable = true;
	res |= MAX31827_DEVICE_ENABLE(1);

	res |= MAX31827_CONFIGURATION_RESOLUTION_MASK;

	prop = fwnode_property_read_bool(fwnode, "adi,comp-int");
	res |= FIELD_PREP(MAX31827_CONFIGURATION_COMP_INT_MASK, prop);

	prop = fwnode_property_read_bool(fwnode, "adi,timeout-enable");
	res |= FIELD_PREP(MAX31827_CONFIGURATION_TIMEOUT_MASK, !prop);

	type = (enum chips)(uintptr_t)device_get_match_data(dev);

	if (fwnode_property_present(fwnode, "adi,alarm-pol")) {
		ret = fwnode_property_read_u32(fwnode, "adi,alarm-pol", &data);
		if (ret)
			return ret;

		res |= FIELD_PREP(MAX31827_CONFIGURATION_ALRM_POL_MASK, !!data);
	} else {
		/*
		 * Set default value.
		 */
		switch (type) {
		case max31827:
		case max31828:
			res |= FIELD_PREP(MAX31827_CONFIGURATION_ALRM_POL_MASK,
					  MAX31827_ALRM_POL_LOW);
			break;
		case max31829:
			res |= FIELD_PREP(MAX31827_CONFIGURATION_ALRM_POL_MASK,
					  MAX31827_ALRM_POL_HIGH);
			break;
		default:
			return -EOPNOTSUPP;
		}
	}

	if (fwnode_property_present(fwnode, "adi,fault-q")) {
		ret = fwnode_property_read_u32(fwnode, "adi,fault-q", &data);
		if (ret)
			return ret;

		/*
		 * Convert the desired fault queue into register bits.
		 */
		if (data != 0)
			lsb_idx = __ffs(data);

		if (hweight32(data) != 1 || lsb_idx > 4) {
			dev_err(dev, "Invalid data in adi,fault-q\n");
			return -EINVAL;
		}

		res |= FIELD_PREP(MAX31827_CONFIGURATION_FLT_Q_MASK, lsb_idx);
	} else {
		/*
		 * Set default value.
		 */
		switch (type) {
		case max31827:
			res |= FIELD_PREP(MAX31827_CONFIGURATION_FLT_Q_MASK,
					  MAX31827_FLT_Q_1);
			break;
		case max31828:
		case max31829:
			res |= FIELD_PREP(MAX31827_CONFIGURATION_FLT_Q_MASK,
					  MAX31827_FLT_Q_4);
			break;
		default:
			return -EOPNOTSUPP;
		}
	}

	return regmap_write(st->regmap, MAX31827_CONFIGURATION_REG, res);
}

static const struct hwmon_channel_info *max31827_info[] = {
	HWMON_CHANNEL_INFO(temp, HWMON_T_ENABLE | HWMON_T_INPUT | HWMON_T_MIN |
					 HWMON_T_MIN_HYST | HWMON_T_MIN_ALARM |
					 HWMON_T_MAX | HWMON_T_MAX_HYST |
					 HWMON_T_MAX_ALARM),
	HWMON_CHANNEL_INFO(chip, HWMON_C_UPDATE_INTERVAL | HWMON_C_PEC),
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

	err = devm_regulator_get_enable(dev, "vref");
	if (err)
		return dev_err_probe(dev, err, "failed to enable regulator\n");

	err = max31827_init_client(st, dev);
	if (err)
		return err;

	hwmon_dev = devm_hwmon_device_register_with_info(dev, client->name, st,
							 &max31827_chip_info,
							 max31827_groups);

	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static const struct of_device_id max31827_of_match[] = {
	{
		.compatible = "adi,max31827",
		.data = (void *)max31827
	},
	{
		.compatible = "adi,max31828",
		.data = (void *)max31828
	},
	{
		.compatible = "adi,max31829",
		.data = (void *)max31829
	},
	{ }
};
MODULE_DEVICE_TABLE(of, max31827_of_match);

static struct i2c_driver max31827_driver = {
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
