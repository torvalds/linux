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

int cxd2820r_set_frontend_t(struct dvb_frontend *fe)
{
	struct cxd2820r_priv *priv = fe->demodulator_priv;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	int ret, i, bw_i;
	u32 if_freq, if_ctl;
	u64 num;
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

		{ 0x00070, priv->cfg.ts_mode, 0xff },
		{ 0x00071, !priv->cfg.ts_clock_inv << 4, 0x10 },
		{ 0x000cb, priv->cfg.if_agc_polarity << 6, 0x40 },
		{ 0x000a5, 0x00, 0x01 },
		{ 0x00082, 0x20, 0x60 },
		{ 0x000c2, 0xc3, 0xff },
		{ 0x0016a, 0x50, 0xff },
		{ 0x00427, 0x41, 0xff },
	};

	dev_dbg(&priv->i2c->dev, "%s: frequency=%d bandwidth_hz=%d\n", __func__,
			c->frequency, c->bandwidth_hz);

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
		for (i = 0; i < ARRAY_SIZE(tab); i++) {
			ret = cxd2820r_wr_reg_mask(priv, tab[i].reg,
				tab[i].val, tab[i].mask);
			if (ret)
				goto error;
		}
	}

	priv->delivery_system = SYS_DVBT;
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
	num *= 0x1000000;
	if_ctl = DIV_ROUND_CLOSEST_ULL(num, 41000);
	buf[0] = ((if_ctl >> 16) & 0xff);
	buf[1] = ((if_ctl >>  8) & 0xff);
	buf[2] = ((if_ctl >>  0) & 0xff);

	ret = cxd2820r_wr_regs(priv, 0x000b6, buf, 3);
	if (ret)
		goto error;

	ret = cxd2820r_wr_regs(priv, 0x0009f, bw_params1[bw_i], 5);
	if (ret)
		goto error;

	ret = cxd2820r_wr_reg_mask(priv, 0x000d7, bw_param << 6, 0xc0);
	if (ret)
		goto error;

	ret = cxd2820r_wr_regs(priv, 0x000d9, bw_params2[bw_i], 2);
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

int cxd2820r_get_frontend_t(struct dvb_frontend *fe)
{
	struct cxd2820r_priv *priv = fe->demodulator_priv;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	int ret;
	u8 buf[2];

	ret = cxd2820r_rd_regs(priv, 0x0002f, buf, sizeof(buf));
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

	ret = cxd2820r_rd_reg(priv, 0x007c6, &buf[0]);
	if (ret)
		goto error;

	switch ((buf[0] >> 0) & 0x01) {
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

int cxd2820r_read_ber_t(struct dvb_frontend *fe, u32 *ber)
{
	struct cxd2820r_priv *priv = fe->demodulator_priv;
	int ret;
	u8 buf[3], start_ber = 0;
	*ber = 0;

	if (priv->ber_running) {
		ret = cxd2820r_rd_regs(priv, 0x00076, buf, sizeof(buf));
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
		ret = cxd2820r_wr_reg(priv, 0x00079, 0x01);
		if (ret)
			goto error;
	}

	return ret;
error:
	dev_dbg(&priv->i2c->dev, "%s: failed=%d\n", __func__, ret);
	return ret;
}

int cxd2820r_read_signal_strength_t(struct dvb_frontend *fe,
	u16 *strength)
{
	struct cxd2820r_priv *priv = fe->demodulator_priv;
	int ret;
	u8 buf[2];
	u16 tmp;

	ret = cxd2820r_rd_regs(priv, 0x00026, buf, sizeof(buf));
	if (ret)
		goto error;

	tmp = (buf[0] & 0x0f) << 8 | buf[1];
	tmp = ~tmp & 0x0fff;

	/* scale value to 0x0000-0xffff from 0x0000-0x0fff */
	*strength = tmp * 0xffff / 0x0fff;

	return ret;
error:
	dev_dbg(&priv->i2c->dev, "%s: failed=%d\n", __func__, ret);
	return ret;
}

int cxd2820r_read_snr_t(struct dvb_frontend *fe, u16 *snr)
{
	struct cxd2820r_priv *priv = fe->demodulator_priv;
	int ret;
	u8 buf[2];
	u16 tmp;
	/* report SNR in dB * 10 */

	ret = cxd2820r_rd_regs(priv, 0x00028, buf, sizeof(buf));
	if (ret)
		goto error;

	tmp = (buf[0] & 0x1f) << 8 | buf[1];
	#define CXD2820R_LOG10_8_24 15151336 /* log10(8) << 24 */
	if (tmp)
		*snr = (intlog10(tmp) - CXD2820R_LOG10_8_24) / ((1 << 24)
			/ 100);
	else
		*snr = 0;

	dev_dbg(&priv->i2c->dev, "%s: dBx10=%d val=%04x\n", __func__, *snr,
			tmp);

	return ret;
error:
	dev_dbg(&priv->i2c->dev, "%s: failed=%d\n", __func__, ret);
	return ret;
}

int cxd2820r_read_ucblocks_t(struct dvb_frontend *fe, u32 *ucblocks)
{
	*ucblocks = 0;
	/* no way to read ? */
	return 0;
}

int cxd2820r_read_status_t(struct dvb_frontend *fe, fe_status_t *status)
{
	struct cxd2820r_priv *priv = fe->demodulator_priv;
	int ret;
	u8 buf[4];
	*status = 0;

	ret = cxd2820r_rd_reg(priv, 0x00010, &buf[0]);
	if (ret)
		goto error;

	if ((buf[0] & 0x07) == 6) {
		ret = cxd2820r_rd_reg(priv, 0x00073, &buf[1]);
		if (ret)
			goto error;

		if (((buf[1] >> 3) & 0x01) == 1) {
			*status |= FE_HAS_SIGNAL | FE_HAS_CARRIER |
				FE_HAS_VITERBI | FE_HAS_SYNC | FE_HAS_LOCK;
		} else {
			*status |= FE_HAS_SIGNAL | FE_HAS_CARRIER |
				FE_HAS_VITERBI | FE_HAS_SYNC;
		}
	} else {
		ret = cxd2820r_rd_reg(priv, 0x00014, &buf[2]);
		if (ret)
			goto error;

		if ((buf[2] & 0x0f) >= 4) {
			ret = cxd2820r_rd_reg(priv, 0x00a14, &buf[3]);
			if (ret)
				goto error;

			if (((buf[3] >> 4) & 0x01) == 1)
				*status |= FE_HAS_SIGNAL;
		}
	}

	dev_dbg(&priv->i2c->dev, "%s: lock=%*ph\n", __func__, 4, buf);

	return ret;
error:
	dev_dbg(&priv->i2c->dev, "%s: failed=%d\n", __func__, ret);
	return ret;
}

int cxd2820r_init_t(struct dvb_frontend *fe)
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

int cxd2820r_sleep_t(struct dvb_frontend *fe)
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

int cxd2820r_get_tune_settings_t(struct dvb_frontend *fe,
	struct dvb_frontend_tune_settings *s)
{
	s->min_delay_ms = 500;
	s->step_size = fe->ops.info.frequency_stepsize * 2;
	s->max_drift = (fe->ops.info.frequency_stepsize * 2) + 1;

	return 0;
}
