/*
 * ISDB-T driver for VA1J5JF8007/VA1J5JF8011
 *
 * Copyright (C) 2009 HIRANO Takahito <hiranotaka@zng.info>
 *
 * based on pt1dvr - http://pt1dvr.sourceforge.jp/
 *	by Tomoaki Ishikawa <tomy@users.sourceforge.jp>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <media/dvb_frontend.h>
#include <media/dvb_math.h>
#include "va1j5jf8007t.h"

enum va1j5jf8007t_tune_state {
	VA1J5JF8007T_IDLE,
	VA1J5JF8007T_SET_FREQUENCY,
	VA1J5JF8007T_CHECK_FREQUENCY,
	VA1J5JF8007T_SET_MODULATION,
	VA1J5JF8007T_CHECK_MODULATION,
	VA1J5JF8007T_TRACK,
	VA1J5JF8007T_ABORT,
};

struct va1j5jf8007t_state {
	const struct va1j5jf8007t_config *config;
	struct i2c_adapter *adap;
	struct dvb_frontend fe;
	enum va1j5jf8007t_tune_state tune_state;
};

static int va1j5jf8007t_read_snr(struct dvb_frontend *fe, u16 *snr)
{
	struct va1j5jf8007t_state *state;
	u8 addr;
	int i;
	u8 write_buf[1], read_buf[1];
	struct i2c_msg msgs[2];
	s32 word, x, y;

	state = fe->demodulator_priv;
	addr = state->config->demod_address;

	word = 0;
	for (i = 0; i < 3; i++) {
		write_buf[0] = 0x8b + i;

		msgs[0].addr = addr;
		msgs[0].flags = 0;
		msgs[0].len = sizeof(write_buf);
		msgs[0].buf = write_buf;

		msgs[1].addr = addr;
		msgs[1].flags = I2C_M_RD;
		msgs[1].len = sizeof(read_buf);
		msgs[1].buf = read_buf;

		if (i2c_transfer(state->adap, msgs, 2) != 2)
			return -EREMOTEIO;

		word <<= 8;
		word |= read_buf[0];
	}

	if (!word)
		return -EIO;

	x = 10 * (intlog10(0x540000 * 100 / word) - (2 << 24));
	y = (24ll << 46) / 1000000;
	y = ((s64)y * x >> 30) - (16ll << 40) / 10000;
	y = ((s64)y * x >> 29) + (398ll << 35) / 10000;
	y = ((s64)y * x >> 30) + (5491ll << 29) / 10000;
	y = ((s64)y * x >> 30) + (30965ll << 23) / 10000;
	*snr = y >> 15;
	return 0;
}

static int va1j5jf8007t_get_frontend_algo(struct dvb_frontend *fe)
{
	return DVBFE_ALGO_HW;
}

static int
va1j5jf8007t_read_status(struct dvb_frontend *fe, enum fe_status *status)
{
	struct va1j5jf8007t_state *state;

	state = fe->demodulator_priv;

	switch (state->tune_state) {
	case VA1J5JF8007T_IDLE:
	case VA1J5JF8007T_SET_FREQUENCY:
	case VA1J5JF8007T_CHECK_FREQUENCY:
		*status = 0;
		return 0;


	case VA1J5JF8007T_SET_MODULATION:
	case VA1J5JF8007T_CHECK_MODULATION:
	case VA1J5JF8007T_ABORT:
		*status |= FE_HAS_SIGNAL;
		return 0;

	case VA1J5JF8007T_TRACK:
		*status |= FE_HAS_SIGNAL | FE_HAS_CARRIER | FE_HAS_LOCK;
		return 0;
	}

	BUG();
}

struct va1j5jf8007t_cb_map {
	u32 frequency;
	u8 cb;
};

static const struct va1j5jf8007t_cb_map va1j5jf8007t_cb_maps[] = {
	{  90000000, 0x80 },
	{ 140000000, 0x81 },
	{ 170000000, 0xa1 },
	{ 220000000, 0x62 },
	{ 330000000, 0xa2 },
	{ 402000000, 0xe2 },
	{ 450000000, 0x64 },
	{ 550000000, 0x84 },
	{ 600000000, 0xa4 },
	{ 700000000, 0xc4 },
};

static u8 va1j5jf8007t_lookup_cb(u32 frequency)
{
	int i;
	const struct va1j5jf8007t_cb_map *map;

	for (i = 0; i < ARRAY_SIZE(va1j5jf8007t_cb_maps); i++) {
		map = &va1j5jf8007t_cb_maps[i];
		if (frequency < map->frequency)
			return map->cb;
	}
	return 0xe4;
}

static int va1j5jf8007t_set_frequency(struct va1j5jf8007t_state *state)
{
	u32 frequency;
	u16 word;
	u8 buf[6];
	struct i2c_msg msg;

	frequency = state->fe.dtv_property_cache.frequency;

	word = (frequency + 71428) / 142857 + 399;
	buf[0] = 0xfe;
	buf[1] = 0xc2;
	buf[2] = word >> 8;
	buf[3] = word;
	buf[4] = 0x80;
	buf[5] = va1j5jf8007t_lookup_cb(frequency);

	msg.addr = state->config->demod_address;
	msg.flags = 0;
	msg.len = sizeof(buf);
	msg.buf = buf;

	if (i2c_transfer(state->adap, &msg, 1) != 1)
		return -EREMOTEIO;

	return 0;
}

static int
va1j5jf8007t_check_frequency(struct va1j5jf8007t_state *state, int *lock)
{
	u8 addr;
	u8 write_buf[2], read_buf[1];
	struct i2c_msg msgs[2];

	addr = state->config->demod_address;

	write_buf[0] = 0xfe;
	write_buf[1] = 0xc3;

	msgs[0].addr = addr;
	msgs[0].flags = 0;
	msgs[0].len = sizeof(write_buf);
	msgs[0].buf = write_buf;

	msgs[1].addr = addr;
	msgs[1].flags = I2C_M_RD;
	msgs[1].len = sizeof(read_buf);
	msgs[1].buf = read_buf;

	if (i2c_transfer(state->adap, msgs, 2) != 2)
		return -EREMOTEIO;

	*lock = read_buf[0] & 0x40;
	return 0;
}

static int va1j5jf8007t_set_modulation(struct va1j5jf8007t_state *state)
{
	u8 buf[2];
	struct i2c_msg msg;

	buf[0] = 0x01;
	buf[1] = 0x40;

	msg.addr = state->config->demod_address;
	msg.flags = 0;
	msg.len = sizeof(buf);
	msg.buf = buf;

	if (i2c_transfer(state->adap, &msg, 1) != 1)
		return -EREMOTEIO;

	return 0;
}

static int va1j5jf8007t_check_modulation(struct va1j5jf8007t_state *state,
					 int *lock, int *retry)
{
	u8 addr;
	u8 write_buf[1], read_buf[1];
	struct i2c_msg msgs[2];

	addr = state->config->demod_address;

	write_buf[0] = 0x80;

	msgs[0].addr = addr;
	msgs[0].flags = 0;
	msgs[0].len = sizeof(write_buf);
	msgs[0].buf = write_buf;

	msgs[1].addr = addr;
	msgs[1].flags = I2C_M_RD;
	msgs[1].len = sizeof(read_buf);
	msgs[1].buf = read_buf;

	if (i2c_transfer(state->adap, msgs, 2) != 2)
		return -EREMOTEIO;

	*lock = !(read_buf[0] & 0x10);
	*retry = read_buf[0] & 0x80;
	return 0;
}

static int
va1j5jf8007t_tune(struct dvb_frontend *fe,
		  bool re_tune,
		  unsigned int mode_flags,  unsigned int *delay,
		  enum fe_status *status)
{
	struct va1j5jf8007t_state *state;
	int ret;
	int lock = 0, retry = 0;

	state = fe->demodulator_priv;

	if (re_tune)
		state->tune_state = VA1J5JF8007T_SET_FREQUENCY;

	switch (state->tune_state) {
	case VA1J5JF8007T_IDLE:
		*delay = 3 * HZ;
		*status = 0;
		return 0;

	case VA1J5JF8007T_SET_FREQUENCY:
		ret = va1j5jf8007t_set_frequency(state);
		if (ret < 0)
			return ret;

		state->tune_state = VA1J5JF8007T_CHECK_FREQUENCY;
		*delay = 0;
		*status = 0;
		return 0;

	case VA1J5JF8007T_CHECK_FREQUENCY:
		ret = va1j5jf8007t_check_frequency(state, &lock);
		if (ret < 0)
			return ret;

		if (!lock)  {
			*delay = (HZ + 999) / 1000;
			*status = 0;
			return 0;
		}

		state->tune_state = VA1J5JF8007T_SET_MODULATION;
		*delay = 0;
		*status = FE_HAS_SIGNAL;
		return 0;

	case VA1J5JF8007T_SET_MODULATION:
		ret = va1j5jf8007t_set_modulation(state);
		if (ret < 0)
			return ret;

		state->tune_state = VA1J5JF8007T_CHECK_MODULATION;
		*delay = 0;
		*status = FE_HAS_SIGNAL;
		return 0;

	case VA1J5JF8007T_CHECK_MODULATION:
		ret = va1j5jf8007t_check_modulation(state, &lock, &retry);
		if (ret < 0)
			return ret;

		if (!lock)  {
			if (!retry)  {
				state->tune_state = VA1J5JF8007T_ABORT;
				*delay = 3 * HZ;
				*status = FE_HAS_SIGNAL;
				return 0;
			}
			*delay = (HZ + 999) / 1000;
			*status = FE_HAS_SIGNAL;
			return 0;
		}

		state->tune_state = VA1J5JF8007T_TRACK;
		/* fall through */

	case VA1J5JF8007T_TRACK:
		*delay = 3 * HZ;
		*status = FE_HAS_SIGNAL | FE_HAS_CARRIER | FE_HAS_LOCK;
		return 0;

	case VA1J5JF8007T_ABORT:
		*delay = 3 * HZ;
		*status = FE_HAS_SIGNAL;
		return 0;
	}

	BUG();
}

