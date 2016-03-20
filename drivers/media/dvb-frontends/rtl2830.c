/*
 * Realtek RTL2830 DVB-T demodulator driver
 *
 * Copyright (C) 2011 Antti Palosaari <crope@iki.fi>
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

#include "rtl2830_priv.h"

/* Our regmap is bypassing I2C adapter lock, thus we do it! */
static int rtl2830_bulk_write(struct i2c_client *client, unsigned int reg,
			      const void *val, size_t val_count)
{
	struct rtl2830_dev *dev = i2c_get_clientdata(client);
	int ret;

	i2c_lock_adapter(client->adapter);
	ret = regmap_bulk_write(dev->regmap, reg, val, val_count);
	i2c_unlock_adapter(client->adapter);
	return ret;
}

static int rtl2830_update_bits(struct i2c_client *client, unsigned int reg,
			       unsigned int mask, unsigned int val)
{
	struct rtl2830_dev *dev = i2c_get_clientdata(client);
	int ret;

	i2c_lock_adapter(client->adapter);
	ret = regmap_update_bits(dev->regmap, reg, mask, val);
	i2c_unlock_adapter(client->adapter);
	return ret;
}

static int rtl2830_bulk_read(struct i2c_client *client, unsigned int reg,
			     void *val, size_t val_count)
{
	struct rtl2830_dev *dev = i2c_get_clientdata(client);
	int ret;

	i2c_lock_adapter(client->adapter);
	ret = regmap_bulk_read(dev->regmap, reg, val, val_count);
	i2c_unlock_adapter(client->adapter);
	return ret;
}

static int rtl2830_init(struct dvb_frontend *fe)
{
	struct i2c_client *client = fe->demodulator_priv;
	struct rtl2830_dev *dev = i2c_get_clientdata(client);
	struct dtv_frontend_properties *c = &dev->fe.dtv_property_cache;
	int ret, i;
	struct rtl2830_reg_val_mask tab[] = {
		{0x00d, 0x01, 0x03},
		{0x00d, 0x10, 0x10},
		{0x104, 0x00, 0x1e},
		{0x105, 0x80, 0x80},
		{0x110, 0x02, 0x03},
		{0x110, 0x08, 0x0c},
		{0x17b, 0x00, 0x40},
		{0x17d, 0x05, 0x0f},
		{0x17d, 0x50, 0xf0},
		{0x18c, 0x08, 0x0f},
		{0x18d, 0x00, 0xc0},
		{0x188, 0x05, 0x0f},
		{0x189, 0x00, 0xfc},
		{0x2d5, 0x02, 0x02},
		{0x2f1, 0x02, 0x06},
		{0x2f1, 0x20, 0xf8},
		{0x16d, 0x00, 0x01},
		{0x1a6, 0x00, 0x80},
		{0x106, dev->pdata->vtop, 0x3f},
		{0x107, dev->pdata->krf, 0x3f},
		{0x112, 0x28, 0xff},
		{0x103, dev->pdata->agc_targ_val, 0xff},
		{0x00a, 0x02, 0x07},
		{0x140, 0x0c, 0x3c},
		{0x140, 0x40, 0xc0},
		{0x15b, 0x05, 0x07},
		{0x15b, 0x28, 0x38},
		{0x15c, 0x05, 0x07},
		{0x15c, 0x28, 0x38},
		{0x115, dev->pdata->spec_inv, 0x01},
		{0x16f, 0x01, 0x07},
		{0x170, 0x18, 0x38},
		{0x172, 0x0f, 0x0f},
		{0x173, 0x08, 0x38},
		{0x175, 0x01, 0x07},
		{0x176, 0x00, 0xc0},
	};

	for (i = 0; i < ARRAY_SIZE(tab); i++) {
		ret = rtl2830_update_bits(client, tab[i].reg, tab[i].mask,
					  tab[i].val);
		if (ret)
			goto err;
	}

	ret = rtl2830_bulk_write(client, 0x18f, "\x28\x00", 2);
	if (ret)
		goto err;

	ret = rtl2830_bulk_write(client, 0x195,
				 "\x04\x06\x0a\x12\x0a\x12\x1e\x28", 8);
	if (ret)
		goto err;

	/* TODO: spec init */

	/* soft reset */
	ret = rtl2830_update_bits(client, 0x101, 0x04, 0x04);
	if (ret)
		goto err;

	ret = rtl2830_update_bits(client, 0x101, 0x04, 0x00);
	if (ret)
		goto err;

	/* init stats here in order signal app which stats are supported */
	c->strength.len = 1;
	c->strength.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	c->cnr.len = 1;
	c->cnr.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	c->post_bit_error.len = 1;
	c->post_bit_error.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	c->post_bit_count.len = 1;
	c->post_bit_count.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	/* start statistics polling */
	schedule_delayed_work(&dev->stat_work, msecs_to_jiffies(2000));

	dev->sleeping = false;

	return ret;
err:
	dev_dbg(&client->dev, "failed=%d\n", ret);
	return ret;
}

