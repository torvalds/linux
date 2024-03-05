// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics lsm6ds3h buffer driver
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
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/iio/iio.h>
#include <linux/iio/buffer.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/version.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,11,0)
#include <linux/iio/buffer_impl.h>
#endif /* LINUX_VERSION_CODE */

#include "st_lsm6ds3h.h"

#define ST_LSM6DS3H_ENABLE_AXIS			0x07
#define ST_LSM6DS3H_FIFO_DIFF_L			0x3a
#define ST_LSM6DS3H_FIFO_DIFF_MASK		0x0f
#define ST_LSM6DS3H_FIFO_DATA_OUT_L		0x3e
#define ST_LSM6DS3H_FIFO_DATA_OVR		0x40
#define ST_LSM6DS3H_FIFO_DATA_EMPTY		0x10
#define ST_LSM6DS3H_STEP_MASK_64BIT		(0xFFFFFFFFFFFF0000)

#define MIN_ID(a, b, c, d)			(((a) < (b)) ? ((a == 0) ? \
						(d) : (c)) : ((b == 0) ? \
						(c) : (d)))

int st_lsm6ds3h_push_data_with_timestamp(struct lsm6ds3h_data *cdata,
					u8 index, u8 *data, int64_t timestamp)
{
	size_t offset;
	int i, n = 0;
	struct iio_chan_spec const *chs = cdata->indio_dev[index]->channels;
	uint16_t bfch, bfchs_out = 0, bfchs_in = 0;
	struct lsm6ds3h_sensor_data *sdata = iio_priv(cdata->indio_dev[index]);

	if (timestamp <= cdata->fifo_output[index].timestamp_p)
		return -EINVAL;

	for (i = 0; i < sdata->num_data_channels; i++) {
		bfch = chs[i].scan_type.storagebits >> 3;

		if (test_bit(i, cdata->indio_dev[index]->active_scan_mask)) {
			memcpy(&sdata->buffer_data[bfchs_out],
						&data[bfchs_in], bfch);
			n++;
			bfchs_out += bfch;
		}

		bfchs_in += bfch;
	}

	if (cdata->indio_dev[index]->scan_timestamp) {
		offset = cdata->indio_dev[index]->scan_bytes / sizeof(s64) - 1;
		((s64 *)sdata->buffer_data)[offset] = timestamp;
	}

	iio_push_to_buffers(cdata->indio_dev[index], sdata->buffer_data);

	cdata->fifo_output[index].timestamp_p = timestamp;

	return 0;
}

