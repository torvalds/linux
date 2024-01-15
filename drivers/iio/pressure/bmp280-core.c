// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2010 Christoph Mair <christoph.mair@gmail.com>
 * Copyright (c) 2012 Bosch Sensortec GmbH
 * Copyright (c) 2012 Unixphere AB
 * Copyright (c) 2014 Intel Corporation
 * Copyright (c) 2016 Linus Walleij <linus.walleij@linaro.org>
 *
 * Driver for Bosch Sensortec BMP180 and BMP280 digital pressure sensor.
 *
 * Datasheet:
 * https://cdn-shop.adafruit.com/datasheets/BST-BMP180-DS000-09.pdf
 * https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmp280-ds001.pdf
 * https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bme280-ds002.pdf
 * https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmp388-ds001.pdf
 * https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmp581-ds004.pdf
 *
 * Notice:
 * The link to the bmp180 datasheet points to an outdated version missing these changes:
 * - Changed document referral from ANP015 to BST-MPS-AN004-00 on page 26
 * - Updated equation for B3 param on section 3.5 to ((((long)AC1 * 4 + X3) << oss) + 2) / 4
 * - Updated RoHS directive to 2011/65/EU effective 8 June 2011 on page 26
 */

#define pr_fmt(fmt) "bmp280: " fmt

#include <linux/bitops.h>
#include <linux/bitfield.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/nvmem-provider.h>
#include <linux/regmap.h>
#include <linux/delay.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/gpio/consumer.h>
#include <linux/regulator/consumer.h>
#include <linux/interrupt.h>
#include <linux/irq.h> /* For irq_get_irq_data() */
#include <linux/completion.h>
#include <linux/pm_runtime.h>
#include <linux/random.h>

#include <asm/unaligned.h>

#include "bmp280.h"

/*
 * These enums are used for indexing into the array of calibration
 * coefficients for BMP180.
 */
enum { AC1, AC2, AC3, AC4, AC5, AC6, B1, B2, MB, MC, MD };


enum bmp380_odr {
	BMP380_ODR_200HZ,
	BMP380_ODR_100HZ,
	BMP380_ODR_50HZ,
	BMP380_ODR_25HZ,
	BMP380_ODR_12_5HZ,
	BMP380_ODR_6_25HZ,
	BMP380_ODR_3_125HZ,
	BMP380_ODR_1_5625HZ,
	BMP380_ODR_0_78HZ,
	BMP380_ODR_0_39HZ,
	BMP380_ODR_0_2HZ,
	BMP380_ODR_0_1HZ,
	BMP380_ODR_0_05HZ,
	BMP380_ODR_0_02HZ,
	BMP380_ODR_0_01HZ,
	BMP380_ODR_0_006HZ,
	BMP380_ODR_0_003HZ,
	BMP380_ODR_0_0015HZ,
};

enum bmp580_odr {
	BMP580_ODR_240HZ,
	BMP580_ODR_218HZ,
	BMP580_ODR_199HZ,
	BMP580_ODR_179HZ,
	BMP580_ODR_160HZ,
	BMP580_ODR_149HZ,
	BMP580_ODR_140HZ,
	BMP580_ODR_129HZ,
	BMP580_ODR_120HZ,
	BMP580_ODR_110HZ,
	BMP580_ODR_100HZ,
	BMP580_ODR_89HZ,
	BMP580_ODR_80HZ,
	BMP580_ODR_70HZ,
	BMP580_ODR_60HZ,
	BMP580_ODR_50HZ,
	BMP580_ODR_45HZ,
	BMP580_ODR_40HZ,
	BMP580_ODR_35HZ,
	BMP580_ODR_30HZ,
	BMP580_ODR_25HZ,
	BMP580_ODR_20HZ,
	BMP580_ODR_15HZ,
	BMP580_ODR_10HZ,
	BMP580_ODR_5HZ,
	BMP580_ODR_4HZ,
	BMP580_ODR_3HZ,
	BMP580_ODR_2HZ,
	BMP580_ODR_1HZ,
	BMP580_ODR_0_5HZ,
	BMP580_ODR_0_25HZ,
	BMP580_ODR_0_125HZ,
};

/*
 * These enums are used for indexing into the array of compensation
 * parameters for BMP280.
 */
enum { T1, T2, T3, P1, P2, P3, P4, P5, P6, P7, P8, P9 };

enum {
	/* Temperature calib indexes */
	BMP380_T1 = 0,
	BMP380_T2 = 2,
	BMP380_T3 = 4,
	/* Pressure calib indexes */
	BMP380_P1 = 5,
	BMP380_P2 = 7,
	BMP380_P3 = 9,
	BMP380_P4 = 10,
	BMP380_P5 = 11,
	BMP380_P6 = 13,
	BMP380_P7 = 15,
	BMP380_P8 = 16,
	BMP380_P9 = 17,
	BMP380_P10 = 19,
	BMP380_P11 = 20,
};

static const struct iio_chan_spec bmp280_channels[] = {
	{
		.type = IIO_PRESSURE,
		.info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED) |
				      BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO),
	},
	{
		.type = IIO_TEMP,
		.info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED) |
				      BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO),
	},
	{
		.type = IIO_HUMIDITYRELATIVE,
		.info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED) |
				      BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO),
	},
};

static const struct iio_chan_spec bmp380_channels[] = {
	{
		.type = IIO_PRESSURE,
		.info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED) |
				      BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO),
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ) |
					   BIT(IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY),
	},
	{
		.type = IIO_TEMP,
		.info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED) |
				      BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO),
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ) |
					   BIT(IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY),
	},
	{
		.type = IIO_HUMIDITYRELATIVE,
		.info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED) |
				      BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO),
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ) |
					   BIT(IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY),
	},
};

static int bmp280_read_calib(struct bmp280_data *data)
{
	struct bmp280_calib *calib = &data->calib.bmp280;
	int ret;


	/* Read temperature and pressure calibration values. */
	ret = regmap_bulk_read(data->regmap, BMP280_REG_COMP_TEMP_START,
			       data->bmp280_cal_buf, sizeof(data->bmp280_cal_buf));
	if (ret < 0) {
		dev_err(data->dev,
			"failed to read temperature and pressure calibration parameters\n");
		return ret;
	}

	/* Toss the temperature and pressure calibration data into the entropy pool */
	add_device_randomness(data->bmp280_cal_buf, sizeof(data->bmp280_cal_buf));

	/* Parse temperature calibration values. */
	calib->T1 = le16_to_cpu(data->bmp280_cal_buf[T1]);
	calib->T2 = le16_to_cpu(data->bmp280_cal_buf[T2]);
	calib->T3 = le16_to_cpu(data->bmp280_cal_buf[T3]);

	/* Parse pressure calibration values. */
	calib->P1 = le16_to_cpu(data->bmp280_cal_buf[P1]);
	calib->P2 = le16_to_cpu(data->bmp280_cal_buf[P2]);
	calib->P3 = le16_to_cpu(data->bmp280_cal_buf[P3]);
	calib->P4 = le16_to_cpu(data->bmp280_cal_buf[P4]);
	calib->P5 = le16_to_cpu(data->bmp280_cal_buf[P5]);
	calib->P6 = le16_to_cpu(data->bmp280_cal_buf[P6]);
	calib->P7 = le16_to_cpu(data->bmp280_cal_buf[P7]);
	calib->P8 = le16_to_cpu(data->bmp280_cal_buf[P8]);
	calib->P9 = le16_to_cpu(data->bmp280_cal_buf[P9]);

	return 0;
}

static int bme280_read_calib(struct bmp280_data *data)
{
	struct bmp280_calib *calib = &data->calib.bmp280;
	struct device *dev = data->dev;
	unsigned int tmp;
	int ret;

	/* Load shared calibration params with bmp280 first */
	ret = bmp280_read_calib(data);
	if  (ret < 0) {
		dev_err(dev, "failed to read common bmp280 calibration parameters\n");
		return ret;
	}

	/*
	 * Read humidity calibration values.
	 * Due to some odd register addressing we cannot just
	 * do a big bulk read. Instead, we have to read each Hx
	 * value separately and sometimes do some bit shifting...
	 * Humidity data is only available on BME280.
	 */

	ret = regmap_read(data->regmap, BMP280_REG_COMP_H1, &tmp);
	if (ret < 0) {
		dev_err(dev, "failed to read H1 comp value\n");
		return ret;
	}
	calib->H1 = tmp;

	ret = regmap_bulk_read(data->regmap, BMP280_REG_COMP_H2,
			       &data->le16, sizeof(data->le16));
	if (ret < 0) {
		dev_err(dev, "failed to read H2 comp value\n");
		return ret;
	}
	calib->H2 = sign_extend32(le16_to_cpu(data->le16), 15);

	ret = regmap_read(data->regmap, BMP280_REG_COMP_H3, &tmp);
	if (ret < 0) {
		dev_err(dev, "failed to read H3 comp value\n");
		return ret;
	}
	calib->H3 = tmp;

	ret = regmap_bulk_read(data->regmap, BMP280_REG_COMP_H4,
			       &data->be16, sizeof(data->be16));
	if (ret < 0) {
		dev_err(dev, "failed to read H4 comp value\n");
		return ret;
	}
	calib->H4 = sign_extend32(((be16_to_cpu(data->be16) >> 4) & 0xff0) |
				  (be16_to_cpu(data->be16) & 0xf), 11);

	ret = regmap_bulk_read(data->regmap, BMP280_REG_COMP_H5,
			       &data->le16, sizeof(data->le16));
	if (ret < 0) {
		dev_err(dev, "failed to read H5 comp value\n");
		return ret;
	}
	calib->H5 = sign_extend32(FIELD_GET(BMP280_COMP_H5_MASK, le16_to_cpu(data->le16)), 11);

	ret = regmap_read(data->regmap, BMP280_REG_COMP_H6, &tmp);
	if (ret < 0) {
		dev_err(dev, "failed to read H6 comp value\n");
		return ret;
	}
	calib->H6 = sign_extend32(tmp, 7);

	return 0;
}
/*
 * Returns humidity in percent, resolution is 0.01 percent. Output value of
 * "47445" represents 47445/1024 = 46.333 %RH.
 *
 * Taken from BME280 datasheet, Section 4.2.3, "Compensation formula".
 */
