/* SPDX-License-Identifier: GPL-2.0 */
/****************************************************************************/

/*
 *	m5307sim.h -- ColdFire 5307 System Integration Module support.
 *
 *	(C) Copyright 1999,  Moreton Bay Ventures Pty Ltd.
 *	(C) Copyright 1999,  Lineo (www.lineo.com)
 *
 *      Modified by David W. Miller for the MCF5307 Eval Board.
 */

/****************************************************************************/
#ifndef	m5307sim_h
#define	m5307sim_h
/****************************************************************************/

#define	CPU_NAME		"COLDFIRE(m5307)"
#define	CPU_INSTR_PER_JIFFY	3
#define	MCF_BUSCLK		(MCF_CLK / 2)

#include <asm/m53xxacr.h>

/*
 *	Define the 5307 SIM register set addresses.
 */
#define	MCFSIM_RSR		(MCF_MBAR + 0x00)	/* Reset Status reg */
#define	MCFSIM_SYPCR		(MCF_MBAR + 0x01)	/* System Protection */
#define	MCFSIM_SWIVR		(MCF_MBAR + 0x02)	/* SW Watchdog intr */
#define	MCFSIM_SWSR		(MCF_MBAR + 0x03)	/* SW Watchdog service*/
#define	MCFSIM_PAR		(MCF_MBAR + 0x04)	/* Pin Assignment */
#define	MCFSIM_IRQPAR		(MCF_MBAR + 0x06)	/* Itr Assignment */
#define	MCFSIM_PLLCR		(MCF_MBAR + 0x08)	/* PLL Ctrl Reg */
#define	MCFSIM_MPARK		(MCF_MBAR + 0x0C)	/* BUS Master Ctrl */
#define	MCFSIM_IPR		(MCF_MBAR + 0x40)	/* Interrupt Pend */
#define	MCFSIM_IMR		(MCF_MBAR + 0x44)	/* Interrupt Mask */
#define	MCFSIM_AVR		(MCF_MBAR + 0x4b)	/* Autovector Ctrl */
#define	MCFSIM_ICR0		(MCF_MBAR + 0x4c)	/* Intr Ctrl reg 0 */
#define	MCFSIM_ICR1		(MCF_MBAR + 0x4d)	/* Intr Ctrl reg 1 */
#define	MCFSIM_ICR2		(MCF_MBAR + 0x4e)	/* Intr Ctrl reg 2 */
#define	MCFSIM_ICR3		(MCF_MBAR + 0x4f)	/* Intr Ctrl reg 3 */
#define	MCFSIM_ICR4		(MCF_MBAR + 0x50)	/* Intr Ctrl reg 4 */
#define	MCFSIM_ICR5		(MCF_MBAR + 0x51)	/* Intr Ctrl reg 5 */
#define	MCFSIM_ICR6		(MCF_MBAR + 0x52)	/* Intr Ctrl reg 6 */
#define	MCFSIM_ICR7		(MCF_MBAR + 0x53)	/* Intr Ctrl reg 7 */
#define	MCFSIM_ICR8		(MCF_MBAR + 0x54)	/* Intr Ctrl reg 8 */
#define	MCFSIM_ICR9		(MCF_MBAR + 0x55)	/* Intr Ctrl reg 9 */
#define	MCFSIM_ICR10		(MCF_MBAR + 0x56)	/* Intr Ctrl reg 10 */
#define	MCFSIM_ICR11		(MCF_MBAR + 0x57)	/* Intr Ctrl reg 11 */

#define MCFSIM_CSAR0		(MCF_MBAR + 0x80)	/* CS 0 Address reg */
#define MCFSIM_CSMR0		(MCF_MBAR + 0x84)	/* CS 0 Mask reg */
#define MCFSIM_CSCR0		(MCF_MBAR + 0x8a)	/* CS 0 Control reg */
#define MCFSIM_CSAR1		(MCF_MBAR + 0x8c)	/* CS 1 Address reg */
#define MCFSIM_CSMR1		(MCF_MBAR + 0x90)	/* CS 1 Mask reg */
#define MCFSIM_CSCR1		(MCF_MBAR + 0x96)	/* CS 1 Control reg */

