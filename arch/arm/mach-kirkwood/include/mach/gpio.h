/*
 * arch/asm-arm/mach-kirkwood/include/mach/gpio.h
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef __ASM_ARCH_GPIO_H
#define __ASM_ARCH_GPIO_H

#include <mach/irqs.h>
#include <plat/gpio.h>
#include <asm-generic/gpio.h>		/* cansleep wrappers */

#define GPIO_MAX		50
#define GPIO_OFF(pin)		(((pin) >> 5) ? 0x0140 : 0x0100)
#define GPIO_OUT(pin)		(DEV_BUS_VIRT_BASE + GPIO_OFF(pin) + 0x00)
#define GPIO_IO_CONF(pin)	(DEV_BUS_VIRT_BASE + GPIO_OFF(pin) + 0x04)
#define GPIO_BLINK_EN(pin)	(DEV_BUS_VIRT_BASE + GPIO_OFF(pin) + 0x08)
#define GPIO_IN_POL(pin)	(DEV_BUS_VIRT_BASE + GPIO_OFF(pin) + 0x0c)
#define GPIO_DATA_IN(pin)	(DEV_BUS_VIRT_BASE + GPIO_OFF(pin) + 0x10)
#define GPIO_EDGE_CAUSE(pin)	(DEV_BUS_VIRT_BASE + GPIO_OFF(pin) + 0x14)
#define GPIO_EDGE_MASK(pin)	(DEV_BUS_VIRT_BASE + GPIO_OFF(pin) + 0x18)
#define GPIO_LEVEL_MASK(pin)	(DEV_BUS_VIRT_BASE + GPIO_OFF(pin) + 0x1c)

static inline int gpio_to_irq(int pin)
{
	return pin + IRQ_KIRKWOOD_GPIO_START;
}

static inline int irq_to_gpio(int irq)
{
	return irq - IRQ_KIRKWOOD_GPIO_START;
}


#endif
