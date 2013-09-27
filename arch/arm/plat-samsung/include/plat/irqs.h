/* linux/arch/arm/plat-samsung/include/plat/irqs.h
 *
 * Copyright (c) 2009 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * S5P Common IRQ support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __PLAT_SAMSUNG_IRQS_H
#define __PLAT_SAMSUNG_IRQS_H __FILE__

/* we keep the first set of CPU IRQs out of the range of
 * the ISA space, so that the PC104 has them to itself
 * and we don't end up having to do horrible things to the
 * standard ISA drivers....
 *
 * note, since we're using the VICs, our start must be a
 * mulitple of 32 to allow the common code to work
 */

#define S5P_IRQ_OFFSET		(32)

#define S5P_IRQ(x)		((x) + S5P_IRQ_OFFSET)

#define S5P_VIC0_BASE		S5P_IRQ(0)
#define S5P_VIC1_BASE		S5P_IRQ(32)
#define S5P_VIC2_BASE		S5P_IRQ(64)
#define S5P_VIC3_BASE		S5P_IRQ(96)

#define VIC_BASE(x)		(S5P_VIC0_BASE + ((x)*32))

#define IRQ_VIC0_BASE		S5P_VIC0_BASE
#define IRQ_VIC1_BASE		S5P_VIC1_BASE
#define IRQ_VIC2_BASE		S5P_VIC2_BASE

/* VIC based IRQs */

#define S5P_IRQ_VIC0(x)		(S5P_VIC0_BASE + (x))
#define S5P_IRQ_VIC1(x)		(S5P_VIC1_BASE + (x))
#define S5P_IRQ_VIC2(x)		(S5P_VIC2_BASE + (x))
#define S5P_IRQ_VIC3(x)		(S5P_VIC3_BASE + (x))

#define IRQ_EINT(x)		((x) < 16 ? ((x) + S5P_EINT_BASE1) \
					: ((x) - 16 + S5P_EINT_BASE2))

#define EINT_OFFSET(irq)	((irq) < S5P_EINT_BASE2 ? \
						((irq) - S5P_EINT_BASE1) : \
						((irq) + 16 - S5P_EINT_BASE2))

#define IRQ_EINT_BIT(x)		EINT_OFFSET(x)

/* Typically only a few gpio chips require gpio interrupt support.
   To avoid memory waste irq descriptors are allocated only for
   S5P_GPIOINT_GROUP_COUNT chips, each with total number of
   S5P_GPIOINT_GROUP_SIZE pins/irqs. Each GPIOINT group can be assiged
   to any gpio chip with the s5p_register_gpio_interrupt() function */
#define S5P_GPIOINT_GROUP_COUNT 4
#define S5P_GPIOINT_GROUP_SIZE	8
#define S5P_GPIOINT_COUNT	(S5P_GPIOINT_GROUP_COUNT * S5P_GPIOINT_GROUP_SIZE)

/* IRQ types common for all s5p platforms */
#define S5P_IRQ_TYPE_LEVEL_LOW		(0x00)
#define S5P_IRQ_TYPE_LEVEL_HIGH		(0x01)
#define S5P_IRQ_TYPE_EDGE_FALLING	(0x02)
#define S5P_IRQ_TYPE_EDGE_RISING	(0x03)
#define S5P_IRQ_TYPE_EDGE_BOTH		(0x04)

#endif /* __PLAT_SAMSUNG_IRQS_H */
