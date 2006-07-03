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
**	Module		: unixrup.h
**	SID		: 1.2
**	Last Modified	: 11/6/98 11:34:20
**	Retrieved	: 11/6/98 11:34:22
**
**  ident @(#)unixrup.h	1.2
**
** -----------------------------------------------------------------------------
*/

#ifndef __rio_unixrup_h__
#define __rio_unixrup_h__

#ifdef SCCS_LABELS
static char *_unixrup_h_sccs_ = "@(#)unixrup.h	1.2";
#endif

/*
**    UnixRup data structure. This contains pointers to actual RUPs on the
**    host card, and all the command/boot control stuff.
*/
struct UnixRup {
	struct CmdBlk *CmdsWaitingP;	/* Commands waiting to be done */
	struct CmdBlk *CmdPendingP;	/* The command currently being sent */
	struct RUP __iomem *RupP;	/* the Rup to send it to */
	unsigned int Id;		/* Id number */
	unsigned int BaseSysPort;	/* SysPort of first tty on this RTA */
	unsigned int ModTypes;		/* Modules on this RTA */
	spinlock_t RupLock;	/* Lock structure for MPX */
	/*    struct lockb     RupLock;	*//* Lock structure for MPX */
};

#endif				/* __rio_unixrup_h__ */
