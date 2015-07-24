/*
 * ISDB-S driver for VA1J5JF8007/VA1J5JF8011
 *
 * Copyright (C) 2009 HIRANO Takahito <hiranotaka@zng.info>
 *
 * based on pt1dvr - http://pt1dvr.sourceforge.jp/
 * 	by Tomoaki Ishikawa <tomy@users.sourceforge.jp>
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
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include "dvb_frontend.h"
#include "va1j5jf8007s.h"

enum va1j5jf8007s_tune_state {
	VA1J5JF8007S_IDLE,
	VA1J5JF8007S_SET_FREQUENCY_1,
	VA1J5JF8007S_SET_FREQUENCY_2,
	VA1J5JF8007S_SET_FREQUENCY_3,
	VA1J5JF8007S_CHECK_FREQUENCY,
	VA1J5JF8007S_SET_MODULATION,
	VA1J5JF8007S_CHECK_MODULATION,
	VA1J5JF8007S_SET_TS_ID,
	VA1J5JF8007S_CHECK_TS_ID,
	VA1J5JF8007S_TRACK,
};

struct va1j5jf8007s_state {
	const struct va1j5jf8007s_config *config;
	struct i2c_adapter *adap;
	struct dvb_frontend fe;
	enum va1j5jf8007s_tune_state tune_state;
};

static int va1j5jf8007s_read_snr(struct dvb_frontend *fe, u16 *snr)
{
	struct va1j5jf8007s_state *state;
	u8 addr;
	int i;
	u8 write_buf[1], read_buf[1];
	struct i2c_msg msgs[2];
	s32 word, x1, x2, x3, x4, x5, y;

	state = fe->demodulator_priv;
	addr = state->config->demod_address;

	word = 0;
	for (i = 0; i < 2; i++) {
		write_buf[0] = 0xbc + i;

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

	word -= 3000;
	if (word < 0)
		word = 0;

	x1 = int_sqrt(word << 16) * ((15625ll << 21) / 1000000);
	x2 = (s64)x1 * x1 >> 31;
	x3 = (s64)x2 * x1 >> 31;
	x4 = (s64)x2 * x2 >> 31;
	x5 = (s64)x4 * x1 >> 31;

	y = (58857ll << 23) / 1000;
	y -= (s64)x1 * ((89565ll << 24) / 1000) >> 30;
	y += (s64)x2 * ((88977ll << 24) / 1000) >> 28;
	y -= (s64)x3 * ((50259ll << 25) / 1000) >> 27;
	y += (s64)x4 * ((14341ll << 27) / 1000) >> 27;
	y -= (s64)x5 * ((16346ll << 30) / 10000) >> 28;

	*snr = y < 0 ? 0 : y >> 15;
	return 0;
}

static int va1j5jf8007s_get_frontend_algo(struct dvb_frontend *fe)
{
	return DVBFE_ALGO_HW;
}

static int
va1j5jf8007s_read_status(struct dvb_frontend *fe, enum fe_status *status)
{
	struct va1j5jf8007s_state *state;

	state = fe->demodulator_priv;

	switch (state->tune_state) {
	case VA1J5JF8007S_IDLE:
	case VA1J5JF8007S_SET_FREQUENCY_1:
	case VA1J5JF8007S_SET_FREQUENCY_2:
	case VA1J5JF8007S_SET_FREQUENCY_3:
	case VA1J5JF8007S_CHECK_FREQUENCY:
		*status = 0;
		return 0;


	case VA1J5JF8007S_SET_MODULATION:
	case VA1J5JF8007S_CHECK_MODULATION:
		*status |= FE_HAS_SIGNAL;
		return 0;

	case VA1J5JF8007S_SET_TS_ID:
	case VA1J5JF8007S_CHECK_TS_ID:
		*status |= FE_HAS_SIGNAL | FE_HAS_CARRIER;
		return 0;

	case VA1J5JF8007S_TRACK:
		*status |= FE_HAS_SIGNAL | FE_HAS_CARRIER | FE_HAS_LOCK;
		return 0;
	}

	BUG();
}

struct va1j5jf8007s_cb_map {
	u32 frequency;
	u8 cb;
};

static const struct va1j5jf8007s_cb_map va1j5jf8007s_cb_maps[] = {
	{  986000, 0xb2 },
	{ 1072000, 0xd2 },
	{ 1154000, 0xe2 },
	{ 1291000, 0x20 },
	{ 1447000, 0x40 },
	{ 1615000, 0x60 },
	{ 1791000, 0x80 },
	{ 1972000, 0xa0 },
};

static u8 va1j5jf8007s_lookup_cb(u32 frequency)
{
	int i;
	const struct va1j5jf8007s_cb_map *map;

	for (i = 0; i < ARRAY_SIZE(va1j5jf8007s_cb_maps); i++) {
		map = &va1j5jf8007s_cb_maps[i];
		if (frequency < map->frequency)
			return map->cb;
	}
	return 0xc0;
}

static int va1j5jf8007s_set_frequency_1(struct va1j5jf8007s_state *state)
{
	u32 frequency;
	u16 word;
	u8 buf[6];
	struct i2c_msg msg;

	frequency = state->fe.dtv_property_cache.frequency;

	word = (frequency + 500) / 1000;
	if (frequency < 1072000)
		word = (word << 1 & ~0x1f) | (word & 0x0f);

	buf[0] = 0xfe;
	buf[1] = 0xc0;
	buf[2] = 0x40 | word >> 8;
	buf[3] = word;
	buf[4] = 0xe0;
	buf[5] = va1j5jf8007s_lookup_cb(frequency);

	msg.addr = state->config->demod_address;
	msg.flags = 0;
	msg.len = sizeof(buf);
	msg.buf = buf;

	if (i2c_transfer(state->adap, &msg, 1) != 1)
		return -EREMOTEIO;

	return 0;
}

static int va1j5jf8007s_set_frequency_2(struct va1j5jf8007s_state *state)
{
	u8 buf[3];
	struct i2c_msg msg;

	buf[0] = 0xfe;
	buf[1] = 0xc0;
	buf[2] = 0xe4;

	msg.addr = state->config->demod_address;
	msg.flags = 0;
	msg.len = sizeof(buf);
	msg.buf = buf;

	if (i2c_transfer(state->adap, &msg, 1) != 1)
		return -EREMOTEIO;

	return 0;
}

static int va1j5jf8007s_set_frequency_3(struct va1j5jf8007s_state *state)
{
	u32 frequency;
	u8 buf[4];
	struct i2c_msg msg;

	frequency = state->fe.dtv_property_cache.frequency;

	buf[0] = 0xfe;
	buf[1] = 0xc0;
	buf[2] = 0xf4;
	buf[3] = va1j5jf8007s_lookup_cb(frequency) | 0x4;

	msg.addr = state->config->demod_address;
	msg.flags = 0;
	msg.len = sizeof(buf);
	msg.buf = buf;

	if (i2c_transfer(state->adap, &msg, 1) != 1)
		return -EREMOTEIO;

	return 0;
}

static int
va1j5jf8007s_check_frequency(struct va1j5jf8007s_state *state, int *lock)
{
	u8 addr;
	u8 write_buf[2], read_buf[1];
	struct i2c_msg msgs[2];

	addr = state->config->demod_address;

	write_buf[0] = 0xfe;
	write_buf[1] = 0xc1;

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

static int va1j5jf8007s_set_modulation(struct va1j5jf8007s_state *state)
{
	u8 buf[2];
	struct i2c_msg msg;

	buf[0] = 0x03;
	buf[1] = 0x01;

	msg.addr = state->config->demod_address;
	msg.flags = 0;
	msg.len = sizeof(buf);
	msg.buf = buf;

	if (i2c_transfer(state->adap, &msg, 1) != 1)
		return -EREMOTEIO;

	return 0;
}

static int
va1j5jf8007s_check_modulation(struct va1j5jf8007s_state *state, int *lock)
{
	u8 addr;
	u8 write_buf[1], read_buf[1];
	struct i2c_msg msgs[2];

	addr = state->config->demod_address;

	write_buf[0] = 0xc3;

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
	return 0;
}

static int
va1j5jf8007s_set_ts_id(struct va1j5jf8007s_state *state)
{
	u32 ts_id;
	u8 buf[3];
	struct i2c_msg msg;

	ts_id = state->fe.dtv_property_cache.stream_id;
	if (!ts_id || ts_id == NO_STREAM_ID_FILTER)
		return 0;

	buf[0] = 0x8f;
	buf[1] = ts_id >> 8;
	buf[2] = ts_id;

	msg.addr = state->config->demod_address;
	msg.flags = 0;
	msg.len = sizeof(buf);
	msg.buf = buf;

	if (i2c_transfer(state->adap, &msg, 1) != 1)
		return -EREMOTEIO;

	return 0;
}

static int
va1j5jf8007s_check_ts_id(struct va1j5jf8007s_state *state, int *lock)
{
	u8 addr;
	u8 write_buf[1], read_buf[2];
	struct i2c_msg msgs[2];
	u32 ts_id;

	ts_id = state->fe.dtv_property_cache.stream_id;
	if (!ts_id || ts_id == NO_STREAM_ID_FILTER) {
		*lock = 1;
		return 0;
	}

	addr = state->config->demod_address;

	write_buf[0] = 0xe6;

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

	*lock = (read_buf[0] << 8 | read_buf[1]) == ts_id;
	return 0;
}

static int
va1j5jf8007s_tune(struct dvb_frontend *fe,
		  bool re_tune,
		  unsigned int mode_flags,  unsigned int *delay,
		  enum fe_status *status)
{
	struct va1j5jf8007s_state *state;
	int ret;
	int lock = 0;

	state = fe->demodulator_priv;

	if (re_tune)
		state->tune_state = VA1J5JF8007S_SET_FREQUENCY_1;

	switch (state->tune_state) {
	case VA1J5JF8007S_IDLE:
		*delay = 3 * HZ;
		*status = 0;
		return 0;

	case VA1J5JF8007S_SET_FREQUENCY_1:
		ret = va1j5jf8007s_set_frequency_1(state);
		if (ret < 0)
			return ret;

		state->tune_state = VA1J5JF8007S_SET_FREQUENCY_2;
		*delay = 0;
		*status = 0;
		return 0;

	case VA1J5JF8007S_SET_FREQUENCY_2:
		ret = va1j5jf8007s_set_frequency_2(state);
		if (ret < 0)
			return ret;

		state->tune_state = VA1J5JF8007S_SET_FREQUENCY_3;
		*delay = (HZ + 99) / 100;
		*status = 0;
		return 0;

	case VA1J5JF8007S_SET_FREQUENCY_3:
		ret = va1j5jf8007s_set_frequency_3(state);
		if (ret < 0)
			return ret;

		state->tune_state = VA1J5JF8007S_CHECK_FREQUENCY;
		*delay = 0;
		*status = 0;
		return 0;

	case VA1J5JF8007S_CHECK_FREQUENCY:
		ret = va1j5jf8007s_check_frequency(state, &lock);
		if (ret < 0)
			return ret;

		if (!lock)  {
			*delay = (HZ + 999) / 1000;
			*status = 0;
			return 0;
		}

		state->tune_state = VA1J5JF8007S_SET_MODULATION;
		*delay = 0;
		*status = FE_HAS_SIGNAL;
		return 0;

	case VA1J5JF8007S_SET_MODULATION:
		ret = va1j5jf8007s_set_modulation(state);
		if (ret < 0)
			return ret;

		state->tune_state = VA1J5JF8007S_CHECK_MODULATION;
		*delay = 0;
		*status = FE_HAS_SIGNAL;
		return 0;

	case VA1J5JF8007S_CHECK_MODULATION:
		ret = va1j5jf8007s_check_modulation(state, &lock);
		if (ret < 0)
			return ret;

		if (!lock)  {
			*delay = (HZ + 49) / 50;
			*status = FE_HAS_SIGNAL;
			return 0;
		}

		state->tune_state = VA1J5JF8007S_SET_TS_ID;
		*delay = 0;
		*status = FE_HAS_SIGNAL | FE_HAS_CARRIER;
		return 0;

	case VA1J5JF8007S_SET_TS_ID:
		ret = va1j5jf8007s_set_ts_id(state);
		if (ret < 0)
			return ret;

		state->tune_state = VA1J5JF8007S_CHECK_TS_ID;
		return 0;

	case VA1J5JF8007S_CHECK_TS_ID:
		ret = va1j5jf8007s_check_ts_id(state, &lock);
		if (ret < 0)
			return ret;

		if (!lock)  {
			*delay = (HZ + 99) / 100;
			*status = FE_HAS_SIGNAL | FE_HAS_CARRIER;
			return 0;
		}

		state->tune_state = VA1J5JF8007S_TRACK;
		/* fall through */

	case VA1J5JF8007S_TRACK:
		*delay = 3 * HZ;
		*status = FE_HAS_SIGNAL | FE_HAS_CARRIER | FE_HAS_LOCK;
		return 0;
	}

	BUG();
}

