/*
** -----------------------------------------------------------------------------
**
**  Perle Specialix driver for Linux
**  Ported from existing RIO Driver for SCO sources.
 *
 *  (C) 1990 - 2000 Specialix International Ltd., Byfleet, Surrey, UK.
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with this program; if not, write to the Free Software
 *      Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
**
**	Module		: mca.h
**	SID		: 1.2
**	Last Modified	: 11/6/98 11:34:11
**	Retrieved	: 11/6/98 11:34:21
**
**  ident @(#)mca.h	1.2
**
** -----------------------------------------------------------------------------
*/

#ifndef __rio_mca_h__
#define	__rio_mca_h__

#ifdef SCCS_LABELS
static char *_mca_h_sccs_ = "@(#)mca.h	1.2";
#endif

/*
** Micro Channel stuff
*/

#define	McaMaxSlots	8
#define McaSlotSelect	0x96
#define	McaSlotEnable	0x08
#define	McaIdLow	0x100
#define	McaIdHigh	0x101
#define	McaIrqEnable	0x102
#define	McaMemory	0x103
#define McaRIOId	0x6a5c
#define	McaIrq9		0x00
#define	McaIrq3		0x02
#define	McaIrq4		0x04
#define	McaIrq7		0x06
#define	McaIrq10	0x08
#define	McaIrq11	0x0A
#define	McaIrq12	0x0C
#define	McaIrq15	0x0E
#define McaIrqMask	0x0E
#define	McaCardEnable	0x01
#define	McaAddress(X)	(((X)&0xFF)<<16)

#define	McaTpFastLinks	        0x40
#define	McaTpSlowLinks	        0x00
#define	McaTpBootFromRam	0x01
#define	McaTpBootFromLink	0x00
#define	McaTpBusEnable		0x02
#define	McaTpBusDisable		0x00

#define	RIO_MCA_DEFAULT_MODE	SLOW_LINKS

#endif	/* __rio_mca_h__ */
