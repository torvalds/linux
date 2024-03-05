// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics st_lis2duxs12 FIFO buffer library driver
 *
 * MEMS Software Solutions Team
 *
 * Copyright 2022 STMicroelectronics Inc.
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

#include "st_lis2duxs12.h"

/* Timestamp convergence filter parameters */
#define ST_LIS2DUXS12_EWMA_LEVEL			120
#define ST_LIS2DUXS12_EWMA_DIV				128

/* FIFO tags */
enum {
	ST_LIS2DUXS12_ACC_TEMP_TAG = 0x02,
	ST_LIS2DUXS12_TS_TAG = 0x04,
	ST_LIS2DUXS12_STEP_COUNTER_TAG = 0x12,
	ST_LIS2DUXS12_ACC_QVAR_TAG = 0x1f,
};

static inline s64 st_lis2duxs12_ewma(s64 old, s64 new, int weight)
{
	s64 diff, incr;

	diff = new - old;
	incr = div_s64((ST_LIS2DUXS12_EWMA_DIV - weight) * diff,
			ST_LIS2DUXS12_EWMA_DIV);

	return old + incr;
}

static inline int st_lis2duxs12_reset_hwts(struct st_lis2duxs12_hw *hw)
{
	u8 data = 0xaa;

	hw->ts = iio_get_time_ns(hw->iio_devs[0]);
	hw->ts_offset = hw->ts;
	hw->tsample = 0ull;

	return st_lis2duxs12_write_locked(hw, ST_LIS2DUXS12_TIMESTAMP2_ADDR,
					  data);
}

int st_lis2duxs12_set_fifo_mode(struct st_lis2duxs12_hw *hw,
				enum st_lis2duxs12_fifo_mode fifo_mode)
{
	int err;

	err = st_lis2duxs12_write_with_mask_locked(hw,
					   ST_LIS2DUXS12_FIFO_CTRL_ADDR,
					   ST_LIS2DUXS12_FIFO_MODE_MASK,
					   fifo_mode);
	if (err < 0)
		return err;

	hw->fifo_mode = fifo_mode;

	return 0;
}

static int st_lis2duxs12_update_watermark(struct st_lis2duxs12_sensor *sensor,
					  u8 watermark)
{
	u8 fifo_watermark = ST_LIS2DUXS12_MAX_FIFO_DEPTH;
	struct st_lis2duxs12_hw *hw = sensor->hw;
	struct st_lis2duxs12_sensor *cur_sensor;
	u8 cur_watermark = 0;
	int err;
	int i;

	for (i = 0; i < ARRAY_SIZE(st_lis2duxs12_buffered_sensor_list);
	     i++) {
		enum st_lis2duxs12_sensor_id id =
					  st_lis2duxs12_buffered_sensor_list[i];

		if (!hw->iio_devs[id])
			continue;

		cur_sensor = iio_priv(hw->iio_devs[id]);

		if (!(hw->enable_mask & BIT(cur_sensor->id)))
			continue;

		cur_watermark = (cur_sensor == sensor) ?
				watermark : cur_sensor->watermark;

		fifo_watermark = min_t(u8, fifo_watermark,
				       cur_watermark);
	}

	fifo_watermark = max_t(u8, fifo_watermark,
			       hw->timestamp ? 2 : 1);

	err = st_lis2duxs12_write_with_mask_locked(hw,
					    ST_LIS2DUXS12_FIFO_WTM_ADDR,
					    ST_LIS2DUXS12_FIFO_WTM_MASK,
					    fifo_watermark);

	return err;
}

