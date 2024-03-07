// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics st_acc33 sensor driver
 *
 * MEMS Software Solutions Team
 *
 * Copyright 2016 STMicroelectronics Inc.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/iio/events.h>
#include <linux/iio/iio.h>
#include <linux/iio/kfifo_buf.h>
#include <linux/iio/buffer.h>
#include <linux/iio/trigger.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/version.h>

#include "st_acc33.h"

#define REG_CTRL5_ACC_ADDR		0x24
#define REG_CTRL5_ACC_FIFO_EN_MASK	BIT(6)
#define REG_CTRL5_ACC_BOOT_MASK		BIT(7)

#define REG_FIFO_CTRL_REG		0x2e
#define REG_FIFO_CTRL_REG_WTM_MASK	GENMASK(4, 0)
#define REG_FIFO_CTRL_MODE_MASK		GENMASK(7, 6)

#define REG_FIFO_SRC_ADDR		0x2f
#define REG_FIFO_SRC_OVR_MASK		BIT(6)
#define REG_FIFO_SRC_FSS_MASK		GENMASK(4, 0)

#define ST_ACC33_MAX_WATERMARK		28

static inline s64 st_acc33_get_timestamp(struct iio_dev *iio_dev)
{
	return iio_get_time_ns(iio_dev);
}

#define ST_ACC33_EWMA_DIV			128
static inline s64 st_acc33_ewma(s64 old, s64 new, int weight)
{
	s64 diff, incr;

	diff = new - old;
	incr = div_s64((ST_ACC33_EWMA_DIV - weight) * diff,
		       ST_ACC33_EWMA_DIV);

	return old + incr;
}

static int st_acc33_set_fifo_mode(struct st_acc33_hw *hw,
				  enum st_acc33_fifo_mode mode)
{
	return st_acc33_write_with_mask(hw, REG_FIFO_CTRL_REG,
					REG_FIFO_CTRL_MODE_MASK, mode);
}

static int st_acc33_read_fifo(struct st_acc33_hw *hw)
{
	u8 iio_buff[ALIGN(ST_ACC33_DATA_SIZE, sizeof(s64)) + sizeof(s64)];
	u8 buff[ST_ACC33_RX_MAX_LENGTH], data, nsamples;
	struct iio_dev *iio_dev = hw->iio_dev;
	struct iio_chan_spec const *ch = iio_dev->channels;
	int i, err, word_len, fifo_len, read_len = 0;
	s64 delta_ts;

	err = hw->tf->read(hw->dev, REG_FIFO_SRC_ADDR, sizeof(data), &data);
	if (err < 0)
		return err;

	delta_ts = div_s64(hw->delta_ts, (hw->watermark + 1));
	nsamples = data & REG_FIFO_SRC_FSS_MASK;
	fifo_len = nsamples * ST_ACC33_DATA_SIZE;

	while (read_len < fifo_len) {
		word_len = min_t(int, fifo_len - read_len, sizeof(buff));
		err = hw->tf->read(hw->dev, ch[0].address, word_len, buff);
		if (err < 0)
			return err;

		for (i = 0; i < word_len; i += ST_ACC33_DATA_SIZE) {
			if (unlikely(++hw->samples <= hw->std_level)) {
				hw->ts += delta_ts;
				continue;
			}

			memcpy(iio_buff, &buff[i], ST_ACC33_DATA_SIZE);
			iio_push_to_buffers_with_timestamp(iio_dev, iio_buff,
							   hw->ts);
			hw->ts += delta_ts;
		}
		read_len += word_len;
	}

	return read_len;
}

ssize_t st_acc33_flush_hwfifo(struct device *device,
			      struct device_attribute *attr,
			      const char *buf, size_t size)
{
	struct iio_dev *iio_dev = dev_to_iio_dev(device);
	struct st_acc33_hw *hw = iio_priv(iio_dev);
	s64 code;
	int err;

	mutex_lock(&hw->fifo_lock);

	err = st_acc33_read_fifo(hw);
	hw->ts_irq = st_acc33_get_timestamp(iio_dev);

	mutex_unlock(&hw->fifo_lock);

	code = IIO_UNMOD_EVENT_CODE(IIO_ACCEL, -1,
				    STM_IIO_EV_TYPE_FIFO_FLUSH,
				    IIO_EV_DIR_EITHER);
	iio_push_event(iio_dev, code, hw->ts_irq);

	return err < 0 ? err : size;
}

ssize_t st_acc33_get_hwfifo_watermark(struct device *device,
				      struct device_attribute *attr,
				      char *buf)
{
	struct iio_dev *iio_dev = dev_to_iio_dev(device);
	struct st_acc33_hw *hw = iio_priv(iio_dev);

	return sprintf(buf, "%d\n", hw->watermark);
}

int st_acc33_update_watermark(struct st_acc33_hw *hw, u8 watermark)
{
	return st_acc33_write_with_mask(hw, REG_FIFO_CTRL_REG,
					REG_FIFO_CTRL_REG_WTM_MASK,
					watermark);
}