static int rtl2830_sleep(struct dvb_frontend *fe)
{
	struct i2c_client *client = fe->demodulator_priv;
	struct rtl2830_dev *dev = i2c_get_clientdata(client);

	dev->sleeping = true;
	/* stop statistics polling */
	cancel_delayed_work_sync(&dev->stat_work);
	dev->fe_status = 0;

	return 0;
}

static int rtl2830_get_tune_settings(struct dvb_frontend *fe,
				     struct dvb_frontend_tune_settings *s)
{
	s->min_delay_ms = 500;
	s->step_size = fe->ops.info.frequency_stepsize * 2;
	s->max_drift = (fe->ops.info.frequency_stepsize * 2) + 1;

	return 0;
}

static int rtl2830_set_frontend(struct dvb_frontend *fe)
{
	struct i2c_client *client = fe->demodulator_priv;
	struct rtl2830_dev *dev = i2c_get_clientdata(client);
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	int ret, i;
	u64 num;
	u8 buf[3], u8tmp;
	u32 if_ctl, if_frequency;
	static const u8 bw_params1[3][34] = {
		{
		0x1f, 0xf0, 0x1f, 0xf0, 0x1f, 0xfa, 0x00, 0x17, 0x00, 0x41,
		0x00, 0x64, 0x00, 0x67, 0x00, 0x38, 0x1f, 0xde, 0x1f, 0x7a,
		0x1f, 0x47, 0x1f, 0x7c, 0x00, 0x30, 0x01, 0x4b, 0x02, 0x82,
		0x03, 0x73, 0x03, 0xcf, /* 6 MHz */
		}, {
		0x1f, 0xfa, 0x1f, 0xda, 0x1f, 0xc1, 0x1f, 0xb3, 0x1f, 0xca,
		0x00, 0x07, 0x00, 0x4d, 0x00, 0x6d, 0x00, 0x40, 0x1f, 0xca,
		0x1f, 0x4d, 0x1f, 0x2a, 0x1f, 0xb2, 0x00, 0xec, 0x02, 0x7e,
		0x03, 0xd0, 0x04, 0x53, /* 7 MHz */
		}, {
		0x00, 0x10, 0x00, 0x0e, 0x1f, 0xf7, 0x1f, 0xc9, 0x1f, 0xa0,
		0x1f, 0xa6, 0x1f, 0xec, 0x00, 0x4e, 0x00, 0x7d, 0x00, 0x3a,
		0x1f, 0x98, 0x1f, 0x10, 0x1f, 0x40, 0x00, 0x75, 0x02, 0x5f,
		0x04, 0x24, 0x04, 0xdb, /* 8 MHz */
		},
	};
	static const u8 bw_params2[3][6] = {
		{0xc3, 0x0c, 0x44, 0x33, 0x33, 0x30}, /* 6 MHz */
		{0xb8, 0xe3, 0x93, 0x99, 0x99, 0x98}, /* 7 MHz */
		{0xae, 0xba, 0xf3, 0x26, 0x66, 0x64}, /* 8 MHz */
	};

	dev_dbg(&client->dev, "frequency=%u bandwidth_hz=%u inversion=%u\n",
		c->frequency, c->bandwidth_hz, c->inversion);

	/* program tuner */
	if (fe->ops.tuner_ops.set_params)
		fe->ops.tuner_ops.set_params(fe);

	switch (c->bandwidth_hz) {
	case 6000000:
		i = 0;
		break;
	case 7000000:
		i = 1;
		break;
	case 8000000:
		i = 2;
		break;
	default:
		dev_err(&client->dev, "invalid bandwidth_hz %u\n",
			c->bandwidth_hz);
		return -EINVAL;
	}

	ret = rtl2830_update_bits(client, 0x008, 0x06, i << 1);
	if (ret)
		goto err;

	/* program if frequency */
	if (fe->ops.tuner_ops.get_if_frequency)
		ret = fe->ops.tuner_ops.get_if_frequency(fe, &if_frequency);
	else
		ret = -EINVAL;
	if (ret)
		goto err;

	num = if_frequency % dev->pdata->clk;
	num *= 0x400000;
	num = div_u64(num, dev->pdata->clk);
	num = -num;
	if_ctl = num & 0x3fffff;
	dev_dbg(&client->dev, "if_frequency=%d if_ctl=%08x\n",
		if_frequency, if_ctl);

	buf[0] = (if_ctl >> 16) & 0x3f;
	buf[1] = (if_ctl >>  8) & 0xff;
	buf[2] = (if_ctl >>  0) & 0xff;

	ret = rtl2830_bulk_read(client, 0x119, &u8tmp, 1);
	if (ret)
		goto err;

	buf[0] |= u8tmp & 0xc0;  /* [7:6] */

	ret = rtl2830_bulk_write(client, 0x119, buf, 3);
	if (ret)
		goto err;

	/* 1/2 split I2C write */
	ret = rtl2830_bulk_write(client, 0x11c, &bw_params1[i][0], 17);
	if (ret)
		goto err;

	/* 2/2 split I2C write */
	ret = rtl2830_bulk_write(client, 0x12d, &bw_params1[i][17], 17);
	if (ret)
		goto err;

	ret = rtl2830_bulk_write(client, 0x19d, bw_params2[i], 6);
	if (ret)
		goto err;

	return ret;
err:
	dev_dbg(&client->dev, "failed=%d\n", ret);
	return ret;
}