static int va1j5jf8007s_init_frequency(struct va1j5jf8007s_state *state)
{
	u8 buf[4];
	struct i2c_msg msg;

	buf[0] = 0xfe;
	buf[1] = 0xc0;
	buf[2] = 0xf0;
	buf[3] = 0x04;

	msg.addr = state->config->demod_address;
	msg.flags = 0;
	msg.len = sizeof(buf);
	msg.buf = buf;

	if (i2c_transfer(state->adap, &msg, 1) != 1)
		return -EREMOTEIO;

	return 0;
}

static int va1j5jf8007s_set_sleep(struct va1j5jf8007s_state *state, int sleep)
{
	u8 buf[2];
	struct i2c_msg msg;

	buf[0] = 0x17;
	buf[1] = sleep ? 0x01 : 0x00;

	msg.addr = state->config->demod_address;
	msg.flags = 0;
	msg.len = sizeof(buf);
	msg.buf = buf;

	if (i2c_transfer(state->adap, &msg, 1) != 1)
		return -EREMOTEIO;

	return 0;
}

static int va1j5jf8007s_sleep(struct dvb_frontend *fe)
{
	struct va1j5jf8007s_state *state;
	int ret;

	state = fe->demodulator_priv;

	ret = va1j5jf8007s_init_frequency(state);
	if (ret < 0)
		return ret;

	return va1j5jf8007s_set_sleep(state, 1);
}

