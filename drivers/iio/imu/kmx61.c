// SPDX-License-Identifier: GPL-2.0-only
/*
 * KMX61 - Kionix 6-axis Accelerometer/Magnetometer
 *
 * Copyright (c) 2014, Intel Corporation.
 *
 * IIO driver for KMX61 (7-bit I2C slave address 0x0E or 0x0F).
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/acpi.h>
#include <linux/interrupt.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/events.h>
#include <linux/iio/trigger.h>
#include <linux/iio/buffer.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/iio/trigger_consumer.h>

#define KMX61_DRV_NAME "kmx61"
#define KMX61_IRQ_NAME "kmx61_event"

#define KMX61_REG_WHO_AM_I	0x00
#define KMX61_REG_INS1		0x01
#define KMX61_REG_INS2		0x02

/*
 * three 16-bit accelerometer output registers for X/Y/Z axis
 * we use only XOUT_L as a base register, all other addresses
 * can be obtained by applying an offset and are provided here
 * only for clarity.
 */
#define KMX61_ACC_XOUT_L	0x0A
#define KMX61_ACC_XOUT_H	0x0B
#define KMX61_ACC_YOUT_L	0x0C
#define KMX61_ACC_YOUT_H	0x0D
#define KMX61_ACC_ZOUT_L	0x0E
#define KMX61_ACC_ZOUT_H	0x0F

/*
 * one 16-bit temperature output register
 */
#define KMX61_TEMP_L		0x10
#define KMX61_TEMP_H		0x11

/*
 * three 16-bit magnetometer output registers for X/Y/Z axis
 */
#define KMX61_MAG_XOUT_L	0x12
#define KMX61_MAG_XOUT_H	0x13
#define KMX61_MAG_YOUT_L	0x14
#define KMX61_MAG_YOUT_H	0x15
#define KMX61_MAG_ZOUT_L	0x16
#define KMX61_MAG_ZOUT_H	0x17

#define KMX61_REG_INL		0x28
#define KMX61_REG_STBY		0x29
#define KMX61_REG_CTRL1		0x2A
#define KMX61_REG_CTRL2		0x2B
#define KMX61_REG_ODCNTL	0x2C
#define KMX61_REG_INC1		0x2D

#define KMX61_REG_WUF_THRESH	0x3D
#define KMX61_REG_WUF_TIMER	0x3E

#define KMX61_ACC_STBY_BIT	BIT(0)
#define KMX61_MAG_STBY_BIT	BIT(1)
#define KMX61_ACT_STBY_BIT	BIT(7)

#define KMX61_ALL_STBY		(KMX61_ACC_STBY_BIT | KMX61_MAG_STBY_BIT)

#define KMX61_REG_INS1_BIT_WUFS		BIT(1)

#define KMX61_REG_INS2_BIT_ZP		BIT(0)
#define KMX61_REG_INS2_BIT_ZN		BIT(1)
#define KMX61_REG_INS2_BIT_YP		BIT(2)
#define KMX61_REG_INS2_BIT_YN		BIT(3)
#define KMX61_REG_INS2_BIT_XP		BIT(4)
#define KMX61_REG_INS2_BIT_XN		BIT(5)

#define KMX61_REG_CTRL1_GSEL_MASK	0x03

#define KMX61_REG_CTRL1_BIT_RES		BIT(4)
#define KMX61_REG_CTRL1_BIT_DRDYE	BIT(5)
#define KMX61_REG_CTRL1_BIT_WUFE	BIT(6)
#define KMX61_REG_CTRL1_BIT_BTSE	BIT(7)

#define KMX61_REG_INC1_BIT_WUFS		BIT(0)
#define KMX61_REG_INC1_BIT_DRDYM	BIT(1)
#define KMX61_REG_INC1_BIT_DRDYA	BIT(2)
#define KMX61_REG_INC1_BIT_IEN		BIT(5)

#define KMX61_ACC_ODR_SHIFT	0
#define KMX61_MAG_ODR_SHIFT	4
#define KMX61_ACC_ODR_MASK	0x0F
#define KMX61_MAG_ODR_MASK	0xF0

#define KMX61_OWUF_MASK		0x7

#define KMX61_DEFAULT_WAKE_THRESH	1
#define KMX61_DEFAULT_WAKE_DURATION	1

#define KMX61_SLEEP_DELAY_MS	2000

#define KMX61_CHIP_ID		0x12

/* KMX61 devices */
#define KMX61_ACC	0x01
#define KMX61_MAG	0x02

struct kmx61_data {
	struct i2c_client *client;

	/* serialize access to non-atomic ops, e.g set_mode */
	struct mutex lock;

	/* standby state */
	bool acc_stby;
	bool mag_stby;

	/* power state */
	bool acc_ps;
	bool mag_ps;

	/* config bits */
	u8 range;
	u8 odr_bits;
	u8 wake_thresh;
	u8 wake_duration;

	/* accelerometer specific data */
	struct iio_dev *acc_indio_dev;
	struct iio_trigger *acc_dready_trig;
	struct iio_trigger *motion_trig;
	bool acc_dready_trig_on;
	bool motion_trig_on;
	bool ev_enable_state;

	/* magnetometer specific data */
	struct iio_dev *mag_indio_dev;
	struct iio_trigger *mag_dready_trig;
	bool mag_dready_trig_on;
};

enum kmx61_range {
	KMX61_RANGE_2G,
	KMX61_RANGE_4G,
	KMX61_RANGE_8G,
};

enum kmx61_axis {
	KMX61_AXIS_X,
	KMX61_AXIS_Y,
	KMX61_AXIS_Z,
};

static const u16 kmx61_uscale_table[] = {9582, 19163, 38326};

static const struct {
	int val;
	int val2;
} kmx61_samp_freq_table[] = { {12, 500000},
			{25, 0},
			{50, 0},
			{100, 0},
			{200, 0},
			{400, 0},
			{800, 0},
			{1600, 0},
			{0, 781000},
			{1, 563000},
			{3, 125000},
			{6, 250000} };

