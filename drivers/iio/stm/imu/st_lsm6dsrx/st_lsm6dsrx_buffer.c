// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics st_lsm6dsrx FIFO buffer library driver
 *
 * MEMS Software Solutions Team
 *
 * Copyright 2021 STMicroelectronics Inc.
 */
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/iio/iio.h>
#include <linux/iio/kfifo_buf.h>
#include <linux/iio/events.h>
#include <linux/iio/buffer.h>
#include <asm/unaligned.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/iio/trigger.h>
#include <linux/iio/buffer.h>
#include <linux/of.h>
#include <linux/version.h>

#include "st_lsm6dsrx.h"

#define ST_LSM6DSRX_SAMPLE_DISCHARD			0x7ffd

/* Timestamp convergence filter parameters */
#define ST_LSM6DSRX_EWMA_LEVEL				120
#define ST_LSM6DSRX_EWMA_DIV				128

#define ST_LSM6DSRX_TIMESTAMP_RESET_VALUE	0xaa

/* FIFO tags */
enum {
	ST_LSM6DSRX_GYRO_TAG = 0x01,
	ST_LSM6DSRX_ACC_TAG = 0x02,
	ST_LSM6DSRX_TEMP_TAG = 0x03,
	ST_LSM6DSRX_TS_TAG = 0x04,
	ST_LSM6DSRX_EXT0_TAG = 0x0f,
	ST_LSM6DSRX_EXT1_TAG = 0x10,
	ST_LSM6DSRX_STEPC_TAG = 0x12,
};

