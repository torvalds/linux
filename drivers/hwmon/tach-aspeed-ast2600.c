// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) ASPEED Technology Inc.
 */

#include <linux/clk.h>
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
#include <linux/mfd/syscon.h>
#include <linux/sysfs.h>
#include <linux/reset.h>
#include <linux/regmap.h>
#include <linux/bitfield.h>

/* The channel number of Aspeed tach controller */
#define TACH_ASPEED_NR_TACHS 16
/* TACH Control Register */
#define TACH_ASPEED_CTRL(ch) (((ch) * 0x10) + 0x08)
#define TACH_ASPEED_IER BIT(31)
#define TACH_ASPEED_INVERS_LIMIT BIT(30)
#define TACH_ASPEED_LOOPBACK BIT(29)
#define TACH_ASPEED_ENABLE BIT(28)
#define TACH_ASPEED_DEBOUNCE_MASK GENMASK(27, 26)
#define TACH_ASPEED_DEBOUNCE_BIT (26)
#define TACH_ASPEED_IO_EDGE_MASK GENMASK(25, 24)
#define TACH_ASPEED_IO_EDGE_BIT (24)
#define TACH_ASPEED_CLK_DIV_T_MASK GENMASK(23, 20)
#define TACH_ASPEED_CLK_DIV_BIT (20)
#define TACH_ASPEED_THRESHOLD_MASK GENMASK(19, 0)
/* [27:26] */
#define DEBOUNCE_3_CLK 0x00
#define DEBOUNCE_2_CLK 0x01
#define DEBOUNCE_1_CLK 0x02
#define DEBOUNCE_0_CLK 0x03
/* [25:24] */
#define F2F_EDGES 0x00
#define R2R_EDGES 0x01
#define BOTH_EDGES 0x02
/* [23:20] */
/* divisor = 4 to the nth power, n = register value */
#define DEFAULT_TACH_DIV 1024
#define DIV_TO_REG(divisor) (ilog2(divisor) >> 1)

/* TACH Status Register */
#define TACH_ASPEED_STS(ch) (((ch) * 0x10) + 0x0C)

/*PWM_TACH_STS */
#define TACH_ASPEED_ISR BIT(31)
#define TACH_ASPEED_PWM_OUT BIT(25)
#define TACH_ASPEED_PWM_OEN BIT(24)
#define TACH_ASPEED_DEB_INPUT BIT(23)
#define TACH_ASPEED_RAW_INPUT BIT(22)
#define TACH_ASPEED_VALUE_UPDATE BIT(21)
#define TACH_ASPEED_FULL_MEASUREMENT BIT(20)
#define TACH_ASPEED_VALUE_MASK GENMASK(19, 0)
/**********************************************************
 * Software setting
 *********************************************************/
#define DEFAULT_FAN_MIN_RPM 1000
#define DEFAULT_FAN_PULSE_PR 2
/*
 * Add this value to avoid CPU consuming a lot of resources in waiting rpm
 * updating. Assume the max rpm of fan is 60000, the fastest period of updating
 * tach value will be equal to (1000000 * 2 * 60) / (2 * max_rpm) = 1000us.
 */
#define DEFAULT_FAN_MAX_RPM 60000

struct aspeed_tach_channel_params {
	int limited_inverse;
	u16 threshold;
	u8 tach_edge;
	u8 tach_debounce;
	u8 pulse_pr;
	u32 min_rpm;
	u32 max_rpm;
	u32 divisor;
	u32 sample_period; /* unit is us */
	u32 polling_period; /* unit is us */
};

struct aspeed_tach_data {
	struct device *dev;
	struct regmap *regmap;
	struct clk *clk;
	struct reset_control *reset;
	bool tach_present[TACH_ASPEED_NR_TACHS];
	struct aspeed_tach_channel_params *tach_channel;
	/* for hwmon */
	const struct attribute_group *groups[2];
};