static const struct {
	int val;
	int val2;
	int odr_bits;
} kmx61_wake_up_odr_table[] = { {0, 781000, 0x00},
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

static IIO_CONST_ATTR(accel_scale_available, "0.009582 0.019163 0.038326");
static IIO_CONST_ATTR(magn_scale_available, "0.001465");
static IIO_CONST_ATTR_SAMP_FREQ_AVAIL(
	"0.781000 1.563000 3.125000 6.250000 12.500000 25 50 100 200 400 800");

static struct attribute *kmx61_acc_attributes[] = {
	&iio_const_attr_accel_scale_available.dev_attr.attr,
	&iio_const_attr_sampling_frequency_available.dev_attr.attr,
	NULL,
};

static struct attribute *kmx61_mag_attributes[] = {
	&iio_const_attr_magn_scale_available.dev_attr.attr,
	&iio_const_attr_sampling_frequency_available.dev_attr.attr,
	NULL,
};

static const struct attribute_group kmx61_acc_attribute_group = {
	.attrs = kmx61_acc_attributes,
};

static const struct attribute_group kmx61_mag_attribute_group = {
	.attrs = kmx61_mag_attributes,
};

static const struct iio_event_spec kmx61_event = {
	.type = IIO_EV_TYPE_THRESH,
	.dir = IIO_EV_DIR_EITHER,
	.mask_separate = BIT(IIO_EV_INFO_VALUE) |
			 BIT(IIO_EV_INFO_ENABLE) |
			 BIT(IIO_EV_INFO_PERIOD),
};

#define KMX61_ACC_CHAN(_axis) { \
	.type = IIO_ACCEL, \
	.modified = 1, \
	.channel2 = IIO_MOD_ ## _axis, \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW), \
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE) | \
				BIT(IIO_CHAN_INFO_SAMP_FREQ), \
	.address = KMX61_ACC, \
	.scan_index = KMX61_AXIS_ ## _axis, \
	.scan_type = { \
		.sign = 's', \
		.realbits = 12, \
		.storagebits = 16, \
		.shift = 4, \
		.endianness = IIO_LE, \
	}, \
	.event_spec = &kmx61_event, \
	.num_event_specs = 1 \
}

#define KMX61_MAG_CHAN(_axis) { \
	.type = IIO_MAGN, \
	.modified = 1, \
	.channel2 = IIO_MOD_ ## _axis, \
	.address = KMX61_MAG, \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW), \
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE) | \
				BIT(IIO_CHAN_INFO_SAMP_FREQ), \
	.scan_index = KMX61_AXIS_ ## _axis, \
	.scan_type = { \
		.sign = 's', \
		.realbits = 14, \
		.storagebits = 16, \
		.shift = 2, \
		.endianness = IIO_LE, \
	}, \
}

static const struct iio_chan_spec kmx61_acc_channels[] = {
	KMX61_ACC_CHAN(X),
	KMX61_ACC_CHAN(Y),
	KMX61_ACC_CHAN(Z),
};

static const struct iio_chan_spec kmx61_mag_channels[] = {
	KMX61_MAG_CHAN(X),
	KMX61_MAG_CHAN(Y),
	KMX61_MAG_CHAN(Z),
};

static void kmx61_set_data(struct iio_dev *indio_dev, struct kmx61_data *data)
{
	struct kmx61_data **priv = iio_priv(indio_dev);

	*priv = data;
}

static struct kmx61_data *kmx61_get_data(struct iio_dev *indio_dev)
{
	return *(struct kmx61_data **)iio_priv(indio_dev);
}

static int kmx61_convert_freq_to_bit(int val, int val2)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(kmx61_samp_freq_table); i++)
		if (val == kmx61_samp_freq_table[i].val &&
		    val2 == kmx61_samp_freq_table[i].val2)
			return i;
	return -EINVAL;
}

static int kmx61_convert_wake_up_odr_to_bit(int val, int val2)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(kmx61_wake_up_odr_table); ++i)
		if (kmx61_wake_up_odr_table[i].val == val &&
			kmx61_wake_up_odr_table[i].val2 == val2)
				return kmx61_wake_up_odr_table[i].odr_bits;
	return -EINVAL;
}

/**
 * kmx61_set_mode() - set KMX61 device operating mode
 * @data: kmx61 device private data pointer
 * @mode: bitmask, indicating operating mode for @device
 * @device: bitmask, indicating device for which @mode needs to be set
 * @update: update stby bits stored in device's private  @data
 *
 * For each sensor (accelerometer/magnetometer) there are two operating modes
 * STANDBY and OPERATION. Neither accel nor magn can be disabled independently
 * if they are both enabled. Internal sensors state is saved in acc_stby and
 * mag_stby members of driver's private @data.
 */
static int kmx61_set_mode(struct kmx61_data *data, u8 mode, u8 device,
			  bool update)
{
	int ret;
	int acc_stby = -1, mag_stby = -1;

	ret = i2c_smbus_read_byte_data(data->client, KMX61_REG_STBY);
	if (ret < 0) {
		dev_err(&data->client->dev, "Error reading reg_stby\n");
		return ret;
	}
	if (device & KMX61_ACC) {
		if (mode & KMX61_ACC_STBY_BIT) {
			ret |= KMX61_ACC_STBY_BIT;
			acc_stby = 1;
		} else {
			ret &= ~KMX61_ACC_STBY_BIT;
			acc_stby = 0;
		}
	}

	if (device & KMX61_MAG) {
		if (mode & KMX61_MAG_STBY_BIT) {
			ret |= KMX61_MAG_STBY_BIT;
			mag_stby = 1;
		} else {
			ret &= ~KMX61_MAG_STBY_BIT;
			mag_stby = 0;
		}
	}

	if (mode & KMX61_ACT_STBY_BIT)
		ret |= KMX61_ACT_STBY_BIT;

	ret = i2c_smbus_write_byte_data(data->client, KMX61_REG_STBY, ret);
	if (ret < 0) {
		dev_err(&data->client->dev, "Error writing reg_stby\n");
		return ret;
	}

	if (acc_stby != -1 && update)
		data->acc_stby = acc_stby;
	if (mag_stby != -1 && update)
		data->mag_stby = mag_stby;

	return 0;
}

