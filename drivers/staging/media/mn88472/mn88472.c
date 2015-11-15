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

static int mn88472_get_tune_settings(struct dvb_frontend *fe,
	struct dvb_frontend_tune_settings *s)
{
	s->min_delay_ms = 800;
	return 0;
}

static int mn88472_set_frontend(struct dvb_frontend *fe)
{
	struct i2c_client *client = fe->demodulator_priv;
	struct mn88472_dev *dev = i2c_get_clientdata(client);
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	int ret, i;
	u32 if_frequency = 0;
	u64 tmp;
	u8 delivery_system_val, if_val[3], bw_val[7], bw_val2;

	dev_dbg(&client->dev,
			"delivery_system=%d modulation=%d frequency=%d symbol_rate=%d inversion=%d\n",
			c->delivery_system, c->modulation,
			c->frequency, c->symbol_rate, c->inversion);

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

	if (c->bandwidth_hz <= 5000000) {
		memcpy(bw_val, "\xe5\x99\x9a\x1b\xa9\x1b\xa9", 7);
		bw_val2 = 0x03;
	} else if (c->bandwidth_hz <= 6000000) {
		/* IF 3570000 Hz, BW 6000000 Hz */
		memcpy(bw_val, "\xbf\x55\x55\x15\x6b\x15\x6b", 7);
		bw_val2 = 0x02;
	} else if (c->bandwidth_hz <= 7000000) {
		/* IF 4570000 Hz, BW 7000000 Hz */
		memcpy(bw_val, "\xa4\x00\x00\x0f\x2c\x0f\x2c", 7);
		bw_val2 = 0x01;
	} else if (c->bandwidth_hz <= 8000000) {
		/* IF 4570000 Hz, BW 8000000 Hz */
		memcpy(bw_val, "\x8f\x80\x00\x08\xee\x08\xee", 7);
		bw_val2 = 0x00;
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
	}

	/* Calculate IF registers ( (1<<24)*IF / Xtal ) */
	tmp =  div_u64(if_frequency * (u64)(1<<24) + (dev->xtal / 2),
				   dev->xtal);
	if_val[0] = ((tmp >> 16) & 0xff);
	if_val[1] = ((tmp >>  8) & 0xff);
	if_val[2] = ((tmp >>  0) & 0xff);

	ret = regmap_write(dev->regmap[2], 0xfb, 0x13);
	ret = regmap_write(dev->regmap[2], 0xef, 0x13);
	ret = regmap_write(dev->regmap[2], 0xf9, 0x13);
	if (ret)
		goto err;

	ret = regmap_write(dev->regmap[2], 0x00, 0x66);
	if (ret)
		goto err;
	ret = regmap_write(dev->regmap[2], 0x01, 0x00);
	if (ret)
		goto err;
	ret = regmap_write(dev->regmap[2], 0x02, 0x01);
	if (ret)
		goto err;
	ret = regmap_write(dev->regmap[2], 0x03, delivery_system_val);
	if (ret)
		goto err;
	ret = regmap_write(dev->regmap[2], 0x04, bw_val2);
	if (ret)
		goto err;

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

	switch (c->delivery_system) {
	case SYS_DVBT:
		ret = regmap_write(dev->regmap[0], 0x07, 0x26);
		ret = regmap_write(dev->regmap[0], 0xb0, 0x0a);
		ret = regmap_write(dev->regmap[0], 0xb4, 0x00);
		ret = regmap_write(dev->regmap[0], 0xcd, 0x1f);
		ret = regmap_write(dev->regmap[0], 0xd4, 0x0a);
		ret = regmap_write(dev->regmap[0], 0xd6, 0x48);
		ret = regmap_write(dev->regmap[0], 0x00, 0xba);
		ret = regmap_write(dev->regmap[0], 0x01, 0x13);
		if (ret)
			goto err;
		break;
	case SYS_DVBT2:
		ret = regmap_write(dev->regmap[2], 0x2b, 0x13);
		ret = regmap_write(dev->regmap[2], 0x4f, 0x05);
		ret = regmap_write(dev->regmap[1], 0xf6, 0x05);
		ret = regmap_write(dev->regmap[0], 0xb0, 0x0a);
		ret = regmap_write(dev->regmap[0], 0xb4, 0xf6);
		ret = regmap_write(dev->regmap[0], 0xcd, 0x01);
		ret = regmap_write(dev->regmap[0], 0xd4, 0x09);
		ret = regmap_write(dev->regmap[0], 0xd6, 0x46);
		ret = regmap_write(dev->regmap[2], 0x30, 0x80);
		ret = regmap_write(dev->regmap[2], 0x32, 0x00);
		if (ret)
			goto err;
		break;
	case SYS_DVBC_ANNEX_A:
		ret = regmap_write(dev->regmap[0], 0xb0, 0x0b);
		ret = regmap_write(dev->regmap[0], 0xb4, 0x00);
		ret = regmap_write(dev->regmap[0], 0xcd, 0x17);
		ret = regmap_write(dev->regmap[0], 0xd4, 0x09);
		ret = regmap_write(dev->regmap[0], 0xd6, 0x48);
		ret = regmap_write(dev->regmap[1], 0x00, 0xb0);
		if (ret)
			goto err;
		break;
	default:
		ret = -EINVAL;
		goto err;
	}

	ret = regmap_write(dev->regmap[0], 0x46, 0x00);
	ret = regmap_write(dev->regmap[0], 0xae, 0x00);

	switch (dev->ts_mode) {
	case SERIAL_TS_MODE:
		ret = regmap_write(dev->regmap[2], 0x08, 0x1d);
		break;
	case PARALLEL_TS_MODE:
		ret = regmap_write(dev->regmap[2], 0x08, 0x00);
		break;
	default:
		dev_dbg(&client->dev, "ts_mode error: %d\n", dev->ts_mode);
		ret = -EINVAL;
		goto err;
	}

	switch (dev->ts_clock) {
	case VARIABLE_TS_CLOCK:
		ret = regmap_write(dev->regmap[0], 0xd9, 0xe3);
		break;
	case FIXED_TS_CLOCK:
		ret = regmap_write(dev->regmap[0], 0xd9, 0xe1);
		break;
	default:
		dev_dbg(&client->dev, "ts_clock error: %d\n", dev->ts_clock);
		ret = -EINVAL;
		goto err;
	}

	/* Reset demod */
	ret = regmap_write(dev->regmap[2], 0xf8, 0x9f);
	if (ret)
		goto err;

	dev->delivery_system = c->delivery_system;

	return 0;
err:
	dev_dbg(&client->dev, "failed=%d\n", ret);
	return ret;
}

