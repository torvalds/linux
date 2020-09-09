// SPDX-License-Identifier: GPL-2.0
/*
 * MS5611 pressure and temperature sensor driver
 *
 * Copyright (c) Tomasz Duszynski <tduszyns@gmail.com>
 *
 * Data sheet:
 *  http://www.meas-spec.com/downloads/MS5611-01BA03.pdf
 *  http://www.meas-spec.com/downloads/MS5607-02BA03.pdf
 *
 */

#include <linux/module.h>
#include <linux/iio/iio.h>
#include <linux/delay.h>
#include <linux/regulator/consumer.h>

#include <linux/iio/sysfs.h>
#include <linux/iio/buffer.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/iio/trigger_consumer.h>
#include "ms5611.h"

#define MS5611_INIT_OSR(_cmd, _conv_usec, _rate) \
	{ .cmd = _cmd, .conv_usec = _conv_usec, .rate = _rate }

static const struct ms5611_osr ms5611_avail_pressure_osr[] = {
	MS5611_INIT_OSR(0x40, 600,  256),
	MS5611_INIT_OSR(0x42, 1170, 512),
	MS5611_INIT_OSR(0x44, 2280, 1024),
	MS5611_INIT_OSR(0x46, 4540, 2048),
	MS5611_INIT_OSR(0x48, 9040, 4096)
};

static const struct ms5611_osr ms5611_avail_temp_osr[] = {
	MS5611_INIT_OSR(0x50, 600,  256),
	MS5611_INIT_OSR(0x52, 1170, 512),
	MS5611_INIT_OSR(0x54, 2280, 1024),
	MS5611_INIT_OSR(0x56, 4540, 2048),
	MS5611_INIT_OSR(0x58, 9040, 4096)
};

static const char ms5611_show_osr[] = "256 512 1024 2048 4096";

static IIO_CONST_ATTR(oversampling_ratio_available, ms5611_show_osr);

static struct attribute *ms5611_attributes[] = {
	&iio_const_attr_oversampling_ratio_available.dev_attr.attr,
	NULL,
};

static const struct attribute_group ms5611_attribute_group = {
	.attrs = ms5611_attributes,
};

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
		s64 off2, sens2, t2, tmp;

		t2 = (dt * dt) >> 31;
		tmp = (t - 2000) * (t - 2000);
		off2 = (61 * tmp) >> 4;
		sens2 = tmp << 1;

		if (t < -1500) {
			tmp = (t + 1500) * (t + 1500);
			off2 += 15 * tmp;
			sens2 += 8 * tmp;
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

static irqreturn_t ms5611_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct ms5611_state *st = iio_priv(indio_dev);
	/* Ensure buffer elements are naturally aligned */
	struct {
		s32 channels[2];
		s64 ts __aligned(8);
	} scan;
	int ret;

	mutex_lock(&st->lock);
	ret = ms5611_read_temp_and_pressure(indio_dev, &scan.channels[1],
					    &scan.channels[0]);
	mutex_unlock(&st->lock);
	if (ret < 0)
		goto err;

	iio_push_to_buffers_with_timestamp(indio_dev, &scan,
					   iio_get_time_ns(indio_dev));

err:
	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
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
	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_TEMP:
			*val = 10;
			return IIO_VAL_INT;
		case IIO_PRESSURE:
			*val = 0;
			*val2 = 1000;
			return IIO_VAL_INT_PLUS_MICRO;
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_OVERSAMPLING_RATIO:
		if (chan->type != IIO_TEMP && chan->type != IIO_PRESSURE)
			break;
		mutex_lock(&st->lock);
		if (chan->type == IIO_TEMP)
			*val = (int)st->temp_osr->rate;
		else
			*val = (int)st->pressure_osr->rate;
		mutex_unlock(&st->lock);
		return IIO_VAL_INT;
	}

	return -EINVAL;
}

static const struct ms5611_osr *ms5611_find_osr(int rate,
						const struct ms5611_osr *osr,
						size_t count)
{
	unsigned int r;

	for (r = 0; r < count; r++)
		if ((unsigned short)rate == osr[r].rate)
			break;
	if (r >= count)
		return NULL;
	return &osr[r];
}

