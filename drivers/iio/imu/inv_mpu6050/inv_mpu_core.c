/*
* Copyright (C) 2012 Invensense, Inc.
*
* This software is licensed under the terms of the GNU General Public
* License version 2, as published by the Free Software Foundation, and
* may be copied, distributed, and modified under those terms.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*/

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/sysfs.h>
#include <linux/jiffies.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/kfifo.h>
#include <linux/spinlock.h>
#include <linux/iio/iio.h>
#include <linux/acpi.h>
#include "inv_mpu_iio.h"

/*
 * this is the gyro scale translated from dynamic range plus/minus
 * {250, 500, 1000, 2000} to rad/s
 */
static const int gyro_scale_6050[] = {133090, 266181, 532362, 1064724};

/*
 * this is the accel scale translated from dynamic range plus/minus
 * {2, 4, 8, 16} to m/s^2
 */
static const int accel_scale[] = {598, 1196, 2392, 4785};

static const struct inv_mpu6050_reg_map reg_set_6500 = {
	.sample_rate_div	= INV_MPU6050_REG_SAMPLE_RATE_DIV,
	.lpf                    = INV_MPU6050_REG_CONFIG,
	.accel_lpf              = INV_MPU6500_REG_ACCEL_CONFIG_2,
	.user_ctrl              = INV_MPU6050_REG_USER_CTRL,
	.fifo_en                = INV_MPU6050_REG_FIFO_EN,
	.gyro_config            = INV_MPU6050_REG_GYRO_CONFIG,
	.accl_config            = INV_MPU6050_REG_ACCEL_CONFIG,
	.fifo_count_h           = INV_MPU6050_REG_FIFO_COUNT_H,
	.fifo_r_w               = INV_MPU6050_REG_FIFO_R_W,
	.raw_gyro               = INV_MPU6050_REG_RAW_GYRO,
	.raw_accl               = INV_MPU6050_REG_RAW_ACCEL,
	.temperature            = INV_MPU6050_REG_TEMPERATURE,
	.int_enable             = INV_MPU6050_REG_INT_ENABLE,
	.pwr_mgmt_1             = INV_MPU6050_REG_PWR_MGMT_1,
	.pwr_mgmt_2             = INV_MPU6050_REG_PWR_MGMT_2,
	.int_pin_cfg		= INV_MPU6050_REG_INT_PIN_CFG,
	.accl_offset		= INV_MPU6500_REG_ACCEL_OFFSET,
	.gyro_offset		= INV_MPU6050_REG_GYRO_OFFSET,
};

static const struct inv_mpu6050_reg_map reg_set_6050 = {
	.sample_rate_div	= INV_MPU6050_REG_SAMPLE_RATE_DIV,
	.lpf                    = INV_MPU6050_REG_CONFIG,
	.user_ctrl              = INV_MPU6050_REG_USER_CTRL,
	.fifo_en                = INV_MPU6050_REG_FIFO_EN,
	.gyro_config            = INV_MPU6050_REG_GYRO_CONFIG,
	.accl_config            = INV_MPU6050_REG_ACCEL_CONFIG,
	.fifo_count_h           = INV_MPU6050_REG_FIFO_COUNT_H,
	.fifo_r_w               = INV_MPU6050_REG_FIFO_R_W,
	.raw_gyro               = INV_MPU6050_REG_RAW_GYRO,
	.raw_accl               = INV_MPU6050_REG_RAW_ACCEL,
	.temperature            = INV_MPU6050_REG_TEMPERATURE,
	.int_enable             = INV_MPU6050_REG_INT_ENABLE,
	.pwr_mgmt_1             = INV_MPU6050_REG_PWR_MGMT_1,
	.pwr_mgmt_2             = INV_MPU6050_REG_PWR_MGMT_2,
	.int_pin_cfg		= INV_MPU6050_REG_INT_PIN_CFG,
	.accl_offset		= INV_MPU6050_REG_ACCEL_OFFSET,
	.gyro_offset		= INV_MPU6050_REG_GYRO_OFFSET,
};

static const struct inv_mpu6050_chip_config chip_config_6050 = {
	.fsr = INV_MPU6050_FSR_2000DPS,
	.lpf = INV_MPU6050_FILTER_20HZ,
	.fifo_rate = INV_MPU6050_INIT_FIFO_RATE,
	.gyro_fifo_enable = false,
	.accl_fifo_enable = false,
	.accl_fs = INV_MPU6050_FS_02G,
};

