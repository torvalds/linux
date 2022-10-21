// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for Silicon Labs Si570/Si571 Programmable XO/VCXO
 *
 * Copyright (C) 2010, 2011 Ericsson AB.
 * Copyright (C) 2011 Guenter Roeck.
 * Copyright (C) 2011 - 2021 Xilinx Inc.
 *
 * Author: Guenter Roeck <guenter.roeck@ericsson.com>
 *	   Sören Brinkmann <soren.brinkmann@xilinx.com>
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/slab.h>

/* Si570 registers */
#define SI570_REG_HS_N1		7
#define SI570_REG_N1_RFREQ0	8
#define SI570_REG_RFREQ1	9
#define SI570_REG_RFREQ2	10
#define SI570_REG_RFREQ3	11
#define SI570_REG_RFREQ4	12
#define SI570_REG_CONTROL	135
#define SI570_REG_FREEZE_DCO	137
#define SI570_DIV_OFFSET_7PPM	6

#define HS_DIV_SHIFT		5
#define HS_DIV_MASK		0xe0
#define HS_DIV_OFFSET		4
#define N1_6_2_MASK		0x1f
#define N1_1_0_MASK		0xc0
#define RFREQ_37_32_MASK	0x3f

#define SI570_MIN_FREQ		10000000L
#define SI570_MAX_FREQ		1417500000L
#define SI598_MAX_FREQ		525000000L

#define FDCO_MIN		4850000000LL
#define FDCO_MAX		5670000000LL

#define SI570_CNTRL_RECALL	(1 << 0)
#define SI570_CNTRL_FREEZE_M	(1 << 5)
#define SI570_CNTRL_NEWFREQ	(1 << 6)

#define SI570_FREEZE_DCO	(1 << 4)

/**
 * struct clk_si570:
 * @hw:	Clock hw struct
 * @regmap:	Device's regmap
 * @div_offset:	Rgister offset for dividers
 * @max_freq:	Maximum frequency for this device
 * @fxtal:	Factory xtal frequency
 * @n1:		Clock divider N1
 * @hs_div:	Clock divider HSDIV
 * @rfreq:	Clock multiplier RFREQ
 * @frequency:	Current output frequency
 * @i2c_client:	I2C client pointer
 */
struct clk_si570 {
	struct clk_hw hw;
	struct regmap *regmap;
	unsigned int div_offset;
	u64 max_freq;
	u64 fxtal;
	unsigned int n1;
	unsigned int hs_div;
	u64 rfreq;
	u64 frequency;
	struct i2c_client *i2c_client;
};
#define to_clk_si570(_hw)	container_of(_hw, struct clk_si570, hw)

enum clk_si570_variant {
	si57x,
	si59x
};

/**
 * si570_get_divs() - Read clock dividers from HW
 * @data:	Pointer to struct clk_si570
 * @rfreq:	Fractional multiplier (output)
 * @n1:		Divider N1 (output)
 * @hs_div:	Divider HSDIV (output)
 * Returns 0 on success, negative errno otherwise.
 *
 * Retrieve clock dividers and multipliers from the HW.
 */
static int si570_get_divs(struct clk_si570 *data, u64 *rfreq,
		unsigned int *n1, unsigned int *hs_div)
{
	int err;
	u8 reg[6];
	u64 tmp;

	err = regmap_bulk_read(data->regmap, SI570_REG_HS_N1 + data->div_offset,
			reg, ARRAY_SIZE(reg));
	if (err)
		return err;

	*hs_div = ((reg[0] & HS_DIV_MASK) >> HS_DIV_SHIFT) + HS_DIV_OFFSET;
	*n1 = ((reg[0] & N1_6_2_MASK) << 2) + ((reg[1] & N1_1_0_MASK) >> 6) + 1;
	/* Handle invalid cases */
	if (*n1 > 1)
		*n1 &= ~1;

	tmp = reg[1] & RFREQ_37_32_MASK;
	tmp = (tmp << 8) + reg[2];
	tmp = (tmp << 8) + reg[3];
	tmp = (tmp << 8) + reg[4];
	tmp = (tmp << 8) + reg[5];
	*rfreq = tmp;

	return 0;
}

