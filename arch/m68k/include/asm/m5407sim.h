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

/*
 *	Define the 5407 SIM register set addresses.
 */
#define	MCFSIM_RSR		0x00		/* Reset Status reg (r/w) */
#define	MCFSIM_SYPCR		0x01		/* System Protection reg (r/w)*/
#define	MCFSIM_SWIVR		0x02		/* SW Watchdog intr reg (r/w) */
#define	MCFSIM_SWSR		0x03		/* SW Watchdog service (r/w) */
#define	MCFSIM_PAR		0x04		/* Pin Assignment reg (r/w) */
#define	MCFSIM_IRQPAR		0x06		/* Interrupt Assignment reg (r/w) */
#define	MCFSIM_PLLCR		0x08		/* PLL Controll Reg*/
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

#define MCFSIM_DCR		0x100		/* DRAM Control reg (r/w) */
#define MCFSIM_DACR0		0x108		/* DRAM 0 Addr and Ctrl (r/w) */
#define MCFSIM_DMR0		0x10c		/* DRAM 0 Mask reg (r/w) */
#define MCFSIM_DACR1		0x110		/* DRAM 1 Addr and Ctrl (r/w) */
#define MCFSIM_DMR1		0x114		/* DRAM 1 Mask reg (r/w) */

#define	MCFSIM_PADDR		(MCF_MBAR + 0x244)
#define	MCFSIM_PADAT		(MCF_MBAR + 0x248)

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

/*
 *	Define the Cache register flags.
 */
#define	CACR_DEC		0x80000000	/* Enable data cache */
#define	CACR_DWP		0x40000000	/* Data write protection */
#define	CACR_DESB		0x20000000	/* Enable data store buffer */
#define	CACR_DDPI		0x10000000	/* Disable CPUSHL */
#define	CACR_DHCLK		0x08000000	/* Half data cache lock mode */
#define	CACR_DDCM_WT		0x00000000	/* Write through cache*/
#define	CACR_DDCM_CP		0x02000000	/* Copyback cache */
#define	CACR_DDCM_P		0x04000000	/* No cache, precise */
#define	CACR_DDCM_IMP		0x06000000	/* No cache, imprecise */
#define	CACR_DCINVA		0x01000000	/* Invalidate data cache */
#define	CACR_BEC		0x00080000	/* Enable branch cache */
#define	CACR_BCINVA		0x00040000	/* Invalidate branch cache */
#define	CACR_IEC		0x00008000	/* Enable instruction cache */
#define	CACR_DNFB		0x00002000	/* Inhibited fill buffer */
#define	CACR_IDPI		0x00001000	/* Disable CPUSHL */
#define	CACR_IHLCK		0x00000800	/* Intruction cache half lock */
#define	CACR_IDCM		0x00000400	/* Intruction cache inhibit */
#define	CACR_ICINVA		0x00000100	/* Invalidate instr cache */

#define	ACR_BASE_POS		24		/* Address Base */
#define	ACR_MASK_POS		16		/* Address Mask */
#define	ACR_ENABLE		0x00008000	/* Enable address */
#define	ACR_USER		0x00000000	/* User mode access only */
#define	ACR_SUPER		0x00002000	/* Supervisor mode only */
#define	ACR_ANY			0x00004000	/* Match any access mode */
#define	ACR_CM_WT		0x00000000	/* Write through mode */
#define	ACR_CM_CP		0x00000020	/* Copyback mode */
#define	ACR_CM_OFF_PRE		0x00000040	/* No cache, precise */
#define	ACR_CM_OFF_IMP		0x00000060	/* No cache, imprecise */
#define	ACR_WPROTECT		0x00000004	/* Write protect */

/****************************************************************************/
#endif	/* m5407sim_h */
