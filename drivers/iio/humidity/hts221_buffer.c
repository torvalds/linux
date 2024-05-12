// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics hts221 sensor driver
 *
 * Copyright 2016 STMicroelectronics Inc.
 *
 * Lorenzo Bianconi <lorenzo.bianconi@st.com>
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/irqreturn.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/bitfield.h>

#include <linux/iio/iio.h>
#include <linux/iio/trigger.h>
#include <linux/iio/events.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/iio/buffer.h>

#include <linux/platform_data/st_sensors_pdata.h>

#include "hts221.h"

#define HTS221_REG_DRDY_HL_ADDR		0x22
#define HTS221_REG_DRDY_HL_MASK		BIT(7)
#define HTS221_REG_DRDY_PP_OD_ADDR	0x22
#define HTS221_REG_DRDY_PP_OD_MASK	BIT(6)
#define HTS221_REG_DRDY_EN_ADDR		0x22
#define HTS221_REG_DRDY_EN_MASK		BIT(2)
#define HTS221_REG_STATUS_ADDR		0x27
#define HTS221_RH_DRDY_MASK		BIT(1)
#define HTS221_TEMP_DRDY_MASK		BIT(0)

static int hts221_trig_set_state(struct iio_trigger *trig, bool state)
{
	struct iio_dev *iio_dev = iio_trigger_get_drvdata(trig);
	struct hts221_hw *hw = iio_priv(iio_dev);

	return regmap_update_bits(hw->regmap, HTS221_REG_DRDY_EN_ADDR,
				  HTS221_REG_DRDY_EN_MASK,
				  FIELD_PREP(HTS221_REG_DRDY_EN_MASK, state));
}

static const struct iio_trigger_ops hts221_trigger_ops = {
	.set_trigger_state = hts221_trig_set_state,
};

static irqreturn_t hts221_trigger_handler_thread(int irq, void *private)
{
	struct hts221_hw *hw = private;
	int err, status;

	err = regmap_read(hw->regmap, HTS221_REG_STATUS_ADDR, &status);
	if (err < 0)
		return IRQ_HANDLED;

	/*
	 * H_DA bit (humidity data available) is routed to DRDY line.
	 * Humidity sample is computed after temperature one.
	 * Here we can assume data channels are both available if H_DA bit
	 * is set in status register
	 */
	if (!(status & HTS221_RH_DRDY_MASK))
		return IRQ_NONE;

	iio_trigger_poll_nested(hw->trig);

	return IRQ_HANDLED;
}

int hts221_allocate_trigger(struct iio_dev *iio_dev)
{
	struct hts221_hw *hw = iio_priv(iio_dev);
	struct st_sensors_platform_data *pdata = dev_get_platdata(hw->dev);
	bool irq_active_low = false, open_drain = false;
	unsigned long irq_type;
	int err;

	irq_type = irqd_get_trigger_type(irq_get_irq_data(hw->irq));

	switch (irq_type) {
	case IRQF_TRIGGER_HIGH:
	case IRQF_TRIGGER_RISING:
		break;
	case IRQF_TRIGGER_LOW:
	case IRQF_TRIGGER_FALLING:
		irq_active_low = true;
		break;
	default:
		dev_info(hw->dev,
			 "mode %lx unsupported, using IRQF_TRIGGER_RISING\n",
			 irq_type);
		irq_type = IRQF_TRIGGER_RISING;
		break;
	}

	err = regmap_update_bits(hw->regmap, HTS221_REG_DRDY_HL_ADDR,
				 HTS221_REG_DRDY_HL_MASK,
				 FIELD_PREP(HTS221_REG_DRDY_HL_MASK,
					    irq_active_low));
	if (err < 0)
		return err;

	if (device_property_read_bool(hw->dev, "drive-open-drain") ||
	    (pdata && pdata->open_drain)) {
		irq_type |= IRQF_SHARED;
		open_drain = true;
	}

	err = regmap_update_bits(hw->regmap, HTS221_REG_DRDY_PP_OD_ADDR,
				 HTS221_REG_DRDY_PP_OD_MASK,
				 FIELD_PREP(HTS221_REG_DRDY_PP_OD_MASK,
					    open_drain));
	if (err < 0)
		return err;

	err = devm_request_threaded_irq(hw->dev, hw->irq, NULL,
					hts221_trigger_handler_thread,
					irq_type | IRQF_ONESHOT,
					hw->name, hw);
	if (err) {
		dev_err(hw->dev, "failed to request trigger irq %d\n",
			hw->irq);
		return err;
	}

	hw->trig = devm_iio_trigger_alloc(hw->dev, "%s-trigger",
					  iio_dev->name);
	if (!hw->trig)
		return -ENOMEM;

	iio_trigger_set_drvdata(hw->trig, iio_dev);
	hw->trig->ops = &hts221_trigger_ops;

	err = devm_iio_trigger_register(hw->dev, hw->trig);

	iio_dev->trig = iio_trigger_get(hw->trig);

	return err;
}

static int hts221_buffer_preenable(struct iio_dev *iio_dev)
{
	return hts221_set_enable(iio_priv(iio_dev), true);
}

static int hts221_buffer_postdisable(struct iio_dev *iio_dev)
{
	return hts221_set_enable(iio_priv(iio_dev), false);
}

static const struct iio_buffer_setup_ops hts221_buffer_ops = {
	.preenable = hts221_buffer_preenable,
	.postdisable = hts221_buffer_postdisable,
};

static irqreturn_t hts221_buffer_handler_thread(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *iio_dev = pf->indio_dev;
	struct hts221_hw *hw = iio_priv(iio_dev);
	struct iio_chan_spec const *ch;
	int err;

	/* humidity data */
	ch = &iio_dev->channels[HTS221_SENSOR_H];
	err = regmap_bulk_read(hw->regmap, ch->address,
			       &hw->scan.channels[0],
			       sizeof(hw->scan.channels[0]));
	if (err < 0)
		goto out;

	/* temperature data */
	ch = &iio_dev->channels[HTS221_SENSOR_T];
	err = regmap_bulk_read(hw->regmap, ch->address,
			       &hw->scan.channels[1],
			       sizeof(hw->scan.channels[1]));
	if (err < 0)
		goto out;

	iio_push_to_buffers_with_timestamp(iio_dev, &hw->scan,
					   iio_get_time_ns(iio_dev));

out:
	iio_trigger_notify_done(hw->trig);

	return IRQ_HANDLED;
}

int hts221_allocate_buffers(struct iio_dev *iio_dev)
{
	struct hts221_hw *hw = iio_priv(iio_dev);
	return devm_iio_triggered_buffer_setup(hw->dev, iio_dev,
					NULL, hts221_buffer_handler_thread,
					&hts221_buffer_ops);
}

MODULE_AUTHOR("Lorenzo Bianconi <lorenzo.bianconi@st.com>");
MODULE_DESCRIPTION("STMicroelectronics hts221 buffer driver");
MODULE_LICENSE("GPL v2");