#ifdef CONFIG_OLDMASK
#define MCFSIM_CSBAR		(MCF_MBAR + 0x98)	/* CS Base Address */
#define MCFSIM_CSBAMR		(MCF_MBAR + 0x9c)	/* CS Base Mask */
#define MCFSIM_CSMR2		(MCF_MBAR + 0x9e)	/* CS 2 Mask reg */
#define MCFSIM_CSCR2		(MCF_MBAR + 0xa2)	/* CS 2 Control reg */
#define MCFSIM_CSMR3		(MCF_MBAR + 0xaa)	/* CS 3 Mask reg */
#define MCFSIM_CSCR3		(MCF_MBAR + 0xae)	/* CS 3 Control reg */
#define MCFSIM_CSMR4		(MCF_MBAR + 0xb6)	/* CS 4 Mask reg */
#define MCFSIM_CSCR4		(MCF_MBAR + 0xba)	/* CS 4 Control reg */
#define MCFSIM_CSMR5		(MCF_MBAR + 0xc2)	/* CS 5 Mask reg */
#define MCFSIM_CSCR5		(MCF_MBAR + 0xc6)	/* CS 5 Control reg */
#define MCFSIM_CSMR6		(MCF_MBAR + 0xce)	/* CS 6 Mask reg */
#define MCFSIM_CSCR6		(MCF_MBAR + 0xd2)	/* CS 6 Control reg */
#define MCFSIM_CSMR7		(MCF_MBAR + 0xda)	/* CS 7 Mask reg */
#define MCFSIM_CSCR7		(MCF_MBAR + 0xde)	/* CS 7 Control reg */
#else
#define MCFSIM_CSAR2		(MCF_MBAR + 0x98)	/* CS 2 Address reg */
#define MCFSIM_CSMR2		(MCF_MBAR + 0x9c)	/* CS 2 Mask reg */
#define MCFSIM_CSCR2		(MCF_MBAR + 0xa2)	/* CS 2 Control reg */
#define MCFSIM_CSAR3		(MCF_MBAR + 0xa4)	/* CS 3 Address reg */
#define MCFSIM_CSMR3		(MCF_MBAR + 0xa8)	/* CS 3 Mask reg */
#define MCFSIM_CSCR3		(MCF_MBAR + 0xae)	/* CS 3 Control reg */
#define MCFSIM_CSAR4		(MCF_MBAR + 0xb0)	/* CS 4 Address reg */
#define MCFSIM_CSMR4		(MCF_MBAR + 0xb4)	/* CS 4 Mask reg */
#define MCFSIM_CSCR4		(MCF_MBAR + 0xba)	/* CS 4 Control reg */
#define MCFSIM_CSAR5		(MCF_MBAR + 0xbc)	/* CS 5 Address reg */
#define MCFSIM_CSMR5		(MCF_MBAR + 0xc0)	/* CS 5 Mask reg */
#define MCFSIM_CSCR5		(MCF_MBAR + 0xc6)	/* CS 5 Control reg */
#define MCFSIM_CSAR6		(MCF_MBAR + 0xc8)	/* CS 6 Address reg */
#define MCFSIM_CSMR6		(MCF_MBAR + 0xcc)	/* CS 6 Mask reg */
#define MCFSIM_CSCR6		(MCF_MBAR + 0xd2)	/* CS 6 Control reg */
#define MCFSIM_CSAR7		(MCF_MBAR + 0xd4)	/* CS 7 Address reg */
#define MCFSIM_CSMR7		(MCF_MBAR + 0xd8)	/* CS 7 Mask reg */
#define MCFSIM_CSCR7		(MCF_MBAR + 0xde)	/* CS 7 Control reg */
#endif /* CONFIG_OLDMASK */

#define MCFSIM_DCR		(MCF_MBAR + 0x100)	/* DRAM Control */
#define MCFSIM_DACR0		(MCF_MBAR + 0x108)	/* DRAM Addr/Ctrl 0 */
#define MCFSIM_DMR0		(MCF_MBAR + 0x10c)	/* DRAM Mask 0 */
#define MCFSIM_DACR1		(MCF_MBAR + 0x110)	/* DRAM Addr/Ctrl 1 */
#define MCFSIM_DMR1		(MCF_MBAR + 0x114)	/* DRAM Mask 1 */

/*
 *  Timer module.
 */
