/*
 * Copyright (C) 2012 Heiko Stuebner <heiko@sntech.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __PLAT_S3C_SPI_CORE_H
#define __PLAT_S3C_SPI_CORE_H

/* These functions are only for use with the core support code, such as
 * the cpu specific initialisation code
 */

/* re-define device name depending on support. */
static inline void s3c64xx_spi_setname(char *name)
{
#ifdef CONFIG_S3C64XX_DEV_SPI0
	s3c64xx_device_spi0.name = name;
#endif
#ifdef CONFIG_S3C64XX_DEV_SPI1
	s3c64xx_device_spi1.name = name;
#endif
#ifdef CONFIG_S3C64XX_DEV_SPI2
	s3c64xx_device_spi2.name = name;
#endif
}

#endif /* __PLAT_S3C_SPI_CORE_H */
