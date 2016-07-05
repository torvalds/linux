/*
 * spidev platform data initilization file
 *
 * (C) Copyright 2014, 2016 Intel Corporation
 * Authors: Andy Shevchenko <andriy.shevchenko@linux.intel.com>
 *	    Dan O'Donovan <dan@emutex.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 */

#include <linux/init.h>
#include <linux/sfi.h>
#include <linux/spi/pxa2xx_spi.h>
#include <linux/spi/spi.h>

#include <asm/intel-mid.h>

#define MRFLD_SPI_DEFAULT_DMA_BURST	8
#define MRFLD_SPI_DEFAULT_TIMEOUT	500

/* GPIO pin for spidev chipselect */
#define MRFLD_SPIDEV_GPIO_CS		111

static struct pxa2xx_spi_chip spidev_spi_chip = {
	.dma_burst_size		= MRFLD_SPI_DEFAULT_DMA_BURST,
	.timeout		= MRFLD_SPI_DEFAULT_TIMEOUT,
	.gpio_cs		= MRFLD_SPIDEV_GPIO_CS,
};

static void __init *spidev_platform_data(void *info)
{
	struct spi_board_info *spi_info = info;

	spi_info->mode = SPI_MODE_0;
	spi_info->controller_data = &spidev_spi_chip;

	return NULL;
}

static const struct devs_id spidev_dev_id __initconst = {
	.name			= "spidev",
	.type			= SFI_DEV_TYPE_SPI,
	.delay			= 0,
	.get_platform_data	= &spidev_platform_data,
};

sfi_device(spidev_dev_id);
