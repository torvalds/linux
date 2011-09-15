/*
 * arch/arm/mach-at91/include/mach/at91sam9261.h
 *
 * Copyright (C) SAN People
 *
 * Common definitions.
 * Based on AT91SAM9261 datasheet revision E. (Preliminary)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef AT91SAM9261_H
#define AT91SAM9261_H

/*
 * Peripheral identifiers/interrupts.
 */
#define AT91SAM9261_ID_PIOA	2	/* Parallel IO Controller A */
#define AT91SAM9261_ID_PIOB	3	/* Parallel IO Controller B */
#define AT91SAM9261_ID_PIOC	4	/* Parallel IO Controller C */
#define AT91SAM9261_ID_US0	6	/* USART 0 */
#define AT91SAM9261_ID_US1	7	/* USART 1 */
#define AT91SAM9261_ID_US2	8	/* USART 2 */
#define AT91SAM9261_ID_MCI	9	/* Multimedia Card Interface */
#define AT91SAM9261_ID_UDP	10	/* USB Device Port */
#define AT91SAM9261_ID_TWI	11	/* Two-Wire Interface */
#define AT91SAM9261_ID_SPI0	12	/* Serial Peripheral Interface 0 */
#define AT91SAM9261_ID_SPI1	13	/* Serial Peripheral Interface 1 */
#define AT91SAM9261_ID_SSC0	14	/* Serial Synchronous Controller 0 */
#define AT91SAM9261_ID_SSC1	15	/* Serial Synchronous Controller 1 */
#define AT91SAM9261_ID_SSC2	16	/* Serial Synchronous Controller 2 */
#define AT91SAM9261_ID_TC0	17	/* Timer Counter 0 */
#define AT91SAM9261_ID_TC1	18	/* Timer Counter 1 */
#define AT91SAM9261_ID_TC2	19	/* Timer Counter 2 */
#define AT91SAM9261_ID_UHP	20	/* USB Host port */
#define AT91SAM9261_ID_LCDC	21	/* LDC Controller */
#define AT91SAM9261_ID_IRQ0	29	/* Advanced Interrupt Controller (IRQ0) */
#define AT91SAM9261_ID_IRQ1	30	/* Advanced Interrupt Controller (IRQ1) */
#define AT91SAM9261_ID_IRQ2	31	/* Advanced Interrupt Controller (IRQ2) */


/*
 * User Peripheral physical base addresses.
 */
#define AT91SAM9261_BASE_TCB0		0xfffa0000
#define AT91SAM9261_BASE_TC0		0xfffa0000
#define AT91SAM9261_BASE_TC1		0xfffa0040
#define AT91SAM9261_BASE_TC2		0xfffa0080
#define AT91SAM9261_BASE_UDP		0xfffa4000
#define AT91SAM9261_BASE_MCI		0xfffa8000
#define AT91SAM9261_BASE_TWI		0xfffac000
#define AT91SAM9261_BASE_US0		0xfffb0000
#define AT91SAM9261_BASE_US1		0xfffb4000
#define AT91SAM9261_BASE_US2		0xfffb8000
#define AT91SAM9261_BASE_SSC0		0xfffbc000
#define AT91SAM9261_BASE_SSC1		0xfffc0000
#define AT91SAM9261_BASE_SSC2		0xfffc4000
#define AT91SAM9261_BASE_SPI0		0xfffc8000
#define AT91SAM9261_BASE_SPI1		0xfffcc000


/*
 * System Peripherals (offset from AT91_BASE_SYS)
 */
#define AT91_SDRAMC0	(0xffffea00 - AT91_BASE_SYS)
#define AT91_SMC	(0xffffec00 - AT91_BASE_SYS)
#define AT91_MATRIX	(0xffffee00 - AT91_BASE_SYS)
#define AT91_AIC	(0xfffff000 - AT91_BASE_SYS)
#define AT91_DBGU	(0xfffff200 - AT91_BASE_SYS)
#define AT91_PIOA	(0xfffff400 - AT91_BASE_SYS)
#define AT91_PIOB	(0xfffff600 - AT91_BASE_SYS)
#define AT91_PIOC	(0xfffff800 - AT91_BASE_SYS)
#define AT91_PMC	(0xfffffc00 - AT91_BASE_SYS)
#define AT91_RSTC	(0xfffffd00 - AT91_BASE_SYS)
#define AT91_SHDWC	(0xfffffd10 - AT91_BASE_SYS)
#define AT91_RTT	(0xfffffd20 - AT91_BASE_SYS)
#define AT91_PIT	(0xfffffd30 - AT91_BASE_SYS)
#define AT91_WDT	(0xfffffd40 - AT91_BASE_SYS)
#define AT91_GPBR	(0xfffffd50 - AT91_BASE_SYS)

#define AT91_USART0	AT91SAM9261_BASE_US0
#define AT91_USART1	AT91SAM9261_BASE_US1
#define AT91_USART2	AT91SAM9261_BASE_US2


/*
 * Internal Memory.
 */
#define AT91SAM9261_SRAM_BASE	0x00300000	/* Internal SRAM base address */
#define AT91SAM9261_SRAM_SIZE	0x00028000	/* Internal SRAM size (160Kb) */

#define AT91SAM9G10_SRAM_BASE	AT91SAM9261_SRAM_BASE	/* Internal SRAM base address */
#define AT91SAM9G10_SRAM_SIZE	0x00004000	/* Internal SRAM size (16Kb) */

#define AT91SAM9261_ROM_BASE	0x00400000	/* Internal ROM base address */
#define AT91SAM9261_ROM_SIZE	SZ_32K		/* Internal ROM size (32Kb) */

#define AT91SAM9261_UHP_BASE	0x00500000	/* USB Host controller */
#define AT91SAM9261_LCDC_BASE	0x00600000	/* LDC controller */


#endif
