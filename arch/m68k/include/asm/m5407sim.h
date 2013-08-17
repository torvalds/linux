/****************************************************************************/

/*
 *	m5407sim.h -- ColdFire 5407 System Integration Module support.
 *
 *	(C) Copyright 2000,  Lineo (www.lineo.com)
 *	(C) Copyright 1999,  Moreton Bay Ventures Pty Ltd.
 *
 *      Modified by David W. Miller for the MCF5307 Eval Board.
 */

/****************************************************************************/
#ifndef	m5407sim_h
#define	m5407sim_h
/****************************************************************************/

#define	CPU_NAME		"COLDFIRE(m5407)"
#define	CPU_INSTR_PER_JIFFY	3
#define	MCF_BUSCLK		(MCF_CLK / 2)

#include <asm/m54xxacr.h>

/*
 *	Define the 5407 SIM register set addresses.
 */
#define	MCFSIM_RSR		0x00		/* Reset Status reg (r/w) */
#define	MCFSIM_SYPCR		0x01		/* System Protection reg (r/w)*/
#define	MCFSIM_SWIVR		0x02		/* SW Watchdog intr reg (r/w) */
#define	MCFSIM_SWSR		0x03		/* SW Watchdog service (r/w) */
#define	MCFSIM_PAR		0x04		/* Pin Assignment reg (r/w) */
#define	MCFSIM_IRQPAR		0x06		/* Interrupt Assignment reg (r/w) */
#define	MCFSIM_PLLCR		0x08		/* PLL Control Reg*/
#define	MCFSIM_MPARK		0x0C		/* BUS Master Control Reg*/
#define	MCFSIM_IPR		0x40		/* Interrupt Pend reg (r/w) */
#define	MCFSIM_IMR		0x44		/* Interrupt Mask reg (r/w) */
#define	MCFSIM_AVR		0x4b		/* Autovector Ctrl reg (r/w) */
#define	MCFSIM_ICR0		0x4c		/* Intr Ctrl reg 0 (r/w) */
#define	MCFSIM_ICR1		0x4d		/* Intr Ctrl reg 1 (r/w) */
#define	MCFSIM_ICR2		0x4e		/* Intr Ctrl reg 2 (r/w) */
#define	MCFSIM_ICR3		0x4f		/* Intr Ctrl reg 3 (r/w) */
#define	MCFSIM_ICR4		0x50		/* Intr Ctrl reg 4 (r/w) */
#define	MCFSIM_ICR5		0x51		/* Intr Ctrl reg 5 (r/w) */
#define	MCFSIM_ICR6		0x52		/* Intr Ctrl reg 6 (r/w) */
#define	MCFSIM_ICR7		0x53		/* Intr Ctrl reg 7 (r/w) */
#define	MCFSIM_ICR8		0x54		/* Intr Ctrl reg 8 (r/w) */
#define	MCFSIM_ICR9		0x55		/* Intr Ctrl reg 9 (r/w) */
#define	MCFSIM_ICR10		0x56		/* Intr Ctrl reg 10 (r/w) */
#define	MCFSIM_ICR11		0x57		/* Intr Ctrl reg 11 (r/w) */

#define MCFSIM_CSAR0		0x80		/* CS 0 Address 0 reg (r/w) */
#define MCFSIM_CSMR0		0x84		/* CS 0 Mask 0 reg (r/w) */
#define MCFSIM_CSCR0		0x8a		/* CS 0 Control reg (r/w) */
#define MCFSIM_CSAR1		0x8c		/* CS 1 Address reg (r/w) */
#define MCFSIM_CSMR1		0x90		/* CS 1 Mask reg (r/w) */
#define MCFSIM_CSCR1		0x96		/* CS 1 Control reg (r/w) */

