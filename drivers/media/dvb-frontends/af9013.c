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
 */

#include "af9013_priv.h"

struct af9013_state {
	struct i2c_client *client;
	struct regmap *regmap;
	struct dvb_frontend fe;
	u32 clk;
	u8 tuner;
	u32 if_frequency;
	u8 ts_mode;
	u8 ts_output_pin;
	bool spec_inv;
	u8 api_version[4];
	u8 gpio[4];

	/* tuner/demod RF and IF AGC limits used for signal strength calc */
	u8 signal_strength_en, rf_50, rf_80, if_50, if_80;
	u16 signal_strength;
	u32 ber;
	u32 ucblocks;
	u16 snr;
	u32 bandwidth_hz;
	enum fe_status fe_status;
	unsigned long set_frontend_jiffies;
	unsigned long read_status_jiffies;
	bool first_tune;
	bool i2c_gate_state;
	unsigned int statistics_step:3;
	struct delayed_work statistics_work;
};

static int af9013_set_gpio(struct af9013_state *state, u8 gpio, u8 gpioval)
{
	struct i2c_client *client = state->client;
	int ret;
	u8 pos;
	u16 addr;

	dev_dbg(&client->dev, "gpio %u, gpioval %02x\n", gpio, gpioval);

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

	ret = regmap_update_bits(state->regmap, addr, 0x0f << pos,
				 gpioval << pos);
	if (ret)
		goto err;

	return 0;
err:
	dev_dbg(&client->dev, "failed %d\n", ret);
	return ret;
}

static int af9013_statistics_ber_unc_start(struct dvb_frontend *fe)
{
	struct af9013_state *state = fe->demodulator_priv;
	struct i2c_client *client = state->client;
	int ret;

	dev_dbg(&client->dev, "\n");

	/* reset and start BER counter */
	ret = regmap_update_bits(state->regmap, 0xd391, 0x10, 0x10);
	if (ret)
		goto err;

	return 0;
err:
	dev_dbg(&client->dev, "failed %d\n", ret);
	return ret;
}

static int af9013_statistics_ber_unc_result(struct dvb_frontend *fe)
{
	struct af9013_state *state = fe->demodulator_priv;
	struct i2c_client *client = state->client;
	int ret;
	unsigned int utmp;
	u8 buf[5];

	dev_dbg(&client->dev, "\n");

	/* check if error bit count is ready */
	ret = regmap_read(state->regmap, 0xd391, &utmp);
	if (ret)
		goto err;

	if (!((utmp >> 4) & 0x01)) {
		dev_dbg(&client->dev, "not ready\n");
		return 0;
	}

	ret = regmap_bulk_read(state->regmap, 0xd387, buf, 5);
	if (ret)
		goto err;

	state->ber = (buf[2] << 16) | (buf[1] << 8) | buf[0];
	state->ucblocks += (buf[4] << 8) | buf[3];

	return 0;
err:
	dev_dbg(&client->dev, "failed %d\n", ret);
	return ret;
}

static int af9013_statistics_snr_start(struct dvb_frontend *fe)
{
	struct af9013_state *state = fe->demodulator_priv;
	struct i2c_client *client = state->client;
	int ret;

	dev_dbg(&client->dev, "\n");

	/* start SNR meas */
	ret = regmap_update_bits(state->regmap, 0xd2e1, 0x08, 0x08);
	if (ret)
		goto err;

	return 0;
err:
	dev_dbg(&client->dev, "failed %d\n", ret);
	return ret;
}

