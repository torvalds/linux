// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics lis2du12 fifo driver
 *
 * MEMS Software Solutions Team
 *
 * Copyright 2022 STMicroelectronics Inc.
 */

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/iio/kfifo_buf.h>
#include <linux/iio/events.h>
#include <linux/iio/iio.h>
#include <linux/iio/buffer.h>
#include <linux/of_device.h>
#include <linux/version.h>

#include "st_lis2du12.h"

#define ST_LIS2DU12_EWMA_LEVEL			120
#define ST_LIS2DU12_EWMA_DIV			128
static inline s64 st_lis2du12_ewma(s64 old, s64 new, int weight)
{
	s64 diff, incr;

	diff = new - old;
	incr = div_s64((ST_LIS2DU12_EWMA_DIV - weight) * diff,
		       ST_LIS2DU12_EWMA_DIV);

	return old + incr;
}

static int st_lis2du12_set_fifo_mode(struct st_lis2du12_hw *hw,
				     enum st_lis2du12_fifo_mode mode)
{
	int err;

	err = regmap_update_bits(hw->regmap,
				 ST_LIS2DU12_FIFO_CTRL_ADDR,
				 ST_LIS2DU12_FIFOMODE_MASK,
				 FIELD_PREP(ST_LIS2DU12_FIFOMODE_MASK, mode));
	if (err < 0)
		return err;

	hw->fifo_mode = mode;

	return 0;
}

static int st_lis2du12_update_fifo_watermark(struct st_lis2du12_sensor *sensor,
					     u8 watermark)
{
	u16 fifo_watermark = ST_LIS2DU12_MAX_WATERMARK;
	struct st_lis2du12_hw *hw = sensor->hw;
	struct st_lis2du12_sensor *cur_sensor;
	u16 cur_watermark = 0;
	int i, err;

	for (i = ST_LIS2DU12_ID_ACC; i <= ST_LIS2DU12_MAX_BUFFER; i++) {
		if (!hw->iio_devs[i])
			continue;

		cur_sensor = iio_priv(hw->iio_devs[i]);

		if (!(hw->enable_mask & BIT(cur_sensor->id)))
			continue;

		cur_watermark = (cur_sensor == sensor) ? watermark :
				cur_sensor->watermark;

		fifo_watermark = min_t(u8, fifo_watermark,
				       cur_watermark);
	}

	mutex_lock(&hw->fifo_lock);
	err = regmap_update_bits(hw->regmap,
				 ST_LIS2DU12_FIFO_WTM_ADDR,
				 ST_LIS2DU12_FTH_MASK,
				 FIELD_PREP(ST_LIS2DU12_FTH_MASK, fifo_watermark));
	if (err < 0)
		goto out;

	hw->fifo_watermark = fifo_watermark;

out:
	mutex_unlock(&hw->fifo_lock);

	return err;
}

static int st_lis2du12_update_fifo(struct iio_dev *iio_dev, bool enable)
{
	struct st_lis2du12_sensor *sensor = iio_priv(iio_dev);
	struct st_lis2du12_hw *hw = sensor->hw;
	int err;

	if (enable) {
		hw->ts_irq = hw->ts = st_lis2du12_get_timestamp(hw);
		hw->delta_ts = div_s64(1000000000LL, sensor->odr) *
			       hw->fifo_watermark;
		hw->samples = 0;
	}

	disable_irq(hw->irq);

	err = st_lis2du12_sensor_set_enable(sensor, enable);
	if (err < 0)
		goto out;

	if (sensor->id == ST_LIS2DU12_ID_TEMP) {
		u8 round = enable ? false : true;

		/* configure rounding accordingly */
		err = regmap_update_bits(hw->regmap,
					 ST_LIS2DU12_FIFO_CTRL_ADDR,
					 ST_LIS2DU12_ROUNDING_XYZ_MASK,
					 FIELD_PREP(ST_LIS2DU12_ROUNDING_XYZ_MASK, round));
		if (err < 0)
			return err;

		hw->round_xl_xyz = round;
	}

	st_lis2du12_update_fifo_watermark(sensor, sensor->watermark);

	if (enable && (hw->fifo_mode == ST_LIS2DU12_FIFO_BYPASS))
		err = st_lis2du12_set_fifo_mode(hw, ST_LIS2DU12_FIFO_CONTINUOUS);
	else if (!st_lis2du12_fifo_enabled(hw))
		err = st_lis2du12_set_fifo_mode(hw, ST_LIS2DU12_FIFO_BYPASS);

out:
	enable_irq(hw->irq);

	return err < 0 ? err : 0;
}

