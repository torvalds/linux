/****************************************************************************/

/*
 *	m5272sim.h -- ColdFire 5272 System Integration Module support.
 *
 *	(C) Copyright 1999, Greg Ungerer (gerg@snapgear.com)
 * 	(C) Copyright 2000, Lineo Inc. (www.lineo.com) 
 */

/****************************************************************************/
#ifndef	m5272sim_h
#define	m5272sim_h
/****************************************************************************/

/*
 *	Define the 5272 SIM register set addresses.
 */
#define	MCFSIM_SCR		0x04		/* SIM Config reg (r/w) */
#define	MCFSIM_SPR		0x06		/* System Protection reg (r/w)*/
#define	MCFSIM_PMR		0x08		/* Power Management reg (r/w) */
#define	MCFSIM_APMR		0x0e		/* Active Low Power reg (r/w) */
#define	MCFSIM_DIR		0x10		/* Device Identity reg (r/w) */

#define	MCFSIM_ICR1		0x20		/* Intr Ctrl reg 1 (r/w) */
#define	MCFSIM_ICR2		0x24		/* Intr Ctrl reg 2 (r/w) */
#define	MCFSIM_ICR3		0x28		/* Intr Ctrl reg 3 (r/w) */
#define	MCFSIM_ICR4		0x2c		/* Intr Ctrl reg 4 (r/w) */

#define MCFSIM_ISR		0x30		/* Interrupt Source reg (r/w) */
#define MCFSIM_PITR		0x34		/* Interrupt Transition (r/w) */
#define	MCFSIM_PIWR		0x38		/* Interrupt Wakeup reg (r/w) */
#define	MCFSIM_PIVR		0x3f		/* Interrupt Vector reg (r/w( */

#define	MCFSIM_WRRR		0x280		/* Watchdog reference (r/w) */
#define	MCFSIM_WIRR		0x284		/* Watchdog interrupt (r/w) */
#define	MCFSIM_WCR		0x288		/* Watchdog counter (r/w) */
#define	MCFSIM_WER		0x28c		/* Watchdog event (r/w) */

#define	MCFSIM_CSBR0		0x40		/* CS0 Base Address (r/w) */
#define	MCFSIM_CSOR0		0x44		/* CS0 Option (r/w) */
#define	MCFSIM_CSBR1		0x48		/* CS1 Base Address (r/w) */
#define	MCFSIM_CSOR1		0x4c		/* CS1 Option (r/w) */
#define	MCFSIM_CSBR2		0x50		/* CS2 Base Address (r/w) */
#define	MCFSIM_CSOR2		0x54		/* CS2 Option (r/w) */
#define	MCFSIM_CSBR3		0x58		/* CS3 Base Address (r/w) */
#define	MCFSIM_CSOR3		0x5c		/* CS3 Option (r/w) */
#define	MCFSIM_CSBR4		0x60		/* CS4 Base Address (r/w) */
#define	MCFSIM_CSOR4		0x64		/* CS4 Option (r/w) */
#define	MCFSIM_CSBR5		0x68		/* CS5 Base Address (r/w) */
#define	MCFSIM_CSOR5		0x6c		/* CS5 Option (r/w) */
#define	MCFSIM_CSBR6		0x70		/* CS6 Base Address (r/w) */
#define	MCFSIM_CSOR6		0x74		/* CS6 Option (r/w) */
#define	MCFSIM_CSBR7		0x78		/* CS7 Base Address (r/w) */
#define	MCFSIM_CSOR7		0x7c		/* CS7 Option (r/w) */

#define	MCFSIM_SDCR		0x180		/* SDRAM Configuration (r/w) */
#define	MCFSIM_SDTR		0x184		/* SDRAM Timing (r/w) */
#define	MCFSIM_DCAR0		0x4c		/* DRAM 0 Address reg(r/w) */
#define	MCFSIM_DCMR0		0x50		/* DRAM 0 Mask reg (r/w) */
#define	MCFSIM_DCCR0		0x57		/* DRAM 0 Control reg (r/w) */
#define	MCFSIM_DCAR1		0x58		/* DRAM 1 Address reg (r/w) */
#define	MCFSIM_DCMR1		0x5c		/* DRAM 1 Mask reg (r/w) */
#define	MCFSIM_DCCR1		0x63		/* DRAM 1 Control reg (r/w) */

