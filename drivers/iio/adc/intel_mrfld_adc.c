// SPDX-License-Identifier: GPL-2.0
/*
 * ADC driver for Basin Cove PMIC
 *
 * Copyright (C) 2012 Intel Corporation
 * Author: Bin Yang <bin.yang@intel.com>
 *
 * Rewritten for upstream by:
 *	 Vincent Pelletier <plr.vincent@gmail.com>
 *	 Andy Shevchenko <andriy.shevchenko@linux.intel.com>
 */

#include <linux/bitops.h>
#include <linux/completion.h>
#include <linux/interrupt.h>
#include <linux/mfd/intel_soc_pmic.h>
#include <linux/mfd/intel_soc_pmic_mrfld.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include <linux/iio/driver.h>
#include <linux/iio/iio.h>
#include <linux/iio/machine.h>

#include <asm/unaligned.h>

#define BCOVE_GPADCREQ			0xDC
#define BCOVE_GPADCREQ_BUSY		BIT(0)
#define BCOVE_GPADCREQ_IRQEN		BIT(1)

#define BCOVE_ADCIRQ_ALL (		\
	BCOVE_ADCIRQ_BATTEMP |		\
	BCOVE_ADCIRQ_SYSTEMP |		\
	BCOVE_ADCIRQ_BATTID |		\
	BCOVE_ADCIRQ_VIBATT |		\
	BCOVE_ADCIRQ_CCTICK)

#define BCOVE_ADC_TIMEOUT		msecs_to_jiffies(1000)

static const u8 mrfld_adc_requests[] = {
	BCOVE_ADCIRQ_VIBATT,
	BCOVE_ADCIRQ_BATTID,
	BCOVE_ADCIRQ_VIBATT,
	BCOVE_ADCIRQ_SYSTEMP,
	BCOVE_ADCIRQ_BATTEMP,
	BCOVE_ADCIRQ_BATTEMP,
	BCOVE_ADCIRQ_SYSTEMP,
	BCOVE_ADCIRQ_SYSTEMP,
	BCOVE_ADCIRQ_SYSTEMP,
};

struct mrfld_adc {
	struct regmap *regmap;
	struct completion completion;
	/* Lock to protect the IPC transfers */
	struct mutex lock;
};

static irqreturn_t mrfld_adc_thread_isr(int irq, void *data)
{
	struct iio_dev *indio_dev = data;
	struct mrfld_adc *adc = iio_priv(indio_dev);

	complete(&adc->completion);
	return IRQ_HANDLED;
}

static int mrfld_adc_single_conv(struct iio_dev *indio_dev,
				 struct iio_chan_spec const *chan,
				 int *result)
{
	struct mrfld_adc *adc = iio_priv(indio_dev);
	struct regmap *regmap = adc->regmap;
	unsigned int req;
	long timeout;
	u8 buf[2];
	int ret;

	reinit_completion(&adc->completion);

	regmap_update_bits(regmap, BCOVE_MADCIRQ, BCOVE_ADCIRQ_ALL, 0);
	regmap_update_bits(regmap, BCOVE_MIRQLVL1, BCOVE_LVL1_ADC, 0);

	ret = regmap_read_poll_timeout(regmap, BCOVE_GPADCREQ, req,
				       !(req & BCOVE_GPADCREQ_BUSY),
				       2000, 1000000);
	if (ret)
		goto done;

	req = mrfld_adc_requests[chan->channel];
	ret = regmap_write(regmap, BCOVE_GPADCREQ, BCOVE_GPADCREQ_IRQEN | req);
	if (ret)
		goto done;

	timeout = wait_for_completion_interruptible_timeout(&adc->completion,
							    BCOVE_ADC_TIMEOUT);
	if (timeout < 0) {
		ret = timeout;
		goto done;
	}
	if (timeout == 0) {
		ret = -ETIMEDOUT;
		goto done;
	}

	ret = regmap_bulk_read(regmap, chan->address, buf, 2);
	if (ret)
		goto done;

	*result = get_unaligned_be16(buf);
	ret = IIO_VAL_INT;

done:
	regmap_update_bits(regmap, BCOVE_MIRQLVL1, BCOVE_LVL1_ADC, 0xff);
	regmap_update_bits(regmap, BCOVE_MADCIRQ, BCOVE_ADCIRQ_ALL, 0xff);

	return ret;
}

static int mrfld_adc_read_raw(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *chan,
			      int *val, int *val2, long mask)
{
	struct mrfld_adc *adc = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		mutex_lock(&adc->lock);
		ret = mrfld_adc_single_conv(indio_dev, chan, val);
		mutex_unlock(&adc->lock);
		return ret;
	default:
		return -EINVAL;
	}
}

