// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * DA9150 GPADC Driver
 *
 * Copyright (c) 2014 Dialog Semiconductor
 *
 * Author: Adam Thomson <Adam.Thomson.Opensource@diasemi.com>
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <linux/completion.h>
#include <linux/iio/iio.h>
#include <linux/iio/machine.h>
#include <linux/iio/driver.h>
#include <linux/mfd/da9150/core.h>
#include <linux/mfd/da9150/registers.h>

/* Channels */
enum da9150_gpadc_hw_channel {
	DA9150_GPADC_HW_CHAN_GPIOA_2V = 0,
	DA9150_GPADC_HW_CHAN_GPIOA_2V_,
	DA9150_GPADC_HW_CHAN_GPIOB_2V,
	DA9150_GPADC_HW_CHAN_GPIOB_2V_,
	DA9150_GPADC_HW_CHAN_GPIOC_2V,
	DA9150_GPADC_HW_CHAN_GPIOC_2V_,
	DA9150_GPADC_HW_CHAN_GPIOD_2V,
	DA9150_GPADC_HW_CHAN_GPIOD_2V_,
	DA9150_GPADC_HW_CHAN_IBUS_SENSE,
	DA9150_GPADC_HW_CHAN_IBUS_SENSE_,
	DA9150_GPADC_HW_CHAN_VBUS_DIV,
	DA9150_GPADC_HW_CHAN_VBUS_DIV_,
	DA9150_GPADC_HW_CHAN_ID,
	DA9150_GPADC_HW_CHAN_ID_,
	DA9150_GPADC_HW_CHAN_VSYS,
	DA9150_GPADC_HW_CHAN_VSYS_,
	DA9150_GPADC_HW_CHAN_GPIOA_6V,
	DA9150_GPADC_HW_CHAN_GPIOA_6V_,
	DA9150_GPADC_HW_CHAN_GPIOB_6V,
	DA9150_GPADC_HW_CHAN_GPIOB_6V_,
	DA9150_GPADC_HW_CHAN_GPIOC_6V,
	DA9150_GPADC_HW_CHAN_GPIOC_6V_,
	DA9150_GPADC_HW_CHAN_GPIOD_6V,
	DA9150_GPADC_HW_CHAN_GPIOD_6V_,
	DA9150_GPADC_HW_CHAN_VBAT,
	DA9150_GPADC_HW_CHAN_VBAT_,
	DA9150_GPADC_HW_CHAN_TBAT,
	DA9150_GPADC_HW_CHAN_TBAT_,
	DA9150_GPADC_HW_CHAN_TJUNC_CORE,
	DA9150_GPADC_HW_CHAN_TJUNC_CORE_,
	DA9150_GPADC_HW_CHAN_TJUNC_OVP,
	DA9150_GPADC_HW_CHAN_TJUNC_OVP_,
};

enum da9150_gpadc_channel {
	DA9150_GPADC_CHAN_GPIOA = 0,
	DA9150_GPADC_CHAN_GPIOB,
	DA9150_GPADC_CHAN_GPIOC,
	DA9150_GPADC_CHAN_GPIOD,
	DA9150_GPADC_CHAN_IBUS,
	DA9150_GPADC_CHAN_VBUS,
	DA9150_GPADC_CHAN_VSYS,
	DA9150_GPADC_CHAN_VBAT,
	DA9150_GPADC_CHAN_TBAT,
	DA9150_GPADC_CHAN_TJUNC_CORE,
	DA9150_GPADC_CHAN_TJUNC_OVP,
};

/* Private data */
struct da9150_gpadc {
	struct da9150 *da9150;
	struct device *dev;

	struct mutex lock;
	struct completion complete;
};


static irqreturn_t da9150_gpadc_irq(int irq, void *data)
{

	struct da9150_gpadc *gpadc = data;

	complete(&gpadc->complete);

	return IRQ_HANDLED;
}

