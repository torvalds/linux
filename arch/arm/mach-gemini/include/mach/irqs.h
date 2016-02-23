/*
 *  Copyright (C) 2001-2006 Storlink, Corp.
 *  Copyright (C) 2008-2009 Paulius Zaleckas <paulius.zaleckas@teltonika.lt>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __MACH_IRQS_H__
#define __MACH_IRQS_H__

#define IRQ_SERIRQ1	31
#define IRQ_SERIRQ0	30
#define IRQ_PCID	29
#define IRQ_PCIC	28
#define IRQ_PCIB	27
#define IRQ_PWR		26
#define IRQ_CIR		25
#define IRQ_GPIO(x)	(22 + (x))
#define IRQ_SSP		21
#define IRQ_LPC		20
#define IRQ_LCD		19
#define IRQ_UART	18
#define IRQ_RTC		17
#define IRQ_TIMER3	16
#define IRQ_TIMER2	15
#define IRQ_TIMER1	14
#define IRQ_FLASH	12
#define IRQ_USB1	11
#define IRQ_USB0	10
#define IRQ_DMA		9
#define IRQ_PCI		8
#define IRQ_IPSEC	7
#define IRQ_RAID	6
#define IRQ_IDE1	5
#define IRQ_IDE0	4
#define IRQ_WATCHDOG	3
#define IRQ_GMAC1	2
#define IRQ_GMAC0	1
#define IRQ_IPI		0

#define NORMAL_IRQ_NUM	32

#define GPIO_IRQ_BASE	NORMAL_IRQ_NUM
#define GPIO_IRQ_NUM	(3 * 32)

#define ARCH_TIMER_IRQ	IRQ_TIMER2

#define NR_IRQS		(NORMAL_IRQ_NUM + GPIO_IRQ_NUM)

#endif /* __MACH_IRQS_H__ */
