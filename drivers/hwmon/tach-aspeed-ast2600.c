// SPDX-License-Identifier: GPL-2.0-only
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
/* TACH Control Register */
#define ASPEED_TACHO_CTRL_CH(ch) (((ch) * 0x10) + 0x08)
#define TACHO_IER BIT(31)
#define TACHO_INVERS_LIMIT BIT(30)
#define TACHO_LOOPBACK BIT(29)
#define TACHO_ENABLE BIT(28)
#define TACHO_DEBOUNCE_MASK (0x3 << 26)
#define TACHO_DEBOUNCE_BIT (26)
#define TECHIO_EDGE_MASK (0x3 << 24)
#define TECHIO_EDGE_BIT (24)
#define TACHO_CLK_DIV_T_MASK (0xf << 20)
#define TACHO_CLK_DIV_BIT (20)
#define TACHO_THRESHOLD_MASK (0xfffff)
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
/* Cover rpm range 5~5859375 */
#define DEFAULT_TACHO_DIV 5

/* TACH Status Register */
#define ASPEED_TACHO_STS_CH(x) (((x) * 0x10) + 0x0C)

/*PWM_TACHO_STS */
#define TACHO_ISR BIT(31)
#define PWM_OUT BIT(25)
#define PWM_OEN BIT(24)
#define TACHO_DEB_INPUT BIT(23)
#define TACHO_RAW_INPUT BIT(22)
#define TACHO_VALUE_UPDATE BIT(21)
#define TACHO_FULL_MEASUREMENT BIT(20)
#define TACHO_VALUE_MASK 0xfffff
/**********************************************************
 * Software setting
 *********************************************************/
#define DEFAULT_FAN_MIN_RPM 1000
#define DEFAULT_FAN_PULSE_PR 2
/*
 * Add this value to avoid CPU consuming a lot of resources in waiting rpm
 * updating. Assume the max rpm of fan is 60000, the period of updating tach
 * value will equal to (1000000 * 2 * 60) / (2 * max_rpm) = 1000.
 */
#define RPM_POLLING_PERIOD_US 1000

struct aspeed_tacho_channel_params {
	int limited_inverse;
	u16 threshold;
	u8 tacho_edge;
	u8 tacho_debounce;
	u8 pulse_pr;
	u32 min_rpm;
	u32 divide;
	u32 sample_period; /* unit is us */
};

struct aspeed_tach_data {
	struct device *dev;
	struct regmap *regmap;
	unsigned long clk_freq;
	struct reset_control *reset;
	bool tach_present[16];
	struct aspeed_tacho_channel_params *tacho_channel;
	/* for hwmon */
	const struct attribute_group *groups[2];
};

static u32 aspeed_get_fan_tach_sample_period(struct aspeed_tach_data *priv,
					     u8 fan_tach_ch)
{
	u32 tach_period_us;
	u8 pulse_pr = priv->tacho_channel[fan_tach_ch].pulse_pr;
	u32 min_rpm = priv->tacho_channel[fan_tach_ch].min_rpm;
	/*
	 * min(Tach input clock) = (PulsePR * minRPM) / 60
	 * max(Tach input period) = 60 / (PulsePR * minRPM)
	 * Tach sample period > 2 * max(Tach input period) = (2*60) / (PulsePR * minRPM)
	 */
	tach_period_us = (1000000 * 2 * 60) / (pulse_pr * min_rpm);
	/* Add the margin (about 1.2) of tach sample period to avoid sample miss */
	tach_period_us = (tach_period_us * 1200) >> 10;
	dev_dbg(priv->dev, "tach%d sample period = %dus", fan_tach_ch, tach_period_us);
	return tach_period_us;
}