static int kmx61_get_mode(struct kmx61_data *data, u8 *mode, u8 device)
{
	int ret;

	ret = i2c_smbus_read_byte_data(data->client, KMX61_REG_STBY);
	if (ret < 0) {
		dev_err(&data->client->dev, "Error reading reg_stby\n");
		return ret;
	}
	*mode = 0;

	if (device & KMX61_ACC) {
		if (ret & KMX61_ACC_STBY_BIT)
			*mode |= KMX61_ACC_STBY_BIT;
		else
			*mode &= ~KMX61_ACC_STBY_BIT;
	}

	if (device & KMX61_MAG) {
		if (ret & KMX61_MAG_STBY_BIT)
			*mode |= KMX61_MAG_STBY_BIT;
		else
			*mode &= ~KMX61_MAG_STBY_BIT;
	}

	return 0;
}

static int kmx61_set_wake_up_odr(struct kmx61_data *data, int val, int val2)
{
	int ret, odr_bits;

	odr_bits = kmx61_convert_wake_up_odr_to_bit(val, val2);
	if (odr_bits < 0)
		return odr_bits;

	ret = i2c_smbus_write_byte_data(data->client, KMX61_REG_CTRL2,
					odr_bits);
	if (ret < 0)
		dev_err(&data->client->dev, "Error writing reg_ctrl2\n");
	return ret;
}

static int kmx61_set_odr(struct kmx61_data *data, int val, int val2, u8 device)
{
	int ret;
	u8 mode;
	int lodr_bits, odr_bits;

	ret = kmx61_get_mode(data, &mode, KMX61_ACC | KMX61_MAG);
	if (ret < 0)
		return ret;

	lodr_bits = kmx61_convert_freq_to_bit(val, val2);
	if (lodr_bits < 0)
		return lodr_bits;

	/* To change ODR, accel and magn must be in STDBY */
	ret = kmx61_set_mode(data, KMX61_ALL_STBY, KMX61_ACC | KMX61_MAG,
			     true);
	if (ret < 0)
		return ret;

	odr_bits = 0;
	if (device & KMX61_ACC)
		odr_bits |= lodr_bits << KMX61_ACC_ODR_SHIFT;
	if (device & KMX61_MAG)
		odr_bits |= lodr_bits << KMX61_MAG_ODR_SHIFT;

	ret = i2c_smbus_write_byte_data(data->client, KMX61_REG_ODCNTL,
					odr_bits);
	if (ret < 0)
		return ret;

	data->odr_bits = odr_bits;

	if (device & KMX61_ACC) {
		ret = kmx61_set_wake_up_odr(data, val, val2);
		if (ret)
			return ret;
	}

	return kmx61_set_mode(data, mode, KMX61_ACC | KMX61_MAG, true);
}

static int kmx61_get_odr(struct kmx61_data *data, int *val, int *val2,
			 u8 device)
{
	u8 lodr_bits;

	if (device & KMX61_ACC)
		lodr_bits = (data->odr_bits >> KMX61_ACC_ODR_SHIFT) &
			     KMX61_ACC_ODR_MASK;
	else if (device & KMX61_MAG)
		lodr_bits = (data->odr_bits >> KMX61_MAG_ODR_SHIFT) &
			     KMX61_MAG_ODR_MASK;
	else
		return -EINVAL;

	if (lodr_bits >= ARRAY_SIZE(kmx61_samp_freq_table))
		return -EINVAL;

	*val = kmx61_samp_freq_table[lodr_bits].val;
	*val2 = kmx61_samp_freq_table[lodr_bits].val2;

	return 0;
}

static int kmx61_set_range(struct kmx61_data *data, u8 range)
{
	int ret;

	ret = i2c_smbus_read_byte_data(data->client, KMX61_REG_CTRL1);
	if (ret < 0) {
		dev_err(&data->client->dev, "Error reading reg_ctrl1\n");
		return ret;
	}

	ret &= ~KMX61_REG_CTRL1_GSEL_MASK;
	ret |= range & KMX61_REG_CTRL1_GSEL_MASK;

	ret = i2c_smbus_write_byte_data(data->client, KMX61_REG_CTRL1, ret);
	if (ret < 0) {
		dev_err(&data->client->dev, "Error writing reg_ctrl1\n");
		return ret;
	}

	data->range = range;

	return 0;
}

static int kmx61_set_scale(struct kmx61_data *data, u16 uscale)
{
	int ret, i;
	u8  mode;

	for (i = 0; i < ARRAY_SIZE(kmx61_uscale_table); i++) {
		if (kmx61_uscale_table[i] == uscale) {
			ret = kmx61_get_mode(data, &mode,
					     KMX61_ACC | KMX61_MAG);
			if (ret < 0)
				return ret;

			ret = kmx61_set_mode(data, KMX61_ALL_STBY,
					     KMX61_ACC | KMX61_MAG, true);
			if (ret < 0)
				return ret;

			ret = kmx61_set_range(data, i);
			if (ret < 0)
				return ret;

			return  kmx61_set_mode(data, mode,
					       KMX61_ACC | KMX61_MAG, true);
		}
	}
	return -EINVAL;
}

static int kmx61_chip_init(struct kmx61_data *data)
{
	int ret, val, val2;

	ret = i2c_smbus_read_byte_data(data->client, KMX61_REG_WHO_AM_I);
	if (ret < 0) {
		dev_err(&data->client->dev, "Error reading who_am_i\n");
		return ret;
	}

	if (ret != KMX61_CHIP_ID) {
		dev_err(&data->client->dev,
			"Wrong chip id, got %x expected %x\n",
			 ret, KMX61_CHIP_ID);
		return -EINVAL;
	}

	/* set accel 12bit, 4g range */
	ret = kmx61_set_range(data, KMX61_RANGE_4G);
	if (ret < 0)
		return ret;

	ret = i2c_smbus_read_byte_data(data->client, KMX61_REG_ODCNTL);
	if (ret < 0) {
		dev_err(&data->client->dev, "Error reading reg_odcntl\n");
		return ret;
	}
	data->odr_bits = ret;

	/*
	 * set output data rate for wake up (motion detection) function
	 * to match data rate for accelerometer sampling
	 */
	ret = kmx61_get_odr(data, &val, &val2, KMX61_ACC);
	if (ret < 0)
		return ret;

	ret = kmx61_set_wake_up_odr(data, val, val2);
	if (ret < 0)
		return ret;

	/* set acc/magn to OPERATION mode */
	ret = kmx61_set_mode(data, 0, KMX61_ACC | KMX61_MAG, true);
	if (ret < 0)
		return ret;

	data->wake_thresh = KMX61_DEFAULT_WAKE_THRESH;
	data->wake_duration = KMX61_DEFAULT_WAKE_DURATION;

	return 0;
}

