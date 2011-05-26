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

int cxd2820r_set_frontend_t2(struct dvb_frontend *fe,
	struct dvb_frontend_parameters *params)
{
	struct cxd2820r_priv *priv = fe->demodulator_priv;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	int ret, i;
	u32 if_khz, if_ctl;
	u64 num;
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
		{ 0x020cb, priv->cfg.if_agc_polarity << 6, 0x40 },
		{ 0x02070, priv->cfg.ts_mode, 0xff },
		{ 0x020b5, priv->cfg.spec_inv << 4, 0x10 },
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

	dbg("%s: RF=%d BW=%d", __func__, c->frequency, c->bandwidth_hz);

	/* update GPIOs */
	ret = cxd2820r_gpio(fe);
	if (ret)
		goto error;

	/* program tuner */
	if (fe->ops.tuner_ops.set_params)
		fe->ops.tuner_ops.set_params(fe, params);

	if (priv->delivery_system != SYS_DVBT2) {
		for (i = 0; i < ARRAY_SIZE(tab); i++) {
			ret = cxd2820r_wr_reg_mask(priv, tab[i].reg,
				tab[i].val, tab[i].mask);
			if (ret)
				goto error;
		}
	}

	priv->delivery_system = SYS_DVBT2;

	switch (c->bandwidth_hz) {
	case 5000000:
		if_khz = priv->cfg.if_dvbt2_5;
		i = 0;
		bw_param = 3;
		break;
	case 6000000:
		if_khz = priv->cfg.if_dvbt2_6;
		i = 1;
		bw_param = 2;
		break;
	case 7000000:
		if_khz = priv->cfg.if_dvbt2_7;
		i = 2;
		bw_param = 1;
		break;
	case 8000000:
		if_khz = priv->cfg.if_dvbt2_8;
		i = 3;
		bw_param = 0;
		break;
	default:
		return -EINVAL;
	}

	num = if_khz;
	num *= 0x1000000;
	if_ctl = cxd2820r_div_u64_round_closest(num, 41000);
	buf[0] = ((if_ctl >> 16) & 0xff);
	buf[1] = ((if_ctl >>  8) & 0xff);
	buf[2] = ((if_ctl >>  0) & 0xff);

	ret = cxd2820r_wr_regs(priv, 0x020b6, buf, 3);
	if (ret)
		goto error;

	ret = cxd2820r_wr_regs(priv, 0x0209f, bw_params1[i], 5);
	if (ret)
		goto error;

	ret = cxd2820r_wr_reg_mask(priv, 0x020d7, bw_param << 6, 0xc0);
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
	dbg("%s: failed:%d", __func__, ret);
	return ret;

}

int cxd2820r_get_frontend_t2(struct dvb_frontend *fe,
	struct dvb_frontend_parameters *p)
{
	struct cxd2820r_priv *priv = fe->demodulator_priv;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	int ret;
	u8 buf[2];

	ret = cxd2820r_rd_regs(priv, 0x0205c, buf, 2);
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

	ret = cxd2820r_rd_regs(priv, 0x0225b, buf, 2);
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

	ret = cxd2820r_rd_reg(priv, 0x020b5, &buf[0]);
	if (ret)
		goto error;

	switch ((buf[0] >> 4) & 0x01) {
	case 0:
		c->inversion = INVERSION_OFF;
		break;
	case 1:
		c->inversion = INVERSION_ON;
		break;
	}

	return ret;
error:
	dbg("%s: failed:%d", __func__, ret);
	return ret;
}

int cxd2820r_read_status_t2(struct dvb_frontend *fe, fe_status_t *status)
{
	struct cxd2820r_priv *priv = fe->demodulator_priv;
	int ret;
	u8 buf[1];
	*status = 0;

	ret = cxd2820r_rd_reg(priv, 0x02010 , &buf[0]);
	if (ret)
		goto error;

	if ((buf[0] & 0x07) == 6) {
		if (((buf[0] >> 5) & 0x01) == 1) {
			*status |= FE_HAS_SIGNAL | FE_HAS_CARRIER |
				FE_HAS_VITERBI | FE_HAS_SYNC | FE_HAS_LOCK;
		} else {
			*status |= FE_HAS_SIGNAL | FE_HAS_CARRIER |
				FE_HAS_VITERBI | FE_HAS_SYNC;
		}
	}

	dbg("%s: lock=%02x", __func__, buf[0]);

	return ret;
error:
	dbg("%s: failed:%d", __func__, ret);
	return ret;
}

