// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics lis2ds12 driver
 *
 * MEMS Software Solutions Team
 *
 * Copyright 2015 STMicroelectronics Inc.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/iio/iio.h>
#include <linux/iio/buffer.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>

#include "st_lis2ds12.h"

#define LIS2DS12_ACCEL_BUFFER_SIZE \
		ALIGN(LIS2DS12_FIFO_BYTE_FOR_SAMPLE + LIS2DS12_TIMESTAMP_SIZE, \
		      LIS2DS12_TIMESTAMP_SIZE)
#define LIS2DS12_STEP_C_BUFFER_SIZE \
		ALIGN(LIS2DS12_FIFO_BYTE_X_AXIS + LIS2DS12_TIMESTAMP_SIZE, \
		      LIS2DS12_TIMESTAMP_SIZE)

static void lis2ds12_push_accel_data(struct lis2ds12_data *cdata,
					u8 *acc_buf, u16 read_length)
{
	size_t offset;
	uint16_t i, j, k;
	u8 buffer[LIS2DS12_ACCEL_BUFFER_SIZE], out_buf_index;
	struct iio_dev *indio_dev = cdata->iio_sensors_dev[LIS2DS12_ACCEL];
	u32 delta_ts = div_s64(cdata->accel_deltatime, cdata->hwfifo_watermark);

	for (i = 0; i < read_length; i += LIS2DS12_FIFO_BYTE_FOR_SAMPLE) {
		/* Skip first samples. */
		if (unlikely(++cdata->samples <= cdata->std_level)) {
			cdata->sample_timestamp += delta_ts;
			continue;
		}

		for (j = 0, out_buf_index = 0; j < LIS2DS12_FIFO_NUM_AXIS;
		     j++) {
			k = i + LIS2DS12_FIFO_BYTE_X_AXIS * j;
			if (test_bit(j, indio_dev->active_scan_mask)) {
				memcpy(&buffer[out_buf_index],
				       &acc_buf[k],
				       LIS2DS12_FIFO_BYTE_X_AXIS);
				out_buf_index += LIS2DS12_FIFO_BYTE_X_AXIS;
			}
		}

		if (indio_dev->scan_timestamp) {
			offset = indio_dev->scan_bytes / sizeof(s64) - 1;
			((s64 *)buffer)[offset] = cdata->sample_timestamp;
			cdata->sample_timestamp += delta_ts;
		}

		iio_push_to_buffers(indio_dev, buffer);
	}
}

void lis2ds12_read_xyz(struct lis2ds12_data *cdata)
{
	int err;
	u8 xyz_buf[LIS2DS12_FIFO_BYTE_FOR_SAMPLE];

	err = lis2ds12_read_register(cdata, LIS2DS12_OUTX_L_ADDR,
				LIS2DS12_FIFO_BYTE_FOR_SAMPLE, xyz_buf, true);
	if (err < 0)
		return;

	cdata->sample_timestamp = cdata->timestamp;
	lis2ds12_push_accel_data(cdata, xyz_buf, LIS2DS12_FIFO_BYTE_FOR_SAMPLE);
}

void lis2ds12_read_fifo(struct lis2ds12_data *cdata, bool check_fifo_len)
{
	int err;
	u8 fifo_src[2];
	u16 read_len;
#if (CONFIG_ST_LIS2DS12_IIO_LIMIT_FIFO > 0)
	u16 data_remaining, data_to_read, extra_bytes;
#endif /* CONFIG_ST_LIS2DS12_IIO_LIMIT_FIFO */

	err = lis2ds12_read_register(cdata, LIS2DS12_FIFO_SRC, 2,
							fifo_src, true);
	if (err < 0)
		return;

	read_len = (fifo_src[0] & LIS2DS12_FIFO_SRC_DIFF_MASK) ?
							(1 << 8) : 0;
	read_len |= fifo_src[1];
	read_len *= LIS2DS12_FIFO_BYTE_FOR_SAMPLE;

	if (read_len == 0)
		return;

#if (CONFIG_ST_LIS2DS12_IIO_LIMIT_FIFO == 0)
	err = lis2ds12_read_register(cdata, LIS2DS12_OUTX_L_ADDR, read_len,
							cdata->fifo_data, true);
	if (err < 0)
		return;
#else /* CONFIG_ST_LIS2DS12_IIO_LIMIT_FIFO */
	data_remaining = read_len;

	do {
		if (data_remaining > CONFIG_ST_LIS2DS12_IIO_LIMIT_FIFO)
			data_to_read = CONFIG_ST_LIS2DS12_IIO_LIMIT_FIFO;
		else
			data_to_read = data_remaining;

		extra_bytes = (data_to_read % LIS2DS12_FIFO_BYTE_FOR_SAMPLE);
		if (extra_bytes != 0) {
			data_to_read -= extra_bytes;

			if (data_to_read < LIS2DS12_FIFO_BYTE_FOR_SAMPLE)
				data_to_read = LIS2DS12_FIFO_BYTE_FOR_SAMPLE;
		}

		err = lis2ds12_read_register(cdata, LIS2DS12_OUTX_L_ADDR, data_to_read,
							&cdata->fifo_data[read_len - data_remaining], true);
		if (err < 0)
			return;

		data_remaining -= data_to_read;
	} while (data_remaining > 0);
#endif /* CONFIG_ST_LIS2DS12_IIO_LIMIT_FIFO */

	lis2ds12_push_accel_data(cdata, cdata->fifo_data, read_len);
}

