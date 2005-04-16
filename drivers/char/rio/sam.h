/****************************************************************************
 *******                                                              *******
 *******                    S A M . H
 *******                                                              *******
 ****************************************************************************

 Author  : Ian Nandhra
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
#ifndef _sam_h
#define _sam_h 1

#ifdef SCCS_LABELS
#ifndef lint
/* static char *_rio_sam_h_sccs = "@(#)sam.h	1.3"; */
#endif
#endif


#if !defined( HOST ) && !defined( INKERNEL )
#define RTA 1
#endif

#define NUM_FREE_LIST_UNITS     500

#ifndef FALSE
#define FALSE (short)  0x00
#endif
#ifndef TRUE
#define TRUE  (short)  !FALSE
#endif

#define TX    TRUE
#define RX    FALSE


typedef struct FREE_LIST FREE_LIST ;
struct FREE_LIST   {
                       FREE_LIST_ptr next ;
                       FREE_LIST_ptr prev ;
                   } ;


#endif
/*********** end of file ***********/



