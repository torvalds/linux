/*
 * Copyright (c) 2015 Intel Corporation
 *
 * Driver for UPISEMI us5182d Proximity and Ambient Light Sensor.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * To do: Interrupt support.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/acpi.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/iio/events.h>
#include <linux/iio/iio.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/iio/sysfs.h>
#include <linux/mutex.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>

#define US5182D_REG_CFG0				0x00
#define US5182D_CFG0_ONESHOT_EN				BIT(6)
#define US5182D_CFG0_SHUTDOWN_EN			BIT(7)
#define US5182D_CFG0_WORD_ENABLE			BIT(0)
#define US5182D_CFG0_PROX				BIT(3)
#define US5182D_CFG0_PX_IRQ				BIT(2)

#define US5182D_REG_CFG1				0x01
#define US5182D_CFG1_ALS_RES16				BIT(4)
#define US5182D_CFG1_AGAIN_DEFAULT			0x00

#define US5182D_REG_CFG2				0x02
#define US5182D_CFG2_PX_RES16				BIT(4)
#define US5182D_CFG2_PXGAIN_DEFAULT			BIT(2)

#define US5182D_REG_CFG3				0x03
#define US5182D_CFG3_LED_CURRENT100			(BIT(4) | BIT(5))
#define US5182D_CFG3_INT_SOURCE_PX			BIT(3)

#define US5182D_REG_CFG4				0x10

/*
 * Registers for tuning the auto dark current cancelling feature.
 * DARK_TH(reg 0x27,0x28) - threshold (counts) for auto dark cancelling.
 * when ALS  > DARK_TH --> ALS_Code = ALS - Upper(0x2A) * Dark
 * when ALS < DARK_TH --> ALS_Code = ALS - Lower(0x29) * Dark
 */
#define US5182D_REG_UDARK_TH			0x27
#define US5182D_REG_DARK_AUTO_EN		0x2b
#define US5182D_REG_AUTO_LDARK_GAIN		0x29
#define US5182D_REG_AUTO_HDARK_GAIN		0x2a

/* Thresholds for events: px low (0x08-l, 0x09-h), px high (0x0a-l 0x0b-h) */
#define US5182D_REG_PXL_TH			0x08
#define US5182D_REG_PXH_TH			0x0a

#define US5182D_REG_PXL_TH_DEFAULT		1000
#define US5182D_REG_PXH_TH_DEFAULT		30000

#define US5182D_OPMODE_ALS			0x01
#define US5182D_OPMODE_PX			0x02
#define US5182D_OPMODE_SHIFT			4

#define US5182D_REG_DARK_AUTO_EN_DEFAULT	0x80
#define US5182D_REG_AUTO_LDARK_GAIN_DEFAULT	0x16
#define US5182D_REG_AUTO_HDARK_GAIN_DEFAULT	0x00

#define US5182D_REG_ADL				0x0c
#define US5182D_REG_PDL				0x0e

#define US5182D_REG_MODE_STORE			0x21
#define US5182D_STORE_MODE			0x01

#define US5182D_REG_CHIPID			0xb2

#define US5182D_OPMODE_MASK			GENMASK(5, 4)
#define US5182D_AGAIN_MASK			0x07
#define US5182D_RESET_CHIP			0x01

#define US5182D_CHIPID				0x26
#define US5182D_DRV_NAME			"us5182d"

#define US5182D_GA_RESOLUTION			1000

#define US5182D_READ_BYTE			1
#define US5182D_READ_WORD			2
#define US5182D_OPSTORE_SLEEP_TIME		20 /* ms */
#define US5182D_SLEEP_MS			3000 /* ms */
#define US5182D_PXH_TH_DISABLE			0xffff
#define US5182D_PXL_TH_DISABLE			0x0000

/* Available ranges: [12354, 7065, 3998, 2202, 1285, 498, 256, 138] lux */
static const int us5182d_scales[] = {188500, 107800, 61000, 33600, 19600, 7600,
				     3900, 2100};

/*
 * Experimental thresholds that work with US5182D sensor on evaluation board
 * roughly between 12-32 lux
 */
static u16 us5182d_dark_ths_vals[] = {170, 200, 512, 512, 800, 2000, 4000,
				      8000};

