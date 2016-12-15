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
	s->min_delay_ms = 1000;
	return 0;
}

static int mn88472_read_status(struct dvb_frontend *fe, enum fe_status *status)
{
	struct i2c_client *client = fe->demodulator_priv;
	struct mn88472_dev *dev = i2c_get_clientdata(client);
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	int ret;
	unsigned int utmp;

	if (!dev->active) {
		ret = -EAGAIN;
		goto err;
	}

	switch (c->delivery_system) {
	case SYS_DVBT:
		ret = regmap_read(dev->regmap[0], 0x7f, &utmp);
		if (ret)
			goto err;
		if ((utmp & 0x0f) >= 0x09)
			*status = FE_HAS_SIGNAL | FE_HAS_CARRIER |
				  FE_HAS_VITERBI | FE_HAS_SYNC | FE_HAS_LOCK;
		else
			*status = 0;
		break;
	case SYS_DVBT2:
		ret = regmap_read(dev->regmap[2], 0x92, &utmp);
		if (ret)
			goto err;
		if ((utmp & 0x0f) >= 0x0d)
			*status = FE_HAS_SIGNAL | FE_HAS_CARRIER |
				  FE_HAS_VITERBI | FE_HAS_SYNC | FE_HAS_LOCK;
		else if ((utmp & 0x0f) >= 0x0a)
			*status = FE_HAS_SIGNAL | FE_HAS_CARRIER |
				  FE_HAS_VITERBI;
		else if ((utmp & 0x0f) >= 0x07)
			*status = FE_HAS_SIGNAL | FE_HAS_CARRIER;
		else
			*status = 0;
		break;
	case SYS_DVBC_ANNEX_A:
		ret = regmap_read(dev->regmap[1], 0x84, &utmp);
		if (ret)
			goto err;
		if ((utmp & 0x0f) >= 0x08)
			*status = FE_HAS_SIGNAL | FE_HAS_CARRIER |
				  FE_HAS_VITERBI | FE_HAS_SYNC | FE_HAS_LOCK;
		else
			*status = 0;
		break;
	default:
		ret = -EINVAL;
		goto err;
	}

	return 0;
err:
	dev_dbg(&client->dev, "failed=%d\n", ret);
	return ret;
}

