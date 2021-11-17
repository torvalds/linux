// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Infineon TUA9001 silicon tuner driver
 *
 * Copyright (C) 2009 Antti Palosaari <crope@iki.fi>
 */

#include "tua9001_priv.h"

static int tua9001_init(struct dvb_frontend *fe)
{
	struct tua9001_dev *dev = fe->tuner_priv;
	struct i2c_client *client = dev->client;
	int ret, i;
	static const struct tua9001_reg_val data[] = {
		{0x1e, 0x6512},
		{0x25, 0xb888},
		{0x39, 0x5460},
		{0x3b, 0x00c0},
		{0x3a, 0xf000},
		{0x08, 0x0000},
		{0x32, 0x0030},
		{0x41, 0x703a},
		{0x40, 0x1c78},
		{0x2c, 0x1c00},
		{0x36, 0xc013},
		{0x37, 0x6f18},
		{0x27, 0x0008},
		{0x2a, 0x0001},
		{0x34, 0x0a40},
	};

	dev_dbg(&client->dev, "\n");

	if (fe->callback) {
		ret = fe->callback(client->adapter,
				   DVB_FRONTEND_COMPONENT_TUNER,
				   TUA9001_CMD_RESETN, 0);
		if (ret)
			goto err;
	}

	for (i = 0; i < ARRAY_SIZE(data); i++) {
		ret = regmap_write(dev->regmap, data[i].reg, data[i].val);
		if (ret)
			goto err;
	}
	return 0;
err:
	dev_dbg(&client->dev, "failed=%d\n", ret);
	return ret;
}

static int tua9001_sleep(struct dvb_frontend *fe)
{
	struct tua9001_dev *dev = fe->tuner_priv;
	struct i2c_client *client = dev->client;
	int ret;

	dev_dbg(&client->dev, "\n");

	if (fe->callback) {
		ret = fe->callback(client->adapter,
				   DVB_FRONTEND_COMPONENT_TUNER,
				   TUA9001_CMD_RESETN, 1);
		if (ret)
			goto err;
	}
	return 0;
err:
	dev_dbg(&client->dev, "failed=%d\n", ret);
	return ret;
}

static int tua9001_set_params(struct dvb_frontend *fe)
{
	struct tua9001_dev *dev = fe->tuner_priv;
	struct i2c_client *client = dev->client;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	int ret, i;
	u16 val;
	struct tua9001_reg_val data[2];

	dev_dbg(&client->dev,
		"delivery_system=%u frequency=%u bandwidth_hz=%u\n",
		c->delivery_system, c->frequency, c->bandwidth_hz);

	switch (c->delivery_system) {
	case SYS_DVBT:
		switch (c->bandwidth_hz) {
		case 8000000:
			val  = 0x0000;
			break;
		case 7000000:
			val  = 0x1000;
			break;
		case 6000000:
			val  = 0x2000;
			break;
		case 5000000:
			val  = 0x3000;
			break;
		default:
			ret = -EINVAL;
			goto err;
		}
		break;
	default:
		ret = -EINVAL;
		goto err;
	}

	data[0].reg = 0x04;
	data[0].val = val;
	data[1].reg = 0x1f;
	data[1].val = div_u64((u64) (c->frequency - 150000000) * 48, 1000000);

	if (fe->callback) {
		ret = fe->callback(client->adapter,
				   DVB_FRONTEND_COMPONENT_TUNER,
				   TUA9001_CMD_RXEN, 0);
		if (ret)
			goto err;
	}

	for (i = 0; i < ARRAY_SIZE(data); i++) {
		ret = regmap_write(dev->regmap, data[i].reg, data[i].val);
		if (ret)
			goto err;
	}

	if (fe->callback) {
		ret = fe->callback(client->adapter,
				   DVB_FRONTEND_COMPONENT_TUNER,
				   TUA9001_CMD_RXEN, 1);
		if (ret)
			goto err;
	}
	return 0;
err:
	dev_dbg(&client->dev, "failed=%d\n", ret);
	return ret;
}

