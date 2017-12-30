/* SPDX-License-Identifier: GPL-2.0 */
/****************************************************************************/

/*
 *	m5206sim.h -- ColdFire 5206 System Integration Module support.
 *
 *	(C) Copyright 1999, Greg Ungerer (gerg@snapgear.com)
 * 	(C) Copyright 2000, Lineo Inc. (www.lineo.com) 
 */

/****************************************************************************/
#ifndef	m5206sim_h
#define	m5206sim_h
/****************************************************************************/

#define	CPU_NAME		"COLDFIRE(m5206)"
#define	CPU_INSTR_PER_JIFFY	3
#define	MCF_BUSCLK		MCF_CLK

#include <asm/m52xxacr.h>

/*
 *	Define the 5206 SIM register set addresses.
 */
#define	MCFSIM_SIMR		(MCF_MBAR + 0x03)	/* SIM Config reg */
#define	MCFSIM_ICR1		(MCF_MBAR + 0x14)	/* Intr Ctrl reg 1 */
#define	MCFSIM_ICR2		(MCF_MBAR + 0x15)	/* Intr Ctrl reg 2 */
#define	MCFSIM_ICR3		(MCF_MBAR + 0x16)	/* Intr Ctrl reg 3 */
#define	MCFSIM_ICR4		(MCF_MBAR + 0x17)	/* Intr Ctrl reg 4 */
#define	MCFSIM_ICR5		(MCF_MBAR + 0x18)	/* Intr Ctrl reg 5 */
#define	MCFSIM_ICR6		(MCF_MBAR + 0x19)	/* Intr Ctrl reg 6 */
#define	MCFSIM_ICR7		(MCF_MBAR + 0x1a)	/* Intr Ctrl reg 7 */
#define	MCFSIM_ICR8		(MCF_MBAR + 0x1b)	/* Intr Ctrl reg 8 */
#define	MCFSIM_ICR9		(MCF_MBAR + 0x1c)	/* Intr Ctrl reg 9 */
#define	MCFSIM_ICR10		(MCF_MBAR + 0x1d)	/* Intr Ctrl reg 10 */
#define	MCFSIM_ICR11		(MCF_MBAR + 0x1e)	/* Intr Ctrl reg 11 */
#define	MCFSIM_ICR12		(MCF_MBAR + 0x1f)	/* Intr Ctrl reg 12 */
#define	MCFSIM_ICR13		(MCF_MBAR + 0x20)	/* Intr Ctrl reg 13 */
#ifdef CONFIG_M5206e
#define	MCFSIM_ICR14		(MCF_MBAR + 0x21)	/* Intr Ctrl reg 14 */
#define	MCFSIM_ICR15		(MCF_MBAR + 0x22)	/* Intr Ctrl reg 15 */
#endif

#define	MCFSIM_IMR		(MCF_MBAR + 0x36)	/* Interrupt Mask */
#define	MCFSIM_IPR		(MCF_MBAR + 0x3a)	/* Interrupt Pending */

#define	MCFSIM_RSR		(MCF_MBAR + 0x40)	/* Reset Status */
#define	MCFSIM_SYPCR		(MCF_MBAR + 0x41)	/* System Protection */

#define	MCFSIM_SWIVR		(MCF_MBAR + 0x42)	/* SW Watchdog intr */
#define	MCFSIM_SWSR		(MCF_MBAR + 0x43)	/* SW Watchdog srv */

#define	MCFSIM_DCRR		(MCF_MBAR + 0x46) /* DRAM Refresh reg (r/w) */
#define	MCFSIM_DCTR		(MCF_MBAR + 0x4a) /* DRAM Timing reg (r/w) */
#define	MCFSIM_DAR0		(MCF_MBAR + 0x4c) /* DRAM 0 Address reg(r/w) */
#define	MCFSIM_DMR0		(MCF_MBAR + 0x50) /* DRAM 0 Mask reg (r/w) */
#define	MCFSIM_DCR0		(MCF_MBAR + 0x57) /* DRAM 0 Control reg (r/w) */
#define	MCFSIM_DAR1		(MCF_MBAR + 0x58) /* DRAM 1 Address reg (r/w) */
#define	MCFSIM_DMR1		(MCF_MBAR + 0x5c) /* DRAM 1 Mask reg (r/w) */
#define	MCFSIM_DCR1		(MCF_MBAR + 0x63) /* DRAM 1 Control reg (r/w) */

