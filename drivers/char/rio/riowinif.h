/************************************************************************/
/*									*/
/*	Title		:	RIO Shared Memory Window Inteface	*/
/*									*/
/*	Author		:	N.P.Vassallo				*/
/*									*/
/*	Creation	:	7th June 1999				*/
/*									*/
/*	Version		:	1.0.0					*/
/*									*/
/*	Copyright	:	(c) Specialix International Ltd. 1999	*
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
 *									*/
/*	Description	:	Prototypes, structures and definitions	*/
/*				describing RIO host card shared	memory	*/
/*				window interface structures:		*/
/*					PARMMAP				*/
/*					RUP				*/
/*					PHB				*/
/*					LPB				*/
/*					PKT				*/
/*									*/
/************************************************************************/

/* History...

1.0.0	07/06/99 NPV	Creation. (based on PARMMAP.H)

*/

#ifndef	_riowinif_h				/* If RIOWINDIF.H not already defined */
#define	_riowinif_h    1

/*****************************************************************************
********************************             *********************************
********************************   General   *********************************
********************************             *********************************
*****************************************************************************/

#define	TPNULL		((_u16)(0x8000))

/*****************************************************************************
********************************              ********************************
********************************   PARM_MAP   ********************************
********************************              ********************************
*****************************************************************************/

/* The PARM_MAP structure defines global values relating to the Host Card / RTA
   and is the main structure from which all other structures are referenced. */

typedef	struct	_PARM_MAP
{
	_u16	phb_ptr;		/* 0x00 Pointer to the PHB array */
	_u16	phb_num_ptr;		/* 0x02 Ptr to Number of PHB's */
	_u16	free_list;		/* 0x04 Free List pointer */
	_u16	free_list_end;		/* 0x06 Free List End pointer */
	_u16	q_free_list_ptr;	/* 0x08 Ptr to Q_BUF variable */
	_u16	unit_id_ptr;		/* 0x0A Unit Id */
	_u16	link_str_ptr;		/* 0x0C Link Structure Array */
	_u16	bootloader_1;		/* 0x0E 1st Stage Boot Loader */
	_u16	bootloader_2;		/* 0x10 2nd Stage Boot Loader */
	_u16	port_route_map_ptr;	/* 0x12 Port Route Map */
	_u16	route_ptr;		/* 0x14 Route Map */
	_u16	map_present;		/* 0x16 Route Map present */
	_u16	pkt_num;		/* 0x18 Total number of packets */
	_u16	q_num;			/* 0x1A Total number of Q packets */
	_u16	buffers_per_port;	/* 0x1C Number of buffers per port */
	_u16	heap_size;		/* 0x1E Initial size of heap */
	_u16	heap_left;		/* 0x20 Current Heap left */
	_u16	error;			/* 0x22 Error code */
	_u16	tx_max;			/* 0x24 Max number of tx pkts per phb */
	_u16	rx_max;			/* 0x26 Max number of rx pkts per phb */
	_u16	rx_limit;		/* 0x28 For high / low watermarks */
	_u16	links;			/* 0x2A Links to use */
	_u16	timer;			/* 0x2C Interrupts per second */
	_u16	rups;			/* 0x2E Pointer to the RUPs */
	_u16	max_phb;		/* 0x30 Mostly for debugging */
	_u16	living;			/* 0x32 Just increments!! */
	_u16	init_done;		/* 0x34 Initialisation over */
	_u16	booting_link;		/* 0x36 */
	_u16	idle_count;		/* 0x38 Idle time counter */
	_u16	busy_count;		/* 0x3A Busy counter */
	_u16	idle_control;		/* 0x3C Control Idle Process */
	_u16	tx_intr;		/* 0x3E TX interrupt pending */
	_u16	rx_intr;		/* 0x40 RX interrupt pending */
	_u16	rup_intr;		/* 0x42 RUP interrupt pending */

} PARM_MAP;

/* Same thing again, but defined as offsets... */

#define	PM_phb_ptr		0x00	/* 0x00 Pointer to the PHB array */
#define	PM_phb_num_ptr		0x02	/* 0x02 Ptr to Number of PHB's */
#define	PM_free_list		0x04	/* 0x04 Free List pointer */
#define	PM_free_list_end	0x06	/* 0x06 Free List End pointer */
#define	PM_q_free_list_ptr	0x08	/* 0x08 Ptr to Q_BUF variable */
#define	PM_unit_id_ptr		0x0A	/* 0x0A Unit Id */
#define	PM_link_str_ptr		0x0C	/* 0x0C Link Structure Array */
#define	PM_bootloader_1		0x0E	/* 0x0E 1st Stage Boot Loader */
#define	PM_bootloader_2		0x10	/* 0x10 2nd Stage Boot Loader */
#define	PM_port_route_map_ptr	0x12	/* 0x12 Port Route Map */
#define	PM_route_ptr		0x14	/* 0x14 Route Map */
#define	PM_map_present		0x16	/* 0x16 Route Map present */
#define	PM_pkt_num		0x18	/* 0x18 Total number of packets */
#define	PM_q_num		0x1A	/* 0x1A Total number of Q packets */
#define	PM_buffers_per_port	0x1C	/* 0x1C Number of buffers per port */
#define	PM_heap_size		0x1E	/* 0x1E Initial size of heap */
#define	PM_heap_left		0x20	/* 0x20 Current Heap left */
#define	PM_error		0x22	/* 0x22 Error code */
#define	PM_tx_max		0x24	/* 0x24 Max number of tx pkts per phb */
#define	PM_rx_max		0x26	/* 0x26 Max number of rx pkts per phb */
#define	PM_rx_limit		0x28	/* 0x28 For high / low watermarks */
#define	PM_links		0x2A	/* 0x2A Links to use */
#define	PM_timer		0x2C	/* 0x2C Interrupts per second */
#define	PM_rups			0x2E	/* 0x2E Pointer to the RUPs */
#define	PM_max_phb		0x30	/* 0x30 Mostly for debugging */
#define	PM_living		0x32	/* 0x32 Just increments!! */
#define	PM_init_done		0x34	/* 0x34 Initialisation over */
#define	PM_booting_link		0x36	/* 0x36 */
#define	PM_idle_count		0x38	/* 0x38 Idle time counter */
#define	PM_busy_count		0x3A	/* 0x3A Busy counter */
#define	PM_idle_control		0x3C	/* 0x3C Control Idle Process */
#define	PM_tx_intr		0x3E	/* 0x4E TX interrupt pending */
#define	PM_rx_intr		0x40	/* 0x40 RX interrupt pending */
#define	PM_rup_intr		0x42	/* 0x42 RUP interrupt pending */
#define	sizeof_PARM_MAP		0x44	/* structure size = 0x44 */

/* PARM_MAP.error definitions... */
#define	E_NO_ERROR		0x00
#define	E_PROCESS_NOT_INIT	0x01
#define	E_LINK_TIMEOUT		0x02
#define	E_NO_ROUTE		0x03
#define	E_CONFUSED		0x04
#define	E_HOME			0x05
#define	E_CSUM_FAIL		0x06
#define	E_DISCONNECTED		0x07
#define	E_BAD_RUP		0x08
#define	E_NO_VIRGIN		0x09
#define	E_BOOT_RUP_BUSY		0x10
#define	E_CHANALLOC		0x80
#define	E_POLL_ALLOC		0x81
#define	E_LTTWAKE		0x82
#define	E_LTT_ALLOC		0x83
#define	E_LRT_ALLOC		0x84
#define	E_CIRRUS		0x85
#define	E_MONITOR		0x86
#define	E_PHB_ALLOC		0x87
#define	E_ARRAY_ALLOC		0x88
#define	E_QBUF_ALLOC		0x89
#define	E_PKT_ALLOC		0x8a
#define	E_GET_TX_Q_BUF		0x8b
#define	E_GET_RX_Q_BUF		0x8c
#define	E_MEM_OUT		0x8d
#define	E_MMU_INIT		0x8e
#define	E_LTT_INIT		0x8f
#define	E_LRT_INIT		0x90
#define	E_LINK_RUN		0x91
#define	E_MONITOR_ALLOC		0x92
#define	E_MONITOR_INIT		0x93
#define	E_POLL_INIT		0x94