static int tua9001_get_if_frequency(struct dvb_frontend *fe, u32 *frequency)
{
	struct tua9001_dev *dev = fe->tuner_priv;
	struct i2c_client *client = dev->client;

	dev_dbg(&client->dev, "\n");

	*frequency = 0; /* Zero-IF */
	return 0;
}

static const struct dvb_tuner_ops tua9001_tuner_ops = {
	.info = {
		.name             = "Infineon TUA9001",
		.frequency_min_hz = 170 * MHz,
		.frequency_max_hz = 862 * MHz,
	},

	.init = tua9001_init,
	.sleep = tua9001_sleep,
	.set_params = tua9001_set_params,

	.get_if_frequency = tua9001_get_if_frequency,
};

static int tua9001_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct tua9001_dev *dev;
	struct tua9001_platform_data *pdata = client->dev.platform_data;
	struct dvb_frontend *fe = pdata->dvb_frontend;
	int ret;
	static const struct regmap_config regmap_config = {
		.reg_bits =  8,
		.val_bits = 16,
	};

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev) {
		ret = -ENOMEM;
		goto err;
	}

	dev->fe = pdata->dvb_frontend;
	dev->client = client;
	dev->regmap = devm_regmap_init_i2c(client, &regmap_config);
	if (IS_ERR(dev->regmap)) {
		ret = PTR_ERR(dev->regmap);
		goto err_kfree;
	}

	if (fe->callback) {
		ret = fe->callback(client->adapter,
				   DVB_FRONTEND_COMPONENT_TUNER,
				   TUA9001_CMD_CEN, 1);
		if (ret)
			goto err_kfree;

		ret = fe->callback(client->adapter,
				   DVB_FRONTEND_COMPONENT_TUNER,
				   TUA9001_CMD_RXEN, 0);
		if (ret)
			goto err_kfree;

		ret = fe->callback(client->adapter,
				   DVB_FRONTEND_COMPONENT_TUNER,
				   TUA9001_CMD_RESETN, 1);
		if (ret)
			goto err_kfree;
	}

	fe->tuner_priv = dev;
	memcpy(&fe->ops.tuner_ops, &tua9001_tuner_ops,
			sizeof(struct dvb_tuner_ops));
	i2c_set_clientdata(client, dev);

	dev_info(&client->dev, "Infineon TUA9001 successfully attached\n");
	return 0;
err_kfree:
	kfree(dev);
err:
	dev_dbg(&client->dev, "failed=%d\n", ret);
	return ret;
}

static int tua9001_remove(struct i2c_client *client)
{
	struct tua9001_dev *dev = i2c_get_clientdata(client);
	struct dvb_frontend *fe = dev->fe;
	int ret;

	dev_dbg(&client->dev, "\n");

	if (fe->callback) {
		ret = fe->callback(client->adapter,
				   DVB_FRONTEND_COMPONENT_TUNER,
				   TUA9001_CMD_CEN, 0);
		if (ret)
			goto err_kfree;
	}
	kfree(dev);
	return 0;
err_kfree:
	kfree(dev);
	dev_dbg(&client->dev, "failed=%d\n", ret);
	return ret;
}

static const struct i2c_device_id tua9001_id_table[] = {
	{"tua9001", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, tua9001_id_table);

static struct i2c_driver tua9001_driver = {
	.driver = {
		.name	= "tua9001",
		.suppress_bind_attrs = true,
	},
	.probe		= tua9001_probe,
	.remove		= tua9001_remove,
	.id_table	= tua9001_id_table,
};

module_i2c_driver(tua9001_driver);

MODULE_DESCRIPTION("Infineon TUA9001 silicon tuner driver");
MODULE_AUTHOR("Antti Palosaari <crope@iki.fi>");
MODULE_LICENSE("GPL");
