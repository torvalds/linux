// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Monolithic Power Systems MPQ4210 buck-boost regulator
 *
 * Copyright (c) 2026 Vaisala Oyj
 */

#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/math.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>

#define MPQ4210_REF_LSB			0x00
#define MPQ4210_REF_LSB_MASK		GENMASK(2, 0)
#define MPQ4210_REF_LSB_BITS		3
#define MPQ4210_REF_MSB			0x01
#define MPQ4210_CONTROL1		0x02
#define MPQ4210_CONTROL1_SR		GENMASK(7, 6)
#define MPQ4210_CONTROL1_RESERVED	BIT(2)
#define MPQ4210_CONTROL1_GO		BIT(1)
#define MPQ4210_CONTROL1_ENPWR		BIT(0)
#define MPQ4210_INT_MASK		0x06

/*
 * The feedback reference is an 11 bit value with a 1mV step. The datasheet
 * specifies no reference below 0.3V, so those selectors are not offered.
 */
#define MPQ4210_REF_MIN			0x12c
#define MPQ4210_REF_MAX			0x7ff
#define MPQ4210_REF_STEP_UV		1000

#define MPQ4210_ENPWR_DELAY_MS		200

/* Feedback reference slew rate per MPQ4210_CONTROL1_SR value, in uV/us. */
static const unsigned int mpq4210_ref_slew_rate[] = { 38, 50, 75, 150 };

static const struct regmap_config mpq4210_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = MPQ4210_INT_MASK,
};

/* Scale a feedback reference value by the output voltage divider ratio. */
static unsigned int mpq4210_scale(unsigned int val, u32 r1, u32 r2)
{
	u64 tmp = (u64)val * (r1 + r2);

	do_div(tmp, r2);

	return tmp;
}

static int mpq4210_set_voltage_sel(struct regulator_dev *rdev, unsigned int sel)
{
	int ret;

	ret = regmap_write(rdev->regmap, MPQ4210_REF_LSB,
			   sel & MPQ4210_REF_LSB_MASK);
	if (ret)
		return ret;

	ret = regmap_write(rdev->regmap, MPQ4210_REF_MSB,
			   sel >> MPQ4210_REF_LSB_BITS);
	if (ret)
		return ret;

	return regmap_set_bits(rdev->regmap, MPQ4210_CONTROL1,
			       MPQ4210_CONTROL1_GO);
}

static int mpq4210_get_voltage_sel(struct regulator_dev *rdev)
{
	unsigned int lsb, msb;
	int ret;

	ret = regmap_read(rdev->regmap, MPQ4210_REF_MSB, &msb);
	if (ret)
		return ret;

	ret = regmap_read(rdev->regmap, MPQ4210_REF_LSB, &lsb);
	if (ret)
		return ret;

	return (msb << MPQ4210_REF_LSB_BITS) | (lsb & MPQ4210_REF_LSB_MASK);
}

static int mpq4210_enable(struct regulator_dev *rdev)
{
	int ret;

	ret = regmap_set_bits(rdev->regmap, MPQ4210_CONTROL1,
			      MPQ4210_CONTROL1_GO);
	if (ret)
		return ret;

	/* The reference has to settle before power switching may start. */
	msleep(MPQ4210_ENPWR_DELAY_MS);

	return regmap_set_bits(rdev->regmap, MPQ4210_CONTROL1,
			       MPQ4210_CONTROL1_ENPWR);
}

static const struct regulator_ops mpq4210_regulator_ops = {
	.set_voltage_sel = mpq4210_set_voltage_sel,
	.get_voltage_sel = mpq4210_get_voltage_sel,
	.list_voltage = regulator_list_voltage_linear,
	.set_ramp_delay = regulator_set_ramp_delay_regmap,
	.enable = mpq4210_enable,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
};

static const struct regulator_desc mpq4210_regulator = {
	.name = "mpq4210",
	.type = REGULATOR_VOLTAGE,
	.owner = THIS_MODULE,
	.ops = &mpq4210_regulator_ops,
	.n_voltages = MPQ4210_REF_MAX + 1,
	.linear_min_sel = MPQ4210_REF_MIN,
	.enable_reg = MPQ4210_CONTROL1,
	.enable_mask = MPQ4210_CONTROL1_ENPWR,
	.ramp_reg = MPQ4210_CONTROL1,
	.ramp_mask = MPQ4210_CONTROL1_SR,
	.n_ramp_values = ARRAY_SIZE(mpq4210_ref_slew_rate),
};

