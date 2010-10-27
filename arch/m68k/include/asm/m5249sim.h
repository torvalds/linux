/****************************************************************************/

/*
 *	m5249sim.h -- ColdFire 5249 System Integration Module support.
 *
 *	(C) Copyright 2002, Greg Ungerer (gerg@snapgear.com)
 */

/****************************************************************************/
#ifndef	m5249sim_h
#define	m5249sim_h
/****************************************************************************/

/*
 *	Define the 5249 SIM register set addresses.
 */
#define	MCFSIM_RSR		0x00		/* Reset Status reg (r/w) */
#define	MCFSIM_SYPCR		0x01		/* System Protection reg (r/w)*/
#define	MCFSIM_SWIVR		0x02		/* SW Watchdog intr reg (r/w) */
#define	MCFSIM_SWSR		0x03		/* SW Watchdog service (r/w) */
#define	MCFSIM_PAR		0x04		/* Pin Assignment reg (r/w) */
#define	MCFSIM_IRQPAR		0x06		/* Interrupt Assignment reg (r/w) */
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

#define MCFSIM_DCR		0x100		/* DRAM Control reg (r/w) */
#define MCFSIM_DACR0		0x108		/* DRAM 0 Addr and Ctrl (r/w) */
#define MCFSIM_DMR0		0x10c		/* DRAM 0 Mask reg (r/w) */
#define MCFSIM_DACR1		0x110		/* DRAM 1 Addr and Ctrl (r/w) */
#define MCFSIM_DMR1		0x114		/* DRAM 1 Mask reg (r/w) */


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
#define	MCFSIM_QSPIICR		MCFSIM_ICR10	/* QSPI ICR */

/*
 *	Define system peripheral IRQ usage.
 */
#define	MCF_IRQ_QSPI		28		/* QSPI, Level 4 */
#define	MCF_IRQ_TIMER		30		/* Timer0, Level 6 */
#define	MCF_IRQ_PROFILER	31		/* Timer1, Level 7 */

/*
 *	General purpose IO registers (in MBAR2).
 */
#define	MCFSIM2_GPIOREAD	(MCF_MBAR2 + 0x000)	/* GPIO read values */
#define	MCFSIM2_GPIOWRITE	(MCF_MBAR2 + 0x004)	/* GPIO write values */
#define	MCFSIM2_GPIOENABLE	(MCF_MBAR2 + 0x008)	/* GPIO enabled */
#define	MCFSIM2_GPIOFUNC	(MCF_MBAR2 + 0x00C)	/* GPIO function */
#define	MCFSIM2_GPIO1READ	(MCF_MBAR2 + 0x0B0)	/* GPIO1 read values */
#define	MCFSIM2_GPIO1WRITE	(MCF_MBAR2 + 0x0B4)	/* GPIO1 write values */
#define	MCFSIM2_GPIO1ENABLE	(MCF_MBAR2 + 0x0B8)	/* GPIO1 enabled */
#define	MCFSIM2_GPIO1FUNC	(MCF_MBAR2 + 0x0BC)	/* GPIO1 function */

#define	MCFSIM2_GPIOINTSTAT	0xc0		/* GPIO interrupt status */
#define	MCFSIM2_GPIOINTCLEAR	0xc0		/* GPIO interrupt clear */
#define	MCFSIM2_GPIOINTENABLE	0xc4		/* GPIO interrupt enable */

#define	MCFSIM2_INTLEVEL1	0x140		/* Interrupt level reg 1 */
#define	MCFSIM2_INTLEVEL2	0x144		/* Interrupt level reg 2 */
#define	MCFSIM2_INTLEVEL3	0x148		/* Interrupt level reg 3 */
#define	MCFSIM2_INTLEVEL4	0x14c		/* Interrupt level reg 4 */
#define	MCFSIM2_INTLEVEL5	0x150		/* Interrupt level reg 5 */
#define	MCFSIM2_INTLEVEL6	0x154		/* Interrupt level reg 6 */
#define	MCFSIM2_INTLEVEL7	0x158		/* Interrupt level reg 7 */
#define	MCFSIM2_INTLEVEL8	0x15c		/* Interrupt level reg 8 */

#define	MCFSIM2_DMAROUTE	0x188		/* DMA routing */

#define	MCFSIM2_IDECONFIG1	0x18c		/* IDEconfig1 */
#define	MCFSIM2_IDECONFIG2	0x190		/* IDEconfig2 */

/*
 * Define the base interrupt for the second interrupt controller.
 * We set it to 128, out of the way of the base interrupts, and plenty
 * of room for its 64 interrupts.
 */
#define	MCFINTC2_VECBASE	128

