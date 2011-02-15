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

#ifndef _ATH79_DEV_SPI_H
#define _ATH79_DEV_SPI_H

#include <linux/spi/spi.h>
#include <asm/mach-ath79/ath79_spi_platform.h>

void ath79_register_spi(struct ath79_spi_platform_data *pdata,
			 struct spi_board_info const *info,
			 unsigned n);

#endif /* _ATH79_DEV_SPI_H */
