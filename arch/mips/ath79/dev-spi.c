/*
 *  Atheros AR71XX/AR724X/AR913X SPI controller device
 *
 *  Copyright (C) 2008-2010 Gabor Juhos <juhosg@openwrt.org>
 *  Copyright (C) 2008 Imre Kaloz <kaloz@openwrt.org>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License version 2 as published
 *  by the Free Software Foundation.
 */

#include <linux/platform_device.h>
#include <asm/mach-ath79/ar71xx_regs.h>
#include "dev-spi.h"

static struct resource ath79_spi_resources[] = {
	{
		.start	= AR71XX_SPI_BASE,
		.end	= AR71XX_SPI_BASE + AR71XX_SPI_SIZE - 1,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device ath79_spi_device = {
	.name		= "ath79-spi",
	.id		= -1,
	.resource	= ath79_spi_resources,
	.num_resources	= ARRAY_SIZE(ath79_spi_resources),
};

void __init ath79_register_spi(struct ath79_spi_platform_data *pdata,
			       struct spi_board_info const *info,
			       unsigned n)
{
	spi_register_board_info(info, n);
	ath79_spi_device.dev.platform_data = pdata;
	platform_device_register(&ath79_spi_device);
}
