// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * NXP TDA18212HN silicon tuner driver
 *
 * Copyright (C) 2011 Antti Palosaari <crope@iki.fi>
 */

#include "tda18212.h"
#include <linux/regmap.h>

struct tda18212_dev {
	struct tda18212_config cfg;
	struct i2c_client *client;
	struct regmap *regmap;

	u32 if_frequency;
};

static int tda18212_set_params(struct dvb_frontend *fe)
{
	struct tda18212_dev *dev = fe->tuner_priv;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	int ret, i;
	u32 if_khz;
	u8 buf[9];
	#define DVBT_6   0
	#define DVBT_7   1
	#define DVBT_8   2
	#define DVBT2_6  3
	#define DVBT2_7  4
	#define DVBT2_8  5
	#define DVBC_6   6
	#define DVBC_8   7
	#define ATSC_VSB 8
	#define ATSC_QAM 9
	static const u8 bw_params[][3] = {
		     /* reg:   0f    13    23 */
		[DVBT_6]  = { 0xb3, 0x20, 0x03 },
		[DVBT_7]  = { 0xb3, 0x31, 0x01 },
		[DVBT_8]  = { 0xb3, 0x22, 0x01 },
		[DVBT2_6] = { 0xbc, 0x20, 0x03 },
		[DVBT2_7] = { 0xbc, 0x72, 0x03 },
		[DVBT2_8] = { 0xbc, 0x22, 0x01 },
		[DVBC_6]  = { 0x92, 0x50, 0x03 },
		[DVBC_8]  = { 0x92, 0x53, 0x03 },
		[ATSC_VSB] = { 0x7d, 0x20, 0x63 },
		[ATSC_QAM] = { 0x7d, 0x20, 0x63 },
	};

	dev_dbg(&dev->client->dev,
			"delivery_system=%d frequency=%d bandwidth_hz=%d\n",
			c->delivery_system, c->frequency,
			c->bandwidth_hz);

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1); /* open I2C-gate */

	switch (c->delivery_system) {
	case SYS_ATSC:
		if_khz = dev->cfg.if_atsc_vsb;
		i = ATSC_VSB;
		break;
	case SYS_DVBC_ANNEX_B:
		if_khz = dev->cfg.if_atsc_qam;
		i = ATSC_QAM;
		break;
	case SYS_DVBT:
		switch (c->bandwidth_hz) {
		case 6000000:
			if_khz = dev->cfg.if_dvbt_6;
			i = DVBT_6;
			break;
		case 7000000:
			if_khz = dev->cfg.if_dvbt_7;
			i = DVBT_7;
			break;
		case 8000000:
			if_khz = dev->cfg.if_dvbt_8;
			i = DVBT_8;
			break;
		default:
			ret = -EINVAL;
			goto error;
		}
		break;
	case SYS_DVBT2:
		switch (c->bandwidth_hz) {
		case 6000000:
			if_khz = dev->cfg.if_dvbt2_6;
			i = DVBT2_6;
			break;
		case 7000000:
			if_khz = dev->cfg.if_dvbt2_7;
			i = DVBT2_7;
			break;
		case 8000000:
			if_khz = dev->cfg.if_dvbt2_8;
			i = DVBT2_8;
			break;
		default:
			ret = -EINVAL;
			goto error;
		}
		break;
	case SYS_DVBC_ANNEX_A:
	case SYS_DVBC_ANNEX_C:
		if_khz = dev->cfg.if_dvbc;
		i = DVBC_8;
		break;
	default:
		ret = -EINVAL;
		goto error;
	}

	ret = regmap_write(dev->regmap, 0x23, bw_params[i][2]);
	if (ret)
		goto error;

	ret = regmap_write(dev->regmap, 0x06, 0x00);
	if (ret)
		goto error;

	ret = regmap_write(dev->regmap, 0x0f, bw_params[i][0]);
	if (ret)
		goto error;

	buf[0] = 0x02;
	buf[1] = bw_params[i][1];
	buf[2] = 0x03; /* default value */
	buf[3] = DIV_ROUND_CLOSEST(if_khz, 50);
	buf[4] = ((c->frequency / 1000) >> 16) & 0xff;
	buf[5] = ((c->frequency / 1000) >>  8) & 0xff;
	buf[6] = ((c->frequency / 1000) >>  0) & 0xff;
	buf[7] = 0xc1;
	buf[8] = 0x01;
	ret = regmap_bulk_write(dev->regmap, 0x12, buf, sizeof(buf));
	if (ret)
		goto error;

	/* actual IF rounded as it is on register */
	dev->if_frequency = buf[3] * 50 * 1000;

