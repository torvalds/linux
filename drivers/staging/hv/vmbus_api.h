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

#define MAX_PAGE_BUFFER_COUNT				16
#define MAX_MULTIPAGE_BUFFER_COUNT			32 /* 128K */

#pragma pack(push, 1)

/* Single-page buffer */
struct hv_page_buffer {
	u32 Length;
	u32 Offset;
	u64 Pfn;
};

/* Multiple-page buffer */
struct hv_multipage_buffer {
	/* Length and Offset determines the # of pfns in the array */
	u32 Length;
	u32 Offset;
	u64 PfnArray[MAX_MULTIPAGE_BUFFER_COUNT];
};

/* 0x18 includes the proprietary packet header */
#define MAX_PAGE_BUFFER_PACKET		(0x18 +			\
					(sizeof(struct hv_page_buffer) * \
					 MAX_PAGE_BUFFER_COUNT))
#define MAX_MULTIPAGE_BUFFER_PACKET	(0x18 +			\
					 sizeof(struct hv_multipage_buffer))


#pragma pack(pop)

struct hv_driver;
struct hv_device;

struct hv_dev_port_info {
	u32 InterruptMask;
	u32 ReadIndex;
	u32 WriteIndex;
	u32 BytesAvailToRead;
	u32 BytesAvailToWrite;
};

struct hv_device_info {
	u32 ChannelId;
	u32 ChannelState;
	struct hv_guid ChannelType;
	struct hv_guid ChannelInstance;

	u32 MonitorId;
	u32 ServerMonitorPending;
	u32 ServerMonitorLatency;
	u32 ServerMonitorConnectionId;
	u32 ClientMonitorPending;
	u32 ClientMonitorLatency;
	u32 ClientMonitorConnectionId;

	struct hv_dev_port_info Inbound;
	struct hv_dev_port_info Outbound;
};

/**
 * struct vmbus_channel_interface - Contains member functions for vmbus channel
 * @Open:      Open the channel
 * @Close:     Close the channel
 * @SendPacket:        Send a packet over the channel
 * @SendPacketPageBuffer:      Send a single page buffer over the channel
 * @SendPacketMultiPageBuffer: Send a multiple page buffers
 * @RecvPacket:        Receive packet
 * @RecvPacketRaw:     Receive Raw packet
 * @EstablishGpadl:    Set up GPADL for ringbuffer
 * @TeardownGpadl:     Teardown GPADL for ringbuffer
 * @GetInfo:   Get info about the channel
 *
 * This structure contains function pointer to control vmbus channel
 * behavior. None of these functions is externally callable, but they
 * are used for normal vmbus channel internal behavior.
 * Only used by Hyper-V drivers.
 */
struct vmbus_channel_interface {
	int (*Open)(struct hv_device *Device, u32 SendBufferSize,
		    u32 RecvRingBufferSize, void *UserData, u32 UserDataLen,
		    void (*ChannelCallback)(void *context),
		    void *Context);
	void (*Close)(struct hv_device *device);
	int (*SendPacket)(struct hv_device *Device, const void *Buffer,
			  u32 BufferLen, u64 RequestId, u32 Type, u32 Flags);
	int (*SendPacketPageBuffer)(struct hv_device *dev,
				    struct hv_page_buffer PageBuffers[],
				    u32 PageCount, void *Buffer, u32 BufferLen,
				    u64 RequestId);
	int (*SendPacketMultiPageBuffer)(struct hv_device *device,
					 struct hv_multipage_buffer *mpb,
					 void *Buffer,
					 u32 BufferLen,
					 u64 RequestId);
	int (*RecvPacket)(struct hv_device *dev, void *buf, u32 buflen,
			  u32 *BufferActualLen, u64 *RequestId);
	int (*RecvPacketRaw)(struct hv_device *dev, void *buf, u32 buflen,
			     u32 *BufferActualLen, u64 *RequestId);
	int (*EstablishGpadl)(struct hv_device *dev, void *buf, u32 buflen,
			      u32 *GpadlHandle);
	int (*TeardownGpadl)(struct hv_device *device, u32 GpadlHandle);
	void (*GetInfo)(struct hv_device *dev, struct hv_device_info *devinfo);
};

/* Base driver object */
struct hv_driver {
	const char *name;

	/* the device type supported by this driver */
	struct hv_guid deviceType;

	int (*OnDeviceAdd)(struct hv_device *device, void *data);
	int (*OnDeviceRemove)(struct hv_device *device);
	void (*OnCleanup)(struct hv_driver *driver);

	struct vmbus_channel_interface VmbusChannelInterface;
};

/* Base device object */
struct hv_device {
	/* the driver for this device */
	struct hv_driver *Driver;

	char name[64];

	/* the device type id of this device */
	struct hv_guid deviceType;

	/* the device instance id of this device */
	struct hv_guid deviceInstance;

	void *context;

	/* Device extension; */
	void *Extension;
};

/* Vmbus driver object */
struct vmbus_driver {
	/* !! Must be the 1st field !! */
	/* FIXME if ^, then someone is doing somthing stupid */
	struct hv_driver Base;

	/* Set by the caller */
	struct hv_device * (*OnChildDeviceCreate)(struct hv_guid *DeviceType,
						struct hv_guid *DeviceInstance,
						void *Context);
	void (*OnChildDeviceDestroy)(struct hv_device *device);
	int (*OnChildDeviceAdd)(struct hv_device *RootDevice,
				struct hv_device *ChildDevice);
	void (*OnChildDeviceRemove)(struct hv_device *device);

	/* Set by the callee */
	int (*OnIsr)(struct hv_driver *driver);
	void (*OnMsgDpc)(struct hv_driver *driver);
	void (*OnEventDpc)(struct hv_driver *driver);
	void (*GetChannelOffers)(void);

	void (*GetChannelInterface)(struct vmbus_channel_interface *i);
	void (*GetChannelInfo)(struct hv_device *dev,
			       struct hv_device_info *devinfo);
};

int VmbusInitialize(struct hv_driver *drv);

#endif /* _VMBUS_API_H_ */
