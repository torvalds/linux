// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics st_h3lis331dl trigger buffer driver
 *
 * MEMS Software Solutions Team
 *
 * Copyright 2023 STMicroelectronics Inc.
 */

#include <asm/unaligned.h>
#include <linux/interrupt.h>
#include <linux/iio/buffer.h>
#include <linux/iio/buffer.h>
#include <linux/iio/events.h>
#include <linux/iio/iio.h>
#include <linux/iio/kfifo_buf.h>
#include <linux/iio/sw_trigger.h>
#include <linux/iio/trigger.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/platform_data/st_sensors_pdata.h>
#include <linux/property.h>
#include <linux/version.h>

#include "st_h3lis331dl.h"

static int st_h3lis331dl_get_int_reg(struct st_h3lis331dl_hw *hw)
{
	int err, int_pin;

	if (!dev_fwnode(hw->dev))
		return -EINVAL;

	err = device_property_read_u32(hw->dev, "st,drdy-int-pin", &int_pin);
	if (err < 0) {
		struct st_sensors_platform_data *pdata;
		struct device *dev = hw->dev;

		pdata = (struct st_sensors_platform_data *)dev->platform_data;
		int_pin = pdata ? pdata->drdy_int_pin : 1;
	}

	hw->int_pin = int_pin;

	return err;
}

static int st_h3lis331dl_buffer_enable(struct iio_dev *iio_dev, bool enable)
{
	struct st_h3lis331dl_sensor *sensor = iio_priv(iio_dev);

	return st_h3lis331dl_sensor_set_enable(sensor, enable);
}

static irqreturn_t st_h3lis331dl_handler_irq(int irq, void *private)
{
	struct st_h3lis331dl_hw *hw = (struct st_h3lis331dl_hw *)private;

	hw->ts = st_h3lis331dl_get_time_ns(hw->iio_devs);

	return IRQ_WAKE_THREAD;
}