static int st_lis2duxs12_read_fifo(struct st_lis2duxs12_hw *hw)
{
	u8 iio_buf[ALIGN(ST_LIS2DUXS12_SAMPLE_SIZE, sizeof(s64)) + sizeof(s64)];
	u8 buf[6 * ST_LIS2DUXS12_FIFO_SAMPLE_SIZE], tag, *ptr;
	int i, err, word_len, fifo_len, read_len;
	u8 fifo_status, fifo_depth;
	s64 ts_irq, hw_ts_old;
	u32 val;

	if (hw->fifo_mode == ST_LIS2DUXS12_FIFO_BYPASS)
		return 0;

	ts_irq = hw->ts -hw->delta_ts;

	err = st_lis2duxs12_read_locked(hw,
					ST_LIS2DUXS12_FIFO_STATUS2_ADDR,
					&fifo_status,
					sizeof(fifo_status));
	if (err < 0)
		return err;

	fifo_depth = fifo_status & ST_LIS2DUXS12_FIFO_FSS_MASK;
	if (!fifo_depth)
		return 0;

	fifo_len = fifo_depth * ST_LIS2DUXS12_FIFO_SAMPLE_SIZE;
	read_len = 0;

	while (read_len < fifo_len) {
		word_len = min_t(int, fifo_len - read_len, sizeof(buf));
		err = st_lis2duxs12_read_locked(hw,
				ST_LIS2DUXS12_FIFO_DATA_OUT_TAG_ADDR,
				buf, word_len);
		if (err < 0)
			return err;

		for (i = 0; i < word_len; i += ST_LIS2DUXS12_FIFO_SAMPLE_SIZE) {
			ptr = &buf[i + ST_LIS2DUXS12_TAG_SIZE];
			tag = buf[i] >> 3;

			switch (tag) {
			case ST_LIS2DUXS12_TS_TAG:
				val = get_unaligned_le32(ptr + 2);
				hw_ts_old = hw->hw_ts;
				hw->hw_ts = val * ST_LIS2DUXS12_TS_DELTA_NS;
				hw->ts_offset =
					st_lis2duxs12_ewma(hw->ts_offset,
						ts_irq - hw->hw_ts,
						ST_LIS2DUXS12_EWMA_LEVEL);
				ts_irq += hw->hw_ts;

				if (!hw->tsample)
					hw->tsample = hw->ts_offset + hw->hw_ts;
				else
					hw->tsample = hw->tsample + hw->hw_ts -
						      hw_ts_old;
				break;
			case ST_LIS2DUXS12_ACC_TEMP_TAG: {
				struct iio_dev *iio_dev =
					     hw->iio_devs[ST_LIS2DUXS12_ID_ACC];

				if (hw->timestamp)
					hw->tsample = min_t(s64,
						       iio_get_time_ns(iio_dev),
						       hw->tsample);
				else
					hw->tsample = iio_get_time_ns(iio_dev);

				hw->last_fifo_timestamp = hw->tsample;

				if (hw->xl_only) {
					/*
					 * data representation in FIFO
					 * when ACC only:
					 *  ----------- -----------
					 * |    LSBX   |    MSBX   |
					 *  ----------- -----------
					 * |    LSBY   |    MSBY   |
					 *  ----------- -----------
					 * |    LSBZ   |    MSBZ   |
					 *  ----------- -----------
					 */
					memcpy(iio_buf,
					       ptr, ST_LIS2DUXS12_SAMPLE_SIZE);
					if (unlikely(++hw->samples < hw->std_level))
						continue;

					iio_push_to_buffers_with_timestamp(iio_dev,
							  iio_buf, hw->tsample);
				} else {
					struct raw_data_compact_t *raw_data_c;
					struct iio_dev *iio_temp_dev;
					struct raw_data_t raw_data;
					__le16 temp;

					raw_data_c = (struct raw_data_compact_t *)ptr;
					iio_temp_dev =
					    hw->iio_devs[ST_LIS2DUXS12_ID_TEMP];

					/*
					 * data representation in FIFO
					 * when ACC/Temp available:
					 *  ------------- -------------
					 * |    LSB0     | LSN1 | MSN0 |
					 *  ------------- -------------
					 * |    MSB1     |    LSB2     |
					 *  ------------- -------------
					 * | LSN3 | MSN2 |    MSB3     |
					 *  ------------- -------------
					 */

					/* extends to 16 bit */
					temp = cpu_to_le16(le16_to_cpu(raw_data_c->t) << 4);

					memcpy(iio_buf, (u8 *)&temp, sizeof(temp));
					iio_push_to_buffers_with_timestamp(iio_temp_dev,
							  iio_buf, hw->tsample);

					if (unlikely(++hw->samples < hw->std_level))
						continue;

					/* extends to 16 bit */
					raw_data.x = cpu_to_le16(le16_to_cpu(raw_data_c->x) << 4);
					raw_data.y = cpu_to_le16(le16_to_cpu(raw_data_c->y) << 4);
					raw_data.z = cpu_to_le16(le16_to_cpu(raw_data_c->z) << 4);

					memcpy(iio_buf, (u8 *)&raw_data, sizeof(raw_data));
					iio_push_to_buffers_with_timestamp(iio_dev,
							  iio_buf, hw->tsample);
				}
				break;
			}
			case ST_LIS2DUXS12_ACC_QVAR_TAG: {
				struct iio_dev *iio_dev =
					     hw->iio_devs[ST_LIS2DUXS12_ID_ACC];
				struct raw_data_compact_t *raw_data_c;
				struct iio_dev *iio_qvar_dev;
				struct raw_data_t raw_data;
				__le16 qvar;

				if (hw->timestamp)
					hw->tsample = min_t(s64,
						       iio_get_time_ns(iio_dev),
						       hw->tsample);
				else
					hw->tsample = iio_get_time_ns(iio_dev);

				hw->last_fifo_timestamp = hw->tsample;

				raw_data_c = (struct raw_data_compact_t *)ptr;
				iio_qvar_dev =
					    hw->iio_devs[ST_LIS2DUXS12_ID_QVAR];

				/*
				 * data representation in FIFO
				 * when ACC/Qvar available:
				 *  ------------- -------------
				 * |    LSB0     | LSN1 | MSN0 |
				 *  ------------- -------------
				 * |    MSB1     |    LSB2     |
				 *  ------------- -------------
				 * | LSN3 | MSN2 |    MSB3     |
				 *  ------------- -------------
				 */

				/* extends to 16 bit */
				qvar = cpu_to_le16(le16_to_cpu(raw_data_c->t) << 4);

				memcpy(iio_buf, (u8 *)&qvar, sizeof(qvar));
				iio_push_to_buffers_with_timestamp(iio_qvar_dev,
								   iio_buf,
								   hw->tsample);

				/* skip push acc if not enabled */
				if (!(hw->enable_mask & BIT(ST_LIS2DUXS12_ID_ACC)) ||
				    unlikely(++hw->samples < hw->std_level))
					continue;

				/* extends to 16 bit */
				raw_data.x = cpu_to_le16(le16_to_cpu(raw_data_c->x) << 4);
				raw_data.y = cpu_to_le16(le16_to_cpu(raw_data_c->y) << 4);
				raw_data.z = cpu_to_le16(le16_to_cpu(raw_data_c->z) << 4);

				memcpy(iio_buf, (u8 *)&raw_data, sizeof(raw_data));
				iio_push_to_buffers_with_timestamp(iio_dev,
								   iio_buf,
								   hw->tsample);
				break;
			}
			case ST_LIS2DUXS12_STEP_COUNTER_TAG: {
				struct iio_dev *iio_dev =
				    hw->iio_devs[ST_LIS2DUXS12_ID_STEP_COUNTER];

				val = get_unaligned_le32(ptr + 2);
				hw->tsample = val * ST_LIS2DUXS12_TS_DELTA_NS;
				memcpy(iio_buf, ptr, ST_LIS2DUXS12_SAMPLE_SIZE);
				iio_push_to_buffers_with_timestamp(iio_dev,
								   iio_buf,
								   hw->tsample);
				break;
			}
			default:
				break;
			}
		}

		read_len += word_len;
	}

	return read_len;
}

