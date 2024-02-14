// SPDX-License-Identifier: GPL-2.0+
/*
 * hdc3020.c - Support for the TI HDC3020,HDC3021 and HDC3022
 * temperature + relative humidity sensors
 *
 * Copyright (C) 2023
 *
 * Datasheet: https://www.ti.com/lit/ds/symlink/hdc3020.pdf
 */

#include <linux/bitops.h>
#include <linux/cleanup.h>
#include <linux/crc8.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mutex.h>

#include <asm/unaligned.h>

#include <linux/iio/iio.h>

#define HDC3020_S_AUTO_10HZ_MOD0	0x2737
#define HDC3020_HEATER_DISABLE		0x3066
#define HDC3020_HEATER_ENABLE		0x306D
#define HDC3020_HEATER_CONFIG		0x306E
#define HDC3020_EXIT_AUTO		0x3093
#define HDC3020_R_T_RH_AUTO		0xE000
#define HDC3020_R_T_LOW_AUTO		0xE002
#define HDC3020_R_T_HIGH_AUTO		0xE003
#define HDC3020_R_RH_LOW_AUTO		0xE004
#define HDC3020_R_RH_HIGH_AUTO		0xE005

#define HDC3020_READ_RETRY_TIMES	10
#define HDC3020_BUSY_DELAY_MS		10

#define HDC3020_CRC8_POLYNOMIAL		0x31

struct hdc3020_data {
	struct i2c_client *client;
	/*
	 * Ensure that the sensor configuration (currently only heater is
	 * supported) will not be changed during the process of reading
	 * sensor data (this driver will try HDC3020_READ_RETRY_TIMES times
	 * if the device does not respond).
	 */
	struct mutex lock;
};

static const int hdc3020_heater_vals[] = {0, 1, 0x3FFF};

static const struct iio_chan_spec hdc3020_channels[] = {
	{
		.type = IIO_TEMP,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
		BIT(IIO_CHAN_INFO_SCALE) | BIT(IIO_CHAN_INFO_PEAK) |
		BIT(IIO_CHAN_INFO_TROUGH) | BIT(IIO_CHAN_INFO_OFFSET),
	},
	{
		.type = IIO_HUMIDITYRELATIVE,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
		BIT(IIO_CHAN_INFO_SCALE) | BIT(IIO_CHAN_INFO_PEAK) |
		BIT(IIO_CHAN_INFO_TROUGH),
	},
	{
		/*
		 * For setting the internal heater, which can be switched on to
		 * prevent or remove any condensation that may develop when the
		 * ambient environment approaches its dew point temperature.
		 */
		.type = IIO_CURRENT,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_separate_available = BIT(IIO_CHAN_INFO_RAW),
		.output = 1,
	},
};

DECLARE_CRC8_TABLE(hdc3020_crc8_table);

static int hdc3020_write_bytes(struct hdc3020_data *data, u8 *buf, u8 len)
{
	struct i2c_client *client = data->client;
	struct i2c_msg msg;
	int ret, cnt;

	msg.addr = client->addr;
	msg.flags = 0;
	msg.buf = buf;
	msg.len = len;

	/*
	 * During the measurement process, HDC3020 will not return data.
	 * So wait for a while and try again
	 */
	for (cnt = 0; cnt < HDC3020_READ_RETRY_TIMES; cnt++) {
		ret = i2c_transfer(client->adapter, &msg, 1);
		if (ret == 1)
			return 0;

		mdelay(HDC3020_BUSY_DELAY_MS);
	}
	dev_err(&client->dev, "Could not write sensor command\n");

	return -ETIMEDOUT;
}

