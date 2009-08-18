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


#ifndef _CHANNEL_MGMT_H_
#define _CHANNEL_MGMT_H_

#include "include/osd.h"
#include "include/List.h"
#include "RingBuffer.h"

#include "include/VmbusChannelInterface.h"
#include "include/ChannelMessages.h"



typedef void (*PFN_CHANNEL_CALLBACK)(void *context);

typedef enum {
	CHANNEL_OFFER_STATE,
	CHANNEL_OPENING_STATE,
	CHANNEL_OPEN_STATE,
} VMBUS_CHANNEL_STATE;

typedef struct _VMBUS_CHANNEL {
	LIST_ENTRY ListEntry;

	struct hv_device *DeviceObject;

	struct timer_list poll_timer; /* SA-111 workaround */

	VMBUS_CHANNEL_STATE State;

	VMBUS_CHANNEL_OFFER_CHANNEL OfferMsg;
	/*
	 * These are based on the OfferMsg.MonitorId.
	 * Save it here for easy access.
	 */
	u8 MonitorGroup;
	u8 MonitorBit;

	u32 RingBufferGpadlHandle;

	/* Allocated memory for ring buffer */
	void *RingBufferPages;
	u32 RingBufferPageCount;
	RING_BUFFER_INFO Outbound;	/* send to parent */
	RING_BUFFER_INFO Inbound;	/* receive from parent */
	spinlock_t inbound_lock;
	struct workqueue_struct *ControlWQ;

	/* Channel callback are invoked in this workqueue context */
	/* HANDLE dataWorkQueue; */

	PFN_CHANNEL_CALLBACK OnChannelCallback;
	void *ChannelCallbackContext;
} VMBUS_CHANNEL;


typedef struct _VMBUS_CHANNEL_DEBUG_INFO {
	u32 RelId;
	VMBUS_CHANNEL_STATE State;
	GUID InterfaceType;
	GUID InterfaceInstance;
	u32 MonitorId;
	u32 ServerMonitorPending;
	u32 ServerMonitorLatency;
	u32 ServerMonitorConnectionId;
	u32 ClientMonitorPending;
	u32 ClientMonitorLatency;
	u32 ClientMonitorConnectionId;

	RING_BUFFER_DEBUG_INFO Inbound;
	RING_BUFFER_DEBUG_INFO Outbound;
} VMBUS_CHANNEL_DEBUG_INFO;


typedef union {
	VMBUS_CHANNEL_VERSION_SUPPORTED VersionSupported;
	VMBUS_CHANNEL_OPEN_RESULT OpenResult;
	VMBUS_CHANNEL_GPADL_TORNDOWN GpadlTorndown;
	VMBUS_CHANNEL_GPADL_CREATED GpadlCreated;
	VMBUS_CHANNEL_VERSION_RESPONSE VersionResponse;
} VMBUS_CHANNEL_MESSAGE_RESPONSE;


/*
 * Represents each channel msg on the vmbus connection This is a
 * variable-size data structure depending on the msg type itself
 */
typedef struct _VMBUS_CHANNEL_MSGINFO {
	/* Bookkeeping stuff */
	LIST_ENTRY MsgListEntry;

	/* So far, this is only used to handle gpadl body message */
	LIST_ENTRY SubMsgList;

	/* Synchronize the request/response if needed */
	struct osd_waitevent *WaitEvent;

	VMBUS_CHANNEL_MESSAGE_RESPONSE Response;

	u32 MessageSize;
	/*
	 * The channel message that goes out on the "wire".
	 * It will contain at minimum the VMBUS_CHANNEL_MESSAGE_HEADER header
	 */
	unsigned char Msg[0];
} VMBUS_CHANNEL_MSGINFO;


VMBUS_CHANNEL *AllocVmbusChannel(void);

void FreeVmbusChannel(VMBUS_CHANNEL *Channel);

void VmbusOnChannelMessage(void *Context);

int VmbusChannelRequestOffers(void);

void VmbusChannelReleaseUnattachedChannels(void);

#endif /* _CHANNEL_MGMT_H_ */
