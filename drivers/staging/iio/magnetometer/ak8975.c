/*
 * A sensor driver for the magnetometer AK8975.
 *
 * Magnetic compass sensor driver for monitoring magnetic flux information.
 *
 * Copyright (c) 2010, NVIDIA Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA	02110-1301, USA.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/delay.h>

#include <linux/gpio.h>

#include "../iio.h"
#include "../sysfs.h"
/*
 * Register definitions, as well as various shifts and masks to get at the
 * individual fields of the registers.
 */
#define AK8975_REG_WIA			0x00
#define AK8975_DEVICE_ID		0x48

#define AK8975_REG_INFO			0x01

#define AK8975_REG_ST1			0x02
#define AK8975_REG_ST1_DRDY_SHIFT	0
#define AK8975_REG_ST1_DRDY_MASK	(1 << AK8975_REG_ST1_DRDY_SHIFT)

#define AK8975_REG_HXL			0x03
#define AK8975_REG_HXH			0x04
#define AK8975_REG_HYL			0x05
#define AK8975_REG_HYH			0x06
#define AK8975_REG_HZL			0x07
#define AK8975_REG_HZH			0x08
#define AK8975_REG_ST2			0x09
#define AK8975_REG_ST2_DERR_SHIFT	2
#define AK8975_REG_ST2_DERR_MASK	(1 << AK8975_REG_ST2_DERR_SHIFT)

#define AK8975_REG_ST2_HOFL_SHIFT	3
#define AK8975_REG_ST2_HOFL_MASK	(1 << AK8975_REG_ST2_HOFL_SHIFT)

#define AK8975_REG_CNTL			0x0A
#define AK8975_REG_CNTL_MODE_SHIFT	0
#define AK8975_REG_CNTL_MODE_MASK	(0xF << AK8975_REG_CNTL_MODE_SHIFT)
#define AK8975_REG_CNTL_MODE_POWER_DOWN	0
#define AK8975_REG_CNTL_MODE_ONCE	1
#define AK8975_REG_CNTL_MODE_SELF_TEST	8
#define AK8975_REG_CNTL_MODE_FUSE_ROM	0xF

#define AK8975_REG_RSVC			0x0B
#define AK8975_REG_ASTC			0x0C
#define AK8975_REG_TS1			0x0D
#define AK8975_REG_TS2			0x0E
#define AK8975_REG_I2CDIS		0x0F
#define AK8975_REG_ASAX			0x10
#define AK8975_REG_ASAY			0x11
#define AK8975_REG_ASAZ			0x12

#define AK8975_MAX_REGS			AK8975_REG_ASAZ

/*
 * Miscellaneous values.
 */
#define AK8975_MAX_CONVERSION_TIMEOUT	500
#define AK8975_CONVERSION_DONE_POLL_TIME 10

/*
 * Per-instance context data for the device.
 */
struct ak8975_data {
	struct i2c_client	*client;
	struct attribute_group	attrs;
	struct mutex		lock;
	u8			asa[3];
	long			raw_to_gauss[3];
	bool			mode;
	u8			reg_cache[AK8975_MAX_REGS];
	int			eoc_gpio;
	int			eoc_irq;
};

static const int ak8975_index_to_reg[] = {
	AK8975_REG_HXL, AK8975_REG_HYL, AK8975_REG_HZL,
};

/*
 * Helper function to write to the I2C device's registers.
 */
static int ak8975_write_data(struct i2c_client *client,
			     u8 reg, u8 val, u8 mask, u8 shift)
{
	struct ak8975_data *data = i2c_get_clientdata(client);
	u8 regval;
	int ret;

	regval = (data->reg_cache[reg] & ~mask) | (val << shift);
	ret = i2c_smbus_write_byte_data(client, reg, regval);
	if (ret < 0) {
		dev_err(&client->dev, "Write to device fails status %x\n", ret);
		return ret;
	}
	data->reg_cache[reg] = regval;

	return 0;
}

/*
 * Helper function to read a contiguous set of the I2C device's registers.
 */
static int ak8975_read_data(struct i2c_client *client,
			    u8 reg, u8 length, u8 *buffer)
{
	int ret;
	struct i2c_msg msg[2] = {
		{
			.addr = client->addr,
			.flags = I2C_M_NOSTART,
			.len = 1,
			.buf = &reg,
		}, {
			.addr = client->addr,
			.flags = I2C_M_RD,
			.len = length,
			.buf = buffer,
		}
	};

	ret = i2c_transfer(client->adapter, msg, 2);
	if (ret < 0) {
		dev_err(&client->dev, "Read from device fails\n");
		return ret;
	}

	return 0;
}