static int va1j5jf8007t_init_frequency(struct va1j5jf8007t_state *state)
{
	u8 buf[7];
	struct i2c_msg msg;

	buf[0] = 0xfe;
	buf[1] = 0xc2;
	buf[2] = 0x01;
	buf[3] = 0x8f;
	buf[4] = 0xc1;
	buf[5] = 0x80;
	buf[6] = 0x80;

	msg.addr = state->config->demod_address;
	msg.flags = 0;
	msg.len = sizeof(buf);
	msg.buf = buf;

	if (i2c_transfer(state->adap, &msg, 1) != 1)
		return -EREMOTEIO;

	return 0;
}

static int va1j5jf8007t_set_sleep(struct va1j5jf8007t_state *state, int sleep)
{
	u8 buf[2];
	struct i2c_msg msg;

	buf[0] = 0x03;
	buf[1] = sleep ? 0x90 : 0x80;

	msg.addr = state->config->demod_address;
	msg.flags = 0;
	msg.len = sizeof(buf);
	msg.buf = buf;

	if (i2c_transfer(state->adap, &msg, 1) != 1)
		return -EREMOTEIO;

	return 0;
}

static int va1j5jf8007t_sleep(struct dvb_frontend *fe)
{
	struct va1j5jf8007t_state *state;
	int ret;

	state = fe->demodulator_priv;

	ret = va1j5jf8007t_init_frequency(state);
	if (ret < 0)
		return ret;

	return va1j5jf8007t_set_sleep(state, 1);
}

