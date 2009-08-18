/*
 *
 * Copyright (c) 2009, Microsoft Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston, MA 02111-1307 USA.
 *
 * Authors:
 *   Hank Janssen  <hjanssen@microsoft.com>
 *
 */


#ifndef _NETVSC_H_
#define _NETVSC_H_

#include "include/VmbusPacketFormat.h"
#include "include/nvspprotocol.h"

#include "include/List.h"

#include "include/NetVscApi.h"

/* #define NVSC_MIN_PROTOCOL_VERSION		1 */
/* #define NVSC_MAX_PROTOCOL_VERSION		1 */

#define NETVSC_SEND_BUFFER_SIZE			(64*1024)	/* 64K */
#define NETVSC_SEND_BUFFER_ID			0xface


#define NETVSC_RECEIVE_BUFFER_SIZE		(1024*1024)	/* 1MB */

#define NETVSC_RECEIVE_BUFFER_ID		0xcafe

#define NETVSC_RECEIVE_SG_COUNT			1

/* Preallocated receive packets */
#define NETVSC_RECEIVE_PACKETLIST_COUNT		256


/* Per netvsc channel-specific */
struct NETVSC_DEVICE {
	struct hv_device *Device;

	atomic_t RefCount;
	atomic_t NumOutstandingSends;
	/*
	 * List of free preallocated hv_netvsc_packet to represent receive
	 * packet
	 */
	LIST_ENTRY ReceivePacketList;
	spinlock_t receive_packet_list_lock;

	/* Send buffer allocated by us but manages by NetVSP */
	void *SendBuffer;
	u32 SendBufferSize;
	u32 SendBufferGpadlHandle;
	u32 SendSectionSize;

	/* Receive buffer allocated by us but manages by NetVSP */
	void *ReceiveBuffer;
	u32 ReceiveBufferSize;
	u32 ReceiveBufferGpadlHandle;
	u32 ReceiveSectionCount;
	PNVSP_1_RECEIVE_BUFFER_SECTION ReceiveSections;

	/* Used for NetVSP initialization protocol */
	struct osd_waitevent *ChannelInitEvent;
	NVSP_MESSAGE ChannelInitPacket;

	NVSP_MESSAGE RevokePacket;
	/* unsigned char HwMacAddr[HW_MACADDR_LEN]; */

	/* Holds rndis device info */
	void *Extension;
};

#endif /* _NETVSC_H_ */
