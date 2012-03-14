/*
 *  Copyright (C) 2008-2010 Gabor Juhos <juhosg@openwrt.org>
 *  Copyright (C) 2008 Imre Kaloz <kaloz@openwrt.org>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License version 2 as published
 *  by the Free Software Foundation.
 */
#ifndef __ASM_MACH_ATH79_IRQ_H
#define __ASM_MACH_ATH79_IRQ_H

#define MIPS_CPU_IRQ_BASE	0
#define NR_IRQS			46

#define ATH79_MISC_IRQ_BASE	8
#define ATH79_MISC_IRQ_COUNT	32

#define ATH79_PCI_IRQ_BASE	(ATH79_MISC_IRQ_BASE + ATH79_MISC_IRQ_COUNT)
#define ATH79_PCI_IRQ_COUNT	6
#define ATH79_PCI_IRQ(_x)	(ATH79_PCI_IRQ_BASE + (_x))

#define ATH79_CPU_IRQ_IP2	(MIPS_CPU_IRQ_BASE + 2)
#define ATH79_CPU_IRQ_USB	(MIPS_CPU_IRQ_BASE + 3)
#define ATH79_CPU_IRQ_GE0	(MIPS_CPU_IRQ_BASE + 4)
#define ATH79_CPU_IRQ_GE1	(MIPS_CPU_IRQ_BASE + 5)
#define ATH79_CPU_IRQ_MISC	(MIPS_CPU_IRQ_BASE + 6)
#define ATH79_CPU_IRQ_TIMER	(MIPS_CPU_IRQ_BASE + 7)

#define ATH79_MISC_IRQ_TIMER	(ATH79_MISC_IRQ_BASE + 0)
#define ATH79_MISC_IRQ_ERROR	(ATH79_MISC_IRQ_BASE + 1)
#define ATH79_MISC_IRQ_GPIO	(ATH79_MISC_IRQ_BASE + 2)
#define ATH79_MISC_IRQ_UART	(ATH79_MISC_IRQ_BASE + 3)
#define ATH79_MISC_IRQ_WDOG	(ATH79_MISC_IRQ_BASE + 4)
#define ATH79_MISC_IRQ_PERFC	(ATH79_MISC_IRQ_BASE + 5)
#define ATH79_MISC_IRQ_OHCI	(ATH79_MISC_IRQ_BASE + 6)
#define ATH79_MISC_IRQ_DMA	(ATH79_MISC_IRQ_BASE + 7)
#define ATH79_MISC_IRQ_TIMER2	(ATH79_MISC_IRQ_BASE + 8)
#define ATH79_MISC_IRQ_TIMER3	(ATH79_MISC_IRQ_BASE + 9)
#define ATH79_MISC_IRQ_TIMER4	(ATH79_MISC_IRQ_BASE + 10)
#define ATH79_MISC_IRQ_ETHSW	(ATH79_MISC_IRQ_BASE + 12)

#include_next <irq.h>

#endif /* __ASM_MACH_ATH79_IRQ_H */
