/******************************************************************************
 *
 * Copyright(c) 2007 - 2010 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 * Modifications for inclusion into the Linux staging tree are
 * Copyright(c) 2010 Larry Finger. All rights reserved.
 *
 * Contact information:
 * WLAN FAE <wlanfae@realtek.com>
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 ******************************************************************************/
#ifndef __INC_ETHERNET_H
#define __INC_ETHERNET_H

#define ETHERNET_ADDRESS_LENGTH		6	/*!< Ethernet Address Length*/
#define ETHERNET_HEADER_SIZE		14	/*!< Ethernet Header Length*/
#define LLC_HEADER_SIZE			6	/*!< LLC Header Length*/
#define TYPE_LENGTH_FIELD_SIZE		2	/*!< Type/Length Size*/
#define MINIMUM_ETHERNET_PACKET_SIZE	60	/*!< Min Ethernet Packet Size*/
#define MAXIMUM_ETHERNET_PACKET_SIZE	1514	/*!< Max Ethernet Packet Size*/

/*!< Is Multicast Address? */
#define RT_ETH_IS_MULTICAST(_pAddr)	((((u8 *)(_pAddr))[0]&0x01) != 0)

#endif /* #ifndef __INC_ETHERNET_H */

