/*
 * Driver for ADI Direct Digital Synthesis ad9910
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
#include <linux/module.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>

#define DRV_NAME "ad9910"

#define CFR1 0x0
#define CFR2 0x1
#define CFR3 0x2

#define AUXDAC 0x3
#define IOUPD 0x4
#define FTW 0x7
#define POW 0x8
#define ASF 0x9
#define MULTC 0x0A
#define DIG_RAMPL 0x0B
#define DIG_RAMPS 0x0C
#define DIG_RAMPR 0x0D
#define SIN_TONEP0 0x0E
#define SIN_TONEP1 0x0F
#define SIN_TONEP2 0x10
#define SIN_TONEP3 0x11
#define SIN_TONEP4 0x12
#define SIN_TONEP5 0x13
#define SIN_TONEP6 0x14
#define SIN_TONEP7 0x15

#define RAM_ENABLE	(1 << 7)

#define MANUAL_OSK	(1 << 7)
#define INVSIC		(1 << 6)
#define DDS_SINEOP	(1)

#define AUTO_OSK	(1)
#define OSKEN		(1 << 1)
#define LOAD_ARR	(1 << 2)
#define CLR_PHA		(1 << 3)
#define CLR_DIG		(1 << 4)
#define ACLR_PHA	(1 << 5)
#define ACLR_DIG	(1 << 6)
#define LOAD_LRR	(1 << 7)

#define LSB_FST		(1)
#define SDIO_IPT	(1 << 1)
#define EXT_PWD		(1 << 3)
#define ADAC_PWD	(1 << 4)
#define REFCLK_PWD	(1 << 5)
#define DAC_PWD		(1 << 6)
#define DIG_PWD		(1 << 7)

#define ENA_AMP		(1)
#define READ_FTW	(1)
#define DIGR_LOW	(1 << 1)
#define DIGR_HIGH	(1 << 2)
#define DIGR_ENA	(1 << 3)
#define SYNCCLK_ENA	(1 << 6)
#define ITER_IOUPD	(1 << 7)

#define TX_ENA		(1 << 1)
#define PDCLK_INV	(1 << 2)
#define PDCLK_ENB	(1 << 3)

#define PARA_ENA	(1 << 4)
#define SYNC_DIS	(1 << 5)
#define DATA_ASS	(1 << 6)
#define MATCH_ENA	(1 << 7)

#define PLL_ENA		(1)
#define PFD_RST		(1 << 2)
#define REFCLK_RST	(1 << 6)
#define REFCLK_BYP	(1 << 7)

/* Register format: 1 byte addr + value */
struct ad9910_config {
	u8 auxdac[5];
	u8 ioupd[5];
	u8 ftw[5];
	u8 pow[3];
	u8 asf[5];
	u8 multc[5];
	u8 dig_rampl[9];
	u8 dig_ramps[9];
	u8 dig_rampr[5];
	u8 sin_tonep0[9];
	u8 sin_tonep1[9];
	u8 sin_tonep2[9];
	u8 sin_tonep3[9];
	u8 sin_tonep4[9];
	u8 sin_tonep5[9];
	u8 sin_tonep6[9];
	u8 sin_tonep7[9];
};

struct ad9910_state {
	struct mutex lock;
	struct spi_device *sdev;
};

static ssize_t ad9910_set_parameter(struct device *dev,
					struct device_attribute *attr,
					const char *buf,
					size_t len)
{
	struct spi_message msg;
	struct spi_transfer xfer;
	int ret;
	struct ad9910_config *config = (struct ad9910_config *)buf;
	struct iio_dev *idev = dev_to_iio_dev(dev);
	struct ad9910_state *st = iio_priv(idev);

	xfer.len = 5;
	xfer.tx_buf = &config->auxdac[0];
	mutex_lock(&st->lock);

	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);
	ret = spi_sync(st->sdev, &msg);
	if (ret)
		goto error_ret;

	xfer.len = 5;
	xfer.tx_buf = &config->ioupd[0];

	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);
	ret = spi_sync(st->sdev, &msg);
	if (ret)
		goto error_ret;

	xfer.len = 5;
	xfer.tx_buf = &config->ftw[0];

	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);
	ret = spi_sync(st->sdev, &msg);
	if (ret)
		goto error_ret;

	xfer.len = 3;
	xfer.tx_buf = &config->pow[0];

	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);
	ret = spi_sync(st->sdev, &msg);
	if (ret)
		goto error_ret;

	xfer.len = 5;
	xfer.tx_buf = &config->asf[0];

	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);
	ret = spi_sync(st->sdev, &msg);
	if (ret)
		goto error_ret;

	xfer.len = 5;
	xfer.tx_buf = &config->multc[0];

	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);
	ret = spi_sync(st->sdev, &msg);
	if (ret)
		goto error_ret;

	xfer.len = 9;
	xfer.tx_buf = &config->dig_rampl[0];

	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);
	ret = spi_sync(st->sdev, &msg);
	if (ret)
		goto error_ret;

	xfer.len = 9;
	xfer.tx_buf = &config->dig_ramps[0];

	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);
	ret = spi_sync(st->sdev, &msg);
	if (ret)
		goto error_ret;

	xfer.len = 5;
	xfer.tx_buf = &config->dig_rampr[0];

	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);
	ret = spi_sync(st->sdev, &msg);
	if (ret)
		goto error_ret;

	xfer.len = 9;
	xfer.tx_buf = &config->sin_tonep0[0];

	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);
	ret = spi_sync(st->sdev, &msg);
	if (ret)
		goto error_ret;

	xfer.len = 9;
	xfer.tx_buf = &config->sin_tonep1[0];

	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);
	ret = spi_sync(st->sdev, &msg);
	if (ret)
		goto error_ret;

	xfer.len = 9;
	xfer.tx_buf = &config->sin_tonep2[0];

	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);
	ret = spi_sync(st->sdev, &msg);
	if (ret)
		goto error_ret;
	xfer.len = 9;
	xfer.tx_buf = &config->sin_tonep3[0];

	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);
	ret = spi_sync(st->sdev, &msg);
	if (ret)
		goto error_ret;

	xfer.len = 9;
	xfer.tx_buf = &config->sin_tonep4[0];

	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);
	ret = spi_sync(st->sdev, &msg);
	if (ret)
		goto error_ret;

	xfer.len = 9;
	xfer.tx_buf = &config->sin_tonep5[0];

	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);
	ret = spi_sync(st->sdev, &msg);
	if (ret)
		goto error_ret;

	xfer.len = 9;
	xfer.tx_buf = &config->sin_tonep6[0];

	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);
	ret = spi_sync(st->sdev, &msg);
	if (ret)
		goto error_ret;

	xfer.len = 9;
	xfer.tx_buf = &config->sin_tonep7[0];

	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);
	ret = spi_sync(st->sdev, &msg);
	if (ret)
		goto error_ret;
