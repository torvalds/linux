// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics st_asm330lhhx FIFO buffer library driver
 *
 * MEMS Software Solutions Team
 *
 * Copyright 2019 STMicroelectronics Inc.
 */
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/iio/iio.h>
#include <linux/iio/kfifo_buf.h>
#include <linux/iio/events.h>
#include <linux/iio/buffer.h>
#include <asm/unaligned.h>
#include <linux/iio/buffer.h>
#include <linux/version.h>

#include "st_asm330lhhx.h"

#define ST_ASM330LHHX_REG_FIFO_STATUS1_ADDR		0x3a
#define ST_ASM330LHHX_REG_TIMESTAMP2_ADDR		0x42
#define ST_ASM330LHHX_REG_FIFO_DATA_OUT_TAG_ADDR	0x78

#define ST_ASM330LHHX_SAMPLE_DISCHARD			0x7ffd

/* Timestamp convergence filter parameter */
#define ST_ASM330LHHX_EWMA_LEVEL			120
#define ST_ASM330LHHX_EWMA_DIV				128

#define ST_ASM330LHHX_TIMESTAMP_RESET_VALUE		0xaa

enum {
	ST_ASM330LHHX_GYRO_TAG = 0x01,
	ST_ASM330LHHX_ACC_TAG = 0x02,
	ST_ASM330LHHX_TEMP_TAG = 0x03,
	ST_ASM330LHHX_TS_TAG = 0x04,
	ST_ASM330LHHX_EXT0_TAG = 0x0f,
	ST_ASM330LHHX_EXT1_TAG = 0x10,
};

