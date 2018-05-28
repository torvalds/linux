/*
 * Copyright (c) 2010 Christoph Mair <christoph.mair@gmail.com>
 * Copyright (c) 2012 Bosch Sensortec GmbH
 * Copyright (c) 2012 Unixphere AB
 * Copyright (c) 2014 Intel Corporation
 * Copyright (c) 2016 Linus Walleij <linus.walleij@linaro.org>
 *
 * Driver for Bosch Sensortec BMP180 and BMP280 digital pressure sensor.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Datasheet:
 * https://ae-bst.resource.bosch.com/media/_tech/media/datasheets/BST-BMP180-DS000-121.pdf
 * https://ae-bst.resource.bosch.com/media/_tech/media/datasheets/BST-BMP280-DS001-12.pdf
 * https://ae-bst.resource.bosch.com/media/_tech/media/datasheets/BST-BME280_DS001-11.pdf
 */

#define pr_fmt(fmt) "bmp280: " fmt

#include <linux/device.h>
#include <linux/module.h>
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

#include "bmp280.h"

/*
 * These enums are used for indexing into the array of calibration
 * coefficients for BMP180.
 */
enum { AC1, AC2, AC3, AC4, AC5, AC6, B1, B2, MB, MC, MD };

struct bmp180_calib {
	s16 AC1;
	s16 AC2;
	s16 AC3;
	u16 AC4;
	u16 AC5;
	u16 AC6;
	s16 B1;
	s16 B2;
	s16 MB;
	s16 MC;
	s16 MD;
};

struct bmp280_data {
	struct device *dev;
	struct mutex lock;
	struct regmap *regmap;
	struct completion done;
	bool use_eoc;
	const struct bmp280_chip_info *chip_info;
	struct bmp180_calib calib;
	struct regulator *vddd;
	struct regulator *vdda;
	unsigned int start_up_time; /* in microseconds */

	/* log of base 2 of oversampling rate */
	u8 oversampling_press;
	u8 oversampling_temp;
	u8 oversampling_humid;

	/*
	 * Carryover value from temperature conversion, used in pressure
	 * calculation.
	 */
	s32 t_fine;
};

struct bmp280_chip_info {
	const int *oversampling_temp_avail;
	int num_oversampling_temp_avail;

	const int *oversampling_press_avail;
	int num_oversampling_press_avail;

	const int *oversampling_humid_avail;
	int num_oversampling_humid_avail;

	int (*chip_config)(struct bmp280_data *);
	int (*read_temp)(struct bmp280_data *, int *);
	int (*read_press)(struct bmp280_data *, int *, int *);
	int (*read_humid)(struct bmp280_data *, int *, int *);
};

/*
 * These enums are used for indexing into the array of compensation
 * parameters for BMP280.
 */
enum { T1, T2, T3 };
enum { P1, P2, P3, P4, P5, P6, P7, P8, P9 };

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

/*
 * Returns humidity in percent, resolution is 0.01 percent. Output value of
 * "47445" represents 47445/1024 = 46.333 %RH.
 *
 * Taken from BME280 datasheet, Section 4.2.3, "Compensation formula".
 */

