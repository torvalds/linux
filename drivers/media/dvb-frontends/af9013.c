/*
 * Afatech AF9013 demodulator driver
 *
 * Copyright (C) 2007 Antti Palosaari <crope@iki.fi>
 * Copyright (C) 2011 Antti Palosaari <crope@iki.fi>
 *
 * Thanks to Afatech who kindly provided information.
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
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include "af9013_priv.h"

/* Max transfer size done by I2C transfer functions */
#define MAX_XFER_SIZE  64

struct af9013_state {
	struct i2c_adapter *i2c;
	struct dvb_frontend fe;
	struct af9013_config config;

	/* tuner/demod RF and IF AGC limits used for signal strength calc */
	u8 signal_strength_en, rf_50, rf_80, if_50, if_80;
	u16 signal_strength;
	u32 ber;
	u32 ucblocks;
	u16 snr;
	u32 bandwidth_hz;
	fe_status_t fe_status;
	unsigned long set_frontend_jiffies;
	unsigned long read_status_jiffies;
	bool first_tune;
	bool i2c_gate_state;
	unsigned int statistics_step:3;
	struct delayed_work statistics_work;
};

/* write multiple registers */
static int af9013_wr_regs_i2c(struct af9013_state *priv, u8 mbox, u16 reg,
	const u8 *val, int len)
{
	int ret;
	u8 buf[MAX_XFER_SIZE];
	struct i2c_msg msg[1] = {
		{
			.addr = priv->config.i2c_addr,
			.flags = 0,
			.len = 3 + len,
			.buf = buf,
		}
	};

	if (3 + len > sizeof(buf)) {
		dev_warn(&priv->i2c->dev,
			 "%s: i2c wr reg=%04x: len=%d is too big!\n",
			 KBUILD_MODNAME, reg, len);
		return -EINVAL;
	}

	buf[0] = (reg >> 8) & 0xff;
	buf[1] = (reg >> 0) & 0xff;
	buf[2] = mbox;
	memcpy(&buf[3], val, len);

	ret = i2c_transfer(priv->i2c, msg, 1);
	if (ret == 1) {
		ret = 0;
	} else {
		dev_warn(&priv->i2c->dev, "%s: i2c wr failed=%d reg=%04x " \
				"len=%d\n", KBUILD_MODNAME, ret, reg, len);
		ret = -EREMOTEIO;
	}
	return ret;
}

/* read multiple registers */
static int af9013_rd_regs_i2c(struct af9013_state *priv, u8 mbox, u16 reg,
	u8 *val, int len)
{
	int ret;
	u8 buf[3];
	struct i2c_msg msg[2] = {
		{
			.addr = priv->config.i2c_addr,
			.flags = 0,
			.len = 3,
			.buf = buf,
		}, {
			.addr = priv->config.i2c_addr,
			.flags = I2C_M_RD,
			.len = len,
			.buf = val,
		}
	};

	buf[0] = (reg >> 8) & 0xff;
	buf[1] = (reg >> 0) & 0xff;
	buf[2] = mbox;

	ret = i2c_transfer(priv->i2c, msg, 2);
	if (ret == 2) {
		ret = 0;
	} else {
		dev_warn(&priv->i2c->dev, "%s: i2c rd failed=%d reg=%04x " \
				"len=%d\n", KBUILD_MODNAME, ret, reg, len);
		ret = -EREMOTEIO;
	}
	return ret;
}

/* write multiple registers */
static int af9013_wr_regs(struct af9013_state *priv, u16 reg, const u8 *val,
	int len)
{
	int ret, i;
	u8 mbox = (0 << 7)|(0 << 6)|(1 << 1)|(1 << 0);

	if ((priv->config.ts_mode == AF9013_TS_USB) &&
		((reg & 0xff00) != 0xff00) && ((reg & 0xff00) != 0xae00)) {
		mbox |= ((len - 1) << 2);
		ret = af9013_wr_regs_i2c(priv, mbox, reg, val, len);
	} else {
		for (i = 0; i < len; i++) {
			ret = af9013_wr_regs_i2c(priv, mbox, reg+i, val+i, 1);
			if (ret)
				goto err;
		}
	}

err:
	return 0;
}

/* read multiple registers */
static int af9013_rd_regs(struct af9013_state *priv, u16 reg, u8 *val, int len)
{
	int ret, i;
	u8 mbox = (0 << 7)|(0 << 6)|(1 << 1)|(0 << 0);

	if ((priv->config.ts_mode == AF9013_TS_USB) &&
		((reg & 0xff00) != 0xff00) && ((reg & 0xff00) != 0xae00)) {
		mbox |= ((len - 1) << 2);
		ret = af9013_rd_regs_i2c(priv, mbox, reg, val, len);
	} else {
		for (i = 0; i < len; i++) {
			ret = af9013_rd_regs_i2c(priv, mbox, reg+i, val+i, 1);
			if (ret)
				goto err;
		}
	}

err:
	return 0;
}

/* write single register */
static int af9013_wr_reg(struct af9013_state *priv, u16 reg, u8 val)
{
	return af9013_wr_regs(priv, reg, &val, 1);
}

/* read single register */
static int af9013_rd_reg(struct af9013_state *priv, u16 reg, u8 *val)
{
	return af9013_rd_regs(priv, reg, val, 1);
}

static int af9013_write_ofsm_regs(struct af9013_state *state, u16 reg, u8 *val,
	u8 len)
{
	u8 mbox = (1 << 7)|(1 << 6)|((len - 1) << 2)|(1 << 1)|(1 << 0);
	return af9013_wr_regs_i2c(state, mbox, reg, val, len);
}

