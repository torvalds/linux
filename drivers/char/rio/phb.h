/****************************************************************************
 *******                                                              *******
 *******                 P H B     H E A D E R                        *******
 *******                                                              *******
 ****************************************************************************

 Author  : Ian Nandhra, Jeremy Rolls
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

#ifndef _phb_h
#define _phb_h 1

#ifdef SCCS_LABELS
#ifndef lint
/* static char *_rio_phb_h_sccs = "@(#)phb.h	1.12"; */
#endif
#endif


 /*************************************************
  * Set the LIMIT values.
  ************************************************/
#ifdef RTA
#define RX_LIMIT       (ushort) 3
#endif
#ifdef HOST
#define RX_LIMIT       (ushort) 1
#endif


/*************************************************
 * Handshake asserted. Deasserted by the LTT(s)
 ************************************************/
#define PHB_HANDSHAKE_SET      ((ushort) 0x001) /* Set by LRT */

#define PHB_HANDSHAKE_RESET     ((ushort) 0x002) /* Set by ISR / driver */

#define PHB_HANDSHAKE_FLAGS     (PHB_HANDSHAKE_RESET | PHB_HANDSHAKE_SET)
                                                /* Reset by ltt */


/*************************************************
 * Maximum number of PHB's
 ************************************************/
#if defined (HOST) || defined (INKERNEL)
#define MAX_PHB               ((ushort) 128)  /* range 0-127 */
#else
#define MAX_PHB               ((ushort) 8)    /* range 0-7 */
#endif

/*************************************************
 * Defines for the mode fields
 ************************************************/
#define TXPKT_INCOMPLETE        0x0001  /* Previous tx packet not completed */
#define TXINTR_ENABLED          0x0002  /* Tx interrupt is enabled */
#define TX_TAB3                 0x0004  /* TAB3 mode */
#define TX_OCRNL                0x0008  /* OCRNL mode */
#define TX_ONLCR                0x0010  /* ONLCR mode */
#define TX_SENDSPACES           0x0020  /* Send n spaces command needs 
                                           completing */
#define TX_SENDNULL             0x0040  /* Escaping NULL needs completing */
#define TX_SENDLF               0x0080  /* LF -> CR LF needs completing */
#define TX_PARALLELBUG          0x0100  /* CD1400 LF -> CR LF bug on parallel
                                           port */
#define TX_HANGOVER             (TX_SENDSPACES | TX_SENDLF | TX_SENDNULL)
#define TX_DTRFLOW		0x0200	/* DTR tx flow control */
#define	TX_DTRFLOWED		0x0400	/* DTR is low - don't allow more data
					   into the FIFO */
#define	TX_DATAINFIFO		0x0800	/* There is data in the FIFO */
#define	TX_BUSY			0x1000	/* Data in FIFO, shift or holding regs */

#define RX_SPARE	        0x0001   /* SPARE */
#define RXINTR_ENABLED          0x0002   /* Rx interrupt enabled */
#define RX_ICRNL                0x0008   /* ICRNL mode */
#define RX_INLCR                0x0010   /* INLCR mode */
#define RX_IGNCR                0x0020   /* IGNCR mode */
#define RX_CTSFLOW              0x0040   /* CTSFLOW enabled */
#define RX_IXOFF                0x0080   /* IXOFF enabled */
#define RX_CTSFLOWED            0x0100   /* CTSFLOW and CTS dropped */
#define RX_IXOFFED              0x0200   /* IXOFF and xoff sent */
#define RX_BUFFERED		0x0400	 /* Try and pass on complete packets */

#define PORT_ISOPEN             0x0001  /* Port open? */
#define PORT_HUPCL              0x0002  /* Hangup on close? */
#define PORT_MOPENPEND          0x0004  /* Modem open pending */
#define PORT_ISPARALLEL         0x0008  /* Parallel port */
#define PORT_BREAK              0x0010  /* Port on break */
#define PORT_STATUSPEND		0x0020  /* Status packet pending */
#define PORT_BREAKPEND          0x0040  /* Break packet pending */
#define PORT_MODEMPEND          0x0080  /* Modem status packet pending */
#define PORT_PARALLELBUG        0x0100  /* CD1400 LF -> CR LF bug on parallel
                                           port */
#define PORT_FULLMODEM          0x0200  /* Full modem signals */
#define PORT_RJ45               0x0400  /* RJ45 connector - no RI signal */
#define PORT_RESTRICTED         0x0600  /* Restricted connector - no RI / DTR */

#define PORT_MODEMBITS          0x0600  /* Mask for modem fields */

#define PORT_WCLOSE             0x0800  /* Waiting for close */
#define	PORT_HANDSHAKEFIX	0x1000	/* Port has H/W flow control fix */
#define	PORT_WASPCLOSED		0x2000	/* Port closed with PCLOSE */
#define	DUMPMODE		0x4000	/* Dump RTA mem */
#define	READ_REG		0x8000	/* Read CD1400 register */



/**************************************************************************
 * PHB Structure
 * A  few words.
 *
 * Normally Packets are added to the end of the list and removed from
 * the start. The pointer tx_add points to a SPACE to put a Packet.
 * The pointer tx_remove points to the next Packet to remove
 *************************************************************************/
