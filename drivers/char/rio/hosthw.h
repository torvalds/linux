/****************************************************************************
 *******                                                              *******
 *******                H O S T      H A R D W A R E
 *******                                                              *******
 ****************************************************************************

 Author  : Ian Nandhra / Jeremy Rolls
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


#ifndef lint
#ifdef SCCS_LABELS
static char *_rio_hosthw_h_sccs = "@(#)hosthw.h	1.2" ;
#endif
#endif

#define SET_OTHER_INTERRUPT  ( (volatile u_short *) 0x7c80 )
#define SET_EISA_INTERRUPT  ( (volatile u_short *) 0x7ef0 )

#define EISA_HOST    0x30
#define AT_HOST      0xa0
#define MCA_HOST     0xb0
#define PCI_HOST     0xd0

#define PRODUCT_MASK 0xf0


/*********** end of file ***********/


