/**
 * IIO driver for the 3-axis accelerometer Domintech ARD10.
 *
 * Copyright (c) 2016 Hans de Goede <hdegoede@redhat.com>
 * Copyright (c) 2012 Domintech Technology Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/byteorder/generic.h>

#define DMARD10_REG_ACTR			0x00
#define DMARD10_REG_AFEM			0x0c
#define DMARD10_REG_STADR			0x12
#define DMARD10_REG_STAINT			0x1c
#define DMARD10_REG_MISC2			0x1f
#define DMARD10_REG_PD				0x21

#define DMARD10_MODE_OFF			0x00
#define DMARD10_MODE_STANDBY			0x02
#define DMARD10_MODE_ACTIVE			0x06
#define DMARD10_MODE_READ_OTP			0x12
#define DMARD10_MODE_RESET_DATA_PATH		0x82

/* AFEN set 1, ATM[2:0]=b'000 (normal), EN_Z/Y/X/T=1 */
#define DMARD10_VALUE_AFEM_AFEN_NORMAL		0x8f
/* ODR[3:0]=b'0111 (100Hz), CCK[3:0]=b'0100 (204.8kHZ) */
#define DMARD10_VALUE_CKSEL_ODR_100_204		0x74
/* INTC[6:5]=b'00 */
#define DMARD10_VALUE_INTC			0x00
/* TAP1/TAP2 Average 2 */
#define DMARD10_VALUE_TAPNS_AVE_2		0x11

#define DMARD10_VALUE_STADR			0x55
#define DMARD10_VALUE_STAINT			0xaa
#define DMARD10_VALUE_MISC2_OSCA_EN		0x08
#define DMARD10_VALUE_PD_RST			0x52

/* Offsets into the buffer read in dmard10_read_raw() */
#define DMARD10_X_OFFSET			1
#define DMARD10_Y_OFFSET			2
#define DMARD10_Z_OFFSET			3

/*
 * a value of + or -128 corresponds to + or - 1G
 * scale = 9.81 / 128 = 0.076640625
 */

static const int dmard10_nscale = 76640625;

#define DMARD10_CHANNEL(reg, axis) {	\
	.type = IIO_ACCEL,	\
	.address = reg,	\
	.modified = 1,	\
	.channel2 = IIO_MOD_##axis,	\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),	\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),	\
}

static const struct iio_chan_spec dmard10_channels[] = {
	DMARD10_CHANNEL(DMARD10_X_OFFSET, X),
	DMARD10_CHANNEL(DMARD10_Y_OFFSET, Y),
	DMARD10_CHANNEL(DMARD10_Z_OFFSET, Z),
};

struct dmard10_data {
	struct i2c_client *client;
};

/* Init sequence taken from the android driver */
static int dmard10_reset(struct i2c_client *client)
{
	unsigned char buffer[7];
	int ret;

	/* 1. Powerdown reset */
	ret = i2c_smbus_write_byte_data(client, DMARD10_REG_PD,
						DMARD10_VALUE_PD_RST);
	if (ret < 0)
		return ret;

	/*
	 * 2. ACTR => Standby mode => Download OTP to parameter reg =>
	 *    Standby mode => Reset data path => Standby mode
	 */
	buffer[0] = DMARD10_REG_ACTR;
	buffer[1] = DMARD10_MODE_STANDBY;
	buffer[2] = DMARD10_MODE_READ_OTP;
	buffer[3] = DMARD10_MODE_STANDBY;
	buffer[4] = DMARD10_MODE_RESET_DATA_PATH;
	buffer[5] = DMARD10_MODE_STANDBY;
	ret = i2c_master_send(client, buffer, 6);
	if (ret < 0)
		return ret;

	/* 3. OSCA_EN = 1, TSTO = b'000 (INT1 = normal, TEST0 = normal) */
	ret = i2c_smbus_write_byte_data(client, DMARD10_REG_MISC2,
						DMARD10_VALUE_MISC2_OSCA_EN);
	if (ret < 0)
		return ret;

	/* 4. AFEN = 1 (AFE will powerdown after ADC) */
	buffer[0] = DMARD10_REG_AFEM;
	buffer[1] = DMARD10_VALUE_AFEM_AFEN_NORMAL;
	buffer[2] = DMARD10_VALUE_CKSEL_ODR_100_204;
	buffer[3] = DMARD10_VALUE_INTC;
	buffer[4] = DMARD10_VALUE_TAPNS_AVE_2;
	buffer[5] = 0x00; /* DLYC, no delay timing */
	buffer[6] = 0x07; /* INTD=1 push-pull, INTA=1 active high, AUTOT=1 */
	ret = i2c_master_send(client, buffer, 7);
	if (ret < 0)
		return ret;

	/* 5. Activation mode */
	ret = i2c_smbus_write_byte_data(client, DMARD10_REG_ACTR,
						DMARD10_MODE_ACTIVE);
	if (ret < 0)
		return ret;

	return 0;
}

