/* linux/arch/arm/plat-s3c24xx/spi-bus0-gpg5_6_7.c
 *
 * Copyright (c) 2008 Simtec Electronics
 *	http://armlinux.simtec.co.uk/
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * S3C24XX SPI - gpio configuration for bus 1 on gpg5,6,7
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License.
*/

#include <linux/kernel.h>
#include <linux/gpio.h>

#include <mach/spi.h>
#include <mach/regs-gpio.h>

void s3c24xx_spi_gpiocfg_bus1_gpg5_6_7(struct s3c2410_spi_info *spi,
				       int enable)
{
	if (enable) {
		s3c_gpio_cfgpin(S3C2410_GPG(7), S3C2410_GPG7_SPICLK1);
		s3c_gpio_cfgpin(S3C2410_GPG(6), S3C2410_GPG6_SPIMOSI1);
		s3c_gpio_cfgpin(S3C2410_GPG(5), S3C2410_GPG5_SPIMISO1);
		s3c2410_gpio_pullup(S3C2410_GPG(5), 0);
		s3c2410_gpio_pullup(S3C2410_GPG(6), 0);
	} else {
		s3c_gpio_cfgpin(S3C2410_GPG(7), S3C2410_GPIO_INPUT);
		s3c_gpio_cfgpin(S3C2410_GPG(5), S3C2410_GPIO_INPUT);
		s3c_gpio_setpull(S3C2410_GPG(5), S3C_GPIO_PULL_NONE);
		s3c_gpio_setpull(S3C2410_GPG(6), S3C_GPIO_PULL_NONE);
		s3c_gpio_setpull(S3C2410_GPG(7), S3C_GPIO_PULL_NONE);
	}
}
