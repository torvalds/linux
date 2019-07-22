// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Silicon Labs Si2168 DVB-T/T2/C demodulator driver
 *
 * Copyright (C) 2014 Antti Palosaari <crope@iki.fi>
 */

#include <linux/delay.h>

#include "si2168_priv.h"

static const struct dvb_frontend_ops si2168_ops;

/* execute firmware command */
static int si2168_cmd_execute(struct i2c_client *client, struct si2168_cmd *cmd)
{
	struct si2168_dev *dev = i2c_get_clientdata(client);
	int ret;
	unsigned long timeout;

	mutex_lock(&dev->i2c_mutex);

	if (cmd->wlen) {
		/* write cmd and args for firmware */
		ret = i2c_master_send(client, cmd->args, cmd->wlen);
		if (ret < 0) {
			goto err_mutex_unlock;
		} else if (ret != cmd->wlen) {
			ret = -EREMOTEIO;
			goto err_mutex_unlock;
		}
	}

	if (cmd->rlen) {
		/* wait cmd execution terminate */
		#define TIMEOUT 70
		timeout = jiffies + msecs_to_jiffies(TIMEOUT);
		while (!time_after(jiffies, timeout)) {
			ret = i2c_master_recv(client, cmd->args, cmd->rlen);
			if (ret < 0) {
				goto err_mutex_unlock;
			} else if (ret != cmd->rlen) {
				ret = -EREMOTEIO;
				goto err_mutex_unlock;
			}

			/* firmware ready? */
			if ((cmd->args[0] >> 7) & 0x01)
				break;
		}

		dev_dbg(&client->dev, "cmd execution took %d ms\n",
				jiffies_to_msecs(jiffies) -
				(jiffies_to_msecs(timeout) - TIMEOUT));

		/* error bit set? */
		if ((cmd->args[0] >> 6) & 0x01) {
			ret = -EREMOTEIO;
			goto err_mutex_unlock;
		}

		if (!((cmd->args[0] >> 7) & 0x01)) {
			ret = -ETIMEDOUT;
			goto err_mutex_unlock;
		}
	}

	mutex_unlock(&dev->i2c_mutex);
	return 0;
err_mutex_unlock:
	mutex_unlock(&dev->i2c_mutex);
	dev_dbg(&client->dev, "failed=%d\n", ret);
	return ret;
}

static int si2168_ts_bus_ctrl(struct dvb_frontend *fe, int acquire)
{
	struct i2c_client *client = fe->demodulator_priv;
	struct si2168_dev *dev = i2c_get_clientdata(client);
	struct si2168_cmd cmd;
	int ret = 0;

	dev_dbg(&client->dev, "%s acquire: %d\n", __func__, acquire);

	/* set TS_MODE property */
	memcpy(cmd.args, "\x14\x00\x01\x10\x10\x00", 6);
	if (acquire)
		cmd.args[4] |= dev->ts_mode;
	else
		cmd.args[4] |= SI2168_TS_TRISTATE;
	if (dev->ts_clock_gapped)
		cmd.args[4] |= 0x40;
	cmd.wlen = 6;
	cmd.rlen = 4;
	ret = si2168_cmd_execute(client, &cmd);

	return ret;
}