static void aspeed_update_tach_sample_period(struct aspeed_tach_data *priv,
					     u8 fan_tach_ch)
{
	u32 tach_period_us;
	u8 pulse_pr = priv->tach_channel[fan_tach_ch].pulse_pr;
	u32 min_rpm = priv->tach_channel[fan_tach_ch].min_rpm;

	/*
	 * min(Tach input clock) = (PulsePR * minRPM) / 60
	 * max(Tach input period) = 60 / (PulsePR * minRPM)
	 * Tach sample period > 2 * max(Tach input period) = (2*60) / (PulsePR * minRPM)
	 */
	tach_period_us = (USEC_PER_SEC * 2 * 60) / (pulse_pr * min_rpm);
	/* Add the margin (about 1.5) of tach sample period to avoid sample miss */
	tach_period_us = (tach_period_us * 1500) >> 10;
	dev_dbg(priv->dev, "tach%d sample period = %dus", fan_tach_ch, tach_period_us);
	priv->tach_channel[fan_tach_ch].sample_period = tach_period_us;
}

static void aspeed_update_tach_polling_period(struct aspeed_tach_data *priv,
					     u8 fan_tach_ch)
{
	u32 tach_period_us;
	u8 pulse_pr = priv->tach_channel[fan_tach_ch].pulse_pr;
	u32 max_rpm = priv->tach_channel[fan_tach_ch].max_rpm;

	tach_period_us = (USEC_PER_SEC * 2 * 60) / (pulse_pr * max_rpm);
	dev_dbg(priv->dev, "tach%d polling period = %dus", fan_tach_ch, tach_period_us);
	priv->tach_channel[fan_tach_ch].polling_period = tach_period_us;
}

static void aspeed_tach_ch_enable(struct aspeed_tach_data *priv, u8 tach_ch,
				  bool enable)
{
	if (enable)
		regmap_set_bits(priv->regmap, TACH_ASPEED_CTRL(tach_ch),
				TACH_ASPEED_ENABLE);
	else
		regmap_clear_bits(priv->regmap, TACH_ASPEED_CTRL(tach_ch),
				   TACH_ASPEED_ENABLE);
}

static int aspeed_get_fan_tach_ch_rpm(struct aspeed_tach_data *priv,
				      u8 fan_tach_ch)
{
	u32 raw_data, tach_div, val;
	unsigned long clk_source;
	u64 rpm;
	int ret;

	/* Restart the Tach channel to guarantee the value is fresh */
	aspeed_tach_ch_enable(priv, fan_tach_ch, false);
	aspeed_tach_ch_enable(priv, fan_tach_ch, true);
	ret = regmap_read_poll_timeout(
		priv->regmap, TACH_ASPEED_STS(fan_tach_ch), val,
		(val & TACH_ASPEED_FULL_MEASUREMENT) &&
			(val & TACH_ASPEED_VALUE_UPDATE),
		priv->tach_channel[fan_tach_ch].polling_period,
		priv->tach_channel[fan_tach_ch].sample_period);

	if (ret) {
		/* return 0 if we didn't get an answer because of timeout*/
		if (ret == -ETIMEDOUT)
			return 0;
		else
			return ret;
	}

	raw_data = val & TACH_ASPEED_VALUE_MASK;
	/*
	 * We need the mode to determine if the raw_data is double (from
	 * counting both edges).
	 */
	if (priv->tach_channel[fan_tach_ch].tach_edge == BOTH_EDGES)
		raw_data <<= 1;

	tach_div = raw_data * (priv->tach_channel[fan_tach_ch].divisor) *
		   (priv->tach_channel[fan_tach_ch].pulse_pr);

	clk_source = clk_get_rate(priv->clk);
	dev_dbg(priv->dev, "clk %ld, raw_data %d , tach_div %d\n", clk_source,
		raw_data, tach_div);

	if (tach_div == 0)
		return -EDOM;

	rpm = (u64)clk_source * 60;
	do_div(rpm, tach_div);

	return rpm;
}

