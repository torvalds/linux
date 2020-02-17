// SPDX-License-Identifier: GPL-2.0-only
/*
* Copyright (C) 2012 Invensense, Inc.
*/

#include "inv_mpu_iio.h"

static void inv_scan_query_mpu6050(struct iio_dev *indio_dev)
{
	struct inv_mpu6050_state  *st = iio_priv(indio_dev);

	st->chip_config.gyro_fifo_enable =
		test_bit(INV_MPU6050_SCAN_GYRO_X,
			 indio_dev->active_scan_mask) ||
		test_bit(INV_MPU6050_SCAN_GYRO_Y,
			 indio_dev->active_scan_mask) ||
		test_bit(INV_MPU6050_SCAN_GYRO_Z,
			 indio_dev->active_scan_mask);

	st->chip_config.accl_fifo_enable =
		test_bit(INV_MPU6050_SCAN_ACCL_X,
			 indio_dev->active_scan_mask) ||
		test_bit(INV_MPU6050_SCAN_ACCL_Y,
			 indio_dev->active_scan_mask) ||
		test_bit(INV_MPU6050_SCAN_ACCL_Z,
			 indio_dev->active_scan_mask);

	st->chip_config.temp_fifo_enable =
		test_bit(INV_MPU6050_SCAN_TEMP, indio_dev->active_scan_mask);
}

static void inv_scan_query_mpu9x50(struct iio_dev *indio_dev)
{
	struct inv_mpu6050_state *st = iio_priv(indio_dev);

	inv_scan_query_mpu6050(indio_dev);

	/* no magnetometer if i2c auxiliary bus is used */
	if (st->magn_disabled)
		return;

	st->chip_config.magn_fifo_enable =
		test_bit(INV_MPU9X50_SCAN_MAGN_X,
			 indio_dev->active_scan_mask) ||
		test_bit(INV_MPU9X50_SCAN_MAGN_Y,
			 indio_dev->active_scan_mask) ||
		test_bit(INV_MPU9X50_SCAN_MAGN_Z,
			 indio_dev->active_scan_mask);
}

static void inv_scan_query(struct iio_dev *indio_dev)
{
	struct inv_mpu6050_state *st = iio_priv(indio_dev);

	switch (st->chip_type) {
	case INV_MPU9150:
	case INV_MPU9250:
	case INV_MPU9255:
		return inv_scan_query_mpu9x50(indio_dev);
	default:
		return inv_scan_query_mpu6050(indio_dev);
	}
}

static unsigned int inv_compute_skip_samples(const struct inv_mpu6050_state *st)
{
	unsigned int gyro_skip = 0;
	unsigned int magn_skip = 0;
	unsigned int skip_samples;

	/* gyro first sample is out of specs, skip it */
	if (st->chip_config.gyro_fifo_enable)
		gyro_skip = 1;

	/* mag first sample is always not ready, skip it */
	if (st->chip_config.magn_fifo_enable)
		magn_skip = 1;

	/* compute first samples to skip */
	skip_samples = gyro_skip;
	if (magn_skip > skip_samples)
		skip_samples = magn_skip;

	return skip_samples;
}

/**
 *  inv_mpu6050_set_enable() - enable chip functions.
 *  @indio_dev:	Device driver instance.
 *  @enable: enable/disable
 */