enum mode {
	US5182D_ALS_PX,
	US5182D_ALS_ONLY,
	US5182D_PX_ONLY
};

enum pmode {
	US5182D_CONTINUOUS,
	US5182D_ONESHOT
};

struct us5182d_data {
	struct i2c_client *client;
	struct mutex lock;

	/* Glass attenuation factor */
	u32 ga;

	/* Dark gain tuning */
	u8 lower_dark_gain;
	u8 upper_dark_gain;
	u16 *us5182d_dark_ths;

	u16 px_low_th;
	u16 px_high_th;

	int rising_en;
	int falling_en;

	u8 opmode;
	u8 power_mode;

	bool als_enabled;
	bool px_enabled;

	bool default_continuous;
};

static IIO_CONST_ATTR(in_illuminance_scale_available,
		      "0.0021 0.0039 0.0076 0.0196 0.0336 0.061 0.1078 0.1885");

static struct attribute *us5182d_attrs[] = {
	&iio_const_attr_in_illuminance_scale_available.dev_attr.attr,
	NULL
};

static const struct attribute_group us5182d_attr_group = {
	.attrs = us5182d_attrs,
};

static const struct {
	u8 reg;
	u8 val;
} us5182d_regvals[] = {
	{US5182D_REG_CFG0, US5182D_CFG0_WORD_ENABLE},
	{US5182D_REG_CFG1, US5182D_CFG1_ALS_RES16},
	{US5182D_REG_CFG2, (US5182D_CFG2_PX_RES16 |
			    US5182D_CFG2_PXGAIN_DEFAULT)},
	{US5182D_REG_CFG3, US5182D_CFG3_LED_CURRENT100 |
			   US5182D_CFG3_INT_SOURCE_PX},
	{US5182D_REG_CFG4, 0x00},
};

static const struct iio_event_spec us5182d_events[] = {
	{
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_RISING,
		.mask_separate = BIT(IIO_EV_INFO_VALUE) |
				BIT(IIO_EV_INFO_ENABLE),
	},
	{
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_FALLING,
		.mask_separate = BIT(IIO_EV_INFO_VALUE) |
				BIT(IIO_EV_INFO_ENABLE),
	},
};

static const struct iio_chan_spec us5182d_channels[] = {
	{
		.type = IIO_LIGHT,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_SCALE),
	},
	{
		.type = IIO_PROXIMITY,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.event_spec = us5182d_events,
		.num_event_specs = ARRAY_SIZE(us5182d_events),
	}
};

static int us5182d_oneshot_en(struct us5182d_data *data)
{
	int ret;

	ret = i2c_smbus_read_byte_data(data->client, US5182D_REG_CFG0);
	if (ret < 0)
		return ret;

	/*
	 * In oneshot mode the chip will power itself down after taking the
	 * required measurement.
	 */
	ret = ret | US5182D_CFG0_ONESHOT_EN;

	return i2c_smbus_write_byte_data(data->client, US5182D_REG_CFG0, ret);
}

static int us5182d_set_opmode(struct us5182d_data *data, u8 mode)
{
	int ret;

	if (mode == data->opmode)
		return 0;

	ret = i2c_smbus_read_byte_data(data->client, US5182D_REG_CFG0);
	if (ret < 0)
		return ret;

	/* update mode */
	ret = ret & ~US5182D_OPMODE_MASK;
	ret = ret | (mode << US5182D_OPMODE_SHIFT);

	/*
	 * After updating the operating mode, the chip requires that
	 * the operation is stored, by writing 1 in the STORE_MODE
	 * register (auto-clearing).
	 */
	ret = i2c_smbus_write_byte_data(data->client, US5182D_REG_CFG0, ret);
	if (ret < 0)
		return ret;

	ret = i2c_smbus_write_byte_data(data->client, US5182D_REG_MODE_STORE,
					US5182D_STORE_MODE);
	if (ret < 0)
		return ret;

	data->opmode = mode;
	msleep(US5182D_OPSTORE_SLEEP_TIME);

	return 0;
}

