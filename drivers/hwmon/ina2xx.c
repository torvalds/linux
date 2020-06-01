// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for Texas Instruments INA219, INA226 power monitor chips
 *
 * INA219:
 * Zero Drift Bi-Directional Current/Power Monitor with I2C Interface
 * Datasheet: http://www.ti.com/product/ina219
 *
 * INA220:
 * Bi-Directional Current/Power Monitor with I2C Interface
 * Datasheet: http://www.ti.com/product/ina220
 *
 * INA226:
 * Bi-Directional Current/Power Monitor with I2C Interface
 * Datasheet: http://www.ti.com/product/ina226
 *
 * INA230:
 * Bi-directional Current/Power Monitor with I2C Interface
 * Datasheet: http://www.ti.com/product/ina230
 *
 * Copyright (C) 2012 Lothar Felten <lothar.felten@gmail.com>
 * Thanks to Jan Volkering
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/jiffies.h>
#include <linux/of_device.h>
#include <linux/of.h>
#include <linux/delay.h>
#include <linux/util_macros.h>
#include <linux/regmap.h>

#include <linux/platform_data/ina2xx.h>

/* common register definitions */
#define INA2XX_CONFIG			0x00
#define INA2XX_SHUNT_VOLTAGE		0x01 /* readonly */
#define INA2XX_BUS_VOLTAGE		0x02 /* readonly */
#define INA2XX_POWER			0x03 /* readonly */
#define INA2XX_CURRENT			0x04 /* readonly */
#define INA2XX_CALIBRATION		0x05

/* INA226 register definitions */
#define INA226_MASK_ENABLE		0x06
#define INA226_ALERT_LIMIT		0x07
#define INA226_DIE_ID			0xFF

/* register count */
#define INA219_REGISTERS		6
#define INA226_REGISTERS		8

#define INA2XX_MAX_REGISTERS		8

/* settings - depend on use case */
#define INA219_CONFIG_DEFAULT		0x399F	/* PGA=8 */
#define INA226_CONFIG_DEFAULT		0x4527	/* averages=16 */

/* worst case is 68.10 ms (~14.6Hz, ina219) */
#define INA2XX_CONVERSION_RATE		15
#define INA2XX_MAX_DELAY		69 /* worst case delay in ms */

#define INA2XX_RSHUNT_DEFAULT		10000

/* bit mask for reading the averaging setting in the configuration register */
#define INA226_AVG_RD_MASK		0x0E00

#define INA226_READ_AVG(reg)		(((reg) & INA226_AVG_RD_MASK) >> 9)
#define INA226_SHIFT_AVG(val)		((val) << 9)

/* bit number of alert functions in Mask/Enable Register */
#define INA226_SHUNT_OVER_VOLTAGE_BIT	15
#define INA226_SHUNT_UNDER_VOLTAGE_BIT	14
#define INA226_BUS_OVER_VOLTAGE_BIT	13
#define INA226_BUS_UNDER_VOLTAGE_BIT	12
#define INA226_POWER_OVER_LIMIT_BIT	11

/* bit mask for alert config bits of Mask/Enable Register */
#define INA226_ALERT_CONFIG_MASK	0xFC00
#define INA226_ALERT_FUNCTION_FLAG	BIT(4)

/* common attrs, ina226 attrs and NULL */
#define INA2XX_MAX_ATTRIBUTE_GROUPS	3

/*
 * Both bus voltage and shunt voltage conversion times for ina226 are set
 * to 0b0100 on POR, which translates to 2200 microseconds in total.
 */
#define INA226_TOTAL_CONV_TIME_DEFAULT	2200

static struct regmap_config ina2xx_regmap_config = {
	.reg_bits = 8,
	.val_bits = 16,
};

enum ina2xx_ids { ina219, ina226 };

struct ina2xx_config {
	u16 config_default;
	int calibration_value;
	int registers;
	int shunt_div;
	int bus_voltage_shift;
	int bus_voltage_lsb;	/* uV */
	int power_lsb_factor;
};

struct ina2xx_data {
	const struct ina2xx_config *config;