static int inv_mpu6050_set_enable(struct iio_dev *indio_dev, bool enable)
{
	struct inv_mpu6050_state *st = iio_priv(indio_dev);
	uint8_t d;
	int result;

	if (enable) {
		result = inv_mpu6050_set_power_itg(st, true);
		if (result)
			return result;
		inv_scan_query(indio_dev);
		if (st->chip_config.gyro_fifo_enable) {
			result = inv_mpu6050_switch_engine(st, true,
					INV_MPU6050_BIT_PWR_GYRO_STBY);
			if (result)
				goto error_power_off;
		}
		if (st->chip_config.accl_fifo_enable) {
			result = inv_mpu6050_switch_engine(st, true,
					INV_MPU6050_BIT_PWR_ACCL_STBY);
			if (result)
				goto error_gyro_off;
		}
		if (st->chip_config.magn_fifo_enable) {
			d = st->chip_config.user_ctrl |
					INV_MPU6050_BIT_I2C_MST_EN;
			result = regmap_write(st->map, st->reg->user_ctrl, d);
			if (result)
				goto error_accl_off;
			st->chip_config.user_ctrl = d;
		}
		st->skip_samples = inv_compute_skip_samples(st);
		result = inv_reset_fifo(indio_dev);
		if (result)
			goto error_magn_off;
	} else {
		result = regmap_write(st->map, st->reg->fifo_en, 0);
		if (result)
			goto error_magn_off;

		result = regmap_write(st->map, st->reg->int_enable, 0);
		if (result)
			goto error_magn_off;

		d = st->chip_config.user_ctrl & ~INV_MPU6050_BIT_I2C_MST_EN;
		result = regmap_write(st->map, st->reg->user_ctrl, d);
		if (result)
			goto error_magn_off;
		st->chip_config.user_ctrl = d;

		result = inv_mpu6050_switch_engine(st, false,
					INV_MPU6050_BIT_PWR_ACCL_STBY);
		if (result)
			goto error_accl_off;

		result = inv_mpu6050_switch_engine(st, false,
					INV_MPU6050_BIT_PWR_GYRO_STBY);
		if (result)
			goto error_gyro_off;

		result = inv_mpu6050_set_power_itg(st, false);
		if (result)
			goto error_power_off;
	}

	return 0;

error_magn_off:
	/* always restore user_ctrl to disable fifo properly */
	st->chip_config.user_ctrl &= ~INV_MPU6050_BIT_I2C_MST_EN;
	regmap_write(st->map, st->reg->user_ctrl, st->chip_config.user_ctrl);
error_accl_off:
	if (st->chip_config.accl_fifo_enable)
		inv_mpu6050_switch_engine(st, false,
					  INV_MPU6050_BIT_PWR_ACCL_STBY);
error_gyro_off:
	if (st->chip_config.gyro_fifo_enable)
		inv_mpu6050_switch_engine(st, false,
					  INV_MPU6050_BIT_PWR_GYRO_STBY);
error_power_off:
	inv_mpu6050_set_power_itg(st, false);
	return result;
}

/**
 * inv_mpu_data_rdy_trigger_set_state() - set data ready interrupt state
 * @trig: Trigger instance
 * @state: Desired trigger state
 */
static int inv_mpu_data_rdy_trigger_set_state(struct iio_trigger *trig,
					      bool state)
{
	struct iio_dev *indio_dev = iio_trigger_get_drvdata(trig);
	struct inv_mpu6050_state *st = iio_priv(indio_dev);
	int result;

	mutex_lock(&st->lock);
	result = inv_mpu6050_set_enable(indio_dev, state);
	mutex_unlock(&st->lock);

	return result;
}

static const struct iio_trigger_ops inv_mpu_trigger_ops = {
	.set_trigger_state = &inv_mpu_data_rdy_trigger_set_state,
};

int inv_mpu6050_probe_trigger(struct iio_dev *indio_dev, int irq_type)
{
	int ret;
	struct inv_mpu6050_state *st = iio_priv(indio_dev);

	st->trig = devm_iio_trigger_alloc(&indio_dev->dev,
					  "%s-dev%d",
					  indio_dev->name,
					  indio_dev->id);
	if (!st->trig)
		return -ENOMEM;

	ret = devm_request_irq(&indio_dev->dev, st->irq,
			       &iio_trigger_generic_data_rdy_poll,
			       irq_type,
			       "inv_mpu",
			       st->trig);
	if (ret)
		return ret;

	st->trig->dev.parent = regmap_get_device(st->map);
	st->trig->ops = &inv_mpu_trigger_ops;
	iio_trigger_set_drvdata(st->trig, indio_dev);

	ret = devm_iio_trigger_register(&indio_dev->dev, st->trig);
	if (ret)
		return ret;

	indio_dev->trig = iio_trigger_get(st->trig);

	return 0;
}
