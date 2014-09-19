/*
 * KXCJK-1013 3-axis accelerometer driver
 * Copyright (c) 2014, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/bitops.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/acpi.h>
#include <linux/gpio/consumer.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/buffer.h>
#include <linux/iio/trigger.h>
#include <linux/iio/events.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/iio/accel/kxcjk_1013.h>

#define KXCJK1013_DRV_NAME "kxcjk1013"
#define KXCJK1013_IRQ_NAME "kxcjk1013_event"

#define KXCJK1013_REG_XOUT_L		0x06
/*
 * From low byte X axis register, all the other addresses of Y and Z can be
 * obtained by just applying axis offset. The following axis defines are just
 * provide clarity, but not used.
 */
#define KXCJK1013_REG_XOUT_H		0x07
#define KXCJK1013_REG_YOUT_L		0x08
#define KXCJK1013_REG_YOUT_H		0x09
#define KXCJK1013_REG_ZOUT_L		0x0A
#define KXCJK1013_REG_ZOUT_H		0x0B

#define KXCJK1013_REG_DCST_RESP		0x0C
#define KXCJK1013_REG_WHO_AM_I		0x0F
#define KXCJK1013_REG_INT_SRC1		0x16
#define KXCJK1013_REG_INT_SRC2		0x17
#define KXCJK1013_REG_STATUS_REG	0x18
#define KXCJK1013_REG_INT_REL		0x1A
#define KXCJK1013_REG_CTRL1		0x1B
#define KXCJK1013_REG_CTRL2		0x1D
#define KXCJK1013_REG_INT_CTRL1		0x1E
#define KXCJK1013_REG_INT_CTRL2		0x1F
#define KXCJK1013_REG_DATA_CTRL		0x21
#define KXCJK1013_REG_WAKE_TIMER	0x29
#define KXCJK1013_REG_SELF_TEST		0x3A
#define KXCJK1013_REG_WAKE_THRES	0x6A

#define KXCJK1013_REG_CTRL1_BIT_PC1	BIT(7)
#define KXCJK1013_REG_CTRL1_BIT_RES	BIT(6)
#define KXCJK1013_REG_CTRL1_BIT_DRDY	BIT(5)
#define KXCJK1013_REG_CTRL1_BIT_GSEL1	BIT(4)
#define KXCJK1013_REG_CTRL1_BIT_GSEL0	BIT(3)
#define KXCJK1013_REG_CTRL1_BIT_WUFE	BIT(1)
#define KXCJK1013_REG_INT_REG1_BIT_IEA	BIT(4)
#define KXCJK1013_REG_INT_REG1_BIT_IEN	BIT(5)

#define KXCJK1013_DATA_MASK_12_BIT	0x0FFF
#define KXCJK1013_MAX_STARTUP_TIME_US	100000

#define KXCJK1013_SLEEP_DELAY_MS	2000

#define KXCJK1013_REG_INT_SRC2_BIT_ZP	BIT(0)
#define KXCJK1013_REG_INT_SRC2_BIT_ZN	BIT(1)
#define KXCJK1013_REG_INT_SRC2_BIT_YP	BIT(2)
#define KXCJK1013_REG_INT_SRC2_BIT_YN	BIT(3)
#define KXCJK1013_REG_INT_SRC2_BIT_XP	BIT(4)
#define KXCJK1013_REG_INT_SRC2_BIT_XN	BIT(5)

#define KXCJK1013_DEFAULT_WAKE_THRES	1

enum kx_chipset {
	KXCJK1013,
	KXCJ91008,
	KXTJ21009,
	KX_MAX_CHIPS /* this must be last */
};

struct kxcjk1013_data {
	struct i2c_client *client;
	struct iio_trigger *dready_trig;
	struct iio_trigger *motion_trig;
	struct mutex mutex;
	s16 buffer[8];
	u8 odr_bits;
	u8 range;
	int wake_thres;
	int wake_dur;
	bool active_high_intr;
	bool dready_trigger_on;
	int ev_enable_state;
	bool motion_trigger_on;
	int64_t timestamp;
	enum kx_chipset chipset;
};

enum kxcjk1013_axis {
	AXIS_X,
	AXIS_Y,
	AXIS_Z,
};

enum kxcjk1013_mode {
	STANDBY,
	OPERATION,
};

enum kxcjk1013_range {
	KXCJK1013_RANGE_2G,
	KXCJK1013_RANGE_4G,
	KXCJK1013_RANGE_8G,
};

static const struct {
	int val;
	int val2;
	int odr_bits;
} samp_freq_table[] = { {0, 781000, 0x08}, {1, 563000, 0x09},
			{3, 125000, 0x0A}, {6, 250000, 0x0B}, {12, 500000, 0},
			{25, 0, 0x01}, {50, 0, 0x02}, {100, 0, 0x03},
			{200, 0, 0x04}, {400, 0, 0x05}, {800, 0, 0x06},
			{1600, 0, 0x07} };

