/* SPDX-License-Identifier: GPL-2.0 */
/****************************************************************************/

/*
 *	mcf8390.h -- NS8390 support for ColdFire eval boards.
 *
 *	(C) Copyright 1999-2000, Greg Ungerer (gerg@snapgear.com)
 *	(C) Copyright 2000,      Lineo (www.lineo.com)
 *	(C) Copyright 2001,      SnapGear (www.snapgear.com)
 *
 *      19990409 David W. Miller  Converted from m5206ne.h for 5307 eval board
 *
 *      Hacked support for m5206e Cadre III evaluation board
 *      Fred Stevens (fred.stevens@pemstar.com) 13 April 1999
 */

/****************************************************************************/
#ifndef	mcf8390_h
#define	mcf8390_h
/****************************************************************************/


/*
 *	Support for NE2000 clones devices in ColdFire based boards.
 *	Not all boards address these parts the same way, some use a
 *	direct addressing method, others use a side-band address space
 *	to access odd address registers, some require byte swapping
 *	others do not.
 */
#define	BSWAP(w)	(((w) << 8) | ((w) >> 8))
#define	RSWAP(w)	(w)


/*
 *	Define the basic hardware resources of NE2000 boards.
 */

#if defined(CONFIG_ARN5206)
#define NE2000_ADDR		0x40000300
#define NE2000_ODDOFFSET	0x00010000
#define NE2000_ADDRSIZE		0x00020000
#define	NE2000_IRQ_VECTOR	0xf0
#define	NE2000_IRQ_PRIORITY	2
#define	NE2000_IRQ_LEVEL	4
#define	NE2000_BYTE		volatile unsigned short
#endif

#if defined(CONFIG_M5206eC3)
#define	NE2000_ADDR		0x40000300
#define	NE2000_ODDOFFSET	0x00010000
#define	NE2000_ADDRSIZE		0x00020000
#define	NE2000_IRQ_VECTOR	0x1c
#define	NE2000_IRQ_PRIORITY	2
#define	NE2000_IRQ_LEVEL	4
#define	NE2000_BYTE		volatile unsigned short
#endif

#if defined(CONFIG_M5206e) && defined(CONFIG_NETtel)
#define NE2000_ADDR		0x30000300
#define NE2000_ADDRSIZE		0x00001000
#define NE2000_IRQ_VECTOR	25
#define NE2000_IRQ_PRIORITY	1
#define NE2000_IRQ_LEVEL	3
#define	NE2000_BYTE		volatile unsigned char
#endif

#if defined(CONFIG_M5307C3)
#define NE2000_ADDR		0x40000300
#define NE2000_ODDOFFSET	0x00010000
#define NE2000_ADDRSIZE		0x00020000
#define NE2000_IRQ_VECTOR	0x1b
#define	NE2000_BYTE		volatile unsigned short
#endif

#if defined(CONFIG_M5272) && defined(CONFIG_NETtel)
#define NE2000_ADDR		0x30600300
#define NE2000_ODDOFFSET	0x00008000
#define NE2000_ADDRSIZE		0x00010000
#define NE2000_IRQ_VECTOR	67
#undef	BSWAP
#define	BSWAP(w)		(w)
#define	NE2000_BYTE		volatile unsigned short
#undef	RSWAP
#define	RSWAP(w)		(((w) << 8) | ((w) >> 8))
#endif

#if defined(CONFIG_M5307) && defined(CONFIG_NETtel)
#define NE2000_ADDR0		0x30600300
#define NE2000_ADDR1		0x30800300
#define NE2000_ODDOFFSET	0x00008000
#define NE2000_ADDRSIZE		0x00010000
#define NE2000_IRQ_VECTOR0	27
#define NE2000_IRQ_VECTOR1	29
#undef	BSWAP
#define	BSWAP(w)		(w)
#define	NE2000_BYTE		volatile unsigned short
#undef	RSWAP
#define	RSWAP(w)		(((w) << 8) | ((w) >> 8))
#endif

#if defined(CONFIG_M5307) && defined(CONFIG_SECUREEDGEMP3)
#define NE2000_ADDR		0x30600300
#define NE2000_ODDOFFSET	0x00008000
#define NE2000_ADDRSIZE		0x00010000
#define NE2000_IRQ_VECTOR	27
#undef	BSWAP
#define	BSWAP(w)		(w)
#define	NE2000_BYTE		volatile unsigned short
#undef	RSWAP
#define	RSWAP(w)		(((w) << 8) | ((w) >> 8))
#endif

#if defined(CONFIG_ARN5307)
#define NE2000_ADDR		0xfe600300
#define NE2000_ODDOFFSET	0x00010000
#define NE2000_ADDRSIZE		0x00020000
#define NE2000_IRQ_VECTOR	0x1b
#define NE2000_IRQ_PRIORITY	2
#define NE2000_IRQ_LEVEL	3
#define	NE2000_BYTE		volatile unsigned short
#endif

#if defined(CONFIG_M5407C3)
#define NE2000_ADDR		0x40000300
#define NE2000_ODDOFFSET	0x00010000
#define NE2000_ADDRSIZE		0x00020000
#define NE2000_IRQ_VECTOR	0x1b
#define	NE2000_BYTE		volatile unsigned short
#endif

/****************************************************************************/
#endif	/* mcf8390_h */
