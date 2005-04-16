
/****************************************************************************
 *******                                                              *******
 *******                R T A    H A R D W A R E
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
static char *_rio_rtahw_h_sccs = "@(#)rtahw.h	1.5" ;
#endif
#endif

#define	WATCHDOG_ADDR	((unsigned short *)0x7a00)
#define RTA_LED_ADDR	((unsigned short *)0x7c00)
#define SERIALNUM_ADDR	((unsigned char *)0x7809)
#define LATCH_ADDR      ((unsigned char *)0x7800)

/*
** Here we define where the cd1400 chips are in memory.
*/
#define CD1400_ONE_ADDR		(0x7300)
#define CD1400_TWO_ADDR		(0x7200)
#define CD1400_THREE_ADDR	(0x7100)
#define CD1400_FOUR_ADDR	(0x7000)

/*
** Define the different types of modules we can have
*/
enum module {
    MOD_BLANK		= 0x0f,		/* Blank plate attached */
    MOD_RS232DB25	= 0x00,		/* RS232 DB25 connector */
    MOD_RS232RJ45	= 0x01,		/* RS232 RJ45 connector */
    MOD_RS422DB25	= 0x02,		/* RS422 DB25 connector */
    MOD_RS485DB25	= 0x03,		/* RS485 DB25 connector */
    MOD_PARALLEL	= 0x04		/* Centronics parallel */
};

#define TYPE_HOST	0
#define TYPE_RTA8	1
#define TYPE_RTA16	2

#define	WATCH_DOG	WATCHDOG_ADDR

/*********** end of file ***********/
