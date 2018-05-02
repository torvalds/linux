/*
 * STMicroelectronics uvis25 sensor driver
 *
 * Copyright 2017 STMicroelectronics Inc.
 *
 * Lorenzo Bianconi <lorenzo.bianconi83@gmail.com>
 *
 * Licensed under the GPL-2.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/iio/sysfs.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/interrupt.h>
#include <linux/irqreturn.h>
#include <linux/iio/trigger.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/iio/buffer.h>
#include <linux/regmap.h>

#include "st_uvis25.h"

#define ST_UVIS25_REG_WHOAMI_ADDR	0x0f
#define ST_UVIS25_REG_WHOAMI_VAL	0xca
#define ST_UVIS25_REG_CTRL1_ADDR	0x20
#define ST_UVIS25_REG_ODR_MASK		BIT(0)
#define ST_UVIS25_REG_BDU_MASK		BIT(1)
#define ST_UVIS25_REG_CTRL2_ADDR	0x21
#define ST_UVIS25_REG_BOOT_MASK		BIT(7)
#define ST_UVIS25_REG_CTRL3_ADDR	0x22
#define ST_UVIS25_REG_HL_MASK		BIT(7)
#define ST_UVIS25_REG_STATUS_ADDR	0x27
#define ST_UVIS25_REG_UV_DA_MASK	BIT(0)
#define ST_UVIS25_REG_OUT_ADDR		0x28

static const struct iio_chan_spec st_uvis25_channels[] = {
	{
		.type = IIO_UVINDEX,
		.address = ST_UVIS25_REG_OUT_ADDR,
		.info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED),
		.scan_index = 0,
		.scan_type = {
			.sign = 'u',
			.realbits = 8,
			.storagebits = 8,
		},
	},
	IIO_CHAN_SOFT_TIMESTAMP(1),
};

static int st_uvis25_check_whoami(struct st_uvis25_hw *hw)
{
	int err, data;

	err = regmap_read(hw->regmap, ST_UVIS25_REG_WHOAMI_ADDR, &data);
	if (err < 0) {
		dev_err(regmap_get_device(hw->regmap),
			"failed to read whoami register\n");
		return err;
	}

	if (data != ST_UVIS25_REG_WHOAMI_VAL) {
		dev_err(regmap_get_device(hw->regmap),
			"wrong whoami {%02x vs %02x}\n",
			data, ST_UVIS25_REG_WHOAMI_VAL);
		return -ENODEV;
	}

	return 0;
}

static int st_uvis25_set_enable(struct st_uvis25_hw *hw, bool enable)
{
	int err;

	err = regmap_update_bits(hw->regmap, ST_UVIS25_REG_CTRL1_ADDR,
				 ST_UVIS25_REG_ODR_MASK, enable);
	if (err < 0)
		return err;

	hw->enabled = enable;

	return 0;
}

static int st_uvis25_read_oneshot(struct st_uvis25_hw *hw, u8 addr, int *val)
{
	int err;

	err = st_uvis25_set_enable(hw, true);
	if (err < 0)
		return err;

	msleep(1500);

	/*
	 * in order to avoid possible race conditions with interrupt
	 * generation, disable the sensor first and then poll output
	 * register. That sequence guarantees the interrupt will be reset
	 * when irq line is unmasked
	 */
	err = st_uvis25_set_enable(hw, false);
	if (err < 0)
		return err;

	err = regmap_read(hw->regmap, addr, val);

	return err < 0 ? err : IIO_VAL_INT;
}

static int st_uvis25_read_raw(struct iio_dev *iio_dev,
			      struct iio_chan_spec const *ch,
			      int *val, int *val2, long mask)
{
	int ret;

	ret = iio_device_claim_direct_mode(iio_dev);
	if (ret)
		return ret;

	switch (mask) {
	case IIO_CHAN_INFO_PROCESSED: {
		struct st_uvis25_hw *hw = iio_priv(iio_dev);

		/*
		 * mask irq line during oneshot read since the sensor
		 * does not export the capability to disable data-ready line
		 * in the register map and it is enabled by default.
		 * If the line is unmasked during read_raw() it will be set
		 * active and never reset since the trigger is disabled
		 */
		if (hw->irq > 0)
			disable_irq(hw->irq);
		ret = st_uvis25_read_oneshot(hw, ch->address, val);
		if (hw->irq > 0)
			enable_irq(hw->irq);
		break;
	}
	default:
		ret = -EINVAL;
		break;
	}

	iio_device_release_direct_mode(iio_dev);

	return ret;
}

static irqreturn_t st_uvis25_trigger_handler_thread(int irq, void *private)
{
	struct st_uvis25_hw *hw = private;
	int err, status;

	err = regmap_read(hw->regmap, ST_UVIS25_REG_STATUS_ADDR, &status);
	if (err < 0)
		return IRQ_HANDLED;

	if (!(status & ST_UVIS25_REG_UV_DA_MASK))
		return IRQ_NONE;

	iio_trigger_poll_chained(hw->trig);

	return IRQ_HANDLED;
}

