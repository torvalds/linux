/*
 * Driver for Silicon Labs Si514 Programmable Oscillator
 *
 * Copyright (C) 2015 Topic Embedded Products
 *
 * Author: Mike Looijmans <mike.looijmans@topic.nl>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/slab.h>

/* I2C registers */
#define SI514_REG_LP		0
#define SI514_REG_M_FRAC1	5
#define SI514_REG_M_FRAC2	6
#define SI514_REG_M_FRAC3	7
#define SI514_REG_M_INT_FRAC	8
#define SI514_REG_M_INT		9
#define SI514_REG_HS_DIV	10
#define SI514_REG_LS_HS_DIV	11
#define SI514_REG_OE_STATE	14
#define SI514_REG_RESET		128
#define SI514_REG_CONTROL	132

/* Register values */
#define SI514_RESET_RST		BIT(7)

#define SI514_CONTROL_FCAL	BIT(0)
#define SI514_CONTROL_OE	BIT(2)

#define SI514_MIN_FREQ	    100000U
#define SI514_MAX_FREQ	 250000000U

#define FXO		  31980000U

#define FVCO_MIN	2080000000U
#define FVCO_MAX	2500000000U

#define HS_DIV_MAX	1022

struct clk_si514 {
	struct clk_hw hw;
	struct regmap *regmap;
	struct i2c_client *i2c_client;
};
#define to_clk_si514(_hw)	container_of(_hw, struct clk_si514, hw)

/* Multiplier/divider settings */
struct clk_si514_muldiv {
	u32 m_frac;  /* 29-bit Fractional part of multiplier M */
	u8 m_int; /* Integer part of multiplier M, 65..78 */
	u8 ls_div_bits; /* 2nd divider, as 2^x */
	u16 hs_div; /* 1st divider, must be even and 10<=x<=1022 */
};

/* Enables or disables the output driver */
static int si514_enable_output(struct clk_si514 *data, bool enable)
{
	return regmap_update_bits(data->regmap, SI514_REG_CONTROL,
		SI514_CONTROL_OE, enable ? SI514_CONTROL_OE : 0);
}

/* Retrieve clock multiplier and dividers from hardware */
static int si514_get_muldiv(struct clk_si514 *data,
	struct clk_si514_muldiv *settings)
{
	int err;
	u8 reg[7];

	err = regmap_bulk_read(data->regmap, SI514_REG_M_FRAC1,
			reg, ARRAY_SIZE(reg));
	if (err)
		return err;

	settings->m_frac = reg[0] | reg[1] << 8 | reg[2] << 16 |
			   (reg[3] & 0x1F) << 24;
	settings->m_int = (reg[4] & 0x3f) << 3 | reg[3] >> 5;
	settings->ls_div_bits = (reg[6] >> 4) & 0x07;
	settings->hs_div = (reg[6] & 0x03) << 8 | reg[5];
	return 0;
}

static int si514_set_muldiv(struct clk_si514 *data,
	struct clk_si514_muldiv *settings)
{
	u8 lp;
	u8 reg[7];
	int err;

	/* Calculate LP1/LP2 according to table 13 in the datasheet */
	/* 65.259980246 */
	if (settings->m_int < 65 ||
		(settings->m_int == 65 && settings->m_frac <= 139575831))
		lp = 0x22;
	/* 67.859763463 */
	else if (settings->m_int < 67 ||
		(settings->m_int == 67 && settings->m_frac <= 461581994))
		lp = 0x23;
	/* 72.937624981 */
	else if (settings->m_int < 72 ||
		(settings->m_int == 72 && settings->m_frac <= 503383578))
		lp = 0x33;
	/* 75.843265046 */
	else if (settings->m_int < 75 ||
		(settings->m_int == 75 && settings->m_frac <= 452724474))
		lp = 0x34;
	else
		lp = 0x44;

	err = regmap_write(data->regmap, SI514_REG_LP, lp);
	if (err < 0)
		return err;

	reg[0] = settings->m_frac;
	reg[1] = settings->m_frac >> 8;
	reg[2] = settings->m_frac >> 16;
	reg[3] = settings->m_frac >> 24 | settings->m_int << 5;
	reg[4] = settings->m_int >> 3;
	reg[5] = settings->hs_div;
	reg[6] = (settings->hs_div >> 8) | (settings->ls_div_bits << 4);

	err = regmap_bulk_write(data->regmap, SI514_REG_HS_DIV, reg + 5, 2);
	if (err < 0)
		return err;
	/*
	 * Writing to SI514_REG_M_INT_FRAC triggers the clock change, so that
	 * must be written last
	 */
	return regmap_bulk_write(data->regmap, SI514_REG_M_FRAC1, reg, 5);
}

/* Calculate divider settings for a given frequency */
static int si514_calc_muldiv(struct clk_si514_muldiv *settings,
	unsigned long frequency)
{
	u64 m;
	u32 ls_freq;
	u32 tmp;
	u8 res;

	if ((frequency < SI514_MIN_FREQ) || (frequency > SI514_MAX_FREQ))
		return -EINVAL;

	/* Determine the minimum value of LS_DIV and resulting target freq. */
	ls_freq = frequency;
	if (frequency >= (FVCO_MIN / HS_DIV_MAX))
		settings->ls_div_bits = 0;
	else {
		res = 1;
		tmp = 2 * HS_DIV_MAX;
		while (tmp <= (HS_DIV_MAX * 32)) {
			if ((frequency * tmp) >= FVCO_MIN)
				break;
			++res;
			tmp <<= 1;
		}
		settings->ls_div_bits = res;
		ls_freq = frequency << res;
	}

	/* Determine minimum HS_DIV, round up to even number */
	settings->hs_div = DIV_ROUND_UP(FVCO_MIN >> 1, ls_freq) << 1;

	/* M = LS_DIV x HS_DIV x frequency / F_XO (in fixed-point) */
	m = ((u64)(ls_freq * settings->hs_div) << 29) + (FXO / 2);
	do_div(m, FXO);
	settings->m_frac = (u32)m & (BIT(29) - 1);
	settings->m_int = (u32)(m >> 29);

	return 0;
}

