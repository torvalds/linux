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

static struct dvb_frontend_ops mn88472_ops;

/* write multiple registers */
static int mn88472_wregs(struct mn88472_dev *dev, u16 reg, const u8 *val, int len)
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

	ret = i2c_transfer(dev->client[0]->adapter, msg, 1);
	if (ret == 1) {
		ret = 0;
	} else {
		dev_warn(&dev->client[0]->dev,
				"i2c wr failed=%d reg=%02x len=%d\n",
				ret, reg, len);
		ret = -EREMOTEIO;
	}

	return ret;
}

/* read multiple registers */
static int mn88472_rregs(struct mn88472_dev *dev, u16 reg, u8 *val, int len)
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

	ret = i2c_transfer(dev->client[0]->adapter, msg, 2);
	if (ret == 2) {
		memcpy(val, buf, len);
		ret = 0;
	} else {
		dev_warn(&dev->client[0]->dev,
				"i2c rd failed=%d reg=%02x len=%d\n",
				ret, reg, len);
		ret = -EREMOTEIO;
	}

	return ret;
}

/* write single register */
static int mn88472_wreg(struct mn88472_dev *dev, u16 reg, u8 val)
{
	return mn88472_wregs(dev, reg, &val, 1);
}

/* read single register */
static int mn88472_rreg(struct mn88472_dev *dev, u16 reg, u8 *val)
{
	return mn88472_rregs(dev, reg, val, 1);
}

static int mn88472_get_tune_settings(struct dvb_frontend *fe,
	struct dvb_frontend_tune_settings *s)
{
	s->min_delay_ms = 400;
	return 0;
}

static int mn88472_set_frontend(struct dvb_frontend *fe)
{
	struct i2c_client *client = fe->demodulator_priv;
	struct mn88472_dev *dev = i2c_get_clientdata(client);
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	int ret;
	u32 if_frequency = 0;

	dev_dbg(&client->dev,
			"delivery_system=%d modulation=%d frequency=%d symbol_rate=%d inversion=%d\n",
			c->delivery_system, c->modulation,
			c->frequency, c->symbol_rate, c->inversion);

	if (!dev->warm) {
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

		dev_dbg(&client->dev, "get_if_frequency=%d\n", if_frequency);
	}

	if (if_frequency != 5070000) {
		dev_err(&client->dev, "IF frequency %d not supported\n",
				if_frequency);
		ret = -EINVAL;
		goto err;
	}

	ret = mn88472_wregs(dev, 0x1c08, "\x1d", 1);
	if (ret)
		goto err;

	ret = mn88472_wregs(dev, 0x18d9, "\xe3", 1);
	if (ret)
		goto err;

	ret = mn88472_wregs(dev, 0x1c83, "\x01", 1);
	if (ret)
		goto err;

	ret = mn88472_wregs(dev, 0x1c00, "\x66\x00\x01\x04\x00", 5);
	if (ret)
		goto err;

	ret = mn88472_wregs(dev, 0x1c10,
			"\x3f\x50\x2c\x8f\x80\x00\x08\xee\x08\xee", 10);
	if (ret)
		goto err;

	ret = mn88472_wregs(dev, 0x1846, "\x00", 1);
	if (ret)
		goto err;

	ret = mn88472_wregs(dev, 0x18ae, "\x00", 1);
	if (ret)
		goto err;

	ret = mn88472_wregs(dev, 0x18b0, "\x0b", 1);
	if (ret)
		goto err;

	ret = mn88472_wregs(dev, 0x18b4, "\x00", 1);
	if (ret)
		goto err;

	ret = mn88472_wregs(dev, 0x18cd, "\x17", 1);
	if (ret)
		goto err;

	ret = mn88472_wregs(dev, 0x18d4, "\x09", 1);
	if (ret)
		goto err;

	ret = mn88472_wregs(dev, 0x18d6, "\x48", 1);
	if (ret)
		goto err;

	ret = mn88472_wregs(dev, 0x1a00, "\xb0", 1);
	if (ret)
		goto err;

	ret = mn88472_wregs(dev, 0x1cf8, "\x9f", 1);
	if (ret)
		goto err;

	dev->delivery_system = c->delivery_system;

	return 0;
err:
	dev_dbg(&client->dev, "failed=%d\n", ret);
	return ret;
}