#define MCFTIMER_BASE1		(MCF_MBAR + 0x140)	/* Base of TIMER1 */
#define MCFTIMER_BASE2		(MCF_MBAR + 0x180)	/* Base of TIMER2 */

#define	MCFSIM_PADDR		(MCF_MBAR + 0x244)
#define	MCFSIM_PADAT		(MCF_MBAR + 0x248)

/*
 *  DMA unit base addresses.
 */
#define MCFDMA_BASE0		(MCF_MBAR + 0x300)	/* Base address DMA 0 */
#define MCFDMA_BASE1		(MCF_MBAR + 0x340)	/* Base address DMA 1 */
#define MCFDMA_BASE2		(MCF_MBAR + 0x380)	/* Base address DMA 2 */
#define MCFDMA_BASE3		(MCF_MBAR + 0x3C0)	/* Base address DMA 3 */

/*
 *  UART module.
 */
#if defined(CONFIG_NETtel) || defined(CONFIG_SECUREEDGEMP3)
#define MCFUART_BASE0		(MCF_MBAR + 0x200)	/* Base address UART0 */
#define MCFUART_BASE1		(MCF_MBAR + 0x1c0)	/* Base address UART1 */
#else
#define MCFUART_BASE0		(MCF_MBAR + 0x1c0)	/* Base address UART0 */
#define MCFUART_BASE1		(MCF_MBAR + 0x200)	/* Base address UART1 */
#endif

/*
 * Generic GPIO support
 */
#define MCFGPIO_PIN_MAX		16
#define MCFGPIO_IRQ_MAX		-1
#define MCFGPIO_IRQ_VECBASE	-1


/* Definition offset address for CS2-7  -- old mask 5307 */

#define	MCF5307_CS2		(0x400000)
#define	MCF5307_CS3		(0x600000)
#define	MCF5307_CS4		(0x800000)
#define	MCF5307_CS5		(0xA00000)
#define	MCF5307_CS6		(0xC00000)
#define	MCF5307_CS7		(0xE00000)


/*
 *	Some symbol defines for the above...
 */
#define	MCFSIM_SWDICR		MCFSIM_ICR0	/* Watchdog timer ICR */
#define	MCFSIM_TIMER1ICR	MCFSIM_ICR1	/* Timer 1 ICR */
#define	MCFSIM_TIMER2ICR	MCFSIM_ICR2	/* Timer 2 ICR */
#define	MCFSIM_I2CICR		MCFSIM_ICR3	/* I2C ICR */
#define	MCFSIM_UART1ICR		MCFSIM_ICR4	/* UART 1 ICR */
#define	MCFSIM_UART2ICR		MCFSIM_ICR5	/* UART 2 ICR */
#define	MCFSIM_DMA0ICR		MCFSIM_ICR6	/* DMA 0 ICR */
#define	MCFSIM_DMA1ICR		MCFSIM_ICR7	/* DMA 1 ICR */
#define	MCFSIM_DMA2ICR		MCFSIM_ICR8	/* DMA 2 ICR */
#define	MCFSIM_DMA3ICR		MCFSIM_ICR9	/* DMA 3 ICR */

/*
 *	Some symbol defines for the Parallel Port Pin Assignment Register
 */
#define MCFSIM_PAR_DREQ0        0x40            /* Set to select DREQ0 input */
                                                /* Clear to select par I/O */
#define MCFSIM_PAR_DREQ1        0x20            /* Select DREQ1 input */
                                                /* Clear to select par I/O */

/*
 *       Defines for the IRQPAR Register
 */
#define IRQ5_LEVEL4		0x80
#define IRQ3_LEVEL6		0x40
#define IRQ1_LEVEL2		0x20

/*
 *	Define system peripheral IRQ usage.
 */
#define	MCF_IRQ_I2C0		29		/* I2C, Level 5 */
#define	MCF_IRQ_TIMER		30		/* Timer0, Level 6 */
#define	MCF_IRQ_PROFILER	31		/* Timer1, Level 7 */
#define	MCF_IRQ_UART0		73		/* UART0 */
#define	MCF_IRQ_UART1		74		/* UART1 */

/*
 * I2C module
 */
#define	MCFI2C_BASE0		(MCF_MBAR + 0x280)
#define	MCFI2C_SIZE0		0x40

/****************************************************************************/
#endif	/* m5307sim_h */
