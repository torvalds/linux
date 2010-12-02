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

/* Base driver object */
struct hv_driver {
	const char *name;

	/* the device type supported by this driver */
	struct hv_guid deviceType;

	int (*OnDeviceAdd)(struct hv_device *device, void *data);
	int (*OnDeviceRemove)(struct hv_device *device);
	void (*OnCleanup)(struct hv_driver *driver);
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

	struct vmbus_channel *channel;

	/* Device extension; */
	void *Extension;
};

/* Vmbus driver object */
struct vmbus_driver {
	/* !! Must be the 1st field !! */
	/* FIXME if ^, then someone is doing somthing stupid */
	struct hv_driver Base;

	/* Set by the caller */
	int (*OnChildDeviceAdd)(struct hv_device *RootDevice,
				struct hv_device *ChildDevice);

	/* Set by the callee */
	int (*OnIsr)(struct hv_driver *driver);
	void (*OnMsgDpc)(struct hv_driver *driver);
	void (*OnEventDpc)(struct hv_driver *driver);
	void (*GetChannelOffers)(void);
};

int VmbusInitialize(struct hv_driver *drv);

#endif /* _VMBUS_API_H_ */
