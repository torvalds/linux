/*
 * arch/asm-arm/mach-mv78xx0/include/mach/gpio.h
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

extern int mv78xx0_core_index(void);

#define GPIO_MAX		32
#define GPIO_OUT(pin)		(DEV_BUS_VIRT_BASE + 0x0100)
#define GPIO_IO_CONF(pin)	(DEV_BUS_VIRT_BASE + 0x0104)
#define GPIO_BLINK_EN(pin)	(DEV_BUS_VIRT_BASE + 0x0108)
#define GPIO_IN_POL(pin)	(DEV_BUS_VIRT_BASE + 0x010c)
#define GPIO_DATA_IN(pin)	(DEV_BUS_VIRT_BASE + 0x0110)
#define GPIO_EDGE_CAUSE(pin)	(DEV_BUS_VIRT_BASE + 0x0114)
#define GPIO_MASK_OFF		(mv78xx0_core_index() ? 0x18 : 0)
#define GPIO_EDGE_MASK(pin)	(DEV_BUS_VIRT_BASE + 0x0118 + GPIO_MASK_OFF)
#define GPIO_LEVEL_MASK(pin)	(DEV_BUS_VIRT_BASE + 0x011c + GPIO_MASK_OFF)

static inline int gpio_to_irq(int pin)
{
	return pin + IRQ_MV78XX0_GPIO_START;
}

static inline int irq_to_gpio(int irq)
{
	return irq - IRQ_MV78XX0_GPIO_START;
}


#endif