static int af9013_wr_reg_bits(struct af9013_state *state, u16 reg, int pos,
	int len, u8 val)
{
	int ret;
	u8 tmp, mask;

	/* no need for read if whole reg is written */
	if (len != 8) {
		ret = af9013_rd_reg(state, reg, &tmp);
		if (ret)
			return ret;

		mask = (0xff >> (8 - len)) << pos;
		val <<= pos;
		tmp &= ~mask;
		val |= tmp;
	}

	return af9013_wr_reg(state, reg, val);
}

static int af9013_rd_reg_bits(struct af9013_state *state, u16 reg, int pos,
	int len, u8 *val)
{
	int ret;
	u8 tmp;

	ret = af9013_rd_reg(state, reg, &tmp);
	if (ret)
		return ret;

	*val = (tmp >> pos);
	*val &= (0xff >> (8 - len));

	return 0;
}

static int af9013_set_gpio(struct af9013_state *state, u8 gpio, u8 gpioval)
{
	int ret;
	u8 pos;
	u16 addr;

	dev_dbg(&state->i2c->dev, "%s: gpio=%d gpioval=%02x\n",
			__func__, gpio, gpioval);

	/*
	 * GPIO0 & GPIO1 0xd735
	 * GPIO2 & GPIO3 0xd736
	 */

	switch (gpio) {
	case 0:
	case 1:
		addr = 0xd735;
		break;
	case 2:
	case 3:
		addr = 0xd736;
		break;

	default:
		dev_err(&state->i2c->dev, "%s: invalid gpio=%d\n",
				KBUILD_MODNAME, gpio);
		ret = -EINVAL;
		goto err;
	}

	switch (gpio) {
	case 0:
	case 2:
		pos = 0;
		break;
	case 1:
	case 3:
	default:
		pos = 4;
		break;
	}

	ret = af9013_wr_reg_bits(state, addr, pos, 4, gpioval);
	if (ret)
		goto err;

	return ret;
err:
	dev_dbg(&state->i2c->dev, "%s: failed=%d\n", __func__, ret);
	return ret;
}

static u32 af9013_div(struct af9013_state *state, u32 a, u32 b, u32 x)
{
	u32 r = 0, c = 0, i;

	dev_dbg(&state->i2c->dev, "%s: a=%d b=%d x=%d\n", __func__, a, b, x);

	if (a > b) {
		c = a / b;
		a = a - c * b;
	}

	for (i = 0; i < x; i++) {
		if (a >= b) {
			r += 1;
			a -= b;
		}
		a <<= 1;
		r <<= 1;
	}
	r = (c << (u32)x) + r;

	dev_dbg(&state->i2c->dev, "%s: a=%d b=%d x=%d r=%d r=%x\n",
			__func__, a, b, x, r, r);

	return r;
}

static int af9013_power_ctrl(struct af9013_state *state, u8 onoff)
{
	int ret, i;
	u8 tmp;

	dev_dbg(&state->i2c->dev, "%s: onoff=%d\n", __func__, onoff);

	/* enable reset */
	ret = af9013_wr_reg_bits(state, 0xd417, 4, 1, 1);
	if (ret)
		goto err;

	/* start reset mechanism */
	ret = af9013_wr_reg(state, 0xaeff, 1);
	if (ret)
		goto err;

	/* wait reset performs */
	for (i = 0; i < 150; i++) {
		ret = af9013_rd_reg_bits(state, 0xd417, 1, 1, &tmp);
		if (ret)
			goto err;

		if (tmp)
			break; /* reset done */

		usleep_range(5000, 25000);
	}

	if (!tmp)
		return -ETIMEDOUT;

	if (onoff) {
		/* clear reset */
		ret = af9013_wr_reg_bits(state, 0xd417, 1, 1, 0);
		if (ret)
			goto err;

		/* disable reset */
		ret = af9013_wr_reg_bits(state, 0xd417, 4, 1, 0);

		/* power on */
		ret = af9013_wr_reg_bits(state, 0xd73a, 3, 1, 0);
	} else {
		/* power off */
		ret = af9013_wr_reg_bits(state, 0xd73a, 3, 1, 1);
	}

	return ret;
err:
	dev_dbg(&state->i2c->dev, "%s: failed=%d\n", __func__, ret);
	return ret;
}

static int af9013_statistics_ber_unc_start(struct dvb_frontend *fe)
{
	struct af9013_state *state = fe->demodulator_priv;
	int ret;

	dev_dbg(&state->i2c->dev, "%s:\n", __func__);

	/* reset and start BER counter */
	ret = af9013_wr_reg_bits(state, 0xd391, 4, 1, 1);
	if (ret)
		goto err;

	return ret;
err:
	dev_dbg(&state->i2c->dev, "%s: failed=%d\n", __func__, ret);
	return ret;
}

static int af9013_statistics_ber_unc_result(struct dvb_frontend *fe)
{
	struct af9013_state *state = fe->demodulator_priv;
	int ret;
	u8 buf[5];

	dev_dbg(&state->i2c->dev, "%s:\n", __func__);

	/* check if error bit count is ready */
	ret = af9013_rd_reg_bits(state, 0xd391, 4, 1, &buf[0]);
	if (ret)
		goto err;

	if (!buf[0]) {
		dev_dbg(&state->i2c->dev, "%s: not ready\n", __func__);
		return 0;
	}

	ret = af9013_rd_regs(state, 0xd387, buf, 5);
	if (ret)
		goto err;

	state->ber = (buf[2] << 16) | (buf[1] << 8) | buf[0];
	state->ucblocks += (buf[4] << 8) | buf[3];

	return ret;
err:
	dev_dbg(&state->i2c->dev, "%s: failed=%d\n", __func__, ret);
	return ret;
}

