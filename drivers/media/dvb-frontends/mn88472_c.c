/*
 * Panasonic MN88472 DVB-T/T2/C demodulator driver
 *
 * Copyright (C) 2013 Antti Palosaari <crope@iki.fi>
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
 */

#include "mn88472_priv.h"

static struct dvb_frontend_ops mn88472_ops_c;

/* write multiple registers */
static int mn88472_wregs(struct mn88472_state *s, u16 reg, const u8 *val, int len)
{
#define MAX_WR_LEN 21
#define MAX_WR_XFER_LEN (MAX_WR_LEN + 1)
	int ret;
	u8 buf[MAX_WR_XFER_LEN];
	struct i2c_msg msg[1] = {
		{
			.addr = (reg >> 8) & 0xff,
			.flags = 0,
			.len = 1 + len,
			.buf = buf,
		}
	};

	if (WARN_ON(len > MAX_WR_LEN))
		return -EINVAL;

	buf[0] = (reg >> 0) & 0xff;
	memcpy(&buf[1], val, len);

	ret = i2c_transfer(s->i2c, msg, 1);
	if (ret == 1) {
		ret = 0;
	} else {
		dev_warn(&s->i2c->dev,
				"%s: i2c wr failed=%d reg=%02x len=%d\n",
				KBUILD_MODNAME, ret, reg, len);
		ret = -EREMOTEIO;
	}

	return ret;
}

/* read multiple registers */
static int mn88472_rregs(struct mn88472_state *s, u16 reg, u8 *val, int len)
{
#define MAX_RD_LEN 2
#define MAX_RD_XFER_LEN (MAX_RD_LEN)
	int ret;
	u8 buf[MAX_RD_XFER_LEN];
	struct i2c_msg msg[2] = {
		{
			.addr = (reg >> 8) & 0xff,
			.flags = 0,
			.len = 1,
			.buf = buf,
		}, {
			.addr = (reg >> 8) & 0xff,
			.flags = I2C_M_RD,
			.len = len,
			.buf = buf,
		}
	};

	if (WARN_ON(len > MAX_RD_LEN))
		return -EINVAL;

	buf[0] = (reg >> 0) & 0xff;

	ret = i2c_transfer(s->i2c, msg, 2);
	if (ret == 2) {
		memcpy(val, buf, len);
		ret = 0;
	} else {
		dev_warn(&s->i2c->dev,
				"%s: i2c rd failed=%d reg=%02x len=%d\n",
				KBUILD_MODNAME, ret, reg, len);
		ret = -EREMOTEIO;
	}

	return ret;
}

/* write single register */
static int mn88472_wreg(struct mn88472_state *s, u16 reg, u8 val)
{
	return mn88472_wregs(s, reg, &val, 1);
}

/* read single register */
static int mn88472_rreg(struct mn88472_state *s, u16 reg, u8 *val)
{
	return mn88472_rregs(s, reg, val, 1);
}

static int mn88472_get_tune_settings(struct dvb_frontend *fe,
	struct dvb_frontend_tune_settings *s)
{
	s->min_delay_ms = 400;
	return 0;
}