static int si2168_read_status(struct dvb_frontend *fe, enum fe_status *status)
{
	struct i2c_client *client = fe->demodulator_priv;
	struct si2168_dev *dev = i2c_get_clientdata(client);
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	int ret, i;
	unsigned int utmp, utmp1, utmp2;
	struct si2168_cmd cmd;

	*status = 0;

	if (!dev->active) {
		ret = -EAGAIN;
		goto err;
	}

	switch (c->delivery_system) {
	case SYS_DVBT:
		memcpy(cmd.args, "\xa0\x01", 2);
		cmd.wlen = 2;
		cmd.rlen = 13;
		break;
	case SYS_DVBC_ANNEX_A:
		memcpy(cmd.args, "\x90\x01", 2);
		cmd.wlen = 2;
		cmd.rlen = 9;
		break;
	case SYS_DVBT2:
		memcpy(cmd.args, "\x50\x01", 2);
		cmd.wlen = 2;
		cmd.rlen = 14;
		break;
	default:
		ret = -EINVAL;
		goto err;
	}

	ret = si2168_cmd_execute(client, &cmd);
	if (ret)
		goto err;

	switch ((cmd.args[2] >> 1) & 0x03) {
	case 0x01:
		*status = FE_HAS_SIGNAL | FE_HAS_CARRIER;
		break;
	case 0x03:
		*status = FE_HAS_SIGNAL | FE_HAS_CARRIER | FE_HAS_VITERBI |
				FE_HAS_SYNC | FE_HAS_LOCK;
		break;
	}

	dev->fe_status = *status;

	if (*status & FE_HAS_LOCK) {
		c->cnr.len = 1;
		c->cnr.stat[0].scale = FE_SCALE_DECIBEL;
		c->cnr.stat[0].svalue = cmd.args[3] * 1000 / 4;
	} else {
		c->cnr.len = 1;
		c->cnr.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	}

	dev_dbg(&client->dev, "status=%02x args=%*ph\n",
			*status, cmd.rlen, cmd.args);

	/* BER */
	if (*status & FE_HAS_VITERBI) {
		memcpy(cmd.args, "\x82\x00", 2);
		cmd.wlen = 2;
		cmd.rlen = 3;
		ret = si2168_cmd_execute(client, &cmd);
		if (ret)
			goto err;

		/*
		 * Firmware returns [0, 255] mantissa and [0, 8] exponent.
		 * Convert to DVB API: mantissa * 10^(8 - exponent) / 10^8
		 */
		utmp = clamp(8 - cmd.args[1], 0, 8);
		for (i = 0, utmp1 = 1; i < utmp; i++)
			utmp1 = utmp1 * 10;

		utmp1 = cmd.args[2] * utmp1;
		utmp2 = 100000000; /* 10^8 */

		dev_dbg(&client->dev,
			"post_bit_error=%u post_bit_count=%u ber=%u*10^-%u\n",
			utmp1, utmp2, cmd.args[2], cmd.args[1]);

		c->post_bit_error.stat[0].scale = FE_SCALE_COUNTER;
		c->post_bit_error.stat[0].uvalue += utmp1;
		c->post_bit_count.stat[0].scale = FE_SCALE_COUNTER;
		c->post_bit_count.stat[0].uvalue += utmp2;
	} else {
		c->post_bit_error.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
		c->post_bit_count.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	}

	/* UCB */
	if (*status & FE_HAS_SYNC) {
		memcpy(cmd.args, "\x84\x01", 2);
		cmd.wlen = 2;
		cmd.rlen = 3;
		ret = si2168_cmd_execute(client, &cmd);
		if (ret)
			goto err;

		utmp1 = cmd.args[2] << 8 | cmd.args[1] << 0;
		dev_dbg(&client->dev, "block_error=%u\n", utmp1);

		/* Sometimes firmware returns bogus value */
		if (utmp1 == 0xffff)
			utmp1 = 0;

		c->block_error.stat[0].scale = FE_SCALE_COUNTER;
		c->block_error.stat[0].uvalue += utmp1;
	} else {
		c->block_error.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	}

	return 0;
err:
	dev_dbg(&client->dev, "failed=%d\n", ret);
	return ret;
}

