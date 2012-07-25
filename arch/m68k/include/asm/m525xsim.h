/****************************************************************************/

/*
 *	m525xsim.h -- ColdFire 525x System Integration Module support.
 *
 *	(C) Copyright 2012, Steven king <sfking@fdwdc.com>
 *	(C) Copyright 2002, Greg Ungerer (gerg@snapgear.com)
 */

/****************************************************************************/
#ifndef	m525xsim_h
#define m525xsim_h
/****************************************************************************/

#define CPU_NAME		"COLDFIRE(m525x)"
#define CPU_INSTR_PER_JIFFY	3
#define MCF_BUSCLK		(MCF_CLK / 2)

#include <asm/m52xxacr.h>

/*
 *	The 525x has a second MBAR region, define its address.
 */
#define MCF_MBAR2		0x80000000

/*
 *	Define the 525x SIM register set addresses.
 */
#define MCFSIM_RSR		0x00		/* Reset Status reg (r/w) */
#define MCFSIM_SYPCR		0x01		/* System Protection reg (r/w)*/
#define MCFSIM_SWIVR		0x02		/* SW Watchdog intr reg (r/w) */
#define MCFSIM_SWSR		0x03		/* SW Watchdog service (r/w) */
#define MCFSIM_MPARK		0x0C		/* BUS Master Control Reg*/
#define MCFSIM_IPR		0x40		/* Interrupt Pend reg (r/w) */
#define MCFSIM_IMR		0x44		/* Interrupt Mask reg (r/w) */
#define MCFSIM_ICR0		0x4c		/* Intr Ctrl reg 0 (r/w) */
#define MCFSIM_ICR1		0x4d		/* Intr Ctrl reg 1 (r/w) */
#define MCFSIM_ICR2		0x4e		/* Intr Ctrl reg 2 (r/w) */
#define MCFSIM_ICR3		0x4f		/* Intr Ctrl reg 3 (r/w) */
#define MCFSIM_ICR4		0x50		/* Intr Ctrl reg 4 (r/w) */
#define MCFSIM_ICR5		0x51		/* Intr Ctrl reg 5 (r/w) */
#define MCFSIM_ICR6		0x52		/* Intr Ctrl reg 6 (r/w) */
#define MCFSIM_ICR7		0x53		/* Intr Ctrl reg 7 (r/w) */
#define MCFSIM_ICR8		0x54		/* Intr Ctrl reg 8 (r/w) */
#define MCFSIM_ICR9		0x55		/* Intr Ctrl reg 9 (r/w) */
#define MCFSIM_ICR10		0x56		/* Intr Ctrl reg 10 (r/w) */
#define MCFSIM_ICR11		0x57		/* Intr Ctrl reg 11 (r/w) */

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

#define MCFSIM_DCR		(MCF_MBAR + 0x100)	/* DRAM Control */
#define MCFSIM_DACR0		(MCF_MBAR + 0x108)	/* DRAM 0 Addr/Ctrl */
#define MCFSIM_DMR0		(MCF_MBAR + 0x10c)	/* DRAM 0 Mask */

/*
 * Secondary Interrupt Controller (in MBAR2)
*/
#define MCFINTC2_INTBASE	(MCF_MBAR2 + 0x168)	/* Base Vector Reg */
#define MCFINTC2_INTPRI1	(MCF_MBAR2 + 0x140)	/* 0-7 priority */
#define MCFINTC2_INTPRI2	(MCF_MBAR2 + 0x144)	/* 8-15 priority */
#define MCFINTC2_INTPRI3	(MCF_MBAR2 + 0x148)	/* 16-23 priority */
#define MCFINTC2_INTPRI4	(MCF_MBAR2 + 0x14c)	/* 24-31 priority */
#define MCFINTC2_INTPRI5	(MCF_MBAR2 + 0x150)	/* 32-39 priority */
#define MCFINTC2_INTPRI6	(MCF_MBAR2 + 0x154)	/* 40-47 priority */
#define MCFINTC2_INTPRI7	(MCF_MBAR2 + 0x158)	/* 48-55 priority */
#define MCFINTC2_INTPRI8	(MCF_MBAR2 + 0x15c)	/* 56-63 priority */

#define MCFINTC2_INTPRI_REG(i)	(MCFINTC2_INTPRI1 + \
				((((i) - MCFINTC2_VECBASE) / 8) * 4))
#define MCFINTC2_INTPRI_BITS(b, i)	((b) << (((i) % 8) * 4))

/*
 *	Timer module.
 */
#define MCFTIMER_BASE1		(MCF_MBAR + 0x140)	/* Base of TIMER1 */
#define MCFTIMER_BASE2		(MCF_MBAR + 0x180)	/* Base of TIMER2 */

/*
 *	UART module.
 */
#define MCFUART_BASE0		(MCF_MBAR + 0x1c0)	/* Base address UART0 */
#define MCFUART_BASE1		(MCF_MBAR + 0x200)	/* Base address UART1 */

/*
 *	QSPI module.
 */
#define MCFQSPI_BASE		(MCF_MBAR + 0x300)	/* Base address QSPI */
#define MCFQSPI_SIZE		0x40			/* Register set size */


#define MCFQSPI_CS0		15
#define MCFQSPI_CS1		16
#define MCFQSPI_CS2		24
#define MCFQSPI_CS3		28