#ifndef INKERNEL
#define src_unit     u2.s2.unit
#define src_port     u2.s2.port
#define dest_unit    u1.s1.unit
#define dest_port    u1.s1.port
#endif
#ifdef HOST
#define tx_start     u3.s1.tx_start_ptr_ptr
#define tx_add       u3.s1.tx_add_ptr_ptr
#define tx_end       u3.s1.tx_end_ptr_ptr
#define tx_remove    u3.s1.tx_remove_ptr_ptr
#define rx_start     u4.s1.rx_start_ptr_ptr
#define rx_add       u4.s1.rx_add_ptr_ptr
#define rx_end       u4.s1.rx_end_ptr_ptr
#define rx_remove    u4.s1.rx_remove_ptr_ptr
#endif
typedef struct PHB PHB ;
struct PHB {
#ifdef RTA
        ushort      port;
#endif
#ifdef INKERNEL
        WORD      source;
#else
        union       
        {
            ushort source;              /* Complete source */
            struct
            {
                unsigned char unit;     /* Source unit */
                unsigned char port;     /* Source port */
            } s2;
        } u2;
#endif
        WORD      handshake ;
        WORD      status ;
        NUMBER       timeout ;           /* Maximum of 1.9 seconds */
        WORD      link ;              /* Send down this link */
#ifdef INKERNEL
        WORD      destination;
#else
        union       
        {
            ushort destination;         /* Complete destination */
            struct
            {
                unsigned char unit;     /* Destination unit */
                unsigned char port;     /* Destination port */
            } s1;
        } u1;
#endif
#ifdef RTA
        ushort      tx_pkts_added;
        ushort      tx_pkts_removed;
        Q_BUF_ptr   tx_q_start ;        /* Start of the Q list chain */
        short       num_tx_q_bufs ;     /* Number of Q buffers in the chain */
        PKT_ptr_ptr tx_add ;            /* Add a new Packet here */
        Q_BUF_ptr   tx_add_qb;          /* Pointer to the add Q buf */
        PKT_ptr_ptr tx_add_st_qbb ;     /* Pointer to start of the Q's buf */
        PKT_ptr_ptr tx_add_end_qbb ;    /* Pointer to the end of the Q's buf */
        PKT_ptr_ptr tx_remove ;         /* Remove a Packet here */
        Q_BUF_ptr   tx_remove_qb ;      /* Pointer to the remove Q buf */
        PKT_ptr_ptr tx_remove_st_qbb ;  /* Pointer to the start of the Q buf */
        PKT_ptr_ptr tx_remove_end_qbb ; /* Pointer to the end of the Q buf */
#endif
#ifdef INKERNEL
        PKT_ptr_ptr tx_start ;
        PKT_ptr_ptr tx_end ;
        PKT_ptr_ptr tx_add ;
        PKT_ptr_ptr tx_remove ;
#endif
#ifdef HOST
        union
        {
            struct
            {
                PKT_ptr_ptr tx_start_ptr_ptr;
                PKT_ptr_ptr tx_end_ptr_ptr;
                PKT_ptr_ptr tx_add_ptr_ptr;
                PKT_ptr_ptr tx_remove_ptr_ptr;
            } s1;
            struct
            {
                ushort * tx_start_ptr;
                ushort * tx_end_ptr;
                ushort * tx_add_ptr;
                ushort * tx_remove_ptr;
            } s2;
        } u3;
#endif

#ifdef  RTA
        ushort      rx_pkts_added;
        ushort      rx_pkts_removed;
        Q_BUF_ptr   rx_q_start ;        /* Start of the Q list chain */
        short       num_rx_q_bufs ;     /* Number of Q buffers in the chain */
        PKT_ptr_ptr rx_add ;            /* Add a new Packet here */
        Q_BUF_ptr   rx_add_qb ;         /* Pointer to the add Q buf */
        PKT_ptr_ptr rx_add_st_qbb ;     /* Pointer to start of the Q's buf */
        PKT_ptr_ptr rx_add_end_qbb ;    /* Pointer to the end of the Q's buf */
        PKT_ptr_ptr rx_remove ;         /* Remove a Packet here */
        Q_BUF_ptr   rx_remove_qb ;      /* Pointer to the remove Q buf */
        PKT_ptr_ptr rx_remove_st_qbb ;  /* Pointer to the start of the Q buf */
        PKT_ptr_ptr rx_remove_end_qbb ; /* Pointer to the end of the Q buf */
#endif
#ifdef INKERNEL
        PKT_ptr_ptr rx_start ;
        PKT_ptr_ptr rx_end ;
        PKT_ptr_ptr rx_add ;
        PKT_ptr_ptr rx_remove ;
#endif
#ifdef HOST
        union
        {
            struct
            {
                PKT_ptr_ptr rx_start_ptr_ptr;
                PKT_ptr_ptr rx_end_ptr_ptr;
                PKT_ptr_ptr rx_add_ptr_ptr;
                PKT_ptr_ptr rx_remove_ptr_ptr;
            } s1;
            struct
            {
                ushort * rx_start_ptr;
                ushort * rx_end_ptr;
                ushort * rx_add_ptr;
                ushort * rx_remove_ptr;
            } s2;
        } u4;
#endif

#ifdef RTA                              /* some fields for the remotes */
        ushort     flush_count;		/* Count of write flushes */
        ushort     txmode;		/* Modes for tx */
        ushort     rxmode;		/* Modes for rx */
        ushort     portmode;		/* Generic modes */
        ushort     column;		/* TAB3 column count */
        ushort     tx_subscript;	/* (TX) Subscript into data field */
        ushort     rx_subscript;	/* (RX) Subscript into data field */
        PKT_ptr    rx_incomplete;	/* Hold an incomplete packet here */
        ushort     modem_bits;		/* Modem bits to mask */
	ushort	   lastModem;		/* Modem control lines. */
        ushort     addr;		/* Address for sub commands */
        ushort     MonitorTstate;	/* TRUE if monitoring tstop */
#endif

        } ;

#endif

/*********** end of file ***********/