/* Indexed by enum inv_devices */
static const struct inv_mpu6050_hw hw_info[] = {
	{
		.whoami = INV_MPU6050_WHOAMI_VALUE,
		.name = "MPU6050",
		.reg = &reg_set_6050,
		.config = &chip_config_6050,
	},
	{
		.whoami = INV_MPU6500_WHOAMI_VALUE,
		.name = "MPU6500",
		.reg = &reg_set_6500,
		.config = &chip_config_6050,
	},
	{
		.whoami = INV_MPU6000_WHOAMI_VALUE,
		.name = "MPU6000",
		.reg = &reg_set_6050,
		.config = &chip_config_6050,
	},
	{
		.whoami = INV_MPU9150_WHOAMI_VALUE,
		.name = "MPU9150",
		.reg = &reg_set_6050,
		.config = &chip_config_6050,
	},
	{
		.whoami = INV_MPU9250_WHOAMI_VALUE,
		.name = "MPU9250",
		.reg = &reg_set_6500,
		.config = &chip_config_6050,
	},
	{
		.whoami = INV_ICM20608_WHOAMI_VALUE,
		.name = "ICM20608",
		.reg = &reg_set_6500,
		.config = &chip_config_6050,
	},
};

int inv_mpu6050_switch_engine(struct inv_mpu6050_state *st, bool en, u32 mask)
{
	unsigned int d, mgmt_1;
	int result;
	/*
	 * switch clock needs to be careful. Only when gyro is on, can
	 * clock source be switched to gyro. Otherwise, it must be set to
	 * internal clock
	 */
	if (mask == INV_MPU6050_BIT_PWR_GYRO_STBY) {
		result = regmap_read(st->map, st->reg->pwr_mgmt_1, &mgmt_1);
		if (result)
			return result;

		mgmt_1 &= ~INV_MPU6050_BIT_CLK_MASK;
	}

	if ((mask == INV_MPU6050_BIT_PWR_GYRO_STBY) && (!en)) {
		/*
		 * turning off gyro requires switch to internal clock first.
		 * Then turn off gyro engine
		 */
		mgmt_1 |= INV_CLK_INTERNAL;
		result = regmap_write(st->map, st->reg->pwr_mgmt_1, mgmt_1);
		if (result)
			return result;
	}

	result = regmap_read(st->map, st->reg->pwr_mgmt_2, &d);
	if (result)
		return result;
	if (en)
		d &= ~mask;
	else
		d |= mask;
	result = regmap_write(st->map, st->reg->pwr_mgmt_2, d);
	if (result)
		return result;

	if (en) {
		/* Wait for output stabilize */
		msleep(INV_MPU6050_TEMP_UP_TIME);
		if (mask == INV_MPU6050_BIT_PWR_GYRO_STBY) {
			/* switch internal clock to PLL */
			mgmt_1 |= INV_CLK_PLL;
			result = regmap_write(st->map,
					      st->reg->pwr_mgmt_1, mgmt_1);
			if (result)
				return result;
		}
	}

	return 0;
}

int inv_mpu6050_set_power_itg(struct inv_mpu6050_state *st, bool power_on)
{
	int result = 0;

	if (power_on) {
		if (!st->powerup_count)
			result = regmap_write(st->map, st->reg->pwr_mgmt_1, 0);
		if (!result)
			st->powerup_count++;
	} else {
		st->powerup_count--;
		if (!st->powerup_count)
			result = regmap_write(st->map, st->reg->pwr_mgmt_1,
					      INV_MPU6050_BIT_SLEEP);
	}

	if (result)
		return result;

	if (power_on)
		usleep_range(INV_MPU6050_REG_UP_TIME_MIN,
			     INV_MPU6050_REG_UP_TIME_MAX);

	return 0;
}
EXPORT_SYMBOL_GPL(inv_mpu6050_set_power_itg);

/**
 *  inv_mpu6050_set_lpf_regs() - set low pass filter registers, chip dependent
 *
 *  MPU60xx/MPU9150 use only 1 register for accelerometer + gyroscope
 *  MPU6500 and above have a dedicated register for accelerometer
 */
static int inv_mpu6050_set_lpf_regs(struct inv_mpu6050_state *st,
				    enum inv_mpu6050_filter_e val)
{
	int result;

