/****************************************************************************/

/*
 *	elia.h -- Lineo (formerly Moreton Bay) eLIA platform support.
 *
 *	(C) Copyright 1999-2000, Moreton Bay (www.moreton.com.au)
 *	(C) Copyright 1999-2000, Lineo (www.lineo.com)
 */

/****************************************************************************/
#ifndef	elia_h
#define	elia_h
/****************************************************************************/

#include <asm/coldfire.h>

#ifdef CONFIG_eLIA

/*
 *	The serial port DTR and DCD lines are also on the Parallel I/O
 *	as well, so define those too.
 */

#define	eLIA_DCD1		0x0001
#define	eLIA_DCD0		0x0002
#define	eLIA_DTR1		0x0004
#define	eLIA_DTR0		0x0008

#define	eLIA_PCIRESET		0x0020

/*
 *	Kernel macros to set and unset the LEDs.
 */
#ifndef __ASSEMBLY__
extern unsigned short	ppdata;
#endif /* __ASSEMBLY__ */

#endif	/* CONFIG_eLIA */

/****************************************************************************/
#endif	/* elia_h */