/**
 * si570_get_defaults() - Get default values
 * @data:	Driver data structure
 * @fout:	Factory frequency output
 * @skip_recall:	If true, don't recall NVM into RAM
 * Returns 0 on success, negative errno otherwise.
 */
static int si570_get_defaults(struct clk_si570 *data, u64 fout,
			      bool skip_recall)
{
	int err;
	u64 fdco;

	if (!skip_recall)
		regmap_write(data->regmap, SI570_REG_CONTROL,
			     SI570_CNTRL_RECALL);

	err = si570_get_divs(data, &data->rfreq, &data->n1, &data->hs_div);
	if (err)
		return err;

	/*
	 * Accept optional precision loss to avoid arithmetic overflows.
	 * Acceptable per Silicon Labs Application Note AN334.
	 */
	fdco = fout * data->n1 * data->hs_div;
	if (fdco >= (1LL << 36))
		data->fxtal = div64_u64(fdco << 24, data->rfreq >> 4);
	else
		data->fxtal = div64_u64(fdco << 28, data->rfreq);

	data->frequency = fout;

	return 0;
}

/**
 * si570_update_rfreq() - Update clock multiplier
 * @data:	Driver data structure
 * Passes on regmap_bulk_write() return value.
 */
static int si570_update_rfreq(struct clk_si570 *data)
{
	u8 reg[5];

	reg[0] = ((data->n1 - 1) << 6) |
		((data->rfreq >> 32) & RFREQ_37_32_MASK);
	reg[1] = (data->rfreq >> 24) & 0xff;
	reg[2] = (data->rfreq >> 16) & 0xff;
	reg[3] = (data->rfreq >> 8) & 0xff;
	reg[4] = data->rfreq & 0xff;

	return regmap_bulk_write(data->regmap, SI570_REG_N1_RFREQ0 +
			data->div_offset, reg, ARRAY_SIZE(reg));
}

/**
 * si570_calc_divs() - Caluclate clock dividers
 * @frequency:	Target frequency
 * @data:	Driver data structure
 * @out_rfreq:	RFREG fractional multiplier (output)
 * @out_n1:	Clock divider N1 (output)
 * @out_hs_div:	Clock divider HSDIV (output)
 * Returns 0 on success, negative errno otherwise.
 *
 * Calculate the clock dividers (@out_hs_div, @out_n1) and clock multiplier
 * (@out_rfreq) for a given target @frequency.
 */
static int si570_calc_divs(unsigned long frequency, struct clk_si570 *data,
		u64 *out_rfreq, unsigned int *out_n1, unsigned int *out_hs_div)
{
	int i;
	unsigned int n1, hs_div;
	u64 fdco, best_fdco = ULLONG_MAX;
	static const uint8_t si570_hs_div_values[] = { 11, 9, 7, 6, 5, 4 };

	for (i = 0; i < ARRAY_SIZE(si570_hs_div_values); i++) {
		hs_div = si570_hs_div_values[i];
		/* Calculate lowest possible value for n1 */
		n1 = div_u64(div_u64(FDCO_MIN, hs_div), frequency);
		if (!n1 || (n1 & 1))
			n1++;
		while (n1 <= 128) {
			fdco = (u64)frequency * (u64)hs_div * (u64)n1;
			if (fdco > FDCO_MAX)
				break;
			if (fdco >= FDCO_MIN && fdco < best_fdco) {
				*out_n1 = n1;
				*out_hs_div = hs_div;
				*out_rfreq = div64_u64(fdco << 28, data->fxtal);
				best_fdco = fdco;
			}
			n1 += (n1 == 1 ? 1 : 2);
		}
	}

	if (best_fdco == ULLONG_MAX)
		return -EINVAL;

	return 0;
}

static unsigned long si570_recalc_rate(struct clk_hw *hw,
		unsigned long parent_rate)
{
	int err;
	u64 rfreq, rate;
	unsigned int n1, hs_div;
	struct clk_si570 *data = to_clk_si570(hw);

	err = si570_get_divs(data, &rfreq, &n1, &hs_div);
	if (err) {
		dev_err(&data->i2c_client->dev, "unable to recalc rate\n");
		return data->frequency;
	}

	rfreq = div_u64(rfreq, hs_div * n1);
	rate = (data->fxtal * rfreq) >> 28;

	return rate;
}

