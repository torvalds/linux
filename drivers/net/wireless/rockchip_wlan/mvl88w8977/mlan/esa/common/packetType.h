/** @file packetType.h
 *
 *  @brief This file defines Packet Type enumeration used for PacketType fields in RX and TX
 *          packet descriptors
 *
 * Copyright (C) 2014-2017, Marvell International Ltd.
 *
 * This software file (the "File") is distributed by Marvell International
 * Ltd. under the terms of the GNU General Public License Version 2, June 1991
 * (the "License").  You may use, redistribute and/or modify this File in
 * accordance with the terms and conditions of the License, a copy of which
 * is available by writing to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA or on the
 * worldwide web at http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt.
 *
 * THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
 * ARE EXPRESSLY DISCLAIMED.  The License provides additional details about
 * this warranty disclaimer.
 */

/******************************************************
Change log:
    03/07/2014: Initial version
******************************************************/
#ifndef _PACKETTYPE_H_
#define _PACKETTYPE_H_

/**
*** @brief Enumeration of different Packets Types.
**/

typedef enum {
	PKT_TYPE_802DOT3_DEFAULT = 0,	/*!< For RX packets it represents
					 **   IEEE 802.3 SNAP frame .  For
					 **   TX Packets.  This Field is for
					 **   backwards compatibility only and
					 **   should not be used going
					 **   forward.
					 */
	PKT_TYPE_802DOT3_LLC = 1,	//!< IEEE 802.3 frame with LLC header
	PKT_TYPE_ETHERNET_V2 = 2,	//!< Ethernet version 2 frame
	PKT_TYPE_802DOT3_SNAP = 3,	//!< IEEE 802.3 SNAP frame
	PKT_TYPE_802DOT3 = 4,	//!< IEEE 802.3 frame
	PKT_TYPE_802DOT11 = 5,	//!< IEEE 802.11 frame
	PKT_TYPE_ETCP_SOCKET_DATA = 7,	//!< eTCP Socket Data
	PKT_TYPE_RAW_DATA = 8,	//!< Non socket data when using eTCP
	PKT_TYPE_MRVL_MESH = 9,	//!< Marvell Mesh frame

	/* Marvell Internal firmware packet types
	 ** Range from 0x0E to 0xEE
	 ** These internal Packet types should grow from
	 ** 0xEE down.  This will leave room incase the packet
	 ** types between the driver & firmware need to be expanded
	 */
	PKT_TYPE_MRVL_EAPOL_MSG = 0xDF,
	PKT_TYPE_MRVL_BT_AMP = 0xE0,
	PKT_TYPE_FWD_MGT = 0xE2,
	PKT_TYPE_MGT = 0xE5,
	PKT_TYPE_MRVL_AMSDU = 0xE6,
	PKT_TYPE_MRVL_BAR = 0xE7,
	PKT_TYPE_MRVL_LOOPBACK = 0xE8,
	PKT_TYPE_MRVL_DATA_MORE = 0xE9,
	PKT_TYPE_MRVL_DATA_LAST = 0xEA,
	PKT_TYPE_MRVL_DATA_NULL = 0xEB,
	PKT_TYPE_MRVL_UNKNOWN = 0xEC,
	PKT_TYPE_MRVL_SEND_TO_DATASWITCH = 0xED,
	PKT_TYPE_MRVL_SEND_TO_HOST = 0xEE,

	PKT_TYPE_DEBUG = 0xEF,

	// Customer range for Packet Types
	// Range 0xF0 to 0xFF
} PacketType;

#endif