ssize_t st_acc33_set_hwfifo_watermark(struct device *device,
				      struct device_attribute *attr,
				      const char *buf, size_t size)
{
	struct iio_dev *iio_dev = dev_to_iio_dev(device);
	struct st_acc33_hw *hw = iio_priv(iio_dev);
	int err, val;

	mutex_lock(&iio_dev->mlock);
	if (iio_buffer_enabled(iio_dev)) {
		err = -EBUSY;
		goto unlock;
	}

	err = kstrtoint(buf, 10, &val);
	if (err < 0)
		goto unlock;

	if (val < 1 || val > ST_ACC33_MAX_WATERMARK) {
		err = -EINVAL;
		goto unlock;
	}

	err = st_acc33_update_watermark(hw, val);
	if (err < 0)
		goto unlock;

	hw->watermark = val;

unlock:
	mutex_unlock(&iio_dev->mlock);

	return err < 0 ? err : size;
}

ssize_t st_acc33_get_max_hwfifo_watermark(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	return sprintf(buf, "%d\n", ST_ACC33_MAX_WATERMARK);
}

static irqreturn_t st_acc33_buffer_handler_irq(int irq, void *private)
{
	struct st_acc33_hw *hw = private;
	struct iio_dev *iio_dev = hw->iio_dev;
	u8 ewma_level;
	s64 ts;

	ewma_level = hw->odr >= 100 ? 120 : 96;
	ts = st_acc33_get_timestamp(iio_dev);
	hw->delta_ts = st_acc33_ewma(hw->delta_ts, ts - hw->ts_irq,
				     ewma_level);
	hw->ts_irq = ts;

	return IRQ_WAKE_THREAD;
}

static irqreturn_t st_acc33_buffer_handler_thread(int irq, void *private)
{
	struct st_acc33_hw *hw = private;

	mutex_lock(&hw->fifo_lock);
	st_acc33_read_fifo(hw);
	mutex_unlock(&hw->fifo_lock);

	return IRQ_HANDLED;
}

static int st_acc33_update_fifo(struct st_acc33_hw *hw, bool enable)
{
	struct iio_dev *iio_dev = hw->iio_dev;
	enum st_acc33_fifo_mode mode;

	int err;

	if (enable) {
		hw->ts_irq = hw->ts = st_acc33_get_timestamp(iio_dev);
		hw->delta_ts = div_s64(1000000000LL, hw->odr) *
			       (hw->watermark + 1);
		hw->samples = 0;
	}

	err = st_acc33_write_with_mask(hw, REG_CTRL5_ACC_ADDR,
				       REG_CTRL5_ACC_FIFO_EN_MASK, enable);
	if (err < 0)
		return err;

	mode = enable ? ST_ACC33_FIFO_STREAM : ST_ACC33_FIFO_BYPASS;
	err = st_acc33_set_fifo_mode(hw, mode);
	if (err < 0)
		return err;

	return st_acc33_set_enable(hw, enable);
}

static int st_acc33_buffer_preenable(struct iio_dev *iio_dev)
{
	return st_acc33_update_fifo(iio_priv(iio_dev), true);
}

static int st_acc33_buffer_postdisable(struct iio_dev *iio_dev)
{
	return st_acc33_update_fifo(iio_priv(iio_dev), false);
}

static const struct iio_buffer_setup_ops st_acc33_buffer_ops = {
	.preenable = st_acc33_buffer_preenable,
	.postdisable = st_acc33_buffer_postdisable,
};

int st_acc33_fifo_setup(struct st_acc33_hw *hw)
{
	struct iio_dev *iio_dev = hw->iio_dev;
#if LINUX_VERSION_CODE < KERNEL_VERSION(5,13,0)
	struct iio_buffer *buffer;
#endif /* LINUX_VERSION_CODE */
	int ret;

	ret = devm_request_threaded_irq(hw->dev, hw->irq,
					st_acc33_buffer_handler_irq,
					st_acc33_buffer_handler_thread,
					IRQF_TRIGGER_HIGH | IRQF_ONESHOT,
					hw->name, hw);
	if (ret) {
		dev_err(hw->dev, "failed to request trigger irq %d\n",
			hw->irq);
		return ret;
	}

#if KERNEL_VERSION(5, 19, 0) <= LINUX_VERSION_CODE
	ret = devm_iio_kfifo_buffer_setup(hw->dev, iio_dev,
					  &st_acc33_buffer_ops);
	if (ret)
		return ret;
#elif KERNEL_VERSION(5, 13, 0) <= LINUX_VERSION_CODE
	ret = devm_iio_kfifo_buffer_setup(hw->dev, iio_dev,
					  INDIO_BUFFER_SOFTWARE,
					  &st_acc33_buffer_ops);
	if (ret)
		return ret;
#else /* LINUX_VERSION_CODE */
	buffer = devm_iio_kfifo_allocate(hw->dev);
	if (!buffer)
		return -ENOMEM;

	iio_device_attach_buffer(iio_dev, buffer);
	iio_dev->setup_ops = &st_acc33_buffer_ops;
	iio_dev->modes |= INDIO_BUFFER_SOFTWARE;
#endif /* LINUX_VERSION_CODE */

	return 0;
}