error_ret:
	mutex_unlock(&st->lock);

	return ret ? ret : len;
}

static IIO_DEVICE_ATTR(dds, S_IWUSR, NULL, ad9910_set_parameter, 0);

static void ad9910_init(struct ad9910_state *st)
{
	struct spi_message msg;
	struct spi_transfer xfer;
	int ret;
	u8 cfr[5];

	cfr[0] = CFR1;
	cfr[1] = 0;
	cfr[2] = MANUAL_OSK | INVSIC | DDS_SINEOP;
	cfr[3] = AUTO_OSK | OSKEN | ACLR_PHA | ACLR_DIG | LOAD_LRR;
	cfr[4] = 0;

	mutex_lock(&st->lock);

	xfer.len = 5;
	xfer.tx_buf = &cfr;

	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);
	ret = spi_sync(st->sdev, &msg);
	if (ret)
		goto error_ret;

	cfr[0] = CFR2;
	cfr[1] = ENA_AMP;
	cfr[2] = READ_FTW | DIGR_ENA | ITER_IOUPD;
	cfr[3] = TX_ENA | PDCLK_INV | PDCLK_ENB;
	cfr[4] = PARA_ENA;

	xfer.len = 5;
	xfer.tx_buf = &cfr;

	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);
	ret = spi_sync(st->sdev, &msg);
	if (ret)
		goto error_ret;

	cfr[0] = CFR3;
	cfr[1] = PLL_ENA;
	cfr[2] = 0;
	cfr[3] = REFCLK_RST | REFCLK_BYP;
	cfr[4] = 0;

	xfer.len = 5;
	xfer.tx_buf = &cfr;

	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);
	ret = spi_sync(st->sdev, &msg);
	if (ret)
		goto error_ret;

error_ret:
	mutex_unlock(&st->lock);



}

static struct attribute *ad9910_attributes[] = {
	&iio_dev_attr_dds.dev_attr.attr,
	NULL,
};

static const struct attribute_group ad9910_attribute_group = {
	.attrs = ad9910_attributes,
};

static const struct iio_info ad9910_info = {
	.attrs = &ad9910_attribute_group,
	.driver_module = THIS_MODULE,
};

static int ad9910_probe(struct spi_device *spi)
{
	struct ad9910_state *st;
	struct iio_dev *idev;
	int ret = 0;

	idev = iio_device_alloc(sizeof(*st));
	if (idev == NULL) {
		ret = -ENOMEM;
		goto error_ret;
	}
	spi_set_drvdata(spi, idev);
	st = iio_priv(idev);
	mutex_init(&st->lock);
	st->sdev = spi;

	idev->dev.parent = &spi->dev;
	idev->info = &ad9910_info;
	idev->modes = INDIO_DIRECT_MODE;

	ret = iio_device_register(idev);
	if (ret)
		goto error_free_dev;
	spi->max_speed_hz = 2000000;
	spi->mode = SPI_MODE_3;
	spi->bits_per_word = 8;
	spi_setup(spi);
	ad9910_init(st);
	return 0;

error_free_dev:
	iio_device_free(idev);
error_ret:
	return ret;
}

static int __devexit ad9910_remove(struct spi_device *spi)
{
	iio_device_unregister(spi_get_drvdata(spi));
	iio_device_free(spi_get_drvdata(spi));

	return 0;
}

static struct spi_driver ad9910_driver = {
	.driver = {
		.name = DRV_NAME,
		.owner = THIS_MODULE,
	},
	.probe = ad9910_probe,
	.remove = __devexit_p(ad9910_remove),
};
module_spi_driver(ad9910_driver);

MODULE_AUTHOR("Cliff Cai");
MODULE_DESCRIPTION("Analog Devices ad9910 driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("spi:" DRV_NAME);