static void st_lsm6ds3h_parse_fifo_data(struct lsm6ds3h_data *cdata,
			u16 read_len, int64_t time_top, u16 num_pattern)
{
	int err;
	u16 fifo_offset = 0;
	u8 gyro_sip, accel_sip;
#ifdef CONFIG_ST_LSM6DS3H_IIO_MASTER_SUPPORT
	u8 ext0_sip;
#endif /* CONFIG_ST_LSM6DS3H_IIO_MASTER_SUPPORT */

	while (fifo_offset < read_len) {
		gyro_sip = cdata->fifo_output[ST_MASK_ID_GYRO].sip;
		accel_sip = cdata->fifo_output[ST_MASK_ID_ACCEL].sip;
#ifdef CONFIG_ST_LSM6DS3H_IIO_MASTER_SUPPORT
		ext0_sip = cdata->fifo_output[ST_MASK_ID_EXT0].sip;
#endif /* CONFIG_ST_LSM6DS3H_IIO_MASTER_SUPPORT */

		do {
			if (gyro_sip > 0) {
				if (cdata->fifo_output[ST_MASK_ID_GYRO].timestamp == 0) {
					if (cdata->slower_id == ST_MASK_ID_GYRO)
						cdata->fifo_output[ST_MASK_ID_GYRO].timestamp = time_top -
							(num_pattern * gyro_sip * cdata->fifo_output[ST_MASK_ID_GYRO].deltatime) - 300000;
					else
						cdata->fifo_output[ST_MASK_ID_GYRO].timestamp = time_top -
							(num_pattern * gyro_sip * cdata->fifo_output[ST_MASK_ID_GYRO].deltatime) - 300000 -
							(cdata->fifo_output[cdata->slower_id].deltatime - cdata->fifo_output[ST_MASK_ID_GYRO].deltatime);
				} else
					cdata->fifo_output[ST_MASK_ID_GYRO].timestamp += cdata->fifo_output[ST_MASK_ID_GYRO].deltatime;

				if (cdata->fifo_output[ST_MASK_ID_GYRO].timestamp > time_top) {
					cdata->fifo_output[ST_MASK_ID_GYRO].timestamp -= cdata->fifo_output[ST_MASK_ID_GYRO].deltatime;
					cdata->samples_to_discard[ST_MASK_ID_GYRO] = 1;
				}

				if (cdata->samples_to_discard[ST_MASK_ID_GYRO] > 0)
					cdata->samples_to_discard[ST_MASK_ID_GYRO]--;
				else {
					cdata->fifo_output[ST_MASK_ID_GYRO].num_samples++;

					if (cdata->fifo_output[ST_MASK_ID_GYRO].num_samples >= cdata->fifo_output[ST_MASK_ID_GYRO].decimator) {
						cdata->fifo_output[ST_MASK_ID_GYRO].num_samples = 0;

						if (cdata->sensors_enabled & BIT(ST_MASK_ID_GYRO)) {
							if (cdata->samples_to_discard_2[ST_MASK_ID_GYRO] == 0) {
								err = st_lsm6ds3h_push_data_with_timestamp(
									cdata, ST_MASK_ID_GYRO,
									&cdata->fifo_data[fifo_offset],
									cdata->fifo_output[ST_MASK_ID_GYRO].timestamp);

								if (err >= 0)
									cdata->fifo_output[ST_MASK_ID_GYRO].initialized = true;

								memcpy(cdata->gyro_last_push, &cdata->fifo_data[fifo_offset], 6);
							} else {
								cdata->samples_to_discard_2[ST_MASK_ID_GYRO]--;

								if (cdata->fifo_output[ST_MASK_ID_GYRO].initialized) {
									err = st_lsm6ds3h_push_data_with_timestamp(
										cdata, ST_MASK_ID_GYRO,
										cdata->gyro_last_push,
										cdata->fifo_output[ST_MASK_ID_GYRO].timestamp);
								}
							}
						}
					}
				}

				fifo_offset += ST_LSM6DS3H_FIFO_ELEMENT_LEN_BYTE;
				gyro_sip--;
			}

			if (accel_sip > 0) {
				if (cdata->fifo_output[ST_MASK_ID_ACCEL].timestamp == 0) {
					if (cdata->slower_id == ST_MASK_ID_ACCEL)
						cdata->fifo_output[ST_MASK_ID_ACCEL].timestamp = time_top -
							(num_pattern * accel_sip * cdata->fifo_output[ST_MASK_ID_ACCEL].deltatime) - 300000;
					else
						cdata->fifo_output[ST_MASK_ID_ACCEL].timestamp = time_top -
							(num_pattern * accel_sip * cdata->fifo_output[ST_MASK_ID_ACCEL].deltatime) - 300000 -
							(cdata->fifo_output[cdata->slower_id].deltatime - cdata->fifo_output[ST_MASK_ID_ACCEL].deltatime);
				} else
					cdata->fifo_output[ST_MASK_ID_ACCEL].timestamp += cdata->fifo_output[ST_MASK_ID_ACCEL].deltatime;

				if (cdata->fifo_output[ST_MASK_ID_ACCEL].timestamp > time_top) {
					cdata->fifo_output[ST_MASK_ID_ACCEL].timestamp -= cdata->fifo_output[ST_MASK_ID_ACCEL].deltatime;
					cdata->samples_to_discard[ST_MASK_ID_ACCEL] = 1;
				}

				if (cdata->samples_to_discard[ST_MASK_ID_ACCEL] > 0)
					cdata->samples_to_discard[ST_MASK_ID_ACCEL]--;
				else {
					cdata->fifo_output[ST_MASK_ID_ACCEL].num_samples++;

					if (cdata->fifo_output[ST_MASK_ID_ACCEL].num_samples >= cdata->fifo_output[ST_MASK_ID_ACCEL].decimator) {
						cdata->fifo_output[ST_MASK_ID_ACCEL].num_samples = 0;

						if (cdata->sensors_enabled & BIT(ST_MASK_ID_ACCEL)) {
							if (cdata->samples_to_discard_2[ST_MASK_ID_ACCEL] == 0) {
								err = st_lsm6ds3h_push_data_with_timestamp(
									cdata, ST_MASK_ID_ACCEL,
									&cdata->fifo_data[fifo_offset],
									cdata->fifo_output[ST_MASK_ID_ACCEL].timestamp);

								if (err >= 0)
									cdata->fifo_output[ST_MASK_ID_ACCEL].initialized = true;

								memcpy(cdata->accel_last_push, &cdata->fifo_data[fifo_offset], 6);
							} else {
								cdata->samples_to_discard_2[ST_MASK_ID_ACCEL]--;

								if (cdata->fifo_output[ST_MASK_ID_ACCEL].initialized) {
									err = st_lsm6ds3h_push_data_with_timestamp(
										cdata, ST_MASK_ID_ACCEL,
										cdata->accel_last_push,
										cdata->fifo_output[ST_MASK_ID_ACCEL].timestamp);
								}
							}
						}
					}
				}

				fifo_offset += ST_LSM6DS3H_FIFO_ELEMENT_LEN_BYTE;
				accel_sip--;
			}

#ifdef CONFIG_ST_LSM6DS3H_IIO_MASTER_SUPPORT
			if (ext0_sip > 0) {
				if (cdata->fifo_output[ST_MASK_ID_EXT0].timestamp == 0) {
					if (cdata->slower_id == ST_MASK_ID_EXT0)
						cdata->fifo_output[ST_MASK_ID_EXT0].timestamp = time_top -
							(num_pattern * ext0_sip * cdata->fifo_output[ST_MASK_ID_EXT0].deltatime) - 300000;
					else
						cdata->fifo_output[ST_MASK_ID_EXT0].timestamp = time_top -
							(num_pattern * ext0_sip * cdata->fifo_output[ST_MASK_ID_EXT0].deltatime) - 300000 -
							(cdata->fifo_output[cdata->slower_id].deltatime - cdata->fifo_output[ST_MASK_ID_EXT0].deltatime);
				} else
					cdata->fifo_output[ST_MASK_ID_EXT0].timestamp += cdata->fifo_output[ST_MASK_ID_EXT0].deltatime;

				if (cdata->fifo_output[ST_MASK_ID_EXT0].timestamp > time_top) {
					cdata->fifo_output[ST_MASK_ID_EXT0].timestamp -= cdata->fifo_output[ST_MASK_ID_EXT0].deltatime;
					cdata->samples_to_discard[ST_MASK_ID_EXT0] = 1;
				}

				if (cdata->samples_to_discard[ST_MASK_ID_EXT0] > 0)
					cdata->samples_to_discard[ST_MASK_ID_EXT0]--;
				else {
					cdata->fifo_output[ST_MASK_ID_EXT0].num_samples++;

					if (cdata->fifo_output[ST_MASK_ID_EXT0].num_samples >= cdata->fifo_output[ST_MASK_ID_EXT0].decimator) {
						cdata->fifo_output[ST_MASK_ID_EXT0].num_samples = 0;

						if (cdata->sensors_enabled & BIT(ST_MASK_ID_EXT0)) {
							if (cdata->samples_to_discard_2[ST_MASK_ID_EXT0] == 0) {
								err = st_lsm6ds3h_push_data_with_timestamp(
									cdata, ST_MASK_ID_EXT0,
									&cdata->fifo_data[fifo_offset],
									cdata->fifo_output[ST_MASK_ID_EXT0].timestamp);

								if (err >= 0)
									cdata->fifo_output[ST_MASK_ID_EXT0].initialized = true;

								memcpy(cdata->ext0_last_push, &cdata->fifo_data[fifo_offset], 6);
							} else {
								cdata->samples_to_discard_2[ST_MASK_ID_EXT0]--;

								if (cdata->fifo_output[ST_MASK_ID_EXT0].initialized) {
									err = st_lsm6ds3h_push_data_with_timestamp(
										cdata, ST_MASK_ID_EXT0,
										cdata->ext0_last_push,
										cdata->fifo_output[ST_MASK_ID_EXT0].timestamp);
								}
							}
						}
					}
				}

				fifo_offset += ST_LSM6DS3H_FIFO_ELEMENT_LEN_BYTE;
				ext0_sip--;
			}

		} while ((accel_sip > 0) || (gyro_sip > 0) || (ext0_sip > 0));
#else /* CONFIG_ST_LSM6DS3H_IIO_MASTER_SUPPORT */
		} while ((accel_sip > 0) || (gyro_sip > 0));
#endif /* CONFIG_ST_LSM6DS3H_IIO_MASTER_SUPPORT */
	}
}