/* Default timeout before to re-enable gyro */
int delay_gyro = 10;
module_param(delay_gyro, int, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
MODULE_PARM_DESC(delay_gyro, "Delay for Gyro arming");
static bool delayed_enable_gyro;

static inline s64 st_asm330lhhx_ewma(s64 old, s64 new, int weight)
{
	s64 diff, incr;

	diff = new - old;
	incr = div_s64((ST_ASM330LHHX_EWMA_DIV - weight) * diff,
		       ST_ASM330LHHX_EWMA_DIV);

	return old + incr;
}

inline int st_asm330lhhx_reset_hwts(struct st_asm330lhhx_hw *hw)
{
	u8 data = ST_ASM330LHHX_TIMESTAMP_RESET_VALUE;
	int ret;

	ret = st_asm330lhhx_write_locked(hw, ST_ASM330LHHX_REG_TIMESTAMP2_ADDR,
				     data);
	if (ret < 0)
		return ret;

#if defined(CONFIG_IIO_ST_ASM330LHHX_ASYNC_HW_TIMESTAMP)
	spin_lock_irq(&hw->hwtimestamp_lock);
	hw->hw_timestamp_global = (hw->hw_timestamp_global + (1LL << 32)) &
				  GENMASK_ULL(63, 32);
	spin_unlock_irq(&hw->hwtimestamp_lock);
	hw->timesync_c = 0;
	hw->timesync_ktime = ktime_set(0, ST_ASM330LHHX_FAST_KTIME);
#else /* CONFIG_IIO_ST_ASM330LHHX_ASYNC_HW_TIMESTAMP */
	hw->hw_timestamp_global = (hw->hw_timestamp_global + (1LL << 32)) &
				  GENMASK_ULL(63, 32);
#endif /* CONFIG_IIO_ST_ASM330LHHX_ASYNC_HW_TIMESTAMP */

	hw->ts = st_asm330lhhx_get_time_ns(hw->iio_devs[0]);
	hw->ts_offset = hw->ts;
	hw->val_ts_old = 0;
	hw->hw_ts_high = 0;
	hw->tsample = 0ull;

	return 0;
}

int st_asm330lhhx_set_fifo_mode(struct st_asm330lhhx_hw *hw,
				enum st_asm330lhhx_fifo_mode fifo_mode)
{
	int err;

	err = st_asm330lhhx_write_with_mask_locked(hw,
					  ST_ASM330LHHX_REG_FIFO_CTRL4_ADDR,
					  ST_ASM330LHHX_REG_FIFO_MODE_MASK,
					  fifo_mode);
	if (err < 0)
		return err;

	hw->fifo_mode = fifo_mode;

	if (fifo_mode == ST_ASM330LHHX_FIFO_BYPASS)
		clear_bit(ST_ASM330LHHX_HW_OPERATIONAL, &hw->state);
	else
		set_bit(ST_ASM330LHHX_HW_OPERATIONAL, &hw->state);

	return 0;
}

static inline int
st_asm330lhhx_set_sensor_batching_odr(struct st_asm330lhhx_sensor *s,
					    bool enable)
{
	enum st_asm330lhhx_sensor_id id = s->id;
	struct st_asm330lhhx_hw *hw = s->hw;
	u8 data = 0;
	int err;

	if (enable) {
		err = st_asm330lhhx_get_batch_val(s, s->odr, s->uodr, &data);
		if (err < 0)
			return err;
	}

	return st_asm330lhhx_update_bits_locked(hw,
			      hw->odr_table_entry[id].batching_reg.addr,
			      hw->odr_table_entry[id].batching_reg.mask,
			      data);
}

int st_asm330lhhx_update_watermark(struct st_asm330lhhx_sensor *sensor,
				   u16 watermark)
{
	u16 fifo_watermark = ST_ASM330LHHX_MAX_FIFO_DEPTH, cur_watermark = 0;
	struct st_asm330lhhx_hw *hw = sensor->hw;
	struct st_asm330lhhx_sensor *cur_sensor;
	__le16 wdata;
	int data = 0;
	int i, err;

	for (i = ST_ASM330LHHX_ID_GYRO; i <= ST_ASM330LHHX_ID_EXT1; i++) {
		if (!hw->iio_devs[i])
			continue;

		cur_sensor = iio_priv(hw->iio_devs[i]);

		if (!(hw->enable_mask & BIT_ULL(cur_sensor->id)))
			continue;

		cur_watermark = (cur_sensor == sensor) ? watermark
						       : cur_sensor->watermark;

		fifo_watermark = min_t(u16, fifo_watermark, cur_watermark);
	}

	fifo_watermark = max_t(u16, fifo_watermark, 2);

	mutex_lock(&hw->page_lock);
	err = regmap_read(hw->regmap, ST_ASM330LHHX_REG_FIFO_CTRL1_ADDR + 1,
			  &data);
	if (err < 0)
		goto out;

	fifo_watermark = ((data << 8) & ~ST_ASM330LHHX_REG_FIFO_WTM_MASK) |
			 (fifo_watermark & ST_ASM330LHHX_REG_FIFO_WTM_MASK);
	wdata = cpu_to_le16(fifo_watermark);

	err = regmap_bulk_write(hw->regmap,
				ST_ASM330LHHX_REG_FIFO_CTRL1_ADDR,
				&wdata, sizeof(wdata));
out:
	mutex_unlock(&hw->page_lock);

	return err < 0 ? err : 0;
}

static struct iio_dev *st_asm330lhhx_get_iiodev_from_tag(struct st_asm330lhhx_hw *hw,
							 u8 tag)
{
	struct iio_dev *iio_dev;

	switch (tag) {
	case ST_ASM330LHHX_GYRO_TAG:
		iio_dev = hw->iio_devs[ST_ASM330LHHX_ID_GYRO];
		break;
	case ST_ASM330LHHX_ACC_TAG:
		iio_dev = hw->iio_devs[ST_ASM330LHHX_ID_ACC];
		break;
	case ST_ASM330LHHX_TEMP_TAG:
		iio_dev = hw->iio_devs[ST_ASM330LHHX_ID_TEMP];
		break;
	case ST_ASM330LHHX_EXT0_TAG:
		if (hw->enable_mask & BIT_ULL(ST_ASM330LHHX_ID_EXT0))
			iio_dev = hw->iio_devs[ST_ASM330LHHX_ID_EXT0];
		else
			iio_dev = hw->iio_devs[ST_ASM330LHHX_ID_EXT1];
		break;
	case ST_ASM330LHHX_EXT1_TAG:
		iio_dev = hw->iio_devs[ST_ASM330LHHX_ID_EXT1];
		break;
	default:
		iio_dev = NULL;
		break;
	}

	return iio_dev;
}

static inline void st_asm330lhhx_sync_hw_ts(struct st_asm330lhhx_hw *hw, s64 ts)
{
	s64 delta = ts - hw->hw_ts;

	hw->ts_offset = st_asm330lhhx_ewma(hw->ts_offset, delta,
					  ST_ASM330LHHX_EWMA_LEVEL);
}

static int st_asm330lhhx_read_fifo(struct st_asm330lhhx_hw *hw)
{
	u8 iio_buf[ALIGN(ST_ASM330LHHX_SAMPLE_SIZE, sizeof(s64)) +
		   sizeof(s64) + sizeof(s64)];
	u8 buf[6 * ST_ASM330LHHX_FIFO_SAMPLE_SIZE], tag, *ptr;
	int i, err, word_len, fifo_len, read_len;
	__le64 hw_timestamp_push;
	struct iio_dev *iio_dev;
	s64 ts_irq, hw_ts_old;
	__le16 fifo_status;
	u16 fifo_depth;
	s16 drdymask;
	u32 val;

	/* return if FIFO is already disabled */
	if (!test_bit(ST_ASM330LHHX_HW_OPERATIONAL, &hw->state)) {
		dev_warn(hw->dev, "%s: FIFO in bypass mode\n", __func__);

		return 0;
	}

	ts_irq = hw->ts - hw->delta_ts;

	err = st_asm330lhhx_read_locked(hw, ST_ASM330LHHX_REG_FIFO_STATUS1_ADDR,
				    &fifo_status, sizeof(fifo_status));
	if (err < 0)
		return err;

	fifo_depth = le16_to_cpu(fifo_status) & ST_ASM330LHHX_REG_FIFO_STATUS_DIFF;
	if (!fifo_depth)
		return 0;

	fifo_len = fifo_depth * ST_ASM330LHHX_FIFO_SAMPLE_SIZE;
	read_len = 0;
	while (read_len < fifo_len) {
		word_len = min_t(int, fifo_len - read_len, sizeof(buf));
		err = st_asm330lhhx_read_locked(hw,
				   ST_ASM330LHHX_REG_FIFO_DATA_OUT_TAG_ADDR,
				   buf, word_len);
		if (err < 0)
			return err;

		for (i = 0; i < word_len; i += ST_ASM330LHHX_FIFO_SAMPLE_SIZE) {
			ptr = &buf[i + ST_ASM330LHHX_TAG_SIZE];
			tag = buf[i] >> 3;

			if (tag == ST_ASM330LHHX_TS_TAG) {
				val = get_unaligned_le32(ptr);

#if defined(CONFIG_IIO_ST_ASM330LHHX_ASYNC_HW_TIMESTAMP)
				spin_lock_irq(&hw->hwtimestamp_lock);
#endif /* CONFIG_IIO_ST_ASM330LHHX_ASYNC_HW_TIMESTAMP */
				hw->hw_timestamp_global =
					(hw->hw_timestamp_global &
					 GENMASK_ULL(63, 32)) |
					(u32)le32_to_cpu(get_unaligned_le32(ptr));
#if defined(CONFIG_IIO_ST_ASM330LHHX_ASYNC_HW_TIMESTAMP)
				spin_unlock_irq(&hw->hwtimestamp_lock);
#endif /* CONFIG_IIO_ST_ASM330LHHX_ASYNC_HW_TIMESTAMP */

				if (hw->val_ts_old > val)
					hw->hw_ts_high++;

				hw_ts_old = hw->hw_ts;

				/* check hw rollover */
				hw->val_ts_old = val;
				hw->hw_ts = (val + ((s64)hw->hw_ts_high << 32)) *
					    hw->ts_delta_ns;
				hw->ts_offset = st_asm330lhhx_ewma(hw->ts_offset,
						ts_irq - hw->hw_ts,
						ST_ASM330LHHX_EWMA_LEVEL);

				if (!test_bit(ST_ASM330LHHX_HW_FLUSH, &hw->state))
					/* sync ap timestamp and sensor one */
					st_asm330lhhx_sync_hw_ts(hw, ts_irq);

				ts_irq += hw->hw_ts;

				if (!hw->tsample)
					hw->tsample = hw->ts_offset + hw->hw_ts;
				else
					hw->tsample = hw->tsample + hw->hw_ts - hw_ts_old;
			} else {
				struct st_asm330lhhx_sensor *sensor;

				iio_dev = st_asm330lhhx_get_iiodev_from_tag(hw, tag);
				if (!iio_dev)
					continue;

				sensor = iio_priv(iio_dev);

				/* skip samples if not ready */
				drdymask = (s16)le16_to_cpu(get_unaligned_le16(ptr));
				if (unlikely(drdymask >= ST_ASM330LHHX_SAMPLE_DISCHARD)) {
#ifdef ST_ASM330LHHX_DEBUG_DISCHARGE
					sensor->discharged_samples++;
#endif /* ST_ASM330LHHX_DEBUG_DISCHARGE */
					continue;
				}

				memcpy(iio_buf, ptr, ST_ASM330LHHX_SAMPLE_SIZE);
#if defined(CONFIG_IIO_ST_ASM330LHHX_ASYNC_HW_TIMESTAMP)
				spin_lock_irq(&hw->hwtimestamp_lock);
#endif /* CONFIG_IIO_ST_ASM330LHHX_ASYNC_HW_TIMESTAMP */
				hw_timestamp_push = cpu_to_le64(hw->hw_timestamp_global);
#if defined(CONFIG_IIO_ST_ASM330LHHX_ASYNC_HW_TIMESTAMP)
				spin_unlock_irq(&hw->hwtimestamp_lock);
#endif /* CONFIG_IIO_ST_ASM330LHHX_ASYNC_HW_TIMESTAMP */
				memcpy(&iio_buf[ALIGN(ST_ASM330LHHX_SAMPLE_SIZE, sizeof(s64))],
				       &hw_timestamp_push, sizeof(hw_timestamp_push));

				hw->tsample = min_t(s64,
						    st_asm330lhhx_get_time_ns(hw->iio_devs[0]),
						    hw->tsample);

				/* support decimation for ODR < 12.5 Hz */
				if (sensor->dec_counter > 0) {
					sensor->dec_counter--;
				} else {
					sensor->dec_counter = sensor->decimator;
					iio_push_to_buffers_with_timestamp(iio_dev,
								iio_buf,
								hw->tsample);
					sensor->last_fifo_timestamp = hw_timestamp_push;
				}
			}
		}
		read_len += word_len;
	}

	return read_len;
}

ssize_t st_asm330lhhx_get_max_watermark(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct st_asm330lhhx_sensor *sensor = iio_priv(dev_to_iio_dev(dev));

	return sprintf(buf, "%d\n", sensor->max_watermark);
}

ssize_t st_asm330lhhx_get_watermark(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	struct st_asm330lhhx_sensor *sensor = iio_priv(dev_to_iio_dev(dev));

	return sprintf(buf, "%d\n", sensor->watermark);
}

ssize_t st_asm330lhhx_set_watermark(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t size)
{
	struct iio_dev *iio_dev = dev_to_iio_dev(dev);
	struct st_asm330lhhx_sensor *sensor = iio_priv(iio_dev);
	int err, val;

	err = iio_device_claim_direct_mode(iio_dev);
	if (err)
		return err;

	err = kstrtoint(buf, 10, &val);
	if (err < 0)
		goto out;

	err = st_asm330lhhx_update_watermark(sensor, val);
	if (err < 0)
		goto out;

	sensor->watermark = val;

out:
	iio_device_release_direct_mode(iio_dev);

	return err < 0 ? err : size;
}

ssize_t st_asm330lhhx_flush_fifo(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t size)
{
	struct iio_dev *iio_dev = dev_to_iio_dev(dev);
	struct st_asm330lhhx_sensor *sensor = iio_priv(iio_dev);
	struct st_asm330lhhx_hw *hw = sensor->hw;
	s64 event;
	int count;
	s64 type;
	s64 fts;
	s64 ts;

	mutex_lock(&hw->fifo_lock);
	ts = st_asm330lhhx_get_time_ns(iio_dev);
	hw->delta_ts = ts - hw->ts;
	hw->ts = ts;
	set_bit(ST_ASM330LHHX_HW_FLUSH, &hw->state);
	count = st_asm330lhhx_read_fifo(hw);
	sensor->dec_counter = 0;
	fts = sensor->last_fifo_timestamp;
	mutex_unlock(&hw->fifo_lock);

	type = count > 0 ? STM_IIO_EV_DIR_FIFO_DATA : STM_IIO_EV_DIR_FIFO_EMPTY;
	event = IIO_UNMOD_EVENT_CODE(iio_dev->channels[0].type, -1,
				     STM_IIO_EV_TYPE_FIFO_FLUSH, type);
	iio_push_event(iio_dev, event, fts);

	return size;
}

int st_asm330lhhx_suspend_fifo(struct st_asm330lhhx_hw *hw)
{
	int err;

	mutex_lock(&hw->fifo_lock);
	st_asm330lhhx_read_fifo(hw);
	err = st_asm330lhhx_set_fifo_mode(hw, ST_ASM330LHHX_FIFO_BYPASS);
	mutex_unlock(&hw->fifo_lock);

	return err;
}

int st_asm330lhhx_update_batching(struct iio_dev *iio_dev, bool enable)
{
	struct st_asm330lhhx_sensor *sensor = iio_priv(iio_dev);
	struct st_asm330lhhx_hw *hw = sensor->hw;
	int err;

	disable_irq(hw->irq);

	err = st_asm330lhhx_set_sensor_batching_odr(sensor, enable);
	enable_irq(hw->irq);

	return err;
}

static int st_asm330lhhx_update_fifo(struct iio_dev *iio_dev,
				     bool enable)
{
	struct st_asm330lhhx_sensor *sensor = iio_priv(iio_dev);
	struct st_asm330lhhx_hw *hw = sensor->hw;
	int err;

	if (sensor->id == ST_ASM330LHHX_ID_GYRO && !enable)
		delayed_enable_gyro = true;

	if (sensor->id == ST_ASM330LHHX_ID_GYRO &&
	    enable && delayed_enable_gyro) {
		delayed_enable_gyro = false;
		msleep(delay_gyro);
	}

	disable_irq(hw->irq);

#if defined(CONFIG_IIO_ST_ASM330LHHX_ASYNC_HW_TIMESTAMP)
	hrtimer_cancel(&hw->timesync_timer);
	cancel_work_sync(&hw->timesync_work);
#endif /* CONFIG_IIO_ST_ASM330LHHX_ASYNC_HW_TIMESTAMP */

	if (sensor->id == ST_ASM330LHHX_ID_EXT0 ||
	    sensor->id == ST_ASM330LHHX_ID_EXT1) {
		err = st_asm330lhhx_shub_set_enable(sensor, enable);
		if (err < 0)
			goto out;
	} else {
		err = st_asm330lhhx_sensor_set_enable(sensor, enable);
		if (err < 0)
			goto out;

		err = st_asm330lhhx_set_sensor_batching_odr(sensor, enable);
		if (err < 0)
			goto out;
	}

	/*
	 * This is an auxiliary sensor, it need to get batched
	 * toghether at least with a primary sensor (Acc/Gyro).
	 */
	if (sensor->id == ST_ASM330LHHX_ID_TEMP) {
		if (!(hw->enable_mask & (BIT_ULL(ST_ASM330LHHX_ID_ACC) |
					 BIT_ULL(ST_ASM330LHHX_ID_GYRO)))) {
			struct st_asm330lhhx_sensor *acc_sensor;
			u8 data = 0;

			acc_sensor = iio_priv(hw->iio_devs[ST_ASM330LHHX_ID_ACC]);
			if (enable) {
				err = st_asm330lhhx_get_batch_val(acc_sensor,
						sensor->odr, sensor->uodr,
						&data);
				if (err < 0)
					goto out;
			}

			err = st_asm330lhhx_update_bits_locked(hw,
				hw->odr_table_entry[ST_ASM330LHHX_ID_ACC].batching_reg.addr,
				hw->odr_table_entry[ST_ASM330LHHX_ID_ACC].batching_reg.mask,
				data);
			if (err < 0)
				goto out;
		}
	}

	err = st_asm330lhhx_update_watermark(sensor, sensor->watermark);
	if (err < 0)
		goto out;

	if (enable && hw->fifo_mode == ST_ASM330LHHX_FIFO_BYPASS) {
		st_asm330lhhx_reset_hwts(hw);
		err = st_asm330lhhx_set_fifo_mode(hw, ST_ASM330LHHX_FIFO_CONT);
	} else if (!hw->enable_mask) {
		err = st_asm330lhhx_set_fifo_mode(hw, ST_ASM330LHHX_FIFO_BYPASS);
	}

#if defined(CONFIG_IIO_ST_ASM330LHHX_ASYNC_HW_TIMESTAMP)
	if (hw->fifo_mode != ST_ASM330LHHX_FIFO_BYPASS) {
		hrtimer_start(&hw->timesync_timer,
			      ktime_set(0, 0),
			      HRTIMER_MODE_REL);
	}
#endif /* CONFIG_IIO_ST_ASM330LHHX_ASYNC_HW_TIMESTAMP */

out:
	enable_irq(hw->irq);

	return err;
}

static irqreturn_t st_asm330lhhx_handler_irq(int irq, void *private)
{
	struct st_asm330lhhx_hw *hw = (struct st_asm330lhhx_hw *)private;
	s64 ts = st_asm330lhhx_get_time_ns(hw->iio_devs[0]);

	hw->delta_ts = ts - hw->ts;
	hw->ts = ts;

	return IRQ_WAKE_THREAD;
}

static irqreturn_t st_asm330lhhx_handler_thread(int irq, void *private)
{
	struct st_asm330lhhx_hw *hw = (struct st_asm330lhhx_hw *)private;

	if (hw->settings->st_mlc_probe)
		st_asm330lhhx_mlc_check_status(hw);

	mutex_lock(&hw->fifo_lock);
	st_asm330lhhx_read_fifo(hw);
	clear_bit(ST_ASM330LHHX_HW_FLUSH, &hw->state);
	mutex_unlock(&hw->fifo_lock);

#ifdef CONFIG_IIO_ST_ASM330LHHX_EN_BASIC_FEATURES
	return st_asm330lhhx_event_handler(hw);
#else /* CONFIG_IIO_ST_ASM330LHHX_EN_BASIC_FEATURES */
	return IRQ_HANDLED;
#endif /* CONFIG_IIO_ST_ASM330LHHX_EN_BASIC_FEATURES */
}

static int st_asm330lhhx_fifo_preenable(struct iio_dev *iio_dev)
{
	return st_asm330lhhx_update_fifo(iio_dev, true);
}

static int st_asm330lhhx_fifo_postdisable(struct iio_dev *iio_dev)
{
	return st_asm330lhhx_update_fifo(iio_dev, false);
}

static const struct iio_buffer_setup_ops st_asm330lhhx_fifo_ops = {
	.preenable = st_asm330lhhx_fifo_preenable,
	.postdisable = st_asm330lhhx_fifo_postdisable,
};

int st_asm330lhhx_buffers_setup(struct st_asm330lhhx_hw *hw)
{
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
		irq_active_low = false;
		break;
	case IRQF_TRIGGER_LOW:
		irq_active_low = true;
		break;
	default:
		dev_info(hw->dev, "mode %lx unsupported\n", irq_type);
		return -EINVAL;
	}

	err = regmap_update_bits(hw->regmap,
				 ST_ASM330LHHX_REG_CTRL3_C_ADDR,
				 ST_ASM330LHHX_REG_H_LACTIVE_MASK,
				 FIELD_PREP(ST_ASM330LHHX_REG_H_LACTIVE_MASK,
				 irq_active_low));
	if (err < 0)
		return err;

	if (device_property_read_bool(hw->dev, "drive-open-drain")) {
		err = regmap_update_bits(hw->regmap,
					 ST_ASM330LHHX_REG_CTRL3_C_ADDR,
					 ST_ASM330LHHX_REG_PP_OD_MASK,
					 FIELD_PREP(ST_ASM330LHHX_REG_PP_OD_MASK,
						    1));
		if (err < 0)
			return err;

		irq_type |= IRQF_SHARED;
	}

	err = devm_request_threaded_irq(hw->dev, hw->irq,
					st_asm330lhhx_handler_irq,
					st_asm330lhhx_handler_thread,
					irq_type | IRQF_ONESHOT,
					hw->settings->id.name, hw);
	if (err) {
		dev_err(hw->dev, "failed to request trigger irq %d\n",
			hw->irq);
		return err;
	}

	for (i = ST_ASM330LHHX_ID_GYRO; i <= ST_ASM330LHHX_ID_EXT1; i++) {
		if (!hw->iio_devs[i])
			continue;

#if KERNEL_VERSION(5, 19, 0) <= LINUX_VERSION_CODE
		err = devm_iio_kfifo_buffer_setup(hw->dev, hw->iio_devs[i],
						  &st_asm330lhhx_fifo_ops);
		if (err)
			return err;
#elif KERNEL_VERSION(5, 13, 0) <= LINUX_VERSION_CODE
		err = devm_iio_kfifo_buffer_setup(hw->dev, hw->iio_devs[i],
						  INDIO_BUFFER_SOFTWARE,
						  &st_asm330lhhx_fifo_ops);
		if (err)
			return err;
#else /* LINUX_VERSION_CODE */
		buffer = devm_iio_kfifo_allocate(hw->dev);
		if (!buffer)
			return -ENOMEM;

		iio_device_attach_buffer(hw->iio_devs[i], buffer);
		hw->iio_devs[i]->modes |= INDIO_BUFFER_SOFTWARE;
		hw->iio_devs[i]->setup_ops = &st_asm330lhhx_fifo_ops;
#endif /* LINUX_VERSION_CODE */

	}

	err = st_asm330lhhx_hwtimesync_init(hw);
	if (err)
		return err;

	return regmap_update_bits(hw->regmap,
				  ST_ASM330LHHX_REG_FIFO_CTRL4_ADDR,
				  ST_ASM330LHHX_REG_DEC_TS_MASK,
				  FIELD_PREP(ST_ASM330LHHX_REG_DEC_TS_MASK,
					     1));
}
