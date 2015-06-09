/*
 * Panasonic MN88473 DVB-T/T2/C demodulator driver
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

#include "mn88473_priv.h"

static int mn88473_get_tune_settings(struct dvb_frontend *fe,
				     struct dvb_frontend_tune_settings *s)
{
	s->min_delay_ms = 1000;
	return 0;
}

static int mn88473_set_frontend(struct dvb_frontend *fe)
{
	struct i2c_client *client = fe->demodulator_priv;
	struct mn88473_dev *dev = i2c_get_clientdata(client);
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	int ret, i;
	u32 if_frequency;
	u64 tmp;
	u8 delivery_system_val, if_val[3], bw_val[7];

	dev_dbg(&client->dev,
		"delivery_system=%u modulation=%u frequency=%u bandwidth_hz=%u symbol_rate=%u inversion=%d stream_id=%d\n",
		c->delivery_system,
		c->modulation,
		c->frequency,
		c->bandwidth_hz,
		c->symbol_rate,
		c->inversion,
		c->stream_id);

	if (!dev->warm) {
		ret = -EAGAIN;
		goto err;
	}

	switch (c->delivery_system) {
	case SYS_DVBT:
		delivery_system_val = 0x02;
		break;
	case SYS_DVBT2:
		delivery_system_val = 0x03;
		break;
	case SYS_DVBC_ANNEX_A:
		delivery_system_val = 0x04;
		break;
	default:
		ret = -EINVAL;
		goto err;
	}

	if (c->bandwidth_hz <= 6000000) {
		memcpy(bw_val, "\xe9\x55\x55\x1c\x29\x1c\x29", 7);
	} else if (c->bandwidth_hz <= 7000000) {
		memcpy(bw_val, "\xc8\x00\x00\x17\x0a\x17\x0a", 7);
	} else if (c->bandwidth_hz <= 8000000) {
		memcpy(bw_val, "\xaf\x00\x00\x11\xec\x11\xec", 7);
	} else {
		ret = -EINVAL;
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
	} else {
		if_frequency = 0;
	}

	/* Calculate IF registers ( (1<<24)*IF / Xtal ) */
	tmp =  div_u64(if_frequency * (u64)(1<<24) + (dev->xtal / 2),
				   dev->xtal);
	if_val[0] = ((tmp >> 16) & 0xff);
	if_val[1] = ((tmp >>  8) & 0xff);
	if_val[2] = ((tmp >>  0) & 0xff);

	ret = regmap_write(dev->regmap[2], 0x05, 0x00);
	ret = regmap_write(dev->regmap[2], 0xfb, 0x13);
	ret = regmap_write(dev->regmap[2], 0xef, 0x13);
	ret = regmap_write(dev->regmap[2], 0xf9, 0x13);
	ret = regmap_write(dev->regmap[2], 0x00, 0x18);
	ret = regmap_write(dev->regmap[2], 0x01, 0x01);
	ret = regmap_write(dev->regmap[2], 0x02, 0x21);
	ret = regmap_write(dev->regmap[2], 0x03, delivery_system_val);
	ret = regmap_write(dev->regmap[2], 0x0b, 0x00);

	for (i = 0; i < sizeof(if_val); i++) {
		ret = regmap_write(dev->regmap[2], 0x10 + i, if_val[i]);
		if (ret)
			goto err;
	}

	for (i = 0; i < sizeof(bw_val); i++) {
		ret = regmap_write(dev->regmap[2], 0x13 + i, bw_val[i]);
		if (ret)
			goto err;
	}

	ret = regmap_write(dev->regmap[2], 0x2d, 0x3b);
	ret = regmap_write(dev->regmap[2], 0x2e, 0x00);
	ret = regmap_write(dev->regmap[2], 0x56, 0x0d);
	ret = regmap_write(dev->regmap[0], 0x01, 0xba);
	ret = regmap_write(dev->regmap[0], 0x02, 0x13);
	ret = regmap_write(dev->regmap[0], 0x03, 0x80);
	ret = regmap_write(dev->regmap[0], 0x04, 0xba);
	ret = regmap_write(dev->regmap[0], 0x05, 0x91);
	ret = regmap_write(dev->regmap[0], 0x07, 0xe7);
	ret = regmap_write(dev->regmap[0], 0x08, 0x28);
	ret = regmap_write(dev->regmap[0], 0x0a, 0x1a);
	ret = regmap_write(dev->regmap[0], 0x13, 0x1f);
	ret = regmap_write(dev->regmap[0], 0x19, 0x03);
	ret = regmap_write(dev->regmap[0], 0x1d, 0xb0);
	ret = regmap_write(dev->regmap[0], 0x2a, 0x72);
	ret = regmap_write(dev->regmap[0], 0x2d, 0x00);
	ret = regmap_write(dev->regmap[0], 0x3c, 0x00);
	ret = regmap_write(dev->regmap[0], 0x3f, 0xf8);
	ret = regmap_write(dev->regmap[0], 0x40, 0xf4);
	ret = regmap_write(dev->regmap[0], 0x41, 0x08);
	ret = regmap_write(dev->regmap[0], 0xd2, 0x29);
	ret = regmap_write(dev->regmap[0], 0xd4, 0x55);
	ret = regmap_write(dev->regmap[1], 0x10, 0x10);
	ret = regmap_write(dev->regmap[1], 0x11, 0xab);
	ret = regmap_write(dev->regmap[1], 0x12, 0x0d);
	ret = regmap_write(dev->regmap[1], 0x13, 0xae);
	ret = regmap_write(dev->regmap[1], 0x14, 0x1d);
	ret = regmap_write(dev->regmap[1], 0x15, 0x9d);
	ret = regmap_write(dev->regmap[1], 0xbe, 0x08);
	ret = regmap_write(dev->regmap[2], 0x09, 0x08);
	ret = regmap_write(dev->regmap[2], 0x08, 0x1d);
	ret = regmap_write(dev->regmap[0], 0xb2, 0x37);
	ret = regmap_write(dev->regmap[0], 0xd7, 0x04);
	ret = regmap_write(dev->regmap[2], 0x32, 0x80);
	ret = regmap_write(dev->regmap[2], 0x36, 0x00);
	ret = regmap_write(dev->regmap[2], 0xf8, 0x9f);
	if (ret)
		goto err;

	dev->delivery_system = c->delivery_system;

	return 0;
