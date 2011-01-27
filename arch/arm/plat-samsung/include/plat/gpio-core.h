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

struct s3c_gpio_chip;

/**
 * struct s3c_gpio_pm - power management (suspend/resume) information
 * @save: Routine to save the state of the GPIO block
 * @resume: Routine to resume the GPIO block.
 */
struct s3c_gpio_pm {
	void (*save)(struct s3c_gpio_chip *chip);
	void (*resume)(struct s3c_gpio_chip *chip);
};

struct s3c_gpio_cfg;

/**
 * struct s3c_gpio_chip - wrapper for specific implementation of gpio
 * @chip: The chip structure to be exported via gpiolib.
 * @base: The base pointer to the gpio configuration registers.
 * @group: The group register number for gpio interrupt support.
 * @irq_base: The base irq number.
 * @config: special function and pull-resistor control information.
 * @lock: Lock for exclusive access to this gpio bank.
 * @pm_save: Save information for suspend/resume support.
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
struct s3c_gpio_chip {
	struct gpio_chip	chip;
	struct s3c_gpio_cfg	*config;
	struct s3c_gpio_pm	*pm;
	void __iomem		*base;
	int			irq_base;
	int			group;
	spinlock_t		 lock;
#ifdef CONFIG_PM
	u32			pm_save[4];
#endif
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

/* CONFIG_S3C_GPIO_TRACK enables the tracking of the s3c specific gpios
 * for use with the configuration calls, and other parts of the s3c gpiolib
 * support code.
 *
 * Not all s3c support code will need this, as some configurations of cpu
 * may only support one or two different configuration options and have an
 * easy gpio to s3c_gpio_chip mapping function. If this is the case, then
 * the machine support file should provide its own s3c_gpiolib_getchip()
 * and any other necessary functions.
 */

/**
 * samsung_gpiolib_add_4bit_chips - 4bit single register GPIO config.
 * @chip: The gpio chip that is being configured.
 * @nr_chips: The no of chips (gpio ports) for the GPIO being configured.
 *
 * This helper deal with the GPIO cases where the control register has 4 bits
 * of control per GPIO, generally in the form of:
 * 0000 = Input
 * 0001 = Output
 * others = Special functions (dependant on bank)
 *
 * Note, since the code to deal with the case where there are two control
 * registers instead of one, we do not have a separate set of function
 * (samsung_gpiolib_add_4bit2_chips)for each case.
 */
extern void samsung_gpiolib_add_4bit_chips(struct s3c_gpio_chip *chip,
					   int nr_chips);
extern void samsung_gpiolib_add_4bit2_chips(struct s3c_gpio_chip *chip,
					    int nr_chips);
extern void samsung_gpiolib_add_2bit_chips(struct s3c_gpio_chip *chip,
					   int nr_chips);

extern void samsung_gpiolib_add_4bit(struct s3c_gpio_chip *chip);
extern void samsung_gpiolib_add_4bit2(struct s3c_gpio_chip *chip);


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
extern struct s3c_gpio_cfg s3c24xx_gpiocfg_default;

#ifdef CONFIG_S3C_GPIO_TRACK
extern struct s3c_gpio_chip *s3c_gpios[S3C_GPIO_END];

static inline struct s3c_gpio_chip *s3c_gpiolib_getchip(unsigned int chip)
{
	return (chip < S3C_GPIO_END) ? s3c_gpios[chip] : NULL;
}
#else
/* machine specific code should provide s3c_gpiolib_getchip */

#include <mach/gpio-track.h>

static inline void s3c_gpiolib_track(struct s3c_gpio_chip *chip) { }
#endif

#ifdef CONFIG_PM
extern struct s3c_gpio_pm s3c_gpio_pm_1bit;
extern struct s3c_gpio_pm s3c_gpio_pm_2bit;
extern struct s3c_gpio_pm s3c_gpio_pm_4bit;
#define __gpio_pm(x) x
#else
#define s3c_gpio_pm_1bit NULL
#define s3c_gpio_pm_2bit NULL
#define s3c_gpio_pm_4bit NULL
#define __gpio_pm(x) NULL

#endif /* CONFIG_PM */

/* locking wrappers to deal with multiple access to the same gpio bank */
#define s3c_gpio_lock(_oc, _fl) spin_lock_irqsave(&(_oc)->lock, _fl)
#define s3c_gpio_unlock(_oc, _fl) spin_unlock_irqrestore(&(_oc)->lock, _fl)
