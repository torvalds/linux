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


/*
 *	Define the 5206 SIM register set addresses.
 */
#define	MCFSIM_SIMR		0x03		/* SIM Config reg (r/w) */
#define	MCFSIM_ICR1		0x14		/* Intr Ctrl reg 1 (r/w) */
#define	MCFSIM_ICR2		0x15		/* Intr Ctrl reg 2 (r/w) */
#define	MCFSIM_ICR3		0x16		/* Intr Ctrl reg 3 (r/w) */
#define	MCFSIM_ICR4		0x17		/* Intr Ctrl reg 4 (r/w) */
#define	MCFSIM_ICR5		0x18		/* Intr Ctrl reg 5 (r/w) */
#define	MCFSIM_ICR6		0x19		/* Intr Ctrl reg 6 (r/w) */
#define	MCFSIM_ICR7		0x1a		/* Intr Ctrl reg 7 (r/w) */
#define	MCFSIM_ICR8		0x1b		/* Intr Ctrl reg 8 (r/w) */
#define	MCFSIM_ICR9		0x1c		/* Intr Ctrl reg 9 (r/w) */
#define	MCFSIM_ICR10		0x1d		/* Intr Ctrl reg 10 (r/w) */
#define	MCFSIM_ICR11		0x1e		/* Intr Ctrl reg 11 (r/w) */
#define	MCFSIM_ICR12		0x1f		/* Intr Ctrl reg 12 (r/w) */
#define	MCFSIM_ICR13		0x20		/* Intr Ctrl reg 13 (r/w) */
#ifdef CONFIG_M5206e
#define	MCFSIM_ICR14		0x21		/* Intr Ctrl reg 14 (r/w) */
#define	MCFSIM_ICR15		0x22		/* Intr Ctrl reg 15 (r/w) */
#endif

#define MCFSIM_IMR		0x36		/* Interrupt Mask reg (r/w) */
#define MCFSIM_IPR		0x3a		/* Interrupt Pend reg (r/w) */

#define	MCFSIM_RSR		0x40		/* Reset Status reg (r/w) */
#define	MCFSIM_SYPCR		0x41		/* System Protection reg (r/w)*/

#define	MCFSIM_SWIVR		0x42		/* SW Watchdog intr reg (r/w) */
#define	MCFSIM_SWSR		0x43		/* SW Watchdog service (r/w) */

#define	MCFSIM_DCRR		0x46		/* DRAM Refresh reg (r/w) */
#define	MCFSIM_DCTR		0x4a		/* DRAM Timing reg (r/w) */
#define	MCFSIM_DAR0		0x4c		/* DRAM 0 Address reg(r/w) */
#define	MCFSIM_DMR0		0x50		/* DRAM 0 Mask reg (r/w) */
#define	MCFSIM_DCR0		0x57		/* DRAM 0 Control reg (r/w) */
#define	MCFSIM_DAR1		0x58		/* DRAM 1 Address reg (r/w) */
#define	MCFSIM_DMR1		0x5c		/* DRAM 1 Mask reg (r/w) */
#define	MCFSIM_DCR1		0x63		/* DRAM 1 Control reg (r/w) */

#define	MCFSIM_CSAR0		0x64		/* CS 0 Address 0 reg (r/w) */
#define	MCFSIM_CSMR0		0x68		/* CS 0 Mask 0 reg (r/w) */
#define	MCFSIM_CSCR0		0x6e		/* CS 0 Control reg (r/w) */
#define	MCFSIM_CSAR1		0x70		/* CS 1 Address reg (r/w) */
#define	MCFSIM_CSMR1		0x74		/* CS 1 Mask reg (r/w) */
#define	MCFSIM_CSCR1		0x7a		/* CS 1 Control reg (r/w) */
#define	MCFSIM_CSAR2		0x7c		/* CS 2 Address reg (r/w) */
#define	MCFSIM_CSMR2		0x80		/* CS 2 Mask reg (r/w) */
#define	MCFSIM_CSCR2		0x86		/* CS 2 Control reg (r/w) */
#define	MCFSIM_CSAR3		0x88		/* CS 3 Address reg (r/w) */
#define	MCFSIM_CSMR3		0x8c		/* CS 3 Mask reg (r/w) */
#define	MCFSIM_CSCR3		0x92		/* CS 3 Control reg (r/w) */
#define	MCFSIM_CSAR4		0x94		/* CS 4 Address reg (r/w) */
#define	MCFSIM_CSMR4		0x98		/* CS 4 Mask reg (r/w) */
#define	MCFSIM_CSCR4		0x9e		/* CS 4 Control reg (r/w) */
#define	MCFSIM_CSAR5		0xa0		/* CS 5 Address reg (r/w) */
#define	MCFSIM_CSMR5		0xa4		/* CS 5 Mask reg (r/w) */
#define	MCFSIM_CSCR5		0xaa		/* CS 5 Control reg (r/w) */
#define	MCFSIM_CSAR6		0xac		/* CS 6 Address reg (r/w) */
#define	MCFSIM_CSMR6		0xb0		/* CS 6 Mask reg (r/w) */
#define	MCFSIM_CSCR6		0xb6		/* CS 6 Control reg (r/w) */
#define	MCFSIM_CSAR7		0xb8		/* CS 7 Address reg (r/w) */
#define	MCFSIM_CSMR7		0xbc		/* CS 7 Mask reg (r/w) */
#define	MCFSIM_CSCR7		0xc2		/* CS 7 Control reg (r/w) */
#define	MCFSIM_DMCR		0xc6		/* Default control */

#ifdef CONFIG_M5206e
#define	MCFSIM_PAR		0xca		/* Pin Assignment reg (r/w) */
#else
#define	MCFSIM_PAR		0xcb		/* Pin Assignment reg (r/w) */
#endif

#define	MCFSIM_PADDR		(MCF_MBAR + 0x1c5)	/* Parallel Direction (r/w) */
#define	MCFSIM_PADAT		(MCF_MBAR + 0x1c9)	/* Parallel Port Value (r/w) */

/*
 *	Define system peripheral IRQ usage.
 */
#define	MCF_IRQ_TIMER		30		/* Timer0, Level 6 */
#define	MCF_IRQ_PROFILER	31		/* Timer1, Level 7 */

/*
 * Generic GPIO
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
#define	MCFSIM_UART1ICR		MCFSIM_ICR12	/* UART 1 ICR */
#define	MCFSIM_UART2ICR		MCFSIM_ICR13	/* UART 2 ICR */
#ifdef CONFIG_M5206e
#define	MCFSIM_DMA1ICR		MCFSIM_ICR14	/* DMA 1 ICR */
#define	MCFSIM_DMA2ICR		MCFSIM_ICR15	/* DMA 2 ICR */
#endif

/****************************************************************************/
#endif	/* m5206sim_h */