static int si2168_set_frontend(struct dvb_frontend *fe)
{
	struct i2c_client *client = fe->demodulator_priv;
	struct si2168_dev *dev = i2c_get_clientdata(client);
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	int ret;
	struct si2168_cmd cmd;
	u8 bandwidth, delivery_system;

	dev_dbg(&client->dev,
			"delivery_system=%u modulation=%u frequency=%u bandwidth_hz=%u symbol_rate=%u inversion=%u stream_id=%u\n",
			c->delivery_system, c->modulation, c->frequency,
			c->bandwidth_hz, c->symbol_rate, c->inversion,
			c->stream_id);

	if (!dev->active) {
		ret = -EAGAIN;
		goto err;
	}

	switch (c->delivery_system) {
	case SYS_DVBT:
		delivery_system = 0x20;
		break;
	case SYS_DVBC_ANNEX_A:
		delivery_system = 0x30;
		break;
	case SYS_DVBT2:
		delivery_system = 0x70;
		break;
	default:
		ret = -EINVAL;
		goto err;
	}

	if (c->bandwidth_hz == 0) {
		ret = -EINVAL;
		goto err;
	} else if (c->bandwidth_hz <= 2000000)
		bandwidth = 0x02;
	else if (c->bandwidth_hz <= 5000000)
		bandwidth = 0x05;
	else if (c->bandwidth_hz <= 6000000)
		bandwidth = 0x06;
	else if (c->bandwidth_hz <= 7000000)
		bandwidth = 0x07;
	else if (c->bandwidth_hz <= 8000000)
		bandwidth = 0x08;
	else if (c->bandwidth_hz <= 9000000)
		bandwidth = 0x09;
	else if (c->bandwidth_hz <= 10000000)
		bandwidth = 0x0a;
	else
		bandwidth = 0x0f;

	/* program tuner */
	if (fe->ops.tuner_ops.set_params) {
		ret = fe->ops.tuner_ops.set_params(fe);
		if (ret)
			goto err;
	}

	memcpy(cmd.args, "\x88\x02\x02\x02\x02", 5);
	cmd.wlen = 5;
	cmd.rlen = 5;
	ret = si2168_cmd_execute(client, &cmd);
	if (ret)
		goto err;

	/* that has no big effect */
	if (c->delivery_system == SYS_DVBT)
		memcpy(cmd.args, "\x89\x21\x06\x11\xff\x98", 6);
	else if (c->delivery_system == SYS_DVBC_ANNEX_A)
		memcpy(cmd.args, "\x89\x21\x06\x11\x89\xf0", 6);
	else if (c->delivery_system == SYS_DVBT2)
		memcpy(cmd.args, "\x89\x21\x06\x11\x89\x20", 6);
	cmd.wlen = 6;
	cmd.rlen = 3;
	ret = si2168_cmd_execute(client, &cmd);
	if (ret)
		goto err;

	if (c->delivery_system == SYS_DVBT2) {
		/* select PLP */
		cmd.args[0] = 0x52;
		cmd.args[1] = c->stream_id & 0xff;
		cmd.args[2] = c->stream_id == NO_STREAM_ID_FILTER ? 0 : 1;
		cmd.wlen = 3;
		cmd.rlen = 1;
		ret = si2168_cmd_execute(client, &cmd);
		if (ret)
			goto err;
	}

	memcpy(cmd.args, "\x51\x03", 2);
	cmd.wlen = 2;
	cmd.rlen = 12;
	ret = si2168_cmd_execute(client, &cmd);
	if (ret)
		goto err;

	memcpy(cmd.args, "\x12\x08\x04", 3);
	cmd.wlen = 3;
	cmd.rlen = 3;
	ret = si2168_cmd_execute(client, &cmd);
	if (ret)
		goto err;

	memcpy(cmd.args, "\x14\x00\x0c\x10\x12\x00", 6);
	cmd.wlen = 6;
	cmd.rlen = 4;
	ret = si2168_cmd_execute(client, &cmd);
	if (ret)
		goto err;

	memcpy(cmd.args, "\x14\x00\x06\x10\x24\x00", 6);
	cmd.wlen = 6;
	cmd.rlen = 4;
	ret = si2168_cmd_execute(client, &cmd);
	if (ret)
		goto err;

	memcpy(cmd.args, "\x14\x00\x07\x10\x00\x24", 6);
	cmd.wlen = 6;
	cmd.rlen = 4;
	ret = si2168_cmd_execute(client, &cmd);
	if (ret)
		goto err;

	memcpy(cmd.args, "\x14\x00\x0a\x10\x00\x00", 6);
	cmd.args[4] = delivery_system | bandwidth;
	if (dev->spectral_inversion)
		cmd.args[5] |= 1;
	cmd.wlen = 6;
	cmd.rlen = 4;
	ret = si2168_cmd_execute(client, &cmd);
	if (ret)
		goto err;

	/* set DVB-C symbol rate */
	if (c->delivery_system == SYS_DVBC_ANNEX_A) {
		memcpy(cmd.args, "\x14\x00\x02\x11", 4);
		cmd.args[4] = ((c->symbol_rate / 1000) >> 0) & 0xff;
		cmd.args[5] = ((c->symbol_rate / 1000) >> 8) & 0xff;
		cmd.wlen = 6;
		cmd.rlen = 4;
		ret = si2168_cmd_execute(client, &cmd);
		if (ret)
			goto err;
	}

	memcpy(cmd.args, "\x14\x00\x0f\x10\x10\x00", 6);
	cmd.wlen = 6;
	cmd.rlen = 4;
	ret = si2168_cmd_execute(client, &cmd);
	if (ret)
		goto err;

	memcpy(cmd.args, "\x14\x00\x09\x10\xe3\x08", 6);
	cmd.args[5] |= dev->ts_clock_inv ? 0x00 : 0x10;
	cmd.wlen = 6;
	cmd.rlen = 4;
	ret = si2168_cmd_execute(client, &cmd);
	if (ret)
		goto err;

	memcpy(cmd.args, "\x14\x00\x08\x10\xd7\x05", 6);
	cmd.args[5] |= dev->ts_clock_inv ? 0x00 : 0x10;
	cmd.wlen = 6;
	cmd.rlen = 4;
	ret = si2168_cmd_execute(client, &cmd);
	if (ret)
		goto err;

	memcpy(cmd.args, "\x14\x00\x01\x12\x00\x00", 6);
	cmd.wlen = 6;
	cmd.rlen = 4;
	ret = si2168_cmd_execute(client, &cmd);
	if (ret)
		goto err;

	memcpy(cmd.args, "\x14\x00\x01\x03\x0c\x00", 6);
	cmd.wlen = 6;
	cmd.rlen = 4;
	ret = si2168_cmd_execute(client, &cmd);
	if (ret)
		goto err;

	memcpy(cmd.args, "\x85", 1);
	cmd.wlen = 1;
	cmd.rlen = 1;
	ret = si2168_cmd_execute(client, &cmd);
	if (ret)
		goto err;

	dev->delivery_system = c->delivery_system;

	/* enable ts bus */
	ret = si2168_ts_bus_ctrl(fe, 1);
	if (ret)
		goto err;

	return 0;
err:
	dev_dbg(&client->dev, "failed=%d\n", ret);
	return ret;
}

