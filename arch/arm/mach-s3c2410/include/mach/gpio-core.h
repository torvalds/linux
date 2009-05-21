/* arch/arm/mach-s3c24100/include/mach/gpio-core.h
 *
 * Copyright 2008 Openmoko, Inc.
 * Copyright 2008 Simtec Electronics
 *      Ben Dooks <ben@simtec.co.uk>
 *      http://armlinux.simtec.co.uk/
 *
 * S3C2410 - GPIO core support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __ASM_ARCH_GPIO_CORE_H
#define __ASM_ARCH_GPIO_CORE_H __FILE__

#include <plat/gpio-core.h>
#include <mach/regs-gpio.h>

extern struct s3c_gpio_chip s3c24xx_gpios[];

static inline struct s3c_gpio_chip *s3c_gpiolib_getchip(unsigned int pin)
{
	struct s3c_gpio_chip *chip;

	if (pin > S3C2410_GPG(10))
		return NULL;

	chip = &s3c24xx_gpios[pin/32];
	return (S3C2410_GPIO_OFFSET(pin) > chip->chip.ngpio) ? chip : NULL;
}

#endif /* __ASM_ARCH_GPIO_CORE_H */
