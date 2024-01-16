// SPDX-License-Identifier: GPL-2.0+
/*
 * Support for ON Semiconductor NOA1305 ambient light sensor
 *
 * Copyright (C) 2016 Emcraft Systems
 * Copyright (C) 2019 Collabora Ltd.
 */

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>

#define NOA1305_REG_POWER_CONTROL	0x0
#define   NOA1305_POWER_CONTROL_DOWN	0x00
#define   NOA1305_POWER_CONTROL_ON	0x08
#define NOA1305_REG_RESET		0x1
#define   NOA1305_RESET_RESET		0x10
#define NOA1305_REG_INTEGRATION_TIME	0x2
#define   NOA1305_INTEGR_TIME_800MS	0x00
#define   NOA1305_INTEGR_TIME_400MS	0x01
#define   NOA1305_INTEGR_TIME_200MS	0x02
#define   NOA1305_INTEGR_TIME_100MS	0x03
#define   NOA1305_INTEGR_TIME_50MS	0x04
#define   NOA1305_INTEGR_TIME_25MS	0x05
#define   NOA1305_INTEGR_TIME_12_5MS	0x06
#define   NOA1305_INTEGR_TIME_6_25MS	0x07
#define NOA1305_REG_INT_SELECT		0x3
#define   NOA1305_INT_SEL_ACTIVE_HIGH	0x01
#define   NOA1305_INT_SEL_ACTIVE_LOW	0x02
#define   NOA1305_INT_SEL_INACTIVE	0x03
#define NOA1305_REG_INT_THRESH_LSB	0x4
#define NOA1305_REG_INT_THRESH_MSB	0x5
#define NOA1305_REG_ALS_DATA_LSB	0x6
#define NOA1305_REG_ALS_DATA_MSB	0x7
#define NOA1305_REG_DEVICE_ID_LSB	0x8
#define NOA1305_REG_DEVICE_ID_MSB	0x9

#define NOA1305_DEVICE_ID	0x0519
#define NOA1305_DRIVER_NAME	"noa1305"

struct noa1305_priv {
	struct i2c_client *client;
	struct regmap *regmap;
	struct regulator *vin_reg;
};

static int noa1305_measure(struct noa1305_priv *priv)
{
	__le16 data;
	int ret;

	ret = regmap_bulk_read(priv->regmap, NOA1305_REG_ALS_DATA_LSB, &data,
			       2);
	if (ret < 0)
		return ret;

	return le16_to_cpu(data);
}

static int noa1305_scale(struct noa1305_priv *priv, int *val, int *val2)
{
	int data;
	int ret;

	ret = regmap_read(priv->regmap, NOA1305_REG_INTEGRATION_TIME, &data);
	if (ret < 0)
		return ret;

	/*
	 * Lux = count / (<Integration Constant> * <Integration Time>)
	 *
	 * Integration Constant = 7.7
	 * Integration Time in Seconds
	 */
	switch (data) {
	case NOA1305_INTEGR_TIME_800MS:
		*val = 100;
		*val2 = 77 * 8;
		break;
	case NOA1305_INTEGR_TIME_400MS:
		*val = 100;
		*val2 = 77 * 4;
		break;
	case NOA1305_INTEGR_TIME_200MS:
		*val = 100;
		*val2 = 77 * 2;
		break;
	case NOA1305_INTEGR_TIME_100MS:
		*val = 100;
		*val2 = 77;
		break;
	case NOA1305_INTEGR_TIME_50MS:
		*val = 1000;
		*val2 = 77 * 5;
		break;
	case NOA1305_INTEGR_TIME_25MS:
		*val = 10000;
		*val2 = 77 * 25;
		break;
	case NOA1305_INTEGR_TIME_12_5MS:
		*val = 100000;
		*val2 = 77 * 125;
		break;
	case NOA1305_INTEGR_TIME_6_25MS:
		*val = 1000000;
		*val2 = 77 * 625;
		break;
	default:
		return -EINVAL;
	}

	return IIO_VAL_FRACTIONAL;
}

static const struct iio_chan_spec noa1305_channels[] = {
	{
		.type = IIO_LIGHT,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),
	}
};

static int noa1305_read_raw(struct iio_dev *indio_dev,
				struct iio_chan_spec const *chan,
				int *val, int *val2, long mask)
{
	int ret = -EINVAL;
	struct noa1305_priv *priv = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		switch (chan->type) {
		case IIO_LIGHT:
			ret = noa1305_measure(priv);
			if (ret < 0)
				return ret;
			*val = ret;
			return IIO_VAL_INT;
		default:
			break;
		}
		break;
	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_LIGHT:
			return noa1305_scale(priv, val, val2);
		default:
			break;
		}
		break;
	default:
		break;
	}

	return ret;
}

