// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics st_ism330dhcx FIFO buffer library driver
 *
 * MEMS Software Solutions Team
 *
 * Copyright 2020 STMicroelectronics Inc.
 */
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/iio/iio.h>
#include <linux/iio/kfifo_buf.h>
#include <linux/iio/events.h>
#include <asm/unaligned.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/iio/trigger.h>
#include <linux/iio/buffer.h>
#include <linux/version.h>

#include "st_ism330dhcx.h"

#define ST_ISM330DHCX_REG_EMB_FUNC_STATUS_MAINPAGE		0x35
#define ST_ISM330DHCX_REG_INT_STEP_DET_MASK		BIT(3)
#define ST_ISM330DHCX_REG_INT_TILT_MASK			BIT(4)
#define ST_ISM330DHCX_REG_INT_SIGMOT_MASK			BIT(5)
#define ST_ISM330DHCX_REG_INT_GLANCE_MASK			BIT(0)
#define ST_ISM330DHCX_REG_INT_MOTION_MASK			BIT(1)
#define ST_ISM330DHCX_REG_INT_NO_MOTION_MASK		BIT(2)
#define ST_ISM330DHCX_REG_INT_WAKEUP_MASK			BIT(3)
#define ST_ISM330DHCX_REG_INT_PICKUP_MASK			BIT(4)
#define ST_ISM330DHCX_REG_INT_ORIENTATION_MASK		BIT(5)
#define ST_ISM330DHCX_REG_INT_WRIST_MASK			BIT(6)

#define ST_ISM330DHCX_SAMPLE_DISCHARD			0x7ffd

#define ST_ISM330DHCX_EWMA_LEVEL				120
#define ST_ISM330DHCX_EWMA_DIV				128

enum {
	ST_ISM330DHCX_GYRO_TAG = 0x01,
	ST_ISM330DHCX_ACC_TAG = 0x02,
	ST_ISM330DHCX_TEMP_TAG = 0x03,
	ST_ISM330DHCX_TS_TAG = 0x04,
	ST_ISM330DHCX_EXT0_TAG = 0x0f,
	ST_ISM330DHCX_EXT1_TAG = 0x10,
	ST_ISM330DHCX_SC_TAG = 0x12,
};

/**
 * Get Linux timestamp (SW)
 *
 * @return  timestamp in ns
 */
static inline s64 st_ism330dhcx_get_time_ns(struct st_ism330dhcx_hw *hw)
{
	return iio_get_time_ns(hw->iio_devs[ST_ISM330DHCX_ID_GYRO]);
}

/**
 * Timestamp low pass filter
 *
 * @param  old: ST IMU MEMS hw instance
 * @param  new: ST IMU MEMS hw instance
 * @param  weight: ST IMU MEMS hw instance
 * @return  estimation of the timestamp average
 */
static inline s64 st_ism330dhcx_ewma(s64 old, s64 new, int weight)
{
	s64 diff, incr;

	diff = new - old;
	incr = div_s64((ST_ISM330DHCX_EWMA_DIV - weight) * diff,
			ST_ISM330DHCX_EWMA_DIV);

	return old + incr;
}

/**
 * Reset HW Timestamp counter and clear timestamp data structure
 *
 * @param  hw: ST IMU MEMS hw instance
 * @return  < 0 if error, 0 otherwise
 */
inline int st_ism330dhcx_reset_hwts(struct st_ism330dhcx_hw *hw)
{
	u8 data = 0xaa;

	hw->ts = st_ism330dhcx_get_time_ns(hw);
	hw->ts_offset = hw->ts;
	hw->val_ts_old = 0;
	hw->hw_ts_high = 0;
	hw->tsample = 0ull;

	return st_ism330dhcx_write_atomic(hw, ST_ISM330DHCX_REG_TIMESTAMP2_ADDR,
				       sizeof(data), &data);
}

/**
 * Setting FIFO mode
 *
 * @param  hw: ST IMU MEMS hw instance
 * @param  fifo_mode: ST_ISM330DHCX_FIFO_BYPASS or ST_ISM330DHCX_FIFO_CONT
 * @return  0 FIFO configured accordingly, non zero otherwise
 */
int st_ism330dhcx_set_fifo_mode(struct st_ism330dhcx_hw *hw,
			     enum st_ism330dhcx_fifo_mode fifo_mode)
{
	int err;

	err = st_ism330dhcx_write_with_mask(hw, ST_ISM330DHCX_REG_FIFO_CTRL4_ADDR,
					 ST_ISM330DHCX_REG_FIFO_MODE_MASK,
					 fifo_mode);
	if (err < 0)
		return err;

	hw->fifo_mode = fifo_mode;

	return 0;
}

