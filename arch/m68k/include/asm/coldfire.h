/* SPDX-License-Identifier: GPL-2.0 */
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
 *	Define master clock frequency. This is done at config time now.
 *	No point enumerating dozens of possible clock options here. And
 *	in any case new boards come along from time to time that have yet
 *	another different clocking frequency.
 */
#ifdef CONFIG_CLOCK_FREQ
#define	MCF_CLK		CONFIG_CLOCK_FREQ
#else
#error "Don't know what your ColdFire CPU clock frequency is??"
#endif

/*
 *	Define the processor internal peripherals base address.
 *
 *	The majority of ColdFire parts use an MBAR register to set
 *	the base address. Some have an IPSBAR register instead, and it
 *	has slightly different rules on its size and alignment. Some
 *	parts have fixed addresses and the internal peripherals cannot
 *	be relocated in the CPU address space.
 *
 *	The value of MBAR or IPSBAR is config time selectable, we no
 *	longer hard define it here. No MBAR or IPSBAR will be defined if
 *	this part has a fixed peripheral address map.
 */
#ifdef CONFIG_MBAR
#define	MCF_MBAR	CONFIG_MBAR
#endif
#ifdef CONFIG_IPSBAR
#define	MCF_IPSBAR	CONFIG_IPSBAR
#endif

/****************************************************************************/
#endif	/* coldfire_h */
