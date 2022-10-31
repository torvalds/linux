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
 * updating. Assume the max rpm of fan is 60000, the period of updating tach
 * value will equal to (1000000 * 2 * 60) / (2 * max_rpm) = 1000.
 */
#define RPM_POLLING_PERIOD_US 1000

struct aspeed_tach_channel_params {
	int limited_inverse;
	u16 threshold;
	u8 tach_edge;
	u8 tach_debounce;
	u8 pulse_pr;
	u32 min_rpm;
	u32 divisor;
	u32 sample_period; /* unit is us */
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
	u32 raw_data, tach_div, usec, val;
	unsigned long clk_source;
	u64 rpm;
	int ret;

	usec = priv->tach_channel[fan_tach_ch].sample_period;
	/* Restart the Tach channel to guarantee the value is fresh */
	aspeed_tach_ch_enable(priv, fan_tach_ch, false);
	aspeed_tach_ch_enable(priv, fan_tach_ch, true);
	ret = regmap_read_poll_timeout(
		priv->regmap, TACH_ASPEED_STS(fan_tach_ch), val,
		(val & TACH_ASPEED_FULL_MEASUREMENT) && (val & TACH_ASPEED_VALUE_UPDATE),
		RPM_POLLING_PERIOD_US, usec);

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
	priv->clk = devm_clk_get(&parent_dev->dev, 0);
	if (IS_ERR(priv->clk))
		return dev_err_probe(dev, PTR_ERR(priv->clk),
				     "Couldn't get clock\n");

	priv->reset = devm_reset_control_get_shared(&parent_dev->dev, NULL);
	if (IS_ERR(priv->reset))
		return dev_err_probe(dev, PTR_ERR(priv->reset),
				     "Couldn't get reset control\n");

	ret = clk_prepare_enable(priv->clk);
	if (ret)
		return dev_err_probe(dev, ret, "Couldn't enable clock\n");

	ret = reset_control_deassert(priv->reset);
	if (ret) {
		dev_err_probe(dev, ret, "Couldn't deassert reset control\n");
		goto err_disable_clk;
	}
	for_each_child_of_node(dev->of_node, child) {
		ret = aspeed_tach_create_fan(dev, child, priv);
		if (ret) {
			of_node_put(child);
			goto err_assert_reset;
		}
	}

	priv->groups[0] = &fan_dev_group;
	priv->groups[1] = NULL;
	hwmon = devm_hwmon_device_register_with_groups(dev, "aspeed_tach", priv,
						       priv->groups);
	ret = PTR_ERR_OR_ZERO(hwmon);
	if (ret) {
		dev_err_probe(dev, ret, "Failed to register hwmon device\n");
		goto err_assert_reset;
	}
	return 0;
err_assert_reset:
	reset_control_assert(priv->reset);
err_disable_clk:
	clk_disable_unprepare(priv->clk);
	return ret;
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
