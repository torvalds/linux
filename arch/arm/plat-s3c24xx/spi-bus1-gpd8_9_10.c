/* linux/arch/arm/plat-s3c24xx/spi-bus0-gpd8_9_10.c
 *
 * Copyright (c) 2008 Simtec Electronics
 *	http://armlinux.simtec.co.uk/
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * S3C24XX SPI - gpio configuration for bus 1 on gpd8,9,10
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License.
*/

#include <linux/kernel.h>
#include <linux/gpio.h>

#include <mach/spi.h>
#include <mach/regs-gpio.h>

void s3c24xx_spi_gpiocfg_bus1_gpd8_9_10(struct s3c2410_spi_info *spi,
					int enable)
{

	printk(KERN_INFO "%s(%d)\n", __func__, enable);
	if (enable) {
		s3c2410_gpio_cfgpin(S3C2410_GPD(10), S3C2440_GPD10_SPICLK1);
		s3c2410_gpio_cfgpin(S3C2410_GPD(9), S3C2440_GPD9_SPIMOSI1);
		s3c2410_gpio_cfgpin(S3C2410_GPD(8), S3C2440_GPD8_SPIMISO1);
		s3c2410_gpio_pullup(S3C2410_GPD(10), 0);
		s3c2410_gpio_pullup(S3C2410_GPD(9), 0);
	} else {
		s3c2410_gpio_cfgpin(S3C2410_GPD(8), S3C2410_GPIO_INPUT);
		s3c2410_gpio_cfgpin(S3C2410_GPD(9), S3C2410_GPIO_INPUT);
		s3c2410_gpio_pullup(S3C2410_GPD(10), 1);
		s3c2410_gpio_pullup(S3C2410_GPD(9), 1);
		s3c2410_gpio_pullup(S3C2410_GPD(8), 1);
	}
}
