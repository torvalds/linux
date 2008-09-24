/*
 *  arch/arm/mach-aaec2000/include/mach/aaec2000.h
 *
 *  AAEC-2000 registers definition
 *
 *  Copyright (c) 2005 Nicolas Bellido Y Ortega
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#ifndef __ASM_ARCH_AAEC2000_H
#define __ASM_ARCH_AAEC2000_H

#ifndef __ASM_ARCH_HARDWARE_H
#error You must include hardware.h not this file
#endif /* __ASM_ARCH_HARDWARE_H */

/* Chip selects */
#define AAEC_CS0	0x00000000
#define AAEC_CS1	0x10000000
#define AAEC_CS2	0x20000000
#define AAEC_CS3	0x30000000

/* Flash */
#define AAEC_FLASH_BASE	AAEC_CS0
#define AAEC_FLASH_SIZE	SZ_64M

/* Interrupt controller */
#define IRQ_BASE	__REG(0x80000500)
#define IRQ_INTSR	__REG(0x80000500)	/* Int Status Register */
#define IRQ_INTRSR	__REG(0x80000504)	/* Int Raw (unmasked) Status */
#define IRQ_INTENS	__REG(0x80000508)	/* Int Enable Set */
#define IRQ_INTENC	__REG(0x8000050c)	/* Int Enable Clear */

/* UART 1 */
#define UART1_BASE	__REG(0x80000600)
#define UART1_DR	__REG(0x80000600) /* Data/FIFO Register */
#define UART1_LCR	__REG(0x80000604) /* Link Control Register */
#define UART1_BRCR	__REG(0x80000608) /* Baud Rate Control Register */
#define UART1_CR	__REG(0x8000060c) /* Control Register */
#define UART1_SR	__REG(0x80000610) /* Status Register */
#define UART1_INT	__REG(0x80000614) /* Interrupt Status Register */
#define UART1_INTM	__REG(0x80000618) /* Interrupt Mask Register */
#define UART1_INTRES	__REG(0x8000061c) /* Int Result (masked status) Register */

/* UART 2 */
#define UART2_BASE	__REG(0x80000700)
#define UART2_DR	__REG(0x80000700) /* Data/FIFO Register */
#define UART2_LCR	__REG(0x80000704) /* Link Control Register */
#define UART2_BRCR	__REG(0x80000708) /* Baud Rate Control Register */
#define UART2_CR	__REG(0x8000070c) /* Control Register */
#define UART2_SR	__REG(0x80000710) /* Status Register */
#define UART2_INT	__REG(0x80000714) /* Interrupt Status Register */
#define UART2_INTM	__REG(0x80000718) /* Interrupt Mask Register */
#define UART2_INTRES	__REG(0x8000071c) /* Int Result (masked status) Register */

/* UART 3 */
#define UART3_BASE	__REG(0x80000800)
#define UART3_DR	__REG(0x80000800) /* Data/FIFO Register */
#define UART3_LCR	__REG(0x80000804) /* Link Control Register */
#define UART3_BRCR	__REG(0x80000808) /* Baud Rate Control Register */
#define UART3_CR	__REG(0x8000080c) /* Control Register */
#define UART3_SR	__REG(0x80000810) /* Status Register */
#define UART3_INT	__REG(0x80000814) /* Interrupt Status Register */
#define UART3_INTM	__REG(0x80000818) /* Interrupt Mask Register */
#define UART3_INTRES	__REG(0x8000081c) /* Int Result (masked status) Register */

/* These are used in some places */
#define _UART1_BASE __PREG(UART1_BASE)
#define _UART2_BASE __PREG(UART2_BASE)
#define _UART3_BASE __PREG(UART3_BASE)

/* UART Registers Offsets */
#define UART_DR		0x00
#define UART_LCR	0x04
#define UART_BRCR	0x08
#define UART_CR		0x0c
#define UART_SR		0x10
#define UART_INT	0x14
#define UART_INTM	0x18
#define UART_INTRES	0x1c

/* UART_LCR Bitmask */
#define UART_LCR_BRK	(1 << 0) /* Send Break */
#define UART_LCR_PEN	(1 << 1) /* Parity Enable */
#define UART_LCR_EP	(1 << 2) /* Even/Odd Parity */
#define UART_LCR_S2	(1 << 3) /* One/Two Stop bits */
#define UART_LCR_FIFO	(1 << 4) /* FIFO Enable */
#define UART_LCR_WL5	(0 << 5) /* Word Length - 5 bits */
#define UART_LCR_WL6	(1 << 5) /* Word Length - 6 bits */
#define UART_LCR_WL7	(1 << 6) /* Word Length - 7 bits */
#define UART_LCR_WL8	(1 << 7) /* Word Length - 8 bits */