static int us5182d_als_enable(struct us5182d_data *data)
{
	int ret;
	u8 mode;

	if (data->power_mode == US5182D_ONESHOT) {
		ret = us5182d_set_opmode(data, US5182D_ALS_ONLY);
		if (ret < 0)
			return ret;
		data->px_enabled = false;
	}

	if (data->als_enabled)
		return 0;

	mode = data->px_enabled ? US5182D_ALS_PX : US5182D_ALS_ONLY;

	ret = us5182d_set_opmode(data, mode);
	if (ret < 0)
		return ret;

	data->als_enabled = true;

	return 0;
}

static int us5182d_px_enable(struct us5182d_data *data)
{
	int ret;
	u8 mode;

	if (data->power_mode == US5182D_ONESHOT) {
		ret = us5182d_set_opmode(data, US5182D_PX_ONLY);
		if (ret < 0)
			return ret;
		data->als_enabled = false;
	}

	if (data->px_enabled)
		return 0;

	mode = data->als_enabled ? US5182D_ALS_PX : US5182D_PX_ONLY;

	ret = us5182d_set_opmode(data, mode);
	if (ret < 0)
		return ret;

	data->px_enabled = true;

	return 0;
}

static int us5182d_get_als(struct us5182d_data *data)
{
	int ret;
	unsigned long result;

	ret = us5182d_als_enable(data);
	if (ret < 0)
		return ret;

	ret = i2c_smbus_read_word_data(data->client,
				       US5182D_REG_ADL);
	if (ret < 0)
		return ret;

	result = ret * data->ga / US5182D_GA_RESOLUTION;
	if (result > 0xffff)
		result = 0xffff;

	return result;
}

static int us5182d_get_px(struct us5182d_data *data)
{
	int ret;

	ret = us5182d_px_enable(data);
	if (ret < 0)
		return ret;

	return i2c_smbus_read_word_data(data->client,
					US5182D_REG_PDL);
}

static int us5182d_shutdown_en(struct us5182d_data *data, u8 state)
{
	int ret;

	if (data->power_mode == US5182D_ONESHOT)
		return 0;

	ret = i2c_smbus_read_byte_data(data->client, US5182D_REG_CFG0);
	if (ret < 0)
		return ret;

	ret = ret & ~US5182D_CFG0_SHUTDOWN_EN;
	ret = ret | state;

	ret = i2c_smbus_write_byte_data(data->client, US5182D_REG_CFG0, ret);
	if (ret < 0)
		return ret;

	if (state & US5182D_CFG0_SHUTDOWN_EN) {
		data->als_enabled = false;
		data->px_enabled = false;
	}

	return ret;
}


static int us5182d_set_power_state(struct us5182d_data *data, bool on)
{
	int ret;

	if (data->power_mode == US5182D_ONESHOT)
		return 0;

	if (on) {
		ret = pm_runtime_get_sync(&data->client->dev);
		if (ret < 0)
			pm_runtime_put_noidle(&data->client->dev);
	} else {
		pm_runtime_mark_last_busy(&data->client->dev);
		ret = pm_runtime_put_autosuspend(&data->client->dev);
	}

	return ret;
}

static int us5182d_read_value(struct us5182d_data *data,
			      struct iio_chan_spec const *chan)
{
	int ret, value;

	mutex_lock(&data->lock);

	if (data->power_mode == US5182D_ONESHOT) {
		ret = us5182d_oneshot_en(data);
		if (ret < 0)
			goto out_err;
	}

	ret = us5182d_set_power_state(data, true);
	if (ret < 0)
		goto out_err;

	if (chan->type == IIO_LIGHT)
		ret = us5182d_get_als(data);
	else
		ret = us5182d_get_px(data);
	if (ret < 0)
		goto out_poweroff;

	value = ret;

	ret = us5182d_set_power_state(data, false);
	if (ret < 0)
		goto out_err;

	mutex_unlock(&data->lock);
	return value;

out_poweroff:
	us5182d_set_power_state(data, false);
out_err:
	mutex_unlock(&data->lock);
	return ret;
}

static int us5182d_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan, int *val,
			    int *val2, long mask)
{
	struct us5182d_data *data = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = us5182d_read_value(data, chan);
		if (ret < 0)
			return ret;
		*val = ret;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		ret = i2c_smbus_read_byte_data(data->client, US5182D_REG_CFG1);
		if (ret < 0)
			return ret;
		*val = 0;
		*val2 = us5182d_scales[ret & US5182D_AGAIN_MASK];
		return IIO_VAL_INT_PLUS_MICRO;
	default:
		return -EINVAL;
	}
}