static int af9013_statistics_snr_result(struct dvb_frontend *fe)
{
	struct af9013_state *state = fe->demodulator_priv;
	struct i2c_client *client = state->client;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	int ret, i, len;
	unsigned int utmp;
	u8 buf[3];
	u32 snr_val;
	const struct af9013_snr *uninitialized_var(snr_lut);

	dev_dbg(&client->dev, "\n");

	/* check if SNR ready */
	ret = regmap_read(state->regmap, 0xd2e1, &utmp);
	if (ret)
		goto err;

	if (!((utmp >> 3) & 0x01)) {
		dev_dbg(&client->dev, "not ready\n");
		return 0;
	}

	/* read value */
	ret = regmap_bulk_read(state->regmap, 0xd2e3, buf, 3);
	if (ret)
		goto err;

	snr_val = (buf[2] << 16) | (buf[1] << 8) | buf[0];

	/* read current modulation */
	ret = regmap_read(state->regmap, 0xd3c1, &utmp);
	if (ret)
		goto err;

	switch ((utmp >> 6) & 3) {
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
	}

	for (i = 0; i < len; i++) {
		utmp = snr_lut[i].snr;

		if (snr_val < snr_lut[i].val)
			break;
	}
	state->snr = utmp * 10; /* dB/10 */

	c->cnr.stat[0].svalue = 1000 * utmp;
	c->cnr.stat[0].scale = FE_SCALE_DECIBEL;

	return 0;
err:
	dev_dbg(&client->dev, "failed %d\n", ret);
	return ret;
}

static int af9013_statistics_signal_strength(struct dvb_frontend *fe)
{
	struct af9013_state *state = fe->demodulator_priv;
	struct i2c_client *client = state->client;
	int ret = 0;
	u8 buf[2], rf_gain, if_gain;
	int signal_strength;

	dev_dbg(&client->dev, "\n");

	if (!state->signal_strength_en)
		return 0;

	ret = regmap_bulk_read(state->regmap, 0xd07c, buf, 2);
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

	return 0;
err:
	dev_dbg(&client->dev, "failed %d\n", ret);
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
		/* fall-through */
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
	struct i2c_client *client = state->client;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	int ret, i, sampling_freq;
	bool auto_mode, spec_inv;
	u8 buf[6];
	u32 if_frequency, freq_cw;

	dev_dbg(&client->dev, "frequency %u, bandwidth_hz %u\n",
		c->frequency, c->bandwidth_hz);

	/* program tuner */
	if (fe->ops.tuner_ops.set_params) {
		ret = fe->ops.tuner_ops.set_params(fe);
		if (ret)
			goto err;
	}

	/* program CFOE coefficients */
	if (c->bandwidth_hz != state->bandwidth_hz) {
		for (i = 0; i < ARRAY_SIZE(coeff_lut); i++) {
			if (coeff_lut[i].clock == state->clk &&
				coeff_lut[i].bandwidth_hz == c->bandwidth_hz) {
				break;
			}
		}

		/* Return an error if can't find bandwidth or the right clock */
		if (i == ARRAY_SIZE(coeff_lut)) {
			ret = -EINVAL;
			goto err;
		}

		ret = regmap_bulk_write(state->regmap, 0xae00, coeff_lut[i].val,
					sizeof(coeff_lut[i].val));
		if (ret)
			goto err;
	}

	/* program frequency control */
	if (c->bandwidth_hz != state->bandwidth_hz || state->first_tune) {
		/* get used IF frequency */
		if (fe->ops.tuner_ops.get_if_frequency) {
			ret = fe->ops.tuner_ops.get_if_frequency(fe,
								 &if_frequency);
			if (ret)
				goto err;
		} else {
			if_frequency = state->if_frequency;
		}

		dev_dbg(&client->dev, "if_frequency %u\n", if_frequency);

		sampling_freq = if_frequency;

		while (sampling_freq > (state->clk / 2))
			sampling_freq -= state->clk;

		if (sampling_freq < 0) {
			sampling_freq *= -1;
			spec_inv = state->spec_inv;
		} else {
			spec_inv = !state->spec_inv;
		}

		freq_cw = DIV_ROUND_CLOSEST_ULL((u64)sampling_freq * 0x800000,
						state->clk);

		if (spec_inv)
			freq_cw = 0x800000 - freq_cw;

		buf[0] = (freq_cw >>  0) & 0xff;
		buf[1] = (freq_cw >>  8) & 0xff;
		buf[2] = (freq_cw >> 16) & 0x7f;

		freq_cw = 0x800000 - freq_cw;

		buf[3] = (freq_cw >>  0) & 0xff;
		buf[4] = (freq_cw >>  8) & 0xff;
		buf[5] = (freq_cw >> 16) & 0x7f;

		ret = regmap_bulk_write(state->regmap, 0xd140, buf, 3);
		if (ret)
			goto err;

		ret = regmap_bulk_write(state->regmap, 0x9be7, buf, 6);
		if (ret)
			goto err;
	}

	/* clear TPS lock flag */
	ret = regmap_update_bits(state->regmap, 0xd330, 0x08, 0x08);
	if (ret)
		goto err;

	/* clear MPEG2 lock flag */
	ret = regmap_update_bits(state->regmap, 0xd507, 0x40, 0x00);
	if (ret)
		goto err;

	/* empty channel function */
	ret = regmap_update_bits(state->regmap, 0x9bfe, 0x01, 0x00);
	if (ret)
		goto err;

	/* empty DVB-T channel function */
	ret = regmap_update_bits(state->regmap, 0x9bc2, 0x01, 0x00);
	if (ret)
		goto err;

	/* transmission parameters */
	auto_mode = false;
	memset(buf, 0, 3);

	switch (c->transmission_mode) {
	case TRANSMISSION_MODE_AUTO:
		auto_mode = true;
		break;
	case TRANSMISSION_MODE_2K:
		break;
	case TRANSMISSION_MODE_8K:
		buf[0] |= (1 << 0);
		break;
	default:
		dev_dbg(&client->dev, "invalid transmission_mode\n");
		auto_mode = true;
	}

	switch (c->guard_interval) {
	case GUARD_INTERVAL_AUTO:
		auto_mode = true;
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
		dev_dbg(&client->dev, "invalid guard_interval\n");
		auto_mode = true;
	}

	switch (c->hierarchy) {
	case HIERARCHY_AUTO:
		auto_mode = true;
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
		dev_dbg(&client->dev, "invalid hierarchy\n");
		auto_mode = true;
	}

	switch (c->modulation) {
	case QAM_AUTO:
		auto_mode = true;
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
		dev_dbg(&client->dev, "invalid modulation\n");
		auto_mode = true;
	}

	/* Use HP. How and which case we can switch to LP? */
	buf[1] |= (1 << 4);

	switch (c->code_rate_HP) {
	case FEC_AUTO:
		auto_mode = true;
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
		dev_dbg(&client->dev, "invalid code_rate_HP\n");
		auto_mode = true;
	}

	switch (c->code_rate_LP) {
	case FEC_AUTO:
		auto_mode = true;
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
		dev_dbg(&client->dev, "invalid code_rate_LP\n");
		auto_mode = true;
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
		dev_dbg(&client->dev, "invalid bandwidth_hz\n");
		ret = -EINVAL;
		goto err;
	}

	ret = regmap_bulk_write(state->regmap, 0xd3c0, buf, 3);
	if (ret)
		goto err;

	if (auto_mode) {
		/* clear easy mode flag */
		ret = regmap_write(state->regmap, 0xaefd, 0x00);
		if (ret)
			goto err;

		dev_dbg(&client->dev, "auto params\n");
	} else {
		/* set easy mode flag */
		ret = regmap_write(state->regmap, 0xaefd, 0x01);
		if (ret)
			goto err;

		ret = regmap_write(state->regmap, 0xaefe, 0x00);
		if (ret)
			goto err;

		dev_dbg(&client->dev, "manual params\n");
	}

	/* Reset FSM */
	ret = regmap_write(state->regmap, 0xffff, 0x00);
	if (ret)
		goto err;

	state->bandwidth_hz = c->bandwidth_hz;
	state->set_frontend_jiffies = jiffies;
	state->first_tune = false;

	return 0;
err:
	dev_dbg(&client->dev, "failed %d\n", ret);
	return ret;
}