/* Default timeout before to re-enable gyro */
static int lsm6dsrx_delay_gyro = 10;
module_param(lsm6dsrx_delay_gyro, int, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
MODULE_PARM_DESC(lsm6dsrx_delay_gyro, "Delay for Gyro arming");
static bool delayed_enable_gyro;

static inline s64 st_lsm6dsrx_ewma(s64 old, s64 new, int weight)
{
	s64 diff, incr;

	diff = new - old;
	incr = div_s64((ST_LSM6DSRX_EWMA_DIV - weight) * diff,
			ST_LSM6DSRX_EWMA_DIV);

	return old + incr;
}

static inline int st_lsm6dsrx_reset_hwts(struct st_lsm6dsrx_hw *hw)
{
	u8 data = ST_LSM6DSRX_TIMESTAMP_RESET_VALUE;
	int ret;

	ret = st_lsm6dsrx_write_locked(hw, ST_LSM6DSRX_REG_TIMESTAMP2_ADDR,
				       data);
	if (ret < 0)
		return ret;

#if defined(CONFIG_IIO_ST_LSM6DSRX_ASYNC_HW_TIMESTAMP)
	spin_lock_irq(&hw->hwtimestamp_lock);
	hw->hw_timestamp_global = (hw->hw_timestamp_global + (1LL << 32)) &
				   GENMASK_ULL(63, 32);
	spin_unlock_irq(&hw->hwtimestamp_lock);
	hw->timesync_c = 0;
	hw->timesync_ktime = ktime_set(0, ST_LSM6DSRX_FAST_KTIME);
#else /* CONFIG_IIO_ST_LSM6DSRX_ASYNC_HW_TIMESTAMP */
	hw->hw_timestamp_global = (hw->hw_timestamp_global + (1LL << 32)) &
				   GENMASK_ULL(63, 32);
#endif /* CONFIG_IIO_ST_LSM6DSRX_ASYNC_HW_TIMESTAMP */

	hw->ts = iio_get_time_ns(hw->iio_devs[0]);
	hw->ts_offset = hw->ts;
	hw->tsample = 0ull;

	return 0;
}

int st_lsm6dsrx_set_fifo_mode(struct st_lsm6dsrx_hw *hw,
			      enum st_lsm6dsrx_fifo_mode fifo_mode)
{
	int err;

	err = st_lsm6dsrx_write_with_mask_locked(hw,
					      ST_LSM6DSRX_REG_FIFO_CTRL4_ADDR,
					      ST_LSM6DSRX_REG_FIFO_MODE_MASK,
					      fifo_mode);
	if (err < 0)
		return err;

	hw->fifo_mode = fifo_mode;

	return 0;
}

static inline int
st_lsm6dsrx_set_sensor_batching_odr(struct st_lsm6dsrx_sensor *sensor,
				    bool enable)
{
	struct st_lsm6dsrx_hw *hw = sensor->hw;
	enum st_lsm6dsrx_sensor_id id = sensor->id;
	u8 data = 0;
	int err;

	if (enable) {
		err = st_lsm6dsrx_get_batch_val(sensor, sensor->odr,
						sensor->uodr, &data);
		if (err < 0)
			return err;
	}

	return st_lsm6dsrx_update_bits_locked(hw,
				hw->odr_table_entry[id].batching_reg.addr,
				hw->odr_table_entry[id].batching_reg.mask,
				data);
}

int st_lsm6dsrx_update_watermark(struct st_lsm6dsrx_sensor *sensor,
				 u16 watermark)
{
	u16 fifo_watermark = ST_LSM6DSRX_MAX_FIFO_DEPTH, cur_watermark = 0;
	struct st_lsm6dsrx_hw *hw = sensor->hw;
	struct st_lsm6dsrx_sensor *cur_sensor;
	__le16 wdata;
	int i, err;
	int data = 0;

	for (i = 0; i < ARRAY_SIZE(st_lsm6dsrx_buffered_sensor_list);
	     i++) {
		enum st_lsm6dsrx_sensor_id id =
				    st_lsm6dsrx_buffered_sensor_list[i];

		if (!hw->iio_devs[id])
			continue;

		cur_sensor = iio_priv(hw->iio_devs[id]);

		if (!(hw->enable_mask & BIT_ULL(cur_sensor->id)))
			continue;

		cur_watermark = (cur_sensor == sensor) ? watermark
						       : cur_sensor->watermark;

		fifo_watermark = min_t(u16, fifo_watermark, cur_watermark);
	}

	fifo_watermark = max_t(u16, fifo_watermark, 2);

	mutex_lock(&hw->page_lock);
	err = regmap_read(hw->regmap,
			  ST_LSM6DSRX_REG_FIFO_CTRL1_ADDR + 1, &data);
	if (err < 0)
		goto out;

	fifo_watermark = ((data << 8) & ~ST_LSM6DSRX_REG_FIFO_WTM_MASK) |
			 (fifo_watermark & ST_LSM6DSRX_REG_FIFO_WTM_MASK);
	wdata = cpu_to_le16(fifo_watermark);

	err = regmap_bulk_write(hw->regmap, ST_LSM6DSRX_REG_FIFO_CTRL1_ADDR,
				&wdata, sizeof(wdata));
out:
	mutex_unlock(&hw->page_lock);

	return err;
}

static struct
iio_dev *st_lsm6dsrx_get_iiodev_from_tag(struct st_lsm6dsrx_hw *hw, u8 tag)
{
	struct iio_dev *iio_dev;

	switch (tag) {
	case ST_LSM6DSRX_GYRO_TAG:
		iio_dev = hw->iio_devs[ST_LSM6DSRX_ID_GYRO];
		break;
	case ST_LSM6DSRX_ACC_TAG:
		iio_dev = hw->iio_devs[ST_LSM6DSRX_ID_ACC];
		break;
	case ST_LSM6DSRX_TEMP_TAG:
		iio_dev = hw->iio_devs[ST_LSM6DSRX_ID_TEMP];
		break;
	case ST_LSM6DSRX_EXT0_TAG:
		if (hw->enable_mask & BIT_ULL(ST_LSM6DSRX_ID_EXT0))
			iio_dev = hw->iio_devs[ST_LSM6DSRX_ID_EXT0];
		else
			iio_dev = hw->iio_devs[ST_LSM6DSRX_ID_EXT1];
		break;
	case ST_LSM6DSRX_EXT1_TAG:
		iio_dev = hw->iio_devs[ST_LSM6DSRX_ID_EXT1];
		break;
	case ST_LSM6DSRX_STEPC_TAG:
		iio_dev = hw->iio_devs[ST_LSM6DSRX_ID_STEP_COUNTER];
		break;
	default:
		iio_dev = NULL;
		break;
	}

	return iio_dev;
}

static int st_lsm6dsrx_read_fifo(struct st_lsm6dsrx_hw *hw)
{
	u8 iio_buf[ALIGN(ST_LSM6DSRX_FIFO_SAMPLE_SIZE, sizeof(s64)) +
		   sizeof(s64) + sizeof(s64)];
	u8 buf[6 * ST_LSM6DSRX_FIFO_SAMPLE_SIZE], tag, *ptr;
	int i, err, word_len, fifo_len, read_len;
	struct st_lsm6dsrx_sensor *sensor;
	__le64 hw_timestamp_push;
	struct iio_dev *iio_dev;
	s64 ts_irq, hw_ts_old;
	__le16 fifo_status;
	u16 fifo_depth;
	s16 drdymask;
	u32 val;

	/* return if FIFO is already disabled */
	if (hw->fifo_mode == ST_LSM6DSRX_FIFO_BYPASS)
		return 0;

	ts_irq = hw->ts - hw->delta_ts;

	err = st_lsm6dsrx_read_locked(hw, ST_LSM6DSRX_REG_FIFO_STATUS1_ADDR,
				      &fifo_status, sizeof(fifo_status));
	if (err < 0)
		return err;

	fifo_depth = le16_to_cpu(fifo_status) &
		     ST_LSM6DSRX_REG_FIFO_STATUS_DIFF;
	if (!fifo_depth)
		return 0;

	fifo_len = fifo_depth * ST_LSM6DSRX_FIFO_SAMPLE_SIZE;
	read_len = 0;

	while (read_len < fifo_len) {
		word_len = min_t(int, fifo_len - read_len, sizeof(buf));
		err = st_lsm6dsrx_read_locked(hw,
					ST_LSM6DSRX_REG_FIFO_DATA_OUT_TAG_ADDR,
					buf, word_len);
		if (err < 0)
			return err;

		for (i = 0; i < word_len; i += ST_LSM6DSRX_FIFO_SAMPLE_SIZE) {
			ptr = &buf[i + ST_LSM6DSRX_TAG_SIZE];
			tag = buf[i] >> 3;

			if (tag == ST_LSM6DSRX_TS_TAG) {
				val = get_unaligned_le32(ptr);

#if defined(CONFIG_IIO_ST_LSM6DSRX_ASYNC_HW_TIMESTAMP)
				spin_lock_irq(&hw->hwtimestamp_lock);
#endif /* CONFIG_IIO_ST_LSM6DSRX_ASYNC_HW_TIMESTAMP */

				hw->hw_timestamp_global =
						(hw->hw_timestamp_global &
						 GENMASK_ULL(63, 32)) |
						(u32)le32_to_cpu(val);

#if defined(CONFIG_IIO_ST_LSM6DSRX_ASYNC_HW_TIMESTAMP)
				spin_unlock_irq(&hw->hwtimestamp_lock);
#endif /* CONFIG_IIO_ST_LSM6DSRX_ASYNC_HW_TIMESTAMP */

				hw_ts_old = hw->hw_ts;
				hw->hw_ts = val * hw->ts_delta_ns;
				hw->ts_offset = st_lsm6dsrx_ewma(hw->ts_offset,
						ts_irq - hw->hw_ts,
						ST_LSM6DSRX_EWMA_LEVEL);
				ts_irq += hw->hw_ts;

				if (!hw->tsample)
					hw->tsample = hw->ts_offset + hw->hw_ts;
				else
					hw->tsample = hw->tsample + hw->hw_ts -
						      hw_ts_old;
			} else {
				iio_dev = st_lsm6dsrx_get_iiodev_from_tag(hw,
									  tag);
				if (!iio_dev)
					continue;

				sensor = iio_priv(iio_dev);

				/* Skip samples if not ready */
				drdymask = (s16)le16_to_cpu(get_unaligned_le16(ptr));
				if (unlikely(drdymask >=
				    ST_LSM6DSRX_SAMPLE_DISCHARD)) {
					continue;
				}

				/*
				 * hw ts in not queued in FIFO if only step
				 * counter enabled
				 */
				if (sensor->id == ST_LSM6DSRX_ID_STEP_COUNTER) {
					val = get_unaligned_le32(ptr + 2);
					hw->tsample = val * hw->ts_delta_ns;
				}
				memcpy(iio_buf, ptr, ST_LSM6DSRX_SAMPLE_SIZE);

#if defined(CONFIG_IIO_ST_LSM6DSRX_ASYNC_HW_TIMESTAMP)
					spin_lock_irq(&hw->hwtimestamp_lock);
#endif /* CONFIG_IIO_ST_LSM6DSRX_ASYNC_HW_TIMESTAMP */

				hw_timestamp_push = cpu_to_le64(hw->hw_timestamp_global);

#if defined(CONFIG_IIO_ST_LSM6DSRX_ASYNC_HW_TIMESTAMP)
					spin_unlock_irq(&hw->hwtimestamp_lock);
#endif /* CONFIG_IIO_ST_LSM6DSRX_ASYNC_HW_TIMESTAMP */

				memcpy(&iio_buf[ALIGN(ST_LSM6DSRX_SAMPLE_SIZE, sizeof(s64))],
				       &hw_timestamp_push, sizeof(hw_timestamp_push));
				hw->tsample = min_t(s64,
					iio_get_time_ns(hw->iio_devs[0]),
					hw->tsample);

				sensor->last_fifo_timestamp = hw_timestamp_push;

				/* support decimation for ODR < 12.5 Hz */
				if (sensor->dec_counter > 0) {
					sensor->dec_counter--;
				} else {
					sensor->dec_counter = sensor->decimator;
					iio_push_to_buffers_with_timestamp(iio_dev,
								iio_buf,
								hw->tsample);
				}
			}
		}
		read_len += word_len;
	}

	return read_len;
}

ssize_t st_lsm6dsrx_get_max_watermark(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct iio_dev *iio_dev = dev_to_iio_dev(dev);
	struct st_lsm6dsrx_sensor *sensor = iio_priv(iio_dev);

	return sprintf(buf, "%d\n", sensor->max_watermark);
}

ssize_t st_lsm6dsrx_get_watermark(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct iio_dev *iio_dev = dev_to_iio_dev(dev);
	struct st_lsm6dsrx_sensor *sensor = iio_priv(iio_dev);

	return sprintf(buf, "%d\n", sensor->watermark);
}

ssize_t st_lsm6dsrx_set_watermark(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t size)
{
	struct iio_dev *iio_dev = dev_to_iio_dev(dev);
	struct st_lsm6dsrx_sensor *sensor = iio_priv(iio_dev);
	int err, val;

	err = iio_device_claim_direct_mode(iio_dev);
	if (err)
		return err;

	err = kstrtoint(buf, 10, &val);
	if (err < 0)
		goto out;

	err = st_lsm6dsrx_update_watermark(sensor, val);
	if (err < 0)
		goto out;

	sensor->watermark = val;
	iio_device_release_direct_mode(iio_dev);

out:
	return err < 0 ? err : size;
}

ssize_t st_lsm6dsrx_flush_fifo(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t size)
{
	struct iio_dev *iio_dev = dev_to_iio_dev(dev);
	struct st_lsm6dsrx_sensor *sensor = iio_priv(iio_dev);
	struct st_lsm6dsrx_hw *hw = sensor->hw;
	s64 type;
	s64 event;
	int count;
	s64 fts;
	s64 ts;

	mutex_lock(&hw->fifo_lock);
	ts = iio_get_time_ns(iio_dev);
	hw->delta_ts = ts - hw->ts;
	hw->ts = ts;
	set_bit(ST_LSM6DSRX_HW_FLUSH, &hw->state);
	count = st_lsm6dsrx_read_fifo(hw);
	sensor->dec_counter = 0;
	fts = sensor->last_fifo_timestamp;
	mutex_unlock(&hw->fifo_lock);

	type = count > 0 ? STM_IIO_EV_DIR_FIFO_DATA : STM_IIO_EV_DIR_FIFO_EMPTY;
	event = IIO_UNMOD_EVENT_CODE(iio_dev->channels[0].type, -1,
				     STM_IIO_EV_TYPE_FIFO_FLUSH, type);
	iio_push_event(iio_dev, event, fts);

	return size;
}

int st_lsm6dsrx_suspend_fifo(struct st_lsm6dsrx_hw *hw)
{
	int err;

	mutex_lock(&hw->fifo_lock);
	st_lsm6dsrx_read_fifo(hw);
	err = st_lsm6dsrx_set_fifo_mode(hw, ST_LSM6DSRX_FIFO_BYPASS);
	mutex_unlock(&hw->fifo_lock);

	return err;
}

int st_lsm6dsrx_update_batching(struct iio_dev *iio_dev, bool enable)
{
	struct st_lsm6dsrx_sensor *sensor = iio_priv(iio_dev);
	struct st_lsm6dsrx_hw *hw = sensor->hw;
	int err;

	disable_irq(hw->irq);
	err = st_lsm6dsrx_set_sensor_batching_odr(sensor, enable);
	enable_irq(hw->irq);

	return err;
}

static int st_lsm6dsrx_update_fifo(struct iio_dev *iio_dev, bool enable)
{
	struct st_lsm6dsrx_sensor *sensor = iio_priv(iio_dev);
	struct st_lsm6dsrx_hw *hw = sensor->hw;
	int err;

	if (sensor->id == ST_LSM6DSRX_ID_GYRO && !enable)
		delayed_enable_gyro = true;

	if (sensor->id == ST_LSM6DSRX_ID_GYRO &&
	    enable && delayed_enable_gyro) {
		delayed_enable_gyro = false;
		msleep(lsm6dsrx_delay_gyro);
	}

	disable_irq(hw->irq);

#if defined(CONFIG_IIO_ST_LSM6DSRX_ASYNC_HW_TIMESTAMP)
	hrtimer_cancel(&hw->timesync_timer);
	cancel_work_sync(&hw->timesync_work);
#endif /* CONFIG_IIO_ST_LSM6DSRX_ASYNC_HW_TIMESTAMP */

	if (sensor->id == ST_LSM6DSRX_ID_EXT0 ||
	    sensor->id == ST_LSM6DSRX_ID_EXT1) {
		err = st_lsm6dsrx_shub_set_enable(sensor, enable);
		if (err < 0)
			goto out;
	} else {
		if (sensor->id == ST_LSM6DSRX_ID_STEP_COUNTER) {
			err = st_lsm6dsrx_step_counter_set_enable(sensor,
								  enable);
			if (err < 0)
				goto out;
		} else {
			err = st_lsm6dsrx_sensor_set_enable(sensor,
							    enable);
			if (err < 0)
				goto out;

			err = st_lsm6dsrx_set_sensor_batching_odr(sensor,
								  enable);
			if (err < 0)
				goto out;
		}
	}

	/*
	 * this is an auxiliary sensor, it need to get batched
	 * toghether at least with a primary sensor (Acc/Gyro).
	 */
	if (sensor->id == ST_LSM6DSRX_ID_TEMP) {
		if (!(hw->enable_mask & (BIT_ULL(ST_LSM6DSRX_ID_ACC) |
					 BIT_ULL(ST_LSM6DSRX_ID_GYRO)))) {
			struct st_lsm6dsrx_sensor *acc_sensor;
			u8 data = 0;

			acc_sensor = iio_priv(hw->iio_devs[ST_LSM6DSRX_ID_ACC]);
			if (enable) {
				err = st_lsm6dsrx_get_batch_val(acc_sensor,
								sensor->odr,
								sensor->uodr,
								&data);
				if (err < 0)
					goto out;
			}

			err = st_lsm6dsrx_update_bits_locked(hw,
				hw->odr_table_entry[ST_LSM6DSRX_ID_ACC].batching_reg.addr,
				hw->odr_table_entry[ST_LSM6DSRX_ID_ACC].batching_reg.mask,
				data);
			if (err < 0)
				goto out;
		}
	}

	err = st_lsm6dsrx_update_watermark(sensor, sensor->watermark);
	if (err < 0)
		goto out;

	if (enable && hw->fifo_mode == ST_LSM6DSRX_FIFO_BYPASS) {
		st_lsm6dsrx_reset_hwts(hw);
		err = st_lsm6dsrx_set_fifo_mode(hw, ST_LSM6DSRX_FIFO_CONT);
	} else if (!hw->enable_mask) {
		err = st_lsm6dsrx_set_fifo_mode(hw, ST_LSM6DSRX_FIFO_BYPASS);
	}

#if defined(CONFIG_IIO_ST_LSM6DSRX_ASYNC_HW_TIMESTAMP)
	if (hw->fifo_mode != ST_LSM6DSRX_FIFO_BYPASS) {
		hrtimer_start(&hw->timesync_timer,
			      ktime_set(0, 0),
			      HRTIMER_MODE_REL);
	}
#endif /* CONFIG_IIO_ST_LSM6DSRX_ASYNC_HW_TIMESTAMP */

out:
	enable_irq(hw->irq);

	return err;
}

static irqreturn_t st_lsm6dsrx_handler_irq(int irq, void *private)
{
	struct st_lsm6dsrx_hw *hw = (struct st_lsm6dsrx_hw *)private;
	s64 ts = iio_get_time_ns(hw->iio_devs[0]);

	hw->delta_ts = ts - hw->ts;
	hw->ts = ts;

	return IRQ_WAKE_THREAD;
}

static irqreturn_t st_lsm6dsrx_handler_thread(int irq, void *private)
{
	struct st_lsm6dsrx_hw *hw = (struct st_lsm6dsrx_hw *)private;

	if (st_lsm6dsrx_run_mlc_task(hw))
		st_lsm6dsrx_mlc_check_status(hw);

	mutex_lock(&hw->fifo_lock);
	st_lsm6dsrx_read_fifo(hw);
	clear_bit(ST_LSM6DSRX_HW_FLUSH, &hw->state);
	mutex_unlock(&hw->fifo_lock);

	st_lsm6dsrx_event_handler(hw);
	st_lsm6dsrx_embfunc_handler_thread(hw);

	return IRQ_HANDLED;
}

static int st_lsm6dsrx_fifo_preenable(struct iio_dev *iio_dev)
{
	return st_lsm6dsrx_update_fifo(iio_dev, true);
}

static int st_lsm6dsrx_fifo_postdisable(struct iio_dev *iio_dev)
{
	return st_lsm6dsrx_update_fifo(iio_dev, false);
}

static const struct iio_buffer_setup_ops st_lsm6dsrx_fifo_ops = {
	.preenable = st_lsm6dsrx_fifo_preenable,
	.postdisable = st_lsm6dsrx_fifo_postdisable,
};

int st_lsm6dsrx_buffers_setup(struct st_lsm6dsrx_hw *hw)
{
	struct device_node *np = hw->dev->of_node;
#if LINUX_VERSION_CODE < KERNEL_VERSION(5,13,0)
	struct iio_buffer *buffer;
#endif /* LINUX_VERSION_CODE */
	unsigned long irq_type;
	bool irq_active_low;
	int i, err;

	irq_type = irqd_get_trigger_type(irq_get_irq_data(hw->irq));
	if (irq_type == IRQF_TRIGGER_NONE)
		irq_type = IRQF_TRIGGER_HIGH;

	switch (irq_type) {
	case IRQF_TRIGGER_HIGH:
	case IRQF_TRIGGER_RISING:
		irq_active_low = false;
		break;
	case IRQF_TRIGGER_LOW:
	case IRQF_TRIGGER_FALLING:
		irq_active_low = true;
		break;
	default:
		dev_info(hw->dev, "mode %lx unsupported\n", irq_type);
		return -EINVAL;
	}

	err = regmap_update_bits(hw->regmap,
				 ST_LSM6DSRX_REG_CTRL3_C_ADDR,
				 ST_LSM6DSRX_REG_H_LACTIVE_MASK,
				 FIELD_PREP(ST_LSM6DSRX_REG_H_LACTIVE_MASK,
				 irq_active_low));
	if (err < 0)
		return err;

	if (np && of_property_read_bool(np, "drive-open-drain")) {
		err = regmap_update_bits(hw->regmap,
					 ST_LSM6DSRX_REG_CTRL3_C_ADDR,
					 ST_LSM6DSRX_REG_PP_OD_MASK,
					 FIELD_PREP(ST_LSM6DSRX_REG_PP_OD_MASK, 1));
		if (err < 0)
			return err;

		irq_type |= IRQF_SHARED;
	}

	err = devm_request_threaded_irq(hw->dev, hw->irq,
					st_lsm6dsrx_handler_irq,
					st_lsm6dsrx_handler_thread,
					irq_type | IRQF_ONESHOT,
					hw->settings->id.name, hw);
	if (err) {
		dev_err(hw->dev, "failed to request trigger irq %d\n",
			hw->irq);
		return err;
	}

	for (i = 0; i < ARRAY_SIZE(st_lsm6dsrx_buffered_sensor_list);
	     i++) {
		enum st_lsm6dsrx_sensor_id id = st_lsm6dsrx_buffered_sensor_list[i];

		if (!hw->iio_devs[id])
			continue;

#if KERNEL_VERSION(5, 19, 0) <= LINUX_VERSION_CODE
		err = devm_iio_kfifo_buffer_setup(hw->dev, hw->iio_devs[id],
						  &st_lsm6dsrx_fifo_ops);
		if (err)
			return err;
#elif KERNEL_VERSION(5, 13, 0) <= LINUX_VERSION_CODE
		err = devm_iio_kfifo_buffer_setup(hw->dev, hw->iio_devs[id],
						  INDIO_BUFFER_SOFTWARE,
						  &st_lsm6dsrx_fifo_ops);
		if (err)
			return err;
#else /* LINUX_VERSION_CODE */
		buffer = devm_iio_kfifo_allocate(hw->dev);
		if (!buffer)
			return -ENOMEM;

		iio_device_attach_buffer(hw->iio_devs[id], buffer);
		hw->iio_devs[id]->modes |= INDIO_BUFFER_SOFTWARE;
		hw->iio_devs[id]->setup_ops = &st_lsm6dsrx_fifo_ops;
#endif /* LINUX_VERSION_CODE */

	}

	err = st_lsm6dsrx_hwtimesync_init(hw);
	if (err)
		return err;

	return regmap_update_bits(hw->regmap,
				  ST_LSM6DSRX_REG_FIFO_CTRL4_ADDR,
				  ST_LSM6DSRX_REG_DEC_TS_MASK,
				  FIELD_PREP(ST_LSM6DSRX_REG_DEC_TS_MASK, 1));
}