ssize_t st_lis2du12_set_hwfifo_watermark(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf, size_t size)
{
	struct iio_dev *iio_dev = dev_to_iio_dev(dev);
	struct st_lis2du12_sensor *sensor = iio_priv(iio_dev);
	int err, val;

	mutex_lock(&iio_dev->mlock);
	if (iio_buffer_enabled(iio_dev)) {
		err = -EBUSY;
		goto unlock;
	}

	err = kstrtoint(buf, 10, &val);
	if (err < 0)
		goto unlock;

	if (val < 1 || val > ST_LIS2DU12_MAX_WATERMARK) {
		err = -EINVAL;
		goto unlock;
	}

	err = st_lis2du12_update_fifo_watermark(sensor, val);
	if (err < 0)
		goto unlock;

	sensor->watermark = val;

unlock:
	mutex_unlock(&iio_dev->mlock);

	return err < 0 ? err : size;
}

static int st_lis2du12_buffer_preenable(struct iio_dev *iio_dev)
{
	return st_lis2du12_update_fifo(iio_dev, true);
}

static int st_lis2du12_buffer_postdisable(struct iio_dev *iio_dev)
{
	return st_lis2du12_update_fifo(iio_dev, false);
}

static const struct
iio_buffer_setup_ops st_lis2du12_buffer_setup_ops = {
	.preenable = st_lis2du12_buffer_preenable,
	.postdisable = st_lis2du12_buffer_postdisable,
};

static int st_lis2du12_read_fifo(struct st_lis2du12_hw *hw)
{
	u8 iio_buff[ALIGN(ST_LIS2DU12_ACC_DATA_SIZE, sizeof(s64)) + sizeof(s64)];
	u8 buff[16 * ST_LIS2DU12_ACC_DATA_SIZE], samples, data_size;
	struct iio_dev *iio_acc_dev = hw->iio_devs[ST_LIS2DU12_ID_ACC];
	struct iio_dev *iio_temp_dev = hw->iio_devs[ST_LIS2DU12_ID_TEMP];
	struct iio_chan_spec const *ch = iio_acc_dev->channels;
	int i, err, word_len, fifo_len, read_len = 0;
	s64 delta_ts;
	int status;

	err = regmap_read(hw->regmap, ST_LIS2DU12_FIFO_STATUS2_ADDR, &status);
	if (err < 0)
		return err;

	samples = status & ST_LIS2DU12_FSS_MASK;
	delta_ts = div_s64(hw->delta_ts, hw->fifo_watermark);

	if (hw->round_xl_xyz)
		data_size = ST_LIS2DU12_ACC_DATA_SIZE;
	else
		data_size = ST_LIS2DU12_DATA_SIZE;

	fifo_len = samples * data_size;

	while (read_len < fifo_len) {
		word_len = min_t(int, fifo_len - read_len,
				 sizeof(buff));
		err = st_lis2du12_read_locked(hw, ch[0].address,
					      buff, word_len);
		if (err < 0)
			return err;

		for (i = 0; i < word_len; i += data_size) {
			if (unlikely(++hw->samples < hw->std_level)) {
				hw->ts += delta_ts;
				continue;
			}

			hw->ts = min_t(s64,
				       st_lis2du12_get_timestamp(hw),
				       hw->ts);

			if (hw->enable_mask & BIT(ST_LIS2DU12_ID_ACC)) {
				memcpy(iio_buff, &buff[i],
				       ST_LIS2DU12_ACC_DATA_SIZE);
				iio_push_to_buffers_with_timestamp(iio_acc_dev,
								   iio_buff,
								   hw->ts);
			}

			if (!hw->round_xl_xyz &&
			    (hw->enable_mask & BIT(ST_LIS2DU12_ID_TEMP))) {
				memcpy(iio_buff,
				       &buff[i + ST_LIS2DU12_ACC_DATA_SIZE],
				       ST_LIS2DU12_TEMP_DATA_SIZE);
				iio_push_to_buffers_with_timestamp(iio_temp_dev,
								   iio_buff,
								   hw->ts);
			}

			hw->ts += delta_ts;
		}

		read_len += word_len;
	}

	return read_len;
}