static int af9013_get_frontend(struct dvb_frontend *fe,
			       struct dtv_frontend_properties *c)
{
	struct af9013_state *state = fe->demodulator_priv;
	struct i2c_client *client = state->client;
	int ret;
	u8 buf[3];

	dev_dbg(&client->dev, "\n");

	ret = regmap_bulk_read(state->regmap, 0xd3c0, buf, 3);
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

	return 0;
err:
	dev_dbg(&client->dev, "failed %d\n", ret);
	return ret;
}

static int af9013_read_status(struct dvb_frontend *fe, enum fe_status *status)
{
	struct af9013_state *state = fe->demodulator_priv;
	struct i2c_client *client = state->client;
	int ret;
	unsigned int utmp;

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
	ret = regmap_read(state->regmap, 0xd507, &utmp);
	if (ret)
		goto err;

	if ((utmp >> 6) & 0x01)
		*status |= FE_HAS_SIGNAL | FE_HAS_CARRIER | FE_HAS_VITERBI |
			FE_HAS_SYNC | FE_HAS_LOCK;

	if (!*status) {
		/* TPS lock */
		ret = regmap_read(state->regmap, 0xd330, &utmp);
		if (ret)
			goto err;

		if ((utmp >> 3) & 0x01)
			*status |= FE_HAS_SIGNAL | FE_HAS_CARRIER |
				FE_HAS_VITERBI;
	}

	state->fe_status = *status;
	state->read_status_jiffies = jiffies;

	return 0;
err:
	dev_dbg(&client->dev, "failed %d\n", ret);
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
	struct i2c_client *client = state->client;
	int ret, i, len;
	unsigned int utmp;
	u8 buf[3];
	const struct af9013_reg_bit *init;

	dev_dbg(&client->dev, "\n");

	/* ADC on */
	ret = regmap_update_bits(state->regmap, 0xd73a, 0x08, 0x00);
	if (ret)
		goto err;

	/* Clear reset */
	ret = regmap_update_bits(state->regmap, 0xd417, 0x02, 0x00);
	if (ret)
		goto err;

	/* Disable reset */
	ret = regmap_update_bits(state->regmap, 0xd417, 0x10, 0x00);
	if (ret)
		goto err;

	/* write API version to firmware */
	ret = regmap_bulk_write(state->regmap, 0x9bf2, state->api_version, 4);
	if (ret)
		goto err;

	/* program ADC control */
	switch (state->clk) {
	case 28800000: /* 28.800 MHz */
		utmp = 0;
		break;
	case 20480000: /* 20.480 MHz */
		utmp = 1;
		break;
	case 28000000: /* 28.000 MHz */
		utmp = 2;
		break;
	case 25000000: /* 25.000 MHz */
		utmp = 3;
		break;
	default:
		ret = -EINVAL;
		goto err;
	}

	ret = regmap_update_bits(state->regmap, 0x9bd2, 0x0f, utmp);
	if (ret)
		goto err;

	utmp = div_u64((u64)state->clk * 0x80000, 1000000);
	buf[0] = (utmp >>  0) & 0xff;
	buf[1] = (utmp >>  8) & 0xff;
	buf[2] = (utmp >> 16) & 0xff;
	ret = regmap_bulk_write(state->regmap, 0xd180, buf, 3);
	if (ret)
		goto err;

	/* load OFSM settings */
	dev_dbg(&client->dev, "load ofsm settings\n");
	len = ARRAY_SIZE(ofsm_init);
	init = ofsm_init;
	for (i = 0; i < len; i++) {
		u16 reg = init[i].addr;
		u8 mask = GENMASK(init[i].pos + init[i].len - 1, init[i].pos);
		u8 val = init[i].val << init[i].pos;

		ret = regmap_update_bits(state->regmap, reg, mask, val);
		if (ret)
			goto err;
	}

	/* load tuner specific settings */
	dev_dbg(&client->dev, "load tuner specific settings\n");
	switch (state->tuner) {
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
		u16 reg = init[i].addr;
		u8 mask = GENMASK(init[i].pos + init[i].len - 1, init[i].pos);
		u8 val = init[i].val << init[i].pos;

		ret = regmap_update_bits(state->regmap, reg, mask, val);
		if (ret)
			goto err;
	}

	/* TS interface */
	if (state->ts_output_pin == 7)
		utmp = 1 << 3 | state->ts_mode << 1;
	else
		utmp = 0 << 3 | state->ts_mode << 1;
	ret = regmap_update_bits(state->regmap, 0xd500, 0x0e, utmp);
	if (ret)
		goto err;

	/* enable lock led */
	ret = regmap_update_bits(state->regmap, 0xd730, 0x01, 0x01);
	if (ret)
		goto err;

	/* check if we support signal strength */
	if (!state->signal_strength_en) {
		ret = regmap_read(state->regmap, 0x9bee, &utmp);
		if (ret)
			goto err;

		state->signal_strength_en = (utmp >> 0) & 0x01;
	}

	/* read values needed for signal strength calculation */
	if (state->signal_strength_en && !state->rf_50) {
		ret = regmap_bulk_read(state->regmap, 0x9bbd, &state->rf_50, 1);
		if (ret)
			goto err;
		ret = regmap_bulk_read(state->regmap, 0x9bd0, &state->rf_80, 1);
		if (ret)
			goto err;
		ret = regmap_bulk_read(state->regmap, 0x9be2, &state->if_50, 1);
		if (ret)
			goto err;
		ret = regmap_bulk_read(state->regmap, 0x9be4, &state->if_80, 1);
		if (ret)
			goto err;
	}

	/* SNR */
	ret = regmap_write(state->regmap, 0xd2e2, 0x01);
	if (ret)
		goto err;

	/* BER / UCB */
	buf[0] = (10000 >> 0) & 0xff;
	buf[1] = (10000 >> 8) & 0xff;
	ret = regmap_bulk_write(state->regmap, 0xd385, buf, 2);
	if (ret)
		goto err;

	/* enable FEC monitor */
	ret = regmap_update_bits(state->regmap, 0xd392, 0x02, 0x02);
	if (ret)
		goto err;

	state->first_tune = true;
	schedule_delayed_work(&state->statistics_work, msecs_to_jiffies(400));

	return 0;
err:
	dev_dbg(&client->dev, "failed %d\n", ret);
	return ret;
}

