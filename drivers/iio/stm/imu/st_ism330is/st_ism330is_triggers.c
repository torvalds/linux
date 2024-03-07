// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics st_ism330is trigger buffer library driver
 *
 * MEMS Software Solutions Team
 *
 * Copyright 2023 STMicroelectronics Inc.
 */

#include <asm/unaligned.h>
#include <linux/iio/iio.h>
#include <linux/iio/buffer.h>
#include <linux/iio/sw_trigger.h>
#include <linux/iio/trigger.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/module.h>
#include <linux/version.h>

#include "st_ism330is.h"

#define ST_ISM330IS_AG_SAMPLE_SIZE	6
#define ST_ISM330IS_PT_SAMPLE_SIZE	2

static int st_ism330is_buffer_enable(struct iio_dev *iio_dev, bool enable)
{
	struct st_ism330is_sensor *sensor = iio_priv(iio_dev);

	if (sensor->id == ST_ISM330IS_ID_EXT0 ||
	    sensor->id == ST_ISM330IS_ID_EXT1)
		return st_ism330is_shub_set_enable(sensor, enable);

	return st_ism330is_sensor_set_enable(sensor, enable);
}

static int st_ism330is_fifo_preenable(struct iio_dev *iio_dev)
{
	return st_ism330is_buffer_enable(iio_dev, true);
}

static int st_ism330is_fifo_postdisable(struct iio_dev *iio_dev)
{
	return st_ism330is_buffer_enable(iio_dev, false);
}

static const struct iio_buffer_setup_ops st_ism330is_buffer_setup_ops = {
	.preenable = st_ism330is_fifo_preenable,

#if KERNEL_VERSION(5, 10, 0) > LINUX_VERSION_CODE
	.postenable = iio_triggered_buffer_postenable,
	.predisable = iio_triggered_buffer_predisable,
#endif /* LINUX_VERSION_CODE */

	.postdisable = st_ism330is_fifo_postdisable,
};

static irqreturn_t st_ism330is_buffer_pollfunc(int irq, void *private)
{
	u8 iio_buf[ALIGN(ST_ISM330IS_AG_SAMPLE_SIZE, sizeof(s64)) +
		   sizeof(s64) + sizeof(s64)];
	struct iio_poll_func *pf = private;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct st_ism330is_sensor *sensor = iio_priv(indio_dev);
	struct st_ism330is_hw *hw = sensor->hw;
	int addr = indio_dev->channels[0].address;

	switch (indio_dev->channels[0].type) {
	case IIO_ACCEL:
	case IIO_ANGL_VEL:
		st_ism330is_read_locked(hw, addr, &iio_buf,
					  ST_ISM330IS_AG_SAMPLE_SIZE);
		break;
	case IIO_TEMP:
		st_ism330is_read_locked(hw, addr, &iio_buf,
					  ST_ISM330IS_PT_SAMPLE_SIZE);
		break;
	case IIO_PRESSURE:
		st_ism330is_shub_read(sensor, addr, (u8 *)&iio_buf,
					ST_ISM330IS_PT_SAMPLE_SIZE);
		break;
	case IIO_MAGN:
		st_ism330is_shub_read(sensor, addr, (u8 *)&iio_buf,
					ST_ISM330IS_AG_SAMPLE_SIZE);
		break;
	default:
		return -EINVAL;
	}

	iio_push_to_buffers_with_timestamp(indio_dev, iio_buf,
					   iio_get_time_ns(indio_dev));
	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

static int st_ism330is_trig_set_state(struct iio_trigger *trig, bool state)
{
	struct st_ism330is_hw *hw = iio_trigger_get_drvdata(trig);

	dev_dbg(hw->dev, "trigger set %d\n", state);

	return 0;
}

static const struct iio_trigger_ops st_ism330is_trigger_ops = {
	.set_trigger_state = st_ism330is_trig_set_state,
};

int st_ism330is_allocate_buffers(struct st_ism330is_hw *hw)
{
	int i;

	for (i = 0;
	     i < ARRAY_SIZE(st_ism330is_triggered_main_sensor_list);
	     i++) {
		enum st_ism330is_sensor_id id;
		int err;

		id = st_ism330is_triggered_main_sensor_list[i];
		if (!hw->iio_devs[id])
			continue;

		err = devm_iio_triggered_buffer_setup(hw->dev,
						 hw->iio_devs[id], NULL,
						 st_ism330is_buffer_pollfunc,
						 &st_ism330is_buffer_setup_ops);
		if (err)
			return err;
	}

	return 0;
}
