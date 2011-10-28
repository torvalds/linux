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
**	Module		: rioioctl.h
**	SID		: 1.2
**	Last Modified	: 11/6/98 11:34:13
**	Retrieved	: 11/6/98 11:34:22
**
**  ident @(#)rioioctl.h	1.2
**
** -----------------------------------------------------------------------------
*/

#ifndef	__rioioctl_h__
#define	__rioioctl_h__

/*
** RIO device driver - user ioctls and associated structures.
*/

struct portStats {
	int port;
	int gather;
	unsigned long txchars;
	unsigned long rxchars;
	unsigned long opens;
	unsigned long closes;
	unsigned long ioctls;
};

#define	RIOC	('R'<<8)|('i'<<16)|('o'<<24)

#define	RIO_QUICK_CHECK	  	(RIOC | 105)
#define RIO_GATHER_PORT_STATS	(RIOC | 193)
#define RIO_RESET_PORT_STATS	(RIOC | 194)
#define RIO_GET_PORT_STATS	(RIOC | 195)

#endif				/* __rioioctl_h__ */
