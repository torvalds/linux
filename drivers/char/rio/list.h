/****************************************************************************
 *******                                                              *******
 *******                      L I S T                                 *******
 *******                                                              *******
 ****************************************************************************

 Author  : Jeremy Rolls.
 Date    : 04-Nov-1990

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

 Version : 0.01


                            Mods
 ----------------------------------------------------------------------------
  Date     By                Description
 ----------------------------------------------------------------------------
 ***************************************************************************/

#ifndef _list_h
#define _list_h 1

#ifdef SCCS_LABELS
#ifndef lint
static char *_rio_list_h_sccs = "@(#)list.h	1.9";
#endif
#endif

#define PKT_IN_USE    0x1

#define ZERO_PTR (ushort) 0x8000
#define	CaD	PortP->Caddr

/*
** We can add another packet to a transmit queue if the packet pointer pointed
** to by the TxAdd pointer has PKT_IN_USE clear in its address.
*/

#endif				/* ifndef _list.h */
/*********** end of file ***********/
