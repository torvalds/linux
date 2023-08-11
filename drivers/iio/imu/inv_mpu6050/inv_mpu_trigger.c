// SPDX-License-Identifier: GPL-2.0-only
/*
* Copyright (C) 2012 Invensense, Inc.
*/

#include <linux/pm_runtime.h>

#include <linux/iio/common/inv_sensors_timestamp.h>

#include "inv_mpu_iio.h"

static unsigned int inv_scan_query_mpu6050(struct iio_dev *indio_dev)
{
	struct inv_mpu6050_state  *st = iio_priv(indio_dev);
	unsigned int mask;

	/*
	 * If the MPU6050 is just used as a trigger, then the scan mask
	 * is not allocated so we simply enable the temperature channel
	 * as a dummy and bail out.
	 */
	if (!indio_dev->active_scan_mask) {
		st->chip_config.temp_fifo_enable = true;
		return INV_MPU6050_SENSOR_TEMP;
	}

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

	mask = 0;
	if (st->chip_config.gyro_fifo_enable)
		mask |= INV_MPU6050_SENSOR_GYRO;
	if (st->chip_config.accl_fifo_enable)
		mask |= INV_MPU6050_SENSOR_ACCL;
	if (st->chip_config.temp_fifo_enable)
		mask |= INV_MPU6050_SENSOR_TEMP;

	return mask;
}

static unsigned int inv_scan_query_mpu9x50(struct iio_dev *indio_dev)
{
	struct inv_mpu6050_state *st = iio_priv(indio_dev);
	unsigned int mask;

	mask = inv_scan_query_mpu6050(indio_dev);

	/* no magnetometer if i2c auxiliary bus is used */
	if (st->magn_disabled)
		return mask;

	st->chip_config.magn_fifo_enable =
		test_bit(INV_MPU9X50_SCAN_MAGN_X,
			 indio_dev->active_scan_mask) ||
		test_bit(INV_MPU9X50_SCAN_MAGN_Y,
			 indio_dev->active_scan_mask) ||
		test_bit(INV_MPU9X50_SCAN_MAGN_Z,
			 indio_dev->active_scan_mask);
	if (st->chip_config.magn_fifo_enable)
		mask |= INV_MPU6050_SENSOR_MAGN;

	return mask;
}

static unsigned int inv_scan_query(struct iio_dev *indio_dev)
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
	unsigned int skip_samples = 0;

	/* mag first sample is always not ready, skip it */
	if (st->chip_config.magn_fifo_enable)
		skip_samples = 1;

	return skip_samples;
}

int inv_mpu6050_prepare_fifo(struct inv_mpu6050_state *st, bool enable)
{
	uint8_t d;
	int ret;

	if (enable) {
		/* reset timestamping */
		inv_sensors_timestamp_reset(&st->timestamp);
		/* reset FIFO */
		d = st->chip_config.user_ctrl | INV_MPU6050_BIT_FIFO_RST;
		ret = regmap_write(st->map, st->reg->user_ctrl, d);
		if (ret)
			return ret;
		/* enable sensor output to FIFO */
		d = 0;
		if (st->chip_config.gyro_fifo_enable)
			d |= INV_MPU6050_BITS_GYRO_OUT;
		if (st->chip_config.accl_fifo_enable)
			d |= INV_MPU6050_BIT_ACCEL_OUT;
		if (st->chip_config.temp_fifo_enable)
			d |= INV_MPU6050_BIT_TEMP_OUT;
		if (st->chip_config.magn_fifo_enable)
			d |= INV_MPU6050_BIT_SLAVE_0;
		ret = regmap_write(st->map, st->reg->fifo_en, d);
		if (ret)
			return ret;
		/* enable FIFO reading */
		d = st->chip_config.user_ctrl | INV_MPU6050_BIT_FIFO_EN;
		ret = regmap_write(st->map, st->reg->user_ctrl, d);
		if (ret)
			return ret;
		/* enable interrupt */
		ret = regmap_write(st->map, st->reg->int_enable,
				   INV_MPU6050_BIT_DATA_RDY_EN);
	} else {
		ret = regmap_write(st->map, st->reg->int_enable, 0);
		if (ret)
			return ret;
		ret = regmap_write(st->map, st->reg->fifo_en, 0);
		if (ret)
			return ret;
		/* restore user_ctrl for disabling FIFO reading */
		ret = regmap_write(st->map, st->reg->user_ctrl,
				   st->chip_config.user_ctrl);
	}

	return ret;
}

/**
 *  inv_mpu6050_set_enable() - enable chip functions.
 *  @indio_dev:	Device driver instance.
 *  @enable: enable/disable
 */
static int inv_mpu6050_set_enable(struct iio_dev *indio_dev, bool enable)
{
	struct inv_mpu6050_state *st = iio_priv(indio_dev);
	struct device *pdev = regmap_get_device(st->map);
	unsigned int scan;
	int result;

	if (enable) {
		scan = inv_scan_query(indio_dev);
		result = pm_runtime_resume_and_get(pdev);
		if (result)
			return result;
		/*
		 * In case autosuspend didn't trigger, turn off first not
		 * required sensors.
		 */
		result = inv_mpu6050_switch_engine(st, false, ~scan);
		if (result)
			goto error_power_off;
		result = inv_mpu6050_switch_engine(st, true, scan);
		if (result)
			goto error_power_off;
		st->skip_samples = inv_compute_skip_samples(st);
		result = inv_mpu6050_prepare_fifo(st, true);
		if (result)
			goto error_power_off;
	} else {
		result = inv_mpu6050_prepare_fifo(st, false);
		if (result)
			goto error_power_off;
		pm_runtime_mark_last_busy(pdev);
		pm_runtime_put_autosuspend(pdev);
	}

	return 0;

error_power_off:
	pm_runtime_put_autosuspend(pdev);
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
					  iio_device_id(indio_dev));
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
