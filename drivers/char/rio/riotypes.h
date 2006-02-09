/****************************************************************************
 *******                                                              *******
 *******                      R I O T Y P E S
 *******                                                              *******
 ****************************************************************************

 Author  : Jon Brawn
 Date    :

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

#ifndef _riotypes_h
#define _riotypes_h 1

#ifdef SCCS_LABELS
#ifndef lint
/* static char *_rio_riotypes_h_sccs = "@(#)riotypes.h	1.10"; */
#endif
#endif

typedef unsigned short NUMBER_ptr;
typedef unsigned short WORD_ptr;
typedef unsigned short BYTE_ptr;
typedef unsigned short char_ptr;
typedef unsigned short Channel_ptr;
typedef unsigned short FREE_LIST_ptr_ptr;
typedef unsigned short FREE_LIST_ptr;
typedef unsigned short LPB_ptr;
typedef unsigned short Process_ptr;
typedef unsigned short PHB_ptr;
typedef unsigned short PKT_ptr;
typedef unsigned short PKT_ptr_ptr;
typedef unsigned short Q_BUF_ptr;
typedef unsigned short Q_BUF_ptr_ptr;
typedef unsigned short ROUTE_STR_ptr;
typedef unsigned short RUP_ptr;
typedef unsigned short short_ptr;
typedef unsigned short u_short_ptr;
typedef unsigned short ushort_ptr;

#endif				/* __riotypes__ */

/*********** end of file ***********/
