// SPDX-License-Identifier: GPL-2.0+
/*
 * vz89x.c - Support for SGX Sensortech MiCS VZ89X VOC sensors
 *
 * Copyright (C) 2015-2018
 * Author: Matt Ranostay <matt.ranostay@konsulko.com>
 */

#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/of.h>
#include <linux/of_device.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>

#define VZ89X_REG_MEASUREMENT		0x09
#define VZ89X_REG_MEASUREMENT_RD_SIZE	6
#define VZ89X_REG_MEASUREMENT_WR_SIZE	3

#define VZ89X_VOC_CO2_IDX		0
#define VZ89X_VOC_SHORT_IDX		1
#define VZ89X_VOC_TVOC_IDX		2
#define VZ89X_VOC_RESISTANCE_IDX	3

#define VZ89TE_REG_MEASUREMENT		0x0c
#define VZ89TE_REG_MEASUREMENT_RD_SIZE	7
#define VZ89TE_REG_MEASUREMENT_WR_SIZE	6

#define VZ89TE_VOC_TVOC_IDX		0
#define VZ89TE_VOC_CO2_IDX		1
#define VZ89TE_VOC_RESISTANCE_IDX	2

enum {
	VZ89X,
	VZ89TE,
};

struct vz89x_chip_data;

struct vz89x_data {
	struct i2c_client *client;
	const struct vz89x_chip_data *chip;
	struct mutex lock;
	int (*xfer)(struct vz89x_data *data, u8 cmd);

	bool is_valid;
	unsigned long last_update;
	u8 buffer[VZ89TE_REG_MEASUREMENT_RD_SIZE];
};

struct vz89x_chip_data {
	bool (*valid)(struct vz89x_data *data);
	const struct iio_chan_spec *channels;
	u8 num_channels;

	u8 cmd;
	u8 read_size;
	u8 write_size;
};

static const struct iio_chan_spec vz89x_channels[] = {
	{
		.type = IIO_CONCENTRATION,
		.channel2 = IIO_MOD_CO2,
		.modified = 1,
		.info_mask_separate =
			BIT(IIO_CHAN_INFO_OFFSET) | BIT(IIO_CHAN_INFO_RAW),
		.address = VZ89X_VOC_CO2_IDX,
	},
	{
		.type = IIO_CONCENTRATION,
		.channel2 = IIO_MOD_VOC,
		.modified = 1,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.address = VZ89X_VOC_SHORT_IDX,
		.extend_name = "short",
	},
	{
		.type = IIO_CONCENTRATION,
		.channel2 = IIO_MOD_VOC,
		.modified = 1,
		.info_mask_separate =
			BIT(IIO_CHAN_INFO_OFFSET) | BIT(IIO_CHAN_INFO_RAW),
		.address = VZ89X_VOC_TVOC_IDX,
	},
	{
		.type = IIO_RESISTANCE,
		.info_mask_separate =
			BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_SCALE),
		.address = VZ89X_VOC_RESISTANCE_IDX,
		.scan_index = -1,
		.scan_type = {
			.endianness = IIO_LE,
		},
	},
};

static const struct iio_chan_spec vz89te_channels[] = {
	{
		.type = IIO_CONCENTRATION,
		.channel2 = IIO_MOD_VOC,
		.modified = 1,
		.info_mask_separate =
			BIT(IIO_CHAN_INFO_OFFSET) | BIT(IIO_CHAN_INFO_RAW),
		.address = VZ89TE_VOC_TVOC_IDX,
	},

	{
		.type = IIO_CONCENTRATION,
		.channel2 = IIO_MOD_CO2,
		.modified = 1,
		.info_mask_separate =
			BIT(IIO_CHAN_INFO_OFFSET) | BIT(IIO_CHAN_INFO_RAW),
		.address = VZ89TE_VOC_CO2_IDX,
	},
	{
		.type = IIO_RESISTANCE,
		.info_mask_separate =
			BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_SCALE),
		.address = VZ89TE_VOC_RESISTANCE_IDX,
		.scan_index = -1,
		.scan_type = {
			.endianness = IIO_BE,
		},
	},
};

static IIO_CONST_ATTR(in_concentration_co2_scale, "0.00000698689");
static IIO_CONST_ATTR(in_concentration_voc_scale, "0.00000000436681223");

