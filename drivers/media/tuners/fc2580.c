/*
 * FCI FC2580 silicon tuner driver
 *
 * Copyright (C) 2012 Antti Palosaari <crope@iki.fi>
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License along
 *    with this program; if not, write to the Free Software Foundation, Inc.,
 *    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "fc2580_priv.h"

/*
 * TODO:
 * I2C write and read works only for one single register. Multiple registers
 * could not be accessed using normal register address auto-increment.
 * There could be (very likely) register to change that behavior....
 */

/* write single register conditionally only when value differs from 0xff
 * XXX: This is special routine meant only for writing fc2580_freq_regs_lut[]
 * values. Do not use for the other purposes. */
static int fc2580_wr_reg_ff(struct fc2580_dev *dev, u8 reg, u8 val)
{
	if (val == 0xff)
		return 0;
	else
		return regmap_write(dev->regmap, reg, val);
}

static int fc2580_set_params(struct dvb_frontend *fe)
{
	struct fc2580_dev *dev = fe->tuner_priv;
	struct i2c_client *client = dev->client;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	int ret, i;
	unsigned int uitmp, div_ref, div_ref_val, div_n, k, k_cw, div_out;
	u64 f_vco;
	u8 u8tmp, synth_config;
	unsigned long timeout;

	dev_dbg(&client->dev,
		"delivery_system=%u frequency=%u bandwidth_hz=%u\n",
		c->delivery_system, c->frequency, c->bandwidth_hz);

	/*
	 * Fractional-N synthesizer
	 *
	 *                      +---------------------------------------+
	 *                      v                                       |
	 *  Fref   +----+     +----+     +-------+         +----+     +------+     +---+
	 * ------> | /R | --> | PD | --> |  VCO  | ------> | /2 | --> | /N.F | <-- | K |
	 *         +----+     +----+     +-------+         +----+     +------+     +---+
	 *                                 |
	 *                                 |
	 *                                 v
	 *                               +-------+  Fout
	 *                               | /Rout | ------>
	 *                               +-------+
	 */
	for (i = 0; i < ARRAY_SIZE(fc2580_pll_lut); i++) {
		if (c->frequency <= fc2580_pll_lut[i].freq)
			break;
	}
	if (i == ARRAY_SIZE(fc2580_pll_lut)) {
		ret = -EINVAL;
		goto err;
	}

	#define DIV_PRE_N 2
	#define F_REF dev->clk
	div_out = fc2580_pll_lut[i].div_out;
	f_vco = (u64) c->frequency * div_out;
	synth_config = fc2580_pll_lut[i].band;
	if (f_vco < 2600000000ULL)
		synth_config |= 0x06;
	else
		synth_config |= 0x0e;

	/* select reference divider R (keep PLL div N in valid range) */
	#define DIV_N_MIN 76
	if (f_vco >= div_u64((u64) DIV_PRE_N * DIV_N_MIN * F_REF, 1)) {
		div_ref = 1;
		div_ref_val = 0x00;
	} else if (f_vco >= div_u64((u64) DIV_PRE_N * DIV_N_MIN * F_REF, 2)) {
		div_ref = 2;
		div_ref_val = 0x10;
	} else {
		div_ref = 4;
		div_ref_val = 0x20;
	}

	/* calculate PLL integer and fractional control word */
	uitmp = DIV_PRE_N * F_REF / div_ref;
	div_n = div_u64_rem(f_vco, uitmp, &k);
	k_cw = div_u64((u64) k * 0x100000, uitmp);

	dev_dbg(&client->dev,
		"frequency=%u f_vco=%llu F_REF=%u div_ref=%u div_n=%u k=%u div_out=%u k_cw=%0x\n",
		c->frequency, f_vco, F_REF, div_ref, div_n, k, div_out, k_cw);

	ret = regmap_write(dev->regmap, 0x02, synth_config);
	if (ret)
		goto err;

	ret = regmap_write(dev->regmap, 0x18, div_ref_val << 0 | k_cw >> 16);
	if (ret)
		goto err;

	ret = regmap_write(dev->regmap, 0x1a, (k_cw >> 8) & 0xff);
	if (ret)
		goto err;

	ret = regmap_write(dev->regmap, 0x1b, (k_cw >> 0) & 0xff);
	if (ret)
		goto err;

	ret = regmap_write(dev->regmap, 0x1c, div_n);
	if (ret)
		goto err;

	/* registers */
	for (i = 0; i < ARRAY_SIZE(fc2580_freq_regs_lut); i++) {
		if (c->frequency <= fc2580_freq_regs_lut[i].freq)
			break;
	}
	if (i == ARRAY_SIZE(fc2580_freq_regs_lut)) {
		ret = -EINVAL;
		goto err;
	}

	ret = fc2580_wr_reg_ff(dev, 0x25, fc2580_freq_regs_lut[i].r25_val);
	if (ret)
		goto err;

	ret = fc2580_wr_reg_ff(dev, 0x27, fc2580_freq_regs_lut[i].r27_val);
	if (ret)
		goto err;

	ret = fc2580_wr_reg_ff(dev, 0x28, fc2580_freq_regs_lut[i].r28_val);
	if (ret)
		goto err;

	ret = fc2580_wr_reg_ff(dev, 0x29, fc2580_freq_regs_lut[i].r29_val);
	if (ret)
		goto err;

	ret = fc2580_wr_reg_ff(dev, 0x2b, fc2580_freq_regs_lut[i].r2b_val);
	if (ret)
		goto err;

	ret = fc2580_wr_reg_ff(dev, 0x2c, fc2580_freq_regs_lut[i].r2c_val);
	if (ret)
		goto err;

	ret = fc2580_wr_reg_ff(dev, 0x2d, fc2580_freq_regs_lut[i].r2d_val);
	if (ret)
		goto err;

	ret = fc2580_wr_reg_ff(dev, 0x30, fc2580_freq_regs_lut[i].r30_val);
	if (ret)
		goto err;

	ret = fc2580_wr_reg_ff(dev, 0x44, fc2580_freq_regs_lut[i].r44_val);
	if (ret)
		goto err;

	ret = fc2580_wr_reg_ff(dev, 0x50, fc2580_freq_regs_lut[i].r50_val);
	if (ret)
		goto err;

	ret = fc2580_wr_reg_ff(dev, 0x53, fc2580_freq_regs_lut[i].r53_val);
	if (ret)
		goto err;

	ret = fc2580_wr_reg_ff(dev, 0x5f, fc2580_freq_regs_lut[i].r5f_val);
	if (ret)
		goto err;

	ret = fc2580_wr_reg_ff(dev, 0x61, fc2580_freq_regs_lut[i].r61_val);
	if (ret)
		goto err;

	ret = fc2580_wr_reg_ff(dev, 0x62, fc2580_freq_regs_lut[i].r62_val);
	if (ret)
		goto err;

	ret = fc2580_wr_reg_ff(dev, 0x63, fc2580_freq_regs_lut[i].r63_val);
	if (ret)
		goto err;

	ret = fc2580_wr_reg_ff(dev, 0x67, fc2580_freq_regs_lut[i].r67_val);
	if (ret)
		goto err;

	ret = fc2580_wr_reg_ff(dev, 0x68, fc2580_freq_regs_lut[i].r68_val);
	if (ret)
		goto err;

	ret = fc2580_wr_reg_ff(dev, 0x69, fc2580_freq_regs_lut[i].r69_val);
	if (ret)
		goto err;

	ret = fc2580_wr_reg_ff(dev, 0x6a, fc2580_freq_regs_lut[i].r6a_val);
	if (ret)
		goto err;

	ret = fc2580_wr_reg_ff(dev, 0x6b, fc2580_freq_regs_lut[i].r6b_val);
	if (ret)
		goto err;

	ret = fc2580_wr_reg_ff(dev, 0x6c, fc2580_freq_regs_lut[i].r6c_val);
	if (ret)
		goto err;

	ret = fc2580_wr_reg_ff(dev, 0x6d, fc2580_freq_regs_lut[i].r6d_val);
	if (ret)
		goto err;

	ret = fc2580_wr_reg_ff(dev, 0x6e, fc2580_freq_regs_lut[i].r6e_val);
	if (ret)
		goto err;

	ret = fc2580_wr_reg_ff(dev, 0x6f, fc2580_freq_regs_lut[i].r6f_val);
	if (ret)
		goto err;

	/* IF filters */
	for (i = 0; i < ARRAY_SIZE(fc2580_if_filter_lut); i++) {
		if (c->bandwidth_hz <= fc2580_if_filter_lut[i].freq)
			break;
	}
	if (i == ARRAY_SIZE(fc2580_if_filter_lut)) {
		ret = -EINVAL;
		goto err;
	}

	ret = regmap_write(dev->regmap, 0x36, fc2580_if_filter_lut[i].r36_val);
	if (ret)
		goto err;

	u8tmp = div_u64((u64) dev->clk * fc2580_if_filter_lut[i].mul,
			1000000000);
	ret = regmap_write(dev->regmap, 0x37, u8tmp);
	if (ret)
		goto err;

	ret = regmap_write(dev->regmap, 0x39, fc2580_if_filter_lut[i].r39_val);
	if (ret)
		goto err;

	timeout = jiffies + msecs_to_jiffies(30);
	for (uitmp = ~0xc0; !time_after(jiffies, timeout) && uitmp != 0xc0;) {
		/* trigger filter */
		ret = regmap_write(dev->regmap, 0x2e, 0x09);
		if (ret)
			goto err;

		/* locked when [7:6] are set (val: d7 6MHz, d5 7MHz, cd 8MHz) */
		ret = regmap_read(dev->regmap, 0x2f, &uitmp);
		if (ret)
			goto err;
		uitmp &= 0xc0;

		ret = regmap_write(dev->regmap, 0x2e, 0x01);
		if (ret)
			goto err;
	}
	if (uitmp != 0xc0)
		dev_dbg(&client->dev, "filter did not lock %02x\n", uitmp);

	return 0;
err:
	dev_dbg(&client->dev, "failed=%d\n", ret);
	return ret;
}

