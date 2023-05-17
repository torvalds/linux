// SPDX-License-Identifier: GPL-2.0
/*
 * Lochnagar hardware monitoring features
 *
 * Copyright (c) 2016-2019 Cirrus Logic, Inc. and
 *                         Cirrus Logic International Semiconductor Ltd.
 *
 * Author: Lucas Tanure <tanureal@opensource.cirrus.com>
 */

#include <linux/delay.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/math64.h>
#include <linux/mfd/lochnagar.h>
#include <linux/mfd/lochnagar2_regs.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#define LN2_MAX_NSAMPLE 1023
#define LN2_SAMPLE_US   1670

#define LN2_CURR_UNITS  1000
#define LN2_VOLT_UNITS  1000
#define LN2_TEMP_UNITS  1000
#define LN2_PWR_UNITS   1000000

static const char * const lochnagar_chan_names[] = {
	"DBVDD1",
	"1V8 DSP",
	"1V8 CDC",
	"VDDCORE DSP",
	"AVDD 1V8",
	"SYSVDD",
	"VDDCORE CDC",
	"MICVDD",
};

struct lochnagar_hwmon {
	struct regmap *regmap;

	long power_nsamples[ARRAY_SIZE(lochnagar_chan_names)];

	/* Lock to ensure only a single sensor is read at a time */
	struct mutex sensor_lock;
};

enum lochnagar_measure_mode {
	LN2_CURR = 0,
	LN2_VOLT,
	LN2_TEMP,
};

/**
 * float_to_long - Convert ieee754 reading from hardware to an integer
 *
 * @data: Value read from the hardware
 * @precision: Units to multiply up to eg. 1000 = milli, 1000000 = micro
 *
 * Return: Converted integer reading
 *
 * Depending on the measurement type the hardware returns an ieee754
 * floating point value in either volts, amps or celsius. This function
 * will convert that into an integer in a smaller unit such as micro-amps
 * or milli-celsius. The hardware does not return NaN, so consideration of
 * that is not required.
 */
static long float_to_long(u32 data, u32 precision)
{
	u64 man = data & 0x007FFFFF;
	int exp = ((data & 0x7F800000) >> 23) - 127 - 23;
	bool negative = data & 0x80000000;
	long result;

	man = (man + (1 << 23)) * precision;

	if (fls64(man) + exp > (int)sizeof(long) * 8 - 1)
		result = LONG_MAX;
	else if (exp < 0)
		result = (man + (1ull << (-exp - 1))) >> -exp;
	else
		result = man << exp;

	return negative ? -result : result;
}

static int do_measurement(struct regmap *regmap, int chan,
			  enum lochnagar_measure_mode mode, int nsamples)
{
	unsigned int val;
	int ret;

	chan = 1 << (chan + LOCHNAGAR2_IMON_MEASURED_CHANNELS_SHIFT);

	ret = regmap_write(regmap, LOCHNAGAR2_IMON_CTRL1,
			   LOCHNAGAR2_IMON_ENA_MASK | chan | mode);
	if (ret < 0)
		return ret;

	ret = regmap_write(regmap, LOCHNAGAR2_IMON_CTRL2, nsamples);
	if (ret < 0)
		return ret;

	ret = regmap_write(regmap, LOCHNAGAR2_IMON_CTRL3,
			   LOCHNAGAR2_IMON_CONFIGURE_MASK);
	if (ret < 0)
		return ret;

	ret =  regmap_read_poll_timeout(regmap, LOCHNAGAR2_IMON_CTRL3, val,
					val & LOCHNAGAR2_IMON_DONE_MASK,
					1000, 10000);
	if (ret < 0)
		return ret;

	ret = regmap_write(regmap, LOCHNAGAR2_IMON_CTRL3,
			   LOCHNAGAR2_IMON_MEASURE_MASK);
	if (ret < 0)
		return ret;

	/*
	 * Actual measurement time is ~1.67mS per sample, approximate this
	 * with a 1.5mS per sample msleep and then poll for success up to
	 * ~0.17mS * 1023 (LN2_MAX_NSAMPLES). Normally for smaller values
	 * of nsamples the poll will complete on the first loop due to
	 * other latency in the system.
	 */
	msleep((nsamples * 3) / 2);

	ret =  regmap_read_poll_timeout(regmap, LOCHNAGAR2_IMON_CTRL3, val,
					val & LOCHNAGAR2_IMON_DONE_MASK,
					5000, 200000);
	if (ret < 0)
		return ret;

	return regmap_write(regmap, LOCHNAGAR2_IMON_CTRL3, 0);
}