static int mn88472_set_frontend(struct dvb_frontend *fe)
{
	struct i2c_client *client = fe->demodulator_priv;
	struct mn88472_dev *dev = i2c_get_clientdata(client);
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	int ret, i;
	unsigned int utmp;
	u32 if_frequency;
	u8 buf[3], delivery_system_val, bandwidth_val, *bandwidth_vals_ptr;
	u8 reg_bank0_b4_val, reg_bank0_cd_val, reg_bank0_d4_val;
	u8 reg_bank0_d6_val;

	dev_dbg(&client->dev,
		"delivery_system=%u modulation=%u frequency=%u bandwidth_hz=%u symbol_rate=%u inversion=%d stream_id=%d\n",
		c->delivery_system, c->modulation, c->frequency,
		c->bandwidth_hz, c->symbol_rate, c->inversion, c->stream_id);

	if (!dev->active) {
		ret = -EAGAIN;
		goto err;
	}

	switch (c->delivery_system) {
	case SYS_DVBT:
		delivery_system_val = 0x02;
		reg_bank0_b4_val = 0x00;
		reg_bank0_cd_val = 0x1f;
		reg_bank0_d4_val = 0x0a;
		reg_bank0_d6_val = 0x48;
		break;
	case SYS_DVBT2:
		delivery_system_val = 0x03;
		reg_bank0_b4_val = 0xf6;
		reg_bank0_cd_val = 0x01;
		reg_bank0_d4_val = 0x09;
		reg_bank0_d6_val = 0x46;
		break;
	case SYS_DVBC_ANNEX_A:
		delivery_system_val = 0x04;
		reg_bank0_b4_val = 0x00;
		reg_bank0_cd_val = 0x17;
		reg_bank0_d4_val = 0x09;
		reg_bank0_d6_val = 0x48;
		break;
	default:
		ret = -EINVAL;
		goto err;
	}

	switch (c->delivery_system) {
	case SYS_DVBT:
	case SYS_DVBT2:
		switch (c->bandwidth_hz) {
		case 5000000:
			bandwidth_vals_ptr = "\xe5\x99\x9a\x1b\xa9\x1b\xa9";
			bandwidth_val = 0x03;
			break;
		case 6000000:
			bandwidth_vals_ptr = "\xbf\x55\x55\x15\x6b\x15\x6b";
			bandwidth_val = 0x02;
			break;
		case 7000000:
			bandwidth_vals_ptr = "\xa4\x00\x00\x0f\x2c\x0f\x2c";
			bandwidth_val = 0x01;
			break;
		case 8000000:
			bandwidth_vals_ptr = "\x8f\x80\x00\x08\xee\x08\xee";
			bandwidth_val = 0x00;
			break;
		default:
			ret = -EINVAL;
			goto err;
		}
		break;
	case SYS_DVBC_ANNEX_A:
		bandwidth_vals_ptr = NULL;
		bandwidth_val = 0x00;
		break;
	default:
		break;
	}

	/* Program tuner */
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
		ret = -EINVAL;
		goto err;
	}

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
	ret = regmap_write(dev->regmap[2], 0x04, bandwidth_val);
	if (ret)
		goto err;

	/* IF */
	utmp = DIV_ROUND_CLOSEST_ULL((u64)if_frequency * 0x1000000, dev->clk);
	buf[0] = (utmp >> 16) & 0xff;
	buf[1] = (utmp >>  8) & 0xff;
	buf[2] = (utmp >>  0) & 0xff;
	for (i = 0; i < 3; i++) {
		ret = regmap_write(dev->regmap[2], 0x10 + i, buf[i]);
		if (ret)
			goto err;
	}

	/* Bandwidth */
	if (bandwidth_vals_ptr) {
		for (i = 0; i < 7; i++) {
			ret = regmap_write(dev->regmap[2], 0x13 + i,
					   bandwidth_vals_ptr[i]);
			if (ret)
				goto err;
		}
	}

	ret = regmap_write(dev->regmap[0], 0xb4, reg_bank0_b4_val);
	if (ret)
		goto err;
	ret = regmap_write(dev->regmap[0], 0xcd, reg_bank0_cd_val);
	if (ret)
		goto err;
	ret = regmap_write(dev->regmap[0], 0xd4, reg_bank0_d4_val);
	if (ret)
		goto err;
	ret = regmap_write(dev->regmap[0], 0xd6, reg_bank0_d6_val);
	if (ret)
		goto err;

	switch (c->delivery_system) {
	case SYS_DVBT:
		ret = regmap_write(dev->regmap[0], 0x07, 0x26);
		if (ret)
			goto err;
		ret = regmap_write(dev->regmap[0], 0x00, 0xba);
		if (ret)
			goto err;
		ret = regmap_write(dev->regmap[0], 0x01, 0x13);
		if (ret)
			goto err;
		break;
	case SYS_DVBT2:
		ret = regmap_write(dev->regmap[2], 0x2b, 0x13);
		if (ret)
			goto err;
		ret = regmap_write(dev->regmap[2], 0x4f, 0x05);
		if (ret)
			goto err;
		ret = regmap_write(dev->regmap[1], 0xf6, 0x05);
		if (ret)
			goto err;
		ret = regmap_write(dev->regmap[2], 0x32, c->stream_id);
		if (ret)
			goto err;
		break;
	case SYS_DVBC_ANNEX_A:
		break;
	default:
		break;
	}

	/* Reset FSM */
	ret = regmap_write(dev->regmap[2], 0xf8, 0x9f);
	if (ret)
		goto err;

	return 0;
err:
	dev_dbg(&client->dev, "failed=%d\n", ret);
	return ret;
}

static int mn88472_init(struct dvb_frontend *fe)
{
	struct i2c_client *client = fe->demodulator_priv;
	struct mn88472_dev *dev = i2c_get_clientdata(client);
	int ret, len, rem;
	unsigned int utmp;
	const struct firmware *firmware;
	const char *name = MN88472_FIRMWARE;

	dev_dbg(&client->dev, "\n");

	/* Power up */
	ret = regmap_write(dev->regmap[2], 0x05, 0x00);
	if (ret)
		goto err;
	ret = regmap_write(dev->regmap[2], 0x0b, 0x00);
	if (ret)
		goto err;
	ret = regmap_write(dev->regmap[2], 0x0c, 0x00);
	if (ret)
		goto err;

	/* Check if firmware is already running */
	ret = regmap_read(dev->regmap[0], 0xf5, &utmp);
	if (ret)
		goto err;
	if (!(utmp & 0x01))
		goto warm;

	ret = request_firmware(&firmware, name, &client->dev);
	if (ret) {
		dev_err(&client->dev, "firmware file '%s' not found\n", name);
		goto err;
	}

	dev_info(&client->dev, "downloading firmware from file '%s'\n", name);

	ret = regmap_write(dev->regmap[0], 0xf5, 0x03);
	if (ret)
		goto err_release_firmware;

	for (rem = firmware->size; rem > 0; rem -= (dev->i2c_write_max - 1)) {
		len = min(dev->i2c_write_max - 1, rem);
		ret = regmap_bulk_write(dev->regmap[0], 0xf6,
					&firmware->data[firmware->size - rem],
					len);
		if (ret) {
			dev_err(&client->dev, "firmware download failed %d\n",
				ret);
			goto err_release_firmware;
		}
	}

	/* Parity check of firmware */
	ret = regmap_read(dev->regmap[0], 0xf8, &utmp);
	if (ret)
		goto err_release_firmware;
	if (utmp & 0x10) {
		ret = -EINVAL;
		dev_err(&client->dev, "firmware did not run\n");
		goto err_release_firmware;
	}

	ret = regmap_write(dev->regmap[0], 0xf5, 0x00);
	if (ret)
		goto err_release_firmware;

	release_firmware(firmware);
warm:
	/* TS config */
	switch (dev->ts_mode) {
	case SERIAL_TS_MODE:
		utmp = 0x1d;
		break;
	case PARALLEL_TS_MODE:
		utmp = 0x00;
		break;
	default:
		ret = -EINVAL;
		goto err;
	}
	ret = regmap_write(dev->regmap[2], 0x08, utmp);
	if (ret)
		goto err;

	switch (dev->ts_clk) {
	case VARIABLE_TS_CLOCK:
		utmp = 0xe3;
		break;
	case FIXED_TS_CLOCK:
		utmp = 0xe1;
		break;
	default:
		ret = -EINVAL;
		goto err;
	}
	ret = regmap_write(dev->regmap[0], 0xd9, utmp);
	if (ret)
		goto err;

	dev->active = true;

	return 0;
err_release_firmware:
	release_firmware(firmware);
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

	/* Power down */
	ret = regmap_write(dev->regmap[2], 0x0c, 0x30);
	if (ret)
		goto err;
	ret = regmap_write(dev->regmap[2], 0x0b, 0x30);
	if (ret)
		goto err;
	ret = regmap_write(dev->regmap[2], 0x05, 0x3e);
	if (ret)
		goto err;

	return 0;
err:
	dev_dbg(&client->dev, "failed=%d\n", ret);
	return ret;
}

