/*
 * MEN 16z188 Analog to Digial Converter
 *
 * Copyright (C) 2014 MEN Mikroelektronik GmbH (www.men.de)
 * Author: Johannes Thumshirn <johannes.thumshirn@men.de>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; version 2 of the License.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mcb.h>
#include <linux/io.h>
#include <linux/iio/iio.h>

#define Z188_ADC_MAX_CHAN	8
#define Z188_ADC_GAIN		0x0700000
#define Z188_MODE_VOLTAGE	BIT(27)
#define Z188_CFG_AUTO		0x1
#define Z188_CTRL_REG		0x40

#define ADC_DATA(x) (((x) >> 2) & 0x7ffffc)
#define ADC_OVR(x) ((x) & 0x1)

struct z188_adc {
	struct resource *mem;
	void __iomem *base;
};

#define Z188_ADC_CHANNEL(idx) {					\
		.type = IIO_VOLTAGE,				\
		.indexed = 1,					\
		.channel = (idx),				\
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),   \
}

static const struct iio_chan_spec z188_adc_iio_channels[] = {
	Z188_ADC_CHANNEL(0),
	Z188_ADC_CHANNEL(1),
	Z188_ADC_CHANNEL(2),
	Z188_ADC_CHANNEL(3),
	Z188_ADC_CHANNEL(4),
	Z188_ADC_CHANNEL(5),
	Z188_ADC_CHANNEL(6),
	Z188_ADC_CHANNEL(7),
};

static int z188_iio_read_raw(struct iio_dev *iio_dev,
			struct iio_chan_spec const *chan,
			int *val,
			int *val2,
			long info)
{
	struct z188_adc *adc = iio_priv(iio_dev);
	int ret;
	u16 tmp;

	switch (info) {
	case IIO_CHAN_INFO_RAW:
		tmp = readw(adc->base + chan->channel * 4);

		if (ADC_OVR(tmp)) {
			dev_info(&iio_dev->dev,
				"Oversampling error on ADC channel %d\n",
				chan->channel);
			return -EIO;
		}
		*val = ADC_DATA(tmp);
		ret = IIO_VAL_INT;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static const struct iio_info z188_adc_info = {
	.read_raw = &z188_iio_read_raw,
};

static void men_z188_config_channels(void __iomem *addr)
{
	int i;
	u32 cfg;
	u32 ctl;

	ctl = readl(addr + Z188_CTRL_REG);
	ctl |= Z188_CFG_AUTO;
	writel(ctl, addr + Z188_CTRL_REG);

	for (i = 0; i < Z188_ADC_MAX_CHAN; i++) {
		cfg = readl(addr + i);
		cfg &= ~Z188_ADC_GAIN;
		cfg |= Z188_MODE_VOLTAGE;
		writel(cfg, addr + i);
	}
}

static int men_z188_probe(struct mcb_device *dev,
			const struct mcb_device_id *id)
{
	struct z188_adc *adc;
	struct iio_dev *indio_dev;
	struct resource *mem;

	indio_dev = devm_iio_device_alloc(&dev->dev, sizeof(struct z188_adc));
	if (!indio_dev)
		return -ENOMEM;

	adc = iio_priv(indio_dev);
	indio_dev->name = "z188-adc";
	indio_dev->dev.parent = &dev->dev;
	indio_dev->info = &z188_adc_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = z188_adc_iio_channels;
	indio_dev->num_channels = ARRAY_SIZE(z188_adc_iio_channels);

	mem = mcb_request_mem(dev, "z188-adc");
	if (IS_ERR(mem))
		return PTR_ERR(mem);

	adc->base = ioremap(mem->start, resource_size(mem));
	if (adc->base == NULL)
		goto err;

	men_z188_config_channels(adc->base);

	adc->mem = mem;
	mcb_set_drvdata(dev, indio_dev);

	return iio_device_register(indio_dev);

err:
	mcb_release_mem(mem);
	return -ENXIO;
}

static void men_z188_remove(struct mcb_device *dev)
{
	struct iio_dev *indio_dev  = mcb_get_drvdata(dev);
	struct z188_adc *adc = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);
	iounmap(adc->base);
	mcb_release_mem(adc->mem);
}

static const struct mcb_device_id men_z188_ids[] = {
	{ .device = 0xbc },
	{ }
};
MODULE_DEVICE_TABLE(mcb, men_z188_ids);

static struct mcb_driver men_z188_driver = {
	.driver = {
		.name = "z188-adc",
		.owner = THIS_MODULE,
	},
	.probe = men_z188_probe,
	.remove = men_z188_remove,
	.id_table = men_z188_ids,
};
module_mcb_driver(men_z188_driver);

MODULE_AUTHOR("Johannes Thumshirn <johannes.thumshirn@men.de>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("IIO ADC driver for MEN 16z188 ADC Core");
MODULE_ALIAS("mcb:16z188");