static int request_data(struct regmap *regmap, int chan, u32 *data)
{
	unsigned int val;
	int ret;

	ret = regmap_write(regmap, LOCHNAGAR2_IMON_CTRL4,
			   LOCHNAGAR2_IMON_DATA_REQ_MASK |
			   chan << LOCHNAGAR2_IMON_CH_SEL_SHIFT);
	if (ret < 0)
		return ret;

	ret =  regmap_read_poll_timeout(regmap, LOCHNAGAR2_IMON_CTRL4, val,
					val & LOCHNAGAR2_IMON_DATA_RDY_MASK,
					1000, 10000);
	if (ret < 0)
		return ret;

	ret = regmap_read(regmap, LOCHNAGAR2_IMON_DATA1, &val);
	if (ret < 0)
		return ret;

	*data = val << 16;

	ret = regmap_read(regmap, LOCHNAGAR2_IMON_DATA2, &val);
	if (ret < 0)
		return ret;

	*data |= val;

	return regmap_write(regmap, LOCHNAGAR2_IMON_CTRL4, 0);
}

static int read_sensor(struct device *dev, int chan,
		       enum lochnagar_measure_mode mode, int nsamples,
		       unsigned int precision, long *val)
{
	struct lochnagar_hwmon *priv = dev_get_drvdata(dev);
	struct regmap *regmap = priv->regmap;
	u32 data;
	int ret;

	mutex_lock(&priv->sensor_lock);

	ret = do_measurement(regmap, chan, mode, nsamples);
	if (ret < 0) {
		dev_err(dev, "Failed to perform measurement: %d\n", ret);
		goto error;
	}

	ret = request_data(regmap, chan, &data);
	if (ret < 0) {
		dev_err(dev, "Failed to read measurement: %d\n", ret);
		goto error;
	}

	*val = float_to_long(data, precision);

error:
	mutex_unlock(&priv->sensor_lock);

	return ret;
}

static int read_power(struct device *dev, int chan, long *val)
{
	struct lochnagar_hwmon *priv = dev_get_drvdata(dev);
	int nsamples = priv->power_nsamples[chan];
	u64 power;
	int ret;

	if (!strcmp("SYSVDD", lochnagar_chan_names[chan])) {
		power = 5 * LN2_PWR_UNITS;
	} else {
		ret = read_sensor(dev, chan, LN2_VOLT, 1, LN2_PWR_UNITS, val);
		if (ret < 0)
			return ret;

		power = abs(*val);
	}

	ret = read_sensor(dev, chan, LN2_CURR, nsamples, LN2_PWR_UNITS, val);
	if (ret < 0)
		return ret;

	power *= abs(*val);
	power = DIV_ROUND_CLOSEST_ULL(power, LN2_PWR_UNITS);

	if (power > LONG_MAX)
		*val = LONG_MAX;
	else
		*val = power;

	return 0;
}

static umode_t lochnagar_is_visible(const void *drvdata,
				    enum hwmon_sensor_types type,
				    u32 attr, int chan)
{
	switch (type) {
	case hwmon_in:
		if (!strcmp("SYSVDD", lochnagar_chan_names[chan]))
			return 0;
		break;
	case hwmon_power:
		if (attr == hwmon_power_average_interval)
			return 0644;
		break;
	default:
		break;
	}

	return 0444;
}

static int lochnagar_read(struct device *dev, enum hwmon_sensor_types type,
			  u32 attr, int chan, long *val)
{
	struct lochnagar_hwmon *priv = dev_get_drvdata(dev);
	int interval;

	switch (type) {
	case hwmon_in:
		return read_sensor(dev, chan, LN2_VOLT, 1, LN2_VOLT_UNITS, val);
	case hwmon_curr:
		return read_sensor(dev, chan, LN2_CURR, 1, LN2_CURR_UNITS, val);
	case hwmon_temp:
		return read_sensor(dev, chan, LN2_TEMP, 1, LN2_TEMP_UNITS, val);
	case hwmon_power:
		switch (attr) {
		case hwmon_power_average:
			return read_power(dev, chan, val);
		case hwmon_power_average_interval:
			interval = priv->power_nsamples[chan] * LN2_SAMPLE_US;
			*val = DIV_ROUND_CLOSEST(interval, 1000);
			return 0;
		default:
			return -EOPNOTSUPP;
		}
	default:
		return -EOPNOTSUPP;
	}
}

