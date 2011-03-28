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

/* SPEAr3xx IRQ definitions */
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

#define VIRQ_START				IRQ_VIC_END

/* SPEAr300 Virtual irq definitions */
#ifdef CONFIG_MACH_SPEAR300
/* IRQs sharing IRQ_GEN_RAS_1 */
#define VIRQ_IT_PERS_S				(VIRQ_START + 0)
#define VIRQ_IT_CHANGE_S			(VIRQ_START + 1)
#define VIRQ_I2S				(VIRQ_START + 2)
#define VIRQ_TDM				(VIRQ_START + 3)
#define VIRQ_CAMERA_L				(VIRQ_START + 4)
#define VIRQ_CAMERA_F				(VIRQ_START + 5)
#define VIRQ_CAMERA_V				(VIRQ_START + 6)
#define VIRQ_KEYBOARD				(VIRQ_START + 7)
#define VIRQ_GPIO1				(VIRQ_START + 8)

/* IRQs sharing IRQ_GEN_RAS_3 */
#define IRQ_CLCD				IRQ_GEN_RAS_3

/* IRQs sharing IRQ_INTRCOMM_RAS_ARM */
#define IRQ_SDHCI				IRQ_INTRCOMM_RAS_ARM

/* GPIO pins virtual irqs */
#define SPEAR_GPIO_INT_BASE			(VIRQ_START + 9)
#define SPEAR_GPIO1_INT_BASE			(SPEAR_GPIO_INT_BASE + 8)
#define SPEAR_GPIO_INT_END			(SPEAR_GPIO1_INT_BASE + 8)

/* SPEAr310 Virtual irq definitions */
#elif defined(CONFIG_MACH_SPEAR310)
/* IRQs sharing IRQ_GEN_RAS_1 */
#define VIRQ_SMII0				(VIRQ_START + 0)
#define VIRQ_SMII1				(VIRQ_START + 1)
#define VIRQ_SMII2				(VIRQ_START + 2)
#define VIRQ_SMII3				(VIRQ_START + 3)
#define VIRQ_WAKEUP_SMII0			(VIRQ_START + 4)
#define VIRQ_WAKEUP_SMII1			(VIRQ_START + 5)
#define VIRQ_WAKEUP_SMII2			(VIRQ_START + 6)
#define VIRQ_WAKEUP_SMII3			(VIRQ_START + 7)

/* IRQs sharing IRQ_GEN_RAS_2 */
#define VIRQ_UART1				(VIRQ_START + 8)
#define VIRQ_UART2				(VIRQ_START + 9)
#define VIRQ_UART3				(VIRQ_START + 10)
#define VIRQ_UART4				(VIRQ_START + 11)
#define VIRQ_UART5				(VIRQ_START + 12)

/* IRQs sharing IRQ_GEN_RAS_3 */
#define VIRQ_EMI				(VIRQ_START + 13)
#define VIRQ_PLGPIO				(VIRQ_START + 14)

/* IRQs sharing IRQ_INTRCOMM_RAS_ARM */
#define VIRQ_TDM_HDLC				(VIRQ_START + 15)
#define VIRQ_RS485_0				(VIRQ_START + 16)
#define VIRQ_RS485_1				(VIRQ_START + 17)

/* GPIO pins virtual irqs */
#define SPEAR_GPIO_INT_BASE			(VIRQ_START + 18)

/* SPEAr320 Virtual irq definitions */
#else
/* IRQs sharing IRQ_GEN_RAS_1 */
#define VIRQ_EMI				(VIRQ_START + 0)
#define VIRQ_CLCD				(VIRQ_START + 1)
#define VIRQ_SPP				(VIRQ_START + 2)

/* IRQs sharing IRQ_GEN_RAS_2 */
#define IRQ_SDHCI				IRQ_GEN_RAS_2

/* IRQs sharing IRQ_GEN_RAS_3 */
#define VIRQ_PLGPIO				(VIRQ_START + 3)
#define VIRQ_I2S_PLAY				(VIRQ_START + 4)
#define VIRQ_I2S_REC				(VIRQ_START + 5)

/* IRQs sharing IRQ_INTRCOMM_RAS_ARM */
#define VIRQ_CANU				(VIRQ_START + 6)
#define VIRQ_CANL				(VIRQ_START + 7)
#define VIRQ_UART1				(VIRQ_START + 8)
#define VIRQ_UART2				(VIRQ_START + 9)
#define VIRQ_SSP1				(VIRQ_START + 10)
#define VIRQ_SSP2				(VIRQ_START + 11)
#define VIRQ_SMII0				(VIRQ_START + 12)
#define VIRQ_MII1_SMII1				(VIRQ_START + 13)
#define VIRQ_WAKEUP_SMII0			(VIRQ_START + 14)
#define VIRQ_WAKEUP_MII1_SMII1			(VIRQ_START + 15)
#define VIRQ_I2C				(VIRQ_START + 16)

/* GPIO pins virtual irqs */
#define SPEAR_GPIO_INT_BASE			(VIRQ_START + 17)

#endif

/* PLGPIO Virtual IRQs */
#if defined(CONFIG_MACH_SPEAR310) || defined(CONFIG_MACH_SPEAR320)
#define SPEAR_PLGPIO_INT_BASE			(SPEAR_GPIO_INT_BASE + 8)
#define SPEAR_GPIO_INT_END			(SPEAR_PLGPIO_INT_BASE + 102)
#endif

#define VIRQ_END				SPEAR_GPIO_INT_END
#define NR_IRQS					VIRQ_END

#endif /* __MACH_IRQS_H */