static void aspeed_set_fan_tach_ch_enable(struct aspeed_tach_data *priv,
					  u8 fan_tach_ch, bool enable,
					  u32 tacho_div)
{
	u32 reg_value = 0;

	if (enable) {
		/* divide = 2^(tacho_div*2) */
		priv->tacho_channel[fan_tach_ch].divide = 1 << (tacho_div << 1);

		reg_value = TACHO_ENABLE |
			    (priv->tacho_channel[fan_tach_ch].tacho_edge
			     << TECHIO_EDGE_BIT) |
			    (tacho_div << TACHO_CLK_DIV_BIT) |
			    (priv->tacho_channel[fan_tach_ch].tacho_debounce
			     << TACHO_DEBOUNCE_BIT);

		if (priv->tacho_channel[fan_tach_ch].limited_inverse)
			reg_value |= TACHO_INVERS_LIMIT;

		if (priv->tacho_channel[fan_tach_ch].threshold)
			reg_value |=
				(TACHO_IER |
				 priv->tacho_channel[fan_tach_ch].threshold);

		regmap_write(priv->regmap, ASPEED_TACHO_CTRL_CH(fan_tach_ch),
			     reg_value);

		priv->tacho_channel[fan_tach_ch].sample_period =
			aspeed_get_fan_tach_sample_period(priv, fan_tach_ch);
	} else
		regmap_update_bits(priv->regmap,
				   ASPEED_TACHO_CTRL_CH(fan_tach_ch),
				   TACHO_ENABLE, 0);
}

static int aspeed_get_fan_tach_ch_rpm(struct aspeed_tach_data *priv,
				      u8 fan_tach_ch)
{
	u32 raw_data, tach_div, clk_source, usec, val;
	u64 rpm;
	int ret;

	usec = priv->tacho_channel[fan_tach_ch].sample_period;
	/* Restart the Tach channel to guarantee the value is fresh */
	regmap_update_bits(priv->regmap, ASPEED_TACHO_CTRL_CH(fan_tach_ch),
			     TACHO_ENABLE, 0);
	regmap_update_bits(priv->regmap, ASPEED_TACHO_CTRL_CH(fan_tach_ch),
			     TACHO_ENABLE, TACHO_ENABLE);
	ret = regmap_read_poll_timeout(
		priv->regmap, ASPEED_TACHO_STS_CH(fan_tach_ch), val,
		(val & TACHO_FULL_MEASUREMENT) && (val & TACHO_VALUE_UPDATE),
		RPM_POLLING_PERIOD_US, usec);

	if (ret) {
		/* return 0 if we didn't get an answer because of timeout*/
		if (ret == -ETIMEDOUT)
			return 0;
		else
			return ret;
	}

	raw_data = val & TACHO_VALUE_MASK;
	/*
	 * We need the mode to determine if the raw_data is double (from
	 * counting both edges).
	 */
	if (priv->tacho_channel[fan_tach_ch].tacho_edge == BOTH_EDGES)
		raw_data <<= 1;

	tach_div = raw_data * (priv->tacho_channel[fan_tach_ch].divide) *
		   (priv->tacho_channel[fan_tach_ch].pulse_pr);

	dev_dbg(priv->dev, "clk %ld, raw_data %d , tach_div %d\n",
		priv->clk_freq, raw_data, tach_div);

	clk_source = priv->clk_freq;

	if (tach_div == 0)
		return -EDOM;

	rpm = (u64)clk_source * 60;
	do_div(rpm, tach_div);

	return rpm;
}

static ssize_t show_rpm(struct device *dev, struct device_attribute *attr,
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

	if (!priv->tach_present[index])
		return 0;
	return a->mode;
}

static SENSOR_DEVICE_ATTR(fan1_input, 0444, show_rpm, NULL, 0);
static SENSOR_DEVICE_ATTR(fan2_input, 0444, show_rpm, NULL, 1);
static SENSOR_DEVICE_ATTR(fan3_input, 0444, show_rpm, NULL, 2);
static SENSOR_DEVICE_ATTR(fan4_input, 0444, show_rpm, NULL, 3);
static SENSOR_DEVICE_ATTR(fan5_input, 0444, show_rpm, NULL, 4);
static SENSOR_DEVICE_ATTR(fan6_input, 0444, show_rpm, NULL, 5);
static SENSOR_DEVICE_ATTR(fan7_input, 0444, show_rpm, NULL, 6);
static SENSOR_DEVICE_ATTR(fan8_input, 0444, show_rpm, NULL, 7);
static SENSOR_DEVICE_ATTR(fan9_input, 0444, show_rpm, NULL, 8);
static SENSOR_DEVICE_ATTR(fan10_input, 0444, show_rpm, NULL, 9);
static SENSOR_DEVICE_ATTR(fan11_input, 0444, show_rpm, NULL, 10);
static SENSOR_DEVICE_ATTR(fan12_input, 0444, show_rpm, NULL, 11);
static SENSOR_DEVICE_ATTR(fan13_input, 0444, show_rpm, NULL, 12);
static SENSOR_DEVICE_ATTR(fan14_input, 0444, show_rpm, NULL, 13);
static SENSOR_DEVICE_ATTR(fan15_input, 0444, show_rpm, NULL, 14);
static SENSOR_DEVICE_ATTR(fan16_input, 0444, show_rpm, NULL, 15);
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
	NULL
};