ssize_t st_lis2duxs12_get_max_watermark(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct iio_dev *iio_dev = dev_to_iio_dev(dev);
	struct st_lis2duxs12_sensor *sensor = iio_priv(iio_dev);

	return sprintf(buf, "%d\n", sensor->max_watermark);
}

ssize_t st_lis2duxs12_get_watermark(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	struct iio_dev *iio_dev = dev_to_iio_dev(dev);
	struct st_lis2duxs12_sensor *sensor = iio_priv(iio_dev);

	return sprintf(buf, "%d\n", sensor->watermark);
}

ssize_t st_lis2duxs12_set_watermark(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t size)
{
	struct iio_dev *iio_dev = dev_to_iio_dev(dev);
	struct st_lis2duxs12_sensor *sensor = iio_priv(iio_dev);
	int err, val;

	err = iio_device_claim_direct_mode(iio_dev);
	if (err)
		return err;

	err = kstrtoint(buf, 10, &val);
	if (err < 0)
		goto out;

	err = st_lis2duxs12_update_watermark(sensor, val);
	if (err < 0)
		goto out;

	sensor->watermark = val;

out:
	iio_device_release_direct_mode(iio_dev);

	return err < 0 ? err : size;
}

ssize_t st_lis2duxs12_flush_fifo(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t size)
{
	struct iio_dev *iio_dev = dev_to_iio_dev(dev);
	struct st_lis2duxs12_sensor *sensor = iio_priv(iio_dev);
	struct st_lis2duxs12_hw *hw = sensor->hw;
	s64 type, event, fts, ts;
	int count;

	mutex_lock(&hw->fifo_lock);
	ts = iio_get_time_ns(iio_dev);
	hw->delta_ts = ts -hw->ts;
	hw->ts = ts;
	set_bit(ST_LIS2DUXS12_HW_FLUSH, &hw->state);
	count = st_lis2duxs12_read_fifo(hw);
	sensor->dec_counter = 0;
	if (count > 0)
		fts = hw->last_fifo_timestamp;
	else
		fts = ts;
	mutex_unlock(&hw->fifo_lock);

	type = count > 0 ? STM_IIO_EV_DIR_FIFO_DATA : STM_IIO_EV_DIR_FIFO_EMPTY;
	event = IIO_UNMOD_EVENT_CODE(iio_dev->channels[0].type, -1,
				     STM_IIO_EV_TYPE_FIFO_FLUSH, type);
	iio_push_event(iio_dev, event, fts);

	return size;
}