	long rshunt;
	long current_lsb_uA;
	long power_lsb_uW;
	struct mutex config_lock;
	struct regmap *regmap;

	const struct attribute_group *groups[INA2XX_MAX_ATTRIBUTE_GROUPS];
};

static const struct ina2xx_config ina2xx_config[] = {
	[ina219] = {
		.config_default = INA219_CONFIG_DEFAULT,
		.calibration_value = 4096,
		.registers = INA219_REGISTERS,
		.shunt_div = 100,
		.bus_voltage_shift = 3,
		.bus_voltage_lsb = 4000,
		.power_lsb_factor = 20,
	},
	[ina226] = {
		.config_default = INA226_CONFIG_DEFAULT,
		.calibration_value = 2048,
		.registers = INA226_REGISTERS,
		.shunt_div = 400,
		.bus_voltage_shift = 0,
		.bus_voltage_lsb = 1250,
		.power_lsb_factor = 25,
	},
};

/*
 * Available averaging rates for ina226. The indices correspond with
 * the bit values expected by the chip (according to the ina226 datasheet,
 * table 3 AVG bit settings, found at
 * http://www.ti.com/lit/ds/symlink/ina226.pdf.
 */
static const int ina226_avg_tab[] = { 1, 4, 16, 64, 128, 256, 512, 1024 };

static int ina226_reg_to_interval(u16 config)
{
	int avg = ina226_avg_tab[INA226_READ_AVG(config)];

	/*
	 * Multiply the total conversion time by the number of averages.
	 * Return the result in milliseconds.
	 */
	return DIV_ROUND_CLOSEST(avg * INA226_TOTAL_CONV_TIME_DEFAULT, 1000);
}

/*
 * Return the new, shifted AVG field value of CONFIG register,
 * to use with regmap_update_bits
 */
static u16 ina226_interval_to_reg(int interval)
{
	int avg, avg_bits;

	avg = DIV_ROUND_CLOSEST(interval * 1000,
				INA226_TOTAL_CONV_TIME_DEFAULT);
	avg_bits = find_closest(avg, ina226_avg_tab,
				ARRAY_SIZE(ina226_avg_tab));

	return INA226_SHIFT_AVG(avg_bits);
}

/*
 * Calibration register is set to the best value, which eliminates
 * truncation errors on calculating current register in hardware.
 * According to datasheet (eq. 3) the best values are 2048 for
 * ina226 and 4096 for ina219. They are hardcoded as calibration_value.
 */
static int ina2xx_calibrate(struct ina2xx_data *data)
{
	return regmap_write(data->regmap, INA2XX_CALIBRATION,
			    data->config->calibration_value);
}

/*
 * Initialize the configuration and calibration registers.
 */
static int ina2xx_init(struct ina2xx_data *data)
{
	int ret = regmap_write(data->regmap, INA2XX_CONFIG,
			       data->config->config_default);
	if (ret < 0)
		return ret;

	return ina2xx_calibrate(data);
}

static int ina2xx_read_reg(struct device *dev, int reg, unsigned int *regval)
{
	struct ina2xx_data *data = dev_get_drvdata(dev);
	int ret, retry;

	dev_dbg(dev, "Starting register %d read\n", reg);

	for (retry = 5; retry; retry--) {

		ret = regmap_read(data->regmap, reg, regval);
		if (ret < 0)
			return ret;

		dev_dbg(dev, "read %d, val = 0x%04x\n", reg, *regval);

		/*
		 * If the current value in the calibration register is 0, the
		 * power and current registers will also remain at 0. In case
		 * the chip has been reset let's check the calibration
		 * register and reinitialize if needed.
		 * We do that extra read of the calibration register if there
		 * is some hint of a chip reset.
		 */
		if (*regval == 0) {
			unsigned int cal;

			ret = regmap_read(data->regmap, INA2XX_CALIBRATION,
					  &cal);
			if (ret < 0)
				return ret;

			if (cal == 0) {
				dev_warn(dev, "chip not calibrated, reinitializing\n");

				ret = ina2xx_init(data);
				if (ret < 0)
					return ret;
				/*
				 * Let's make sure the power and current
				 * registers have been updated before trying
				 * again.
				 */
				msleep(INA2XX_MAX_DELAY);
				continue;
			}
		}
		return 0;
	}

	/*
	 * If we're here then although all write operations succeeded, the
	 * chip still returns 0 in the calibration register. Nothing more we
	 * can do here.
	 */
	dev_err(dev, "unable to reinitialize the chip\n");
	return -ENODEV;
}

