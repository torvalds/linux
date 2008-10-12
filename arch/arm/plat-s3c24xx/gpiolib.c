/* linux/arch/arm/plat-s3c24xx/gpiolib.c
 *
 * Copyright (c) 2008 Simtec Electronics
 *	http://armlinux.simtec.co.uk/
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * S3C24XX GPIOlib support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License.
*/

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/io.h>
#include <linux/gpio.h>

#include <mach/hardware.h>
#include <asm/irq.h>

#include <mach/regs-gpio.h>

struct s3c24xx_gpio_chip {
	struct gpio_chip	chip;
	void __iomem		*base;
};

static inline struct s3c24xx_gpio_chip *to_s3c_chip(struct gpio_chip *gpc)
{
	return container_of(gpc, struct s3c24xx_gpio_chip, chip);
}

/* these routines are exported for use by other parts of the platform
 * and system support, but are not intended to be used directly by the
 * drivers themsevles.
 */

static int s3c24xx_gpiolib_input(struct gpio_chip *chip, unsigned offset)
{
	struct s3c24xx_gpio_chip *ourchip = to_s3c_chip(chip);
	void __iomem *base = ourchip->base;
	unsigned long flags;
	unsigned long con;

	local_irq_save(flags);

	con = __raw_readl(base + 0x00);
	con &= ~(3 << (offset * 2));
	con |= (S3C2410_GPIO_OUTPUT & 0xf) << (offset * 2);

	__raw_writel(con, base + 0x00);

	local_irq_restore(flags);
	return 0;
}

static int s3c24xx_gpiolib_output(struct gpio_chip *chip,
				  unsigned offset, int value)
{
	struct s3c24xx_gpio_chip *ourchip = to_s3c_chip(chip);
	void __iomem *base = ourchip->base;
	unsigned long flags;
	unsigned long dat;
	unsigned long con;

	local_irq_save(flags);

	dat = __raw_readl(base + 0x04);
	dat &= ~(1 << offset);
	if (value)
		dat |= 1 << offset;
	__raw_writel(dat, base + 0x04);

	con = __raw_readl(base + 0x00);
	con &= ~(3 << (offset * 2));
	con |= (S3C2410_GPIO_OUTPUT & 0xf) << (offset * 2);

	__raw_writel(con, base + 0x00);
	__raw_writel(dat, base + 0x04);

	local_irq_restore(flags);
	return 0;
}

static void s3c24xx_gpiolib_set(struct gpio_chip *chip,
				unsigned offset, int value)
{
	struct s3c24xx_gpio_chip *ourchip = to_s3c_chip(chip);
	void __iomem *base = ourchip->base;
	unsigned long flags;
	unsigned long dat;

	local_irq_save(flags);

	dat = __raw_readl(base + 0x04);
	dat &= ~(1 << offset);
	if (value)
		dat |= 1 << offset;
	__raw_writel(dat, base + 0x04);

	local_irq_restore(flags);
}

static int s3c24xx_gpiolib_get(struct gpio_chip *chip, unsigned offset)
{
	struct s3c24xx_gpio_chip *ourchip = to_s3c_chip(chip);
	unsigned long val;

	val = __raw_readl(ourchip->base + 0x04);
	val >>= offset;
	val &= 1;

	return val;
}

static int s3c24xx_gpiolib_banka_input(struct gpio_chip *chip, unsigned offset)
{
	return -EINVAL;
}

static int s3c24xx_gpiolib_banka_output(struct gpio_chip *chip,
					unsigned offset, int value)
{
	struct s3c24xx_gpio_chip *ourchip = to_s3c_chip(chip);
	void __iomem *base = ourchip->base;
	unsigned long flags;
	unsigned long dat;
	unsigned long con;

	local_irq_save(flags);

	con = __raw_readl(base + 0x00);
	dat = __raw_readl(base + 0x04);

	dat &= ~(1 << offset);
	if (value)
		dat |= 1 << offset;

	__raw_writel(dat, base + 0x04);

	con &= ~(1 << offset);

	__raw_writel(con, base + 0x00);
	__raw_writel(dat, base + 0x04);

	local_irq_restore(flags);
	return 0;
}