static int va1j5jf8007s_init(struct dvb_frontend *fe)
{
	struct va1j5jf8007s_state *state;

	state = fe->demodulator_priv;
	state->tune_state = VA1J5JF8007S_IDLE;

	return va1j5jf8007s_set_sleep(state, 0);
}

static void va1j5jf8007s_release(struct dvb_frontend *fe)
{
	struct va1j5jf8007s_state *state;
	state = fe->demodulator_priv;
	kfree(state);
}

static struct dvb_frontend_ops va1j5jf8007s_ops = {
	.delsys = { SYS_ISDBS },
	.info = {
		.name = "VA1J5JF8007/VA1J5JF8011 ISDB-S",
		.frequency_min = 950000,
		.frequency_max = 2150000,
		.frequency_stepsize = 1000,
		.caps = FE_CAN_INVERSION_AUTO | FE_CAN_FEC_AUTO |
			FE_CAN_QAM_AUTO | FE_CAN_TRANSMISSION_MODE_AUTO |
			FE_CAN_GUARD_INTERVAL_AUTO | FE_CAN_HIERARCHY_AUTO |
			FE_CAN_MULTISTREAM,
	},

	.read_snr = va1j5jf8007s_read_snr,
	.get_frontend_algo = va1j5jf8007s_get_frontend_algo,
	.read_status = va1j5jf8007s_read_status,
	.tune = va1j5jf8007s_tune,
	.sleep = va1j5jf8007s_sleep,
	.init = va1j5jf8007s_init,
	.release = va1j5jf8007s_release,
};