err:
	dev_dbg(&client->dev, "failed=%d\n", ret);
	return ret;
}

static int mn88473_read_status(struct dvb_frontend *fe, fe_status_t *status)
{
	struct i2c_client *client = fe->demodulator_priv;
	struct mn88473_dev *dev = i2c_get_clientdata(client);
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	int ret;
	unsigned int utmp;
	int lock = 0;

	*status = 0;

	if (!dev->warm) {
		ret = -EAGAIN;
		goto err;
	}

	switch (c->delivery_system) {
	case SYS_DVBT:
		ret = regmap_read(dev->regmap[0], 0x62, &utmp);
		if (ret)
			goto err;
		if (!(utmp & 0xA0)) {
			if ((utmp & 0xF) >= 0x03)
				*status |= FE_HAS_SIGNAL;
			if ((utmp & 0xF) >= 0x09)
				lock = 1;
		}
		break;
	case SYS_DVBT2:
		ret = regmap_read(dev->regmap[2], 0x8B, &utmp);
		if (ret)
			goto err;
		if (!(utmp & 0x40)) {
			if ((utmp & 0xF) >= 0x07)
				*status |= FE_HAS_SIGNAL;
			if ((utmp & 0xF) >= 0x0a)
				*status |= FE_HAS_CARRIER;
			if ((utmp & 0xF) >= 0x0d)
				*status |= FE_HAS_VITERBI | FE_HAS_SYNC | FE_HAS_LOCK;
		}
		break;
	case SYS_DVBC_ANNEX_A:
		ret = regmap_read(dev->regmap[1], 0x85, &utmp);
		if (ret)
			goto err;
		if (!(utmp & 0x40)) {
			ret = regmap_read(dev->regmap[1], 0x89, &utmp);
			if (ret)
				goto err;
			if (utmp & 0x01)
				lock = 1;
		}
		break;
	default:
		ret = -EINVAL;
		goto err;
	}

	if (lock)
		*status = FE_HAS_SIGNAL | FE_HAS_CARRIER | FE_HAS_VITERBI |
				FE_HAS_SYNC | FE_HAS_LOCK;

	return 0;
err:
	dev_dbg(&client->dev, "failed=%d\n", ret);
	return ret;
}

