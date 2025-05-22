// SPDX-License-Identifier: GPL-2.0-only
/*
 * JSA1212 Ambient Light & Proximity Sensor Driver
 *
 * Copyright (c) 2014, Intel Corporation.
 *
 * JSA1212 I2C slave address: 0x44(ADDR tied to GND), 0x45(ADDR tied to VDD)
 *
 * TODO: Interrupt support, thresholds, range support.
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/mutex.h>
#include <linux/regmap.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>

/* JSA1212 reg address */
#define JSA1212_CONF_REG		0x01
#define JSA1212_INT_REG			0x02
#define JSA1212_PXS_LT_REG		0x03
#define JSA1212_PXS_HT_REG		0x04
#define JSA1212_ALS_TH1_REG		0x05
#define JSA1212_ALS_TH2_REG		0x06
#define JSA1212_ALS_TH3_REG		0x07
#define JSA1212_PXS_DATA_REG		0x08
#define JSA1212_ALS_DT1_REG		0x09
#define JSA1212_ALS_DT2_REG		0x0A
#define JSA1212_ALS_RNG_REG		0x0B
#define JSA1212_MAX_REG			0x0C

/* JSA1212 reg masks */
#define JSA1212_CONF_MASK		0xFF
#define JSA1212_INT_MASK		0xFF
#define JSA1212_PXS_LT_MASK		0xFF
#define JSA1212_PXS_HT_MASK		0xFF
#define JSA1212_ALS_TH1_MASK		0xFF
#define JSA1212_ALS_TH2_LT_MASK		0x0F
#define JSA1212_ALS_TH2_HT_MASK		0xF0
#define JSA1212_ALS_TH3_MASK		0xFF
#define JSA1212_PXS_DATA_MASK		0xFF
#define JSA1212_ALS_DATA_MASK		0x0FFF
#define JSA1212_ALS_DT1_MASK		0xFF
#define JSA1212_ALS_DT2_MASK		0x0F
#define JSA1212_ALS_RNG_MASK		0x07

/* JSA1212 CONF REG bits */
#define JSA1212_CONF_PXS_MASK		0x80
#define JSA1212_CONF_PXS_ENABLE		0x80
#define JSA1212_CONF_PXS_DISABLE	0x00
#define JSA1212_CONF_ALS_MASK		0x04
#define JSA1212_CONF_ALS_ENABLE		0x04
#define JSA1212_CONF_ALS_DISABLE	0x00
#define JSA1212_CONF_IRDR_MASK		0x08
/* Proxmity sensing IRDR current sink settings */
#define JSA1212_CONF_IRDR_200MA		0x08
#define JSA1212_CONF_IRDR_100MA		0x00
#define JSA1212_CONF_PXS_SLP_MASK	0x70
#define JSA1212_CONF_PXS_SLP_0MS	0x70
#define JSA1212_CONF_PXS_SLP_12MS	0x60
#define JSA1212_CONF_PXS_SLP_50MS	0x50
#define JSA1212_CONF_PXS_SLP_75MS	0x40
#define JSA1212_CONF_PXS_SLP_100MS	0x30
#define JSA1212_CONF_PXS_SLP_200MS	0x20
#define JSA1212_CONF_PXS_SLP_400MS	0x10
#define JSA1212_CONF_PXS_SLP_800MS	0x00

/* JSA1212 INT REG bits */
#define JSA1212_INT_CTRL_MASK		0x01
#define JSA1212_INT_CTRL_EITHER		0x00
#define JSA1212_INT_CTRL_BOTH		0x01
#define JSA1212_INT_ALS_PRST_MASK	0x06
#define JSA1212_INT_ALS_PRST_1CONV	0x00
#define JSA1212_INT_ALS_PRST_4CONV	0x02
#define JSA1212_INT_ALS_PRST_8CONV	0x04
#define JSA1212_INT_ALS_PRST_16CONV	0x06
#define JSA1212_INT_ALS_FLAG_MASK	0x08
#define JSA1212_INT_ALS_FLAG_CLR	0x00
#define JSA1212_INT_PXS_PRST_MASK	0x60
#define JSA1212_INT_PXS_PRST_1CONV	0x00
#define JSA1212_INT_PXS_PRST_4CONV	0x20
#define JSA1212_INT_PXS_PRST_8CONV	0x40
#define JSA1212_INT_PXS_PRST_16CONV	0x60
#define JSA1212_INT_PXS_FLAG_MASK	0x80
#define JSA1212_INT_PXS_FLAG_CLR	0x00

/* JSA1212 ALS RNG REG bits */
#define JSA1212_ALS_RNG_0_2048		0x00
#define JSA1212_ALS_RNG_0_1024		0x01
#define JSA1212_ALS_RNG_0_512		0x02
#define JSA1212_ALS_RNG_0_256		0x03
#define JSA1212_ALS_RNG_0_128		0x04