static int va1j5jf8007s_prepare_1(struct va1j5jf8007s_state *state)
{
	u8 addr;
	u8 write_buf[1], read_buf[1];
	struct i2c_msg msgs[2];

	addr = state->config->demod_address;

	write_buf[0] = 0x07;

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

	if (read_buf[0] != 0x41)
		return -EIO;

	return 0;
}

static const u8 va1j5jf8007s_20mhz_prepare_bufs[][2] = {
	{0x04, 0x02}, {0x0d, 0x55}, {0x11, 0x40}, {0x13, 0x80}, {0x17, 0x01},
	{0x1c, 0x0a}, {0x1d, 0xaa}, {0x1e, 0x20}, {0x1f, 0x88}, {0x51, 0xb0},
	{0x52, 0x89}, {0x53, 0xb3}, {0x5a, 0x2d}, {0x5b, 0xd3}, {0x85, 0x69},
	{0x87, 0x04}, {0x8e, 0x02}, {0xa3, 0xf7}, {0xa5, 0xc0},
};

static const u8 va1j5jf8007s_25mhz_prepare_bufs[][2] = {
	{0x04, 0x02}, {0x11, 0x40}, {0x13, 0x80}, {0x17, 0x01}, {0x1c, 0x0a},
	{0x1d, 0xaa}, {0x1e, 0x20}, {0x1f, 0x88}, {0x51, 0xb0}, {0x52, 0x89},
	{0x53, 0xb3}, {0x5a, 0x2d}, {0x5b, 0xd3}, {0x85, 0x69}, {0x87, 0x04},
	{0x8e, 0x26}, {0xa3, 0xf7}, {0xa5, 0xc0},
};