	result = regmap_write(st->map, st->reg->lpf, val);
	if (result)
		return result;

	switch (st->chip_type) {
	case INV_MPU6050:
	case INV_MPU6000:
	case INV_MPU9150:
		/* old chips, nothing to do */
		result = 0;
		break;
	default:
		/* set accel lpf */
		result = regmap_write(st->map, st->reg->accel_lpf, val);
		break;
	}

	return result;
}

/**
 *  inv_mpu6050_init_config() - Initialize hardware, disable FIFO.
 *
 *  Initial configuration:
 *  FSR: ± 2000DPS
 *  DLPF: 20Hz
 *  FIFO rate: 50Hz
 *  Clock source: Gyro PLL
 */
static int inv_mpu6050_init_config(struct iio_dev *indio_dev)
{
	int result;
	u8 d;
	struct inv_mpu6050_state *st = iio_priv(indio_dev);

	result = inv_mpu6050_set_power_itg(st, true);
	if (result)
		return result;
	d = (INV_MPU6050_FSR_2000DPS << INV_MPU6050_GYRO_CONFIG_FSR_SHIFT);
	result = regmap_write(st->map, st->reg->gyro_config, d);
	if (result)
		return result;

	result = inv_mpu6050_set_lpf_regs(st, INV_MPU6050_FILTER_20HZ);
	if (result)
		return result;

	d = INV_MPU6050_ONE_K_HZ / INV_MPU6050_INIT_FIFO_RATE - 1;
	result = regmap_write(st->map, st->reg->sample_rate_div, d);
	if (result)
		return result;

	d = (INV_MPU6050_FS_02G << INV_MPU6050_ACCL_CONFIG_FSR_SHIFT);
	result = regmap_write(st->map, st->reg->accl_config, d);
	if (result)
		return result;

	memcpy(&st->chip_config, hw_info[st->chip_type].config,
	       sizeof(struct inv_mpu6050_chip_config));
	result = inv_mpu6050_set_power_itg(st, false);

	return result;
}

static int inv_mpu6050_sensor_set(struct inv_mpu6050_state  *st, int reg,
				int axis, int val)
{
	int ind, result;
	__be16 d = cpu_to_be16(val);

	ind = (axis - IIO_MOD_X) * 2;
	result = regmap_bulk_write(st->map, reg + ind, (u8 *)&d, 2);
	if (result)
		return -EINVAL;

	return 0;
}

static int inv_mpu6050_sensor_show(struct inv_mpu6050_state  *st, int reg,
				   int axis, int *val)
{
	int ind, result;
	__be16 d;

	ind = (axis - IIO_MOD_X) * 2;
	result = regmap_bulk_read(st->map, reg + ind, (u8 *)&d, 2);
	if (result)
		return -EINVAL;
	*val = (short)be16_to_cpup(&d);

	return IIO_VAL_INT;
}