/* Refer to section 4 of the specification */
static const struct {
	int odr_bits;
	int usec;
} odr_start_up_times[KX_MAX_CHIPS][12] = {
	/* KXCJK-1013 */
	{
		{0x08, 100000},
		{0x09, 100000},
		{0x0A, 100000},
		{0x0B, 100000},
		{0, 80000},
		{0x01, 41000},
		{0x02, 21000},
		{0x03, 11000},
		{0x04, 6400},
		{0x05, 3900},
		{0x06, 2700},
		{0x07, 2100},
	},
	/* KXCJ9-1008 */
	{
		{0x08, 100000},
		{0x09, 100000},
		{0x0A, 100000},
		{0x0B, 100000},
		{0, 80000},
		{0x01, 41000},
		{0x02, 21000},
		{0x03, 11000},
		{0x04, 6400},
		{0x05, 3900},
		{0x06, 2700},
		{0x07, 2100},
	},
	/* KXCTJ2-1009 */
	{
		{0x08, 1240000},
		{0x09, 621000},
		{0x0A, 309000},
		{0x0B, 151000},
		{0, 80000},
		{0x01, 41000},
		{0x02, 21000},
		{0x03, 11000},
		{0x04, 6000},
		{0x05, 4000},
		{0x06, 3000},
		{0x07, 2000},
	},
};

static const struct {
	u16 scale;
	u8 gsel_0;
	u8 gsel_1;
} KXCJK1013_scale_table[] = { {9582, 0, 0},
			      {19163, 1, 0},
			      {38326, 0, 1} };

static const struct {
	int val;
	int val2;
	int odr_bits;
} wake_odr_data_rate_table[] = { {0, 781000, 0x00},
				 {1, 563000, 0x01},
				 {3, 125000, 0x02},
				 {6, 250000, 0x03},
				 {12, 500000, 0x04},
				 {25, 0, 0x05},
				 {50, 0, 0x06},
				 {100, 0, 0x06},
				 {200, 0, 0x06},
				 {400, 0, 0x06},
				 {800, 0, 0x06},
				 {1600, 0, 0x06} };

static int kxcjk1013_set_mode(struct kxcjk1013_data *data,
			      enum kxcjk1013_mode mode)
{
	int ret;

	ret = i2c_smbus_read_byte_data(data->client, KXCJK1013_REG_CTRL1);
	if (ret < 0) {
		dev_err(&data->client->dev, "Error reading reg_ctrl1\n");
		return ret;
	}

	if (mode == STANDBY)
		ret &= ~KXCJK1013_REG_CTRL1_BIT_PC1;
	else
		ret |= KXCJK1013_REG_CTRL1_BIT_PC1;

	ret = i2c_smbus_write_byte_data(data->client,
					KXCJK1013_REG_CTRL1, ret);
	if (ret < 0) {
		dev_err(&data->client->dev, "Error writing reg_ctrl1\n");
		return ret;
	}

	return 0;
}

static int kxcjk1013_get_mode(struct kxcjk1013_data *data,
			      enum kxcjk1013_mode *mode)
{
	int ret;

	ret = i2c_smbus_read_byte_data(data->client, KXCJK1013_REG_CTRL1);
	if (ret < 0) {
		dev_err(&data->client->dev, "Error reading reg_ctrl1\n");
		return ret;
	}

	if (ret & KXCJK1013_REG_CTRL1_BIT_PC1)
		*mode = OPERATION;
	else
		*mode = STANDBY;

	return 0;
}

static int kxcjk1013_set_range(struct kxcjk1013_data *data, int range_index)
{
	int ret;

	ret = i2c_smbus_read_byte_data(data->client, KXCJK1013_REG_CTRL1);
	if (ret < 0) {
		dev_err(&data->client->dev, "Error reading reg_ctrl1\n");
		return ret;
	}

	ret |= (KXCJK1013_scale_table[range_index].gsel_0 << 3);
	ret |= (KXCJK1013_scale_table[range_index].gsel_1 << 4);

	ret = i2c_smbus_write_byte_data(data->client,
					KXCJK1013_REG_CTRL1,
					ret);
	if (ret < 0) {
		dev_err(&data->client->dev, "Error writing reg_ctrl1\n");
		return ret;
	}

	data->range = range_index;

	return 0;
}

static int kxcjk1013_chip_init(struct kxcjk1013_data *data)
{
	int ret;

	ret = i2c_smbus_read_byte_data(data->client, KXCJK1013_REG_WHO_AM_I);
	if (ret < 0) {
		dev_err(&data->client->dev, "Error reading who_am_i\n");
		return ret;
	}

	dev_dbg(&data->client->dev, "KXCJK1013 Chip Id %x\n", ret);

	ret = kxcjk1013_set_mode(data, STANDBY);
	if (ret < 0)
		return ret;

	ret = i2c_smbus_read_byte_data(data->client, KXCJK1013_REG_CTRL1);
	if (ret < 0) {
		dev_err(&data->client->dev, "Error reading reg_ctrl1\n");
		return ret;
	}

	/* Set 12 bit mode */
	ret |= KXCJK1013_REG_CTRL1_BIT_RES;

	ret = i2c_smbus_write_byte_data(data->client, KXCJK1013_REG_CTRL1,
					ret);
	if (ret < 0) {
		dev_err(&data->client->dev, "Error reading reg_ctrl\n");
		return ret;
	}

	/* Setting range to 4G */
	ret = kxcjk1013_set_range(data, KXCJK1013_RANGE_4G);
	if (ret < 0)
		return ret;

	ret = i2c_smbus_read_byte_data(data->client, KXCJK1013_REG_DATA_CTRL);
	if (ret < 0) {
		dev_err(&data->client->dev, "Error reading reg_data_ctrl\n");
		return ret;
	}

	data->odr_bits = ret;

	/* Set up INT polarity */
	ret = i2c_smbus_read_byte_data(data->client, KXCJK1013_REG_INT_CTRL1);
	if (ret < 0) {
		dev_err(&data->client->dev, "Error reading reg_int_ctrl1\n");
		return ret;
	}

	if (data->active_high_intr)
		ret |= KXCJK1013_REG_INT_REG1_BIT_IEA;
	else
		ret &= ~KXCJK1013_REG_INT_REG1_BIT_IEA;

	ret = i2c_smbus_write_byte_data(data->client, KXCJK1013_REG_INT_CTRL1,
					ret);
	if (ret < 0) {
		dev_err(&data->client->dev, "Error writing reg_int_ctrl1\n");
		return ret;
	}

	ret = kxcjk1013_set_mode(data, OPERATION);
	if (ret < 0)
		return ret;

	data->wake_thres = KXCJK1013_DEFAULT_WAKE_THRES;

	return 0;
}

