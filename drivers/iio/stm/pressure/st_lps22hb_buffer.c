// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics lps22hb buffer driver
 *
 * MEMS Software Solutions Team
 *
 * Copyright 2017 STMicroelectronics Inc.
 */

#include <linux/interrupt.h>
#include <linux/iio/iio.h>
#include <linux/iio/buffer.h>
#include <linux/iio/kfifo_buf.h>
#include <linux/iio/events.h>
#include <linux/version.h>

#include "st_lps22hb.h"

#define ST_LPS22HB_FIFO_CTRL_ADDR		0x14
#define ST_LPS22HB_FIFO_THS_MASK		0x1f
#define ST_LPS22HB_FIFO_MODE_MASK		0xe0
#define ST_LPS22HB_INT_FTH_MASK			0x10

#define ST_LPS22HB_FIFO_SRC_ADDR		0x26
#define ST_LPS22HB_FIFO_SRC_DIFF_MASK		0x1f

#define ST_LPS22HB_PRESS_OUT_XL_ADDR		0x28

#define ST_LPS22HB_PRESS_SAMPLE_LEN		3
#define ST_LPS22HB_TEMP_SAMPLE_LEN		2
#define ST_LPS22HB_FIFO_SAMPLE_LEN		(ST_LPS22HB_PRESS_SAMPLE_LEN + \
						 ST_LPS22HB_TEMP_SAMPLE_LEN)

static inline s64 st_lps22hb_get_time_ns(struct iio_dev *iio_dev)
{
	return iio_get_time_ns(iio_dev);
}

#define ST_LPS22HB_EWMA_LEVEL			96
#define ST_LPS22HB_EWMA_DIV			128
static inline s64 st_lps22hb_ewma(s64 old, s64 new, int weight)
{
	s64 diff, incr;

	diff = new - old;
	incr = div_s64((ST_LPS22HB_EWMA_DIV - weight) * diff,
		       ST_LPS22HB_EWMA_DIV);

	return old + incr;
}

static int st_lps22hb_set_fifo_mode(struct st_lps22hb_hw *hw,
				    enum st_lps22hb_fifo_mode mode)
{
	switch (mode) {
	case ST_LPS22HB_BYPASS:
	case ST_LPS22HB_STREAM:
		break;
	default:
		return -EINVAL;
	}
	return st_lps22hb_write_with_mask(hw, ST_LPS22HB_FIFO_CTRL_ADDR,
					  ST_LPS22HB_FIFO_MODE_MASK, mode);
}

static int st_lps22hb_update_fifo_watermark(struct st_lps22hb_hw *hw, u8 val)
{
	int err;

	err = st_lps22hb_write_with_mask(hw, ST_LPS22HB_FIFO_CTRL_ADDR,
					 ST_LPS22HB_FIFO_THS_MASK, val);
	if (err < 0)
		return err;

	hw->watermark = val;

	return 0;
}

ssize_t st_lps22hb_sysfs_set_hwfifo_watermark(struct device * dev,
					      struct device_attribute * attr,
					      const char *buf, size_t count)
{
	struct st_lps22hb_sensor *sensor = iio_priv(dev_to_iio_dev(dev));
	int err, watermark;

	err = kstrtoint(buf, 10, &watermark);
	if (err < 0)
		return err;

	if (watermark < 1 || watermark > ST_LPS22HB_MAX_FIFO_LENGTH)
		return -EINVAL;

	err = st_lps22hb_update_fifo_watermark(sensor->hw, watermark);

	return err < 0 ? err : count;
}

static int st_lps22hb_read_fifo(struct st_lps22hb_hw *hw)
{
	u8 iio_buff[ALIGN(sizeof(u32) + sizeof(s64), sizeof(s64))];
	u8 status, buff[ST_LPS22HB_RX_MAX_LENGTH];
	int err, i, read_len;

	err = hw->tf->read(hw->dev, ST_LPS22HB_FIFO_SRC_ADDR,
			   sizeof(status), &status);
	if (err < 0)
		return err;

	read_len = (status & ST_LPS22HB_FIFO_SRC_DIFF_MASK) *
		   ST_LPS22HB_FIFO_SAMPLE_LEN;
	if (!read_len)
		return 0;

	err = hw->tf->read(hw->dev, ST_LPS22HB_PRESS_OUT_XL_ADDR,
			   read_len, buff);
	if (err < 0)
		return err;

	for (i = 0; i < read_len; i += ST_LPS22HB_FIFO_SAMPLE_LEN) {
		/* press sample */
		memcpy(iio_buff, buff + i, ST_LPS22HB_PRESS_SAMPLE_LEN);
		iio_push_to_buffers_with_timestamp(
				hw->iio_devs[ST_LPS22HB_PRESS],
				iio_buff, hw->ts);
		/* temp sample */
		memcpy(iio_buff, buff + i + ST_LPS22HB_PRESS_SAMPLE_LEN,
		       ST_LPS22HB_TEMP_SAMPLE_LEN);
		iio_push_to_buffers_with_timestamp(
				hw->iio_devs[ST_LPS22HB_TEMP],
				iio_buff, hw->ts);
		hw->ts += hw->delta_ts;
	}

	return read_len;
}

