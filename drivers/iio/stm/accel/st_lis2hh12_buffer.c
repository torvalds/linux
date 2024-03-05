// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics lis2hh12 driver
 *
 * MEMS Software Solutions Team
 *
 * Copyright 2016 STMicroelectronics Inc.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/iio/iio.h>
#include <linux/iio/buffer.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>

#include "st_lis2hh12.h"

#define LIS2HH12_ACCEL_BUFFER_SIZE \
		ALIGN(LIS2HH12_FIFO_BYTE_FOR_SAMPLE + LIS2HH12_TIMESTAMP_SIZE, \
		      LIS2HH12_TIMESTAMP_SIZE)

static void lis2hh12_push_fifo_data(struct lis2hh12_data *cdata, u16 fifo_ptr)
{
	size_t offset;
	u8 buffer[LIS2HH12_ACCEL_BUFFER_SIZE], out_buf_index;
	struct iio_dev *indio_dev = cdata->iio_sensors_dev[LIS2HH12_ACCEL];
	struct lis2hh12_sensor_data *sdata;

	out_buf_index = 0;

	/* Accelerometer data */
	sdata = iio_priv(indio_dev);
	if (sdata->enabled) {
		if (indio_dev->active_scan_mask &&
						test_bit(0, indio_dev->active_scan_mask)) {
			memcpy(&buffer[out_buf_index], &cdata->fifo_data[fifo_ptr],
									LIS2HH12_FIFO_BYTE_FOR_SAMPLE);
			out_buf_index += LIS2HH12_FIFO_BYTE_FOR_SAMPLE;
		}

		if (indio_dev->scan_timestamp) {
			offset = indio_dev->scan_bytes / sizeof(s64) - 1;
			((s64 *)buffer)[offset] = cdata->sensor_timestamp;
		}

		iio_push_to_buffers(indio_dev, buffer);
	}
}

void lis2hh12_read_xyz(struct lis2hh12_data *cdata)
{
	int err;

	err = lis2hh12_read_register(cdata, LIS2HH12_OUTX_L_ADDR,
						LIS2HH12_FIFO_BYTE_FOR_SAMPLE, cdata->fifo_data);
	if (err < 0)
		return;

	cdata->sensor_timestamp = cdata->timestamp;
	lis2hh12_push_fifo_data(cdata, 0);
}

void lis2hh12_read_fifo(struct lis2hh12_data *cdata, bool check_fifo_len)
{
	int err;
	u8 fifo_src;
	u16 read_len = cdata->fifo_size;
	uint16_t i;

	if (!cdata->fifo_data)
		return;

	if (check_fifo_len) {
		err = lis2hh12_read_register(cdata, LIS2HH12_FIFO_STATUS_ADDR, 1, &fifo_src);
		if (err < 0)
			return;

		read_len = (fifo_src & LIS2HH12_FIFO_FSS_MASK);
		read_len *= LIS2HH12_FIFO_BYTE_FOR_SAMPLE;

		if (read_len > cdata->fifo_size)
			read_len = cdata->fifo_size;
	}

	err = lis2hh12_read_register(cdata, LIS2HH12_OUTX_L_ADDR, read_len,
							cdata->fifo_data);
	if (err < 0)
		return;

	for (i = 0; i < read_len; i += LIS2HH12_FIFO_BYTE_FOR_SAMPLE) {
		lis2hh12_push_fifo_data(cdata, i);
		cdata->sensor_timestamp += cdata->sensor_deltatime;
	}
}

static inline irqreturn_t lis2hh12_handler_empty(int irq, void *p)
{
	return IRQ_HANDLED;
}

int lis2hh12_trig_set_state(struct iio_trigger *trig, bool state)
{
	return 0;
}

static int lis2hh12_buffer_preenable(struct iio_dev *indio_dev)
{
	int err;
	struct lis2hh12_sensor_data *sdata = iio_priv(indio_dev);

	err = lis2hh12_set_enable(sdata, true);
	if (err < 0)
		return err;

	return 0;
}

static int lis2hh12_buffer_postdisable(struct iio_dev *indio_dev)
{
	int err;
	struct lis2hh12_sensor_data *sdata = iio_priv(indio_dev);

	err = lis2hh12_set_enable(sdata, false);
	if (err < 0)
		return err;

	return 0;
}

static const struct iio_buffer_setup_ops lis2hh12_buffer_setup_ops = {
	.preenable = &lis2hh12_buffer_preenable,
	.postdisable = &lis2hh12_buffer_postdisable,
};

int lis2hh12_allocate_rings(struct lis2hh12_data *cdata)
{
	int err, i;

	for (i = 0; i < LIS2HH12_SENSORS_NUMB; i++) {
		err = iio_triggered_buffer_setup(
				cdata->iio_sensors_dev[i],
				&lis2hh12_handler_empty,
				NULL,
				&lis2hh12_buffer_setup_ops);
		if (err < 0)
			goto buffer_cleanup;
	}

	return 0;

buffer_cleanup:
	for (i--; i >= 0; i--)
		iio_triggered_buffer_cleanup(cdata->iio_sensors_dev[i]);

	return err;
}

void lis2hh12_deallocate_rings(struct lis2hh12_data *cdata)
{
	int i;

	for (i = 0; i < LIS2HH12_SENSORS_NUMB; i++)
		iio_triggered_buffer_cleanup(cdata->iio_sensors_dev[i]);
}
