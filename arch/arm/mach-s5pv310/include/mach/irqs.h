/* linux/arch/arm/mach-s5pv310/include/mach/irqs.h
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * S5PV310 - IRQ definitions
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

#define IRQ_MCT1		IRQ_SPI(35)

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
#define IRQ_MCT0		IRQ_SPI(51)
#define IRQ_MFC			IRQ_SPI(52)
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

#define IRQ_RTC_ALARM		COMBINER_IRQ(23, 0)
#define IRQ_RTC_TIC		COMBINER_IRQ(23, 1)

#define IRQ_UART0		COMBINER_IRQ(26, 0)
#define IRQ_UART1		COMBINER_IRQ(26, 1)
#define IRQ_UART2		COMBINER_IRQ(26, 2)
#define IRQ_UART3		COMBINER_IRQ(26, 3)
#define IRQ_UART4		COMBINER_IRQ(26, 4)

#define IRQ_IIC			COMBINER_IRQ(27, 0)
#define IRQ_IIC1		COMBINER_IRQ(27, 1)
#define IRQ_IIC2		COMBINER_IRQ(27, 2)
#define IRQ_IIC3		COMBINER_IRQ(27, 3)
#define IRQ_IIC4		COMBINER_IRQ(27, 4)
#define IRQ_IIC5		COMBINER_IRQ(27, 5)
#define IRQ_IIC6		COMBINER_IRQ(27, 6)
#define IRQ_IIC7		COMBINER_IRQ(27, 7)

#define IRQ_HSMMC0		COMBINER_IRQ(29, 0)
#define IRQ_HSMMC1		COMBINER_IRQ(29, 1)
#define IRQ_HSMMC2		COMBINER_IRQ(29, 2)
#define IRQ_HSMMC3		COMBINER_IRQ(29, 3)

#define IRQ_ONENAND_AUDI	COMBINER_IRQ(34, 0)

#define IRQ_MCT_L1		COMBINER_IRQ(35, 3)

#define IRQ_EINT4		COMBINER_IRQ(37, 0)
#define IRQ_EINT5		COMBINER_IRQ(37, 1)
#define IRQ_EINT6		COMBINER_IRQ(37, 2)
#define IRQ_EINT7		COMBINER_IRQ(37, 3)
#define IRQ_EINT8		COMBINER_IRQ(38, 0)

#define IRQ_EINT9		COMBINER_IRQ(38, 1)
#define IRQ_EINT10		COMBINER_IRQ(38, 2)
#define IRQ_EINT11		COMBINER_IRQ(38, 3)
#define IRQ_EINT12		COMBINER_IRQ(38, 4)
#define IRQ_EINT13		COMBINER_IRQ(38, 5)
#define IRQ_EINT14		COMBINER_IRQ(38, 6)
#define IRQ_EINT15		COMBINER_IRQ(38, 7)

#define IRQ_EINT16_31		COMBINER_IRQ(39, 0)

#define IRQ_MCT_L0		COMBINER_IRQ(51, 0)

#define IRQ_WDT			COMBINER_IRQ(53, 0)

#define MAX_COMBINER_NR		54

#define S5P_IRQ_EINT_BASE	COMBINER_IRQ(MAX_COMBINER_NR, 0)

#define S5P_EINT_BASE1		(S5P_IRQ_EINT_BASE + 0)
#define S5P_EINT_BASE2		(S5P_IRQ_EINT_BASE + 16)

/* Set the default NR_IRQS */

#define NR_IRQS			(S5P_IRQ_EINT_BASE + 32)

#endif /* __ASM_ARCH_IRQS_H */