static int ina2xx_get_value(struct ina2xx_data *data, u8 reg,
			    unsigned int regval)
{
	int val;

	switch (reg) {
	case INA2XX_SHUNT_VOLTAGE:
		/* signed register */
		val = DIV_ROUND_CLOSEST((s16)regval, data->config->shunt_div);
		break;
	case INA2XX_BUS_VOLTAGE:
		val = (regval >> data->config->bus_voltage_shift)
		  * data->config->bus_voltage_lsb;
		val = DIV_ROUND_CLOSEST(val, 1000);
		break;
	case INA2XX_POWER:
		val = regval * data->power_lsb_uW;
		break;
	case INA2XX_CURRENT:
		/* signed register, result in mA */
		val = (s16)regval * data->current_lsb_uA;
		val = DIV_ROUND_CLOSEST(val, 1000);
		break;
	case INA2XX_CALIBRATION:
		val = regval;
		break;
	default:
		/* programmer goofed */
		WARN_ON_ONCE(1);
		val = 0;
		break;
	}

	return val;
}

static ssize_t ina2xx_value_show(struct device *dev,
				 struct device_attribute *da, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
	struct ina2xx_data *data = dev_get_drvdata(dev);
	unsigned int regval;

	int err = ina2xx_read_reg(dev, attr->index, &regval);

	if (err < 0)
		return err;

	return snprintf(buf, PAGE_SIZE, "%d\n",
			ina2xx_get_value(data, attr->index, regval));
}

static int ina226_reg_to_alert(struct ina2xx_data *data, u8 bit, u16 regval)
{
	int reg;

	switch (bit) {
	case INA226_SHUNT_OVER_VOLTAGE_BIT:
	case INA226_SHUNT_UNDER_VOLTAGE_BIT:
		reg = INA2XX_SHUNT_VOLTAGE;
		break;
	case INA226_BUS_OVER_VOLTAGE_BIT:
	case INA226_BUS_UNDER_VOLTAGE_BIT:
		reg = INA2XX_BUS_VOLTAGE;
		break;
	case INA226_POWER_OVER_LIMIT_BIT:
		reg = INA2XX_POWER;
		break;
	default:
		/* programmer goofed */
		WARN_ON_ONCE(1);
		return 0;
	}

	return ina2xx_get_value(data, reg, regval);
}

/*
 * Turns alert limit values into register values.
 * Opposite of the formula in ina2xx_get_value().
 */
static s16 ina226_alert_to_reg(struct ina2xx_data *data, u8 bit, int val)
{
	switch (bit) {
	case INA226_SHUNT_OVER_VOLTAGE_BIT:
	case INA226_SHUNT_UNDER_VOLTAGE_BIT:
		val *= data->config->shunt_div;
		return clamp_val(val, SHRT_MIN, SHRT_MAX);
	case INA226_BUS_OVER_VOLTAGE_BIT:
	case INA226_BUS_UNDER_VOLTAGE_BIT:
		val = (val * 1000) << data->config->bus_voltage_shift;
		val = DIV_ROUND_CLOSEST(val, data->config->bus_voltage_lsb);
		return clamp_val(val, 0, SHRT_MAX);
	case INA226_POWER_OVER_LIMIT_BIT:
		val = DIV_ROUND_CLOSEST(val, data->power_lsb_uW);
		return clamp_val(val, 0, USHRT_MAX);
	default:
		/* programmer goofed */
		WARN_ON_ONCE(1);
		return 0;
	}
}

