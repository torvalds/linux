/*
 * arch/arm/mach-dove/include/mach/gpio.h
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef __ASM_ARCH_GPIO_H
#define __ASM_ARCH_GPIO_H

#include <asm/errno.h>
#include <mach/irqs.h>
#include <plat/gpio.h>
#include <asm-generic/gpio.h>		/* cansleep wrappers */

#define GPIO_MAX	64

#define GPIO_BASE_LO		(DOVE_GPIO_VIRT_BASE + 0x00)
#define GPIO_BASE_HI		(DOVE_GPIO_VIRT_BASE + 0x20)

#define GPIO_BASE(pin)		((pin < 32) ? GPIO_BASE_LO : GPIO_BASE_HI)

#define GPIO_OUT(pin)		(GPIO_BASE(pin) + 0x00)
#define GPIO_IO_CONF(pin)	(GPIO_BASE(pin) + 0x04)
#define GPIO_BLINK_EN(pin)	(GPIO_BASE(pin) + 0x08)
#define GPIO_IN_POL(pin)	(GPIO_BASE(pin) + 0x0c)
#define GPIO_DATA_IN(pin)	(GPIO_BASE(pin) + 0x10)
#define GPIO_EDGE_CAUSE(pin)	(GPIO_BASE(pin) + 0x14)
#define GPIO_EDGE_MASK(pin)	(GPIO_BASE(pin) + 0x18)
#define GPIO_LEVEL_MASK(pin)	(GPIO_BASE(pin) + 0x1c)

static inline int gpio_to_irq(int pin)
{
	if (pin < NR_GPIO_IRQS)
		return pin + IRQ_DOVE_GPIO_START;

	return -EINVAL;
}

static inline int irq_to_gpio(int irq)
{
	if (IRQ_DOVE_GPIO_START < irq && irq < NR_IRQS)
		return irq - IRQ_DOVE_GPIO_START;

	return -EINVAL;
}

#endif
