// SPDX-License-Identifier: GPL-2.0-only
/*
* Copyright (C) 2012 Invensense, Inc.
*/

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/sysfs.h>
#include <linux/jiffies.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/poll.h>
#include <linux/math64.h>
#include <asm/unaligned.h>
#include "inv_mpu_iio.h"

/**
 *  inv_mpu6050_update_period() - Update chip internal period estimation
 *
 *  @st:		driver state
 *  @timestamp:		the interrupt timestamp
 *  @nb:		number of data set in the fifo
 *
 *  This function uses interrupt timestamps to estimate the chip period and
 *  to choose the data timestamp to come.
 */
static void inv_mpu6050_update_period(struct inv_mpu6050_state *st,
				      s64 timestamp, size_t nb)
{
	/* Period boundaries for accepting timestamp */
	const s64 period_min =
		(NSEC_PER_MSEC * (100 - INV_MPU6050_TS_PERIOD_JITTER)) / 100;
	const s64 period_max =
		(NSEC_PER_MSEC * (100 + INV_MPU6050_TS_PERIOD_JITTER)) / 100;
	const s32 divider = INV_MPU6050_FREQ_DIVIDER(st);
	s64 delta, interval;
	bool use_it_timestamp = false;

	if (st->it_timestamp == 0) {
		/* not initialized, forced to use it_timestamp */
		use_it_timestamp = true;
	} else if (nb == 1) {
		/*
		 * Validate the use of it timestamp by checking if interrupt
		 * has been delayed.
		 * nb > 1 means interrupt was delayed for more than 1 sample,
		 * so it's obviously not good.
		 * Compute the chip period between 2 interrupts for validating.
		 */
		delta = div_s64(timestamp - st->it_timestamp, divider);
		if (delta > period_min && delta < period_max) {
			/* update chip period and use it timestamp */
			st->chip_period = (st->chip_period + delta) / 2;
			use_it_timestamp = true;
		}
	}

	if (use_it_timestamp) {
		/*
		 * Manage case of multiple samples in the fifo (nb > 1):
		 * compute timestamp corresponding to the first sample using
		 * estimated chip period.
		 */
		interval = (nb - 1) * st->chip_period * divider;
		st->data_timestamp = timestamp - interval;
	}

	/* save it timestamp */
	st->it_timestamp = timestamp;
}

/**
 *  inv_mpu6050_get_timestamp() - Return the current data timestamp
 *
 *  @st:		driver state
 *  @return:		current data timestamp
 *
 *  This function returns the current data timestamp and prepares for next one.
 */
static s64 inv_mpu6050_get_timestamp(struct inv_mpu6050_state *st)
{
	s64 ts;

	/* return current data timestamp and increment */
	ts = st->data_timestamp;
	st->data_timestamp += st->chip_period * INV_MPU6050_FREQ_DIVIDER(st);

	return ts;
}

int inv_reset_fifo(struct iio_dev *indio_dev)
{
	int result;
	u8 d;
	struct inv_mpu6050_state  *st = iio_priv(indio_dev);

	/* reset it timestamp validation */
	st->it_timestamp = 0;

	/* disable interrupt */
	result = regmap_write(st->map, st->reg->int_enable, 0);
	if (result) {
		dev_err(regmap_get_device(st->map), "int_enable failed %d\n",
			result);
		return result;
	}
	/* disable the sensor output to FIFO */
	result = regmap_write(st->map, st->reg->fifo_en, 0);
	if (result)
		goto reset_fifo_fail;
	/* disable fifo reading */
	result = regmap_write(st->map, st->reg->user_ctrl,
			      st->chip_config.user_ctrl);
	if (result)
		goto reset_fifo_fail;

	/* reset FIFO*/
	d = st->chip_config.user_ctrl | INV_MPU6050_BIT_FIFO_RST;
	result = regmap_write(st->map, st->reg->user_ctrl, d);
	if (result)
		goto reset_fifo_fail;

	/* enable interrupt */
	if (st->chip_config.accl_fifo_enable ||
	    st->chip_config.gyro_fifo_enable ||
	    st->chip_config.magn_fifo_enable) {
		result = regmap_write(st->map, st->reg->int_enable,
				      INV_MPU6050_BIT_DATA_RDY_EN);
		if (result)
			return result;
	}
	/* enable FIFO reading */
	d = st->chip_config.user_ctrl | INV_MPU6050_BIT_FIFO_EN;
	result = regmap_write(st->map, st->reg->user_ctrl, d);
	if (result)
		goto reset_fifo_fail;
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
	result = regmap_write(st->map, st->reg->fifo_en, d);
	if (result)
		goto reset_fifo_fail;

	return 0;

reset_fifo_fail:
	dev_err(regmap_get_device(st->map), "reset fifo failed %d\n", result);
	result = regmap_write(st->map, st->reg->int_enable,
			      INV_MPU6050_BIT_DATA_RDY_EN);

	return result;
}