static ssize_t fan_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int index = sensor_attr->index;
	int rpm;
	struct aspeed_tach_data *priv = dev_get_drvdata(dev);

	rpm = aspeed_get_fan_tach_ch_rpm(priv, index);
	if (rpm < 0)
		return rpm;

	return sprintf(buf, "%d\n", rpm);
}

static umode_t fan_dev_is_visible(struct kobject *kobj, struct attribute *a,
				  int index)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct aspeed_tach_data *priv = dev_get_drvdata(dev);

	if (!priv->tach_present[index % TACH_ASPEED_NR_TACHS])
		return 0;
	return a->mode;
}

static SENSOR_DEVICE_ATTR_RO(fan1_input, fan, 0);
static SENSOR_DEVICE_ATTR_RO(fan2_input, fan, 1);
static SENSOR_DEVICE_ATTR_RO(fan3_input, fan, 2);
static SENSOR_DEVICE_ATTR_RO(fan4_input, fan, 3);
static SENSOR_DEVICE_ATTR_RO(fan5_input, fan, 4);
static SENSOR_DEVICE_ATTR_RO(fan6_input, fan, 5);
static SENSOR_DEVICE_ATTR_RO(fan7_input, fan, 6);
static SENSOR_DEVICE_ATTR_RO(fan8_input, fan, 7);
static SENSOR_DEVICE_ATTR_RO(fan9_input, fan, 8);
static SENSOR_DEVICE_ATTR_RO(fan10_input, fan, 9);
static SENSOR_DEVICE_ATTR_RO(fan11_input, fan, 10);
static SENSOR_DEVICE_ATTR_RO(fan12_input, fan, 11);
static SENSOR_DEVICE_ATTR_RO(fan13_input, fan, 12);
static SENSOR_DEVICE_ATTR_RO(fan14_input, fan, 13);
static SENSOR_DEVICE_ATTR_RO(fan15_input, fan, 14);
static SENSOR_DEVICE_ATTR_RO(fan16_input, fan, 15);

static ssize_t fan_max_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int index = sensor_attr->index;
	struct aspeed_tach_data *priv = dev_get_drvdata(dev);
	u32 max_rpm = priv->tach_channel[index].max_rpm;

	return sprintf(buf, "%d\n", max_rpm);
}

static ssize_t fan_max_store(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t count)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int index = sensor_attr->index;
	struct aspeed_tach_data *priv = dev_get_drvdata(dev);
	long max_rpm;
	int ret;

	ret = kstrtoul(buf, 10, &max_rpm);
	if (ret < 0)
		return ret;

	priv->tach_channel[index].max_rpm = max_rpm;
	aspeed_update_tach_polling_period(priv, index);
	return count;
}

static SENSOR_DEVICE_ATTR_RW(fan1_max, fan_max, 0);
static SENSOR_DEVICE_ATTR_RW(fan2_max, fan_max, 1);
static SENSOR_DEVICE_ATTR_RW(fan3_max, fan_max, 2);
static SENSOR_DEVICE_ATTR_RW(fan4_max, fan_max, 3);
static SENSOR_DEVICE_ATTR_RW(fan5_max, fan_max, 4);
static SENSOR_DEVICE_ATTR_RW(fan6_max, fan_max, 5);
static SENSOR_DEVICE_ATTR_RW(fan7_max, fan_max, 6);
static SENSOR_DEVICE_ATTR_RW(fan8_max, fan_max, 7);
static SENSOR_DEVICE_ATTR_RW(fan9_max, fan_max, 8);
static SENSOR_DEVICE_ATTR_RW(fan10_max, fan_max, 9);
static SENSOR_DEVICE_ATTR_RW(fan11_max, fan_max, 10);
static SENSOR_DEVICE_ATTR_RW(fan12_max, fan_max, 11);
static SENSOR_DEVICE_ATTR_RW(fan13_max, fan_max, 12);
static SENSOR_DEVICE_ATTR_RW(fan14_max, fan_max, 13);
static SENSOR_DEVICE_ATTR_RW(fan15_max, fan_max, 14);
static SENSOR_DEVICE_ATTR_RW(fan16_max, fan_max, 15);

