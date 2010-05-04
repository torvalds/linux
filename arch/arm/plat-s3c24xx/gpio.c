/* linux/arch/arm/plat-s3c24xx/gpio.c
 *
 * Copyright (c) 2004-2005 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * S3C24XX GPIO support
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
#include <linux/io.h>

#include <mach/hardware.h>
#include <mach/gpio-fns.h>
#include <asm/irq.h>

#include <mach/regs-gpio.h>

unsigned int s3c2410_gpio_getcfg(unsigned int pin)
{
	void __iomem *base = S3C24XX_GPIO_BASE(pin);
	unsigned long val = __raw_readl(base);

	if (pin < S3C2410_GPIO_BANKB) {
		val >>= S3C2410_GPIO_OFFSET(pin);
		val &= 1;
		val += 1;
	} else {
		val >>= S3C2410_GPIO_OFFSET(pin)*2;
		val &= 3;
	}

	return val | S3C2410_GPIO_INPUT;
}

EXPORT_SYMBOL(s3c2410_gpio_getcfg);

void s3c2410_gpio_pullup(unsigned int pin, unsigned int to)
{
	void __iomem *base = S3C24XX_GPIO_BASE(pin);
	unsigned long offs = S3C2410_GPIO_OFFSET(pin);
	unsigned long flags;
	unsigned long up;

	if (pin < S3C2410_GPIO_BANKB)
		return;

	local_irq_save(flags);

	up = __raw_readl(base + 0x08);
	up &= ~(1L << offs);
	up |= to << offs;
	__raw_writel(up, base + 0x08);

	local_irq_restore(flags);
}

EXPORT_SYMBOL(s3c2410_gpio_pullup);


void s3c2410_gpio_setpin(unsigned int pin, unsigned int to)
{
	void __iomem *base = S3C24XX_GPIO_BASE(pin);
	unsigned long offs = S3C2410_GPIO_OFFSET(pin);
	unsigned long flags;
	unsigned long dat;

	local_irq_save(flags);

	dat = __raw_readl(base + 0x04);
	dat &= ~(1 << offs);
	dat |= to << offs;
	__raw_writel(dat, base + 0x04);

	local_irq_restore(flags);
}

EXPORT_SYMBOL(s3c2410_gpio_setpin);

unsigned int s3c2410_gpio_getpin(unsigned int pin)
{
	void __iomem *base = S3C24XX_GPIO_BASE(pin);
	unsigned long offs = S3C2410_GPIO_OFFSET(pin);

	return __raw_readl(base + 0x04) & (1<< offs);
}

EXPORT_SYMBOL(s3c2410_gpio_getpin);

unsigned int s3c2410_modify_misccr(unsigned int clear, unsigned int change)
{
	unsigned long flags;
	unsigned long misccr;

	local_irq_save(flags);
	misccr = __raw_readl(S3C24XX_MISCCR);
	misccr &= ~clear;
	misccr ^= change;
	__raw_writel(misccr, S3C24XX_MISCCR);
	local_irq_restore(flags);

	return misccr;
}

EXPORT_SYMBOL(s3c2410_modify_misccr);
