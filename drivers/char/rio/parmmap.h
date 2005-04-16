/****************************************************************************
 *******                                                              *******
 *******               H O S T   M E M O R Y  M A P
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
6/4/1991   jonb		     Made changes to accommodate Mips R3230 bus
 ***************************************************************************/

#ifndef _parmap_h
#define _parmap_h


#ifdef SCCS_LABELS
#ifndef lint
/* static char *_rio_parmmap_h_sccs = "@(#)parmmap.h	1.4"; */
#endif
#endif

typedef struct PARM_MAP PARM_MAP ;

struct PARM_MAP
{
PHB_ptr           phb_ptr ;              /* Pointer to the PHB array */
WORD_ptr          phb_num_ptr ;          /* Ptr to Number of PHB's */
FREE_LIST_ptr     free_list;             /* Free List pointer */
FREE_LIST_ptr     free_list_end;         /* Free List End pointer */
Q_BUF_ptr_ptr     q_free_list_ptr ;      /* Ptr to Q_BUF variable */
BYTE_ptr          unit_id_ptr ;          /* Unit Id */
LPB_ptr           link_str_ptr ;         /* Link Structure Array */
BYTE_ptr          bootloader_1 ;         /* 1st Stage Boot Loader */
BYTE_ptr          bootloader_2 ;         /* 2nd Stage Boot Loader */
WORD_ptr          port_route_map_ptr ;   /* Port Route Map */
ROUTE_STR_ptr     route_ptr ;            /* Unit Route Map */
NUMBER_ptr        map_present ;          /* Route Map present */
NUMBER            pkt_num ;               /* Total number of packets */
NUMBER            q_num ;                 /* Total number of Q packets */
WORD              buffers_per_port ;      /* Number of buffers per port */
WORD              heap_size ;             /* Initial size of heap */
WORD              heap_left ;             /* Current Heap left */
WORD              error ;                 /* Error code */
WORD              tx_max;                 /* Max number of tx pkts per phb */
WORD              rx_max;                 /* Max number of rx pkts per phb */
WORD              rx_limit;               /* For high / low watermarks */
NUMBER            links ;                 /* Links to use */
NUMBER            timer ;                 /* Interrupts per second */
RUP_ptr           rups ;                 /* Pointer to the RUPs */
WORD              max_phb ;              /* Mostly for debugging */
WORD              living ;               /* Just increments!! */
WORD              init_done ;            /* Initialisation over */
WORD              booting_link ;
WORD              idle_count ;           /* Idle time counter */
WORD              busy_count ;           /* Busy counter */
WORD              idle_control ;         /* Control Idle Process */
#if defined(HOST) || defined(INKERNEL)
WORD              tx_intr;               /* TX interrupt pending */
WORD              rx_intr;               /* RX interrupt pending */
WORD              rup_intr;              /* RUP interrupt pending */
#endif
#if defined(RTA)
WORD		  dying_count;		/* Count of processes dead */
#endif
} ;

#endif

/*********** end of file ***********/


