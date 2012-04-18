/*
 *  mach-nomadik/include/mach/irqs.h
 *
 *  Copyright (C) ST Microelectronics
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#ifndef __ASM_ARCH_IRQS_H
#define __ASM_ARCH_IRQS_H

#include <mach/hardware.h>

#define IRQ_VIC_START		1	/* first VIC interrupt is 1 */

/*
 * Interrupt numbers generic for all Nomadik Chip cuts
 */
#define IRQ_WATCHDOG			1
#define IRQ_SOFTINT			2
#define IRQ_CRYPTO			3
#define IRQ_OWM				4
#define IRQ_MTU0			5
#define IRQ_MTU1			6
#define IRQ_GPIO0			7
#define IRQ_GPIO1			8
#define IRQ_GPIO2			9
#define IRQ_GPIO3			10
#define IRQ_RTC_RTT			11
#define IRQ_SSP				12
#define IRQ_UART0			13
#define IRQ_DMA1			14
#define IRQ_CLCD_MDIF			15
#define IRQ_DMA0			16
#define IRQ_PWRFAIL			17
#define IRQ_UART1			18
#define IRQ_FIRDA			19
#define IRQ_MSP0			20
#define IRQ_I2C0			21
#define IRQ_I2C1			22
#define IRQ_SDMMC			23
#define IRQ_USBOTG			24
#define IRQ_SVA_IT0			25
#define IRQ_SVA_IT1			26
#define IRQ_SAA_IT0			27
#define IRQ_SAA_IT1			28
#define IRQ_UART2			29
#define IRQ_MSP2			30
#define IRQ_L2CC			49
#define IRQ_HPI				50
#define IRQ_SKE				51
#define IRQ_KP				52
#define IRQ_MEMST			55
#define IRQ_SGA_IT			59
#define IRQ_USBM			61
#define IRQ_MSP1			63

#define NOMADIK_GPIO_OFFSET		(IRQ_VIC_START+64)

/* After chip-specific IRQ numbers we have the GPIO ones */
#define NOMADIK_NR_GPIO			128 /* last 4 not wired to pins */
#define NOMADIK_GPIO_TO_IRQ(gpio)	((gpio) + NOMADIK_GPIO_OFFSET)
#define NOMADIK_IRQ_TO_GPIO(irq)	((irq) - NOMADIK_GPIO_OFFSET)
#define NR_IRQS				NOMADIK_GPIO_TO_IRQ(NOMADIK_NR_GPIO)

/* Following two are used by entry_macro.S, to access our dual-vic */
#define VIC_REG_IRQSR0		0
#define VIC_REG_IRQSR1		0x20

#endif /* __ASM_ARCH_IRQS_H */
