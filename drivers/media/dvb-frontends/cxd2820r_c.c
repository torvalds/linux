/*
 * Sony CXD2820R demodulator driver
 *
 * Copyright (C) 2010 Antti Palosaari <crope@iki.fi>
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


#include "cxd2820r_priv.h"

int cxd2820r_set_frontend_c(struct dvb_frontend *fe)
{
	struct cxd2820r_priv *priv = fe->demodulator_priv;
	struct i2c_client *client = priv->client[0];
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	int ret, i;
	unsigned int utmp;
	u8 buf[2];
	u32 if_frequency;
	struct reg_val_mask tab[] = {
		{ 0x00080, 0x01, 0xff },
		{ 0x00081, 0x05, 0xff },
		{ 0x00085, 0x07, 0xff },
		{ 0x00088, 0x01, 0xff },

		{ 0x00082, 0x20, 0x60 },
		{ 0x1016a, 0x48, 0xff },
		{ 0x100a5, 0x00, 0x01 },
		{ 0x10020, 0x06, 0x07 },
		{ 0x10059, 0x50, 0xff },
		{ 0x10087, 0x0c, 0x3c },
		{ 0x1008b, 0x07, 0xff },
		{ 0x1001f, priv->if_agc_polarity << 7, 0x80 },
		{ 0x10070, priv->ts_mode, 0xff },
		{ 0x10071, !priv->ts_clk_inv << 4, 0x10 },
	};

	dev_dbg(&client->dev,
		"delivery_system=%d modulation=%d frequency=%u symbol_rate=%u inversion=%d\n",
		c->delivery_system, c->modulation, c->frequency,
		c->symbol_rate, c->inversion);

	/* program tuner */
	if (fe->ops.tuner_ops.set_params)
		fe->ops.tuner_ops.set_params(fe);

	if (priv->delivery_system !=  SYS_DVBC_ANNEX_A) {
		for (i = 0; i < ARRAY_SIZE(tab); i++) {
			ret = cxd2820r_wr_reg_mask(priv, tab[i].reg,
				tab[i].val, tab[i].mask);
			if (ret)
				goto error;
		}
	}

	priv->delivery_system = SYS_DVBC_ANNEX_A;
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

	utmp = 0x4000 - DIV_ROUND_CLOSEST_ULL((u64)if_frequency * 0x4000, CXD2820R_CLK);
	buf[0] = (utmp >> 8) & 0xff;
	buf[1] = (utmp >> 0) & 0xff;
	ret = cxd2820r_wr_regs(priv, 0x10042, buf, 2);
	if (ret)
		goto error;

	ret = cxd2820r_wr_reg(priv, 0x000ff, 0x08);
	if (ret)
		goto error;

	ret = cxd2820r_wr_reg(priv, 0x000fe, 0x01);
	if (ret)
		goto error;

	return ret;
error:
	dev_dbg(&client->dev, "failed=%d\n", ret);
	return ret;
}

int cxd2820r_get_frontend_c(struct dvb_frontend *fe,
			    struct dtv_frontend_properties *c)
{
	struct cxd2820r_priv *priv = fe->demodulator_priv;
	struct i2c_client *client = priv->client[0];
	int ret;
	u8 buf[2];

	dev_dbg(&client->dev, "\n");

	ret = cxd2820r_rd_regs(priv, 0x1001a, buf, 2);
	if (ret)
		goto error;

	c->symbol_rate = 2500 * ((buf[0] & 0x0f) << 8 | buf[1]);

	ret = cxd2820r_rd_reg(priv, 0x10019, &buf[0]);
	if (ret)
		goto error;

	switch ((buf[0] >> 0) & 0x07) {
	case 0:
		c->modulation = QAM_16;
		break;
	case 1:
		c->modulation = QAM_32;
		break;
	case 2:
		c->modulation = QAM_64;
		break;
	case 3:
		c->modulation = QAM_128;
		break;
	case 4:
		c->modulation = QAM_256;
		break;
	}