static int af9013_sleep(struct dvb_frontend *fe)
{
	struct af9013_state *state = fe->demodulator_priv;
	struct i2c_client *client = state->client;
	int ret;
	unsigned int utmp;

	dev_dbg(&client->dev, "\n");

	/* stop statistics polling */
	cancel_delayed_work_sync(&state->statistics_work);

	/* disable lock led */
	ret = regmap_update_bits(state->regmap, 0xd730, 0x01, 0x00);
	if (ret)
		goto err;

	/* Enable reset */
	ret = regmap_update_bits(state->regmap, 0xd417, 0x10, 0x10);
	if (ret)
		goto err;

	/* Start reset execution */
	ret = regmap_write(state->regmap, 0xaeff, 0x01);
	if (ret)
		goto err;

	/* Wait reset performs */
	ret = regmap_read_poll_timeout(state->regmap, 0xd417, utmp,
				       (utmp >> 1) & 0x01, 5000, 1000000);
	if (ret)
		goto err;

	if (!((utmp >> 1) & 0x01)) {
		ret = -ETIMEDOUT;
		goto err;
	}

	/* ADC off */
	ret = regmap_update_bits(state->regmap, 0xd73a, 0x08, 0x08);
	if (ret)
		goto err;

	return 0;
err:
	dev_dbg(&client->dev, "failed %d\n", ret);
	return ret;
}