#ifdef CONFIG_PM_RUNTIME
static int kxcjk1013_get_startup_times(struct kxcjk1013_data *data)
{
	int i;
	int idx = data->chipset;

	for (i = 0; i < ARRAY_SIZE(odr_start_up_times[idx]); ++i) {
		if (odr_start_up_times[idx][i].odr_bits == data->odr_bits)
			return odr_start_up_times[idx][i].usec;
	}

	return KXCJK1013_MAX_STARTUP_TIME_US;
}
#endif

static int kxcjk1013_set_power_state(struct kxcjk1013_data *data, bool on)
{
	int ret;

	if (on)
		ret = pm_runtime_get_sync(&data->client->dev);
	else {
		pm_runtime_mark_last_busy(&data->client->dev);
		ret = pm_runtime_put_autosuspend(&data->client->dev);
	}
	if (ret < 0) {
		dev_err(&data->client->dev,
			"Failed: kxcjk1013_set_power_state for %d\n", on);
		return ret;
	}

	return 0;
}

static int kxcjk1013_chip_update_thresholds(struct kxcjk1013_data *data)
{
	int ret;

	ret = i2c_smbus_write_byte_data(data->client,
					KXCJK1013_REG_WAKE_TIMER,
					data->wake_dur);
	if (ret < 0) {
		dev_err(&data->client->dev,
			"Error writing reg_wake_timer\n");
		return ret;
	}

	ret = i2c_smbus_write_byte_data(data->client,
					KXCJK1013_REG_WAKE_THRES,
					data->wake_thres);
	if (ret < 0) {
		dev_err(&data->client->dev, "Error writing reg_wake_thres\n");
		return ret;
	}

	return 0;
}

static int kxcjk1013_setup_any_motion_interrupt(struct kxcjk1013_data *data,
						bool status)
{
	int ret;
	enum kxcjk1013_mode store_mode;

	ret = kxcjk1013_get_mode(data, &store_mode);
	if (ret < 0)
		return ret;

	/* This is requirement by spec to change state to STANDBY */
	ret = kxcjk1013_set_mode(data, STANDBY);
	if (ret < 0)
		return ret;

	ret = kxcjk1013_chip_update_thresholds(data);
	if (ret < 0)
		return ret;

	ret = i2c_smbus_read_byte_data(data->client, KXCJK1013_REG_INT_CTRL1);
	if (ret < 0) {
		dev_err(&data->client->dev, "Error reading reg_int_ctrl1\n");
		return ret;
	}

	if (status)
		ret |= KXCJK1013_REG_INT_REG1_BIT_IEN;
	else
		ret &= ~KXCJK1013_REG_INT_REG1_BIT_IEN;

	ret = i2c_smbus_write_byte_data(data->client, KXCJK1013_REG_INT_CTRL1,
					ret);
	if (ret < 0) {
		dev_err(&data->client->dev, "Error writing reg_int_ctrl1\n");
		return ret;
	}

	ret = i2c_smbus_read_byte_data(data->client, KXCJK1013_REG_CTRL1);
	if (ret < 0) {
		dev_err(&data->client->dev, "Error reading reg_ctrl1\n");
		return ret;
	}

	if (status)
		ret |= KXCJK1013_REG_CTRL1_BIT_WUFE;
	else
		ret &= ~KXCJK1013_REG_CTRL1_BIT_WUFE;

	ret = i2c_smbus_write_byte_data(data->client,
					KXCJK1013_REG_CTRL1, ret);
	if (ret < 0) {
		dev_err(&data->client->dev, "Error writing reg_ctrl1\n");
		return ret;
	}

	if (store_mode == OPERATION) {
		ret = kxcjk1013_set_mode(data, OPERATION);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int kxcjk1013_setup_new_data_interrupt(struct kxcjk1013_data *data,
					      bool status)
{
	int ret;
	enum kxcjk1013_mode store_mode;

	ret = kxcjk1013_get_mode(data, &store_mode);
	if (ret < 0)
		return ret;

	/* This is requirement by spec to change state to STANDBY */
	ret = kxcjk1013_set_mode(data, STANDBY);
	if (ret < 0)
		return ret;

	ret = i2c_smbus_read_byte_data(data->client, KXCJK1013_REG_INT_CTRL1);
	if (ret < 0) {
		dev_err(&data->client->dev, "Error reading reg_int_ctrl1\n");
		return ret;
	}

	if (status)
		ret |= KXCJK1013_REG_INT_REG1_BIT_IEN;
	else
		ret &= ~KXCJK1013_REG_INT_REG1_BIT_IEN;

	ret = i2c_smbus_write_byte_data(data->client, KXCJK1013_REG_INT_CTRL1,
					ret);
	if (ret < 0) {
		dev_err(&data->client->dev, "Error writing reg_int_ctrl1\n");
		return ret;
	}

	ret = i2c_smbus_read_byte_data(data->client, KXCJK1013_REG_CTRL1);
	if (ret < 0) {
		dev_err(&data->client->dev, "Error reading reg_ctrl1\n");
		return ret;
	}

	if (status)
		ret |= KXCJK1013_REG_CTRL1_BIT_DRDY;
	else
		ret &= ~KXCJK1013_REG_CTRL1_BIT_DRDY;

	ret = i2c_smbus_write_byte_data(data->client,
					KXCJK1013_REG_CTRL1, ret);
	if (ret < 0) {
		dev_err(&data->client->dev, "Error writing reg_ctrl1\n");
		return ret;
	}

	if (store_mode == OPERATION) {
		ret = kxcjk1013_set_mode(data, OPERATION);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int kxcjk1013_convert_freq_to_bit(int val, int val2)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(samp_freq_table); ++i) {
		if (samp_freq_table[i].val == val &&
			samp_freq_table[i].val2 == val2) {
			return samp_freq_table[i].odr_bits;
		}
	}

	return -EINVAL;
}

static int kxcjk1013_convert_wake_odr_to_bit(int val, int val2)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(wake_odr_data_rate_table); ++i) {
		if (wake_odr_data_rate_table[i].val == val &&
			wake_odr_data_rate_table[i].val2 == val2) {
			return wake_odr_data_rate_table[i].odr_bits;
		}
	}

	return -EINVAL;
}

