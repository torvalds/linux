/****************************************************************************/

/*
 *	coldfire.h -- Motorola ColdFire CPU sepecific defines
 *
 *	(C) Copyright 1999-2006, Greg Ungerer (gerg@snapgear.com)
 *	(C) Copyright 2000, Lineo (www.lineo.com)
 */

/****************************************************************************/
#ifndef	coldfire_h
#define	coldfire_h
/****************************************************************************/


/*
 *	Define master clock frequency. This is essentially done at config
 *	time now. No point enumerating dozens of possible clock options
 *	here. Also the peripheral clock (bus clock) divide ratio is set
 *	at config time too.
 */
#ifdef CONFIG_CLOCK_SET
#define	MCF_CLK		CONFIG_CLOCK_FREQ
#define	MCF_BUSCLK	(CONFIG_CLOCK_FREQ / CONFIG_CLOCK_DIV)
#else
#error "Don't know what your ColdFire CPU clock frequency is??"
#endif

/*
 *	Define the processor support peripherals base address.
 *	This is generally setup by the boards start up code.
 */
#define	MCF_MBAR	0x10000000
#define	MCF_MBAR2	0x80000000
#if defined(CONFIG_M520x)
#define	MCF_IPSBAR	0xFC000000
#else
#define	MCF_IPSBAR	0x40000000
#endif

#if defined(CONFIG_M523x) || defined(CONFIG_M527x) || defined(CONFIG_M528x) || \
    defined(CONFIG_M520x)
#undef MCF_MBAR
#define	MCF_MBAR	MCF_IPSBAR
#elif defined(CONFIG_M532x)
#undef MCF_MBAR
#define MCF_MBAR	0x00000000
#endif

/****************************************************************************/
#endif	/* coldfire_h */
