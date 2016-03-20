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
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	int ret, i;
	u8 buf[2];
	u32 if_freq;
	u16 if_ctl;
	u64 num;
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
		{ 0x1001f, priv->cfg.if_agc_polarity << 7, 0x80 },
		{ 0x10070, priv->cfg.ts_mode, 0xff },
		{ 0x10071, !priv->cfg.ts_clock_inv << 4, 0x10 },
	};

	dev_dbg(&priv->i2c->dev, "%s: frequency=%d symbol_rate=%d\n", __func__,
			c->frequency, c->symbol_rate);

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
		ret = fe->ops.tuner_ops.get_if_frequency(fe, &if_freq);
		if (ret)
			goto error;
	} else
		if_freq = 0;

	dev_dbg(&priv->i2c->dev, "%s: if_freq=%d\n", __func__, if_freq);

	num = if_freq / 1000; /* Hz => kHz */
	num *= 0x4000;
	if_ctl = 0x4000 - DIV_ROUND_CLOSEST_ULL(num, 41000);
	buf[0] = (if_ctl >> 8) & 0x3f;
	buf[1] = (if_ctl >> 0) & 0xff;

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
	dev_dbg(&priv->i2c->dev, "%s: failed=%d\n", __func__, ret);
	return ret;
}

int cxd2820r_get_frontend_c(struct dvb_frontend *fe,
			    struct dtv_frontend_properties *c)
{
	struct cxd2820r_priv *priv = fe->demodulator_priv;
	int ret;
	u8 buf[2];

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
	dev_dbg(&priv->i2c->dev, "%s: failed=%d\n", __func__, ret);
	return ret;
}

int cxd2820r_read_ber_c(struct dvb_frontend *fe, u32 *ber)
{
	struct cxd2820r_priv *priv = fe->demodulator_priv;
	int ret;
	u8 buf[3], start_ber = 0;
	*ber = 0;

	if (priv->ber_running) {
		ret = cxd2820r_rd_regs(priv, 0x10076, buf, sizeof(buf));
		if (ret)
			goto error;

		if ((buf[2] >> 7) & 0x01 || (buf[2] >> 4) & 0x01) {
			*ber = (buf[2] & 0x0f) << 16 | buf[1] << 8 | buf[0];
			start_ber = 1;
		}
	} else {
		priv->ber_running = true;
		start_ber = 1;
	}

	if (start_ber) {
		/* (re)start BER */
		ret = cxd2820r_wr_reg(priv, 0x10079, 0x01);
		if (ret)
			goto error;
	}

	return ret;
error:
	dev_dbg(&priv->i2c->dev, "%s: failed=%d\n", __func__, ret);
	return ret;
}

int cxd2820r_read_signal_strength_c(struct dvb_frontend *fe,
	u16 *strength)
{
	struct cxd2820r_priv *priv = fe->demodulator_priv;
	int ret;
	u8 buf[2];
	u16 tmp;

	ret = cxd2820r_rd_regs(priv, 0x10049, buf, sizeof(buf));
	if (ret)
		goto error;

	tmp = (buf[0] & 0x03) << 8 | buf[1];
	tmp = (~tmp & 0x03ff);

	if (tmp == 512)
		/* ~no signal */
		tmp = 0;
	else if (tmp > 350)
		tmp = 350;

	/* scale value to 0x0000-0xffff */
	*strength = tmp * 0xffff / (350-0);

	return ret;
error:
	dev_dbg(&priv->i2c->dev, "%s: failed=%d\n", __func__, ret);
	return ret;
}

int cxd2820r_read_snr_c(struct dvb_frontend *fe, u16 *snr)
{
	struct cxd2820r_priv *priv = fe->demodulator_priv;
	int ret;
	u8 tmp;
	unsigned int A, B;
	/* report SNR in dB * 10 */

	ret = cxd2820r_rd_reg(priv, 0x10019, &tmp);
	if (ret)
		goto error;

	if (((tmp >> 0) & 0x03) % 2) {
		A = 875;
		B = 650;
	} else {
		A = 950;
		B = 760;
	}

	ret = cxd2820r_rd_reg(priv, 0x1004d, &tmp);
	if (ret)
		goto error;

	#define CXD2820R_LOG2_E_24 24204406 /* log2(e) << 24 */
	if (tmp)
		*snr = A * (intlog2(B / tmp) >> 5) / (CXD2820R_LOG2_E_24 >> 5)
			/ 10;
	else
		*snr = 0;

	return ret;
error:
	dev_dbg(&priv->i2c->dev, "%s: failed=%d\n", __func__, ret);
	return ret;
}

int cxd2820r_read_ucblocks_c(struct dvb_frontend *fe, u32 *ucblocks)
{
	*ucblocks = 0;
	/* no way to read ? */
	return 0;
}

int cxd2820r_read_status_c(struct dvb_frontend *fe, enum fe_status *status)
{
	struct cxd2820r_priv *priv = fe->demodulator_priv;
	int ret;
	u8 buf[2];
	*status = 0;

	ret = cxd2820r_rd_regs(priv, 0x10088, buf, sizeof(buf));
	if (ret)
		goto error;

	if (((buf[0] >> 0) & 0x01) == 1) {
		*status |= FE_HAS_SIGNAL | FE_HAS_CARRIER |
			FE_HAS_VITERBI | FE_HAS_SYNC;

		if (((buf[1] >> 3) & 0x01) == 1) {
			*status |= FE_HAS_SIGNAL | FE_HAS_CARRIER |
				FE_HAS_VITERBI | FE_HAS_SYNC | FE_HAS_LOCK;
		}
	}

	dev_dbg(&priv->i2c->dev, "%s: lock=%02x %02x\n", __func__, buf[0],
			buf[1]);

	return ret;
error:
	dev_dbg(&priv->i2c->dev, "%s: failed=%d\n", __func__, ret);
	return ret;
}

int cxd2820r_init_c(struct dvb_frontend *fe)
{
	struct cxd2820r_priv *priv = fe->demodulator_priv;
	int ret;

	ret = cxd2820r_wr_reg(priv, 0x00085, 0x07);
	if (ret)
		goto error;

	return ret;
error:
	dev_dbg(&priv->i2c->dev, "%s: failed=%d\n", __func__, ret);
	return ret;
}

int cxd2820r_sleep_c(struct dvb_frontend *fe)
{
	struct cxd2820r_priv *priv = fe->demodulator_priv;
	int ret, i;
	struct reg_val_mask tab[] = {
		{ 0x000ff, 0x1f, 0xff },
		{ 0x00085, 0x00, 0xff },
		{ 0x00088, 0x01, 0xff },
		{ 0x00081, 0x00, 0xff },
		{ 0x00080, 0x00, 0xff },
	};

	dev_dbg(&priv->i2c->dev, "%s\n", __func__);

	priv->delivery_system = SYS_UNDEFINED;

	for (i = 0; i < ARRAY_SIZE(tab); i++) {
		ret = cxd2820r_wr_reg_mask(priv, tab[i].reg, tab[i].val,
			tab[i].mask);
		if (ret)
			goto error;
	}

	return ret;
error:
	dev_dbg(&priv->i2c->dev, "%s: failed=%d\n", __func__, ret);
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