static int va1j5jf8007t_init(struct dvb_frontend *fe)
{
	struct va1j5jf8007t_state *state;

	state = fe->demodulator_priv;
	state->tune_state = VA1J5JF8007T_IDLE;

	return va1j5jf8007t_set_sleep(state, 0);
}

static void va1j5jf8007t_release(struct dvb_frontend *fe)
{
	struct va1j5jf8007t_state *state;
	state = fe->demodulator_priv;
	kfree(state);
}

static const struct dvb_frontend_ops va1j5jf8007t_ops = {
	.delsys = { SYS_ISDBT },
	.info = {
		.name = "VA1J5JF8007/VA1J5JF8011 ISDB-T",
		.frequency_min = 90000000,
		.frequency_max = 770000000,
		.frequency_stepsize = 142857,
		.caps = FE_CAN_INVERSION_AUTO | FE_CAN_FEC_AUTO |
			FE_CAN_QAM_AUTO | FE_CAN_TRANSMISSION_MODE_AUTO |
			FE_CAN_GUARD_INTERVAL_AUTO | FE_CAN_HIERARCHY_AUTO,
	},

	.read_snr = va1j5jf8007t_read_snr,
	.get_frontend_algo = va1j5jf8007t_get_frontend_algo,
	.read_status = va1j5jf8007t_read_status,
	.tune = va1j5jf8007t_tune,
	.sleep = va1j5jf8007t_sleep,
	.init = va1j5jf8007t_init,
	.release = va1j5jf8007t_release,
};