/* PARM_MAP.links definitions... */
#define	RIO_LINK_ENABLE	0x80FF

/*****************************************************************************
**********************************         ***********************************
**********************************   RUP   ***********************************
**********************************         ***********************************
*****************************************************************************/

/* The RUP (Remote Unit Port) structure relates to the Remote Terminal Adapters
   attached to the system and there is normally an array of MAX_RUPS (=16) structures
   in a host card, defined by PARM_MAP->rup. */

typedef	struct	_RUP
{
	_u16		txpkt;			/* 0x00 Outgoing packet */
	_u16		rxpkt;			/* 0x02 ncoming packet */
	_u16		link;			/* 0x04 Which link to send packet down ? */
	_u8		rup_dest_unit[2];	/* 0x06 Destination Unit */
	_u16		handshake;		/* 0x08 Handshaking */
	_u16		timeout;		/* 0x0A Timeout */
	_u16		status;			/* 0x0C Status */
	_u16		txcontrol;		/* 0x0E Transmit control */
	_u16		rxcontrol;		/* 0x10 Receive control */

} RUP;

/* Same thing again, but defined as offsets... */

#define	RUP_txpkt		0x00		/* 0x00 Outgoing packet */
#define	RUP_rxpkt		0x02		/* 0x02 Incoming packet */
#define	RUP_link		0x04		/* 0x04 Which link to send packet down ? */
#define	RUP_rup_dest_unit	0x06		/* 0x06 Destination Unit */
#define	RUP_handshake		0x08		/* 0x08 Handshaking */
#define	RUP_timeout		0x0A		/* 0x0A Timeout */
#define	RUP_status		0x0C		/* 0x0C Status */
#define	RUP_txcontrol		0x0E		/* 0x0E Transmit control */
#define	RUP_rxcontrol		0x10		/* 0x10 Receive control */
#define	sizeof_RUP		0x12		/* structure size = 0x12 */

#define MAX_RUP			16

/* RUP.txcontrol definitions... */
#define	TX_RUP_INACTIVE		0		/* Nothing to transmit */
#define	TX_PACKET_READY		1		/* Transmit packet ready */
#define	TX_LOCK_RUP		2		/* Transmit side locked */

/* RUP.txcontrol definitions... */
#define	RX_RUP_INACTIVE		0		/* Nothing received */
#define	RX_PACKET_READY		1		/* Packet received */

#define	RUP_NO_OWNER		0xFF		/* RUP not owned by any process */

/*****************************************************************************
**********************************         ***********************************
**********************************   PHB   ***********************************
**********************************         ***********************************
*****************************************************************************/

/* The PHB (Port Header Block) structure relates to the serial ports attached
   to the system and there is normally an array of MAX_PHBS (=128) structures
   in a host card, defined by PARM_MAP->phb_ptr and PARM_MAP->phb_num_ptr. */

typedef	struct	_PHB
{
	_u16		source;			/* 0x00 Location of the PHB in the host card */
	_u16		handshake;		/* 0x02 Used to manage receive packet flow control */
	_u16		status;			/* 0x04 Internal port transmit/receive status */
	_u16		timeout;		/* 0x06 Time period to wait for an ACK */
	_u16		link;			/* 0x08 The host link associated with the PHB */
	_u16		destination;		/* 0x0A Location of the remote port on the network */

	_u16		tx_start;		/* 0x0C first entry in the packet array for transmit packets */
	_u16		tx_end;			/* 0x0E last entry in the packet array for transmit packets */
	_u16		tx_add;			/* 0x10 position in the packet array for new transmit packets */
	_u16		tx_remove;		/* 0x12 current position in the packet pointer array */

	_u16		rx_start;		/* 0x14 first entry in the packet array for receive packets */
	_u16		rx_end;			/* 0x16 last entry in the packet array for receive packets */
	_u16		rx_add;			/* 0x18 position in the packet array for new receive packets */
	_u16		rx_remove;		/* 0x1A current position in the packet pointer array */

} PHB;

/* Same thing again, but defined as offsets... */

#define	PHB_source		0x00		/* 0x00 Location of the PHB in the host card */
#define	PHB_handshake		0x02		/* 0x02 Used to manage receive packet flow control */
#define	PHB_status		0x04		/* 0x04 Internal port transmit/receive status */
#define	PHB_timeout		0x06		/* 0x06 Time period to wait for an ACK */
#define	PHB_link		0x08		/* 0x08 The host link associated with the PHB */
#define	PHB_destination		0x0A		/* 0x0A Location of the remote port on the network */
#define	PHB_tx_start		0x0C		/* 0x0C first entry in the packet array for transmit packets */
#define	PHB_tx_end		0x0E		/* 0x0E last entry in the packet array for transmit packets */
#define	PHB_tx_add		0x10		/* 0x10 position in the packet array for new transmit packets */
#define	PHB_tx_remove		0x12		/* 0x12 current position in the packet pointer array */
#define	PHB_rx_start		0x14		/* 0x14 first entry in the packet array for receive packets */
#define	PHB_rx_end		0x16		/* 0x16 last entry in the packet array for receive packets */
#define	PHB_rx_add		0x18		/* 0x18 position in the packet array for new receive packets */
#define	PHB_rx_remove		0x1A		/* 0x1A current position in the packet pointer array */
#define	sizeof_PHB		0x1C		/* structure size = 0x1C */

/* PHB.handshake definitions... */
#define	PHB_HANDSHAKE_SET	0x0001		/* Set by LRT */
#define	PHB_HANDSHAKE_RESET	0x0002		/* Set by ISR / driver */
#define	PHB_HANDSHAKE_FLAGS	(PHB_HANDSHAKE_RESET|PHB_HANDSHAKE_SET)
						/* Reset by ltt */

#define	MAX_PHB			128		/* range 0-127 */

/*****************************************************************************
**********************************         ***********************************
**********************************   LPB   ***********************************
**********************************         ***********************************
*****************************************************************************/

/* The LPB (Link Parameter Block) structure relates to a RIO Network Link
   and there is normally an array of MAX_LINKS (=4) structures in a host card,
   defined by PARM_MAP->link_str_ptr. */

