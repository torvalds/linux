/*
 * Chip-specific header file for the AT91SAM9x5 family
 *
 *  Copyright (C) 2009-2012 Atmel Corporation.
 *
 * Common definitions.
 * Based on AT91SAM9x5 datasheet.
 *
 * Licensed under GPLv2 or later.
 */

#ifndef AT91SAM9X5_H
#define AT91SAM9X5_H

/*
 * Peripheral identifiers/interrupts.
 */
#define AT91SAM9X5_ID_PIOAB	2	/* Parallel I/O Controller A and B */
#define AT91SAM9X5_ID_PIOCD	3	/* Parallel I/O Controller C and D */
#define AT91SAM9X5_ID_SMD	4	/* SMD Soft Modem (SMD) */
#define AT91SAM9X5_ID_USART0	5	/* USART 0 */
#define AT91SAM9X5_ID_USART1	6	/* USART 1 */
#define AT91SAM9X5_ID_USART2	7	/* USART 2 */
#define AT91SAM9X5_ID_USART3	8	/* USART 3 */
#define AT91SAM9X5_ID_TWI0	9	/* Two-Wire Interface 0 */
#define AT91SAM9X5_ID_TWI1	10	/* Two-Wire Interface 1 */
#define AT91SAM9X5_ID_TWI2	11	/* Two-Wire Interface 2 */
#define AT91SAM9X5_ID_MCI0	12	/* High Speed Multimedia Card Interface 0 */
#define AT91SAM9X5_ID_SPI0	13	/* Serial Peripheral Interface 0 */
#define AT91SAM9X5_ID_SPI1	14	/* Serial Peripheral Interface 1 */
#define AT91SAM9X5_ID_UART0	15	/* UART 0 */
#define AT91SAM9X5_ID_UART1	16	/* UART 1 */
#define AT91SAM9X5_ID_TCB	17	/* Timer Counter 0, 1, 2, 3, 4 and 5 */
#define AT91SAM9X5_ID_PWM	18	/* Pulse Width Modulation Controller */
#define AT91SAM9X5_ID_ADC	19	/* ADC Controller */
#define AT91SAM9X5_ID_DMA0	20	/* DMA Controller 0 */
#define AT91SAM9X5_ID_DMA1	21	/* DMA Controller 1 */
#define AT91SAM9X5_ID_UHPHS	22	/* USB Host High Speed */
#define AT91SAM9X5_ID_UDPHS	23	/* USB Device High Speed */
#define AT91SAM9X5_ID_EMAC0	24	/* Ethernet MAC0 */
#define AT91SAM9X5_ID_LCDC	25	/* LCD Controller */
#define AT91SAM9X5_ID_ISI	25	/* Image Sensor Interface */
#define AT91SAM9X5_ID_MCI1	26	/* High Speed Multimedia Card Interface 1 */
#define AT91SAM9X5_ID_EMAC1	27	/* Ethernet MAC1 */
#define AT91SAM9X5_ID_SSC	28	/* Synchronous Serial Controller */
#define AT91SAM9X5_ID_CAN0	29	/* CAN Controller 0 */
#define AT91SAM9X5_ID_CAN1	30	/* CAN Controller 1 */
#define AT91SAM9X5_ID_IRQ0	31	/* Advanced Interrupt Controller */

/*
 * User Peripheral physical base addresses.
 */
#define AT91SAM9X5_BASE_USART0	0xf801c000
#define AT91SAM9X5_BASE_USART1	0xf8020000
#define AT91SAM9X5_BASE_USART2	0xf8024000

/*
 * Internal Memory.
 */
#define AT91SAM9X5_SRAM_BASE	0x00300000	/* Internal SRAM base address */
#define AT91SAM9X5_SRAM_SIZE	SZ_32K		/* Internal SRAM size (32Kb) */

#define AT91SAM9X5_ROM_BASE	0x00400000	/* Internal ROM base address */
#define AT91SAM9X5_ROM_SIZE	SZ_64K		/* Internal ROM size (64Kb) */

#endif