static int af9013_statistics_snr_start(struct dvb_frontend *fe)
{
	struct af9013_state *state = fe->demodulator_priv;
	int ret;

	dev_dbg(&state->i2c->dev, "%s:\n", __func__);

	/* start SNR meas */
	ret = af9013_wr_reg_bits(state, 0xd2e1, 3, 1, 1);
	if (ret)
		goto err;

	return ret;
err:
	dev_dbg(&state->i2c->dev, "%s: failed=%d\n", __func__, ret);
	return ret;
}

static int af9013_statistics_snr_result(struct dvb_frontend *fe)
{
	struct af9013_state *state = fe->demodulator_priv;
	int ret, i, len;
	u8 buf[3], tmp;
	u32 snr_val;
	const struct af9013_snr *uninitialized_var(snr_lut);

	dev_dbg(&state->i2c->dev, "%s:\n", __func__);

	/* check if SNR ready */
	ret = af9013_rd_reg_bits(state, 0xd2e1, 3, 1, &tmp);
	if (ret)
		goto err;

	if (!tmp) {
		dev_dbg(&state->i2c->dev, "%s: not ready\n", __func__);
		return 0;
	}

	/* read value */
	ret = af9013_rd_regs(state, 0xd2e3, buf, 3);
	if (ret)
		goto err;

	snr_val = (buf[2] << 16) | (buf[1] << 8) | buf[0];

	/* read current modulation */
	ret = af9013_rd_reg(state, 0xd3c1, &tmp);
	if (ret)
		goto err;

	switch ((tmp >> 6) & 3) {
	case 0:
		len = ARRAY_SIZE(qpsk_snr_lut);
		snr_lut = qpsk_snr_lut;
		break;
	case 1:
		len = ARRAY_SIZE(qam16_snr_lut);
		snr_lut = qam16_snr_lut;
		break;
	case 2:
		len = ARRAY_SIZE(qam64_snr_lut);
		snr_lut = qam64_snr_lut;
		break;
	default:
		goto err;
		break;
	}

	for (i = 0; i < len; i++) {
		tmp = snr_lut[i].snr;

		if (snr_val < snr_lut[i].val)
			break;
	}
	state->snr = tmp * 10; /* dB/10 */

	return ret;
err:
	dev_dbg(&state->i2c->dev, "%s: failed=%d\n", __func__, ret);
	return ret;
}

static int af9013_statistics_signal_strength(struct dvb_frontend *fe)
{
	struct af9013_state *state = fe->demodulator_priv;
	int ret = 0;
	u8 buf[2], rf_gain, if_gain;
	int signal_strength;

	dev_dbg(&state->i2c->dev, "%s:\n", __func__);

	if (!state->signal_strength_en)
		return 0;

	ret = af9013_rd_regs(state, 0xd07c, buf, 2);
	if (ret)
		goto err;

	rf_gain = buf[0];
	if_gain = buf[1];

	signal_strength = (0xffff / \
		(9 * (state->rf_50 + state->if_50) - \
		11 * (state->rf_80 + state->if_80))) * \
		(10 * (rf_gain + if_gain) - \
		11 * (state->rf_80 + state->if_80));
	if (signal_strength < 0)
		signal_strength = 0;
	else if (signal_strength > 0xffff)
		signal_strength = 0xffff;

	state->signal_strength = signal_strength;

	return ret;
err:
	dev_dbg(&state->i2c->dev, "%s: failed=%d\n", __func__, ret);
	return ret;
}

static void af9013_statistics_work(struct work_struct *work)
{
	struct af9013_state *state = container_of(work,
		struct af9013_state, statistics_work.work);
	unsigned int next_msec;

	/* update only signal strength when demod is not locked */
	if (!(state->fe_status & FE_HAS_LOCK)) {
		state->statistics_step = 0;
		state->ber = 0;
		state->snr = 0;
	}

	switch (state->statistics_step) {
	default:
		state->statistics_step = 0;
	case 0:
		af9013_statistics_signal_strength(&state->fe);
		state->statistics_step++;
		next_msec = 300;
		break;
	case 1:
		af9013_statistics_snr_start(&state->fe);
		state->statistics_step++;
		next_msec = 200;
		break;
	case 2:
		af9013_statistics_ber_unc_start(&state->fe);
		state->statistics_step++;
		next_msec = 1000;
		break;
	case 3:
		af9013_statistics_snr_result(&state->fe);
		state->statistics_step++;
		next_msec = 400;
		break;
	case 4:
		af9013_statistics_ber_unc_result(&state->fe);
		state->statistics_step++;
		next_msec = 100;
		break;
	}

	schedule_delayed_work(&state->statistics_work,
		msecs_to_jiffies(next_msec));
}

static int af9013_get_tune_settings(struct dvb_frontend *fe,
	struct dvb_frontend_tune_settings *fesettings)
{
	fesettings->min_delay_ms = 800;
	fesettings->step_size = 0;
	fesettings->max_drift = 0;

	return 0;
}