static u32 bmp280_compensate_humidity(struct bmp280_data *data,
				      s32 adc_humidity)
{
	struct device *dev = data->dev;
	unsigned int H1, H3, tmp;
	int H2, H4, H5, H6, ret, var;

	ret = regmap_read(data->regmap, BMP280_REG_COMP_H1, &H1);
	if (ret < 0) {
		dev_err(dev, "failed to read H1 comp value\n");
		return ret;
	}

	ret = regmap_bulk_read(data->regmap, BMP280_REG_COMP_H2, &tmp, 2);
	if (ret < 0) {
		dev_err(dev, "failed to read H2 comp value\n");
		return ret;
	}
	H2 = sign_extend32(le16_to_cpu(tmp), 15);

	ret = regmap_read(data->regmap, BMP280_REG_COMP_H3, &H3);
	if (ret < 0) {
		dev_err(dev, "failed to read H3 comp value\n");
		return ret;
	}

	ret = regmap_bulk_read(data->regmap, BMP280_REG_COMP_H4, &tmp, 2);
	if (ret < 0) {
		dev_err(dev, "failed to read H4 comp value\n");
		return ret;
	}
	H4 = sign_extend32(((be16_to_cpu(tmp) >> 4) & 0xff0) |
			  (be16_to_cpu(tmp) & 0xf), 11);

	ret = regmap_bulk_read(data->regmap, BMP280_REG_COMP_H5, &tmp, 2);
	if (ret < 0) {
		dev_err(dev, "failed to read H5 comp value\n");
		return ret;
	}
	H5 = sign_extend32(((le16_to_cpu(tmp) >> 4) & 0xfff), 11);

	ret = regmap_read(data->regmap, BMP280_REG_COMP_H6, &tmp);
	if (ret < 0) {
		dev_err(dev, "failed to read H6 comp value\n");
		return ret;
	}
	H6 = sign_extend32(tmp, 7);

	var = ((s32)data->t_fine) - (s32)76800;
	var = ((((adc_humidity << 14) - (H4 << 20) - (H5 * var))
		+ (s32)16384) >> 15) * (((((((var * H6) >> 10)
		* (((var * (s32)H3) >> 11) + (s32)32768)) >> 10)
		+ (s32)2097152) * H2 + 8192) >> 14);
	var -= ((((var >> 15) * (var >> 15)) >> 7) * (s32)H1) >> 4;

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
	int ret;
	s32 var1, var2;
	__le16 buf[BMP280_COMP_TEMP_REG_COUNT / 2];

	ret = regmap_bulk_read(data->regmap, BMP280_REG_COMP_TEMP_START,
			       buf, BMP280_COMP_TEMP_REG_COUNT);
	if (ret < 0) {
		dev_err(data->dev,
			"failed to read temperature calibration parameters\n");
		return ret;
	}

	/*
	 * The double casts are necessary because le16_to_cpu returns an
	 * unsigned 16-bit value.  Casting that value directly to a
	 * signed 32-bit will not do proper sign extension.
	 *
	 * Conversely, T1 and P1 are unsigned values, so they can be
	 * cast straight to the larger type.
	 */
	var1 = (((adc_temp >> 3) - ((s32)le16_to_cpu(buf[T1]) << 1)) *
		((s32)(s16)le16_to_cpu(buf[T2]))) >> 11;
	var2 = (((((adc_temp >> 4) - ((s32)le16_to_cpu(buf[T1]))) *
		  ((adc_temp >> 4) - ((s32)le16_to_cpu(buf[T1])))) >> 12) *
		((s32)(s16)le16_to_cpu(buf[T3]))) >> 14;
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
	int ret;
	s64 var1, var2, p;
	__le16 buf[BMP280_COMP_PRESS_REG_COUNT / 2];

	ret = regmap_bulk_read(data->regmap, BMP280_REG_COMP_PRESS_START,
			       buf, BMP280_COMP_PRESS_REG_COUNT);
	if (ret < 0) {
		dev_err(data->dev,
			"failed to read pressure calibration parameters\n");
		return ret;
	}

	var1 = ((s64)data->t_fine) - 128000;
	var2 = var1 * var1 * (s64)(s16)le16_to_cpu(buf[P6]);
	var2 += (var1 * (s64)(s16)le16_to_cpu(buf[P5])) << 17;
	var2 += ((s64)(s16)le16_to_cpu(buf[P4])) << 35;
	var1 = ((var1 * var1 * (s64)(s16)le16_to_cpu(buf[P3])) >> 8) +
		((var1 * (s64)(s16)le16_to_cpu(buf[P2])) << 12);
	var1 = ((((s64)1) << 47) + var1) * ((s64)le16_to_cpu(buf[P1])) >> 33;

	if (var1 == 0)
		return 0;

	p = ((((s64)1048576 - adc_press) << 31) - var2) * 3125;
	p = div64_s64(p, var1);
	var1 = (((s64)(s16)le16_to_cpu(buf[P9])) * (p >> 13) * (p >> 13)) >> 25;
	var2 = (((s64)(s16)le16_to_cpu(buf[P8])) * p) >> 19;
	p = ((p + var1 + var2) >> 8) + (((s64)(s16)le16_to_cpu(buf[P7])) << 4);

	return (u32)p;
}

