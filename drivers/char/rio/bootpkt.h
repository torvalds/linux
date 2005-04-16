

/****************************************************************************
 *******                                                              *******
 *******        B O O T    P A C K E T   H E A D E R   F I L E
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

#ifndef _pkt_h
#define _pkt_h 1

#ifndef lint
#ifdef SCCS
static char *_rio_bootpkt_h_sccs = "@(#)bootpkt.h	1.1" ;
#endif
#endif

    /*************************************************
     * Overlayed onto the Data fields of a regular
     * Packet
     ************************************************/
typedef struct BOOT_PKT BOOT_PKT ;
struct BOOT_PKT {
                    short     seq_num ;
                    char      data[10] ;
                } ;


#endif

/*********** end of file ***********/

