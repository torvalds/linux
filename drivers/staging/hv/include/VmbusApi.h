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


#ifndef _VMBUS_API_H_
#define _VMBUS_API_H_



/* Defines */


#define MAX_PAGE_BUFFER_COUNT				16
#define MAX_MULTIPAGE_BUFFER_COUNT			32 /* 128K */

/* Data types */


#pragma pack(push,1)

/* Single-page buffer */
typedef struct _PAGE_BUFFER {
	u32	Length;
	u32	Offset;
	u64	Pfn;
} PAGE_BUFFER;

/* Multiple-page buffer */
typedef struct _MULTIPAGE_BUFFER {
	/* Length and Offset determines the # of pfns in the array */
	u32	Length;
	u32	Offset;
	u64	PfnArray[MAX_MULTIPAGE_BUFFER_COUNT];
}MULTIPAGE_BUFFER;

/* 0x18 includes the proprietary packet header */
#define MAX_PAGE_BUFFER_PACKET			(0x18 + (sizeof(PAGE_BUFFER) * MAX_PAGE_BUFFER_COUNT))
#define MAX_MULTIPAGE_BUFFER_PACKET		(0x18 + sizeof(MULTIPAGE_BUFFER))


#pragma pack(pop)

struct hv_driver;
struct hv_device;

/* All drivers */
typedef int (*PFN_ON_DEVICEADD)(struct hv_device *Device, void* AdditionalInfo);
typedef int (*PFN_ON_DEVICEREMOVE)(struct hv_device *Device);
typedef char** (*PFN_ON_GETDEVICEIDS)(void);
typedef void (*PFN_ON_CLEANUP)(struct hv_driver *Driver);

/* Vmbus extensions */
/* typedef int (*PFN_ON_MATCH)(struct hv_device *dev, struct hv_driver *drv); */
/* typedef int (*PFN_ON_PROBE)(struct hv_device *dev); */
typedef int	(*PFN_ON_ISR)(struct hv_driver *drv);
typedef void (*PFN_ON_DPC)(struct hv_driver *drv);
typedef void (*PFN_GET_CHANNEL_OFFERS)(void);

typedef struct hv_device *(*PFN_ON_CHILDDEVICE_CREATE)(GUID DeviceType, GUID DeviceInstance, void *Context);
typedef void (*PFN_ON_CHILDDEVICE_DESTROY)(struct hv_device *Device);
typedef int (*PFN_ON_CHILDDEVICE_ADD)(struct hv_device *RootDevice, struct hv_device *ChildDevice);
typedef void (*PFN_ON_CHILDDEVICE_REMOVE)(struct hv_device *Device);

/* Vmbus channel interface */
typedef void (*VMBUS_CHANNEL_CALLBACK)(void * context);

typedef int	(*VMBUS_CHANNEL_OPEN)(
	struct hv_device *Device,
	u32				SendBufferSize,
	u32				RecvRingBufferSize,
	void *				UserData,
	u32				UserDataLen,
	VMBUS_CHANNEL_CALLBACK ChannelCallback,
	void *				Context
	);

typedef void (*VMBUS_CHANNEL_CLOSE)(
	struct hv_device *Device
	);

typedef int	(*VMBUS_CHANNEL_SEND_PACKET)(
	struct hv_device *Device,
	const void *			Buffer,
	u32				BufferLen,
	u64				RequestId,
	u32				Type,
	u32				Flags
);

typedef int	(*VMBUS_CHANNEL_SEND_PACKET_PAGEBUFFER)(
	struct hv_device *Device,
	PAGE_BUFFER			PageBuffers[],
	u32				PageCount,
	void *				Buffer,
	u32				BufferLen,
	u64				RequestId
	);

typedef int	(*VMBUS_CHANNEL_SEND_PACKET_MULTIPAGEBUFFER)(
	struct hv_device *Device,
	MULTIPAGE_BUFFER	*MultiPageBuffer,
	void *				Buffer,
	u32				BufferLen,
	u64				RequestId
);

typedef int	(*VMBUS_CHANNEL_RECV_PACKET)(
	struct hv_device *Device,
	void *				Buffer,
	u32				BufferLen,
	u32*				BufferActualLen,
	u64*				RequestId
	);

typedef int	(*VMBUS_CHANNEL_RECV_PACKET_PAW)(
	struct hv_device *Device,
	void *				Buffer,
	u32				BufferLen,
	u32*				BufferActualLen,
	u64*				RequestId
	);