static u32 bmp280_compensate_humidity(struct bmp280_data *data,
				      s32 adc_humidity)
{
	struct bmp280_calib *calib = &data->calib.bmp280;
	s32 var;

	var = ((s32)data->t_fine) - (s32)76800;
	var = ((((adc_humidity << 14) - (calib->H4 << 20) - (calib->H5 * var))
		+ (s32)16384) >> 15) * (((((((var * calib->H6) >> 10)
		* (((var * (s32)calib->H3) >> 11) + (s32)32768)) >> 10)
		+ (s32)2097152) * calib->H2 + 8192) >> 14);
	var -= ((((var >> 15) * (var >> 15)) >> 7) * (s32)calib->H1) >> 4;

	var = clamp_val(var, 0, 419430400);

	return var >> 12;
};

/*
 * Returns temperature in DegC, resolution is 0.01 DegC.  Output value of
 * "5123" equals 51.23 DegC.  t_fine carries fine temperature as global
 * value.
 *
 * Taken from datasheet, Section 3.11.3, "Compensation formula".
 */
static s32 bmp280_compensate_temp(struct bmp280_data *data,
				  s32 adc_temp)
{
	struct bmp280_calib *calib = &data->calib.bmp280;
	s32 var1, var2;

	var1 = (((adc_temp >> 3) - ((s32)calib->T1 << 1)) *
		((s32)calib->T2)) >> 11;
	var2 = (((((adc_temp >> 4) - ((s32)calib->T1)) *
		  ((adc_temp >> 4) - ((s32)calib->T1))) >> 12) *
		((s32)calib->T3)) >> 14;
	data->t_fine = var1 + var2;

	return (data->t_fine * 5 + 128) >> 8;
}

/*
 * Returns pressure in Pa as unsigned 32 bit integer in Q24.8 format (24
 * integer bits and 8 fractional bits).  Output value of "24674867"
 * represents 24674867/256 = 96386.2 Pa = 963.862 hPa
 *
 * Taken from datasheet, Section 3.11.3, "Compensation formula".
 */
static u32 bmp280_compensate_press(struct bmp280_data *data,
				   s32 adc_press)
{
	struct bmp280_calib *calib = &data->calib.bmp280;
	s64 var1, var2, p;

	var1 = ((s64)data->t_fine) - 128000;
	var2 = var1 * var1 * (s64)calib->P6;
	var2 += (var1 * (s64)calib->P5) << 17;
	var2 += ((s64)calib->P4) << 35;
	var1 = ((var1 * var1 * (s64)calib->P3) >> 8) +
		((var1 * (s64)calib->P2) << 12);
	var1 = ((((s64)1) << 47) + var1) * ((s64)calib->P1) >> 33;

	if (var1 == 0)
		return 0;

	p = ((((s64)1048576 - adc_press) << 31) - var2) * 3125;
	p = div64_s64(p, var1);
	var1 = (((s64)calib->P9) * (p >> 13) * (p >> 13)) >> 25;
	var2 = ((s64)(calib->P8) * p) >> 19;
	p = ((p + var1 + var2) >> 8) + (((s64)calib->P7) << 4);

	return (u32)p;
}

static int bmp280_read_temp(struct bmp280_data *data,
			    int *val, int *val2)
{
	s32 adc_temp, comp_temp;
	int ret;

	ret = regmap_bulk_read(data->regmap, BMP280_REG_TEMP_MSB,
			       data->buf, sizeof(data->buf));
	if (ret < 0) {
		dev_err(data->dev, "failed to read temperature\n");
		return ret;
	}

	adc_temp = FIELD_GET(BMP280_MEAS_TRIM_MASK, get_unaligned_be24(data->buf));
	if (adc_temp == BMP280_TEMP_SKIPPED) {
		/* reading was skipped */
		dev_err(data->dev, "reading temperature skipped\n");
		return -EIO;
	}
	comp_temp = bmp280_compensate_temp(data, adc_temp);

	/*
	 * val might be NULL if we're called by the read_press routine,
	 * who only cares about the carry over t_fine value.
	 */
	if (val) {
		*val = comp_temp * 10;
		return IIO_VAL_INT;
	}

	return 0;
}

static int bmp280_read_press(struct bmp280_data *data,
			     int *val, int *val2)
{
	u32 comp_press;
	s32 adc_press;
	int ret;

	/* Read and compensate temperature so we get a reading of t_fine. */
	ret = bmp280_read_temp(data, NULL, NULL);
	if (ret < 0)
		return ret;

	ret = regmap_bulk_read(data->regmap, BMP280_REG_PRESS_MSB,
			       data->buf, sizeof(data->buf));
	if (ret < 0) {
		dev_err(data->dev, "failed to read pressure\n");
		return ret;
	}

	adc_press = FIELD_GET(BMP280_MEAS_TRIM_MASK, get_unaligned_be24(data->buf));
	if (adc_press == BMP280_PRESS_SKIPPED) {
		/* reading was skipped */
		dev_err(data->dev, "reading pressure skipped\n");
		return -EIO;
	}
	comp_press = bmp280_compensate_press(data, adc_press);

	*val = comp_press;
	*val2 = 256000;

	return IIO_VAL_FRACTIONAL;
}

static int bmp280_read_humid(struct bmp280_data *data, int *val, int *val2)
{
	u32 comp_humidity;
	s32 adc_humidity;
	int ret;

	/* Read and compensate temperature so we get a reading of t_fine. */
	ret = bmp280_read_temp(data, NULL, NULL);
	if (ret < 0)
		return ret;

	ret = regmap_bulk_read(data->regmap, BMP280_REG_HUMIDITY_MSB,
			       &data->be16, sizeof(data->be16));
	if (ret < 0) {
		dev_err(data->dev, "failed to read humidity\n");
		return ret;
	}

	adc_humidity = be16_to_cpu(data->be16);
	if (adc_humidity == BMP280_HUMIDITY_SKIPPED) {
		/* reading was skipped */
		dev_err(data->dev, "reading humidity skipped\n");
		return -EIO;
	}
	comp_humidity = bmp280_compensate_humidity(data, adc_humidity);

	*val = comp_humidity * 1000 / 1024;

	return IIO_VAL_INT;
}