static int kmx61_setup_new_data_interrupt(struct kmx61_data *data,
					  bool status, u8 device)
{
	u8 mode;
	int ret;

	ret = kmx61_get_mode(data, &mode, KMX61_ACC | KMX61_MAG);
	if (ret < 0)
		return ret;

	ret = kmx61_set_mode(data, KMX61_ALL_STBY, KMX61_ACC | KMX61_MAG, true);
	if (ret < 0)
		return ret;

	ret = i2c_smbus_read_byte_data(data->client, KMX61_REG_INC1);
	if (ret < 0) {
		dev_err(&data->client->dev, "Error reading reg_ctrl1\n");
		return ret;
	}

	if (status) {
		ret |= KMX61_REG_INC1_BIT_IEN;
		if (device & KMX61_ACC)
			ret |= KMX61_REG_INC1_BIT_DRDYA;
		if (device & KMX61_MAG)
			ret |=  KMX61_REG_INC1_BIT_DRDYM;
	} else {
		ret &= ~KMX61_REG_INC1_BIT_IEN;
		if (device & KMX61_ACC)
			ret &= ~KMX61_REG_INC1_BIT_DRDYA;
		if (device & KMX61_MAG)
			ret &= ~KMX61_REG_INC1_BIT_DRDYM;
	}
	ret = i2c_smbus_write_byte_data(data->client, KMX61_REG_INC1, ret);
	if (ret < 0) {
		dev_err(&data->client->dev, "Error writing reg_int_ctrl1\n");
		return ret;
	}

	ret = i2c_smbus_read_byte_data(data->client, KMX61_REG_CTRL1);
	if (ret < 0) {
		dev_err(&data->client->dev, "Error reading reg_ctrl1\n");
		return ret;
	}

	if (status)
		ret |= KMX61_REG_CTRL1_BIT_DRDYE;
	else
		ret &= ~KMX61_REG_CTRL1_BIT_DRDYE;

	ret = i2c_smbus_write_byte_data(data->client, KMX61_REG_CTRL1, ret);
	if (ret < 0) {
		dev_err(&data->client->dev, "Error writing reg_ctrl1\n");
		return ret;
	}

	return kmx61_set_mode(data, mode, KMX61_ACC | KMX61_MAG, true);
}

static int kmx61_chip_update_thresholds(struct kmx61_data *data)
{
	int ret;

	ret = i2c_smbus_write_byte_data(data->client,
					KMX61_REG_WUF_TIMER,
					data->wake_duration);
	if (ret < 0) {
		dev_err(&data->client->dev, "Errow writing reg_wuf_timer\n");
		return ret;
	}

	ret = i2c_smbus_write_byte_data(data->client,
					KMX61_REG_WUF_THRESH,
					data->wake_thresh);
	if (ret < 0)
		dev_err(&data->client->dev, "Error writing reg_wuf_thresh\n");

	return ret;
}

static int kmx61_setup_any_motion_interrupt(struct kmx61_data *data,
					    bool status)
{
	u8 mode;
	int ret;

	ret = kmx61_get_mode(data, &mode, KMX61_ACC | KMX61_MAG);
	if (ret < 0)
		return ret;

	ret = kmx61_set_mode(data, KMX61_ALL_STBY, KMX61_ACC | KMX61_MAG, true);
	if (ret < 0)
		return ret;

	ret = kmx61_chip_update_thresholds(data);
	if (ret < 0)
		return ret;

	ret = i2c_smbus_read_byte_data(data->client, KMX61_REG_INC1);
	if (ret < 0) {
		dev_err(&data->client->dev, "Error reading reg_inc1\n");
		return ret;
	}
	if (status)
		ret |= (KMX61_REG_INC1_BIT_IEN | KMX61_REG_INC1_BIT_WUFS);
	else
		ret &= ~(KMX61_REG_INC1_BIT_IEN | KMX61_REG_INC1_BIT_WUFS);

	ret = i2c_smbus_write_byte_data(data->client, KMX61_REG_INC1, ret);
	if (ret < 0) {
		dev_err(&data->client->dev, "Error writing reg_inc1\n");
		return ret;
	}

	ret = i2c_smbus_read_byte_data(data->client, KMX61_REG_CTRL1);
	if (ret < 0) {
		dev_err(&data->client->dev, "Error reading reg_ctrl1\n");
		return ret;
	}

	if (status)
		ret |= KMX61_REG_CTRL1_BIT_WUFE | KMX61_REG_CTRL1_BIT_BTSE;
	else
		ret &= ~(KMX61_REG_CTRL1_BIT_WUFE | KMX61_REG_CTRL1_BIT_BTSE);

	ret = i2c_smbus_write_byte_data(data->client, KMX61_REG_CTRL1, ret);
	if (ret < 0) {
		dev_err(&data->client->dev, "Error writing reg_ctrl1\n");
		return ret;
	}
	mode |= KMX61_ACT_STBY_BIT;
	return kmx61_set_mode(data, mode, KMX61_ACC | KMX61_MAG, true);
}

/**
 * kmx61_set_power_state() - set power state for kmx61 @device
 * @data: kmx61 device private pointer
 * @on: power state to be set for @device
 * @device: bitmask indicating device for which @on state needs to be set
 *
 * Notice that when ACC power state needs to be set to ON and MAG is in
 * OPERATION then we know that kmx61_runtime_resume was already called
 * so we must set ACC OPERATION mode here. The same happens when MAG power
 * state needs to be set to ON and ACC is in OPERATION.
 */