/* UART_CR Bitmask */
#define UART_CR_EN	(1 << 0) /* UART Enable */
#define UART_CR_SIR	(1 << 1) /* IrDA SIR Enable */
#define UART_CR_SIRLP	(1 << 2) /* Low Power IrDA Enable */
#define UART_CR_RXP	(1 << 3) /* Receive Pin Polarity */
#define UART_CR_TXP	(1 << 4) /* Transmit Pin Polarity */
#define UART_CR_MXP	(1 << 5) /* Modem Pin Polarity */
#define UART_CR_LOOP	(1 << 6) /* Loopback Mode */

/* UART_SR Bitmask */
#define UART_SR_CTS	(1 << 0) /* Clear To Send Status */
#define UART_SR_DSR	(1 << 1) /* Data Set Ready Status */
#define UART_SR_DCD	(1 << 2) /* Data Carrier Detect Status */
#define UART_SR_TxBSY	(1 << 3) /* Transmitter Busy Status */
#define UART_SR_RxFE	(1 << 4) /* Receive FIFO Empty Status */
#define UART_SR_TxFF	(1 << 5) /* Transmit FIFO Full Status */
#define UART_SR_RxFF	(1 << 6) /* Receive FIFO Full Status */
#define UART_SR_TxFE	(1 << 7) /* Transmit FIFO Empty Status */

/* UART_INT Bitmask */
#define UART_INT_RIS	(1 << 0) /* Rx Interrupt */
#define UART_INT_TIS	(1 << 1) /* Tx Interrupt */
#define UART_INT_MIS	(1 << 2) /* Modem Interrupt */
#define UART_INT_RTIS	(1 << 3) /* Receive Timeout Interrupt */

/* Timer 1 */
#define TIMER1_BASE	__REG(0x80000c00)
#define TIMER1_LOAD	__REG(0x80000c00)	/* Timer 1 Load Register */
#define TIMER1_VAL	__REG(0x80000c04)	/* Timer 1 Value Register */
#define TIMER1_CTRL	__REG(0x80000c08)	/* Timer 1 Control Register */
#define TIMER1_CLEAR	__REG(0x80000c0c)	/* Timer 1 Clear Register */

/* Timer 2 */
#define TIMER2_BASE	__REG(0x80000d00)
#define TIMER2_LOAD	__REG(0x80000d00)	/* Timer 2 Load Register */
#define TIMER2_VAL	__REG(0x80000d04)	/* Timer 2 Value Register */
#define TIMER2_CTRL	__REG(0x80000d08)	/* Timer 2 Control Register */
#define TIMER2_CLEAR	__REG(0x80000d0c)	/* Timer 2 Clear Register */

/* Timer 3 */
#define TIMER3_BASE	__REG(0x80000e00)
#define TIMER3_LOAD	__REG(0x80000e00)	/* Timer 3 Load Register */
#define TIMER3_VAL	__REG(0x80000e04)	/* Timer 3 Value Register */
#define TIMER3_CTRL	__REG(0x80000e08)	/* Timer 3 Control Register */
#define TIMER3_CLEAR	__REG(0x80000e0c)	/* Timer 3 Clear Register */

/* Timer Control register bits */
#define TIMER_CTRL_ENABLE	(1 << 7) /* Enable (Start Timer) */
#define TIMER_CTRL_PERIODIC	(1 << 6) /* Periodic Running Mode */
#define TIMER_CTRL_FREE_RUNNING (0 << 6) /* Normal Running Mode */
#define TIMER_CTRL_CLKSEL_508K	(1 << 3) /* 508KHz Clock select (Timer 1, 2) */
#define TIMER_CTRL_CLKSEL_2K	(0 << 3) /* 2KHz Clock Select (Timer 1, 2) */

/* Power and State Control */
#define POWER_BASE	__REG(0x80000400)
#define POWER_PWRSR	__REG(0x80000400) /* Power Status Register */
#define POWER_PWRCNT	__REG(0x80000404) /* Power/Clock control */
#define POWER_HALT	__REG(0x80000408) /* Power Idle Mode */
#define POWER_STDBY	__REG(0x8000040c) /* Power Standby Mode */
#define POWER_BLEOI	__REG(0x80000410) /* Battery Low End of Interrupt */
#define POWER_MCEOI	__REG(0x80000414) /* Media Changed EoI */
#define POWER_TEOI	__REG(0x80000418) /* Tick EoI */
#define POWER_STFCLR	__REG(0x8000041c) /* NbFlg, RSTFlg, PFFlg, CLDFlg Clear */
#define POWER_CLKSET	__REG(0x80000420) /* Clock Speed Control */

