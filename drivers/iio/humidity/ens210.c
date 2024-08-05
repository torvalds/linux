// SPDX-License-Identifier: GPL-2.0+
/*
 * ens210.c - Support for ScioSense ens210 temperature & humidity sensor family
 *
 * (7-bit I2C slave address 0x43 ENS210)
 * (7-bit I2C slave address 0x43 ENS210A)
 * (7-bit I2C slave address 0x44 ENS211)
 * (7-bit I2C slave address 0x45 ENS212)
 * (7-bit I2C slave address 0x46 ENS213A)
 * (7-bit I2C slave address 0x47 ENS215)
 *
 * Datasheet:
 *  https://www.sciosense.com/wp-content/uploads/2024/04/ENS21x-Datasheet.pdf
 *  https://www.sciosense.com/wp-content/uploads/2023/12/ENS210-Datasheet.pdf
 */

#include <linux/crc7.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/iio/iio.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/types.h>

#include <asm/unaligned.h>

/* register definitions */
#define ENS210_REG_PART_ID		0x00
#define ENS210_REG_DIE_REV		0x02
#define ENS210_REG_UID			0x04
#define ENS210_REG_SYS_CTRL		0x10
#define ENS210_REG_SYS_STAT		0x11
#define ENS210_REG_SENS_RUN		0x21
#define ENS210_REG_SENS_START		0x22
#define ENS210_REG_SENS_STOP		0x23
#define ENS210_REG_SENS_STAT		0x24
#define ENS210_REG_T_VAL		0x30
#define ENS210_REG_H_VAL		0x33

/* value definitions */
#define ENS210_SENS_START_T_START		BIT(0)
#define ENS210_SENS_START_H_START		BIT(1)

#define ENS210_SENS_STAT_T_ACTIVE		BIT(0)
#define ENS210_SENS_STAT_H_ACTIVE		BIT(1)

#define ENS210_SYS_CTRL_LOW_POWER_ENABLE	BIT(0)
#define ENS210_SYS_CTRL_SYS_RESET		BIT(7)

#define ENS210_SYS_STAT_SYS_ACTIVE		BIT(0)

enum ens210_partnumber {
	ENS210	= 0x0210,
	ENS210A	= 0xa210,
	ENS211	= 0x0211,
	ENS212	= 0x0212,
	ENS213A	= 0xa213,
	ENS215	= 0x0215,
};

/**
 * struct ens210_chip_info - Humidity/Temperature chip specific information
 * @name:		name of device
 * @part_id:		chip identifier
 * @conv_time_msec:	time for conversion calculation in m/s
 */
struct ens210_chip_info {
	const char *name;
	enum ens210_partnumber part_id;
	unsigned int conv_time_msec;
};

/**
 * struct ens210_data - Humidity/Temperature sensor device structure
 * @client:	i2c client
 * @chip_info:	chip specific information
 * @lock:	lock protecting against simultaneous callers of get_measurement
 *		since multiple uninterrupted transactions are required
 */
struct ens210_data {
	struct i2c_client *client;
	const struct ens210_chip_info *chip_info;
	struct mutex lock;
};

/* calculate 17-bit crc7 */
static u8 ens210_crc7(u32 val)
{
	unsigned int val_be = (val & 0x1ffff) >> 0x8;

	return crc7_be(0xde, (u8 *)&val_be, 3) >> 1;
}

static int ens210_get_measurement(struct iio_dev *indio_dev, bool temp, int *val)
{
	struct ens210_data *data = iio_priv(indio_dev);
	struct device *dev = &data->client->dev;
	u32 regval;
	u8 regval_le[3];
	int ret;

	/* assert read */
	ret = i2c_smbus_write_byte_data(data->client, ENS210_REG_SENS_START,
					temp ? ENS210_SENS_START_T_START :
					       ENS210_SENS_START_H_START);
	if (ret)
		return ret;

	/* wait for conversion to be ready */
	msleep(data->chip_info->conv_time_msec);

	ret = i2c_smbus_read_byte_data(data->client, ENS210_REG_SENS_STAT);
	if (ret < 0)
		return ret;

	/* perform read */
	ret = i2c_smbus_read_i2c_block_data(
		data->client, temp ? ENS210_REG_T_VAL : ENS210_REG_H_VAL, 3,
		regval_le);
	if (ret < 0) {
		dev_err(dev, "failed to read register");
		return -EIO;
	}
	if (ret != 3) {
		dev_err(dev, "expected 3 bytes, received %d\n", ret);
		return -EIO;
	}

	regval = get_unaligned_le24(regval_le);
	if (ens210_crc7(regval) != ((regval >> 17) & 0x7f)) {
		dev_err(dev, "invalid crc\n");
		return -EIO;
	}

	if (!((regval >> 16) & 0x1)) {
		dev_err(dev, "data is not valid");
		return -EIO;
	}

	*val = regval & GENMASK(15, 0);
	return IIO_VAL_INT;
}

static int ens210_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *channel, int *val,
			   int *val2, long mask)
{
	struct ens210_data *data = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		scoped_guard(mutex, &data->lock) {
			ret = ens210_get_measurement(
				indio_dev, channel->type == IIO_TEMP, val);
			if (ret)
				return ret;
			return IIO_VAL_INT;
		}
		return -EINVAL; /* compiler warning workaround */
	case IIO_CHAN_INFO_SCALE:
		if (channel->type == IIO_TEMP) {
			*val = 15;
			*val2 = 625000;
		} else {
			*val = 1;
			*val2 = 953125;
		}
		return IIO_VAL_INT_PLUS_MICRO;
	case IIO_CHAN_INFO_OFFSET:
		*val = -17481;
		*val2 = 600000;
		return IIO_VAL_INT_PLUS_MICRO;
	default:
		return -EINVAL;
	}
}