int st_lis2duxs12_suspend_fifo(struct st_lis2duxs12_hw *hw)
{
	int err;

	mutex_lock(&hw->fifo_lock);
	st_lis2duxs12_read_fifo(hw);
	err = st_lis2duxs12_set_fifo_mode(hw, ST_LIS2DUXS12_FIFO_BYPASS);
	mutex_unlock(&hw->fifo_lock);

	return err;
}

static int st_lis2duxs12_update_fifo(struct iio_dev *iio_dev, bool enable)
{
	struct st_lis2duxs12_sensor *sensor = iio_priv(iio_dev);
	struct st_lis2duxs12_hw *hw = sensor->hw;
	int err;

	disable_irq(hw->irq);
	mutex_lock(&hw->fifo_lock);

	switch (sensor->id) {
	case ST_LIS2DUXS12_ID_QVAR: {
		u8 xl_only = enable ? 0 : 1;

		/*
		 * check consistency, temperature sensor need to be
		 * disabled because share the same QVAR output registers
		 */
		if (hw->enable_mask & BIT(ST_LIS2DUXS12_ID_TEMP)) {
			err = -EBUSY;

			goto out;
		}

		err = st_lis2duxs12_qvar_set_enable(sensor, enable);
		if (err < 0)
			goto out;

		/* enable XL and Temp */
		err = regmap_update_bits(hw->regmap,
			     ST_LIS2DUXS12_FIFO_WTM_ADDR,
			     ST_LIS2DUXS12_XL_ONLY_FIFO_MASK,
			     FIELD_PREP(ST_LIS2DUXS12_XL_ONLY_FIFO_MASK,
					xl_only));
		if (err < 0)
			goto out;

		hw->xl_only = !!xl_only;
		break;
	}
	case ST_LIS2DUXS12_ID_TEMP: {
		u8 xl_only = enable ? 0 : 1;

		/*
		 * check consistency, QVAR sensor need to be disabled
		 * because share the same TEMP output registers
		 */
		if (hw->enable_mask & BIT(ST_LIS2DUXS12_ID_QVAR)) {
			err = -EBUSY;

			goto out;
		}

		err = st_lis2duxs12_sensor_set_enable(sensor, enable);
		if (err < 0)
			goto out;

		/* enable XL and Temp */
		err = regmap_update_bits(hw->regmap,
			     ST_LIS2DUXS12_FIFO_WTM_ADDR,
			     ST_LIS2DUXS12_XL_ONLY_FIFO_MASK,
			     FIELD_PREP(ST_LIS2DUXS12_XL_ONLY_FIFO_MASK,
					xl_only));
		if (err < 0)
			goto out;

		hw->xl_only = !!xl_only;
		break;
	}
	case ST_LIS2DUXS12_ID_STEP_COUNTER:
		err = st_lis2duxs12_step_counter_set_enable(sensor, enable);
		if (err < 0)
			goto out;
		break;
	case ST_LIS2DUXS12_ID_ACC:
		err = st_lis2duxs12_sensor_set_enable(sensor, enable);
		if (err < 0)
			goto out;
		break;
	default:
		break;
	}

	err = st_lis2duxs12_update_watermark(sensor, sensor->watermark);
	if (err < 0)
		goto out;

	if (enable && hw->fifo_mode == ST_LIS2DUXS12_FIFO_BYPASS) {
		st_lis2duxs12_reset_hwts(hw);
		err = st_lis2duxs12_set_fifo_mode(hw,
						  ST_LIS2DUXS12_FIFO_CONT);
	} else if (!(hw->enable_mask & ST_LIS2DUXS12_BUFFERED_ENABLED)) {
		err = st_lis2duxs12_set_fifo_mode(hw,
						  ST_LIS2DUXS12_FIFO_BYPASS);
	}

out:
	mutex_unlock(&hw->fifo_lock);
	enable_irq(hw->irq);

	return err;
}

