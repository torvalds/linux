/****************************************************************************
 *******                                                              *******
 *******                      L I N K
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

#ifndef _link_h
#define _link_h 1

#ifndef lint
#ifdef SCCS_LABELS
/* static char *_rio_link_h_sccs = "@(#)link.h	1.15"; */
#endif
#endif



/*************************************************
 * Define the Link Status stuff
 ************************************************/
#define LRT_ACTIVE         ((ushort) 0x01)
#define LRT_SPARE1         ((ushort) 0x02)
#define INTRO_RCVD         ((ushort) 0x04)
#define FORCED_DISCONNECT  ((ushort) 0x08)
#define LRT_SPARE2	   ((ushort) 0x80)

#define TOP_OF_RTA_RAM     ((ushort) 0x7000)
#define HOST_SERIAL_POINTER (unsigned char **) (TOP_OF_RTA_RAM - 2 * sizeof (ushort))

/* Flags for ltt_status */
#define  WAITING_ACK		(ushort) 0x0001
#define  DATA_SENT		(ushort) 0x0002
#define  WAITING_RUP		(ushort) 0x0004
#define  WAITING_RETRY		(ushort) 0x0008
#define  WAITING_TOPOLOGY	(ushort) 0x0010
#define  SEND_SYNC		(ushort) 0x0020
#define  FOAD_THIS_LINK		(ushort) 0x0040
#define  REQUEST_SYNC		(ushort) 0x0080
#define  REMOTE_DYING		(ushort) 0x0100
#define  DIE_NOW		(ushort) 0x0200

/* Boot request stuff */
#define BOOT_REQUEST       ((ushort) 0)    /* Request for a boot */
#define BOOT_ABORT         ((ushort) 1)    /* Abort a boot */
#define BOOT_SEQUENCE      ((ushort) 2)    /* Packet with the number of packets
                                              and load address */
#define BOOT_COMPLETED     ((ushort) 3)    /* Boot completed */

/* States that a link can be in */
#define	LINK_DISCONNECTED  ((ushort) 0)    /* Disconnected */
#define LINK_BOOT1         ((ushort) 1)    /* Trying to send 1st stage boot */
#define LINK_BOOT2         ((ushort) 2)    /* Trying to send 2nd stage boot */
#define LINK_BOOT2WAIT     ((ushort) 3)    /* Waiting for selftest results */
#define LINK_BOOT3         ((ushort) 4)    /* Trying to send 3rd stage boots */
#define LINK_SYNC          ((ushort) 5)    /* Syncing */

#define LINK_INTRO         ((ushort) 10)    /* Introductory packet */
#define LINK_SUPPLYID      ((ushort) 11)    /* Trying to supply an ID */
#define LINK_TOPOLOGY      ((ushort) 12)    /* Send a topology update */
#define LINK_REQUESTID     ((ushort) 13)    /* Waiting for an ID */
#define LINK_CONNECTED     ((ushort) 14)    /* Connected */

#define LINK_INTERCONNECT  ((ushort) 20)   /* Subnets interconnected */

#define LINK_SPARE	   ((ushort) 40)

/*
** Set the default timeout for link communications.
*/
#define	LINKTIMEOUT		(400 * MILLISECOND)

/*
** LED stuff
*/
#if defined(RTA)
#define LED_OFF            ((ushort) 0)    /* LED off */
#define LED_RED            ((ushort) 1)    /* LED Red */
#define LED_GREEN          ((ushort) 2)    /* LED Green */
#define LED_ORANGE         ((ushort) 4)    /* LED Orange */
#define LED_1TO8_OPEN      ((ushort) 1)    /* Port 1->8 LED on */
#define LED_9TO16_OPEN     ((ushort) 2)    /* Port 9->16 LED on */
#define LED_SET_COLOUR(colour)	(link->led = (colour))
#define LED_OR_COLOUR(colour)	(link->led |= (colour))
#define LED_TIMEOUT(time)    (link->led_timeout = RioTimePlus(RioTime(),(time)))
#else
#define LED_SET_COLOUR(colour)
#define LED_OR_COLOUR(colour)
#define LED_TIMEOUT(time)
#endif /* RTA */

struct LPB {
               WORD          link_number ;       /* Link Number */
               Channel_ptr   in_ch ;             /* Link In Channel */
               Channel_ptr   out_ch ;            /* Link Out Channel */
#ifdef RTA
               uchar        stat_led ;          /* Port open leds */
               uchar        led ;               /* True, light led! */
#endif
               BYTE attached_serial[4]; /* Attached serial number */
               BYTE attached_host_serial[4];
                                                 /* Serial number of Host who
                                                    booted the other end */
               WORD          descheduled ;       /* Currently Descheduled */
               WORD          state;              /* Current state */
               WORD          send_poll ;         /* Send a Poll Packet */
               Process_ptr   ltt_p ;             /* Process Descriptor */
               Process_ptr   lrt_p ;             /* Process Descriptor */
               WORD          lrt_status ;        /* Current lrt status */
               WORD          ltt_status ;        /* Current ltt status */
               WORD          timeout ;           /* Timeout value */
               WORD          topology;           /* Topology bits */
               WORD          mon_ltt ;
               WORD          mon_lrt ;
               WORD          WaitNoBoot ;	 /* Secs to hold off booting */
               PKT_ptr       add_packet_list;    /* Add packets to here */
               PKT_ptr       remove_packet_list; /* Send packets from here */
#ifdef RTA
#ifdef DCIRRUS
#define    QBUFS_PER_REDIRECT (4 / PKTS_PER_BUFFER + 1) 
#else
#define    QBUFS_PER_REDIRECT (8 / PKTS_PER_BUFFER + 1) 
#endif
               PKT_ptr_ptr   rd_add ;            /* Add a new Packet here */
               Q_BUF_ptr     rd_add_qb;          /* Pointer to the add Q buf */
               PKT_ptr_ptr   rd_add_st_qbb ;     /* Pointer to start of the Q's buf */
               PKT_ptr_ptr   rd_add_end_qbb ;    /* Pointer to the end of the Q's buf */
               PKT_ptr_ptr   rd_remove ;         /* Remove a Packet here */
               Q_BUF_ptr     rd_remove_qb ;      /* Pointer to the remove Q buf */
               PKT_ptr_ptr   rd_remove_st_qbb ;  /* Pointer to the start of the Q buf */
               PKT_ptr_ptr   rd_remove_end_qbb ; /* Pointer to the end of the Q buf */
               ushort        pkts_in_q ;         /* Packets in queue */
#endif

               Channel_ptr   lrt_fail_chan ;     /* Lrt's failure channel */
               Channel_ptr   ltt_fail_chan ;     /* Ltt's failure channel */

#if defined (HOST) || defined (INKERNEL)
 /* RUP structure for HOST to driver communications */
               struct RUP           rup ;              
#endif
               struct RUP           link_rup;           /* RUP for the link (POLL,
                                                    topology etc.) */
               WORD          attached_link ;     /* Number of attached link */
               WORD          csum_errors ;       /* csum errors */
               WORD          num_disconnects ;   /* number of disconnects */
               WORD          num_sync_rcvd ;     /* # sync's received */
               WORD          num_sync_rqst ;     /* # sync requests */
               WORD          num_tx ;            /* Num pkts sent */
               WORD          num_rx ;            /* Num pkts received */
               WORD          module_attached;    /* Module tpyes of attached */
               WORD          led_timeout;        /* LED timeout */
               WORD          first_port;         /* First port to service */
               WORD          last_port;          /* Last port to service */
           } ;

#endif

/*********** end of file ***********/