static int si2168_init(struct dvb_frontend *fe)
{
	struct i2c_client *client = fe->demodulator_priv;
	struct si2168_dev *dev = i2c_get_clientdata(client);
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	int ret, len, remaining;
	const struct firmware *fw;
	struct si2168_cmd cmd;

	dev_dbg(&client->dev, "\n");

	/* initialize */
	memcpy(cmd.args, "\xc0\x12\x00\x0c\x00\x0d\x16\x00\x00\x00\x00\x00\x00", 13);
	cmd.wlen = 13;
	cmd.rlen = 0;
	ret = si2168_cmd_execute(client, &cmd);
	if (ret)
		goto err;

	if (dev->warm) {
		/* resume */
		memcpy(cmd.args, "\xc0\x06\x08\x0f\x00\x20\x21\x01", 8);
		cmd.wlen = 8;
		cmd.rlen = 1;
		ret = si2168_cmd_execute(client, &cmd);
		if (ret)
			goto err;

		udelay(100);
		memcpy(cmd.args, "\x85", 1);
		cmd.wlen = 1;
		cmd.rlen = 1;
		ret = si2168_cmd_execute(client, &cmd);
		if (ret)
			goto err;

		goto warm;
	}

	/* power up */
	memcpy(cmd.args, "\xc0\x06\x01\x0f\x00\x20\x20\x01", 8);
	cmd.wlen = 8;
	cmd.rlen = 1;
	ret = si2168_cmd_execute(client, &cmd);
	if (ret)
		goto err;

	/* request the firmware, this will block and timeout */
	ret = request_firmware(&fw, dev->firmware_name, &client->dev);
	if (ret) {
		/* fallback mechanism to handle old name for Si2168 B40 fw */
		if (dev->chip_id == SI2168_CHIP_ID_B40) {
			dev->firmware_name = SI2168_B40_FIRMWARE_FALLBACK;
			ret = request_firmware(&fw, dev->firmware_name,
					       &client->dev);
		}

		if (ret == 0) {
			dev_notice(&client->dev,
					"please install firmware file '%s'\n",
					SI2168_B40_FIRMWARE);
		} else {
			dev_err(&client->dev,
					"firmware file '%s' not found\n",
					dev->firmware_name);
			goto err_release_firmware;
		}
	}

	dev_info(&client->dev, "downloading firmware from file '%s'\n",
			dev->firmware_name);

	if ((fw->size % 17 == 0) && (fw->data[0] > 5)) {
		/* firmware is in the new format */
		for (remaining = fw->size; remaining > 0; remaining -= 17) {
			len = fw->data[fw->size - remaining];
			if (len > SI2168_ARGLEN) {
				ret = -EINVAL;
				break;
			}
			memcpy(cmd.args, &fw->data[(fw->size - remaining) + 1], len);
			cmd.wlen = len;
			cmd.rlen = 1;
			ret = si2168_cmd_execute(client, &cmd);
			if (ret)
				break;
		}
	} else if (fw->size % 8 == 0) {
		/* firmware is in the old format */
		for (remaining = fw->size; remaining > 0; remaining -= 8) {
			len = 8;
			memcpy(cmd.args, &fw->data[fw->size - remaining], len);
			cmd.wlen = len;
			cmd.rlen = 1;
			ret = si2168_cmd_execute(client, &cmd);
			if (ret)
				break;
		}
	} else {
		/* bad or unknown firmware format */
		ret = -EINVAL;
	}

	if (ret) {
		dev_err(&client->dev, "firmware download failed %d\n", ret);
		goto err_release_firmware;
	}

	release_firmware(fw);

	memcpy(cmd.args, "\x01\x01", 2);
	cmd.wlen = 2;
	cmd.rlen = 1;
	ret = si2168_cmd_execute(client, &cmd);
	if (ret)
		goto err;

	/* query firmware version */
	memcpy(cmd.args, "\x11", 1);
	cmd.wlen = 1;
	cmd.rlen = 10;
	ret = si2168_cmd_execute(client, &cmd);
	if (ret)
		goto err;

	dev->version = (cmd.args[9] + '@') << 24 | (cmd.args[6] - '0') << 16 |
		       (cmd.args[7] - '0') << 8 | (cmd.args[8]) << 0;
	dev_info(&client->dev, "firmware version: %c %d.%d.%d\n",
		 dev->version >> 24 & 0xff, dev->version >> 16 & 0xff,
		 dev->version >> 8 & 0xff, dev->version >> 0 & 0xff);

	/* set ts mode */
	ret = si2168_ts_bus_ctrl(fe, 1);
	if (ret)
		goto err;

	dev->warm = true;
warm:
	/* Init stats here to indicate which stats are supported */
	c->cnr.len = 1;
	c->cnr.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	c->post_bit_error.len = 1;
	c->post_bit_error.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	c->post_bit_count.len = 1;
	c->post_bit_count.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	c->block_error.len = 1;
	c->block_error.stat[0].scale = FE_SCALE_NOT_AVAILABLE;

	dev->active = true;

	return 0;
err_release_firmware:
	release_firmware(fw);
err:
	dev_dbg(&client->dev, "failed=%d\n", ret);
	return ret;
}

