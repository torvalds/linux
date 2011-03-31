/*
 * include/mach/at572d940hf.h
 *
 * Antonio R. Costa <costa.antonior@gmail.com>
 * Copyright (C) 2008 Atmel
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#ifndef AT572D940HF_H
#define AT572D940HF_H

/*
 * Peripheral identifiers/interrupts.
 */
#define AT91_ID_FIQ		0	/* Advanced Interrupt Controller (FIQ) */
#define AT91_ID_SYS		1	/* System Peripherals */
#define AT572D940HF_ID_PIOA	2	/* Parallel IO Controller A */
#define AT572D940HF_ID_PIOB	3	/* Parallel IO Controller B */
#define AT572D940HF_ID_PIOC	4	/* Parallel IO Controller C */
#define AT572D940HF_ID_EMAC	5	/* MACB ethernet controller */
#define AT572D940HF_ID_US0	6	/* USART 0 */
#define AT572D940HF_ID_US1	7	/* USART 1 */
#define AT572D940HF_ID_US2	8	/* USART 2 */
#define AT572D940HF_ID_MCI	9	/* Multimedia Card Interface */
#define AT572D940HF_ID_UDP	10	/* USB Device Port */
#define AT572D940HF_ID_TWI0	11	/* Two-Wire Interface 0 */
#define AT572D940HF_ID_SPI0	12	/* Serial Peripheral Interface 0 */
#define AT572D940HF_ID_SPI1	13	/* Serial Peripheral Interface 1 */
#define AT572D940HF_ID_SSC0	14	/* Serial Synchronous Controller 0 */
#define AT572D940HF_ID_SSC1	15	/* Serial Synchronous Controller 1 */
#define AT572D940HF_ID_SSC2	16	/* Serial Synchronous Controller 2 */
#define AT572D940HF_ID_TC0	17	/* Timer Counter 0 */
#define AT572D940HF_ID_TC1	18	/* Timer Counter 1 */
#define AT572D940HF_ID_TC2	19	/* Timer Counter 2 */
#define AT572D940HF_ID_UHP	20	/* USB Host port */
#define AT572D940HF_ID_SSC3	21	/* Serial Synchronous Controller 3 */
#define AT572D940HF_ID_TWI1	22	/* Two-Wire Interface 1 */
#define AT572D940HF_ID_CAN0	23	/* CAN Controller 0 */
#define AT572D940HF_ID_CAN1	24	/* CAN Controller 1 */
#define AT572D940HF_ID_MHALT	25	/* mAgicV HALT line */
#define AT572D940HF_ID_MSIRQ0	26	/* mAgicV SIRQ0 line */
#define AT572D940HF_ID_MEXC	27	/* mAgicV exception line */
#define AT572D940HF_ID_MEDMA	28	/* mAgicV end of DMA line */
#define AT572D940HF_ID_IRQ0	29	/* External Interrupt Source (IRQ0) */
#define AT572D940HF_ID_IRQ1	30	/* External Interrupt Source (IRQ1) */
#define AT572D940HF_ID_IRQ2	31	/* External Interrupt Source (IRQ2) */


/*
 * User Peripheral physical base addresses.
 */
#define AT572D940HF_BASE_TCB	0xfffa0000
#define AT572D940HF_BASE_TC0	0xfffa0000
#define AT572D940HF_BASE_TC1	0xfffa0040
#define AT572D940HF_BASE_TC2	0xfffa0080
#define AT572D940HF_BASE_UDP	0xfffa4000
#define AT572D940HF_BASE_MCI	0xfffa8000
#define AT572D940HF_BASE_TWI0	0xfffac000
#define AT572D940HF_BASE_US0	0xfffb0000
#define AT572D940HF_BASE_US1	0xfffb4000
#define AT572D940HF_BASE_US2	0xfffb8000
#define AT572D940HF_BASE_SSC0	0xfffbc000
#define AT572D940HF_BASE_SSC1	0xfffc0000
#define AT572D940HF_BASE_SSC2	0xfffc4000
#define AT572D940HF_BASE_SPI0	0xfffc8000
#define AT572D940HF_BASE_SPI1	0xfffcc000
#define AT572D940HF_BASE_SSC3	0xfffd0000
#define AT572D940HF_BASE_TWI1	0xfffd4000
#define AT572D940HF_BASE_EMAC	0xfffd8000
#define AT572D940HF_BASE_CAN0	0xfffdc000
#define AT572D940HF_BASE_CAN1	0xfffe0000
#define AT91_BASE_SYS		0xffffea00


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
#define AT91_RTT	(0xfffffd20 - AT91_BASE_SYS)
#define AT91_PIT	(0xfffffd30 - AT91_BASE_SYS)
#define AT91_WDT	(0xfffffd40 - AT91_BASE_SYS)

#define AT91_USART0	AT572D940HF_ID_US0
#define AT91_USART1	AT572D940HF_ID_US1
#define AT91_USART2	AT572D940HF_ID_US2


/*
 * Internal Memory.
 */
#define AT572D940HF_SRAM_BASE	0x00300000	/* Internal SRAM base address */
#define AT572D940HF_SRAM_SIZE	(48 * SZ_1K)	/* Internal SRAM size (48Kb) */

#define AT572D940HF_ROM_BASE	0x00400000	/* Internal ROM base address */
#define AT572D940HF_ROM_SIZE	SZ_32K		/* Internal ROM size (32Kb) */

#define AT572D940HF_UHP_BASE	0x00500000	/* USB Host controller */


#endif