static struct s3c24xx_gpio_chip gpios[] = {
	[0] = {
		.base	= S3C24XX_GPIO_BASE(S3C2410_GPA0),
		.chip	= {
			.base			= S3C2410_GPA0,
			.owner			= THIS_MODULE,
			.label			= "GPIOA",
			.ngpio			= 24,
			.direction_input	= s3c24xx_gpiolib_banka_input,
			.direction_output	= s3c24xx_gpiolib_banka_output,
			.set			= s3c24xx_gpiolib_set,
			.get			= s3c24xx_gpiolib_get,
		},
	},
	[1] = {
		.base	= S3C24XX_GPIO_BASE(S3C2410_GPB0),
		.chip	= {
			.base			= S3C2410_GPB0,
			.owner			= THIS_MODULE,
			.label			= "GPIOB",
			.ngpio			= 16,
			.direction_input	= s3c24xx_gpiolib_input,
			.direction_output	= s3c24xx_gpiolib_output,
			.set			= s3c24xx_gpiolib_set,
			.get			= s3c24xx_gpiolib_get,
		},
	},
	[2] = {
		.base	= S3C24XX_GPIO_BASE(S3C2410_GPC0),
		.chip	= {
			.base			= S3C2410_GPC0,
			.owner			= THIS_MODULE,
			.label			= "GPIOC",
			.ngpio			= 16,
			.direction_input	= s3c24xx_gpiolib_input,
			.direction_output	= s3c24xx_gpiolib_output,
			.set			= s3c24xx_gpiolib_set,
			.get			= s3c24xx_gpiolib_get,
		},
	},
	[3] = {
		.base	= S3C24XX_GPIO_BASE(S3C2410_GPD0),
		.chip	= {
			.base			= S3C2410_GPD0,
			.owner			= THIS_MODULE,
			.label			= "GPIOD",
			.ngpio			= 16,
			.direction_input	= s3c24xx_gpiolib_input,
			.direction_output	= s3c24xx_gpiolib_output,
			.set			= s3c24xx_gpiolib_set,
			.get			= s3c24xx_gpiolib_get,
		},
	},
	[4] = {
		.base	= S3C24XX_GPIO_BASE(S3C2410_GPE0),
		.chip	= {
			.base			= S3C2410_GPE0,
			.label			= "GPIOE",
			.owner			= THIS_MODULE,
			.ngpio			= 16,
			.direction_input	= s3c24xx_gpiolib_input,
			.direction_output	= s3c24xx_gpiolib_output,
			.set			= s3c24xx_gpiolib_set,
			.get			= s3c24xx_gpiolib_get,
		},
	},
	[5] = {
		.base	= S3C24XX_GPIO_BASE(S3C2410_GPF0),
		.chip	= {
			.base			= S3C2410_GPF0,
			.owner			= THIS_MODULE,
			.label			= "GPIOF",
			.ngpio			= 8,
			.direction_input	= s3c24xx_gpiolib_input,
			.direction_output	= s3c24xx_gpiolib_output,
			.set			= s3c24xx_gpiolib_set,
			.get			= s3c24xx_gpiolib_get,
		},
	},
	[6] = {
		.base	= S3C24XX_GPIO_BASE(S3C2410_GPG0),
		.chip	= {
			.base			= S3C2410_GPG0,
			.owner			= THIS_MODULE,
			.label			= "GPIOG",
			.ngpio			= 10,
			.direction_input	= s3c24xx_gpiolib_input,
			.direction_output	= s3c24xx_gpiolib_output,
			.set			= s3c24xx_gpiolib_set,
			.get			= s3c24xx_gpiolib_get,
		},
	},
};

static __init int s3c24xx_gpiolib_init(void)
{
	struct s3c24xx_gpio_chip *chip = gpios;
	int gpn;

	for (gpn = 0; gpn < ARRAY_SIZE(gpios); gpn++, chip++)
		gpiochip_add(&chip->chip);

	return 0;
}

arch_initcall(s3c24xx_gpiolib_init);
