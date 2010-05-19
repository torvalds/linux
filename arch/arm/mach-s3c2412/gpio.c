/* linux/arch/arm/mach-s3c2412/gpio.c
 *
 * Copyright (c) 2007 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * http://armlinux.simtec.co.uk/.
 *
 * S3C2412/S3C2413 specific GPIO support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <mach/regs-gpio.h>
#include <mach/hardware.h>

#include <plat/gpio-core.h>

int s3c2412_gpio_set_sleepcfg(unsigned int pin, unsigned int state)
{
	struct s3c_gpio_chip *chip = s3c_gpiolib_getchip(pin);
	unsigned long offs = pin - chip->chip.base;
	unsigned long flags;
	unsigned long slpcon;

	offs *= 2;

	if (pin < S3C2410_GPB(0))
		return -EINVAL;

	if (pin >= S3C2410_GPF(0) &&
	    pin <= S3C2410_GPG(16))
		return -EINVAL;

	if (pin > S3C2410_GPH(16))
		return -EINVAL;

	local_irq_save(flags);

	slpcon = __raw_readl(chip->base + 0x0C);

	slpcon &= ~(3 << offs);
	slpcon |= state << offs;

	__raw_writel(slpcon, chip->base + 0x0C);

	local_irq_restore(flags);

	return 0;
}

EXPORT_SYMBOL(s3c2412_gpio_set_sleepcfg);
