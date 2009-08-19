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

#include "VmbusApi.h"
#include "List.h"

/* Defines */

#define NETVSC_DEVICE_RING_BUFFER_SIZE			64*PAGE_SIZE

#define HW_MACADDR_LEN		6


/* Fwd declaration */

struct hv_netvsc_packet;



/* Data types */


typedef int (*PFN_ON_OPEN)(struct hv_device *Device);
typedef int (*PFN_ON_CLOSE)(struct hv_device *Device);

typedef void (*PFN_QUERY_LINKSTATUS)(struct hv_device *Device);
typedef int (*PFN_ON_SEND)(struct hv_device *dev, struct hv_netvsc_packet *packet);
typedef void (*PFN_ON_SENDRECVCOMPLETION)(void * Context);

typedef int (*PFN_ON_RECVCALLBACK)(struct hv_device *dev, struct hv_netvsc_packet *packet);
typedef void (*PFN_ON_LINKSTATUS_CHANGED)(struct hv_device *dev, u32 Status);

/* Represent the xfer page packet which contains 1 or more netvsc packet */
typedef struct _XFERPAGE_PACKET {
	LIST_ENTRY ListEntry;

	/* # of netvsc packets this xfer packet contains */
	u32				Count;
} XFERPAGE_PACKET;


/* The number of pages which are enough to cover jumbo frame buffer. */
#define NETVSC_PACKET_MAXPAGE  4

/*
 * Represent netvsc packet which contains 1 RNDIS and 1 ethernet frame
 * within the RNDIS
 */
struct hv_netvsc_packet {
	/* Bookkeeping stuff */
	LIST_ENTRY ListEntry;

	struct hv_device *Device;
	bool					IsDataPacket;

	/*
	 * Valid only for receives when we break a xfer page packet
	 * into multiple netvsc packets
	 */
	XFERPAGE_PACKET		*XferPagePacket;

	union {
		struct{
			u64						ReceiveCompletionTid;
			void *						ReceiveCompletionContext;
			PFN_ON_SENDRECVCOMPLETION	OnReceiveCompletion;
		} Recv;
		struct{
			u64						SendCompletionTid;
			void *						SendCompletionContext;
			PFN_ON_SENDRECVCOMPLETION	OnSendCompletion;
		} Send;
	} Completion;

	/* This points to the memory after PageBuffers */
	void *					Extension;

	u32					TotalDataBufferLength;
	/* Points to the send/receive buffer where the ethernet frame is */
	u32					PageBufferCount;
	PAGE_BUFFER				PageBuffers[NETVSC_PACKET_MAXPAGE];

};


/* Represents the net vsc driver */
typedef struct _NETVSC_DRIVER_OBJECT {
	struct hv_driver Base; /* Must be the first field */

	u32						RingBufferSize;
	u32						RequestExtSize;

	/* Additional num  of page buffers to allocate */
	u32						AdditionalRequestPageBufferCount;

	/*
	 * This is set by the caller to allow us to callback when we
	 * receive a packet from the "wire"
	 */
	PFN_ON_RECVCALLBACK			OnReceiveCallback;

	PFN_ON_LINKSTATUS_CHANGED	OnLinkStatusChanged;

	/* Specific to this driver */
	PFN_ON_OPEN					OnOpen;
	PFN_ON_CLOSE				OnClose;
	PFN_ON_SEND					OnSend;
	/* PFN_ON_RECVCOMPLETION	OnReceiveCompletion; */

	/* PFN_QUERY_LINKSTATUS		QueryLinkStatus; */

	void*						Context;
} NETVSC_DRIVER_OBJECT;


typedef struct _NETVSC_DEVICE_INFO {
    unsigned char	MacAddr[6];
    bool	LinkState;	/* 0 - link up, 1 - link down */
} NETVSC_DEVICE_INFO;


/* Interface */

int
NetVscInitialize(
	struct hv_driver *drv
	);

#endif /* _NETVSC_API_H_ */