static int bmp280_read_temp(struct bmp280_data *data,
			    int *val)
{
	int ret;
	__be32 tmp = 0;
	s32 adc_temp, comp_temp;

	ret = regmap_bulk_read(data->regmap, BMP280_REG_TEMP_MSB,
			       (u8 *) &tmp, 3);
	if (ret < 0) {
		dev_err(data->dev, "failed to read temperature\n");
		return ret;
	}

	adc_temp = be32_to_cpu(tmp) >> 12;
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
	int ret;
	__be32 tmp = 0;
	s32 adc_press;
	u32 comp_press;

	/* Read and compensate temperature so we get a reading of t_fine. */
	ret = bmp280_read_temp(data, NULL);
	if (ret < 0)
		return ret;

	ret = regmap_bulk_read(data->regmap, BMP280_REG_PRESS_MSB,
			       (u8 *) &tmp, 3);
	if (ret < 0) {
		dev_err(data->dev, "failed to read pressure\n");
		return ret;
	}

	adc_press = be32_to_cpu(tmp) >> 12;
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
	int ret;
	__be16 tmp = 0;
	s32 adc_humidity;
	u32 comp_humidity;

	/* Read and compensate temperature so we get a reading of t_fine. */
	ret = bmp280_read_temp(data, NULL);
	if (ret < 0)
		return ret;

	ret = regmap_bulk_read(data->regmap, BMP280_REG_HUMIDITY_MSB,
			       (u8 *) &tmp, 2);
	if (ret < 0) {
		dev_err(data->dev, "failed to read humidity\n");
		return ret;
	}

	adc_humidity = be16_to_cpu(tmp);
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
	int ret;
	struct bmp280_data *data = iio_priv(indio_dev);

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
			ret = data->chip_info->read_temp(data, val);
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
	int i;
	const int *avail = data->chip_info->oversampling_humid_avail;
	const int n = data->chip_info->num_oversampling_humid_avail;

	for (i = 0; i < n; i++) {
		if (avail[i] == val) {
			data->oversampling_humid = ilog2(val);

			return data->chip_info->chip_config(data);
		}
	}
	return -EINVAL;
}

static int bmp280_write_oversampling_ratio_temp(struct bmp280_data *data,
					       int val)
{
	int i;
	const int *avail = data->chip_info->oversampling_temp_avail;
	const int n = data->chip_info->num_oversampling_temp_avail;

	for (i = 0; i < n; i++) {
		if (avail[i] == val) {
			data->oversampling_temp = ilog2(val);

			return data->chip_info->chip_config(data);
		}
	}
	return -EINVAL;
}

static int bmp280_write_oversampling_ratio_press(struct bmp280_data *data,
					       int val)
{
	int i;
	const int *avail = data->chip_info->oversampling_press_avail;
	const int n = data->chip_info->num_oversampling_press_avail;

	for (i = 0; i < n; i++) {
		if (avail[i] == val) {
			data->oversampling_press = ilog2(val);

			return data->chip_info->chip_config(data);
		}
	}
	return -EINVAL;
}

static int bmp280_write_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int val, int val2, long mask)
{
	int ret = 0;
	struct bmp280_data *data = iio_priv(indio_dev);

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
	default:
		return -EINVAL;
	}

	return ret;
}

static ssize_t bmp280_show_avail(char *buf, const int *vals, const int n)
{
	size_t len = 0;
	int i;

	for (i = 0; i < n; i++)
		len += scnprintf(buf + len, PAGE_SIZE - len, "%d ", vals[i]);

	buf[len - 1] = '\n';

	return len;
}

static ssize_t bmp280_show_temp_oversampling_avail(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct bmp280_data *data = iio_priv(dev_to_iio_dev(dev));

	return bmp280_show_avail(buf, data->chip_info->oversampling_temp_avail,
				 data->chip_info->num_oversampling_temp_avail);
}

static ssize_t bmp280_show_press_oversampling_avail(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct bmp280_data *data = iio_priv(dev_to_iio_dev(dev));

	return bmp280_show_avail(buf, data->chip_info->oversampling_press_avail,
				 data->chip_info->num_oversampling_press_avail);
}

static IIO_DEVICE_ATTR(in_temp_oversampling_ratio_available,
	S_IRUGO, bmp280_show_temp_oversampling_avail, NULL, 0);

static IIO_DEVICE_ATTR(in_pressure_oversampling_ratio_available,
	S_IRUGO, bmp280_show_press_oversampling_avail, NULL, 0);

