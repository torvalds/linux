/*
 * arch/arm/mach-w90x900/include/mach/irqs.h
 *
 * Copyright (c) 2008 Nuvoton technology corporation
 * All rights reserved.
 *
 * Wan ZongShun <mcuos.com@gmail.com>
 *
 * Based on arch/arm/mach-s3c2410/include/mach/irqs.h
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#ifndef __ASM_ARCH_IRQS_H
#define __ASM_ARCH_IRQS_H

/*
 * we keep the first set of CPU IRQs out of the range of
 * the ISA space, so that the PC104 has them to itself
 * and we don't end up having to do horrible things to the
 * standard ISA drivers....
 *
 */

#define W90X900_IRQ(x)	(x)

/* Main cpu interrupts */

#define IRQ_WDT		W90X900_IRQ(1)
#define IRQ_UART0	W90X900_IRQ(7)
#define IRQ_UART1	W90X900_IRQ(8)
#define IRQ_UART2	W90X900_IRQ(9)
#define IRQ_UART3	W90X900_IRQ(10)
#define IRQ_UART4	W90X900_IRQ(11)
#define IRQ_TIMER0	W90X900_IRQ(12)
#define IRQ_TIMER1	W90X900_IRQ(13)
#define IRQ_T_INT_GROUP	W90X900_IRQ(14)
#define IRQ_ADC		W90X900_IRQ(31)
#define NR_IRQS		(IRQ_ADC+1)

#endif /* __ASM_ARCH_IRQ_H */