static int
inv_mpu6050_read_raw(struct iio_dev *indio_dev,
		     struct iio_chan_spec const *chan,
		     int *val, int *val2, long mask)
{
	struct inv_mpu6050_state  *st = iio_priv(indio_dev);
	int ret = 0;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
	{
		int result;

		ret = IIO_VAL_INT;
		mutex_lock(&st->lock);
		result = iio_device_claim_direct_mode(indio_dev);
		if (result)
			goto error_read_raw_unlock;
		result = inv_mpu6050_set_power_itg(st, true);
		if (result)
			goto error_read_raw_release;
		switch (chan->type) {
		case IIO_ANGL_VEL:
			result = inv_mpu6050_switch_engine(st, true,
					INV_MPU6050_BIT_PWR_GYRO_STBY);
			if (result)
				goto error_read_raw_power_off;
			ret = inv_mpu6050_sensor_show(st, st->reg->raw_gyro,
						      chan->channel2, val);
			result = inv_mpu6050_switch_engine(st, false,
					INV_MPU6050_BIT_PWR_GYRO_STBY);
			if (result)
				goto error_read_raw_power_off;
			break;
		case IIO_ACCEL:
			result = inv_mpu6050_switch_engine(st, true,
					INV_MPU6050_BIT_PWR_ACCL_STBY);
			if (result)
				goto error_read_raw_power_off;
			ret = inv_mpu6050_sensor_show(st, st->reg->raw_accl,
						      chan->channel2, val);
			result = inv_mpu6050_switch_engine(st, false,
					INV_MPU6050_BIT_PWR_ACCL_STBY);
			if (result)
				goto error_read_raw_power_off;
			break;
		case IIO_TEMP:
			/* wait for stablization */
			msleep(INV_MPU6050_SENSOR_UP_TIME);
			ret = inv_mpu6050_sensor_show(st, st->reg->temperature,
						IIO_MOD_X, val);
			break;
		default:
			ret = -EINVAL;
			break;
		}
error_read_raw_power_off:
		result |= inv_mpu6050_set_power_itg(st, false);
error_read_raw_release:
		iio_device_release_direct_mode(indio_dev);
error_read_raw_unlock:
		mutex_unlock(&st->lock);
		if (result)
			return result;

		return ret;
	}
	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_ANGL_VEL:
			mutex_lock(&st->lock);
			*val  = 0;
			*val2 = gyro_scale_6050[st->chip_config.fsr];
			mutex_unlock(&st->lock);

			return IIO_VAL_INT_PLUS_NANO;
		case IIO_ACCEL:
			mutex_lock(&st->lock);
			*val = 0;
			*val2 = accel_scale[st->chip_config.accl_fs];
			mutex_unlock(&st->lock);

			return IIO_VAL_INT_PLUS_MICRO;
		case IIO_TEMP:
			*val = 0;
			*val2 = INV_MPU6050_TEMP_SCALE;

			return IIO_VAL_INT_PLUS_MICRO;
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_OFFSET:
		switch (chan->type) {
		case IIO_TEMP:
			*val = INV_MPU6050_TEMP_OFFSET;

			return IIO_VAL_INT;
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_CALIBBIAS:
		switch (chan->type) {
		case IIO_ANGL_VEL:
			mutex_lock(&st->lock);
			ret = inv_mpu6050_sensor_show(st, st->reg->gyro_offset,
						chan->channel2, val);
			mutex_unlock(&st->lock);
			return IIO_VAL_INT;
		case IIO_ACCEL:
			mutex_lock(&st->lock);
			ret = inv_mpu6050_sensor_show(st, st->reg->accl_offset,
						chan->channel2, val);
			mutex_unlock(&st->lock);
			return IIO_VAL_INT;

		default:
			return -EINVAL;
		}
	default:
		return -EINVAL;
	}
}

static int inv_mpu6050_write_gyro_scale(struct inv_mpu6050_state *st, int val)
{
	int result, i;
	u8 d;

	for (i = 0; i < ARRAY_SIZE(gyro_scale_6050); ++i) {
		if (gyro_scale_6050[i] == val) {
			d = (i << INV_MPU6050_GYRO_CONFIG_FSR_SHIFT);
			result = regmap_write(st->map, st->reg->gyro_config, d);
			if (result)
				return result;

			st->chip_config.fsr = i;
			return 0;
		}
	}

	return -EINVAL;
}

static int inv_write_raw_get_fmt(struct iio_dev *indio_dev,
				 struct iio_chan_spec const *chan, long mask)
{
	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_ANGL_VEL:
			return IIO_VAL_INT_PLUS_NANO;
		default:
			return IIO_VAL_INT_PLUS_MICRO;
		}
	default:
		return IIO_VAL_INT_PLUS_MICRO;
	}

	return -EINVAL;
}

static int inv_mpu6050_write_accel_scale(struct inv_mpu6050_state *st, int val)
{
	int result, i;
	u8 d;

	for (i = 0; i < ARRAY_SIZE(accel_scale); ++i) {
		if (accel_scale[i] == val) {
			d = (i << INV_MPU6050_ACCL_CONFIG_FSR_SHIFT);
			result = regmap_write(st->map, st->reg->accl_config, d);
			if (result)
				return result;

			st->chip_config.accl_fs = i;
			return 0;
		}
	}

	return -EINVAL;
}

