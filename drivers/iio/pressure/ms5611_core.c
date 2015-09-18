/*
 * MS5611 pressure and temperature sensor driver
 *
 * Copyright (c) Tomasz Duszynski <tduszyns@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Data sheet:
 *  http://www.meas-spec.com/downloads/MS5611-01BA03.pdf
 *  http://www.meas-spec.com/downloads/MS5607-02BA03.pdf
 *
 */

#include <linux/module.h>
#include <linux/iio/iio.h>
#include <linux/delay.h>

#include "ms5611.h"

static bool ms5611_prom_is_valid(u16 *prom, size_t len)
{
	int i, j;
	uint16_t crc = 0, crc_orig = prom[7] & 0x000F;

	prom[7] &= 0xFF00;

	for (i = 0; i < len * 2; i++) {
		if (i % 2 == 1)
			crc ^= prom[i >> 1] & 0x00FF;
		else
			crc ^= prom[i >> 1] >> 8;

		for (j = 0; j < 8; j++) {
			if (crc & 0x8000)
				crc = (crc << 1) ^ 0x3000;
			else
				crc <<= 1;
		}
	}

	crc = (crc >> 12) & 0x000F;

	return crc_orig != 0x0000 && crc == crc_orig;
}

static int ms5611_read_prom(struct iio_dev *indio_dev)
{
	int ret, i;
	struct ms5611_state *st = iio_priv(indio_dev);

	for (i = 0; i < MS5611_PROM_WORDS_NB; i++) {
		ret = st->read_prom_word(&indio_dev->dev,
					 i, &st->chip_info->prom[i]);
		if (ret < 0) {
			dev_err(&indio_dev->dev,
				"failed to read prom at %d\n", i);
			return ret;
		}
	}

	if (!ms5611_prom_is_valid(st->chip_info->prom, MS5611_PROM_WORDS_NB)) {
		dev_err(&indio_dev->dev, "PROM integrity check failed\n");
		return -ENODEV;
	}

	return 0;
}

static int ms5611_read_temp_and_pressure(struct iio_dev *indio_dev,
					 s32 *temp, s32 *pressure)
{
	int ret;
	struct ms5611_state *st = iio_priv(indio_dev);

	ret = st->read_adc_temp_and_pressure(&indio_dev->dev, temp, pressure);
	if (ret < 0) {
		dev_err(&indio_dev->dev,
			"failed to read temperature and pressure\n");
		return ret;
	}

	return st->chip_info->temp_and_pressure_compensate(st->chip_info,
							   temp, pressure);
}

static int ms5611_temp_and_pressure_compensate(struct ms5611_chip_info *chip_info,
					       s32 *temp, s32 *pressure)
{
	s32 t = *temp, p = *pressure;
	s64 off, sens, dt;

	dt = t - (chip_info->prom[5] << 8);
	off = ((s64)chip_info->prom[2] << 16) + ((chip_info->prom[4] * dt) >> 7);
	sens = ((s64)chip_info->prom[1] << 15) + ((chip_info->prom[3] * dt) >> 8);

	t = 2000 + ((chip_info->prom[6] * dt) >> 23);
	if (t < 2000) {
		s64 off2, sens2, t2;

		t2 = (dt * dt) >> 31;
		off2 = (5 * (t - 2000) * (t - 2000)) >> 1;
		sens2 = off2 >> 1;

		if (t < -1500) {
			s64 tmp = (t + 1500) * (t + 1500);

			off2 += 7 * tmp;
			sens2 += (11 * tmp) >> 1;
		}

		t -= t2;
		off -= off2;
		sens -= sens2;
	}

	*temp = t;
	*pressure = (((p * sens) >> 21) - off) >> 15;

	return 0;
}