static int si2168_sleep(struct dvb_frontend *fe)
{
	struct i2c_client *client = fe->demodulator_priv;
	struct si2168_dev *dev = i2c_get_clientdata(client);
	int ret;
	struct si2168_cmd cmd;

	dev_dbg(&client->dev, "\n");

	dev->active = false;

	/* tri-state data bus */
	ret = si2168_ts_bus_ctrl(fe, 0);
	if (ret)
		goto err;

	/* Firmware later than B 4.0-11 loses warm state during sleep */
	if (dev->version > ('B' << 24 | 4 << 16 | 0 << 8 | 11 << 0))
		dev->warm = false;

	memcpy(cmd.args, "\x13", 1);
	cmd.wlen = 1;
	cmd.rlen = 0;
	ret = si2168_cmd_execute(client, &cmd);
	if (ret)
		goto err;

	return 0;
err:
	dev_dbg(&client->dev, "failed=%d\n", ret);
	return ret;
}

static int si2168_get_tune_settings(struct dvb_frontend *fe,
	struct dvb_frontend_tune_settings *s)
{
	s->min_delay_ms = 900;

	return 0;
}

static int si2168_select(struct i2c_mux_core *muxc, u32 chan)
{
	struct i2c_client *client = i2c_mux_priv(muxc);
	int ret;
	struct si2168_cmd cmd;

	/* open I2C gate */
	memcpy(cmd.args, "\xc0\x0d\x01", 3);
	cmd.wlen = 3;
	cmd.rlen = 0;
	ret = si2168_cmd_execute(client, &cmd);
	if (ret)
		goto err;

	return 0;
err:
	dev_dbg(&client->dev, "failed=%d\n", ret);
	return ret;
}