static int bmp280_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val, int *val2, long mask)
{
	struct bmp280_data *data = iio_priv(indio_dev);
	int ret;

	pm_runtime_get_sync(data->dev);
	mutex_lock(&data->lock);

	switch (mask) {
	case IIO_CHAN_INFO_PROCESSED:
		switch (chan->type) {
		case IIO_HUMIDITYRELATIVE:
			ret = data->chip_info->read_humid(data, val, val2);
			break;
		case IIO_PRESSURE:
			ret = data->chip_info->read_press(data, val, val2);
			break;
		case IIO_TEMP:
			ret = data->chip_info->read_temp(data, val, val2);
			break;
		default:
			ret = -EINVAL;
			break;
		}
		break;
	case IIO_CHAN_INFO_OVERSAMPLING_RATIO:
		switch (chan->type) {
		case IIO_HUMIDITYRELATIVE:
			*val = 1 << data->oversampling_humid;
			ret = IIO_VAL_INT;
			break;
		case IIO_PRESSURE:
			*val = 1 << data->oversampling_press;
			ret = IIO_VAL_INT;
			break;
		case IIO_TEMP:
			*val = 1 << data->oversampling_temp;
			ret = IIO_VAL_INT;
			break;
		default:
			ret = -EINVAL;
			break;
		}
		break;
	case IIO_CHAN_INFO_SAMP_FREQ:
		if (!data->chip_info->sampling_freq_avail) {
			ret = -EINVAL;
			break;
		}

		*val = data->chip_info->sampling_freq_avail[data->sampling_freq][0];
		*val2 = data->chip_info->sampling_freq_avail[data->sampling_freq][1];
		ret = IIO_VAL_INT_PLUS_MICRO;
		break;
	case IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY:
		if (!data->chip_info->iir_filter_coeffs_avail) {
			ret = -EINVAL;
			break;
		}

		*val = (1 << data->iir_filter_coeff) - 1;
		ret = IIO_VAL_INT;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	mutex_unlock(&data->lock);
	pm_runtime_mark_last_busy(data->dev);
	pm_runtime_put_autosuspend(data->dev);

	return ret;
}

static int bmp280_write_oversampling_ratio_humid(struct bmp280_data *data,
					       int val)
{
	const int *avail = data->chip_info->oversampling_humid_avail;
	const int n = data->chip_info->num_oversampling_humid_avail;
	int ret, prev;
	int i;

	for (i = 0; i < n; i++) {
		if (avail[i] == val) {
			prev = data->oversampling_humid;
			data->oversampling_humid = ilog2(val);

			ret = data->chip_info->chip_config(data);
			if (ret) {
				data->oversampling_humid = prev;
				data->chip_info->chip_config(data);
				return ret;
			}
			return 0;
		}
	}
	return -EINVAL;
}

static int bmp280_write_oversampling_ratio_temp(struct bmp280_data *data,
					       int val)
{
	const int *avail = data->chip_info->oversampling_temp_avail;
	const int n = data->chip_info->num_oversampling_temp_avail;
	int ret, prev;
	int i;

	for (i = 0; i < n; i++) {
		if (avail[i] == val) {
			prev = data->oversampling_temp;
			data->oversampling_temp = ilog2(val);

			ret = data->chip_info->chip_config(data);
			if (ret) {
				data->oversampling_temp = prev;
				data->chip_info->chip_config(data);
				return ret;
			}
			return 0;
		}
	}
	return -EINVAL;
}

static int bmp280_write_oversampling_ratio_press(struct bmp280_data *data,
					       int val)
{
	const int *avail = data->chip_info->oversampling_press_avail;
	const int n = data->chip_info->num_oversampling_press_avail;
	int ret, prev;
	int i;

	for (i = 0; i < n; i++) {
		if (avail[i] == val) {
			prev = data->oversampling_press;
			data->oversampling_press = ilog2(val);

			ret = data->chip_info->chip_config(data);
			if (ret) {
				data->oversampling_press = prev;
				data->chip_info->chip_config(data);
				return ret;
			}
			return 0;
		}
	}
	return -EINVAL;
}

static int bmp280_write_sampling_frequency(struct bmp280_data *data,
					   int val, int val2)
{
	const int (*avail)[2] = data->chip_info->sampling_freq_avail;
	const int n = data->chip_info->num_sampling_freq_avail;
	int ret, prev;
	int i;

	for (i = 0; i < n; i++) {
		if (avail[i][0] == val && avail[i][1] == val2) {
			prev = data->sampling_freq;
			data->sampling_freq = i;

			ret = data->chip_info->chip_config(data);
			if (ret) {
				data->sampling_freq = prev;
				data->chip_info->chip_config(data);
				return ret;
			}
			return 0;
		}
	}
	return -EINVAL;
}

static int bmp280_write_iir_filter_coeffs(struct bmp280_data *data, int val)
{
	const int *avail = data->chip_info->iir_filter_coeffs_avail;
	const int n = data->chip_info->num_iir_filter_coeffs_avail;
	int ret, prev;
	int i;

	for (i = 0; i < n; i++) {
		if (avail[i] - 1  == val) {
			prev = data->iir_filter_coeff;
			data->iir_filter_coeff = i;

			ret = data->chip_info->chip_config(data);
			if (ret) {
				data->iir_filter_coeff = prev;
				data->chip_info->chip_config(data);
				return ret;

			}
			return 0;
		}
	}
	return -EINVAL;
}

static int bmp280_write_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int val, int val2, long mask)
{
	struct bmp280_data *data = iio_priv(indio_dev);
	int ret = 0;

	/*
	 * Helper functions to update sensor running configuration.
	 * If an error happens applying new settings, will try restore
	 * previous parameters to ensure the sensor is left in a known
	 * working configuration.
	 */
	switch (mask) {
	case IIO_CHAN_INFO_OVERSAMPLING_RATIO:
		pm_runtime_get_sync(data->dev);
		mutex_lock(&data->lock);
		switch (chan->type) {
		case IIO_HUMIDITYRELATIVE:
			ret = bmp280_write_oversampling_ratio_humid(data, val);
			break;
		case IIO_PRESSURE:
			ret = bmp280_write_oversampling_ratio_press(data, val);
			break;
		case IIO_TEMP:
			ret = bmp280_write_oversampling_ratio_temp(data, val);
			break;
		default:
			ret = -EINVAL;
			break;
		}
		mutex_unlock(&data->lock);
		pm_runtime_mark_last_busy(data->dev);
		pm_runtime_put_autosuspend(data->dev);
		break;
	case IIO_CHAN_INFO_SAMP_FREQ:
		pm_runtime_get_sync(data->dev);
		mutex_lock(&data->lock);
		ret = bmp280_write_sampling_frequency(data, val, val2);
		mutex_unlock(&data->lock);
		pm_runtime_mark_last_busy(data->dev);
		pm_runtime_put_autosuspend(data->dev);
		break;
	case IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY:
		pm_runtime_get_sync(data->dev);
		mutex_lock(&data->lock);
		ret = bmp280_write_iir_filter_coeffs(data, val);
		mutex_unlock(&data->lock);
		pm_runtime_mark_last_busy(data->dev);
		pm_runtime_put_autosuspend(data->dev);
		break;
	default:
		return -EINVAL;
	}

	return ret;
}

static int bmp280_read_avail(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     const int **vals, int *type, int *length,
			     long mask)
{
	struct bmp280_data *data = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_OVERSAMPLING_RATIO:
		switch (chan->type) {
		case IIO_PRESSURE:
			*vals = data->chip_info->oversampling_press_avail;
			*length = data->chip_info->num_oversampling_press_avail;
			break;
		case IIO_TEMP:
			*vals = data->chip_info->oversampling_temp_avail;
			*length = data->chip_info->num_oversampling_temp_avail;
			break;
		default:
			return -EINVAL;
		}
		*type = IIO_VAL_INT;
		return IIO_AVAIL_LIST;
	case IIO_CHAN_INFO_SAMP_FREQ:
		*vals = (const int *)data->chip_info->sampling_freq_avail;
		*type = IIO_VAL_INT_PLUS_MICRO;
		/* Values are stored in a 2D matrix */
		*length = data->chip_info->num_sampling_freq_avail;
		return IIO_AVAIL_LIST;
	case IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY:
		*vals = data->chip_info->iir_filter_coeffs_avail;
		*type = IIO_VAL_INT;
		*length = data->chip_info->num_iir_filter_coeffs_avail;
		return IIO_AVAIL_LIST;
	default:
		return -EINVAL;
	}
}

static const struct iio_info bmp280_info = {
	.read_raw = &bmp280_read_raw,
	.read_avail = &bmp280_read_avail,
	.write_raw = &bmp280_write_raw,
};

static int bmp280_chip_config(struct bmp280_data *data)
{
	u8 osrs = FIELD_PREP(BMP280_OSRS_TEMP_MASK, data->oversampling_temp + 1) |
		  FIELD_PREP(BMP280_OSRS_PRESS_MASK, data->oversampling_press + 1);
	int ret;

	ret = regmap_write_bits(data->regmap, BMP280_REG_CTRL_MEAS,
				 BMP280_OSRS_TEMP_MASK |
				 BMP280_OSRS_PRESS_MASK |
				 BMP280_MODE_MASK,
				 osrs | BMP280_MODE_NORMAL);
	if (ret < 0) {
		dev_err(data->dev,
			"failed to write ctrl_meas register\n");
		return ret;
	}

	ret = regmap_update_bits(data->regmap, BMP280_REG_CONFIG,
				 BMP280_FILTER_MASK,
				 BMP280_FILTER_4X);
	if (ret < 0) {
		dev_err(data->dev,
			"failed to write config register\n");
		return ret;
	}

	return ret;
}

static const int bmp280_oversampling_avail[] = { 1, 2, 4, 8, 16 };

const struct bmp280_chip_info bmp280_chip_info = {
	.id_reg = BMP280_REG_ID,
	.chip_id = BMP280_CHIP_ID,
	.regmap_config = &bmp280_regmap_config,
	.start_up_time = 2000,
	.channels = bmp280_channels,
	.num_channels = 2,

	.oversampling_temp_avail = bmp280_oversampling_avail,
	.num_oversampling_temp_avail = ARRAY_SIZE(bmp280_oversampling_avail),
	/*
	 * Oversampling config values on BMx280 have one additional setting
	 * that other generations of the family don't:
	 * The value 0 means the measurement is bypassed instead of
	 * oversampling set to x1.
	 *
	 * To account for this difference, and preserve the same common
	 * config logic, this is handled later on chip_config callback
	 * incrementing one unit the oversampling setting.
	 */
	.oversampling_temp_default = BMP280_OSRS_TEMP_2X - 1,

	.oversampling_press_avail = bmp280_oversampling_avail,
	.num_oversampling_press_avail = ARRAY_SIZE(bmp280_oversampling_avail),
	.oversampling_press_default = BMP280_OSRS_PRESS_16X - 1,

	.chip_config = bmp280_chip_config,
	.read_temp = bmp280_read_temp,
	.read_press = bmp280_read_press,
	.read_calib = bmp280_read_calib,
};
EXPORT_SYMBOL_NS(bmp280_chip_info, IIO_BMP280);

static int bme280_chip_config(struct bmp280_data *data)
{
	u8 osrs = FIELD_PREP(BMP280_OSRS_HUMIDITY_MASK, data->oversampling_humid + 1);
	int ret;

	/*
	 * Oversampling of humidity must be set before oversampling of
	 * temperature/pressure is set to become effective.
	 */
	ret = regmap_update_bits(data->regmap, BMP280_REG_CTRL_HUMIDITY,
				  BMP280_OSRS_HUMIDITY_MASK, osrs);

	if (ret < 0)
		return ret;

	return bmp280_chip_config(data);
}

const struct bmp280_chip_info bme280_chip_info = {
	.id_reg = BMP280_REG_ID,
	.chip_id = BME280_CHIP_ID,
	.regmap_config = &bmp280_regmap_config,
	.start_up_time = 2000,
	.channels = bmp280_channels,
	.num_channels = 3,

	.oversampling_temp_avail = bmp280_oversampling_avail,
	.num_oversampling_temp_avail = ARRAY_SIZE(bmp280_oversampling_avail),
	.oversampling_temp_default = BMP280_OSRS_TEMP_2X - 1,

	.oversampling_press_avail = bmp280_oversampling_avail,
	.num_oversampling_press_avail = ARRAY_SIZE(bmp280_oversampling_avail),
	.oversampling_press_default = BMP280_OSRS_PRESS_16X - 1,

	.oversampling_humid_avail = bmp280_oversampling_avail,
	.num_oversampling_humid_avail = ARRAY_SIZE(bmp280_oversampling_avail),
	.oversampling_humid_default = BMP280_OSRS_HUMIDITY_16X - 1,

	.chip_config = bme280_chip_config,
	.read_temp = bmp280_read_temp,
	.read_press = bmp280_read_press,
	.read_humid = bmp280_read_humid,
	.read_calib = bme280_read_calib,
};
EXPORT_SYMBOL_NS(bme280_chip_info, IIO_BMP280);

/*
 * Helper function to send a command to BMP3XX sensors.
 *
 * Sensor processes commands written to the CMD register and signals
 * execution result through "cmd_rdy" and "cmd_error" flags available on
 * STATUS and ERROR registers.
 */
