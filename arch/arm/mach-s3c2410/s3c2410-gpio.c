/* linux/arch/arm/mach-s3c2410/gpio.c
 *
 * Copyright (c) 2004-2006 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * S3C2410 GPIO support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>

#include <asm/hardware.h>
#include <asm/irq.h>
#include <asm/io.h>

#include <asm/arch/regs-gpio.h>

int s3c2410_gpio_irqfilter(unsigned int pin, unsigned int on,
			   unsigned int config)
{
	void __iomem *reg = S3C24XX_EINFLT0;
	unsigned long flags;
	unsigned long val;

	if (pin < S3C2410_GPG8 || pin > S3C2410_GPG15)
		return -1;

	config &= 0xff;

	pin -= S3C2410_GPG8;
	reg += pin & ~3;

	local_irq_save(flags);

	/* update filter width and clock source */

	val = __raw_readl(reg);
	val &= ~(0xff << ((pin & 3) * 8));
	val |= config << ((pin & 3) * 8);
	__raw_writel(val, reg);

	/* update filter enable */

	val = __raw_readl(S3C24XX_EXTINT2);
	val &= ~(1 << ((pin * 4) + 3));
	val |= on << ((pin * 4) + 3);
	__raw_writel(val, S3C24XX_EXTINT2);

	local_irq_restore(flags);

	return 0;
}

EXPORT_SYMBOL(s3c2410_gpio_irqfilter);

int s3c2410_gpio_getirq(unsigned int pin)
{
	if (pin < S3C2410_GPF0 || pin > S3C2410_GPG15)
		return -1;	/* not valid interrupts */

	if (pin < S3C2410_GPG0 && pin > S3C2410_GPF7)
		return -1;	/* not valid pin */

	if (pin < S3C2410_GPF4)
		return (pin - S3C2410_GPF0) + IRQ_EINT0;

	if (pin < S3C2410_GPG0)
		return (pin - S3C2410_GPF4) + IRQ_EINT4;

	return (pin - S3C2410_GPG0) + IRQ_EINT8;
}

EXPORT_SYMBOL(s3c2410_gpio_getirq);