static int inv_mpu6050_write_raw(struct iio_dev *indio_dev,
				 struct iio_chan_spec const *chan,
				 int val, int val2, long mask)
{
	struct inv_mpu6050_state  *st = iio_priv(indio_dev);
	int result;

	mutex_lock(&st->lock);
	/*
	 * we should only update scale when the chip is disabled, i.e.
	 * not running
	 */
	result = iio_device_claim_direct_mode(indio_dev);
	if (result)
		goto error_write_raw_unlock;
	result = inv_mpu6050_set_power_itg(st, true);
	if (result)
		goto error_write_raw_release;

	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_ANGL_VEL:
			result = inv_mpu6050_write_gyro_scale(st, val2);
			break;
		case IIO_ACCEL:
			result = inv_mpu6050_write_accel_scale(st, val2);
			break;
		default:
			result = -EINVAL;
			break;
		}
		break;
	case IIO_CHAN_INFO_CALIBBIAS:
		switch (chan->type) {
		case IIO_ANGL_VEL:
			result = inv_mpu6050_sensor_set(st,
							st->reg->gyro_offset,
							chan->channel2, val);
			break;
		case IIO_ACCEL:
			result = inv_mpu6050_sensor_set(st,
							st->reg->accl_offset,
							chan->channel2, val);
			break;
		default:
			result = -EINVAL;
			break;
		}
		break;
	default:
		result = -EINVAL;
		break;
	}

	result |= inv_mpu6050_set_power_itg(st, false);
error_write_raw_release:
	iio_device_release_direct_mode(indio_dev);
error_write_raw_unlock:
	mutex_unlock(&st->lock);

	return result;
}

/**
 *  inv_mpu6050_set_lpf() - set low pass filer based on fifo rate.
 *
 *                  Based on the Nyquist principle, the sampling rate must
 *                  exceed twice of the bandwidth of the signal, or there
 *                  would be alising. This function basically search for the
 *                  correct low pass parameters based on the fifo rate, e.g,
 *                  sampling frequency.
 *
 *  lpf is set automatically when setting sampling rate to avoid any aliases.
 */
static int inv_mpu6050_set_lpf(struct inv_mpu6050_state *st, int rate)
{
	static const int hz[] = {188, 98, 42, 20, 10, 5};
	static const int d[] = {
		INV_MPU6050_FILTER_188HZ, INV_MPU6050_FILTER_98HZ,
		INV_MPU6050_FILTER_42HZ, INV_MPU6050_FILTER_20HZ,
		INV_MPU6050_FILTER_10HZ, INV_MPU6050_FILTER_5HZ
	};
	int i, h, result;
	u8 data;

	h = (rate >> 1);
	i = 0;
	while ((h < hz[i]) && (i < ARRAY_SIZE(d) - 1))
		i++;
	data = d[i];
	result = inv_mpu6050_set_lpf_regs(st, data);
	if (result)
		return result;
	st->chip_config.lpf = data;

	return 0;
}

/**
 * inv_mpu6050_fifo_rate_store() - Set fifo rate.
 */
static ssize_t
inv_mpu6050_fifo_rate_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	s32 fifo_rate;
	u8 d;
	int result;
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct inv_mpu6050_state *st = iio_priv(indio_dev);

	if (kstrtoint(buf, 10, &fifo_rate))
		return -EINVAL;
	if (fifo_rate < INV_MPU6050_MIN_FIFO_RATE ||
	    fifo_rate > INV_MPU6050_MAX_FIFO_RATE)
		return -EINVAL;

	mutex_lock(&st->lock);
	if (fifo_rate == st->chip_config.fifo_rate) {
		result = 0;
		goto fifo_rate_fail_unlock;
	}
	result = iio_device_claim_direct_mode(indio_dev);
	if (result)
		goto fifo_rate_fail_unlock;
	result = inv_mpu6050_set_power_itg(st, true);
	if (result)
		goto fifo_rate_fail_release;

	d = INV_MPU6050_ONE_K_HZ / fifo_rate - 1;
	result = regmap_write(st->map, st->reg->sample_rate_div, d);
	if (result)
		goto fifo_rate_fail_power_off;
	st->chip_config.fifo_rate = fifo_rate;

	result = inv_mpu6050_set_lpf(st, fifo_rate);
	if (result)
		goto fifo_rate_fail_power_off;

fifo_rate_fail_power_off:
	result |= inv_mpu6050_set_power_itg(st, false);
fifo_rate_fail_release:
	iio_device_release_direct_mode(indio_dev);
fifo_rate_fail_unlock:
	mutex_unlock(&st->lock);
	if (result)
		return result;

	return count;
}

/**
 * inv_fifo_rate_show() - Get the current sampling rate.
 */