#define MCFSIM_CSAR2		0x98		/* CS 2 Address reg (r/w) */
#define MCFSIM_CSMR2		0x9c		/* CS 2 Mask reg (r/w) */
#define MCFSIM_CSCR2		0xa2		/* CS 2 Control reg (r/w) */
#define MCFSIM_CSAR3		0xa4		/* CS 3 Address reg (r/w) */
#define MCFSIM_CSMR3		0xa8		/* CS 3 Mask reg (r/w) */
#define MCFSIM_CSCR3		0xae		/* CS 3 Control reg (r/w) */
#define MCFSIM_CSAR4		0xb0		/* CS 4 Address reg (r/w) */
#define MCFSIM_CSMR4		0xb4		/* CS 4 Mask reg (r/w) */
#define MCFSIM_CSCR4		0xba		/* CS 4 Control reg (r/w) */
#define MCFSIM_CSAR5		0xbc		/* CS 5 Address reg (r/w) */
#define MCFSIM_CSMR5		0xc0		/* CS 5 Mask reg (r/w) */
#define MCFSIM_CSCR5		0xc6		/* CS 5 Control reg (r/w) */
#define MCFSIM_CSAR6		0xc8		/* CS 6 Address reg (r/w) */
#define MCFSIM_CSMR6		0xcc		/* CS 6 Mask reg (r/w) */
#define MCFSIM_CSCR6		0xd2		/* CS 6 Control reg (r/w) */
#define MCFSIM_CSAR7		0xd4		/* CS 7 Address reg (r/w) */
#define MCFSIM_CSMR7		0xd8		/* CS 7 Mask reg (r/w) */
#define MCFSIM_CSCR7		0xde		/* CS 7 Control reg (r/w) */

#define MCFSIM_DCR		(MCF_MBAR + 0x100)	/* DRAM Control */
#define MCFSIM_DACR0		(MCF_MBAR + 0x108)	/* DRAM 0 Addr/Ctrl */
#define MCFSIM_DMR0		(MCF_MBAR + 0x10c)	/* DRAM 0 Mask */
#define MCFSIM_DACR1		(MCF_MBAR + 0x110)	/* DRAM 1 Addr/Ctrl */
#define MCFSIM_DMR1		(MCF_MBAR + 0x114)	/* DRAM 1 Mask */

/*
 *	Timer module.
 */
#define MCFTIMER_BASE1		(MCF_MBAR + 0x140)	/* Base of TIMER1 */
#define MCFTIMER_BASE2		(MCF_MBAR + 0x180)	/* Base of TIMER2 */

#define MCFUART_BASE0		(MCF_MBAR + 0x1c0)	/* Base address UART0 */
#define MCFUART_BASE1		(MCF_MBAR + 0x200)	/* Base address UART1 */

#define	MCFSIM_PADDR		(MCF_MBAR + 0x244)
#define	MCFSIM_PADAT		(MCF_MBAR + 0x248)

/*
 *	DMA unit base addresses.
 */
#define MCFDMA_BASE0		(MCF_MBAR + 0x300)	/* Base address DMA 0 */
#define MCFDMA_BASE1		(MCF_MBAR + 0x340)	/* Base address DMA 1 */
#define MCFDMA_BASE2		(MCF_MBAR + 0x380)	/* Base address DMA 2 */
#define MCFDMA_BASE3		(MCF_MBAR + 0x3C0)	/* Base address DMA 3 */

/*
 * Generic GPIO support
 */
#define MCFGPIO_PIN_MAX			16
#define MCFGPIO_IRQ_MAX			-1
#define MCFGPIO_IRQ_VECBASE		-1

/*
 *	Some symbol defines for the above...
 */
#define	MCFSIM_SWDICR		MCFSIM_ICR0	/* Watchdog timer ICR */
#define	MCFSIM_TIMER1ICR	MCFSIM_ICR1	/* Timer 1 ICR */
#define	MCFSIM_TIMER2ICR	MCFSIM_ICR2	/* Timer 2 ICR */
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
#define IRQ5_LEVEL4	0x80
#define IRQ3_LEVEL6	0x40
#define IRQ1_LEVEL2	0x20

/*
 *	Define system peripheral IRQ usage.
 */
#define	MCF_IRQ_TIMER		30		/* Timer0, Level 6 */
#define	MCF_IRQ_PROFILER	31		/* Timer1, Level 7 */
#define	MCF_IRQ_UART0		73		/* UART0 */
#define	MCF_IRQ_UART1		74		/* UART1 */

/****************************************************************************/
#endif	/* m5407sim_h */
