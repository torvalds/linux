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

#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <asm/arch/regs-gpio.h>

#include <asm/hardware.h>

int s3c2412_gpio_set_sleepcfg(unsigned int pin, unsigned int state)
{
	void __iomem *base = S3C24XX_GPIO_BASE(pin);
	unsigned long offs = S3C2410_GPIO_OFFSET(pin);
	unsigned long flags;
	unsigned long slpcon;

	offs *= 2;

	if (pin < S3C2410_GPIO_BANKB)
		return -EINVAL;

	if (pin >= S3C2410_GPIO_BANKF &&
	    pin <= S3C2410_GPIO_BANKG)
		return -EINVAL;

	if (pin > (S3C2410_GPIO_BANKH + 32))
		return -EINVAL;

	local_irq_save(flags);

	slpcon = __raw_readl(base + 0x0C);

	slpcon &= ~(3 << offs);
	slpcon |= state << offs;

	__raw_writel(slpcon, base + 0x0C);

	local_irq_restore(flags);

	return 0;
}

EXPORT_SYMBOL(s3c2412_gpio_set_sleepcfg);