static int mn88472_set_frontend_c(struct dvb_frontend *fe)
{
	struct mn88472_state *s = fe->demodulator_priv;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	int ret;
	u32 if_frequency = 0;
	dev_dbg(&s->i2c->dev,
			"%s: delivery_system=%d modulation=%d frequency=%d symbol_rate=%d inversion=%d\n",
			__func__, c->delivery_system, c->modulation,
			c->frequency, c->symbol_rate, c->inversion);

	if (!s->warm) {
		ret = -EAGAIN;
		goto err;
	}

	/* program tuner */
	if (fe->ops.tuner_ops.set_params) {
		ret = fe->ops.tuner_ops.set_params(fe);
		if (ret)
			goto err;
	}

	if (fe->ops.tuner_ops.get_if_frequency) {
		ret = fe->ops.tuner_ops.get_if_frequency(fe, &if_frequency);
		if (ret)
			goto err;

		dev_dbg(&s->i2c->dev, "%s: get_if_frequency=%d\n",
				__func__, if_frequency);
	}

	if (if_frequency != 5070000) {
		dev_err(&s->i2c->dev, "%s: IF frequency %d not supported\n",
				KBUILD_MODNAME, if_frequency);
		ret = -EINVAL;
		goto err;
	}

	ret = mn88472_wregs(s, 0x1c08, "\x1d", 1);
	if (ret)
		goto err;

	ret = mn88472_wregs(s, 0x18d9, "\xe3", 1);
	if (ret)
		goto err;

	ret = mn88472_wregs(s, 0x1c83, "\x01", 1);
	if (ret)
		goto err;

	ret = mn88472_wregs(s, 0x1c00, "\x66\x00\x01\x04\x00", 5);
	if (ret)
		goto err;

	ret = mn88472_wregs(s, 0x1c10,
			"\x3f\x50\x2c\x8f\x80\x00\x08\xee\x08\xee", 10);
	if (ret)
		goto err;

	ret = mn88472_wregs(s, 0x1846, "\x00", 1);
	if (ret)
		goto err;

	ret = mn88472_wregs(s, 0x18ae, "\x00", 1);
	if (ret)
		goto err;

	ret = mn88472_wregs(s, 0x18b0, "\x0b", 1);
	if (ret)
		goto err;

	ret = mn88472_wregs(s, 0x18b4, "\x00", 1);
	if (ret)
		goto err;

	ret = mn88472_wregs(s, 0x18cd, "\x17", 1);
	if (ret)
		goto err;

	ret = mn88472_wregs(s, 0x18d4, "\x09", 1);
	if (ret)
		goto err;

	ret = mn88472_wregs(s, 0x18d6, "\x48", 1);
	if (ret)
		goto err;

	ret = mn88472_wregs(s, 0x1a00, "\xb0", 1);
	if (ret)
		goto err;

	ret = mn88472_wregs(s, 0x1cf8, "\x9f", 1);
	if (ret)
		goto err;

	s->delivery_system = c->delivery_system;

	return 0;
err:
	dev_dbg(&s->i2c->dev, "%s: failed=%d\n", __func__, ret);
	return ret;
}

static int mn88472_read_status_c(struct dvb_frontend *fe, fe_status_t *status)
{
	struct mn88472_state *s = fe->demodulator_priv;
	int ret;
	u8 u8tmp;

	*status = 0;

	if (!s->warm) {
		ret = -EAGAIN;
		goto err;
	}

	ret = mn88472_rreg(s, 0x1a84, &u8tmp);
	if (ret)
		goto err;

	if (u8tmp == 0x08)
		*status = FE_HAS_SIGNAL | FE_HAS_CARRIER | FE_HAS_VITERBI |
				FE_HAS_SYNC | FE_HAS_LOCK;

	return 0;
err:
	dev_dbg(&s->i2c->dev, "%s: failed=%d\n", __func__, ret);
	return ret;
}

static int mn88472_init_c(struct dvb_frontend *fe)
{
	struct mn88472_state *s = fe->demodulator_priv;
	int ret, len, remaining;
	const struct firmware *fw = NULL;
	u8 *fw_file = MN88472_FIRMWARE;
	dev_dbg(&s->i2c->dev, "%s:\n", __func__);

	/* set cold state by default */
	s->warm = false;

	/* power on */
	ret = mn88472_wreg(s, 0x1c05, 0x00);
	if (ret)
		goto err;

	ret = mn88472_wregs(s, 0x1c0b, "\x00\x00", 2);
	if (ret)
		goto err;

	/* request the firmware, this will block and timeout */
	ret = request_firmware(&fw, fw_file, s->i2c->dev.parent);
	if (ret) {
		dev_err(&s->i2c->dev, "%s: firmare file '%s' not found\n",
				KBUILD_MODNAME, fw_file);
		goto err;
	}

	dev_info(&s->i2c->dev, "%s: downloading firmware from file '%s'\n",
			KBUILD_MODNAME, fw_file);

	ret = mn88472_wreg(s, 0x18f5, 0x03);
	if (ret)
		goto err;

	for (remaining = fw->size; remaining > 0;
			remaining -= (s->cfg->i2c_wr_max - 1)) {
		len = remaining;
		if (len > (s->cfg->i2c_wr_max - 1))
			len = (s->cfg->i2c_wr_max - 1);

		ret = mn88472_wregs(s, 0x18f6,
				&fw->data[fw->size - remaining], len);
		if (ret) {
			dev_err(&s->i2c->dev,
					"%s: firmware download failed=%d\n",
					KBUILD_MODNAME, ret);
			goto err;
		}
	}

	ret = mn88472_wreg(s, 0x18f5, 0x00);
	if (ret)
		goto err;

	release_firmware(fw);
	fw = NULL;

	/* warm state */
	s->warm = true;

	return 0;
err:
	if (fw)
		release_firmware(fw);

	dev_dbg(&s->i2c->dev, "%s: failed=%d\n", __func__, ret);
	return ret;
}