static struct attribute *vz89x_attributes[] = {
	&iio_const_attr_in_concentration_co2_scale.dev_attr.attr,
	&iio_const_attr_in_concentration_voc_scale.dev_attr.attr,
	NULL,
};

static const struct attribute_group vz89x_attrs_group = {
	.attrs = vz89x_attributes,
};

/*
 * Chipset sometime updates in the middle of a reading causing it to reset the
 * data pointer, and causing invalid reading of previous data.
 * We can check for this by reading MSB of the resistance reading that is
 * always zero, and by also confirming the VOC_short isn't zero.
 */

static bool vz89x_measurement_is_valid(struct vz89x_data *data)
{
	if (data->buffer[VZ89X_VOC_SHORT_IDX] == 0)
		return true;

	return !!(data->buffer[data->chip->read_size - 1] > 0);
}

/* VZ89TE device has a modified CRC-8 two complement check */
static bool vz89te_measurement_is_valid(struct vz89x_data *data)
{
	u8 crc = 0;
	int i, sum = 0;

	for (i = 0; i < (data->chip->read_size - 1); i++) {
		sum = crc + data->buffer[i];
		crc = sum;
		crc += sum / 256;
	}

	return !((0xff - crc) == data->buffer[data->chip->read_size - 1]);
}

static int vz89x_i2c_xfer(struct vz89x_data *data, u8 cmd)
{
	const struct vz89x_chip_data *chip = data->chip;
	struct i2c_client *client = data->client;
	struct i2c_msg msg[2];
	int ret;
	u8 buf[6] = { cmd, 0, 0, 0, 0, 0xf3 };

	msg[0].addr = client->addr;
	msg[0].flags = client->flags;
	msg[0].len = chip->write_size;
	msg[0].buf  = (char *) &buf;

	msg[1].addr = client->addr;
	msg[1].flags = client->flags | I2C_M_RD;
	msg[1].len = chip->read_size;
	msg[1].buf = (char *) &data->buffer;

	ret = i2c_transfer(client->adapter, msg, 2);

	return (ret == 2) ? 0 : ret;
}

static int vz89x_smbus_xfer(struct vz89x_data *data, u8 cmd)
{
	struct i2c_client *client = data->client;
	int ret;
	int i;

	ret = i2c_smbus_write_word_data(client, cmd, 0);
	if (ret < 0)
		return ret;

	for (i = 0; i < data->chip->read_size; i++) {
		ret = i2c_smbus_read_byte(client);
		if (ret < 0)
			return ret;
		data->buffer[i] = ret;
	}

	return 0;
}

static int vz89x_get_measurement(struct vz89x_data *data)
{
	const struct vz89x_chip_data *chip = data->chip;
	int ret;

	/* sensor can only be polled once a second max per datasheet */
	if (!time_after(jiffies, data->last_update + HZ))
		return data->is_valid ? 0 : -EAGAIN;

	data->is_valid = false;
	data->last_update = jiffies;

	ret = data->xfer(data, chip->cmd);
	if (ret < 0)
		return ret;

	ret = chip->valid(data);
	if (ret)
		return -EAGAIN;

	data->is_valid = true;

	return 0;
}

static int vz89x_get_resistance_reading(struct vz89x_data *data,
					struct iio_chan_spec const *chan,
					int *val)
{
	u8 *tmp = (u8 *) &data->buffer[chan->address];

	switch (chan->scan_type.endianness) {
	case IIO_LE:
		*val = le32_to_cpup((__le32 *) tmp) & GENMASK(23, 0);
		break;
	case IIO_BE:
		*val = be32_to_cpup((__be32 *) tmp) >> 8;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int vz89x_read_raw(struct iio_dev *indio_dev,
			  struct iio_chan_spec const *chan, int *val,
			  int *val2, long mask)
{
	struct vz89x_data *data = iio_priv(indio_dev);
	int ret = -EINVAL;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		mutex_lock(&data->lock);
		ret = vz89x_get_measurement(data);
		mutex_unlock(&data->lock);

		if (ret)
			return ret;

		switch (chan->type) {
		case IIO_CONCENTRATION:
			*val = data->buffer[chan->address];
			return IIO_VAL_INT;
		case IIO_RESISTANCE:
			ret = vz89x_get_resistance_reading(data, chan, val);
			if (!ret)
				return IIO_VAL_INT;
			break;
		default:
			return -EINVAL;
		}
		break;
	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_RESISTANCE:
			*val = 10;
			return IIO_VAL_INT;
		default:
			return -EINVAL;
		}
		break;
	case IIO_CHAN_INFO_OFFSET:
		switch (chan->channel2) {
		case IIO_MOD_CO2:
			*val = 44;
			*val2 = 250000;
			return IIO_VAL_INT_PLUS_MICRO;
		case IIO_MOD_VOC:
			*val = -13;
			return IIO_VAL_INT;
		default:
			return -EINVAL;
		}
	}

	return ret;
}