static int bmp380_cmd(struct bmp280_data *data, u8 cmd)
{
	unsigned int reg;
	int ret;

	/* Check if device is ready to process a command */
	ret = regmap_read(data->regmap, BMP380_REG_STATUS, &reg);
	if (ret) {
		dev_err(data->dev, "failed to read error register\n");
		return ret;
	}
	if (!(reg & BMP380_STATUS_CMD_RDY_MASK)) {
		dev_err(data->dev, "device is not ready to accept commands\n");
		return -EBUSY;
	}

	/* Send command to process */
	ret = regmap_write(data->regmap, BMP380_REG_CMD, cmd);
	if (ret) {
		dev_err(data->dev, "failed to send command to device\n");
		return ret;
	}
	/* Wait for 2ms for command to be processed */
	usleep_range(data->start_up_time, data->start_up_time + 100);
	/* Check for command processing error */
	ret = regmap_read(data->regmap, BMP380_REG_ERROR, &reg);
	if (ret) {
		dev_err(data->dev, "error reading ERROR reg\n");
		return ret;
	}
	if (reg & BMP380_ERR_CMD_MASK) {
		dev_err(data->dev, "error processing command 0x%X\n", cmd);
		return -EINVAL;
	}

	return 0;
}

/*
 * Returns temperature in Celsius dregrees, resolution is 0.01º C. Output value of
 * "5123" equals 51.2º C. t_fine carries fine temperature as global value.
 *
 * Taken from datasheet, Section Appendix 9, "Compensation formula" and repo
 * https://github.com/BoschSensortec/BMP3-Sensor-API.
 */
static s32 bmp380_compensate_temp(struct bmp280_data *data, u32 adc_temp)
{
	s64 var1, var2, var3, var4, var5, var6, comp_temp;
	struct bmp380_calib *calib = &data->calib.bmp380;

	var1 = ((s64) adc_temp) - (((s64) calib->T1) << 8);
	var2 = var1 * ((s64) calib->T2);
	var3 = var1 * var1;
	var4 = var3 * ((s64) calib->T3);
	var5 = (var2 << 18) + var4;
	var6 = var5 >> 32;
	data->t_fine = (s32) var6;
	comp_temp = (var6 * 25) >> 14;

	comp_temp = clamp_val(comp_temp, BMP380_MIN_TEMP, BMP380_MAX_TEMP);
	return (s32) comp_temp;
}

/*
 * Returns pressure in Pa as an unsigned 32 bit integer in fractional Pascal.
 * Output value of "9528709" represents 9528709/100 = 95287.09 Pa = 952.8709 hPa.
 *
 * Taken from datasheet, Section 9.3. "Pressure compensation" and repository
 * https://github.com/BoschSensortec/BMP3-Sensor-API.
 */
static u32 bmp380_compensate_press(struct bmp280_data *data, u32 adc_press)
{
	s64 var1, var2, var3, var4, var5, var6, offset, sensitivity;
	struct bmp380_calib *calib = &data->calib.bmp380;
	u32 comp_press;

	var1 = (s64)data->t_fine * (s64)data->t_fine;
	var2 = var1 >> 6;
	var3 = (var2 * ((s64) data->t_fine)) >> 8;
	var4 = ((s64)calib->P8 * var3) >> 5;
	var5 = ((s64)calib->P7 * var1) << 4;
	var6 = ((s64)calib->P6 * (s64)data->t_fine) << 22;
	offset = ((s64)calib->P5 << 47) + var4 + var5 + var6;
	var2 = ((s64)calib->P4 * var3) >> 5;
	var4 = ((s64)calib->P3 * var1) << 2;
	var5 = ((s64)calib->P2 - ((s64)1 << 14)) *
	       ((s64)data->t_fine << 21);
	sensitivity = (((s64) calib->P1 - ((s64) 1 << 14)) << 46) +
			var2 + var4 + var5;
	var1 = (sensitivity >> 24) * (s64)adc_press;
	var2 = (s64)calib->P10 * (s64)data->t_fine;
	var3 = var2 + ((s64)calib->P9 << 16);
	var4 = (var3 * (s64)adc_press) >> 13;

	/*
	 * Dividing by 10 followed by multiplying by 10 to avoid
	 * possible overflow caused by (uncomp_data->pressure * partial_data4).
	 */
	var5 = ((s64)adc_press * div_s64(var4, 10)) >> 9;
	var5 *= 10;
	var6 = (s64)adc_press * (s64)adc_press;
	var2 = ((s64)calib->P11 * var6) >> 16;
	var3 = (var2 * (s64)adc_press) >> 7;
	var4 = (offset >> 2) + var1 + var5 + var3;
	comp_press = ((u64)var4 * 25) >> 40;

	comp_press = clamp_val(comp_press, BMP380_MIN_PRES, BMP380_MAX_PRES);
	return comp_press;
}

static int bmp380_read_temp(struct bmp280_data *data, int *val, int *val2)
{
	s32 comp_temp;
	u32 adc_temp;
	int ret;

	ret = regmap_bulk_read(data->regmap, BMP380_REG_TEMP_XLSB,
			       data->buf, sizeof(data->buf));
	if (ret) {
		dev_err(data->dev, "failed to read temperature\n");
		return ret;
	}

	adc_temp = get_unaligned_le24(data->buf);
	if (adc_temp == BMP380_TEMP_SKIPPED) {
		dev_err(data->dev, "reading temperature skipped\n");
		return -EIO;
	}
	comp_temp = bmp380_compensate_temp(data, adc_temp);

	/*
	 * Val might be NULL if we're called by the read_press routine,
	 * who only cares about the carry over t_fine value.
	 */
	if (val) {
		/* IIO reports temperatures in milli Celsius */
		*val = comp_temp * 10;
		return IIO_VAL_INT;
	}

	return 0;
}

static int bmp380_read_press(struct bmp280_data *data, int *val, int *val2)
{
	s32 comp_press;
	u32 adc_press;
	int ret;

	/* Read and compensate for temperature so we get a reading of t_fine */
	ret = bmp380_read_temp(data, NULL, NULL);
	if (ret)
		return ret;

	ret = regmap_bulk_read(data->regmap, BMP380_REG_PRESS_XLSB,
			       data->buf, sizeof(data->buf));
	if (ret) {
		dev_err(data->dev, "failed to read pressure\n");
		return ret;
	}

	adc_press = get_unaligned_le24(data->buf);
	if (adc_press == BMP380_PRESS_SKIPPED) {
		dev_err(data->dev, "reading pressure skipped\n");
		return -EIO;
	}
	comp_press = bmp380_compensate_press(data, adc_press);

	*val = comp_press;
	/* Compensated pressure is in cPa (centipascals) */
	*val2 = 100000;

	return IIO_VAL_FRACTIONAL;
}

static int bmp380_read_calib(struct bmp280_data *data)
{
	struct bmp380_calib *calib = &data->calib.bmp380;
	int ret;

	/* Read temperature and pressure calibration data */
	ret = regmap_bulk_read(data->regmap, BMP380_REG_CALIB_TEMP_START,
			       data->bmp380_cal_buf, sizeof(data->bmp380_cal_buf));
	if (ret) {
		dev_err(data->dev,
			"failed to read temperature calibration parameters\n");
		return ret;
	}

	/* Toss the temperature calibration data into the entropy pool */
	add_device_randomness(data->bmp380_cal_buf, sizeof(data->bmp380_cal_buf));

	/* Parse calibration values */
	calib->T1 = get_unaligned_le16(&data->bmp380_cal_buf[BMP380_T1]);
	calib->T2 = get_unaligned_le16(&data->bmp380_cal_buf[BMP380_T2]);
	calib->T3 = data->bmp380_cal_buf[BMP380_T3];
	calib->P1 = get_unaligned_le16(&data->bmp380_cal_buf[BMP380_P1]);
	calib->P2 = get_unaligned_le16(&data->bmp380_cal_buf[BMP380_P2]);
	calib->P3 = data->bmp380_cal_buf[BMP380_P3];
	calib->P4 = data->bmp380_cal_buf[BMP380_P4];
	calib->P5 = get_unaligned_le16(&data->bmp380_cal_buf[BMP380_P5]);
	calib->P6 = get_unaligned_le16(&data->bmp380_cal_buf[BMP380_P6]);
	calib->P7 = data->bmp380_cal_buf[BMP380_P7];
	calib->P8 = data->bmp380_cal_buf[BMP380_P8];
	calib->P9 = get_unaligned_le16(&data->bmp380_cal_buf[BMP380_P9]);
	calib->P10 = data->bmp380_cal_buf[BMP380_P10];
	calib->P11 = data->bmp380_cal_buf[BMP380_P11];

	return 0;
}

static const int bmp380_odr_table[][2] = {
	[BMP380_ODR_200HZ]	= {200, 0},
	[BMP380_ODR_100HZ]	= {100, 0},
	[BMP380_ODR_50HZ]	= {50, 0},
	[BMP380_ODR_25HZ]	= {25, 0},
	[BMP380_ODR_12_5HZ]	= {12, 500000},
	[BMP380_ODR_6_25HZ]	= {6, 250000},
	[BMP380_ODR_3_125HZ]	= {3, 125000},
	[BMP380_ODR_1_5625HZ]	= {1, 562500},
	[BMP380_ODR_0_78HZ]	= {0, 781250},
	[BMP380_ODR_0_39HZ]	= {0, 390625},
	[BMP380_ODR_0_2HZ]	= {0, 195313},
	[BMP380_ODR_0_1HZ]	= {0, 97656},
	[BMP380_ODR_0_05HZ]	= {0, 48828},
	[BMP380_ODR_0_02HZ]	= {0, 24414},
	[BMP380_ODR_0_01HZ]	= {0, 12207},
	[BMP380_ODR_0_006HZ]	= {0, 6104},
	[BMP380_ODR_0_003HZ]	= {0, 3052},
	[BMP380_ODR_0_0015HZ]	= {0, 1526},
};

static int bmp380_preinit(struct bmp280_data *data)
{
	/* BMP3xx requires soft-reset as part of initialization */
	return bmp380_cmd(data, BMP380_CMD_SOFT_RESET);
}