static int fc2580_init(struct dvb_frontend *fe)
{
	struct fc2580_dev *dev = fe->tuner_priv;
	struct i2c_client *client = dev->client;
	int ret, i;

	dev_dbg(&client->dev, "\n");

	for (i = 0; i < ARRAY_SIZE(fc2580_init_reg_vals); i++) {
		ret = regmap_write(dev->regmap, fc2580_init_reg_vals[i].reg,
				fc2580_init_reg_vals[i].val);
		if (ret)
			goto err;
	}

	return 0;
err:
	dev_dbg(&client->dev, "failed=%d\n", ret);
	return ret;
}

static int fc2580_sleep(struct dvb_frontend *fe)
{
	struct fc2580_dev *dev = fe->tuner_priv;
	struct i2c_client *client = dev->client;
	int ret;

	dev_dbg(&client->dev, "\n");

	ret = regmap_write(dev->regmap, 0x02, 0x0a);
	if (ret)
		goto err;

	return 0;
err:
	dev_dbg(&client->dev, "failed=%d\n", ret);
	return ret;
}

static int fc2580_get_if_frequency(struct dvb_frontend *fe, u32 *frequency)
{
	struct fc2580_dev *dev = fe->tuner_priv;
	struct i2c_client *client = dev->client;

	dev_dbg(&client->dev, "\n");

	*frequency = 0; /* Zero-IF */

	return 0;
}

