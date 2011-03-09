/* linux/arch/arm/plat-s5p/include/plat/irqs.h
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

#ifndef __ASM_PLAT_S5P_IRQS_H
#define __ASM_PLAT_S5P_IRQS_H __FILE__

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

/* UART interrupts, each UART has 4 intterupts per channel so
 * use the space between the ISA and S3C main interrupts. Note, these
 * are not in the same order as the S3C24XX series! */

#define IRQ_S5P_UART_BASE0	(16)
#define IRQ_S5P_UART_BASE1	(20)
#define IRQ_S5P_UART_BASE2	(24)
#define IRQ_S5P_UART_BASE3	(28)

#define UART_IRQ_RXD		(0)
#define UART_IRQ_ERR		(1)
#define UART_IRQ_TXD		(2)

#define IRQ_S5P_UART_RX0	(IRQ_S5P_UART_BASE0 + UART_IRQ_RXD)
#define IRQ_S5P_UART_TX0	(IRQ_S5P_UART_BASE0 + UART_IRQ_TXD)
#define IRQ_S5P_UART_ERR0	(IRQ_S5P_UART_BASE0 + UART_IRQ_ERR)

#define IRQ_S5P_UART_RX1	(IRQ_S5P_UART_BASE1 + UART_IRQ_RXD)
#define IRQ_S5P_UART_TX1	(IRQ_S5P_UART_BASE1 + UART_IRQ_TXD)
#define IRQ_S5P_UART_ERR1	(IRQ_S5P_UART_BASE1 + UART_IRQ_ERR)

#define IRQ_S5P_UART_RX2	(IRQ_S5P_UART_BASE2 + UART_IRQ_RXD)
#define IRQ_S5P_UART_TX2	(IRQ_S5P_UART_BASE2 + UART_IRQ_TXD)
#define IRQ_S5P_UART_ERR2	(IRQ_S5P_UART_BASE2 + UART_IRQ_ERR)

#define IRQ_S5P_UART_RX3	(IRQ_S5P_UART_BASE3 + UART_IRQ_RXD)
#define IRQ_S5P_UART_TX3	(IRQ_S5P_UART_BASE3 + UART_IRQ_TXD)
#define IRQ_S5P_UART_ERR3	(IRQ_S5P_UART_BASE3 + UART_IRQ_ERR)

/* S3C compatibilty defines */
#define IRQ_S3CUART_RX0		IRQ_S5P_UART_RX0
#define IRQ_S3CUART_RX1		IRQ_S5P_UART_RX1
#define IRQ_S3CUART_RX2		IRQ_S5P_UART_RX2
#define IRQ_S3CUART_RX3		IRQ_S5P_UART_RX3

/* VIC based IRQs */

#define S5P_IRQ_VIC0(x)		(S5P_VIC0_BASE + (x))
#define S5P_IRQ_VIC1(x)		(S5P_VIC1_BASE + (x))
#define S5P_IRQ_VIC2(x)		(S5P_VIC2_BASE + (x))
#define S5P_IRQ_VIC3(x)		(S5P_VIC3_BASE + (x))

#define S5P_TIMER_IRQ(x)	(11 + (x))

#define IRQ_TIMER0		S5P_TIMER_IRQ(0)
#define IRQ_TIMER1		S5P_TIMER_IRQ(1)
#define IRQ_TIMER2		S5P_TIMER_IRQ(2)
#define IRQ_TIMER3		S5P_TIMER_IRQ(3)
#define IRQ_TIMER4		S5P_TIMER_IRQ(4)

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

#endif /* __ASM_PLAT_S5P_IRQS_H */