/**
 * inv_mpu6050_read_fifo() - Transfer data from hardware FIFO to KFIFO.
 */
irqreturn_t inv_mpu6050_read_fifo(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct inv_mpu6050_state *st = iio_priv(indio_dev);
	size_t bytes_per_datum;
	int result;
	u8 data[INV_MPU6050_OUTPUT_DATA_SIZE];
	u16 fifo_count;
	s64 timestamp;
	int int_status;
	size_t i, nb;

	mutex_lock(&st->lock);

	/* ack interrupt and check status */
	result = regmap_read(st->map, st->reg->int_status, &int_status);
	if (result) {
		dev_err(regmap_get_device(st->map),
			"failed to ack interrupt\n");
		goto flush_fifo;
	}
	if (!(int_status & INV_MPU6050_BIT_RAW_DATA_RDY_INT))
		goto end_session;

	if (!(st->chip_config.accl_fifo_enable |
		st->chip_config.gyro_fifo_enable |
		st->chip_config.magn_fifo_enable))
		goto end_session;
	bytes_per_datum = 0;
	if (st->chip_config.accl_fifo_enable)
		bytes_per_datum += INV_MPU6050_BYTES_PER_3AXIS_SENSOR;

	if (st->chip_config.gyro_fifo_enable)
		bytes_per_datum += INV_MPU6050_BYTES_PER_3AXIS_SENSOR;

	if (st->chip_config.temp_fifo_enable)
		bytes_per_datum += INV_MPU6050_BYTES_PER_TEMP_SENSOR;

	if (st->chip_config.magn_fifo_enable)
		bytes_per_datum += INV_MPU9X50_BYTES_MAGN;

	/*
	 * read fifo_count register to know how many bytes are inside the FIFO
	 * right now
	 */
	result = regmap_bulk_read(st->map, st->reg->fifo_count_h, data,
				  INV_MPU6050_FIFO_COUNT_BYTE);
	if (result)
		goto end_session;
	fifo_count = get_unaligned_be16(&data[0]);

	/*
	 * Handle fifo overflow by resetting fifo.
	 * Reset if there is only 3 data set free remaining to mitigate
	 * possible delay between reading fifo count and fifo data.
	 */
	nb = 3 * bytes_per_datum;
	if (fifo_count >= st->hw->fifo_size - nb) {
		dev_warn(regmap_get_device(st->map), "fifo overflow reset\n");
		goto flush_fifo;
	}

	/* compute and process all complete datum */
	nb = fifo_count / bytes_per_datum;
	inv_mpu6050_update_period(st, pf->timestamp, nb);
	for (i = 0; i < nb; ++i) {
		result = regmap_bulk_read(st->map, st->reg->fifo_r_w,
					  data, bytes_per_datum);
		if (result)
			goto flush_fifo;
		/* skip first samples if needed */
		if (st->skip_samples) {
			st->skip_samples--;
			continue;
		}
		timestamp = inv_mpu6050_get_timestamp(st);
		iio_push_to_buffers_with_timestamp(indio_dev, data, timestamp);
	}

end_session:
	mutex_unlock(&st->lock);
	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;

flush_fifo:
	/* Flush HW and SW FIFOs. */
	inv_reset_fifo(indio_dev);
	mutex_unlock(&st->lock);
	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}
