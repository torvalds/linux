// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * ADXRS290 SPI Gyroscope Driver
 *
 * Copyright (C) 2020 Nishant Malpani <nish.malpani25@gmail.com>
 * Copyright (C) 2020 Analog Devices, Inc.
 */

#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/spi/spi.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>

#define ADXRS290_ADI_ID		0xAD
#define ADXRS290_MEMS_ID	0x1D
#define ADXRS290_DEV_ID		0x92

#define ADXRS290_REG_ADI_ID	0x00
#define ADXRS290_REG_MEMS_ID	0x01
#define ADXRS290_REG_DEV_ID	0x02
#define ADXRS290_REG_REV_ID	0x03
#define ADXRS290_REG_SN0	0x04 /* Serial Number Registers, 4 bytes */
#define ADXRS290_REG_DATAX0	0x08 /* Roll Rate o/p Data Regs, 2 bytes */
#define ADXRS290_REG_DATAY0	0x0A /* Pitch Rate o/p Data Regs, 2 bytes */
#define ADXRS290_REG_TEMP0	0x0C
#define ADXRS290_REG_POWER_CTL	0x10
#define ADXRS290_REG_FILTER	0x11
#define ADXRS290_REG_DATA_RDY	0x12

#define ADXRS290_READ		BIT(7)
#define ADXRS290_TSM		BIT(0)
#define ADXRS290_MEASUREMENT	BIT(1)
#define ADXRS290_SYNC		GENMASK(1, 0)
#define ADXRS290_LPF_MASK	GENMASK(2, 0)
#define ADXRS290_LPF(x)		FIELD_PREP(ADXRS290_LPF_MASK, x)
#define ADXRS290_HPF_MASK	GENMASK(7, 4)
#define ADXRS290_HPF(x)		FIELD_PREP(ADXRS290_HPF_MASK, x)

#define ADXRS290_READ_REG(reg)	(ADXRS290_READ | (reg))

#define ADXRS290_MAX_TRANSITION_TIME_MS 100

enum adxrs290_mode {
	ADXRS290_MODE_STANDBY,
	ADXRS290_MODE_MEASUREMENT,
};

struct adxrs290_state {
	struct spi_device	*spi;
	/* Serialize reads and their subsequent processing */
	struct mutex		lock;
	enum adxrs290_mode	mode;
	unsigned int		lpf_3db_freq_idx;
	unsigned int		hpf_3db_freq_idx;
};

/*
 * Available cut-off frequencies of the low pass filter in Hz.
 * The integer part and fractional part are represented separately.
 */
static const int adxrs290_lpf_3db_freq_hz_table[][2] = {
	[0] = {480, 0},
	[1] = {320, 0},
	[2] = {160, 0},
	[3] = {80, 0},
	[4] = {56, 600000},
	[5] = {40, 0},
	[6] = {28, 300000},
	[7] = {20, 0},
};

/*
 * Available cut-off frequencies of the high pass filter in Hz.
 * The integer part and fractional part are represented separately.
 */
static const int adxrs290_hpf_3db_freq_hz_table[][2] = {
	[0] = {0, 0},
	[1] = {0, 11000},
	[2] = {0, 22000},
	[3] = {0, 44000},
	[4] = {0, 87000},
	[5] = {0, 175000},
	[6] = {0, 350000},
	[7] = {0, 700000},
	[8] = {1, 400000},
	[9] = {2, 800000},
	[10] = {11, 300000},
};

static int adxrs290_get_rate_data(struct iio_dev *indio_dev, const u8 cmd, int *val)
{
	struct adxrs290_state *st = iio_priv(indio_dev);
	int ret = 0;
	int temp;

	mutex_lock(&st->lock);
	temp = spi_w8r16(st->spi, cmd);
	if (temp < 0) {
		ret = temp;
		goto err_unlock;
	}

	*val = temp;

err_unlock:
	mutex_unlock(&st->lock);
	return ret;
}

static int adxrs290_get_temp_data(struct iio_dev *indio_dev, int *val)
{
	const u8 cmd = ADXRS290_READ_REG(ADXRS290_REG_TEMP0);
	struct adxrs290_state *st = iio_priv(indio_dev);
	int ret = 0;
	int temp;

	mutex_lock(&st->lock);
	temp = spi_w8r16(st->spi, cmd);
	if (temp < 0) {
		ret = temp;
		goto err_unlock;
	}

	/* extract lower 12 bits temperature reading */
	*val = temp & 0x0FFF;

err_unlock:
	mutex_unlock(&st->lock);
	return ret;
}

static int adxrs290_get_3db_freq(struct iio_dev *indio_dev, u8 *val, u8 *val2)
{
	const u8 cmd = ADXRS290_READ_REG(ADXRS290_REG_FILTER);
	struct adxrs290_state *st = iio_priv(indio_dev);
	int ret = 0;
	short temp;

	mutex_lock(&st->lock);
	temp = spi_w8r8(st->spi, cmd);
	if (temp < 0) {
		ret = temp;
		goto err_unlock;
	}

	*val = FIELD_GET(ADXRS290_LPF_MASK, temp);
	*val2 = FIELD_GET(ADXRS290_HPF_MASK, temp);

err_unlock:
	mutex_unlock(&st->lock);
	return ret;
}

