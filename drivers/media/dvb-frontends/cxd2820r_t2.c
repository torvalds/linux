// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Sony CXD2820R demodulator driver
 *
 * Copyright (C) 2010 Antti Palosaari <crope@iki.fi>
 */


#include "cxd2820r_priv.h"

int cxd2820r_set_frontend_t2(struct dvb_frontend *fe)
{
	struct cxd2820r_priv *priv = fe->demodulator_priv;
	struct i2c_client *client = priv->client[0];
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	int ret, bw_i;
	unsigned int utmp;
	u32 if_frequency;
	u8 buf[3], bw_param;
	u8 bw_params1[][5] = {
		{ 0x1c, 0xb3, 0x33, 0x33, 0x33 }, /* 5 MHz */
		{ 0x17, 0xea, 0xaa, 0xaa, 0xaa }, /* 6 MHz */
		{ 0x14, 0x80, 0x00, 0x00, 0x00 }, /* 7 MHz */
		{ 0x11, 0xf0, 0x00, 0x00, 0x00 }, /* 8 MHz */
	};
	struct reg_val_mask tab[] = {
		{ 0x00080, 0x02, 0xff },
		{ 0x00081, 0x20, 0xff },
		{ 0x00085, 0x07, 0xff },
		{ 0x00088, 0x01, 0xff },
		{ 0x02069, 0x01, 0xff },

		{ 0x0207f, 0x2a, 0xff },
		{ 0x02082, 0x0a, 0xff },
		{ 0x02083, 0x0a, 0xff },
		{ 0x020cb, priv->if_agc_polarity << 6, 0x40 },
		{ 0x02070, priv->ts_mode, 0xff },
		{ 0x02071, !priv->ts_clk_inv << 6, 0x40 },
		{ 0x020b5, priv->spec_inv << 4, 0x10 },
		{ 0x02567, 0x07, 0x0f },
		{ 0x02569, 0x03, 0x03 },
		{ 0x02595, 0x1a, 0xff },
		{ 0x02596, 0x50, 0xff },
		{ 0x02a8c, 0x00, 0xff },
		{ 0x02a8d, 0x34, 0xff },
		{ 0x02a45, 0x06, 0x07 },
		{ 0x03f10, 0x0d, 0xff },
		{ 0x03f11, 0x02, 0xff },
		{ 0x03f12, 0x01, 0xff },
		{ 0x03f23, 0x2c, 0xff },
		{ 0x03f51, 0x13, 0xff },
		{ 0x03f52, 0x01, 0xff },
		{ 0x03f53, 0x00, 0xff },
		{ 0x027e6, 0x14, 0xff },
		{ 0x02786, 0x02, 0x07 },
		{ 0x02787, 0x40, 0xe0 },
		{ 0x027ef, 0x10, 0x18 },
	};

	dev_dbg(&client->dev,
		"delivery_system=%d modulation=%d frequency=%u bandwidth_hz=%u inversion=%d stream_id=%u\n",
		c->delivery_system, c->modulation, c->frequency,
		c->bandwidth_hz, c->inversion, c->stream_id);

	switch (c->bandwidth_hz) {
	case 5000000:
		bw_i = 0;
		bw_param = 3;
		break;
	case 6000000:
		bw_i = 1;
		bw_param = 2;
		break;
	case 7000000:
		bw_i = 2;
		bw_param = 1;
		break;
	case 8000000:
		bw_i = 3;
		bw_param = 0;
		break;
	default:
		return -EINVAL;
	}

	/* program tuner */
	if (fe->ops.tuner_ops.set_params)
		fe->ops.tuner_ops.set_params(fe);

	if (priv->delivery_system != SYS_DVBT2) {
		ret = cxd2820r_wr_reg_val_mask_tab(priv, tab, ARRAY_SIZE(tab));
		if (ret)
			goto error;
	}

	priv->delivery_system = SYS_DVBT2;

	/* program IF frequency */
	if (fe->ops.tuner_ops.get_if_frequency) {
		ret = fe->ops.tuner_ops.get_if_frequency(fe, &if_frequency);
		if (ret)
			goto error;
		dev_dbg(&client->dev, "if_frequency=%u\n", if_frequency);
	} else {
		ret = -EINVAL;
		goto error;
	}

	utmp = DIV_ROUND_CLOSEST_ULL((u64)if_frequency * 0x1000000, CXD2820R_CLK);
	buf[0] = (utmp >> 16) & 0xff;
	buf[1] = (utmp >>  8) & 0xff;
	buf[2] = (utmp >>  0) & 0xff;
	ret = regmap_bulk_write(priv->regmap[0], 0x20b6, buf, 3);
	if (ret)
		goto error;

	/* PLP filtering */
	if (c->stream_id > 255) {
		dev_dbg(&client->dev, "disable PLP filtering\n");
		ret = regmap_write(priv->regmap[0], 0x23ad, 0x00);
		if (ret)
			goto error;
	} else {
		dev_dbg(&client->dev, "enable PLP filtering\n");
		ret = regmap_write(priv->regmap[0], 0x23af, c->stream_id & 0xff);
		if (ret)
			goto error;
		ret = regmap_write(priv->regmap[0], 0x23ad, 0x01);
		if (ret)
			goto error;
	}

	ret = regmap_bulk_write(priv->regmap[0], 0x209f, bw_params1[bw_i], 5);
	if (ret)
		goto error;

	ret = regmap_update_bits(priv->regmap[0], 0x20d7, 0xc0, bw_param << 6);
	if (ret)
		goto error;

	ret = regmap_write(priv->regmap[0], 0x00ff, 0x08);
	if (ret)
		goto error;

	ret = regmap_write(priv->regmap[0], 0x00fe, 0x01);
	if (ret)
		goto error;

	return ret;
error:
	dev_dbg(&client->dev, "failed=%d\n", ret);
	return ret;

}