static ssize_t
inv_fifo_rate_show(struct device *dev, struct device_attribute *attr,
		   char *buf)
{
	struct inv_mpu6050_state *st = iio_priv(dev_to_iio_dev(dev));
	unsigned fifo_rate;

	mutex_lock(&st->lock);
	fifo_rate = st->chip_config.fifo_rate;
	mutex_unlock(&st->lock);

	return scnprintf(buf, PAGE_SIZE, "%u\n", fifo_rate);
}

/**
 * inv_attr_show() - calling this function will show current
 *                    parameters.
 *
 * Deprecated in favor of IIO mounting matrix API.
 *
 * See inv_get_mount_matrix()
 */
static ssize_t inv_attr_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct inv_mpu6050_state *st = iio_priv(dev_to_iio_dev(dev));
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);
	s8 *m;

	switch (this_attr->address) {
	/*
	 * In MPU6050, the two matrix are the same because gyro and accel
	 * are integrated in one chip
	 */
	case ATTR_GYRO_MATRIX:
	case ATTR_ACCL_MATRIX:
		m = st->plat_data.orientation;

		return scnprintf(buf, PAGE_SIZE,
			"%d, %d, %d; %d, %d, %d; %d, %d, %d\n",
			m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7], m[8]);
	default:
		return -EINVAL;
	}
}

/**
 * inv_mpu6050_validate_trigger() - validate_trigger callback for invensense
 *                                  MPU6050 device.
 * @indio_dev: The IIO device
 * @trig: The new trigger
 *
 * Returns: 0 if the 'trig' matches the trigger registered by the MPU6050
 * device, -EINVAL otherwise.
 */
static int inv_mpu6050_validate_trigger(struct iio_dev *indio_dev,
					struct iio_trigger *trig)
{
	struct inv_mpu6050_state *st = iio_priv(indio_dev);

	if (st->trig != trig)
		return -EINVAL;

	return 0;
}

static const struct iio_mount_matrix *
inv_get_mount_matrix(const struct iio_dev *indio_dev,
		     const struct iio_chan_spec *chan)
{
	return &((struct inv_mpu6050_state *)iio_priv(indio_dev))->orientation;
}

static const struct iio_chan_spec_ext_info inv_ext_info[] = {
	IIO_MOUNT_MATRIX(IIO_SHARED_BY_TYPE, inv_get_mount_matrix),
	{ },
};

#define INV_MPU6050_CHAN(_type, _channel2, _index)                    \
	{                                                             \
		.type = _type,                                        \
		.modified = 1,                                        \
		.channel2 = _channel2,                                \
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE), \
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |	      \
				      BIT(IIO_CHAN_INFO_CALIBBIAS),   \
		.scan_index = _index,                                 \
		.scan_type = {                                        \
				.sign = 's',                          \
				.realbits = 16,                       \
				.storagebits = 16,                    \
				.shift = 0,                           \
				.endianness = IIO_BE,                 \
			     },                                       \
		.ext_info = inv_ext_info,                             \
	}

static const struct iio_chan_spec inv_mpu_channels[] = {
	IIO_CHAN_SOFT_TIMESTAMP(INV_MPU6050_SCAN_TIMESTAMP),
	/*
	 * Note that temperature should only be via polled reading only,
	 * not the final scan elements output.
	 */
	{
		.type = IIO_TEMP,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW)
				| BIT(IIO_CHAN_INFO_OFFSET)
				| BIT(IIO_CHAN_INFO_SCALE),
		.scan_index = -1,
	},
	INV_MPU6050_CHAN(IIO_ANGL_VEL, IIO_MOD_X, INV_MPU6050_SCAN_GYRO_X),
	INV_MPU6050_CHAN(IIO_ANGL_VEL, IIO_MOD_Y, INV_MPU6050_SCAN_GYRO_Y),
	INV_MPU6050_CHAN(IIO_ANGL_VEL, IIO_MOD_Z, INV_MPU6050_SCAN_GYRO_Z),

	INV_MPU6050_CHAN(IIO_ACCEL, IIO_MOD_X, INV_MPU6050_SCAN_ACCL_X),
	INV_MPU6050_CHAN(IIO_ACCEL, IIO_MOD_Y, INV_MPU6050_SCAN_ACCL_Y),
	INV_MPU6050_CHAN(IIO_ACCEL, IIO_MOD_Z, INV_MPU6050_SCAN_ACCL_Z),
};

