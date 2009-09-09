/* linux/arch/arm/plat-s3c24xx/spi-bus0-gpe11_12_13.c
 *
 * Copyright (c) 2008 Simtec Electronics
 *	http://armlinux.simtec.co.uk/
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * S3C24XX SPI - gpio configuration for bus 0 on gpe11,12,13
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License.
*/

#include <linux/kernel.h>
#include <linux/gpio.h>

#include <mach/spi.h>
#include <mach/regs-gpio.h>

void s3c24xx_spi_gpiocfg_bus0_gpe11_12_13(struct s3c2410_spi_info *spi,
					  int enable)
{
	if (enable) {
		s3c2410_gpio_cfgpin(S3C2410_GPE(13), S3C2410_GPE13_SPICLK0);
		s3c2410_gpio_cfgpin(S3C2410_GPE(12), S3C2410_GPE12_SPIMOSI0);
		s3c2410_gpio_cfgpin(S3C2410_GPE(11), S3C2410_GPE11_SPIMISO0);
		s3c2410_gpio_pullup(S3C2410_GPE(11), 0);
		s3c2410_gpio_pullup(S3C2410_GPE(13), 0);
	} else {
		s3c2410_gpio_cfgpin(S3C2410_GPE(13), S3C2410_GPIO_INPUT);
		s3c2410_gpio_cfgpin(S3C2410_GPE(11), S3C2410_GPIO_INPUT);
		s3c2410_gpio_pullup(S3C2410_GPE(11), 1);
		s3c2410_gpio_pullup(S3C2410_GPE(12), 1);
		s3c2410_gpio_pullup(S3C2410_GPE(13), 1);
	}
}