static int lochnagar_read_string(struct device *dev,
				 enum hwmon_sensor_types type, u32 attr,
				 int chan, const char **str)
{
	switch (type) {
	case hwmon_in:
	case hwmon_curr:
	case hwmon_power:
		*str = lochnagar_chan_names[chan];
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static int lochnagar_write(struct device *dev, enum hwmon_sensor_types type,
			   u32 attr, int chan, long val)
{
	struct lochnagar_hwmon *priv = dev_get_drvdata(dev);

	if (type != hwmon_power || attr != hwmon_power_average_interval)
		return -EOPNOTSUPP;

	val = clamp_t(long, val, 1, (LN2_MAX_NSAMPLE * LN2_SAMPLE_US) / 1000);
	val = DIV_ROUND_CLOSEST(val * 1000, LN2_SAMPLE_US);

	priv->power_nsamples[chan] = val;

	return 0;
}

static const struct hwmon_ops lochnagar_ops = {
	.is_visible = lochnagar_is_visible,
	.read = lochnagar_read,
	.read_string = lochnagar_read_string,
	.write = lochnagar_write,
};

static const struct hwmon_channel_info * const lochnagar_info[] = {
	HWMON_CHANNEL_INFO(temp,  HWMON_T_INPUT),
	HWMON_CHANNEL_INFO(in,    HWMON_I_INPUT | HWMON_I_LABEL,
				  HWMON_I_INPUT | HWMON_I_LABEL,
				  HWMON_I_INPUT | HWMON_I_LABEL,
				  HWMON_I_INPUT | HWMON_I_LABEL,
				  HWMON_I_INPUT | HWMON_I_LABEL,
				  HWMON_I_INPUT | HWMON_I_LABEL,
				  HWMON_I_INPUT | HWMON_I_LABEL,
				  HWMON_I_INPUT | HWMON_I_LABEL),
	HWMON_CHANNEL_INFO(curr,  HWMON_C_INPUT | HWMON_C_LABEL,
				  HWMON_C_INPUT | HWMON_C_LABEL,
				  HWMON_C_INPUT | HWMON_C_LABEL,
				  HWMON_C_INPUT | HWMON_C_LABEL,
				  HWMON_C_INPUT | HWMON_C_LABEL,
				  HWMON_C_INPUT | HWMON_C_LABEL,
				  HWMON_C_INPUT | HWMON_C_LABEL,
				  HWMON_C_INPUT | HWMON_C_LABEL),
	HWMON_CHANNEL_INFO(power, HWMON_P_AVERAGE | HWMON_P_AVERAGE_INTERVAL |
				  HWMON_P_LABEL,
				  HWMON_P_AVERAGE | HWMON_P_AVERAGE_INTERVAL |
				  HWMON_P_LABEL,
				  HWMON_P_AVERAGE | HWMON_P_AVERAGE_INTERVAL |
				  HWMON_P_LABEL,
				  HWMON_P_AVERAGE | HWMON_P_AVERAGE_INTERVAL |
				  HWMON_P_LABEL,
				  HWMON_P_AVERAGE | HWMON_P_AVERAGE_INTERVAL |
				  HWMON_P_LABEL,
				  HWMON_P_AVERAGE | HWMON_P_AVERAGE_INTERVAL |
				  HWMON_P_LABEL,
				  HWMON_P_AVERAGE | HWMON_P_AVERAGE_INTERVAL |
				  HWMON_P_LABEL,
				  HWMON_P_AVERAGE | HWMON_P_AVERAGE_INTERVAL |
				  HWMON_P_LABEL),
	NULL
};

static const struct hwmon_chip_info lochnagar_chip_info = {
	.ops = &lochnagar_ops,
	.info = lochnagar_info,
};

static const struct of_device_id lochnagar_of_match[] = {
	{ .compatible = "cirrus,lochnagar2-hwmon" },
	{}
};
MODULE_DEVICE_TABLE(of, lochnagar_of_match);

static int lochnagar_hwmon_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device *hwmon_dev;
	struct lochnagar_hwmon *priv;
	int i;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	mutex_init(&priv->sensor_lock);

	priv->regmap = dev_get_regmap(dev->parent, NULL);
	if (!priv->regmap) {
		dev_err(dev, "No register map found\n");
		return -EINVAL;
	}

	for (i = 0; i < ARRAY_SIZE(priv->power_nsamples); i++)
		priv->power_nsamples[i] = 96;

	hwmon_dev = devm_hwmon_device_register_with_info(dev, "Lochnagar", priv,
							 &lochnagar_chip_info,
							 NULL);

	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static struct platform_driver lochnagar_hwmon_driver = {
	.driver = {
		.name = "lochnagar-hwmon",
		.of_match_table = lochnagar_of_match,
	},
	.probe = lochnagar_hwmon_probe,
};
module_platform_driver(lochnagar_hwmon_driver);

MODULE_AUTHOR("Lucas Tanure <tanureal@opensource.cirrus.com>");
MODULE_DESCRIPTION("Lochnagar hardware monitoring features");
MODULE_LICENSE("GPL");