/* constant IIO attribute */
static IIO_CONST_ATTR_SAMP_FREQ_AVAIL("10 20 50 100 200 500");
static IIO_CONST_ATTR(in_anglvel_scale_available,
					  "0.000133090 0.000266181 0.000532362 0.001064724");
static IIO_CONST_ATTR(in_accel_scale_available,
					  "0.000598 0.001196 0.002392 0.004785");
static IIO_DEV_ATTR_SAMP_FREQ(S_IRUGO | S_IWUSR, inv_fifo_rate_show,
	inv_mpu6050_fifo_rate_store);

/* Deprecated: kept for userspace backward compatibility. */
static IIO_DEVICE_ATTR(in_gyro_matrix, S_IRUGO, inv_attr_show, NULL,
	ATTR_GYRO_MATRIX);
static IIO_DEVICE_ATTR(in_accel_matrix, S_IRUGO, inv_attr_show, NULL,
	ATTR_ACCL_MATRIX);

static struct attribute *inv_attributes[] = {
	&iio_dev_attr_in_gyro_matrix.dev_attr.attr,  /* deprecated */
	&iio_dev_attr_in_accel_matrix.dev_attr.attr, /* deprecated */
	&iio_dev_attr_sampling_frequency.dev_attr.attr,
	&iio_const_attr_sampling_frequency_available.dev_attr.attr,
	&iio_const_attr_in_accel_scale_available.dev_attr.attr,
	&iio_const_attr_in_anglvel_scale_available.dev_attr.attr,
	NULL,
};

static const struct attribute_group inv_attribute_group = {
	.attrs = inv_attributes
};

static const struct iio_info mpu_info = {
	.read_raw = &inv_mpu6050_read_raw,
	.write_raw = &inv_mpu6050_write_raw,
	.write_raw_get_fmt = &inv_write_raw_get_fmt,
	.attrs = &inv_attribute_group,
	.validate_trigger = inv_mpu6050_validate_trigger,
};

/**
 *  inv_check_and_setup_chip() - check and setup chip.
 */
static int inv_check_and_setup_chip(struct inv_mpu6050_state *st)
{
	int result;
	unsigned int regval;
	int i;

	st->hw  = &hw_info[st->chip_type];
	st->reg = hw_info[st->chip_type].reg;

	/* check chip self-identification */
	result = regmap_read(st->map, INV_MPU6050_REG_WHOAMI, &regval);
	if (result)
		return result;
	if (regval != st->hw->whoami) {
		/* check whoami against all possible values */
		for (i = 0; i < INV_NUM_PARTS; ++i) {
			if (regval == hw_info[i].whoami) {
				dev_warn(regmap_get_device(st->map),
					"whoami mismatch got %#02x (%s)"
					"expected %#02hhx (%s)\n",
					regval, hw_info[i].name,
					st->hw->whoami, st->hw->name);
				break;
			}
		}
		if (i >= INV_NUM_PARTS) {
			dev_err(regmap_get_device(st->map),
				"invalid whoami %#02x expected %#02hhx (%s)\n",
				regval, st->hw->whoami, st->hw->name);
			return -ENODEV;
		}
	}

	/* reset to make sure previous state are not there */
	result = regmap_write(st->map, st->reg->pwr_mgmt_1,
			      INV_MPU6050_BIT_H_RESET);
	if (result)
		return result;
	msleep(INV_MPU6050_POWER_UP_TIME);

	/*
	 * toggle power state. After reset, the sleep bit could be on
	 * or off depending on the OTP settings. Toggling power would
	 * make it in a definite state as well as making the hardware
	 * state align with the software state
	 */
	result = inv_mpu6050_set_power_itg(st, false);
	if (result)
		return result;
	result = inv_mpu6050_set_power_itg(st, true);
	if (result)
		return result;

	result = inv_mpu6050_switch_engine(st, false,
					   INV_MPU6050_BIT_PWR_ACCL_STBY);
	if (result)
		return result;
	result = inv_mpu6050_switch_engine(st, false,
					   INV_MPU6050_BIT_PWR_GYRO_STBY);
	if (result)
		return result;

	return 0;
}