/* JSA1212 INT threshold range */
#define JSA1212_ALS_TH_MIN	0x0000
#define JSA1212_ALS_TH_MAX	0x0FFF
#define JSA1212_PXS_TH_MIN	0x00
#define JSA1212_PXS_TH_MAX	0xFF

#define JSA1212_ALS_DELAY_MS	200
#define JSA1212_PXS_DELAY_MS	100

#define JSA1212_DRIVER_NAME	"jsa1212"
#define JSA1212_REGMAP_NAME	"jsa1212_regmap"

enum jsa1212_op_mode {
	JSA1212_OPMODE_ALS_EN,
	JSA1212_OPMODE_PXS_EN,
};

struct jsa1212_data {
	struct i2c_client *client;
	struct mutex lock;
	u8 als_rng_idx;
	bool als_en; /* ALS enable status */
	bool pxs_en; /* proximity enable status */
	struct regmap *regmap;
};

/* ALS range idx to val mapping */
static const int jsa1212_als_range_val[] = {2048, 1024, 512, 256, 128,
						128, 128, 128};

/* Enables or disables ALS function based on status */
static int jsa1212_als_enable(struct jsa1212_data *data, u8 status)
{
	int ret;

	ret = regmap_update_bits(data->regmap, JSA1212_CONF_REG,
				JSA1212_CONF_ALS_MASK,
				status);
	if (ret < 0)
		return ret;

	data->als_en = !!status;

	return 0;
}

/* Enables or disables PXS function based on status */
static int jsa1212_pxs_enable(struct jsa1212_data *data, u8 status)
{
	int ret;

	ret = regmap_update_bits(data->regmap, JSA1212_CONF_REG,
				JSA1212_CONF_PXS_MASK,
				status);
	if (ret < 0)
		return ret;

	data->pxs_en = !!status;

	return 0;
}

static int jsa1212_read_als_data(struct jsa1212_data *data,
				unsigned int *val)
{
	int ret;
	__le16 als_data;

	ret = jsa1212_als_enable(data, JSA1212_CONF_ALS_ENABLE);
	if (ret < 0)
		return ret;

	/* Delay for data output */
	msleep(JSA1212_ALS_DELAY_MS);

	/* Read 12 bit data */
	ret = regmap_bulk_read(data->regmap, JSA1212_ALS_DT1_REG, &als_data, 2);
	if (ret < 0) {
		dev_err(&data->client->dev, "als data read err\n");
		goto als_data_read_err;
	}

	*val = le16_to_cpu(als_data);

als_data_read_err:
	return jsa1212_als_enable(data, JSA1212_CONF_ALS_DISABLE);
}

static int jsa1212_read_pxs_data(struct jsa1212_data *data,
				unsigned int *val)
{
	int ret;
	unsigned int pxs_data;

	ret = jsa1212_pxs_enable(data, JSA1212_CONF_PXS_ENABLE);
	if (ret < 0)
		return ret;

	/* Delay for data output */
	msleep(JSA1212_PXS_DELAY_MS);

	/* Read out all data */
	ret = regmap_read(data->regmap, JSA1212_PXS_DATA_REG, &pxs_data);
	if (ret < 0) {
		dev_err(&data->client->dev, "pxs data read err\n");
		goto pxs_data_read_err;
	}

	*val = pxs_data & JSA1212_PXS_DATA_MASK;

pxs_data_read_err:
	return jsa1212_pxs_enable(data, JSA1212_CONF_PXS_DISABLE);
}

static int jsa1212_read_raw(struct iio_dev *indio_dev,
				struct iio_chan_spec const *chan,
				int *val, int *val2, long mask)
{
	int ret;
	struct jsa1212_data *data = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		mutex_lock(&data->lock);
		switch (chan->type) {
		case IIO_LIGHT:
			ret = jsa1212_read_als_data(data, val);
			break;
		case IIO_PROXIMITY:
			ret = jsa1212_read_pxs_data(data, val);
			break;
		default:
			ret = -EINVAL;
			break;
		}
		mutex_unlock(&data->lock);
		return ret < 0 ? ret : IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_LIGHT:
			*val = jsa1212_als_range_val[data->als_rng_idx];
			*val2 = BIT(12); /* Max 12 bit value */
			return IIO_VAL_FRACTIONAL;
		default:
			break;
		}
		break;
	default:
		break;
	}

	return -EINVAL;
}

static const struct iio_chan_spec jsa1212_channels[] = {
	{
		.type = IIO_LIGHT,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
			BIT(IIO_CHAN_INFO_SCALE),
	},
	{
		.type = IIO_PROXIMITY,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
	}
};

static const struct iio_info jsa1212_info = {
	.read_raw		= &jsa1212_read_raw,
};

static int jsa1212_chip_init(struct jsa1212_data *data)
{
	int ret;

	ret = regmap_write(data->regmap, JSA1212_CONF_REG,
				(JSA1212_CONF_PXS_SLP_50MS |
				JSA1212_CONF_IRDR_200MA));
	if (ret < 0)
		return ret;

	ret = regmap_write(data->regmap, JSA1212_INT_REG,
				JSA1212_INT_ALS_PRST_4CONV);
	if (ret < 0)
		return ret;

	data->als_rng_idx = JSA1212_ALS_RNG_0_2048;

	return 0;
}