static int kmx61_set_power_state(struct kmx61_data *data, bool on, u8 device)
{
#ifdef CONFIG_PM
	int ret;

	if (device & KMX61_ACC) {
		if (on && !data->acc_ps && !data->mag_stby) {
			ret = kmx61_set_mode(data, 0, KMX61_ACC, true);
			if (ret < 0)
				return ret;
		}
		data->acc_ps = on;
	}
	if (device & KMX61_MAG) {
		if (on && !data->mag_ps && !data->acc_stby) {
			ret = kmx61_set_mode(data, 0, KMX61_MAG, true);
			if (ret < 0)
				return ret;
		}
		data->mag_ps = on;
	}

	if (on) {
		ret = pm_runtime_resume_and_get(&data->client->dev);
	} else {
		pm_runtime_mark_last_busy(&data->client->dev);
		ret = pm_runtime_put_autosuspend(&data->client->dev);
	}
	if (ret < 0) {
		dev_err(&data->client->dev,
			"Failed: kmx61_set_power_state for %d, ret %d\n",
			on, ret);

		return ret;
	}
#endif
	return 0;
}

static int kmx61_read_measurement(struct kmx61_data *data, u8 base, u8 offset)
{
	int ret;
	u8 reg = base + offset * 2;

	ret = i2c_smbus_read_word_data(data->client, reg);
	if (ret < 0)
		dev_err(&data->client->dev, "failed to read reg at %x\n", reg);

	return ret;
}

static int kmx61_read_raw(struct iio_dev *indio_dev,
			  struct iio_chan_spec const *chan, int *val,
			  int *val2, long mask)
{
	int ret;
	u8 base_reg;
	struct kmx61_data *data = kmx61_get_data(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		switch (chan->type) {
		case IIO_ACCEL:
			base_reg = KMX61_ACC_XOUT_L;
			break;
		case IIO_MAGN:
			base_reg = KMX61_MAG_XOUT_L;
			break;
		default:
			return -EINVAL;
		}
		mutex_lock(&data->lock);

		ret = kmx61_set_power_state(data, true, chan->address);
		if (ret) {
			mutex_unlock(&data->lock);
			return ret;
		}

		ret = kmx61_read_measurement(data, base_reg, chan->scan_index);
		if (ret < 0) {
			kmx61_set_power_state(data, false, chan->address);
			mutex_unlock(&data->lock);
			return ret;
		}
		*val = sign_extend32(ret >> chan->scan_type.shift,
				     chan->scan_type.realbits - 1);
		ret = kmx61_set_power_state(data, false, chan->address);

		mutex_unlock(&data->lock);
		if (ret)
			return ret;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_ACCEL:
			*val = 0;
			*val2 = kmx61_uscale_table[data->range];
			return IIO_VAL_INT_PLUS_MICRO;
		case IIO_MAGN:
			/* 14 bits res, 1465 microGauss per magn count */
			*val = 0;
			*val2 = 1465;
			return IIO_VAL_INT_PLUS_MICRO;
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_SAMP_FREQ:
		if (chan->type != IIO_ACCEL && chan->type != IIO_MAGN)
			return -EINVAL;

		mutex_lock(&data->lock);
		ret = kmx61_get_odr(data, val, val2, chan->address);
		mutex_unlock(&data->lock);
		if (ret)
			return -EINVAL;
		return IIO_VAL_INT_PLUS_MICRO;
	}
	return -EINVAL;
}

static int kmx61_write_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan, int val,
			   int val2, long mask)
{
	int ret;
	struct kmx61_data *data = kmx61_get_data(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		if (chan->type != IIO_ACCEL && chan->type != IIO_MAGN)
			return -EINVAL;

		mutex_lock(&data->lock);
		ret = kmx61_set_odr(data, val, val2, chan->address);
		mutex_unlock(&data->lock);
		return ret;
	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_ACCEL:
			if (val != 0)
				return -EINVAL;
			mutex_lock(&data->lock);
			ret = kmx61_set_scale(data, val2);
			mutex_unlock(&data->lock);
			return ret;
		default:
			return -EINVAL;
		}
	default:
		return -EINVAL;
	}
}

static int kmx61_read_event(struct iio_dev *indio_dev,
			    const struct iio_chan_spec *chan,
			    enum iio_event_type type,
			    enum iio_event_direction dir,
			    enum iio_event_info info,
			    int *val, int *val2)
{
	struct kmx61_data *data = kmx61_get_data(indio_dev);

	*val2 = 0;
	switch (info) {
	case IIO_EV_INFO_VALUE:
		*val = data->wake_thresh;
		return IIO_VAL_INT;
	case IIO_EV_INFO_PERIOD:
		*val = data->wake_duration;
		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}
}

static int kmx61_write_event(struct iio_dev *indio_dev,
			     const struct iio_chan_spec *chan,
			     enum iio_event_type type,
			     enum iio_event_direction dir,
			     enum iio_event_info info,
			     int val, int val2)
{
	struct kmx61_data *data = kmx61_get_data(indio_dev);

	if (data->ev_enable_state)
		return -EBUSY;

	switch (info) {
	case IIO_EV_INFO_VALUE:
		data->wake_thresh = val;
		return IIO_VAL_INT;
	case IIO_EV_INFO_PERIOD:
		data->wake_duration = val;
		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}
}

static int kmx61_read_event_config(struct iio_dev *indio_dev,
				   const struct iio_chan_spec *chan,
				   enum iio_event_type type,
				   enum iio_event_direction dir)
{
	struct kmx61_data *data = kmx61_get_data(indio_dev);

	return data->ev_enable_state;
}

static int kmx61_write_event_config(struct iio_dev *indio_dev,
				    const struct iio_chan_spec *chan,
				    enum iio_event_type type,
				    enum iio_event_direction dir,
				    int state)
{
	struct kmx61_data *data = kmx61_get_data(indio_dev);
	int ret = 0;

	if (state && data->ev_enable_state)
		return 0;

	mutex_lock(&data->lock);

	if (!state && data->motion_trig_on) {
		data->ev_enable_state = false;
		goto err_unlock;
	}

	ret = kmx61_set_power_state(data, state, KMX61_ACC);
	if (ret < 0)
		goto err_unlock;

	ret = kmx61_setup_any_motion_interrupt(data, state);
	if (ret < 0) {
		kmx61_set_power_state(data, false, KMX61_ACC);
		goto err_unlock;
	}

	data->ev_enable_state = state;

err_unlock:
	mutex_unlock(&data->lock);

	return ret;
}