static int mn88472_sleep_c(struct dvb_frontend *fe)
{
	struct mn88472_state *s = fe->demodulator_priv;
	int ret;
	dev_dbg(&s->i2c->dev, "%s:\n", __func__);

	/* power off */
	ret = mn88472_wreg(s, 0x1c0b, 0x30);
	if (ret)
		goto err;

	ret = mn88472_wreg(s, 0x1c05, 0x3e);
	if (ret)
		goto err;

	s->delivery_system = SYS_UNDEFINED;

	return 0;
err:
	dev_dbg(&s->i2c->dev, "%s: failed=%d\n", __func__, ret);
	return ret;
}

static void mn88472_release_c(struct dvb_frontend *fe)
{
	struct mn88472_state *s = fe->demodulator_priv;
	kfree(s);
}

struct dvb_frontend *mn88472_attach_c(const struct mn88472_c_config *cfg,
		struct i2c_adapter *i2c)
{
	int ret;
	struct mn88472_state *s;
	u8 u8tmp;
	dev_dbg(&i2c->dev, "%s:\n", __func__);

	/* allocate memory for the internal state */
	s = kzalloc(sizeof(struct mn88472_state), GFP_KERNEL);
	if (!s) {
		ret = -ENOMEM;
		dev_err(&i2c->dev, "%s: kzalloc() failed\n", KBUILD_MODNAME);
		goto err;
	}

	s->cfg = cfg;
	s->i2c = i2c;

	/* check demod responds to I2C */
	ret = mn88472_rreg(s, 0x1c00, &u8tmp);
	if (ret)
		goto err;

	/* create dvb_frontend */
	memcpy(&s->fe.ops, &mn88472_ops_c, sizeof(struct dvb_frontend_ops));
	s->fe.demodulator_priv = s;

	return &s->fe;
err:
	dev_dbg(&i2c->dev, "%s: failed=%d\n", __func__, ret);
	kfree(s);
	return NULL;
}
EXPORT_SYMBOL(mn88472_attach_c);

static struct dvb_frontend_ops mn88472_ops_c = {
	.delsys = {SYS_DVBC_ANNEX_A},
	.info = {
		.name = "Panasonic MN88472",
		.caps =	FE_CAN_FEC_1_2			|
			FE_CAN_FEC_2_3			|
			FE_CAN_FEC_3_4			|
			FE_CAN_FEC_5_6			|
			FE_CAN_FEC_7_8			|
			FE_CAN_FEC_AUTO			|
			FE_CAN_QPSK			|
			FE_CAN_QAM_16			|
			FE_CAN_QAM_32			|
			FE_CAN_QAM_64			|
			FE_CAN_QAM_128			|
			FE_CAN_QAM_256			|
			FE_CAN_QAM_AUTO			|
			FE_CAN_TRANSMISSION_MODE_AUTO	|
			FE_CAN_GUARD_INTERVAL_AUTO	|
			FE_CAN_HIERARCHY_AUTO		|
			FE_CAN_MUTE_TS			|
			FE_CAN_2G_MODULATION		|
			FE_CAN_MULTISTREAM
	},

	.release = mn88472_release_c,

	.get_tune_settings = mn88472_get_tune_settings,

	.init = mn88472_init_c,
	.sleep = mn88472_sleep_c,

	.set_frontend = mn88472_set_frontend_c,
/*	.get_frontend = mn88472_get_frontend_c, */

	.read_status = mn88472_read_status_c,
/*	.read_snr = mn88472_read_snr_c, */
};

MODULE_AUTHOR("Antti Palosaari <crope@iki.fi>");
MODULE_DESCRIPTION("Panasonic MN88472 DVB-T/T2/C demodulator driver");
MODULE_LICENSE("GPL");
MODULE_FIRMWARE(MN88472_FIRMWARE);
