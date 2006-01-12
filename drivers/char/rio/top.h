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
**	Module		: top.h
**	SID		: 1.2
**	Last Modified	: 11/6/98 11:34:19
**	Retrieved	: 11/6/98 11:34:22
**
**  ident @(#)top.h	1.2
**
** -----------------------------------------------------------------------------
*/

#ifndef __rio_top_h__
#define __rio_top_h__

#ifdef SCCS_LABELS
static char *_top_h_sccs_ = "@(#)top.h	1.2";
#endif

/*
** Topology information
*/
struct Top {
	uchar Unit;
	uchar Link;
};

#endif				/* __rio_top_h__ */