static int ms5611_write_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int val, int val2, long mask)
{
	struct ms5611_state *st = iio_priv(indio_dev);
	const struct ms5611_osr *osr = NULL;
	int ret;

	if (mask != IIO_CHAN_INFO_OVERSAMPLING_RATIO)
		return -EINVAL;

	if (chan->type == IIO_TEMP)
		osr = ms5611_find_osr(val, ms5611_avail_temp_osr,
				      ARRAY_SIZE(ms5611_avail_temp_osr));
	else if (chan->type == IIO_PRESSURE)
		osr = ms5611_find_osr(val, ms5611_avail_pressure_osr,
				      ARRAY_SIZE(ms5611_avail_pressure_osr));
	if (!osr)
		return -EINVAL;

	ret = iio_device_claim_direct_mode(indio_dev);
	if (ret)
		return ret;

	mutex_lock(&st->lock);

	if (chan->type == IIO_TEMP)
		st->temp_osr = osr;
	else
		st->pressure_osr = osr;

	mutex_unlock(&st->lock);
	iio_device_release_direct_mode(indio_dev);

	return 0;
}

static const unsigned long ms5611_scan_masks[] = {0x3, 0};

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
		.info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED) |
			BIT(IIO_CHAN_INFO_SCALE) |
			BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO),
		.scan_index = 0,
		.scan_type = {
			.sign = 's',
			.realbits = 32,
			.storagebits = 32,
			.endianness = IIO_CPU,
		},
	},
	{
		.type = IIO_TEMP,
		.info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED) |
			BIT(IIO_CHAN_INFO_SCALE) |
			BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO),
		.scan_index = 1,
		.scan_type = {
			.sign = 's',
			.realbits = 32,
			.storagebits = 32,
			.endianness = IIO_CPU,
		},
	},
	IIO_CHAN_SOFT_TIMESTAMP(2),
};

static const struct iio_info ms5611_info = {
	.read_raw = &ms5611_read_raw,
	.write_raw = &ms5611_write_raw,
	.attrs = &ms5611_attribute_group,
};

static int ms5611_init(struct iio_dev *indio_dev)
{
	int ret;
	struct ms5611_state *st = iio_priv(indio_dev);

	/* Enable attached regulator if any. */
	st->vdd = devm_regulator_get(indio_dev->dev.parent, "vdd");
	if (IS_ERR(st->vdd))
		return PTR_ERR(st->vdd);

	ret = regulator_enable(st->vdd);
	if (ret) {
		dev_err(indio_dev->dev.parent,
			"failed to enable Vdd supply: %d\n", ret);
		return ret;
	}

	ret = ms5611_reset(indio_dev);
	if (ret < 0)
		goto err_regulator_disable;

	ret = ms5611_read_prom(indio_dev);
	if (ret < 0)
		goto err_regulator_disable;

	return 0;

err_regulator_disable:
	regulator_disable(st->vdd);
	return ret;
}

static void ms5611_fini(const struct iio_dev *indio_dev)
{
	const struct ms5611_state *st = iio_priv(indio_dev);

	regulator_disable(st->vdd);
}

int ms5611_probe(struct iio_dev *indio_dev, struct device *dev,
		 const char *name, int type)
{
	int ret;
	struct ms5611_state *st = iio_priv(indio_dev);

	mutex_init(&st->lock);
	st->chip_info = &chip_info_tbl[type];
	st->temp_osr =
		&ms5611_avail_temp_osr[ARRAY_SIZE(ms5611_avail_temp_osr) - 1];
	st->pressure_osr =
		&ms5611_avail_pressure_osr[ARRAY_SIZE(ms5611_avail_pressure_osr)
					   - 1];
	indio_dev->name = name;
	indio_dev->info = &ms5611_info;
	indio_dev->channels = ms5611_channels;
	indio_dev->num_channels = ARRAY_SIZE(ms5611_channels);
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->available_scan_masks = ms5611_scan_masks;

	ret = ms5611_init(indio_dev);
	if (ret < 0)
		return ret;

	ret = iio_triggered_buffer_setup(indio_dev, NULL,
					 ms5611_trigger_handler, NULL);
	if (ret < 0) {
		dev_err(dev, "iio triggered buffer setup failed\n");
		goto err_fini;
	}

	ret = iio_device_register(indio_dev);
	if (ret < 0) {
		dev_err(dev, "unable to register iio device\n");
		goto err_buffer_cleanup;
	}

	return 0;

err_buffer_cleanup:
	iio_triggered_buffer_cleanup(indio_dev);
err_fini:
	ms5611_fini(indio_dev);
	return ret;
}
EXPORT_SYMBOL(ms5611_probe);

int ms5611_remove(struct iio_dev *indio_dev)
{
	iio_device_unregister(indio_dev);
	iio_triggered_buffer_cleanup(indio_dev);
	ms5611_fini(indio_dev);

	return 0;
}
EXPORT_SYMBOL(ms5611_remove);

MODULE_AUTHOR("Tomasz Duszynski <tduszyns@gmail.com>");
MODULE_DESCRIPTION("MS5611 core driver");
MODULE_LICENSE("GPL v2");
