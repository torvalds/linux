/*
** File:		debug.h
**
** Author:		David Dix
**
** Created:		12th March 1993
**
** Last modified:	93/04/27
**
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
*/

#ifndef _debug_h_
#define _debug_h_


#if defined(DCIRRUS)
#define	DBPACKET(pkt, opt, str, chn) 	debug_packet((pkt), (opt), (str), (chn))
#else
#define	DBPACKET(pkt, opt, str, c)
#endif	/* DCIRRUS */


#endif	/* _debug_h_ */