typedef int	(*VMBUS_CHANNEL_ESTABLISH_GPADL)(
	struct hv_device *Device,
	void *				Buffer,	/* from kmalloc() */
	u32				BufferLen,		/* page-size multiple */
	u32*				GpadlHandle
	);

typedef int	(*VMBUS_CHANNEL_TEARDOWN_GPADL)(
	struct hv_device *Device,
	u32				GpadlHandle
	);


typedef struct _PORT_INFO {
	u32		InterruptMask;
	u32		ReadIndex;
	u32		WriteIndex;
	u32		BytesAvailToRead;
	u32		BytesAvailToWrite;
} PORT_INFO;


typedef struct _DEVICE_INFO {
	u32		ChannelId;
	u32		ChannelState;
	GUID		ChannelType;
	GUID		ChannelInstance;

	u32						MonitorId;
	u32						ServerMonitorPending;
	u32						ServerMonitorLatency;
	u32						ServerMonitorConnectionId;
	u32						ClientMonitorPending;
	u32						ClientMonitorLatency;
	u32						ClientMonitorConnectionId;

	PORT_INFO	Inbound;
	PORT_INFO	Outbound;
} DEVICE_INFO;

typedef void (*VMBUS_GET_CHANNEL_INFO)(struct hv_device *Device, DEVICE_INFO* DeviceInfo);

typedef struct _VMBUS_CHANNEL_INTERFACE {
	VMBUS_CHANNEL_OPEN							Open;
	VMBUS_CHANNEL_CLOSE							Close;
	VMBUS_CHANNEL_SEND_PACKET					SendPacket;
	VMBUS_CHANNEL_SEND_PACKET_PAGEBUFFER		SendPacketPageBuffer;
	VMBUS_CHANNEL_SEND_PACKET_MULTIPAGEBUFFER	SendPacketMultiPageBuffer;
	VMBUS_CHANNEL_RECV_PACKET					RecvPacket;
	VMBUS_CHANNEL_RECV_PACKET_PAW				RecvPacketRaw;
	VMBUS_CHANNEL_ESTABLISH_GPADL				EstablishGpadl;
	VMBUS_CHANNEL_TEARDOWN_GPADL				TeardownGpadl;
	VMBUS_GET_CHANNEL_INFO						GetInfo;
} VMBUS_CHANNEL_INTERFACE;

typedef void (*VMBUS_GET_CHANNEL_INTERFACE)(VMBUS_CHANNEL_INTERFACE *Interface);

/* Base driver object */
struct hv_driver {
	const char*				name;
	GUID					deviceType; /* the device type supported by this driver */

	PFN_ON_DEVICEADD		OnDeviceAdd;
	PFN_ON_DEVICEREMOVE		OnDeviceRemove;
	PFN_ON_GETDEVICEIDS		OnGetDeviceIds; /* device ids supported by this driver */
	PFN_ON_CLEANUP			OnCleanup;

	VMBUS_CHANNEL_INTERFACE VmbusChannelInterface;
};


/* Base device object */
struct hv_device {
	struct hv_driver *Driver;		/* the driver for this device */
	char				name[64];
	GUID				deviceType; /* the device type id of this device */
	GUID				deviceInstance; /* the device instance id of this device */
	void*				context;
	void*				Extension;		/* Device extension; */
};


/* Vmbus driver object */
typedef struct _VMBUS_DRIVER_OBJECT {
	struct hv_driver Base; /* !! Must be the 1st field !! */

	/* Set by the caller */
	PFN_ON_CHILDDEVICE_CREATE	OnChildDeviceCreate;
	PFN_ON_CHILDDEVICE_DESTROY	OnChildDeviceDestroy;
	PFN_ON_CHILDDEVICE_ADD		OnChildDeviceAdd;
	PFN_ON_CHILDDEVICE_REMOVE	OnChildDeviceRemove;

	/* Set by the callee */
	/* PFN_ON_MATCH		OnMatch; */
	/* PFN_ON_PROBE		OnProbe; */
	PFN_ON_ISR				OnIsr;
	PFN_ON_DPC				OnMsgDpc;
	PFN_ON_DPC				OnEventDpc;
	PFN_GET_CHANNEL_OFFERS	GetChannelOffers;

	VMBUS_GET_CHANNEL_INTERFACE GetChannelInterface;
	VMBUS_GET_CHANNEL_INFO		GetChannelInfo;
} VMBUS_DRIVER_OBJECT;



/* Interface */

int
VmbusInitialize(
	struct hv_driver *drv
	);

#endif /* _VMBUS_API_H_ */