static long si570_round_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long *parent_rate)
{
	int err;
	u64 rfreq;
	unsigned int n1, hs_div;
	struct clk_si570 *data = to_clk_si570(hw);

	if (!rate)
		return 0;

	if (div64_u64(abs(rate - data->frequency) * 10000LL,
				data->frequency) < 35) {
		rfreq = div64_u64((data->rfreq * rate) +
				div64_u64(data->frequency, 2), data->frequency);
		n1 = data->n1;
		hs_div = data->hs_div;

	} else {
		err = si570_calc_divs(rate, data, &rfreq, &n1, &hs_div);
		if (err) {
			dev_err(&data->i2c_client->dev,
					"unable to round rate\n");
			return 0;
		}
	}

	return rate;
}

/**
 * si570_set_frequency() - Adjust output frequency
 * @data:	Driver data structure
 * @frequency:	Target frequency
 * Returns 0 on success.
 *
 * Update output frequency for big frequency changes (> 3,500 ppm).
 */
static int si570_set_frequency(struct clk_si570 *data, unsigned long frequency)
{
	int err;

	err = si570_calc_divs(frequency, data, &data->rfreq, &data->n1,
			&data->hs_div);
	if (err)
		return err;

	/*
	 * The DCO reg should be accessed with a read-modify-write operation
	 * per AN334
	 */
	regmap_write(data->regmap, SI570_REG_FREEZE_DCO, SI570_FREEZE_DCO);
	regmap_write(data->regmap, SI570_REG_HS_N1 + data->div_offset,
			((data->hs_div - HS_DIV_OFFSET) << HS_DIV_SHIFT) |
			(((data->n1 - 1) >> 2) & N1_6_2_MASK));
	si570_update_rfreq(data);
	regmap_write(data->regmap, SI570_REG_FREEZE_DCO, 0);
	regmap_write(data->regmap, SI570_REG_CONTROL, SI570_CNTRL_NEWFREQ);

	/* Applying a new frequency can take up to 10ms */
	usleep_range(10000, 12000);

	return 0;
}

/**
 * si570_set_frequency_small() - Adjust output frequency
 * @data:	Driver data structure
 * @frequency:	Target frequency
 * Returns 0 on success.
 *
 * Update output frequency for small frequency changes (< 3,500 ppm).
 */
static int si570_set_frequency_small(struct clk_si570 *data,
				     unsigned long frequency)
{
	/*
	 * This is a re-implementation of DIV_ROUND_CLOSEST
	 * using the div64_u64 function lieu of letting the compiler
	 * insert EABI calls
	 */
	data->rfreq = div64_u64((data->rfreq * frequency) +
			div_u64(data->frequency, 2), data->frequency);
	regmap_write(data->regmap, SI570_REG_CONTROL, SI570_CNTRL_FREEZE_M);
	si570_update_rfreq(data);
	regmap_write(data->regmap, SI570_REG_CONTROL, 0);

	/* Applying a new frequency (small change) can take up to 100us */
	usleep_range(100, 200);

	return 0;
}

static int si570_set_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long parent_rate)
{
	struct clk_si570 *data = to_clk_si570(hw);
	struct i2c_client *client = data->i2c_client;
	int err;

	if (rate < SI570_MIN_FREQ || rate > data->max_freq) {
		dev_err(&client->dev,
			"requested frequency %lu Hz is out of range\n", rate);
		return -EINVAL;
	}

	if (div64_u64(abs(rate - data->frequency) * 10000LL,
				data->frequency) < 35)
		err = si570_set_frequency_small(data, rate);
	else
		err = si570_set_frequency(data, rate);

	if (err)
		return err;

	data->frequency = rate;

	return 0;
}

static const struct clk_ops si570_clk_ops = {
	.recalc_rate = si570_recalc_rate,
	.round_rate = si570_round_rate,
	.set_rate = si570_set_rate,
};

static bool si570_regmap_is_volatile(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case SI570_REG_CONTROL:
		return true;
	default:
		return false;
	}
}

static bool si570_regmap_is_writeable(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case SI570_REG_HS_N1 ... (SI570_REG_RFREQ4 + SI570_DIV_OFFSET_7PPM):
	case SI570_REG_CONTROL:
	case SI570_REG_FREEZE_DCO:
		return true;
	default:
		return false;
	}
}