typedef	struct	_LPB
{
	_u16		link_number;		/* 0x00 Link Number */
	_u16		in_ch;			/* 0x02 Link In Channel */
	_u16		out_ch;			/* 0x04 Link Out Channel */
	_u8		attached_serial[4];	/* 0x06 Attached serial number */
	_u8		attached_host_serial[4];/* 0x0A Serial number of Host who booted other end */
	_u16		descheduled;		/* 0x0E Currently Descheduled */
	_u16		state;			/* 0x10 Current state */
	_u16		send_poll;		/* 0x12 Send a Poll Packet */
	_u16		ltt_p;			/* 0x14 Process Descriptor */
	_u16		lrt_p;			/* 0x16 Process Descriptor */
	_u16		lrt_status;		/* 0x18 Current lrt status */
	_u16		ltt_status;		/* 0x1A Current ltt status */
	_u16		timeout;		/* 0x1C Timeout value */
	_u16		topology;		/* 0x1E Topology bits */
	_u16		mon_ltt;		/* 0x20 */
	_u16		mon_lrt;		/* 0x22 */
	_u16		num_pkts;		/* 0x24 */
	_u16		add_packet_list;	/* 0x26 Add packets to here */
	_u16		remove_packet_list;	/* 0x28 Send packets from here */

	_u16		lrt_fail_chan;		/* 0x2A Lrt's failure channel */
	_u16		ltt_fail_chan;		/* 0x2C Ltt's failure channel */

	RUP		rup;			/* 0x2E RUP structure for HOST to driver comms */
	RUP		link_rup;		/* 0x40 RUP for the link (POLL, topology etc.) */
	_u16		attached_link;		/* 0x52 Number of attached link */
	_u16		csum_errors;		/* 0x54 csum errors */
	_u16		num_disconnects;	/* 0x56 number of disconnects */
	_u16		num_sync_rcvd;		/* 0x58 # sync's received */
	_u16		num_sync_rqst;		/* 0x5A # sync requests */
	_u16		num_tx;			/* 0x5C Num pkts sent */
	_u16		num_rx;			/* 0x5E Num pkts received */
	_u16		module_attached;	/* 0x60 Module tpyes of attached */
	_u16		led_timeout;		/* 0x62 LED timeout */
	_u16		first_port;		/* 0x64 First port to service */
	_u16		last_port;		/* 0x66 Last port to service */

} LPB;

/* Same thing again, but defined as offsets... */

#define	LPB_link_number		0x00		/* 0x00 Link Number */
#define	LPB_in_ch		0x02		/* 0x02 Link In Channel */
#define	LPB_out_ch		0x04		/* 0x04 Link Out Channel */
#define	LPB_attached_serial	0x06		/* 0x06 Attached serial number */
#define	LPB_attached_host_serial 0x0A		/* 0x0A Serial number of Host who booted other end */
#define	LPB_descheduled		0x0E		/* 0x0E Currently Descheduled */
#define	LPB_state		0x10		/* 0x10 Current state */
#define	LPB_send_poll		0x12		/* 0x12 Send a Poll Packet */
#define	LPB_ltt_p		0x14		/* 0x14 Process Descriptor */
#define	LPB_lrt_p		0x16		/* 0x16 Process Descriptor */
#define	LPB_lrt_status		0x18		/* 0x18 Current lrt status */
#define	LPB_ltt_status		0x1A		/* 0x1A Current ltt status */
#define	LPB_timeout		0x1C		/* 0x1C Timeout value */
#define	LPB_topology		0x1E		/* 0x1E Topology bits */
#define	LPB_mon_ltt		0x20		/* 0x20 */
#define	LPB_mon_lrt		0x22		/* 0x22 */
#define	LPB_num_pkts		0x24		/* 0x24 */
#define	LPB_add_packet_list	0x26		/* 0x26 Add packets to here */
#define	LPB_remove_packet_list	0x28		/* 0x28 Send packets from here */
#define	LPB_lrt_fail_chan	0x2A		/* 0x2A Lrt's failure channel */
#define	LPB_ltt_fail_chan	0x2C		/* 0x2C Ltt's failure channel */
#define	LPB_rup			0x2E		/* 0x2E RUP structure for HOST to driver comms */
#define	LPB_link_rup		0x40		/* 0x40 RUP for the link (POLL, topology etc.) */
#define	LPB_attached_link	0x52		/* 0x52 Number of attached link */
#define	LPB_csum_errors		0x54		/* 0x54 csum errors */
#define	LPB_num_disconnects	0x56		/* 0x56 number of disconnects */
#define	LPB_num_sync_rcvd	0x58		/* 0x58 # sync's received */
#define	LPB_num_sync_rqst	0x5A		/* 0x5A # sync requests */
#define	LPB_num_tx		0x5C		/* 0x5C Num pkts sent */
#define	LPB_num_rx		0x5E		/* 0x5E Num pkts received */
#define	LPB_module_attached	0x60		/* 0x60 Module tpyes of attached */
#define	LPB_led_timeout		0x62		/* 0x62 LED timeout */
#define	LPB_first_port		0x64		/* 0x64 First port to service */
#define	LPB_last_port		0x66		/* 0x66 Last port to service */
#define	sizeof_LPB		0x68		/* structure size = 0x68 */

#define	LINKS_PER_UNIT		4		/* number of links from a host */

/*****************************************************************************
********************************               *******************************
********************************   FREE_LIST   *******************************
********************************               *******************************
*****************************************************************************/

/* Used to overlay packet headers when allocating/freeing packets from the free list */

typedef	struct	_FREE_LIST
{
	_u16		next;			/* 0x00 offset of next list item */
	_u16		prev;			/* 0x02 offset of previous list item */

} FREE_LIST;

/* Same thing again, but defined as offsets... */

#define	FL_next			0x00		/* 0x00 offset of next list item */
#define	FL_prev			0x02		/* 0x02 offset of previous list item */

/*****************************************************************************
**********************************         ***********************************
**********************************   PKT   ***********************************
**********************************         ***********************************
*****************************************************************************/

/* The PKT is the main unit of communication between Host Cards and RTAs across
   the RIO network.  */

#define PKT_MAX_DATA_LEN   72			/* Size of packet data */

typedef	struct	_PKT
{
	_u8		dest_unit;		/* 0x00 Destination Unit Id */
	_u8		dest_port;		/* 0x01 Destination Port */
	_u8		src_unit;		/* 0x02 Source Unit Id */
	_u8		src_port;		/* 0x03 Source Port */
	_u8		len;			/* 0x04 Length (in bytes) of data field */
	_u8		control;		/* 0x05 */
	_u8		data[PKT_MAX_DATA_LEN];	/* 0x06 Actual data */
	_u16		csum;			/* 0x4E C-SUM */

} PKT;

/* Same thing again, but defined as offsets... */

#define	PKT_dest_unit		0x00		/* 0x00 Destination Unit Id */
#define	PKT_dest_port		0x01		/* 0x01 Destination Port */
#define	PKT_src_unit		0x02		/* 0x02 Source Unit Id */
#define	PKT_src_port		0x03		/* 0x03 Source Port */
#define	PKT_len			0x04		/* 0x04 Length (in bytes) of data field */
#define	PKT_control		0x05		/* 0x05 */
#define	PKT_data		0x06		/* 0x06 Actual data */
#define	PKT_csum		0x4E		/* 0x4E C-SUM */
#define	sizeof_PKT		0x50		/* structure size = 0x50 */

/* PKT.len definitions... */
#define	PKT_CMD_BIT		0x80
#define	PKT_CMD_DATA		0x80
#define	PKT_LEN_MASK		0x7F

/* PKT.control definitions... */
#define	PKT_ACK			0x40
#define	PKT_TGL			0x20
#define	DATA_WNDW		0x10
#define	PKT_TTL_MASK		0x0F
#define	MAX_TTL			0x0F

/*****************************************************************************
*****************************                     ****************************
*****************************   Control Packets   ****************************
*****************************                     ****************************
*****************************************************************************/

/* The following definitions and structures define the control packets sent
   between the driver and RIO Ports, RTAs and Host Cards. */

#define	PRE_EMPTIVE		0x80			/* Pre-emptive command (sent via port's RUP) */

