/*
 * arch/arm/mach-at91/include/mach/at91sam9260.h
 *
 * (C) 2006 Andrew Victor
 *
 * Common definitions.
 * Based on AT91SAM9260 datasheet revision A (Preliminary).
 *
 * Includes also definitions for AT91SAM9XE and AT91SAM9G families
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef AT91SAM9260_H
#define AT91SAM9260_H

/*
 * Peripheral identifiers/interrupts.
 */
#define AT91SAM9260_ID_PIOA	2	/* Parallel IO Controller A */
#define AT91SAM9260_ID_PIOB	3	/* Parallel IO Controller B */
#define AT91SAM9260_ID_PIOC	4	/* Parallel IO Controller C */
#define AT91SAM9260_ID_ADC	5	/* Analog-to-Digital Converter */
#define AT91SAM9260_ID_US0	6	/* USART 0 */
#define AT91SAM9260_ID_US1	7	/* USART 1 */
#define AT91SAM9260_ID_US2	8	/* USART 2 */
#define AT91SAM9260_ID_MCI	9	/* Multimedia Card Interface */
#define AT91SAM9260_ID_UDP	10	/* USB Device Port */
#define AT91SAM9260_ID_TWI	11	/* Two-Wire Interface */
#define AT91SAM9260_ID_SPI0	12	/* Serial Peripheral Interface 0 */
#define AT91SAM9260_ID_SPI1	13	/* Serial Peripheral Interface 1 */
#define AT91SAM9260_ID_SSC	14	/* Serial Synchronous Controller */
#define AT91SAM9260_ID_TC0	17	/* Timer Counter 0 */
#define AT91SAM9260_ID_TC1	18	/* Timer Counter 1 */
#define AT91SAM9260_ID_TC2	19	/* Timer Counter 2 */
#define AT91SAM9260_ID_UHP	20	/* USB Host port */
#define AT91SAM9260_ID_EMAC	21	/* Ethernet */
#define AT91SAM9260_ID_ISI	22	/* Image Sensor Interface */
#define AT91SAM9260_ID_US3	23	/* USART 3 */
#define AT91SAM9260_ID_US4	24	/* USART 4 */
#define AT91SAM9260_ID_US5	25	/* USART 5 */
#define AT91SAM9260_ID_TC3	26	/* Timer Counter 3 */
#define AT91SAM9260_ID_TC4	27	/* Timer Counter 4 */
#define AT91SAM9260_ID_TC5	28	/* Timer Counter 5 */
#define AT91SAM9260_ID_IRQ0	29	/* Advanced Interrupt Controller (IRQ0) */
#define AT91SAM9260_ID_IRQ1	30	/* Advanced Interrupt Controller (IRQ1) */
#define AT91SAM9260_ID_IRQ2	31	/* Advanced Interrupt Controller (IRQ2) */


/*
 * User Peripheral physical base addresses.
 */
#define AT91SAM9260_BASE_TCB0		0xfffa0000
#define AT91SAM9260_BASE_TC0		0xfffa0000
#define AT91SAM9260_BASE_TC1		0xfffa0040
#define AT91SAM9260_BASE_TC2		0xfffa0080
#define AT91SAM9260_BASE_UDP		0xfffa4000
#define AT91SAM9260_BASE_MCI		0xfffa8000
#define AT91SAM9260_BASE_TWI		0xfffac000
#define AT91SAM9260_BASE_US0		0xfffb0000
#define AT91SAM9260_BASE_US1		0xfffb4000
#define AT91SAM9260_BASE_US2		0xfffb8000
#define AT91SAM9260_BASE_SSC		0xfffbc000
#define AT91SAM9260_BASE_ISI		0xfffc0000
#define AT91SAM9260_BASE_EMAC		0xfffc4000
#define AT91SAM9260_BASE_SPI0		0xfffc8000
#define AT91SAM9260_BASE_SPI1		0xfffcc000
#define AT91SAM9260_BASE_US3		0xfffd0000
#define AT91SAM9260_BASE_US4		0xfffd4000
#define AT91SAM9260_BASE_US5		0xfffd8000
#define AT91SAM9260_BASE_TCB1		0xfffdc000
#define AT91SAM9260_BASE_TC3		0xfffdc000
#define AT91SAM9260_BASE_TC4		0xfffdc040
#define AT91SAM9260_BASE_TC5		0xfffdc080
#define AT91SAM9260_BASE_ADC		0xfffe0000

/*
 * System Peripherals (offset from AT91_BASE_SYS)
 */
#define AT91_ECC	(0xffffe800 - AT91_BASE_SYS)
#define AT91_SDRAMC0	(0xffffea00 - AT91_BASE_SYS)
#define AT91_SMC	(0xffffec00 - AT91_BASE_SYS)
#define AT91_MATRIX	(0xffffee00 - AT91_BASE_SYS)
#define AT91_CCFG	(0xffffef10 - AT91_BASE_SYS)
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

#define AT91_USART0	AT91SAM9260_BASE_US0
#define AT91_USART1	AT91SAM9260_BASE_US1
#define AT91_USART2	AT91SAM9260_BASE_US2
#define AT91_USART3	AT91SAM9260_BASE_US3
#define AT91_USART4	AT91SAM9260_BASE_US4
#define AT91_USART5	AT91SAM9260_BASE_US5


/*
 * Internal Memory.
 */
#define AT91SAM9260_ROM_BASE	0x00100000	/* Internal ROM base address */
#define AT91SAM9260_ROM_SIZE	SZ_32K		/* Internal ROM size (32Kb) */

#define AT91SAM9260_SRAM0_BASE	0x00200000	/* Internal SRAM 0 base address */
#define AT91SAM9260_SRAM0_SIZE	SZ_4K		/* Internal SRAM 0 size (4Kb) */
#define AT91SAM9260_SRAM1_BASE	0x00300000	/* Internal SRAM 1 base address */
#define AT91SAM9260_SRAM1_SIZE	SZ_4K		/* Internal SRAM 1 size (4Kb) */

#define AT91SAM9260_UHP_BASE	0x00500000	/* USB Host controller */

#define AT91SAM9XE_FLASH_BASE	0x00200000	/* Internal FLASH base address */
#define AT91SAM9XE_SRAM_BASE	0x00300000	/* Internal SRAM base address */

#define AT91SAM9G20_ROM_BASE	0x00100000	/* Internal ROM base address */
#define AT91SAM9G20_ROM_SIZE	SZ_32K		/* Internal ROM size (32Kb) */

#define AT91SAM9G20_SRAM0_BASE	0x00200000	/* Internal SRAM 0 base address */
#define AT91SAM9G20_SRAM0_SIZE	SZ_16K		/* Internal SRAM 0 size (16Kb) */
#define AT91SAM9G20_SRAM1_BASE	0x00300000	/* Internal SRAM 1 base address */
#define AT91SAM9G20_SRAM1_SIZE	SZ_16K		/* Internal SRAM 1 size (16Kb) */

#define AT91SAM9G20_UHP_BASE	0x00500000	/* USB Host controller */

#endif
