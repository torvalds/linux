// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) ASPEED Technology Inc.
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/hwmon.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/sysfs.h>

/* The channel number of Aspeed tach controller */
#define TACH_ASPEED_NR_TACHS		16
/* TACH Control Register */
#define TACH_ASPEED_CTRL(ch)		(((ch) * 0x10) + 0x08)
#define TACH_ASPEED_IER			BIT(31)
#define TACH_ASPEED_INVERS_LIMIT	BIT(30)
#define TACH_ASPEED_LOOPBACK		BIT(29)
#define TACH_ASPEED_ENABLE		BIT(28)
#define TACH_ASPEED_DEBOUNCE_MASK	GENMASK(27, 26)
#define TACH_ASPEED_DEBOUNCE_BIT	26
#define TACH_ASPEED_IO_EDGE_MASK	GENMASK(25, 24)
#define TACH_ASPEED_IO_EDGE_BIT		24
#define TACH_ASPEED_CLK_DIV_T_MASK	GENMASK(23, 20)
#define TACH_ASPEED_CLK_DIV_BIT		20
#define TACH_ASPEED_THRESHOLD_MASK	GENMASK(19, 0)
/* [27:26] */
#define DEBOUNCE_3_CLK			0x00
#define DEBOUNCE_2_CLK			0x01
#define DEBOUNCE_1_CLK			0x02
#define DEBOUNCE_0_CLK			0x03
/* [25:24] */
#define F2F_EDGES			0x00
#define R2R_EDGES			0x01
#define BOTH_EDGES			0x02
/* [23:20] */
/* divisor = 4 to the nth power, n = register value */
#define DEFAULT_TACH_DIV		1024
#define DIV_TO_REG(divisor)		(ilog2(divisor) >> 1)

/* TACH Status Register */
#define TACH_ASPEED_STS(ch)		(((ch) * 0x10) + 0x0C)

/*PWM_TACH_STS */
#define TACH_ASPEED_ISR			BIT(31)
#define TACH_ASPEED_PWM_OUT		BIT(25)
#define TACH_ASPEED_PWM_OEN		BIT(24)
#define TACH_ASPEED_DEB_INPUT		BIT(23)
#define TACH_ASPEED_RAW_INPUT		BIT(22)
#define TACH_ASPEED_VALUE_UPDATE	BIT(21)
#define TACH_ASPEED_FULL_MEASUREMENT	BIT(20)
#define TACH_ASPEED_VALUE_MASK		GENMASK(19, 0)
/**********************************************************
 * Software setting
 *********************************************************/
#define DEFAULT_FAN_PULSE_PR		2
struct aspeed_tach_channel_params {
	int limited_inverse;
	u16 threshold;
	u8 tach_edge;
	u8 tach_debounce;
	u8 pulse_pr;
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
	struct aspeed_tach_channel_params tach_channel[TACH_ASPEED_NR_TACHS];
	unsigned long clk_source;
};

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

static u64 aspeed_tach_val_to_rpm(struct aspeed_tach_data *priv, u8 fan_tach_ch,
				  u32 tach_val)
{
	u64 rpm;
	u32 tach_div;

	/*
	 * We need the mode to determine if the tach_val is double (from
	 * counting both edges).
	 */
	if (priv->tach_channel[fan_tach_ch].tach_edge == BOTH_EDGES)
		tach_val <<= 1;

	tach_div = tach_val * (priv->tach_channel[fan_tach_ch].divisor) *
		   (priv->tach_channel[fan_tach_ch].pulse_pr);

	dev_dbg(priv->dev, "clk %ld, tach_val %d , tach_div %d\n",
		priv->clk_source, tach_val, tach_div);

	rpm = (u64)priv->clk_source * 60;
	do_div(rpm, tach_div);

	return rpm;
}

static int aspeed_get_fan_tach_ch_rpm(struct aspeed_tach_data *priv,
				      u8 fan_tach_ch)
{
	u32 val;
	u64 rpm;
	int ret;

	ret = regmap_read(priv->regmap, TACH_ASPEED_STS(fan_tach_ch), &val);
	if (ret)
		return ret;

	if (!(val & TACH_ASPEED_FULL_MEASUREMENT))
		return 0;
	rpm = aspeed_tach_val_to_rpm(priv, fan_tach_ch,
				     val & TACH_ASPEED_VALUE_MASK);

	return rpm;
}

static int aspeed_tach_hwmon_read(struct device *dev,
				  enum hwmon_sensor_types type, u32 attr,
				  int channel, long *val)
{
	struct aspeed_tach_data *priv = dev_get_drvdata(dev);
	u32 reg_val;
	int ret;

	switch (attr) {
	case hwmon_fan_input:
		ret = aspeed_get_fan_tach_ch_rpm(priv, channel);
		if (ret < 0)
			return ret;
		*val = ret;
		break;
	case hwmon_fan_div:
		regmap_read(priv->regmap, TACH_ASPEED_CTRL(channel), &reg_val);
		reg_val = FIELD_GET(TACH_ASPEED_CLK_DIV_T_MASK, reg_val);
		*val = BIT(reg_val << 1);
		break;
	case hwmon_fan_pulses:
		*val = priv->tach_channel[channel].pulse_pr;
		break;
	default:
		return -EOPNOTSUPP;
	}
	return 0;
}

