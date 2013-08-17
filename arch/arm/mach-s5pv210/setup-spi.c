/* linux/arch/arm/mach-s5pv210/setup-spi.c
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

#include <plat/gpio-cfg.h>
#include <plat/s3c64xx-spi.h>

#ifdef CONFIG_S3C64XX_DEV_SPI0
struct s3c64xx_spi_info s3c64xx_spi0_pdata = {
	.fifo_lvl_mask	= 0x1ff,
	.rx_lvl_offset	= 15,
	.high_speed	= 1,
	.tx_st_done	= 25,
};

int s3c64xx_spi0_cfg_gpio(struct platform_device *dev)
{
	s3c_gpio_cfgpin(S5PV210_GPB(0), S3C_GPIO_SFN(2));
	s3c_gpio_setpull(S5PV210_GPB(0), S3C_GPIO_PULL_UP);
	s3c_gpio_cfgall_range(S5PV210_GPB(2), 2,
				S3C_GPIO_SFN(2), S3C_GPIO_PULL_UP);
	return 0;
}
#endif

#ifdef CONFIG_S3C64XX_DEV_SPI1
struct s3c64xx_spi_info s3c64xx_spi1_pdata = {
	.fifo_lvl_mask	= 0x7f,
	.rx_lvl_offset	= 15,
	.high_speed	= 1,
	.tx_st_done	= 25,
};

int s3c64xx_spi1_cfg_gpio(struct platform_device *dev)
{
	s3c_gpio_cfgpin(S5PV210_GPB(4), S3C_GPIO_SFN(2));
	s3c_gpio_setpull(S5PV210_GPB(4), S3C_GPIO_PULL_UP);
	s3c_gpio_cfgall_range(S5PV210_GPB(6), 2,
				S3C_GPIO_SFN(2), S3C_GPIO_PULL_UP);
	return 0;
}
#endif