/*
 * Perform some start-of-day setup, including reading the asa calibration
 * values and caching them.
 */
static int ak8975_setup(struct i2c_client *client)
{
	struct ak8975_data *data = i2c_get_clientdata(client);
	u8 device_id;
	int ret;

	/* Confirm that the device we're talking to is really an AK8975. */
	ret = ak8975_read_data(client, AK8975_REG_WIA, 1, &device_id);
	if (ret < 0) {
		dev_err(&client->dev, "Error reading WIA\n");
		return ret;
	}
	if (device_id != AK8975_DEVICE_ID) {
		dev_err(&client->dev, "Device ak8975 not found\n");
		return -ENODEV;
	}

	/* Write the fused rom access mode. */
	ret = ak8975_write_data(client,
				AK8975_REG_CNTL,
				AK8975_REG_CNTL_MODE_FUSE_ROM,
				AK8975_REG_CNTL_MODE_MASK,
				AK8975_REG_CNTL_MODE_SHIFT);
	if (ret < 0) {
		dev_err(&client->dev, "Error in setting fuse access mode\n");
		return ret;
	}

	/* Get asa data and store in the device data. */
	ret = ak8975_read_data(client, AK8975_REG_ASAX, 3, data->asa);
	if (ret < 0) {
		dev_err(&client->dev, "Not able to read asa data\n");
		return ret;
	}

/*
 * Precalculate scale factor (in Gauss units) for each axis and
 * store in the device data.
 *
 * This scale factor is axis-dependent, and is derived from 3 calibration
 * factors ASA(x), ASA(y), and ASA(z).
 *
 * These ASA values are read from the sensor device at start of day, and
 * cached in the device context struct.
 *
 * Adjusting the flux value with the sensitivity adjustment value should be
 * done via the following formula:
 *
 * Hadj = H * ( ( ( (ASA-128)*0.5 ) / 128 ) + 1 )
 *
 * where H is the raw value, ASA is the sensitivity adjustment, and Hadj
 * is the resultant adjusted value.
 *
 * We reduce the formula to:
 *
 * Hadj = H * (ASA + 128) / 256
 *
 * H is in the range of -4096 to 4095.  The magnetometer has a range of
 * +-1229uT.  To go from the raw value to uT is:
 *
 * HuT = H * 1229/4096, or roughly, 3/10.
 *
 * Since 1uT = 100 gauss, our final scale factor becomes:
 *
 * Hadj = H * ((ASA + 128) / 256) * 3/10 * 100
 * Hadj = H * ((ASA + 128) * 30 / 256
 *
 * Since ASA doesn't change, we cache the resultant scale factor into the
 * device context in ak8975_setup().
 */
	data->raw_to_gauss[0] = ((data->asa[0] + 128) * 30) >> 8;
	data->raw_to_gauss[1] = ((data->asa[1] + 128) * 30) >> 8;
	data->raw_to_gauss[2] = ((data->asa[2] + 128) * 30) >> 8;

	return 0;
}

/*
 * Shows the device's mode.  0 = off, 1 = on.
 */
static ssize_t show_mode(struct device *dev, struct device_attribute *devattr,
			 char *buf)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct ak8975_data *data = iio_priv(indio_dev);

	return sprintf(buf, "%u\n", data->mode);
}

/*
 * Sets the device's mode.  0 = off, 1 = on.  The device's mode must be on
 * for the magn raw attributes to be available.
 */
static ssize_t store_mode(struct device *dev, struct device_attribute *devattr,
			  const char *buf, size_t count)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct ak8975_data *data = iio_priv(indio_dev);
	struct i2c_client *client = data->client;
	bool value;
	int ret;

	/* Convert mode string and do some basic sanity checking on it.
	   only 0 or 1 are valid. */
	ret = strtobool(buf, &value);
	if (ret < 0)
		return ret;

	mutex_lock(&data->lock);

	/* Write the mode to the device. */
	if (data->mode != value) {
		ret = ak8975_write_data(client,
					AK8975_REG_CNTL,
					(u8)value,
					AK8975_REG_CNTL_MODE_MASK,
					AK8975_REG_CNTL_MODE_SHIFT);

		if (ret < 0) {
			dev_err(&client->dev, "Error in setting mode\n");
			mutex_unlock(&data->lock);
			return ret;
		}
		data->mode = value;
	}

	mutex_unlock(&data->lock);

	return count;
}