static int bmp380_chip_config(struct bmp280_data *data)
{
	bool change = false, aux;
	unsigned int tmp;
	u8 osrs;
	int ret;

	/* Configure power control register */
	ret = regmap_update_bits(data->regmap, BMP380_REG_POWER_CONTROL,
				 BMP380_CTRL_SENSORS_MASK,
				 BMP380_CTRL_SENSORS_PRESS_EN |
				 BMP380_CTRL_SENSORS_TEMP_EN);
	if (ret) {
		dev_err(data->dev,
			"failed to write operation control register\n");
		return ret;
	}

	/* Configure oversampling */
	osrs = FIELD_PREP(BMP380_OSRS_TEMP_MASK, data->oversampling_temp) |
	       FIELD_PREP(BMP380_OSRS_PRESS_MASK, data->oversampling_press);

	ret = regmap_update_bits_check(data->regmap, BMP380_REG_OSR,
				       BMP380_OSRS_TEMP_MASK |
				       BMP380_OSRS_PRESS_MASK,
				       osrs, &aux);
	if (ret) {
		dev_err(data->dev, "failed to write oversampling register\n");
		return ret;
	}
	change = change || aux;

	/* Configure output data rate */
	ret = regmap_update_bits_check(data->regmap, BMP380_REG_ODR,
				       BMP380_ODRS_MASK, data->sampling_freq, &aux);
	if (ret) {
		dev_err(data->dev, "failed to write ODR selection register\n");
		return ret;
	}
	change = change || aux;

	/* Set filter data */
	ret = regmap_update_bits_check(data->regmap, BMP380_REG_CONFIG, BMP380_FILTER_MASK,
				       FIELD_PREP(BMP380_FILTER_MASK, data->iir_filter_coeff),
				       &aux);
	if (ret) {
		dev_err(data->dev, "failed to write config register\n");
		return ret;
	}
	change = change || aux;

	if (change) {
		/*
		 * The configurations errors are detected on the fly during a measurement
		 * cycle. If the sampling frequency is too low, it's faster to reset
		 * the measurement loop than wait until the next measurement is due.
		 *
		 * Resets sensor measurement loop toggling between sleep and normal
		 * operating modes.
		 */
		ret = regmap_write_bits(data->regmap, BMP380_REG_POWER_CONTROL,
					BMP380_MODE_MASK,
					FIELD_PREP(BMP380_MODE_MASK, BMP380_MODE_SLEEP));
		if (ret) {
			dev_err(data->dev, "failed to set sleep mode\n");
			return ret;
		}
		usleep_range(2000, 2500);
		ret = regmap_write_bits(data->regmap, BMP380_REG_POWER_CONTROL,
					BMP380_MODE_MASK,
					FIELD_PREP(BMP380_MODE_MASK, BMP380_MODE_NORMAL));
		if (ret) {
			dev_err(data->dev, "failed to set normal mode\n");
			return ret;
		}
		/*
		 * Waits for measurement before checking configuration error flag.
		 * Selected longest measure time indicated in section 3.9.1
		 * in the datasheet.
		 */
		msleep(80);

		/* Check config error flag */
		ret = regmap_read(data->regmap, BMP380_REG_ERROR, &tmp);
		if (ret) {
			dev_err(data->dev,
				"failed to read error register\n");
			return ret;
		}
		if (tmp & BMP380_ERR_CONF_MASK) {
			dev_warn(data->dev,
				"sensor flagged configuration as incompatible\n");
			return -EINVAL;
		}
	}

	return 0;
}

static const int bmp380_oversampling_avail[] = { 1, 2, 4, 8, 16, 32 };
static const int bmp380_iir_filter_coeffs_avail[] = { 1, 2, 4, 8, 16, 32, 64, 128};

const struct bmp280_chip_info bmp380_chip_info = {
	.id_reg = BMP380_REG_ID,
	.chip_id = BMP380_CHIP_ID,
	.regmap_config = &bmp380_regmap_config,
	.start_up_time = 2000,
	.channels = bmp380_channels,
	.num_channels = 2,

	.oversampling_temp_avail = bmp380_oversampling_avail,
	.num_oversampling_temp_avail = ARRAY_SIZE(bmp380_oversampling_avail),
	.oversampling_temp_default = ilog2(1),

	.oversampling_press_avail = bmp380_oversampling_avail,
	.num_oversampling_press_avail = ARRAY_SIZE(bmp380_oversampling_avail),
	.oversampling_press_default = ilog2(4),

	.sampling_freq_avail = bmp380_odr_table,
	.num_sampling_freq_avail = ARRAY_SIZE(bmp380_odr_table) * 2,
	.sampling_freq_default = BMP380_ODR_50HZ,

	.iir_filter_coeffs_avail = bmp380_iir_filter_coeffs_avail,
	.num_iir_filter_coeffs_avail = ARRAY_SIZE(bmp380_iir_filter_coeffs_avail),
	.iir_filter_coeff_default = 2,

	.chip_config = bmp380_chip_config,
	.read_temp = bmp380_read_temp,
	.read_press = bmp380_read_press,
	.read_calib = bmp380_read_calib,
	.preinit = bmp380_preinit,
};
EXPORT_SYMBOL_NS(bmp380_chip_info, IIO_BMP280);

static int bmp580_soft_reset(struct bmp280_data *data)
{
	unsigned int reg;
	int ret;

	ret = regmap_write(data->regmap, BMP580_REG_CMD, BMP580_CMD_SOFT_RESET);
	if (ret) {
		dev_err(data->dev, "failed to send reset command to device\n");
		return ret;
	}
	usleep_range(2000, 2500);

	/* Dummy read of chip_id */
	ret = regmap_read(data->regmap, BMP580_REG_CHIP_ID, &reg);
	if (ret) {
		dev_err(data->dev, "failed to reestablish comms after reset\n");
		return ret;
	}

	ret = regmap_read(data->regmap, BMP580_REG_INT_STATUS, &reg);
	if (ret) {
		dev_err(data->dev, "error reading interrupt status register\n");
		return ret;
	}
	if (!(reg & BMP580_INT_STATUS_POR_MASK)) {
		dev_err(data->dev, "error resetting sensor\n");
		return -EINVAL;
	}

	return 0;
}

/**
 * bmp580_nvm_operation() - Helper function to commit NVM memory operations
 * @data: sensor data struct
 * @is_write: flag to signal write operation
 */
static int bmp580_nvm_operation(struct bmp280_data *data, bool is_write)
{
	unsigned long timeout, poll;
	unsigned int reg;
	int ret;

	/* Check NVM ready flag */
	ret = regmap_read(data->regmap, BMP580_REG_STATUS, &reg);
	if (ret) {
		dev_err(data->dev, "failed to check nvm status\n");
		return ret;
	}
	if (!(reg & BMP580_STATUS_NVM_RDY_MASK)) {
		dev_err(data->dev, "sensor's nvm is not ready\n");
		return -EIO;
	}

	/* Start NVM operation sequence */
	ret = regmap_write(data->regmap, BMP580_REG_CMD, BMP580_CMD_NVM_OP_SEQ_0);
	if (ret) {
		dev_err(data->dev, "failed to send nvm operation's first sequence\n");
		return ret;
	}
	if (is_write) {
		/* Send NVM write sequence */
		ret = regmap_write(data->regmap, BMP580_REG_CMD,
				   BMP580_CMD_NVM_WRITE_SEQ_1);
		if (ret) {
			dev_err(data->dev, "failed to send nvm write sequence\n");
			return ret;
		}
		/* Datasheet says on 4.8.1.2 it takes approximately 10ms */
		poll = 2000;
		timeout = 12000;
	} else {
		/* Send NVM read sequence */
		ret = regmap_write(data->regmap, BMP580_REG_CMD,
				   BMP580_CMD_NVM_READ_SEQ_1);
		if (ret) {
			dev_err(data->dev, "failed to send nvm read sequence\n");
			return ret;
		}
		/* Datasheet says on 4.8.1.1 it takes approximately 200us */
		poll = 50;
		timeout = 400;
	}
	if (ret) {
		dev_err(data->dev, "failed to write command sequence\n");
		return -EIO;
	}

	/* Wait until NVM is ready again */
	ret = regmap_read_poll_timeout(data->regmap, BMP580_REG_STATUS, reg,
				       (reg & BMP580_STATUS_NVM_RDY_MASK),
				       poll, timeout);
	if (ret) {
		dev_err(data->dev, "error checking nvm operation status\n");
		return ret;
	}

	/* Check NVM error flags */
	if ((reg & BMP580_STATUS_NVM_ERR_MASK) || (reg & BMP580_STATUS_NVM_CMD_ERR_MASK)) {
		dev_err(data->dev, "error processing nvm operation\n");
		return -EIO;
	}

	return 0;
}

/*
 * Contrary to previous sensors families, compensation algorithm is builtin.
 * We are only required to read the register raw data and adapt the ranges
 * for what is expected on IIO ABI.
 */

static int bmp580_read_temp(struct bmp280_data *data, int *val, int *val2)
{
	s32 raw_temp;
	int ret;

	ret = regmap_bulk_read(data->regmap, BMP580_REG_TEMP_XLSB, data->buf,
			       sizeof(data->buf));
	if (ret) {
		dev_err(data->dev, "failed to read temperature\n");
		return ret;
	}

	raw_temp = get_unaligned_le24(data->buf);
	if (raw_temp == BMP580_TEMP_SKIPPED) {
		dev_err(data->dev, "reading temperature skipped\n");
		return -EIO;
	}

	/*
	 * Temperature is returned in Celsius degrees in fractional
	 * form down 2^16. We reescale by x1000 to return milli Celsius
	 * to respect IIO ABI.
	 */
	*val = raw_temp * 1000;
	*val2 = 16;
	return IIO_VAL_FRACTIONAL_LOG2;
}