static int st_uvis25_allocate_trigger(struct iio_dev *iio_dev)
{
	struct st_uvis25_hw *hw = iio_priv(iio_dev);
	struct device *dev = regmap_get_device(hw->regmap);
	bool irq_active_low = false;
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
		dev_info(dev, "mode %lx unsupported\n", irq_type);
		return -EINVAL;
	}

	err = regmap_update_bits(hw->regmap, ST_UVIS25_REG_CTRL3_ADDR,
				 ST_UVIS25_REG_HL_MASK, irq_active_low);
	if (err < 0)
		return err;

	err = devm_request_threaded_irq(dev, hw->irq, NULL,
					st_uvis25_trigger_handler_thread,
					irq_type | IRQF_ONESHOT,
					iio_dev->name, hw);
	if (err) {
		dev_err(dev, "failed to request trigger irq %d\n",
			hw->irq);
		return err;
	}

	hw->trig = devm_iio_trigger_alloc(dev, "%s-trigger",
					  iio_dev->name);
	if (!hw->trig)
		return -ENOMEM;

	iio_trigger_set_drvdata(hw->trig, iio_dev);
	hw->trig->dev.parent = dev;

	return devm_iio_trigger_register(dev, hw->trig);
}

static int st_uvis25_buffer_preenable(struct iio_dev *iio_dev)
{
	return st_uvis25_set_enable(iio_priv(iio_dev), true);
}

static int st_uvis25_buffer_postdisable(struct iio_dev *iio_dev)
{
	return st_uvis25_set_enable(iio_priv(iio_dev), false);
}

static const struct iio_buffer_setup_ops st_uvis25_buffer_ops = {
	.preenable = st_uvis25_buffer_preenable,
	.postenable = iio_triggered_buffer_postenable,
	.predisable = iio_triggered_buffer_predisable,
	.postdisable = st_uvis25_buffer_postdisable,
};

static irqreturn_t st_uvis25_buffer_handler_thread(int irq, void *p)
{
	u8 buffer[ALIGN(sizeof(u8), sizeof(s64)) + sizeof(s64)];
	struct iio_poll_func *pf = p;
	struct iio_dev *iio_dev = pf->indio_dev;
	struct st_uvis25_hw *hw = iio_priv(iio_dev);
	int err;

	err = regmap_read(hw->regmap, ST_UVIS25_REG_OUT_ADDR, (int *)buffer);
	if (err < 0)
		goto out;

	iio_push_to_buffers_with_timestamp(iio_dev, buffer,
					   iio_get_time_ns(iio_dev));

out:
	iio_trigger_notify_done(hw->trig);

	return IRQ_HANDLED;
}

static int st_uvis25_allocate_buffer(struct iio_dev *iio_dev)
{
	struct st_uvis25_hw *hw = iio_priv(iio_dev);

	return devm_iio_triggered_buffer_setup(regmap_get_device(hw->regmap),
					       iio_dev, NULL,
					       st_uvis25_buffer_handler_thread,
					       &st_uvis25_buffer_ops);
}

static const struct iio_info st_uvis25_info = {
	.read_raw = st_uvis25_read_raw,
};

static int st_uvis25_init_sensor(struct st_uvis25_hw *hw)
{
	int err;

	err = regmap_update_bits(hw->regmap, ST_UVIS25_REG_CTRL2_ADDR,
				 ST_UVIS25_REG_BOOT_MASK, 1);
	if (err < 0)
		return err;

	msleep(2000);

	return regmap_update_bits(hw->regmap, ST_UVIS25_REG_CTRL1_ADDR,
				  ST_UVIS25_REG_BDU_MASK, 1);
}

int st_uvis25_probe(struct device *dev, int irq, struct regmap *regmap)
{
	struct st_uvis25_hw *hw;
	struct iio_dev *iio_dev;
	int err;

	iio_dev = devm_iio_device_alloc(dev, sizeof(*hw));
	if (!iio_dev)
		return -ENOMEM;

	dev_set_drvdata(dev, (void *)iio_dev);

	hw = iio_priv(iio_dev);
	hw->irq = irq;
	hw->regmap = regmap;

	err = st_uvis25_check_whoami(hw);
	if (err < 0)
		return err;

	iio_dev->modes = INDIO_DIRECT_MODE;
	iio_dev->dev.parent = dev;
	iio_dev->channels = st_uvis25_channels;
	iio_dev->num_channels = ARRAY_SIZE(st_uvis25_channels);
	iio_dev->name = ST_UVIS25_DEV_NAME;
	iio_dev->info = &st_uvis25_info;

	err = st_uvis25_init_sensor(hw);
	if (err < 0)
		return err;

	if (hw->irq > 0) {
		err = st_uvis25_allocate_buffer(iio_dev);
		if (err < 0)
			return err;

		err = st_uvis25_allocate_trigger(iio_dev);
		if (err)
			return err;
	}

	return devm_iio_device_register(dev, iio_dev);
}
EXPORT_SYMBOL(st_uvis25_probe);

static int __maybe_unused st_uvis25_suspend(struct device *dev)
{
	struct iio_dev *iio_dev = dev_get_drvdata(dev);
	struct st_uvis25_hw *hw = iio_priv(iio_dev);

	return regmap_update_bits(hw->regmap, ST_UVIS25_REG_CTRL1_ADDR,
				  ST_UVIS25_REG_ODR_MASK, 0);
}

static int __maybe_unused st_uvis25_resume(struct device *dev)
{
	struct iio_dev *iio_dev = dev_get_drvdata(dev);
	struct st_uvis25_hw *hw = iio_priv(iio_dev);

	if (hw->enabled)
		return regmap_update_bits(hw->regmap, ST_UVIS25_REG_CTRL1_ADDR,
					  ST_UVIS25_REG_ODR_MASK, 1);

	return 0;
}

const struct dev_pm_ops st_uvis25_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(st_uvis25_suspend, st_uvis25_resume)
};
EXPORT_SYMBOL(st_uvis25_pm_ops);

MODULE_AUTHOR("Lorenzo Bianconi <lorenzo.bianconi83@gmail.com>");
MODULE_DESCRIPTION("STMicroelectronics uvis25 sensor driver");
MODULE_LICENSE("GPL v2");
