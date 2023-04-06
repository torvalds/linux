// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * adt7x10.c - Part of lm_sensors, Linux kernel modules for hardware
 *	 monitoring
 * This driver handles the ADT7410 and compatible digital temperature sensors.
 * Hartmut Knaack <knaack.h@gmx.de> 2012-07-22
 * based on lm75.c by Frodo Looijaard <frodol@dds.nl>
 * and adt7410.c from iio-staging by Sonic Zhang <sonic.zhang@analog.com>
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/hwmon.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/regmap.h>

#include "adt7x10.h"

/*
 * ADT7X10 status
 */
#define ADT7X10_STAT_T_LOW		(1 << 4)
#define ADT7X10_STAT_T_HIGH		(1 << 5)
#define ADT7X10_STAT_T_CRIT		(1 << 6)
#define ADT7X10_STAT_NOT_RDY		(1 << 7)

/*
 * ADT7X10 config
 */
#define ADT7X10_FAULT_QUEUE_MASK	(1 << 0 | 1 << 1)
#define ADT7X10_CT_POLARITY		(1 << 2)
#define ADT7X10_INT_POLARITY		(1 << 3)
#define ADT7X10_EVENT_MODE		(1 << 4)
#define ADT7X10_MODE_MASK		(1 << 5 | 1 << 6)
#define ADT7X10_FULL			(0 << 5 | 0 << 6)
#define ADT7X10_PD			(1 << 5 | 1 << 6)
#define ADT7X10_RESOLUTION		(1 << 7)

/*
 * ADT7X10 masks
 */
#define ADT7X10_T13_VALUE_MASK		0xFFF8
#define ADT7X10_T_HYST_MASK		0xF

/* straight from the datasheet */
#define ADT7X10_TEMP_MIN (-55000)
#define ADT7X10_TEMP_MAX 150000

/* Each client has this additional data */
struct adt7x10_data {
	struct regmap		*regmap;
	struct mutex		update_lock;
	u8			config;
	u8			oldconfig;
	bool			valid;		/* true if temperature valid */
};

enum {
	adt7x10_temperature = 0,
	adt7x10_t_alarm_high,
	adt7x10_t_alarm_low,
	adt7x10_t_crit,
};

static const u8 ADT7X10_REG_TEMP[] = {
	[adt7x10_temperature] = ADT7X10_TEMPERATURE,		/* input */
	[adt7x10_t_alarm_high] = ADT7X10_T_ALARM_HIGH,		/* high */
	[adt7x10_t_alarm_low] = ADT7X10_T_ALARM_LOW,		/* low */
	[adt7x10_t_crit] = ADT7X10_T_CRIT,			/* critical */
};

static irqreturn_t adt7x10_irq_handler(int irq, void *private)
{
	struct device *dev = private;
	struct adt7x10_data *d = dev_get_drvdata(dev);
	unsigned int status;
	int ret;

	ret = regmap_read(d->regmap, ADT7X10_STATUS, &status);
	if (ret < 0)
		return IRQ_HANDLED;

	if (status & ADT7X10_STAT_T_HIGH)
		hwmon_notify_event(dev, hwmon_temp, hwmon_temp_max_alarm, 0);
	if (status & ADT7X10_STAT_T_LOW)
		hwmon_notify_event(dev, hwmon_temp, hwmon_temp_min_alarm, 0);
	if (status & ADT7X10_STAT_T_CRIT)
		hwmon_notify_event(dev, hwmon_temp, hwmon_temp_crit_alarm, 0);

	return IRQ_HANDLED;
}

static int adt7x10_temp_ready(struct regmap *regmap)
{
	unsigned int status;
	int i, ret;

	for (i = 0; i < 6; i++) {
		ret = regmap_read(regmap, ADT7X10_STATUS, &status);
		if (ret < 0)
			return ret;
		if (!(status & ADT7X10_STAT_NOT_RDY))
			return 0;
		msleep(60);
	}
	return -ETIMEDOUT;
}

static s16 ADT7X10_TEMP_TO_REG(long temp)
{
	return DIV_ROUND_CLOSEST(clamp_val(temp, ADT7X10_TEMP_MIN,
					   ADT7X10_TEMP_MAX) * 128, 1000);
}

static int ADT7X10_REG_TO_TEMP(struct adt7x10_data *data, s16 reg)
{
	/* in 13 bit mode, bits 0-2 are status flags - mask them out */
	if (!(data->config & ADT7X10_RESOLUTION))
		reg &= ADT7X10_T13_VALUE_MASK;
	/*
	 * temperature is stored in twos complement format, in steps of
	 * 1/128°C
	 */
	return DIV_ROUND_CLOSEST(reg * 1000, 128);
}

/*-----------------------------------------------------------------------*/