static int wait_conversion_complete_gpio(struct ak8975_data *data)
{
	struct i2c_client *client = data->client;
	u8 read_status;
	u32 timeout_ms = AK8975_MAX_CONVERSION_TIMEOUT;
	int ret;

	/* Wait for the conversion to complete. */
	while (timeout_ms) {
		msleep(AK8975_CONVERSION_DONE_POLL_TIME);
		if (gpio_get_value(data->eoc_gpio))
			break;
		timeout_ms -= AK8975_CONVERSION_DONE_POLL_TIME;
	}
	if (!timeout_ms) {
		dev_err(&client->dev, "Conversion timeout happened\n");
		return -EINVAL;
	}

	ret = ak8975_read_data(client, AK8975_REG_ST1, 1, &read_status);
	if (ret < 0) {
		dev_err(&client->dev, "Error in reading ST1\n");
		return ret;
	}
	return read_status;
}

static int wait_conversion_complete_polled(struct ak8975_data *data)
{
	struct i2c_client *client = data->client;
	u8 read_status;
	u32 timeout_ms = AK8975_MAX_CONVERSION_TIMEOUT;
	int ret;

	/* Wait for the conversion to complete. */
	while (timeout_ms) {
		msleep(AK8975_CONVERSION_DONE_POLL_TIME);
		ret = ak8975_read_data(client, AK8975_REG_ST1, 1, &read_status);
		if (ret < 0) {
			dev_err(&client->dev, "Error in reading ST1\n");
			return ret;
		}
		if (read_status)
			break;
		timeout_ms -= AK8975_CONVERSION_DONE_POLL_TIME;
	}
	if (!timeout_ms) {
		dev_err(&client->dev, "Conversion timeout happened\n");
		return -EINVAL;
	}
	return read_status;
}

/*
 * Emits the raw flux value for the x, y, or z axis.
 */
static int ak8975_read_axis(struct iio_dev *indio_dev, int index, int *val)
{
	struct ak8975_data *data = iio_priv(indio_dev);
	struct i2c_client *client = data->client;
	u16 meas_reg;
	s16 raw;
	u8 read_status;
	int ret;

	mutex_lock(&data->lock);

	if (data->mode == 0) {
		dev_err(&client->dev, "Operating mode is in power down mode\n");
		ret = -EBUSY;
		goto exit;
	}

	/* Set up the device for taking a sample. */
	ret = ak8975_write_data(client,
				AK8975_REG_CNTL,
				AK8975_REG_CNTL_MODE_ONCE,
				AK8975_REG_CNTL_MODE_MASK,
				AK8975_REG_CNTL_MODE_SHIFT);
	if (ret < 0) {
		dev_err(&client->dev, "Error in setting operating mode\n");
		goto exit;
	}

	/* Wait for the conversion to complete. */
	if (gpio_is_valid(data->eoc_gpio))
		ret = wait_conversion_complete_gpio(data);
	else
		ret = wait_conversion_complete_polled(data);
	if (ret < 0)
		goto exit;

	read_status = ret;

	if (read_status & AK8975_REG_ST1_DRDY_MASK) {
		ret = ak8975_read_data(client, AK8975_REG_ST2, 1, &read_status);
		if (ret < 0) {
			dev_err(&client->dev, "Error in reading ST2\n");
			goto exit;
		}
		if (read_status & (AK8975_REG_ST2_DERR_MASK |
				   AK8975_REG_ST2_HOFL_MASK)) {
			dev_err(&client->dev, "ST2 status error 0x%x\n",
				read_status);
			ret = -EINVAL;
			goto exit;
		}
	}

	/* Read the flux value from the appropriate register
	   (the register is specified in the iio device attributes). */
	ret = ak8975_read_data(client, ak8975_index_to_reg[index],
			       2, (u8 *)&meas_reg);
	if (ret < 0) {
		dev_err(&client->dev, "Read axis data fails\n");
		goto exit;
	}

	mutex_unlock(&data->lock);

	/* Endian conversion of the measured values. */
	raw = (s16) (le16_to_cpu(meas_reg));

	/* Clamp to valid range. */
	raw = clamp_t(s16, raw, -4096, 4095);
	*val = raw;
	return IIO_VAL_INT;

exit:
	mutex_unlock(&data->lock);
	return ret;
}

