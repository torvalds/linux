/****************************************************************************
 *******                                                              *******
 *******                 R O U T E     H E A D E R
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

#ifndef _route_h
#define _route_h

#ifdef SCCS_LABELS
#ifndef lint
/* static char *_rio_route_h_sccs = "@(#)route.h	1.3"; */
#endif
#endif

#define MAX_LINKS 4
#define MAX_NODES 17                          /* Maximum nodes in a subnet */
#define NODE_BYTES ((MAX_NODES / 8) + 1)      /* Number of bytes needed for
                                                 1 bit per node */
#define ROUTE_DATA_SIZE  (NODE_BYTES + 2)     /* Number of bytes for complete 
                                                 info about cost etc. */
#define ROUTES_PER_PACKET ((PKT_MAX_DATA_LEN -2)/ ROUTE_DATA_SIZE)
                                              /* Number of nodes we can squeeze
                                                 into one packet */
#define MAX_TOPOLOGY_PACKETS (MAX_NODES / ROUTES_PER_PACKET + 1)
/************************************************
 * Define the types of command for the ROUTE RUP.
 ************************************************/
#define ROUTE_REQUEST    0                    /* Request an ID */
#define ROUTE_FOAD       1                    /* Kill the RTA */
#define ROUTE_ALREADY    2                    /* ID given already */
#define ROUTE_USED       3                    /* All ID's used */
#define ROUTE_ALLOCATE   4                    /* Here it is */
#define ROUTE_REQ_TOP    5                    /* I bet you didn't expect....
                                                 the Topological Inquisition */
#define ROUTE_TOPOLOGY   6                    /* Topology request answered FD */
/*******************************************************************
 * Define the Route Map Structure
 *
 * The route map gives a pointer to a Link Structure to use.
 * This allows Disconnected Links to be checked quickly
 ******************************************************************/
typedef struct COST_ROUTE COST_ROUTE;
struct COST_ROUTE {
                      unsigned char cost;        /* Cost down this link */
                      unsigned char route[NODE_BYTES]; /* Nodes thorough this route */
                  } ;

typedef struct ROUTE_STR ROUTE_STR ;
struct  ROUTE_STR {
                      COST_ROUTE cost_route[MAX_LINKS];
                                                /* cost / route for this link */
                      ushort favoured;          /* favoured link */
                  } ;


#define NO_LINK            (short) 5      /* Link unattached */
#define ROUTE_NO_ID        (short) 100    /* No Id */
#define ROUTE_DISCONNECT   (ushort) 0xff  /* Not connected */
#define ROUTE_INTERCONNECT (ushort) 0x40  /* Sub-net interconnect */


#define SYNC_RUP         (ushort) 255
#define COMMAND_RUP      (ushort) 254
#define ERROR_RUP        (ushort) 253
#define POLL_RUP         (ushort) 252
#define BOOT_RUP         (ushort) 251
#define ROUTE_RUP        (ushort) 250
#define STATUS_RUP       (ushort) 249
#define POWER_RUP        (ushort) 248

#define HIGHEST_RUP      (ushort) 255   /* Set to Top one */
#define LOWEST_RUP       (ushort) 248   /* Set to bottom one */

#endif

/*********** end of file ***********/

