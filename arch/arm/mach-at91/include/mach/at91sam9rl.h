/*
 * arch/arm/mach-at91/include/mach/at91sam9260.h
 *
 *  Copyright (C) 2007 Atmel Corporation
 *
 * Common definitions.
 * Based on AT91SAM9RL datasheet revision A. (Preliminary)
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive for
 * more details.
 */

#ifndef AT91SAM9RL_H
#define AT91SAM9RL_H

/*
 * Peripheral identifiers/interrupts.
 */
#define AT91_ID_FIQ		0	/* Advanced Interrupt Controller (FIQ) */
#define AT91_ID_SYS		1	/* System Controller */
#define AT91SAM9RL_ID_PIOA	2	/* Parallel IO Controller A */
#define AT91SAM9RL_ID_PIOB	3	/* Parallel IO Controller B */
#define AT91SAM9RL_ID_PIOC	4	/* Parallel IO Controller C */
#define AT91SAM9RL_ID_PIOD	5	/* Parallel IO Controller D */
#define AT91SAM9RL_ID_US0	6	/* USART 0 */
#define AT91SAM9RL_ID_US1	7	/* USART 1 */
#define AT91SAM9RL_ID_US2	8	/* USART 2 */
#define AT91SAM9RL_ID_US3	9	/* USART 3 */
#define AT91SAM9RL_ID_MCI	10	/* Multimedia Card Interface */
#define AT91SAM9RL_ID_TWI0	11	/* TWI 0 */
#define AT91SAM9RL_ID_TWI1	12	/* TWI 1 */
#define AT91SAM9RL_ID_SPI	13	/* Serial Peripheral Interface */
#define AT91SAM9RL_ID_SSC0	14	/* Serial Synchronous Controller 0 */
#define AT91SAM9RL_ID_SSC1	15	/* Serial Synchronous Controller 1 */
#define AT91SAM9RL_ID_TC0	16	/* Timer Counter 0 */
#define AT91SAM9RL_ID_TC1	17	/* Timer Counter 1 */
#define AT91SAM9RL_ID_TC2	18	/* Timer Counter 2 */
#define AT91SAM9RL_ID_PWMC	19	/* Pulse Width Modulation Controller */
#define AT91SAM9RL_ID_TSC	20	/* Touch Screen Controller */
#define AT91SAM9RL_ID_DMA	21	/* DMA Controller */
#define AT91SAM9RL_ID_UDPHS	22	/* USB Device HS */
#define AT91SAM9RL_ID_LCDC	23	/* LCD Controller */
#define AT91SAM9RL_ID_AC97C	24	/* AC97 Controller */
#define AT91SAM9RL_ID_IRQ0	31	/* Advanced Interrupt Controller (IRQ0) */


/*
 * User Peripheral physical base addresses.
 */
#define AT91SAM9RL_BASE_TCB0	0xfffa0000
#define AT91SAM9RL_BASE_TC0	0xfffa0000
#define AT91SAM9RL_BASE_TC1	0xfffa0040
#define AT91SAM9RL_BASE_TC2	0xfffa0080
#define AT91SAM9RL_BASE_MCI	0xfffa4000
#define AT91SAM9RL_BASE_TWI0	0xfffa8000
#define AT91SAM9RL_BASE_TWI1	0xfffac000
#define AT91SAM9RL_BASE_US0	0xfffb0000
#define AT91SAM9RL_BASE_US1	0xfffb4000
#define AT91SAM9RL_BASE_US2	0xfffb8000
#define AT91SAM9RL_BASE_US3	0xfffbc000
#define AT91SAM9RL_BASE_SSC0	0xfffc0000
#define AT91SAM9RL_BASE_SSC1	0xfffc4000
#define AT91SAM9RL_BASE_PWMC	0xfffc8000
#define AT91SAM9RL_BASE_SPI	0xfffcc000
#define AT91SAM9RL_BASE_TSC	0xfffd0000
#define AT91SAM9RL_BASE_UDPHS	0xfffd4000
#define AT91SAM9RL_BASE_AC97C	0xfffd8000
#define AT91_BASE_SYS		0xffffc000


/*
 * System Peripherals (offset from AT91_BASE_SYS)
 */
#define AT91_DMA	(0xffffe600 - AT91_BASE_SYS)
#define AT91_ECC	(0xffffe800 - AT91_BASE_SYS)
#define AT91_SDRAMC	(0xffffea00 - AT91_BASE_SYS)
#define AT91_SMC	(0xffffec00 - AT91_BASE_SYS)
#define AT91_MATRIX	(0xffffee00 - AT91_BASE_SYS)
#define AT91_CCFG	(0xffffef10 - AT91_BASE_SYS)
#define AT91_AIC	(0xfffff000 - AT91_BASE_SYS)
#define AT91_DBGU	(0xfffff200 - AT91_BASE_SYS)
#define AT91_PIOA	(0xfffff400 - AT91_BASE_SYS)
#define AT91_PIOB	(0xfffff600 - AT91_BASE_SYS)
#define AT91_PIOC	(0xfffff800 - AT91_BASE_SYS)
#define AT91_PIOD	(0xfffffa00 - AT91_BASE_SYS)
#define AT91_PMC	(0xfffffc00 - AT91_BASE_SYS)
#define AT91_RSTC	(0xfffffd00 - AT91_BASE_SYS)
#define AT91_SHDWC	(0xfffffd10 - AT91_BASE_SYS)
#define AT91_RTT	(0xfffffd20 - AT91_BASE_SYS)
#define AT91_PIT	(0xfffffd30 - AT91_BASE_SYS)
#define AT91_WDT	(0xfffffd40 - AT91_BASE_SYS)
#define AT91_SCKCR	(0xfffffd50 - AT91_BASE_SYS)
#define AT91_GPBR	(0xfffffd60 - AT91_BASE_SYS)
#define AT91_RTC	(0xfffffe00 - AT91_BASE_SYS)

#define AT91_USART0	AT91SAM9RL_BASE_US0
#define AT91_USART1	AT91SAM9RL_BASE_US1
#define AT91_USART2	AT91SAM9RL_BASE_US2
#define AT91_USART3	AT91SAM9RL_BASE_US3


/*
 * Internal Memory.
 */
#define AT91SAM9RL_SRAM_BASE	0x00300000	/* Internal SRAM base address */
#define AT91SAM9RL_SRAM_SIZE	SZ_16K		/* Internal SRAM size (16Kb) */

#define AT91SAM9RL_ROM_BASE	0x00400000	/* Internal ROM base address */
#define AT91SAM9RL_ROM_SIZE	(2 * SZ_16K)	/* Internal ROM size (32Kb) */

#define AT91SAM9RL_LCDC_BASE	0x00500000	/* LCD Controller */
#define AT91SAM9RL_UDPHS_FIFO	0x00600000	/* USB Device HS controller */

#endif
