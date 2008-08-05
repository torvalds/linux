/*
 * arch/arm/mach-at91/include/mach/at91sam9263.h
 *
 * (C) 2007 Atmel Corporation.
 *
 * Common definitions.
 * Based on AT91SAM9263 datasheet revision B (Preliminary).
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef AT91SAM9263_H
#define AT91SAM9263_H

/*
 * Peripheral identifiers/interrupts.
 */
#define AT91_ID_FIQ		0	/* Advanced Interrupt Controller (FIQ) */
#define AT91_ID_SYS		1	/* System Peripherals */
#define AT91SAM9263_ID_PIOA	2	/* Parallel IO Controller A */
#define AT91SAM9263_ID_PIOB	3	/* Parallel IO Controller B */
#define AT91SAM9263_ID_PIOCDE	4	/* Parallel IO Controller C, D and E */
#define AT91SAM9263_ID_US0	7	/* USART 0 */
#define AT91SAM9263_ID_US1	8	/* USART 1 */
#define AT91SAM9263_ID_US2	9	/* USART 2 */
#define AT91SAM9263_ID_MCI0	10	/* Multimedia Card Interface 0 */
#define AT91SAM9263_ID_MCI1	11	/* Multimedia Card Interface 1 */
#define AT91SAM9263_ID_CAN	12	/* CAN */
#define AT91SAM9263_ID_TWI	13	/* Two-Wire Interface */
#define AT91SAM9263_ID_SPI0	14	/* Serial Peripheral Interface 0 */
#define AT91SAM9263_ID_SPI1	15	/* Serial Peripheral Interface 1 */
#define AT91SAM9263_ID_SSC0	16	/* Serial Synchronous Controller 0 */
#define AT91SAM9263_ID_SSC1	17	/* Serial Synchronous Controller 1 */
#define AT91SAM9263_ID_AC97C	18	/* AC97 Controller */
#define AT91SAM9263_ID_TCB	19	/* Timer Counter 0, 1 and 2 */
#define AT91SAM9263_ID_PWMC	20	/* Pulse Width Modulation Controller */
#define AT91SAM9263_ID_EMAC	21	/* Ethernet */
#define AT91SAM9263_ID_2DGE	23	/* 2D Graphic Engine */
#define AT91SAM9263_ID_UDP	24	/* USB Device Port */
#define AT91SAM9263_ID_ISI	25	/* Image Sensor Interface */
#define AT91SAM9263_ID_LCDC	26	/* LCD Controller */
#define AT91SAM9263_ID_DMA	27	/* DMA Controller */
#define AT91SAM9263_ID_UHP	29	/* USB Host port */
#define AT91SAM9263_ID_IRQ0	30	/* Advanced Interrupt Controller (IRQ0) */
#define AT91SAM9263_ID_IRQ1	31	/* Advanced Interrupt Controller (IRQ1) */


/*
 * User Peripheral physical base addresses.
 */
#define AT91SAM9263_BASE_UDP		0xfff78000
#define AT91SAM9263_BASE_TCB0		0xfff7c000
#define AT91SAM9263_BASE_TC0		0xfff7c000
#define AT91SAM9263_BASE_TC1		0xfff7c040
#define AT91SAM9263_BASE_TC2		0xfff7c080
#define AT91SAM9263_BASE_MCI0		0xfff80000
#define AT91SAM9263_BASE_MCI1		0xfff84000
#define AT91SAM9263_BASE_TWI		0xfff88000
#define AT91SAM9263_BASE_US0		0xfff8c000
#define AT91SAM9263_BASE_US1		0xfff90000
#define AT91SAM9263_BASE_US2		0xfff94000
#define AT91SAM9263_BASE_SSC0		0xfff98000
#define AT91SAM9263_BASE_SSC1		0xfff9c000
#define AT91SAM9263_BASE_AC97C		0xfffa0000
#define AT91SAM9263_BASE_SPI0		0xfffa4000
#define AT91SAM9263_BASE_SPI1		0xfffa8000
#define AT91SAM9263_BASE_CAN		0xfffac000
#define AT91SAM9263_BASE_PWMC		0xfffb8000
#define AT91SAM9263_BASE_EMAC		0xfffbc000
#define AT91SAM9263_BASE_ISI		0xfffc4000
#define AT91SAM9263_BASE_2DGE		0xfffc8000
#define AT91_BASE_SYS			0xffffe000

/*
 * System Peripherals (offset from AT91_BASE_SYS)
 */
#define AT91_ECC0	(0xffffe000 - AT91_BASE_SYS)
#define AT91_SDRAMC0	(0xffffe200 - AT91_BASE_SYS)
#define AT91_SMC0	(0xffffe400 - AT91_BASE_SYS)
#define AT91_ECC1	(0xffffe600 - AT91_BASE_SYS)
#define AT91_SDRAMC1	(0xffffe800 - AT91_BASE_SYS)
#define AT91_SMC1	(0xffffea00 - AT91_BASE_SYS)
#define AT91_MATRIX	(0xffffec00 - AT91_BASE_SYS)
#define AT91_CCFG	(0xffffed10 - AT91_BASE_SYS)
#define AT91_DBGU	(0xffffee00 - AT91_BASE_SYS)
#define AT91_AIC	(0xfffff000 - AT91_BASE_SYS)
#define AT91_PIOA	(0xfffff200 - AT91_BASE_SYS)
#define AT91_PIOB	(0xfffff400 - AT91_BASE_SYS)
#define AT91_PIOC	(0xfffff600 - AT91_BASE_SYS)
#define AT91_PIOD	(0xfffff800 - AT91_BASE_SYS)
#define AT91_PIOE	(0xfffffa00 - AT91_BASE_SYS)
#define AT91_PMC	(0xfffffc00 - AT91_BASE_SYS)
#define AT91_RSTC	(0xfffffd00 - AT91_BASE_SYS)
#define AT91_SHDWC	(0xfffffd10 - AT91_BASE_SYS)
#define AT91_RTT0	(0xfffffd20 - AT91_BASE_SYS)
#define AT91_PIT	(0xfffffd30 - AT91_BASE_SYS)
#define AT91_WDT	(0xfffffd40 - AT91_BASE_SYS)
#define AT91_RTT1	(0xfffffd50 - AT91_BASE_SYS)
#define AT91_GPBR	(0xfffffd60 - AT91_BASE_SYS)

#define AT91_USART0	AT91SAM9263_BASE_US0
#define AT91_USART1	AT91SAM9263_BASE_US1
#define AT91_USART2	AT91SAM9263_BASE_US2

#define AT91_SMC	AT91_SMC0

/*
 * Internal Memory.
 */
#define AT91SAM9263_SRAM0_BASE	0x00300000	/* Internal SRAM 0 base address */
#define AT91SAM9263_SRAM0_SIZE	(80 * SZ_1K)	/* Internal SRAM 0 size (80Kb) */

#define AT91SAM9263_ROM_BASE	0x00400000	/* Internal ROM base address */
#define AT91SAM9263_ROM_SIZE	SZ_128K		/* Internal ROM size (128Kb) */

#define AT91SAM9263_SRAM1_BASE	0x00500000	/* Internal SRAM 1 base address */
#define AT91SAM9263_SRAM1_SIZE	SZ_16K		/* Internal SRAM 1 size (16Kb) */

#define AT91SAM9263_LCDC_BASE	0x00700000	/* LCD Controller */
#define AT91SAM9263_DMAC_BASE	0x00800000	/* DMA Controller */
#define AT91SAM9263_UHP_BASE	0x00a00000	/* USB Host controller */


#endif