static irqreturn_t st_h3lis331dl_handler_thread(int irq, void *private)
{
	struct st_h3lis331dl_hw *hw = (struct st_h3lis331dl_hw *)private;
	unsigned int status;
	int err;

	err = regmap_read(hw->regmap, ST_H3LIS331DL_STATUS_REG_ADDR, &status);
	if (err < 0)
		return IRQ_HANDLED;

	if (status & ST_H3LIS331DL_ZYXDA_MASK) {
		struct st_h3lis331dl_sensor *sensor;

		sensor = iio_priv(hw->iio_devs);
		iio_trigger_poll_chained(sensor->trig);

		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

static int st_h3lis331dl_fifo_preenable(struct iio_dev *iio_dev)
{
	return st_h3lis331dl_buffer_enable(iio_dev, true);
}

static int st_h3lis331dl_fifo_postdisable(struct iio_dev *iio_dev)
{
	return st_h3lis331dl_buffer_enable(iio_dev, false);
}

static const struct iio_buffer_setup_ops st_h3lis331dl_buffer_setup_ops = {
	.preenable = st_h3lis331dl_fifo_preenable,
	.postdisable = st_h3lis331dl_fifo_postdisable,
};

static irqreturn_t st_h3lis331dl_buffer_pollfunc(int irq, void *private)
{
	u8 iio_buf[ALIGN(ST_H3LIS331DL_SAMPLE_SIZE, sizeof(s64)) + sizeof(s64) + sizeof(s64)];
	struct iio_poll_func *pf = private;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct st_h3lis331dl_sensor *sensor = iio_priv(indio_dev);
	struct st_h3lis331dl_hw *hw = sensor->hw;
	int addr = indio_dev->channels[0].address;
	int err;

	err = regmap_bulk_read(hw->regmap, ST_H3LIS331DL_AUTO_INCREMENT(addr),
			       &iio_buf, ST_H3LIS331DL_SAMPLE_SIZE);
	if (err < 0)
		goto out;

	if (indio_dev->trig)
		iio_push_to_buffers_with_timestamp(indio_dev, iio_buf,
					  st_h3lis331dl_get_time_ns(indio_dev));
	else
		iio_push_to_buffers_with_timestamp(indio_dev, iio_buf, hw->ts);

out:
	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

int st_h3lis331dl_allocate_triggered_buffer(struct st_h3lis331dl_hw *hw)
{
	return devm_iio_triggered_buffer_setup(hw->dev,
					       hw->iio_devs, NULL,
					       st_h3lis331dl_buffer_pollfunc,
					       &st_h3lis331dl_buffer_setup_ops);
}

static int st_h3lis331dl_config_interrupt(struct st_h3lis331dl_hw *hw,
					  bool enable)
{
	u8 cfg_mask;
	u8 cfg_cfg;

	switch (hw->int_pin) {
	case 1:
		cfg_mask = ST_H3LIS331DL_I1CFG_MASK;
		break;
	case 2:
		cfg_mask = ST_H3LIS331DL_I2CFG_MASK;
		break;
	default:
		return -EINVAL;
	}

	cfg_cfg = enable ? ST_H3LIS331DL_CFG_DRDY_VAL : 0;
	cfg_cfg = ST_H3LIS331DL_SHIFT_VAL(cfg_cfg, cfg_mask);

	return regmap_update_bits(hw->regmap, ST_H3LIS331DL_CTRL_REG3_ADDR,
				  cfg_mask, cfg_cfg);
}

static int st_h3lis331dl_trig_set_state(struct iio_trigger *trig, bool state)
{
	struct st_h3lis331dl_hw *hw = iio_trigger_get_drvdata(trig);

	dev_dbg(hw->dev, "trigger set %d\n", state);

	return st_h3lis331dl_config_interrupt(hw, state);
}

static const struct iio_trigger_ops st_h3lis331dl_trigger_ops = {
	.set_trigger_state = st_h3lis331dl_trig_set_state,
};

int st_h3lis331dl_trigger_setup(struct st_h3lis331dl_hw *hw)
{
	struct st_h3lis331dl_sensor *sensor;
	unsigned long irq_type;
	bool irq_active_low;
	int err;

	err = st_h3lis331dl_get_int_reg(hw);
	if (err < 0)
		return err;

	irq_type = irqd_get_trigger_type(irq_get_irq_data(hw->irq));
	switch (irq_type) {
	case IRQF_TRIGGER_HIGH:
	case IRQF_TRIGGER_NONE:
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
				 ST_H3LIS331DL_CTRL_REG3_ADDR,
				 ST_H3LIS331DL_IHL_MASK,
				 FIELD_PREP(ST_H3LIS331DL_IHL_MASK,
					    irq_active_low));
	if (err < 0)
		return err;

	if (device_property_read_bool(hw->dev, "drive-open-drain")) {
		err = regmap_update_bits(hw->regmap,
					 ST_H3LIS331DL_CTRL_REG3_ADDR,
					 ST_H3LIS331DL_PP_OD_MASK,
					 FIELD_PREP(ST_H3LIS331DL_PP_OD_MASK,
						    1));
		if (err < 0)
			return err;

		irq_type |= IRQF_SHARED;
	}

	err = devm_request_threaded_irq(hw->dev, hw->irq,
					st_h3lis331dl_handler_irq,
					st_h3lis331dl_handler_thread,
					irq_type | IRQF_ONESHOT,
					ST_H3LIS331DL_DEV_NAME, hw);
	if (err) {
		dev_err(hw->dev, "failed to request trigger irq %d\n",
			hw->irq);
		return err;
	}

	/* attach trigger to iio devs */
	sensor = iio_priv(hw->iio_devs);
	sensor->trig = devm_iio_trigger_alloc(hw->dev, "st_%s-trigger",
					      hw->iio_devs->name);
	if (!sensor->trig) {
		dev_err(hw->dev, "failed to allocate iio trigger.\n");

		return -ENOMEM;
	}

	iio_trigger_set_drvdata(sensor->trig, hw);
	sensor->trig->ops = &st_h3lis331dl_trigger_ops;
	sensor->trig->dev.parent = hw->dev;

	err = devm_iio_trigger_register(hw->dev, sensor->trig);
	if (err < 0) {
		dev_err(hw->dev, "failed to register iio trigger.\n");

		return err;
	}

	hw->iio_devs->trig = iio_trigger_get(sensor->trig);

	return 0;
}
