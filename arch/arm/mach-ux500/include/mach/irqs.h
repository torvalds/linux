/*
 *  Copyright (C) 2008 STMicroelectronics
 *  Copyright (C) 2009 ST-Ericsson.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#ifndef ASM_ARCH_IRQS_H
#define ASM_ARCH_IRQS_H

#include <mach/irqs-db5500.h>
#include <mach/irqs-db8500.h>

#define IRQ_LOCALTIMER                  29
#define IRQ_LOCALWDOG                   30

/* Shared Peripheral Interrupt (SHPI) */
#define IRQ_SHPI_START			32

/* Interrupt numbers generic for shared peripheral */
#define IRQ_MTU0		(IRQ_SHPI_START + 4)
#define IRQ_SPI2		(IRQ_SHPI_START + 6)
#define IRQ_SPI0		(IRQ_SHPI_START + 8)
#define IRQ_UART0		(IRQ_SHPI_START + 11)
#define IRQ_I2C3		(IRQ_SHPI_START + 12)
#define IRQ_SSP0		(IRQ_SHPI_START + 14)
#define IRQ_MTU1		(IRQ_SHPI_START + 17)
#define IRQ_RTC_RTT		(IRQ_SHPI_START + 18)
#define IRQ_UART1		(IRQ_SHPI_START + 19)
#define IRQ_I2C0		(IRQ_SHPI_START + 21)
#define IRQ_I2C1		(IRQ_SHPI_START + 22)
#define IRQ_USBOTG		(IRQ_SHPI_START + 23)
#define IRQ_DMA			(IRQ_SHPI_START + 25)
#define IRQ_UART2		(IRQ_SHPI_START + 26)
#define IRQ_HSIR_EXCEP		(IRQ_SHPI_START + 29)
#define IRQ_MSP0		(IRQ_SHPI_START + 31)
#define IRQ_HSIR_CH0_OVRRUN	(IRQ_SHPI_START + 32)
#define IRQ_HSIR_CH1_OVRRUN	(IRQ_SHPI_START + 33)
#define IRQ_HSIR_CH2_OVRRUN	(IRQ_SHPI_START + 34)
#define IRQ_HSIR_CH3_OVRRUN	(IRQ_SHPI_START + 35)
#define IRQ_AB4500		(IRQ_SHPI_START + 40)
#define IRQ_DISP		(IRQ_SHPI_START + 48)
#define IRQ_SiPI3		(IRQ_SHPI_START + 49)
#define IRQ_I2C4		(IRQ_SHPI_START + 51)
#define IRQ_SSP1		(IRQ_SHPI_START + 52)
#define IRQ_I2C2		(IRQ_SHPI_START + 55)
#define IRQ_SDMMC0		(IRQ_SHPI_START + 60)
#define IRQ_MSP1		(IRQ_SHPI_START + 62)
#define IRQ_SPI1		(IRQ_SHPI_START + 96)
#define IRQ_MSP2		(IRQ_SHPI_START + 98)
#define IRQ_SDMMC4		(IRQ_SHPI_START + 99)
#define IRQ_HSIRD0		(IRQ_SHPI_START + 104)
#define IRQ_HSIRD1		(IRQ_SHPI_START + 105)
#define IRQ_HSITD0		(IRQ_SHPI_START + 106)
#define IRQ_HSITD1		(IRQ_SHPI_START + 107)
#define IRQ_GPIO0		(IRQ_SHPI_START + 119)
#define IRQ_GPIO1		(IRQ_SHPI_START + 120)
#define IRQ_GPIO2		(IRQ_SHPI_START + 121)
#define IRQ_GPIO3		(IRQ_SHPI_START + 122)
#define IRQ_GPIO4		(IRQ_SHPI_START + 123)
#define IRQ_GPIO5		(IRQ_SHPI_START + 124)
#define IRQ_GPIO6		(IRQ_SHPI_START + 125)
#define IRQ_GPIO7		(IRQ_SHPI_START + 126)
#define IRQ_GPIO8		(IRQ_SHPI_START + 127)

/* There are 128 shared peripheral interrupts assigned to
 * INTID[160:32]. The first 32 interrupts are reserved.
 */
#define DBX500_NR_INTERNAL_IRQS		161

/* After chip-specific IRQ numbers we have the GPIO ones */
#define NOMADIK_NR_GPIO			288
#define NOMADIK_GPIO_TO_IRQ(gpio)	((gpio) + DBX500_NR_INTERNAL_IRQS)
#define NOMADIK_IRQ_TO_GPIO(irq)	((irq) - DBX500_NR_INTERNAL_IRQS)
#define IRQ_BOARD_START			NOMADIK_GPIO_TO_IRQ(NOMADIK_NR_GPIO)

/* This will be overridden by board-specific irq headers */
#define IRQ_BOARD_END			IRQ_BOARD_START

#ifdef CONFIG_MACH_U8500_MOP
#include <mach/irqs-board-mop500.h>
#endif

#define NR_IRQS				IRQ_BOARD_END

#endif /* ASM_ARCH_IRQS_H */
