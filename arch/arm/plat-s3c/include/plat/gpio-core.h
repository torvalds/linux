/* linux/arch/arm/plat-s3c/include/plat/gpio-core.h
 *
 * Copyright 2008 Simtec Electronics
 *	http://armlinux.simtec.co.uk/
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * S3C Platform - GPIO core
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

/* Define the core gpiolib support functions that the s3c platforms may
 * need to extend or change depending on the hardware and the s3c chip
 * selected at build or found at run time.
 *
 * These definitions are not intended for driver inclusion, there is
 * nothing here that should not live outside the platform and core
 * specific code.
*/

/**
 * struct s3c_gpio_chip - wrapper for specific implementation of gpio
 * @chip: The chip structure to be exported via gpiolib.
 * @base: The base pointer to the gpio configuration registers.
 *
 * This wrapper provides the necessary information for the Samsung
 * specific gpios being registered with gpiolib.
 */
struct s3c_gpio_chip {
	struct gpio_chip	chip;
	void __iomem		*base;
};

static inline struct s3c_gpio_chip *to_s3c_gpio(struct gpio_chip *gpc)
{
	return container_of(gpc, struct s3c_gpio_chip, chip);
}

/** s3c_gpiolib_add() - add the s3c specific version of a gpio_chip.
 * @chip: The chip to register
 *
 * This is a wrapper to gpiochip_add() that takes our specific gpio chip
 * information and makes the necessary alterations for the platform and
 * notes the information for use with the configuration systems and any
 * other parts of the system.
 */
extern void s3c_gpiolib_add(struct s3c_gpio_chip *chip);