static int af9013_i2c_gate_ctrl(struct dvb_frontend *fe, int enable)
{
	int ret;
	struct af9013_state *state = fe->demodulator_priv;
	struct i2c_client *client = state->client;

	dev_dbg(&client->dev, "enable %d\n", enable);

	/* gate already open or close */
	if (state->i2c_gate_state == enable)
		return 0;

	if (state->ts_mode == AF9013_TS_MODE_USB)
		ret = regmap_update_bits(state->regmap, 0xd417, 0x08,
					 enable << 3);
	else
		ret = regmap_update_bits(state->regmap, 0xd607, 0x04,
					 enable << 2);
	if (ret)
		goto err;

	state->i2c_gate_state = enable;

	return 0;
err:
	dev_dbg(&client->dev, "failed %d\n", ret);
	return ret;
}

static void af9013_release(struct dvb_frontend *fe)
{
	struct af9013_state *state = fe->demodulator_priv;
	struct i2c_client *client = state->client;

	dev_dbg(&client->dev, "\n");

	i2c_unregister_device(client);
}

static const struct dvb_frontend_ops af9013_ops;

static int af9013_download_firmware(struct af9013_state *state)
{
	struct i2c_client *client = state->client;
	int ret, i, len, rem;
	unsigned int utmp;
	u8 buf[4];
	u16 checksum = 0;
	const struct firmware *firmware;
	const char *name = AF9013_FIRMWARE;

	dev_dbg(&client->dev, "\n");

	/* Check whether firmware is already running */
	ret = regmap_read(state->regmap, 0x98be, &utmp);
	if (ret)
		goto err;

	dev_dbg(&client->dev, "firmware status %02x\n", utmp);

	if (utmp == 0x0c)
		return 0;

	dev_info(&client->dev, "found a '%s' in cold state, will try to load a firmware\n",
		 af9013_ops.info.name);

	/* Request the firmware, will block and timeout */
	ret = request_firmware(&firmware, name, &client->dev);
	if (ret) {
		dev_info(&client->dev, "firmware file '%s' not found %d\n",
			 name, ret);
		goto err;
	}

	dev_info(&client->dev, "downloading firmware from file '%s'\n",
		 name);

	/* Write firmware checksum & size */
	for (i = 0; i < firmware->size; i++)
		checksum += firmware->data[i];

	buf[0] = (checksum >> 8) & 0xff;
	buf[1] = (checksum >> 0) & 0xff;
	buf[2] = (firmware->size >> 8) & 0xff;
	buf[3] = (firmware->size >> 0) & 0xff;
	ret = regmap_bulk_write(state->regmap, 0x50fc, buf, 4);
	if (ret)
		goto err_release_firmware;

	/* Download firmware */
	#define LEN_MAX 16
	for (rem = firmware->size; rem > 0; rem -= LEN_MAX) {
		len = min(LEN_MAX, rem);
		ret = regmap_bulk_write(state->regmap,
					0x5100 + firmware->size - rem,
					&firmware->data[firmware->size - rem],
					len);
		if (ret) {
			dev_err(&client->dev, "firmware download failed %d\n",
				ret);
			goto err_release_firmware;
		}
	}

	release_firmware(firmware);

	/* Boot firmware */
	ret = regmap_write(state->regmap, 0xe205, 0x01);
	if (ret)
		goto err;

	/* Check firmware status. 0c=OK, 04=fail */
	ret = regmap_read_poll_timeout(state->regmap, 0x98be, utmp,
				       (utmp == 0x0c || utmp == 0x04),
				       5000, 1000000);
	if (ret)
		goto err;

	dev_dbg(&client->dev, "firmware status %02x\n", utmp);

	if (utmp == 0x04) {
		ret = -ENODEV;
		dev_err(&client->dev, "firmware did not run\n");
		goto err;
	} else if (utmp != 0x0c) {
		ret = -ENODEV;
		dev_err(&client->dev, "firmware boot timeout\n");
		goto err;
	}

	dev_info(&client->dev, "found a '%s' in warm state\n",
		 af9013_ops.info.name);

	return 0;
err_release_firmware:
	release_firmware(firmware);
err:
	dev_dbg(&client->dev, "failed %d\n", ret);
	return ret;
}