static const struct attribute_group fan_dev_group = {
	.attrs = fan_dev_attrs,
	.is_visible = fan_dev_is_visible,
};

static void aspeed_create_fan_tach_channel(struct aspeed_tach_data *priv,
					   u32 tach_ch, int count,
					   u32 fan_pulse_pr, u32 fan_min_rpm,
					   u32 tacho_div)
{
	priv->tach_present[tach_ch] = true;
	priv->tacho_channel[tach_ch].pulse_pr = fan_pulse_pr;
	priv->tacho_channel[tach_ch].min_rpm = fan_min_rpm;
	priv->tacho_channel[tach_ch].limited_inverse = 0;
	priv->tacho_channel[tach_ch].threshold = 0;
	priv->tacho_channel[tach_ch].tacho_edge = F2F_EDGES;
	priv->tacho_channel[tach_ch].tacho_debounce = DEBOUNCE_3_CLK;
	aspeed_set_fan_tach_ch_enable(priv, tach_ch, true, tacho_div);
}

static int aspeed_tach_create_fan(struct device *dev, struct device_node *child,
				  struct aspeed_tach_data *priv)
{
	u32 fan_pulse_pr, fan_min_rpm;
	u32 tacho_div;
	u32 tach_channel;
	int ret, count;

	ret = of_property_read_u32(child, "reg", &tach_channel);
	if (ret)
		return ret;

	ret = of_property_read_u32(child, "aspeed,pulse-pr", &fan_pulse_pr);
	if (ret)
		fan_pulse_pr = DEFAULT_FAN_PULSE_PR;

	ret = of_property_read_u32(child, "aspeed,min-rpm", &fan_min_rpm);
	if (ret)
		fan_min_rpm = DEFAULT_FAN_MIN_RPM;

	ret = of_property_read_u32(child, "aspeed,tach-div", &tacho_div);
	if (ret)
		tacho_div = DEFAULT_TACHO_DIV;

	aspeed_create_fan_tach_channel(priv, tach_channel, count, fan_pulse_pr,
				       fan_min_rpm, tacho_div);

	return 0;
}

static int aspeed_tach_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np, *child;
	struct aspeed_tach_data *priv;
	struct device *hwmon;
	struct clk *clk;
	int ret;

	np = dev->parent->of_node;
	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	priv->dev = &pdev->dev;
	priv->tacho_channel = devm_kzalloc(
		dev, 16 * sizeof(*priv->tacho_channel), GFP_KERNEL);

	priv->regmap = syscon_node_to_regmap(np);
	if (IS_ERR(priv->regmap)) {
		dev_err(priv->dev, "Couldn't get regmap\n");
		return -ENODEV;
	}

	clk = of_clk_get(np, 0);
	if (IS_ERR(clk))
		return -ENODEV;
	priv->clk_freq = clk_get_rate(clk);

	priv->reset = of_reset_control_get_shared(np, NULL);
	if (IS_ERR(priv->reset)) {
		dev_err(priv->dev, "can't get aspeed_pwm_tacho reset\n");
		return PTR_ERR(priv->reset);
	}

	reset_control_deassert(priv->reset);
	for_each_child_of_node(dev->of_node, child) {
		ret = aspeed_tach_create_fan(dev, child, priv);
		if (ret) {
			of_node_put(child);
			return ret;
		}
	}

	priv->groups[0] = &fan_dev_group;
	priv->groups[1] = NULL;
	dev_info(priv->dev, "tach probe done\n");
	hwmon = devm_hwmon_device_register_with_groups(dev, "aspeed_tach", priv,
						       priv->groups);

	return PTR_ERR_OR_ZERO(hwmon);
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

MODULE_AUTHOR("Ryan Chen <ryan_chen@aspeedtech.com>");
MODULE_DESCRIPTION("ASPEED Fan tach device driver");
MODULE_LICENSE("GPL");