#define	MCFSIM_CSAR0		(MCF_MBAR + 0x64)	/* CS 0 Address reg */
#define	MCFSIM_CSMR0		(MCF_MBAR + 0x68)	/* CS 0 Mask reg */
#define	MCFSIM_CSCR0		(MCF_MBAR + 0x6e)	/* CS 0 Control reg */
#define	MCFSIM_CSAR1		(MCF_MBAR + 0x70)	/* CS 1 Address reg */
#define	MCFSIM_CSMR1		(MCF_MBAR + 0x74)	/* CS 1 Mask reg */
#define	MCFSIM_CSCR1		(MCF_MBAR + 0x7a)	/* CS 1 Control reg */
#define	MCFSIM_CSAR2		(MCF_MBAR + 0x7c)	/* CS 2 Address reg */
#define	MCFSIM_CSMR2		(MCF_MBAR + 0x80)	/* CS 2 Mask reg */
#define	MCFSIM_CSCR2		(MCF_MBAR + 0x86)	/* CS 2 Control reg */
#define	MCFSIM_CSAR3		(MCF_MBAR + 0x88)	/* CS 3 Address reg */
#define	MCFSIM_CSMR3		(MCF_MBAR + 0x8c)	/* CS 3 Mask reg */
#define	MCFSIM_CSCR3		(MCF_MBAR + 0x92)	/* CS 3 Control reg */
#define	MCFSIM_CSAR4		(MCF_MBAR + 0x94)	/* CS 4 Address reg */
#define	MCFSIM_CSMR4		(MCF_MBAR + 0x98)	/* CS 4 Mask reg */
#define	MCFSIM_CSCR4		(MCF_MBAR + 0x9e)	/* CS 4 Control reg */
#define	MCFSIM_CSAR5		(MCF_MBAR + 0xa0)	/* CS 5 Address reg */
#define	MCFSIM_CSMR5		(MCF_MBAR + 0xa4)	/* CS 5 Mask reg */
#define	MCFSIM_CSCR5		(MCF_MBAR + 0xaa)	/* CS 5 Control reg */
#define	MCFSIM_CSAR6		(MCF_MBAR + 0xac)	/* CS 6 Address reg */
#define	MCFSIM_CSMR6		(MCF_MBAR + 0xb0)	/* CS 6 Mask reg */
#define	MCFSIM_CSCR6		(MCF_MBAR + 0xb6)	/* CS 6 Control reg */
#define	MCFSIM_CSAR7		(MCF_MBAR + 0xb8)	/* CS 7 Address reg */
#define	MCFSIM_CSMR7		(MCF_MBAR + 0xbc)	/* CS 7 Mask reg */
#define	MCFSIM_CSCR7		(MCF_MBAR + 0xc2)	/* CS 7 Control reg */
#define	MCFSIM_DMCR		(MCF_MBAR + 0xc6)	/* Default control */

#ifdef CONFIG_M5206e
#define	MCFSIM_PAR		(MCF_MBAR + 0xca)	/* Pin Assignment */
#else
#define	MCFSIM_PAR		(MCF_MBAR + 0xcb)	/* Pin Assignment */
#endif

#define	MCFTIMER_BASE1		(MCF_MBAR + 0x100)	/* Base of TIMER1 */
#define	MCFTIMER_BASE2		(MCF_MBAR + 0x120)	/* Base of TIMER2 */

#define	MCFSIM_PADDR		(MCF_MBAR + 0x1c5)	/* Parallel Direction (r/w) */
#define	MCFSIM_PADAT		(MCF_MBAR + 0x1c9)	/* Parallel Port Value (r/w) */

#define	MCFDMA_BASE0		(MCF_MBAR + 0x200)	/* Base address DMA 0 */
#define	MCFDMA_BASE1		(MCF_MBAR + 0x240)	/* Base address DMA 1 */

#if defined(CONFIG_NETtel)
#define	MCFUART_BASE0		(MCF_MBAR + 0x180)	/* Base address UART0 */
#define	MCFUART_BASE1		(MCF_MBAR + 0x140)	/* Base address UART1 */
#else
#define	MCFUART_BASE0		(MCF_MBAR + 0x140)	/* Base address UART0 */
#define	MCFUART_BASE1		(MCF_MBAR + 0x180)	/* Base address UART1 */
#endif

/*
 *	Define system peripheral IRQ usage.
 */
#define	MCF_IRQ_I2C0		29		/* I2C, Level 5 */
#define	MCF_IRQ_TIMER		30		/* Timer0, Level 6 */
#define	MCF_IRQ_PROFILER	31		/* Timer1, Level 7 */
#define	MCF_IRQ_UART0		73		/* UART0 */
#define	MCF_IRQ_UART1		74		/* UART1 */

/*
 *	Generic GPIO
 */
#define MCFGPIO_PIN_MAX		8
#define MCFGPIO_IRQ_VECBASE	-1
#define MCFGPIO_IRQ_MAX		-1

/*
 *	Some symbol defines for the Parallel Port Pin Assignment Register
 */
#ifdef CONFIG_M5206e
#define MCFSIM_PAR_DREQ0        0x100           /* Set to select DREQ0 input */
                                                /* Clear to select T0 input */
#define MCFSIM_PAR_DREQ1        0x200           /* Select DREQ1 input */
                                                /* Clear to select T0 output */
#endif

/*
 *	Some symbol defines for the Interrupt Control Register
 */
#define	MCFSIM_SWDICR		MCFSIM_ICR8	/* Watchdog timer ICR */
#define	MCFSIM_TIMER1ICR	MCFSIM_ICR9	/* Timer 1 ICR */
#define	MCFSIM_TIMER2ICR	MCFSIM_ICR10	/* Timer 2 ICR */
#define	MCFSIM_I2CICR		MCFSIM_ICR11	/* I2C ICR */
#define	MCFSIM_UART1ICR		MCFSIM_ICR12	/* UART 1 ICR */
#define	MCFSIM_UART2ICR		MCFSIM_ICR13	/* UART 2 ICR */
#ifdef CONFIG_M5206e
#define	MCFSIM_DMA1ICR		MCFSIM_ICR14	/* DMA 1 ICR */
#define	MCFSIM_DMA2ICR		MCFSIM_ICR15	/* DMA 2 ICR */
#endif

/*
 * I2C Controller
*/
#define MCFI2C_BASE0		(MCF_MBAR + 0x1e0)
#define MCFI2C_SIZE0		0x40

/****************************************************************************/
#endif	/* m5206sim_h */
