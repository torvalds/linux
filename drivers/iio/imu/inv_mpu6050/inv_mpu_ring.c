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
#include <linux/poll.h>
#include <asm/unaligned.h>
#include "inv_mpu_iio.h"

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
	    st->chip_config.gyro_fifo_enable) {
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
	s64 timestamp = pf->timestamp;
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
	/* handle fifo overflow by reseting fifo */
	if (int_status & INV_MPU6050_BIT_FIFO_OVERFLOW_INT)
		goto flush_fifo;
	if (!(int_status & INV_MPU6050_BIT_RAW_DATA_RDY_INT)) {
		dev_warn(regmap_get_device(st->map),
			"spurious interrupt with status 0x%x\n", int_status);
		goto end_session;
	}

	if (!(st->chip_config.accl_fifo_enable |
		st->chip_config.gyro_fifo_enable))
		goto end_session;
	bytes_per_datum = 0;
	if (st->chip_config.accl_fifo_enable)
		bytes_per_datum += INV_MPU6050_BYTES_PER_3AXIS_SENSOR;

	if (st->chip_config.gyro_fifo_enable)
		bytes_per_datum += INV_MPU6050_BYTES_PER_3AXIS_SENSOR;

	/*
	 * read fifo_count register to know how many bytes are inside the FIFO
	 * right now
	 */
	result = regmap_bulk_read(st->map, st->reg->fifo_count_h, data,
				  INV_MPU6050_FIFO_COUNT_BYTE);
	if (result)
		goto end_session;
	fifo_count = get_unaligned_be16(&data[0]);
	/* compute and process all complete datum */
	nb = fifo_count / bytes_per_datum;
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