/* "in-band" and "pre-emptive" port commands... */
#define	OPEN			0x00			/* Driver->RIO Open a port */
#define	CONFIG			0x01			/* Driver->RIO Configure a port */
#define	MOPEN			0x02			/* Driver->RIO Modem open (wait for DCD) */
#define	CLOSE			0x03			/* Driver->RIO Close a port */
#define	WFLUSH			(0x04|PRE_EMPTIVE)	/* Driver->RIO Write flush */
#define	RFLUSH			(0x05|PRE_EMPTIVE)	/* Driver->RIO Read flush */
#define	RESUME			(0x06|PRE_EMPTIVE)	/* Driver->RIO Behave as if XON received */
#define	SBREAK			0x07			/* Driver->RIO Start break */
#define	EBREAK			0x08			/* Driver->RIO End break */
#define	SUSPEND			(0x09|PRE_EMPTIVE)	/* Driver->RIO Behave as if XOFF received */
#define	FCLOSE			(0x0A|PRE_EMPTIVE)	/* Driver->RIO Force close */
#define	XPRINT			0x0B			/* Driver->RIO Xprint packet */
#define	MBIS			(0x0C|PRE_EMPTIVE)	/* Driver->RIO Set modem lines */
#define	MBIC			(0x0D|PRE_EMPTIVE)	/* Driver->RIO Clear modem lines */
#define	MSET			(0x0E|PRE_EMPTIVE)	/* Driver->RIO Set modem lines */
#define	PCLOSE			0x0F			/* Driver->RIO Pseudo close */
#define	MGET			(0x10|PRE_EMPTIVE)	/* Driver->RIO Force update of modem status */
#define	MEMDUMP			(0x11|PRE_EMPTIVE)	/* Driver->RIO DEBUG request for RTA memory */
#define	READ_REGISTER		(0x12|PRE_EMPTIVE)	/* Driver->RIO DEBUG read CD1400 register */

/* Remote Unit Port (RUP) packet definitions... (specified in PKT.dest_unit and PKT.src_unit) */
#define	SYNC_RUP		0xFF			/* Download internal */
#define	COMMAND_RUP		0xFE			/* Command ack/status */
#define	ERROR_RUP		0xFD			/* Download internal */
#define	POLL_RUP		0xFC			/* Download internal */
#define	BOOT_RUP		0xFB			/* Used to boot RTAs */
#define	ROUTE_RUP		0xFA			/* Used to specify routing/topology */
#define	STATUS_RUP		0xF9			/* Not used */
#define	POWER_RUP		0xF8			/* Download internal */

/* COMMAND_RUP definitions... */
#define	COMPLETE		(0x20|PRE_EMPTIVE)	/* RIO->Driver Command complete */
#define	BREAK_RECEIVED		(0x21|PRE_EMPTIVE)	/* RIO->Driver Break received */
#define	MODEM_STATUS		(0x22|PRE_EMPTIVE)	/* RIO->Driver Modem status change */

/* BOOT_RUP definitions... */
#define	BOOT_REQUEST		0x00			/* RIO->Driver Request for boot */
#define	BOOT_ABORT		0x01			/* Driver->RIO Abort a boot */
#define	BOOT_SEQUENCE		0x02			/* Driver->RIO Packet with firmware details */
#define	BOOT_COMPLETED		0x03			/* RIO->Driver Boot completed */
#define IFOAD			0x2F			/* Driver->RIO Shutdown/Reboot RTA (Fall Over And Die) */
#define	IDENTIFY		0x30			/* Driver->RIO Identify RTA */
#define	ZOMBIE			0x31			/* Driver->RIO Shutdown/Flash LEDs */
#define	UFOAD			0x32			/* Driver->RIO Shutdown/Reboot neighbouring RTA */
#define IWAIT			0x33			/* Driver->RIO Pause booting process */

/* ROUTE_RUP definitions... */
#define	ROUTE_REQUEST		0x00			/* RIO->Driver Request an ID */
#define	ROUTE_FOAD		0x01			/* Driver->RIO Shutdown/reboot RTA */
#define	ROUTE_ALREADY		0x02			/* Driver->RIO Not used */
#define	ROUTE_USED		0x03			/* Driver->RIO Not used */
#define	ROUTE_ALLOCATE		0x04			/* Driver->RIO Allocate RTA RUP numbers */
#define	ROUTE_REQ_TOP		0x05			/* Driver->RIO Not used */
#define ROUTE_TOPOLOGY		0x06			/* RIO->Driver Route/Topology status */

/*****************************************************************************
**********************************          **********************************
**********************************   OPEN   **********************************
**********************************          **********************************
*****************************************************************************/

/* (Driver->RIO,in-band)

   Sent to open a port. 
   Structure of configuration info used with OPEN, CONFIG and MOPEN packets... */

#define	PKT_Cmd			(PKT_Data+0)		/* Command code */
#define	PKT_Cor1		(PKT_Data+1)		/* Channel Option Register 1 */
#define	PKT_Cor2		(PKT_Data+2)		/* Channel Option Register 2 */
#define	PKT_Cor4		(PKT_Data+3)		/* Channel Option Register 4 */
#define	PKT_Cor5		(PKT_Data+4)		/* Channel Option Register 5 */
#define	PKT_TxXon		(PKT_Data+5)		/* Transmit XON character */
#define	PKT_TxXoff		(PKT_Data+6)		/* Transmit XOFF character */
#define	PKT_RxXon		(PKT_Data+7)		/* Receive XON character */
#define	PKT_RxXoff		(PKT_Data+8)		/* Receive XOFF character */
#define	PKT_Lnext		(PKT_Data+9)		/* Lnext character */
#define	PKT_TxBaud		(PKT_Data+10)		/* Transmit baud rate */
#define	PKT_RxBaud		(PKT_Data+11)		/* Receive baud rate */

/* COR1 definitions... */
#define	COR1_PARITY		0xE0			/* Parity mask */
#define	COR1_NONE		0x00			/* No parity */
#define	COR1_SPACE		0x20			/* Space parity */
#define	COR1_EVEN		0x40			/* Even parity */
#define	COR1_MARK		0xA0			/* Mark parity */
#define	COR1_ODD		0xC0			/* Odd parity */

#define	COR1_STOPBITS		0x0C			/* Stop bits mask */
#define	COR1_STOP1		0x00			/* 1 stop bit */
#define	COR1_STOP1_5		0x04			/* 1.5 stop bits */
#define	COR1_STOP2		0x08			/* 2 stop bits */

#define	COR1_DATABITS		0x03			/* Data bits mask */
#define	COR1_DATA5		0x00			/* 5 data bits */
#define	COR1_DATA6		0x01			/* 6 data bits */
#define	COR1_DATA7		0x02			/* 7 data bits */
#define	COR1_DATA8		0x03			/* 8 data bits */

/* COR2 definitions... */
#define	COR2_XON_TXFLOW		0x40			/* XON/XOFF Transmit Flow */
#define	COR2_XANY_TXFLOW	0xC0			/* XON/XANY Transmit Flow */
#define	COR2_HUPCL		0x20			/* Hang Up On Close */
#define	COR2_DSR_TXFLOW		0x08			/* DSR Transmit Flow Control */
#define	COR2_RTS_RXFLOW		0x04			/* RTS Receive Flow Control */
#define	COR2_CTS_TXFLOW		0x02			/* CTS Transmit Flow Control */
#define	COR2_XON_RXFLOW		0x01			/* XON/XOFF Receive Flow */