static const struct iio_info mrfld_adc_iio_info = {
	.read_raw = &mrfld_adc_read_raw,
};

#define BCOVE_ADC_CHANNEL(_type, _channel, _datasheet_name, _address)	\
	{								\
		.indexed = 1,						\
		.type = _type,						\
		.channel = _channel,					\
		.address = _address,					\
		.datasheet_name = _datasheet_name,			\
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),		\
	}

static const struct iio_chan_spec mrfld_adc_channels[] = {
	BCOVE_ADC_CHANNEL(IIO_VOLTAGE,    0, "CH0", 0xE9),
	BCOVE_ADC_CHANNEL(IIO_RESISTANCE, 1, "CH1", 0xEB),
	BCOVE_ADC_CHANNEL(IIO_CURRENT,    2, "CH2", 0xED),
	BCOVE_ADC_CHANNEL(IIO_TEMP,       3, "CH3", 0xCC),
	BCOVE_ADC_CHANNEL(IIO_TEMP,       4, "CH4", 0xC8),
	BCOVE_ADC_CHANNEL(IIO_TEMP,       5, "CH5", 0xCA),
	BCOVE_ADC_CHANNEL(IIO_TEMP,       6, "CH6", 0xC2),
	BCOVE_ADC_CHANNEL(IIO_TEMP,       7, "CH7", 0xC4),
	BCOVE_ADC_CHANNEL(IIO_TEMP,       8, "CH8", 0xC6),
};

static struct iio_map iio_maps[] = {
	IIO_MAP("CH0", "bcove-battery", "VBATRSLT"),
	IIO_MAP("CH1", "bcove-battery", "BATTID"),
	IIO_MAP("CH2", "bcove-battery", "IBATRSLT"),
	IIO_MAP("CH3", "bcove-temp",    "PMICTEMP"),
	IIO_MAP("CH4", "bcove-temp",    "BATTEMP0"),
	IIO_MAP("CH5", "bcove-temp",    "BATTEMP1"),
	IIO_MAP("CH6", "bcove-temp",    "SYSTEMP0"),
	IIO_MAP("CH7", "bcove-temp",    "SYSTEMP1"),
	IIO_MAP("CH8", "bcove-temp",    "SYSTEMP2"),
	{}
};

static int mrfld_adc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct intel_soc_pmic *pmic = dev_get_drvdata(dev->parent);
	struct iio_dev *indio_dev;
	struct mrfld_adc *adc;
	int irq;
	int ret;

	indio_dev = devm_iio_device_alloc(dev, sizeof(struct mrfld_adc));
	if (!indio_dev)
		return -ENOMEM;

	adc = iio_priv(indio_dev);

	mutex_init(&adc->lock);
	init_completion(&adc->completion);
	adc->regmap = pmic->regmap;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	ret = devm_request_threaded_irq(dev, irq, NULL, mrfld_adc_thread_isr,
					IRQF_ONESHOT | IRQF_SHARED, pdev->name,
					indio_dev);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, indio_dev);

	indio_dev->dev.parent = dev;
	indio_dev->name = pdev->name;

	indio_dev->channels = mrfld_adc_channels;
	indio_dev->num_channels = ARRAY_SIZE(mrfld_adc_channels);
	indio_dev->info = &mrfld_adc_iio_info;
	indio_dev->modes = INDIO_DIRECT_MODE;

	ret = iio_map_array_register(indio_dev, iio_maps);
	if (ret)
		return ret;

	ret = devm_iio_device_register(dev, indio_dev);
	if (ret < 0)
		goto err_array_unregister;

	return 0;

err_array_unregister:
	iio_map_array_unregister(indio_dev);
	return ret;
}

static int mrfld_adc_remove(struct platform_device *pdev)
{
	struct iio_dev *indio_dev = platform_get_drvdata(pdev);

	iio_map_array_unregister(indio_dev);

	return 0;
}

static const struct platform_device_id mrfld_adc_id_table[] = {
	{ .name = "mrfld_bcove_adc" },
	{}
};
MODULE_DEVICE_TABLE(platform, mrfld_adc_id_table);

static struct platform_driver mrfld_adc_driver = {
	.driver = {
		.name = "mrfld_bcove_adc",
	},
	.probe = mrfld_adc_probe,
	.remove = mrfld_adc_remove,
	.id_table = mrfld_adc_id_table,
};
module_platform_driver(mrfld_adc_driver);

MODULE_AUTHOR("Bin Yang <bin.yang@intel.com>");
MODULE_AUTHOR("Vincent Pelletier <plr.vincent@gmail.com>");
MODULE_AUTHOR("Andy Shevchenko <andriy.shevchenko@linux.intel.com>");
MODULE_DESCRIPTION("ADC driver for Basin Cove PMIC");
MODULE_LICENSE("GPL v2");