static int mn88473_init(struct dvb_frontend *fe)
{
	struct i2c_client *client = fe->demodulator_priv;
	struct mn88473_dev *dev = i2c_get_clientdata(client);
	int ret, len, remaining;
	const struct firmware *fw = NULL;
	u8 *fw_file = MN88473_FIRMWARE;
	unsigned int tmp;

	dev_dbg(&client->dev, "\n");

	/* set cold state by default */
	dev->warm = false;

	/* check if firmware is already running */
	ret = regmap_read(dev->regmap[0], 0xf5, &tmp);
	if (ret)
		goto err;

	if (!(tmp & 0x1)) {
		dev_info(&client->dev, "firmware already running\n");
		dev->warm = true;
		return 0;
	}

	/* request the firmware, this will block and timeout */
	ret = request_firmware(&fw, fw_file, &client->dev);
	if (ret) {
		dev_err(&client->dev, "firmare file '%s' not found\n", fw_file);
		goto err_request_firmware;
	}

	dev_info(&client->dev, "downloading firmware from file '%s'\n",
		 fw_file);

	ret = regmap_write(dev->regmap[0], 0xf5, 0x03);
	if (ret)
		goto err;

	for (remaining = fw->size; remaining > 0;
			remaining -= (dev->i2c_wr_max - 1)) {
		len = remaining;
		if (len > (dev->i2c_wr_max - 1))
			len = dev->i2c_wr_max - 1;

		ret = regmap_bulk_write(dev->regmap[0], 0xf6,
					&fw->data[fw->size - remaining], len);
		if (ret) {
			dev_err(&client->dev, "firmware download failed=%d\n",
				ret);
			goto err;
		}
	}

	/* parity check of firmware */
	ret = regmap_read(dev->regmap[0], 0xf8, &tmp);
	if (ret) {
		dev_err(&client->dev,
				"parity reg read failed=%d\n", ret);
		goto err;
	}
	if (tmp & 0x10) {
		dev_err(&client->dev,
				"firmware parity check failed=0x%x\n", tmp);
		goto err;
	}
	dev_err(&client->dev, "firmware parity check succeeded=0x%x\n", tmp);

	ret = regmap_write(dev->regmap[0], 0xf5, 0x00);
	if (ret)
		goto err;

	release_firmware(fw);
	fw = NULL;

	/* warm state */
	dev->warm = true;

	return 0;

err:
	release_firmware(fw);
err_request_firmware:
	dev_dbg(&client->dev, "failed=%d\n", ret);
	return ret;
}

static int mn88473_sleep(struct dvb_frontend *fe)
{
	struct i2c_client *client = fe->demodulator_priv;
	struct mn88473_dev *dev = i2c_get_clientdata(client);
	int ret;

	dev_dbg(&client->dev, "\n");

	ret = regmap_write(dev->regmap[2], 0x05, 0x3e);
	if (ret)
		goto err;

	dev->delivery_system = SYS_UNDEFINED;

	return 0;
err:
	dev_dbg(&client->dev, "failed=%d\n", ret);
	return ret;
}