static irqreturn_t st_lis2duxs12_handler_irq(int irq, void *private)
{
	struct st_lis2duxs12_hw *hw = (struct st_lis2duxs12_hw *)private;
	s64 ts = iio_get_time_ns(hw->iio_devs[0]);

	hw->delta_ts = ts -hw->ts;
	hw->ts = ts;

	return IRQ_WAKE_THREAD;
}

static irqreturn_t st_lis2duxs12_handler_thread(int irq, void *private)
{
	struct st_lis2duxs12_hw *hw = (struct st_lis2duxs12_hw *)private;

	st_lis2duxs12_mlc_check_status(hw);

	mutex_lock(&hw->fifo_lock);
	st_lis2duxs12_read_fifo(hw);
	clear_bit(ST_LIS2DUXS12_HW_FLUSH, &hw->state);
	mutex_unlock(&hw->fifo_lock);

	if (hw->enable_mask & (BIT(ST_LIS2DUXS12_ID_STEP_DETECTOR) |
			       BIT(ST_LIS2DUXS12_ID_TILT) |
			       BIT(ST_LIS2DUXS12_ID_SIGN_MOTION))) {
		struct iio_dev *iio_dev;
		u8 status;
		s64 event;
		int err;

		err = st_lis2duxs12_read_locked(hw,
			    ST_LIS2DUXS12_EMB_FUNC_STATUS_MAINPAGE_ADDR,
			    &status, sizeof(status));
		if (err < 0)
			goto out;

		/* embedded function sensors */
		if (status & ST_LIS2DUXS12_IS_STEP_DET_MASK) {
			iio_dev = hw->iio_devs[ST_LIS2DUXS12_ID_STEP_DETECTOR];
			event = IIO_UNMOD_EVENT_CODE(IIO_STEPS, -1,
						     IIO_EV_TYPE_THRESH,
						     IIO_EV_DIR_RISING);
			iio_push_event(iio_dev, event,
				       iio_get_time_ns(iio_dev));
		}

		if (status & ST_LIS2DUXS12_IS_SIGMOT_MASK) {
			iio_dev = hw->iio_devs[ST_LIS2DUXS12_ID_SIGN_MOTION];
			event = IIO_UNMOD_EVENT_CODE(STM_IIO_SIGN_MOTION, -1,
						     IIO_EV_TYPE_THRESH,
						     IIO_EV_DIR_RISING);
			iio_push_event(iio_dev, event,
				       iio_get_time_ns(iio_dev));
		}

		if (status & ST_LIS2DUXS12_IS_TILT_MASK) {
			iio_dev = hw->iio_devs[ST_LIS2DUXS12_ID_TILT];
			event = IIO_UNMOD_EVENT_CODE(STM_IIO_TILT, -1,
						     IIO_EV_TYPE_THRESH,
						     IIO_EV_DIR_RISING);
			iio_push_event(iio_dev, event,
				       iio_get_time_ns(iio_dev));
		}
	}

out:
	return st_lis2duxs12_event_handler(hw);
}

static int st_lis2duxs12_fifo_preenable(struct iio_dev *iio_dev)
{
	return st_lis2duxs12_update_fifo(iio_dev, true);
}

static int st_lis2duxs12_fifo_postdisable(struct iio_dev *iio_dev)
{
	return st_lis2duxs12_update_fifo(iio_dev, false);
}

static const struct iio_buffer_setup_ops st_lis2duxs12_fifo_ops = {
	.preenable = st_lis2duxs12_fifo_preenable,
	.postdisable = st_lis2duxs12_fifo_postdisable,
};