static int mpq4210_i2c_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct regulator_config config = { };
	struct regulator_desc *desc;
	struct regulator_dev *rdev;
	struct gpio_desc *enable;
	struct regmap *regmap;
	unsigned int *slew;
	unsigned int i, val;
	u32 r[2];
	int ret;

	ret = of_property_read_u32_array(dev->of_node,
					 "mps,fb-voltage-divider-ohms",
					 r, ARRAY_SIZE(r));
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to read mps,fb-voltage-divider-ohms\n");

	if (!r[1])
		return dev_err_probe(dev, -EINVAL,
				     "feedback divider R2 must not be zero\n");

	/* The controller does not answer on the bus while EN is deasserted. */
	enable = devm_gpiod_get_optional(dev, "enable", GPIOD_OUT_HIGH);
	if (IS_ERR(enable))
		return dev_err_probe(dev, PTR_ERR(enable),
				     "failed to get enable GPIO\n");

	regmap = devm_regmap_init_i2c(client, &mpq4210_regmap_config);
	if (IS_ERR(regmap))
		return dev_err_probe(dev, PTR_ERR(regmap),
				     "failed to init regmap\n");

	desc = devm_kmemdup(dev, &mpq4210_regulator, sizeof(*desc), GFP_KERNEL);
	if (!desc)
		return -ENOMEM;

	desc->min_uV = mpq4210_scale(MPQ4210_REF_MIN * MPQ4210_REF_STEP_UV,
				     r[0], r[1]);
	desc->uV_step = mpq4210_scale(MPQ4210_REF_STEP_UV, r[0], r[1]);

	slew = devm_kcalloc(dev, ARRAY_SIZE(mpq4210_ref_slew_rate),
			    sizeof(*slew), GFP_KERNEL);
	if (!slew)
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(mpq4210_ref_slew_rate); i++)
		slew[i] = mpq4210_scale(mpq4210_ref_slew_rate[i], r[0], r[1]);

	desc->ramp_delay_table = slew;

	ret = regmap_read(regmap, MPQ4210_CONTROL1, &val);
	if (ret)
		return dev_err_probe(dev, ret, "failed to read control 1\n");

	/* The core overrides this if the board sets regulator-ramp-delay. */
	desc->ramp_delay = slew[FIELD_GET(MPQ4210_CONTROL1_SR, val)];

	/*
	 * Documented as reserved, but the datasheet requires it to be set
	 * before the controller starts up.
	 */
	ret = regmap_set_bits(regmap, MPQ4210_CONTROL1,
			      MPQ4210_CONTROL1_RESERVED);
	if (ret)
		return dev_err_probe(dev, ret, "failed to write control 1\n");

	config.dev = dev;
	config.regmap = regmap;
	config.of_node = dev->of_node;
	config.init_data = of_get_regulator_init_data(dev, dev->of_node, desc);
	if (!config.init_data)
		return -ENOMEM;

	rdev = devm_regulator_register(dev, desc, &config);
	if (IS_ERR(rdev))
		return dev_err_probe(dev, PTR_ERR(rdev),
				     "failed to register regulator\n");

	return 0;
}

static const struct of_device_id mpq4210_of_match[] = {
	{ .compatible = "mps,mpq4210" },
	{ }
};
MODULE_DEVICE_TABLE(of, mpq4210_of_match);

static const struct i2c_device_id mpq4210_i2c_id[] = {
	{ .name = "mpq4210" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, mpq4210_i2c_id);

static struct i2c_driver mpq4210_regulator_driver = {
	.driver = {
		.name = "mpq4210",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
		.of_match_table = mpq4210_of_match,
	},
	.probe = mpq4210_i2c_probe,
	.id_table = mpq4210_i2c_id,
};

module_i2c_driver(mpq4210_regulator_driver);

MODULE_DESCRIPTION("Monolithic Power Systems MPQ4210 voltage regulator driver");
MODULE_AUTHOR("Tapio Reijonen <tapio.reijonen@vaisala.com>");
MODULE_LICENSE("GPL");
