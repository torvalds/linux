/*
 * arch/arm/mach-spear6xx/include/mach/irqs.h
 *
 * IRQ helper macros for SPEAr6xx machine family
 *
 * Copyright (C) 2009 ST Microelectronics
 * Rajeev Kumar<rajeev-dlh.kumar@st.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef __MACH_IRQS_H
#define __MACH_IRQS_H

/* IRQ definitions */
/* VIC 1 */
#define IRQ_INTRCOMM_SW_IRQ			0
#define IRQ_INTRCOMM_CPU_1			1
#define IRQ_INTRCOMM_CPU_2			2
#define IRQ_INTRCOMM_RAS2A11_1			3
#define IRQ_INTRCOMM_RAS2A11_2			4
#define IRQ_INTRCOMM_RAS2A12_1			5
#define IRQ_INTRCOMM_RAS2A12_2			6
#define IRQ_GEN_RAS_0				7
#define IRQ_GEN_RAS_1				8
#define IRQ_GEN_RAS_2				9
#define IRQ_GEN_RAS_3				10
#define IRQ_GEN_RAS_4				11
#define IRQ_GEN_RAS_5				12
#define IRQ_GEN_RAS_6				13
#define IRQ_GEN_RAS_7				14
#define IRQ_GEN_RAS_8				15
#define IRQ_CPU_GPT1_1				16
#define IRQ_CPU_GPT1_2				17
#define IRQ_LOCAL_GPIO				18
#define IRQ_PLL_UNLOCK				19
#define IRQ_JPEG				20
#define IRQ_FSMC				21
#define IRQ_IRDA				22
#define IRQ_RESERVED				23
#define IRQ_UART_0				24
#define IRQ_UART_1				25
#define IRQ_SSP_1				26
#define IRQ_SSP_2				27
#define IRQ_I2C					28
#define IRQ_GEN_RAS_9				29
#define IRQ_GEN_RAS_10				30
#define IRQ_GEN_RAS_11				31

/* VIC 2 */
#define IRQ_APPL_GPT1_1				32
#define IRQ_APPL_GPT1_2				33
#define IRQ_APPL_GPT2_1				34
#define IRQ_APPL_GPT2_2				35
#define IRQ_APPL_GPIO				36
#define IRQ_APPL_SSP				37
#define IRQ_APPL_ADC				38
#define IRQ_APPL_RESERVED			39
#define IRQ_AHB_EXP_MASTER			40
#define IRQ_DDR_CONTROLLER			41
#define IRQ_BASIC_DMA				42
#define IRQ_BASIC_RESERVED1			43
#define IRQ_BASIC_SMI				44
#define IRQ_BASIC_CLCD				45
#define IRQ_EXP_AHB_1				46
#define IRQ_EXP_AHB_2				47
#define IRQ_BASIC_GPT1_1			48
#define IRQ_BASIC_GPT1_2			49
#define IRQ_BASIC_RTC				50
#define IRQ_BASIC_GPIO				51
#define IRQ_BASIC_WDT				52
#define IRQ_BASIC_RESERVED			53
#define IRQ_AHB_EXP_SLAVE			54
#define IRQ_GMAC_1				55
#define IRQ_GMAC_2				56
#define IRQ_USB_DEV				57
#define IRQ_USB_H_OHCI_0			58
#define IRQ_USB_H_EHCI_0			59
#define IRQ_USB_H_OHCI_1			60
#define IRQ_USB_H_EHCI_1			61
#define IRQ_EXP_AHB_3				62
#define IRQ_EXP_AHB_4				63

#define IRQ_VIC_END				64

/* GPIO pins virtual irqs */
#define SPEAR_GPIO_INT_BASE	IRQ_VIC_END
#define SPEAR_GPIO0_INT_BASE	SPEAR_GPIO_INT_BASE
#define SPEAR_GPIO1_INT_BASE	(SPEAR_GPIO0_INT_BASE + 8)
#define SPEAR_GPIO2_INT_BASE	(SPEAR_GPIO1_INT_BASE + 8)
#define SPEAR_GPIO_INT_END	(SPEAR_GPIO2_INT_BASE + 8)
#define VIRTUAL_IRQS		(SPEAR_GPIO_INT_END - IRQ_VIC_END)
#define NR_IRQS			(IRQ_VIC_END + VIRTUAL_IRQS)

#endif	/* __MACH_IRQS_H */
