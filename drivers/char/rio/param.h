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
**	Module		: param.h
**	SID		: 1.2
**	Last Modified	: 11/6/98 11:34:12
**	Retrieved	: 11/6/98 11:34:21
**
**  ident @(#)param.h	1.2
**
** -----------------------------------------------------------------------------
*/

#ifndef __rio_param_h__
#define __rio_param_h__

#ifdef SCCS_LABELS
static char *_param_h_sccs_ = "@(#)param.h	1.2";
#endif


/*
** the param command block, as used in OPEN and PARAM calls.
*/

struct phb_param {
	u8 Cmd;			/* It is very important that these line up */
	u8 Cor1;		/* with what is expected at the other end. */
	u8 Cor2;		/* to confirm that you've got it right,    */
	u8 Cor4;		/* check with cirrus/cirrus.h              */
	u8 Cor5;
	u8 TxXon;		/* Transmit X-On character */
	u8 TxXoff;		/* Transmit X-Off character */
	u8 RxXon;		/* Receive X-On character */
	u8 RxXoff;		/* Receive X-Off character */
	u8 LNext;		/* Literal-next character */
	u8 TxBaud;		/* Transmit baudrate */
	u8 RxBaud;		/* Receive baudrate */
};

#endif