/*
 * XXX: That is wrapper to af9013_probe() via driver core in order to provide
 * proper I2C client for legacy media attach binding.
 * New users must use I2C client binding directly!
 */
struct dvb_frontend *af9013_attach(const struct af9013_config *config,
				   struct i2c_adapter *i2c)
{
	struct i2c_client *client;
	struct i2c_board_info board_info;
	struct af9013_platform_data pdata;

	pdata.clk = config->clock;
	pdata.tuner = config->tuner;
	pdata.if_frequency = config->if_frequency;
	pdata.ts_mode = config->ts_mode;
	pdata.ts_output_pin = 7;
	pdata.spec_inv = config->spec_inv;
	memcpy(&pdata.api_version, config->api_version, sizeof(pdata.api_version));
	memcpy(&pdata.gpio, config->gpio, sizeof(pdata.gpio));
	pdata.attach_in_use = true;

	memset(&board_info, 0, sizeof(board_info));
	strlcpy(board_info.type, "af9013", sizeof(board_info.type));
	board_info.addr = config->i2c_addr;
	board_info.platform_data = &pdata;
	client = i2c_new_device(i2c, &board_info);
	if (!client || !client->dev.driver)
		return NULL;

	return pdata.get_dvb_frontend(client);
}
EXPORT_SYMBOL(af9013_attach);