/* GPIO Registers */
#define AAEC_GPIO_PHYS	0x80000e00

#define AAEC_GPIO_PADR		__REG(AAEC_GPIO_PHYS + 0x00)
#define AAEC_GPIO_PBDR		__REG(AAEC_GPIO_PHYS + 0x04)
#define AAEC_GPIO_PCDR		__REG(AAEC_GPIO_PHYS + 0x08)
#define AAEC_GPIO_PDDR		__REG(AAEC_GPIO_PHYS + 0x0c)
#define AAEC_GPIO_PADDR		__REG(AAEC_GPIO_PHYS + 0x10)
#define AAEC_GPIO_PBDDR		__REG(AAEC_GPIO_PHYS + 0x14)
#define AAEC_GPIO_PCDDR		__REG(AAEC_GPIO_PHYS + 0x18)
#define AAEC_GPIO_PDDDR		__REG(AAEC_GPIO_PHYS + 0x1c)
#define AAEC_GPIO_PEDR		__REG(AAEC_GPIO_PHYS + 0x20)
#define AAEC_GPIO_PEDDR		__REG(AAEC_GPIO_PHYS + 0x24)
#define AAEC_GPIO_KSCAN		__REG(AAEC_GPIO_PHYS + 0x28)
#define AAEC_GPIO_PINMUX	__REG(AAEC_GPIO_PHYS + 0x2c)
#define AAEC_GPIO_PFDR		__REG(AAEC_GPIO_PHYS + 0x30)
#define AAEC_GPIO_PFDDR		__REG(AAEC_GPIO_PHYS + 0x34)
#define AAEC_GPIO_PGDR		__REG(AAEC_GPIO_PHYS + 0x38)
#define AAEC_GPIO_PGDDR		__REG(AAEC_GPIO_PHYS + 0x3c)
#define AAEC_GPIO_PHDR		__REG(AAEC_GPIO_PHYS + 0x40)
#define AAEC_GPIO_PHDDR		__REG(AAEC_GPIO_PHYS + 0x44)
#define AAEC_GPIO_RAZ		__REG(AAEC_GPIO_PHYS + 0x48)
#define AAEC_GPIO_INTTYPE1	__REG(AAEC_GPIO_PHYS + 0x4c)
#define AAEC_GPIO_INTTYPE2	__REG(AAEC_GPIO_PHYS + 0x50)
#define AAEC_GPIO_FEOI		__REG(AAEC_GPIO_PHYS + 0x54)
#define AAEC_GPIO_INTEN		__REG(AAEC_GPIO_PHYS + 0x58)
#define AAEC_GPIO_INTSTATUS	__REG(AAEC_GPIO_PHYS + 0x5c)
#define AAEC_GPIO_RAWINTSTATUS	__REG(AAEC_GPIO_PHYS + 0x60)
#define AAEC_GPIO_DB		__REG(AAEC_GPIO_PHYS + 0x64)
#define AAEC_GPIO_PAPINDR	__REG(AAEC_GPIO_PHYS + 0x68)
#define AAEC_GPIO_PBPINDR	__REG(AAEC_GPIO_PHYS + 0x6c)
#define AAEC_GPIO_PCPINDR	__REG(AAEC_GPIO_PHYS + 0x70)
#define AAEC_GPIO_PDPINDR	__REG(AAEC_GPIO_PHYS + 0x74)
#define AAEC_GPIO_PEPINDR	__REG(AAEC_GPIO_PHYS + 0x78)
#define AAEC_GPIO_PFPINDR	__REG(AAEC_GPIO_PHYS + 0x7c)
#define AAEC_GPIO_PGPINDR	__REG(AAEC_GPIO_PHYS + 0x80)
#define AAEC_GPIO_PHPINDR	__REG(AAEC_GPIO_PHYS + 0x84)

#define AAEC_GPIO_PINMUX_PE0CON		(1 << 0)
#define AAEC_GPIO_PINMUX_PD0CON		(1 << 1)
#define AAEC_GPIO_PINMUX_CODECON	(1 << 2)
#define AAEC_GPIO_PINMUX_UART3CON	(1 << 3)

/* LCD Controller */
#define AAEC_CLCD_PHYS	0x80003000

#endif /* __ARM_ARCH_AAEC2000_H */
