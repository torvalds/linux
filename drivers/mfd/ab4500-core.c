/*
 * Copyright (C) 2009 ST-Ericsson
 *
 * Author: Srinidhi KASAGAR <srinidhi.kasagar@stericsson.com>
 *
 * This program is free software; you can redistribute it
 * and/or modify it under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation.
 *
 * AB4500 is a companion power management chip used with U8500.
 * On this platform, this is interfaced with SSP0 controller
 * which is a ARM primecell pl022.
 *
 * At the moment the module just exports read/write features.
 * Interrupt management to be added - TODO.
 */
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/mfd/ab4500.h>

/* just required if probe fails, we need to
 * unregister the device
 */
static struct spi_driver ab4500_driver;

/*
 * This funtion writes to any AB4500 registers using
 * SPI protocol &  before it writes it packs the data
 * in the below 24 bit frame format
 *
 *	 *|------------------------------------|
 *	 *| 23|22...18|17.......10|9|8|7......0|
 *	 *| r/w  bank       adr          data  |
 *	 * ------------------------------------
 *
 * This function shouldn't be called from interrupt
 * context
 */
int ab4500_write(struct ab4500 *ab4500, unsigned char block,
		unsigned long addr, unsigned char data)
{
	struct spi_transfer xfer;
	struct spi_message	msg;
	int err;
	unsigned long spi_data =
		block << 18 | addr << 10 | data;

	mutex_lock(&ab4500->lock);
	ab4500->tx_buf[0] = spi_data;
	ab4500->rx_buf[0] = 0;

	xfer.tx_buf	= ab4500->tx_buf;
	xfer.rx_buf 	= NULL;
	xfer.len	= sizeof(unsigned long);

	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);

	err = spi_sync(ab4500->spi, &msg);
	mutex_unlock(&ab4500->lock);

	return err;
}
EXPORT_SYMBOL(ab4500_write);

int ab4500_read(struct ab4500 *ab4500, unsigned char block,
		unsigned long addr)
{
	struct spi_transfer xfer;
	struct spi_message	msg;
	unsigned long spi_data =
		1 << 23 | block << 18 | addr << 10;

	mutex_lock(&ab4500->lock);
	ab4500->tx_buf[0] = spi_data;
	ab4500->rx_buf[0] = 0;

	xfer.tx_buf	= ab4500->tx_buf;
	xfer.rx_buf 	= ab4500->rx_buf;
	xfer.len	= sizeof(unsigned long);

	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);

	spi_sync(ab4500->spi, &msg);
	mutex_unlock(&ab4500->lock);

	return  ab4500->rx_buf[0];
}
EXPORT_SYMBOL(ab4500_read);

/* ref: ab3100 core */
#define AB4500_DEVICE(devname, devid)				\
static struct platform_device ab4500_##devname##_device = {	\
	.name	= devid,					\
	.id	= -1,						\
}

/* list of childern devices of ab4500 - all are
 * not populated here - TODO
 */
AB4500_DEVICE(charger, "ab4500-charger");
AB4500_DEVICE(audio, "ab4500-audio");
AB4500_DEVICE(usb, "ab4500-usb");
AB4500_DEVICE(tvout, "ab4500-tvout");
AB4500_DEVICE(sim, "ab4500-sim");
AB4500_DEVICE(gpadc, "ab4500-gpadc");
AB4500_DEVICE(clkmgt, "ab4500-clkmgt");
AB4500_DEVICE(misc, "ab4500-misc");

static struct platform_device *ab4500_platform_devs[] = {
	&ab4500_charger_device,
	&ab4500_audio_device,
	&ab4500_usb_device,
	&ab4500_tvout_device,
	&ab4500_sim_device,
	&ab4500_gpadc_device,
	&ab4500_clkmgt_device,
	&ab4500_misc_device,
};

static int __init ab4500_probe(struct spi_device *spi)
{
	struct ab4500	*ab4500;
	unsigned char revision;
	int err = 0;
	int i;

	ab4500 = kzalloc(sizeof *ab4500, GFP_KERNEL);
	if (!ab4500) {
		dev_err(&spi->dev, "could not allocate AB4500\n");
		err = -ENOMEM;
		goto not_detect;
	}

	ab4500->spi = spi;
	spi_set_drvdata(spi, ab4500);

	mutex_init(&ab4500->lock);

	/* read the revision register */
	revision = ab4500_read(ab4500, AB4500_MISC, AB4500_REV_REG);

	/* revision id 0x0 is for early drop, 0x10 is for cut1.0 */
	if (revision == 0x0 || revision == 0x10)
		dev_info(&spi->dev, "Detected chip: %s, revision = %x\n",
			ab4500_driver.driver.name, revision);
	else	{
		dev_err(&spi->dev, "unknown chip: 0x%x\n", revision);
		goto not_detect;
	}

	for (i = 0; i < ARRAY_SIZE(ab4500_platform_devs); i++)	{
		ab4500_platform_devs[i]->dev.parent =
			&spi->dev;
		platform_set_drvdata(ab4500_platform_devs[i], ab4500);
	}

	/* register the ab4500 platform devices */
	platform_add_devices(ab4500_platform_devs,
			ARRAY_SIZE(ab4500_platform_devs));

	return err;

 not_detect:
	spi_unregister_driver(&ab4500_driver);
	kfree(ab4500);
	return err;
}

static int __devexit ab4500_remove(struct spi_device *spi)
{
	struct ab4500 *ab4500 =
		spi_get_drvdata(spi);

	kfree(ab4500);

	return 0;
}

static struct spi_driver ab4500_driver = {
	.driver = {
		.name = "ab4500",
		.owner = THIS_MODULE,
	},
	.probe = ab4500_probe,
	.remove = __devexit_p(ab4500_remove)
};

static int __devinit ab4500_init(void)
{
	return spi_register_driver(&ab4500_driver);
}

static void __exit ab4500_exit(void)
{
	spi_unregister_driver(&ab4500_driver);
}

subsys_initcall(ab4500_init);
module_exit(ab4500_exit);

MODULE_AUTHOR("Srinidhi KASAGAR <srinidhi.kasagar@stericsson.com");
MODULE_DESCRIPTION("AB4500 core driver");
MODULE_LICENSE("GPL");
