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

#define	CPU_NAME		"COLDFIRE(m5272)"
#define	CPU_INSTR_PER_JIFFY	3
#define	MCF_BUSCLK		MCF_CLK

#include <asm/m52xxacr.h>

/*
 *	Define the 5272 SIM register set addresses.
 */
#define	MCFSIM_SCR		(MCF_MBAR + 0x04)	/* SIM Config reg */
#define	MCFSIM_SPR		(MCF_MBAR + 0x06)	/* System Protection */
#define	MCFSIM_PMR		(MCF_MBAR + 0x08)	/* Power Management */
#define	MCFSIM_APMR		(MCF_MBAR + 0x0e)	/* Active Low Power */
#define	MCFSIM_DIR		(MCF_MBAR + 0x10)	/* Device Identity */

#define	MCFSIM_ICR1		(MCF_MBAR + 0x20)	/* Intr Ctrl reg 1 */
#define	MCFSIM_ICR2		(MCF_MBAR + 0x24)	/* Intr Ctrl reg 2 */
#define	MCFSIM_ICR3		(MCF_MBAR + 0x28)	/* Intr Ctrl reg 3 */
#define	MCFSIM_ICR4		(MCF_MBAR + 0x2c)	/* Intr Ctrl reg 4 */

#define	MCFSIM_ISR		(MCF_MBAR + 0x30)	/* Intr Source */
#define	MCFSIM_PITR		(MCF_MBAR + 0x34)	/* Intr Transition */
#define	MCFSIM_PIWR		(MCF_MBAR + 0x38)	/* Intr Wakeup */
#define	MCFSIM_PIVR		(MCF_MBAR + 0x3f)	/* Intr Vector */

#define	MCFSIM_WRRR		(MCF_MBAR + 0x280)	/* Watchdog reference */
#define	MCFSIM_WIRR		(MCF_MBAR + 0x284)	/* Watchdog interrupt */
#define	MCFSIM_WCR		(MCF_MBAR + 0x288)	/* Watchdog counter */
#define	MCFSIM_WER		(MCF_MBAR + 0x28c)	/* Watchdog event */

#define	MCFSIM_CSBR0		(MCF_MBAR + 0x40)	/* CS0 Base Address */
#define	MCFSIM_CSOR0		(MCF_MBAR + 0x44)	/* CS0 Option */
#define	MCFSIM_CSBR1		(MCF_MBAR + 0x48)	/* CS1 Base Address */
#define	MCFSIM_CSOR1		(MCF_MBAR + 0x4c)	/* CS1 Option */
#define	MCFSIM_CSBR2		(MCF_MBAR + 0x50)	/* CS2 Base Address */
#define	MCFSIM_CSOR2		(MCF_MBAR + 0x54)	/* CS2 Option */
#define	MCFSIM_CSBR3		(MCF_MBAR + 0x58)	/* CS3 Base Address */
#define	MCFSIM_CSOR3		(MCF_MBAR + 0x5c)	/* CS3 Option */
#define	MCFSIM_CSBR4		(MCF_MBAR + 0x60)	/* CS4 Base Address */
#define	MCFSIM_CSOR4		(MCF_MBAR + 0x64)	/* CS4 Option */
#define	MCFSIM_CSBR5		(MCF_MBAR + 0x68)	/* CS5 Base Address */
#define	MCFSIM_CSOR5		(MCF_MBAR + 0x6c)	/* CS5 Option */
#define	MCFSIM_CSBR6		(MCF_MBAR + 0x70)	/* CS6 Base Address */
#define	MCFSIM_CSOR6		(MCF_MBAR + 0x74)	/* CS6 Option */
#define	MCFSIM_CSBR7		(MCF_MBAR + 0x78)	/* CS7 Base Address */
#define	MCFSIM_CSOR7		(MCF_MBAR + 0x7c)	/* CS7 Option */

#define	MCFSIM_SDCR		(MCF_MBAR + 0x180)	/* SDRAM Config */
#define	MCFSIM_SDTR		(MCF_MBAR + 0x184)	/* SDRAM Timing */
#define	MCFSIM_DCAR0		(MCF_MBAR + 0x4c)	/* DRAM 0 Address */
#define	MCFSIM_DCMR0		(MCF_MBAR + 0x50)	/* DRAM 0 Mask */
#define	MCFSIM_DCCR0		(MCF_MBAR + 0x57)	/* DRAM 0 Control */
#define	MCFSIM_DCAR1		(MCF_MBAR + 0x58)	/* DRAM 1 Address */
#define	MCFSIM_DCMR1		(MCF_MBAR + 0x5c)	/* DRAM 1 Mask reg */
#define	MCFSIM_DCCR1		(MCF_MBAR + 0x63)	/* DRAM 1 Control */

#define	MCFUART_BASE0		(MCF_MBAR + 0x100) /* Base address UART0 */
#define	MCFUART_BASE1		(MCF_MBAR + 0x140) /* Base address UART1 */

#define	MCFSIM_PACNT		(MCF_MBAR + 0x80) /* Port A Control (r/w) */
#define	MCFSIM_PADDR		(MCF_MBAR + 0x84) /* Port A Direction (r/w) */
#define	MCFSIM_PADAT		(MCF_MBAR + 0x86) /* Port A Data (r/w) */
#define	MCFSIM_PBCNT		(MCF_MBAR + 0x88) /* Port B Control (r/w) */
#define	MCFSIM_PBDDR		(MCF_MBAR + 0x8c) /* Port B Direction (r/w) */
#define	MCFSIM_PBDAT		(MCF_MBAR + 0x8e) /* Port B Data (r/w) */
#define	MCFSIM_PCDDR		(MCF_MBAR + 0x94) /* Port C Direction (r/w) */
#define	MCFSIM_PCDAT		(MCF_MBAR + 0x96) /* Port C Data (r/w) */
#define	MCFSIM_PDCNT		(MCF_MBAR + 0x98) /* Port D Control (r/w) */

#define	MCFDMA_BASE0		(MCF_MBAR + 0xe0) /* Base address DMA 0 */

#define	MCFTIMER_BASE1		(MCF_MBAR + 0x200) /* Base address TIMER1 */
#define	MCFTIMER_BASE2		(MCF_MBAR + 0x220) /* Base address TIMER2 */
#define	MCFTIMER_BASE3		(MCF_MBAR + 0x240) /* Base address TIMER4 */
#define	MCFTIMER_BASE4		(MCF_MBAR + 0x260) /* Base address TIMER3 */

#define	MCFFEC_BASE0		(MCF_MBAR + 0x840) /* Base FEC ethernet */
#define	MCFFEC_SIZE0		0x1d0

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
#define	MCF_IRQ_UART0		73		/* UART 0 */
#define	MCF_IRQ_UART1		74		/* UART 1 */
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
#define	MCF_IRQ_FECRX0		86		/* Ethernet Receiver */
#define	MCF_IRQ_FECTX0		87		/* Ethernet Transmitter */
#define	MCF_IRQ_FECENTC0	88		/* Ethernet Non-Time Critical */
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
#define MCFGPIO_PIN_MAX		48
#define MCFGPIO_IRQ_MAX		-1
#define MCFGPIO_IRQ_VECBASE	-1

/****************************************************************************/
#endif	/* m5272sim_h */
