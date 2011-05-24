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

#include <linux/device.h>
#include <linux/workqueue.h>

#define MAX_PAGE_BUFFER_COUNT				16
#define MAX_MULTIPAGE_BUFFER_COUNT			32 /* 128K */

#pragma pack(push, 1)

/* Single-page buffer */
struct hv_page_buffer {
	u32 len;
	u32 offset;
	u64 pfn;
};

/* Multiple-page buffer */
struct hv_multipage_buffer {
	/* Length and Offset determines the # of pfns in the array */
	u32 len;
	u32 offset;
	u64 pfn_array[MAX_MULTIPAGE_BUFFER_COUNT];
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
	u32 int_mask;
	u32 read_idx;
	u32 write_idx;
	u32 bytes_avail_toread;
	u32 bytes_avail_towrite;
};

struct hv_device_info {
	u32 chn_id;
	u32 chn_state;
	struct hv_guid chn_type;
	struct hv_guid chn_instance;

	u32 monitor_id;
	u32 server_monitor_pending;
	u32 server_monitor_latency;
	u32 server_monitor_conn_id;
	u32 client_monitor_pending;
	u32 client_monitor_latency;
	u32 client_monitor_conn_id;

	struct hv_dev_port_info inbound;
	struct hv_dev_port_info outbound;
};

/* Base driver object */
struct hv_driver {
	const char *name;

	/* the device type supported by this driver */
	struct hv_guid dev_type;

	/*
	 * Device type specific drivers (net, blk etc.)
	 * need a mechanism to get a pointer to
	 * device type specific driver structure given
	 * a pointer to the base hyperv driver structure.
	 * The current code solves this problem using
	 * a hack. Support this need explicitly
	 */
	void *priv;

	struct device_driver driver;

	int (*dev_add)(struct hv_device *device, void *data);
	int (*dev_rm)(struct hv_device *device);
	void (*cleanup)(struct hv_driver *driver);
};

/* Base device object */
struct hv_device {
	/* the driver for this device */
	struct hv_driver *drv;

	char name[64];

	struct work_struct probe_failed_work_item;

	int probe_error;

	/* the device type id of this device */
	struct hv_guid dev_type;

	/* the device instance id of this device */
	struct hv_guid dev_instance;

	struct device device;

	struct vmbus_channel *channel;

	/* Device extension; */
	void *ext;
};

#endif /* _VMBUS_API_H_ */