/**
 * Setting sensor ODR in batching mode
 *
 * @param  sensor: ST IMU sensor instance
 * @param  enable: enable or disable batching mode
 * @return  0 FIFO configured accordingly, non zero otherwise
 */
int __st_ism330dhcx_set_sensor_batching_odr(struct st_ism330dhcx_sensor *sensor,
					 bool enable)
{
	struct st_ism330dhcx_hw *hw = sensor->hw;
	u8 data = 0;
	int err;
	int podr, puodr;

	if (enable) {
		err = st_ism330dhcx_get_odr_val(sensor->id, sensor->odr,
					     sensor->uodr, &podr, &puodr,
					     &data);
		if (err < 0)
			return err;
	}

	err = __st_ism330dhcx_write_with_mask(hw, sensor->batch_reg.addr,
					   sensor->batch_reg.mask, data);
	return err < 0 ? err : 0;
}

/**
 * Setting timestamp ODR in batching mode
 *
 * @param  hw: ST IMU MEMS hw instance
 * @return  Timestamp ODR
 */
static int st_ism330dhcx_ts_odr(struct st_ism330dhcx_hw *hw)
{
	struct st_ism330dhcx_sensor *sensor;
	int odr = 0;
	u8 i;

	for (i = ST_ISM330DHCX_ID_GYRO; i <= ST_ISM330DHCX_ID_EXT1; i++) {
		if (!hw->iio_devs[i])
			continue;

		sensor = iio_priv(hw->iio_devs[i]);
		if (hw->enable_mask & BIT(sensor->id)) {
			odr = max_t(int, odr, sensor->odr);
		}
	}

	return odr;
}

/**
 * Setting sensor ODR in batching mode
 *
 * @param  sensor: ST IMU sensor instance
 * @param  enable: enable or disable batching mode
 * @return  0 FIFO configured accordingly, non zero otherwise
 */
static inline int
st_ism330dhcx_set_sensor_batching_odr(struct st_ism330dhcx_sensor *sensor,
				   bool enable)
{
	struct st_ism330dhcx_hw *hw = sensor->hw;
	int err;

	mutex_lock(&hw->page_lock);
	err = __st_ism330dhcx_set_sensor_batching_odr(sensor, enable);
	mutex_unlock(&hw->page_lock);

	return err;
}

/**
 * Update watermark level in FIFO
 *
 * @param  sensor: ST IMU sensor instance
 * @param  watermark: New watermark level
 * @return  0 if FIFO configured, non zero for error
 */
int st_ism330dhcx_update_watermark(struct st_ism330dhcx_sensor *sensor,
				u16 watermark)
{
	u16 fifo_watermark = ST_ISM330DHCX_MAX_FIFO_DEPTH, cur_watermark = 0;
	struct st_ism330dhcx_hw *hw = sensor->hw;
	struct st_ism330dhcx_sensor *cur_sensor;
	__le16 wdata;
	int i, err;
	u8 data;

	for (i = ST_ISM330DHCX_ID_GYRO; i <= ST_ISM330DHCX_ID_STEP_COUNTER; i++) {
		if (!hw->iio_devs[i])
			continue;

		cur_sensor = iio_priv(hw->iio_devs[i]);

		if (!(hw->enable_mask & BIT(cur_sensor->id)))
			continue;

		cur_watermark = (cur_sensor == sensor) ? watermark :
							 cur_sensor->watermark;

		fifo_watermark = min_t(u16, fifo_watermark, cur_watermark);
	}

	fifo_watermark = max_t(u16, fifo_watermark, 2);

	mutex_lock(&hw->lock);

	err = st_ism330dhcx_read_atomic(hw, ST_ISM330DHCX_REG_FIFO_CTRL1_ADDR + 1,
				     sizeof(data), &data);
	if (err < 0)
		goto out;

	fifo_watermark = ((data << 8) & ~ST_ISM330DHCX_REG_FIFO_WTM_MASK) |
			 (fifo_watermark & ST_ISM330DHCX_REG_FIFO_WTM_MASK);
	wdata = cpu_to_le16(fifo_watermark);
	err = st_ism330dhcx_write_atomic(hw, ST_ISM330DHCX_REG_FIFO_CTRL1_ADDR,
				      sizeof(wdata), (u8 *)&wdata);
out:
	mutex_unlock(&hw->lock);

	return err < 0 ? err : 0;
}

/**
 * Timestamp correlation finction
 *
 * @param  hw: ST IMU MEMS hw instance
 * @param  ts: New timestamp
 */
static inline void st_ism330dhcx_sync_hw_ts(struct st_ism330dhcx_hw *hw, s64 ts)
{
	s64 delta = ts - hw->hw_ts;

	hw->ts_offset = st_ism330dhcx_ewma(hw->ts_offset, delta,
					ST_ISM330DHCX_EWMA_LEVEL);
}

