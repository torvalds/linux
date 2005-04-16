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
**	Module		: typdef.h
**	SID		: 1.2
**	Last Modified	: 11/6/98 11:34:20
**	Retrieved	: 11/6/98 11:34:22
**
**  ident @(#)typdef.h	1.2
**
** -----------------------------------------------------------------------------
*/

#ifndef __rio_typdef_h__
#define __rio_typdef_h__

#ifdef SCCS_LABELS
static char *_typdef_h_sccs_ = "@(#)typdef.h	1.2";
#endif

#undef VPIX

/*
** IT IS REALLY, REALLY, IMPORTANT THAT BYTES ARE UNSIGNED!
**
** These types are ONLY to be used for refering to data structures
** on the RIO Host card!
*/
typedef	volatile unsigned char	BYTE;
typedef volatile unsigned short	WORD;
typedef volatile unsigned int	DWORD;
typedef	volatile unsigned short RIOP;
typedef	volatile short          NUMBER;


/*
** 27.01.199 ARG - mods to compile 'newutils' on LyxnOS -
** These #defines are for the benefit of the 'libfuncs' library
** only. They are not necessarily correct type mappings and
** are here only to make the source compile.
*/
/* typedef unsigned int	uint; */
typedef unsigned long	ulong_t;
typedef unsigned short	ushort_t;
typedef unsigned char	uchar_t;
typedef unsigned char	queue_t;
typedef unsigned char	mblk_t;
typedef	unsigned int 	paddr_t;
typedef unsigned char   uchar;

#define	TPNULL	((ushort)(0x8000))


/*
** RIO structures defined in other include files.
*/
typedef struct PKT	 	PKT;
typedef struct LPB	 	LPB;
typedef struct RUP	 	RUP;
typedef struct Port		Port;
typedef struct DpRam		DpRam;

#endif /* __rio_typdef_h__ */
