/* linux/arch/arm/plat-s3c/gpio.c
 *
 * Copyright 2008 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *	http://armlinux.simtec.co.uk/
 *
 * S3C series GPIO core
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/spinlock.h>

#include <plat/gpio-core.h>

#ifdef CONFIG_S3C_GPIO_TRACK
struct s3c_gpio_chip *s3c_gpios[S3C_GPIO_END];

static __init void s3c_gpiolib_track(struct s3c_gpio_chip *chip)
{
	unsigned int gpn;
	int i;

	gpn = chip->chip.base;
	for (i = 0; i < chip->chip.ngpio; i++, gpn++) {
		BUG_ON(gpn >= ARRAY_SIZE(s3c_gpios));
		s3c_gpios[gpn] = chip;
	}
}
#endif /* CONFIG_S3C_GPIO_TRACK */

/* Default routines for controlling GPIO, based on the original S3C24XX
 * GPIO functions which deal with the case where each gpio bank of the
 * chip is as following:
 *
 * base + 0x00: Control register, 2 bits per gpio
 *	        gpio n: 2 bits starting at (2*n)
 *		00 = input, 01 = output, others mean special-function
 * base + 0x04: Data register, 1 bit per gpio
 *		bit n: data bit n
*/

static int s3c_gpiolib_input(struct gpio_chip *chip, unsigned offset)
{
	struct s3c_gpio_chip *ourchip = to_s3c_gpio(chip);
	void __iomem *base = ourchip->base;
	unsigned long flags;
	unsigned long con;

	s3c_gpio_lock(ourchip, flags);

	con = __raw_readl(base + 0x00);
	con &= ~(3 << (offset * 2));

	__raw_writel(con, base + 0x00);

	s3c_gpio_unlock(ourchip, flags);
	return 0;
}

static int s3c_gpiolib_output(struct gpio_chip *chip,
			      unsigned offset, int value)
{
	struct s3c_gpio_chip *ourchip = to_s3c_gpio(chip);
	void __iomem *base = ourchip->base;
	unsigned long flags;
	unsigned long dat;
	unsigned long con;

	s3c_gpio_lock(ourchip, flags);

	dat = __raw_readl(base + 0x04);
	dat &= ~(1 << offset);
	if (value)
		dat |= 1 << offset;
	__raw_writel(dat, base + 0x04);

	con = __raw_readl(base + 0x00);
	con &= ~(3 << (offset * 2));
	con |= 1 << (offset * 2);

	__raw_writel(con, base + 0x00);
	__raw_writel(dat, base + 0x04);

	s3c_gpio_unlock(ourchip, flags);
	return 0;
}

static void s3c_gpiolib_set(struct gpio_chip *chip,
			    unsigned offset, int value)
{
	struct s3c_gpio_chip *ourchip = to_s3c_gpio(chip);
	void __iomem *base = ourchip->base;
	unsigned long flags;
	unsigned long dat;

	s3c_gpio_lock(ourchip, flags);

	dat = __raw_readl(base + 0x04);
	dat &= ~(1 << offset);
	if (value)
		dat |= 1 << offset;
	__raw_writel(dat, base + 0x04);

	s3c_gpio_unlock(ourchip, flags);
}

static int s3c_gpiolib_get(struct gpio_chip *chip, unsigned offset)
{
	struct s3c_gpio_chip *ourchip = to_s3c_gpio(chip);
	unsigned long val;

	val = __raw_readl(ourchip->base + 0x04);
	val >>= offset;
	val &= 1;

	return val;
}

__init void s3c_gpiolib_add(struct s3c_gpio_chip *chip)
{
	struct gpio_chip *gc = &chip->chip;
	int ret;

	BUG_ON(!chip->base);
	BUG_ON(!gc->label);
	BUG_ON(!gc->ngpio);

	spin_lock_init(&chip->lock);

	if (!gc->direction_input)
		gc->direction_input = s3c_gpiolib_input;
	if (!gc->direction_output)
		gc->direction_output = s3c_gpiolib_output;
	if (!gc->set)
		gc->set = s3c_gpiolib_set;
	if (!gc->get)
		gc->get = s3c_gpiolib_get;

#ifdef CONFIG_PM
	if (chip->pm != NULL) {
		if (!chip->pm->save || !chip->pm->resume)
			printk(KERN_ERR "gpio: %s has missing PM functions\n",
			       gc->label);
	} else
		printk(KERN_ERR "gpio: %s has no PM function\n", gc->label);
#endif

	/* gpiochip_add() prints own failure message on error. */
	ret = gpiochip_add(gc);
	if (ret >= 0)
		s3c_gpiolib_track(chip);
}