int st_lsm6ds3h_read_fifo(struct lsm6ds3h_data *cdata, bool async)
{
	int err;
	u8 fifo_status[2];
#if (CONFIG_ST_LSM6DS3H_IIO_LIMIT_FIFO > 0)
	u16 data_remaining, data_to_read;
#endif /* CONFIG_ST_LSM6DS3H_IIO_LIMIT_FIFO */
	u16 read_len = 0, byte_in_pattern, num_pattern;
	int64_t temp_counter = 0, timestamp_diff, slower_deltatime;

	err = cdata->tf->read(cdata, ST_LSM6DS3H_FIFO_DIFF_L,
						2, fifo_status, true);
	if (err < 0)
		return err;

	timestamp_diff =
					iio_get_time_ns(cdata->indio_dev[ST_MASK_ID_ACCEL]);

	if (fifo_status[1] & ST_LSM6DS3H_FIFO_DATA_OVR) {
		st_lsm6ds3h_set_fifo_mode(cdata, BYPASS);
		st_lsm6ds3h_set_fifo_mode(cdata, CONTINUOS);
		dev_err(cdata->dev, "data fifo overrun, failed to read it.\n");
		return -EINVAL;
	}

	if (fifo_status[1] & ST_LSM6DS3H_FIFO_DATA_EMPTY)
		return 0;

	read_len = ((fifo_status[1] & ST_LSM6DS3H_FIFO_DIFF_MASK) << 8) | fifo_status[0];
	read_len *= ST_LSM6DS3H_BYTE_FOR_CHANNEL;

#ifdef CONFIG_ST_LSM6DS3H_IIO_MASTER_SUPPORT
	byte_in_pattern = (cdata->fifo_output[ST_MASK_ID_ACCEL].sip +
				cdata->fifo_output[ST_MASK_ID_GYRO].sip +
				cdata->fifo_output[ST_MASK_ID_EXT0].sip) *
				ST_LSM6DS3H_FIFO_ELEMENT_LEN_BYTE;
#else /* CONFIG_ST_LSM6DS3H_IIO_MASTER_SUPPORT */
	byte_in_pattern = (cdata->fifo_output[ST_MASK_ID_ACCEL].sip +
				cdata->fifo_output[ST_MASK_ID_GYRO].sip) *
				ST_LSM6DS3H_FIFO_ELEMENT_LEN_BYTE;
#endif /* CONFIG_ST_LSM6DS3H_IIO_MASTER_SUPPORT */
	if (byte_in_pattern == 0)
		return 0;

	num_pattern = read_len / byte_in_pattern;

	read_len = (read_len / byte_in_pattern) * byte_in_pattern;
	if (read_len == 0)
		return 0;

#if (CONFIG_ST_LSM6DS3H_IIO_LIMIT_FIFO == 0)
	err = cdata->tf->read(cdata, ST_LSM6DS3H_FIFO_DATA_OUT_L,
					read_len, cdata->fifo_data, true);
	if (err < 0)
		return err;
#else /* CONFIG_ST_LSM6DS3H_IIO_LIMIT_FIFO */
	data_remaining = read_len;

	do {
		if (data_remaining > CONFIG_ST_LSM6DS3H_IIO_LIMIT_FIFO)
			data_to_read = CONFIG_ST_LSM6DS3H_IIO_LIMIT_FIFO;
		else
			data_to_read = data_remaining;

		err = cdata->tf->read(cdata, ST_LSM6DS3H_FIFO_DATA_OUT_L,
				data_to_read,
				&cdata->fifo_data[read_len - data_remaining], true);
		if (err < 0)
			return err;

		data_remaining -= data_to_read;
	} while (data_remaining > 0);
#endif /* CONFIG_ST_LSM6DS3H_IIO_LIMIT_FIFO */

	cdata->slower_id = MIN_ID(cdata->fifo_output[ST_MASK_ID_GYRO].sip,
				cdata->fifo_output[ST_MASK_ID_ACCEL].sip,
				ST_MASK_ID_GYRO, ST_MASK_ID_ACCEL);
#ifdef CONFIG_ST_LSM6DS3H_IIO_MASTER_SUPPORT
	cdata->slower_id = MIN_ID(cdata->fifo_output[cdata->slower_id].sip,
				cdata->fifo_output[ST_MASK_ID_EXT0].sip,
				cdata->slower_id, ST_MASK_ID_EXT0);
#endif /* CONFIG_ST_LSM6DS3H_IIO_LIMIT_FIFO */

	temp_counter = cdata->slower_counter;
	cdata->slower_counter += (read_len / byte_in_pattern) * cdata->fifo_output[cdata->slower_id].sip;

	if (async)
		goto parse_fifo;

	if (temp_counter > 0) {
		slower_deltatime = div64_s64(timestamp_diff - cdata->fifo_enable_timestamp, cdata->slower_counter);

		switch (cdata->slower_id) {
		case ST_MASK_ID_ACCEL:
			if (cdata->fifo_output[ST_MASK_ID_GYRO].sip != 0)
				cdata->fifo_output[ST_MASK_ID_GYRO].deltatime = div64_s64(slower_deltatime *
					cdata->fifo_output[ST_MASK_ID_ACCEL].sip, cdata->fifo_output[ST_MASK_ID_GYRO].sip);

			if (cdata->fifo_output[ST_MASK_ID_EXT0].sip != 0)
				cdata->fifo_output[ST_MASK_ID_EXT0].deltatime = div64_s64(slower_deltatime *
					cdata->fifo_output[ST_MASK_ID_ACCEL].sip, cdata->fifo_output[ST_MASK_ID_EXT0].sip);

			cdata->fifo_output[ST_MASK_ID_ACCEL].deltatime = slower_deltatime;
			break;

		case ST_MASK_ID_GYRO:
			if (cdata->fifo_output[ST_MASK_ID_ACCEL].sip != 0)
				cdata->fifo_output[ST_MASK_ID_ACCEL].deltatime = div64_s64(slower_deltatime *
					cdata->fifo_output[ST_MASK_ID_GYRO].sip, cdata->fifo_output[ST_MASK_ID_ACCEL].sip);

			if (cdata->fifo_output[ST_MASK_ID_EXT0].sip != 0)
				cdata->fifo_output[ST_MASK_ID_EXT0].deltatime = div64_s64(slower_deltatime *
					cdata->fifo_output[ST_MASK_ID_GYRO].sip, cdata->fifo_output[ST_MASK_ID_EXT0].sip);

			cdata->fifo_output[ST_MASK_ID_GYRO].deltatime = slower_deltatime;
			break;

		case ST_MASK_ID_EXT0:
			if (cdata->fifo_output[ST_MASK_ID_ACCEL].sip != 0)
				cdata->fifo_output[ST_MASK_ID_ACCEL].deltatime = div64_s64(slower_deltatime *
					cdata->fifo_output[ST_MASK_ID_EXT0].sip, cdata->fifo_output[ST_MASK_ID_ACCEL].sip);

			if (cdata->fifo_output[ST_MASK_ID_GYRO].sip != 0)
				cdata->fifo_output[ST_MASK_ID_GYRO].deltatime = div64_s64(slower_deltatime *
					cdata->fifo_output[ST_MASK_ID_EXT0].sip, cdata->fifo_output[ST_MASK_ID_GYRO].sip);

			cdata->fifo_output[ST_MASK_ID_EXT0].deltatime = slower_deltatime;
			break;

		default:
			break;
		}
	} else {
		cdata->fifo_output[ST_MASK_ID_ACCEL].deltatime = cdata->fifo_output[ST_MASK_ID_ACCEL].deltatime_default;
		cdata->fifo_output[ST_MASK_ID_GYRO].deltatime = cdata->fifo_output[ST_MASK_ID_GYRO].deltatime_default;
#ifdef CONFIG_ST_LSM6DS3H_IIO_MASTER_SUPPORT
		cdata->fifo_output[ST_MASK_ID_EXT0].deltatime = cdata->fifo_output[ST_MASK_ID_EXT0].deltatime_default;
#endif /* CONFIG_ST_LSM6DS3H_IIO_LIMIT_FIFO */
	}

parse_fifo:
	st_lsm6ds3h_parse_fifo_data(cdata, read_len, timestamp_diff, num_pattern);

	return 0;
}