int inv_mpu_core_probe(struct regmap *regmap, int irq, const char *name,
		int (*inv_mpu_bus_setup)(struct iio_dev *), int chip_type)
{
	struct inv_mpu6050_state *st;
	struct iio_dev *indio_dev;
	struct inv_mpu6050_platform_data *pdata;
	struct device *dev = regmap_get_device(regmap);
	int result;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;

	BUILD_BUG_ON(ARRAY_SIZE(hw_info) != INV_NUM_PARTS);
	if (chip_type < 0 || chip_type >= INV_NUM_PARTS) {
		dev_err(dev, "Bad invensense chip_type=%d name=%s\n",
				chip_type, name);
		return -ENODEV;
	}
	st = iio_priv(indio_dev);
	mutex_init(&st->lock);
	st->chip_type = chip_type;
	st->powerup_count = 0;
	st->irq = irq;
	st->map = regmap;

	pdata = dev_get_platdata(dev);
	if (!pdata) {
		result = of_iio_read_mount_matrix(dev, "mount-matrix",
						  &st->orientation);
		if (result) {
			dev_err(dev, "Failed to retrieve mounting matrix %d\n",
				result);
			return result;
		}
	} else {
		st->plat_data = *pdata;
	}

	/* power is turned on inside check chip type*/
	result = inv_check_and_setup_chip(st);
	if (result)
		return result;

	if (inv_mpu_bus_setup)
		inv_mpu_bus_setup(indio_dev);

	result = inv_mpu6050_init_config(indio_dev);
	if (result) {
		dev_err(dev, "Could not initialize device.\n");
		return result;
	}

	dev_set_drvdata(dev, indio_dev);
	indio_dev->dev.parent = dev;
	/* name will be NULL when enumerated via ACPI */
	if (name)
		indio_dev->name = name;
	else
		indio_dev->name = dev_name(dev);
	indio_dev->channels = inv_mpu_channels;
	indio_dev->num_channels = ARRAY_SIZE(inv_mpu_channels);

	indio_dev->info = &mpu_info;
	indio_dev->modes = INDIO_BUFFER_TRIGGERED;

	result = iio_triggered_buffer_setup(indio_dev,
					    inv_mpu6050_irq_handler,
					    inv_mpu6050_read_fifo,
					    NULL);
	if (result) {
		dev_err(dev, "configure buffer fail %d\n", result);
		return result;
	}
	result = inv_mpu6050_probe_trigger(indio_dev);
	if (result) {
		dev_err(dev, "trigger probe fail %d\n", result);
		goto out_unreg_ring;
	}

	INIT_KFIFO(st->timestamps);
	spin_lock_init(&st->time_stamp_lock);
	result = iio_device_register(indio_dev);
	if (result) {
		dev_err(dev, "IIO register fail %d\n", result);
		goto out_remove_trigger;
	}

	return 0;

out_remove_trigger:
	inv_mpu6050_remove_trigger(st);
out_unreg_ring:
	iio_triggered_buffer_cleanup(indio_dev);
	return result;
}
EXPORT_SYMBOL_GPL(inv_mpu_core_probe);

int inv_mpu_core_remove(struct device  *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);

	iio_device_unregister(indio_dev);
	inv_mpu6050_remove_trigger(iio_priv(indio_dev));
	iio_triggered_buffer_cleanup(indio_dev);

	return 0;
}
EXPORT_SYMBOL_GPL(inv_mpu_core_remove);

#ifdef CONFIG_PM_SLEEP

static int inv_mpu_resume(struct device *dev)
{
	struct inv_mpu6050_state *st = iio_priv(dev_get_drvdata(dev));
	int result;

	mutex_lock(&st->lock);
	result = inv_mpu6050_set_power_itg(st, true);
	mutex_unlock(&st->lock);

	return result;
}

static int inv_mpu_suspend(struct device *dev)
{
	struct inv_mpu6050_state *st = iio_priv(dev_get_drvdata(dev));
	int result;

	mutex_lock(&st->lock);
	result = inv_mpu6050_set_power_itg(st, false);
	mutex_unlock(&st->lock);

	return result;
}
#endif /* CONFIG_PM_SLEEP */

SIMPLE_DEV_PM_OPS(inv_mpu_pmops, inv_mpu_suspend, inv_mpu_resume);
EXPORT_SYMBOL_GPL(inv_mpu_pmops);

MODULE_AUTHOR("Invensense Corporation");
MODULE_DESCRIPTION("Invensense device MPU6050 driver");
MODULE_LICENSE("GPL");