static int da9150_gpadc_read_adc(struct da9150_gpadc *gpadc, int hw_chan)
{
	u8 result_regs[2];
	int result;

	mutex_lock(&gpadc->lock);

	/* Set channel & enable measurement */
	da9150_reg_write(gpadc->da9150, DA9150_GPADC_MAN,
			 (DA9150_GPADC_EN_MASK |
			  hw_chan << DA9150_GPADC_MUX_SHIFT));

	/* Consume left-over completion from a previous timeout */
	try_wait_for_completion(&gpadc->complete);

	/* Check for actual completion */
	wait_for_completion_timeout(&gpadc->complete, msecs_to_jiffies(5));

	/* Read result and status from device */
	da9150_bulk_read(gpadc->da9150, DA9150_GPADC_RES_A, 2, result_regs);

	mutex_unlock(&gpadc->lock);

	/* Check to make sure device really has completed reading */
	if (result_regs[1] & DA9150_GPADC_RUN_MASK) {
		dev_err(gpadc->dev, "Timeout on channel %d of GPADC\n",
			hw_chan);
		return -ETIMEDOUT;
	}

	/* LSBs - 2 bits */
	result = (result_regs[1] & DA9150_GPADC_RES_L_MASK) >>
		 DA9150_GPADC_RES_L_SHIFT;
	/* MSBs - 8 bits */
	result |= result_regs[0] << DA9150_GPADC_RES_L_BITS;

	return result;
}

static inline int da9150_gpadc_gpio_6v_voltage_now(int raw_val)
{
	/* Convert to mV */
	return (6 * ((raw_val * 1000) + 500)) / 1024;
}

static inline int da9150_gpadc_ibus_current_avg(int raw_val)
{
	/* Convert to mA */
	return (4 * ((raw_val * 1000) + 500)) / 2048;
}

static inline int da9150_gpadc_vbus_21v_voltage_now(int raw_val)
{
	/* Convert to mV */
	return (21 * ((raw_val * 1000) + 500)) / 1024;
}

static inline int da9150_gpadc_vsys_6v_voltage_now(int raw_val)
{
	/* Convert to mV */
	return (3 * ((raw_val * 1000) + 500)) / 512;
}

static int da9150_gpadc_read_processed(struct da9150_gpadc *gpadc, int channel,
				       int hw_chan, int *val)
{
	int raw_val;

	raw_val = da9150_gpadc_read_adc(gpadc, hw_chan);
	if (raw_val < 0)
		return raw_val;

	switch (channel) {
	case DA9150_GPADC_CHAN_GPIOA:
	case DA9150_GPADC_CHAN_GPIOB:
	case DA9150_GPADC_CHAN_GPIOC:
	case DA9150_GPADC_CHAN_GPIOD:
		*val = da9150_gpadc_gpio_6v_voltage_now(raw_val);
		break;
	case DA9150_GPADC_CHAN_IBUS:
		*val = da9150_gpadc_ibus_current_avg(raw_val);
		break;
	case DA9150_GPADC_CHAN_VBUS:
		*val = da9150_gpadc_vbus_21v_voltage_now(raw_val);
		break;
	case DA9150_GPADC_CHAN_VSYS:
		*val = da9150_gpadc_vsys_6v_voltage_now(raw_val);
		break;
	default:
		/* No processing for other channels so return raw value */
		*val = raw_val;
		break;
	}

	return IIO_VAL_INT;
}

static int da9150_gpadc_read_scale(int channel, int *val, int *val2)
{
	switch (channel) {
	case DA9150_GPADC_CHAN_VBAT:
		*val = 2932;
		*val2 = 1000;
		return IIO_VAL_FRACTIONAL;
	case DA9150_GPADC_CHAN_TJUNC_CORE:
	case DA9150_GPADC_CHAN_TJUNC_OVP:
		*val = 1000000;
		*val2 = 4420;
		return IIO_VAL_FRACTIONAL;
	default:
		return -EINVAL;
	}
}

