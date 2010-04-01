/*
 * arch/arm/mach-spear3xx/include/mach/irqs.h
 *
 * IRQ helper macros for SPEAr3xx machine family
 *
 * Copyright (C) 2009 ST Microelectronics
 * Viresh Kumar<viresh.kumar@st.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef __MACH_IRQS_H
#define __MACH_IRQS_H

/* IRQ definitions */
#define IRQ_HW_ACCEL_MOD_0			0
#define IRQ_INTRCOMM_RAS_ARM			1
#define IRQ_CPU_GPT1_1				2
#define IRQ_CPU_GPT1_2				3
#define IRQ_BASIC_GPT1_1			4
#define IRQ_BASIC_GPT1_2			5
#define IRQ_BASIC_GPT2_1			6
#define IRQ_BASIC_GPT2_2			7
#define IRQ_BASIC_DMA				8
#define IRQ_BASIC_SMI				9
#define IRQ_BASIC_RTC				10
#define IRQ_BASIC_GPIO				11
#define IRQ_BASIC_WDT				12
#define IRQ_DDR_CONTROLLER			13
#define IRQ_SYS_ERROR				14
#define IRQ_WAKEUP_RCV				15
#define IRQ_JPEG				16
#define IRQ_IRDA				17
#define IRQ_ADC					18
#define IRQ_UART				19
#define IRQ_SSP					20
#define IRQ_I2C					21
#define IRQ_MAC_1				22
#define IRQ_MAC_2				23
#define IRQ_USB_DEV				24
#define IRQ_USB_H_OHCI_0			25
#define IRQ_USB_H_EHCI_0			26
#define IRQ_USB_H_EHCI_1			IRQ_USB_H_EHCI_0
#define IRQ_USB_H_OHCI_1			27
#define IRQ_GEN_RAS_1				28
#define IRQ_GEN_RAS_2				29
#define IRQ_GEN_RAS_3				30
#define IRQ_HW_ACCEL_MOD_1			31
#define IRQ_VIC_END				32

#define SPEAR_GPIO_INT_BASE	IRQ_VIC_END

#ifdef CONFIG_MACH_SPEAR300
#define SPEAR_GPIO1_INT_BASE	(SPEAR_GPIO_INT_BASE + 8)
#define SPEAR_GPIO_INT_END	(SPEAR_GPIO1_INT_BASE + 8)
#else
#define SPEAR_GPIO_INT_END	(SPEAR_GPIO_INT_BASE + 8)
#endif

#define VIRTUAL_IRQS		(SPEAR_GPIO_INT_END - IRQ_VIC_END)
#define NR_IRQS			(IRQ_VIC_END + VIRTUAL_IRQS)

#endif /* __MACH_IRQS_H */
