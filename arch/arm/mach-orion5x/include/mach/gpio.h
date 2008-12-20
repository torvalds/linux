/*
 * arch/arm/mach-orion5x/include/mach/gpio.h
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

#define GPIO_MAX		32
#define GPIO_OUT(pin)		ORION5X_DEV_BUS_REG(0x100)
#define GPIO_IO_CONF(pin)	ORION5X_DEV_BUS_REG(0x104)
#define GPIO_BLINK_EN(pin)	ORION5X_DEV_BUS_REG(0x108)
#define GPIO_IN_POL(pin)	ORION5X_DEV_BUS_REG(0x10c)
#define GPIO_DATA_IN(pin)	ORION5X_DEV_BUS_REG(0x110)
#define GPIO_EDGE_CAUSE(pin)	ORION5X_DEV_BUS_REG(0x114)
#define GPIO_EDGE_MASK(pin)	ORION5X_DEV_BUS_REG(0x118)
#define GPIO_LEVEL_MASK(pin)	ORION5X_DEV_BUS_REG(0x11c)

static inline int gpio_to_irq(int pin)
{
	return pin + IRQ_ORION5X_GPIO_START;
}

static inline int irq_to_gpio(int irq)
{
	return irq - IRQ_ORION5X_GPIO_START;
}


#endif