static int adxrs290_spi_write_reg(struct spi_device *spi, const u8 reg,
				  const u8 val)
{
	u8 buf[2];

	buf[0] = reg;
	buf[1] = val;

	return spi_write_then_read(spi, buf, ARRAY_SIZE(buf), NULL, 0);
}

static int adxrs290_find_match(const int (*freq_tbl)[2], const int n,
			       const int val, const int val2)
{
	int i;

	for (i = 0; i < n; i++) {
		if (freq_tbl[i][0] == val && freq_tbl[i][1] == val2)
			return i;
	}

	return -EINVAL;
}

static int adxrs290_set_filter_freq(struct iio_dev *indio_dev,
				    const unsigned int lpf_idx,
				    const unsigned int hpf_idx)
{
	struct adxrs290_state *st = iio_priv(indio_dev);
	u8 val;

	val = ADXRS290_HPF(hpf_idx) | ADXRS290_LPF(lpf_idx);

	return adxrs290_spi_write_reg(st->spi, ADXRS290_REG_FILTER, val);
}

static int adxrs290_initial_setup(struct iio_dev *indio_dev)
{
	struct adxrs290_state *st = iio_priv(indio_dev);

	st->mode = ADXRS290_MODE_MEASUREMENT;

	return adxrs290_spi_write_reg(st->spi,
				      ADXRS290_REG_POWER_CTL,
				      ADXRS290_MEASUREMENT | ADXRS290_TSM);
}

static int adxrs290_read_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     int *val,
			     int *val2,
			     long mask)
{
	struct adxrs290_state *st = iio_priv(indio_dev);
	unsigned int t;
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		switch (chan->type) {
		case IIO_ANGL_VEL:
			ret = adxrs290_get_rate_data(indio_dev,
						     ADXRS290_READ_REG(chan->address),
						     val);
			if (ret < 0)
				return ret;

			return IIO_VAL_INT;
		case IIO_TEMP:
			ret = adxrs290_get_temp_data(indio_dev, val);
			if (ret < 0)
				return ret;

			return IIO_VAL_INT;
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_ANGL_VEL:
			/* 1 LSB = 0.005 degrees/sec */
			*val = 0;
			*val2 = 87266;
			return IIO_VAL_INT_PLUS_NANO;
		case IIO_TEMP:
			/* 1 LSB = 0.1 degrees Celsius */
			*val = 100;
			return IIO_VAL_INT;
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY:
		switch (chan->type) {
		case IIO_ANGL_VEL:
			t = st->lpf_3db_freq_idx;
			*val = adxrs290_lpf_3db_freq_hz_table[t][0];
			*val2 = adxrs290_lpf_3db_freq_hz_table[t][1];
			return IIO_VAL_INT_PLUS_MICRO;
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_HIGH_PASS_FILTER_3DB_FREQUENCY:
		switch (chan->type) {
		case IIO_ANGL_VEL:
			t = st->hpf_3db_freq_idx;
			*val = adxrs290_hpf_3db_freq_hz_table[t][0];
			*val2 = adxrs290_hpf_3db_freq_hz_table[t][1];
			return IIO_VAL_INT_PLUS_MICRO;
		default:
			return -EINVAL;
		}
	}

	return -EINVAL;
}

static int adxrs290_write_raw(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *chan,
			      int val,
			      int val2,
			      long mask)
{
	struct adxrs290_state *st = iio_priv(indio_dev);
	int lpf_idx, hpf_idx;

	switch (mask) {
	case IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY:
		lpf_idx = adxrs290_find_match(adxrs290_lpf_3db_freq_hz_table,
					      ARRAY_SIZE(adxrs290_lpf_3db_freq_hz_table),
					      val, val2);
		if (lpf_idx < 0)
			return -EINVAL;
		/* caching the updated state of the low-pass filter */
		st->lpf_3db_freq_idx = lpf_idx;
		/* retrieving the current state of the high-pass filter */
		hpf_idx = st->hpf_3db_freq_idx;
		return adxrs290_set_filter_freq(indio_dev, lpf_idx, hpf_idx);
	case IIO_CHAN_INFO_HIGH_PASS_FILTER_3DB_FREQUENCY:
		hpf_idx = adxrs290_find_match(adxrs290_hpf_3db_freq_hz_table,
					      ARRAY_SIZE(adxrs290_hpf_3db_freq_hz_table),
					      val, val2);
		if (hpf_idx < 0)
			return -EINVAL;
		/* caching the updated state of the high-pass filter */
		st->hpf_3db_freq_idx = hpf_idx;
		/* retrieving the current state of the low-pass filter */
		lpf_idx = st->lpf_3db_freq_idx;
		return adxrs290_set_filter_freq(indio_dev, lpf_idx, hpf_idx);
	}

	return -EINVAL;
}

static int adxrs290_read_avail(struct iio_dev *indio_dev,
			       struct iio_chan_spec const *chan,
			       const int **vals, int *type, int *length,
			       long mask)
{
	switch (mask) {
	case IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY:
		*vals = (const int *)adxrs290_lpf_3db_freq_hz_table;
		*type = IIO_VAL_INT_PLUS_MICRO;
		/* Values are stored in a 2D matrix */
		*length = ARRAY_SIZE(adxrs290_lpf_3db_freq_hz_table) * 2;

		return IIO_AVAIL_LIST;
	case IIO_CHAN_INFO_HIGH_PASS_FILTER_3DB_FREQUENCY:
		*vals = (const int *)adxrs290_hpf_3db_freq_hz_table;
		*type = IIO_VAL_INT_PLUS_MICRO;
		/* Values are stored in a 2D matrix */
		*length = ARRAY_SIZE(adxrs290_hpf_3db_freq_hz_table) * 2;

		return IIO_AVAIL_LIST;
	default:
		return -EINVAL;
	}
}

#define ADXRS290_ANGL_VEL_CHANNEL(reg, axis) {				\
	.type = IIO_ANGL_VEL,						\
	.address = reg,							\
	.modified = 1,							\
	.channel2 = IIO_MOD_##axis,					\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),			\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE) |		\
	BIT(IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY) |		\
	BIT(IIO_CHAN_INFO_HIGH_PASS_FILTER_3DB_FREQUENCY),		\
	.info_mask_shared_by_type_available =				\
	BIT(IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY) |		\
	BIT(IIO_CHAN_INFO_HIGH_PASS_FILTER_3DB_FREQUENCY),		\
}