static const struct dvb_frontend_ops mn88472_ops = {
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

static struct dvb_frontend *mn88472_get_dvb_frontend(struct i2c_client *client)
{
	struct mn88472_dev *dev = i2c_get_clientdata(client);

	dev_dbg(&client->dev, "\n");

	return &dev->fe;
}

static int mn88472_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct mn88472_config *pdata = client->dev.platform_data;
	struct mn88472_dev *dev;
	int ret;
	unsigned int utmp;
	static const struct regmap_config regmap_config = {
		.reg_bits = 8,
		.val_bits = 8,
	};

	dev_dbg(&client->dev, "\n");

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev) {
		ret = -ENOMEM;
		goto err;
	}

	dev->i2c_write_max = pdata->i2c_wr_max ? pdata->i2c_wr_max : ~0;
	dev->clk = pdata->xtal;
	dev->ts_mode = pdata->ts_mode;
	dev->ts_clk = pdata->ts_clock;
	dev->client[0] = client;
	dev->regmap[0] = regmap_init_i2c(dev->client[0], &regmap_config);
	if (IS_ERR(dev->regmap[0])) {
		ret = PTR_ERR(dev->regmap[0]);
		goto err_kfree;
	}

	/*
	 * Chip has three I2C addresses for different register banks. Used
	 * addresses are 0x18, 0x1a and 0x1c. We register two dummy clients,
	 * 0x1a and 0x1c, in order to get own I2C client for each register bank.
	 *
	 * Also, register bank 2 do not support sequential I/O. Only single
	 * register write or read is allowed to that bank.
	 */
	dev->client[1] = i2c_new_dummy(client->adapter, 0x1a);
	if (!dev->client[1]) {
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
	if (!dev->client[2]) {
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

	/* Check demod answers with correct chip id */
	ret = regmap_read(dev->regmap[2], 0xff, &utmp);
	if (ret)
		goto err_regmap_2_regmap_exit;

	dev_dbg(&client->dev, "chip id=%02x\n", utmp);

	if (utmp != 0x02) {
		ret = -ENODEV;
		goto err_regmap_2_regmap_exit;
	}

	/* Sleep because chip is active by default */
	ret = regmap_write(dev->regmap[2], 0x05, 0x3e);
	if (ret)
		goto err_regmap_2_regmap_exit;

	/* Create dvb frontend */
	memcpy(&dev->fe.ops, &mn88472_ops, sizeof(struct dvb_frontend_ops));
	dev->fe.demodulator_priv = client;
	*pdata->fe = &dev->fe;
	i2c_set_clientdata(client, dev);

	/* Setup callbacks */
	pdata->get_dvb_frontend = mn88472_get_dvb_frontend;

	dev_info(&client->dev, "Panasonic MN88472 successfully identified\n");

	return 0;
err_regmap_2_regmap_exit:
	regmap_exit(dev->regmap[2]);
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
		.name = "mn88472",
		.suppress_bind_attrs = true,
	},
	.probe    = mn88472_probe,
	.remove   = mn88472_remove,
	.id_table = mn88472_id_table,
};

module_i2c_driver(mn88472_driver);

MODULE_AUTHOR("Antti Palosaari <crope@iki.fi>");
MODULE_DESCRIPTION("Panasonic MN88472 DVB-T/T2/C demodulator driver");
MODULE_LICENSE("GPL");
MODULE_FIRMWARE(MN88472_FIRMWARE);