/**
 * Return the iio device structure based on FIFO TAG ID
 *
 * @param  hw: ST IMU MEMS hw instance
 * @param  tag: FIFO sample TAG ID
 * @return  0 if FIFO configured, non zero for error
 */
static struct
iio_dev *st_ism330dhcx_get_iiodev_from_tag(struct st_ism330dhcx_hw *hw,
					u8 tag)
{
	struct iio_dev *iio_dev;

	switch (tag) {
	case ST_ISM330DHCX_GYRO_TAG:
		iio_dev = hw->iio_devs[ST_ISM330DHCX_ID_GYRO];
		break;
	case ST_ISM330DHCX_ACC_TAG:
		iio_dev = hw->iio_devs[ST_ISM330DHCX_ID_ACC];
		break;
	case ST_ISM330DHCX_TEMP_TAG:
		iio_dev = hw->iio_devs[ST_ISM330DHCX_ID_TEMP];
		break;
	case ST_ISM330DHCX_EXT0_TAG:
		if (hw->enable_mask & BIT(ST_ISM330DHCX_ID_EXT0))
			iio_dev = hw->iio_devs[ST_ISM330DHCX_ID_EXT0];
		else
			iio_dev = hw->iio_devs[ST_ISM330DHCX_ID_EXT1];
		break;
	case ST_ISM330DHCX_EXT1_TAG:
		iio_dev = hw->iio_devs[ST_ISM330DHCX_ID_EXT1];
		break;
	case ST_ISM330DHCX_SC_TAG:
		iio_dev = hw->iio_devs[ST_ISM330DHCX_ID_STEP_COUNTER];
		break;
	default:
		iio_dev = NULL;
		break;
	}

	return iio_dev;
}

/**
 * Read all FIFO data stored after WTM FIFO irq fired interrupt
 *
 * @param hw: ST IMU MEMS hw instance
 * @return Number of read bytes in FIFO or error if negative
 */