static int kxcjk1013_set_odr(struct kxcjk1013_data *data, int val, int val2)
{
	int ret;
	int odr_bits;
	enum kxcjk1013_mode store_mode;

	ret = kxcjk1013_get_mode(data, &store_mode);
	if (ret < 0)
		return ret;

	odr_bits = kxcjk1013_convert_freq_to_bit(val, val2);
	if (odr_bits < 0)
		return odr_bits;

	/* To change ODR, the chip must be set to STANDBY as per spec */
	ret = kxcjk1013_set_mode(data, STANDBY);
	if (ret < 0)
		return ret;

	ret = i2c_smbus_write_byte_data(data->client, KXCJK1013_REG_DATA_CTRL,
					odr_bits);
	if (ret < 0) {
		dev_err(&data->client->dev, "Error writing data_ctrl\n");
		return ret;
	}

	data->odr_bits = odr_bits;

	odr_bits = kxcjk1013_convert_wake_odr_to_bit(val, val2);
	if (odr_bits < 0)
		return odr_bits;

	ret = i2c_smbus_write_byte_data(data->client, KXCJK1013_REG_CTRL2,
					odr_bits);
	if (ret < 0) {
		dev_err(&data->client->dev, "Error writing reg_ctrl2\n");
		return ret;
	}

	if (store_mode == OPERATION) {
		ret = kxcjk1013_set_mode(data, OPERATION);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int kxcjk1013_get_odr(struct kxcjk1013_data *data, int *val, int *val2)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(samp_freq_table); ++i) {
		if (samp_freq_table[i].odr_bits == data->odr_bits) {
			*val = samp_freq_table[i].val;
			*val2 = samp_freq_table[i].val2;
			return IIO_VAL_INT_PLUS_MICRO;
		}
	}

	return -EINVAL;
}

static int kxcjk1013_get_acc_reg(struct kxcjk1013_data *data, int axis)
{
	u8 reg = KXCJK1013_REG_XOUT_L + axis * 2;
	int ret;

	ret = i2c_smbus_read_word_data(data->client, reg);
	if (ret < 0) {
		dev_err(&data->client->dev,
			"failed to read accel_%c registers\n", 'x' + axis);
		return ret;
	}

	return ret;
}

static int kxcjk1013_set_scale(struct kxcjk1013_data *data, int val)
{
	int ret, i;
	enum kxcjk1013_mode store_mode;


	for (i = 0; i < ARRAY_SIZE(KXCJK1013_scale_table); ++i) {
		if (KXCJK1013_scale_table[i].scale == val) {

			ret = kxcjk1013_get_mode(data, &store_mode);
			if (ret < 0)
				return ret;

			ret = kxcjk1013_set_mode(data, STANDBY);
			if (ret < 0)
				return ret;

			ret = kxcjk1013_set_range(data, i);
			if (ret < 0)
				return ret;

			if (store_mode == OPERATION) {
				ret = kxcjk1013_set_mode(data, OPERATION);
				if (ret)
					return ret;
			}

			return 0;
		}
	}

	return -EINVAL;
}

static int kxcjk1013_read_raw(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *chan, int *val,
			      int *val2, long mask)
{
	struct kxcjk1013_data *data = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		mutex_lock(&data->mutex);
		if (iio_buffer_enabled(indio_dev))
			ret = -EBUSY;
		else {
			ret = kxcjk1013_set_power_state(data, true);
			if (ret < 0) {
				mutex_unlock(&data->mutex);
				return ret;
			}
			ret = kxcjk1013_get_acc_reg(data, chan->scan_index);
			if (ret < 0) {
				kxcjk1013_set_power_state(data, false);
				mutex_unlock(&data->mutex);
				return ret;
			}
			*val = sign_extend32(ret >> 4, 11);
			ret = kxcjk1013_set_power_state(data, false);
		}
		mutex_unlock(&data->mutex);

		if (ret < 0)
			return ret;

		return IIO_VAL_INT;

	case IIO_CHAN_INFO_SCALE:
		*val = 0;
		*val2 = KXCJK1013_scale_table[data->range].scale;
		return IIO_VAL_INT_PLUS_MICRO;

	case IIO_CHAN_INFO_SAMP_FREQ:
		mutex_lock(&data->mutex);
		ret = kxcjk1013_get_odr(data, val, val2);
		mutex_unlock(&data->mutex);
		return ret;

	default:
		return -EINVAL;
	}
}

