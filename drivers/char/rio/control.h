

/****************************************************************************
 *******                                                              *******
 *******           C O N T R O L   P A C K E T   H E A D E R S
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


#ifndef _control_h
#define _control_h

#ifndef lint
/* static char *_rio_control_h_sccs = "@(#)control.h	1.4"; */
#endif

#define	CONTROL		'^'
#define IFOAD		( CONTROL + 1 )
#define	IDENTIFY	( CONTROL + 2 )
#define	ZOMBIE		( CONTROL + 3 )
#define	UFOAD		( CONTROL + 4 )
#define IWAIT		( CONTROL + 5 )

#define	IFOAD_MAGIC	0xF0AD	/* of course */
#define	ZOMBIE_MAGIC	(~0xDEAD)	/* not dead -> zombie */
#define	UFOAD_MAGIC	0xD1E	/* kill-your-neighbour */
#define	IWAIT_MAGIC	0xB1DE	/* Bide your time */

#endif

/*********** end of file ***********/