static int rtl2830_get_frontend(struct dvb_frontend *fe,
				struct dtv_frontend_properties *c)
{
	struct i2c_client *client = fe->demodulator_priv;
	struct rtl2830_dev *dev = i2c_get_clientdata(client);
	int ret;
	u8 buf[3];

	if (dev->sleeping)
		return 0;

	ret = rtl2830_bulk_read(client, 0x33c, buf, 2);
	if (ret)
		goto err;

	ret = rtl2830_bulk_read(client, 0x351, &buf[2], 1);
	if (ret)
		goto err;

	dev_dbg(&client->dev, "TPS=%*ph\n", 3, buf);

	switch ((buf[0] >> 2) & 3) {
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

	switch ((buf[2] >> 2) & 1) {
	case 0:
		c->transmission_mode = TRANSMISSION_MODE_2K;
		break;
	case 1:
		c->transmission_mode = TRANSMISSION_MODE_8K;
	}

	switch ((buf[2] >> 0) & 3) {
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

	switch ((buf[1] >> 3) & 7) {
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

	switch ((buf[1] >> 0) & 7) {
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

	return 0;
err:
	dev_dbg(&client->dev, "failed=%d\n", ret);
	return ret;
}

static int rtl2830_read_status(struct dvb_frontend *fe, enum fe_status *status)
{
	struct i2c_client *client = fe->demodulator_priv;
	struct rtl2830_dev *dev = i2c_get_clientdata(client);
	int ret;
	u8 u8tmp;

	*status = 0;

	if (dev->sleeping)
		return 0;

	ret = rtl2830_bulk_read(client, 0x351, &u8tmp, 1);
	if (ret)
		goto err;

	u8tmp = (u8tmp >> 3) & 0x0f; /* [6:3] */
	if (u8tmp == 11) {
		*status |= FE_HAS_SIGNAL | FE_HAS_CARRIER |
			FE_HAS_VITERBI | FE_HAS_SYNC | FE_HAS_LOCK;
	} else if (u8tmp == 10) {
		*status |= FE_HAS_SIGNAL | FE_HAS_CARRIER |
			FE_HAS_VITERBI;
	}

	dev->fe_status = *status;

	return ret;
err:
	dev_dbg(&client->dev, "failed=%d\n", ret);
	return ret;
}

static int rtl2830_read_snr(struct dvb_frontend *fe, u16 *snr)
{
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;

	if (c->cnr.stat[0].scale == FE_SCALE_DECIBEL)
		*snr = div_s64(c->cnr.stat[0].svalue, 100);
	else
		*snr = 0;

	return 0;
}

static int rtl2830_read_ber(struct dvb_frontend *fe, u32 *ber)
{
	struct i2c_client *client = fe->demodulator_priv;
	struct rtl2830_dev *dev = i2c_get_clientdata(client);

	*ber = (dev->post_bit_error - dev->post_bit_error_prev);
	dev->post_bit_error_prev = dev->post_bit_error;

	return 0;
}

static int rtl2830_read_ucblocks(struct dvb_frontend *fe, u32 *ucblocks)
{
	*ucblocks = 0;

	return 0;
}

static int rtl2830_read_signal_strength(struct dvb_frontend *fe, u16 *strength)
{
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;

	if (c->strength.stat[0].scale == FE_SCALE_RELATIVE)
		*strength = c->strength.stat[0].uvalue;
	else
		*strength = 0;

	return 0;
}

static struct dvb_frontend_ops rtl2830_ops = {
	.delsys = {SYS_DVBT},
	.info = {
		.name = "Realtek RTL2830 (DVB-T)",
		.caps = FE_CAN_FEC_1_2 |
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

	.init = rtl2830_init,
	.sleep = rtl2830_sleep,

	.get_tune_settings = rtl2830_get_tune_settings,

	.set_frontend = rtl2830_set_frontend,
	.get_frontend = rtl2830_get_frontend,

	.read_status = rtl2830_read_status,
	.read_snr = rtl2830_read_snr,
	.read_ber = rtl2830_read_ber,
	.read_ucblocks = rtl2830_read_ucblocks,
	.read_signal_strength = rtl2830_read_signal_strength,
};

static void rtl2830_stat_work(struct work_struct *work)
{
	struct rtl2830_dev *dev = container_of(work, struct rtl2830_dev, stat_work.work);
	struct i2c_client *client = dev->client;
	struct dtv_frontend_properties *c = &dev->fe.dtv_property_cache;
	int ret, tmp;
	u8 u8tmp, buf[2];
	u16 u16tmp;

	dev_dbg(&client->dev, "\n");

	/* signal strength */
	if (dev->fe_status & FE_HAS_SIGNAL) {
		struct {signed int x:14; } s;

		/* read IF AGC */
		ret = rtl2830_bulk_read(client, 0x359, buf, 2);
		if (ret)
			goto err;

		u16tmp = buf[0] << 8 | buf[1] << 0;
		u16tmp &= 0x3fff; /* [13:0] */
		tmp = s.x = u16tmp; /* 14-bit bin to 2 complement */
		u16tmp = clamp_val(-4 * tmp + 32767, 0x0000, 0xffff);

		dev_dbg(&client->dev, "IF AGC=%d\n", tmp);

		c->strength.stat[0].scale = FE_SCALE_RELATIVE;
		c->strength.stat[0].uvalue = u16tmp;
	} else {
		c->strength.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	}

	/* CNR */
	if (dev->fe_status & FE_HAS_VITERBI) {
		unsigned hierarchy, constellation;
		#define CONSTELLATION_NUM 3
		#define HIERARCHY_NUM 4
		static const u32 constant[CONSTELLATION_NUM][HIERARCHY_NUM] = {
			{70705899, 70705899, 70705899, 70705899},
			{82433173, 82433173, 87483115, 94445660},
			{92888734, 92888734, 95487525, 99770748},
		};

		ret = rtl2830_bulk_read(client, 0x33c, &u8tmp, 1);
		if (ret)
			goto err;

		constellation = (u8tmp >> 2) & 0x03; /* [3:2] */
		if (constellation > CONSTELLATION_NUM - 1)
			goto err_schedule_delayed_work;

		hierarchy = (u8tmp >> 4) & 0x07; /* [6:4] */
		if (hierarchy > HIERARCHY_NUM - 1)
			goto err_schedule_delayed_work;

		ret = rtl2830_bulk_read(client, 0x40c, buf, 2);
		if (ret)
			goto err;

		u16tmp = buf[0] << 8 | buf[1] << 0;
		if (u16tmp)
			tmp = (constant[constellation][hierarchy] -
			       intlog10(u16tmp)) / ((1 << 24) / 10000);
		else
			tmp = 0;

		dev_dbg(&client->dev, "CNR raw=%u\n", u16tmp);

		c->cnr.stat[0].scale = FE_SCALE_DECIBEL;
		c->cnr.stat[0].svalue = tmp;
	} else {
		c->cnr.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	}

	/* BER */
	if (dev->fe_status & FE_HAS_LOCK) {
		ret = rtl2830_bulk_read(client, 0x34e, buf, 2);
		if (ret)
			goto err;

		u16tmp = buf[0] << 8 | buf[1] << 0;
		dev->post_bit_error += u16tmp;
		dev->post_bit_count += 1000000;

		dev_dbg(&client->dev, "BER errors=%u total=1000000\n", u16tmp);

		c->post_bit_error.stat[0].scale = FE_SCALE_COUNTER;
		c->post_bit_error.stat[0].uvalue = dev->post_bit_error;
		c->post_bit_count.stat[0].scale = FE_SCALE_COUNTER;
		c->post_bit_count.stat[0].uvalue = dev->post_bit_count;
	} else {
		c->post_bit_error.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
		c->post_bit_count.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	}

err_schedule_delayed_work:
	schedule_delayed_work(&dev->stat_work, msecs_to_jiffies(2000));
	return;
err:
	dev_dbg(&client->dev, "failed=%d\n", ret);
}

static int rtl2830_pid_filter_ctrl(struct dvb_frontend *fe, int onoff)
{
	struct i2c_client *client = fe->demodulator_priv;
	int ret;
	u8 u8tmp;

	dev_dbg(&client->dev, "onoff=%d\n", onoff);

	/* enable / disable PID filter */
	if (onoff)
		u8tmp = 0x80;
	else
		u8tmp = 0x00;

	ret = rtl2830_update_bits(client, 0x061, 0x80, u8tmp);
	if (ret)
		goto err;

	return 0;
err:
	dev_dbg(&client->dev, "failed=%d\n", ret);
	return ret;
}

static int rtl2830_pid_filter(struct dvb_frontend *fe, u8 index, u16 pid, int onoff)
{
	struct i2c_client *client = fe->demodulator_priv;
	struct rtl2830_dev *dev = i2c_get_clientdata(client);
	int ret;
	u8 buf[4];

	dev_dbg(&client->dev, "index=%d pid=%04x onoff=%d\n",
		index, pid, onoff);

	/* skip invalid PIDs (0x2000) */
	if (pid > 0x1fff || index > 32)
		return 0;

	if (onoff)
		set_bit(index, &dev->filters);
	else
		clear_bit(index, &dev->filters);

	/* enable / disable PIDs */
	buf[0] = (dev->filters >>  0) & 0xff;
	buf[1] = (dev->filters >>  8) & 0xff;
	buf[2] = (dev->filters >> 16) & 0xff;
	buf[3] = (dev->filters >> 24) & 0xff;
	ret = rtl2830_bulk_write(client, 0x062, buf, 4);
	if (ret)
		goto err;

	/* add PID */
	buf[0] = (pid >> 8) & 0xff;
	buf[1] = (pid >> 0) & 0xff;
	ret = rtl2830_bulk_write(client, 0x066 + 2 * index, buf, 2);
	if (ret)
		goto err;

	return 0;
err:
	dev_dbg(&client->dev, "failed=%d\n", ret);
	return ret;
}

/*
 * I2C gate/mux/repeater logic
 * We must use unlocked __i2c_transfer() here (through regmap) because of I2C
 * adapter lock is already taken by tuner driver.
 * Gate is closed automatically after single I2C transfer.
 */
static int rtl2830_select(struct i2c_adapter *adap, void *mux_priv, u32 chan_id)
{
	struct i2c_client *client = mux_priv;
	struct rtl2830_dev *dev = i2c_get_clientdata(client);
	int ret;

	dev_dbg(&client->dev, "\n");

	/* open I2C repeater for 1 transfer, closes automatically */
	/* XXX: regmap_update_bits() does not lock I2C adapter */
	ret = regmap_update_bits(dev->regmap, 0x101, 0x08, 0x08);
	if (ret)
		goto err;

	return 0;
err:
	dev_dbg(&client->dev, "failed=%d\n", ret);
	return ret;
}

static struct dvb_frontend *rtl2830_get_dvb_frontend(struct i2c_client *client)
{
	struct rtl2830_dev *dev = i2c_get_clientdata(client);

	dev_dbg(&client->dev, "\n");

	return &dev->fe;
}

static struct i2c_adapter *rtl2830_get_i2c_adapter(struct i2c_client *client)
{
	struct rtl2830_dev *dev = i2c_get_clientdata(client);

	dev_dbg(&client->dev, "\n");

	return dev->adapter;
}

/*
 * We implement own I2C access routines for regmap in order to get manual access
 * to I2C adapter lock, which is needed for I2C mux adapter.
 */
static int rtl2830_regmap_read(void *context, const void *reg_buf,
			       size_t reg_size, void *val_buf, size_t val_size)
{
	struct i2c_client *client = context;
	int ret;
	struct i2c_msg msg[2] = {
		{
			.addr = client->addr,
			.flags = 0,
			.len = reg_size,
			.buf = (u8 *)reg_buf,
		}, {
			.addr = client->addr,
			.flags = I2C_M_RD,
			.len = val_size,
			.buf = val_buf,
		}
	};

	ret = __i2c_transfer(client->adapter, msg, 2);
	if (ret != 2) {
		dev_warn(&client->dev, "i2c reg read failed %d\n", ret);
		if (ret >= 0)
			ret = -EREMOTEIO;
		return ret;
	}
	return 0;
}

static int rtl2830_regmap_write(void *context, const void *data, size_t count)
{
	struct i2c_client *client = context;
	int ret;
	struct i2c_msg msg[1] = {
		{
			.addr = client->addr,
			.flags = 0,
			.len = count,
			.buf = (u8 *)data,
		}
	};

	ret = __i2c_transfer(client->adapter, msg, 1);
	if (ret != 1) {
		dev_warn(&client->dev, "i2c reg write failed %d\n", ret);
		if (ret >= 0)
			ret = -EREMOTEIO;
		return ret;
	}
	return 0;
}

static int rtl2830_regmap_gather_write(void *context, const void *reg,
				       size_t reg_len, const void *val,
				       size_t val_len)
{
	struct i2c_client *client = context;
	int ret;
	u8 buf[256];
	struct i2c_msg msg[1] = {
		{
			.addr = client->addr,
			.flags = 0,
			.len = 1 + val_len,
			.buf = buf,
		}
	};

	buf[0] = *(u8 const *)reg;
	memcpy(&buf[1], val, val_len);

	ret = __i2c_transfer(client->adapter, msg, 1);
	if (ret != 1) {
		dev_warn(&client->dev, "i2c reg write failed %d\n", ret);
		if (ret >= 0)
			ret = -EREMOTEIO;
		return ret;
	}
	return 0;
}

static int rtl2830_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct rtl2830_platform_data *pdata = client->dev.platform_data;
	struct rtl2830_dev *dev;
	int ret;
	u8 u8tmp;
	static const struct regmap_bus regmap_bus = {
		.read = rtl2830_regmap_read,
		.write = rtl2830_regmap_write,
		.gather_write = rtl2830_regmap_gather_write,
		.val_format_endian_default = REGMAP_ENDIAN_NATIVE,
	};
	static const struct regmap_range_cfg regmap_range_cfg[] = {
		{
			.selector_reg     = 0x00,
			.selector_mask    = 0xff,
			.selector_shift   = 0,
			.window_start     = 0,
			.window_len       = 0x100,
			.range_min        = 0 * 0x100,
			.range_max        = 5 * 0x100,
		},
	};
	static const struct regmap_config regmap_config = {
		.reg_bits    =  8,
		.val_bits    =  8,
		.max_register = 5 * 0x100,
		.ranges = regmap_range_cfg,
		.num_ranges = ARRAY_SIZE(regmap_range_cfg),
	};

	dev_dbg(&client->dev, "\n");

	if (pdata == NULL) {
		ret = -EINVAL;
		goto err;
	}

	/* allocate memory for the internal state */
	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (dev == NULL) {
		ret = -ENOMEM;
		goto err;
	}

	/* setup the state */
	i2c_set_clientdata(client, dev);
	dev->client = client;
	dev->pdata = client->dev.platform_data;
	dev->sleeping = true;
	INIT_DELAYED_WORK(&dev->stat_work, rtl2830_stat_work);
	dev->regmap = regmap_init(&client->dev, &regmap_bus, client,
				  &regmap_config);
	if (IS_ERR(dev->regmap)) {
		ret = PTR_ERR(dev->regmap);
		goto err_kfree;
	}

	/* check if the demod is there */
	ret = rtl2830_bulk_read(client, 0x000, &u8tmp, 1);
	if (ret)
		goto err_regmap_exit;

	/* create muxed i2c adapter for tuner */
	dev->adapter = i2c_add_mux_adapter(client->adapter, &client->dev,
			client, 0, 0, 0, rtl2830_select, NULL);
	if (dev->adapter == NULL) {
		ret = -ENODEV;
		goto err_regmap_exit;
	}

	/* create dvb frontend */
	memcpy(&dev->fe.ops, &rtl2830_ops, sizeof(dev->fe.ops));
	dev->fe.demodulator_priv = client;

	/* setup callbacks */
	pdata->get_dvb_frontend = rtl2830_get_dvb_frontend;
	pdata->get_i2c_adapter = rtl2830_get_i2c_adapter;
	pdata->pid_filter = rtl2830_pid_filter;
	pdata->pid_filter_ctrl = rtl2830_pid_filter_ctrl;

	dev_info(&client->dev, "Realtek RTL2830 successfully attached\n");

	return 0;
err_regmap_exit:
	regmap_exit(dev->regmap);
err_kfree:
	kfree(dev);
err:
	dev_dbg(&client->dev, "failed=%d\n", ret);
	return ret;
}

static int rtl2830_remove(struct i2c_client *client)
{
	struct rtl2830_dev *dev = i2c_get_clientdata(client);

	dev_dbg(&client->dev, "\n");

	/* stop statistics polling */
	cancel_delayed_work_sync(&dev->stat_work);

	i2c_del_mux_adapter(dev->adapter);
	regmap_exit(dev->regmap);
	kfree(dev);

	return 0;
}

static const struct i2c_device_id rtl2830_id_table[] = {
	{"rtl2830", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, rtl2830_id_table);

static struct i2c_driver rtl2830_driver = {
	.driver = {
		.name	= "rtl2830",
	},
	.probe		= rtl2830_probe,
	.remove		= rtl2830_remove,
	.id_table	= rtl2830_id_table,
};

module_i2c_driver(rtl2830_driver);

MODULE_AUTHOR("Antti Palosaari <crope@iki.fi>");
MODULE_DESCRIPTION("Realtek RTL2830 DVB-T demodulator driver");
MODULE_LICENSE("GPL");
