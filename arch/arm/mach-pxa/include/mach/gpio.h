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

#include <mach/pxa-regs.h>
#include <asm/irq.h>
#include <mach/hardware.h>

#include <asm-generic/gpio.h>


/* NOTE: some PXAs have fewer on-chip GPIOs (like PXA255, with 85).
 * Those cases currently cause holes in the GPIO number space.
 */
#define NR_BUILTIN_GPIO 128

static inline int gpio_get_value(unsigned gpio)
{
	if (__builtin_constant_p(gpio) && (gpio < NR_BUILTIN_GPIO))
		return GPLR(gpio) & GPIO_bit(gpio);
	else
		return __gpio_get_value(gpio);
}

static inline void gpio_set_value(unsigned gpio, int value)
{
	if (__builtin_constant_p(gpio) && (gpio < NR_BUILTIN_GPIO)) {
		if (value)
			GPSR(gpio) = GPIO_bit(gpio);
		else
			GPCR(gpio) = GPIO_bit(gpio);
	} else {
		__gpio_set_value(gpio, value);
	}
}

#define gpio_cansleep __gpio_cansleep

#define gpio_to_irq(gpio)	IRQ_GPIO(gpio)
#define irq_to_gpio(irq)	IRQ_TO_GPIO(irq)


#endif