static const struct iio_chan_spec ens210_channels[] = {
	{
		.type = IIO_TEMP,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_SCALE) |
				      BIT(IIO_CHAN_INFO_OFFSET),
	},
	{
		.type = IIO_HUMIDITYRELATIVE,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_SCALE),
	}
};

static const struct iio_info ens210_info = {
	.read_raw = ens210_read_raw,
};

static int ens210_probe(struct i2c_client *client)
{
	struct ens210_data *data;
	struct iio_dev *indio_dev;
	uint16_t part_id;
	int ret;

	if (!i2c_check_functionality(client->adapter,
				     I2C_FUNC_SMBUS_WRITE_BYTE_DATA |
				     I2C_FUNC_SMBUS_WRITE_BYTE |
				     I2C_FUNC_SMBUS_READ_I2C_BLOCK)) {
		return dev_err_probe(&client->dev, -EOPNOTSUPP,
			"adapter does not support some i2c transactions\n");
	}

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	data->client = client;
	mutex_init(&data->lock);
	data->chip_info = i2c_get_match_data(client);

	ret = devm_regulator_get_enable(&client->dev, "vdd");
	if (ret)
		return ret;

	/* reset device */
	ret = i2c_smbus_write_byte_data(client, ENS210_REG_SYS_CTRL,
					ENS210_SYS_CTRL_SYS_RESET);
	if (ret)
		return ret;

	/* wait for device to become active */
	usleep_range(4000, 5000);

	/* disable low power mode */
	ret = i2c_smbus_write_byte_data(client, ENS210_REG_SYS_CTRL, 0x00);
	if (ret)
		return ret;

	/* wait for device to finish */
	usleep_range(4000, 5000);

	/* get part_id */
	ret = i2c_smbus_read_word_data(client, ENS210_REG_PART_ID);
	if (ret < 0)
		return ret;
	part_id = ret;

	if (part_id != data->chip_info->part_id) {
		dev_info(&client->dev,
			 "Part ID does not match (0x%04x != 0x%04x)\n", part_id,
			 data->chip_info->part_id);
	}

	/* reenable low power */
	ret = i2c_smbus_write_byte_data(client, ENS210_REG_SYS_CTRL,
					ENS210_SYS_CTRL_LOW_POWER_ENABLE);
	if (ret)
		return ret;

	indio_dev->name = data->chip_info->name;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = ens210_channels;
	indio_dev->num_channels = ARRAY_SIZE(ens210_channels);
	indio_dev->info = &ens210_info;

	return devm_iio_device_register(&client->dev, indio_dev);
}

static const struct ens210_chip_info ens210_chip_info_data = {
	.name = "ens210",
	.part_id = ENS210,
	.conv_time_msec = 130,
};

static const struct ens210_chip_info ens210a_chip_info_data = {
	.name = "ens210a",
	.part_id = ENS210A,
	.conv_time_msec = 130,
};

static const struct ens210_chip_info ens211_chip_info_data = {
	.name = "ens211",
	.part_id = ENS211,
	.conv_time_msec = 32,
};

static const struct ens210_chip_info ens212_chip_info_data = {
	.name = "ens212",
	.part_id = ENS212,
	.conv_time_msec = 32,
};

static const struct ens210_chip_info ens213a_chip_info_data = {
	.name = "ens213a",
	.part_id = ENS213A,
	.conv_time_msec = 130,
};

static const struct ens210_chip_info ens215_chip_info_data = {
	.name = "ens215",
	.part_id = ENS215,
	.conv_time_msec = 130,
};

static const struct of_device_id ens210_of_match[] = {
	{ .compatible = "sciosense,ens210", .data = &ens210_chip_info_data },
	{ .compatible = "sciosense,ens210a", .data = &ens210a_chip_info_data },
	{ .compatible = "sciosense,ens211", .data = &ens211_chip_info_data },
	{ .compatible = "sciosense,ens212", .data = &ens212_chip_info_data },
	{ .compatible = "sciosense,ens213a", .data = &ens213a_chip_info_data },
	{ .compatible = "sciosense,ens215", .data = &ens215_chip_info_data },
	{ }
};
MODULE_DEVICE_TABLE(of, ens210_of_match);

static const struct i2c_device_id ens210_id_table[] = {
	{ "ens210", (kernel_ulong_t)&ens210_chip_info_data },
	{ "ens210a", (kernel_ulong_t)&ens210a_chip_info_data },
	{ "ens211", (kernel_ulong_t)&ens211_chip_info_data },
	{ "ens212", (kernel_ulong_t)&ens212_chip_info_data },
	{ "ens213a", (kernel_ulong_t)&ens213a_chip_info_data },
	{ "ens215", (kernel_ulong_t)&ens215_chip_info_data },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ens210_id_table);

static struct i2c_driver ens210_driver = {
	.probe = ens210_probe,
	.id_table = ens210_id_table,
	.driver = {
		.name = "ens210",
		.of_match_table = ens210_of_match,
	},
};
module_i2c_driver(ens210_driver);

MODULE_DESCRIPTION("ScioSense ENS210 temperature and humidity sensor driver");
MODULE_AUTHOR("Joshua Felmeden <jfelmeden@thegoodpenguin.co.uk>");
MODULE_LICENSE("GPL");
