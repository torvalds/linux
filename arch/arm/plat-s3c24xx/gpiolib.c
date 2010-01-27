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
#include <linux/sysdev.h>
#include <linux/ioport.h>
#include <linux/io.h>
#include <linux/gpio.h>

#include <plat/gpio-core.h>
#include <mach/hardware.h>
#include <asm/irq.h>
#include <plat/pm.h>

#include <mach/regs-gpio.h>

static int s3c24xx_gpiolib_banka_input(struct gpio_chip *chip, unsigned offset)
{
	return -EINVAL;
}

static int s3c24xx_gpiolib_banka_output(struct gpio_chip *chip,
					unsigned offset, int value)
{
	struct s3c_gpio_chip *ourchip = to_s3c_gpio(chip);
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

static int s3c24xx_gpiolib_bankf_toirq(struct gpio_chip *chip, unsigned offset)
{
	if (offset < 4)
		return IRQ_EINT0 + offset;
	
	if (offset < 8)
		return IRQ_EINT4 + offset - 4;
	
	return -EINVAL;
}

static int s3c24xx_gpiolib_bankg_toirq(struct gpio_chip *chip, unsigned offset)
{
	return IRQ_EINT8 + offset;
}

struct s3c_gpio_chip s3c24xx_gpios[] = {
	[0] = {
		.base	= S3C2410_GPACON,
		.pm	= __gpio_pm(&s3c_gpio_pm_1bit),
		.chip	= {
			.base			= S3C2410_GPA(0),
			.owner			= THIS_MODULE,
			.label			= "GPIOA",
			.ngpio			= 24,
			.direction_input	= s3c24xx_gpiolib_banka_input,
			.direction_output	= s3c24xx_gpiolib_banka_output,
		},
	},
	[1] = {
		.base	= S3C2410_GPBCON,
		.pm	= __gpio_pm(&s3c_gpio_pm_2bit),
		.chip	= {
			.base			= S3C2410_GPB(0),
			.owner			= THIS_MODULE,
			.label			= "GPIOB",
			.ngpio			= 16,
		},
	},
	[2] = {
		.base	= S3C2410_GPCCON,
		.pm	= __gpio_pm(&s3c_gpio_pm_2bit),
		.chip	= {
			.base			= S3C2410_GPC(0),
			.owner			= THIS_MODULE,
			.label			= "GPIOC",
			.ngpio			= 16,
		},
	},
	[3] = {
		.base	= S3C2410_GPDCON,
		.pm	= __gpio_pm(&s3c_gpio_pm_2bit),
		.chip	= {
			.base			= S3C2410_GPD(0),
			.owner			= THIS_MODULE,
			.label			= "GPIOD",
			.ngpio			= 16,
		},
	},
	[4] = {
		.base	= S3C2410_GPECON,
		.pm	= __gpio_pm(&s3c_gpio_pm_2bit),
		.chip	= {
			.base			= S3C2410_GPE(0),
			.label			= "GPIOE",
			.owner			= THIS_MODULE,
			.ngpio			= 16,
		},
	},
	[5] = {
		.base	= S3C2410_GPFCON,
		.pm	= __gpio_pm(&s3c_gpio_pm_2bit),
		.chip	= {
			.base			= S3C2410_GPF(0),
			.owner			= THIS_MODULE,
			.label			= "GPIOF",
			.ngpio			= 8,
			.to_irq			= s3c24xx_gpiolib_bankf_toirq,
		},
	},
	[6] = {
		.base	= S3C2410_GPGCON,
		.pm	= __gpio_pm(&s3c_gpio_pm_2bit),
		.chip	= {
			.base			= S3C2410_GPG(0),
			.owner			= THIS_MODULE,
			.label			= "GPIOG",
			.ngpio			= 16,
			.to_irq			= s3c24xx_gpiolib_bankg_toirq,
		},
	}, {
		.base	= S3C2410_GPHCON,
		.pm	= __gpio_pm(&s3c_gpio_pm_2bit),
		.chip	= {
			.base			= S3C2410_GPH(0),
			.owner			= THIS_MODULE,
			.label			= "GPIOH",
			.ngpio			= 11,
		},
	},
};

static __init int s3c24xx_gpiolib_init(void)
{
	struct s3c_gpio_chip *chip = s3c24xx_gpios;
	int gpn;

	for (gpn = 0; gpn < ARRAY_SIZE(s3c24xx_gpios); gpn++, chip++)
		s3c_gpiolib_add(chip);

	return 0;
}

core_initcall(s3c24xx_gpiolib_init);
