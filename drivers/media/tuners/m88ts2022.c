/*
 * Montage M88TS2022 silicon tuner driver
 *
 * Copyright (C) 2013 Antti Palosaari <crope@iki.fi>
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
 * Some calculations are taken from existing TS2020 driver.
 */

#include "m88ts2022_priv.h"

static int m88ts2022_cmd(struct m88ts2022_dev *dev, int op, int sleep, u8 reg,
		u8 mask, u8 val, u8 *reg_val)
{
	int ret, i;
	unsigned int utmp;
	struct m88ts2022_reg_val reg_vals[] = {
		{0x51, 0x1f - op},
		{0x51, 0x1f},
		{0x50, 0x00 + op},
		{0x50, 0x00},
	};

	for (i = 0; i < 2; i++) {
		dev_dbg(&dev->client->dev,
				"i=%d op=%02x reg=%02x mask=%02x val=%02x\n",
				i, op, reg, mask, val);

		for (i = 0; i < ARRAY_SIZE(reg_vals); i++) {
			ret = regmap_write(dev->regmap, reg_vals[i].reg,
					reg_vals[i].val);
			if (ret)
				goto err;
		}

		usleep_range(sleep * 1000, sleep * 10000);

		ret = regmap_read(dev->regmap, reg, &utmp);
		if (ret)
			goto err;

		if ((utmp & mask) != val)
			break;
	}

	if (reg_val)
		*reg_val = utmp;
err:
	return ret;
}