static int mn88472_read_status(struct dvb_frontend *fe, fe_status_t *status)
{
	struct i2c_client *client = fe->demodulator_priv;
	struct mn88472_dev *dev = i2c_get_clientdata(client);
	int ret;
	u8 u8tmp;

	*status = 0;

	if (!dev->warm) {
		ret = -EAGAIN;
		goto err;
	}

	ret = mn88472_rreg(dev, 0x1a84, &u8tmp);
	if (ret)
		goto err;

	if (u8tmp == 0x08)
		*status = FE_HAS_SIGNAL | FE_HAS_CARRIER | FE_HAS_VITERBI |
				FE_HAS_SYNC | FE_HAS_LOCK;

	return 0;
err:
	dev_dbg(&client->dev, "failed=%d\n", ret);
	return ret;
}

static int mn88472_init(struct dvb_frontend *fe)
{
	struct i2c_client *client = fe->demodulator_priv;
	struct mn88472_dev *dev = i2c_get_clientdata(client);
	int ret, len, remaining;
	const struct firmware *fw = NULL;
	u8 *fw_file = MN88472_FIRMWARE;

	dev_dbg(&client->dev, "\n");

	/* set cold state by default */
	dev->warm = false;

	/* power on */
	ret = mn88472_wreg(dev, 0x1c05, 0x00);
	if (ret)
		goto err;

	ret = mn88472_wregs(dev, 0x1c0b, "\x00\x00", 2);
	if (ret)
		goto err;

	/* request the firmware, this will block and timeout */
	ret = request_firmware(&fw, fw_file, &client->dev);
	if (ret) {
		dev_err(&client->dev, "firmare file '%s' not found\n",
				fw_file);
		goto err;
	}

	dev_info(&client->dev, "downloading firmware from file '%s'\n",
			fw_file);

	ret = mn88472_wreg(dev, 0x18f5, 0x03);
	if (ret)
		goto err;

	for (remaining = fw->size; remaining > 0;
			remaining -= (dev->i2c_wr_max - 1)) {
		len = remaining;
		if (len > (dev->i2c_wr_max - 1))
			len = (dev->i2c_wr_max - 1);

		ret = mn88472_wregs(dev, 0x18f6,
				&fw->data[fw->size - remaining], len);
		if (ret) {
			dev_err(&client->dev,
					"firmware download failed=%d\n", ret);
			goto err;
		}
	}

	ret = mn88472_wreg(dev, 0x18f5, 0x00);
	if (ret)
		goto err;

	release_firmware(fw);
	fw = NULL;

	/* warm state */
	dev->warm = true;

	return 0;
err:
	if (fw)
		release_firmware(fw);

	dev_dbg(&client->dev, "failed=%d\n", ret);
	return ret;
}

static int mn88472_sleep(struct dvb_frontend *fe)
{
	struct i2c_client *client = fe->demodulator_priv;
	struct mn88472_dev *dev = i2c_get_clientdata(client);
	int ret;

	dev_dbg(&client->dev, "\n");

	/* power off */
	ret = mn88472_wreg(dev, 0x1c0b, 0x30);
	if (ret)
		goto err;

	ret = mn88472_wreg(dev, 0x1c05, 0x3e);
	if (ret)
		goto err;

	dev->delivery_system = SYS_UNDEFINED;

	return 0;
err:
	dev_dbg(&client->dev, "failed=%d\n", ret);
	return ret;
}