ssize_t st_lis2du12_flush_fifo(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t size)
{
	struct iio_dev *iio_dev = dev_to_iio_dev(dev);
	struct st_lis2du12_sensor *sensor = iio_priv(iio_dev);
	struct st_lis2du12_hw *hw = sensor->hw;
	s64 code;
	int err;

	mutex_lock(&hw->fifo_lock);

	err = st_lis2du12_read_fifo(hw);
	hw->ts_irq = st_lis2du12_get_timestamp(hw);

	mutex_unlock(&hw->fifo_lock);

	code = IIO_UNMOD_EVENT_CODE(IIO_ACCEL, -1,
				    STM_IIO_EV_TYPE_FIFO_FLUSH,
				    IIO_EV_DIR_EITHER);
	iio_push_event(iio_dev, code, hw->ts_irq);

	return err < 0 ? err : size;
}


static irqreturn_t st_lis2du12_handler_irq(int irq, void *private)
{
	struct st_lis2du12_hw *hw = private;
	s64 ts;

	ts = st_lis2du12_get_timestamp(hw);
	hw->delta_ts = st_lis2du12_ewma(hw->delta_ts, ts - hw->ts_irq,
					ST_LIS2DU12_EWMA_LEVEL);
	hw->ts_irq = ts;

	return IRQ_WAKE_THREAD;
}