static const struct dvb_tuner_ops fc2580_tuner_ops = {
	.info = {
		.name           = "FCI FC2580",
		.frequency_min  = 174000000,
		.frequency_max  = 862000000,
	},

	.init = fc2580_init,
	.sleep = fc2580_sleep,
	.set_params = fc2580_set_params,

	.get_if_frequency = fc2580_get_if_frequency,
};

static int fc2580_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct fc2580_dev *dev;
	struct fc2580_platform_data *pdata = client->dev.platform_data;
	struct dvb_frontend *fe = pdata->dvb_frontend;
	int ret;
	unsigned int uitmp;
	static const struct regmap_config regmap_config = {
		.reg_bits = 8,
		.val_bits = 8,
	};

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev) {
		ret = -ENOMEM;
		goto err;
	}

	if (pdata->clk)
		dev->clk = pdata->clk;
	else
		dev->clk = 16384000; /* internal clock */
	dev->client = client;
	dev->regmap = devm_regmap_init_i2c(client, &regmap_config);
	if (IS_ERR(dev->regmap)) {
		ret = PTR_ERR(dev->regmap);
		goto err_kfree;
	}

	/* check if the tuner is there */
	ret = regmap_read(dev->regmap, 0x01, &uitmp);
	if (ret)
		goto err_kfree;

	dev_dbg(&client->dev, "chip_id=%02x\n", uitmp);

	switch (uitmp) {
	case 0x56:
	case 0x5a:
		break;
	default:
		goto err_kfree;
	}

	fe->tuner_priv = dev;
	memcpy(&fe->ops.tuner_ops, &fc2580_tuner_ops,
			sizeof(struct dvb_tuner_ops));
	i2c_set_clientdata(client, dev);

	dev_info(&client->dev, "FCI FC2580 successfully identified\n");
	return 0;
err_kfree:
	kfree(dev);
err:
	dev_dbg(&client->dev, "failed=%d\n", ret);
	return ret;
}

static int fc2580_remove(struct i2c_client *client)
{
	struct fc2580_dev *dev = i2c_get_clientdata(client);

	dev_dbg(&client->dev, "\n");

	kfree(dev);
	return 0;
}

static const struct i2c_device_id fc2580_id_table[] = {
	{"fc2580", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, fc2580_id_table);

static struct i2c_driver fc2580_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= "fc2580",
		.suppress_bind_attrs = true,
	},
	.probe		= fc2580_probe,
	.remove		= fc2580_remove,
	.id_table	= fc2580_id_table,
};

module_i2c_driver(fc2580_driver);

MODULE_DESCRIPTION("FCI FC2580 silicon tuner driver");
MODULE_AUTHOR("Antti Palosaari <crope@iki.fi>");
MODULE_LICENSE("GPL");