static struct dvb_frontend_ops mn88472_ops = {
	.delsys = {SYS_DVBC_ANNEX_A},
	.info = {
		.name = "Panasonic MN88472",
		.caps =	FE_CAN_FEC_1_2                 |
			FE_CAN_FEC_2_3                 |
			FE_CAN_FEC_3_4                 |
			FE_CAN_FEC_5_6                 |
			FE_CAN_FEC_7_8                 |
			FE_CAN_FEC_AUTO                |
			FE_CAN_QPSK                    |
			FE_CAN_QAM_16                  |
			FE_CAN_QAM_32                  |
			FE_CAN_QAM_64                  |
			FE_CAN_QAM_128                 |
			FE_CAN_QAM_256                 |
			FE_CAN_QAM_AUTO                |
			FE_CAN_TRANSMISSION_MODE_AUTO  |
			FE_CAN_GUARD_INTERVAL_AUTO     |
			FE_CAN_HIERARCHY_AUTO          |
			FE_CAN_MUTE_TS                 |
			FE_CAN_2G_MODULATION           |
			FE_CAN_MULTISTREAM
	},

	.get_tune_settings = mn88472_get_tune_settings,

	.init = mn88472_init,
	.sleep = mn88472_sleep,

	.set_frontend = mn88472_set_frontend,

	.read_status = mn88472_read_status,
};

static int mn88472_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	struct mn88472_config *config = client->dev.platform_data;
	struct mn88472_dev *dev;
	int ret;
	u8 u8tmp;

	dev_dbg(&client->dev, "\n");

	/* Caller really need to provide pointer for frontend we create. */
	if (config->fe == NULL) {
		dev_err(&client->dev, "frontend pointer not defined\n");
		ret = -EINVAL;
		goto err;
	}

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (dev == NULL) {
		ret = -ENOMEM;
		goto err;
	}

	dev->client[0] = client;
	dev->i2c_wr_max = config->i2c_wr_max;

	/* check demod answers to I2C */
	ret = mn88472_rreg(dev, 0x1c00, &u8tmp);
	if (ret)
		goto err_kfree;

	/*
	 * Chip has three I2C addresses for different register pages. Used
	 * addresses are 0x18, 0x1a and 0x1c. We register two dummy clients,
	 * 0x1a and 0x1c, in order to get own I2C client for each register page.
	 */
	dev->client[1] = i2c_new_dummy(client->adapter, 0x1a);
	if (dev->client[1] == NULL) {
		ret = -ENODEV;
		dev_err(&client->dev, "I2C registration failed\n");
		if (ret)
			goto err_kfree;
	}
	i2c_set_clientdata(dev->client[1], dev);

	dev->client[2] = i2c_new_dummy(client->adapter, 0x1c);
	if (dev->client[2] == NULL) {
		ret = -ENODEV;
		dev_err(&client->dev, "2nd I2C registration failed\n");
		if (ret)
			goto err_client_1_i2c_unregister_device;
	}
	i2c_set_clientdata(dev->client[2], dev);

	/* create dvb_frontend */
	memcpy(&dev->fe.ops, &mn88472_ops, sizeof(struct dvb_frontend_ops));
	dev->fe.demodulator_priv = client;
	*config->fe = &dev->fe;
	i2c_set_clientdata(client, dev);

	dev_info(&client->dev, "Panasonic MN88472 successfully attached\n");
	return 0;

err_client_1_i2c_unregister_device:
	i2c_unregister_device(dev->client[1]);
err_kfree:
	kfree(dev);
err:
	dev_dbg(&client->dev, "failed=%d\n", ret);
	return ret;
}

static int mn88472_remove(struct i2c_client *client)
{
	struct mn88472_dev *dev = i2c_get_clientdata(client);

	dev_dbg(&client->dev, "\n");

	i2c_unregister_device(dev->client[2]);

	i2c_unregister_device(dev->client[1]);

	kfree(dev);

	return 0;
}

static const struct i2c_device_id mn88472_id_table[] = {
	{"mn88472", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, mn88472_id_table);

static struct i2c_driver mn88472_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= "mn88472",
	},
	.probe		= mn88472_probe,
	.remove		= mn88472_remove,
	.id_table	= mn88472_id_table,
};

module_i2c_driver(mn88472_driver);

MODULE_AUTHOR("Antti Palosaari <crope@iki.fi>");
MODULE_DESCRIPTION("Panasonic MN88472 DVB-T/T2/C demodulator driver");
MODULE_LICENSE("GPL");
MODULE_FIRMWARE(MN88472_FIRMWARE);