static int si2168_deselect(struct i2c_mux_core *muxc, u32 chan)
{
	struct i2c_client *client = i2c_mux_priv(muxc);
	int ret;
	struct si2168_cmd cmd;

	/* close I2C gate */
	memcpy(cmd.args, "\xc0\x0d\x00", 3);
	cmd.wlen = 3;
	cmd.rlen = 0;
	ret = si2168_cmd_execute(client, &cmd);
	if (ret)
		goto err;

	return 0;
err:
	dev_dbg(&client->dev, "failed=%d\n", ret);
	return ret;
}

static const struct dvb_frontend_ops si2168_ops = {
	.delsys = {SYS_DVBT, SYS_DVBT2, SYS_DVBC_ANNEX_A},
	.info = {
		.name = "Silicon Labs Si2168",
		.frequency_min_hz      =  48 * MHz,
		.frequency_max_hz      = 870 * MHz,
		.frequency_stepsize_hz = 62500,
		.symbol_rate_min       = 1000000,
		.symbol_rate_max       = 7200000,
		.caps =	FE_CAN_FEC_1_2 |
			FE_CAN_FEC_2_3 |
			FE_CAN_FEC_3_4 |
			FE_CAN_FEC_5_6 |
			FE_CAN_FEC_7_8 |
			FE_CAN_FEC_AUTO |
			FE_CAN_QPSK |
			FE_CAN_QAM_16 |
			FE_CAN_QAM_32 |
			FE_CAN_QAM_64 |
			FE_CAN_QAM_128 |
			FE_CAN_QAM_256 |
			FE_CAN_QAM_AUTO |
			FE_CAN_TRANSMISSION_MODE_AUTO |
			FE_CAN_GUARD_INTERVAL_AUTO |
			FE_CAN_HIERARCHY_AUTO |
			FE_CAN_MUTE_TS |
			FE_CAN_2G_MODULATION |
			FE_CAN_MULTISTREAM
	},

	.get_tune_settings = si2168_get_tune_settings,

	.init = si2168_init,
	.sleep = si2168_sleep,

	.set_frontend = si2168_set_frontend,

	.read_status = si2168_read_status,
};