static ssize_t fan_min_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int index = sensor_attr->index;
	struct aspeed_tach_data *priv = dev_get_drvdata(dev);
	u32 min_rpm = priv->tach_channel[index].min_rpm;

	return sprintf(buf, "%d\n", min_rpm);
}

static ssize_t fan_min_store(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t count)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int index = sensor_attr->index;
	struct aspeed_tach_data *priv = dev_get_drvdata(dev);
	long min_rpm;
	int ret;

	ret = kstrtoul(buf, 10, &min_rpm);
	if (ret < 0)
		return ret;

	priv->tach_channel[index].min_rpm = min_rpm;
	aspeed_update_tach_sample_period(priv, index);
	return count;
}

static SENSOR_DEVICE_ATTR_RW(fan1_min, fan_min, 0);
static SENSOR_DEVICE_ATTR_RW(fan2_min, fan_min, 1);
static SENSOR_DEVICE_ATTR_RW(fan3_min, fan_min, 2);
static SENSOR_DEVICE_ATTR_RW(fan4_min, fan_min, 3);
static SENSOR_DEVICE_ATTR_RW(fan5_min, fan_min, 4);
static SENSOR_DEVICE_ATTR_RW(fan6_min, fan_min, 5);
static SENSOR_DEVICE_ATTR_RW(fan7_min, fan_min, 6);
static SENSOR_DEVICE_ATTR_RW(fan8_min, fan_min, 7);
static SENSOR_DEVICE_ATTR_RW(fan9_min, fan_min, 8);
static SENSOR_DEVICE_ATTR_RW(fan10_min, fan_min, 9);
static SENSOR_DEVICE_ATTR_RW(fan11_min, fan_min, 10);
static SENSOR_DEVICE_ATTR_RW(fan12_min, fan_min, 11);
static SENSOR_DEVICE_ATTR_RW(fan13_min, fan_min, 12);
static SENSOR_DEVICE_ATTR_RW(fan14_min, fan_min, 13);
static SENSOR_DEVICE_ATTR_RW(fan15_min, fan_min, 14);
static SENSOR_DEVICE_ATTR_RW(fan16_min, fan_min, 15);

static ssize_t fan_pulse_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int index = sensor_attr->index;
	struct aspeed_tach_data *priv = dev_get_drvdata(dev);
	u32 pulse_pr = priv->tach_channel[index].pulse_pr;

	return sprintf(buf, "%d\n", pulse_pr);
}

static ssize_t fan_pulse_store(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t count)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int index = sensor_attr->index;
	struct aspeed_tach_data *priv = dev_get_drvdata(dev);
	long pulse_pr;
	int ret;

	ret = kstrtoul(buf, 10, &pulse_pr);
	if (ret < 0)
		return ret;

	priv->tach_channel[index].pulse_pr = pulse_pr;
	aspeed_update_tach_sample_period(priv, index);
	return count;
}

