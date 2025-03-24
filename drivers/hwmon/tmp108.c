// SPDX-License-Identifier: GPL-2.0-or-later
/* Texas Instruments TMP108 SMBus temperature sensor driver
 *
 * Copyright (C) 2016 John Muir <john@jmuir.com>
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/hwmon.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/i2c.h>
#include <linux/i3c/device.h>
#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>

#define	DRIVER_NAME "tmp108"

#define	TMP108_REG_TEMP		0x00
#define	TMP108_REG_CONF		0x01
#define	TMP108_REG_TLOW		0x02
#define	TMP108_REG_THIGH	0x03

#define TMP108_TEMP_MIN_MC	-50000 /* Minimum millicelcius. */
#define TMP108_TEMP_MAX_MC	127937 /* Maximum millicelcius. */

/* Configuration register bits.
 * Note: these bit definitions are byte swapped.
 */
#define TMP108_CONF_M0		0x0100 /* Sensor mode. */
#define TMP108_CONF_M1		0x0200
#define TMP108_CONF_TM		0x0400 /* Thermostat mode. */
#define TMP108_CONF_FL		0x0800 /* Watchdog flag - TLOW */
#define TMP108_CONF_FH		0x1000 /* Watchdog flag - THIGH */
#define TMP108_CONF_CR0		0x2000 /* Conversion rate. */
#define TMP108_CONF_CR1		0x4000
#define TMP108_CONF_ID		0x8000
#define TMP108_CONF_HYS0	0x0010 /* Hysteresis. */
#define TMP108_CONF_HYS1	0x0020
#define TMP108_CONF_POL		0x0080 /* Polarity of alert. */

/* Defaults set by the hardware upon reset. */
#define TMP108_CONF_DEFAULTS		(TMP108_CONF_CR0 | TMP108_CONF_TM |\
					 TMP108_CONF_HYS0 | TMP108_CONF_M1)
/* These bits are read-only. */
#define TMP108_CONF_READ_ONLY		(TMP108_CONF_FL | TMP108_CONF_FH |\
					 TMP108_CONF_ID)

#define TMP108_CONF_MODE_MASK		(TMP108_CONF_M0|TMP108_CONF_M1)
#define TMP108_MODE_SHUTDOWN		0x0000
#define TMP108_MODE_ONE_SHOT		TMP108_CONF_M0
#define TMP108_MODE_CONTINUOUS		TMP108_CONF_M1		/* Default */
					/* When M1 is set, M0 is ignored. */

#define TMP108_CONF_CONVRATE_MASK	(TMP108_CONF_CR0|TMP108_CONF_CR1)
#define TMP108_CONVRATE_0P25HZ		0x0000
#define TMP108_CONVRATE_1HZ		TMP108_CONF_CR0		/* Default */
#define TMP108_CONVRATE_4HZ		TMP108_CONF_CR1
#define TMP108_CONVRATE_16HZ		(TMP108_CONF_CR0|TMP108_CONF_CR1)

#define TMP108_CONF_HYSTERESIS_MASK	(TMP108_CONF_HYS0|TMP108_CONF_HYS1)
#define TMP108_HYSTERESIS_0C		0x0000
#define TMP108_HYSTERESIS_1C		TMP108_CONF_HYS0	/* Default */
#define TMP108_HYSTERESIS_2C		TMP108_CONF_HYS1
#define TMP108_HYSTERESIS_4C		(TMP108_CONF_HYS0|TMP108_CONF_HYS1)

#define TMP108_CONVERSION_TIME_MS	30	/* in milli-seconds */

struct tmp108 {
	struct regmap *regmap;
	u16 orig_config;
	unsigned long ready_time;
};

/* convert 12-bit TMP108 register value to milliCelsius */
static inline int tmp108_temp_reg_to_mC(s16 val)
{
	return (val & ~0x0f) * 1000 / 256;
}

/* convert milliCelsius to left adjusted 12-bit TMP108 register value */
static inline u16 tmp108_mC_to_temp_reg(int val)
{
	return (val * 256) / 1000;
}