int lsm6ds3h_read_output_data(struct lsm6ds3h_data *cdata, int sindex, bool push)
{
	int err;
	u8 data[6];
	struct iio_dev *indio_dev = cdata->indio_dev[sindex];
	struct lsm6ds3h_sensor_data *sdata = iio_priv(indio_dev);

	err = cdata->tf->read(cdata, sdata->data_out_reg,
				ST_LSM6DS3H_BYTE_FOR_CHANNEL * 3, data, true);
	if (err < 0)
		return err;

	if (push)
		st_lsm6ds3h_push_data_with_timestamp(cdata, sindex,
							data, cdata->timestamp);

	return 0;
}
EXPORT_SYMBOL(lsm6ds3h_read_output_data);

static irqreturn_t st_lsm6ds3h_outdata_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;

	iio_trigger_notify_done(indio_dev->trig);
	return IRQ_HANDLED;
}

static irqreturn_t st_lsm6ds3h_step_counter_trigger_handler(int irq, void *p)
{
	int err;
	u8 steps_data[2];
	int64_t timestamp = 0;
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct lsm6ds3h_sensor_data *sdata = iio_priv(indio_dev);

	if (!sdata->cdata->reset_steps) {
		err = sdata->cdata->tf->read(sdata->cdata,
					(u8)indio_dev->channels[0].address,
					ST_LSM6DS3H_BYTE_FOR_CHANNEL,
					steps_data, true);
		if (err < 0)
			goto st_lsm6ds3h_step_counter_done;

		sdata->cdata->num_steps = (sdata->cdata->num_steps &
			ST_LSM6DS3H_STEP_MASK_64BIT) + *((u16 *)steps_data);
		timestamp = sdata->cdata->timestamp;
	} else {
		sdata->cdata->num_steps = 0;
		timestamp =
			iio_get_time_ns(sdata->cdata->indio_dev[ST_MASK_ID_ACCEL]);
		sdata->cdata->reset_steps = false;
	}

	memcpy(sdata->buffer_data, (u8 *)&sdata->cdata->num_steps, sizeof(u64));

	if (indio_dev->scan_timestamp)
		*(s64 *)((u8 *)sdata->buffer_data +
				ALIGN(ST_LSM6DS3H_BYTE_FOR_CHANNEL,
						sizeof(s64))) = timestamp;

	iio_push_to_buffers(indio_dev, sdata->buffer_data);

st_lsm6ds3h_step_counter_done:
	iio_trigger_notify_done(indio_dev->trig);
	return IRQ_HANDLED;
}

