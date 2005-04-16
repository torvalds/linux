/****************************************************************************
 *******                                                              *******
 *******                      P O L L
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

#ifndef _poll_h
#define _poll_h

#ifndef lint
#ifdef SCCS_LABELS
static char *_rio_poll_h_sccs = "@(#)poll.h	1.2" ;
#endif
#endif


#ifdef HOST
#define POLL_STACK            100
#endif
#ifdef RTA
#define POLL_STACK            200
#endif

#define POLL_PERIOD           (int) SECOND

/* The various poll commands */
#define POLL_POLL             0            /* We are connected and happy.. */
#define POLL_INTRO            1            /* Introduction packet */
#define POLL_TOPOLOGY         2            /* Topology update */
#define POLL_ASSIGN           3            /* ID assign */
#define POLL_FOAD             4            /* F*** Off And Die */
#define POLL_LMD	      5		   /* Let Me Die */
#define POLL_DYB	      6		   /* Die You Ba***** */

/* The way data fields are split up for POLL packets */
#define POLL_HOST_SERIAL      2            /* Host who booted me */
#define POLL_MY_SERIAL        6            /* My serial number */
#define POLL_YOUR_ID          1            /* Your ID number */
#define POLL_TOPOLOGY_FIELDS  2            /* Topology maps */

#endif

/*********** end of file ***********/