static int bmp580_read_press(struct bmp280_data *data, int *val, int *val2)
{
	u32 raw_press;
	int ret;

	ret = regmap_bulk_read(data->regmap, BMP580_REG_PRESS_XLSB, data->buf,
			       sizeof(data->buf));
	if (ret) {
		dev_err(data->dev, "failed to read pressure\n");
		return ret;
	}

	raw_press = get_unaligned_le24(data->buf);
	if (raw_press == BMP580_PRESS_SKIPPED) {
		dev_err(data->dev, "reading pressure skipped\n");
		return -EIO;
	}
	/*
	 * Pressure is returned in Pascals in fractional form down 2^16.
	 * We reescale /1000 to convert to kilopascal to respect IIO ABI.
	 */
	*val = raw_press;
	*val2 = 64000; /* 2^6 * 1000 */
	return IIO_VAL_FRACTIONAL;
}

static const int bmp580_odr_table[][2] = {
	[BMP580_ODR_240HZ] =	{240, 0},
	[BMP580_ODR_218HZ] =	{218, 0},
	[BMP580_ODR_199HZ] =	{199, 0},
	[BMP580_ODR_179HZ] =	{179, 0},
	[BMP580_ODR_160HZ] =	{160, 0},
	[BMP580_ODR_149HZ] =	{149, 0},
	[BMP580_ODR_140HZ] =	{140, 0},
	[BMP580_ODR_129HZ] =	{129, 0},
	[BMP580_ODR_120HZ] =	{120, 0},
	[BMP580_ODR_110HZ] =	{110, 0},
	[BMP580_ODR_100HZ] =	{100, 0},
	[BMP580_ODR_89HZ] =	{89, 0},
	[BMP580_ODR_80HZ] =	{80, 0},
	[BMP580_ODR_70HZ] =	{70, 0},
	[BMP580_ODR_60HZ] =	{60, 0},
	[BMP580_ODR_50HZ] =	{50, 0},
	[BMP580_ODR_45HZ] =	{45, 0},
	[BMP580_ODR_40HZ] =	{40, 0},
	[BMP580_ODR_35HZ] =	{35, 0},
	[BMP580_ODR_30HZ] =	{30, 0},
	[BMP580_ODR_25HZ] =	{25, 0},
	[BMP580_ODR_20HZ] =	{20, 0},
	[BMP580_ODR_15HZ] =	{15, 0},
	[BMP580_ODR_10HZ] =	{10, 0},
	[BMP580_ODR_5HZ] =	{5, 0},
	[BMP580_ODR_4HZ] =	{4, 0},
	[BMP580_ODR_3HZ] =	{3, 0},
	[BMP580_ODR_2HZ] =	{2, 0},
	[BMP580_ODR_1HZ] =	{1, 0},
	[BMP580_ODR_0_5HZ] =	{0, 500000},
	[BMP580_ODR_0_25HZ] =	{0, 250000},
	[BMP580_ODR_0_125HZ] =	{0, 125000},
};

static const int bmp580_nvmem_addrs[] = { 0x20, 0x21, 0x22 };

static int bmp580_nvmem_read(void *priv, unsigned int offset, void *val,
			     size_t bytes)
{
	struct bmp280_data *data = priv;
	u16 *dst = val;
	int ret, addr;

	pm_runtime_get_sync(data->dev);
	mutex_lock(&data->lock);

	/* Set sensor in standby mode */
	ret = regmap_update_bits(data->regmap, BMP580_REG_ODR_CONFIG,
				 BMP580_MODE_MASK | BMP580_ODR_DEEPSLEEP_DIS,
				 BMP580_ODR_DEEPSLEEP_DIS |
				 FIELD_PREP(BMP580_MODE_MASK, BMP580_MODE_SLEEP));
	if (ret) {
		dev_err(data->dev, "failed to change sensor to standby mode\n");
		goto exit;
	}
	/* Wait standby transition time */
	usleep_range(2500, 3000);

	while (bytes >= sizeof(*dst)) {
		addr = bmp580_nvmem_addrs[offset / sizeof(*dst)];

		ret = regmap_write(data->regmap, BMP580_REG_NVM_ADDR,
				   FIELD_PREP(BMP580_NVM_ROW_ADDR_MASK, addr));
		if (ret) {
			dev_err(data->dev, "error writing nvm address\n");
			goto exit;
		}

		ret = bmp580_nvm_operation(data, false);
		if (ret)
			goto exit;

		ret = regmap_bulk_read(data->regmap, BMP580_REG_NVM_DATA_LSB, &data->le16,
				       sizeof(data->le16));
		if (ret) {
			dev_err(data->dev, "error reading nvm data regs\n");
			goto exit;
		}

		*dst++ = le16_to_cpu(data->le16);
		bytes -= sizeof(*dst);
		offset += sizeof(*dst);
	}
exit:
	/* Restore chip config */
	data->chip_info->chip_config(data);
	mutex_unlock(&data->lock);
	pm_runtime_mark_last_busy(data->dev);
	pm_runtime_put_autosuspend(data->dev);
	return ret;
}

static int bmp580_nvmem_write(void *priv, unsigned int offset, void *val,
			      size_t bytes)
{
	struct bmp280_data *data = priv;
	u16 *buf = val;
	int ret, addr;

	pm_runtime_get_sync(data->dev);
	mutex_lock(&data->lock);

	/* Set sensor in standby mode */
	ret = regmap_update_bits(data->regmap, BMP580_REG_ODR_CONFIG,
				 BMP580_MODE_MASK | BMP580_ODR_DEEPSLEEP_DIS,
				 BMP580_ODR_DEEPSLEEP_DIS |
				 FIELD_PREP(BMP580_MODE_MASK, BMP580_MODE_SLEEP));
	if (ret) {
		dev_err(data->dev, "failed to change sensor to standby mode\n");
		goto exit;
	}
	/* Wait standby transition time */
	usleep_range(2500, 3000);

	while (bytes >= sizeof(*buf)) {
		addr = bmp580_nvmem_addrs[offset / sizeof(*buf)];

		ret = regmap_write(data->regmap, BMP580_REG_NVM_ADDR, BMP580_NVM_PROG_EN |
				   FIELD_PREP(BMP580_NVM_ROW_ADDR_MASK, addr));
		if (ret) {
			dev_err(data->dev, "error writing nvm address\n");
			goto exit;
		}
		data->le16 = cpu_to_le16(*buf++);

		ret = regmap_bulk_write(data->regmap, BMP580_REG_NVM_DATA_LSB, &data->le16,
					sizeof(data->le16));
		if (ret) {
			dev_err(data->dev, "error writing LSB NVM data regs\n");
			goto exit;
		}

		ret = bmp580_nvm_operation(data, true);
		if (ret)
			goto exit;

		/* Disable programming mode bit */
		ret = regmap_update_bits(data->regmap, BMP580_REG_NVM_ADDR,
					 BMP580_NVM_PROG_EN, 0);
		if (ret) {
			dev_err(data->dev, "error resetting nvm write\n");
			goto exit;
		}

		bytes -= sizeof(*buf);
		offset += sizeof(*buf);
	}
exit:
	/* Restore chip config */
	data->chip_info->chip_config(data);
	mutex_unlock(&data->lock);
	pm_runtime_mark_last_busy(data->dev);
	pm_runtime_put_autosuspend(data->dev);
	return ret;
}

static int bmp580_preinit(struct bmp280_data *data)
{
	struct nvmem_config config = {
		.dev = data->dev,
		.priv = data,
		.name = "bmp580_nvmem",
		.word_size = sizeof(u16),
		.stride = sizeof(u16),
		.size = 3 * sizeof(u16),
		.reg_read = bmp580_nvmem_read,
		.reg_write = bmp580_nvmem_write,
	};
	unsigned int reg;
	int ret;

	/* Issue soft-reset command */
	ret = bmp580_soft_reset(data);
	if (ret)
		return ret;

	/* Post powerup sequence */
	ret = regmap_read(data->regmap, BMP580_REG_CHIP_ID, &reg);
	if (ret)
		return ret;

	/* Print warn message if we don't know the chip id */
	if (reg != BMP580_CHIP_ID && reg != BMP580_CHIP_ID_ALT)
		dev_warn(data->dev, "preinit: unexpected chip_id\n");

	ret = regmap_read(data->regmap, BMP580_REG_STATUS, &reg);
	if (ret)
		return ret;

	/* Check nvm status */
	if (!(reg & BMP580_STATUS_NVM_RDY_MASK) || (reg & BMP580_STATUS_NVM_ERR_MASK)) {
		dev_err(data->dev, "preinit: nvm error on powerup sequence\n");
		return -EIO;
	}

	/* Register nvmem device */
	return PTR_ERR_OR_ZERO(devm_nvmem_register(config.dev, &config));
}