static int kxcjk1013_write_raw(struct iio_dev *indio_dev,
			       struct iio_chan_spec const *chan, int val,
			       int val2, long mask)
{
	struct kxcjk1013_data *data = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		mutex_lock(&data->mutex);
		ret = kxcjk1013_set_odr(data, val, val2);
		mutex_unlock(&data->mutex);
		break;
	case IIO_CHAN_INFO_SCALE:
		if (val)
			return -EINVAL;

		mutex_lock(&data->mutex);
		ret = kxcjk1013_set_scale(data, val2);
		mutex_unlock(&data->mutex);
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static int kxcjk1013_read_event(struct iio_dev *indio_dev,
				   const struct iio_chan_spec *chan,
				   enum iio_event_type type,
				   enum iio_event_direction dir,
				   enum iio_event_info info,
				   int *val, int *val2)
{
	struct kxcjk1013_data *data = iio_priv(indio_dev);

	*val2 = 0;
	switch (info) {
	case IIO_EV_INFO_VALUE:
		*val = data->wake_thres;
		break;
	case IIO_EV_INFO_PERIOD:
		*val = data->wake_dur;
		break;
	default:
		return -EINVAL;
	}

	return IIO_VAL_INT;
}

static int kxcjk1013_write_event(struct iio_dev *indio_dev,
				    const struct iio_chan_spec *chan,
				    enum iio_event_type type,
				    enum iio_event_direction dir,
				    enum iio_event_info info,
				    int val, int val2)
{
	struct kxcjk1013_data *data = iio_priv(indio_dev);

	if (data->ev_enable_state)
		return -EBUSY;

	switch (info) {
	case IIO_EV_INFO_VALUE:
		data->wake_thres = val;
		break;
	case IIO_EV_INFO_PERIOD:
		data->wake_dur = val;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int kxcjk1013_read_event_config(struct iio_dev *indio_dev,
					  const struct iio_chan_spec *chan,
					  enum iio_event_type type,
					  enum iio_event_direction dir)
{

	struct kxcjk1013_data *data = iio_priv(indio_dev);

	return data->ev_enable_state;
}

static int kxcjk1013_write_event_config(struct iio_dev *indio_dev,
					   const struct iio_chan_spec *chan,
					   enum iio_event_type type,
					   enum iio_event_direction dir,
					   int state)
{
	struct kxcjk1013_data *data = iio_priv(indio_dev);
	int ret;

	if (state && data->ev_enable_state)
		return 0;

	mutex_lock(&data->mutex);

	if (!state && data->motion_trigger_on) {
		data->ev_enable_state = 0;
		mutex_unlock(&data->mutex);
		return 0;
	}

	/*
	 * We will expect the enable and disable to do operation in
	 * in reverse order. This will happen here anyway as our
	 * resume operation uses sync mode runtime pm calls, the
	 * suspend operation will be delayed by autosuspend delay
	 * So the disable operation will still happen in reverse of
	 * enable operation. When runtime pm is disabled the mode
	 * is always on so sequence doesn't matter
	 */
	ret = kxcjk1013_set_power_state(data, state);
	if (ret < 0) {
		mutex_unlock(&data->mutex);
		return ret;
	}

	ret =  kxcjk1013_setup_any_motion_interrupt(data, state);
	if (ret < 0) {
		mutex_unlock(&data->mutex);
		return ret;
	}

	data->ev_enable_state = state;
	mutex_unlock(&data->mutex);

	return 0;
}

static int kxcjk1013_validate_trigger(struct iio_dev *indio_dev,
				      struct iio_trigger *trig)
{
	struct kxcjk1013_data *data = iio_priv(indio_dev);

	if (data->dready_trig != trig && data->motion_trig != trig)
		return -EINVAL;

	return 0;
}

static IIO_CONST_ATTR_SAMP_FREQ_AVAIL(
	"0.781000 1.563000 3.125000 6.250000 12.500000 25 50 100 200 400 800 1600");

static IIO_CONST_ATTR(in_accel_scale_available, "0.009582 0.019163 0.038326");

static struct attribute *kxcjk1013_attributes[] = {
	&iio_const_attr_sampling_frequency_available.dev_attr.attr,
	&iio_const_attr_in_accel_scale_available.dev_attr.attr,
	NULL,
};

static const struct attribute_group kxcjk1013_attrs_group = {
	.attrs = kxcjk1013_attributes,
};

static const struct iio_event_spec kxcjk1013_event = {
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_RISING | IIO_EV_DIR_FALLING,
		.mask_separate = BIT(IIO_EV_INFO_VALUE) |
				 BIT(IIO_EV_INFO_ENABLE) |
				 BIT(IIO_EV_INFO_PERIOD)
};

#define KXCJK1013_CHANNEL(_axis) {					\
	.type = IIO_ACCEL,						\
	.modified = 1,							\
	.channel2 = IIO_MOD_##_axis,					\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),			\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE) |		\
				BIT(IIO_CHAN_INFO_SAMP_FREQ),		\
	.scan_index = AXIS_##_axis,					\
	.scan_type = {							\
		.sign = 's',						\
		.realbits = 12,						\
		.storagebits = 16,					\
		.shift = 4,						\
		.endianness = IIO_CPU,					\
	},								\
	.event_spec = &kxcjk1013_event,				\
	.num_event_specs = 1						\
}

static const struct iio_chan_spec kxcjk1013_channels[] = {
	KXCJK1013_CHANNEL(X),
	KXCJK1013_CHANNEL(Y),
	KXCJK1013_CHANNEL(Z),
	IIO_CHAN_SOFT_TIMESTAMP(3),
};

static const struct iio_info kxcjk1013_info = {
	.attrs			= &kxcjk1013_attrs_group,
	.read_raw		= kxcjk1013_read_raw,
	.write_raw		= kxcjk1013_write_raw,
	.read_event_value	= kxcjk1013_read_event,
	.write_event_value	= kxcjk1013_write_event,
	.write_event_config	= kxcjk1013_write_event_config,
	.read_event_config	= kxcjk1013_read_event_config,
	.validate_trigger	= kxcjk1013_validate_trigger,
	.driver_module		= THIS_MODULE,
};

static irqreturn_t kxcjk1013_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct kxcjk1013_data *data = iio_priv(indio_dev);
	int bit, ret, i = 0;

	mutex_lock(&data->mutex);

	for_each_set_bit(bit, indio_dev->buffer->scan_mask,
			 indio_dev->masklength) {
		ret = kxcjk1013_get_acc_reg(data, bit);
		if (ret < 0) {
			mutex_unlock(&data->mutex);
			goto err;
		}
		data->buffer[i++] = ret;
	}
	mutex_unlock(&data->mutex);

	iio_push_to_buffers_with_timestamp(indio_dev, data->buffer,
					   data->timestamp);
err:
	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

static int kxcjk1013_trig_try_reen(struct iio_trigger *trig)
{
	struct iio_dev *indio_dev = iio_trigger_get_drvdata(trig);
	struct kxcjk1013_data *data = iio_priv(indio_dev);
	int ret;

	ret = i2c_smbus_read_byte_data(data->client, KXCJK1013_REG_INT_REL);
	if (ret < 0) {
		dev_err(&data->client->dev, "Error reading reg_int_rel\n");
		return ret;
	}

	return 0;
}

static int kxcjk1013_data_rdy_trigger_set_state(struct iio_trigger *trig,
						bool state)
{
	struct iio_dev *indio_dev = iio_trigger_get_drvdata(trig);
	struct kxcjk1013_data *data = iio_priv(indio_dev);
	int ret;

	mutex_lock(&data->mutex);

	if (!state && data->ev_enable_state && data->motion_trigger_on) {
		data->motion_trigger_on = false;
		mutex_unlock(&data->mutex);
		return 0;
	}

	ret = kxcjk1013_set_power_state(data, state);
	if (ret < 0) {
		mutex_unlock(&data->mutex);
		return ret;
	}
	if (data->motion_trig == trig)
		ret = kxcjk1013_setup_any_motion_interrupt(data, state);
	else
		ret = kxcjk1013_setup_new_data_interrupt(data, state);
	if (ret < 0) {
		mutex_unlock(&data->mutex);
		return ret;
	}
	if (data->motion_trig == trig)
		data->motion_trigger_on = state;
	else
		data->dready_trigger_on = state;

	mutex_unlock(&data->mutex);

	return 0;
}

static const struct iio_trigger_ops kxcjk1013_trigger_ops = {
	.set_trigger_state = kxcjk1013_data_rdy_trigger_set_state,
	.try_reenable = kxcjk1013_trig_try_reen,
	.owner = THIS_MODULE,
};

static irqreturn_t kxcjk1013_event_handler(int irq, void *private)
{
	struct iio_dev *indio_dev = private;
	struct kxcjk1013_data *data = iio_priv(indio_dev);
	int ret;

	ret = i2c_smbus_read_byte_data(data->client, KXCJK1013_REG_INT_SRC1);
	if (ret < 0) {
		dev_err(&data->client->dev, "Error reading reg_int_src1\n");
		goto ack_intr;
	}

	if (ret & 0x02) {
		ret = i2c_smbus_read_byte_data(data->client,
					       KXCJK1013_REG_INT_SRC2);
		if (ret < 0) {
			dev_err(&data->client->dev,
				"Error reading reg_int_src2\n");
			goto ack_intr;
		}

		if (ret & KXCJK1013_REG_INT_SRC2_BIT_XN)
			iio_push_event(indio_dev,
				       IIO_MOD_EVENT_CODE(IIO_ACCEL,
				       0,
				       IIO_MOD_X,
				       IIO_EV_TYPE_THRESH,
				       IIO_EV_DIR_FALLING),
				       data->timestamp);
		if (ret & KXCJK1013_REG_INT_SRC2_BIT_XP)
			iio_push_event(indio_dev,
				       IIO_MOD_EVENT_CODE(IIO_ACCEL,
				       0,
				       IIO_MOD_X,
				       IIO_EV_TYPE_THRESH,
				       IIO_EV_DIR_RISING),
				       data->timestamp);


		if (ret & KXCJK1013_REG_INT_SRC2_BIT_YN)
			iio_push_event(indio_dev,
				       IIO_MOD_EVENT_CODE(IIO_ACCEL,
				       0,
				       IIO_MOD_Y,
				       IIO_EV_TYPE_THRESH,
				       IIO_EV_DIR_FALLING),
				       data->timestamp);
		if (ret & KXCJK1013_REG_INT_SRC2_BIT_YP)
			iio_push_event(indio_dev,
				       IIO_MOD_EVENT_CODE(IIO_ACCEL,
				       0,
				       IIO_MOD_Y,
				       IIO_EV_TYPE_THRESH,
				       IIO_EV_DIR_RISING),
				       data->timestamp);

		if (ret & KXCJK1013_REG_INT_SRC2_BIT_ZN)
			iio_push_event(indio_dev,
				       IIO_MOD_EVENT_CODE(IIO_ACCEL,
				       0,
				       IIO_MOD_Z,
				       IIO_EV_TYPE_THRESH,
				       IIO_EV_DIR_FALLING),
				       data->timestamp);
		if (ret & KXCJK1013_REG_INT_SRC2_BIT_ZP)
			iio_push_event(indio_dev,
				       IIO_MOD_EVENT_CODE(IIO_ACCEL,
				       0,
				       IIO_MOD_Z,
				       IIO_EV_TYPE_THRESH,
				       IIO_EV_DIR_RISING),
				       data->timestamp);
	}

ack_intr:
	if (data->dready_trigger_on)
		return IRQ_HANDLED;

	ret = i2c_smbus_read_byte_data(data->client, KXCJK1013_REG_INT_REL);
	if (ret < 0)
		dev_err(&data->client->dev, "Error reading reg_int_rel\n");

	return IRQ_HANDLED;
}

static irqreturn_t kxcjk1013_data_rdy_trig_poll(int irq, void *private)
{
	struct iio_dev *indio_dev = private;
	struct kxcjk1013_data *data = iio_priv(indio_dev);

	data->timestamp = iio_get_time_ns();

	if (data->dready_trigger_on)
		iio_trigger_poll(data->dready_trig);
	else if (data->motion_trigger_on)
		iio_trigger_poll(data->motion_trig);

	if (data->ev_enable_state)
		return IRQ_WAKE_THREAD;
	else
		return IRQ_HANDLED;
}

static const char *kxcjk1013_match_acpi_device(struct device *dev,
					       enum kx_chipset *chipset)
{
	const struct acpi_device_id *id;
	id = acpi_match_device(dev->driver->acpi_match_table, dev);
	if (!id)
		return NULL;
	*chipset = (enum kx_chipset)id->driver_data;

	return dev_name(dev);
}

static int kxcjk1013_gpio_probe(struct i2c_client *client,
				struct kxcjk1013_data *data)
{
	struct device *dev;
	struct gpio_desc *gpio;
	int ret;

	if (!client)
		return -EINVAL;

	dev = &client->dev;

	/* data ready gpio interrupt pin */
	gpio = devm_gpiod_get_index(dev, "kxcjk1013_int", 0);
	if (IS_ERR(gpio)) {
		dev_err(dev, "acpi gpio get index failed\n");
		return PTR_ERR(gpio);
	}

	ret = gpiod_direction_input(gpio);
	if (ret)
		return ret;

	ret = gpiod_to_irq(gpio);

	dev_dbg(dev, "GPIO resource, no:%d irq:%d\n", desc_to_gpio(gpio), ret);

	return ret;
}

static int kxcjk1013_probe(struct i2c_client *client,
			   const struct i2c_device_id *id)
{
	struct kxcjk1013_data *data;
	struct iio_dev *indio_dev;
	struct kxcjk_1013_platform_data *pdata;
	const char *name;
	int ret;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	i2c_set_clientdata(client, indio_dev);
	data->client = client;

	pdata = dev_get_platdata(&client->dev);
	if (pdata)
		data->active_high_intr = pdata->active_high_intr;
	else
		data->active_high_intr = true; /* default polarity */

	if (id) {
		data->chipset = (enum kx_chipset)(id->driver_data);
		name = id->name;
	} else if (ACPI_HANDLE(&client->dev)) {
		name = kxcjk1013_match_acpi_device(&client->dev,
						   &data->chipset);
	} else
		return -ENODEV;

	ret = kxcjk1013_chip_init(data);
	if (ret < 0)
		return ret;

	mutex_init(&data->mutex);

	indio_dev->dev.parent = &client->dev;
	indio_dev->channels = kxcjk1013_channels;
	indio_dev->num_channels = ARRAY_SIZE(kxcjk1013_channels);
	indio_dev->name = name;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = &kxcjk1013_info;

	if (client->irq < 0)
		client->irq = kxcjk1013_gpio_probe(client, data);

	if (client->irq >= 0) {
		ret = devm_request_threaded_irq(&client->dev, client->irq,
						kxcjk1013_data_rdy_trig_poll,
						kxcjk1013_event_handler,
						IRQF_TRIGGER_RISING,
						KXCJK1013_IRQ_NAME,
						indio_dev);
		if (ret)
			return ret;

		data->dready_trig = devm_iio_trigger_alloc(&client->dev,
							   "%s-dev%d",
							   indio_dev->name,
							   indio_dev->id);
		if (!data->dready_trig)
			return -ENOMEM;

		data->motion_trig = devm_iio_trigger_alloc(&client->dev,
							  "%s-any-motion-dev%d",
							  indio_dev->name,
							  indio_dev->id);
		if (!data->motion_trig)
			return -ENOMEM;

		data->dready_trig->dev.parent = &client->dev;
		data->dready_trig->ops = &kxcjk1013_trigger_ops;
		iio_trigger_set_drvdata(data->dready_trig, indio_dev);
		indio_dev->trig = data->dready_trig;
		iio_trigger_get(indio_dev->trig);
		ret = iio_trigger_register(data->dready_trig);
		if (ret)
			return ret;

		data->motion_trig->dev.parent = &client->dev;
		data->motion_trig->ops = &kxcjk1013_trigger_ops;
		iio_trigger_set_drvdata(data->motion_trig, indio_dev);
		ret = iio_trigger_register(data->motion_trig);
		if (ret) {
			data->motion_trig = NULL;
			goto err_trigger_unregister;
		}

		ret = iio_triggered_buffer_setup(indio_dev,
						&iio_pollfunc_store_time,
						kxcjk1013_trigger_handler,
						NULL);
		if (ret < 0) {
			dev_err(&client->dev,
					"iio triggered buffer setup failed\n");
			goto err_trigger_unregister;
		}
	}

	ret = iio_device_register(indio_dev);
	if (ret < 0) {
		dev_err(&client->dev, "unable to register iio device\n");
		goto err_buffer_cleanup;
	}

	ret = pm_runtime_set_active(&client->dev);
	if (ret)
		goto err_iio_unregister;

	pm_runtime_enable(&client->dev);
	pm_runtime_set_autosuspend_delay(&client->dev,
					 KXCJK1013_SLEEP_DELAY_MS);
	pm_runtime_use_autosuspend(&client->dev);

	return 0;

err_iio_unregister:
	iio_device_unregister(indio_dev);
err_buffer_cleanup:
	if (data->dready_trig)
		iio_triggered_buffer_cleanup(indio_dev);
err_trigger_unregister:
	if (data->dready_trig)
		iio_trigger_unregister(data->dready_trig);
	if (data->motion_trig)
		iio_trigger_unregister(data->motion_trig);

	return ret;
}

static int kxcjk1013_remove(struct i2c_client *client)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(client);
	struct kxcjk1013_data *data = iio_priv(indio_dev);

	pm_runtime_disable(&client->dev);
	pm_runtime_set_suspended(&client->dev);
	pm_runtime_put_noidle(&client->dev);

	iio_device_unregister(indio_dev);

	if (data->dready_trig) {
		iio_triggered_buffer_cleanup(indio_dev);
		iio_trigger_unregister(data->dready_trig);
		iio_trigger_unregister(data->motion_trig);
	}

	mutex_lock(&data->mutex);
	kxcjk1013_set_mode(data, STANDBY);
	mutex_unlock(&data->mutex);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int kxcjk1013_suspend(struct device *dev)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(to_i2c_client(dev));
	struct kxcjk1013_data *data = iio_priv(indio_dev);
	int ret;

	mutex_lock(&data->mutex);
	ret = kxcjk1013_set_mode(data, STANDBY);
	mutex_unlock(&data->mutex);

	return ret;
}

static int kxcjk1013_resume(struct device *dev)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(to_i2c_client(dev));
	struct kxcjk1013_data *data = iio_priv(indio_dev);
	int ret = 0;

	mutex_lock(&data->mutex);
	/* Check, if the suspend occured while active */
	if (data->dready_trigger_on || data->motion_trigger_on ||
							data->ev_enable_state)
		ret = kxcjk1013_set_mode(data, OPERATION);
	mutex_unlock(&data->mutex);

	return ret;
}
#endif

