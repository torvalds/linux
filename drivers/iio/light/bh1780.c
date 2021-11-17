// SPDX-License-Identifier: GPL-2.0-only
/*
 * ROHM 1780GLI Ambient Light Sensor Driver
 *
 * Copyright (C) 2016 Linaro Ltd.
 * Author: Linus Walleij <linus.walleij@linaro.org>
 * Loosely based on the previous BH1780 ALS misc driver
 * Copyright (C) 2010 Texas Instruments
 * Author: Hemanth V <hemanthv@ti.com>
 */
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/pm_runtime.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/bitops.h>

#define BH1780_CMD_BIT		BIT(7)
#define BH1780_REG_CONTROL	0x00
#define BH1780_REG_PARTID	0x0A
#define BH1780_REG_MANFID	0x0B
#define BH1780_REG_DLOW		0x0C
#define BH1780_REG_DHIGH	0x0D

#define BH1780_REVMASK		GENMASK(3,0)
#define BH1780_POWMASK		GENMASK(1,0)
#define BH1780_POFF		(0x0)
#define BH1780_PON		(0x3)

/* power on settling time in ms */
#define BH1780_PON_DELAY	2
/* max time before value available in ms */
#define BH1780_INTERVAL		250

struct bh1780_data {
	struct i2c_client *client;
};

static int bh1780_write(struct bh1780_data *bh1780, u8 reg, u8 val)
{
	int ret = i2c_smbus_write_byte_data(bh1780->client,
					    BH1780_CMD_BIT | reg,
					    val);
	if (ret < 0)
		dev_err(&bh1780->client->dev,
			"i2c_smbus_write_byte_data failed error "
			"%d, register %01x\n",
			ret, reg);
	return ret;
}

static int bh1780_read(struct bh1780_data *bh1780, u8 reg)
{
	int ret = i2c_smbus_read_byte_data(bh1780->client,
					   BH1780_CMD_BIT | reg);
	if (ret < 0)
		dev_err(&bh1780->client->dev,
			"i2c_smbus_read_byte_data failed error "
			"%d, register %01x\n",
			ret, reg);
	return ret;
}

static int bh1780_read_word(struct bh1780_data *bh1780, u8 reg)
{
	int ret = i2c_smbus_read_word_data(bh1780->client,
					   BH1780_CMD_BIT | reg);
	if (ret < 0)
		dev_err(&bh1780->client->dev,
			"i2c_smbus_read_word_data failed error "
			"%d, register %01x\n",
			ret, reg);
	return ret;
}

static int bh1780_debugfs_reg_access(struct iio_dev *indio_dev,
			      unsigned int reg, unsigned int writeval,
			      unsigned int *readval)
{
	struct bh1780_data *bh1780 = iio_priv(indio_dev);
	int ret;

	if (!readval)
		return bh1780_write(bh1780, (u8)reg, (u8)writeval);

	ret = bh1780_read(bh1780, (u8)reg);
	if (ret < 0)
		return ret;

	*readval = ret;

	return 0;
}

static int bh1780_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val, int *val2, long mask)
{
	struct bh1780_data *bh1780 = iio_priv(indio_dev);
	int value;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		switch (chan->type) {
		case IIO_LIGHT:
			pm_runtime_get_sync(&bh1780->client->dev);
			value = bh1780_read_word(bh1780, BH1780_REG_DLOW);
			if (value < 0)
				return value;
			pm_runtime_mark_last_busy(&bh1780->client->dev);
			pm_runtime_put_autosuspend(&bh1780->client->dev);
			*val = value;

			return IIO_VAL_INT;
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_INT_TIME:
		*val = 0;
		*val2 = BH1780_INTERVAL * 1000;
		return IIO_VAL_INT_PLUS_MICRO;
	default:
		return -EINVAL;
	}
}

static const struct iio_info bh1780_info = {
	.read_raw = bh1780_read_raw,
	.debugfs_reg_access = bh1780_debugfs_reg_access,
};

static const struct iio_chan_spec bh1780_channels[] = {
	{
		.type = IIO_LIGHT,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_INT_TIME)
	}
};