/**
 * us5182d_update_dark_th - update Darh_Th registers
 * @data	us5182d_data structure
 * @index	index in us5182d_dark_ths array to use for the updated value
 *
 * Function needs to be called with a lock held because it needs two i2c write
 * byte operations as these registers (0x27 0x28) don't work in word mode
 * accessing.
 */
static int us5182d_update_dark_th(struct us5182d_data *data, int index)
{
	__be16 dark_th = cpu_to_be16(data->us5182d_dark_ths[index]);
	int ret;

	ret = i2c_smbus_write_byte_data(data->client, US5182D_REG_UDARK_TH,
					((u8 *)&dark_th)[0]);
	if (ret < 0)
		return ret;

	return i2c_smbus_write_byte_data(data->client, US5182D_REG_UDARK_TH + 1,
					((u8 *)&dark_th)[1]);
}

/**
 * us5182d_apply_scale - update the ALS scale
 * @data	us5182d_data structure
 * @index	index in us5182d_scales array to use for the updated value
 *
 * Function needs to be called with a lock held as we're having more than one
 * i2c operation.
 */
static int us5182d_apply_scale(struct us5182d_data *data, int index)
{
	int ret;

	ret = i2c_smbus_read_byte_data(data->client, US5182D_REG_CFG1);
	if (ret < 0)
		return ret;

	ret = ret & (~US5182D_AGAIN_MASK);
	ret |= index;

	ret = i2c_smbus_write_byte_data(data->client, US5182D_REG_CFG1, ret);
	if (ret < 0)
		return ret;

	return us5182d_update_dark_th(data, index);
}

static int us5182d_write_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan, int val,
			     int val2, long mask)
{
	struct us5182d_data *data = iio_priv(indio_dev);
	int ret, i;

	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		if (val != 0)
			return -EINVAL;
		for (i = 0; i < ARRAY_SIZE(us5182d_scales); i++)
			if (val2 == us5182d_scales[i]) {
				mutex_lock(&data->lock);
				ret = us5182d_apply_scale(data, i);
				mutex_unlock(&data->lock);
				return ret;
			}
		break;
	default:
		return -EINVAL;
	}

	return -EINVAL;
}

static int us5182d_setup_prox(struct iio_dev *indio_dev,
			      enum iio_event_direction dir, u16 val)
{
	struct us5182d_data *data = iio_priv(indio_dev);

	if (dir == IIO_EV_DIR_FALLING)
		return i2c_smbus_write_word_data(data->client,
						 US5182D_REG_PXL_TH, val);
	else if (dir == IIO_EV_DIR_RISING)
		return i2c_smbus_write_word_data(data->client,
						 US5182D_REG_PXH_TH, val);

	return 0;
}

static int us5182d_read_thresh(struct iio_dev *indio_dev,
	const struct iio_chan_spec *chan, enum iio_event_type type,
	enum iio_event_direction dir, enum iio_event_info info, int *val,
	int *val2)
{
	struct us5182d_data *data = iio_priv(indio_dev);

	switch (dir) {
	case IIO_EV_DIR_RISING:
		mutex_lock(&data->lock);
		*val = data->px_high_th;
		mutex_unlock(&data->lock);
		break;
	case IIO_EV_DIR_FALLING:
		mutex_lock(&data->lock);
		*val = data->px_low_th;
		mutex_unlock(&data->lock);
		break;
	default:
		return -EINVAL;
	}

	return IIO_VAL_INT;
}

static int us5182d_write_thresh(struct iio_dev *indio_dev,
	const struct iio_chan_spec *chan, enum iio_event_type type,
	enum iio_event_direction dir, enum iio_event_info info, int val,
	int val2)
{
	struct us5182d_data *data = iio_priv(indio_dev);
	int ret;

	if (val < 0 || val > USHRT_MAX || val2 != 0)
		return -EINVAL;

	switch (dir) {
	case IIO_EV_DIR_RISING:
		mutex_lock(&data->lock);
		if (data->rising_en) {
			ret = us5182d_setup_prox(indio_dev, dir, val);
			if (ret < 0)
				goto err;
		}
		data->px_high_th = val;
		mutex_unlock(&data->lock);
		break;
	case IIO_EV_DIR_FALLING:
		mutex_lock(&data->lock);
		if (data->falling_en) {
			ret = us5182d_setup_prox(indio_dev, dir, val);
			if (ret < 0)
				goto err;
		}
		data->px_low_th = val;
		mutex_unlock(&data->lock);
		break;
	default:
		return -EINVAL;
	}

	return 0;
err:
	mutex_unlock(&data->lock);
	return ret;
}