/* Shutdown sequence taken from the android driver */
static int dmard10_shutdown(struct i2c_client *client)
{
	unsigned char buffer[3];

	buffer[0] = DMARD10_REG_ACTR;
	buffer[1] = DMARD10_MODE_STANDBY;
	buffer[2] = DMARD10_MODE_OFF;

	return i2c_master_send(client, buffer, 3);
}

static int dmard10_read_raw(struct iio_dev *indio_dev,
				struct iio_chan_spec const *chan,
				int *val, int *val2, long mask)
{
	struct dmard10_data *data = iio_priv(indio_dev);
	__le16 buf[4];
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		/*
		 * Read 8 bytes starting at the REG_STADR register, trying to
		 * read the individual X, Y, Z registers will always read 0.
		 */
		ret = i2c_smbus_read_i2c_block_data(data->client,
						    DMARD10_REG_STADR,
						    sizeof(buf), (u8 *)buf);
		if (ret < 0)
			return ret;
		ret = le16_to_cpu(buf[chan->address]);
		*val = sign_extend32(ret, 12);
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		*val = 0;
		*val2 = dmard10_nscale;
		return IIO_VAL_INT_PLUS_NANO;
	default:
		return -EINVAL;
	}
}

static const struct iio_info dmard10_info = {
	.read_raw	= dmard10_read_raw,
};

static int dmard10_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	int ret;
	struct iio_dev *indio_dev;
	struct dmard10_data *data;

	/* These 2 registers have special POR reset values used for id */
	ret = i2c_smbus_read_byte_data(client, DMARD10_REG_STADR);
	if (ret != DMARD10_VALUE_STADR)
		return (ret < 0) ? ret : -ENODEV;

	ret = i2c_smbus_read_byte_data(client, DMARD10_REG_STAINT);
	if (ret != DMARD10_VALUE_STAINT)
		return (ret < 0) ? ret : -ENODEV;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
	if (!indio_dev) {
		dev_err(&client->dev, "iio allocation failed!\n");
		return -ENOMEM;
	}

	data = iio_priv(indio_dev);
	data->client = client;
	i2c_set_clientdata(client, indio_dev);

	indio_dev->dev.parent = &client->dev;
	indio_dev->info = &dmard10_info;
	indio_dev->name = "dmard10";
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = dmard10_channels;
	indio_dev->num_channels = ARRAY_SIZE(dmard10_channels);

	ret = dmard10_reset(client);
	if (ret < 0)
		return ret;

	ret = iio_device_register(indio_dev);
	if (ret < 0) {
		dev_err(&client->dev, "device_register failed\n");
		dmard10_shutdown(client);
	}

	return ret;
}

static int dmard10_remove(struct i2c_client *client)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(client);

	iio_device_unregister(indio_dev);

	return dmard10_shutdown(client);
}

#ifdef CONFIG_PM_SLEEP
static int dmard10_suspend(struct device *dev)
{
	return dmard10_shutdown(to_i2c_client(dev));
}

static int dmard10_resume(struct device *dev)
{
	return dmard10_reset(to_i2c_client(dev));
}
#endif

static SIMPLE_DEV_PM_OPS(dmard10_pm_ops, dmard10_suspend, dmard10_resume);

static const struct i2c_device_id dmard10_i2c_id[] = {
	{"dmard10", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, dmard10_i2c_id);

static struct i2c_driver dmard10_driver = {
	.driver = {
		.name = "dmard10",
		.pm = &dmard10_pm_ops,
	},
	.probe		= dmard10_probe,
	.remove		= dmard10_remove,
	.id_table	= dmard10_i2c_id,
};

module_i2c_driver(dmard10_driver);

MODULE_AUTHOR("Hans de Goede <hdegoede@redhat.com>");
MODULE_DESCRIPTION("Domintech ARD10 3-Axis Accelerometer driver");
MODULE_LICENSE("GPL v2");