static int m88ts2022_set_params(struct dvb_frontend *fe)
{
	struct m88ts2022_dev *dev = fe->tuner_priv;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	int ret;
	unsigned int utmp, frequency_khz, frequency_offset_khz, f_3db_hz;
	unsigned int f_ref_khz, f_vco_khz, div_ref, div_out, pll_n, gdiv28;
	u8 buf[3], u8tmp, cap_code, lpf_gm, lpf_mxdiv, div_max, div_min;
	u16 u16tmp;

	dev_dbg(&dev->client->dev,
			"frequency=%d symbol_rate=%d rolloff=%d\n",
			c->frequency, c->symbol_rate, c->rolloff);
	/*
	 * Integer-N PLL synthesizer
	 * kHz is used for all calculations to keep calculations within 32-bit
	 */
	f_ref_khz = DIV_ROUND_CLOSEST(dev->cfg.clock, 1000);
	div_ref = DIV_ROUND_CLOSEST(f_ref_khz, 2000);

	if (c->symbol_rate < 5000000)
		frequency_offset_khz = 3000; /* 3 MHz */
	else
		frequency_offset_khz = 0;

	frequency_khz = c->frequency + frequency_offset_khz;

	if (frequency_khz < 1103000) {
		div_out = 4;
		u8tmp = 0x1b;
	} else {
		div_out = 2;
		u8tmp = 0x0b;
	}

	buf[0] = u8tmp;
	buf[1] = 0x40;
	ret = regmap_bulk_write(dev->regmap, 0x10, buf, 2);
	if (ret)
		goto err;

	f_vco_khz = frequency_khz * div_out;
	pll_n = f_vco_khz * div_ref / f_ref_khz;
	pll_n += pll_n % 2;
	dev->frequency_khz = pll_n * f_ref_khz / div_ref / div_out;

	if (pll_n < 4095)
		u16tmp = pll_n - 1024;
	else if (pll_n < 6143)
		u16tmp = pll_n + 1024;
	else
		u16tmp = pll_n + 3072;

	buf[0] = (u16tmp >> 8) & 0x3f;
	buf[1] = (u16tmp >> 0) & 0xff;
	buf[2] = div_ref - 8;
	ret = regmap_bulk_write(dev->regmap, 0x01, buf, 3);
	if (ret)
		goto err;

	dev_dbg(&dev->client->dev,
			"frequency=%u offset=%d f_vco_khz=%u pll_n=%u div_ref=%u div_out=%u\n",
			dev->frequency_khz, dev->frequency_khz - c->frequency,
			f_vco_khz, pll_n, div_ref, div_out);

	ret = m88ts2022_cmd(dev, 0x10, 5, 0x15, 0x40, 0x00, NULL);
	if (ret)
		goto err;

	ret = regmap_read(dev->regmap, 0x14, &utmp);
	if (ret)
		goto err;

	utmp &= 0x7f;
	if (utmp < 64) {
		ret = regmap_update_bits(dev->regmap, 0x10, 0x80, 0x80);
		if (ret)
			goto err;

		ret = regmap_write(dev->regmap, 0x11, 0x6f);
		if (ret)
			goto err;

		ret = m88ts2022_cmd(dev, 0x10, 5, 0x15, 0x40, 0x00, NULL);
		if (ret)
			goto err;
	}

	ret = regmap_read(dev->regmap, 0x14, &utmp);
	if (ret)
		goto err;

	utmp &= 0x1f;
	if (utmp > 19) {
		ret = regmap_update_bits(dev->regmap, 0x10, 0x02, 0x00);
		if (ret)
			goto err;
	}

	ret = m88ts2022_cmd(dev, 0x08, 5, 0x3c, 0xff, 0x00, NULL);
	if (ret)
		goto err;

	ret = regmap_write(dev->regmap, 0x25, 0x00);
	if (ret)
		goto err;

	ret = regmap_write(dev->regmap, 0x27, 0x70);
	if (ret)
		goto err;

	ret = regmap_write(dev->regmap, 0x41, 0x09);
	if (ret)
		goto err;

	ret = regmap_write(dev->regmap, 0x08, 0x0b);
	if (ret)
		goto err;

	/* filters */
	gdiv28 = DIV_ROUND_CLOSEST(f_ref_khz * 1694U, 1000000U);

	ret = regmap_write(dev->regmap, 0x04, gdiv28);
	if (ret)
		goto err;

	ret = m88ts2022_cmd(dev, 0x04, 2, 0x26, 0xff, 0x00, &u8tmp);
	if (ret)
		goto err;

	cap_code = u8tmp & 0x3f;

	ret = regmap_write(dev->regmap, 0x41, 0x0d);
	if (ret)
		goto err;

	ret = m88ts2022_cmd(dev, 0x04, 2, 0x26, 0xff, 0x00, &u8tmp);
	if (ret)
		goto err;

	u8tmp &= 0x3f;
	cap_code = (cap_code + u8tmp) / 2;
	gdiv28 = gdiv28 * 207 / (cap_code * 2 + 151);
	div_max = gdiv28 * 135 / 100;
	div_min = gdiv28 * 78 / 100;
	div_max = clamp_val(div_max, 0U, 63U);

	f_3db_hz = mult_frac(c->symbol_rate, 135, 200);
	f_3db_hz +=  2000000U + (frequency_offset_khz * 1000U);
	f_3db_hz = clamp(f_3db_hz, 7000000U, 40000000U);

#define LPF_COEFF 3200U
	lpf_gm = DIV_ROUND_CLOSEST(f_3db_hz * gdiv28, LPF_COEFF * f_ref_khz);
	lpf_gm = clamp_val(lpf_gm, 1U, 23U);

	lpf_mxdiv = DIV_ROUND_CLOSEST(lpf_gm * LPF_COEFF * f_ref_khz, f_3db_hz);
	if (lpf_mxdiv < div_min)
		lpf_mxdiv = DIV_ROUND_CLOSEST(++lpf_gm * LPF_COEFF * f_ref_khz, f_3db_hz);
	lpf_mxdiv = clamp_val(lpf_mxdiv, 0U, div_max);

	ret = regmap_write(dev->regmap, 0x04, lpf_mxdiv);
	if (ret)
		goto err;

	ret = regmap_write(dev->regmap, 0x06, lpf_gm);
	if (ret)
		goto err;

	ret = m88ts2022_cmd(dev, 0x04, 2, 0x26, 0xff, 0x00, &u8tmp);
	if (ret)
		goto err;

	cap_code = u8tmp & 0x3f;

	ret = regmap_write(dev->regmap, 0x41, 0x09);
	if (ret)
		goto err;

	ret = m88ts2022_cmd(dev, 0x04, 2, 0x26, 0xff, 0x00, &u8tmp);
	if (ret)
		goto err;

	u8tmp &= 0x3f;
	cap_code = (cap_code + u8tmp) / 2;

	u8tmp = cap_code | 0x80;
	ret = regmap_write(dev->regmap, 0x25, u8tmp);
	if (ret)
		goto err;

	ret = regmap_write(dev->regmap, 0x27, 0x30);
	if (ret)
		goto err;

	ret = regmap_write(dev->regmap, 0x08, 0x09);
	if (ret)
		goto err;

	ret = m88ts2022_cmd(dev, 0x01, 20, 0x21, 0xff, 0x00, NULL);
	if (ret)
		goto err;
err:
	if (ret)
		dev_dbg(&dev->client->dev, "failed=%d\n", ret);

	return ret;
}

