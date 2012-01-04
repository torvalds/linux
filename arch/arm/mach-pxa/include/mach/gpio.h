/*
 * arch/arm/mach-pxa/include/mach/gpio.h
 *
 * PXA GPIO wrappers for arch-neutral GPIO calls
 *
 * Written by Philipp Zabel <philipp.zabel@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */

#ifndef __ASM_ARCH_PXA_GPIO_H
#define __ASM_ARCH_PXA_GPIO_H

#include <asm-generic/gpio.h>
/* The defines for the driver are needed for the accelerated accessors */
#include "gpio-pxa.h"

#define gpio_to_irq(gpio)	IRQ_GPIO(gpio)

static inline int irq_to_gpio(unsigned int irq)
{
	int gpio;

	if (irq == IRQ_GPIO0 || irq == IRQ_GPIO1)
		return irq - IRQ_GPIO0;

	gpio = irq - PXA_GPIO_IRQ_BASE;
	if (gpio >= 2 && gpio < NR_BUILTIN_GPIO)
		return gpio;

	return -1;
}

#include <plat/gpio.h>
#endif