static int da9150_gpadc_read_offset(int channel, int *val)
{
	switch (channel) {
	case DA9150_GPADC_CHAN_VBAT:
		*val = 1500000 / 2932;
		return IIO_VAL_INT;
	case DA9150_GPADC_CHAN_TJUNC_CORE:
	case DA9150_GPADC_CHAN_TJUNC_OVP:
		*val = -144;
		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}
}

static int da9150_gpadc_read_raw(struct iio_dev *indio_dev,
				 struct iio_chan_spec const *chan,
				 int *val, int *val2, long mask)
{
	struct da9150_gpadc *gpadc = iio_priv(indio_dev);

	if ((chan->channel < DA9150_GPADC_CHAN_GPIOA) ||
	    (chan->channel > DA9150_GPADC_CHAN_TJUNC_OVP))
		return -EINVAL;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
	case IIO_CHAN_INFO_PROCESSED:
		return da9150_gpadc_read_processed(gpadc, chan->channel,
						   chan->address, val);
	case IIO_CHAN_INFO_SCALE:
		return da9150_gpadc_read_scale(chan->channel, val, val2);
	case IIO_CHAN_INFO_OFFSET:
		return da9150_gpadc_read_offset(chan->channel, val);
	default:
		return -EINVAL;
	}
}

static const struct iio_info da9150_gpadc_info = {
	.read_raw = &da9150_gpadc_read_raw,
};

#define DA9150_GPADC_CHANNEL(_id, _hw_id, _type, chan_info,	\
			     _ext_name) {			\
	.type = _type,						\
	.indexed = 1,						\
	.channel = DA9150_GPADC_CHAN_##_id,			\
	.address = DA9150_GPADC_HW_CHAN_##_hw_id,		\
	.info_mask_separate = chan_info,			\
	.extend_name = _ext_name,				\
	.datasheet_name = #_id,					\
}

#define DA9150_GPADC_CHANNEL_RAW(_id, _hw_id, _type, _ext_name)	\
	DA9150_GPADC_CHANNEL(_id, _hw_id, _type,		\
			     BIT(IIO_CHAN_INFO_RAW), _ext_name)

#define DA9150_GPADC_CHANNEL_SCALED(_id, _hw_id, _type, _ext_name)	\
	DA9150_GPADC_CHANNEL(_id, _hw_id, _type,			\
			     BIT(IIO_CHAN_INFO_RAW) |			\
			     BIT(IIO_CHAN_INFO_SCALE) |			\
			     BIT(IIO_CHAN_INFO_OFFSET),			\
			     _ext_name)

#define DA9150_GPADC_CHANNEL_PROCESSED(_id, _hw_id, _type, _ext_name)	\
	DA9150_GPADC_CHANNEL(_id, _hw_id, _type,			\
			     BIT(IIO_CHAN_INFO_PROCESSED), _ext_name)

/* Supported channels */
static const struct iio_chan_spec da9150_gpadc_channels[] = {
	DA9150_GPADC_CHANNEL_PROCESSED(GPIOA, GPIOA_6V, IIO_VOLTAGE, NULL),
	DA9150_GPADC_CHANNEL_PROCESSED(GPIOB, GPIOB_6V, IIO_VOLTAGE, NULL),
	DA9150_GPADC_CHANNEL_PROCESSED(GPIOC, GPIOC_6V, IIO_VOLTAGE, NULL),
	DA9150_GPADC_CHANNEL_PROCESSED(GPIOD, GPIOD_6V, IIO_VOLTAGE, NULL),
	DA9150_GPADC_CHANNEL_PROCESSED(IBUS, IBUS_SENSE, IIO_CURRENT, "ibus"),
	DA9150_GPADC_CHANNEL_PROCESSED(VBUS, VBUS_DIV_, IIO_VOLTAGE, "vbus"),
	DA9150_GPADC_CHANNEL_PROCESSED(VSYS, VSYS, IIO_VOLTAGE, "vsys"),
	DA9150_GPADC_CHANNEL_SCALED(VBAT, VBAT, IIO_VOLTAGE, "vbat"),
	DA9150_GPADC_CHANNEL_RAW(TBAT, TBAT, IIO_VOLTAGE, "tbat"),
	DA9150_GPADC_CHANNEL_SCALED(TJUNC_CORE, TJUNC_CORE, IIO_TEMP,
				    "tjunc_core"),
	DA9150_GPADC_CHANNEL_SCALED(TJUNC_OVP, TJUNC_OVP, IIO_TEMP,
				    "tjunc_ovp"),
};

