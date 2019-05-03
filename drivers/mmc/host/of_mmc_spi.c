/*
 * OpenFirmware bindings for the MMC-over-SPI driver
 *
 * Copyright (c) MontaVista Software, Inc. 2008.
 *
 * Author: Anton Vorontsov <avorontsov@ru.mvista.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/irq.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/spi/spi.h>
#include <linux/spi/mmc_spi.h>
#include <linux/mmc/core.h>
#include <linux/mmc/host.h>

/* For archs that don't support NO_IRQ (such as mips), provide a dummy value */
#ifndef NO_IRQ
#define NO_IRQ 0
#endif

MODULE_LICENSE("GPL");

struct of_mmc_spi {
	int detect_irq;
	struct mmc_spi_platform_data pdata;
};

static struct of_mmc_spi *to_of_mmc_spi(struct device *dev)
{
	return container_of(dev->platform_data, struct of_mmc_spi, pdata);
}

static int of_mmc_spi_init(struct device *dev,
			   irqreturn_t (*irqhandler)(int, void *), void *mmc)
{
	struct of_mmc_spi *oms = to_of_mmc_spi(dev);

	return request_threaded_irq(oms->detect_irq, NULL, irqhandler,
					IRQF_ONESHOT, dev_name(dev), mmc);
}

static void of_mmc_spi_exit(struct device *dev, void *mmc)
{
	struct of_mmc_spi *oms = to_of_mmc_spi(dev);

	free_irq(oms->detect_irq, mmc);
}

struct mmc_spi_platform_data *mmc_spi_get_pdata(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	struct device_node *np = dev->of_node;
	struct of_mmc_spi *oms;

	if (dev->platform_data || !np)
		return dev->platform_data;

	oms = kzalloc(sizeof(*oms), GFP_KERNEL);
	if (!oms)
		return NULL;

	if (mmc_of_parse_voltage(np, &oms->pdata.ocr_mask) <= 0)
		goto err_ocr;

	oms->detect_irq = irq_of_parse_and_map(np, 0);
	if (oms->detect_irq != 0) {
		oms->pdata.init = of_mmc_spi_init;
		oms->pdata.exit = of_mmc_spi_exit;
	} else {
		oms->pdata.caps |= MMC_CAP_NEEDS_POLL;
	}

	dev->platform_data = &oms->pdata;
	return dev->platform_data;
err_ocr:
	kfree(oms);
	return NULL;
}
EXPORT_SYMBOL(mmc_spi_get_pdata);

void mmc_spi_put_pdata(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	struct device_node *np = dev->of_node;
	struct of_mmc_spi *oms = to_of_mmc_spi(dev);

	if (!dev->platform_data || !np)
		return;

	kfree(oms);
	dev->platform_data = NULL;
}
EXPORT_SYMBOL(mmc_spi_put_pdata);