static const struct iio_info noa1305_info = {
	.read_raw = noa1305_read_raw,
};

static bool noa1305_writable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case NOA1305_REG_POWER_CONTROL:
	case NOA1305_REG_RESET:
	case NOA1305_REG_INTEGRATION_TIME:
	case NOA1305_REG_INT_SELECT:
	case NOA1305_REG_INT_THRESH_LSB:
	case NOA1305_REG_INT_THRESH_MSB:
		return true;
	default:
		return false;
	}
}

static const struct regmap_config noa1305_regmap_config = {
	.name = NOA1305_DRIVER_NAME,
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = NOA1305_REG_DEVICE_ID_MSB,
	.writeable_reg = noa1305_writable_reg,
};

static void noa1305_reg_remove(void *data)
{
	struct noa1305_priv *priv = data;

	regulator_disable(priv->vin_reg);
}

static int noa1305_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct noa1305_priv *priv;
	struct iio_dev *indio_dev;
	struct regmap *regmap;
	__le16 data;
	unsigned int dev_id;
	int ret;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*priv));
	if (!indio_dev)
		return -ENOMEM;

	regmap = devm_regmap_init_i2c(client, &noa1305_regmap_config);
	if (IS_ERR(regmap)) {
		dev_err(&client->dev, "Regmap initialization failed.\n");
		return PTR_ERR(regmap);
	}

	priv = iio_priv(indio_dev);

	priv->vin_reg = devm_regulator_get(&client->dev, "vin");
	if (IS_ERR(priv->vin_reg))
		return dev_err_probe(&client->dev, PTR_ERR(priv->vin_reg),
				     "get regulator vin failed\n");

	ret = regulator_enable(priv->vin_reg);
	if (ret) {
		dev_err(&client->dev, "enable regulator vin failed\n");
		return ret;
	}

	ret = devm_add_action_or_reset(&client->dev, noa1305_reg_remove, priv);
	if (ret) {
		dev_err(&client->dev, "addition of devm action failed\n");
		return ret;
	}

	i2c_set_clientdata(client, indio_dev);
	priv->client = client;
	priv->regmap = regmap;

	ret = regmap_bulk_read(regmap, NOA1305_REG_DEVICE_ID_LSB, &data, 2);
	if (ret < 0) {
		dev_err(&client->dev, "ID reading failed: %d\n", ret);
		return ret;
	}

	dev_id = le16_to_cpu(data);
	if (dev_id != NOA1305_DEVICE_ID) {
		dev_err(&client->dev, "Unknown device ID: 0x%x\n", dev_id);
		return -ENODEV;
	}

	ret = regmap_write(regmap, NOA1305_REG_POWER_CONTROL,
			   NOA1305_POWER_CONTROL_ON);
	if (ret < 0) {
		dev_err(&client->dev, "Enabling power control failed\n");
		return ret;
	}

	ret = regmap_write(regmap, NOA1305_REG_RESET, NOA1305_RESET_RESET);
	if (ret < 0) {
		dev_err(&client->dev, "Device reset failed\n");
		return ret;
	}

	ret = regmap_write(regmap, NOA1305_REG_INTEGRATION_TIME,
			   NOA1305_INTEGR_TIME_800MS);
	if (ret < 0) {
		dev_err(&client->dev, "Setting integration time failed\n");
		return ret;
	}

	indio_dev->info = &noa1305_info;
	indio_dev->channels = noa1305_channels;
	indio_dev->num_channels = ARRAY_SIZE(noa1305_channels);
	indio_dev->name = NOA1305_DRIVER_NAME;
	indio_dev->modes = INDIO_DIRECT_MODE;

	ret = devm_iio_device_register(&client->dev, indio_dev);
	if (ret)
		dev_err(&client->dev, "registering device failed\n");

	return ret;
}

static const struct of_device_id noa1305_of_match[] = {
	{ .compatible = "onnn,noa1305" },
	{ }
};
MODULE_DEVICE_TABLE(of, noa1305_of_match);

static const struct i2c_device_id noa1305_ids[] = {
	{ "noa1305", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, noa1305_ids);

static struct i2c_driver noa1305_driver = {
	.driver = {
		.name		= NOA1305_DRIVER_NAME,
		.of_match_table	= noa1305_of_match,
	},
	.probe		= noa1305_probe,
	.id_table	= noa1305_ids,
};

module_i2c_driver(noa1305_driver);

MODULE_AUTHOR("Sergei Miroshnichenko <sergeimir@emcraft.com>");
MODULE_AUTHOR("Martyn Welch <martyn.welch@collabora.com");
MODULE_DESCRIPTION("ON Semiconductor NOA1305 ambient light sensor");
MODULE_LICENSE("GPL");