#define	MCFINTC2_GPIOIRQ0	(MCFINTC2_VECBASE + 32)
#define	MCFINTC2_GPIOIRQ1	(MCFINTC2_VECBASE + 33)
#define	MCFINTC2_GPIOIRQ2	(MCFINTC2_VECBASE + 34)
#define	MCFINTC2_GPIOIRQ3	(MCFINTC2_VECBASE + 35)
#define	MCFINTC2_GPIOIRQ4	(MCFINTC2_VECBASE + 36)
#define	MCFINTC2_GPIOIRQ5	(MCFINTC2_VECBASE + 37)
#define	MCFINTC2_GPIOIRQ6	(MCFINTC2_VECBASE + 38)
#define	MCFINTC2_GPIOIRQ7	(MCFINTC2_VECBASE + 39)

/*
 * Generic GPIO support
 */
#define MCFGPIO_PIN_MAX		64
#define MCFGPIO_IRQ_MAX		-1
#define MCFGPIO_IRQ_VECBASE	-1

/****************************************************************************/

#ifdef __ASSEMBLER__

/*
 *	The M5249C3 board needs a little help getting all its SIM devices
 *	initialized at kernel start time. dBUG doesn't set much up, so
 *	we need to do it manually.
 */
.macro m5249c3_setup
	/*
	 *	Set MBAR1 and MBAR2, just incase they are not set.
	 */
	movel	#0x10000001,%a0
	movec	%a0,%MBAR			/* map MBAR region */
	subql	#1,%a0				/* get MBAR address in a0 */

	movel	#0x80000001,%a1
	movec	%a1,#3086			/* map MBAR2 region */
	subql	#1,%a1				/* get MBAR2 address in a1 */

	/*
	 *      Move secondary interrupts to their base (128).
	 */
	moveb	#MCFINTC2_VECBASE,%d0
	moveb	%d0,0x16b(%a1)			/* interrupt base register */

	/*
	 *      Work around broken CSMR0/DRAM vector problem.
	 */
	movel	#0x001F0021,%d0			/* disable C/I bit */
	movel	%d0,0x84(%a0)			/* set CSMR0 */

	/*
	 *	Disable the PLL firstly. (Who knows what state it is
	 *	in here!).
	 */
	movel	0x180(%a1),%d0			/* get current PLL value */
	andl	#0xfffffffe,%d0			/* PLL bypass first */
	movel	%d0,0x180(%a1)			/* set PLL register */
	nop

#if CONFIG_CLOCK_FREQ == 140000000
	/*
	 *	Set initial clock frequency. This assumes M5249C3 board
	 *	is fitted with 11.2896MHz crystal. It will program the
	 *	PLL for 140MHz. Lets go fast :-)
	 */
	movel	#0x125a40f0,%d0			/* set for 140MHz */
	movel	%d0,0x180(%a1)			/* set PLL register */
	orl	#0x1,%d0
	movel	%d0,0x180(%a1)			/* set PLL register */
#endif

	/*
	 *	Setup CS1 for ethernet controller.
	 *	(Setup as per M5249C3 doco).
	 */
	movel  #0xe0000000,%d0			/* CS1 mapped at 0xe0000000 */
	movel  %d0,0x8c(%a0)
	movel  #0x001f0021,%d0			/* CS1 size of 1Mb */
	movel  %d0,0x90(%a0)
	movew  #0x0080,%d0			/* CS1 = 16bit port, AA */
	movew  %d0,0x96(%a0)

	/*
	 *	Setup CS2 for IDE interface.
	 */
	movel	#0x50000000,%d0			/* CS2 mapped at 0x50000000 */
	movel	%d0,0x98(%a0)
	movel	#0x001f0001,%d0			/* CS2 size of 1MB */
	movel	%d0,0x9c(%a0)
	movew	#0x0080,%d0			/* CS2 = 16bit, TA */
	movew	%d0,0xa2(%a0)

	movel	#0x00107000,%d0			/* IDEconfig1 */
	movel	%d0,0x18c(%a1)
	movel	#0x000c0400,%d0			/* IDEconfig2 */
	movel	%d0,0x190(%a1)

	movel	#0x00080000,%d0			/* GPIO19, IDE reset bit */
	orl	%d0,0xc(%a1)			/* function GPIO19 */
	orl	%d0,0x8(%a1)			/* enable GPIO19 as output */
        orl	%d0,0x4(%a1)			/* de-assert IDE reset */
.endm

#define	PLATFORM_SETUP	m5249c3_setup

#endif /* __ASSEMBLER__ */

/****************************************************************************/
#endif	/* m5249sim_h */