static irqreturn_t st_lis2du12_handler_thread(int irq, void *private)
{
	int status, all_int_source, wk_source, sixd_source;
	struct st_lis2du12_hw *hw = private;
	s64 code;
	int err;

	err = regmap_read(hw->regmap, ST_LIS2DU12_FIFO_STATUS1_ADDR,
			  &status);
	if (err < 0)
		return IRQ_HANDLED;

	if (status & ST_LIS2DU12_FTH_WTM_MASK) {
		mutex_lock(&hw->fifo_lock);
		st_lis2du12_read_fifo(hw);
		mutex_unlock(&hw->fifo_lock);
	}

	err = regmap_read(hw->regmap, ST_LIS2DU12_ALL_INT_SRC_ADDR,
			  &all_int_source);
	if (err < 0)
		return IRQ_HANDLED;

	if (((all_int_source & ST_LIS2DU12_SINGLE_TAP_ALL_MASK) &&
	     (hw->enable_mask & BIT(ST_LIS2DU12_ID_TAP))) ||
	    ((all_int_source & ST_LIS2DU12_DOUBLE_TAP_ALL_MASK) &&
	     (hw->enable_mask & BIT(ST_LIS2DU12_ID_TAP_TAP)))) {
		struct iio_dev *iio_dev;
		enum iio_chan_type type;
		int source;

		err = regmap_read(hw->regmap, ST_LIS2DU12_TAP_SRC_ADDR,
				  &source);
		if (err < 0)
			return IRQ_HANDLED;

		if (source & ST_LIS2DU12_DOUBLE_TAP_IA_MASK) {
			iio_dev = hw->iio_devs[ST_LIS2DU12_ID_TAP_TAP];
			type = STM_IIO_TAP_TAP;
			code = IIO_UNMOD_EVENT_CODE(type, -1,
						    IIO_EV_TYPE_THRESH,
						    IIO_EV_DIR_RISING);
			iio_push_event(iio_dev, code,
				       st_lis2du12_get_timestamp(hw));
		}

		if (source & ST_LIS2DU12_SINGLE_TAP_IA_MASK) {
			iio_dev = hw->iio_devs[ST_LIS2DU12_ID_TAP];
			type = STM_IIO_TAP;
			code = IIO_UNMOD_EVENT_CODE(type, -1,
						    IIO_EV_TYPE_THRESH,
						    IIO_EV_DIR_RISING);
			iio_push_event(iio_dev, code,
				       st_lis2du12_get_timestamp(hw));
		}
	}

	if ((all_int_source & ST_LIS2DU12_WU_IA_ALL_MASK) &&
	    (hw->enable_mask & BIT(ST_LIS2DU12_ID_WU))) {
		struct iio_dev *iio_dev;
		enum iio_chan_type type;
		u8 wu_src;

		err = regmap_read(hw->regmap, ST_LIS2DU12_WAKE_UP_SRC_ADDR,
				  &wk_source);
		if (err < 0)
			return IRQ_HANDLED;

		wu_src = wk_source & ST_LIS2DU12_WU_MASK;
		iio_dev = hw->iio_devs[ST_LIS2DU12_ID_WU];
		/* use STM_IIO_GESTURE event type for custom events */
		type = STM_IIO_GESTURE;
		code = IIO_UNMOD_EVENT_CODE(type, wu_src,
					    IIO_EV_TYPE_THRESH,
					    IIO_EV_DIR_RISING);
		iio_push_event(iio_dev, code,
			       st_lis2du12_get_timestamp(hw));
	}

	if ((all_int_source & ST_LIS2DU12_FF_IA_ALL_MASK) &&
	    (hw->enable_mask & BIT(ST_LIS2DU12_ID_FF))) {
		struct iio_dev *iio_dev;
		enum iio_chan_type type;

		iio_dev = hw->iio_devs[ST_LIS2DU12_ID_FF];
		/* use STM_IIO_GESTURE event type for custom events */
		type = STM_IIO_GESTURE;
		code = IIO_UNMOD_EVENT_CODE(type, 1,
					    IIO_EV_TYPE_THRESH,
					    IIO_EV_DIR_RISING);
		iio_push_event(iio_dev, code,
			       st_lis2du12_get_timestamp(hw));
	}

	if ((all_int_source & ST_LIS2DU12_D6D_IA_ALL_MASK) &&
	    (hw->enable_mask & BIT(ST_LIS2DU12_ID_6D))) {
		struct iio_dev *iio_dev;
		enum iio_chan_type type;
		u8 sixd_src;

		err = regmap_read(hw->regmap, ST_LIS2DU12_SIXD_SRC_ADDR,
				  &sixd_source);
		if (err < 0)
			return IRQ_HANDLED;

		sixd_src = sixd_source & ST_LIS2DU12_OVERTHRESHOLD_MASK;
		iio_dev = hw->iio_devs[ST_LIS2DU12_ID_6D];
		/* use IIO_GESTURE event type for custom events */
		type = STM_IIO_GESTURE;
		code = IIO_UNMOD_EVENT_CODE(type, sixd_src,
					    IIO_EV_TYPE_THRESH,
					    IIO_EV_DIR_RISING);
		iio_push_event(iio_dev, code,
			       st_lis2du12_get_timestamp(hw));
	}

	if ((all_int_source & ST_LIS2DU12_SLEEP_CHANGE_IA_ALL_MASK) &&
	    (hw->enable_mask & BIT(ST_LIS2DU12_ID_ACT))) {
		struct iio_dev *iio_dev;
		enum iio_chan_type type;
		u8 sleep_state;

		err = regmap_read(hw->regmap, ST_LIS2DU12_WAKE_UP_SRC_ADDR,
				  &wk_source);
		if (err < 0)
			return IRQ_HANDLED;

		sleep_state = wk_source & ST_LIS2DU12_SLEEP_STATE_MASK;
		iio_dev = hw->iio_devs[ST_LIS2DU12_ID_ACT];
		/* use IIO_GESTURE event type for custom events */
		type = STM_IIO_GESTURE;
		code = IIO_UNMOD_EVENT_CODE(type, sleep_state,
					    IIO_EV_TYPE_THRESH,
					    IIO_EV_DIR_RISING);
		iio_push_event(iio_dev, code,
			       st_lis2du12_get_timestamp(hw));
	}

	return IRQ_HANDLED;
}