static struct dvb_frontend_ops mn88473_ops = {
	.delsys = {SYS_DVBT, SYS_DVBT2, SYS_DVBC_ANNEX_AC},
	.info = {
		.name = "Panasonic MN88473",
		.symbol_rate_min = 1000000,
		.symbol_rate_max = 7200000,
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

	.get_tune_settings = mn88473_get_tune_settings,

	.init = mn88473_init,
	.sleep = mn88473_sleep,

	.set_frontend = mn88473_set_frontend,

	.read_status = mn88473_read_status,
};

static int mn88473_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct mn88473_config *config = client->dev.platform_data;
	struct mn88473_dev *dev;
	int ret;
	unsigned int utmp;
	static const struct regmap_config regmap_config = {
		.reg_bits = 8,
		.val_bits = 8,
	};

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

	dev->i2c_wr_max = config->i2c_wr_max;
	if (!config->xtal)
		dev->xtal = 25000000;
	else
		dev->xtal = config->xtal;
	dev->client[0] = client;
	dev->regmap[0] = regmap_init_i2c(dev->client[0], &regmap_config);
	if (IS_ERR(dev->regmap[0])) {
		ret = PTR_ERR(dev->regmap[0]);
		goto err_kfree;
	}

	/* check demod answers to I2C */
	ret = regmap_read(dev->regmap[0], 0x00, &utmp);
	if (ret)
		goto err_regmap_0_regmap_exit;

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
			goto err_regmap_0_regmap_exit;
	}
	dev->regmap[1] = regmap_init_i2c(dev->client[1], &regmap_config);
	if (IS_ERR(dev->regmap[1])) {
		ret = PTR_ERR(dev->regmap[1]);
		goto err_client_1_i2c_unregister_device;
	}
	i2c_set_clientdata(dev->client[1], dev);

	dev->client[2] = i2c_new_dummy(client->adapter, 0x1c);
	if (dev->client[2] == NULL) {
		ret = -ENODEV;
		dev_err(&client->dev, "2nd I2C registration failed\n");
		if (ret)
			goto err_regmap_1_regmap_exit;
	}
	dev->regmap[2] = regmap_init_i2c(dev->client[2], &regmap_config);
	if (IS_ERR(dev->regmap[2])) {
		ret = PTR_ERR(dev->regmap[2]);
		goto err_client_2_i2c_unregister_device;
	}
	i2c_set_clientdata(dev->client[2], dev);

	/* create dvb_frontend */
	memcpy(&dev->fe.ops, &mn88473_ops, sizeof(struct dvb_frontend_ops));
	dev->fe.demodulator_priv = client;
	*config->fe = &dev->fe;
	i2c_set_clientdata(client, dev);

	dev_info(&dev->client[0]->dev, "Panasonic MN88473 successfully attached\n");
	return 0;

err_client_2_i2c_unregister_device:
	i2c_unregister_device(dev->client[2]);
err_regmap_1_regmap_exit:
	regmap_exit(dev->regmap[1]);
err_client_1_i2c_unregister_device:
	i2c_unregister_device(dev->client[1]);
err_regmap_0_regmap_exit:
	regmap_exit(dev->regmap[0]);
err_kfree:
	kfree(dev);
err:
	dev_dbg(&client->dev, "failed=%d\n", ret);
	return ret;
}

static int mn88473_remove(struct i2c_client *client)
{
	struct mn88473_dev *dev = i2c_get_clientdata(client);

	dev_dbg(&client->dev, "\n");

	regmap_exit(dev->regmap[2]);
	i2c_unregister_device(dev->client[2]);

	regmap_exit(dev->regmap[1]);
	i2c_unregister_device(dev->client[1]);

	regmap_exit(dev->regmap[0]);

	kfree(dev);

	return 0;
}

static const struct i2c_device_id mn88473_id_table[] = {
	{"mn88473", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, mn88473_id_table);

static struct i2c_driver mn88473_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= "mn88473",
	},
	.probe		= mn88473_probe,
	.remove		= mn88473_remove,
	.id_table	= mn88473_id_table,
};

module_i2c_driver(mn88473_driver);

MODULE_AUTHOR("Antti Palosaari <crope@iki.fi>");
MODULE_DESCRIPTION("Panasonic MN88473 DVB-T/T2/C demodulator driver");
MODULE_LICENSE("GPL");
MODULE_FIRMWARE(MN88473_FIRMWARE);
