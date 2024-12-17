// SPDX-License-Identifier: GPL-2.0
/*
 * MS5611 pressure and temperature sensor driver (I2C bus)
 *
 * Copyright (c) Tomasz Duszynski <tduszyns@gmail.com>
 *
 * 7-bit I2C slave addresses:
 *
 * 0x77 (CSB pin low)
 * 0x76 (CSB pin high)
 *
 */

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>

#include <linux/unaligned.h>

#include "ms5611.h"

static int ms5611_i2c_reset(struct ms5611_state *st)
{
	return i2c_smbus_write_byte(st->client, MS5611_RESET);
}

static int ms5611_i2c_read_prom_word(struct ms5611_state *st, int index,
				     u16 *word)
{
	int ret;

	ret = i2c_smbus_read_word_swapped(st->client,
			MS5611_READ_PROM_WORD + (index << 1));
	if (ret < 0)
		return ret;

	*word = ret;

	return 0;
}

static int ms5611_i2c_read_adc(struct ms5611_state *st, s32 *val)
{
	int ret;
	u8 buf[3];

	ret = i2c_smbus_read_i2c_block_data(st->client, MS5611_READ_ADC,
					    3, buf);
	if (ret < 0)
		return ret;

	*val = get_unaligned_be24(&buf[0]);

	return 0;
}

static int ms5611_i2c_read_adc_temp_and_pressure(struct ms5611_state *st,
						 s32 *temp, s32 *pressure)
{
	int ret;
	const struct ms5611_osr *osr = st->temp_osr;

	ret = i2c_smbus_write_byte(st->client, osr->cmd);
	if (ret < 0)
		return ret;

	usleep_range(osr->conv_usec, osr->conv_usec + (osr->conv_usec / 10UL));
	ret = ms5611_i2c_read_adc(st, temp);
	if (ret < 0)
		return ret;

	osr = st->pressure_osr;
	ret = i2c_smbus_write_byte(st->client, osr->cmd);
	if (ret < 0)
		return ret;

	usleep_range(osr->conv_usec, osr->conv_usec + (osr->conv_usec / 10UL));
	return ms5611_i2c_read_adc(st, pressure);
}

static int ms5611_i2c_probe(struct i2c_client *client)
{
	const struct i2c_device_id *id = i2c_client_get_device_id(client);
	struct ms5611_state *st;
	struct iio_dev *indio_dev;

	if (!i2c_check_functionality(client->adapter,
				     I2C_FUNC_SMBUS_WRITE_BYTE |
				     I2C_FUNC_SMBUS_READ_WORD_DATA |
				     I2C_FUNC_SMBUS_READ_I2C_BLOCK))
		return -EOPNOTSUPP;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;

	st = iio_priv(indio_dev);
	i2c_set_clientdata(client, indio_dev);
	st->reset = ms5611_i2c_reset;
	st->read_prom_word = ms5611_i2c_read_prom_word;
	st->read_adc_temp_and_pressure = ms5611_i2c_read_adc_temp_and_pressure;
	st->client = client;

	return ms5611_probe(indio_dev, &client->dev, id->name, id->driver_data);
}

static const struct of_device_id ms5611_i2c_matches[] = {
	{ .compatible = "meas,ms5611" },
	{ .compatible = "meas,ms5607" },
	{ }
};
MODULE_DEVICE_TABLE(of, ms5611_i2c_matches);

static const struct i2c_device_id ms5611_id[] = {
	{ "ms5611", MS5611 },
	{ "ms5607", MS5607 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ms5611_id);

static struct i2c_driver ms5611_driver = {
	.driver = {
		.name = "ms5611",
		.of_match_table = ms5611_i2c_matches,
	},
	.id_table = ms5611_id,
	.probe = ms5611_i2c_probe,
};
module_i2c_driver(ms5611_driver);

MODULE_AUTHOR("Tomasz Duszynski <tduszyns@gmail.com>");
MODULE_DESCRIPTION("MS5611 i2c driver");
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS("IIO_MS5611");
