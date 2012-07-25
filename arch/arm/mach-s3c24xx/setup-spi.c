/*
 * HS-SPI device setup for S3C2443/S3C2416
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

#include <mach/hardware.h>
#include <mach/regs-gpio.h>

#ifdef CONFIG_S3C64XX_DEV_SPI0
struct s3c64xx_spi_info s3c64xx_spi0_pdata __initdata = {
	.fifo_lvl_mask	= 0x7f,
	.rx_lvl_offset	= 13,
	.tx_st_done	= 21,
	.high_speed	= 1,
};

int s3c64xx_spi0_cfg_gpio(struct platform_device *pdev)
{
	/* enable hsspi bit in misccr */
	s3c2410_modify_misccr(S3C2416_MISCCR_HSSPI_EN2, 1);

	s3c_gpio_cfgall_range(S3C2410_GPE(11), 3,
			      S3C_GPIO_SFN(2), S3C_GPIO_PULL_UP);

	return 0;
}
#endif