static int ms5607_temp_and_pressure_compensate(struct ms5611_chip_info *chip_info,
					       s32 *temp, s32 *pressure)
{
	s32 t = *temp, p = *pressure;
	s64 off, sens, dt;

	dt = t - (chip_info->prom[5] << 8);
	off = ((s64)chip_info->prom[2] << 17) + ((chip_info->prom[4] * dt) >> 6);
	sens = ((s64)chip_info->prom[1] << 16) + ((chip_info->prom[3] * dt) >> 7);

	t = 2000 + ((chip_info->prom[6] * dt) >> 23);
	if (t < 2000) {
		s64 off2, sens2, t2;

		t2 = (dt * dt) >> 31;
		off2 = (61 * (t - 2000) * (t - 2000)) >> 4;
		sens2 = off2 << 1;

		if (t < -1500) {
			s64 tmp = (t + 1500) * (t + 1500);

			off2 += 15 * tmp;
			sens2 += (8 * tmp);
		}

		t -= t2;
		off -= off2;
		sens -= sens2;
	}

	*temp = t;
	*pressure = (((p * sens) >> 21) - off) >> 15;

	return 0;
}

static int ms5611_reset(struct iio_dev *indio_dev)
{
	int ret;
	struct ms5611_state *st = iio_priv(indio_dev);

	ret = st->reset(&indio_dev->dev);
	if (ret < 0) {
		dev_err(&indio_dev->dev, "failed to reset device\n");
		return ret;
	}

	usleep_range(3000, 4000);

	return 0;
}

static int ms5611_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val, int *val2, long mask)
{
	int ret;
	s32 temp, pressure;
	struct ms5611_state *st = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_PROCESSED:
		mutex_lock(&st->lock);
		ret = ms5611_read_temp_and_pressure(indio_dev,
						    &temp, &pressure);
		mutex_unlock(&st->lock);
		if (ret < 0)
			return ret;

		switch (chan->type) {
		case IIO_TEMP:
			*val = temp * 10;
			return IIO_VAL_INT;
		case IIO_PRESSURE:
			*val = pressure / 1000;
			*val2 = (pressure % 1000) * 1000;
			return IIO_VAL_INT_PLUS_MICRO;
		default:
			return -EINVAL;
		}
	}

	return -EINVAL;
}

static struct ms5611_chip_info chip_info_tbl[] = {
	[MS5611] = {
		.temp_and_pressure_compensate = ms5611_temp_and_pressure_compensate,
	},
	[MS5607] = {
		.temp_and_pressure_compensate = ms5607_temp_and_pressure_compensate,
	}
};

static const struct iio_chan_spec ms5611_channels[] = {
	{
		.type = IIO_PRESSURE,
		.info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED),
	},
	{
		.type = IIO_TEMP,
		.info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED),
	}
};

static const struct iio_info ms5611_info = {
	.read_raw = &ms5611_read_raw,
	.driver_module = THIS_MODULE,
};

static int ms5611_init(struct iio_dev *indio_dev)
{
	int ret;

	ret = ms5611_reset(indio_dev);
	if (ret < 0)
		return ret;

	return ms5611_read_prom(indio_dev);
}

int ms5611_probe(struct iio_dev *indio_dev, struct device *dev, int type)
{
	int ret;
	struct ms5611_state *st = iio_priv(indio_dev);

	mutex_init(&st->lock);
	st->chip_info = &chip_info_tbl[type];
	indio_dev->dev.parent = dev;
	indio_dev->name = dev->driver->name;
	indio_dev->info = &ms5611_info;
	indio_dev->channels = ms5611_channels;
	indio_dev->num_channels = ARRAY_SIZE(ms5611_channels);
	indio_dev->modes = INDIO_DIRECT_MODE;

	ret = ms5611_init(indio_dev);
	if (ret < 0)
		return ret;

	return devm_iio_device_register(dev, indio_dev);
}
EXPORT_SYMBOL(ms5611_probe);

MODULE_AUTHOR("Tomasz Duszynski <tduszyns@gmail.com>");
MODULE_DESCRIPTION("MS5611 core driver");
MODULE_LICENSE("GPL v2");