static int st_ism330dhcx_read_fifo(struct st_ism330dhcx_hw *hw)
{
	u8 iio_buf[ALIGN(ST_ISM330DHCX_SAMPLE_SIZE, sizeof(s64)) + sizeof(s64)];
	/* acc + gyro + 2 ext + ts + sc */
	u8 buf[6 * ST_ISM330DHCX_FIFO_SAMPLE_SIZE], tag, *ptr;
	int i, err, word_len, fifo_len, read_len;
	struct st_ism330dhcx_sensor *sensor;
	struct iio_dev *iio_dev;
	s64 ts_irq, hw_ts_old;
	__le16 fifo_status;
	u16 fifo_depth;
	s16 drdymask;
	u32 val;

	ts_irq = hw->ts - hw->delta_ts;

	err = st_ism330dhcx_read_atomic(hw, ST_ISM330DHCX_REG_FIFO_STATUS1_ADDR,
				     sizeof(fifo_status), (u8 *)&fifo_status);
	if (err < 0)
		return err;

	fifo_depth = le16_to_cpu(fifo_status) & ST_ISM330DHCX_REG_FIFO_STATUS_DIFF;
	if (!fifo_depth)
		return 0;

	fifo_len = fifo_depth * ST_ISM330DHCX_FIFO_SAMPLE_SIZE;
	read_len = 0;

	while (read_len < fifo_len) {
		word_len = min_t(int, fifo_len - read_len, sizeof(buf));
		err = st_ism330dhcx_read_atomic(hw,
					ST_ISM330DHCX_REG_FIFO_DATA_OUT_TAG_ADDR,
					word_len, buf);
		if (err < 0)
			return err;

		for (i = 0; i < word_len; i += ST_ISM330DHCX_FIFO_SAMPLE_SIZE) {
			ptr = &buf[i + ST_ISM330DHCX_TAG_SIZE];
			tag = buf[i] >> 3;

			if (tag == ST_ISM330DHCX_TS_TAG) {
				val = get_unaligned_le32(ptr);

				if (hw->val_ts_old > val)
					hw->hw_ts_high++;

				hw_ts_old = hw->hw_ts;

				/* check hw rollover */
				hw->val_ts_old = val;
				hw->hw_ts = (val +
					     ((s64)hw->hw_ts_high << 32)) *
					     hw->ts_delta_ns;
				hw->ts_offset = st_ism330dhcx_ewma(hw->ts_offset,
						ts_irq - hw->hw_ts,
						ST_ISM330DHCX_EWMA_LEVEL);

				if (!test_bit(ST_ISM330DHCX_HW_FLUSH, &hw->state))
					/* sync ap timestamp and sensor one */
					st_ism330dhcx_sync_hw_ts(hw, ts_irq);

				ts_irq += hw->hw_ts;

				if (!hw->tsample)
					hw->tsample = hw->ts_offset + hw->hw_ts;
				else
					hw->tsample = hw->tsample +
						      hw->hw_ts - hw_ts_old;
			} else {
				iio_dev =
					st_ism330dhcx_get_iiodev_from_tag(hw, tag);
				if (!iio_dev)
					continue;

				sensor = iio_priv(iio_dev);

				/* skip samples if not ready */
				drdymask =
				      (s16)le16_to_cpu(get_unaligned_le16(ptr));
				if (unlikely(drdymask >=
						ST_ISM330DHCX_SAMPLE_DISCHARD)) {
					continue;
				}

				/*
				 * hw ts in not queued in FIFO if only step
				 * counter enabled
				 */
				if (sensor->id == ST_ISM330DHCX_ID_STEP_COUNTER) {
					val = get_unaligned_le32(ptr + 2);
					hw->tsample = (val +
						((s64)hw->hw_ts_high << 32)) *
						hw->ts_delta_ns;
				}

				memcpy(iio_buf, ptr, ST_ISM330DHCX_SAMPLE_SIZE);

				/* avoid samples in the future */
				hw->tsample = min_t(s64,
						    st_ism330dhcx_get_time_ns(hw),
						    hw->tsample);

				sensor->last_fifo_timestamp = hw->tsample;

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

/**
 * Return the max FIFO watermark level accepted
 *
 * @param  dev: Linux Device
 * @param  attr: Device Attribute
 * @param  buf: User Buffer
 * @return  Number of chars printed into the buffer
 */
ssize_t st_ism330dhcx_get_max_watermark(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	struct iio_dev *iio_dev = dev_to_iio_dev(dev);
	struct st_ism330dhcx_sensor *sensor = iio_priv(iio_dev);

	return sprintf(buf, "%d\n", sensor->max_watermark);
}

/**
 * Return the FIFO watermark level
 *
 * @param  dev: Linux Device
 * @param  attr: Device Attribute
 * @param  buf: User Buffer
 * @return  Number of chars printed into the buffer
 */
ssize_t st_ism330dhcx_get_watermark(struct device *dev,
				 struct device_attribute *attr,
				 char *buf)
{
	struct iio_dev *iio_dev = dev_to_iio_dev(dev);
	struct st_ism330dhcx_sensor *sensor = iio_priv(iio_dev);

	return sprintf(buf, "%d\n", sensor->watermark);
}

/**
 * Set the FIFO watermark level
 *
 * @param  dev: Linux Device
 * @param  attr: Device Attribute
 * @param  buf: User Buffer
 * @param  size: New FIFO watermark level
 * @return  Watermark level if >= 0, error otherwise
 */
ssize_t st_ism330dhcx_set_watermark(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t size)
{
	struct iio_dev *iio_dev = dev_to_iio_dev(dev);
	struct st_ism330dhcx_sensor *sensor = iio_priv(iio_dev);
	int err, val;

	mutex_lock(&iio_dev->mlock);
	if (iio_buffer_enabled(iio_dev)) {
		err = -EBUSY;
		goto out;
	}

	err = kstrtoint(buf, 10, &val);
	if (err < 0)
		goto out;

	err = st_ism330dhcx_update_watermark(sensor, val);
	if (err < 0)
		goto out;

	sensor->watermark = val;

out:
	mutex_unlock(&iio_dev->mlock);

	return err < 0 ? err : size;
}

/**
 * Flush internal HW FIFO
 *
 * @param  dev: Linux Device
 * @param  attr: Device Attribute
 * @param  buf: User Buffer
 * @param  size: unused
 * @return  Watermark level if >= 0, error otherwise
 */
ssize_t st_ism330dhcx_flush_fifo(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t size)
{
	struct iio_dev *iio_dev = dev_to_iio_dev(dev);
	struct st_ism330dhcx_sensor *sensor = iio_priv(iio_dev);
	struct st_ism330dhcx_hw *hw = sensor->hw;
	s64 type;
	s64 event;
	int count;
	s64 fts;
	s64 ts;

	mutex_lock(&hw->fifo_lock);
	ts = st_ism330dhcx_get_time_ns(hw);
	hw->delta_ts = ts - hw->ts;
	hw->ts = ts;
	set_bit(ST_ISM330DHCX_HW_FLUSH, &hw->state);
	count = st_ism330dhcx_read_fifo(hw);
	sensor->dec_counter = 0;
	mutex_unlock(&hw->fifo_lock);
	if (count > 0)
		fts = sensor->last_fifo_timestamp;
	else
		fts = ts;

	type = count > 0 ? STM_IIO_EV_DIR_FIFO_DATA : STM_IIO_EV_DIR_FIFO_EMPTY;
	event = IIO_UNMOD_EVENT_CODE(iio_dev->channels[0].type, -1,
				     STM_IIO_EV_TYPE_FIFO_FLUSH, type);
	iio_push_event(iio_dev, event, fts);

	return size;
}

/**
 * Empty FIFO and set HW FIFO in Bypass mode
 *
 * @param  hw: ST IMU MEMS hw instance
 * @return  Watermark level if >= 0, error otherwise
 */
int st_ism330dhcx_suspend_fifo(struct st_ism330dhcx_hw *hw)
{
	int err;

	mutex_lock(&hw->fifo_lock);
	st_ism330dhcx_read_fifo(hw);
	err = st_ism330dhcx_set_fifo_mode(hw, ST_ISM330DHCX_FIFO_BYPASS);
	mutex_unlock(&hw->fifo_lock);

	return err;
}

/**
 * Update ODR batching in FIFO and Timestamp
 *
 * @param  iio_dev: Linux IIO device
 * @param  enable: enable/disable batcing in FIFO
 * @return  < 0 if error, 0 otherwise
 */
int st_ism330dhcx_update_batching(struct iio_dev *iio_dev, bool enable)
{
	struct st_ism330dhcx_sensor *sensor = iio_priv(iio_dev);
	struct st_ism330dhcx_hw *hw = sensor->hw;
	int err;

	disable_irq(hw->irq);

	err = st_ism330dhcx_set_sensor_batching_odr(sensor, enable);
	if (err < 0)
		goto out;

	/* Calc TS ODR */
	hw->odr = st_ism330dhcx_ts_odr(hw);

out:
	enable_irq(hw->irq);

	return err;
}

/**
 * Update FIFO watermark value based to the enabled sensors
 *
 * @param  iio_dev: Linux IIO device
 * @param  enable: enable/disable batcing in FIFO
 * @return  < 0 if error, 0 otherwise
 */
static int st_ism330dhcx_update_fifo(struct iio_dev *iio_dev, bool enable)
{
	struct st_ism330dhcx_sensor *sensor = iio_priv(iio_dev);
	struct st_ism330dhcx_hw *hw = sensor->hw;
	int err;
	int podr, puodr;

	disable_irq(hw->irq);

	if (sensor->id == ST_ISM330DHCX_ID_EXT0 ||
	    sensor->id == ST_ISM330DHCX_ID_EXT1) {
		err = st_ism330dhcx_shub_set_enable(sensor, enable);
		if (err < 0)
			goto out;
	} else {
		if (sensor->id == ST_ISM330DHCX_ID_STEP_COUNTER) {
			err = st_ism330dhcx_step_counter_set_enable(sensor,
								 enable);
			if (err < 0)
				goto out;
		} else {
			err = st_ism330dhcx_sensor_set_enable(sensor, enable);
			if (err < 0)
				goto out;

			err = st_ism330dhcx_set_sensor_batching_odr(sensor,
								 enable);
			if (err < 0)
				goto out;
		}
	}

	/*
	 * this is an auxiliary sensor, it need to get batched
	 * toghether at least with a primary sensor (Acc/Gyro)
	 */
	if (sensor->id == ST_ISM330DHCX_ID_TEMP) {
		if (!(hw->enable_mask & (BIT(ST_ISM330DHCX_ID_ACC) |
					 BIT(ST_ISM330DHCX_ID_GYRO)))) {
			struct st_ism330dhcx_sensor *acc_sensor;
			u8 data = 0;

			acc_sensor = iio_priv(hw->iio_devs[ST_ISM330DHCX_ID_ACC]);
			if (enable) {
				err = st_ism330dhcx_get_odr_val(ST_ISM330DHCX_ID_ACC,
						sensor->odr, sensor->uodr,
						&podr, &puodr, &data);
				if (err < 0)
					goto out;
			}

			err = st_ism330dhcx_write_with_mask(hw,
					acc_sensor->batch_reg.addr,
					acc_sensor->batch_reg.mask,
					data);
			if (err < 0)
				goto out;
		}
	}

	err = st_ism330dhcx_update_watermark(sensor, sensor->watermark);
	if (err < 0)
		goto out;

	/* Calc TS ODR */
	hw->odr = st_ism330dhcx_ts_odr(hw);

	if (enable && hw->fifo_mode == ST_ISM330DHCX_FIFO_BYPASS) {
		st_ism330dhcx_reset_hwts(hw);
		err = st_ism330dhcx_set_fifo_mode(hw, ST_ISM330DHCX_FIFO_CONT);
	} else if (!hw->enable_mask) {
		err = st_ism330dhcx_set_fifo_mode(hw, ST_ISM330DHCX_FIFO_BYPASS);
	}

out:
	enable_irq(hw->irq);

	return err;
}

/**
 * Bottom handler for FSM Orientation sensor event generation
 *
 * @param  irq: IIO trigger irq number
 * @param  p: iio poll function environment
 * @return  IRQ_HANDLED or < 0 for error
 */
static irqreturn_t st_ism330dhcx_buffer_handler_thread(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *iio_dev = pf->indio_dev;
	struct st_ism330dhcx_sensor *sensor = iio_priv(iio_dev);
	u8 buffer[sizeof(u8) + sizeof(s64)];
	int err;

	err = st_ism330dhcx_fsm_get_orientation(sensor->hw, buffer);
	if (err < 0)
		goto out;

	iio_push_to_buffers_with_timestamp(iio_dev, buffer,
					   st_ism330dhcx_get_time_ns(sensor->hw));
out:
	iio_trigger_notify_done(sensor->trig);

	return IRQ_HANDLED;
}

/**
 * Top handler for sensor event generation + FIFO management
 *
 * @param  irq: IIO trigger irq number
 * @param  private: iio poll function environment
 * @return  IRQ_HANDLED or < 0 for error
 */
static irqreturn_t st_ism330dhcx_handler_irq(int irq, void *private)
{
	struct st_ism330dhcx_hw *hw = (struct st_ism330dhcx_hw *)private;
	s64 ts = st_ism330dhcx_get_time_ns(hw);

	hw->delta_ts = ts - hw->ts;
	hw->ts = ts;

	return IRQ_WAKE_THREAD;
}

/**
 * Bottom handler for sensor event generation + FIFO management
 *
 * @param  irq: irq line number
 * @param  private: device private environment pointer
 * @return  IRQ_HANDLED or < 0 for error
 */
static irqreturn_t st_ism330dhcx_handler_thread(int irq, void *private)
{
	struct st_ism330dhcx_hw *hw = (struct st_ism330dhcx_hw *)private;

	mutex_lock(&hw->fifo_lock);
	st_ism330dhcx_read_fifo(hw);
	clear_bit(ST_ISM330DHCX_HW_FLUSH, &hw->state);
	mutex_unlock(&hw->fifo_lock);

	if (hw->enable_mask & (BIT(ST_ISM330DHCX_ID_STEP_DETECTOR) |
			       BIT(ST_ISM330DHCX_ID_SIGN_MOTION) |
			       BIT(ST_ISM330DHCX_ID_TILT) |
			       BIT(ST_ISM330DHCX_ID_MOTION) |
			       BIT(ST_ISM330DHCX_ID_NO_MOTION) |
			       BIT(ST_ISM330DHCX_ID_WAKEUP) |
			       BIT(ST_ISM330DHCX_ID_PICKUP) |
			       BIT(ST_ISM330DHCX_ID_ORIENTATION) |
			       BIT(ST_ISM330DHCX_ID_WRIST_TILT) |
			       BIT(ST_ISM330DHCX_ID_GLANCE))) {
		struct iio_dev *iio_dev;
		u8 status[3];
		s64 event;
		int err;

		err = hw->tf->read(hw->dev,
				   ST_ISM330DHCX_REG_EMB_FUNC_STATUS_MAINPAGE,
				   sizeof(status), status);
		if (err < 0)
			return IRQ_HANDLED;

		/* embedded function sensors */
		if (status[0] & ST_ISM330DHCX_REG_INT_STEP_DET_MASK) {
			iio_dev = hw->iio_devs[ST_ISM330DHCX_ID_STEP_DETECTOR];
			event = IIO_UNMOD_EVENT_CODE(IIO_STEPS, -1,
						     IIO_EV_TYPE_THRESH,
						     IIO_EV_DIR_RISING);
			iio_push_event(iio_dev, event,
				       st_ism330dhcx_get_time_ns(hw));
		}
		if (status[0] & ST_ISM330DHCX_REG_INT_SIGMOT_MASK) {
			iio_dev = hw->iio_devs[ST_ISM330DHCX_ID_SIGN_MOTION];
			event = IIO_UNMOD_EVENT_CODE(STM_IIO_SIGN_MOTION, -1,
						     IIO_EV_TYPE_THRESH,
						     IIO_EV_DIR_RISING);
			iio_push_event(iio_dev, event,
				       st_ism330dhcx_get_time_ns(hw));
		}
		if (status[0] & ST_ISM330DHCX_REG_INT_TILT_MASK) {
			iio_dev = hw->iio_devs[ST_ISM330DHCX_ID_TILT];
			event = IIO_UNMOD_EVENT_CODE(STM_IIO_TILT, -1,
						     IIO_EV_TYPE_THRESH,
						     IIO_EV_DIR_RISING);
			iio_push_event(iio_dev, event,
				       st_ism330dhcx_get_time_ns(hw));
		}
		/*  fsm sensors */
		if (status[1] & ST_ISM330DHCX_REG_INT_GLANCE_MASK) {
			iio_dev = hw->iio_devs[ST_ISM330DHCX_ID_GLANCE];
			event = IIO_UNMOD_EVENT_CODE(STM_IIO_GESTURE, -1,
						     IIO_EV_TYPE_THRESH,
						     IIO_EV_DIR_RISING);
			iio_push_event(iio_dev, event,
				       st_ism330dhcx_get_time_ns(hw));
		}
		if (status[1] & ST_ISM330DHCX_REG_INT_MOTION_MASK) {
			iio_dev = hw->iio_devs[ST_ISM330DHCX_ID_MOTION];
			event = IIO_UNMOD_EVENT_CODE(STM_IIO_GESTURE, -1,
						     IIO_EV_TYPE_THRESH,
						     IIO_EV_DIR_RISING);
			iio_push_event(iio_dev, event,
				       st_ism330dhcx_get_time_ns(hw));
		}
		if (status[1] & ST_ISM330DHCX_REG_INT_NO_MOTION_MASK) {
			iio_dev = hw->iio_devs[ST_ISM330DHCX_ID_NO_MOTION];
			event = IIO_UNMOD_EVENT_CODE(STM_IIO_GESTURE, -1,
						     IIO_EV_TYPE_THRESH,
						     IIO_EV_DIR_RISING);
			iio_push_event(iio_dev, event,
				       st_ism330dhcx_get_time_ns(hw));
		}
		if (status[1] & ST_ISM330DHCX_REG_INT_WAKEUP_MASK) {
			iio_dev = hw->iio_devs[ST_ISM330DHCX_ID_WAKEUP];
			event = IIO_UNMOD_EVENT_CODE(STM_IIO_GESTURE, -1,
						     IIO_EV_TYPE_THRESH,
						     IIO_EV_DIR_RISING);
			iio_push_event(iio_dev, event,
				       st_ism330dhcx_get_time_ns(hw));
		}
		if (status[1] & ST_ISM330DHCX_REG_INT_PICKUP_MASK) {
			iio_dev = hw->iio_devs[ST_ISM330DHCX_ID_PICKUP];
			event = IIO_UNMOD_EVENT_CODE(STM_IIO_GESTURE, -1,
						     IIO_EV_TYPE_THRESH,
						     IIO_EV_DIR_RISING);
			iio_push_event(iio_dev, event,
				       st_ism330dhcx_get_time_ns(hw));
		}
		if (status[1] & ST_ISM330DHCX_REG_INT_ORIENTATION_MASK) {
			struct st_ism330dhcx_sensor *sensor;

			iio_dev = hw->iio_devs[ST_ISM330DHCX_ID_ORIENTATION];
			sensor = iio_priv(iio_dev);
			iio_trigger_poll_chained(sensor->trig);
		}
		if (status[1] & ST_ISM330DHCX_REG_INT_WRIST_MASK) {
			iio_dev = hw->iio_devs[ST_ISM330DHCX_ID_WRIST_TILT];
			event = IIO_UNMOD_EVENT_CODE(STM_IIO_GESTURE, -1,
						     IIO_EV_TYPE_THRESH,
						     IIO_EV_DIR_RISING);
			iio_push_event(iio_dev, event,
				       st_ism330dhcx_get_time_ns(hw));
		}
	}

	return IRQ_HANDLED;
}

/**
 * IIO fifo pre enabled callback function
 *
 * @param  iio_dev: IIO device
 * @return  < 0 if error, 0 otherwise
 */
static int st_ism330dhcx_fifo_preenable(struct iio_dev *iio_dev)
{
	return st_ism330dhcx_update_fifo(iio_dev, true);
}

/**
 * IIO fifo post disable callback function
 *
 * @param  iio_dev: IIO device
 * @return  < 0 if error, 0 otherwise
 */
static int st_ism330dhcx_fifo_postdisable(struct iio_dev *iio_dev)
{
	return st_ism330dhcx_update_fifo(iio_dev, false);
}

/**
 * IIO fifo callback registruction structure
 */
static const struct iio_buffer_setup_ops st_ism330dhcx_fifo_ops = {
	.preenable = st_ism330dhcx_fifo_preenable,
	.postdisable = st_ism330dhcx_fifo_postdisable,
};

/**
 * Enable HW FIFO
 *
 * @param  hw: ST IMU MEMS hw instance
 * @return  < 0 if error, 0 otherwise
 */
static int st_ism330dhcx_fifo_init(struct st_ism330dhcx_hw *hw)
{
	return st_ism330dhcx_write_with_mask(hw,
					  ST_ISM330DHCX_REG_FIFO_CTRL4_ADDR,
					  ST_ISM330DHCX_REG_DEC_TS_MASK, 1);
}

static const struct iio_trigger_ops st_ism330dhcx_trigger_ops = {
	NULL
};

static int st_ism330dhcx_buffer_preenable(struct iio_dev *iio_dev)
{
	return st_ism330dhcx_embfunc_sensor_set_enable(iio_priv(iio_dev), true);
}

static int st_ism330dhcx_buffer_postdisable(struct iio_dev *iio_dev)
{
	return st_ism330dhcx_embfunc_sensor_set_enable(iio_priv(iio_dev), false);
}

static const struct iio_buffer_setup_ops st_ism330dhcx_buffer_ops = {
	.preenable = st_ism330dhcx_buffer_preenable,
	.postdisable = st_ism330dhcx_buffer_postdisable,
};

/**
 * Init IIO buffers and triggers
 *
 * @param  hw: ST IMU MEMS hw instance
 * @return  < 0 if error, 0 otherwise
 */
int st_ism330dhcx_buffers_setup(struct st_ism330dhcx_hw *hw)
{
	struct device_node *np = hw->dev->of_node;
	struct st_ism330dhcx_sensor *sensor;
#if LINUX_VERSION_CODE < KERNEL_VERSION(5,13,0)
	struct iio_buffer *buffer;
#endif /* LINUX_VERSION_CODE */
	struct iio_dev *iio_dev;
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

	err = st_ism330dhcx_write_with_mask(hw, ST_ISM330DHCX_REG_CTRL3_C_ADDR,
					 ST_ISM330DHCX_REG_H_LACTIVE_MASK,
					 irq_active_low);
	if (err < 0)
		return err;

	if (np && of_property_read_bool(np, "drive-open-drain")) {
		err = st_ism330dhcx_write_with_mask(hw,
						 ST_ISM330DHCX_REG_CTRL3_C_ADDR,
						 ST_ISM330DHCX_REG_PP_OD_MASK, 1);
		if (err < 0)
			return err;

		irq_type |= IRQF_SHARED;
	}

	err = devm_request_threaded_irq(hw->dev, hw->irq,
					st_ism330dhcx_handler_irq,
					st_ism330dhcx_handler_thread,
					irq_type | IRQF_ONESHOT,
					"ism330dhcx", hw);
	if (err) {
		dev_err(hw->dev, "failed to request trigger irq %d\n",
			hw->irq);
		return err;
	}

	for (i = ST_ISM330DHCX_ID_GYRO; i <= ST_ISM330DHCX_ID_STEP_COUNTER; i++) {
		if (!hw->iio_devs[i])
			continue;

#if KERNEL_VERSION(5, 19, 0) <= LINUX_VERSION_CODE
		err = devm_iio_kfifo_buffer_setup(hw->dev, hw->iio_devs[i],
						  &st_ism330dhcx_fifo_ops);
		if (err)
			return err;
#elif KERNEL_VERSION(5, 13, 0) <= LINUX_VERSION_CODE
		err = devm_iio_kfifo_buffer_setup(hw->dev, hw->iio_devs[i],
						  INDIO_BUFFER_SOFTWARE,
						  &st_ism330dhcx_fifo_ops);
		if (err)
			return err;
#else /* LINUX_VERSION_CODE */
		buffer = devm_iio_kfifo_allocate(hw->dev);
		if (!buffer)
			return -ENOMEM;

		iio_device_attach_buffer(hw->iio_devs[i], buffer);
		hw->iio_devs[i]->modes |= INDIO_BUFFER_SOFTWARE;
		hw->iio_devs[i]->setup_ops = &st_ism330dhcx_fifo_ops;
#endif /* LINUX_VERSION_CODE */

	}

	err = st_ism330dhcx_fifo_init(hw);
	if (err < 0)
		return err;

	iio_dev = hw->iio_devs[ST_ISM330DHCX_ID_ORIENTATION];
	sensor = iio_priv(iio_dev);

	err = devm_iio_triggered_buffer_setup(hw->dev, iio_dev,
				NULL, st_ism330dhcx_buffer_handler_thread,
				&st_ism330dhcx_buffer_ops);
	if (err < 0)
		return err;

	sensor->trig = devm_iio_trigger_alloc(hw->dev, "%s-trigger",
					      iio_dev->name);
	if (!sensor->trig)
		return -ENOMEM;

	iio_trigger_set_drvdata(sensor->trig, iio_dev);
	sensor->trig->ops = &st_ism330dhcx_trigger_ops;
	sensor->trig->dev.parent = hw->dev;

	err = devm_iio_trigger_register(hw->dev, sensor->trig);
	if (err < 0) {
		dev_err(hw->dev, "failed to register iio trigger.\n");

		return err;
	}

	iio_dev->trig = iio_trigger_get(sensor->trig);

	return 0;
}