static inline irqreturn_t st_lsm6ds3h_handler_empty(int irq, void *p)
{
	return IRQ_HANDLED;
}

int st_lsm6ds3h_trig_set_state(struct iio_trigger *trig, bool state)
{
	return 0;
}

static int st_lsm6ds3h_buffer_preenable(struct iio_dev *indio_dev)
{
#ifdef CONFIG_ST_LSM6DS3H_XL_DATA_INJECTION
	struct lsm6ds3h_sensor_data *sdata = iio_priv(indio_dev);

	if (sdata->cdata->injection_mode) {
		switch (sdata->sindex) {
		case ST_MASK_ID_ACCEL:
		case ST_MASK_ID_GYRO:
			return -EBUSY;

		default:
			break;
		}
	}
#endif /* CONFIG_ST_LSM6DS3H_XL_DATA_INJECTION */

	return 0;
}

static int st_lsm6ds3h_buffer_postenable(struct iio_dev *indio_dev)
{
	int err;
	struct lsm6ds3h_sensor_data *sdata = iio_priv(indio_dev);

	sdata->cdata->fifo_output[sdata->sindex].initialized = false;

	if ((sdata->cdata->hwfifo_enabled[sdata->sindex]) &&
		(indio_dev->buffer->length < 2 * ST_LSM6DS3H_MAX_FIFO_LENGHT))
		return -EINVAL;

	sdata->buffer_data = kmalloc(indio_dev->scan_bytes, GFP_KERNEL);
	if (!sdata->buffer_data)
		return -ENOMEM;

	mutex_lock(&sdata->cdata->odr_lock);

	err = st_lsm6ds3h_set_enable(sdata, true, true);
	if (err < 0) {
		mutex_unlock(&sdata->cdata->odr_lock);
		return err;
	}

	mutex_unlock(&sdata->cdata->odr_lock);

	if (sdata->sindex == ST_MASK_ID_STEP_COUNTER)
		iio_trigger_poll_chained(sdata->cdata->trig[ST_MASK_ID_STEP_COUNTER]);

	return 0;
}

