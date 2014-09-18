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

#ifndef __PLAT_SAMSUNG_GPIO_CORE_H
#define __PLAT_SAMSUNG_GPIO_CORE_H

/* Bring in machine-local definitions, especially S3C_GPIO_END */
#include <mach/gpio-samsung.h>

#define GPIOCON_OFF	(0x00)
#define GPIODAT_OFF	(0x04)

#define con_4bit_shift(__off) ((__off) * 4)

/* Define the core gpiolib support functions that the s3c platforms may
 * need to extend or change depending on the hardware and the s3c chip
 * selected at build or found at run time.
 *
 * These definitions are not intended for driver inclusion, there is
 * nothing here that should not live outside the platform and core
 * specific code.
*/

struct samsung_gpio_chip;

/**
 * struct samsung_gpio_pm - power management (suspend/resume) information
 * @save: Routine to save the state of the GPIO block
 * @resume: Routine to resume the GPIO block.
 */
struct samsung_gpio_pm {
	void (*save)(struct samsung_gpio_chip *chip);
	void (*resume)(struct samsung_gpio_chip *chip);
};

struct samsung_gpio_cfg;

/**
 * struct samsung_gpio_chip - wrapper for specific implementation of gpio
 * @chip: The chip structure to be exported via gpiolib.
 * @base: The base pointer to the gpio configuration registers.
 * @group: The group register number for gpio interrupt support.
 * @irq_base: The base irq number.
 * @config: special function and pull-resistor control information.
 * @lock: Lock for exclusive access to this gpio bank.
 * @pm_save: Save information for suspend/resume support.
 * @bitmap_gpio_int: Bitmap for representing GPIO interrupt or not.
 *
 * This wrapper provides the necessary information for the Samsung
 * specific gpios being registered with gpiolib.
 *
 * The lock protects each gpio bank from multiple access of the shared
 * configuration registers, or from reading of data whilst another thread
 * is writing to the register set.
 *
 * Each chip has its own lock to avoid any  contention between different
 * CPU cores trying to get one lock for different GPIO banks, where each
 * bank of GPIO has its own register space and configuration registers.
 */
struct samsung_gpio_chip {
	struct gpio_chip	chip;
	struct samsung_gpio_cfg	*config;
	struct samsung_gpio_pm	*pm;
	void __iomem		*base;
	int			irq_base;
	int			group;
	spinlock_t		 lock;
#ifdef CONFIG_PM
	u32			pm_save[4];
#endif
	u32			bitmap_gpio_int;
};

static inline struct samsung_gpio_chip *to_samsung_gpio(struct gpio_chip *gpc)
{
	return container_of(gpc, struct samsung_gpio_chip, chip);
}

/**
 * samsung_gpiolib_to_irq - convert gpio pin to irq number
 * @chip: The gpio chip that the pin belongs to.
 * @offset: The offset of the pin in the chip.
 *
 * This helper returns the irq number calculated from the chip->irq_base and
 * the provided offset.
 */
extern int samsung_gpiolib_to_irq(struct gpio_chip *chip, unsigned int offset);

/* exported for core SoC support to change */
extern struct samsung_gpio_cfg s3c24xx_gpiocfg_default;

#ifdef CONFIG_S3C_GPIO_TRACK
extern struct samsung_gpio_chip *s3c_gpios[S3C_GPIO_END];

static inline struct samsung_gpio_chip *samsung_gpiolib_getchip(unsigned int chip)
{
	return (chip < S3C_GPIO_END) ? s3c_gpios[chip] : NULL;
}
#else
/* machine specific code should provide samsung_gpiolib_getchip */

extern struct samsung_gpio_chip s3c24xx_gpios[];

static inline struct samsung_gpio_chip *samsung_gpiolib_getchip(unsigned int pin)
{
	struct samsung_gpio_chip *chip;

	if (pin > S3C_GPIO_END)
		return NULL;

	chip = &s3c24xx_gpios[pin/32];
	return ((pin - chip->chip.base) < chip->chip.ngpio) ? chip : NULL;
}

static inline void s3c_gpiolib_track(struct samsung_gpio_chip *chip) { }
#endif

#ifdef CONFIG_PM
extern struct samsung_gpio_pm samsung_gpio_pm_1bit;
extern struct samsung_gpio_pm samsung_gpio_pm_2bit;
extern struct samsung_gpio_pm samsung_gpio_pm_4bit;
#define __gpio_pm(x) x
#else
#define samsung_gpio_pm_1bit NULL
#define samsung_gpio_pm_2bit NULL
#define samsung_gpio_pm_4bit NULL
#define __gpio_pm(x) NULL

#endif /* CONFIG_PM */

/* locking wrappers to deal with multiple access to the same gpio bank */
#define samsung_gpio_lock(_oc, _fl) spin_lock_irqsave(&(_oc)->lock, _fl)
#define samsung_gpio_unlock(_oc, _fl) spin_unlock_irqrestore(&(_oc)->lock, _fl)

#endif /* __PLAT_SAMSUNG_GPIO_CORE_H */
