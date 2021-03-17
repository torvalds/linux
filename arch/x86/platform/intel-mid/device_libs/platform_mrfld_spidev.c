// SPDX-License-Identifier: GPL-2.0-only
/*
 * spidev platform data initialization file
 *
 * (C) Copyright 2014, 2016 Intel Corporation
 * Authors: Andy Shevchenko <andriy.shevchenko@linux.intel.com>
 *	    Dan O'Donovan <dan@emutex.com>
 */

#include <linux/err.h>
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

	if (intel_mid_identify_cpu() != INTEL_MID_CPU_CHIP_TANGIER)
		return ERR_PTR(-ENODEV);

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