static SENSOR_DEVICE_ATTR_RW(fan1_pulse, fan_pulse, 0);
static SENSOR_DEVICE_ATTR_RW(fan2_pulse, fan_pulse, 1);
static SENSOR_DEVICE_ATTR_RW(fan3_pulse, fan_pulse, 2);
static SENSOR_DEVICE_ATTR_RW(fan4_pulse, fan_pulse, 3);
static SENSOR_DEVICE_ATTR_RW(fan5_pulse, fan_pulse, 4);
static SENSOR_DEVICE_ATTR_RW(fan6_pulse, fan_pulse, 5);
static SENSOR_DEVICE_ATTR_RW(fan7_pulse, fan_pulse, 6);
static SENSOR_DEVICE_ATTR_RW(fan8_pulse, fan_pulse, 7);
static SENSOR_DEVICE_ATTR_RW(fan9_pulse, fan_pulse, 8);
static SENSOR_DEVICE_ATTR_RW(fan10_pulse, fan_pulse, 9);
static SENSOR_DEVICE_ATTR_RW(fan11_pulse, fan_pulse, 10);
static SENSOR_DEVICE_ATTR_RW(fan12_pulse, fan_pulse, 11);
static SENSOR_DEVICE_ATTR_RW(fan13_pulse, fan_pulse, 12);
static SENSOR_DEVICE_ATTR_RW(fan14_pulse, fan_pulse, 13);
static SENSOR_DEVICE_ATTR_RW(fan15_pulse, fan_pulse, 14);
static SENSOR_DEVICE_ATTR_RW(fan16_pulse, fan_pulse, 15);

static ssize_t fan_div_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int index = sensor_attr->index;
	struct aspeed_tach_data *priv = dev_get_drvdata(dev);
	u32 divisor, val = priv->tach_channel[index].divisor;

	regmap_read(priv->regmap, TACH_ASPEED_CTRL(index), &val);
	divisor = FIELD_GET(TACH_ASPEED_CLK_DIV_T_MASK, val);
	divisor = 1 << (divisor << 1);

	return sprintf(buf, "%d\n", divisor);
}

static ssize_t fan_div_store(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t count)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int index = sensor_attr->index;
	struct aspeed_tach_data *priv = dev_get_drvdata(dev);
	long divisor;
	int ret;

	ret = kstrtoul(buf, 10, &divisor);
	if (ret < 0)
		return ret;

	if ((is_power_of_2(divisor) && !(ilog2(divisor) % 2))) {
		priv->tach_channel[index].divisor = divisor;
		regmap_write_bits(priv->regmap, TACH_ASPEED_CTRL(index),
				  TACH_ASPEED_CLK_DIV_T_MASK,
				  DIV_TO_REG(priv->tach_channel[index].divisor)
					  << TACH_ASPEED_CLK_DIV_BIT);
	} else {
		dev_err(dev,
			"fan_div value %ld not supported. Only support power of 4\n",
			divisor);
		return -EINVAL;
	}

	return count;
}

static SENSOR_DEVICE_ATTR_RW(fan1_div, fan_div, 0);
static SENSOR_DEVICE_ATTR_RW(fan2_div, fan_div, 1);
static SENSOR_DEVICE_ATTR_RW(fan3_div, fan_div, 2);
static SENSOR_DEVICE_ATTR_RW(fan4_div, fan_div, 3);
static SENSOR_DEVICE_ATTR_RW(fan5_div, fan_div, 4);
static SENSOR_DEVICE_ATTR_RW(fan6_div, fan_div, 5);
static SENSOR_DEVICE_ATTR_RW(fan7_div, fan_div, 6);
static SENSOR_DEVICE_ATTR_RW(fan8_div, fan_div, 7);
static SENSOR_DEVICE_ATTR_RW(fan9_div, fan_div, 8);
static SENSOR_DEVICE_ATTR_RW(fan10_div, fan_div, 9);
static SENSOR_DEVICE_ATTR_RW(fan11_div, fan_div, 10);
static SENSOR_DEVICE_ATTR_RW(fan12_div, fan_div, 11);
static SENSOR_DEVICE_ATTR_RW(fan13_div, fan_div, 12);
static SENSOR_DEVICE_ATTR_RW(fan14_div, fan_div, 13);
static SENSOR_DEVICE_ATTR_RW(fan15_div, fan_div, 14);
static SENSOR_DEVICE_ATTR_RW(fan16_div, fan_div, 15);

