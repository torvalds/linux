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
 *   Haiyang Zhang <haiyangz@microsoft.com>
 *   Hank Janssen  <hjanssen@microsoft.com>
 *
 */


#ifndef _NETVSC_API_H_
#define _NETVSC_API_H_

#include "vmbus_api.h"

/* Fwd declaration */
struct hv_netvsc_packet;

/* Represent the xfer page packet which contains 1 or more netvsc packet */
struct xferpage_packet {
	struct list_head ListEntry;

	/* # of netvsc packets this xfer packet contains */
	u32 Count;
};

/* The number of pages which are enough to cover jumbo frame buffer. */
#define NETVSC_PACKET_MAXPAGE		4

/*
 * Represent netvsc packet which contains 1 RNDIS and 1 ethernet frame
 * within the RNDIS
 */
struct hv_netvsc_packet {
	/* Bookkeeping stuff */
	struct list_head ListEntry;

	struct hv_device *Device;
	bool IsDataPacket;

	/*
	 * Valid only for receives when we break a xfer page packet
	 * into multiple netvsc packets
	 */
	struct xferpage_packet *XferPagePacket;

	union {
		struct{
			u64 ReceiveCompletionTid;
			void *ReceiveCompletionContext;
			void (*OnReceiveCompletion)(void *context);
		} Recv;
		struct{
			u64 SendCompletionTid;
			void *SendCompletionContext;
			void (*OnSendCompletion)(void *context);
		} Send;
	} Completion;

	/* This points to the memory after PageBuffers */
	void *Extension;

	u32 TotalDataBufferLength;
	/* Points to the send/receive buffer where the ethernet frame is */
	u32 PageBufferCount;
	struct hv_page_buffer PageBuffers[NETVSC_PACKET_MAXPAGE];
};

/* Represents the net vsc driver */
struct netvsc_driver {
	/* Must be the first field */
	/* Which is a bug FIXME! */
	struct hv_driver Base;

	u32 RingBufferSize;
	u32 RequestExtSize;

	/*
	 * This is set by the caller to allow us to callback when we
	 * receive a packet from the "wire"
	 */
	int (*OnReceiveCallback)(struct hv_device *dev,
				 struct hv_netvsc_packet *packet);
	void (*OnLinkStatusChanged)(struct hv_device *dev, u32 Status);

	/* Specific to this driver */
	int (*OnSend)(struct hv_device *dev, struct hv_netvsc_packet *packet);

	void *Context;
};

struct netvsc_device_info {
    unsigned char MacAddr[6];
    bool LinkState;	/* 0 - link up, 1 - link down */
};

/* Interface */
int NetVscInitialize(struct hv_driver *drv);
int RndisFilterOnOpen(struct hv_device *Device);
int RndisFilterOnClose(struct hv_device *Device);

#endif /* _NETVSC_API_H_ */