	switch ((buf[0] >> 7) & 0x01) {
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

int cxd2820r_read_status_c(struct dvb_frontend *fe, enum fe_status *status)
{
	struct cxd2820r_priv *priv = fe->demodulator_priv;
	struct i2c_client *client = priv->client[0];
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	int ret;
	unsigned int utmp, utmp1, utmp2;
	u8 buf[3];

	/* Lock detection */
	ret = cxd2820r_rd_reg(priv, 0x10088, &buf[0]);
	if (ret)
		goto error;
	ret = cxd2820r_rd_reg(priv, 0x10073, &buf[1]);
	if (ret)
		goto error;

	utmp1 = (buf[0] >> 0) & 0x01;
	utmp2 = (buf[1] >> 3) & 0x01;

	if (utmp1 == 1 && utmp2 == 1) {
		*status = FE_HAS_SIGNAL | FE_HAS_CARRIER |
			  FE_HAS_VITERBI | FE_HAS_SYNC | FE_HAS_LOCK;
	} else if (utmp1 == 1 || utmp2 == 1) {
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

		ret = cxd2820r_rd_regs(priv, 0x10049, buf, 2);
		if (ret)
			goto error;

		utmp = buf[0] << 8 | buf[1] << 0;
		utmp = 511 - sign_extend32(utmp, 9);
		/* Scale value to 0x0000-0xffff */
		strength = utmp << 6 | utmp >> 4;

		c->strength.len = 1;
		c->strength.stat[0].scale = FE_SCALE_RELATIVE;
		c->strength.stat[0].uvalue = strength;
	} else {
		c->strength.len = 1;
		c->strength.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	}

	/* CNR */
	if (*status & FE_HAS_VITERBI) {
		unsigned int cnr, const_a, const_b;

		ret = cxd2820r_rd_reg(priv, 0x10019, &buf[0]);
		if (ret)
			goto error;

		if (((buf[0] >> 0) & 0x03) % 2) {
			const_a = 8750;
			const_b = 650;
		} else {
			const_a = 9500;
			const_b = 760;
		}

		ret = cxd2820r_rd_reg(priv, 0x1004d, &buf[0]);
		if (ret)
			goto error;

		utmp = buf[0] << 0;
		#define CXD2820R_LOG2_E_24 24204406 /* log2(e) << 24 */
		if (utmp)
			cnr = div_u64((u64)(intlog2(const_b) - intlog2(utmp))
				      * const_a, CXD2820R_LOG2_E_24);
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
			ret = cxd2820r_rd_regs(priv, 0x10076, buf, 3);
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
			ret = cxd2820r_wr_reg(priv, 0x10079, 0x01);
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

int cxd2820r_init_c(struct dvb_frontend *fe)
{
	struct cxd2820r_priv *priv = fe->demodulator_priv;
	struct i2c_client *client = priv->client[0];
	int ret;

	dev_dbg(&client->dev, "\n");

	ret = cxd2820r_wr_reg(priv, 0x00085, 0x07);
	if (ret)
		goto error;

	return ret;
error:
	dev_dbg(&client->dev, "failed=%d\n", ret);
	return ret;
}

int cxd2820r_sleep_c(struct dvb_frontend *fe)
{
	struct cxd2820r_priv *priv = fe->demodulator_priv;
	struct i2c_client *client = priv->client[0];
	int ret, i;
	struct reg_val_mask tab[] = {
		{ 0x000ff, 0x1f, 0xff },
		{ 0x00085, 0x00, 0xff },
		{ 0x00088, 0x01, 0xff },
		{ 0x00081, 0x00, 0xff },
		{ 0x00080, 0x00, 0xff },
	};

	dev_dbg(&client->dev, "\n");

	priv->delivery_system = SYS_UNDEFINED;

	for (i = 0; i < ARRAY_SIZE(tab); i++) {
		ret = cxd2820r_wr_reg_mask(priv, tab[i].reg, tab[i].val,
			tab[i].mask);
		if (ret)
			goto error;
	}

	return ret;
error:
	dev_dbg(&client->dev, "failed=%d\n", ret);
	return ret;
}

int cxd2820r_get_tune_settings_c(struct dvb_frontend *fe,
	struct dvb_frontend_tune_settings *s)
{
	s->min_delay_ms = 500;
	s->step_size = 0; /* no zigzag */
	s->max_drift = 0;

	return 0;
}
