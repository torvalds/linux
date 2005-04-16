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
**	Module		: riscos.h
**	SID		: 1.2
**	Last Modified	: 11/6/98 11:34:19
**	Retrieved	: 11/6/98 11:34:22
**
**  ident @(#)riscos.h	1.2
**
** -----------------------------------------------------------------------------
*/

#ifndef __rio_riscos_h__
#define __rio_riscos_h__

#ifdef SCCS_LABELS
static char *_riscos_h_sccs_ = "@(#)riscos.h	1.2";
#endif

/*
** This module used to define all those little itsy bits required for RISC/OS
** now it's full of null macros.
*/

/*
**	RBYTE reads a byte from a location.
**	RWORD reads a word from a location.
**	WBYTE writes a byte to a location.
**	WWORD writes a word to a location.
**	RINDW reads a word through a pointer.
**	WINDW writes a word through a pointer.
**	RIOSWAB swaps the two bytes of a word, if needed.
*/

#define	RIOSWAB(N)      (N)
#define	WBYTE(A,V)	(A)=(uchar)(V)
#define WWORD(A,V)	(A)=(ushort)(V)
#define RBYTE(A)	(uchar)(A)
#define RWORD(A)	(ushort)(A)
#define RINDW(A)	(*(ushort *)(A))
#define WINDW(A,V)	(*(ushort *)(A)=(ushort)(V))

#endif /* __rio_riscos_h__ */