static int us5182d_read_event_config(struct iio_dev *indio_dev,
	const struct iio_chan_spec *chan, enum iio_event_type type,
	enum iio_event_direction dir)
{
	struct us5182d_data *data = iio_priv(indio_dev);
	int ret;

	switch (dir) {
	case IIO_EV_DIR_RISING:
		mutex_lock(&data->lock);
		ret = data->rising_en;
		mutex_unlock(&data->lock);
		break;
	case IIO_EV_DIR_FALLING:
		mutex_lock(&data->lock);
		ret = data->falling_en;
		mutex_unlock(&data->lock);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int us5182d_write_event_config(struct iio_dev *indio_dev,
	const struct iio_chan_spec *chan, enum iio_event_type type,
	enum iio_event_direction dir, int state)
{
	struct us5182d_data *data = iio_priv(indio_dev);
	int ret;
	u16 new_th;

	mutex_lock(&data->lock);

	switch (dir) {
	case IIO_EV_DIR_RISING:
		if (data->rising_en == state) {
			mutex_unlock(&data->lock);
			return 0;
		}
		new_th = US5182D_PXH_TH_DISABLE;
		if (state) {
			data->power_mode = US5182D_CONTINUOUS;
			ret = us5182d_set_power_state(data, true);
			if (ret < 0)
				goto err;
			ret = us5182d_px_enable(data);
			if (ret < 0)
				goto err_poweroff;
			new_th = data->px_high_th;
		}
		ret = us5182d_setup_prox(indio_dev, dir, new_th);
		if (ret < 0)
			goto err_poweroff;
		data->rising_en = state;
		break;
	case IIO_EV_DIR_FALLING:
		if (data->falling_en == state) {
			mutex_unlock(&data->lock);
			return 0;
		}
		new_th =  US5182D_PXL_TH_DISABLE;
		if (state) {
			data->power_mode = US5182D_CONTINUOUS;
			ret = us5182d_set_power_state(data, true);
			if (ret < 0)
				goto err;
			ret = us5182d_px_enable(data);
			if (ret < 0)
				goto err_poweroff;
			new_th = data->px_low_th;
		}
		ret = us5182d_setup_prox(indio_dev, dir, new_th);
		if (ret < 0)
			goto err_poweroff;
		data->falling_en = state;
		break;
	default:
		ret = -EINVAL;
		goto err;
	}

	if (!state) {
		ret = us5182d_set_power_state(data, false);
		if (ret < 0)
			goto err;
	}

	if (!data->falling_en && !data->rising_en && !data->default_continuous)
		data->power_mode = US5182D_ONESHOT;

	mutex_unlock(&data->lock);
	return 0;

err_poweroff:
	if (state)
		us5182d_set_power_state(data, false);
err:
	mutex_unlock(&data->lock);
	return ret;
}

static const struct iio_info us5182d_info = {
	.driver_module	= THIS_MODULE,
	.read_raw = us5182d_read_raw,
	.write_raw = us5182d_write_raw,
	.attrs = &us5182d_attr_group,
	.read_event_value = &us5182d_read_thresh,
	.write_event_value = &us5182d_write_thresh,
	.read_event_config = &us5182d_read_event_config,
	.write_event_config = &us5182d_write_event_config,
};

static int us5182d_reset(struct iio_dev *indio_dev)
{
	struct us5182d_data *data = iio_priv(indio_dev);

	return i2c_smbus_write_byte_data(data->client, US5182D_REG_CFG3,
					 US5182D_RESET_CHIP);
}

static int us5182d_init(struct iio_dev *indio_dev)
{
	struct us5182d_data *data = iio_priv(indio_dev);
	int i, ret;

	ret = us5182d_reset(indio_dev);
	if (ret < 0)
		return ret;

	data->opmode = 0;
	data->power_mode = US5182D_CONTINUOUS;
	data->px_low_th = US5182D_REG_PXL_TH_DEFAULT;
	data->px_high_th = US5182D_REG_PXH_TH_DEFAULT;

	for (i = 0; i < ARRAY_SIZE(us5182d_regvals); i++) {
		ret = i2c_smbus_write_byte_data(data->client,
						us5182d_regvals[i].reg,
						us5182d_regvals[i].val);
		if (ret < 0)
			return ret;
	}

	data->als_enabled = true;
	data->px_enabled = true;

	if (!data->default_continuous) {
		ret = us5182d_shutdown_en(data, US5182D_CFG0_SHUTDOWN_EN);
		if (ret < 0)
			return ret;
		data->power_mode = US5182D_ONESHOT;
	}

	return ret;
}

static void us5182d_get_platform_data(struct iio_dev *indio_dev)
{
	struct us5182d_data *data = iio_priv(indio_dev);

	if (device_property_read_u32(&data->client->dev, "upisemi,glass-coef",
				     &data->ga))
		data->ga = US5182D_GA_RESOLUTION;
	if (device_property_read_u16_array(&data->client->dev,
					   "upisemi,dark-ths",
					   data->us5182d_dark_ths,
					   ARRAY_SIZE(us5182d_dark_ths_vals)))
		data->us5182d_dark_ths = us5182d_dark_ths_vals;
	if (device_property_read_u8(&data->client->dev,
				    "upisemi,upper-dark-gain",
				    &data->upper_dark_gain))
		data->upper_dark_gain = US5182D_REG_AUTO_HDARK_GAIN_DEFAULT;
	if (device_property_read_u8(&data->client->dev,
				    "upisemi,lower-dark-gain",
				    &data->lower_dark_gain))
		data->lower_dark_gain = US5182D_REG_AUTO_LDARK_GAIN_DEFAULT;
	data->default_continuous = device_property_read_bool(&data->client->dev,
							     "upisemi,continuous");
}

static int  us5182d_dark_gain_config(struct iio_dev *indio_dev)
{
	struct us5182d_data *data = iio_priv(indio_dev);
	int ret;

	ret = us5182d_update_dark_th(data, US5182D_CFG1_AGAIN_DEFAULT);
	if (ret < 0)
		return ret;

	ret = i2c_smbus_write_byte_data(data->client,
					US5182D_REG_AUTO_LDARK_GAIN,
					data->lower_dark_gain);
	if (ret < 0)
		return ret;

	ret = i2c_smbus_write_byte_data(data->client,
					US5182D_REG_AUTO_HDARK_GAIN,
					data->upper_dark_gain);
	if (ret < 0)
		return ret;

	return i2c_smbus_write_byte_data(data->client, US5182D_REG_DARK_AUTO_EN,
					 US5182D_REG_DARK_AUTO_EN_DEFAULT);
}

static irqreturn_t us5182d_irq_thread_handler(int irq, void *private)
{
	struct iio_dev *indio_dev = private;
	struct us5182d_data *data = iio_priv(indio_dev);
	enum iio_event_direction dir;
	int ret;
	u64 ev;

	ret = i2c_smbus_read_byte_data(data->client, US5182D_REG_CFG0);
	if (ret < 0) {
		dev_err(&data->client->dev, "i2c transfer error in irq\n");
		return IRQ_HANDLED;
	}

	dir = ret & US5182D_CFG0_PROX ? IIO_EV_DIR_RISING : IIO_EV_DIR_FALLING;
	ev = IIO_UNMOD_EVENT_CODE(IIO_PROXIMITY, 1, IIO_EV_TYPE_THRESH, dir);

	iio_push_event(indio_dev, ev, iio_get_time_ns());

	ret = i2c_smbus_write_byte_data(data->client, US5182D_REG_CFG0,
					ret & ~US5182D_CFG0_PX_IRQ);
	if (ret < 0)
		dev_err(&data->client->dev, "i2c transfer error in irq\n");

	return IRQ_HANDLED;
}

static int us5182d_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct us5182d_data *data;
	struct iio_dev *indio_dev;
	int ret;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	i2c_set_clientdata(client, indio_dev);
	data->client = client;

	mutex_init(&data->lock);

	indio_dev->dev.parent = &client->dev;
	indio_dev->info = &us5182d_info;
	indio_dev->name = US5182D_DRV_NAME;
	indio_dev->channels = us5182d_channels;
	indio_dev->num_channels = ARRAY_SIZE(us5182d_channels);
	indio_dev->modes = INDIO_DIRECT_MODE;

	ret = i2c_smbus_read_byte_data(data->client, US5182D_REG_CHIPID);
	if (ret != US5182D_CHIPID) {
		dev_err(&data->client->dev,
			"Failed to detect US5182 light chip\n");
		return (ret < 0) ? ret : -ENODEV;
	}

	if (client->irq > 0) {
		ret = devm_request_threaded_irq(&client->dev, client->irq, NULL,
						us5182d_irq_thread_handler,
						IRQF_TRIGGER_LOW | IRQF_ONESHOT,
						"us5182d-irq", indio_dev);
		if (ret < 0)
			return ret;
	} else
		dev_warn(&client->dev, "no valid irq found\n");

	us5182d_get_platform_data(indio_dev);
	ret = us5182d_init(indio_dev);
	if (ret < 0)
		return ret;

	ret = us5182d_dark_gain_config(indio_dev);
	if (ret < 0)
		goto out_err;

	if (data->default_continuous) {
		pm_runtime_set_active(&client->dev);
		if (ret < 0)
			goto out_err;
	}

	pm_runtime_enable(&client->dev);
	pm_runtime_set_autosuspend_delay(&client->dev,
					 US5182D_SLEEP_MS);
	pm_runtime_use_autosuspend(&client->dev);

	ret = iio_device_register(indio_dev);
	if (ret < 0)
		goto out_err;

	return 0;

out_err:
	us5182d_shutdown_en(data, US5182D_CFG0_SHUTDOWN_EN);
	return ret;

}

static int us5182d_remove(struct i2c_client *client)
{
	struct us5182d_data *data = iio_priv(i2c_get_clientdata(client));

	iio_device_unregister(i2c_get_clientdata(client));

	pm_runtime_disable(&client->dev);
	pm_runtime_set_suspended(&client->dev);

	return us5182d_shutdown_en(data, US5182D_CFG0_SHUTDOWN_EN);
}

#if defined(CONFIG_PM_SLEEP) || defined(CONFIG_PM)
static int us5182d_suspend(struct device *dev)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(to_i2c_client(dev));
	struct us5182d_data *data = iio_priv(indio_dev);