static const struct dvb_frontend_ops af9013_ops = {
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

static struct dvb_frontend *af9013_get_dvb_frontend(struct i2c_client *client)
{
	struct af9013_state *state = i2c_get_clientdata(client);

	dev_dbg(&client->dev, "\n");

	return &state->fe;
}

/* Own I2C access routines needed for regmap as chip uses extra command byte */
static int af9013_wregs(struct i2c_client *client, u8 cmd, u16 reg,
			const u8 *val, int len)
{
	int ret;
	u8 buf[21];
	struct i2c_msg msg[1] = {
		{
			.addr = client->addr,
			.flags = 0,
			.len = 3 + len,
			.buf = buf,
		}
	};

	if (3 + len > sizeof(buf)) {
		ret = -EINVAL;
		goto err;
	}

	buf[0] = (reg >> 8) & 0xff;
	buf[1] = (reg >> 0) & 0xff;
	buf[2] = cmd;
	memcpy(&buf[3], val, len);
	ret = i2c_transfer(client->adapter, msg, 1);
	if (ret < 0) {
		goto err;
	} else if (ret != 1) {
		ret = -EREMOTEIO;
		goto err;
	}

	return 0;
err:
	dev_dbg(&client->dev, "failed %d\n", ret);
	return ret;
}

static int af9013_rregs(struct i2c_client *client, u8 cmd, u16 reg,
			u8 *val, int len)
{
	int ret;
	u8 buf[3];
	struct i2c_msg msg[2] = {
		{
			.addr = client->addr,
			.flags = 0,
			.len = 3,
			.buf = buf,
		}, {
			.addr = client->addr,
			.flags = I2C_M_RD,
			.len = len,
			.buf = val,
		}
	};

	buf[0] = (reg >> 8) & 0xff;
	buf[1] = (reg >> 0) & 0xff;
	buf[2] = cmd;
	ret = i2c_transfer(client->adapter, msg, 2);
	if (ret < 0) {
		goto err;
	} else if (ret != 2) {
		ret = -EREMOTEIO;
		goto err;
	}

	return 0;
err:
	dev_dbg(&client->dev, "failed %d\n", ret);
	return ret;
}

static int af9013_regmap_write(void *context, const void *data, size_t count)
{
	struct i2c_client *client = context;
	struct af9013_state *state = i2c_get_clientdata(client);
	int ret, i;
	u8 cmd;
	u16 reg = ((u8 *)data)[0] << 8|((u8 *)data)[1] << 0;
	u8 *val = &((u8 *)data)[2];
	const unsigned int len = count - 2;

	if (state->ts_mode == AF9013_TS_MODE_USB && (reg & 0xff00) != 0xae00) {
		cmd = 0 << 7|0 << 6|(len - 1) << 2|1 << 1|1 << 0;
		ret = af9013_wregs(client, cmd, reg, val, len);
		if (ret)
			goto err;
	} else if (reg >= 0x5100 && reg < 0x8fff) {
		/* Firmware download */
		cmd = 1 << 7|1 << 6|(len - 1) << 2|1 << 1|1 << 0;
		ret = af9013_wregs(client, cmd, reg, val, len);
		if (ret)
			goto err;
	} else {
		cmd = 0 << 7|0 << 6|(1 - 1) << 2|1 << 1|1 << 0;
		for (i = 0; i < len; i++) {
			ret = af9013_wregs(client, cmd, reg + i, val + i, 1);
			if (ret)
				goto err;
		}
	}

	return 0;
err:
	dev_dbg(&client->dev, "failed %d\n", ret);
	return ret;
}

static int af9013_regmap_read(void *context, const void *reg_buf,
			      size_t reg_size, void *val_buf, size_t val_size)
{
	struct i2c_client *client = context;
	struct af9013_state *state = i2c_get_clientdata(client);
	int ret, i;
	u8 cmd;
	u16 reg = ((u8 *)reg_buf)[0] << 8|((u8 *)reg_buf)[1] << 0;
	u8 *val = &((u8 *)val_buf)[0];
	const unsigned int len = val_size;

	if (state->ts_mode == AF9013_TS_MODE_USB && (reg & 0xff00) != 0xae00) {
		cmd = 0 << 7|0 << 6|(len - 1) << 2|1 << 1|0 << 0;
		ret = af9013_rregs(client, cmd, reg, val_buf, len);
		if (ret)
			goto err;
	} else {
		cmd = 0 << 7|0 << 6|(1 - 1) << 2|1 << 1|0 << 0;
		for (i = 0; i < len; i++) {
			ret = af9013_rregs(client, cmd, reg + i, val + i, 1);
			if (ret)
				goto err;
		}
	}

	return 0;
err:
	dev_dbg(&client->dev, "failed %d\n", ret);
	return ret;
}

static int af9013_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct af9013_state *state;
	struct af9013_platform_data *pdata = client->dev.platform_data;
	struct dtv_frontend_properties *c;
	int ret, i;
	u8 firmware_version[4];
	static const struct regmap_bus regmap_bus = {
		.read = af9013_regmap_read,
		.write = af9013_regmap_write,
	};
	static const struct regmap_config regmap_config = {
		.reg_bits    =  16,
		.val_bits    =  8,
	};

	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (!state) {
		ret = -ENOMEM;
		goto err;
	}

	/* Setup the state */
	state->client = client;
	i2c_set_clientdata(client, state);
	state->clk = pdata->clk;
	state->tuner = pdata->tuner;
	state->if_frequency = pdata->if_frequency;
	state->ts_mode = pdata->ts_mode;
	state->ts_output_pin = pdata->ts_output_pin;
	state->spec_inv = pdata->spec_inv;
	memcpy(&state->api_version, pdata->api_version, sizeof(state->api_version));
	memcpy(&state->gpio, pdata->gpio, sizeof(state->gpio));
	INIT_DELAYED_WORK(&state->statistics_work, af9013_statistics_work);
	state->regmap = regmap_init(&client->dev, &regmap_bus, client,
				  &regmap_config);
	if (IS_ERR(state->regmap)) {
		ret = PTR_ERR(state->regmap);
		goto err_kfree;
	}

	/* Download firmware */
	if (state->ts_mode != AF9013_TS_MODE_USB) {
		ret = af9013_download_firmware(state);
		if (ret)
			goto err_regmap_exit;
	}

	/* Firmware version */
	ret = regmap_bulk_read(state->regmap, 0x5103, firmware_version,
			       sizeof(firmware_version));
	if (ret)
		goto err_regmap_exit;

	/* Set GPIOs */
	for (i = 0; i < sizeof(state->gpio); i++) {
		ret = af9013_set_gpio(state, i, state->gpio[i]);
		if (ret)
			goto err_regmap_exit;
	}

	/* Create dvb frontend */
	memcpy(&state->fe.ops, &af9013_ops, sizeof(state->fe.ops));
	if (!pdata->attach_in_use)
		state->fe.ops.release = NULL;
	state->fe.demodulator_priv = state;

	/* Setup callbacks */
	pdata->get_dvb_frontend = af9013_get_dvb_frontend;

	/* Init stats to indicate which stats are supported */
	c = &state->fe.dtv_property_cache;
	c->cnr.len = 1;

	dev_info(&client->dev, "Afatech AF9013 successfully attached\n");
	dev_info(&client->dev, "firmware version: %d.%d.%d.%d\n",
		 firmware_version[0], firmware_version[1],
		 firmware_version[2], firmware_version[3]);
	return 0;
err_regmap_exit:
	regmap_exit(state->regmap);
err_kfree:
	kfree(state);
err:
	dev_dbg(&client->dev, "failed %d\n", ret);
	return ret;
}

static int af9013_remove(struct i2c_client *client)
{
	struct af9013_state *state = i2c_get_clientdata(client);

	dev_dbg(&client->dev, "\n");

	/* Stop statistics polling */
	cancel_delayed_work_sync(&state->statistics_work);

	regmap_exit(state->regmap);

	kfree(state);

	return 0;
}

static const struct i2c_device_id af9013_id_table[] = {
	{"af9013", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, af9013_id_table);

static struct i2c_driver af9013_driver = {
	.driver = {
		.name	= "af9013",
		.suppress_bind_attrs = true,
	},
	.probe		= af9013_probe,
	.remove		= af9013_remove,
	.id_table	= af9013_id_table,
};

module_i2c_driver(af9013_driver);

MODULE_AUTHOR("Antti Palosaari <crope@iki.fi>");
MODULE_DESCRIPTION("Afatech AF9013 DVB-T demodulator driver");
MODULE_LICENSE("GPL");
MODULE_FIRMWARE(AF9013_FIRMWARE);