exit:
	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0); /* close I2C-gate */

	return ret;

error:
	dev_dbg(&dev->client->dev, "failed=%d\n", ret);
	goto exit;
}

static int tda18212_get_if_frequency(struct dvb_frontend *fe, u32 *frequency)
{
	struct tda18212_dev *dev = fe->tuner_priv;

	*frequency = dev->if_frequency;

	return 0;
}

static const struct dvb_tuner_ops tda18212_tuner_ops = {
	.info = {
		.name              = "NXP TDA18212",

		.frequency_min_hz  =  48 * MHz,
		.frequency_max_hz  = 864 * MHz,
		.frequency_step_hz =   1 * kHz,
	},

	.set_params    = tda18212_set_params,
	.get_if_frequency = tda18212_get_if_frequency,
};

static int tda18212_probe(struct i2c_client *client)
{
	struct tda18212_config *cfg = client->dev.platform_data;
	struct dvb_frontend *fe = cfg->fe;
	struct tda18212_dev *dev;
	int ret;
	unsigned int chip_id;
	char *version;
	static const struct regmap_config regmap_config = {
		.reg_bits = 8,
		.val_bits = 8,
	};

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (dev == NULL) {
		ret = -ENOMEM;
		dev_err(&client->dev, "kzalloc() failed\n");
		goto err;
	}

	memcpy(&dev->cfg, cfg, sizeof(struct tda18212_config));
	dev->client = client;
	dev->regmap = devm_regmap_init_i2c(client, &regmap_config);
	if (IS_ERR(dev->regmap)) {
		ret = PTR_ERR(dev->regmap);
		goto err;
	}

	/* check if the tuner is there */
	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1); /* open I2C-gate */

	ret = regmap_read(dev->regmap, 0x00, &chip_id);
	dev_dbg(&dev->client->dev, "chip_id=%02x\n", chip_id);

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0); /* close I2C-gate */

	if (ret)
		goto err;

	switch (chip_id) {
	case 0xc7:
		version = "M"; /* master */
		break;
	case 0x47:
		version = "S"; /* slave */
		break;
	default:
		ret = -ENODEV;
		goto err;
	}

	dev_info(&dev->client->dev,
			"NXP TDA18212HN/%s successfully identified\n", version);

	fe->tuner_priv = dev;
	memcpy(&fe->ops.tuner_ops, &tda18212_tuner_ops,
			sizeof(struct dvb_tuner_ops));
	i2c_set_clientdata(client, dev);

	return 0;
err:
	dev_dbg(&client->dev, "failed=%d\n", ret);
	kfree(dev);
	return ret;
}

static void tda18212_remove(struct i2c_client *client)
{
	struct tda18212_dev *dev = i2c_get_clientdata(client);
	struct dvb_frontend *fe = dev->cfg.fe;

	dev_dbg(&client->dev, "\n");

	memset(&fe->ops.tuner_ops, 0, sizeof(struct dvb_tuner_ops));
	fe->tuner_priv = NULL;
	kfree(dev);
}

static const struct i2c_device_id tda18212_id[] = {
	{ "tda18212" },
	{}
};
MODULE_DEVICE_TABLE(i2c, tda18212_id);

static struct i2c_driver tda18212_driver = {
	.driver = {
		.name	= "tda18212",
	},
	.probe		= tda18212_probe,
	.remove		= tda18212_remove,
	.id_table	= tda18212_id,
};

module_i2c_driver(tda18212_driver);

MODULE_DESCRIPTION("NXP TDA18212HN silicon tuner driver");
MODULE_AUTHOR("Antti Palosaari <crope@iki.fi>");
MODULE_LICENSE("GPL");
