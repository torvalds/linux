// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * OpenFirmware bindings for the MMC-over-SPI driver
 *
 * Copyright (c) MontaVista Software, Inc. 2008.
 *
 * Author: Anton Vorontsov <avorontsov@ru.mvista.com>
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

MODULE_DESCRIPTION("OpenFirmware bindings for the MMC-over-SPI driver");
MODULE_LICENSE("GPL");

struct of_mmc_spi {
	struct mmc_spi_platform_data pdata;
	int detect_irq;
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
	struct mmc_host *mmc = dev_get_drvdata(&spi->dev);
	struct device *dev = &spi->dev;
	struct of_mmc_spi *oms;

	if (dev->platform_data || !dev_fwnode(dev))
		return dev->platform_data;

	oms = kzalloc(sizeof(*oms), GFP_KERNEL);
	if (!oms)
		return NULL;

	if (mmc_of_parse_voltage(mmc, &oms->pdata.ocr_mask) < 0)
		goto err_ocr;

	oms->detect_irq = spi->irq;
	if (oms->detect_irq > 0) {
		oms->pdata.init = of_mmc_spi_init;
		oms->pdata.exit = of_mmc_spi_exit;
	} else {
		oms->pdata.caps |= MMC_CAP_NEEDS_POLL;
	}
	if (device_property_read_bool(dev, "cap-sd-highspeed"))
		oms->pdata.caps |= MMC_CAP_SD_HIGHSPEED;
	if (device_property_read_bool(dev, "cap-mmc-highspeed"))
		oms->pdata.caps |= MMC_CAP_MMC_HIGHSPEED;

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
	struct of_mmc_spi *oms = to_of_mmc_spi(dev);

	if (!dev->platform_data || !dev_fwnode(dev))
		return;

	kfree(oms);
	dev->platform_data = NULL;
}
EXPORT_SYMBOL(mmc_spi_put_pdata);
