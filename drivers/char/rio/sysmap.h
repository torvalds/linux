
/****************************************************************************
 *******                                                              *******
 *******          S Y S T E M   M A P   H E A D E R
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

#ifndef lint
#ifdef SCCS_LABELS
static char *_rio_sysmap_h_sccs = "@(#)sysmap.h	1.1" ;
#endif
#endif

#define SYSTEM_MAP_LEN     64           /* Len of System Map array */


typedef struct SYS_MAP        SYS_MAP ;
typedef struct SYS_MAP_LINK   SYS_MAP_LINK ;

struct SYS_MAP_LINK {
                        short id ;          /* Unit Id */
                        short link ;        /* Id's Link */
                        short been_here ;   /* Used by map_gen */
                    } ;

struct SYS_MAP {
                   char         serial_num[4] ;
                   SYS_MAP_LINK link[4] ;
               } ;


/*********** end of file ***********/

