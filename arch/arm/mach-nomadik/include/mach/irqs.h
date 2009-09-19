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

#define IRQ_VIC_START		0	/* first VIC interrupt is 0 */

/*
 * Interrupt numbers generic for all Nomadik Chip cuts
 */
#define IRQ_WATCHDOG			0
#define IRQ_SOFTINT			1
#define IRQ_CRYPTO			2
#define IRQ_OWM				3
#define IRQ_MTU0			4
#define IRQ_MTU1			5
#define IRQ_GPIO0			6
#define IRQ_GPIO1			7
#define IRQ_GPIO2			8
#define IRQ_GPIO3			9
#define IRQ_RTC_RTT			10
#define IRQ_SSP				11
#define IRQ_UART0			12
#define IRQ_DMA1			13
#define IRQ_CLCD_MDIF			14
#define IRQ_DMA0			15
#define IRQ_PWRFAIL			16
#define IRQ_UART1			17
#define IRQ_FIRDA			18
#define IRQ_MSP0			19
#define IRQ_I2C0			20
#define IRQ_I2C1			21
#define IRQ_SDMMC			22
#define IRQ_USBOTG			23
#define IRQ_SVA_IT0			24
#define IRQ_SVA_IT1			25
#define IRQ_SAA_IT0			26
#define IRQ_SAA_IT1			27
#define IRQ_UART2			28
#define IRQ_MSP2			31
#define IRQ_L2CC			48
#define IRQ_HPI				49
#define IRQ_SKE				50
#define IRQ_KP				51
#define IRQ_MEMST			54
#define IRQ_SGA_IT			58
#define IRQ_USBM			60
#define IRQ_MSP1			62

#define NOMADIK_SOC_NR_IRQS		64

/* After chip-specific IRQ numbers we have the GPIO ones */
#define NOMADIK_NR_GPIO			128 /* last 4 not wired to pins */
#define NOMADIK_GPIO_TO_IRQ(gpio)	((gpio) + NOMADIK_SOC_NR_IRQS)
#define NOMADIK_IRQ_TO_GPIO(irq)	((irq) - NOMADIK_SOC_NR_IRQS)
#define NR_IRQS				NOMADIK_GPIO_TO_IRQ(NOMADIK_NR_GPIO)

/* Following two are used by entry_macro.S, to access our dual-vic */
#define VIC_REG_IRQSR0		0
#define VIC_REG_IRQSR1		0x20

#endif /* __ASM_ARCH_IRQS_H */