static int adt7x10_temp_read(struct adt7x10_data *data, int index, long *val)
{
	unsigned int regval;
	int ret;

	mutex_lock(&data->update_lock);
	if (index == adt7x10_temperature && !data->valid) {
		/* wait for valid temperature */
		ret = adt7x10_temp_ready(data->regmap);
		if (ret) {
			mutex_unlock(&data->update_lock);
			return ret;
		}
		data->valid = true;
	}
	mutex_unlock(&data->update_lock);

	ret = regmap_read(data->regmap, ADT7X10_REG_TEMP[index], &regval);
	if (ret)
		return ret;

	*val = ADT7X10_REG_TO_TEMP(data, regval);
	return 0;
}

static int adt7x10_temp_write(struct adt7x10_data *data, int index, long temp)
{
	int ret;

	mutex_lock(&data->update_lock);
	ret = regmap_write(data->regmap, ADT7X10_REG_TEMP[index],
			   ADT7X10_TEMP_TO_REG(temp));
	mutex_unlock(&data->update_lock);
	return ret;
}

static int adt7x10_hyst_read(struct adt7x10_data *data, int index, long *val)
{
	int hyst, temp, ret;

	mutex_lock(&data->update_lock);
	ret = regmap_read(data->regmap, ADT7X10_T_HYST, &hyst);
	if (ret) {
		mutex_unlock(&data->update_lock);
		return ret;
	}

	ret = regmap_read(data->regmap, ADT7X10_REG_TEMP[index], &temp);
	mutex_unlock(&data->update_lock);
	if (ret)
		return ret;

	hyst = (hyst & ADT7X10_T_HYST_MASK) * 1000;

	/*
	 * hysteresis is stored as a 4 bit offset in the device, convert it
	 * to an absolute value
	 */
	/* min has positive offset, others have negative */
	if (index == adt7x10_t_alarm_low)
		hyst = -hyst;

	*val = ADT7X10_REG_TO_TEMP(data, temp) - hyst;
	return 0;
}

static int adt7x10_hyst_write(struct adt7x10_data *data, long hyst)
{
	unsigned int regval;
	int limit, ret;

	mutex_lock(&data->update_lock);

	/* convert absolute hysteresis value to a 4 bit delta value */
	ret = regmap_read(data->regmap, ADT7X10_T_ALARM_HIGH, &regval);
	if (ret < 0)
		goto abort;

	limit = ADT7X10_REG_TO_TEMP(data, regval);

	hyst = clamp_val(hyst, ADT7X10_TEMP_MIN, ADT7X10_TEMP_MAX);
	regval = clamp_val(DIV_ROUND_CLOSEST(limit - hyst, 1000), 0,
			   ADT7X10_T_HYST_MASK);
	ret = regmap_write(data->regmap, ADT7X10_T_HYST, regval);
abort:
	mutex_unlock(&data->update_lock);
	return ret;
}

static int adt7x10_alarm_read(struct adt7x10_data *data, int index, long *val)
{
	unsigned int status;
	int ret;

	ret = regmap_read(data->regmap, ADT7X10_STATUS, &status);
	if (ret < 0)
		return ret;

	*val = !!(status & index);

	return 0;
}

static umode_t adt7x10_is_visible(const void *data,
				  enum hwmon_sensor_types type,
				  u32 attr, int channel)
{
	switch (attr) {
	case hwmon_temp_max:
	case hwmon_temp_min:
	case hwmon_temp_crit:
	case hwmon_temp_max_hyst:
		return 0644;
	case hwmon_temp_input:
	case hwmon_temp_min_alarm:
	case hwmon_temp_max_alarm:
	case hwmon_temp_crit_alarm:
	case hwmon_temp_min_hyst:
	case hwmon_temp_crit_hyst:
		return 0444;
	default:
		break;
	}

	return 0;
}

static int adt7x10_read(struct device *dev, enum hwmon_sensor_types type,
			u32 attr, int channel, long *val)
{
	struct adt7x10_data *data = dev_get_drvdata(dev);

	switch (attr) {
	case hwmon_temp_input:
		return adt7x10_temp_read(data, adt7x10_temperature, val);
	case hwmon_temp_max:
		return adt7x10_temp_read(data, adt7x10_t_alarm_high, val);
	case hwmon_temp_min:
		return adt7x10_temp_read(data, adt7x10_t_alarm_low, val);
	case hwmon_temp_crit:
		return adt7x10_temp_read(data, adt7x10_t_crit, val);
	case hwmon_temp_max_hyst:
		return adt7x10_hyst_read(data, adt7x10_t_alarm_high, val);
	case hwmon_temp_min_hyst:
		return adt7x10_hyst_read(data, adt7x10_t_alarm_low, val);
	case hwmon_temp_crit_hyst:
		return adt7x10_hyst_read(data, adt7x10_t_crit, val);
	case hwmon_temp_min_alarm:
		return adt7x10_alarm_read(data, ADT7X10_STAT_T_LOW, val);
	case hwmon_temp_max_alarm:
		return adt7x10_alarm_read(data, ADT7X10_STAT_T_HIGH, val);
	case hwmon_temp_crit_alarm:
		return adt7x10_alarm_read(data, ADT7X10_STAT_T_CRIT, val);
	default:
		return -EOPNOTSUPP;
	}
}