/* Default maps used by da9150-charger */
static struct iio_map da9150_gpadc_default_maps[] = {
	{
		.consumer_dev_name = "da9150-charger",
		.consumer_channel = "CHAN_IBUS",
		.adc_channel_label = "IBUS",
	},
	{
		.consumer_dev_name = "da9150-charger",
		.consumer_channel = "CHAN_VBUS",
		.adc_channel_label = "VBUS",
	},
	{
		.consumer_dev_name = "da9150-charger",
		.consumer_channel = "CHAN_TJUNC",
		.adc_channel_label = "TJUNC_CORE",
	},
	{
		.consumer_dev_name = "da9150-charger",
		.consumer_channel = "CHAN_VBAT",
		.adc_channel_label = "VBAT",
	},
	{},
};

static int da9150_gpadc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct da9150 *da9150 = dev_get_drvdata(dev->parent);
	struct da9150_gpadc *gpadc;
	struct iio_dev *indio_dev;
	int irq, ret;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*gpadc));
	if (!indio_dev) {
		dev_err(&pdev->dev, "Failed to allocate IIO device\n");
		return -ENOMEM;
	}
	gpadc = iio_priv(indio_dev);

	platform_set_drvdata(pdev, indio_dev);
	gpadc->da9150 = da9150;
	gpadc->dev = dev;
	mutex_init(&gpadc->lock);
	init_completion(&gpadc->complete);

	irq = platform_get_irq_byname(pdev, "GPADC");
	if (irq < 0)
		return irq;

	ret = devm_request_threaded_irq(dev, irq, NULL, da9150_gpadc_irq,
					IRQF_ONESHOT, "GPADC", gpadc);
	if (ret) {
		dev_err(dev, "Failed to request IRQ %d: %d\n", irq, ret);
		return ret;
	}

	ret = iio_map_array_register(indio_dev, da9150_gpadc_default_maps);
	if (ret) {
		dev_err(dev, "Failed to register IIO maps: %d\n", ret);
		return ret;
	}

	indio_dev->name = dev_name(dev);
	indio_dev->info = &da9150_gpadc_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = da9150_gpadc_channels;
	indio_dev->num_channels = ARRAY_SIZE(da9150_gpadc_channels);

	ret = iio_device_register(indio_dev);
	if (ret) {
		dev_err(dev, "Failed to register IIO device: %d\n", ret);
		goto iio_map_unreg;
	}

	return 0;

iio_map_unreg:
	iio_map_array_unregister(indio_dev);

	return ret;
}

static int da9150_gpadc_remove(struct platform_device *pdev)
{
	struct iio_dev *indio_dev = platform_get_drvdata(pdev);

	iio_device_unregister(indio_dev);
	iio_map_array_unregister(indio_dev);

	return 0;
}

static struct platform_driver da9150_gpadc_driver = {
	.driver = {
		.name = "da9150-gpadc",
	},
	.probe = da9150_gpadc_probe,
	.remove = da9150_gpadc_remove,
};

module_platform_driver(da9150_gpadc_driver);

MODULE_DESCRIPTION("GPADC Driver for DA9150");
MODULE_AUTHOR("Adam Thomson <Adam.Thomson.Opensource@diasemi.com>");
MODULE_LICENSE("GPL");
