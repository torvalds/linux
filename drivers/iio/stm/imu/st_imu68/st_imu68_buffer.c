// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics st_imu68 buffer library driver
 *
 * MEMS Software Solutions Team
 *
 * Copyright 2016 STMicroelectronics Inc.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/iio/iio.h>
#include <linux/iio/buffer.h>
#include <linux/iio/trigger.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/version.h>

#include "st_imu68.h"

#define ST_IMU68_REG_INT1_CTRL_ADDR		0x0c
#define ST_IMU68_REG_INT2_CTRL_ADDR		0x0d
#define ST_IMU68_REG_STATUS_ADDR		0x17

static int st_imu68_trig_set_state(struct iio_trigger *trig, bool state)
{
	struct iio_dev *iio_dev = iio_trigger_get_drvdata(trig);
	struct st_imu68_sensor *sensor = iio_priv(iio_dev);
	int err;

	err = st_imu68_write_with_mask(sensor->hw, ST_IMU68_REG_INT1_CTRL_ADDR,
				       sensor->drdy_mask, state);

	return err < 0 ? err : 0;
}

static const struct iio_trigger_ops st_imu68_trigger_ops = {
	.set_trigger_state = st_imu68_trig_set_state,
};

static irqreturn_t st_imu68_trigger_irq_handler(int irq, void *p)
{
	struct st_imu68_hw *hw = (struct st_imu68_hw *)p;

	hw->timestamp = iio_get_time_ns(hw->iio_devs[0]);

	return IRQ_WAKE_THREAD;
}

static irqreturn_t st_imu68_trigger_thread_handler(int irq, void *p)
{
	struct st_imu68_hw *hw = (struct st_imu68_hw *)p;
	struct st_imu68_sensor *sensor;
	int i, err, count = 0;
	u8 status;

	err = hw->tf->read(hw->dev, ST_IMU68_REG_STATUS_ADDR, sizeof(status),
			   &status);
	if (err < 0)
		return IRQ_HANDLED;

	for (i = 0; i < ST_IMU68_ID_MAX; i++) {
		sensor = iio_priv(hw->iio_devs[i]);

		if (status & sensor->status_mask) {
			iio_trigger_poll_chained(sensor->trigger);
			count++;
		}
	}

	return count > 0 ? IRQ_HANDLED : IRQ_NONE;
}

int st_imu68_allocate_triggers(struct st_imu68_hw *hw)
{
	struct st_imu68_sensor *sensor;
	int i, err;

	err = devm_request_threaded_irq(hw->dev, hw->irq,
					st_imu68_trigger_irq_handler,
					st_imu68_trigger_thread_handler,
					IRQF_TRIGGER_HIGH | IRQF_ONESHOT,
					hw->name, hw);
	if (err)
		return err;

	for (i = 0; i < ST_IMU68_ID_MAX; i++) {
		sensor = iio_priv(hw->iio_devs[i]);
		sensor->trigger = devm_iio_trigger_alloc(hw->dev, "%s-trigger",
							 hw->iio_devs[i]->name);
		if (!sensor->trigger) {
			dev_err(hw->dev, "failed to allocate iio trigger.\n");
			err = -ENOMEM;
			goto err;
		}
		iio_trigger_set_drvdata(sensor->trigger, hw->iio_devs[i]);
		sensor->trigger->ops = &st_imu68_trigger_ops;
		sensor->trigger->dev.parent = hw->dev;

		err = iio_trigger_register(sensor->trigger);
		if (err < 0) {
			dev_err(hw->dev, "failed to register iio trigger.\n");

			goto err;
		}
		hw->iio_devs[i]->trig = sensor->trigger;
	}

	return 0;

err:
	for (i--; i >= 0; i--) {
		sensor = iio_priv(hw->iio_devs[i]);
		iio_trigger_unregister(sensor->trigger);
	}

	return err;
}

void st_imu68_deallocate_triggers(struct st_imu68_hw *hw)
{
	struct st_imu68_sensor *sensor;
	int i;

	for (i = 0; i < ST_IMU68_ID_MAX; i++) {
		sensor = iio_priv(hw->iio_devs[i]);
		iio_trigger_unregister(sensor->trigger);
	}
}

static int st_imu68_buffer_preenable(struct iio_dev *iio_dev)
{
	return st_imu68_sensor_enable(iio_priv(iio_dev), true);
}

static int st_imu68_buffer_postdisable(struct iio_dev *iio_dev)
{
	return st_imu68_sensor_enable(iio_priv(iio_dev), false);
}

static const struct iio_buffer_setup_ops st_imu68_buffer_setup_ops = {
	.preenable = st_imu68_buffer_preenable,
#if LINUX_VERSION_CODE < KERNEL_VERSION(5,10,0)
	.postenable = iio_triggered_buffer_postenable,
	.predisable = iio_triggered_buffer_predisable,
#endif /* LINUX_VERSION_CODE */
	.postdisable = st_imu68_buffer_postdisable,
};

static irqreturn_t st_imu68_buffer_thread_handler(int irq, void *p)
{
	u8 buffer[ALIGN(ST_IMU68_OUT_LEN, sizeof(s64)) + sizeof(s64)];
	struct iio_poll_func *pf = p;
	struct iio_chan_spec const *ch = pf->indio_dev->channels;
	struct st_imu68_sensor *sensor = iio_priv(pf->indio_dev);
	struct st_imu68_hw *hw = sensor->hw;
	int err;

	err = hw->tf->read(hw->dev, ch->address, ST_IMU68_OUT_LEN, buffer);
	if (err < 0)
		goto out;

	iio_push_to_buffers_with_timestamp(pf->indio_dev, buffer,
					   hw->timestamp);

out:
	iio_trigger_notify_done(sensor->trigger);

	return IRQ_HANDLED;
}

int st_imu68_allocate_buffers(struct st_imu68_hw *hw)
{
	int err, i;

	for (i = 0; i < ST_IMU68_ID_MAX; i++) {
		err = iio_triggered_buffer_setup(hw->iio_devs[i], NULL,
						 st_imu68_buffer_thread_handler,
						 &st_imu68_buffer_setup_ops);
		if (err)
			goto err;
	}

	return 0;

err:
	for (i--; i >= 0; i--)
		iio_triggered_buffer_cleanup(hw->iio_devs[i]);

	return err;
}

void st_imu68_deallocate_buffers(struct st_imu68_hw *hw)
{
	int i;

	for (i = 0; i < ST_IMU68_ID_MAX; i++)
		iio_triggered_buffer_cleanup(hw->iio_devs[i]);
}