static int mn88472_read_status(struct dvb_frontend *fe, enum fe_status *status)
{
	struct i2c_client *client = fe->demodulator_priv;
	struct mn88472_dev *dev = i2c_get_clientdata(client);
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
		ret = regmap_read(dev->regmap[0], 0x7F, &utmp);
		if (ret)
			goto err;
		if ((utmp & 0xF) >= 0x09)
			lock = 1;
		break;
	case SYS_DVBT2:
		ret = regmap_read(dev->regmap[2], 0x92, &utmp);
		if (ret)
			goto err;
		if ((utmp & 0xF) >= 0x07)
			*status |= FE_HAS_SIGNAL;
		if ((utmp & 0xF) >= 0x0a)
			*status |= FE_HAS_CARRIER;
		if ((utmp & 0xF) >= 0x0d)
			*status |= FE_HAS_VITERBI | FE_HAS_SYNC | FE_HAS_LOCK;
		break;
	case SYS_DVBC_ANNEX_A:
		ret = regmap_read(dev->regmap[1], 0x84, &utmp);
		if (ret)
			goto err;
		if ((utmp & 0xF) >= 0x08)
			lock = 1;
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

static int mn88472_init(struct dvb_frontend *fe)
{
	struct i2c_client *client = fe->demodulator_priv;
	struct mn88472_dev *dev = i2c_get_clientdata(client);
	int ret, len, remaining;
	const struct firmware *fw = NULL;
	u8 *fw_file = MN88472_FIRMWARE;
	unsigned int tmp;

	dev_dbg(&client->dev, "\n");

	/* set cold state by default */
	dev->warm = false;

	/* power on */
	ret = regmap_write(dev->regmap[2], 0x05, 0x00);
	if (ret)
		goto err;

	ret = regmap_bulk_write(dev->regmap[2], 0x0b, "\x00\x00", 2);
	if (ret)
		goto err;

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
		dev_err(&client->dev, "firmare file '%s' not found\n",
				fw_file);
		goto err;
	}

	dev_info(&client->dev, "downloading firmware from file '%s'\n",
			fw_file);

	ret = regmap_write(dev->regmap[0], 0xf5, 0x03);
	if (ret)
		goto firmware_release;

	for (remaining = fw->size; remaining > 0;
			remaining -= (dev->i2c_wr_max - 1)) {
		len = remaining;
		if (len > (dev->i2c_wr_max - 1))
			len = dev->i2c_wr_max - 1;

		ret = regmap_bulk_write(dev->regmap[0], 0xf6,
				&fw->data[fw->size - remaining], len);
		if (ret) {
			dev_err(&client->dev,
					"firmware download failed=%d\n", ret);
			goto firmware_release;
		}
	}

	/* parity check of firmware */
	ret = regmap_read(dev->regmap[0], 0xf8, &tmp);
	if (ret) {
		dev_err(&client->dev,
				"parity reg read failed=%d\n", ret);
		goto firmware_release;
	}
	if (tmp & 0x10) {
		dev_err(&client->dev,
				"firmware parity check failed=0x%x\n", tmp);
		goto firmware_release;
	}
	dev_err(&client->dev, "firmware parity check succeeded=0x%x\n", tmp);

	ret = regmap_write(dev->regmap[0], 0xf5, 0x00);
	if (ret)
		goto firmware_release;

	release_firmware(fw);
	fw = NULL;

	/* warm state */
	dev->warm = true;

	return 0;
firmware_release:
	release_firmware(fw);
err:
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
	ret = regmap_write(dev->regmap[2], 0x0b, 0x30);

	if (ret)
		goto err;

	ret = regmap_write(dev->regmap[2], 0x05, 0x3e);
	if (ret)
		goto err;

	dev->delivery_system = SYS_UNDEFINED;

	return 0;
err:
	dev_dbg(&client->dev, "failed=%d\n", ret);
	return ret;
}

static struct dvb_frontend_ops mn88472_ops = {
	.delsys = {SYS_DVBT, SYS_DVBT2, SYS_DVBC_ANNEX_A},
	.info = {
		.name = "Panasonic MN88472",
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
	dev->xtal = config->xtal;
	dev->ts_mode = config->ts_mode;
	dev->ts_clock = config->ts_clock;
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
	memcpy(&dev->fe.ops, &mn88472_ops, sizeof(struct dvb_frontend_ops));
	dev->fe.demodulator_priv = client;
	*config->fe = &dev->fe;
	i2c_set_clientdata(client, dev);

	dev_info(&client->dev, "Panasonic MN88472 successfully attached\n");
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

static int mn88472_remove(struct i2c_client *client)
{
	struct mn88472_dev *dev = i2c_get_clientdata(client);

	dev_dbg(&client->dev, "\n");

	regmap_exit(dev->regmap[2]);
	i2c_unregister_device(dev->client[2]);

	regmap_exit(dev->regmap[1]);
	i2c_unregister_device(dev->client[1]);

	regmap_exit(dev->regmap[0]);

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
