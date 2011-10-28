/****************************************************************************
 *******                                                              *******
 *******               R U P   S T R U C T U R E
 *******                                                              *******
 ****************************************************************************

 Author  : Ian Nandhra
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

#ifndef _rup_h
#define _rup_h 1

#define MAX_RUP          ((short) 16)
#define PKTS_PER_RUP     ((short) 2)	/* They are always used in pairs */

/*************************************************
 * Define all the  packet request stuff
 ************************************************/
#define TX_RUP_INACTIVE          0	/* Nothing to transmit */
#define TX_PACKET_READY          1	/* Transmit packet ready */
#define TX_LOCK_RUP              2	/* Transmit side locked */

#define RX_RUP_INACTIVE          0	/* Nothing received */
#define RX_PACKET_READY          1	/* Packet received */

#define RUP_NO_OWNER             0xff	/* RUP not owned by any process */

struct RUP {
	u16 txpkt;		/* Outgoing packet */
	u16 rxpkt;		/* Incoming packet */
	u16 link;		/* Which link to send down? */
	u8 rup_dest_unit[2];	/* Destination unit */
	u16 handshake;		/* For handshaking */
	u16 timeout;		/* Timeout */
	u16 status;		/* Status */
	u16 txcontrol;		/* Transmit control */
	u16 rxcontrol;		/* Receive control */
};

#endif

/*********** end of file ***********/
