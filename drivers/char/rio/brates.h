/****************************************************************************
 *******                                                              *******
 *******		BRATES.H				      *******
 *******                                                              *******
 ****************************************************************************

 Author  : Jeremy Rolls
 Date    : 1 Nov 1990

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

#ifndef _brates_h
#ifndef lint
/* static char * _brates_h_sccs = "@(#)brates.h	1.4"; */
#endif
#define _brates_h 1
/* List of baud rate defines. Most are borrowed from /usr/include/sys/termio.h
*/
#ifndef INKERNEL

#define	B0	0x00
#define	B50	0x01
#define	B75	0x02
#define	B110	0x03
#define	B134	0x04
#define	B150	0x05
#define	B200	0x06
#define	B300	0x07
#define	B600	0x08
#define	B1200	0x09
#define	B1800	0x0a
#define	B2400	0x0b
#define	B4800	0x0c
#define	B9600	0x0d
#define	B19200	0x0e
#define	B38400	0x0f

#endif

/*
** The following baudrates may or may not be defined
** on various UNIX systems.
** If they are not then we define them.
** If they are then we do not define them ;-)
**
** This is appalling that we use same definitions as UNIX
** for our own download code as there is no garuntee that
** B57600 will be defined as 0x11 by a UNIX system....
** Arghhhhh!!!!!!!!!!!!!!
*/
#if !defined(B56000)
#define	B56000	0x10
#endif

#if !defined(B57600)
#define	B57600	0x11
#endif

#if !defined(B64000)
#define	B64000	0x12
#endif

#if !defined(B115200)
#define	B115200	0x13
#endif


#if !defined(B2000)
#define B2000	0x14
#endif


#define MAX_RATE B2000

struct    baud_rate            /* Tag for baud rates */
{
     /* short    host_rate,*/        /* As passed by the driver */
     short    divisor,          /* The divisor */
              prescaler;        /* The pre-scaler */
};

#endif