/* COR4 definition... */
#define	COR4_IGNCR		0x80			/* Discard received CR */
#define	COR4_ICRNL		0x40			/* Map received CR -> NL */
#define	COR4_INLCR		0x20			/* Map received NL -> CR */
#define	COR4_IGNBRK		0x10			/* Ignore Received Break */
#define	COR4_NBRKINT		0x08			/* No interrupt on rx Break */
#define	COR4_IGNPAR		0x04			/* ignore rx parity error chars */
#define	COR4_PARMRK		0x02			/* Mark rx parity error chars */
#define	COR4_RAISEMOD		0x01			/* Raise modem lines on !0 baud */

/* COR5 definitions... */
#define	COR5_ISTRIP		0x80			/* Strip input chars to 7 bits */
#define	COR5_LNE		0x40			/* Enable LNEXT processing */
#define	COR5_CMOE		0x20			/* Match good & error characters */
#define	COR5_TAB3		0x10			/* TAB3 mode */
#define	COR5_TSTATE_ON		0x08			/* Enable tbusy/tstop monitoring */
#define	COR5_TSTATE_OFF		0x04			/* Disable tbusy/tstop monitoring */
#define	COR5_ONLCR		0x02			/* NL -> CR NL on output */
#define	COR5_OCRNL		0x01			/* CR -> NL on output */

/* RxBaud and TxBaud definitions... */
#define	RIO_B0			0x00			/* RTS / DTR signals dropped */
#define	RIO_B50			0x01			/* 50 baud */
#define	RIO_B75			0x02			/* 75 baud */
#define	RIO_B110		0x03			/* 110 baud */
#define	RIO_B134		0x04			/* 134.5 baud */
#define	RIO_B150		0x05			/* 150 baud */
#define	RIO_B200		0x06			/* 200 baud */
#define	RIO_B300		0x07			/* 300 baud */
#define	RIO_B600		0x08			/* 600 baud */
#define	RIO_B1200		0x09			/* 1200 baud */
#define	RIO_B1800		0x0A			/* 1800 baud */
#define	RIO_B2400		0x0B			/* 2400 baud */
#define	RIO_B4800		0x0C			/* 4800 baud */
#define	RIO_B9600		0x0D			/* 9600 baud */
#define	RIO_B19200		0x0E			/* 19200 baud */
#define	RIO_B38400		0x0F			/* 38400 baud */
#define	RIO_B56000		0x10			/* 56000 baud */
#define	RIO_B57600		0x11			/* 57600 baud */
#define	RIO_B64000		0x12			/* 64000 baud */
#define	RIO_B115200		0x13			/* 115200 baud */
#define	RIO_B2000		0x14			/* 2000 baud */

/*****************************************************************************
*********************************            *********************************
*********************************   CONFIG   *********************************
*********************************            *********************************
*****************************************************************************/

/* (Driver->RIO,in-band)

   CONFIG is sent from the driver to configure an already opened port.
   Packet structure is same as OPEN.  */

/*****************************************************************************
*********************************           **********************************
*********************************   MOPEN   **********************************
*********************************           **********************************
*****************************************************************************/

/* (Driver->RIO,in-band)

   MOPEN is sent from the driver to open a port attached to a modem. (in-band)
   Packet structure is same as OPEN.  */

/*****************************************************************************
*********************************           **********************************
*********************************   CLOSE   **********************************
*********************************           **********************************
*****************************************************************************/

/* (Driver->RIO,in-band)

   CLOSE is sent from the driver to close a previously opened port.
   No parameters.
 */
#if 0
#define	PKT_Cmd			(PKT_Data+0)		/* Command code */
#endif
/*****************************************************************************
*********************************            *********************************
*********************************   WFLUSH   *********************************
*********************************            *********************************
*****************************************************************************/

/* (Driver->RIO,pre-emptive)

   WFLUSH is sent pre-emptively from the driver to flush the write buffers and
   packets of a port.  (pre-emptive)
   
   WFLUSH is also sent in-band from the driver to a port as a marker to end
   write flushing previously started by a pre-emptive WFLUSH packet. (in-band)
   */
#if 0
#define	PKT_Cmd			(PKT_Data+0)		/* Command code */
#endif
#define	PKT_PhbNum		(PKT_Data+1)		/* Port number wrt RTA */

/*****************************************************************************
*********************************            *********************************
*********************************   RFLUSH   *********************************
*********************************            *********************************
*****************************************************************************/

/* (Driver->RIO,pre-emptive)

   RFLUSH is sent pre-emptively from the driver to flush the read buffers and
   packets of a port.
   */
#if 0
#define	PKT_Cmd			(PKT_Data+0)		/* Command code */
#define	PKT_PhbNum		(PKT_Data+1)		/* Port number wrt RTA */
#endif

/*****************************************************************************
*********************************            *********************************
*********************************   RESUME   *********************************
*********************************            *********************************
*****************************************************************************/

/* (Driver->RIO,pre-emptive)

   RESUME is sent pre-emptively from the driver to cause a port to resume 
   transmission of data if blocked by XOFF.  (as if XON had been received)
   */
#if 0
#define	PKT_Cmd			(PKT_Data+0)		/* Command code */
#define	PKT_PhbNum		(PKT_Data+1)		/* Port number wrt RTA */
#endif

/*****************************************************************************
*********************************            *********************************
*********************************   SBREAK   *********************************
*********************************            *********************************
*****************************************************************************/

/* (Driver->RIO,in-band)

   SBREAK is sent in-band from the driver to a port to suspend data and start
   break signal transmission.

   If the break delay is 0, the break signal will be acknowledged with a
   RUP_COMMAND, COMPLETE packet and continue until an EBREAK packet is received.

   Otherwise, there is no acknowledgement and the break signal will last for the
   specified number of mS.
   */
#if 0
#define	PKT_Cmd			(PKT_Data+0)		/* Command code */
#endif
#define	PKT_BreakDelay		(PKT_Data+1)		/* Break delay in mS */

/*****************************************************************************
*********************************            *********************************
*********************************   EBREAK   *********************************
*********************************            *********************************
*****************************************************************************/

/* (Driver->RIO,in-band)

   EBREAK is sent in-band from the driver to a port to stop transmission of a
   break signal.

   No parameters.  */

/*****************************************************************************
*********************************             ********************************
*********************************   SUSPEND   ********************************
*********************************             ********************************
*****************************************************************************/

/* (Driver->RIO,pre-emptive)

   SUSPEND is sent pre-emptively from the driver to cause a port to suspend
   transmission of data.  (as if XOFF had been received)
   */
#if 0
#define	PKT_Cmd			(PKT_Data+0)		/* Command code */
#define	PKT_PhbNum		(PKT_Data+1)		/* Port number wrt RTA */
#endif

/*****************************************************************************
*********************************            *********************************
*********************************   FCLOSE   *********************************
*********************************            *********************************
*****************************************************************************/

/* (Driver->RIO,pre-emptive)

   FCLOSE is sent pre-emptively from the driver to force close a port.
   A force close flushes receive and transmit queues, and also lowers all output
   modem signals if the COR5_HUPCL (Hang Up On Close) flag is set.
   */
#if 0
#define	PKT_Cmd			(PKT_Data+0)		/* Command code */
#define	PKT_PhbNum		(PKT_Data+1)		/* Port number wrt RTA */
#endif

/*****************************************************************************
*********************************            *********************************
*********************************   XPRINT   *********************************
*********************************            *********************************
*****************************************************************************/

/* (Driver->RIO,in-band)

   XPRINT is sent as a normal I/O data packet except that the PKT_CMD_BIT of
   the "len" field is set, and the first "data" byte is XPRINT.

   The I/O data in the XPRINT packet will contain the following:
   -	Transparent Print Start Sequence
   -	Transparent Print Data
   -	Transparent Print Stop Sequence.
   */
