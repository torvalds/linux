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

#define gpio_cansleep		__gpio_cansleep
#define gpio_to_irq(gpio)	IRQ_GPIO(gpio)
#define irq_to_gpio(irq)	IRQ_TO_GPIO(irq)


#ifdef CONFIG_CPU_PXA26x
/* GPIO86/87/88/89 on PXA26x have their direction bits in GPDR2 inverted,
 * as well as their Alternate Function value being '1' for GPIO in GAFRx.
 */
static inline int __gpio_is_inverted(unsigned gpio)
{
	return cpu_is_pxa25x() && gpio > 85;
}
#else
static inline int __gpio_is_inverted(unsigned gpio) { return 0; }
#endif

/*
 * On PXA25x and PXA27x, GAFRx and GPDRx together decide the alternate
 * function of a GPIO, and GPDRx cannot be altered once configured. It
 * is attributed as "occupied" here (I know this terminology isn't
 * accurate, you are welcome to propose a better one :-)
 */
static inline int __gpio_is_occupied(unsigned gpio)
{
	if (cpu_is_pxa27x() || cpu_is_pxa25x()) {
		int af = (GAFR(gpio) >> ((gpio & 0xf) * 2)) & 0x3;
		int dir = GPDR(gpio) & GPIO_bit(gpio);

		if (__gpio_is_inverted(gpio))
			return af != 1 || dir == 0;
		else
			return af != 0 || dir != 0;
	} else
		return GPDR(gpio) & GPIO_bit(gpio);
}

typedef int (*set_wake_t)(unsigned int irq, unsigned int on);

extern void pxa_init_gpio(int mux_irq, int start, int end, set_wake_t fn);
#endif