static int si2168_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	struct si2168_config *config = client->dev.platform_data;
	struct si2168_dev *dev;
	int ret;
	struct si2168_cmd cmd;

	dev_dbg(&client->dev, "\n");

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev) {
		ret = -ENOMEM;
		goto err;
	}

	i2c_set_clientdata(client, dev);
	mutex_init(&dev->i2c_mutex);

	/* Initialize */
	memcpy(cmd.args, "\xc0\x12\x00\x0c\x00\x0d\x16\x00\x00\x00\x00\x00\x00", 13);
	cmd.wlen = 13;
	cmd.rlen = 0;
	ret = si2168_cmd_execute(client, &cmd);
	if (ret)
		goto err_kfree;

	/* Power up */
	memcpy(cmd.args, "\xc0\x06\x01\x0f\x00\x20\x20\x01", 8);
	cmd.wlen = 8;
	cmd.rlen = 1;
	ret = si2168_cmd_execute(client, &cmd);
	if (ret)
		goto err_kfree;

	/* Query chip revision */
	memcpy(cmd.args, "\x02", 1);
	cmd.wlen = 1;
	cmd.rlen = 13;
	ret = si2168_cmd_execute(client, &cmd);
	if (ret)
		goto err_kfree;

	dev->chip_id = cmd.args[1] << 24 | cmd.args[2] << 16 |
		       cmd.args[3] << 8 | cmd.args[4] << 0;

	switch (dev->chip_id) {
	case SI2168_CHIP_ID_A20:
		dev->firmware_name = SI2168_A20_FIRMWARE;
		break;
	case SI2168_CHIP_ID_A30:
		dev->firmware_name = SI2168_A30_FIRMWARE;
		break;
	case SI2168_CHIP_ID_B40:
		dev->firmware_name = SI2168_B40_FIRMWARE;
		break;
	case SI2168_CHIP_ID_D60:
		dev->firmware_name = SI2168_D60_FIRMWARE;
		break;
	default:
		dev_dbg(&client->dev, "unknown chip version Si21%d-%c%c%c\n",
			cmd.args[2], cmd.args[1], cmd.args[3], cmd.args[4]);
		ret = -ENODEV;
		goto err_kfree;
	}

	dev->version = (cmd.args[1]) << 24 | (cmd.args[3] - '0') << 16 |
		       (cmd.args[4] - '0') << 8 | (cmd.args[5]) << 0;

	/* create mux i2c adapter for tuner */
	dev->muxc = i2c_mux_alloc(client->adapter, &client->dev,
				  1, 0, I2C_MUX_LOCKED,
				  si2168_select, si2168_deselect);
	if (!dev->muxc) {
		ret = -ENOMEM;
		goto err_kfree;
	}
	dev->muxc->priv = client;
	ret = i2c_mux_add_adapter(dev->muxc, 0, 0, 0);
	if (ret)
		goto err_kfree;

	/* create dvb_frontend */
	memcpy(&dev->fe.ops, &si2168_ops, sizeof(struct dvb_frontend_ops));
	dev->fe.demodulator_priv = client;
	*config->i2c_adapter = dev->muxc->adapter[0];
	*config->fe = &dev->fe;
	dev->ts_mode = config->ts_mode;
	dev->ts_clock_inv = config->ts_clock_inv;
	dev->ts_clock_gapped = config->ts_clock_gapped;
	dev->spectral_inversion = config->spectral_inversion;

	dev_info(&client->dev, "Silicon Labs Si2168-%c%d%d successfully identified\n",
		 dev->version >> 24 & 0xff, dev->version >> 16 & 0xff,
		 dev->version >> 8 & 0xff);
	dev_info(&client->dev, "firmware version: %c %d.%d.%d\n",
		 dev->version >> 24 & 0xff, dev->version >> 16 & 0xff,
		 dev->version >> 8 & 0xff, dev->version >> 0 & 0xff);

	return 0;
err_kfree:
	kfree(dev);
err:
	dev_warn(&client->dev, "probe failed = %d\n", ret);
	return ret;
}

static int si2168_remove(struct i2c_client *client)
{
	struct si2168_dev *dev = i2c_get_clientdata(client);

	dev_dbg(&client->dev, "\n");

	i2c_mux_del_adapters(dev->muxc);

	dev->fe.ops.release = NULL;
	dev->fe.demodulator_priv = NULL;

	kfree(dev);

	return 0;
}

static const struct i2c_device_id si2168_id_table[] = {
	{"si2168", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, si2168_id_table);

static struct i2c_driver si2168_driver = {
	.driver = {
		.name                = "si2168",
		.suppress_bind_attrs = true,
	},
	.probe		= si2168_probe,
	.remove		= si2168_remove,
	.id_table	= si2168_id_table,
};

module_i2c_driver(si2168_driver);

MODULE_AUTHOR("Antti Palosaari <crope@iki.fi>");
MODULE_DESCRIPTION("Silicon Labs Si2168 DVB-T/T2/C demodulator driver");
MODULE_LICENSE("GPL");
MODULE_FIRMWARE(SI2168_A20_FIRMWARE);
MODULE_FIRMWARE(SI2168_A30_FIRMWARE);
MODULE_FIRMWARE(SI2168_B40_FIRMWARE);
MODULE_FIRMWARE(SI2168_D60_FIRMWARE);