#if 0
#define	PKT_Cmd			(PKT_Data+0)		/* Command code */
#define	PKT_PhbNum		(PKT_Data+1)		/* Port number wrt RTA */
#endif

/*****************************************************************************
**********************************          **********************************
**********************************   MBIS   **********************************
**********************************          **********************************
*****************************************************************************/

/* (Driver->RIO,pre-emptive)

   MBIS is sent pre-emptively from the driver to set a port's modem signals.
   */
#if 0
#define	PKT_Cmd			(PKT_Data+0)		/* Command code */
#define	PKT_PhbNum		(PKT_Data+1)		/* Port number wrt RTA */
#endif
#define	PKT_ModemSet		(PKT_Data+4)		/* Modem set signals mask */

/* ModemSet definitions... */
#define	MBIS_RTS		0x01			/* RTS modem signal */
#define	MBIS_DTR		0x02			/* DTR modem signal */

/*****************************************************************************
**********************************          **********************************
**********************************   MBIC   **********************************
**********************************          **********************************
*****************************************************************************/

/* (Driver->RIO,pre-emptive)

   MBIC is sent pre-emptively from the driver to clear a port's modem signals.
   */
#if 0   
#define	PKT_Cmd			(PKT_Data+0)		/* Command code */
#define	PKT_PhbNum		(PKT_Data+1)		/* Port number wrt RTA */
#endif

#define	PKT_ModemClear		(PKT_Data+4)		/* Modem clear signals mask */

/* ModemClear definitions... */
#define	MBIC_RTS		0x01			/* RTS modem signal */
#define	MBIC_DTR		0x02			/* DTR modem signal */

/*****************************************************************************
**********************************          **********************************
**********************************   MSET   **********************************
**********************************          **********************************
*****************************************************************************/

/* (Driver->RIO,pre-emptive)

   MSET is sent pre-emptively from the driver to set/clear a port's modem signals. */
#if 0
#define	PKT_Cmd			(PKT_Data+0)		/* Command code */
#define	PKT_PhbNum		(PKT_Data+1)		/* Port number wrt RTA */
#endif

#define	PKT_ModemSet		(PKT_Data+4)		/* Modem set signals mask */

/* ModemSet definitions... */
#define	MSET_RTS		0x01			/* RTS modem signal */
#define	MSET_DTR		0x02			/* DTR modem signal */

/*****************************************************************************
*********************************            *********************************
*********************************   PCLOSE   *********************************
*********************************            *********************************
*****************************************************************************/

/* (Driver->RIO,in-band)

   PCLOSE is sent from the driver to pseudo close a previously opened port.
   
   The port will close when all data has been sent/received, however, the
   port's transmit / receive and modem signals will be left enabled and the
   port marked internally as Pseudo Closed. */

#define	PKT_Cmd			(PKT_Data+0)		/* Command code */

/*****************************************************************************
**********************************          **********************************
**********************************   MGET   **********************************
**********************************          **********************************
*****************************************************************************/

/* (Driver->RIO,pre-emptive)

   MGET is sent pre-emptively from the driver to request the port's current modem signals. */

#define	PKT_Cmd			(PKT_Data+0)		/* Command code */
#define	PKT_PhbNum		(PKT_Data+1)		/* Port number wrt RTA */

/*****************************************************************************
*********************************             ********************************
*********************************   MEMDUMP   ********************************
*********************************             ********************************
*****************************************************************************/

/* (Driver->RIO,pre-emptive)

   MEMDUMP is sent pre-emptively from the driver to request a dump of 32 bytes
   of the specified port's RTA address space.
   */
#if 0
#define	PKT_Cmd			(PKT_Data+0)		/* Command code */
#endif
#define	PKT_PhbNum		(PKT_Data+1)		/* Port number wrt RTA */
#define	PKT_SubCmd		(PKT_Data+5)		/* Sub Command */
#define	PKT_Address		(PKT_Data+6)		/* Requested address */

/*****************************************************************************
******************************                   *****************************
******************************   READ_REGISTER   *****************************
******************************                   *****************************
*****************************************************************************/

/* (Driver->RIO,pre-emptive)

   READ_REGISTER is sent pre-emptively from the driver to request the contents
   of the CD1400 register specified in address.
   */
#if 0
#define	PKT_Cmd			(PKT_Data+0)		/* Command code */
#endif
#define	PKT_PhbNum		(PKT_Data+1)		/* Port number wrt RTA */
#define	PKT_SubCmd		(PKT_Data+5)		/* Sub Command */
#define	PKT_Address		(PKT_Data+6)		/* Requested address */

/*****************************************************************************
************************                            **************************
************************   COMMAND_RUP - COMPLETE   **************************
************************                            **************************
*****************************************************************************/

/* (RIO->Driver,pre-emptive)

   COMMAND_RUP - COMPLETE is sent in response to all port I/O control command
   packets, except MEMDUMP and READ_REGISTER.
   */
#if 0
#define	PKT_Cmd			(PKT_Data+0)		/* Command code */
#endif
#define	PKT_PhbNum		(PKT_Data+1)		/* Port number wrt RTA */
#define	PKT_Cmd2		(PKT_Data+2)		/* Command code copy */
#define	PKT_ModemStatus		(PKT_Data+3)		/* Modem signal status */
#define	PKT_PortStatus		(PKT_Data+4)		/* Port signal status */
#define	PKT_SubCmd		(PKT_Data+5)		/* Sub Command */

/* ModemStatus definitions... */
#define	MODEM_DSR		0x80			/* Data Set Ready modem state */
#define	MODEM_CTS		0x40			/* Clear To Send modem state */
#define	MODEM_RI		0x20			/* Ring Indicate modem state */
#define	MODEM_CD		0x10			/* Carrier Detect modem state */
#define	MODEM_TSTOP		0x08			/* Transmit Stopped state */
#define	MODEM_TEMPTY		0x04			/* Transmit Empty state */
#define	MODEM_DTR		0x02			/* DTR modem output state */
#define	MODEM_RTS		0x01			/* RTS modem output state */

/* PortStatus definitions... */
#define	PORT_ISOPEN		0x01			/* Port open ? */
#define	PORT_HUPCL		0x02			/* Hangup on close? */
#define	PORT_MOPENPEND		0x04			/* Modem open pending */
#define	PORT_ISPARALLEL		0x08			/* Parallel port */
#define	PORT_BREAK		0x10			/* Port on break */
#define	PORT_STATUSPEND		0020			/* Status packet pending */
#define	PORT_BREAKPEND		0x40			/* Break packet pending */
#define	PORT_MODEMPEND		0x80			/* Modem status packet pending */

/*****************************************************************************
************************                            **************************
************************   COMMAND_RUP - COMPLETE   **************************
************************                            **************************
*****************************************************************************/

/* (RIO->Driver,pre-emptive)

   COMMAND_RUP - COMPLETE is sent in response to all port I/O control command
   packets, except MEMDUMP and READ_REGISTER.
   */
#if 0
#define	PKT_Cmd			(PKT_Data+0)		/* Command code */
#define	PKT_PhbNum		(PKT_Data+1)		/* Port number wrt RTA */
#define	PKT_Cmd2		(PKT_Data+2)		/* Command code copy */
#endif
#define	PKT_ModemStatus		(PKT_Data+3)		/* Modem signal status */
#define	PKT_PortStatus		(PKT_Data+4)		/* Port signal status */
#if 0
#define	PKT_SubCmd		(PKT_Data+5)		/* Sub Command */
#endif