#ifdef CONFIG_PM_RUNTIME
static int kxcjk1013_runtime_suspend(struct device *dev)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(to_i2c_client(dev));
	struct kxcjk1013_data *data = iio_priv(indio_dev);

	return kxcjk1013_set_mode(data, STANDBY);
}

static int kxcjk1013_runtime_resume(struct device *dev)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(to_i2c_client(dev));
	struct kxcjk1013_data *data = iio_priv(indio_dev);
	int ret;
	int sleep_val;

	ret = kxcjk1013_set_mode(data, OPERATION);
	if (ret < 0)
		return ret;

	sleep_val = kxcjk1013_get_startup_times(data);
	if (sleep_val < 20000)
		usleep_range(sleep_val, 20000);
	else
		msleep_interruptible(sleep_val/1000);

	return 0;
}
#endif

static const struct dev_pm_ops kxcjk1013_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(kxcjk1013_suspend, kxcjk1013_resume)
	SET_RUNTIME_PM_OPS(kxcjk1013_runtime_suspend,
			   kxcjk1013_runtime_resume, NULL)
};

static const struct acpi_device_id kx_acpi_match[] = {
	{"KXCJ1013", KXCJK1013},
	{"KXCJ1008", KXCJ91008},
	{"KXTJ1009", KXTJ21009},
	{ },
};
MODULE_DEVICE_TABLE(acpi, kx_acpi_match);

static const struct i2c_device_id kxcjk1013_id[] = {
	{"kxcjk1013", KXCJK1013},
	{"kxcj91008", KXCJ91008},
	{"kxtj21009", KXTJ21009},
	{}
};

MODULE_DEVICE_TABLE(i2c, kxcjk1013_id);

static struct i2c_driver kxcjk1013_driver = {
	.driver = {
		.name	= KXCJK1013_DRV_NAME,
		.acpi_match_table = ACPI_PTR(kx_acpi_match),
		.pm	= &kxcjk1013_pm_ops,
	},
	.probe		= kxcjk1013_probe,
	.remove		= kxcjk1013_remove,
	.id_table	= kxcjk1013_id,
};
module_i2c_driver(kxcjk1013_driver);

MODULE_AUTHOR("Srinivas Pandruvada <srinivas.pandruvada@linux.intel.com>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("KXCJK1013 accelerometer driver");
