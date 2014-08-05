/*
 * Silicon Labs Si2168 DVB-T/T2/C demodulator driver
 *
 * Copyright (C) 2014 Antti Palosaari <crope@iki.fi>
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

#include "si2168_priv.h"

static const struct dvb_frontend_ops si2168_ops;

/* execute firmware command */
static int si2168_cmd_execute(struct si2168 *s, struct si2168_cmd *cmd)
{
	int ret;
	unsigned long timeout;

	mutex_lock(&s->i2c_mutex);

	if (cmd->wlen) {
		/* write cmd and args for firmware */
		ret = i2c_master_send(s->client, cmd->args, cmd->wlen);
		if (ret < 0) {
			goto err_mutex_unlock;
		} else if (ret != cmd->wlen) {
			ret = -EREMOTEIO;
			goto err_mutex_unlock;
		}
	}

	if (cmd->rlen) {
		/* wait cmd execution terminate */
		#define TIMEOUT 50
		timeout = jiffies + msecs_to_jiffies(TIMEOUT);
		while (!time_after(jiffies, timeout)) {
			ret = i2c_master_recv(s->client, cmd->args, cmd->rlen);
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

		dev_dbg(&s->client->dev, "%s: cmd execution took %d ms\n",
				__func__,
				jiffies_to_msecs(jiffies) -
				(jiffies_to_msecs(timeout) - TIMEOUT));

		if (!((cmd->args[0] >> 7) & 0x01)) {
			ret = -ETIMEDOUT;
			goto err_mutex_unlock;
		}
	}

	ret = 0;

err_mutex_unlock:
	mutex_unlock(&s->i2c_mutex);
	if (ret)
		goto err;

	return 0;
err:
	dev_dbg(&s->client->dev, "%s: failed=%d\n", __func__, ret);
	return ret;
}

static int si2168_read_status(struct dvb_frontend *fe, fe_status_t *status)
{
	struct si2168 *s = fe->demodulator_priv;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	int ret;
	struct si2168_cmd cmd;

	*status = 0;

	if (!s->active) {
		ret = -EAGAIN;
		goto err;
	}

	switch (c->delivery_system) {
	case SYS_DVBT:
		cmd.args[0] = 0xa0;
		cmd.args[1] = 0x01;
		cmd.wlen = 2;
		cmd.rlen = 13;
		break;
	case SYS_DVBC_ANNEX_A:
		cmd.args[0] = 0x90;
		cmd.args[1] = 0x01;
		cmd.wlen = 2;
		cmd.rlen = 9;
		break;
	case SYS_DVBT2:
		cmd.args[0] = 0x50;
		cmd.args[1] = 0x01;
		cmd.wlen = 2;
		cmd.rlen = 14;
		break;
	default:
		ret = -EINVAL;
		goto err;
	}

	ret = si2168_cmd_execute(s, &cmd);
	if (ret)
		goto err;

	/*
	 * Possible values seen, in order from strong signal to weak:
	 * 16 0001 0110 full lock
	 * 1e 0001 1110 partial lock
	 * 1a 0001 1010 partial lock
	 * 18 0001 1000 no lock
	 *
	 * [b3:b1] lock bits
	 * [b4] statistics ready? Set in a few secs after lock is gained.
	 */

	switch ((cmd.args[2] >> 1) & 0x03) {
	case 0x01:
		*status = FE_HAS_SIGNAL | FE_HAS_CARRIER;
		break;
	case 0x03:
		*status = FE_HAS_SIGNAL | FE_HAS_CARRIER | FE_HAS_VITERBI |
				FE_HAS_SYNC | FE_HAS_LOCK;
		break;
	}

	s->fe_status = *status;

	dev_dbg(&s->client->dev, "%s: status=%02x args=%*ph\n",
			__func__, *status, cmd.rlen, cmd.args);

	return 0;
err:
	dev_dbg(&s->client->dev, "%s: failed=%d\n", __func__, ret);
	return ret;
}

static int si2168_set_frontend(struct dvb_frontend *fe)
{
	struct si2168 *s = fe->demodulator_priv;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	int ret;
	struct si2168_cmd cmd;
	u8 bandwidth, delivery_system;

	dev_dbg(&s->client->dev,
			"%s: delivery_system=%u modulation=%u frequency=%u bandwidth_hz=%u symbol_rate=%u inversion=%u\n",
			__func__, c->delivery_system, c->modulation,
			c->frequency, c->bandwidth_hz, c->symbol_rate,
			c->inversion);

	if (!s->active) {
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

	if (c->bandwidth_hz <= 5000000)
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
	ret = si2168_cmd_execute(s, &cmd);
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
	ret = si2168_cmd_execute(s, &cmd);
	if (ret)
		goto err;

	memcpy(cmd.args, "\x51\x03", 2);
	cmd.wlen = 2;
	cmd.rlen = 12;
	ret = si2168_cmd_execute(s, &cmd);
	if (ret)
		goto err;

	memcpy(cmd.args, "\x12\x08\x04", 3);
	cmd.wlen = 3;
	cmd.rlen = 3;
	ret = si2168_cmd_execute(s, &cmd);
	if (ret)
		goto err;

	memcpy(cmd.args, "\x14\x00\x01\x04\x00\x00", 6);
	cmd.wlen = 6;
	cmd.rlen = 1;
	ret = si2168_cmd_execute(s, &cmd);
	if (ret)
		goto err;

	memcpy(cmd.args, "\x14\x00\x03\x10\x17\x00", 6);
	cmd.wlen = 6;
	cmd.rlen = 1;
	ret = si2168_cmd_execute(s, &cmd);
	if (ret)
		goto err;

	memcpy(cmd.args, "\x14\x00\x02\x10\x15\x00", 6);
	cmd.wlen = 6;
	cmd.rlen = 1;
	ret = si2168_cmd_execute(s, &cmd);
	if (ret)
		goto err;

	memcpy(cmd.args, "\x14\x00\x0c\x10\x12\x00", 6);
	cmd.wlen = 6;
	cmd.rlen = 1;
	ret = si2168_cmd_execute(s, &cmd);
	if (ret)
		goto err;

	memcpy(cmd.args, "\x14\x00\x06\x10\x24\x00", 6);
	cmd.wlen = 6;
	cmd.rlen = 1;
	ret = si2168_cmd_execute(s, &cmd);
	if (ret)
		goto err;

	memcpy(cmd.args, "\x14\x00\x0b\x10\x88\x13", 6);
	cmd.wlen = 6;
	cmd.rlen = 1;
	ret = si2168_cmd_execute(s, &cmd);
	if (ret)
		goto err;

	memcpy(cmd.args, "\x14\x00\x07\x10\x00\x24", 6);
	cmd.wlen = 6;
	cmd.rlen = 1;
	ret = si2168_cmd_execute(s, &cmd);
	if (ret)
		goto err;

	memcpy(cmd.args, "\x14\x00\x0a\x10\x00\x00", 6);
	cmd.args[4] = delivery_system | bandwidth;
	cmd.wlen = 6;
	cmd.rlen = 1;
	ret = si2168_cmd_execute(s, &cmd);
	if (ret)
		goto err;

	memcpy(cmd.args, "\x14\x00\x04\x10\x15\x00", 6);
	cmd.wlen = 6;
	cmd.rlen = 1;
	ret = si2168_cmd_execute(s, &cmd);
	if (ret)
		goto err;

	memcpy(cmd.args, "\x14\x00\x05\x10\xa1\x00", 6);
	cmd.wlen = 6;
	cmd.rlen = 1;
	ret = si2168_cmd_execute(s, &cmd);
	if (ret)
		goto err;

	memcpy(cmd.args, "\x14\x00\x0f\x10\x10\x00", 6);
	cmd.wlen = 6;
	cmd.rlen = 1;
	ret = si2168_cmd_execute(s, &cmd);
	if (ret)
		goto err;

	memcpy(cmd.args, "\x14\x00\x0d\x10\xd0\x02", 6);
	cmd.wlen = 6;
	cmd.rlen = 1;
	ret = si2168_cmd_execute(s, &cmd);
	if (ret)
		goto err;

	memcpy(cmd.args, "\x14\x00\x01\x10\x00\x00", 6);
	cmd.wlen = 6;
	cmd.rlen = 1;
	ret = si2168_cmd_execute(s, &cmd);
	if (ret)
		goto err;

	memcpy(cmd.args, "\x14\x00\x09\x10\xe3\x18", 6);
	cmd.wlen = 6;
	cmd.rlen = 1;
	ret = si2168_cmd_execute(s, &cmd);
	if (ret)
		goto err;

	memcpy(cmd.args, "\x14\x00\x08\x10\xd7\x15", 6);
	cmd.wlen = 6;
	cmd.rlen = 1;
	ret = si2168_cmd_execute(s, &cmd);
	if (ret)
		goto err;

	memcpy(cmd.args, "\x14\x00\x04\x03\x00\x00", 6);
	cmd.wlen = 6;
	cmd.rlen = 1;
	ret = si2168_cmd_execute(s, &cmd);
	if (ret)
		goto err;

	memcpy(cmd.args, "\x14\x00\x03\x03\x00\x00", 6);
	cmd.wlen = 6;
	cmd.rlen = 1;
	ret = si2168_cmd_execute(s, &cmd);
	if (ret)
		goto err;

	memcpy(cmd.args, "\x14\x00\x08\x03\x00\x00", 6);
	cmd.wlen = 6;
	cmd.rlen = 1;
	ret = si2168_cmd_execute(s, &cmd);
	if (ret)
		goto err;

	memcpy(cmd.args, "\x14\x00\x07\x03\x01\x02", 6);
	cmd.wlen = 6;
	cmd.rlen = 1;
	ret = si2168_cmd_execute(s, &cmd);
	if (ret)
		goto err;

	memcpy(cmd.args, "\x14\x00\x06\x03\x00\x00", 6);
	cmd.wlen = 6;
	cmd.rlen = 1;
	ret = si2168_cmd_execute(s, &cmd);
	if (ret)
		goto err;

	memcpy(cmd.args, "\x14\x00\x05\x03\x00\x00", 6);
	cmd.wlen = 6;
	cmd.rlen = 1;
	ret = si2168_cmd_execute(s, &cmd);
	if (ret)
		goto err;

	memcpy(cmd.args, "\x14\x00\x01\x03\x0c\x40", 6);
	cmd.wlen = 6;
	cmd.rlen = 1;
	ret = si2168_cmd_execute(s, &cmd);
	if (ret)
		goto err;

	memcpy(cmd.args, "\x14\x00\x01\x10\x16\x00", 6);
	cmd.wlen = 6;
	cmd.rlen = 1;
	ret = si2168_cmd_execute(s, &cmd);
	if (ret)
		goto err;

	memcpy(cmd.args, "\x14\x00\x01\x12\x00\x00", 6);
	cmd.wlen = 6;
	cmd.rlen = 1;
	ret = si2168_cmd_execute(s, &cmd);
	if (ret)
		goto err;

	cmd.args[0] = 0x85;
	cmd.wlen = 1;
	cmd.rlen = 1;
	ret = si2168_cmd_execute(s, &cmd);
	if (ret)
		goto err;

	s->delivery_system = c->delivery_system;

	return 0;
err:
	dev_dbg(&s->client->dev, "%s: failed=%d\n", __func__, ret);
	return ret;
}

static int si2168_init(struct dvb_frontend *fe)
{
	struct si2168 *s = fe->demodulator_priv;
	int ret, len, remaining;
	const struct firmware *fw = NULL;
	u8 *fw_file = SI2168_FIRMWARE;
	const unsigned int i2c_wr_max = 8;
	struct si2168_cmd cmd;

	dev_dbg(&s->client->dev, "%s:\n", __func__);

	cmd.args[0] = 0x13;
	cmd.wlen = 1;
	cmd.rlen = 0;
	ret = si2168_cmd_execute(s, &cmd);
	if (ret)
		goto err;

	cmd.args[0] = 0xc0;
	cmd.args[1] = 0x12;
	cmd.args[2] = 0x00;
	cmd.args[3] = 0x0c;
	cmd.args[4] = 0x00;
	cmd.args[5] = 0x0d;
	cmd.args[6] = 0x16;
	cmd.args[7] = 0x00;
	cmd.args[8] = 0x00;
	cmd.args[9] = 0x00;
	cmd.args[10] = 0x00;
	cmd.args[11] = 0x00;
	cmd.args[12] = 0x00;
	cmd.wlen = 13;
	cmd.rlen = 0;
	ret = si2168_cmd_execute(s, &cmd);
	if (ret)
		goto err;

	cmd.args[0] = 0xc0;
	cmd.args[1] = 0x06;
	cmd.args[2] = 0x01;
	cmd.args[3] = 0x0f;
	cmd.args[4] = 0x00;
	cmd.args[5] = 0x20;
	cmd.args[6] = 0x20;
	cmd.args[7] = 0x01;
	cmd.wlen = 8;
	cmd.rlen = 1;
	ret = si2168_cmd_execute(s, &cmd);
	if (ret)
		goto err;

	cmd.args[0] = 0x02;
	cmd.wlen = 1;
	cmd.rlen = 13;
	ret = si2168_cmd_execute(s, &cmd);
	if (ret)
		goto err;

	/* cold state - try to download firmware */
	dev_info(&s->client->dev, "%s: found a '%s' in cold state\n",
			KBUILD_MODNAME, si2168_ops.info.name);

	/* request the firmware, this will block and timeout */
	ret = request_firmware(&fw, fw_file, &s->client->dev);
	if (ret) {
		dev_err(&s->client->dev, "%s: firmare file '%s' not found\n",
				KBUILD_MODNAME, fw_file);
		goto err;
	}

	dev_info(&s->client->dev, "%s: downloading firmware from file '%s'\n",
			KBUILD_MODNAME, fw_file);

	for (remaining = fw->size; remaining > 0; remaining -= i2c_wr_max) {
		len = remaining;
		if (len > i2c_wr_max)
			len = i2c_wr_max;

		memcpy(cmd.args, &fw->data[fw->size - remaining], len);
		cmd.wlen = len;
		cmd.rlen = 1;
		ret = si2168_cmd_execute(s, &cmd);
		if (ret) {
			dev_err(&s->client->dev,
					"%s: firmware download failed=%d\n",
					KBUILD_MODNAME, ret);
			goto err;
		}
	}

	release_firmware(fw);
	fw = NULL;

	cmd.args[0] = 0x01;
	cmd.args[1] = 0x01;
	cmd.wlen = 2;
	cmd.rlen = 1;
	ret = si2168_cmd_execute(s, &cmd);
	if (ret)
		goto err;

	dev_info(&s->client->dev, "%s: found a '%s' in warm state\n",
			KBUILD_MODNAME, si2168_ops.info.name);

	s->active = true;

	return 0;
err:
	if (fw)
		release_firmware(fw);

	dev_dbg(&s->client->dev, "%s: failed=%d\n", __func__, ret);
	return ret;
}

static int si2168_sleep(struct dvb_frontend *fe)
{
	struct si2168 *s = fe->demodulator_priv;

	dev_dbg(&s->client->dev, "%s:\n", __func__);

	s->active = false;

	return 0;
}

static int si2168_get_tune_settings(struct dvb_frontend *fe,
	struct dvb_frontend_tune_settings *s)
{
	s->min_delay_ms = 900;

	return 0;
}

/*
 * I2C gate logic
 * We must use unlocked i2c_transfer() here because I2C lock is already taken
 * by tuner driver.
 */
static int si2168_select(struct i2c_adapter *adap, void *mux_priv, u32 chan)
{
	struct si2168 *s = mux_priv;
	int ret;
	struct i2c_msg gate_open_msg = {
		.addr = s->client->addr,
		.flags = 0,
		.len = 3,
		.buf = "\xc0\x0d\x01",
	};

	mutex_lock(&s->i2c_mutex);

	/* open tuner I2C gate */
	ret = __i2c_transfer(s->client->adapter, &gate_open_msg, 1);
	if (ret != 1) {
		dev_warn(&s->client->dev, "%s: i2c write failed=%d\n",
				KBUILD_MODNAME, ret);
		if (ret >= 0)
			ret = -EREMOTEIO;
	} else {
		ret = 0;
	}

	return ret;
}

static int si2168_deselect(struct i2c_adapter *adap, void *mux_priv, u32 chan)
{
	struct si2168 *s = mux_priv;
	int ret;
	struct i2c_msg gate_close_msg = {
		.addr = s->client->addr,
		.flags = 0,
		.len = 3,
		.buf = "\xc0\x0d\x00",
	};

	/* close tuner I2C gate */
	ret = __i2c_transfer(s->client->adapter, &gate_close_msg, 1);
	if (ret != 1) {
		dev_warn(&s->client->dev, "%s: i2c write failed=%d\n",
				KBUILD_MODNAME, ret);
		if (ret >= 0)
			ret = -EREMOTEIO;
	} else {
		ret = 0;
	}

	mutex_unlock(&s->i2c_mutex);

	return ret;
}

static const struct dvb_frontend_ops si2168_ops = {
	.delsys = {SYS_DVBT, SYS_DVBT2, SYS_DVBC_ANNEX_A},
	.info = {
		.name = "Silicon Labs Si2168",
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
			FE_CAN_2G_MODULATION
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
	struct si2168 *s;
	int ret;
	struct si2168_cmd cmd;

	dev_dbg(&client->dev, "%s:\n", __func__);

	s = kzalloc(sizeof(struct si2168), GFP_KERNEL);
	if (!s) {
		ret = -ENOMEM;
		dev_err(&client->dev, "%s: kzalloc() failed\n", KBUILD_MODNAME);
		goto err;
	}

	s->client = client;
	mutex_init(&s->i2c_mutex);

	/* check if the demod is there */
	cmd.wlen = 0;
	cmd.rlen = 1;
	ret = si2168_cmd_execute(s, &cmd);
	if (ret)
		goto err;

	/* create mux i2c adapter for tuner */
	s->adapter = i2c_add_mux_adapter(client->adapter, &client->dev, s,
			0, 0, 0, si2168_select, si2168_deselect);
	if (s->adapter == NULL)
		goto err;

	/* create dvb_frontend */
	memcpy(&s->fe.ops, &si2168_ops, sizeof(struct dvb_frontend_ops));
	s->fe.demodulator_priv = s;

	*config->i2c_adapter = s->adapter;
	*config->fe = &s->fe;

	i2c_set_clientdata(client, s);

	dev_info(&s->client->dev,
			"%s: Silicon Labs Si2168 successfully attached\n",
			KBUILD_MODNAME);
	return 0;
err:
	kfree(s);
	dev_dbg(&client->dev, "%s: failed=%d\n", __func__, ret);
	return ret;
}

static int si2168_remove(struct i2c_client *client)
{
	struct si2168 *s = i2c_get_clientdata(client);

	dev_dbg(&client->dev, "%s:\n", __func__);

	i2c_del_mux_adapter(s->adapter);

	s->fe.ops.release = NULL;
	s->fe.demodulator_priv = NULL;

	kfree(s);

	return 0;
}

static const struct i2c_device_id si2168_id[] = {
	{"si2168", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, si2168_id);

static struct i2c_driver si2168_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= "si2168",
	},
	.probe		= si2168_probe,
	.remove		= si2168_remove,
	.id_table	= si2168_id,
};

module_i2c_driver(si2168_driver);

MODULE_AUTHOR("Antti Palosaari <crope@iki.fi>");
MODULE_DESCRIPTION("Silicon Labs Si2168 DVB-T/T2/C demodulator driver");
MODULE_LICENSE("GPL");
MODULE_FIRMWARE(SI2168_FIRMWARE);