int cxd2820r_get_frontend_t2(struct dvb_frontend *fe,
			     struct dtv_frontend_properties *c)
{
	struct cxd2820r_priv *priv = fe->demodulator_priv;
	struct i2c_client *client = priv->client[0];
	int ret;
	unsigned int utmp;
	u8 buf[2];

	dev_dbg(&client->dev, "\n");

	ret = regmap_bulk_read(priv->regmap[0], 0x205c, buf, 2);
	if (ret)
		goto error;

	switch ((buf[0] >> 0) & 0x07) {
	case 0:
		c->transmission_mode = TRANSMISSION_MODE_2K;
		break;
	case 1:
		c->transmission_mode = TRANSMISSION_MODE_8K;
		break;
	case 2:
		c->transmission_mode = TRANSMISSION_MODE_4K;
		break;
	case 3:
		c->transmission_mode = TRANSMISSION_MODE_1K;
		break;
	case 4:
		c->transmission_mode = TRANSMISSION_MODE_16K;
		break;
	case 5:
		c->transmission_mode = TRANSMISSION_MODE_32K;
		break;
	}

	switch ((buf[1] >> 4) & 0x07) {
	case 0:
		c->guard_interval = GUARD_INTERVAL_1_32;
		break;
	case 1:
		c->guard_interval = GUARD_INTERVAL_1_16;
		break;
	case 2:
		c->guard_interval = GUARD_INTERVAL_1_8;
		break;
	case 3:
		c->guard_interval = GUARD_INTERVAL_1_4;
		break;
	case 4:
		c->guard_interval = GUARD_INTERVAL_1_128;
		break;
	case 5:
		c->guard_interval = GUARD_INTERVAL_19_128;
		break;
	case 6:
		c->guard_interval = GUARD_INTERVAL_19_256;
		break;
	}

	ret = regmap_bulk_read(priv->regmap[0], 0x225b, buf, 2);
	if (ret)
		goto error;

	switch ((buf[0] >> 0) & 0x07) {
	case 0:
		c->fec_inner = FEC_1_2;
		break;
	case 1:
		c->fec_inner = FEC_3_5;
		break;
	case 2:
		c->fec_inner = FEC_2_3;
		break;
	case 3:
		c->fec_inner = FEC_3_4;
		break;
	case 4:
		c->fec_inner = FEC_4_5;
		break;
	case 5:
		c->fec_inner = FEC_5_6;
		break;
	}

	switch ((buf[1] >> 0) & 0x07) {
	case 0:
		c->modulation = QPSK;
		break;
	case 1:
		c->modulation = QAM_16;
		break;
	case 2:
		c->modulation = QAM_64;
		break;
	case 3:
		c->modulation = QAM_256;
		break;
	}

	ret = regmap_read(priv->regmap[0], 0x20b5, &utmp);
	if (ret)
		goto error;

	switch ((utmp >> 4) & 0x01) {
	case 0:
		c->inversion = INVERSION_OFF;
		break;
	case 1:
		c->inversion = INVERSION_ON;
		break;
	}

	return ret;
error:
	dev_dbg(&client->dev, "failed=%d\n", ret);
	return ret;
}

