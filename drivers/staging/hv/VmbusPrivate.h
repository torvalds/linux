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

#include "Hv.h"
#include "include/VmbusApi.h"
#include "Channel.h"
#include "ChannelMgmt.h"
#include "ChannelInterface.h"
//#include "ChannelMessages.h"
#include "RingBuffer.h"
//#include "Packet.h"
#include "include/List.h"

//
// Defines
//

// Maximum channels is determined by the size of the interrupt page which is PAGE_SIZE. 1/2 of PAGE_SIZE is for
// send endpoint interrupt and the other is receive endpoint interrupt
#define MAX_NUM_CHANNELS				(PAGE_SIZE >> 1) << 3  // 16348 channels

// The value here must be in multiple of 32
// TODO: Need to make this configurable
#define MAX_NUM_CHANNELS_SUPPORTED		256

//
// Data types
//

typedef enum {
	Disconnected,
	Connecting,
	Connected,
	Disconnecting
} VMBUS_CONNECT_STATE;

#define MAX_SIZE_CHANNEL_MESSAGE			HV_MESSAGE_PAYLOAD_BYTE_COUNT

typedef struct _VMBUS_CONNECTION {

	VMBUS_CONNECT_STATE					ConnectState;

	u32								NextGpadlHandle;

	// Represents channel interrupts. Each bit position
	// represents a channel.
	// When a channel sends an interrupt via VMBUS, it
	// finds its bit in the sendInterruptPage, set it and
	// calls Hv to generate a port event. The other end
	// receives the port event and parse the recvInterruptPage
	// to see which bit is set
	void *								InterruptPage;
	void *								SendInterruptPage;
	void *								RecvInterruptPage;

	// 2 pages - 1st page for parent->child notification and 2nd is child->parent notification
	void *								MonitorPages;
	LIST_ENTRY							ChannelMsgList;
	spinlock_t channelmsg_lock;

	// List of channels
	LIST_ENTRY							ChannelList;
	HANDLE								ChannelLock;

	HANDLE								WorkQueue;
} VMBUS_CONNECTION;


typedef struct _VMBUS_MSGINFO {
	// Bookkeeping stuff
	LIST_ENTRY			MsgListEntry;

	// Synchronize the request/response if needed
	HANDLE				WaitEvent;

	// The message itself
	unsigned char		Msg[0];
} VMBUS_MSGINFO;


//
// Externs
//
extern VMBUS_CONNECTION gVmbusConnection;

//
// General vmbus interface
//
static DEVICE_OBJECT*
VmbusChildDeviceCreate(
	GUID deviceType,
	GUID deviceInstance,
	void *context);

static int
VmbusChildDeviceAdd(
	DEVICE_OBJECT* Device);

static void
VmbusChildDeviceRemove(
   DEVICE_OBJECT* Device);

//static void
//VmbusChildDeviceDestroy(
//	DEVICE_OBJECT*);

static VMBUS_CHANNEL*
GetChannelFromRelId(
	u32 relId
	);

//
// Connection interface
//
static int
VmbusConnect(
	void
	);

static int
VmbusDisconnect(
	void
	);

static int
VmbusPostMessage(
	void *			buffer,
	size_t			bufSize
	);

static int
VmbusSetEvent(
	u32 childRelId
	);

static void
VmbusOnEvents(
  void
	);


#endif // _VMBUS_PRIVATE_H_