static int aspeed_tach_hwmon_write(struct device *dev,
				   enum hwmon_sensor_types type, u32 attr,
				   int channel, long val)
{
	struct aspeed_tach_data *priv = dev_get_drvdata(dev);

	switch (attr) {
	case hwmon_fan_div:
		if (!(is_power_of_2(val) && !(ilog2(val) % 2))) {
			dev_err(dev,
				"fan_div value %ld not supported. Only support power of 4\n",
				val);
			return -EINVAL;
		}
		priv->tach_channel[channel].divisor = val;
		regmap_write_bits(priv->regmap, TACH_ASPEED_CTRL(channel),
				  TACH_ASPEED_CLK_DIV_T_MASK,
				  DIV_TO_REG(priv->tach_channel[channel].divisor)
					  << TACH_ASPEED_CLK_DIV_BIT);
		break;
	case hwmon_fan_pulses:
		priv->tach_channel[channel].pulse_pr = val;
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static umode_t aspeed_tach_dev_is_visible(const void *drvdata,
					  enum hwmon_sensor_types type,
					  u32 attr, int channel)
{
	const struct aspeed_tach_data *priv = drvdata;

	if (!priv->tach_present[channel])
		return 0;
	switch (attr) {
	case hwmon_fan_input:
		return 0444;
	case hwmon_fan_div:
	case hwmon_fan_pulses:
		return 0644;
	}
	return 0;
}

static const struct hwmon_ops aspeed_tach_ops = {
	.is_visible = aspeed_tach_dev_is_visible,
	.read = aspeed_tach_hwmon_read,
	.write = aspeed_tach_hwmon_write,
};

static const struct hwmon_channel_info *aspeed_tach_info[] = {
	HWMON_CHANNEL_INFO(fan, HWMON_F_INPUT | HWMON_F_DIV | HWMON_F_PULSES,
			   HWMON_F_INPUT | HWMON_F_DIV | HWMON_F_PULSES,
			   HWMON_F_INPUT | HWMON_F_DIV | HWMON_F_PULSES,
			   HWMON_F_INPUT | HWMON_F_DIV | HWMON_F_PULSES,
			   HWMON_F_INPUT | HWMON_F_DIV | HWMON_F_PULSES,
			   HWMON_F_INPUT | HWMON_F_DIV | HWMON_F_PULSES,
			   HWMON_F_INPUT | HWMON_F_DIV | HWMON_F_PULSES,
			   HWMON_F_INPUT | HWMON_F_DIV | HWMON_F_PULSES,
			   HWMON_F_INPUT | HWMON_F_DIV | HWMON_F_PULSES,
			   HWMON_F_INPUT | HWMON_F_DIV | HWMON_F_PULSES,
			   HWMON_F_INPUT | HWMON_F_DIV | HWMON_F_PULSES,
			   HWMON_F_INPUT | HWMON_F_DIV | HWMON_F_PULSES,
			   HWMON_F_INPUT | HWMON_F_DIV | HWMON_F_PULSES,
			   HWMON_F_INPUT | HWMON_F_DIV | HWMON_F_PULSES,
			   HWMON_F_INPUT | HWMON_F_DIV | HWMON_F_PULSES,
			   HWMON_F_INPUT | HWMON_F_DIV | HWMON_F_PULSES),
	NULL
};

static const struct hwmon_chip_info aspeed_tach_chip_info = {
	.ops = &aspeed_tach_ops,
	.info = aspeed_tach_info,
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
	int ret;

	np = dev->parent->of_node;
	if (!of_device_is_compatible(np, "aspeed,ast2600-pwm-tach"))
		return dev_err_probe(dev, -ENODEV,
				     "Unsupported tach device binding\n");

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	priv->dev = &pdev->dev;
	priv->regmap = syscon_node_to_regmap(np);
	if (IS_ERR(priv->regmap))
		return dev_err_probe(dev, PTR_ERR(priv->regmap),
				     "Couldn't get regmap\n");
	priv->clk = devm_clk_get_enabled(dev->parent, NULL);
	if (IS_ERR(priv->clk))
		return dev_err_probe(dev, PTR_ERR(priv->clk),
				     "Couldn't get clock\n");

	priv->clk_source = clk_get_rate(priv->clk);
	priv->reset = devm_reset_control_get_shared(dev->parent, NULL);
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

	hwmon = devm_hwmon_device_register_with_info(dev, "aspeed_tach", priv,
						     &aspeed_tach_chip_info, NULL);
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
MODULE_LICENSE("GPL");
