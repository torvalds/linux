/*
** -----------------------------------------------------------------------------
**
**  Perle Specialix driver for Linux
**  Ported from existing RIO Driver for SCO sources.
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
**
**	Module		: rioioctl.h
**	SID		: 1.2
**	Last Modified	: 11/6/98 11:34:13
**	Retrieved	: 11/6/98 11:34:22
**
**  ident @(#)rioioctl.h	1.2
**
** -----------------------------------------------------------------------------
*/

#ifndef	__rioioctl_h__
#define	__rioioctl_h__

#ifdef SCCS_LABELS
static char *_rioioctl_h_sccs_ = "@(#)rioioctl.h	1.2";
#endif

/*
** RIO device driver - user ioctls and associated structures.
*/

struct portStats {
	int port;
	int gather;
	ulong txchars;
	ulong rxchars;
	ulong opens;
	ulong closes;
	ulong ioctls;
};


#define rIOC	('r'<<8)
#define	TCRIOSTATE	(rIOC | 1)
#define	TCRIOXPON	(rIOC | 2)
#define	TCRIOXPOFF	(rIOC | 3)
#define	TCRIOXPCPS	(rIOC | 4)
#define	TCRIOXPRINT	(rIOC | 5)
#define TCRIOIXANYON	(rIOC | 6)
#define	TCRIOIXANYOFF	(rIOC | 7)
#define TCRIOIXONON	(rIOC | 8)
#define	TCRIOIXONOFF	(rIOC | 9)
#define	TCRIOMBIS	(rIOC | 10)
#define	TCRIOMBIC	(rIOC | 11)
#define	TCRIOTRIAD	(rIOC | 12)
#define TCRIOTSTATE	(rIOC | 13)

/*
** 15.10.1998 ARG - ESIL 0761 part fix
** Add RIO ioctls for manipulating RTS and CTS flow control, (as LynxOS
** appears to not support hardware flow control).
*/
#define TCRIOCTSFLOWEN	(rIOC | 14)	/* enable CTS flow control */
#define TCRIOCTSFLOWDIS	(rIOC | 15)	/* disable CTS flow control */
#define TCRIORTSFLOWEN	(rIOC | 16)	/* enable RTS flow control */
#define TCRIORTSFLOWDIS	(rIOC | 17)	/* disable RTS flow control */

/*
** 09.12.1998 ARG - ESIL 0776 part fix
** Definition for 'RIOC' also appears in daemon.h, so we'd better do a
** #ifndef here first.
** 'RIO_QUICK_CHECK' also #define'd here as this ioctl is now
** allowed to be used by customers.
**
** 05.02.1999 ARG -
** This is what I've decied to do with ioctls etc., which are intended to be
** invoked from users applications :
** Anything that needs to be defined here will be removed from daemon.h, that
** way it won't end up having to be defined/maintained in two places. The only
** consequence of this is that this file should now be #include'd by daemon.h
**
** 'stats' ioctls now #define'd here as they are to be used by customers.
*/
#define	RIOC	('R'<<8)|('i'<<16)|('o'<<24)

#define	RIO_QUICK_CHECK	  	(RIOC | 105)
#define RIO_GATHER_PORT_STATS	(RIOC | 193)
#define RIO_RESET_PORT_STATS	(RIOC | 194)
#define RIO_GET_PORT_STATS	(RIOC | 195)

#endif				/* __rioioctl_h__ */
