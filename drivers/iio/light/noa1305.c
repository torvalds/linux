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
#define   NOA1305_INTEGR_TIME_MASK	0x07
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

static int noa1305_scale_available[] = {
	100, 8 * 77,		/* 800 ms */
	100, 4 * 77,		/* 400 ms */
	100, 2 * 77,		/* 200 ms */
	100, 1 * 77,		/* 100 ms */
	1000, 5 * 77,		/* 50 ms */
	10000, 25 * 77,		/* 25 ms */
	100000, 125 * 77,	/* 12.5 ms */
	1000000, 625 * 77,	/* 6.25 ms */
};

static int noa1305_int_time_available[] = {
	0, 800000,		/* 800 ms */
	0, 400000,		/* 400 ms */
	0, 200000,		/* 200 ms */
	0, 100000,		/* 100 ms */
	0, 50000,		/* 50 ms */
	0, 25000,		/* 25 ms */
	0, 12500,		/* 12.5 ms */
	0, 6250,		/* 6.25 ms */
};

struct noa1305_priv {
	struct i2c_client *client;
	struct regmap *regmap;
};

static int noa1305_measure(struct noa1305_priv *priv, int *val)
{
	__le16 data;
	int ret;

	ret = regmap_bulk_read(priv->regmap, NOA1305_REG_ALS_DATA_LSB, &data,
			       2);
	if (ret < 0)
		return ret;

	*val = le16_to_cpu(data);

	return IIO_VAL_INT;
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
	data &= NOA1305_INTEGR_TIME_MASK;
	*val = noa1305_scale_available[2 * data + 0];
	*val2 = noa1305_scale_available[2 * data + 1];

	return IIO_VAL_FRACTIONAL;
}

static int noa1305_int_time(struct noa1305_priv *priv, int *val, int *val2)
{
	int data;
	int ret;

	ret = regmap_read(priv->regmap, NOA1305_REG_INTEGRATION_TIME, &data);
	if (ret < 0)
		return ret;

	data &= NOA1305_INTEGR_TIME_MASK;
	*val = noa1305_int_time_available[2 * data + 0];
	*val2 = noa1305_int_time_available[2 * data + 1];

	return IIO_VAL_INT_PLUS_MICRO;
}

static const struct iio_chan_spec noa1305_channels[] = {
	{
		.type = IIO_LIGHT,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),
		.info_mask_shared_by_type_available = BIT(IIO_CHAN_INFO_SCALE),
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_INT_TIME),
		.info_mask_shared_by_all_available = BIT(IIO_CHAN_INFO_INT_TIME),
	}
};

static int noa1305_read_avail(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *chan,
			      const int **vals, int *type,
			      int *length, long mask)
{
	if (chan->type != IIO_LIGHT)
		return -EINVAL;

	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		*vals = noa1305_scale_available;
		*length = ARRAY_SIZE(noa1305_scale_available);
		*type = IIO_VAL_FRACTIONAL;
		return IIO_AVAIL_LIST;
	case IIO_CHAN_INFO_INT_TIME:
		*vals = noa1305_int_time_available;
		*length = ARRAY_SIZE(noa1305_int_time_available);
		*type = IIO_VAL_INT_PLUS_MICRO;
		return IIO_AVAIL_LIST;
	default:
		return -EINVAL;
	}
}

static int noa1305_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int *val, int *val2, long mask)
{
	struct noa1305_priv *priv = iio_priv(indio_dev);

	if (chan->type != IIO_LIGHT)
		return -EINVAL;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		return noa1305_measure(priv, val);
	case IIO_CHAN_INFO_SCALE:
		return noa1305_scale(priv, val, val2);
	case IIO_CHAN_INFO_INT_TIME:
		return noa1305_int_time(priv, val, val2);
	default:
		return -EINVAL;
	}
}

static int noa1305_write_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     int val, int val2, long mask)
{
	struct noa1305_priv *priv = iio_priv(indio_dev);
	int i;

	if (chan->type != IIO_LIGHT)
		return -EINVAL;

	if (mask != IIO_CHAN_INFO_INT_TIME)
		return -EINVAL;

	if (val)	/* >= 1s integration time not supported */
		return -EINVAL;

	/* Look up integration time register settings and write it if found. */
	for (i = 0; i < ARRAY_SIZE(noa1305_int_time_available) / 2; i++)
		if (noa1305_int_time_available[2 * i + 1] == val2)
			return regmap_write(priv->regmap, NOA1305_REG_INTEGRATION_TIME, i);

	return -EINVAL;
}

static const struct iio_info noa1305_info = {
	.read_avail = noa1305_read_avail,
	.read_raw = noa1305_read_raw,
	.write_raw = noa1305_write_raw,
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

static int noa1305_probe(struct i2c_client *client)
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

	ret = devm_regulator_get_enable(&client->dev, "vin");
	if (ret)
		return dev_err_probe(&client->dev, ret,
				     "get regulator vin failed\n");

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
	{ "noa1305" },
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