static int adt7x10_write(struct device *dev, enum hwmon_sensor_types type,
			 u32 attr, int channel, long val)
{
	struct adt7x10_data *data = dev_get_drvdata(dev);

	switch (attr) {
	case hwmon_temp_max:
		return adt7x10_temp_write(data, adt7x10_t_alarm_high, val);
	case hwmon_temp_min:
		return adt7x10_temp_write(data, adt7x10_t_alarm_low, val);
	case hwmon_temp_crit:
		return adt7x10_temp_write(data, adt7x10_t_crit, val);
	case hwmon_temp_max_hyst:
		return adt7x10_hyst_write(data, val);
	default:
		return -EOPNOTSUPP;
	}
}

static const struct hwmon_channel_info * const adt7x10_info[] = {
	HWMON_CHANNEL_INFO(temp, HWMON_T_INPUT | HWMON_T_MAX | HWMON_T_MIN |
			   HWMON_T_CRIT | HWMON_T_MAX_HYST | HWMON_T_MIN_HYST |
			   HWMON_T_CRIT_HYST | HWMON_T_MIN_ALARM |
			   HWMON_T_MAX_ALARM | HWMON_T_CRIT_ALARM),
	NULL,
};

static const struct hwmon_ops adt7x10_hwmon_ops = {
	.is_visible = adt7x10_is_visible,
	.read = adt7x10_read,
	.write = adt7x10_write,
};

static const struct hwmon_chip_info adt7x10_chip_info = {
	.ops = &adt7x10_hwmon_ops,
	.info = adt7x10_info,
};

static void adt7x10_restore_config(void *private)
{
	struct adt7x10_data *data = private;

	regmap_write(data->regmap, ADT7X10_CONFIG, data->oldconfig);
}

int adt7x10_probe(struct device *dev, const char *name, int irq,
		  struct regmap *regmap)
{
	struct adt7x10_data *data;
	unsigned int config;
	struct device *hdev;
	int ret;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->regmap = regmap;

	dev_set_drvdata(dev, data);
	mutex_init(&data->update_lock);

	/* configure as specified */
	ret = regmap_read(regmap, ADT7X10_CONFIG, &config);
	if (ret < 0) {
		dev_dbg(dev, "Can't read config? %d\n", ret);
		return ret;
	}
	data->oldconfig = config;

	/*
	 * Set to 16 bit resolution, continous conversion and comparator mode.
	 */
	data->config = data->oldconfig;
	data->config &= ~(ADT7X10_MODE_MASK | ADT7X10_CT_POLARITY |
			ADT7X10_INT_POLARITY);
	data->config |= ADT7X10_FULL | ADT7X10_RESOLUTION | ADT7X10_EVENT_MODE;

	if (data->config != data->oldconfig) {
		ret = regmap_write(regmap, ADT7X10_CONFIG, data->config);
		if (ret)
			return ret;
		ret = devm_add_action_or_reset(dev, adt7x10_restore_config, data);
		if (ret)
			return ret;
	}
	dev_dbg(dev, "Config %02x\n", data->config);

	hdev = devm_hwmon_device_register_with_info(dev, name, data,
						    &adt7x10_chip_info, NULL);
	if (IS_ERR(hdev))
		return PTR_ERR(hdev);

	if (irq > 0) {
		ret = devm_request_threaded_irq(dev, irq, NULL,
						adt7x10_irq_handler,
						IRQF_TRIGGER_FALLING |
						IRQF_ONESHOT,
						dev_name(dev), hdev);
		if (ret)
			return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(adt7x10_probe);

static int adt7x10_suspend(struct device *dev)
{
	struct adt7x10_data *data = dev_get_drvdata(dev);

	return regmap_write(data->regmap, ADT7X10_CONFIG,
			    data->config | ADT7X10_PD);
}

static int adt7x10_resume(struct device *dev)
{
	struct adt7x10_data *data = dev_get_drvdata(dev);

	return regmap_write(data->regmap, ADT7X10_CONFIG, data->config);
}

EXPORT_SIMPLE_DEV_PM_OPS(adt7x10_dev_pm_ops, adt7x10_suspend, adt7x10_resume);

MODULE_AUTHOR("Hartmut Knaack");
MODULE_DESCRIPTION("ADT7410/ADT7420, ADT7310/ADT7320 common code");
MODULE_LICENSE("GPL");