static struct attribute *bmp280_attributes[] = {
	&iio_dev_attr_in_temp_oversampling_ratio_available.dev_attr.attr,
	&iio_dev_attr_in_pressure_oversampling_ratio_available.dev_attr.attr,
	NULL,
};

static const struct attribute_group bmp280_attrs_group = {
	.attrs = bmp280_attributes,
};

static const struct iio_info bmp280_info = {
	.driver_module = THIS_MODULE,
	.read_raw = &bmp280_read_raw,
	.write_raw = &bmp280_write_raw,
	.attrs = &bmp280_attrs_group,
};

static int bmp280_chip_config(struct bmp280_data *data)
{
	int ret;
	u8 osrs = BMP280_OSRS_TEMP_X(data->oversampling_temp + 1) |
		  BMP280_OSRS_PRESS_X(data->oversampling_press + 1);

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

static const struct bmp280_chip_info bmp280_chip_info = {
	.oversampling_temp_avail = bmp280_oversampling_avail,
	.num_oversampling_temp_avail = ARRAY_SIZE(bmp280_oversampling_avail),

	.oversampling_press_avail = bmp280_oversampling_avail,
	.num_oversampling_press_avail = ARRAY_SIZE(bmp280_oversampling_avail),

	.chip_config = bmp280_chip_config,
	.read_temp = bmp280_read_temp,
	.read_press = bmp280_read_press,
};

static int bme280_chip_config(struct bmp280_data *data)
{
	int ret;
	u8 osrs = BMP280_OSRS_HUMIDITIY_X(data->oversampling_humid + 1);

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

static const struct bmp280_chip_info bme280_chip_info = {
	.oversampling_temp_avail = bmp280_oversampling_avail,
	.num_oversampling_temp_avail = ARRAY_SIZE(bmp280_oversampling_avail),

	.oversampling_press_avail = bmp280_oversampling_avail,
	.num_oversampling_press_avail = ARRAY_SIZE(bmp280_oversampling_avail),

	.oversampling_humid_avail = bmp280_oversampling_avail,
	.num_oversampling_humid_avail = ARRAY_SIZE(bmp280_oversampling_avail),

	.chip_config = bme280_chip_config,
	.read_temp = bmp280_read_temp,
	.read_press = bmp280_read_press,
	.read_humid = bmp280_read_humid,
};

static int bmp180_measure(struct bmp280_data *data, u8 ctrl_meas)
{
	int ret;
	const int conversion_time_max[] = { 4500, 7500, 13500, 25500 };
	unsigned int delay_us;
	unsigned int ctrl;

	if (data->use_eoc)
		init_completion(&data->done);

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
		if (ctrl_meas == BMP180_MEAS_TEMP)
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
	__be16 tmp = 0;

	ret = bmp180_measure(data, BMP180_MEAS_TEMP);
	if (ret)
		return ret;

	ret = regmap_bulk_read(data->regmap, BMP180_REG_OUT_MSB, (u8 *)&tmp, 2);
	if (ret)
		return ret;

	*val = be16_to_cpu(tmp);

	return 0;
}

static int bmp180_read_calib(struct bmp280_data *data,
			     struct bmp180_calib *calib)
{
	int ret;
	int i;
	__be16 buf[BMP180_REG_CALIB_COUNT / 2];

	ret = regmap_bulk_read(data->regmap, BMP180_REG_CALIB_START, buf,
			       sizeof(buf));

	if (ret < 0)
		return ret;

	/* None of the words has the value 0 or 0xFFFF */
	for (i = 0; i < ARRAY_SIZE(buf); i++) {
		if (buf[i] == cpu_to_be16(0) || buf[i] == cpu_to_be16(0xffff))
			return -EIO;
	}

	/* Toss the calibration data into the entropy pool */
	add_device_randomness(buf, sizeof(buf));

	calib->AC1 = be16_to_cpu(buf[AC1]);
	calib->AC2 = be16_to_cpu(buf[AC2]);
	calib->AC3 = be16_to_cpu(buf[AC3]);
	calib->AC4 = be16_to_cpu(buf[AC4]);
	calib->AC5 = be16_to_cpu(buf[AC5]);
	calib->AC6 = be16_to_cpu(buf[AC6]);
	calib->B1 = be16_to_cpu(buf[B1]);
	calib->B2 = be16_to_cpu(buf[B2]);
	calib->MB = be16_to_cpu(buf[MB]);
	calib->MC = be16_to_cpu(buf[MC]);
	calib->MD = be16_to_cpu(buf[MD]);

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
	s32 x1, x2;
	struct bmp180_calib *calib = &data->calib;

	x1 = ((adc_temp - calib->AC6) * calib->AC5) >> 15;
	x2 = (calib->MC << 11) / (x1 + calib->MD);
	data->t_fine = x1 + x2;

	return (data->t_fine + 8) >> 4;
}

static int bmp180_read_temp(struct bmp280_data *data, int *val)
{
	int ret;
	s32 adc_temp, comp_temp;

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
	int ret;
	__be32 tmp = 0;
	u8 oss = data->oversampling_press;

	ret = bmp180_measure(data, BMP180_MEAS_PRESS_X(oss));
	if (ret)
		return ret;

	ret = regmap_bulk_read(data->regmap, BMP180_REG_OUT_MSB, (u8 *)&tmp, 3);
	if (ret)
		return ret;

	*val = (be32_to_cpu(tmp) >> 8) >> (8 - oss);

	return 0;
}

/*
 * Returns pressure in Pa, resolution is 1 Pa.
 *
 * Taken from datasheet, Section 3.5, "Calculating pressure and temperature".
 */
static u32 bmp180_compensate_press(struct bmp280_data *data, s32 adc_press)
{
	s32 x1, x2, x3, p;
	s32 b3, b6;
	u32 b4, b7;
	s32 oss = data->oversampling_press;
	struct bmp180_calib *calib = &data->calib;

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
	int ret;
	s32 adc_press;
	u32 comp_press;

	/* Read and compensate temperature so we get a reading of t_fine. */
	ret = bmp180_read_temp(data, NULL);
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

static const struct bmp280_chip_info bmp180_chip_info = {
	.oversampling_temp_avail = bmp180_oversampling_temp_avail,
	.num_oversampling_temp_avail =
		ARRAY_SIZE(bmp180_oversampling_temp_avail),

	.oversampling_press_avail = bmp180_oversampling_press_avail,
	.num_oversampling_press_avail =
		ARRAY_SIZE(bmp180_oversampling_press_avail),

	.chip_config = bmp180_chip_config,
	.read_temp = bmp180_read_temp,
	.read_press = bmp180_read_press,
};

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
		dev_err(dev, "non-rising trigger given for EOC interrupt, "
			"trying to enforce it\n");
		irq_trig = IRQF_TRIGGER_RISING;
	}
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

int bmp280_common_probe(struct device *dev,
			struct regmap *regmap,
			unsigned int chip,
			const char *name,
			int irq)
{
	int ret;
	struct iio_dev *indio_dev;
	struct bmp280_data *data;
	unsigned int chip_id;
	struct gpio_desc *gpiod;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	mutex_init(&data->lock);
	data->dev = dev;

	indio_dev->dev.parent = dev;
	indio_dev->name = name;
	indio_dev->channels = bmp280_channels;
	indio_dev->info = &bmp280_info;
	indio_dev->modes = INDIO_DIRECT_MODE;

	switch (chip) {
	case BMP180_CHIP_ID:
		indio_dev->num_channels = 2;
		data->chip_info = &bmp180_chip_info;
		data->oversampling_press = ilog2(8);
		data->oversampling_temp = ilog2(1);
		data->start_up_time = 10000;
		break;
	case BMP280_CHIP_ID:
		indio_dev->num_channels = 2;
		data->chip_info = &bmp280_chip_info;
		data->oversampling_press = ilog2(16);
		data->oversampling_temp = ilog2(2);
		data->start_up_time = 2000;
		break;
	case BME280_CHIP_ID:
		indio_dev->num_channels = 3;
		data->chip_info = &bme280_chip_info;
		data->oversampling_press = ilog2(16);
		data->oversampling_humid = ilog2(16);
		data->oversampling_temp = ilog2(2);
		data->start_up_time = 2000;
		break;
	default:
		return -EINVAL;
	}

	/* Bring up regulators */
	data->vddd = devm_regulator_get(dev, "vddd");
	if (IS_ERR(data->vddd)) {
		dev_err(dev, "failed to get VDDD regulator\n");
		return PTR_ERR(data->vddd);
	}
	ret = regulator_enable(data->vddd);
	if (ret) {
		dev_err(dev, "failed to enable VDDD regulator\n");
		return ret;
	}
	data->vdda = devm_regulator_get(dev, "vdda");
	if (IS_ERR(data->vdda)) {
		dev_err(dev, "failed to get VDDA regulator\n");
		ret = PTR_ERR(data->vdda);
		goto out_disable_vddd;
	}
	ret = regulator_enable(data->vdda);
	if (ret) {
		dev_err(dev, "failed to enable VDDA regulator\n");
		goto out_disable_vddd;
	}
	/* Wait to make sure we started up properly */
	usleep_range(data->start_up_time, data->start_up_time + 100);

	/* Bring chip out of reset if there is an assigned GPIO line */
	gpiod = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	/* Deassert the signal */
	if (!IS_ERR(gpiod)) {
		dev_info(dev, "release reset\n");
		gpiod_set_value(gpiod, 0);
	}

	data->regmap = regmap;
	ret = regmap_read(regmap, BMP280_REG_ID, &chip_id);
	if (ret < 0)
		goto out_disable_vdda;
	if (chip_id != chip) {
		dev_err(dev, "bad chip id: expected %x got %x\n",
			chip, chip_id);
		ret = -EINVAL;
		goto out_disable_vdda;
	}

	ret = data->chip_info->chip_config(data);
	if (ret < 0)
		goto out_disable_vdda;

	dev_set_drvdata(dev, indio_dev);

	/*
	 * The BMP085 and BMP180 has calibration in an E2PROM, read it out
	 * at probe time. It will not change.
	 */
	if (chip_id  == BMP180_CHIP_ID) {
		ret = bmp180_read_calib(data, &data->calib);
		if (ret < 0) {
			dev_err(data->dev,
				"failed to read calibration coefficients\n");
			goto out_disable_vdda;
		}
	}

	/*
	 * Attempt to grab an optional EOC IRQ - only the BMP085 has this
	 * however as it happens, the BMP085 shares the chip ID of BMP180
	 * so we look for an IRQ if we have that.
	 */
	if (irq > 0 || (chip_id  == BMP180_CHIP_ID)) {
		ret = bmp085_fetch_eoc_irq(dev, name, irq, data);
		if (ret)
			goto out_disable_vdda;
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

	ret = iio_device_register(indio_dev);
	if (ret)
		goto out_runtime_pm_disable;


	return 0;

out_runtime_pm_disable:
	pm_runtime_get_sync(data->dev);
	pm_runtime_put_noidle(data->dev);
	pm_runtime_disable(data->dev);
out_disable_vdda:
	regulator_disable(data->vdda);
out_disable_vddd:
	regulator_disable(data->vddd);
	return ret;
}
EXPORT_SYMBOL(bmp280_common_probe);

int bmp280_common_remove(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct bmp280_data *data = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);
	pm_runtime_get_sync(data->dev);
	pm_runtime_put_noidle(data->dev);
	pm_runtime_disable(data->dev);
	regulator_disable(data->vdda);
	regulator_disable(data->vddd);
	return 0;
}
EXPORT_SYMBOL(bmp280_common_remove);

#ifdef CONFIG_PM
static int bmp280_runtime_suspend(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct bmp280_data *data = iio_priv(indio_dev);
	int ret;

	ret = regulator_disable(data->vdda);
	if (ret)
		return ret;
	return regulator_disable(data->vddd);
}

static int bmp280_runtime_resume(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct bmp280_data *data = iio_priv(indio_dev);
	int ret;

	ret = regulator_enable(data->vddd);
	if (ret)
		return ret;
	ret = regulator_enable(data->vdda);
	if (ret)
		return ret;
	usleep_range(data->start_up_time, data->start_up_time + 100);
	return data->chip_info->chip_config(data);
}
#endif /* CONFIG_PM */

const struct dev_pm_ops bmp280_dev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
	SET_RUNTIME_PM_OPS(bmp280_runtime_suspend,
			   bmp280_runtime_resume, NULL)
};
EXPORT_SYMBOL(bmp280_dev_pm_ops);

MODULE_AUTHOR("Vlad Dogaru <vlad.dogaru@intel.com>");
MODULE_DESCRIPTION("Driver for Bosch Sensortec BMP180/BMP280 pressure and temperature sensor");
MODULE_LICENSE("GPL v2");