static
int hdc3020_read_bytes(struct hdc3020_data *data, u16 reg, u8 *buf, int len)
{
	u8 reg_buf[2];
	int ret, cnt;
	struct i2c_client *client = data->client;
	struct i2c_msg msg[2] = {
		[0] = {
			.addr = client->addr,
			.flags = 0,
			.buf = reg_buf,
			.len = 2,
		},
		[1] = {
			.addr = client->addr,
			.flags = I2C_M_RD,
			.buf = buf,
			.len = len,
		},
	};

	put_unaligned_be16(reg, reg_buf);
	/*
	 * During the measurement process, HDC3020 will not return data.
	 * So wait for a while and try again
	 */
	for (cnt = 0; cnt < HDC3020_READ_RETRY_TIMES; cnt++) {
		ret = i2c_transfer(client->adapter, msg, 2);
		if (ret == 2)
			return 0;

		mdelay(HDC3020_BUSY_DELAY_MS);
	}
	dev_err(&client->dev, "Could not read sensor data\n");

	return -ETIMEDOUT;
}

static int hdc3020_read_be16(struct hdc3020_data *data, u16 reg)
{
	u8 crc, buf[3];
	int ret;

	ret = hdc3020_read_bytes(data, reg, buf, 3);
	if (ret < 0)
		return ret;

	crc = crc8(hdc3020_crc8_table, buf, 2, CRC8_INIT_VALUE);
	if (crc != buf[2])
		return -EINVAL;

	return get_unaligned_be16(buf);
}

static int hdc3020_exec_cmd(struct hdc3020_data *data, u16 reg)
{
	u8 reg_buf[2];

	put_unaligned_be16(reg, reg_buf);
	return hdc3020_write_bytes(data, reg_buf, 2);
}

static int hdc3020_read_measurement(struct hdc3020_data *data,
				    enum iio_chan_type type, int *val)
{
	u8 crc, buf[6];
	int ret;

	ret = hdc3020_read_bytes(data, HDC3020_R_T_RH_AUTO, buf, 6);
	if (ret < 0)
		return ret;

	/* CRC check of the temperature measurement */
	crc = crc8(hdc3020_crc8_table, buf, 2, CRC8_INIT_VALUE);
	if (crc != buf[2])
		return -EINVAL;

	/* CRC check of the relative humidity measurement */
	crc = crc8(hdc3020_crc8_table, buf + 3, 2, CRC8_INIT_VALUE);
	if (crc != buf[5])
		return -EINVAL;

	if (type == IIO_TEMP)
		*val = get_unaligned_be16(buf);
	else if (type == IIO_HUMIDITYRELATIVE)
		*val = get_unaligned_be16(&buf[3]);
	else
		return -EINVAL;

	return 0;
}

static int hdc3020_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan, int *val,
			    int *val2, long mask)
{
	struct hdc3020_data *data = iio_priv(indio_dev);
	int ret;

	if (chan->type != IIO_TEMP && chan->type != IIO_HUMIDITYRELATIVE)
		return -EINVAL;

	switch (mask) {
	case IIO_CHAN_INFO_RAW: {
		guard(mutex)(&data->lock);
		ret = hdc3020_read_measurement(data, chan->type, val);
		if (ret < 0)
			return ret;

		return IIO_VAL_INT;
	}
	case IIO_CHAN_INFO_PEAK: {
		guard(mutex)(&data->lock);
		if (chan->type == IIO_TEMP)
			ret = hdc3020_read_be16(data, HDC3020_R_T_HIGH_AUTO);
		else
			ret = hdc3020_read_be16(data, HDC3020_R_RH_HIGH_AUTO);

		if (ret < 0)
			return ret;

		*val = ret;
		return IIO_VAL_INT;
	}
	case IIO_CHAN_INFO_TROUGH: {
		guard(mutex)(&data->lock);
		if (chan->type == IIO_TEMP)
			ret = hdc3020_read_be16(data, HDC3020_R_T_LOW_AUTO);
		else
			ret = hdc3020_read_be16(data, HDC3020_R_RH_LOW_AUTO);

		if (ret < 0)
			return ret;

		*val = ret;
		return IIO_VAL_INT;
	}
	case IIO_CHAN_INFO_SCALE:
		*val2 = 65536;
		if (chan->type == IIO_TEMP)
			*val = 175;
		else
			*val = 100;
		return IIO_VAL_FRACTIONAL;

	case IIO_CHAN_INFO_OFFSET:
		if (chan->type != IIO_TEMP)
			return -EINVAL;

		*val = 16852;
		return IIO_VAL_INT;

	default:
		return -EINVAL;
	}
}