int st_lis2du12_buffer_setup(struct st_lis2du12_hw *hw)
{
	struct device_node *np = hw->dev->of_node;

#if KERNEL_VERSION(5, 13, 0) > LINUX_VERSION_CODE
	struct iio_buffer *buffer;
#endif /* LINUX_VERSION_CODE */

	unsigned long irq_type;
	u8 irq_active_low, i;
	int ret;

	irq_type = irqd_get_trigger_type(irq_get_irq_data(hw->irq));
	if (irq_type == IRQF_TRIGGER_NONE)
		irq_type = IRQF_TRIGGER_HIGH;

	switch (irq_type) {
	case IRQF_TRIGGER_HIGH:
	case IRQF_TRIGGER_RISING:
		irq_active_low = 0;
		break;
	case IRQF_TRIGGER_LOW:
	case IRQF_TRIGGER_FALLING:
		irq_active_low = 1;
		break;
	default:
		dev_info(hw->dev, "mode %lx unsupported\n", irq_type);
		return -EINVAL;
	}

	if (irq_active_low) {
		/* configure interrupts to low level */
		ret = regmap_update_bits(hw->regmap,
					 ST_LIS2DU12_INTERRUPT_CFG_ADDR,
					 ST_LIS2DU12_H_LACTIVE_MASK,
					 FIELD_PREP(ST_LIS2DU12_H_LACTIVE_MASK, 1));
		if (ret < 0)
			return ret;
	}

	/* check pull down disable on int1 pin property */
	if (np && of_property_read_bool(np, "pd_dis_int1")) {
		ret = regmap_update_bits(hw->regmap,
					 ST_LIS2DU12_IF_CTRL_ADDR,
					 ST_LIS2DU12_PD_DIS_INT1_MASK,
					 FIELD_PREP(ST_LIS2DU12_PD_DIS_INT1_MASK, 1));
		if (ret < 0)
			return ret;
	}

	/* check push pull / open drain int pin property */
	if (np && of_property_read_bool(np, "pp_od_int")) {
		ret = regmap_update_bits(hw->regmap,
					 ST_LIS2DU12_CTRL1_ADDR,
					 ST_LIS2DU12_PP_OD_MASK,
					 FIELD_PREP(ST_LIS2DU12_PP_OD_MASK, 1));
		if (ret < 0)
			return ret;
	}

	ret = devm_request_threaded_irq(hw->dev, hw->irq,
					st_lis2du12_handler_irq,
					st_lis2du12_handler_thread,
					irq_type | IRQF_ONESHOT,
					"st_lis2du12", hw);
	if (ret) {
		dev_err(hw->dev, "failed to request trigger irq %d\n",
			hw->irq);
		return ret;
	}

	/* configure rounding to read only acc data from FIFO */
	ret = regmap_update_bits(hw->regmap,
				 ST_LIS2DU12_FIFO_CTRL_ADDR,
				 ST_LIS2DU12_ROUNDING_XYZ_MASK,
				 FIELD_PREP(ST_LIS2DU12_ROUNDING_XYZ_MASK, 1));
	if (ret < 0)
		return ret;

	hw->round_xl_xyz = true;

	for (i = ST_LIS2DU12_ID_ACC; i <= ST_LIS2DU12_MAX_BUFFER; i++) {
		if (!hw->iio_devs[i])
			continue;

#if KERNEL_VERSION(5, 19, 0) <= LINUX_VERSION_CODE
	ret = devm_iio_kfifo_buffer_setup(hw->dev, hw->iio_devs[i],
					  &st_lis2du12_buffer_setup_ops);
	if (ret)
		return ret;
#elif KERNEL_VERSION(5, 13, 0) <= LINUX_VERSION_CODE
		ret = devm_iio_kfifo_buffer_setup(hw->dev, hw->iio_devs[i],
						  INDIO_BUFFER_SOFTWARE,
						  &st_lis2du12_buffer_setup_ops);
		if (ret)
			return ret;
#else /* LINUX_VERSION_CODE */
		buffer = devm_iio_kfifo_allocate(hw->dev);
		if (!buffer)
			return -ENOMEM;

		iio_device_attach_buffer(hw->iio_devs[i], buffer);
		hw->iio_devs[i]->modes |= INDIO_BUFFER_SOFTWARE;
		hw->iio_devs[i]->setup_ops = &st_lis2du12_buffer_setup_ops;
#endif /* LINUX_VERSION_CODE */
	}

	ret = st_lis2du12_set_fifo_mode(hw, ST_LIS2DU12_FIFO_BYPASS);
	if (ret < 0)
		return ret;

	return regmap_update_bits(hw->regmap,
				  hw->drdy_reg,
				  ST_LIS2DU12_INT_F_FTH_MASK,
				  FIELD_PREP(ST_LIS2DU12_INT_F_FTH_MASK, 1));
}
