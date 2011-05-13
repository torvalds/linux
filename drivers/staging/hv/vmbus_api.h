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

	struct device_driver driver;

	int (*probe)(struct hv_device *);
	int (*remove)(struct hv_device *);
	void (*shutdown)(struct hv_device *);

};

/* Base device object */
struct hv_device {
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
