/* linux/arch/arm/mach-s5pv310/include/mach/irqs.h
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * S5PV210 - IRQ definitions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __ASM_ARCH_IRQS_H
#define __ASM_ARCH_IRQS_H __FILE__

#include <plat/irqs.h>

/* PPI: Private Peripheral Interrupt */

#define IRQ_PPI(x)		S5P_IRQ(x+16)

#define IRQ_LOCALTIMER		IRQ_PPI(13)

/* SPI: Shared Peripheral Interrupt */

#define IRQ_SPI(x)		S5P_IRQ(x+32)

#define IRQ_EINT0		IRQ_SPI(40)
#define IRQ_EINT1		IRQ_SPI(41)
#define IRQ_EINT2		IRQ_SPI(42)
#define IRQ_EINT3		IRQ_SPI(43)
#define IRQ_USB_HSOTG		IRQ_SPI(44)
#define IRQ_USB_HOST		IRQ_SPI(45)
#define IRQ_MODEM_IF		IRQ_SPI(46)
#define IRQ_ROTATOR		IRQ_SPI(47)
#define IRQ_JPEG		IRQ_SPI(48)
#define IRQ_2D			IRQ_SPI(49)
#define IRQ_PCIE		IRQ_SPI(50)
#define IRQ_SYSTEM_TIMER	IRQ_SPI(51)
#define IRQ_MFC			IRQ_SPI(52)
#define IRQ_WDT			IRQ_SPI(53)
#define IRQ_AUDIO_SS		IRQ_SPI(54)
#define IRQ_AC97		IRQ_SPI(55)
#define IRQ_SPDIF		IRQ_SPI(56)
#define IRQ_KEYPAD		IRQ_SPI(57)
#define IRQ_INTFEEDCTRL_SSS	IRQ_SPI(58)
#define IRQ_SLIMBUS		IRQ_SPI(59)
#define IRQ_PMU			IRQ_SPI(60)
#define IRQ_TSI			IRQ_SPI(61)
#define IRQ_SATA		IRQ_SPI(62)
#define IRQ_GPS			IRQ_SPI(63)

#define MAX_IRQ_IN_COMBINER	8
#define COMBINER_GROUP(x)	((x) * MAX_IRQ_IN_COMBINER + IRQ_SPI(64))
#define COMBINER_IRQ(x, y)	(COMBINER_GROUP(x) + y)

#define IRQ_TIMER0_VIC		COMBINER_IRQ(22, 0)
#define IRQ_TIMER1_VIC		COMBINER_IRQ(22, 1)
#define IRQ_TIMER2_VIC		COMBINER_IRQ(22, 2)
#define IRQ_TIMER3_VIC		COMBINER_IRQ(22, 3)
#define IRQ_TIMER4_VIC		COMBINER_IRQ(22, 4)

#define IRQ_UART0		COMBINER_IRQ(26, 0)
#define IRQ_UART1		COMBINER_IRQ(26, 1)
#define IRQ_UART2		COMBINER_IRQ(26, 2)
#define IRQ_UART3		COMBINER_IRQ(26, 3)
#define IRQ_UART4		COMBINER_IRQ(26, 4)

#define IRQ_IIC			COMBINER_IRQ(27, 0)

/* Set the default NR_IRQS */

#define NR_IRQS			COMBINER_IRQ(MAX_COMBINER_NR, 0)

#define MAX_COMBINER_NR		39

#endif /* __ASM_ARCH_IRQS_H */
