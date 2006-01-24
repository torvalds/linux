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
	BYTE Cmd;		/* It is very important that these line up */
	BYTE Cor1;		/* with what is expected at the other end. */
	BYTE Cor2;		/* to confirm that you've got it right,    */
	BYTE Cor4;		/* check with cirrus/cirrus.h              */
	BYTE Cor5;
	BYTE TxXon;		/* Transmit X-On character */
	BYTE TxXoff;		/* Transmit X-Off character */
	BYTE RxXon;		/* Receive X-On character */
	BYTE RxXoff;		/* Receive X-Off character */
	BYTE LNext;		/* Literal-next character */
	BYTE TxBaud;		/* Transmit baudrate */
	BYTE RxBaud;		/* Receive baudrate */
};

#endif