	if (data->power_mode == US5182D_CONTINUOUS)
		return us5182d_shutdown_en(data, US5182D_CFG0_SHUTDOWN_EN);

	return 0;
}

static int us5182d_resume(struct device *dev)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(to_i2c_client(dev));
	struct us5182d_data *data = iio_priv(indio_dev);

	if (data->power_mode == US5182D_CONTINUOUS)
		return us5182d_shutdown_en(data,
					   ~US5182D_CFG0_SHUTDOWN_EN & 0xff);

	return 0;
}
#endif

static const struct dev_pm_ops us5182d_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(us5182d_suspend, us5182d_resume)
	SET_RUNTIME_PM_OPS(us5182d_suspend, us5182d_resume, NULL)
};

static const struct acpi_device_id us5182d_acpi_match[] = {
	{ "USD5182", 0},
	{}
};

MODULE_DEVICE_TABLE(acpi, us5182d_acpi_match);

static const struct i2c_device_id us5182d_id[] = {
		{"usd5182", 0},
		{}
};

MODULE_DEVICE_TABLE(i2c, us5182d_id);

static struct i2c_driver us5182d_driver = {
	.driver = {
		.name = US5182D_DRV_NAME,
		.pm = &us5182d_pm_ops,
		.acpi_match_table = ACPI_PTR(us5182d_acpi_match),
	},
	.probe = us5182d_probe,
	.remove = us5182d_remove,
	.id_table = us5182d_id,

};
module_i2c_driver(us5182d_driver);

MODULE_AUTHOR("Adriana Reus <adriana.reus@intel.com>");
MODULE_DESCRIPTION("Driver for us5182d Proximity and Light Sensor");
MODULE_LICENSE("GPL v2");