static const struct regmap_config si570_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.cache_type = REGCACHE_RBTREE,
	.max_register = 137,
	.writeable_reg = si570_regmap_is_writeable,
	.volatile_reg = si570_regmap_is_volatile,
};

static const struct i2c_device_id si570_id[] = {
	{ "si570", si57x },
	{ "si571", si57x },
	{ "si598", si59x },
	{ "si599", si59x },
	{ }
};
MODULE_DEVICE_TABLE(i2c, si570_id);

static int si570_probe(struct i2c_client *client)
{
	struct clk_si570 *data;
	struct clk_init_data init;
	const struct i2c_device_id *id = i2c_match_id(si570_id, client);
	u32 initial_fout, factory_fout, stability;
	bool skip_recall;
	int err;
	enum clk_si570_variant variant = id->driver_data;

	data = devm_kzalloc(&client->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	init.ops = &si570_clk_ops;
	init.flags = 0;
	init.num_parents = 0;
	data->hw.init = &init;
	data->i2c_client = client;

	if (variant == si57x) {
		err = of_property_read_u32(client->dev.of_node,
				"temperature-stability", &stability);
		if (err) {
			dev_err(&client->dev,
				  "'temperature-stability' property missing\n");
			return err;
		}
		/* adjust register offsets for 7ppm devices */
		if (stability == 7)
			data->div_offset = SI570_DIV_OFFSET_7PPM;

		data->max_freq = SI570_MAX_FREQ;
	} else {
		data->max_freq = SI598_MAX_FREQ;
	}

	if (of_property_read_string(client->dev.of_node, "clock-output-names",
			&init.name))
		init.name = client->dev.of_node->name;

	err = of_property_read_u32(client->dev.of_node, "factory-fout",
			&factory_fout);
	if (err) {
		dev_err(&client->dev, "'factory-fout' property missing\n");
		return err;
	}

	skip_recall = of_property_read_bool(client->dev.of_node,
					    "silabs,skip-recall");

	data->regmap = devm_regmap_init_i2c(client, &si570_regmap_config);
	if (IS_ERR(data->regmap)) {
		dev_err(&client->dev, "failed to allocate register map\n");
		return PTR_ERR(data->regmap);
	}

	i2c_set_clientdata(client, data);
	err = si570_get_defaults(data, factory_fout, skip_recall);
	if (err)
		return err;

	err = devm_clk_hw_register(&client->dev, &data->hw);
	if (err) {
		dev_err(&client->dev, "clock registration failed\n");
		return err;
	}
	err = of_clk_add_hw_provider(client->dev.of_node, of_clk_hw_simple_get,
				     &data->hw);
	if (err) {
		dev_err(&client->dev, "unable to add clk provider\n");
		return err;
	}

	/* Read the requested initial output frequency from device tree */
	if (!of_property_read_u32(client->dev.of_node, "clock-frequency",
				&initial_fout)) {
		err = clk_set_rate(data->hw.clk, initial_fout);
		if (err) {
			of_clk_del_provider(client->dev.of_node);
			return err;
		}
	}

	/* Display a message indicating that we've successfully registered */
	dev_info(&client->dev, "registered, current frequency %llu Hz\n",
			data->frequency);

	return 0;
}

static void si570_remove(struct i2c_client *client)
{
	of_clk_del_provider(client->dev.of_node);
}

static const struct of_device_id clk_si570_of_match[] = {
	{ .compatible = "silabs,si570" },
	{ .compatible = "silabs,si571" },
	{ .compatible = "silabs,si598" },
	{ .compatible = "silabs,si599" },
	{ },
};
MODULE_DEVICE_TABLE(of, clk_si570_of_match);

static struct i2c_driver si570_driver = {
	.driver = {
		.name = "si570",
		.of_match_table = clk_si570_of_match,
	},
	.probe_new	= si570_probe,
	.remove		= si570_remove,
	.id_table	= si570_id,
};
module_i2c_driver(si570_driver);

MODULE_AUTHOR("Guenter Roeck <guenter.roeck@ericsson.com>");
MODULE_AUTHOR("Soeren Brinkmann <soren.brinkmann@xilinx.com>");
MODULE_DESCRIPTION("Si570 driver");
MODULE_LICENSE("GPL");