static const u8 va1j5jf8007t_20mhz_prepare_bufs[][2] = {
	{0x03, 0x90}, {0x14, 0x8f}, {0x1c, 0x2a}, {0x1d, 0xa8}, {0x1e, 0xa2},
	{0x22, 0x83}, {0x31, 0x0d}, {0x32, 0xe0}, {0x39, 0xd3}, {0x3a, 0x00},
	{0x5c, 0x40}, {0x5f, 0x80}, {0x75, 0x02}, {0x76, 0x4e}, {0x77, 0x03},
	{0xef, 0x01}
};

static const u8 va1j5jf8007t_25mhz_prepare_bufs[][2] = {
	{0x03, 0x90}, {0x1c, 0x2a}, {0x1d, 0xa8}, {0x1e, 0xa2}, {0x22, 0x83},
	{0x3a, 0x00}, {0x5c, 0x40}, {0x5f, 0x80}, {0x75, 0x0a}, {0x76, 0x4c},
	{0x77, 0x03}, {0xef, 0x01}
};

int va1j5jf8007t_prepare(struct dvb_frontend *fe)
{
	struct va1j5jf8007t_state *state;
	const u8 (*bufs)[2];
	int size;
	u8 buf[2];
	struct i2c_msg msg;
	int i;

	state = fe->demodulator_priv;

	switch (state->config->frequency) {
	case VA1J5JF8007T_20MHZ:
		bufs = va1j5jf8007t_20mhz_prepare_bufs;
		size = ARRAY_SIZE(va1j5jf8007t_20mhz_prepare_bufs);
		break;
	case VA1J5JF8007T_25MHZ:
		bufs = va1j5jf8007t_25mhz_prepare_bufs;
		size = ARRAY_SIZE(va1j5jf8007t_25mhz_prepare_bufs);
		break;
	default:
		return -EINVAL;
	}

	msg.addr = state->config->demod_address;
	msg.flags = 0;
	msg.len = sizeof(buf);
	msg.buf = buf;

	for (i = 0; i < size; i++) {
		memcpy(buf, bufs[i], sizeof(buf));
		if (i2c_transfer(state->adap, &msg, 1) != 1)
			return -EREMOTEIO;
	}

	return va1j5jf8007t_init_frequency(state);
}

struct dvb_frontend *
va1j5jf8007t_attach(const struct va1j5jf8007t_config *config,
		    struct i2c_adapter *adap)
{
	struct va1j5jf8007t_state *state;
	struct dvb_frontend *fe;
	u8 buf[2];
	struct i2c_msg msg;

	state = kzalloc(sizeof(struct va1j5jf8007t_state), GFP_KERNEL);
	if (!state)
		return NULL;

	state->config = config;
	state->adap = adap;

	fe = &state->fe;
	memcpy(&fe->ops, &va1j5jf8007t_ops, sizeof(struct dvb_frontend_ops));
	fe->demodulator_priv = state;

	buf[0] = 0x01;
	buf[1] = 0x80;

	msg.addr = state->config->demod_address;
	msg.flags = 0;
	msg.len = sizeof(buf);
	msg.buf = buf;

	if (i2c_transfer(state->adap, &msg, 1) != 1) {
		kfree(state);
		return NULL;
	}

	return fe;
}