static int bmp580_chip_config(struct bmp280_data *data)
{
	bool change = false, aux;
	unsigned int tmp;
	u8 reg_val;
	int ret;

	/* Sets sensor in standby mode */
	ret = regmap_update_bits(data->regmap, BMP580_REG_ODR_CONFIG,
				 BMP580_MODE_MASK | BMP580_ODR_DEEPSLEEP_DIS,
				 BMP580_ODR_DEEPSLEEP_DIS |
				 FIELD_PREP(BMP580_MODE_MASK, BMP580_MODE_SLEEP));
	if (ret) {
		dev_err(data->dev, "failed to change sensor to standby mode\n");
		return ret;
	}
	/* From datasheet's table 4: electrical characteristics */
	usleep_range(2500, 3000);

	/* Set default DSP mode settings */
	reg_val = FIELD_PREP(BMP580_DSP_COMP_MASK, BMP580_DSP_PRESS_TEMP_COMP_EN) |
		  BMP580_DSP_SHDW_IIR_TEMP_EN | BMP580_DSP_SHDW_IIR_PRESS_EN;

	ret = regmap_update_bits(data->regmap, BMP580_REG_DSP_CONFIG,
				 BMP580_DSP_COMP_MASK |
				 BMP580_DSP_SHDW_IIR_TEMP_EN |
				 BMP580_DSP_SHDW_IIR_PRESS_EN, reg_val);

	/* Configure oversampling */
	reg_val = FIELD_PREP(BMP580_OSR_TEMP_MASK, data->oversampling_temp) |
		  FIELD_PREP(BMP580_OSR_PRESS_MASK, data->oversampling_press) |
		  BMP580_OSR_PRESS_EN;

	ret = regmap_update_bits_check(data->regmap, BMP580_REG_OSR_CONFIG,
				       BMP580_OSR_TEMP_MASK | BMP580_OSR_PRESS_MASK |
				       BMP580_OSR_PRESS_EN,
				       reg_val, &aux);
	if (ret) {
		dev_err(data->dev, "failed to write oversampling register\n");
		return ret;
	}
	change = change || aux;

	/* Configure output data rate */
	ret = regmap_update_bits_check(data->regmap, BMP580_REG_ODR_CONFIG, BMP580_ODR_MASK,
				       FIELD_PREP(BMP580_ODR_MASK, data->sampling_freq),
				       &aux);
	if (ret) {
		dev_err(data->dev, "failed to write ODR configuration register\n");
		return ret;
	}
	change = change || aux;

	/* Set filter data */
	reg_val = FIELD_PREP(BMP580_DSP_IIR_PRESS_MASK, data->iir_filter_coeff) |
		  FIELD_PREP(BMP580_DSP_IIR_TEMP_MASK, data->iir_filter_coeff);

	ret = regmap_update_bits_check(data->regmap, BMP580_REG_DSP_IIR,
				       BMP580_DSP_IIR_PRESS_MASK |
				       BMP580_DSP_IIR_TEMP_MASK,
				       reg_val, &aux);
	if (ret) {
		dev_err(data->dev, "failed to write config register\n");
		return ret;
	}
	change = change || aux;

	/* Restore sensor to normal operation mode */
	ret = regmap_write_bits(data->regmap, BMP580_REG_ODR_CONFIG,
				BMP580_MODE_MASK,
				FIELD_PREP(BMP580_MODE_MASK, BMP580_MODE_NORMAL));
	if (ret) {
		dev_err(data->dev, "failed to set normal mode\n");
		return ret;
	}
	/* From datasheet's table 4: electrical characteristics */
	usleep_range(3000, 3500);

	if (change) {
		/*
		 * Check if ODR and OSR settings are valid or we are
		 * operating in a degraded mode.
		 */
		ret = regmap_read(data->regmap, BMP580_REG_EFF_OSR, &tmp);
		if (ret) {
			dev_err(data->dev, "error reading effective OSR register\n");
			return ret;
		}
		if (!(tmp & BMP580_EFF_OSR_VALID_ODR)) {
			dev_warn(data->dev, "OSR and ODR incompatible settings detected\n");
			/* Set current OSR settings from data on effective OSR */
			data->oversampling_temp = FIELD_GET(BMP580_EFF_OSR_TEMP_MASK, tmp);
			data->oversampling_press = FIELD_GET(BMP580_EFF_OSR_PRESS_MASK, tmp);
			return -EINVAL;
		}
	}

	return 0;
}

static const int bmp580_oversampling_avail[] = { 1, 2, 4, 8, 16, 32, 64, 128 };

const struct bmp280_chip_info bmp580_chip_info = {
	.id_reg = BMP580_REG_CHIP_ID,
	.chip_id = BMP580_CHIP_ID,
	.regmap_config = &bmp580_regmap_config,
	.start_up_time = 2000,
	.channels = bmp380_channels,
	.num_channels = 2,

	.oversampling_temp_avail = bmp580_oversampling_avail,
	.num_oversampling_temp_avail = ARRAY_SIZE(bmp580_oversampling_avail),
	.oversampling_temp_default = ilog2(1),

	.oversampling_press_avail = bmp580_oversampling_avail,
	.num_oversampling_press_avail = ARRAY_SIZE(bmp580_oversampling_avail),
	.oversampling_press_default = ilog2(4),

	.sampling_freq_avail = bmp580_odr_table,
	.num_sampling_freq_avail = ARRAY_SIZE(bmp580_odr_table) * 2,
	.sampling_freq_default = BMP580_ODR_50HZ,

	.iir_filter_coeffs_avail = bmp380_iir_filter_coeffs_avail,
	.num_iir_filter_coeffs_avail = ARRAY_SIZE(bmp380_iir_filter_coeffs_avail),
	.iir_filter_coeff_default = 2,

	.chip_config = bmp580_chip_config,
	.read_temp = bmp580_read_temp,
	.read_press = bmp580_read_press,
	.preinit = bmp580_preinit,
};
EXPORT_SYMBOL_NS(bmp580_chip_info, IIO_BMP280);

static int bmp180_measure(struct bmp280_data *data, u8 ctrl_meas)
{
	const int conversion_time_max[] = { 4500, 7500, 13500, 25500 };
	unsigned int delay_us;
	unsigned int ctrl;
	int ret;

	if (data->use_eoc)
		reinit_completion(&data->done);

	ret = regmap_write(data->regmap, BMP280_REG_CTRL_MEAS, ctrl_meas);
	if (ret)
		return ret;

	if (data->use_eoc) {
		/*
		 * If we have a completion interrupt, use it, wait up to
		 * 100ms. The longest conversion time listed is 76.5 ms for
		 * advanced resolution mode.
		 */
		ret = wait_for_completion_timeout(&data->done,
						  1 + msecs_to_jiffies(100));
		if (!ret)
			dev_err(data->dev, "timeout waiting for completion\n");
	} else {
		if (FIELD_GET(BMP180_MEAS_CTRL_MASK, ctrl_meas) == BMP180_MEAS_TEMP)
			delay_us = 4500;
		else
			delay_us =
				conversion_time_max[data->oversampling_press];

		usleep_range(delay_us, delay_us + 1000);
	}

	ret = regmap_read(data->regmap, BMP280_REG_CTRL_MEAS, &ctrl);
	if (ret)
		return ret;

	/* The value of this bit reset to "0" after conversion is complete */
	if (ctrl & BMP180_MEAS_SCO)
		return -EIO;

	return 0;
}

static int bmp180_read_adc_temp(struct bmp280_data *data, int *val)
{
	int ret;

	ret = bmp180_measure(data,
			     FIELD_PREP(BMP180_MEAS_CTRL_MASK, BMP180_MEAS_TEMP) |
			     BMP180_MEAS_SCO);
	if (ret)
		return ret;

	ret = regmap_bulk_read(data->regmap, BMP180_REG_OUT_MSB,
			       &data->be16, sizeof(data->be16));
	if (ret)
		return ret;

	*val = be16_to_cpu(data->be16);

	return 0;
}

static int bmp180_read_calib(struct bmp280_data *data)
{
	struct bmp180_calib *calib = &data->calib.bmp180;
	int ret;
	int i;

	ret = regmap_bulk_read(data->regmap, BMP180_REG_CALIB_START,
			       data->bmp180_cal_buf, sizeof(data->bmp180_cal_buf));

	if (ret < 0)
		return ret;

	/* None of the words has the value 0 or 0xFFFF */
	for (i = 0; i < ARRAY_SIZE(data->bmp180_cal_buf); i++) {
		if (data->bmp180_cal_buf[i] == cpu_to_be16(0) ||
		    data->bmp180_cal_buf[i] == cpu_to_be16(0xffff))
			return -EIO;
	}

	/* Toss the calibration data into the entropy pool */
	add_device_randomness(data->bmp180_cal_buf, sizeof(data->bmp180_cal_buf));

	calib->AC1 = be16_to_cpu(data->bmp180_cal_buf[AC1]);
	calib->AC2 = be16_to_cpu(data->bmp180_cal_buf[AC2]);
	calib->AC3 = be16_to_cpu(data->bmp180_cal_buf[AC3]);
	calib->AC4 = be16_to_cpu(data->bmp180_cal_buf[AC4]);
	calib->AC5 = be16_to_cpu(data->bmp180_cal_buf[AC5]);
	calib->AC6 = be16_to_cpu(data->bmp180_cal_buf[AC6]);
	calib->B1 = be16_to_cpu(data->bmp180_cal_buf[B1]);
	calib->B2 = be16_to_cpu(data->bmp180_cal_buf[B2]);
	calib->MB = be16_to_cpu(data->bmp180_cal_buf[MB]);
	calib->MC = be16_to_cpu(data->bmp180_cal_buf[MC]);
	calib->MD = be16_to_cpu(data->bmp180_cal_buf[MD]);

	return 0;
}

/*
 * Returns temperature in DegC, resolution is 0.1 DegC.
 * t_fine carries fine temperature as global value.
 *
 * Taken from datasheet, Section 3.5, "Calculating pressure and temperature".
 */
static s32 bmp180_compensate_temp(struct bmp280_data *data, s32 adc_temp)
{
	struct bmp180_calib *calib = &data->calib.bmp180;
	s32 x1, x2;

	x1 = ((adc_temp - calib->AC6) * calib->AC5) >> 15;
	x2 = (calib->MC << 11) / (x1 + calib->MD);
	data->t_fine = x1 + x2;

	return (data->t_fine + 8) >> 4;
}

static int bmp180_read_temp(struct bmp280_data *data, int *val, int *val2)
{
	s32 adc_temp, comp_temp;
	int ret;

	ret = bmp180_read_adc_temp(data, &adc_temp);
	if (ret)
		return ret;

	comp_temp = bmp180_compensate_temp(data, adc_temp);

	/*
	 * val might be NULL if we're called by the read_press routine,
	 * who only cares about the carry over t_fine value.
	 */
	if (val) {
		*val = comp_temp * 100;
		return IIO_VAL_INT;
	}

	return 0;
}

static int bmp180_read_adc_press(struct bmp280_data *data, int *val)
{
	u8 oss = data->oversampling_press;
	int ret;

	ret = bmp180_measure(data,
			     FIELD_PREP(BMP180_MEAS_CTRL_MASK, BMP180_MEAS_PRESS) |
			     FIELD_PREP(BMP180_OSRS_PRESS_MASK, oss) |
			     BMP180_MEAS_SCO);
	if (ret)
		return ret;

	ret = regmap_bulk_read(data->regmap, BMP180_REG_OUT_MSB,
			       data->buf, sizeof(data->buf));
	if (ret)
		return ret;

	*val = get_unaligned_be24(data->buf) >> (8 - oss);

	return 0;
}