static struct attribute *fan_dev_attrs[] = {
	&sensor_dev_attr_fan1_input.dev_attr.attr,
	&sensor_dev_attr_fan2_input.dev_attr.attr,
	&sensor_dev_attr_fan3_input.dev_attr.attr,
	&sensor_dev_attr_fan4_input.dev_attr.attr,
	&sensor_dev_attr_fan5_input.dev_attr.attr,
	&sensor_dev_attr_fan6_input.dev_attr.attr,
	&sensor_dev_attr_fan7_input.dev_attr.attr,
	&sensor_dev_attr_fan8_input.dev_attr.attr,
	&sensor_dev_attr_fan9_input.dev_attr.attr,
	&sensor_dev_attr_fan10_input.dev_attr.attr,
	&sensor_dev_attr_fan11_input.dev_attr.attr,
	&sensor_dev_attr_fan12_input.dev_attr.attr,
	&sensor_dev_attr_fan13_input.dev_attr.attr,
	&sensor_dev_attr_fan14_input.dev_attr.attr,
	&sensor_dev_attr_fan15_input.dev_attr.attr,
	&sensor_dev_attr_fan16_input.dev_attr.attr,

	&sensor_dev_attr_fan1_max.dev_attr.attr,
	&sensor_dev_attr_fan2_max.dev_attr.attr,
	&sensor_dev_attr_fan3_max.dev_attr.attr,
	&sensor_dev_attr_fan4_max.dev_attr.attr,
	&sensor_dev_attr_fan5_max.dev_attr.attr,
	&sensor_dev_attr_fan6_max.dev_attr.attr,
	&sensor_dev_attr_fan7_max.dev_attr.attr,
	&sensor_dev_attr_fan8_max.dev_attr.attr,
	&sensor_dev_attr_fan9_max.dev_attr.attr,
	&sensor_dev_attr_fan10_max.dev_attr.attr,
	&sensor_dev_attr_fan11_max.dev_attr.attr,
	&sensor_dev_attr_fan12_max.dev_attr.attr,
	&sensor_dev_attr_fan13_max.dev_attr.attr,
	&sensor_dev_attr_fan14_max.dev_attr.attr,
	&sensor_dev_attr_fan15_max.dev_attr.attr,
	&sensor_dev_attr_fan16_max.dev_attr.attr,

	&sensor_dev_attr_fan1_min.dev_attr.attr,
	&sensor_dev_attr_fan2_min.dev_attr.attr,
	&sensor_dev_attr_fan3_min.dev_attr.attr,
	&sensor_dev_attr_fan4_min.dev_attr.attr,
	&sensor_dev_attr_fan5_min.dev_attr.attr,
	&sensor_dev_attr_fan6_min.dev_attr.attr,
	&sensor_dev_attr_fan7_min.dev_attr.attr,
	&sensor_dev_attr_fan8_min.dev_attr.attr,
	&sensor_dev_attr_fan9_min.dev_attr.attr,
	&sensor_dev_attr_fan10_min.dev_attr.attr,
	&sensor_dev_attr_fan11_min.dev_attr.attr,
	&sensor_dev_attr_fan12_min.dev_attr.attr,
	&sensor_dev_attr_fan13_min.dev_attr.attr,
	&sensor_dev_attr_fan14_min.dev_attr.attr,
	&sensor_dev_attr_fan15_min.dev_attr.attr,
	&sensor_dev_attr_fan16_min.dev_attr.attr,

	&sensor_dev_attr_fan1_div.dev_attr.attr,
	&sensor_dev_attr_fan2_div.dev_attr.attr,
	&sensor_dev_attr_fan3_div.dev_attr.attr,
	&sensor_dev_attr_fan4_div.dev_attr.attr,
	&sensor_dev_attr_fan5_div.dev_attr.attr,
	&sensor_dev_attr_fan6_div.dev_attr.attr,
	&sensor_dev_attr_fan7_div.dev_attr.attr,
	&sensor_dev_attr_fan8_div.dev_attr.attr,
	&sensor_dev_attr_fan9_div.dev_attr.attr,
	&sensor_dev_attr_fan10_div.dev_attr.attr,
	&sensor_dev_attr_fan11_div.dev_attr.attr,
	&sensor_dev_attr_fan12_div.dev_attr.attr,
	&sensor_dev_attr_fan13_div.dev_attr.attr,
	&sensor_dev_attr_fan14_div.dev_attr.attr,
	&sensor_dev_attr_fan15_div.dev_attr.attr,
	&sensor_dev_attr_fan16_div.dev_attr.attr,

