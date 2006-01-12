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
**	Module		: cmdblk.h
**	SID		: 1.2
**	Last Modified	: 11/6/98 11:34:09
**	Retrieved	: 11/6/98 11:34:20
**
**  ident @(#)cmdblk.h	1.2
**
** -----------------------------------------------------------------------------
*/

#ifndef __rio_cmdblk_h__
#define __rio_cmdblk_h__

#ifdef SCCS_LABELS
#ifndef lint
static char *_cmdblk_h_sccs_ = "@(#)cmdblk.h	1.2";
#endif
#endif

/*
** the structure of a command block, used to queue commands destined for
** a rup.
*/

struct CmdBlk {
	struct CmdBlk *NextP;	/* Pointer to next command block */
	struct PKT Packet;	/* A packet, to copy to the rup */
	/* The func to call to check if OK */
	int (*PreFuncP) (int, struct CmdBlk *);
	int PreArg;		/* The arg for the func */
	/* The func to call when completed */
	int (*PostFuncP) (int, struct CmdBlk *);
	int PostArg;		/* The arg for the func */
};

#define NUM_RIO_CMD_BLKS (3 * (MAX_RUP * 4 + LINKS_PER_UNIT * 4))
#endif