/*
 * Returns pressure in Pa, resolution is 1 Pa.
 *
 * Taken from datasheet, Section 3.5, "Calculating pressure and temperature".
 */
static u32 bmp180_compensate_press(struct bmp280_data *data, s32 adc_press)
{
	struct bmp180_calib *calib = &data->calib.bmp180;
	s32 oss = data->oversampling_press;
	s32 x1, x2, x3, p;
	s32 b3, b6;
	u32 b4, b7;

	b6 = data->t_fine - 4000;
	x1 = (calib->B2 * (b6 * b6 >> 12)) >> 11;
	x2 = calib->AC2 * b6 >> 11;
	x3 = x1 + x2;
	b3 = ((((s32)calib->AC1 * 4 + x3) << oss) + 2) / 4;
	x1 = calib->AC3 * b6 >> 13;
	x2 = (calib->B1 * ((b6 * b6) >> 12)) >> 16;
	x3 = (x1 + x2 + 2) >> 2;
	b4 = calib->AC4 * (u32)(x3 + 32768) >> 15;
	b7 = ((u32)adc_press - b3) * (50000 >> oss);
	if (b7 < 0x80000000)
		p = (b7 * 2) / b4;
	else
		p = (b7 / b4) * 2;

	x1 = (p >> 8) * (p >> 8);
	x1 = (x1 * 3038) >> 16;
	x2 = (-7357 * p) >> 16;

	return p + ((x1 + x2 + 3791) >> 4);
}

static int bmp180_read_press(struct bmp280_data *data,
			     int *val, int *val2)
{
	u32 comp_press;
	s32 adc_press;
	int ret;

	/* Read and compensate temperature so we get a reading of t_fine. */
	ret = bmp180_read_temp(data, NULL, NULL);
	if (ret)
		return ret;

	ret = bmp180_read_adc_press(data, &adc_press);
	if (ret)
		return ret;

	comp_press = bmp180_compensate_press(data, adc_press);

	*val = comp_press;
	*val2 = 1000;

	return IIO_VAL_FRACTIONAL;
}

static int bmp180_chip_config(struct bmp280_data *data)
{
	return 0;
}

static const int bmp180_oversampling_temp_avail[] = { 1 };
static const int bmp180_oversampling_press_avail[] = { 1, 2, 4, 8 };

const struct bmp280_chip_info bmp180_chip_info = {
	.id_reg = BMP280_REG_ID,
	.chip_id = BMP180_CHIP_ID,
	.regmap_config = &bmp180_regmap_config,
	.start_up_time = 2000,
	.channels = bmp280_channels,
	.num_channels = 2,

	.oversampling_temp_avail = bmp180_oversampling_temp_avail,
	.num_oversampling_temp_avail =
		ARRAY_SIZE(bmp180_oversampling_temp_avail),
	.oversampling_temp_default = 0,

	.oversampling_press_avail = bmp180_oversampling_press_avail,
	.num_oversampling_press_avail =
		ARRAY_SIZE(bmp180_oversampling_press_avail),
	.oversampling_press_default = BMP180_MEAS_PRESS_8X,

	.chip_config = bmp180_chip_config,
	.read_temp = bmp180_read_temp,
	.read_press = bmp180_read_press,
	.read_calib = bmp180_read_calib,
};
EXPORT_SYMBOL_NS(bmp180_chip_info, IIO_BMP280);

static irqreturn_t bmp085_eoc_irq(int irq, void *d)
{
	struct bmp280_data *data = d;

	complete(&data->done);

	return IRQ_HANDLED;
}

static int bmp085_fetch_eoc_irq(struct device *dev,
				const char *name,
				int irq,
				struct bmp280_data *data)
{
	unsigned long irq_trig;
	int ret;

	irq_trig = irqd_get_trigger_type(irq_get_irq_data(irq));
	if (irq_trig != IRQF_TRIGGER_RISING) {
		dev_err(dev, "non-rising trigger given for EOC interrupt, trying to enforce it\n");
		irq_trig = IRQF_TRIGGER_RISING;
	}

	init_completion(&data->done);

	ret = devm_request_threaded_irq(dev,
			irq,
			bmp085_eoc_irq,
			NULL,
			irq_trig,
			name,
			data);
	if (ret) {
		/* Bail out without IRQ but keep the driver in place */
		dev_err(dev, "unable to request DRDY IRQ\n");
		return 0;
	}

	data->use_eoc = true;
	return 0;
}

static void bmp280_pm_disable(void *data)
{
	struct device *dev = data;

	pm_runtime_get_sync(dev);
	pm_runtime_put_noidle(dev);
	pm_runtime_disable(dev);
}

static void bmp280_regulators_disable(void *data)
{
	struct regulator_bulk_data *supplies = data;

	regulator_bulk_disable(BMP280_NUM_SUPPLIES, supplies);
}

int bmp280_common_probe(struct device *dev,
			struct regmap *regmap,
			const struct bmp280_chip_info *chip_info,
			const char *name,
			int irq)
{
	struct iio_dev *indio_dev;
	struct bmp280_data *data;
	struct gpio_desc *gpiod;
	unsigned int chip_id;
	int ret;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	mutex_init(&data->lock);
	data->dev = dev;

	indio_dev->name = name;
	indio_dev->info = &bmp280_info;
	indio_dev->modes = INDIO_DIRECT_MODE;

	data->chip_info = chip_info;

	/* Apply initial values from chip info structure */
	indio_dev->channels = chip_info->channels;
	indio_dev->num_channels = chip_info->num_channels;
	data->oversampling_press = chip_info->oversampling_press_default;
	data->oversampling_humid = chip_info->oversampling_humid_default;
	data->oversampling_temp = chip_info->oversampling_temp_default;
	data->iir_filter_coeff = chip_info->iir_filter_coeff_default;
	data->sampling_freq = chip_info->sampling_freq_default;
	data->start_up_time = chip_info->start_up_time;

	/* Bring up regulators */
	regulator_bulk_set_supply_names(data->supplies,
					bmp280_supply_names,
					BMP280_NUM_SUPPLIES);

	ret = devm_regulator_bulk_get(dev,
				      BMP280_NUM_SUPPLIES, data->supplies);
	if (ret) {
		dev_err(dev, "failed to get regulators\n");
		return ret;
	}

	ret = regulator_bulk_enable(BMP280_NUM_SUPPLIES, data->supplies);
	if (ret) {
		dev_err(dev, "failed to enable regulators\n");
		return ret;
	}

	ret = devm_add_action_or_reset(dev, bmp280_regulators_disable,
				       data->supplies);
	if (ret)
		return ret;

	/* Wait to make sure we started up properly */
	usleep_range(data->start_up_time, data->start_up_time + 100);

	/* Bring chip out of reset if there is an assigned GPIO line */
	gpiod = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_HIGH);
	/* Deassert the signal */
	if (gpiod) {
		dev_info(dev, "release reset\n");
		gpiod_set_value(gpiod, 0);
	}

	data->regmap = regmap;

	ret = regmap_read(regmap, data->chip_info->id_reg, &chip_id);
	if (ret < 0)
		return ret;
	if (chip_id != data->chip_info->chip_id) {
		dev_err(dev, "bad chip id: expected %x got %x\n",
			data->chip_info->chip_id, chip_id);
		return -EINVAL;
	}

	if (data->chip_info->preinit) {
		ret = data->chip_info->preinit(data);
		if (ret)
			return dev_err_probe(data->dev, ret,
					     "error running preinit tasks\n");
	}

	ret = data->chip_info->chip_config(data);
	if (ret < 0)
		return ret;

	dev_set_drvdata(dev, indio_dev);

	/*
	 * Some chips have calibration parameters "programmed into the devices'
	 * non-volatile memory during production". Let's read them out at probe
	 * time once. They will not change.
	 */

	if (data->chip_info->read_calib) {
		ret = data->chip_info->read_calib(data);
		if (ret < 0)
			return dev_err_probe(data->dev, ret,
					     "failed to read calibration coefficients\n");
	}

	/*
	 * Attempt to grab an optional EOC IRQ - only the BMP085 has this
	 * however as it happens, the BMP085 shares the chip ID of BMP180
	 * so we look for an IRQ if we have that.
	 */
	if (irq > 0 && (chip_id  == BMP180_CHIP_ID)) {
		ret = bmp085_fetch_eoc_irq(dev, name, irq, data);
		if (ret)
			return ret;
	}

	/* Enable runtime PM */
	pm_runtime_get_noresume(dev);
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	/*
	 * Set autosuspend to two orders of magnitude larger than the
	 * start-up time.
	 */
	pm_runtime_set_autosuspend_delay(dev, data->start_up_time / 10);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_put(dev);

	ret = devm_add_action_or_reset(dev, bmp280_pm_disable, dev);
	if (ret)
		return ret;

	return devm_iio_device_register(dev, indio_dev);
}
EXPORT_SYMBOL_NS(bmp280_common_probe, IIO_BMP280);

static int bmp280_runtime_suspend(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct bmp280_data *data = iio_priv(indio_dev);

	return regulator_bulk_disable(BMP280_NUM_SUPPLIES, data->supplies);
}

static int bmp280_runtime_resume(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct bmp280_data *data = iio_priv(indio_dev);
	int ret;

	ret = regulator_bulk_enable(BMP280_NUM_SUPPLIES, data->supplies);
	if (ret)
		return ret;
	usleep_range(data->start_up_time, data->start_up_time + 100);
	return data->chip_info->chip_config(data);
}

EXPORT_RUNTIME_DEV_PM_OPS(bmp280_dev_pm_ops, bmp280_runtime_suspend,
			  bmp280_runtime_resume, NULL);

MODULE_AUTHOR("Vlad Dogaru <vlad.dogaru@intel.com>");
MODULE_DESCRIPTION("Driver for Bosch Sensortec BMP180/BMP280 pressure and temperature sensor");
MODULE_LICENSE("GPL v2");