static const struct iio_chan_spec adxrs290_channels[] = {
	ADXRS290_ANGL_VEL_CHANNEL(ADXRS290_REG_DATAX0, X),
	ADXRS290_ANGL_VEL_CHANNEL(ADXRS290_REG_DATAY0, Y),
	{
		.type = IIO_TEMP,
		.address = ADXRS290_REG_TEMP0,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
		BIT(IIO_CHAN_INFO_SCALE),
	},
};

static const struct iio_info adxrs290_info = {
	.read_raw = &adxrs290_read_raw,
	.write_raw = &adxrs290_write_raw,
	.read_avail = &adxrs290_read_avail,
};

static int adxrs290_probe(struct spi_device *spi)
{
	struct iio_dev *indio_dev;
	struct adxrs290_state *st;
	u8 val, val2;
	int ret;

	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;

	st = iio_priv(indio_dev);
	st->spi = spi;

	indio_dev->name = "adxrs290";
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = adxrs290_channels;
	indio_dev->num_channels = ARRAY_SIZE(adxrs290_channels);
	indio_dev->info = &adxrs290_info;

	mutex_init(&st->lock);

	val = spi_w8r8(spi, ADXRS290_READ_REG(ADXRS290_REG_ADI_ID));
	if (val != ADXRS290_ADI_ID) {
		dev_err(&spi->dev, "Wrong ADI ID 0x%02x\n", val);
		return -ENODEV;
	}

	val = spi_w8r8(spi, ADXRS290_READ_REG(ADXRS290_REG_MEMS_ID));
	if (val != ADXRS290_MEMS_ID) {
		dev_err(&spi->dev, "Wrong MEMS ID 0x%02x\n", val);
		return -ENODEV;
	}

	val = spi_w8r8(spi, ADXRS290_READ_REG(ADXRS290_REG_DEV_ID));
	if (val != ADXRS290_DEV_ID) {
		dev_err(&spi->dev, "Wrong DEV ID 0x%02x\n", val);
		return -ENODEV;
	}

	/* default mode the gyroscope starts in */
	st->mode = ADXRS290_MODE_STANDBY;

	/* switch to measurement mode and switch on the temperature sensor */
	ret = adxrs290_initial_setup(indio_dev);
	if (ret < 0)
		return ret;

	/* max transition time to measurement mode */
	msleep(ADXRS290_MAX_TRANSITION_TIME_MS);

	ret = adxrs290_get_3db_freq(indio_dev, &val, &val2);
	if (ret < 0)
		return ret;

	st->lpf_3db_freq_idx = val;
	st->hpf_3db_freq_idx = val2;

	return devm_iio_device_register(&spi->dev, indio_dev);
}

static const struct of_device_id adxrs290_of_match[] = {
	{ .compatible = "adi,adxrs290" },
	{ }
};
MODULE_DEVICE_TABLE(of, adxrs290_of_match);

static struct spi_driver adxrs290_driver = {
	.driver = {
		.name = "adxrs290",
		.of_match_table = adxrs290_of_match,
	},
	.probe = adxrs290_probe,
};
module_spi_driver(adxrs290_driver);

MODULE_AUTHOR("Nishant Malpani <nish.malpani25@gmail.com>");
MODULE_DESCRIPTION("Analog Devices ADXRS290 Gyroscope SPI driver");
MODULE_LICENSE("GPL");
