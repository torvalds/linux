/*
 * Driver for ADI Direct Digital Synthesis ad9832
 *
 * Copyright (c) 2010 Analog Devices Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#include <linux/types.h>
#include <linux/mutex.h>
#include <linux/device.h>
#include <linux/spi/spi.h>
#include <linux/slab.h>
#include <linux/sysfs.h>

#include "../iio.h"
#include "../sysfs.h"

#define DRV_NAME "ad9832"

#define value_mask (u16)0xf000
#define cmd_shift 12
#define add_shift 8
#define AD9832_SYNC (1 << 13)
#define AD9832_SELSRC (1 << 12)
#define AD9832_SLEEP (1 << 13)
#define AD9832_RESET (1 << 12)
#define AD9832_CLR (1 << 11)

#define ADD_FREQ0LL 0x0
#define ADD_FREQ0HL 0x1
#define ADD_FREQ0LM 0x2
#define ADD_FREQ0HM 0x3
#define ADD_FREQ1LL 0x4
#define ADD_FREQ1HL 0x5
#define ADD_FREQ1LM 0x6
#define ADD_FREQ1HM 0x7
#define ADD_PHASE0L 0x8
#define ADD_PHASE0H 0x9
#define ADD_PHASE1L 0xa
#define ADD_PHASE1H 0xb
#define ADD_PHASE2L 0xc
#define ADD_PHASE2H 0xd
#define ADD_PHASE3L 0xe
#define ADD_PHASE3H 0xf

#define CMD_PHA8BITSW 0x1
#define CMD_PHA16BITSW 0x0
#define CMD_FRE8BITSW 0x3
#define CMD_FRE16BITSW 0x2
#define CMD_SELBITSCTL 0x6

struct ad9832_setting {
	u16 freq0[4];
	u16 freq1[4];
	u16 phase0[2];
	u16 phase1[2];
	u16 phase2[2];
	u16 phase3[2];
};

struct ad9832_state {
	struct mutex lock;
	struct iio_dev *idev;
	struct spi_device *sdev;
};

static ssize_t ad9832_set_parameter(struct device *dev,
					struct device_attribute *attr,
					const char *buf,
					size_t len)
{
	struct spi_message msg;
	struct spi_transfer xfer;
	int ret;
	struct ad9832_setting config;
	struct iio_dev *idev = dev_get_drvdata(dev);
	struct ad9832_state *st = idev->dev_data;

	config.freq0[0] = (CMD_FRE8BITSW << add_shift | ADD_FREQ0LL << add_shift | buf[0]);
	config.freq0[1] = (CMD_FRE16BITSW << add_shift | ADD_FREQ0HL << add_shift | buf[1]);
	config.freq0[2] = (CMD_FRE8BITSW << add_shift | ADD_FREQ0LM << add_shift | buf[2]);
	config.freq0[3] = (CMD_FRE16BITSW << add_shift | ADD_FREQ0HM << add_shift | buf[3]);
	config.freq1[0] = (CMD_FRE8BITSW << add_shift | ADD_FREQ1LL << add_shift | buf[4]);
	config.freq1[1] = (CMD_FRE16BITSW << add_shift | ADD_FREQ1HL << add_shift | buf[5]);
	config.freq1[2] = (CMD_FRE8BITSW << add_shift | ADD_FREQ1LM << add_shift | buf[6]);
	config.freq1[3] = (CMD_FRE16BITSW << add_shift | ADD_FREQ1HM << add_shift | buf[7]);

	config.phase0[0] = (CMD_PHA8BITSW << add_shift | ADD_PHASE0L << add_shift | buf[9]);
	config.phase0[1] = (CMD_PHA16BITSW << add_shift | ADD_PHASE0H << add_shift | buf[10]);
	config.phase1[0] = (CMD_PHA8BITSW << add_shift | ADD_PHASE1L << add_shift | buf[11]);
	config.phase1[1] = (CMD_PHA16BITSW << add_shift | ADD_PHASE1H << add_shift | buf[12]);
	config.phase2[0] = (CMD_PHA8BITSW << add_shift | ADD_PHASE2L << add_shift | buf[13]);
	config.phase2[1] = (CMD_PHA16BITSW << add_shift | ADD_PHASE2H << add_shift | buf[14]);
	config.phase3[0] = (CMD_PHA8BITSW << add_shift | ADD_PHASE3L << add_shift | buf[15]);
	config.phase3[1] = (CMD_PHA16BITSW << add_shift | ADD_PHASE3H << add_shift | buf[16]);

	xfer.len = 2 * len;
	xfer.tx_buf = &config;
	mutex_lock(&st->lock);

	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);
	ret = spi_sync(st->sdev, &msg);
	if (ret)
		goto error_ret;
error_ret:
	mutex_unlock(&st->lock);

	return ret ? ret : len;
}

static IIO_DEVICE_ATTR(dds, S_IWUSR, NULL, ad9832_set_parameter, 0);

static struct attribute *ad9832_attributes[] = {
	&iio_dev_attr_dds.dev_attr.attr,
	NULL,
};

static const struct attribute_group ad9832_attribute_group = {
	.name = DRV_NAME,
	.attrs = ad9832_attributes,
};

static void ad9832_init(struct ad9832_state *st)
{
	struct spi_message msg;
	struct spi_transfer xfer;
	int ret;
	u16 config = 0;

	config = 0x3 << 14 | AD9832_SLEEP | AD9832_RESET | AD9832_CLR;

	mutex_lock(&st->lock);

	xfer.len = 2;
	xfer.tx_buf = &config;

	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);
	ret = spi_sync(st->sdev, &msg);
	if (ret)
		goto error_ret;

	config = 0x2 << 14 | AD9832_SYNC | AD9832_SELSRC;
	xfer.len = 2;
	xfer.tx_buf = &config;

	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);
	ret = spi_sync(st->sdev, &msg);
	if (ret)
		goto error_ret;

	config = CMD_SELBITSCTL << cmd_shift;
	xfer.len = 2;
	xfer.tx_buf = &config;

	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);
	ret = spi_sync(st->sdev, &msg);
	if (ret)
		goto error_ret;

	config = 0x3 << 14;

	xfer.len = 2;
	xfer.tx_buf = &config;

	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);
	ret = spi_sync(st->sdev, &msg);
	if (ret)
		goto error_ret;
error_ret:
	mutex_unlock(&st->lock);



}

static int __devinit ad9832_probe(struct spi_device *spi)
{
	struct ad9832_state *st;
	int ret = 0;

	st = kzalloc(sizeof(*st), GFP_KERNEL);
	if (st == NULL) {
		ret = -ENOMEM;
		goto error_ret;
	}
	spi_set_drvdata(spi, st);

	mutex_init(&st->lock);
	st->sdev = spi;

	st->idev = iio_allocate_device();
	if (st->idev == NULL) {
		ret = -ENOMEM;
		goto error_free_st;
	}
	st->idev->dev.parent = &spi->dev;
	st->idev->num_interrupt_lines = 0;
	st->idev->event_attrs = NULL;

	st->idev->attrs = &ad9832_attribute_group;
	st->idev->dev_data = (void *)(st);
	st->idev->driver_module = THIS_MODULE;
	st->idev->modes = INDIO_DIRECT_MODE;

	ret = iio_device_register(st->idev);
	if (ret)
		goto error_free_dev;
	spi->max_speed_hz = 2000000;
	spi->mode = SPI_MODE_3;
	spi->bits_per_word = 16;
	spi_setup(spi);
	ad9832_init(st);
	return 0;

error_free_dev:
	iio_free_device(st->idev);
error_free_st:
	kfree(st);
error_ret:
	return ret;
}

static int __devexit ad9832_remove(struct spi_device *spi)
{
	struct ad9832_state *st = spi_get_drvdata(spi);

	iio_device_unregister(st->idev);
	kfree(st);

	return 0;
}

static struct spi_driver ad9832_driver = {
	.driver = {
		.name = DRV_NAME,
		.owner = THIS_MODULE,
	},
	.probe = ad9832_probe,
	.remove = __devexit_p(ad9832_remove),
};

static __init int ad9832_spi_init(void)
{
	return spi_register_driver(&ad9832_driver);
}
module_init(ad9832_spi_init);

static __exit void ad9832_spi_exit(void)
{
	spi_unregister_driver(&ad9832_driver);
}
module_exit(ad9832_spi_exit);

MODULE_AUTHOR("Cliff Cai");
MODULE_DESCRIPTION("Analog Devices ad9832 driver");
MODULE_LICENSE("GPL v2");
