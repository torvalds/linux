/*
 *  Nano River Technologies viperboard IIO ADC driver
 *
 *  (C) 2012 by Lemonage GmbH
 *  Author: Lars Poeschel <poeschel@lemonage.de>
 *  All rights reserved.
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the	License, or (at your
 *  option) any later version.
 *
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>

#include <linux/usb.h>
#include <linux/iio/iio.h>

#include <linux/mfd/viperboard.h>

#define VPRBRD_ADC_CMD_GET		0x00

struct vprbrd_adc_msg {
	u8 cmd;
	u8 chan;
	u8 val;
} __packed;

struct vprbrd_adc {
	struct vprbrd *vb;
};

#define VPRBRD_ADC_CHANNEL(_index) {			\
	.type = IIO_VOLTAGE,				\
	.indexed = 1,					\
	.channel = _index,				\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),	\
}

static struct iio_chan_spec const vprbrd_adc_iio_channels[] = {
	VPRBRD_ADC_CHANNEL(0),
	VPRBRD_ADC_CHANNEL(1),
	VPRBRD_ADC_CHANNEL(2),
	VPRBRD_ADC_CHANNEL(3),
};

static int vprbrd_iio_read_raw(struct iio_dev *iio_dev,
				struct iio_chan_spec const *chan,
				int *val,
				int *val2,
				long info)
{
	int ret, error = 0;
	struct vprbrd_adc *adc = iio_priv(iio_dev);
	struct vprbrd *vb = adc->vb;
	struct vprbrd_adc_msg *admsg = (struct vprbrd_adc_msg *)vb->buf;

	switch (info) {
	case IIO_CHAN_INFO_RAW:
		mutex_lock(&vb->lock);

		admsg->cmd = VPRBRD_ADC_CMD_GET;
		admsg->chan = chan->channel;
		admsg->val = 0x00;

		ret = usb_control_msg(vb->usb_dev,
			usb_sndctrlpipe(vb->usb_dev, 0), VPRBRD_USB_REQUEST_ADC,
			VPRBRD_USB_TYPE_OUT, 0x0000, 0x0000, admsg,
			sizeof(struct vprbrd_adc_msg), VPRBRD_USB_TIMEOUT_MS);
		if (ret != sizeof(struct vprbrd_adc_msg)) {
			dev_err(&iio_dev->dev, "usb send error on adc read\n");
			error = -EREMOTEIO;
		}

		ret = usb_control_msg(vb->usb_dev,
			usb_rcvctrlpipe(vb->usb_dev, 0), VPRBRD_USB_REQUEST_ADC,
			VPRBRD_USB_TYPE_IN, 0x0000, 0x0000, admsg,
			sizeof(struct vprbrd_adc_msg), VPRBRD_USB_TIMEOUT_MS);

		*val = admsg->val;

		mutex_unlock(&vb->lock);

		if (ret != sizeof(struct vprbrd_adc_msg)) {
			dev_err(&iio_dev->dev, "usb recv error on adc read\n");
			error = -EREMOTEIO;
		}

		if (error)
			goto error;

		return IIO_VAL_INT;
	default:
		error = -EINVAL;
		break;
	}
error:
	return error;
}

static const struct iio_info vprbrd_adc_iio_info = {
	.read_raw = &vprbrd_iio_read_raw,
	.driver_module = THIS_MODULE,
};

static int vprbrd_adc_probe(struct platform_device *pdev)
{
	struct vprbrd *vb = dev_get_drvdata(pdev->dev.parent);
	struct vprbrd_adc *adc;
	struct iio_dev *indio_dev;
	int ret;

	/* registering iio */
	indio_dev = devm_iio_device_alloc(&pdev->dev, sizeof(*adc));
	if (!indio_dev) {
		dev_err(&pdev->dev, "failed allocating iio device\n");
		return -ENOMEM;
	}

	adc = iio_priv(indio_dev);
	adc->vb = vb;
	indio_dev->name = "viperboard adc";
	indio_dev->dev.parent = &pdev->dev;
	indio_dev->info = &vprbrd_adc_iio_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = vprbrd_adc_iio_channels;
	indio_dev->num_channels = ARRAY_SIZE(vprbrd_adc_iio_channels);

	ret = devm_iio_device_register(&pdev->dev, indio_dev);
	if (ret) {
		dev_err(&pdev->dev, "could not register iio (adc)");
		return ret;
	}

	platform_set_drvdata(pdev, indio_dev);

	return 0;
}

static struct platform_driver vprbrd_adc_driver = {
	.driver = {
		.name	= "viperboard-adc",
		.owner	= THIS_MODULE,
	},
	.probe		= vprbrd_adc_probe,
};

module_platform_driver(vprbrd_adc_driver);

MODULE_AUTHOR("Lars Poeschel <poeschel@lemonage.de>");
MODULE_DESCRIPTION("IIO ADC driver for Nano River Techs Viperboard");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:viperboard-adc");