static const struct iio_info vz89x_info = {
	.attrs		= &vz89x_attrs_group,
	.read_raw	= vz89x_read_raw,
};

static const struct vz89x_chip_data vz89x_chips[] = {
	{
		.valid = vz89x_measurement_is_valid,

		.cmd = VZ89X_REG_MEASUREMENT,
		.read_size = VZ89X_REG_MEASUREMENT_RD_SIZE,
		.write_size = VZ89X_REG_MEASUREMENT_WR_SIZE,

		.channels = vz89x_channels,
		.num_channels = ARRAY_SIZE(vz89x_channels),
	},
	{
		.valid = vz89te_measurement_is_valid,

		.cmd = VZ89TE_REG_MEASUREMENT,
		.read_size = VZ89TE_REG_MEASUREMENT_RD_SIZE,
		.write_size = VZ89TE_REG_MEASUREMENT_WR_SIZE,

		.channels = vz89te_channels,
		.num_channels = ARRAY_SIZE(vz89te_channels),
	},
};

static const struct of_device_id vz89x_dt_ids[] = {
	{ .compatible = "sgx,vz89x", .data = (void *) VZ89X },
	{ .compatible = "sgx,vz89te", .data = (void *) VZ89TE },
	{ }
};
MODULE_DEVICE_TABLE(of, vz89x_dt_ids);

static int vz89x_probe(struct i2c_client *client,
		       const struct i2c_device_id *id)
{
	struct iio_dev *indio_dev;
	struct vz89x_data *data;
	const struct of_device_id *of_id;
	int chip_id;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;
	data = iio_priv(indio_dev);

	if (i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		data->xfer = vz89x_i2c_xfer;
	else if (i2c_check_functionality(client->adapter,
				I2C_FUNC_SMBUS_WORD_DATA | I2C_FUNC_SMBUS_BYTE))
		data->xfer = vz89x_smbus_xfer;
	else
		return -EOPNOTSUPP;

	of_id = of_match_device(vz89x_dt_ids, &client->dev);
	if (!of_id)
		chip_id = id->driver_data;
	else
		chip_id = (unsigned long)of_id->data;

	i2c_set_clientdata(client, indio_dev);
	data->client = client;
	data->chip = &vz89x_chips[chip_id];
	data->last_update = jiffies - HZ;
	mutex_init(&data->lock);

	indio_dev->dev.parent = &client->dev;
	indio_dev->info = &vz89x_info;
	indio_dev->name = dev_name(&client->dev);
	indio_dev->modes = INDIO_DIRECT_MODE;

	indio_dev->channels = data->chip->channels;
	indio_dev->num_channels = data->chip->num_channels;

	return devm_iio_device_register(&client->dev, indio_dev);
}

static const struct i2c_device_id vz89x_id[] = {
	{ "vz89x", VZ89X },
	{ "vz89te", VZ89TE },
	{ }
};
MODULE_DEVICE_TABLE(i2c, vz89x_id);

static struct i2c_driver vz89x_driver = {
	.driver = {
		.name	= "vz89x",
		.of_match_table = of_match_ptr(vz89x_dt_ids),
	},
	.probe = vz89x_probe,
	.id_table = vz89x_id,
};
module_i2c_driver(vz89x_driver);

MODULE_AUTHOR("Matt Ranostay <matt.ranostay@konsulko.com>");
MODULE_DESCRIPTION("SGX Sensortech MiCS VZ89X VOC sensors");
MODULE_LICENSE("GPL v2");