static int m88ts2022_init(struct dvb_frontend *fe)
{
	struct m88ts2022_dev *dev = fe->tuner_priv;
	int ret, i;
	u8 u8tmp;
	static const struct m88ts2022_reg_val reg_vals[] = {
		{0x7d, 0x9d},
		{0x7c, 0x9a},
		{0x7a, 0x76},
		{0x3b, 0x01},
		{0x63, 0x88},
		{0x61, 0x85},
		{0x22, 0x30},
		{0x30, 0x40},
		{0x20, 0x23},
		{0x24, 0x02},
		{0x12, 0xa0},
	};

	dev_dbg(&dev->client->dev, "\n");

	ret = regmap_write(dev->regmap, 0x00, 0x01);
	if (ret)
		goto err;

	ret = regmap_write(dev->regmap, 0x00, 0x03);
	if (ret)
		goto err;

	switch (dev->cfg.clock_out) {
	case M88TS2022_CLOCK_OUT_DISABLED:
		u8tmp = 0x60;
		break;
	case M88TS2022_CLOCK_OUT_ENABLED:
		u8tmp = 0x70;
		ret = regmap_write(dev->regmap, 0x05, dev->cfg.clock_out_div);
		if (ret)
			goto err;
		break;
	case M88TS2022_CLOCK_OUT_ENABLED_XTALOUT:
		u8tmp = 0x6c;
		break;
	default:
		goto err;
	}

	ret = regmap_write(dev->regmap, 0x42, u8tmp);
	if (ret)
		goto err;

	if (dev->cfg.loop_through)
		u8tmp = 0xec;
	else
		u8tmp = 0x6c;

	ret = regmap_write(dev->regmap, 0x62, u8tmp);
	if (ret)
		goto err;

	for (i = 0; i < ARRAY_SIZE(reg_vals); i++) {
		ret = regmap_write(dev->regmap, reg_vals[i].reg, reg_vals[i].val);
		if (ret)
			goto err;
	}
err:
	if (ret)
		dev_dbg(&dev->client->dev, "failed=%d\n", ret);
	return ret;
}

static int m88ts2022_sleep(struct dvb_frontend *fe)
{
	struct m88ts2022_dev *dev = fe->tuner_priv;
	int ret;

	dev_dbg(&dev->client->dev, "\n");

	ret = regmap_write(dev->regmap, 0x00, 0x00);
	if (ret)
		goto err;
err:
	if (ret)
		dev_dbg(&dev->client->dev, "failed=%d\n", ret);
	return ret;
}

static int m88ts2022_get_frequency(struct dvb_frontend *fe, u32 *frequency)
{
	struct m88ts2022_dev *dev = fe->tuner_priv;

	dev_dbg(&dev->client->dev, "\n");

	*frequency = dev->frequency_khz;
	return 0;
}

static int m88ts2022_get_if_frequency(struct dvb_frontend *fe, u32 *frequency)
{
	struct m88ts2022_dev *dev = fe->tuner_priv;

	dev_dbg(&dev->client->dev, "\n");

	*frequency = 0; /* Zero-IF */
	return 0;
}

static int m88ts2022_get_rf_strength(struct dvb_frontend *fe, u16 *strength)
{
	struct m88ts2022_dev *dev = fe->tuner_priv;
	int ret;
	u16 gain, u16tmp;
	unsigned int utmp, gain1, gain2, gain3;

	ret = regmap_read(dev->regmap, 0x3d, &utmp);
	if (ret)
		goto err;

	gain1 = (utmp >> 0) & 0x1f;
	gain1 = clamp(gain1, 0U, 15U);

	ret = regmap_read(dev->regmap, 0x21, &utmp);
	if (ret)
		goto err;

	gain2 = (utmp >> 0) & 0x1f;
	gain2 = clamp(gain2, 2U, 16U);

	ret = regmap_read(dev->regmap, 0x66, &utmp);
	if (ret)
		goto err;

	gain3 = (utmp >> 3) & 0x07;
	gain3 = clamp(gain3, 0U, 6U);

	gain = gain1 * 265 + gain2 * 338 + gain3 * 285;

	/* scale value to 0x0000-0xffff */
	u16tmp = (0xffff - gain);
	u16tmp = clamp_val(u16tmp, 59000U, 61500U);

	*strength = (u16tmp - 59000) * 0xffff / (61500 - 59000);
err:
	if (ret)
		dev_dbg(&dev->client->dev, "failed=%d\n", ret);
	return ret;
}