static int kmx61_acc_validate_trigger(struct iio_dev *indio_dev,
				      struct iio_trigger *trig)
{
	struct kmx61_data *data = kmx61_get_data(indio_dev);

	if (data->acc_dready_trig != trig && data->motion_trig != trig)
		return -EINVAL;

	return 0;
}

static int kmx61_mag_validate_trigger(struct iio_dev *indio_dev,
				      struct iio_trigger *trig)
{
	struct kmx61_data *data = kmx61_get_data(indio_dev);

	if (data->mag_dready_trig != trig)
		return -EINVAL;

	return 0;
}

static const struct iio_info kmx61_acc_info = {
	.read_raw		= kmx61_read_raw,
	.write_raw		= kmx61_write_raw,
	.attrs			= &kmx61_acc_attribute_group,
	.read_event_value	= kmx61_read_event,
	.write_event_value	= kmx61_write_event,
	.read_event_config	= kmx61_read_event_config,
	.write_event_config	= kmx61_write_event_config,
	.validate_trigger	= kmx61_acc_validate_trigger,
};

static const struct iio_info kmx61_mag_info = {
	.read_raw		= kmx61_read_raw,
	.write_raw		= kmx61_write_raw,
	.attrs			= &kmx61_mag_attribute_group,
	.validate_trigger	= kmx61_mag_validate_trigger,
};


static int kmx61_data_rdy_trigger_set_state(struct iio_trigger *trig,
					    bool state)
{
	int ret = 0;
	u8 device;

	struct iio_dev *indio_dev = iio_trigger_get_drvdata(trig);
	struct kmx61_data *data = kmx61_get_data(indio_dev);

	mutex_lock(&data->lock);

	if (!state && data->ev_enable_state && data->motion_trig_on) {
		data->motion_trig_on = false;
		goto err_unlock;
	}

	if (data->acc_dready_trig == trig || data->motion_trig == trig)
		device = KMX61_ACC;
	else
		device = KMX61_MAG;

	ret = kmx61_set_power_state(data, state, device);
	if (ret < 0)
		goto err_unlock;

	if (data->acc_dready_trig == trig || data->mag_dready_trig == trig)
		ret = kmx61_setup_new_data_interrupt(data, state, device);
	else
		ret = kmx61_setup_any_motion_interrupt(data, state);
	if (ret < 0) {
		kmx61_set_power_state(data, false, device);
		goto err_unlock;
	}

	if (data->acc_dready_trig == trig)
		data->acc_dready_trig_on = state;
	else if (data->mag_dready_trig == trig)
		data->mag_dready_trig_on = state;
	else
		data->motion_trig_on = state;
err_unlock:
	mutex_unlock(&data->lock);

	return ret;
}

static void kmx61_trig_reenable(struct iio_trigger *trig)
{
	struct iio_dev *indio_dev = iio_trigger_get_drvdata(trig);
	struct kmx61_data *data = kmx61_get_data(indio_dev);
	int ret;

	ret = i2c_smbus_read_byte_data(data->client, KMX61_REG_INL);
	if (ret < 0)
		dev_err(&data->client->dev, "Error reading reg_inl\n");
}

static const struct iio_trigger_ops kmx61_trigger_ops = {
	.set_trigger_state = kmx61_data_rdy_trigger_set_state,
	.reenable = kmx61_trig_reenable,
};

static irqreturn_t kmx61_event_handler(int irq, void *private)
{
	struct kmx61_data *data = private;
	struct iio_dev *indio_dev = data->acc_indio_dev;
	int ret;

	ret = i2c_smbus_read_byte_data(data->client, KMX61_REG_INS1);
	if (ret < 0) {
		dev_err(&data->client->dev, "Error reading reg_ins1\n");
		goto ack_intr;
	}

	if (ret & KMX61_REG_INS1_BIT_WUFS) {
		ret = i2c_smbus_read_byte_data(data->client, KMX61_REG_INS2);
		if (ret < 0) {
			dev_err(&data->client->dev, "Error reading reg_ins2\n");
			goto ack_intr;
		}

		if (ret & KMX61_REG_INS2_BIT_XN)
			iio_push_event(indio_dev,
				       IIO_MOD_EVENT_CODE(IIO_ACCEL,
				       0,
				       IIO_MOD_X,
				       IIO_EV_TYPE_THRESH,
				       IIO_EV_DIR_FALLING),
				       0);

		if (ret & KMX61_REG_INS2_BIT_XP)
			iio_push_event(indio_dev,
				       IIO_MOD_EVENT_CODE(IIO_ACCEL,
				       0,
				       IIO_MOD_X,
				       IIO_EV_TYPE_THRESH,
				       IIO_EV_DIR_RISING),
				       0);

		if (ret & KMX61_REG_INS2_BIT_YN)
			iio_push_event(indio_dev,
				       IIO_MOD_EVENT_CODE(IIO_ACCEL,
				       0,
				       IIO_MOD_Y,
				       IIO_EV_TYPE_THRESH,
				       IIO_EV_DIR_FALLING),
				       0);

		if (ret & KMX61_REG_INS2_BIT_YP)
			iio_push_event(indio_dev,
				       IIO_MOD_EVENT_CODE(IIO_ACCEL,
				       0,
				       IIO_MOD_Y,
				       IIO_EV_TYPE_THRESH,
				       IIO_EV_DIR_RISING),
				       0);

		if (ret & KMX61_REG_INS2_BIT_ZN)
			iio_push_event(indio_dev,
				       IIO_MOD_EVENT_CODE(IIO_ACCEL,
				       0,
				       IIO_MOD_Z,
				       IIO_EV_TYPE_THRESH,
				       IIO_EV_DIR_FALLING),
				       0);

		if (ret & KMX61_REG_INS2_BIT_ZP)
			iio_push_event(indio_dev,
				       IIO_MOD_EVENT_CODE(IIO_ACCEL,
				       0,
				       IIO_MOD_Z,
				       IIO_EV_TYPE_THRESH,
				       IIO_EV_DIR_RISING),
				       0);
	}

ack_intr:
	ret = i2c_smbus_read_byte_data(data->client, KMX61_REG_CTRL1);
	if (ret < 0)
		dev_err(&data->client->dev, "Error reading reg_ctrl1\n");

	ret |= KMX61_REG_CTRL1_BIT_RES;
	ret = i2c_smbus_write_byte_data(data->client, KMX61_REG_CTRL1, ret);
	if (ret < 0)
		dev_err(&data->client->dev, "Error writing reg_ctrl1\n");

	ret = i2c_smbus_read_byte_data(data->client, KMX61_REG_INL);
	if (ret < 0)
		dev_err(&data->client->dev, "Error reading reg_inl\n");

	return IRQ_HANDLED;
}

