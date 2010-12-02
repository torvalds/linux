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


#ifndef _VMBUS_PRIVATE_H_
#define _VMBUS_PRIVATE_H_

#include "hv.h"
#include "vmbus_api.h"
#include "channel.h"
#include "channel_mgmt.h"
#include "ring_buffer.h"
#include <linux/list.h>


/*
 * Maximum channels is determined by the size of the interrupt page
 * which is PAGE_SIZE. 1/2 of PAGE_SIZE is for send endpoint interrupt
 * and the other is receive endpoint interrupt
 */
#define MAX_NUM_CHANNELS	((PAGE_SIZE >> 1) << 3)	/* 16348 channels */

/* The value here must be in multiple of 32 */
/* TODO: Need to make this configurable */
#define MAX_NUM_CHANNELS_SUPPORTED	256


enum VMBUS_CONNECT_STATE {
	Disconnected,
	Connecting,
	Connected,
	Disconnecting
};

#define MAX_SIZE_CHANNEL_MESSAGE	HV_MESSAGE_PAYLOAD_BYTE_COUNT

struct VMBUS_CONNECTION {
	enum VMBUS_CONNECT_STATE ConnectState;

	atomic_t NextGpadlHandle;

	/*
	 * Represents channel interrupts. Each bit position represents a
	 * channel.  When a channel sends an interrupt via VMBUS, it finds its
	 * bit in the sendInterruptPage, set it and calls Hv to generate a port
	 * event. The other end receives the port event and parse the
	 * recvInterruptPage to see which bit is set
	 */
	void *InterruptPage;
	void *SendInterruptPage;
	void *RecvInterruptPage;

	/*
	 * 2 pages - 1st page for parent->child notification and 2nd
	 * is child->parent notification
	 */
	void *MonitorPages;
	struct list_head ChannelMsgList;
	spinlock_t channelmsg_lock;

	/* List of channels */
	struct list_head ChannelList;
	spinlock_t channel_lock;

	struct workqueue_struct *WorkQueue;
};


struct VMBUS_MSGINFO {
	/* Bookkeeping stuff */
	struct list_head MsgListEntry;

	/* Synchronize the request/response if needed */
	struct osd_waitevent *WaitEvent;

	/* The message itself */
	unsigned char Msg[0];
};


extern struct VMBUS_CONNECTION gVmbusConnection;

/* General vmbus interface */

struct hv_device *vmbus_child_device_create(struct hv_guid *deviceType,
					 struct hv_guid *deviceInstance,
					 struct vmbus_channel *channel);

int VmbusChildDeviceAdd(struct hv_device *Device);

void VmbusChildDeviceRemove(struct hv_device *Device);

/* static void */
/* VmbusChildDeviceDestroy( */
/* struct hv_device *); */

struct vmbus_channel *GetChannelFromRelId(u32 relId);


/* Connection interface */

int VmbusConnect(void);

int VmbusDisconnect(void);

int VmbusPostMessage(void *buffer, size_t bufSize);

int VmbusSetEvent(u32 childRelId);

void VmbusOnEvents(void);


#endif /* _VMBUS_PRIVATE_H_ */