static int af9013_set_frontend(struct dvb_frontend *fe)
{
	struct af9013_state *state = fe->demodulator_priv;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	int ret, i, sampling_freq;
	bool auto_mode, spec_inv;
	u8 buf[6];
	u32 if_frequency, freq_cw;

	dev_dbg(&state->i2c->dev, "%s: frequency=%d bandwidth_hz=%d\n",
			__func__, c->frequency, c->bandwidth_hz);

	/* program tuner */
	if (fe->ops.tuner_ops.set_params)
		fe->ops.tuner_ops.set_params(fe);

	/* program CFOE coefficients */
	if (c->bandwidth_hz != state->bandwidth_hz) {
		for (i = 0; i < ARRAY_SIZE(coeff_lut); i++) {
			if (coeff_lut[i].clock == state->config.clock &&
				coeff_lut[i].bandwidth_hz == c->bandwidth_hz) {
				break;
			}
		}

		/* Return an error if can't find bandwidth or the right clock */
		if (i == ARRAY_SIZE(coeff_lut))
			return -EINVAL;

		ret = af9013_wr_regs(state, 0xae00, coeff_lut[i].val,
			sizeof(coeff_lut[i].val));
	}

	/* program frequency control */
	if (c->bandwidth_hz != state->bandwidth_hz || state->first_tune) {
		/* get used IF frequency */
		if (fe->ops.tuner_ops.get_if_frequency)
			fe->ops.tuner_ops.get_if_frequency(fe, &if_frequency);
		else
			if_frequency = state->config.if_frequency;

		dev_dbg(&state->i2c->dev, "%s: if_frequency=%d\n",
				__func__, if_frequency);

		sampling_freq = if_frequency;

		while (sampling_freq > (state->config.clock / 2))
			sampling_freq -= state->config.clock;

		if (sampling_freq < 0) {
			sampling_freq *= -1;
			spec_inv = state->config.spec_inv;
		} else {
			spec_inv = !state->config.spec_inv;
		}

		freq_cw = af9013_div(state, sampling_freq, state->config.clock,
				23);

		if (spec_inv)
			freq_cw = 0x800000 - freq_cw;

		buf[0] = (freq_cw >>  0) & 0xff;
		buf[1] = (freq_cw >>  8) & 0xff;
		buf[2] = (freq_cw >> 16) & 0x7f;

		freq_cw = 0x800000 - freq_cw;

		buf[3] = (freq_cw >>  0) & 0xff;
		buf[4] = (freq_cw >>  8) & 0xff;
		buf[5] = (freq_cw >> 16) & 0x7f;

		ret = af9013_wr_regs(state, 0xd140, buf, 3);
		if (ret)
			goto err;

		ret = af9013_wr_regs(state, 0x9be7, buf, 6);
		if (ret)
			goto err;
	}

	/* clear TPS lock flag */
	ret = af9013_wr_reg_bits(state, 0xd330, 3, 1, 1);
	if (ret)
		goto err;

	/* clear MPEG2 lock flag */
	ret = af9013_wr_reg_bits(state, 0xd507, 6, 1, 0);
	if (ret)
		goto err;

	/* empty channel function */
	ret = af9013_wr_reg_bits(state, 0x9bfe, 0, 1, 0);
	if (ret)
		goto err;

	/* empty DVB-T channel function */
	ret = af9013_wr_reg_bits(state, 0x9bc2, 0, 1, 0);
	if (ret)
		goto err;

	/* transmission parameters */
	auto_mode = false;
	memset(buf, 0, 3);

	switch (c->transmission_mode) {
	case TRANSMISSION_MODE_AUTO:
		auto_mode = 1;
		break;
	case TRANSMISSION_MODE_2K:
		break;
	case TRANSMISSION_MODE_8K:
		buf[0] |= (1 << 0);
		break;
	default:
		dev_dbg(&state->i2c->dev, "%s: invalid transmission_mode\n",
				__func__);
		auto_mode = 1;
	}

	switch (c->guard_interval) {
	case GUARD_INTERVAL_AUTO:
		auto_mode = 1;
		break;
	case GUARD_INTERVAL_1_32:
		break;
	case GUARD_INTERVAL_1_16:
		buf[0] |= (1 << 2);
		break;
	case GUARD_INTERVAL_1_8:
		buf[0] |= (2 << 2);
		break;
	case GUARD_INTERVAL_1_4:
		buf[0] |= (3 << 2);
		break;
	default:
		dev_dbg(&state->i2c->dev, "%s: invalid guard_interval\n",
				__func__);
		auto_mode = 1;
	}

	switch (c->hierarchy) {
	case HIERARCHY_AUTO:
		auto_mode = 1;
		break;
	case HIERARCHY_NONE:
		break;
	case HIERARCHY_1:
		buf[0] |= (1 << 4);
		break;
	case HIERARCHY_2:
		buf[0] |= (2 << 4);
		break;
	case HIERARCHY_4:
		buf[0] |= (3 << 4);
		break;
	default:
		dev_dbg(&state->i2c->dev, "%s: invalid hierarchy\n", __func__);
		auto_mode = 1;
	}

	switch (c->modulation) {
	case QAM_AUTO:
		auto_mode = 1;
		break;
	case QPSK:
		break;
	case QAM_16:
		buf[1] |= (1 << 6);
		break;
	case QAM_64:
		buf[1] |= (2 << 6);
		break;
	default:
		dev_dbg(&state->i2c->dev, "%s: invalid modulation\n", __func__);
		auto_mode = 1;
	}

	/* Use HP. How and which case we can switch to LP? */
	buf[1] |= (1 << 4);

	switch (c->code_rate_HP) {
	case FEC_AUTO:
		auto_mode = 1;
		break;
	case FEC_1_2:
		break;
	case FEC_2_3:
		buf[2] |= (1 << 0);
		break;
	case FEC_3_4:
		buf[2] |= (2 << 0);
		break;
	case FEC_5_6:
		buf[2] |= (3 << 0);
		break;
	case FEC_7_8:
		buf[2] |= (4 << 0);
		break;
	default:
		dev_dbg(&state->i2c->dev, "%s: invalid code_rate_HP\n",
				__func__);
		auto_mode = 1;
	}

	switch (c->code_rate_LP) {
	case FEC_AUTO:
		auto_mode = 1;
		break;
	case FEC_1_2:
		break;
	case FEC_2_3:
		buf[2] |= (1 << 3);
		break;
	case FEC_3_4:
		buf[2] |= (2 << 3);
		break;
	case FEC_5_6:
		buf[2] |= (3 << 3);
		break;
	case FEC_7_8:
		buf[2] |= (4 << 3);
		break;
	case FEC_NONE:
		break;
	default:
		dev_dbg(&state->i2c->dev, "%s: invalid code_rate_LP\n",
				__func__);
		auto_mode = 1;
	}

	switch (c->bandwidth_hz) {
	case 6000000:
		break;
	case 7000000:
		buf[1] |= (1 << 2);
		break;
	case 8000000:
		buf[1] |= (2 << 2);
		break;
	default:
		dev_dbg(&state->i2c->dev, "%s: invalid bandwidth_hz\n",
				__func__);
		ret = -EINVAL;
		goto err;
	}

	ret = af9013_wr_regs(state, 0xd3c0, buf, 3);
	if (ret)
		goto err;

	if (auto_mode) {
		/* clear easy mode flag */
		ret = af9013_wr_reg(state, 0xaefd, 0);
		if (ret)
			goto err;

		dev_dbg(&state->i2c->dev, "%s: auto params\n", __func__);
	} else {
		/* set easy mode flag */
		ret = af9013_wr_reg(state, 0xaefd, 1);
		if (ret)
			goto err;

		ret = af9013_wr_reg(state, 0xaefe, 0);
		if (ret)
			goto err;

		dev_dbg(&state->i2c->dev, "%s: manual params\n", __func__);
	}

	/* tune */
	ret = af9013_wr_reg(state, 0xffff, 0);
	if (ret)
		goto err;

	state->bandwidth_hz = c->bandwidth_hz;
	state->set_frontend_jiffies = jiffies;
	state->first_tune = false;

	return ret;
err:
	dev_dbg(&state->i2c->dev, "%s: failed=%d\n", __func__, ret);
	return ret;
}