/* Calculate resulting frequency given the register settings */
static unsigned long si514_calc_rate(struct clk_si514_muldiv *settings)
{
	u64 m = settings->m_frac | ((u64)settings->m_int << 29);
	u32 d = settings->hs_div * BIT(settings->ls_div_bits);

	return ((u32)(((m * FXO) + (FXO / 2)) >> 29)) / d;
}

static unsigned long si514_recalc_rate(struct clk_hw *hw,
		unsigned long parent_rate)
{
	struct clk_si514 *data = to_clk_si514(hw);
	struct clk_si514_muldiv settings;
	int err;

	err = si514_get_muldiv(data, &settings);
	if (err) {
		dev_err(&data->i2c_client->dev, "unable to retrieve settings\n");
		return 0;
	}

	return si514_calc_rate(&settings);
}

static long si514_round_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long *parent_rate)
{
	struct clk_si514_muldiv settings;
	int err;

	if (!rate)
		return 0;

	err = si514_calc_muldiv(&settings, rate);
	if (err)
		return err;

	return si514_calc_rate(&settings);
}

/*
 * Update output frequency for big frequency changes (> 1000 ppm).
 * The chip supports <1000ppm changes "on the fly", we haven't implemented
 * that here.
 */
static int si514_set_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long parent_rate)
{
	struct clk_si514 *data = to_clk_si514(hw);
	struct clk_si514_muldiv settings;
	int err;

	err = si514_calc_muldiv(&settings, rate);
	if (err)
		return err;

	si514_enable_output(data, false);

	err = si514_set_muldiv(data, &settings);
	if (err < 0)
		return err; /* Undefined state now, best to leave disabled */

	/* Trigger calibration */
	err = regmap_write(data->regmap, SI514_REG_CONTROL, SI514_CONTROL_FCAL);
	if (err < 0)
		return err;

	/* Applying a new frequency can take up to 10ms */
	usleep_range(10000, 12000);

	si514_enable_output(data, true);

	return err;
}

static const struct clk_ops si514_clk_ops = {
	.recalc_rate = si514_recalc_rate,
	.round_rate = si514_round_rate,
	.set_rate = si514_set_rate,
};

static bool si514_regmap_is_volatile(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case SI514_REG_CONTROL:
	case SI514_REG_RESET:
		return true;
	default:
		return false;
	}
}

static bool si514_regmap_is_writeable(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case SI514_REG_LP:
	case SI514_REG_M_FRAC1 ... SI514_REG_LS_HS_DIV:
	case SI514_REG_OE_STATE:
	case SI514_REG_RESET:
	case SI514_REG_CONTROL:
		return true;
	default:
		return false;
	}
}

static const struct regmap_config si514_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.cache_type = REGCACHE_RBTREE,
	.max_register = SI514_REG_CONTROL,
	.writeable_reg = si514_regmap_is_writeable,
	.volatile_reg = si514_regmap_is_volatile,
};

static int si514_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	struct clk_si514 *data;
	struct clk_init_data init;
	struct clk *clk;
	int err;

	data = devm_kzalloc(&client->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	init.ops = &si514_clk_ops;
	init.flags = 0;
	init.num_parents = 0;
	data->hw.init = &init;
	data->i2c_client = client;

	if (of_property_read_string(client->dev.of_node, "clock-output-names",
			&init.name))
		init.name = client->dev.of_node->name;

	data->regmap = devm_regmap_init_i2c(client, &si514_regmap_config);
	if (IS_ERR(data->regmap)) {
		dev_err(&client->dev, "failed to allocate register map\n");
		return PTR_ERR(data->regmap);
	}

	i2c_set_clientdata(client, data);

	clk = devm_clk_register(&client->dev, &data->hw);
	if (IS_ERR(clk)) {
		dev_err(&client->dev, "clock registration failed\n");
		return PTR_ERR(clk);
	}
	err = of_clk_add_provider(client->dev.of_node, of_clk_src_simple_get,
			clk);
	if (err) {
		dev_err(&client->dev, "unable to add clk provider\n");
		return err;
	}

	return 0;
}

static int si514_remove(struct i2c_client *client)
{
	of_clk_del_provider(client->dev.of_node);
	return 0;
}

static const struct i2c_device_id si514_id[] = {
	{ "si514", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, si514_id);

static const struct of_device_id clk_si514_of_match[] = {
	{ .compatible = "silabs,si514" },
	{ },
};
MODULE_DEVICE_TABLE(of, clk_si514_of_match);

static struct i2c_driver si514_driver = {
	.driver = {
		.name = "si514",
		.of_match_table = clk_si514_of_match,
	},
	.probe		= si514_probe,
	.remove		= si514_remove,
	.id_table	= si514_id,
};
module_i2c_driver(si514_driver);

MODULE_AUTHOR("Mike Looijmans <mike.looijmans@topic.nl>");
MODULE_DESCRIPTION("Si514 driver");
MODULE_LICENSE("GPL");