static ssize_t ina226_alert_show(struct device *dev,
				 struct device_attribute *da, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
	struct ina2xx_data *data = dev_get_drvdata(dev);
	int regval;
	int val = 0;
	int ret;

	mutex_lock(&data->config_lock);
	ret = regmap_read(data->regmap, INA226_MASK_ENABLE, &regval);
	if (ret)
		goto abort;

	if (regval & BIT(attr->index)) {
		ret = regmap_read(data->regmap, INA226_ALERT_LIMIT, &regval);
		if (ret)
			goto abort;
		val = ina226_reg_to_alert(data, attr->index, regval);
	}

	ret = snprintf(buf, PAGE_SIZE, "%d\n", val);
abort:
	mutex_unlock(&data->config_lock);
	return ret;
}

static ssize_t ina226_alert_store(struct device *dev,
				  struct device_attribute *da,
				  const char *buf, size_t count)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
	struct ina2xx_data *data = dev_get_drvdata(dev);
	unsigned long val;
	int ret;

	ret = kstrtoul(buf, 10, &val);
	if (ret < 0)
		return ret;

	/*
	 * Clear all alerts first to avoid accidentally triggering ALERT pin
	 * due to register write sequence. Then, only enable the alert
	 * if the value is non-zero.
	 */
	mutex_lock(&data->config_lock);
	ret = regmap_update_bits(data->regmap, INA226_MASK_ENABLE,
				 INA226_ALERT_CONFIG_MASK, 0);
	if (ret < 0)
		goto abort;

	ret = regmap_write(data->regmap, INA226_ALERT_LIMIT,
			   ina226_alert_to_reg(data, attr->index, val));
	if (ret < 0)
		goto abort;

	if (val != 0) {
		ret = regmap_update_bits(data->regmap, INA226_MASK_ENABLE,
					 INA226_ALERT_CONFIG_MASK,
					 BIT(attr->index));
		if (ret < 0)
			goto abort;
	}

	ret = count;
abort:
	mutex_unlock(&data->config_lock);
	return ret;
}

static ssize_t ina226_alarm_show(struct device *dev,
				 struct device_attribute *da, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
	struct ina2xx_data *data = dev_get_drvdata(dev);
	int regval;
	int alarm = 0;
	int ret;

	ret = regmap_read(data->regmap, INA226_MASK_ENABLE, &regval);
	if (ret)
		return ret;

	alarm = (regval & BIT(attr->index)) &&
		(regval & INA226_ALERT_FUNCTION_FLAG);
	return snprintf(buf, PAGE_SIZE, "%d\n", alarm);
}

/*
 * In order to keep calibration register value fixed, the product
 * of current_lsb and shunt_resistor should also be fixed and equal
 * to shunt_voltage_lsb = 1 / shunt_div multiplied by 10^9 in order
 * to keep the scale.
 */
static int ina2xx_set_shunt(struct ina2xx_data *data, long val)
{
	unsigned int dividend = DIV_ROUND_CLOSEST(1000000000,
						  data->config->shunt_div);
	if (val <= 0 || val > dividend)
		return -EINVAL;

	mutex_lock(&data->config_lock);
	data->rshunt = val;
	data->current_lsb_uA = DIV_ROUND_CLOSEST(dividend, val);
	data->power_lsb_uW = data->config->power_lsb_factor *
			     data->current_lsb_uA;
	mutex_unlock(&data->config_lock);

	return 0;
}

static ssize_t ina2xx_shunt_show(struct device *dev,
				 struct device_attribute *da, char *buf)
{
	struct ina2xx_data *data = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%li\n", data->rshunt);
}

static ssize_t ina2xx_shunt_store(struct device *dev,
				  struct device_attribute *da,
				  const char *buf, size_t count)
{
	unsigned long val;
	int status;
	struct ina2xx_data *data = dev_get_drvdata(dev);

	status = kstrtoul(buf, 10, &val);
	if (status < 0)
		return status;

	status = ina2xx_set_shunt(data, val);
	if (status < 0)
		return status;
	return count;
}