static const struct dvb_tuner_ops m88ts2022_tuner_ops = {
	.info = {
		.name          = "Montage M88TS2022",
		.frequency_min = 950000,
		.frequency_max = 2150000,
	},

	.init = m88ts2022_init,
	.sleep = m88ts2022_sleep,
	.set_params = m88ts2022_set_params,

	.get_frequency = m88ts2022_get_frequency,
	.get_if_frequency = m88ts2022_get_if_frequency,
	.get_rf_strength = m88ts2022_get_rf_strength,
};

static int m88ts2022_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	struct m88ts2022_config *cfg = client->dev.platform_data;
	struct dvb_frontend *fe = cfg->fe;
	struct m88ts2022_dev *dev;
	int ret;
	u8 u8tmp;
	unsigned int utmp;
	static const struct regmap_config regmap_config = {
		.reg_bits = 8,
		.val_bits = 8,
	};

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev) {
		ret = -ENOMEM;
		dev_err(&client->dev, "kzalloc() failed\n");
		goto err;
	}

	memcpy(&dev->cfg, cfg, sizeof(struct m88ts2022_config));
	dev->client = client;
	dev->regmap = devm_regmap_init_i2c(client, &regmap_config);
	if (IS_ERR(dev->regmap)) {
		ret = PTR_ERR(dev->regmap);
		goto err;
	}

	/* check if the tuner is there */
	ret = regmap_read(dev->regmap, 0x00, &utmp);
	if (ret)
		goto err;

	if ((utmp & 0x03) == 0x00) {
		ret = regmap_write(dev->regmap, 0x00, 0x01);
		if (ret)
			goto err;

		usleep_range(2000, 50000);
	}

	ret = regmap_write(dev->regmap, 0x00, 0x03);
	if (ret)
		goto err;

	usleep_range(2000, 50000);

	ret = regmap_read(dev->regmap, 0x00, &utmp);
	if (ret)
		goto err;

	dev_dbg(&dev->client->dev, "chip_id=%02x\n", utmp);

	switch (utmp) {
	case 0xc3:
	case 0x83:
		break;
	default:
		goto err;
	}

	switch (dev->cfg.clock_out) {
	case M88TS2022_CLOCK_OUT_DISABLED:
		u8tmp = 0x60;
		break;
	case M88TS2022_CLOCK_OUT_ENABLED:
		u8tmp = 0x70;
		ret = regmap_write(dev->regmap, 0x05, dev->cfg.clock_out_div);
		if (ret)
			goto err;
		break;
	case M88TS2022_CLOCK_OUT_ENABLED_XTALOUT:
		u8tmp = 0x6c;
		break;
	default:
		goto err;
	}

	ret = regmap_write(dev->regmap, 0x42, u8tmp);
	if (ret)
		goto err;

	if (dev->cfg.loop_through)
		u8tmp = 0xec;
	else
		u8tmp = 0x6c;

	ret = regmap_write(dev->regmap, 0x62, u8tmp);
	if (ret)
		goto err;

	/* sleep */
	ret = regmap_write(dev->regmap, 0x00, 0x00);
	if (ret)
		goto err;

	dev_info(&dev->client->dev, "Montage M88TS2022 successfully identified\n");

	fe->tuner_priv = dev;
	memcpy(&fe->ops.tuner_ops, &m88ts2022_tuner_ops,
			sizeof(struct dvb_tuner_ops));

	i2c_set_clientdata(client, dev);
	return 0;
err:
	dev_dbg(&client->dev, "failed=%d\n", ret);
	kfree(dev);
	return ret;
}

static int m88ts2022_remove(struct i2c_client *client)
{
	struct m88ts2022_dev *dev = i2c_get_clientdata(client);
	struct dvb_frontend *fe = dev->cfg.fe;

	dev_dbg(&client->dev, "\n");

	memset(&fe->ops.tuner_ops, 0, sizeof(struct dvb_tuner_ops));
	fe->tuner_priv = NULL;
	kfree(dev);

	return 0;
}

static const struct i2c_device_id m88ts2022_id[] = {
	{"m88ts2022", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, m88ts2022_id);

static struct i2c_driver m88ts2022_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= "m88ts2022",
	},
	.probe		= m88ts2022_probe,
	.remove		= m88ts2022_remove,
	.id_table	= m88ts2022_id,
};

module_i2c_driver(m88ts2022_driver);

MODULE_DESCRIPTION("Montage M88TS2022 silicon tuner driver");
MODULE_AUTHOR("Antti Palosaari <crope@iki.fi>");
MODULE_LICENSE("GPL");