static irqreturn_t kmx61_data_rdy_trig_poll(int irq, void *private)
{
	struct kmx61_data *data = private;

	if (data->acc_dready_trig_on)
		iio_trigger_poll(data->acc_dready_trig);
	if (data->mag_dready_trig_on)
		iio_trigger_poll(data->mag_dready_trig);

	if (data->motion_trig_on)
		iio_trigger_poll(data->motion_trig);

	if (data->ev_enable_state)
		return IRQ_WAKE_THREAD;
	return IRQ_HANDLED;
}

static irqreturn_t kmx61_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct kmx61_data *data = kmx61_get_data(indio_dev);
	int bit, ret, i = 0;
	u8 base;
	s16 buffer[8];

	if (indio_dev == data->acc_indio_dev)
		base = KMX61_ACC_XOUT_L;
	else
		base = KMX61_MAG_XOUT_L;

	mutex_lock(&data->lock);
	for_each_set_bit(bit, indio_dev->active_scan_mask,
			 indio_dev->masklength) {
		ret = kmx61_read_measurement(data, base, bit);
		if (ret < 0) {
			mutex_unlock(&data->lock);
			goto err;
		}
		buffer[i++] = ret;
	}
	mutex_unlock(&data->lock);

	iio_push_to_buffers(indio_dev, buffer);
err:
	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

static const char *kmx61_match_acpi_device(struct device *dev)
{
	const struct acpi_device_id *id;

	id = acpi_match_device(dev->driver->acpi_match_table, dev);
	if (!id)
		return NULL;
	return dev_name(dev);
}

static struct iio_dev *kmx61_indiodev_setup(struct kmx61_data *data,
					    const struct iio_info *info,
					    const struct iio_chan_spec *chan,
					    int num_channels,
					    const char *name)
{
	struct iio_dev *indio_dev;

	indio_dev = devm_iio_device_alloc(&data->client->dev, sizeof(data));
	if (!indio_dev)
		return ERR_PTR(-ENOMEM);

	kmx61_set_data(indio_dev, data);

	indio_dev->channels = chan;
	indio_dev->num_channels = num_channels;
	indio_dev->name = name;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = info;

	return indio_dev;
}

static struct iio_trigger *kmx61_trigger_setup(struct kmx61_data *data,
					       struct iio_dev *indio_dev,
					       const char *tag)
{
	struct iio_trigger *trig;
	int ret;

	trig = devm_iio_trigger_alloc(&data->client->dev,
				      "%s-%s-dev%d",
				      indio_dev->name,
				      tag,
				      iio_device_id(indio_dev));
	if (!trig)
		return ERR_PTR(-ENOMEM);

	trig->ops = &kmx61_trigger_ops;
	iio_trigger_set_drvdata(trig, indio_dev);

	ret = iio_trigger_register(trig);
	if (ret)
		return ERR_PTR(ret);

	return trig;
}