static ssize_t ina226_interval_store(struct device *dev,
				     struct device_attribute *da,
				     const char *buf, size_t count)
{
	struct ina2xx_data *data = dev_get_drvdata(dev);
	unsigned long val;
	int status;

	status = kstrtoul(buf, 10, &val);
	if (status < 0)
		return status;

	if (val > INT_MAX || val == 0)
		return -EINVAL;

	status = regmap_update_bits(data->regmap, INA2XX_CONFIG,
				    INA226_AVG_RD_MASK,
				    ina226_interval_to_reg(val));
	if (status < 0)
		return status;

	return count;
}

static ssize_t ina226_interval_show(struct device *dev,
				    struct device_attribute *da, char *buf)
{
	struct ina2xx_data *data = dev_get_drvdata(dev);
	int status;
	unsigned int regval;

	status = regmap_read(data->regmap, INA2XX_CONFIG, &regval);
	if (status)
		return status;

	return snprintf(buf, PAGE_SIZE, "%d\n", ina226_reg_to_interval(regval));
}

/* shunt voltage */
static SENSOR_DEVICE_ATTR_RO(in0_input, ina2xx_value, INA2XX_SHUNT_VOLTAGE);
/* shunt voltage over/under voltage alert setting and alarm */
static SENSOR_DEVICE_ATTR_RW(in0_crit, ina226_alert,
			     INA226_SHUNT_OVER_VOLTAGE_BIT);
static SENSOR_DEVICE_ATTR_RW(in0_lcrit, ina226_alert,
			     INA226_SHUNT_UNDER_VOLTAGE_BIT);
static SENSOR_DEVICE_ATTR_RO(in0_crit_alarm, ina226_alarm,
			     INA226_SHUNT_OVER_VOLTAGE_BIT);
static SENSOR_DEVICE_ATTR_RO(in0_lcrit_alarm, ina226_alarm,
			     INA226_SHUNT_UNDER_VOLTAGE_BIT);

/* bus voltage */
static SENSOR_DEVICE_ATTR_RO(in1_input, ina2xx_value, INA2XX_BUS_VOLTAGE);
/* bus voltage over/under voltage alert setting and alarm */
static SENSOR_DEVICE_ATTR_RW(in1_crit, ina226_alert,
			     INA226_BUS_OVER_VOLTAGE_BIT);
static SENSOR_DEVICE_ATTR_RW(in1_lcrit, ina226_alert,
			     INA226_BUS_UNDER_VOLTAGE_BIT);
static SENSOR_DEVICE_ATTR_RO(in1_crit_alarm, ina226_alarm,
			     INA226_BUS_OVER_VOLTAGE_BIT);
static SENSOR_DEVICE_ATTR_RO(in1_lcrit_alarm, ina226_alarm,
			     INA226_BUS_UNDER_VOLTAGE_BIT);

/* calculated current */
static SENSOR_DEVICE_ATTR_RO(curr1_input, ina2xx_value, INA2XX_CURRENT);

/* calculated power */
static SENSOR_DEVICE_ATTR_RO(power1_input, ina2xx_value, INA2XX_POWER);
/* over-limit power alert setting and alarm */
static SENSOR_DEVICE_ATTR_RW(power1_crit, ina226_alert,
			     INA226_POWER_OVER_LIMIT_BIT);
static SENSOR_DEVICE_ATTR_RO(power1_crit_alarm, ina226_alarm,
			     INA226_POWER_OVER_LIMIT_BIT);

/* shunt resistance */
static SENSOR_DEVICE_ATTR_RW(shunt_resistor, ina2xx_shunt, INA2XX_CALIBRATION);

/* update interval (ina226 only) */
static SENSOR_DEVICE_ATTR_RW(update_interval, ina226_interval, 0);

/* pointers to created device attributes */
static struct attribute *ina2xx_attrs[] = {
	&sensor_dev_attr_in0_input.dev_attr.attr,
	&sensor_dev_attr_in1_input.dev_attr.attr,
	&sensor_dev_attr_curr1_input.dev_attr.attr,
	&sensor_dev_attr_power1_input.dev_attr.attr,
	&sensor_dev_attr_shunt_resistor.dev_attr.attr,
	NULL,
};

static const struct attribute_group ina2xx_group = {
	.attrs = ina2xx_attrs,
};