/* ModemStatus definitions... */
#define	MODEM_DSR		0x80			/* Data Set Ready modem state */
#define	MODEM_CTS		0x40			/* Clear To Send modem state */
#define	MODEM_RI		0x20			/* Ring Indicate modem state */
#define	MODEM_CD		0x10			/* Carrier Detect modem state */
#define	MODEM_TSTOP		0x08			/* Transmit Stopped state */
#define	MODEM_TEMPTY		0x04			/* Transmit Empty state */
#define	MODEM_DTR		0x02			/* DTR modem output state */
#define	MODEM_RTS		0x01			/* RTS modem output state */

/* PortStatus definitions... */
#define	PORT_ISOPEN		0x01			/* Port open ? */
#define	PORT_HUPCL		0x02			/* Hangup on close? */
#define	PORT_MOPENPEND		0x04			/* Modem open pending */
#define	PORT_ISPARALLEL		0x08			/* Parallel port */
#define	PORT_BREAK		0x10			/* Port on break */
#define	PORT_STATUSPEND		0020			/* Status packet pending */
#define	PORT_BREAKPEND		0x40			/* Break packet pending */
#define	PORT_MODEMPEND		0x80			/* Modem status packet pending */

/*****************************************************************************
********************                                      ********************
********************   COMMAND_RUP - COMPLETE - MEMDUMP   ********************
********************                                      ********************
*****************************************************************************/

/* (RIO->Driver,pre-emptive)

   COMMAND_RUP - COMPLETE - MEMDUMP is sent as an acknowledgement for a MEMDUMP
   port I/O control command packet.
   */
#if 0
#define	PKT_Cmd			(PKT_Data+0)		/* Command code */
#define	PKT_PhbNum		(PKT_Data+1)		/* Port number wrt RTA */
#define	PKT_Cmd2		(PKT_Data+2)		/* Command code copy */
#define	PKT_ModemStatus		(PKT_Data+3)		/* Modem signal status */
#define	PKT_PortStatus		(PKT_Data+4)		/* Port signal status */
#define	PKT_SubCmd		(PKT_Data+5)		/* Sub Command */
#define	PKT_Address		(PKT_Data+6)		/* Requested address */
#endif
#define	PKT_Dump		(PKT_Data+8)		/* 32bytes of requested dump data */

/*****************************************************************************
*****************                                            *****************
*****************   COMMAND_RUP - COMPLETE - READ_REGISTER   *****************
*****************                                            *****************
*****************************************************************************/

/* (RIO->Driver,pre-emptive)

   COMMAND_RUP - COMPLETE - READ_REGISTER is sent as an acknowledgement for a
   READ_REGISTER port I/O control command packet.
   */
#if 0
#define	PKT_Cmd			(PKT_Data+0)		/*Command code */
#define	PKT_PhbNum		(PKT_Data+1)		/*Port number wrt RTA */
#define	PKT_Cmd2		(PKT_Data+2)		/* Command code copy */
#endif
#define	PKT_RegisterValue	(PKT_Data+3)		/* Modem signal status */
#if 0
#define	PKT_PortStatus		(PKT_Data+4)		/* Port signal status */
#define	PKT_SubCmd		(PKT_Data+5)		/* Sub Command */
#endif

/*****************************************************************************
*********************                                  ***********************
*********************   COMMAND_RUP - BREAK_RECEIVED   ***********************
*********************                                  ***********************
*****************************************************************************/

/* (RIO->Driver,pre-emptive)

   COMMAND_RUP - BREAK_RECEIVED packets are sent when the port detects a receive BREAK signal.
   */
#if 0
#define	PKT_Cmd			(PKT_Data+0)		/* Command code */
#define	PKT_PhbNum		(PKT_Data+1)		/* Port number wrt RTA */
#define	PKT_Cmd2		(PKT_Data+2)		/* Command code copy */
#endif

/*****************************************************************************
*********************                                *************************
*********************   COMMAND_RUP - MODEM_STATUS   *************************
*********************                                *************************
*****************************************************************************/

/* (RIO->Driver,pre-emptive)

   COMMAND_RUP - MODEM_STATUS packets are sent whenever the port detects a
   change in the input modem signal states.

   */
#if 0
#define	PKT_Cmd			(PKT_Data+0)		/* Command code */
#define	PKT_PhbNum		(PKT_Data+1)		/* Port number wrt RTA */
#define	PKT_Cmd2		(PKT_Data+2)		/* Command code copy */
#define	PKT_ModemStatus		(PKT_Data+3)		/* Modem signal status */
#endif

/*****************************************************************************
************************                             *************************
************************   BOOT_RUP - BOOT_REQUEST   *************************
************************                             *************************
*****************************************************************************/

/* (RIO->Driver,pre-emptive)

   BOOT_RUP - BOOT_REQUEST packets are sent to the Driver from RIO to request
   firmware code to load onto attached RTAs.
   */
#if 0
#define	PKT_Cmd			(PKT_Data+0)		/* Command code */
#endif

/*****************************************************************************
************************                              ************************
************************   BOOT_RUP - BOOT_SEQUENCE   ************************
************************                              ************************
*****************************************************************************/

/* (Driver->RIO,pre-emptive)

   BOOT_RUP - BOOT_SEQUENCE packets are sent from the Driver to RIO in response
   to a BOOT_RUP - BOOT_REQUEST packet.
   */
#if 0
#define	PKT_Cmd			(PKT_Data+0)		/* Command code */
#endif
#define	PKT_NumPackets		(PKT_Data+2)		/* Packets required to load firmware */
#define	PKT_LoadBase		(PKT_Data+4)		/* RTA firmware load address */
#define	PKT_CodeSize		(PKT_Data+6)		/* Size of firmware in bytes */
#define	PKT_CmdString		(PKT_Data+8)		/* Command string */

/*****************************************************************************
************************                               ***********************
************************   BOOT_RUP - BOOT_COMPLETED   ***********************
************************                               ***********************
*****************************************************************************/

/* (RIO->Driver,pre-emptive)

   BOOT_RUP - BOOT_COMPLETE is sent to the Driver from RIO when downloading of
   RTA firmware has completed.
   */
#if 0
#define	PKT_Cmd			(PKT_Data+0)		/* Command code */
#endif
#define	PKT_LinkNumber		(PKT_Data+1)		/* Link number RTA booted on */
#define	PKT_SerialNumber	(PKT_Data+2)		/* 4 byte serial number */

/*****************************************************************************
************************                               ***********************
************************   BOOT_RUP - Packet Request   ***********************
************************                               ***********************
*****************************************************************************/

/* (RIO->Driver,pre-emptive)

   BOOT_RUP packet without the PKT_CMD_BIT set in the PKT->len field is sent
   from RIO to the Driver as a request for a firmware boot packet. */

#define	PKT_SequenceNumber	(PKT_Data+0)		/* Packet sequence number */

/*****************************************************************************
***********************                                ***********************
***********************   BOOT_RUP - Packet Response   ***********************
***********************                                ***********************
*****************************************************************************/

/* (Driver->RIO,pre-emptive)

   In response to a BOOT_RUP boot packet request, the driver fills out the response
   packet with the 70 bytes of the requested sequence.
   */
#if 0
#define	PKT_SequenceNumber	(PKT_Data+0)		/* Packet sequence number */
#endif
#define	PKT_FirmwarePacket	(PKT_Data+2)		/* Firmware packet */

/*****************************************************************************
****************************                      ****************************
****************************   BOOT_RUP - IFOAD   ****************************
****************************                      ****************************
*****************************************************************************/