ssize_t st_lps22hb_sysfs_flush_fifo(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t size)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct st_lps22hb_sensor *sensor = iio_priv(indio_dev);
	struct st_lps22hb_hw *hw = sensor->hw;
	u64 type, event;
	int len;

	mutex_lock(&indio_dev->mlock);
	if (!iio_buffer_enabled(indio_dev)) {
		mutex_unlock(&indio_dev->mlock);
		return -EINVAL;
	}

	mutex_lock(&hw->fifo_lock);
	len = st_lps22hb_read_fifo(hw);
	mutex_unlock(&hw->fifo_lock);

	type = len > 0 ? STM_IIO_EV_DIR_FIFO_DATA : STM_IIO_EV_DIR_FIFO_EMPTY;
	if (sensor->type == ST_LPS22HB_PRESS)
		event = IIO_UNMOD_EVENT_CODE(IIO_PRESSURE, -1,
					     STM_IIO_EV_TYPE_FIFO_FLUSH, type);
	else
		event = IIO_UNMOD_EVENT_CODE(IIO_TEMP, -1,
					     STM_IIO_EV_TYPE_FIFO_FLUSH, type);
	iio_push_event(indio_dev, event, st_lps22hb_get_time_ns(indio_dev));
	mutex_unlock(&indio_dev->mlock);

	return size;
}


static irqreturn_t st_lps22hb_irq_handler(int irq, void *private)
{
	struct st_lps22hb_hw *hw = private;
	s64 delta_ts, ts;

	ts = st_lps22hb_get_time_ns(hw->iio_devs[ST_LPS22HB_PRESS]);
	delta_ts = div_s64((ts - hw->ts_irq), hw->watermark);
	if (hw->odr >= 50)
		hw->delta_ts = st_lps22hb_ewma(hw->delta_ts, delta_ts,
					       ST_LPS22HB_EWMA_LEVEL);
	else
		hw->delta_ts = delta_ts;
	hw->ts_irq = ts;

	return IRQ_WAKE_THREAD;
}

static irqreturn_t st_lps22hb_irq_thread(int irq, void *private)
{
	struct st_lps22hb_hw *hw = private;

	mutex_lock(&hw->fifo_lock);
	st_lps22hb_read_fifo(hw);
	mutex_unlock(&hw->fifo_lock);

	return IRQ_HANDLED;
}

static int st_lps22hb_buffer_preenable(struct iio_dev *indio_dev)
{
	struct st_lps22hb_sensor *sensor = iio_priv(indio_dev);
	struct st_lps22hb_hw *hw = sensor->hw;
	int err;

	err = st_lps22hb_set_fifo_mode(sensor->hw, ST_LPS22HB_STREAM);
	if (err < 0)
		return err;

	err = st_lps22hb_update_fifo_watermark(hw, hw->watermark);
	if (err < 0)
		return err;

	err = st_lps22hb_write_with_mask(sensor->hw, ST_LPS22HB_CTRL3_ADDR,
					 ST_LPS22HB_INT_FTH_MASK, true);
	if (err < 0)
		return err;

	err = st_lps22hb_set_enable(sensor, true);
	if (err < 0)
		return err;

	hw->delta_ts = div_s64(1000000000UL, hw->odr);
	hw->ts = st_lps22hb_get_time_ns(indio_dev);
	hw->ts_irq = hw->ts;

	return 0;
}

static int st_lps22hb_buffer_postdisable(struct iio_dev *indio_dev)
{
	struct st_lps22hb_sensor *sensor = iio_priv(indio_dev);
	int err;

	err = st_lps22hb_set_fifo_mode(sensor->hw, ST_LPS22HB_BYPASS);
	if (err < 0)
		return err;

	err = st_lps22hb_write_with_mask(sensor->hw, ST_LPS22HB_CTRL3_ADDR,
					 ST_LPS22HB_INT_FTH_MASK, false);
	if (err < 0)
		return err;

	return st_lps22hb_set_enable(sensor, false);
}

static const struct iio_buffer_setup_ops st_lps22hb_buffer_ops = {
	.preenable = st_lps22hb_buffer_preenable,
	.postdisable = st_lps22hb_buffer_postdisable,
};

int st_lps22hb_allocate_buffers(struct st_lps22hb_hw *hw)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(5,13,0)
	struct iio_buffer *buffer;
#endif /* LINUX_VERSION_CODE */
	int err, i;

	err = devm_request_threaded_irq(hw->dev, hw->irq,
					st_lps22hb_irq_handler,
					st_lps22hb_irq_thread,
					IRQF_TRIGGER_HIGH | IRQF_ONESHOT,
					"lps22hb", hw);
	if (err)
		return err;

	for (i = 0; i < ST_LPS22HB_SENSORS_NUMB; i++) {

#if KERNEL_VERSION(5, 19, 0) <= LINUX_VERSION_CODE
		err = devm_iio_kfifo_buffer_setup(hw->dev, hw->iio_devs[i],
						  &st_lps22hb_buffer_ops);
		if (err)
			return err;
#elif KERNEL_VERSION(5, 13, 0) <= LINUX_VERSION_CODE
		err = devm_iio_kfifo_buffer_setup(hw->dev, hw->iio_devs[i],
						  INDIO_BUFFER_SOFTWARE,
						  &st_lps22hb_buffer_ops);
		if (err)
			return err;
#else /* LINUX_VERSION_CODE */
		buffer = devm_iio_kfifo_allocate(hw->dev);
		if (!buffer)
			return -ENOMEM;

		iio_device_attach_buffer(hw->iio_devs[i], buffer);
		hw->iio_devs[i]->modes |= INDIO_BUFFER_SOFTWARE;
		hw->iio_devs[i]->setup_ops = &st_lps22hb_buffer_ops;
#endif /* LINUX_VERSION_CODE */

	}

	return 0;
}
