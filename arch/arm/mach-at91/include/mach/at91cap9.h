/*
 * arch/arm/mach-at91/include/mach/at91cap9.h
 *
 *  Copyright (C) 2007 Stelian Pop <stelian.pop@leadtechdesign.com>
 *  Copyright (C) 2007 Lead Tech Design <www.leadtechdesign.com>
 *  Copyright (C) 2007 Atmel Corporation.
 *
 * Common definitions.
 * Based on AT91CAP9 datasheet revision B (Preliminary).
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef AT91CAP9_H
#define AT91CAP9_H

/*
 * Peripheral identifiers/interrupts.
 */
#define AT91CAP9_ID_PIOABCD	2	/* Parallel IO Controller A, B, C and D */
#define AT91CAP9_ID_MPB0	3	/* MP Block Peripheral 0 */
#define AT91CAP9_ID_MPB1	4	/* MP Block Peripheral 1 */
#define AT91CAP9_ID_MPB2	5	/* MP Block Peripheral 2 */
#define AT91CAP9_ID_MPB3	6	/* MP Block Peripheral 3 */
#define AT91CAP9_ID_MPB4	7	/* MP Block Peripheral 4 */
#define AT91CAP9_ID_US0		8	/* USART 0 */
#define AT91CAP9_ID_US1		9	/* USART 1 */
#define AT91CAP9_ID_US2		10	/* USART 2 */
#define AT91CAP9_ID_MCI0	11	/* Multimedia Card Interface 0 */
#define AT91CAP9_ID_MCI1	12	/* Multimedia Card Interface 1 */
#define AT91CAP9_ID_CAN		13	/* CAN */
#define AT91CAP9_ID_TWI		14	/* Two-Wire Interface */
#define AT91CAP9_ID_SPI0	15	/* Serial Peripheral Interface 0 */
#define AT91CAP9_ID_SPI1	16	/* Serial Peripheral Interface 0 */
#define AT91CAP9_ID_SSC0	17	/* Serial Synchronous Controller 0 */
#define AT91CAP9_ID_SSC1	18	/* Serial Synchronous Controller 1 */
#define AT91CAP9_ID_AC97C	19	/* AC97 Controller */
#define AT91CAP9_ID_TCB		20	/* Timer Counter 0, 1 and 2 */
#define AT91CAP9_ID_PWMC	21	/* Pulse Width Modulation Controller */
#define AT91CAP9_ID_EMAC	22	/* Ethernet */
#define AT91CAP9_ID_AESTDES	23	/* Advanced Encryption Standard, Triple DES */
#define AT91CAP9_ID_ADC		24	/* Analog-to-Digital Converter */
#define AT91CAP9_ID_ISI		25	/* Image Sensor Interface */
#define AT91CAP9_ID_LCDC	26	/* LCD Controller */
#define AT91CAP9_ID_DMA		27	/* DMA Controller */
#define AT91CAP9_ID_UDPHS	28	/* USB High Speed Device Port */
#define AT91CAP9_ID_UHP		29	/* USB Host Port */
#define AT91CAP9_ID_IRQ0	30	/* Advanced Interrupt Controller (IRQ0) */
#define AT91CAP9_ID_IRQ1	31	/* Advanced Interrupt Controller (IRQ1) */

/*
 * User Peripheral physical base addresses.
 */
#define AT91CAP9_BASE_UDPHS		0xfff78000
#define AT91CAP9_BASE_TCB0		0xfff7c000
#define AT91CAP9_BASE_TC0		0xfff7c000
#define AT91CAP9_BASE_TC1		0xfff7c040
#define AT91CAP9_BASE_TC2		0xfff7c080
#define AT91CAP9_BASE_MCI0		0xfff80000
#define AT91CAP9_BASE_MCI1		0xfff84000
#define AT91CAP9_BASE_TWI		0xfff88000
#define AT91CAP9_BASE_US0		0xfff8c000
#define AT91CAP9_BASE_US1		0xfff90000
#define AT91CAP9_BASE_US2		0xfff94000
#define AT91CAP9_BASE_SSC0		0xfff98000
#define AT91CAP9_BASE_SSC1		0xfff9c000
#define AT91CAP9_BASE_AC97C		0xfffa0000
#define AT91CAP9_BASE_SPI0		0xfffa4000
#define AT91CAP9_BASE_SPI1		0xfffa8000
#define AT91CAP9_BASE_CAN		0xfffac000
#define AT91CAP9_BASE_PWMC		0xfffb8000
#define AT91CAP9_BASE_EMAC		0xfffbc000
#define AT91CAP9_BASE_ADC		0xfffc0000
#define AT91CAP9_BASE_ISI		0xfffc4000

/*
 * System Peripherals (offset from AT91_BASE_SYS)
 */
#define AT91_ECC	(0xffffe200 - AT91_BASE_SYS)
#define AT91_BCRAMC	(0xffffe400 - AT91_BASE_SYS)
#define AT91_DDRSDRC0	(0xffffe600 - AT91_BASE_SYS)
#define AT91_SMC	(0xffffe800 - AT91_BASE_SYS)
#define AT91_MATRIX	(0xffffea00 - AT91_BASE_SYS)
#define AT91_CCFG	(0xffffeb10 - AT91_BASE_SYS)
#define AT91_DMA	(0xffffec00 - AT91_BASE_SYS)
#define AT91_DBGU	(0xffffee00 - AT91_BASE_SYS)
#define AT91_AIC	(0xfffff000 - AT91_BASE_SYS)
#define AT91_PIOA	(0xfffff200 - AT91_BASE_SYS)
#define AT91_PIOB	(0xfffff400 - AT91_BASE_SYS)
#define AT91_PIOC	(0xfffff600 - AT91_BASE_SYS)
#define AT91_PIOD	(0xfffff800 - AT91_BASE_SYS)
#define AT91_PMC	(0xfffffc00 - AT91_BASE_SYS)
#define AT91_RSTC	(0xfffffd00 - AT91_BASE_SYS)
#define AT91_SHDWC	(0xfffffd10 - AT91_BASE_SYS)
#define AT91_RTT	(0xfffffd20 - AT91_BASE_SYS)
#define AT91_PIT	(0xfffffd30 - AT91_BASE_SYS)
#define AT91_WDT	(0xfffffd40 - AT91_BASE_SYS)
#define AT91_GPBR	(cpu_is_at91cap9_revB() ?	\
			(0xfffffd50 - AT91_BASE_SYS) :	\
			(0xfffffd60 - AT91_BASE_SYS))

#define AT91_USART0	AT91CAP9_BASE_US0
#define AT91_USART1	AT91CAP9_BASE_US1
#define AT91_USART2	AT91CAP9_BASE_US2


/*
 * Internal Memory.
 */
#define AT91CAP9_SRAM_BASE	0x00100000	/* Internal SRAM base address */
#define AT91CAP9_SRAM_SIZE	(32 * SZ_1K)	/* Internal SRAM size (32Kb) */

#define AT91CAP9_ROM_BASE	0x00400000	/* Internal ROM base address */
#define AT91CAP9_ROM_SIZE	(32 * SZ_1K)	/* Internal ROM size (32Kb) */

#define AT91CAP9_LCDC_BASE	0x00500000	/* LCD Controller */
#define AT91CAP9_UDPHS_FIFO	0x00600000	/* USB High Speed Device Port */
#define AT91CAP9_UHP_BASE	0x00700000	/* USB Host controller */

#endif
