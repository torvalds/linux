/****************************************************************************/

/*
 *	nettel.h -- Lineo (formerly Moreton Bay) NETtel support.
 *
 *	(C) Copyright 1999-2000, Moreton Bay (www.moretonbay.com)
 * 	(C) Copyright 2000-2001, Lineo Inc. (www.lineo.com) 
 * 	(C) Copyright 2001-2002, SnapGear Inc., (www.snapgear.com) 
 */

/****************************************************************************/
#ifndef	nettel_h
#define	nettel_h
/****************************************************************************/


/****************************************************************************/
#ifdef CONFIG_NETtel
/****************************************************************************/

#ifdef CONFIG_COLDFIRE
#include <asm/coldfire.h>
#include <asm/mcfsim.h>
#include <asm/io.h>
#endif

/*---------------------------------------------------------------------------*/
#if defined(CONFIG_M5307)
/*
 *	NETtel/5307 based hardware first. DTR/DCD lines are wired to
 *	GPIO lines. Most of the LED's are driver through a latch
 *	connected to CS2.
 */
#define	MCFPP_DCD1	0x0001
#define	MCFPP_DCD0	0x0002
#define	MCFPP_DTR1	0x0004
#define	MCFPP_DTR0	0x0008

#define	NETtel_LEDADDR	0x30400000

#ifndef __ASSEMBLY__

extern volatile unsigned short ppdata;

/*
 *	These functions defined to give quasi generic access to the
 *	PPIO bits used for DTR/DCD.
 */
static __inline__ unsigned int mcf_getppdata(void)
{
	volatile unsigned short *pp;
	pp = (volatile unsigned short *) MCFSIM_PADAT;
	return((unsigned int) *pp);
}

static __inline__ void mcf_setppdata(unsigned int mask, unsigned int bits)
{
	volatile unsigned short *pp;
	pp = (volatile unsigned short *) MCFSIM_PADAT;
	ppdata = (ppdata & ~mask) | bits;
	*pp = ppdata;
}
#endif

/*---------------------------------------------------------------------------*/
#elif defined(CONFIG_M5206e)
/*
 *	NETtel/5206e based hardware has leds on latch on CS3.
 *	No support modem for lines??
 */
#define	NETtel_LEDADDR	0x50000000

/*---------------------------------------------------------------------------*/
#elif defined(CONFIG_M5272)
/*
 *	NETtel/5272 based hardware. DTR/DCD lines are wired to GPB lines.
 */
#define	MCFPP_DCD0	0x0080
#define	MCFPP_DCD1	0x0000		/* Port 1 no DCD support */
#define	MCFPP_DTR0	0x0040
#define	MCFPP_DTR1	0x0000		/* Port 1 no DTR support */

#ifndef __ASSEMBLY__
/*
 *	These functions defined to give quasi generic access to the
 *	PPIO bits used for DTR/DCD.
 */
static __inline__ unsigned int mcf_getppdata(void)
{
	return readw(MCFSIM_PBDAT);
}

static __inline__ void mcf_setppdata(unsigned int mask, unsigned int bits)
{
	write((readw(MCFSIM_PBDAT) & ~mask) | bits, MCFSIM_PBDAT);
}
#endif

#endif
/*---------------------------------------------------------------------------*/

/****************************************************************************/
#endif /* CONFIG_NETtel */
/****************************************************************************/
#endif	/* nettel_h */