	&sensor_dev_attr_fan1_pulse.dev_attr.attr,
	&sensor_dev_attr_fan2_pulse.dev_attr.attr,
	&sensor_dev_attr_fan3_pulse.dev_attr.attr,
	&sensor_dev_attr_fan4_pulse.dev_attr.attr,
	&sensor_dev_attr_fan5_pulse.dev_attr.attr,
	&sensor_dev_attr_fan6_pulse.dev_attr.attr,
	&sensor_dev_attr_fan7_pulse.dev_attr.attr,
	&sensor_dev_attr_fan8_pulse.dev_attr.attr,
	&sensor_dev_attr_fan9_pulse.dev_attr.attr,
	&sensor_dev_attr_fan10_pulse.dev_attr.attr,
	&sensor_dev_attr_fan11_pulse.dev_attr.attr,
	&sensor_dev_attr_fan12_pulse.dev_attr.attr,
	&sensor_dev_attr_fan13_pulse.dev_attr.attr,
	&sensor_dev_attr_fan14_pulse.dev_attr.attr,
	&sensor_dev_attr_fan15_pulse.dev_attr.attr,
	&sensor_dev_attr_fan16_pulse.dev_attr.attr,
	NULL
};

static const struct attribute_group fan_dev_group = {
	.attrs = fan_dev_attrs,
	.is_visible = fan_dev_is_visible,
};

static void aspeed_create_fan_tach_channel(struct aspeed_tach_data *priv,
					   u32 tach_ch)
{
	priv->tach_present[tach_ch] = true;
	priv->tach_channel[tach_ch].limited_inverse = 0;
	regmap_write_bits(priv->regmap, TACH_ASPEED_CTRL(tach_ch),
			  TACH_ASPEED_INVERS_LIMIT,
			  priv->tach_channel[tach_ch].limited_inverse ?
				  TACH_ASPEED_INVERS_LIMIT :
				  0);

	priv->tach_channel[tach_ch].tach_debounce = DEBOUNCE_3_CLK;
	regmap_write_bits(priv->regmap, TACH_ASPEED_CTRL(tach_ch),
			  TACH_ASPEED_DEBOUNCE_MASK,
			  priv->tach_channel[tach_ch].tach_debounce
				  << TACH_ASPEED_DEBOUNCE_BIT);

	priv->tach_channel[tach_ch].tach_edge = F2F_EDGES;
	regmap_write_bits(priv->regmap, TACH_ASPEED_CTRL(tach_ch),
			  TACH_ASPEED_IO_EDGE_MASK,
			  priv->tach_channel[tach_ch].tach_edge
				  << TACH_ASPEED_IO_EDGE_BIT);

	priv->tach_channel[tach_ch].divisor = DEFAULT_TACH_DIV;
	regmap_write_bits(priv->regmap, TACH_ASPEED_CTRL(tach_ch),
			  TACH_ASPEED_CLK_DIV_T_MASK,
			  DIV_TO_REG(priv->tach_channel[tach_ch].divisor)
				  << TACH_ASPEED_CLK_DIV_BIT);

	priv->tach_channel[tach_ch].threshold = 0;
	regmap_write_bits(priv->regmap, TACH_ASPEED_CTRL(tach_ch),
			  TACH_ASPEED_THRESHOLD_MASK,
			  priv->tach_channel[tach_ch].threshold);

	priv->tach_channel[tach_ch].pulse_pr = DEFAULT_FAN_PULSE_PR;
	priv->tach_channel[tach_ch].min_rpm = DEFAULT_FAN_MIN_RPM;
	aspeed_update_tach_sample_period(priv, tach_ch);

	priv->tach_channel[tach_ch].max_rpm = DEFAULT_FAN_MAX_RPM;
	aspeed_update_tach_polling_period(priv, tach_ch);

	aspeed_tach_ch_enable(priv, tach_ch, true);
}