static int kmx61_probe(struct i2c_client *client,
		       const struct i2c_device_id *id)
{
	int ret;
	struct kmx61_data *data;
	const char *name = NULL;

	data = devm_kzalloc(&client->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	i2c_set_clientdata(client, data);
	data->client = client;

	mutex_init(&data->lock);

	if (id)
		name = id->name;
	else if (ACPI_HANDLE(&client->dev))
		name = kmx61_match_acpi_device(&client->dev);
	else
		return -ENODEV;

	data->acc_indio_dev =
		kmx61_indiodev_setup(data, &kmx61_acc_info,
				     kmx61_acc_channels,
				     ARRAY_SIZE(kmx61_acc_channels),
				     name);
	if (IS_ERR(data->acc_indio_dev))
		return PTR_ERR(data->acc_indio_dev);

	data->mag_indio_dev =
		kmx61_indiodev_setup(data, &kmx61_mag_info,
				     kmx61_mag_channels,
				     ARRAY_SIZE(kmx61_mag_channels),
				     name);
	if (IS_ERR(data->mag_indio_dev))
		return PTR_ERR(data->mag_indio_dev);

	ret = kmx61_chip_init(data);
	if (ret < 0)
		return ret;

	if (client->irq > 0) {
		ret = devm_request_threaded_irq(&client->dev, client->irq,
						kmx61_data_rdy_trig_poll,
						kmx61_event_handler,
						IRQF_TRIGGER_RISING,
						KMX61_IRQ_NAME,
						data);
		if (ret)
			goto err_chip_uninit;

		data->acc_dready_trig =
			kmx61_trigger_setup(data, data->acc_indio_dev,
					    "dready");
		if (IS_ERR(data->acc_dready_trig)) {
			ret = PTR_ERR(data->acc_dready_trig);
			goto err_chip_uninit;
		}

		data->mag_dready_trig =
			kmx61_trigger_setup(data, data->mag_indio_dev,
					    "dready");
		if (IS_ERR(data->mag_dready_trig)) {
			ret = PTR_ERR(data->mag_dready_trig);
			goto err_trigger_unregister_acc_dready;
		}

		data->motion_trig =
			kmx61_trigger_setup(data, data->acc_indio_dev,
					    "any-motion");
		if (IS_ERR(data->motion_trig)) {
			ret = PTR_ERR(data->motion_trig);
			goto err_trigger_unregister_mag_dready;
		}

		ret = iio_triggered_buffer_setup(data->acc_indio_dev,
						 &iio_pollfunc_store_time,
						 kmx61_trigger_handler,
						 NULL);
		if (ret < 0) {
			dev_err(&data->client->dev,
				"Failed to setup acc triggered buffer\n");
			goto err_trigger_unregister_motion;
		}

		ret = iio_triggered_buffer_setup(data->mag_indio_dev,
						 &iio_pollfunc_store_time,
						 kmx61_trigger_handler,
						 NULL);
		if (ret < 0) {
			dev_err(&data->client->dev,
				"Failed to setup mag triggered buffer\n");
			goto err_buffer_cleanup_acc;
		}
	}

	ret = pm_runtime_set_active(&client->dev);
	if (ret < 0)
		goto err_buffer_cleanup_mag;

	pm_runtime_enable(&client->dev);
	pm_runtime_set_autosuspend_delay(&client->dev, KMX61_SLEEP_DELAY_MS);
	pm_runtime_use_autosuspend(&client->dev);

	ret = iio_device_register(data->acc_indio_dev);
	if (ret < 0) {
		dev_err(&client->dev, "Failed to register acc iio device\n");
		goto err_buffer_cleanup_mag;
	}

	ret = iio_device_register(data->mag_indio_dev);
	if (ret < 0) {
		dev_err(&client->dev, "Failed to register mag iio device\n");
		goto err_iio_unregister_acc;
	}

	return 0;

err_iio_unregister_acc:
	iio_device_unregister(data->acc_indio_dev);
err_buffer_cleanup_mag:
	if (client->irq > 0)
		iio_triggered_buffer_cleanup(data->mag_indio_dev);
err_buffer_cleanup_acc:
	if (client->irq > 0)
		iio_triggered_buffer_cleanup(data->acc_indio_dev);
err_trigger_unregister_motion:
	iio_trigger_unregister(data->motion_trig);
err_trigger_unregister_mag_dready:
	iio_trigger_unregister(data->mag_dready_trig);
err_trigger_unregister_acc_dready:
	iio_trigger_unregister(data->acc_dready_trig);
err_chip_uninit:
	kmx61_set_mode(data, KMX61_ALL_STBY, KMX61_ACC | KMX61_MAG, true);
	return ret;
}

static int kmx61_remove(struct i2c_client *client)
{
	struct kmx61_data *data = i2c_get_clientdata(client);

	iio_device_unregister(data->acc_indio_dev);
	iio_device_unregister(data->mag_indio_dev);

	pm_runtime_disable(&client->dev);
	pm_runtime_set_suspended(&client->dev);

	if (client->irq > 0) {
		iio_triggered_buffer_cleanup(data->acc_indio_dev);
		iio_triggered_buffer_cleanup(data->mag_indio_dev);
		iio_trigger_unregister(data->acc_dready_trig);
		iio_trigger_unregister(data->mag_dready_trig);
		iio_trigger_unregister(data->motion_trig);
	}

	mutex_lock(&data->lock);
	kmx61_set_mode(data, KMX61_ALL_STBY, KMX61_ACC | KMX61_MAG, true);
	mutex_unlock(&data->lock);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int kmx61_suspend(struct device *dev)
{
	int ret;
	struct kmx61_data *data = i2c_get_clientdata(to_i2c_client(dev));

	mutex_lock(&data->lock);
	ret = kmx61_set_mode(data, KMX61_ALL_STBY, KMX61_ACC | KMX61_MAG,
			     false);
	mutex_unlock(&data->lock);

	return ret;
}

static int kmx61_resume(struct device *dev)
{
	u8 stby = 0;
	struct kmx61_data *data = i2c_get_clientdata(to_i2c_client(dev));

	if (data->acc_stby)
		stby |= KMX61_ACC_STBY_BIT;
	if (data->mag_stby)
		stby |= KMX61_MAG_STBY_BIT;

	return kmx61_set_mode(data, stby, KMX61_ACC | KMX61_MAG, true);
}
#endif

#ifdef CONFIG_PM
static int kmx61_runtime_suspend(struct device *dev)
{
	struct kmx61_data *data = i2c_get_clientdata(to_i2c_client(dev));
	int ret;

	mutex_lock(&data->lock);
	ret = kmx61_set_mode(data, KMX61_ALL_STBY, KMX61_ACC | KMX61_MAG, true);
	mutex_unlock(&data->lock);

	return ret;
}

static int kmx61_runtime_resume(struct device *dev)
{
	struct kmx61_data *data = i2c_get_clientdata(to_i2c_client(dev));
	u8 stby = 0;

	if (!data->acc_ps)
		stby |= KMX61_ACC_STBY_BIT;
	if (!data->mag_ps)
		stby |= KMX61_MAG_STBY_BIT;

	return kmx61_set_mode(data, stby, KMX61_ACC | KMX61_MAG, true);
}
#endif

static const struct dev_pm_ops kmx61_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(kmx61_suspend, kmx61_resume)
	SET_RUNTIME_PM_OPS(kmx61_runtime_suspend, kmx61_runtime_resume, NULL)
};

static const struct acpi_device_id kmx61_acpi_match[] = {
	{"KMX61021", 0},
	{}
};

MODULE_DEVICE_TABLE(acpi, kmx61_acpi_match);

static const struct i2c_device_id kmx61_id[] = {
	{"kmx611021", 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, kmx61_id);

static struct i2c_driver kmx61_driver = {
	.driver = {
		.name = KMX61_DRV_NAME,
		.acpi_match_table = ACPI_PTR(kmx61_acpi_match),
		.pm = &kmx61_pm_ops,
	},
	.probe		= kmx61_probe,
	.remove		= kmx61_remove,
	.id_table	= kmx61_id,
};

module_i2c_driver(kmx61_driver);

MODULE_AUTHOR("Daniel Baluta <daniel.baluta@intel.com>");
MODULE_DESCRIPTION("KMX61 accelerometer/magnetometer driver");
MODULE_LICENSE("GPL v2");
