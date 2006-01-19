/****************************************************************************
 *******                                                              *******
 *******                      R O M
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

#ifndef _rom_h
#define _rom_h 1

#ifndef lint
#ifdef SCCS
static char *_rio_rom_h_sccs = "@(#)rom.h	1.1";
#endif
#endif

typedef struct ROM ROM;
struct ROM {
	u_short slx;
	char pcb_letter_rev;
	char pcb_number_rev;
	char serial[4];
	char year;
	char week;
};

#endif

#define HOST_ROM    (ROM *) 0x7c00
#define RTA_ROM	    (ROM *) 0x7801
#define ROM_LENGTH  0x20

/*********** end of file ***********/