static int aspeed_tach_create_fan(struct device *dev, struct device_node *child,
				  struct aspeed_tach_data *priv)
{
	u32 tach_channel;
	int ret;

	ret = of_property_read_u32(child, "reg", &tach_channel);
	if (ret)
		return ret;

	aspeed_create_fan_tach_channel(priv, tach_channel);

	return 0;
}

static void aspeed_tach_reset_assert(void *data)
{
	struct reset_control *rst = data;

	reset_control_assert(rst);
}

static int aspeed_tach_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np, *child;
	struct aspeed_tach_data *priv;
	struct device *hwmon;
	struct platform_device *parent_dev;
	int ret;

	np = dev->parent->of_node;
	if (!of_device_is_compatible(np, "aspeed,ast2600-pwm-tach"))
		return dev_err_probe(dev, -ENODEV,
				     "Unsupported tach device binding\n");

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	priv->dev = &pdev->dev;
	priv->tach_channel =
		devm_kzalloc(dev,
			     TACH_ASPEED_NR_TACHS * sizeof(*priv->tach_channel),
			     GFP_KERNEL);

	priv->regmap = syscon_node_to_regmap(np);
	if (IS_ERR(priv->regmap)) {
		dev_err(priv->dev, "Couldn't get regmap\n");
		return -ENODEV;
	}
	parent_dev = of_find_device_by_node(np);
	priv->clk = devm_clk_get_enabled(&parent_dev->dev, 0);
	if (IS_ERR(priv->clk))
		return dev_err_probe(dev, PTR_ERR(priv->clk),
				     "Couldn't get clock\n");

	priv->reset = devm_reset_control_get_shared(&parent_dev->dev, NULL);
	if (IS_ERR(priv->reset))
		return dev_err_probe(dev, PTR_ERR(priv->reset),
				     "Couldn't get reset control\n");

	ret = reset_control_deassert(priv->reset);
	if (ret)
		return dev_err_probe(dev, ret,
				     "Couldn't deassert reset control\n");

	ret = devm_add_action_or_reset(dev, aspeed_tach_reset_assert,
				       priv->reset);
	if (ret)
		return ret;

	for_each_child_of_node(dev->of_node, child) {
		ret = aspeed_tach_create_fan(dev, child, priv);
		if (ret) {
			of_node_put(child);
			return ret;
		}
	}

	priv->groups[0] = &fan_dev_group;
	priv->groups[1] = NULL;
	hwmon = devm_hwmon_device_register_with_groups(dev, "aspeed_tach", priv,
						       priv->groups);
	ret = PTR_ERR_OR_ZERO(hwmon);
	if (ret)
		return dev_err_probe(dev, ret,
				     "Failed to register hwmon device\n");
	return 0;
}

static const struct of_device_id of_stach_match_table[] = {
	{
		.compatible = "aspeed,ast2600-tach",
	},
	{},
};
MODULE_DEVICE_TABLE(of, of_stach_match_table);

static struct platform_driver aspeed_tach_driver = {
	.probe		= aspeed_tach_probe,
	.driver		= {
		.name	= "aspeed_tach",
		.of_match_table = of_stach_match_table,
	},
};

module_platform_driver(aspeed_tach_driver);

MODULE_AUTHOR("Billy Tsai <billy_tsai@aspeedtech.com>");
MODULE_DESCRIPTION("Aspeed ast2600 TACH device driver");
MODULE_LICENSE("GPL v2");

