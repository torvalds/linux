/*
 * Copyright (c) 2011 Zhang, Keguang <keguang.zhang@gmail.com>
 *
 * IRQ mappings for Loongson 1
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */


#ifndef __ASM_MACH_LOONGSON1_IRQ_H
#define __ASM_MACH_LOONGSON1_IRQ_H

/*
 * CPU core Interrupt Numbers
 */
#define MIPS_CPU_IRQ_BASE		0
#define MIPS_CPU_IRQ(x)			(MIPS_CPU_IRQ_BASE + (x))

#define SOFTINT0_IRQ			MIPS_CPU_IRQ(0)
#define SOFTINT1_IRQ			MIPS_CPU_IRQ(1)
#define INT0_IRQ			MIPS_CPU_IRQ(2)
#define INT1_IRQ			MIPS_CPU_IRQ(3)
#define INT2_IRQ			MIPS_CPU_IRQ(4)
#define INT3_IRQ			MIPS_CPU_IRQ(5)
#define INT4_IRQ			MIPS_CPU_IRQ(6)
#define TIMER_IRQ			MIPS_CPU_IRQ(7)		/* cpu timer */

#define MIPS_CPU_IRQS		(MIPS_CPU_IRQ(7) + 1 - MIPS_CPU_IRQ_BASE)

/*
 * INT0~3 Interrupt Numbers
 */
#define LS1X_IRQ_BASE			MIPS_CPU_IRQS
#define LS1X_IRQ(n, x)			(LS1X_IRQ_BASE + (n << 5) + (x))

#define LS1X_UART0_IRQ			LS1X_IRQ(0, 2)
#define LS1X_UART1_IRQ			LS1X_IRQ(0, 3)
#define LS1X_UART2_IRQ			LS1X_IRQ(0, 4)
#define LS1X_UART3_IRQ			LS1X_IRQ(0, 5)
#define LS1X_CAN0_IRQ			LS1X_IRQ(0, 6)
#define LS1X_CAN1_IRQ			LS1X_IRQ(0, 7)
#define LS1X_SPI0_IRQ			LS1X_IRQ(0, 8)
#define LS1X_SPI1_IRQ			LS1X_IRQ(0, 9)
#define LS1X_AC97_IRQ			LS1X_IRQ(0, 10)
#define LS1X_DMA0_IRQ			LS1X_IRQ(0, 13)
#define LS1X_DMA1_IRQ			LS1X_IRQ(0, 14)
#define LS1X_DMA2_IRQ			LS1X_IRQ(0, 15)
#define LS1X_PWM0_IRQ			LS1X_IRQ(0, 17)
#define LS1X_PWM1_IRQ			LS1X_IRQ(0, 18)
#define LS1X_PWM2_IRQ			LS1X_IRQ(0, 19)
#define LS1X_PWM3_IRQ			LS1X_IRQ(0, 20)
#define LS1X_RTC_INT0_IRQ		LS1X_IRQ(0, 21)
#define LS1X_RTC_INT1_IRQ		LS1X_IRQ(0, 22)
#define LS1X_RTC_INT2_IRQ		LS1X_IRQ(0, 23)
#define LS1X_TOY_INT0_IRQ		LS1X_IRQ(0, 24)
#define LS1X_TOY_INT1_IRQ		LS1X_IRQ(0, 25)
#define LS1X_TOY_INT2_IRQ		LS1X_IRQ(0, 26)
#define LS1X_RTC_TICK_IRQ		LS1X_IRQ(0, 27)
#define LS1X_TOY_TICK_IRQ		LS1X_IRQ(0, 28)

#define LS1X_EHCI_IRQ			LS1X_IRQ(1, 0)
#define LS1X_OHCI_IRQ			LS1X_IRQ(1, 1)
#define LS1X_GMAC0_IRQ			LS1X_IRQ(1, 2)
#define LS1X_GMAC1_IRQ			LS1X_IRQ(1, 3)

#define LS1X_IRQS		(LS1X_IRQ(4, 31) + 1 - LS1X_IRQ_BASE)

#define NR_IRQS			(MIPS_CPU_IRQS + LS1X_IRQS)

#endif /* __ASM_MACH_LOONGSON1_IRQ_H */