static struct attribute *ina226_attrs[] = {
	&sensor_dev_attr_in0_crit.dev_attr.attr,
	&sensor_dev_attr_in0_lcrit.dev_attr.attr,
	&sensor_dev_attr_in0_crit_alarm.dev_attr.attr,
	&sensor_dev_attr_in0_lcrit_alarm.dev_attr.attr,
	&sensor_dev_attr_in1_crit.dev_attr.attr,
	&sensor_dev_attr_in1_lcrit.dev_attr.attr,
	&sensor_dev_attr_in1_crit_alarm.dev_attr.attr,
	&sensor_dev_attr_in1_lcrit_alarm.dev_attr.attr,
	&sensor_dev_attr_power1_crit.dev_attr.attr,
	&sensor_dev_attr_power1_crit_alarm.dev_attr.attr,
	&sensor_dev_attr_update_interval.dev_attr.attr,
	NULL,
};

static const struct attribute_group ina226_group = {
	.attrs = ina226_attrs,
};

static int ina2xx_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct ina2xx_data *data;
	struct device *hwmon_dev;
	u32 val;
	int ret, group = 0;
	enum ina2xx_ids chip;

	if (client->dev.of_node)
		chip = (enum ina2xx_ids)of_device_get_match_data(&client->dev);
	else
		chip = id->driver_data;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	/* set the device type */
	data->config = &ina2xx_config[chip];
	mutex_init(&data->config_lock);

	if (of_property_read_u32(dev->of_node, "shunt-resistor", &val) < 0) {
		struct ina2xx_platform_data *pdata = dev_get_platdata(dev);

		if (pdata)
			val = pdata->shunt_uohms;
		else
			val = INA2XX_RSHUNT_DEFAULT;
	}

	ina2xx_set_shunt(data, val);

	ina2xx_regmap_config.max_register = data->config->registers;

	data->regmap = devm_regmap_init_i2c(client, &ina2xx_regmap_config);
	if (IS_ERR(data->regmap)) {
		dev_err(dev, "failed to allocate register map\n");
		return PTR_ERR(data->regmap);
	}

	ret = ina2xx_init(data);
	if (ret < 0) {
		dev_err(dev, "error configuring the device: %d\n", ret);
		return -ENODEV;
	}

	data->groups[group++] = &ina2xx_group;
	if (chip == ina226)
		data->groups[group++] = &ina226_group;

	hwmon_dev = devm_hwmon_device_register_with_groups(dev, client->name,
							   data, data->groups);
	if (IS_ERR(hwmon_dev))
		return PTR_ERR(hwmon_dev);

	dev_info(dev, "power monitor %s (Rshunt = %li uOhm)\n",
		 client->name, data->rshunt);

	return 0;
}

static const struct i2c_device_id ina2xx_id[] = {
	{ "ina219", ina219 },
	{ "ina220", ina219 },
	{ "ina226", ina226 },
	{ "ina230", ina226 },
	{ "ina231", ina226 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ina2xx_id);

static const struct of_device_id __maybe_unused ina2xx_of_match[] = {
	{
		.compatible = "ti,ina219",
		.data = (void *)ina219
	},
	{
		.compatible = "ti,ina220",
		.data = (void *)ina219
	},
	{
		.compatible = "ti,ina226",
		.data = (void *)ina226
	},
	{
		.compatible = "ti,ina230",
		.data = (void *)ina226
	},
	{
		.compatible = "ti,ina231",
		.data = (void *)ina226
	},
	{ },
};
MODULE_DEVICE_TABLE(of, ina2xx_of_match);

static struct i2c_driver ina2xx_driver = {
	.driver = {
		.name	= "ina2xx",
		.of_match_table = of_match_ptr(ina2xx_of_match),
	},
	.probe		= ina2xx_probe,
	.id_table	= ina2xx_id,
};

module_i2c_driver(ina2xx_driver);

MODULE_AUTHOR("Lothar Felten <l-felten@ti.com>");
MODULE_DESCRIPTION("ina2xx driver");
MODULE_LICENSE("GPL");
