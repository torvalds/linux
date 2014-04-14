/*
 * Chip-specific header file for the SAMA5D3 family
 *
 *  Copyright (C) 2013 Atmel,
 *                2013 Ludovic Desroches <ludovic.desroches@atmel.com>
 *
 * Common definitions.
 * Based on SAMA5D3 datasheet.
 *
 * Licensed under GPLv2 or later.
 */

#ifndef SAMA5D3_H
#define SAMA5D3_H

/*
 * Peripheral identifiers/interrupts.
 */
#define AT91_ID_FIQ		 0	/* Advanced Interrupt Controller (FIQ) */
#define AT91_ID_SYS		 1	/* System Peripherals */
#define SAMA5D3_ID_DBGU		 2	/* debug Unit (usually no special interrupt line) */
#define AT91_ID_PIT		 3	/* PIT */
#define SAMA5D3_ID_WDT		 4	/* Watchdog Timer Interrupt */
#define SAMA5D3_ID_HSMC		 5	/* Static Memory Controller */
#define SAMA5D3_ID_PIOA		 6	/* PIOA */
#define SAMA5D3_ID_PIOB		 7	/* PIOB */
#define SAMA5D3_ID_PIOC		 8	/* PIOC */
#define SAMA5D3_ID_PIOD		 9	/* PIOD */
#define SAMA5D3_ID_PIOE		10	/* PIOE */
#define SAMA5D3_ID_SMD		11	/* SMD Soft Modem */
#define SAMA5D3_ID_USART0	12	/* USART0 */
#define SAMA5D3_ID_USART1	13	/* USART1 */
#define SAMA5D3_ID_USART2	14	/* USART2 */
#define SAMA5D3_ID_USART3	15	/* USART3 */
#define SAMA5D3_ID_UART0	16	/* UART 0 */
#define SAMA5D3_ID_UART1	17	/* UART 1 */
#define SAMA5D3_ID_TWI0		18	/* Two-Wire Interface 0 */
#define SAMA5D3_ID_TWI1		19	/* Two-Wire Interface 1 */
#define SAMA5D3_ID_TWI2		20	/* Two-Wire Interface 2 */
#define SAMA5D3_ID_HSMCI0	21	/* MCI */
#define SAMA5D3_ID_HSMCI1	22	/* MCI */
#define SAMA5D3_ID_HSMCI2	23	/* MCI */
#define SAMA5D3_ID_SPI0		24	/* Serial Peripheral Interface 0 */
#define SAMA5D3_ID_SPI1		25	/* Serial Peripheral Interface 1 */
#define SAMA5D3_ID_TC0		26	/* Timer Counter 0 */
#define SAMA5D3_ID_TC1		27	/* Timer Counter 2 */
#define SAMA5D3_ID_PWM		28	/* Pulse Width Modulation Controller */
#define SAMA5D3_ID_ADC		29	/* Touch Screen ADC Controller */
#define SAMA5D3_ID_DMA0		30	/* DMA Controller 0 */
#define SAMA5D3_ID_DMA1		31	/* DMA Controller 1 */
#define SAMA5D3_ID_UHPHS	32	/* USB Host High Speed */
#define SAMA5D3_ID_UDPHS	33	/* USB Device High Speed */
#define SAMA5D3_ID_GMAC		34	/* Gigabit Ethernet MAC */
#define SAMA5D3_ID_EMAC		35	/* Ethernet MAC */
#define SAMA5D3_ID_LCDC		36	/* LCD Controller */
#define SAMA5D3_ID_ISI		37	/* Image Sensor Interface */
#define SAMA5D3_ID_SSC0		38	/* Synchronous Serial Controller 0 */
#define SAMA5D3_ID_SSC1		39	/* Synchronous Serial Controller 1 */
#define SAMA5D3_ID_CAN0		40	/* CAN Controller 0 */
#define SAMA5D3_ID_CAN1		41	/* CAN Controller 1 */
#define SAMA5D3_ID_SHA		42	/* Secure Hash Algorithm */
#define SAMA5D3_ID_AES		43	/* Advanced Encryption Standard */
#define SAMA5D3_ID_TDES		44	/* Triple Data Encryption Standard */
#define SAMA5D3_ID_TRNG		45	/* True Random Generator Number */
#define SAMA5D3_ID_IRQ0		47	/* Advanced Interrupt Controller (IRQ0) */

/*
 * System Peripherals
 */
#define SAMA5D3_BASE_RTC	0xfffffeb0

/*
 * Internal Memory
 */
#define SAMA5D3_SRAM_BASE	0x00300000	/* Internal SRAM base address */
#define SAMA5D3_SRAM_SIZE	(128 * SZ_1K)	/* Internal SRAM size (128Kb) */

#endif