int cxd2820r_read_status_t2(struct dvb_frontend *fe, enum fe_status *status)
{
	struct cxd2820r_priv *priv = fe->demodulator_priv;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	struct i2c_client *client = priv->client[0];
	int ret;
	unsigned int utmp, utmp1, utmp2;
	u8 buf[4];

	/* Lock detection */
	ret = regmap_bulk_read(priv->regmap[0], 0x2010, &buf[0], 1);
	if (ret)
		goto error;

	utmp1 = (buf[0] >> 0) & 0x07;
	utmp2 = (buf[0] >> 5) & 0x01;

	if (utmp1 == 6 && utmp2 == 1) {
		*status = FE_HAS_SIGNAL | FE_HAS_CARRIER |
			  FE_HAS_VITERBI | FE_HAS_SYNC | FE_HAS_LOCK;
	} else if (utmp1 == 6 || utmp2 == 1) {
		*status = FE_HAS_SIGNAL | FE_HAS_CARRIER |
			  FE_HAS_VITERBI | FE_HAS_SYNC;
	} else {
		*status = 0;
	}

	dev_dbg(&client->dev, "status=%02x raw=%*ph sync=%u ts=%u\n",
		*status, 1, buf, utmp1, utmp2);

	/* Signal strength */
	if (*status & FE_HAS_SIGNAL) {
		unsigned int strength;

		ret = regmap_bulk_read(priv->regmap[0], 0x2026, buf, 2);
		if (ret)
			goto error;

		utmp = buf[0] << 8 | buf[1] << 0;
		utmp = ~utmp & 0x0fff;
		/* Scale value to 0x0000-0xffff */
		strength = utmp << 4 | utmp >> 8;

		c->strength.len = 1;
		c->strength.stat[0].scale = FE_SCALE_RELATIVE;
		c->strength.stat[0].uvalue = strength;
	} else {
		c->strength.len = 1;
		c->strength.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	}

	/* CNR */
	if (*status & FE_HAS_VITERBI) {
		unsigned int cnr;

		ret = regmap_bulk_read(priv->regmap[0], 0x2028, buf, 2);
		if (ret)
			goto error;

		utmp = buf[0] << 8 | buf[1] << 0;
		utmp = utmp & 0x0fff;
		#define CXD2820R_LOG10_8_24 15151336 /* log10(8) << 24 */
		if (utmp)
			cnr = div_u64((u64)(intlog10(utmp)
				      - CXD2820R_LOG10_8_24) * 10000,
				      (1 << 24));
		else
			cnr = 0;

		c->cnr.len = 1;
		c->cnr.stat[0].scale = FE_SCALE_DECIBEL;
		c->cnr.stat[0].svalue = cnr;
	} else {
		c->cnr.len = 1;
		c->cnr.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	}

	/* BER */
	if (*status & FE_HAS_SYNC) {
		unsigned int post_bit_error;

		ret = regmap_bulk_read(priv->regmap[0], 0x2039, buf, 4);
		if (ret)
			goto error;

		if ((buf[0] >> 4) & 0x01) {
			post_bit_error = buf[0] << 24 | buf[1] << 16 |
					 buf[2] << 8 | buf[3] << 0;
			post_bit_error &= 0x0fffffff;
		} else {
			post_bit_error = 0;
		}

		priv->post_bit_error += post_bit_error;

		c->post_bit_error.len = 1;
		c->post_bit_error.stat[0].scale = FE_SCALE_COUNTER;
		c->post_bit_error.stat[0].uvalue = priv->post_bit_error;
	} else {
		c->post_bit_error.len = 1;
		c->post_bit_error.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	}

	return ret;
error:
	dev_dbg(&client->dev, "failed=%d\n", ret);
	return ret;
}

int cxd2820r_sleep_t2(struct dvb_frontend *fe)
{
	struct cxd2820r_priv *priv = fe->demodulator_priv;
	struct i2c_client *client = priv->client[0];
	int ret;
	static const struct reg_val_mask tab[] = {
		{ 0x000ff, 0x1f, 0xff },
		{ 0x00085, 0x00, 0xff },
		{ 0x00088, 0x01, 0xff },
		{ 0x02069, 0x00, 0xff },
		{ 0x00081, 0x00, 0xff },
		{ 0x00080, 0x00, 0xff },
	};

	dev_dbg(&client->dev, "\n");

	ret = cxd2820r_wr_reg_val_mask_tab(priv, tab, ARRAY_SIZE(tab));
	if (ret)
		goto error;

	priv->delivery_system = SYS_UNDEFINED;

	return ret;
error:
	dev_dbg(&client->dev, "failed=%d\n", ret);
	return ret;
}

int cxd2820r_get_tune_settings_t2(struct dvb_frontend *fe,
	struct dvb_frontend_tune_settings *s)
{
	s->min_delay_ms = 1500;
	s->step_size = fe->ops.info.frequency_stepsize_hz * 2;
	s->max_drift = (fe->ops.info.frequency_stepsize_hz * 2) + 1;

	return 0;
}