/* (Driver->RIO,pre-emptive)

   BOOT_RUP - IFOAD packets are sent from the Driver to an RTA to cause the
   RTA to shut down and reboot.
   */
#if 0
#define	PKT_Cmd			(PKT_Data+0)		/* Command code */
#endif
#define	PKT_IfoadId1		(PKT_Data+2)		/* IFOAD Id 1 */
#define	PKT_IfoadId2		(PKT_Data+3)		/* IFOAD Id 2 */

#define	IFOADID1		0xAD
#define	IFOADID2		0xF0

/*****************************************************************************
**************************                         ***************************
**************************   BOOT_RUP - IDENTIFY   ***************************
**************************                         ***************************
*****************************************************************************/

/* (Driver->RIO,pre-emptive)

   BOOT_RUP - IDENTIFY packets are sent from the Driver to an RTA to cause the
   RTA to flash its LEDs for a period of time.
   */
#if 0
#define	PKT_Cmd			(PKT_Data+0)		/* Command code */
#endif
#define	PKT_IdentifyId		(PKT_Data+2)		/* defines pattern to flash */

/*****************************************************************************
****************************                       ***************************
****************************   BOOT_RUP - ZOMBIE   ***************************
****************************                       ***************************
*****************************************************************************/

/* (Driver->RIO,pre-emptive)

   BOOT_RUP - ZOMBIE packets are sent from the Driver to an RTA to cause the
   RTA to shut down and flash it's LEDs.
   */
#if 0
#define	PKT_Cmd			(PKT_Data+0)		/* Command code */
#endif
#define	PKT_ZombieId1		(PKT_Data+2)		/* ZOMBIE Id 1 */
#define	PKT_ZombieId2		(PKT_Data+3)		/* ZOMBIE Id 2 */

#define	ZOMBIEID1		0x52
#define	ZOMBIEID2		0x21

/*****************************************************************************
****************************                      ****************************
****************************   BOOT_RUP - UFOAD   ****************************
****************************                      ****************************
*****************************************************************************/

/* (Driver->RIO,pre-emptive)

   BOOT_RUP - UFOAD packets are sent from the Driver to an RTA to cause the RTA
   to ask it's neighbouring RTA to shut down and reboot.
   */
#if 0
#define	PKT_Cmd			(PKT_Data+0)		/* Command code */
#define	PKT_LinkNumber		(PKT_Data+1)		/* Link number of RTA to UFOAD */
#endif
#define	PKT_UfoadId1		(PKT_Data+2)		/* UFOAD Id 1 */
#define	PKT_UfoadId2		(PKT_Data+3)		/* UFOAD Id 2 */

#define	UFOADID1		0x1E
#define	UFOADID2		0x0D

/*****************************************************************************
****************************                      ****************************
****************************   BOOT_RUP - IWAIT   ****************************
****************************                      ****************************
*****************************************************************************/

/* (Driver->RIO,pre-emptive)

   BOOT_RUP - IWAIT packets are sent from the Driver to an RTA to cause the RTA
   to pause booting on the specified link for 30 seconds.
   */
#if 0
#define	PKT_Cmd			(PKT_Data+0)		/* Command code */
#define	PKT_LinkNumber		(PKT_Data+1)		/* Link number of RTA to UFOAD */
#endif
#define	PKT_IwaitId1		(PKT_Data+2)		/* IWAIT Id 1 */
#define	PKT_IwaitId2		(PKT_Data+3)		/* IWAIT Id 2 */

#define	IWAITID1		0xDE
#define	IWAITID2		0xB1

/*****************************************************************************
************************                               ***********************
************************   ROUTE_RUP - ROUTE_REQUEST   ***********************
************************                               ***********************
*****************************************************************************/

/* (RIO->Driver,pre-emptive)

   ROUTE_RUP - ROUTE_REQUEST packets are sent from a newly booted or connected
   RTA to a Driver to request an ID (RUP or unit number).
   */
#if 0
#define	PKT_Cmd			(PKT_Data+0)		/* Command code */
#endif
#define	PKT_SerialNumber	(PKT_Data+2)		/* 4 byte serial number */
#define	PKT_ModuleTypes		(PKT_Data+6)		/* RTA Module types */

/* ModuleTypes definitions... */
#define	MOD_BLANK		0x0F			/* Blank plate attached */
#define	MOD_RS232DB25		0x00			/* RS232 DB25 connector */
#define	MOD_RS232RJ45		0x01			/* RS232 RJ45 connector */
#define	MOD_RS422DB25		0x02			/* RS422 DB25 connector */
#define	MOD_RS485DB25		0x03			/* RS485 DB25 connector */
#define	MOD_PARALLEL		0x04			/* Centronics parallel */

#define	MOD2			0x08			/* Set to indicate Rev2 module */

/*****************************************************************************
*************************                            *************************
*************************   ROUTE_RUP - ROUTE_FOAD   *************************
*************************                            *************************
*****************************************************************************/

/* (Driver->RIO,pre-emptive)

   ROUTE_RUP - ROUTE_FOAD packet is sent as a response to a ROUTE_RUP - ROUTE_REQUEST
   packet to cause the RTA to "Fall Over And Die"., i.e. shutdown and reboot.
   */
#if 0
#define	PKT_Cmd			(PKT_Data+0)		/* Command code */
#endif
#define	PKT_RouteCmdString	(PKT_Data+2)		/* Command string */

/*****************************************************************************
***********************                                ***********************
***********************   ROUTE_RUP - ROUTE_ALLOCATE   ***********************
***********************                                ***********************
*****************************************************************************/

/* (Driver->RIO,pre-emptive)

   ROUTE_RUP - ROUTE_ALLOCATE packet is sent as a response to a ROUTE_RUP - ROUTE_REQUEST
   packet to allocate the RTA's Id number (RUP number 1..16)
   */
#if 0
#define	PKT_Cmd			(PKT_Data+0)		/* Command code */
#endif
#define	PKT_IdNum		(PKT_Data+1)		/* RUP number for ports 1..8 */
#if 0
#define	PKT_RouteCmdString	(PKT_Data+2)		/* Command string */
#endif
#define	PKT_IdNum2		(PKT_Data+0x17)		/* RUP number for ports 9..16 */

/*****************************************************************************
***********************                                ***********************
***********************   ROUTE_RUP - ROUTE_TOPOLOGY   ***********************
***********************                                ***********************
*****************************************************************************/

/* (RIO->Driver,pre-emptive)

   ROUTE_RUP - ROUTE_TOPOLOGY packet is sent to inform the driver of an RTA's
   current link status.
   */
#if 0
#define	PKT_Cmd			(PKT_Data+0)		/* Command code */
#endif
#define	PKT_Link1Rup		(PKT_Data+2)		/* Link 1 RUP number */
#define	PKT_Link1Link		(PKT_Data+3)		/* Link 1 link number */
#define	PKT_Link2Rup		(PKT_Data+4)		/* Link 2 RUP number */
#define	PKT_Link2Link		(PKT_Data+5)		/* Link 2 link number */
#define	PKT_Link3Rup		(PKT_Data+6)		/* Link 3 RUP number */
#define	PKT_Link3Link		(PKT_Data+7)		/* Link 3 link number */
#define	PKT_Link4Rup		(PKT_Data+8)		/* Link 4 RUP number */
#define	PKT_Link4Link		(PKT_Data+9)		/* Link 4 link number */
#define	PKT_RtaVpdProm		(PKT_Data+10)		/* 32 bytes of RTA VPD PROM Contents */

#endif						/* _sxwinif_h */

/* End of RIOWINIF.H */
