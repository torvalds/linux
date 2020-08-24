/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2017 Realtek Corporation.
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
 *****************************************************************************/
/*! \file */
#ifndef __INC_ETHERNET_H
#define __INC_ETHERNET_H

#define ETHERNET_ADDRESS_LENGTH				6		/* !< Ethernet Address Length */
#define ETHERNET_HEADER_SIZE				14		/* !< Ethernet Header Length */
#define LLC_HEADER_SIZE						6		/* !< LLC Header Length */
#define TYPE_LENGTH_FIELD_SIZE				2		/* !< Type/Length Size */
#define MINIMUM_ETHERNET_PACKET_SIZE		60		/* !< Minimum Ethernet Packet Size */
#define MAXIMUM_ETHERNET_PACKET_SIZE		1514	/* !< Maximum Ethernet Packet Size */

#define RT_ETH_IS_MULTICAST(_pAddr)	((((u8 *)(_pAddr))[0]&0x01) != 0)		/* !< Is Multicast Address? */
#define RT_ETH_IS_BROADCAST(_pAddr)	(\
		((u8 *)(_pAddr))[0] == 0xff	&&		\
		((u8 *)(_pAddr))[1] == 0xff	&&		\
		((u8 *)(_pAddr))[2] == 0xff	&&		\
		((u8 *)(_pAddr))[3] == 0xff	&&		\
		((u8 *)(_pAddr))[4] == 0xff	&&		\
		((u8 *)(_pAddr))[5] == 0xff)	/* !< Is Broadcast Address? */


#endif /*  #ifndef __INC_ETHERNET_H */
