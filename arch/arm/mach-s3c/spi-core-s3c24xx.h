/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2012 Heiko Stuebner <heiko@sntech.de>
 */

#ifndef __PLAT_S3C_SPI_CORE_S3C24XX_H
#define __PLAT_S3C_SPI_CORE_S3C24XX_H

/* These functions are only for use with the core support code, such as
 * the cpu specific initialisation code
 */

/* re-define device name depending on support. */
static inline void s3c24xx_spi_setname(char *name)
{
#ifdef CONFIG_S3C64XX_DEV_SPI0
	s3c64xx_device_spi0.name = name;
#endif
}

#endif /* __PLAT_S3C_SPI_CORE_S3C24XX_H */