int st_lis2duxs12_buffers_setup(struct st_lis2duxs12_hw *hw)
{
	struct device_node *np = hw->dev->of_node;
	unsigned long irq_type;
	bool irq_active_low;
	int err;
	int i;

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

	/* configure interrupt pin level */
	if (irq_active_low) {
		err = regmap_update_bits(hw->regmap,
			ST_LIS2DUXS12_PIN_CTRL_ADDR,
			ST_LIS2DUXS12_H_LACTIVE_MASK,
			FIELD_PREP(ST_LIS2DUXS12_H_LACTIVE_MASK, 1));
		if (err < 0)
			return err;
	}

	if (np && of_property_read_bool(np, "drive-open-drain")) {
		err = regmap_update_bits(hw->regmap,
			       ST_LIS2DUXS12_PIN_CTRL_ADDR,
			       ST_LIS2DUXS12_PP_OD_MASK,
			       FIELD_PREP(ST_LIS2DUXS12_PP_OD_MASK, 1));
		if (err < 0)
			return err;

		irq_type |= IRQF_SHARED;
	}

	/* check pull down disable on int1 pin property */
	if (np && of_property_read_bool(np, "pd_dis_int1")) {
		err = regmap_update_bits(hw->regmap,
			ST_LIS2DUXS12_PIN_CTRL_ADDR,
			ST_LIS2DUXS12_PD_DIS_INT1_MASK,
			FIELD_PREP(ST_LIS2DUXS12_PD_DIS_INT1_MASK, 1));
		if (err < 0)
			return err;
	}

	if (hw->settings->st_qvar_support) {
		if (hw->int_pin == 1) {
			/*
			 * route on RES pin the interrupt pin configured when
			 * qvar supported
			 */
			err = regmap_update_bits(hw->regmap,
					 ST_LIS2DUXS12_CTRL1_ADDR,
					 ST_LIS2DUXS12_INT1_ON_RES_MASK,
					 FIELD_PREP(ST_LIS2DUXS12_INT1_ON_RES_MASK, 1));
			if (err < 0)
				return err;
		} else  {
			dev_err(hw->dev,
				"if qvar enabled only irq pin 1 can be used\n");

			return err;
		}
	} else {
		/*
		 * check pull down disable on int2 pin property (not
		 * supported when qvar enabled)
		 */
		if (np && of_property_read_bool(np, "pd_dis_int2")) {
			err = regmap_update_bits(hw->regmap,
				ST_LIS2DUXS12_PIN_CTRL_ADDR,
				ST_LIS2DUXS12_PD_DIS_INT2_MASK,
				FIELD_PREP(ST_LIS2DUXS12_PD_DIS_INT2_MASK, 1));
			if (err < 0)
				return err;
		}
	}

	err = devm_request_threaded_irq(hw->dev, hw->irq,
					st_lis2duxs12_handler_irq,
					st_lis2duxs12_handler_thread,
					irq_type | IRQF_ONESHOT,
					ST_LIS2DUXS12_DEV_NAME, hw);
	if (err) {
		dev_err(hw->dev,
			"failed to request trigger irq %d\n",
			hw->irq);

		return err;
	}

	/* allocate buffer for all buffered sensor type */
	for (i = 0; i < ARRAY_SIZE(st_lis2duxs12_buffered_sensor_list); i++) {
		enum st_lis2duxs12_sensor_id id =
					  st_lis2duxs12_buffered_sensor_list[i];

#if KERNEL_VERSION(5, 13, 0) > LINUX_VERSION_CODE
		struct iio_buffer *buffer;
#endif /* LINUX_VERSION_CODE */

		if (!hw->iio_devs[id])
			continue;

#if KERNEL_VERSION(5, 19, 0) <= LINUX_VERSION_CODE
	err = devm_iio_kfifo_buffer_setup(hw->dev, hw->iio_devs[id],
					  &st_lis2duxs12_fifo_ops);
	if (err)
		return err;
#elif KERNEL_VERSION(5, 13, 0) <= LINUX_VERSION_CODE
		err = devm_iio_kfifo_buffer_setup(hw->dev, hw->iio_devs[id],
						  INDIO_BUFFER_SOFTWARE,
						  &st_lis2duxs12_fifo_ops);
		if (err)
			return err;
#else /* LINUX_VERSION_CODE */
		buffer = devm_iio_kfifo_allocate(hw->dev);
		if (!buffer)
			return -ENOMEM;

		iio_device_attach_buffer(hw->iio_devs[id], buffer);
		hw->iio_devs[id]->modes |= INDIO_BUFFER_SOFTWARE;
		hw->iio_devs[id]->setup_ops = &st_lis2duxs12_fifo_ops;
#endif /* LINUX_VERSION_CODE */
	}

	if (hw->timestamp) {
		err = regmap_update_bits(hw->regmap,
			      ST_LIS2DUXS12_FIFO_BATCH_DEC_ADDR,
			      ST_LIS2DUXS12_DEC_TS_MASK,
			      FIELD_PREP(ST_LIS2DUXS12_DEC_TS_MASK, 1));
		if (err < 0)
			return err;
	}

	return 0;
}
