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
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/sysfs.h>
#include <linux/jiffies.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/kfifo.h>
#include <linux/poll.h>
#include "inv_mpu_iio.h"

static void inv_clear_kfifo(struct inv_mpu6050_state *st)
{
	unsigned long flags;

	/* take the spin lock sem to avoid interrupt kick in */
	spin_lock_irqsave(&st->time_stamp_lock, flags);
	kfifo_reset(&st->timestamps);
	spin_unlock_irqrestore(&st->time_stamp_lock, flags);
}

int inv_reset_fifo(struct iio_dev *indio_dev)
{
	int result;
	u8 d;
	struct inv_mpu6050_state  *st = iio_priv(indio_dev);

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
	result = regmap_write(st->map, st->reg->user_ctrl, 0);
	if (result)
		goto reset_fifo_fail;

	/* reset FIFO*/
	result = regmap_write(st->map, st->reg->user_ctrl,
			      INV_MPU6050_BIT_FIFO_RST);
	if (result)
		goto reset_fifo_fail;

	/* clear timestamps fifo */
	inv_clear_kfifo(st);

	/* enable interrupt */
	if (st->chip_config.accl_fifo_enable ||
	    st->chip_config.gyro_fifo_enable) {
		result = regmap_write(st->map, st->reg->int_enable,
				      INV_MPU6050_BIT_DATA_RDY_EN);
		if (result)
			return result;
	}
	/* enable FIFO reading and I2C master interface*/
	result = regmap_write(st->map, st->reg->user_ctrl,
			      INV_MPU6050_BIT_FIFO_EN);
	if (result)
		goto reset_fifo_fail;
	/* enable sensor output to FIFO */
	d = 0;
	if (st->chip_config.gyro_fifo_enable)
		d |= INV_MPU6050_BITS_GYRO_OUT;
	if (st->chip_config.accl_fifo_enable)
		d |= INV_MPU6050_BIT_ACCEL_OUT;
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
 * inv_mpu6050_irq_handler() - Cache a timestamp at each data ready interrupt.
 */
irqreturn_t inv_mpu6050_irq_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct inv_mpu6050_state *st = iio_priv(indio_dev);
	s64 timestamp;

	timestamp = iio_get_time_ns();
	kfifo_in_spinlocked(&st->timestamps, &timestamp, 1,
			    &st->time_stamp_lock);

	return IRQ_WAKE_THREAD;
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

	mutex_lock(&indio_dev->mlock);
	if (!(st->chip_config.accl_fifo_enable |
		st->chip_config.gyro_fifo_enable))
		goto end_session;
	bytes_per_datum = 0;
	if (st->chip_config.accl_fifo_enable)
		bytes_per_datum += INV_MPU6050_BYTES_PER_3AXIS_SENSOR;

	if (st->chip_config.gyro_fifo_enable)
		bytes_per_datum += INV_MPU6050_BYTES_PER_3AXIS_SENSOR;

	/*
	 * read fifo_count register to know how many bytes inside FIFO
	 * right now
	 */
	result = regmap_bulk_read(st->map, st->reg->fifo_count_h, data,
				  INV_MPU6050_FIFO_COUNT_BYTE);
	if (result)
		goto end_session;
	fifo_count = be16_to_cpup((__be16 *)(&data[0]));
	if (fifo_count < bytes_per_datum)
		goto end_session;
	/* fifo count can't be odd number, if it is odd, reset fifo*/
	if (fifo_count & 1)
		goto flush_fifo;
	if (fifo_count >  INV_MPU6050_FIFO_THRESHOLD)
		goto flush_fifo;
	/* Timestamp mismatch. */
	if (kfifo_len(&st->timestamps) >
	    fifo_count / bytes_per_datum + INV_MPU6050_TIME_STAMP_TOR)
		goto flush_fifo;
	while (fifo_count >= bytes_per_datum) {
		result = regmap_bulk_read(st->map, st->reg->fifo_r_w,
					  data, bytes_per_datum);
		if (result)
			goto flush_fifo;

		result = kfifo_out(&st->timestamps, &timestamp, 1);
		/* when there is no timestamp, put timestamp as 0 */
		if (result == 0)
			timestamp = 0;

		result = iio_push_to_buffers_with_timestamp(indio_dev, data,
							    timestamp);
		if (result)
			goto flush_fifo;
		fifo_count -= bytes_per_datum;
	}

end_session:
	mutex_unlock(&indio_dev->mlock);
	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;

flush_fifo:
	/* Flush HW and SW FIFOs. */
	inv_reset_fifo(indio_dev);
	mutex_unlock(&indio_dev->mlock);
	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}