#define	MCFSIM_PACNT		(MCF_MBAR + 0x80) /* Port A Control (r/w) */
#define	MCFSIM_PADDR		(MCF_MBAR + 0x84) /* Port A Direction (r/w) */
#define	MCFSIM_PADAT		(MCF_MBAR + 0x86) /* Port A Data (r/w) */
#define	MCFSIM_PBCNT		(MCF_MBAR + 0x88) /* Port B Control (r/w) */
#define	MCFSIM_PBDDR		(MCF_MBAR + 0x8c) /* Port B Direction (r/w) */
#define	MCFSIM_PBDAT		(MCF_MBAR + 0x8e) /* Port B Data (r/w) */
#define	MCFSIM_PCDDR		(MCF_MBAR + 0x94) /* Port C Direction (r/w) */
#define	MCFSIM_PCDAT		(MCF_MBAR + 0x96) /* Port C Data (r/w) */
#define	MCFSIM_PDCNT		(MCF_MBAR + 0x98) /* Port D Control (r/w) */

/*
 *	Define system peripheral IRQ usage.
 */
#define	MCFINT_VECBASE		64		/* Base of interrupts */
#define	MCF_IRQ_SPURIOUS	64		/* User Spurious */
#define	MCF_IRQ_EINT1		65		/* External Interrupt 1 */
#define	MCF_IRQ_EINT2		66		/* External Interrupt 2 */
#define	MCF_IRQ_EINT3		67		/* External Interrupt 3 */
#define	MCF_IRQ_EINT4		68		/* External Interrupt 4 */
#define	MCF_IRQ_TIMER1		69		/* Timer 1 */
#define	MCF_IRQ_TIMER2		70		/* Timer 2 */
#define	MCF_IRQ_TIMER3		71		/* Timer 3 */
#define	MCF_IRQ_TIMER4		72		/* Timer 4 */
#define	MCF_IRQ_UART1		73		/* UART 1 */
#define	MCF_IRQ_UART2		74		/* UART 2 */
#define	MCF_IRQ_PLIP		75		/* PLIC 2Khz Periodic */
#define	MCF_IRQ_PLIA		76		/* PLIC Asynchronous */
#define	MCF_IRQ_USB0		77		/* USB Endpoint 0 */
#define	MCF_IRQ_USB1		78		/* USB Endpoint 1 */
#define	MCF_IRQ_USB2		79		/* USB Endpoint 2 */
#define	MCF_IRQ_USB3		80		/* USB Endpoint 3 */
#define	MCF_IRQ_USB4		81		/* USB Endpoint 4 */
#define	MCF_IRQ_USB5		82		/* USB Endpoint 5 */
#define	MCF_IRQ_USB6		83		/* USB Endpoint 6 */
#define	MCF_IRQ_USB7		84		/* USB Endpoint 7 */
#define	MCF_IRQ_DMA		85		/* DMA Controller */
#define	MCF_IRQ_ERX		86		/* Ethernet Receiver */
#define	MCF_IRQ_ETX		87		/* Ethernet Transmitter */
#define	MCF_IRQ_ENTC		88		/* Ethernet Non-Time Critical */
#define	MCF_IRQ_QSPI		89		/* Queued Serial Interface */
#define	MCF_IRQ_EINT5		90		/* External Interrupt 5 */
#define	MCF_IRQ_EINT6		91		/* External Interrupt 6 */
#define	MCF_IRQ_SWTO		92		/* Software Watchdog */
#define	MCFINT_VECMAX		95		/* Maxmum interrupt */

#define	MCF_IRQ_TIMER		MCF_IRQ_TIMER1
#define	MCF_IRQ_PROFILER	MCF_IRQ_TIMER2

/*
 * Generic GPIO support
 */
#define MCFGPIO_PIN_MAX			48
#define MCFGPIO_IRQ_MAX			-1
#define MCFGPIO_IRQ_VECBASE		-1
/****************************************************************************/
#endif	/* m5272sim_h */
