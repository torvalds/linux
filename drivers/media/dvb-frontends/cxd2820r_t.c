// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Sony CXD2820R demodulator driver
 *
 * Copyright (C) 2010 Antti Palosaari <crope@iki.fi>
 */


#include "cxd2820r_priv.h"

int cxd2820r_set_frontend_t(struct dvb_frontend *fe)
{
	struct cxd2820r_priv *priv = fe->demodulator_priv;
	struct i2c_client *client = priv->client[0];
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	int ret, bw_i;
	unsigned int utmp;
	u32 if_frequency;
	u8 buf[3], bw_param;
	u8 bw_params1[][5] = {
		{ 0x17, 0xea, 0xaa, 0xaa, 0xaa }, /* 6 MHz */
		{ 0x14, 0x80, 0x00, 0x00, 0x00 }, /* 7 MHz */
		{ 0x11, 0xf0, 0x00, 0x00, 0x00 }, /* 8 MHz */
	};
	u8 bw_params2[][2] = {
		{ 0x1f, 0xdc }, /* 6 MHz */
		{ 0x12, 0xf8 }, /* 7 MHz */
		{ 0x01, 0xe0 }, /* 8 MHz */
	};
	struct reg_val_mask tab[] = {
		{ 0x00080, 0x00, 0xff },
		{ 0x00081, 0x03, 0xff },
		{ 0x00085, 0x07, 0xff },
		{ 0x00088, 0x01, 0xff },

		{ 0x00070, priv->ts_mode, 0xff },
		{ 0x00071, !priv->ts_clk_inv << 4, 0x10 },
		{ 0x000cb, priv->if_agc_polarity << 6, 0x40 },
		{ 0x000a5, 0x00, 0x01 },
		{ 0x00082, 0x20, 0x60 },
		{ 0x000c2, 0xc3, 0xff },
		{ 0x0016a, 0x50, 0xff },
		{ 0x00427, 0x41, 0xff },
	};

	dev_dbg(&client->dev,
		"delivery_system=%d modulation=%d frequency=%u bandwidth_hz=%u inversion=%d\n",
		c->delivery_system, c->modulation, c->frequency,
		c->bandwidth_hz, c->inversion);

	switch (c->bandwidth_hz) {
	case 6000000:
		bw_i = 0;
		bw_param = 2;
		break;
	case 7000000:
		bw_i = 1;
		bw_param = 1;
		break;
	case 8000000:
		bw_i = 2;
		bw_param = 0;
		break;
	default:
		return -EINVAL;
	}

	/* program tuner */
	if (fe->ops.tuner_ops.set_params)
		fe->ops.tuner_ops.set_params(fe);

	if (priv->delivery_system != SYS_DVBT) {
		ret = cxd2820r_wr_reg_val_mask_tab(priv, tab, ARRAY_SIZE(tab));
		if (ret)
			goto error;
	}

	priv->delivery_system = SYS_DVBT;
	priv->ber_running = false; /* tune stops BER counter */

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
	ret = regmap_bulk_write(priv->regmap[0], 0x00b6, buf, 3);
	if (ret)
		goto error;

	ret = regmap_bulk_write(priv->regmap[0], 0x009f, bw_params1[bw_i], 5);
	if (ret)
		goto error;

	ret = regmap_update_bits(priv->regmap[0], 0x00d7, 0xc0, bw_param << 6);
	if (ret)
		goto error;

	ret = regmap_bulk_write(priv->regmap[0], 0x00d9, bw_params2[bw_i], 2);
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

int cxd2820r_get_frontend_t(struct dvb_frontend *fe,
			    struct dtv_frontend_properties *c)
{
	struct cxd2820r_priv *priv = fe->demodulator_priv;
	struct i2c_client *client = priv->client[0];
	int ret;
	unsigned int utmp;
	u8 buf[2];

	dev_dbg(&client->dev, "\n");

	ret = regmap_bulk_read(priv->regmap[0], 0x002f, buf, sizeof(buf));
	if (ret)
		goto error;

	switch ((buf[0] >> 6) & 0x03) {
	case 0:
		c->modulation = QPSK;
		break;
	case 1:
		c->modulation = QAM_16;
		break;
	case 2:
		c->modulation = QAM_64;
		break;
	}

	switch ((buf[1] >> 1) & 0x03) {
	case 0:
		c->transmission_mode = TRANSMISSION_MODE_2K;
		break;
	case 1:
		c->transmission_mode = TRANSMISSION_MODE_8K;
		break;
	}

	switch ((buf[1] >> 3) & 0x03) {
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
	}

	switch ((buf[0] >> 3) & 0x07) {
	case 0:
		c->hierarchy = HIERARCHY_NONE;
		break;
	case 1:
		c->hierarchy = HIERARCHY_1;
		break;
	case 2:
		c->hierarchy = HIERARCHY_2;
		break;
	case 3:
		c->hierarchy = HIERARCHY_4;
		break;
	}

	switch ((buf[0] >> 0) & 0x07) {
	case 0:
		c->code_rate_HP = FEC_1_2;
		break;
	case 1:
		c->code_rate_HP = FEC_2_3;
		break;
	case 2:
		c->code_rate_HP = FEC_3_4;
		break;
	case 3:
		c->code_rate_HP = FEC_5_6;
		break;
	case 4:
		c->code_rate_HP = FEC_7_8;
		break;
	}

	switch ((buf[1] >> 5) & 0x07) {
	case 0:
		c->code_rate_LP = FEC_1_2;
		break;
	case 1:
		c->code_rate_LP = FEC_2_3;
		break;
	case 2:
		c->code_rate_LP = FEC_3_4;
		break;
	case 3:
		c->code_rate_LP = FEC_5_6;
		break;
	case 4:
		c->code_rate_LP = FEC_7_8;
		break;
	}

	ret = regmap_read(priv->regmap[0], 0x07c6, &utmp);
	if (ret)
		goto error;

	switch ((utmp >> 0) & 0x01) {
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

int cxd2820r_read_status_t(struct dvb_frontend *fe, enum fe_status *status)
{
	struct cxd2820r_priv *priv = fe->demodulator_priv;
	struct i2c_client *client = priv->client[0];
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	int ret;
	unsigned int utmp, utmp1, utmp2;
	u8 buf[3];

	/* Lock detection */
	ret = regmap_bulk_read(priv->regmap[0], 0x0010, &buf[0], 1);
	if (ret)
		goto error;
	ret = regmap_bulk_read(priv->regmap[0], 0x0073, &buf[1], 1);
	if (ret)
		goto error;

	utmp1 = (buf[0] >> 0) & 0x07;
	utmp2 = (buf[1] >> 3) & 0x01;

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
		*status, 2, buf, utmp1, utmp2);

	/* Signal strength */
	if (*status & FE_HAS_SIGNAL) {
		unsigned int strength;

		ret = regmap_bulk_read(priv->regmap[0], 0x0026, buf, 2);
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

		ret = regmap_bulk_read(priv->regmap[0], 0x002c, buf, 2);
		if (ret)
			goto error;

		utmp = buf[0] << 8 | buf[1] << 0;
		if (utmp)
			cnr = div_u64((u64)(intlog10(utmp)
				      - intlog10(32000 - utmp) + 55532585)
				      * 10000, (1 << 24));
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
		bool start_ber;

		if (priv->ber_running) {
			ret = regmap_bulk_read(priv->regmap[0], 0x0076, buf, 3);
			if (ret)
				goto error;

			if ((buf[2] >> 7) & 0x01) {
				post_bit_error = buf[2] << 16 | buf[1] << 8 |
						 buf[0] << 0;
				post_bit_error &= 0x0fffff;
				start_ber = true;
			} else {
				post_bit_error = 0;
				start_ber = false;
			}
		} else {
			post_bit_error = 0;
			start_ber = true;
		}

		if (start_ber) {
			ret = regmap_write(priv->regmap[0], 0x0079, 0x01);
			if (ret)
				goto error;
			priv->ber_running = true;
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

int cxd2820r_init_t(struct dvb_frontend *fe)
{
	struct cxd2820r_priv *priv = fe->demodulator_priv;
	struct i2c_client *client = priv->client[0];
	int ret;

	dev_dbg(&client->dev, "\n");

	ret = regmap_write(priv->regmap[0], 0x0085, 0x07);
	if (ret)
		goto error;

	return ret;
error:
	dev_dbg(&client->dev, "failed=%d\n", ret);
	return ret;
}

int cxd2820r_sleep_t(struct dvb_frontend *fe)
{
	struct cxd2820r_priv *priv = fe->demodulator_priv;
	struct i2c_client *client = priv->client[0];
	int ret;
	static struct reg_val_mask tab[] = {
		{ 0x000ff, 0x1f, 0xff },
		{ 0x00085, 0x00, 0xff },
		{ 0x00088, 0x01, 0xff },
		{ 0x00081, 0x00, 0xff },
		{ 0x00080, 0x00, 0xff },
	};

	dev_dbg(&client->dev, "\n");

	priv->delivery_system = SYS_UNDEFINED;

	ret = cxd2820r_wr_reg_val_mask_tab(priv, tab, ARRAY_SIZE(tab));
	if (ret)
		goto error;

	return ret;
error:
	dev_dbg(&client->dev, "failed=%d\n", ret);
	return ret;
}

int cxd2820r_get_tune_settings_t(struct dvb_frontend *fe,
	struct dvb_frontend_tune_settings *s)
{
	s->min_delay_ms = 500;
	s->step_size = fe->ops.info.frequency_stepsize_hz * 2;
	s->max_drift = (fe->ops.info.frequency_stepsize_hz * 2) + 1;

	return 0;
}
