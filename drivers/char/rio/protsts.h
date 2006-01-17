/****************************************************************************
 *******                                                              *******
 *******      P R O T O C O L    S T A T U S   S T R U C T U R E      *******
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

#ifndef _protsts_h
#define _protsts_h 1


#ifdef SCCS_LABELS
#ifndef lint
/* static char *_rio_protsts_h_sccs = "@(#)protsts.h	1.4"; */
#endif
#endif

/*************************************************
 * ACK bit. Last Packet received OK. Set by
 * rxpkt to indicate that the Packet has been
 * received OK and that the LTT must set the ACK
 * bit in the next outward bound Packet
 * and re-set by LTT's after xmit.
 *
 * Gets shoved into rx_status
 ************************************************/
#define PHB_RX_LAST_PKT_ACKED    ((ushort) 0x080)

/*******************************************************
 * The Rx TOGGLE bit.
 * Stuffed into rx_status by RXPKT
 ******************************************************/
#define PHB_RX_DATA_WNDW         ((ushort) 0x040)

/*******************************************************
 * The Rx TOGGLE bit. Matches the setting in PKT.H
 * Stuffed into rx_status
 ******************************************************/
#define PHB_RX_TGL               ((ushort) 0x2000)


/*************************************************
 * This bit is set by the LRT to indicate that
 * an ACK (packet) must be returned.
 *
 * Gets shoved into tx_status
 ************************************************/
#define PHB_TX_SEND_PKT_ACK      ((ushort) 0x08)

/*************************************************
 * Set by LTT to indicate that an ACK is required
 *************************************************/
#define PHB_TX_ACK_RQRD         ((ushort) 0x01)


/*******************************************************
 * The Tx TOGGLE bit.
 * Stuffed into tx_status by RXPKT from the PKT WndW
 * field. Looked by the LTT when the NEXT Packet
 * is going to be sent.
 ******************************************************/
#define PHB_TX_DATA_WNDW         ((ushort) 0x04)


/*******************************************************
 * The Tx TOGGLE bit. Matches the setting in PKT.H
 * Stuffed into tx_status
 ******************************************************/
#define PHB_TX_TGL               ((ushort) 0x02)

/*******************************************************
 * Request intr bit. Set when the queue has gone quiet
 * and the PHB has requested an interrupt.
 ******************************************************/
#define PHB_TX_INTR             ((ushort) 0x100)

/*******************************************************
 * SET if the PHB cannot send any more data down the
 * Link
 ******************************************************/
#define PHB_TX_HANDSHAKE         ((ushort) 0x010)


#define RUP_SEND_WNDW		 ((ushort) 0x08) ;

#endif

/*********** end of file ***********/