static int ak8975_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val, int *val2,
			   long mask)
{
	struct ak8975_data *data = iio_priv(indio_dev);

	switch (mask) {
	case 0:
		return ak8975_read_axis(indio_dev, chan->address, val);
	case IIO_CHAN_INFO_SCALE:
		*val = data->raw_to_gauss[chan->address];
		return IIO_VAL_INT;
	}
	return -EINVAL;
}

#define AK8975_CHANNEL(axis, index)					\
	{								\
		.type = IIO_MAGN,					\
		.modified = 1,						\
		.channel2 = IIO_MOD_##axis,				\
		.info_mask = IIO_CHAN_INFO_SCALE_SEPARATE_BIT,	\
		.address = index,					\
	}

static const struct iio_chan_spec ak8975_channels[] = {
	AK8975_CHANNEL(X, 0), AK8975_CHANNEL(Y, 1), AK8975_CHANNEL(Z, 2),
};

static IIO_DEVICE_ATTR(mode, S_IRUGO | S_IWUSR, show_mode, store_mode, 0);

static struct attribute *ak8975_attr[] = {
	&iio_dev_attr_mode.dev_attr.attr,
	NULL
};

static struct attribute_group ak8975_attr_group = {
	.attrs = ak8975_attr,
};

static const struct iio_info ak8975_info = {
	.attrs = &ak8975_attr_group,
	.read_raw = &ak8975_read_raw,
	.driver_module = THIS_MODULE,
};

static int ak8975_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct ak8975_data *data;
	struct iio_dev *indio_dev;
	int eoc_gpio;
	int err;

	/* Grab and set up the supplied GPIO. */
	if (client->dev.platform_data == NULL)
		eoc_gpio = -1;
	else
		eoc_gpio = *(int *)(client->dev.platform_data);

	/* We may not have a GPIO based IRQ to scan, that is fine, we will
	   poll if so */
	if (gpio_is_valid(eoc_gpio)) {
		err = gpio_request(eoc_gpio, "ak_8975");
		if (err < 0) {
			dev_err(&client->dev,
				"failed to request GPIO %d, error %d\n",
							eoc_gpio, err);
			goto exit;
		}

		err = gpio_direction_input(eoc_gpio);
		if (err < 0) {
			dev_err(&client->dev,
				"Failed to configure input direction for GPIO %d, error %d\n",
						eoc_gpio, err);
			goto exit_gpio;
		}
	}

	/* Register with IIO */
	indio_dev = iio_allocate_device(sizeof(*data));
	if (indio_dev == NULL) {
		err = -ENOMEM;
		goto exit_gpio;
	}
	data = iio_priv(indio_dev);
	/* Perform some basic start-of-day setup of the device. */
	err = ak8975_setup(client);
	if (err < 0) {
		dev_err(&client->dev, "AK8975 initialization fails\n");
		goto exit_free_iio;
	}

	i2c_set_clientdata(client, indio_dev);
	data->client = client;
	mutex_init(&data->lock);
	data->eoc_irq = client->irq;
	data->eoc_gpio = eoc_gpio;
	indio_dev->dev.parent = &client->dev;
	indio_dev->channels = ak8975_channels;
	indio_dev->num_channels = ARRAY_SIZE(ak8975_channels);
	indio_dev->info = &ak8975_info;
	indio_dev->modes = INDIO_DIRECT_MODE;

	err = iio_device_register(indio_dev);
	if (err < 0)
		goto exit_free_iio;

	return 0;

exit_free_iio:
	iio_free_device(indio_dev);
exit_gpio:
	if (gpio_is_valid(eoc_gpio))
		gpio_free(eoc_gpio);
exit:
	return err;
}

static int ak8975_remove(struct i2c_client *client)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(client);
	struct ak8975_data *data = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);

	if (gpio_is_valid(data->eoc_gpio))
		gpio_free(data->eoc_gpio);

	iio_free_device(indio_dev);

	return 0;
}

static const struct i2c_device_id ak8975_id[] = {
	{"ak8975", 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, ak8975_id);

static struct i2c_driver ak8975_driver = {
	.driver = {
		.name	= "ak8975",
	},
	.probe		= ak8975_probe,
	.remove		= __devexit_p(ak8975_remove),
	.id_table	= ak8975_id,
};
module_i2c_driver(ak8975_driver);

MODULE_AUTHOR("Laxman Dewangan <ldewangan@nvidia.com>");
MODULE_DESCRIPTION("AK8975 magnetometer driver");
MODULE_LICENSE("GPL");
