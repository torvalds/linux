/*
 * tps65912-spi.c  --  SPI access for TI TPS65912x PMIC
 *
 * Copyright 2011 Texas Instruments Inc.
 *
 * Author: Margarita Olaya Cabrera <magi@slimlogic.co.uk>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under  the terms of the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  This driver is based on wm8350 implementation.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/spi/spi.h>
#include <linux/mfd/core.h>
#include <linux/mfd/tps65912.h>

static int tps65912_spi_write(struct tps65912 *tps65912, u8 addr,
							int bytes, void *src)
{
	struct spi_device *spi = tps65912->control_data;
	u8 *data = (u8 *) src;
	int ret;
	/* bit 23 is the read/write bit */
	unsigned long spi_data = 1 << 23 | addr << 15 | *data;
	struct spi_transfer xfer;
	struct spi_message msg;
	u32 tx_buf;

	tx_buf = spi_data;

	xfer.tx_buf	= &tx_buf;
	xfer.rx_buf	= NULL;
	xfer.len	= sizeof(unsigned long);
	xfer.bits_per_word = 24;

	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);

	ret = spi_sync(spi, &msg);
	return ret;
}

static int tps65912_spi_read(struct tps65912 *tps65912, u8 addr,
							int bytes, void *dest)
{
	struct spi_device *spi = tps65912->control_data;
	/* bit 23 is the read/write bit */
	unsigned long spi_data = 0 << 23 | addr << 15;
	struct spi_transfer xfer;
	struct spi_message msg;
	int ret;
	u8 *data = (u8 *) dest;
	u32 tx_buf, rx_buf;

	tx_buf = spi_data;
	rx_buf = 0;

	xfer.tx_buf	= &tx_buf;
	xfer.rx_buf	= &rx_buf;
	xfer.len	= sizeof(unsigned long);
	xfer.bits_per_word = 24;

	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);

	if (spi == NULL)
		return 0;

	ret = spi_sync(spi, &msg);
	if (ret == 0)
		*data = (u8) (rx_buf & 0xFF);
	return ret;
}

static int tps65912_spi_probe(struct spi_device *spi)
{
	struct tps65912 *tps65912;

	tps65912 = devm_kzalloc(&spi->dev,
				sizeof(struct tps65912), GFP_KERNEL);
	if (tps65912 == NULL)
		return -ENOMEM;

	tps65912->dev = &spi->dev;
	tps65912->control_data = spi;
	tps65912->read = tps65912_spi_read;
	tps65912->write = tps65912_spi_write;

	spi_set_drvdata(spi, tps65912);

	return tps65912_device_init(tps65912);
}

static int tps65912_spi_remove(struct spi_device *spi)
{
	struct tps65912 *tps65912 = spi_get_drvdata(spi);

	tps65912_device_exit(tps65912);

	return 0;
}

static struct spi_driver tps65912_spi_driver = {
	.driver = {
		.name = "tps65912",
		.owner = THIS_MODULE,
	},
	.probe	= tps65912_spi_probe,
	.remove = tps65912_spi_remove,
};

static int __init tps65912_spi_init(void)
{
	int ret;

	ret = spi_register_driver(&tps65912_spi_driver);
	if (ret != 0)
		pr_err("Failed to register TPS65912 SPI driver: %d\n", ret);

	return 0;
}
/* init early so consumer devices can complete system boot */
subsys_initcall(tps65912_spi_init);

static void __exit tps65912_spi_exit(void)
{
	spi_unregister_driver(&tps65912_spi_driver);
}
module_exit(tps65912_spi_exit);

MODULE_AUTHOR("Margarita Olaya	<magi@slimlogic.co.uk>");
MODULE_DESCRIPTION("SPI support for TPS65912 chip family mfd");
MODULE_LICENSE("GPL");
