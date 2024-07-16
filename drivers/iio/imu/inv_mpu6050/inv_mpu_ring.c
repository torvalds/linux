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

#include <linux/iio/common/inv_sensors_timestamp.h>

#include "inv_mpu_iio.h"

static int inv_reset_fifo(struct iio_dev *indio_dev)
{
	int result;
	struct inv_mpu6050_state  *st = iio_priv(indio_dev);

	/* disable fifo and reenable it */
	inv_mpu6050_prepare_fifo(st, false);
	result = inv_mpu6050_prepare_fifo(st, true);
	if (result)
		goto reset_fifo_fail;

	return 0;

reset_fifo_fail:
	dev_err(regmap_get_device(st->map), "reset fifo failed %d\n", result);
	return regmap_update_bits(st->map, st->reg->int_enable,
			INV_MPU6050_BIT_DATA_RDY_EN, INV_MPU6050_BIT_DATA_RDY_EN);
}

/*
 * inv_mpu6050_read_fifo() - Transfer data from hardware FIFO to KFIFO.
 */
irqreturn_t inv_mpu6050_read_fifo(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct inv_mpu6050_state *st = iio_priv(indio_dev);
	size_t bytes_per_datum;
	int result;
	u16 fifo_count;
	u32 fifo_period;
	s64 timestamp;
	u8 data[INV_MPU6050_OUTPUT_DATA_SIZE];
	size_t i, nb;

	mutex_lock(&st->lock);

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
	result = regmap_bulk_read(st->map, st->reg->fifo_count_h,
				  st->data, INV_MPU6050_FIFO_COUNT_BYTE);
	if (result)
		goto end_session;
	fifo_count = be16_to_cpup((__be16 *)&st->data[0]);

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

	/* compute and process only all complete datum */
	nb = fifo_count / bytes_per_datum;
	fifo_count = nb * bytes_per_datum;
	if (nb == 0)
		goto end_session;
	/* Each FIFO data contains all sensors, so same number for FIFO and sensor data */
	fifo_period = NSEC_PER_SEC / INV_MPU6050_DIVIDER_TO_FIFO_RATE(st->chip_config.divider);
	inv_sensors_timestamp_interrupt(&st->timestamp, nb, pf->timestamp);
	inv_sensors_timestamp_apply_odr(&st->timestamp, fifo_period, nb, 0);

	/* clear internal data buffer for avoiding kernel data leak */
	memset(data, 0, sizeof(data));

	/* read all data once and process every samples */
	result = regmap_noinc_read(st->map, st->reg->fifo_r_w, st->data, fifo_count);
	if (result)
		goto flush_fifo;
	for (i = 0; i < nb; ++i) {
		/* skip first samples if needed */
		if (st->skip_samples) {
			st->skip_samples--;
			continue;
		}
		memcpy(data, &st->data[i * bytes_per_datum], bytes_per_datum);
		timestamp = inv_sensors_timestamp_pop(&st->timestamp);
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