void lis2ds12_read_step_c(struct lis2ds12_data *cdata)
{
	int err;
	int64_t timestamp = 0;
	char buffer[LIS2DS12_STEP_C_BUFFER_SIZE];
	struct iio_dev *indio_dev = cdata->iio_sensors_dev[LIS2DS12_STEP_C];

	err = lis2ds12_read_register(cdata, (u8)indio_dev->channels[0].address,
								2, buffer, true);
	if (err < 0)
		goto lis2ds12_step_counter_done;

	timestamp = cdata->timestamp;
	if (indio_dev->scan_timestamp)
		*(s64 *) ((u8 *) buffer +
			ALIGN(LIS2DS12_FIFO_BYTE_X_AXIS, sizeof(s64))) =
								timestamp;

	iio_push_to_buffers(indio_dev, buffer);

lis2ds12_step_counter_done:
	iio_trigger_notify_done(indio_dev->trig);
}

static inline irqreturn_t lis2ds12_handler_empty(int irq, void *p)
{
	return IRQ_HANDLED;
}

int lis2ds12_trig_set_state(struct iio_trigger *trig, bool state)
{
	int err;
	struct lis2ds12_sensor_data *sdata;

	sdata = iio_priv(iio_trigger_get_drvdata(trig));
	err = lis2ds12_update_drdy_irq(sdata, state);

	return (err < 0) ? err : 0;
}

static int lis2ds12_buffer_preenable(struct iio_dev *indio_dev)
{
	int err;
	struct lis2ds12_sensor_data *sdata = iio_priv(indio_dev);

	err = lis2ds12_set_enable(sdata, true);
	if (err < 0)
		return err;

	return 0;
}

static int lis2ds12_buffer_postdisable(struct iio_dev *indio_dev)
{
	int err;
	struct lis2ds12_sensor_data *sdata = iio_priv(indio_dev);

	err = lis2ds12_set_enable(sdata, false);
	if (err < 0)
		return err;

	return 0;
}

static const struct iio_buffer_setup_ops lis2ds12_buffer_setup_ops = {
	.preenable = &lis2ds12_buffer_preenable,
	.postdisable = &lis2ds12_buffer_postdisable,
};

int lis2ds12_allocate_rings(struct lis2ds12_data *cdata)
{
	int err, i;

	for (i = 0; i < LIS2DS12_SENSORS_NUMB; i++) {
		err = iio_triggered_buffer_setup(
				cdata->iio_sensors_dev[i],
				&lis2ds12_handler_empty,
				NULL,
				&lis2ds12_buffer_setup_ops);
		if (err < 0)
			goto buffer_cleanup;
	}

	return 0;

buffer_cleanup:
	for (i--; i >= 0; i--)
		iio_triggered_buffer_cleanup(cdata->iio_sensors_dev[i]);

	return err;
}

void lis2ds12_deallocate_rings(struct lis2ds12_data *cdata)
{
	int i;

	for (i = 0; i < LIS2DS12_SENSORS_NUMB; i++)
		iio_triggered_buffer_cleanup(cdata->iio_sensors_dev[i]);
}