static int st_lsm6ds3h_buffer_postdisable(struct iio_dev *indio_dev)
{
	int err;
	struct lsm6ds3h_sensor_data *sdata = iio_priv(indio_dev);

	mutex_lock(&sdata->cdata->odr_lock);

	err = st_lsm6ds3h_set_enable(sdata, false, true);

	mutex_unlock(&sdata->cdata->odr_lock);

	kfree(sdata->buffer_data);

	return err < 0 ? err : 0;
}

static const struct iio_buffer_setup_ops st_lsm6ds3h_buffer_setup_ops = {
	.preenable = &st_lsm6ds3h_buffer_preenable,
	.postenable = &st_lsm6ds3h_buffer_postenable,
	.postdisable = &st_lsm6ds3h_buffer_postdisable,
};

int st_lsm6ds3h_allocate_rings(struct lsm6ds3h_data *cdata)
{
	int err;
	struct lsm6ds3h_sensor_data *sdata;

	sdata = iio_priv(cdata->indio_dev[ST_MASK_ID_ACCEL]);

	err = iio_triggered_buffer_setup(cdata->indio_dev[ST_MASK_ID_ACCEL],
				NULL, &st_lsm6ds3h_outdata_trigger_handler,
				&st_lsm6ds3h_buffer_setup_ops);
	if (err < 0)
		return err;

	sdata = iio_priv(cdata->indio_dev[ST_MASK_ID_GYRO]);

	err = iio_triggered_buffer_setup(cdata->indio_dev[ST_MASK_ID_GYRO],
				NULL, &st_lsm6ds3h_outdata_trigger_handler,
				&st_lsm6ds3h_buffer_setup_ops);
	if (err < 0)
		goto buffer_cleanup_accel;

	err = iio_triggered_buffer_setup(
				cdata->indio_dev[ST_MASK_ID_SIGN_MOTION],
				&st_lsm6ds3h_handler_empty, NULL,
				&st_lsm6ds3h_buffer_setup_ops);
	if (err < 0)
		goto buffer_cleanup_gyro;

	err = iio_triggered_buffer_setup(
				cdata->indio_dev[ST_MASK_ID_STEP_COUNTER],
				NULL,
				&st_lsm6ds3h_step_counter_trigger_handler,
				&st_lsm6ds3h_buffer_setup_ops);
	if (err < 0)
		goto buffer_cleanup_sign_motion;

	err = iio_triggered_buffer_setup(
				cdata->indio_dev[ST_MASK_ID_STEP_DETECTOR],
				&st_lsm6ds3h_handler_empty, NULL,
				&st_lsm6ds3h_buffer_setup_ops);
	if (err < 0)
		goto buffer_cleanup_step_counter;

	err = iio_triggered_buffer_setup(
				cdata->indio_dev[ST_MASK_ID_TILT],
				&st_lsm6ds3h_handler_empty, NULL,
				&st_lsm6ds3h_buffer_setup_ops);
	if (err < 0)
		goto buffer_cleanup_step_detector;

	return 0;

buffer_cleanup_step_detector:
	iio_triggered_buffer_cleanup(
				cdata->indio_dev[ST_MASK_ID_STEP_DETECTOR]);
buffer_cleanup_step_counter:
	iio_triggered_buffer_cleanup(
				cdata->indio_dev[ST_MASK_ID_STEP_COUNTER]);
buffer_cleanup_sign_motion:
	iio_triggered_buffer_cleanup(
				cdata->indio_dev[ST_MASK_ID_SIGN_MOTION]);
buffer_cleanup_gyro:
	iio_triggered_buffer_cleanup(cdata->indio_dev[ST_MASK_ID_GYRO]);
buffer_cleanup_accel:
	iio_triggered_buffer_cleanup(cdata->indio_dev[ST_MASK_ID_ACCEL]);
	return err;
}

void st_lsm6ds3h_deallocate_rings(struct lsm6ds3h_data *cdata)
{
	iio_triggered_buffer_cleanup(cdata->indio_dev[ST_MASK_ID_TILT]);
	iio_triggered_buffer_cleanup(
				cdata->indio_dev[ST_MASK_ID_STEP_DETECTOR]);
	iio_triggered_buffer_cleanup(
				cdata->indio_dev[ST_MASK_ID_STEP_COUNTER]);
	iio_triggered_buffer_cleanup(
				cdata->indio_dev[ST_MASK_ID_SIGN_MOTION]);
	iio_triggered_buffer_cleanup(cdata->indio_dev[ST_MASK_ID_ACCEL]);
	iio_triggered_buffer_cleanup(cdata->indio_dev[ST_MASK_ID_GYRO]);
}