/*
 *	I2C module.
 */
#define MCFI2C_BASE0		(MCF_MBAR + 0x280)	/* Base addreess I2C0 */
#define MCFI2C_SIZE0		0x20			/* Register set size */

#define MCFI2C_BASE1		(MCF_MBAR2 + 0x440)	/* Base addreess I2C1 */
#define MCFI2C_SIZE1		0x20			/* Register set size */
/*
 *	DMA unit base addresses.
 */
#define MCFDMA_BASE0		(MCF_MBAR + 0x300)	/* Base address DMA 0 */
#define MCFDMA_BASE1		(MCF_MBAR + 0x340)	/* Base address DMA 1 */
#define MCFDMA_BASE2		(MCF_MBAR + 0x380)	/* Base address DMA 2 */
#define MCFDMA_BASE3		(MCF_MBAR + 0x3C0)	/* Base address DMA 3 */

/*
 *	Some symbol defines for the above...
 */
#define MCFSIM_SWDICR		MCFSIM_ICR0	/* Watchdog timer ICR */
#define MCFSIM_TIMER1ICR	MCFSIM_ICR1	/* Timer 1 ICR */
#define MCFSIM_TIMER2ICR	MCFSIM_ICR2	/* Timer 2 ICR */
#define MCFSIM_I2CICR		MCFSIM_ICR3	/* I2C ICR */
#define MCFSIM_UART1ICR		MCFSIM_ICR4	/* UART 1 ICR */
#define MCFSIM_UART2ICR		MCFSIM_ICR5	/* UART 2 ICR */
#define MCFSIM_DMA0ICR		MCFSIM_ICR6	/* DMA 0 ICR */
#define MCFSIM_DMA1ICR		MCFSIM_ICR7	/* DMA 1 ICR */
#define MCFSIM_DMA2ICR		MCFSIM_ICR8	/* DMA 2 ICR */
#define MCFSIM_DMA3ICR		MCFSIM_ICR9	/* DMA 3 ICR */
#define MCFSIM_QSPIICR		MCFSIM_ICR10	/* QSPI ICR */

/*
 *	Define system peripheral IRQ usage.
 */
#define MCF_IRQ_QSPI		28		/* QSPI, Level 4 */
#define MCF_IRQ_I2C0		29
#define MCF_IRQ_TIMER		30		/* Timer0, Level 6 */
#define MCF_IRQ_PROFILER	31		/* Timer1, Level 7 */

#define MCF_IRQ_UART0		73		/* UART0 */
#define MCF_IRQ_UART1		74		/* UART1 */

/*
 * Define the base interrupt for the second interrupt controller.
 * We set it to 128, out of the way of the base interrupts, and plenty
 * of room for its 64 interrupts.
 */
#define MCFINTC2_VECBASE	128

#define MCF_IRQ_GPIO0		(MCFINTC2_VECBASE + 32)
#define MCF_IRQ_GPIO1		(MCFINTC2_VECBASE + 33)
#define MCF_IRQ_GPIO2		(MCFINTC2_VECBASE + 34)
#define MCF_IRQ_GPIO3		(MCFINTC2_VECBASE + 35)
#define MCF_IRQ_GPIO4		(MCFINTC2_VECBASE + 36)
#define MCF_IRQ_GPIO5		(MCFINTC2_VECBASE + 37)
#define MCF_IRQ_GPIO6		(MCFINTC2_VECBASE + 38)

#define MCF_IRQ_USBWUP		(MCFINTC2_VECBASE + 40)
#define MCF_IRQ_I2C1		(MCFINTC2_VECBASE + 62)

/*
 *	General purpose IO registers (in MBAR2).
 */
#define MCFSIM2_GPIOREAD	(MCF_MBAR2 + 0x000)	/* GPIO read values */
#define MCFSIM2_GPIOWRITE	(MCF_MBAR2 + 0x004)	/* GPIO write values */
#define MCFSIM2_GPIOENABLE	(MCF_MBAR2 + 0x008)	/* GPIO enabled */
#define MCFSIM2_GPIOFUNC	(MCF_MBAR2 + 0x00C)	/* GPIO function */
#define MCFSIM2_GPIO1READ	(MCF_MBAR2 + 0x0B0)	/* GPIO1 read values */
#define MCFSIM2_GPIO1WRITE	(MCF_MBAR2 + 0x0B4)	/* GPIO1 write values */
#define MCFSIM2_GPIO1ENABLE	(MCF_MBAR2 + 0x0B8)	/* GPIO1 enabled */
#define MCFSIM2_GPIO1FUNC	(MCF_MBAR2 + 0x0BC)	/* GPIO1 function */

#define MCFSIM2_GPIOINTSTAT	(MCF_MBAR2 + 0xc0)	/* GPIO intr status */
#define MCFSIM2_GPIOINTCLEAR	(MCF_MBAR2 + 0xc0)	/* GPIO intr clear */
#define MCFSIM2_GPIOINTENABLE	(MCF_MBAR2 + 0xc4)	/* GPIO intr enable */

/*
 * Generic GPIO support
 */
#define MCFGPIO_PIN_MAX		64
#define MCFGPIO_IRQ_MAX		7
#define MCFGPIO_IRQ_VECBASE	MCF_IRQ_GPIO0

/****************************************************************************/
#endif	/* m525xsim_h */