static bool jsa1212_is_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case JSA1212_PXS_DATA_REG:
	case JSA1212_ALS_DT1_REG:
	case JSA1212_ALS_DT2_REG:
	case JSA1212_INT_REG:
		return true;
	default:
		return false;
	}
}

static const struct regmap_config jsa1212_regmap_config = {
	.name =  JSA1212_REGMAP_NAME,
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = JSA1212_MAX_REG,
	.cache_type = REGCACHE_RBTREE,
	.volatile_reg = jsa1212_is_volatile_reg,
};

static int jsa1212_probe(struct i2c_client *client)
{
	struct jsa1212_data *data;
	struct iio_dev *indio_dev;
	struct regmap *regmap;
	int ret;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	regmap = devm_regmap_init_i2c(client, &jsa1212_regmap_config);
	if (IS_ERR(regmap)) {
		dev_err(&client->dev, "Regmap initialization failed.\n");
		return PTR_ERR(regmap);
	}

	data = iio_priv(indio_dev);

	i2c_set_clientdata(client, indio_dev);
	data->client = client;
	data->regmap = regmap;

	mutex_init(&data->lock);

	ret = jsa1212_chip_init(data);
	if (ret < 0)
		return ret;

	indio_dev->channels = jsa1212_channels;
	indio_dev->num_channels = ARRAY_SIZE(jsa1212_channels);
	indio_dev->name = JSA1212_DRIVER_NAME;
	indio_dev->modes = INDIO_DIRECT_MODE;

	indio_dev->info = &jsa1212_info;

	ret = iio_device_register(indio_dev);
	if (ret < 0)
		dev_err(&client->dev, "%s: register device failed\n", __func__);

	return ret;
}

 /* power off the device */
static int jsa1212_power_off(struct jsa1212_data *data)
{
	int ret;

	mutex_lock(&data->lock);

	ret = regmap_update_bits(data->regmap, JSA1212_CONF_REG,
				JSA1212_CONF_ALS_MASK |
				JSA1212_CONF_PXS_MASK,
				JSA1212_CONF_ALS_DISABLE |
				JSA1212_CONF_PXS_DISABLE);

	if (ret < 0)
		dev_err(&data->client->dev, "power off cmd failed\n");

	mutex_unlock(&data->lock);

	return ret;
}

static void jsa1212_remove(struct i2c_client *client)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(client);
	struct jsa1212_data *data = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);

	jsa1212_power_off(data);
}

static int jsa1212_suspend(struct device *dev)
{
	struct jsa1212_data *data;

	data = iio_priv(i2c_get_clientdata(to_i2c_client(dev)));

	return jsa1212_power_off(data);
}

static int jsa1212_resume(struct device *dev)
{
	int ret = 0;
	struct jsa1212_data *data;

	data = iio_priv(i2c_get_clientdata(to_i2c_client(dev)));

	mutex_lock(&data->lock);

	if (data->als_en) {
		ret = jsa1212_als_enable(data, JSA1212_CONF_ALS_ENABLE);
		if (ret < 0) {
			dev_err(dev, "als resume failed\n");
			goto unlock_and_ret;
		}
	}

	if (data->pxs_en) {
		ret = jsa1212_pxs_enable(data, JSA1212_CONF_PXS_ENABLE);
		if (ret < 0)
			dev_err(dev, "pxs resume failed\n");
	}

unlock_and_ret:
	mutex_unlock(&data->lock);
	return ret;
}

static DEFINE_SIMPLE_DEV_PM_OPS(jsa1212_pm_ops, jsa1212_suspend,
				jsa1212_resume);

static const struct acpi_device_id jsa1212_acpi_match[] = {
	{"JSA1212", 0},
	{ }
};
MODULE_DEVICE_TABLE(acpi, jsa1212_acpi_match);

static const struct i2c_device_id jsa1212_id[] = {
	{ JSA1212_DRIVER_NAME },
	{ }
};
MODULE_DEVICE_TABLE(i2c, jsa1212_id);

static struct i2c_driver jsa1212_driver = {
	.driver = {
		.name	= JSA1212_DRIVER_NAME,
		.pm	= pm_sleep_ptr(&jsa1212_pm_ops),
		.acpi_match_table = jsa1212_acpi_match,
	},
	.probe		= jsa1212_probe,
	.remove		= jsa1212_remove,
	.id_table	= jsa1212_id,
};
module_i2c_driver(jsa1212_driver);

MODULE_AUTHOR("Sathya Kuppuswamy <sathyanarayanan.kuppuswamy@linux.intel.com>");
MODULE_DESCRIPTION("JSA1212 proximity/ambient light sensor driver");
MODULE_LICENSE("GPL v2");
