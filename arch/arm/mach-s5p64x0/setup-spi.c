/* linux/arch/arm/mach-s5p64x0/setup-spi.c
 *
 * Copyright (C) 2011 Samsung Electronics Ltd.
 *		http://www.samsung.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/gpio.h>
#include <linux/platform_device.h>
#include <linux/io.h>

#include <plat/gpio-cfg.h>
#include <plat/cpu.h>
#include <plat/s3c64xx-spi.h>

#ifdef CONFIG_S3C64XX_DEV_SPI0
struct s3c64xx_spi_info s3c64xx_spi0_pdata __initdata = {
	.fifo_lvl_mask	= 0x1ff,
	.rx_lvl_offset	= 15,
	.tx_st_done	= 25,
};

int s3c64xx_spi0_cfg_gpio(struct platform_device *dev)
{
	if (soc_is_s5p6450())
		s3c_gpio_cfgall_range(S5P6450_GPC(0), 3,
					S3C_GPIO_SFN(2), S3C_GPIO_PULL_UP);
	else
		s3c_gpio_cfgall_range(S5P6440_GPC(0), 3,
					S3C_GPIO_SFN(2), S3C_GPIO_PULL_UP);
	return 0;
}
#endif

#ifdef CONFIG_S3C64XX_DEV_SPI1
struct s3c64xx_spi_info s3c64xx_spi1_pdata __initdata = {
	.fifo_lvl_mask	= 0x7f,
	.rx_lvl_offset	= 15,
	.tx_st_done	= 25,
};

int s3c64xx_spi1_cfg_gpio(struct platform_device *dev)
{
	if (soc_is_s5p6450())
		s3c_gpio_cfgall_range(S5P6450_GPC(4), 3,
					S3C_GPIO_SFN(2), S3C_GPIO_PULL_UP);
	else
		s3c_gpio_cfgall_range(S5P6440_GPC(4), 3,
					S3C_GPIO_SFN(2), S3C_GPIO_PULL_UP);
	return 0;
}
#endif