static int hdc3020_read_available(struct iio_dev *indio_dev,
				  struct iio_chan_spec const *chan,
				  const int **vals,
				  int *type, int *length, long mask)
{
	if (mask != IIO_CHAN_INFO_RAW || chan->type != IIO_CURRENT)
		return -EINVAL;

	*vals = hdc3020_heater_vals;
	*type = IIO_VAL_INT;

	return IIO_AVAIL_RANGE;
}

static int hdc3020_update_heater(struct hdc3020_data *data, int val)
{
	u8 buf[5];
	int ret;

	if (val < hdc3020_heater_vals[0] || val > hdc3020_heater_vals[2])
		return -EINVAL;

	if (!val)
		hdc3020_exec_cmd(data, HDC3020_HEATER_DISABLE);

	put_unaligned_be16(HDC3020_HEATER_CONFIG, buf);
	put_unaligned_be16(val & GENMASK(13, 0), &buf[2]);
	buf[4] = crc8(hdc3020_crc8_table, buf + 2, 2, CRC8_INIT_VALUE);
	ret = hdc3020_write_bytes(data, buf, 5);
	if (ret < 0)
		return ret;

	return hdc3020_exec_cmd(data, HDC3020_HEATER_ENABLE);
}

static int hdc3020_write_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     int val, int val2, long mask)
{
	struct hdc3020_data *data = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		if (chan->type != IIO_CURRENT)
			return -EINVAL;

		guard(mutex)(&data->lock);
		return hdc3020_update_heater(data, val);
	}

	return -EINVAL;
}

static const struct iio_info hdc3020_info = {
	.read_raw = hdc3020_read_raw,
	.write_raw = hdc3020_write_raw,
	.read_avail = hdc3020_read_available,
};

static void hdc3020_stop(void *data)
{
	hdc3020_exec_cmd((struct hdc3020_data *)data, HDC3020_EXIT_AUTO);
}

static int hdc3020_probe(struct i2c_client *client)
{
	struct iio_dev *indio_dev;
	struct hdc3020_data *data;
	int ret;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return -EOPNOTSUPP;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	data->client = client;
	mutex_init(&data->lock);

	crc8_populate_msb(hdc3020_crc8_table, HDC3020_CRC8_POLYNOMIAL);

	indio_dev->name = "hdc3020";
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = &hdc3020_info;
	indio_dev->channels = hdc3020_channels;
	indio_dev->num_channels = ARRAY_SIZE(hdc3020_channels);

	ret = hdc3020_exec_cmd(data, HDC3020_S_AUTO_10HZ_MOD0);
	if (ret)
		return dev_err_probe(&client->dev, ret,
				     "Unable to set up measurement\n");

	ret = devm_add_action_or_reset(&data->client->dev, hdc3020_stop, data);
	if (ret)
		return ret;

	ret = devm_iio_device_register(&data->client->dev, indio_dev);
	if (ret)
		return dev_err_probe(&client->dev, ret, "Failed to add device");

	return 0;
}

static const struct i2c_device_id hdc3020_id[] = {
	{ "hdc3020" },
	{ "hdc3021" },
	{ "hdc3022" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, hdc3020_id);

static const struct of_device_id hdc3020_dt_ids[] = {
	{ .compatible = "ti,hdc3020" },
	{ .compatible = "ti,hdc3021" },
	{ .compatible = "ti,hdc3022" },
	{ }
};
MODULE_DEVICE_TABLE(of, hdc3020_dt_ids);

static struct i2c_driver hdc3020_driver = {
	.driver = {
		.name = "hdc3020",
		.of_match_table = hdc3020_dt_ids,
	},
	.probe = hdc3020_probe,
	.id_table = hdc3020_id,
};
module_i2c_driver(hdc3020_driver);

MODULE_AUTHOR("Javier Carrasco <javier.carrasco.cruz@gmail.com>");
MODULE_AUTHOR("Li peiyu <579lpy@gmail.com>");
MODULE_DESCRIPTION("TI HDC3020 humidity and temperature sensor driver");
MODULE_LICENSE("GPL");