static int af9013_get_frontend(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	struct af9013_state *state = fe->demodulator_priv;
	int ret;
	u8 buf[3];

	dev_dbg(&state->i2c->dev, "%s:\n", __func__);

	ret = af9013_rd_regs(state, 0xd3c0, buf, 3);
	if (ret)
		goto err;

	switch ((buf[1] >> 6) & 3) {
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

	switch ((buf[0] >> 0) & 3) {
	case 0:
		c->transmission_mode = TRANSMISSION_MODE_2K;
		break;
	case 1:
		c->transmission_mode = TRANSMISSION_MODE_8K;
	}

	switch ((buf[0] >> 2) & 3) {
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

	switch ((buf[0] >> 4) & 7) {
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

	switch ((buf[2] >> 0) & 7) {
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

	switch ((buf[2] >> 3) & 7) {
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

	switch ((buf[1] >> 2) & 3) {
	case 0:
		c->bandwidth_hz = 6000000;
		break;
	case 1:
		c->bandwidth_hz = 7000000;
		break;
	case 2:
		c->bandwidth_hz = 8000000;
		break;
	}

	return ret;
err:
	dev_dbg(&state->i2c->dev, "%s: failed=%d\n", __func__, ret);
	return ret;
}

static int af9013_read_status(struct dvb_frontend *fe, fe_status_t *status)
{
	struct af9013_state *state = fe->demodulator_priv;
	int ret;
	u8 tmp;

	/*
	 * Return status from the cache if it is younger than 2000ms with the
	 * exception of last tune is done during 4000ms.
	 */
	if (time_is_after_jiffies(
		state->read_status_jiffies + msecs_to_jiffies(2000)) &&
		time_is_before_jiffies(
		state->set_frontend_jiffies + msecs_to_jiffies(4000))
	) {
			*status = state->fe_status;
			return 0;
	} else {
		*status = 0;
	}

	/* MPEG2 lock */
	ret = af9013_rd_reg_bits(state, 0xd507, 6, 1, &tmp);
	if (ret)
		goto err;

	if (tmp)
		*status |= FE_HAS_SIGNAL | FE_HAS_CARRIER | FE_HAS_VITERBI |
			FE_HAS_SYNC | FE_HAS_LOCK;

	if (!*status) {
		/* TPS lock */
		ret = af9013_rd_reg_bits(state, 0xd330, 3, 1, &tmp);
		if (ret)
			goto err;

		if (tmp)
			*status |= FE_HAS_SIGNAL | FE_HAS_CARRIER |
				FE_HAS_VITERBI;
	}

	state->fe_status = *status;
	state->read_status_jiffies = jiffies;

	return ret;
err:
	dev_dbg(&state->i2c->dev, "%s: failed=%d\n", __func__, ret);
	return ret;
}

static int af9013_read_snr(struct dvb_frontend *fe, u16 *snr)
{
	struct af9013_state *state = fe->demodulator_priv;
	*snr = state->snr;
	return 0;
}

static int af9013_read_signal_strength(struct dvb_frontend *fe, u16 *strength)
{
	struct af9013_state *state = fe->demodulator_priv;
	*strength = state->signal_strength;
	return 0;
}

static int af9013_read_ber(struct dvb_frontend *fe, u32 *ber)
{
	struct af9013_state *state = fe->demodulator_priv;
	*ber = state->ber;
	return 0;
}

static int af9013_read_ucblocks(struct dvb_frontend *fe, u32 *ucblocks)
{
	struct af9013_state *state = fe->demodulator_priv;
	*ucblocks = state->ucblocks;
	return 0;
}

static int af9013_init(struct dvb_frontend *fe)
{
	struct af9013_state *state = fe->demodulator_priv;
	int ret, i, len;
	u8 buf[3], tmp;
	u32 adc_cw;
	const struct af9013_reg_bit *init;

	dev_dbg(&state->i2c->dev, "%s:\n", __func__);

	/* power on */
	ret = af9013_power_ctrl(state, 1);
	if (ret)
		goto err;

	/* enable ADC */
	ret = af9013_wr_reg(state, 0xd73a, 0xa4);
	if (ret)
		goto err;

	/* write API version to firmware */
	ret = af9013_wr_regs(state, 0x9bf2, state->config.api_version, 4);
	if (ret)
		goto err;

	/* program ADC control */
	switch (state->config.clock) {
	case 28800000: /* 28.800 MHz */
		tmp = 0;
		break;
	case 20480000: /* 20.480 MHz */
		tmp = 1;
		break;
	case 28000000: /* 28.000 MHz */
		tmp = 2;
		break;
	case 25000000: /* 25.000 MHz */
		tmp = 3;
		break;
	default:
		dev_err(&state->i2c->dev, "%s: invalid clock\n",
				KBUILD_MODNAME);
		return -EINVAL;
	}

	adc_cw = af9013_div(state, state->config.clock, 1000000ul, 19);
	buf[0] = (adc_cw >>  0) & 0xff;
	buf[1] = (adc_cw >>  8) & 0xff;
	buf[2] = (adc_cw >> 16) & 0xff;

	ret = af9013_wr_regs(state, 0xd180, buf, 3);
	if (ret)
		goto err;

	ret = af9013_wr_reg_bits(state, 0x9bd2, 0, 4, tmp);
	if (ret)
		goto err;

	/* set I2C master clock */
	ret = af9013_wr_reg(state, 0xd416, 0x14);
	if (ret)
		goto err;

	/* set 16 embx */
	ret = af9013_wr_reg_bits(state, 0xd700, 1, 1, 1);
	if (ret)
		goto err;

	/* set no trigger */
	ret = af9013_wr_reg_bits(state, 0xd700, 2, 1, 0);
	if (ret)
		goto err;

	/* set read-update bit for constellation */
	ret = af9013_wr_reg_bits(state, 0xd371, 1, 1, 1);
	if (ret)
		goto err;

	/* settings for mp2if */
	if (state->config.ts_mode == AF9013_TS_USB) {
		/* AF9015 split PSB to 1.5k + 0.5k */
		ret = af9013_wr_reg_bits(state, 0xd50b, 2, 1, 1);
		if (ret)
			goto err;
	} else {
		/* AF9013 change the output bit to data7 */
		ret = af9013_wr_reg_bits(state, 0xd500, 3, 1, 1);
		if (ret)
			goto err;

		/* AF9013 set mpeg to full speed */
		ret = af9013_wr_reg_bits(state, 0xd502, 4, 1, 1);
		if (ret)
			goto err;
	}

	ret = af9013_wr_reg_bits(state, 0xd520, 4, 1, 1);
	if (ret)
		goto err;

	/* load OFSM settings */
	dev_dbg(&state->i2c->dev, "%s: load ofsm settings\n", __func__);
	len = ARRAY_SIZE(ofsm_init);
	init = ofsm_init;
	for (i = 0; i < len; i++) {
		ret = af9013_wr_reg_bits(state, init[i].addr, init[i].pos,
			init[i].len, init[i].val);
		if (ret)
			goto err;
	}

	/* load tuner specific settings */
	dev_dbg(&state->i2c->dev, "%s: load tuner specific settings\n",
			__func__);
	switch (state->config.tuner) {
	case AF9013_TUNER_MXL5003D:
		len = ARRAY_SIZE(tuner_init_mxl5003d);
		init = tuner_init_mxl5003d;
		break;
	case AF9013_TUNER_MXL5005D:
	case AF9013_TUNER_MXL5005R:
	case AF9013_TUNER_MXL5007T:
		len = ARRAY_SIZE(tuner_init_mxl5005);
		init = tuner_init_mxl5005;
		break;
	case AF9013_TUNER_ENV77H11D5:
		len = ARRAY_SIZE(tuner_init_env77h11d5);
		init = tuner_init_env77h11d5;
		break;
	case AF9013_TUNER_MT2060:
		len = ARRAY_SIZE(tuner_init_mt2060);
		init = tuner_init_mt2060;
		break;
	case AF9013_TUNER_MC44S803:
		len = ARRAY_SIZE(tuner_init_mc44s803);
		init = tuner_init_mc44s803;
		break;
	case AF9013_TUNER_QT1010:
	case AF9013_TUNER_QT1010A:
		len = ARRAY_SIZE(tuner_init_qt1010);
		init = tuner_init_qt1010;
		break;
	case AF9013_TUNER_MT2060_2:
		len = ARRAY_SIZE(tuner_init_mt2060_2);
		init = tuner_init_mt2060_2;
		break;
	case AF9013_TUNER_TDA18271:
	case AF9013_TUNER_TDA18218:
		len = ARRAY_SIZE(tuner_init_tda18271);
		init = tuner_init_tda18271;
		break;
	case AF9013_TUNER_UNKNOWN:
	default:
		len = ARRAY_SIZE(tuner_init_unknown);
		init = tuner_init_unknown;
		break;
	}

	for (i = 0; i < len; i++) {
		ret = af9013_wr_reg_bits(state, init[i].addr, init[i].pos,
			init[i].len, init[i].val);
		if (ret)
			goto err;
	}

	/* TS mode */
	ret = af9013_wr_reg_bits(state, 0xd500, 1, 2, state->config.ts_mode);
	if (ret)
		goto err;

	/* enable lock led */
	ret = af9013_wr_reg_bits(state, 0xd730, 0, 1, 1);
	if (ret)
		goto err;

	/* check if we support signal strength */
	if (!state->signal_strength_en) {
		ret = af9013_rd_reg_bits(state, 0x9bee, 0, 1,
			&state->signal_strength_en);
		if (ret)
			goto err;
	}

	/* read values needed for signal strength calculation */
	if (state->signal_strength_en && !state->rf_50) {
		ret = af9013_rd_reg(state, 0x9bbd, &state->rf_50);
		if (ret)
			goto err;

		ret = af9013_rd_reg(state, 0x9bd0, &state->rf_80);
		if (ret)
			goto err;

		ret = af9013_rd_reg(state, 0x9be2, &state->if_50);
		if (ret)
			goto err;

		ret = af9013_rd_reg(state, 0x9be4, &state->if_80);
		if (ret)
			goto err;
	}

	/* SNR */
	ret = af9013_wr_reg(state, 0xd2e2, 1);
	if (ret)
		goto err;

	/* BER / UCB */
	buf[0] = (10000 >> 0) & 0xff;
	buf[1] = (10000 >> 8) & 0xff;
	ret = af9013_wr_regs(state, 0xd385, buf, 2);
	if (ret)
		goto err;

	/* enable FEC monitor */
	ret = af9013_wr_reg_bits(state, 0xd392, 1, 1, 1);
	if (ret)
		goto err;

	state->first_tune = true;
	schedule_delayed_work(&state->statistics_work, msecs_to_jiffies(400));

	return ret;
err:
	dev_dbg(&state->i2c->dev, "%s: failed=%d\n", __func__, ret);
	return ret;
}

static int af9013_sleep(struct dvb_frontend *fe)
{
	struct af9013_state *state = fe->demodulator_priv;
	int ret;

	dev_dbg(&state->i2c->dev, "%s:\n", __func__);

	/* stop statistics polling */
	cancel_delayed_work_sync(&state->statistics_work);

	/* disable lock led */
	ret = af9013_wr_reg_bits(state, 0xd730, 0, 1, 0);
	if (ret)
		goto err;

	/* power off */
	ret = af9013_power_ctrl(state, 0);
	if (ret)
		goto err;

	return ret;
err:
	dev_dbg(&state->i2c->dev, "%s: failed=%d\n", __func__, ret);
	return ret;
}

static int af9013_i2c_gate_ctrl(struct dvb_frontend *fe, int enable)
{
	int ret;
	struct af9013_state *state = fe->demodulator_priv;

	dev_dbg(&state->i2c->dev, "%s: enable=%d\n", __func__, enable);

	/* gate already open or close */
	if (state->i2c_gate_state == enable)
		return 0;

	if (state->config.ts_mode == AF9013_TS_USB)
		ret = af9013_wr_reg_bits(state, 0xd417, 3, 1, enable);
	else
		ret = af9013_wr_reg_bits(state, 0xd607, 2, 1, enable);
	if (ret)
		goto err;

	state->i2c_gate_state = enable;

	return ret;
err:
	dev_dbg(&state->i2c->dev, "%s: failed=%d\n", __func__, ret);
	return ret;
}

static void af9013_release(struct dvb_frontend *fe)
{
	struct af9013_state *state = fe->demodulator_priv;
	kfree(state);
}

static struct dvb_frontend_ops af9013_ops;

static int af9013_download_firmware(struct af9013_state *state)
{
	int i, len, remaining, ret;
	const struct firmware *fw;
	u16 checksum = 0;
	u8 val;
	u8 fw_params[4];
	u8 *fw_file = AF9013_FIRMWARE;

	msleep(100);
	/* check whether firmware is already running */
	ret = af9013_rd_reg(state, 0x98be, &val);
	if (ret)
		goto err;
	else
		dev_dbg(&state->i2c->dev, "%s: firmware status=%02x\n",
				__func__, val);

	if (val == 0x0c) /* fw is running, no need for download */
		goto exit;

	dev_info(&state->i2c->dev, "%s: found a '%s' in cold state, will try " \
			"to load a firmware\n",
			KBUILD_MODNAME, af9013_ops.info.name);

	/* request the firmware, this will block and timeout */
	ret = request_firmware(&fw, fw_file, state->i2c->dev.parent);
	if (ret) {
		dev_info(&state->i2c->dev, "%s: did not find the firmware " \
			"file. (%s) Please see linux/Documentation/dvb/ for " \
			"more details on firmware-problems. (%d)\n",
			KBUILD_MODNAME, fw_file, ret);
		goto err;
	}

	dev_info(&state->i2c->dev, "%s: downloading firmware from file '%s'\n",
			KBUILD_MODNAME, fw_file);

	/* calc checksum */
	for (i = 0; i < fw->size; i++)
		checksum += fw->data[i];

	fw_params[0] = checksum >> 8;
	fw_params[1] = checksum & 0xff;
	fw_params[2] = fw->size >> 8;
	fw_params[3] = fw->size & 0xff;

	/* write fw checksum & size */
	ret = af9013_write_ofsm_regs(state, 0x50fc,
		fw_params, sizeof(fw_params));
	if (ret)
		goto err_release;

	#define FW_ADDR 0x5100 /* firmware start address */
	#define LEN_MAX 16 /* max packet size */
	for (remaining = fw->size; remaining > 0; remaining -= LEN_MAX) {
		len = remaining;
		if (len > LEN_MAX)
			len = LEN_MAX;

		ret = af9013_write_ofsm_regs(state,
			FW_ADDR + fw->size - remaining,
			(u8 *) &fw->data[fw->size - remaining], len);
		if (ret) {
			dev_err(&state->i2c->dev,
					"%s: firmware download failed=%d\n",
					KBUILD_MODNAME, ret);
			goto err_release;
		}
	}

	/* request boot firmware */
	ret = af9013_wr_reg(state, 0xe205, 1);
	if (ret)
		goto err_release;

	for (i = 0; i < 15; i++) {
		msleep(100);

		/* check firmware status */
		ret = af9013_rd_reg(state, 0x98be, &val);
		if (ret)
			goto err_release;

		dev_dbg(&state->i2c->dev, "%s: firmware status=%02x\n",
				__func__, val);

		if (val == 0x0c || val == 0x04) /* success or fail */
			break;
	}

	if (val == 0x04) {
		dev_err(&state->i2c->dev, "%s: firmware did not run\n",
				KBUILD_MODNAME);
		ret = -ENODEV;
	} else if (val != 0x0c) {
		dev_err(&state->i2c->dev, "%s: firmware boot timeout\n",
				KBUILD_MODNAME);
		ret = -ENODEV;
	}

err_release:
	release_firmware(fw);
err:
exit:
	if (!ret)
		dev_info(&state->i2c->dev, "%s: found a '%s' in warm state\n",
				KBUILD_MODNAME, af9013_ops.info.name);
	return ret;
}

struct dvb_frontend *af9013_attach(const struct af9013_config *config,
	struct i2c_adapter *i2c)
{
	int ret;
	struct af9013_state *state = NULL;
	u8 buf[4], i;

	/* allocate memory for the internal state */
	state = kzalloc(sizeof(struct af9013_state), GFP_KERNEL);
	if (state == NULL)
		goto err;

	/* setup the state */
	state->i2c = i2c;
	memcpy(&state->config, config, sizeof(struct af9013_config));

	/* download firmware */
	if (state->config.ts_mode != AF9013_TS_USB) {
		ret = af9013_download_firmware(state);
		if (ret)
			goto err;
	}

	/* firmware version */
	ret = af9013_rd_regs(state, 0x5103, buf, 4);
	if (ret)
		goto err;

	dev_info(&state->i2c->dev, "%s: firmware version %d.%d.%d.%d\n",
			KBUILD_MODNAME, buf[0], buf[1], buf[2], buf[3]);

	/* set GPIOs */
	for (i = 0; i < sizeof(state->config.gpio); i++) {
		ret = af9013_set_gpio(state, i, state->config.gpio[i]);
		if (ret)
			goto err;
	}

	/* create dvb_frontend */
	memcpy(&state->fe.ops, &af9013_ops,
		sizeof(struct dvb_frontend_ops));
	state->fe.demodulator_priv = state;

	INIT_DELAYED_WORK(&state->statistics_work, af9013_statistics_work);

	return &state->fe;
err:
	kfree(state);
	return NULL;
}
EXPORT_SYMBOL(af9013_attach);

static struct dvb_frontend_ops af9013_ops = {
	.delsys = { SYS_DVBT },
	.info = {
		.name = "Afatech AF9013",
		.frequency_min = 174000000,
		.frequency_max = 862000000,
		.frequency_stepsize = 250000,
		.frequency_tolerance = 0,
		.caps =	FE_CAN_FEC_1_2 |
			FE_CAN_FEC_2_3 |
			FE_CAN_FEC_3_4 |
			FE_CAN_FEC_5_6 |
			FE_CAN_FEC_7_8 |
			FE_CAN_FEC_AUTO |
			FE_CAN_QPSK |
			FE_CAN_QAM_16 |
			FE_CAN_QAM_64 |
			FE_CAN_QAM_AUTO |
			FE_CAN_TRANSMISSION_MODE_AUTO |
			FE_CAN_GUARD_INTERVAL_AUTO |
			FE_CAN_HIERARCHY_AUTO |
			FE_CAN_RECOVER |
			FE_CAN_MUTE_TS
	},

	.release = af9013_release,

	.init = af9013_init,
	.sleep = af9013_sleep,

	.get_tune_settings = af9013_get_tune_settings,
	.set_frontend = af9013_set_frontend,
	.get_frontend = af9013_get_frontend,

	.read_status = af9013_read_status,
	.read_snr = af9013_read_snr,
	.read_signal_strength = af9013_read_signal_strength,
	.read_ber = af9013_read_ber,
	.read_ucblocks = af9013_read_ucblocks,

	.i2c_gate_ctrl = af9013_i2c_gate_ctrl,
};

MODULE_AUTHOR("Antti Palosaari <crope@iki.fi>");
MODULE_DESCRIPTION("Afatech AF9013 DVB-T demodulator driver");
MODULE_LICENSE("GPL");
MODULE_FIRMWARE(AF9013_FIRMWARE);