int cxd2820r_read_ber_t2(struct dvb_frontend *fe, u32 *ber)
{
	struct cxd2820r_priv *priv = fe->demodulator_priv;
	int ret;
	u8 buf[4];
	unsigned int errbits;
	*ber = 0;
	/* FIXME: correct calculation */

	ret = cxd2820r_rd_regs(priv, 0x02039, buf, sizeof(buf));
	if (ret)
		goto error;

	if ((buf[0] >> 4) & 0x01) {
		errbits = (buf[0] & 0x0f) << 24 | buf[1] << 16 |
			buf[2] << 8 | buf[3];

		if (errbits)
			*ber = errbits * 64 / 16588800;
	}

	return ret;
error:
	dbg("%s: failed:%d", __func__, ret);
	return ret;
}

int cxd2820r_read_signal_strength_t2(struct dvb_frontend *fe,
	u16 *strength)
{
	struct cxd2820r_priv *priv = fe->demodulator_priv;
	int ret;
	u8 buf[2];
	u16 tmp;

	ret = cxd2820r_rd_regs(priv, 0x02026, buf, sizeof(buf));
	if (ret)
		goto error;

	tmp = (buf[0] & 0x0f) << 8 | buf[1];
	tmp = ~tmp & 0x0fff;

	/* scale value to 0x0000-0xffff from 0x0000-0x0fff */
	*strength = tmp * 0xffff / 0x0fff;

	return ret;
error:
	dbg("%s: failed:%d", __func__, ret);
	return ret;
}

int cxd2820r_read_snr_t2(struct dvb_frontend *fe, u16 *snr)
{
	struct cxd2820r_priv *priv = fe->demodulator_priv;
	int ret;
	u8 buf[2];
	u16 tmp;
	/* report SNR in dB * 10 */

	ret = cxd2820r_rd_regs(priv, 0x02028, buf, sizeof(buf));
	if (ret)
		goto error;

	tmp = (buf[0] & 0x0f) << 8 | buf[1];
	#define CXD2820R_LOG10_8_24 15151336 /* log10(8) << 24 */
	if (tmp)
		*snr = (intlog10(tmp) - CXD2820R_LOG10_8_24) / ((1 << 24)
			/ 100);
	else
		*snr = 0;

	dbg("%s: dBx10=%d val=%04x", __func__, *snr, tmp);

	return ret;
error:
	dbg("%s: failed:%d", __func__, ret);
	return ret;
}

int cxd2820r_read_ucblocks_t2(struct dvb_frontend *fe, u32 *ucblocks)
{
	*ucblocks = 0;
	/* no way to read ? */
	return 0;
}

int cxd2820r_sleep_t2(struct dvb_frontend *fe)
{
	struct cxd2820r_priv *priv = fe->demodulator_priv;
	int ret, i;
	struct reg_val_mask tab[] = {
		{ 0x000ff, 0x1f, 0xff },
		{ 0x00085, 0x00, 0xff },
		{ 0x00088, 0x01, 0xff },
		{ 0x02069, 0x00, 0xff },
		{ 0x00081, 0x00, 0xff },
		{ 0x00080, 0x00, 0xff },
	};

	dbg("%s", __func__);

	for (i = 0; i < ARRAY_SIZE(tab); i++) {
		ret = cxd2820r_wr_reg_mask(priv, tab[i].reg, tab[i].val,
			tab[i].mask);
		if (ret)
			goto error;
	}

	priv->delivery_system = SYS_UNDEFINED;

	return ret;
error:
	dbg("%s: failed:%d", __func__, ret);
	return ret;
}

int cxd2820r_get_tune_settings_t2(struct dvb_frontend *fe,
	struct dvb_frontend_tune_settings *s)
{
	s->min_delay_ms = 1500;
	s->step_size = fe->ops.info.frequency_stepsize * 2;
	s->max_drift = (fe->ops.info.frequency_stepsize * 2) + 1;

	return 0;
}