static int va1j5jf8007s_prepare_2(struct va1j5jf8007s_state *state)
{
	const u8 (*bufs)[2];
	int size;
	u8 addr;
	u8 buf[2];
	struct i2c_msg msg;
	int i;

	switch (state->config->frequency) {
	case VA1J5JF8007S_20MHZ:
		bufs = va1j5jf8007s_20mhz_prepare_bufs;
		size = ARRAY_SIZE(va1j5jf8007s_20mhz_prepare_bufs);
		break;
	case VA1J5JF8007S_25MHZ:
		bufs = va1j5jf8007s_25mhz_prepare_bufs;
		size = ARRAY_SIZE(va1j5jf8007s_25mhz_prepare_bufs);
		break;
	default:
		return -EINVAL;
	}

	addr = state->config->demod_address;

	msg.addr = addr;
	msg.flags = 0;
	msg.len = 2;
	msg.buf = buf;
	for (i = 0; i < size; i++) {
		memcpy(buf, bufs[i], sizeof(buf));
		if (i2c_transfer(state->adap, &msg, 1) != 1)
			return -EREMOTEIO;
	}

	return 0;
}

/* must be called after va1j5jf8007t_attach */
int va1j5jf8007s_prepare(struct dvb_frontend *fe)
{
	struct va1j5jf8007s_state *state;
	int ret;

	state = fe->demodulator_priv;

	ret = va1j5jf8007s_prepare_1(state);
	if (ret < 0)
		return ret;

	ret = va1j5jf8007s_prepare_2(state);
	if (ret < 0)
		return ret;

	return va1j5jf8007s_init_frequency(state);
}

struct dvb_frontend *
va1j5jf8007s_attach(const struct va1j5jf8007s_config *config,
		    struct i2c_adapter *adap)
{
	struct va1j5jf8007s_state *state;
	struct dvb_frontend *fe;
	u8 buf[2];
	struct i2c_msg msg;

	state = kzalloc(sizeof(struct va1j5jf8007s_state), GFP_KERNEL);
	if (!state)
		return NULL;

	state->config = config;
	state->adap = adap;

	fe = &state->fe;
	memcpy(&fe->ops, &va1j5jf8007s_ops, sizeof(struct dvb_frontend_ops));
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
