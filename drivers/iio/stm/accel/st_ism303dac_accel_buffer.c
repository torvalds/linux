// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics ism303dac driver
 *
 * MEMS Software Solutions Team
 *
 * Copyright 2018 STMicroelectronics Inc.
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

#include "st_ism303dac_accel.h"

#define ISM303DAC_ACCEL_BUFFER_SIZE \
		ALIGN(ISM303DAC_FIFO_BYTE_FOR_SAMPLE + ISM303DAC_TIMESTAMP_SIZE, \
		      ISM303DAC_TIMESTAMP_SIZE)

static void ism303dac_push_accel_data(struct ism303dac_data *cdata,
				      u8 *acc_buf, u16 read_length)
{
	size_t offset;
	uint16_t i, j, k;
	u8 buffer[ISM303DAC_ACCEL_BUFFER_SIZE], out_buf_index;
	struct iio_dev *indio_dev = cdata->iio_sensors_dev[ISM303DAC_ACCEL];
	u32 delta_ts = div_s64(cdata->accel_deltatime, cdata->hwfifo_watermark);

	for (i = 0; i < read_length; i += ISM303DAC_FIFO_BYTE_FOR_SAMPLE) {
		/* Skip first samples. */
		if (unlikely(++cdata->samples <= cdata->std_level)) {
			cdata->sample_timestamp += delta_ts;
			continue;
		}

		for (j = 0, out_buf_index = 0; j < ISM303DAC_FIFO_NUM_AXIS;
		     j++) {
			k = i + ISM303DAC_FIFO_BYTE_X_AXIS * j;
			if (test_bit(j, indio_dev->active_scan_mask)) {
				memcpy(&buffer[out_buf_index],
				       &acc_buf[k],
				       ISM303DAC_FIFO_BYTE_X_AXIS);
				out_buf_index += ISM303DAC_FIFO_BYTE_X_AXIS;
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

void ism303dac_read_xyz(struct ism303dac_data *cdata)
{
	int err;
	u8 xyz_buf[ISM303DAC_FIFO_BYTE_FOR_SAMPLE];

	err = ism303dac_read_register(cdata, ISM303DAC_OUTX_L_ADDR,
				ISM303DAC_FIFO_BYTE_FOR_SAMPLE, xyz_buf, true);
	if (err < 0)
		return;

	cdata->sample_timestamp = cdata->timestamp;
	ism303dac_push_accel_data(cdata, xyz_buf, ISM303DAC_FIFO_BYTE_FOR_SAMPLE);
}

void ism303dac_read_fifo(struct ism303dac_data *cdata, bool check_fifo_len)
{
	int err;
	u8 fifo_src[2];
	u16 read_len;
#if (CONFIG_ST_ISM303DAC_ACCEL_IIO_LIMIT_FIFO > 0)
	u16 data_remaining, data_to_read, extra_bytes;
#endif /* CONFIG_ST_ISM303DAC_ACCEL_IIO_LIMIT_FIFO */

	err = ism303dac_read_register(cdata, ISM303DAC_FIFO_SRC, 2,
				      fifo_src, true);
	if (err < 0)
		return;

	read_len = (fifo_src[0] & ISM303DAC_FIFO_SRC_DIFF_MASK) ? (1 << 8) : 0;
	read_len |= fifo_src[1];
	read_len *= ISM303DAC_FIFO_BYTE_FOR_SAMPLE;

	if (read_len == 0)
		return;

#if (CONFIG_ST_ISM303DAC_ACCEL_IIO_LIMIT_FIFO == 0)
	err = ism303dac_read_register(cdata, ISM303DAC_OUTX_L_ADDR, read_len,
				      cdata->fifo_data, true);
	if (err < 0)
		return;
#else /* CONFIG_ST_ISM303DAC_ACCEL_IIO_LIMIT_FIFO */
	data_remaining = read_len;

	do {
		if (data_remaining > CONFIG_ST_ISM303DAC_ACCEL_IIO_LIMIT_FIFO)
			data_to_read = CONFIG_ST_ISM303DAC_ACCEL_IIO_LIMIT_FIFO;
		else
			data_to_read = data_remaining;

		extra_bytes = (data_to_read % ISM303DAC_FIFO_BYTE_FOR_SAMPLE);
		if (extra_bytes != 0) {
			data_to_read -= extra_bytes;

			if (data_to_read < ISM303DAC_FIFO_BYTE_FOR_SAMPLE)
				data_to_read = ISM303DAC_FIFO_BYTE_FOR_SAMPLE;
		}

		err = ism303dac_read_register(cdata, ISM303DAC_OUTX_L_ADDR, data_to_read,
				&cdata->fifo_data[read_len - data_remaining], true);
		if (err < 0)
			return;

		data_remaining -= data_to_read;
	} while (data_remaining > 0);
#endif /* CONFIG_ST_ISM303DAC_ACCEL_IIO_LIMIT_FIFO */

	ism303dac_push_accel_data(cdata, cdata->fifo_data, read_len);
}

static inline irqreturn_t ism303dac_handler_empty(int irq, void *p)
{
	return IRQ_HANDLED;
}

int ism303dac_trig_set_state(struct iio_trigger *trig, bool state)
{
	int err;
	struct ism303dac_sensor_data *sdata;

	sdata = iio_priv(iio_trigger_get_drvdata(trig));
	err = ism303dac_update_drdy_irq(sdata, state);

	return (err < 0) ? err : 0;
}

static int ism303dac_buffer_preenable(struct iio_dev *indio_dev)
{
	int err;
	struct ism303dac_sensor_data *sdata = iio_priv(indio_dev);

	err = ism303dac_set_enable(sdata, true);
	if (err < 0)
		return err;

	return 0;
}

static int ism303dac_buffer_postdisable(struct iio_dev *indio_dev)
{
	int err;
	struct ism303dac_sensor_data *sdata = iio_priv(indio_dev);

	err = ism303dac_set_enable(sdata, false);
	if (err < 0)
		return err;

	return 0;
}

static const struct iio_buffer_setup_ops ism303dac_buffer_setup_ops = {
	.preenable = &ism303dac_buffer_preenable,
	.postdisable = &ism303dac_buffer_postdisable,
};

int ism303dac_allocate_rings(struct ism303dac_data *cdata)
{
	int err, i;

	for (i = 0; i < ISM303DAC_SENSORS_NUMB; i++) {
		err = iio_triggered_buffer_setup(
				cdata->iio_sensors_dev[i],
				&ism303dac_handler_empty,
				NULL,
				&ism303dac_buffer_setup_ops);
		if (err < 0)
			goto buffer_cleanup;
	}

	return 0;

buffer_cleanup:
	for (i--; i >= 0; i--)
		iio_triggered_buffer_cleanup(cdata->iio_sensors_dev[i]);

	return err;
}

void ism303dac_deallocate_rings(struct ism303dac_data *cdata)
{
	int i;

	for (i = 0; i < ISM303DAC_SENSORS_NUMB; i++)
		iio_triggered_buffer_cleanup(cdata->iio_sensors_dev[i]);
}