static int bh1780_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	int ret;
	struct bh1780_data *bh1780;
	struct i2c_adapter *adapter = client->adapter;
	struct iio_dev *indio_dev;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE))
		return -EIO;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*bh1780));
	if (!indio_dev)
		return -ENOMEM;

	bh1780 = iio_priv(indio_dev);
	bh1780->client = client;
	i2c_set_clientdata(client, indio_dev);

	/* Power up the device */
	ret = bh1780_write(bh1780, BH1780_REG_CONTROL, BH1780_PON);
	if (ret < 0)
		return ret;
	msleep(BH1780_PON_DELAY);
	pm_runtime_get_noresume(&client->dev);
	pm_runtime_set_active(&client->dev);
	pm_runtime_enable(&client->dev);

	ret = bh1780_read(bh1780, BH1780_REG_PARTID);
	if (ret < 0)
		goto out_disable_pm;
	dev_info(&client->dev,
		 "Ambient Light Sensor, Rev : %lu\n",
		 (ret & BH1780_REVMASK));

	/*
	 * As the device takes 250 ms to even come up with a fresh
	 * measurement after power-on, do not shut it down unnecessarily.
	 * Set autosuspend to a five seconds.
	 */
	pm_runtime_set_autosuspend_delay(&client->dev, 5000);
	pm_runtime_use_autosuspend(&client->dev);
	pm_runtime_put(&client->dev);

	indio_dev->info = &bh1780_info;
	indio_dev->name = "bh1780";
	indio_dev->channels = bh1780_channels;
	indio_dev->num_channels = ARRAY_SIZE(bh1780_channels);
	indio_dev->modes = INDIO_DIRECT_MODE;

	ret = iio_device_register(indio_dev);
	if (ret)
		goto out_disable_pm;
	return 0;

out_disable_pm:
	pm_runtime_put_noidle(&client->dev);
	pm_runtime_disable(&client->dev);
	return ret;
}

static int bh1780_remove(struct i2c_client *client)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(client);
	struct bh1780_data *bh1780 = iio_priv(indio_dev);
	int ret;

	iio_device_unregister(indio_dev);
	pm_runtime_get_sync(&client->dev);
	pm_runtime_put_noidle(&client->dev);
	pm_runtime_disable(&client->dev);
	ret = bh1780_write(bh1780, BH1780_REG_CONTROL, BH1780_POFF);
	if (ret < 0) {
		dev_err(&client->dev, "failed to power off\n");
		return ret;
	}

	return 0;
}

#ifdef CONFIG_PM
static int bh1780_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct iio_dev *indio_dev = i2c_get_clientdata(client);
	struct bh1780_data *bh1780 = iio_priv(indio_dev);
	int ret;

	ret = bh1780_write(bh1780, BH1780_REG_CONTROL, BH1780_POFF);
	if (ret < 0) {
		dev_err(dev, "failed to runtime suspend\n");
		return ret;
	}

	return 0;
}

static int bh1780_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct iio_dev *indio_dev = i2c_get_clientdata(client);
	struct bh1780_data *bh1780 = iio_priv(indio_dev);
	int ret;

	ret = bh1780_write(bh1780, BH1780_REG_CONTROL, BH1780_PON);
	if (ret < 0) {
		dev_err(dev, "failed to runtime resume\n");
		return ret;
	}

	/* Wait for power on, then for a value to be available */
	msleep(BH1780_PON_DELAY + BH1780_INTERVAL);

	return 0;
}
#endif /* CONFIG_PM */

static const struct dev_pm_ops bh1780_dev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
	SET_RUNTIME_PM_OPS(bh1780_runtime_suspend,
			   bh1780_runtime_resume, NULL)
};

static const struct i2c_device_id bh1780_id[] = {
	{ "bh1780", 0 },
	{ },
};

MODULE_DEVICE_TABLE(i2c, bh1780_id);

static const struct of_device_id of_bh1780_match[] = {
	{ .compatible = "rohm,bh1780gli", },
	{},
};
MODULE_DEVICE_TABLE(of, of_bh1780_match);

static struct i2c_driver bh1780_driver = {
	.probe		= bh1780_probe,
	.remove		= bh1780_remove,
	.id_table	= bh1780_id,
	.driver = {
		.name = "bh1780",
		.pm = &bh1780_dev_pm_ops,
		.of_match_table = of_bh1780_match,
	},
};

module_i2c_driver(bh1780_driver);

MODULE_DESCRIPTION("ROHM BH1780GLI Ambient Light Sensor Driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Linus Walleij <linus.walleij@linaro.org>");