static int tmp108_read(struct device *dev, enum hwmon_sensor_types type,
		       u32 attr, int channel, long *temp)
{
	struct tmp108 *tmp108 = dev_get_drvdata(dev);
	unsigned int regval;
	int err, hyst;

	if (type == hwmon_chip) {
		if (attr == hwmon_chip_update_interval) {
			err = regmap_read(tmp108->regmap, TMP108_REG_CONF,
					  &regval);
			if (err < 0)
				return err;
			switch (regval & TMP108_CONF_CONVRATE_MASK) {
			case TMP108_CONVRATE_0P25HZ:
			default:
				*temp = 4000;
				break;
			case TMP108_CONVRATE_1HZ:
				*temp = 1000;
				break;
			case TMP108_CONVRATE_4HZ:
				*temp = 250;
				break;
			case TMP108_CONVRATE_16HZ:
				*temp = 63;
				break;
			}
			return 0;
		}
		return -EOPNOTSUPP;
	}

	switch (attr) {
	case hwmon_temp_input:
		/* Is it too early to return a conversion ? */
		if (time_before(jiffies, tmp108->ready_time)) {
			dev_dbg(dev, "%s: Conversion not ready yet..\n",
				__func__);
			return -EAGAIN;
		}
		err = regmap_read(tmp108->regmap, TMP108_REG_TEMP, &regval);
		if (err < 0)
			return err;
		*temp = tmp108_temp_reg_to_mC(regval);
		break;
	case hwmon_temp_min:
	case hwmon_temp_max:
		err = regmap_read(tmp108->regmap, attr == hwmon_temp_min ?
				  TMP108_REG_TLOW : TMP108_REG_THIGH, &regval);
		if (err < 0)
			return err;
		*temp = tmp108_temp_reg_to_mC(regval);
		break;
	case hwmon_temp_min_alarm:
	case hwmon_temp_max_alarm:
		err = regmap_read(tmp108->regmap, TMP108_REG_CONF, &regval);
		if (err < 0)
			return err;
		*temp = !!(regval & (attr == hwmon_temp_min_alarm ?
				     TMP108_CONF_FL : TMP108_CONF_FH));
		break;
	case hwmon_temp_min_hyst:
	case hwmon_temp_max_hyst:
		err = regmap_read(tmp108->regmap, TMP108_REG_CONF, &regval);
		if (err < 0)
			return err;
		switch (regval & TMP108_CONF_HYSTERESIS_MASK) {
		case TMP108_HYSTERESIS_0C:
		default:
			hyst = 0;
			break;
		case TMP108_HYSTERESIS_1C:
			hyst = 1000;
			break;
		case TMP108_HYSTERESIS_2C:
			hyst = 2000;
			break;
		case TMP108_HYSTERESIS_4C:
			hyst = 4000;
			break;
		}
		err = regmap_read(tmp108->regmap, attr == hwmon_temp_min_hyst ?
				  TMP108_REG_TLOW : TMP108_REG_THIGH, &regval);
		if (err < 0)
			return err;
		*temp = tmp108_temp_reg_to_mC(regval);
		if (attr == hwmon_temp_min_hyst)
			*temp += hyst;
		else
			*temp -= hyst;
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int tmp108_write(struct device *dev, enum hwmon_sensor_types type,
			u32 attr, int channel, long temp)
{
	struct tmp108 *tmp108 = dev_get_drvdata(dev);
	u32 regval, mask;
	int err;

	if (type == hwmon_chip) {
		if (attr == hwmon_chip_update_interval) {
			if (temp < 156)
				mask = TMP108_CONVRATE_16HZ;
			else if (temp < 625)
				mask = TMP108_CONVRATE_4HZ;
			else if (temp < 2500)
				mask = TMP108_CONVRATE_1HZ;
			else
				mask = TMP108_CONVRATE_0P25HZ;
			return regmap_update_bits(tmp108->regmap,
						  TMP108_REG_CONF,
						  TMP108_CONF_CONVRATE_MASK,
						  mask);
		}
		return -EOPNOTSUPP;
	}

	switch (attr) {
	case hwmon_temp_min:
	case hwmon_temp_max:
		temp = clamp_val(temp, TMP108_TEMP_MIN_MC, TMP108_TEMP_MAX_MC);
		return regmap_write(tmp108->regmap,
				    attr == hwmon_temp_min ?
					TMP108_REG_TLOW : TMP108_REG_THIGH,
				    tmp108_mC_to_temp_reg(temp));
	case hwmon_temp_min_hyst:
	case hwmon_temp_max_hyst:
		temp = clamp_val(temp, TMP108_TEMP_MIN_MC, TMP108_TEMP_MAX_MC);
		err = regmap_read(tmp108->regmap,
				  attr == hwmon_temp_min_hyst ?
					TMP108_REG_TLOW : TMP108_REG_THIGH,
				  &regval);
		if (err < 0)
			return err;
		if (attr == hwmon_temp_min_hyst)
			temp -= tmp108_temp_reg_to_mC(regval);
		else
			temp = tmp108_temp_reg_to_mC(regval) - temp;
		if (temp < 500)
			mask = TMP108_HYSTERESIS_0C;
		else if (temp < 1500)
			mask = TMP108_HYSTERESIS_1C;
		else if (temp < 3000)
			mask = TMP108_HYSTERESIS_2C;
		else
			mask = TMP108_HYSTERESIS_4C;
		return regmap_update_bits(tmp108->regmap, TMP108_REG_CONF,
					  TMP108_CONF_HYSTERESIS_MASK, mask);
	default:
		return -EOPNOTSUPP;
	}
}

static umode_t tmp108_is_visible(const void *data, enum hwmon_sensor_types type,
				 u32 attr, int channel)
{
	if (type == hwmon_chip && attr == hwmon_chip_update_interval)
		return 0644;

	if (type != hwmon_temp)
		return 0;

	switch (attr) {
	case hwmon_temp_input:
	case hwmon_temp_min_alarm:
	case hwmon_temp_max_alarm:
		return 0444;
	case hwmon_temp_min:
	case hwmon_temp_max:
	case hwmon_temp_min_hyst:
	case hwmon_temp_max_hyst:
		return 0644;
	default:
		return 0;
	}
}

static const struct hwmon_channel_info * const tmp108_info[] = {
	HWMON_CHANNEL_INFO(chip,
			   HWMON_C_REGISTER_TZ | HWMON_C_UPDATE_INTERVAL),
	HWMON_CHANNEL_INFO(temp,
			   HWMON_T_INPUT | HWMON_T_MAX | HWMON_T_MIN |
			   HWMON_T_MIN_HYST | HWMON_T_MAX_HYST |
			   HWMON_T_MIN_ALARM | HWMON_T_MAX_ALARM),
	NULL
};

static const struct hwmon_ops tmp108_hwmon_ops = {
	.is_visible = tmp108_is_visible,
	.read = tmp108_read,
	.write = tmp108_write,
};

static const struct hwmon_chip_info tmp108_chip_info = {
	.ops = &tmp108_hwmon_ops,
	.info = tmp108_info,
};

static void tmp108_restore_config(void *data)
{
	struct tmp108 *tmp108 = data;

	regmap_write(tmp108->regmap, TMP108_REG_CONF, tmp108->orig_config);
}

static bool tmp108_is_writeable_reg(struct device *dev, unsigned int reg)
{
	return reg != TMP108_REG_TEMP;
}

static bool tmp108_is_volatile_reg(struct device *dev, unsigned int reg)
{
	/* Configuration register must be volatile to enable FL and FH. */
	return reg == TMP108_REG_TEMP || reg == TMP108_REG_CONF;
}

static const struct regmap_config tmp108_regmap_config = {
	.reg_bits = 8,
	.val_bits = 16,
	.max_register = TMP108_REG_THIGH,
	.writeable_reg = tmp108_is_writeable_reg,
	.volatile_reg = tmp108_is_volatile_reg,
	.val_format_endian = REGMAP_ENDIAN_BIG,
	.cache_type = REGCACHE_MAPLE,
	.use_single_read = true,
	.use_single_write = true,
};

static int tmp108_common_probe(struct device *dev, struct regmap *regmap, char *name)
{
	struct device *hwmon_dev;
	struct tmp108 *tmp108;
	u32 config;
	int err;

	err = devm_regulator_get_enable(dev, "vcc");
	if (err)
		return dev_err_probe(dev, err, "Failed to enable regulator\n");

	tmp108 = devm_kzalloc(dev, sizeof(*tmp108), GFP_KERNEL);
	if (!tmp108)
		return -ENOMEM;

	dev_set_drvdata(dev, tmp108);
	tmp108->regmap = regmap;

	err = regmap_read(tmp108->regmap, TMP108_REG_CONF, &config);
	if (err < 0) {
		dev_err(dev, "error reading config register: %d", err);
		return err;
	}
	tmp108->orig_config = config;

	/* Only continuous mode is supported. */
	config &= ~TMP108_CONF_MODE_MASK;
	config |= TMP108_MODE_CONTINUOUS;

	/* Only comparator mode is supported. */
	config &= ~TMP108_CONF_TM;

	err = regmap_write(tmp108->regmap, TMP108_REG_CONF, config);
	if (err < 0) {
		dev_err(dev, "error writing config register: %d", err);
		return err;
	}

	tmp108->ready_time = jiffies;
	if ((tmp108->orig_config & TMP108_CONF_MODE_MASK) ==
	    TMP108_MODE_SHUTDOWN)
		tmp108->ready_time +=
			msecs_to_jiffies(TMP108_CONVERSION_TIME_MS);

	err = devm_add_action_or_reset(dev, tmp108_restore_config, tmp108);
	if (err) {
		dev_err(dev, "add action or reset failed: %d", err);
		return err;
	}

	hwmon_dev = devm_hwmon_device_register_with_info(dev, name,
							 tmp108,
							 &tmp108_chip_info,
							 NULL);
	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static int tmp108_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct regmap *regmap;

	if (!i2c_check_functionality(client->adapter,
				     I2C_FUNC_SMBUS_WORD_DATA))
		return dev_err_probe(dev, -ENODEV,
				     "adapter doesn't support SMBus word transactions\n");

	regmap = devm_regmap_init_i2c(client, &tmp108_regmap_config);
	if (IS_ERR(regmap))
		return dev_err_probe(dev, PTR_ERR(regmap), "regmap init failed");

	return tmp108_common_probe(dev, regmap, client->name);
}

static int tmp108_suspend(struct device *dev)
{
	struct tmp108 *tmp108 = dev_get_drvdata(dev);

	return regmap_update_bits(tmp108->regmap, TMP108_REG_CONF,
				  TMP108_CONF_MODE_MASK, TMP108_MODE_SHUTDOWN);
}

static int tmp108_resume(struct device *dev)
{
	struct tmp108 *tmp108 = dev_get_drvdata(dev);
	int err;

	err = regmap_update_bits(tmp108->regmap, TMP108_REG_CONF,
				 TMP108_CONF_MODE_MASK, TMP108_MODE_CONTINUOUS);
	tmp108->ready_time = jiffies +
			     msecs_to_jiffies(TMP108_CONVERSION_TIME_MS);
	return err;
}

static DEFINE_SIMPLE_DEV_PM_OPS(tmp108_dev_pm_ops, tmp108_suspend, tmp108_resume);

static const struct i2c_device_id tmp108_i2c_ids[] = {
	{ "p3t1085" },
	{ "tmp108" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, tmp108_i2c_ids);

static const struct of_device_id tmp108_of_ids[] = {
	{ .compatible = "nxp,p3t1085", },
	{ .compatible = "ti,tmp108", },
	{}
};
MODULE_DEVICE_TABLE(of, tmp108_of_ids);

static struct i2c_driver tmp108_driver = {
	.driver = {
		.name	= DRIVER_NAME,
		.pm	= pm_sleep_ptr(&tmp108_dev_pm_ops),
		.of_match_table = tmp108_of_ids,
	},
	.probe		= tmp108_probe,
	.id_table	= tmp108_i2c_ids,
};

static const struct i3c_device_id p3t1085_i3c_ids[] = {
	I3C_DEVICE(0x011b, 0x1529, NULL),
	{}
};
MODULE_DEVICE_TABLE(i3c, p3t1085_i3c_ids);

static int p3t1085_i3c_probe(struct i3c_device *i3cdev)
{
	struct device *dev = i3cdev_to_dev(i3cdev);
	struct regmap *regmap;

	regmap = devm_regmap_init_i3c(i3cdev, &tmp108_regmap_config);
	if (IS_ERR(regmap))
		return dev_err_probe(dev, PTR_ERR(regmap),
				     "Failed to register i3c regmap\n");

	return tmp108_common_probe(dev, regmap, "p3t1085_i3c");
}

static struct i3c_driver p3t1085_driver = {
	.driver = {
		.name = "p3t1085_i3c",
	},
	.probe = p3t1085_i3c_probe,
	.id_table = p3t1085_i3c_ids,
};

module_i3c_i2c_driver(p3t1085_driver, &tmp108_driver)

MODULE_AUTHOR("John Muir <john@jmuir.com>");
MODULE_DESCRIPTION("Texas Instruments TMP108 temperature sensor driver");
MODULE_LICENSE("GPL");
