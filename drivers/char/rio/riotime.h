/****************************************************************************
 *******                                                              *******
 *******            T I M E
 *******                                                              *******
 ****************************************************************************

 Author  : Jeremy Rolls
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

#ifndef _riotime_h
#define _riotime_h 1

#ifndef lint
#ifdef SCCS
static char *_rio_riotime_h_sccs = "@(#)riotime.h	1.1" ;
#endif
#endif

#define TWO_POWER_FIFTEEN (ushort)32768
#define RioTime()    riotime
#define RioTimeAfter(time1,time2) ((ushort)time1 - (ushort)time2) < TWO_POWER_FIFTEEN
#define RioTimePlus(time1,time2) ((ushort)time1 + (ushort)time2)

/**************************************
 * Convert a RIO tick (1/10th second)
 * into transputer low priority ticks
 *************************************/ 
#define RioTimeToLow(time) (time*(100000 / 64))
#define RioLowToTime(time) ((time*64)/100000)

#define RIOTENTHSECOND (ushort)1
#define RIOSECOND (ushort)(RIOTENTHSECOND * 10)
#endif

/*********** end of file ***********/
