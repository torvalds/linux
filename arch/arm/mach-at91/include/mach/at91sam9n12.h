/*
 * SoC specific header file for the AT91SAM9N12
 *
 * Copyright (C) 2012 Atmel Corporation
 *
 * Common definitions, based on AT91SAM9N12 SoC datasheet
 *
 * Licensed under GPLv2 or later
 */

#ifndef _AT91SAM9N12_H_
#define _AT91SAM9N12_H_

/*
 * Peripheral identifiers/interrupts.
 */
#define AT91SAM9N12_ID_PIOAB	2	/* Parallel I/O Controller A and B */
#define AT91SAM9N12_ID_PIOCD	3	/* Parallel I/O Controller C and D */
#define AT91SAM9N12_ID_FUSE	4	/* FUSE Controller */
#define AT91SAM9N12_ID_USART0	5	/* USART 0 */
#define AT91SAM9N12_ID_USART1	6	/* USART 1 */
#define AT91SAM9N12_ID_USART2	7	/* USART 2 */
#define AT91SAM9N12_ID_USART3	8	/* USART 3 */
#define AT91SAM9N12_ID_TWI0	9	/* Two-Wire Interface 0 */
#define AT91SAM9N12_ID_TWI1	10	/* Two-Wire Interface 1 */
#define AT91SAM9N12_ID_MCI	12	/* High Speed Multimedia Card Interface */
#define AT91SAM9N12_ID_SPI0	13	/* Serial Peripheral Interface 0 */
#define AT91SAM9N12_ID_SPI1	14	/* Serial Peripheral Interface 1 */
#define AT91SAM9N12_ID_UART0	15	/* UART 0 */
#define AT91SAM9N12_ID_UART1	16	/* UART 1 */
#define AT91SAM9N12_ID_TCB	17	/* Timer Counter 0, 1, 2, 3, 4 and 5 */
#define AT91SAM9N12_ID_PWM	18	/* Pulse Width Modulation Controller */
#define AT91SAM9N12_ID_ADC	19	/* ADC Controller */
#define AT91SAM9N12_ID_DMA	20	/* DMA Controller */
#define AT91SAM9N12_ID_UHP	22	/* USB Host High Speed */
#define AT91SAM9N12_ID_UDP	23	/* USB Device High Speed */
#define AT91SAM9N12_ID_LCDC	25	/* LCD Controller */
#define AT91SAM9N12_ID_ISI	25	/* Image Sensor Interface */
#define AT91SAM9N12_ID_SSC	28	/* Synchronous Serial Controller */
#define AT91SAM9N12_ID_TRNG	30	/* TRNG */
#define AT91SAM9N12_ID_IRQ0	31	/* Advanced Interrupt Controller */

/*
 * User Peripheral physical base addresses.
 */
#define AT91SAM9N12_BASE_USART0	0xf801c000
#define AT91SAM9N12_BASE_USART1	0xf8020000
#define AT91SAM9N12_BASE_USART2	0xf8024000
#define AT91SAM9N12_BASE_USART3	0xf8028000

/*
 * System Peripherals
 */
#define AT91SAM9N12_BASE_RTC	0xfffffeb0

/*
 * Internal Memory.
 */
#define AT91SAM9N12_SRAM_BASE	0x00300000	/* Internal SRAM base address */
#define AT91SAM9N12_SRAM_SIZE	SZ_32K		/* Internal SRAM size (32Kb) */

#define AT91SAM9N12_ROM_BASE	0x00100000	/* Internal ROM base address */
#define AT91SAM9N12_ROM_SIZE	SZ_128K		/* Internal ROM size (128Kb) */

#endif
