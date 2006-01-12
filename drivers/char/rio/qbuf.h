
/****************************************************************************
 *******                                                              *******
 *******       Q U E U E    B U F F E R   S T R U C T U R E S
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

#ifndef _qbuf_h
#define _qbuf_h 1

#ifndef lint
#ifdef SCCS_LABELS
static char *_rio_qbuf_h_sccs = "@(#)qbuf.h	1.1";
#endif
#endif



#ifdef HOST
#define PKTS_PER_BUFFER    1
#else
#define PKTS_PER_BUFFER    (220 / PKT_LENGTH)
#endif

typedef struct Q_BUF Q_BUF;
struct Q_BUF {
	Q_BUF_ptr next;
	Q_BUF_ptr prev;
	PKT_ptr buf[PKTS_PER_BUFFER];
};


#endif


/*********** end of file ***********/
