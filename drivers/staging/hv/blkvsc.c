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
#include <linux/kernel.h>
#include <linux/mm.h>
#include "osd.h"
#include "storvsc.c"

static const char *g_blk_driver_name = "blkvsc";

/* {32412632-86cb-44a2-9b5c-50d1417354f5} */
static const struct hv_guid g_blk_device_type = {
	.data = {
		0x32, 0x26, 0x41, 0x32, 0xcb, 0x86, 0xa2, 0x44,
		0x9b, 0x5c, 0x50, 0xd1, 0x41, 0x73, 0x54, 0xf5
	}
};

static int blk_vsc_on_device_add(struct hv_device *device, void *additional_info)
{
	struct storvsc_device_info *device_info;
	int ret = 0;

	device_info = (struct storvsc_device_info *)additional_info;

	ret = stor_vsc_on_device_add(device, additional_info);
	if (ret != 0)
		return ret;

	/*
	 * We need to use the device instance guid to set the path and target
	 * id. For IDE devices, the device instance id is formatted as
	 * <bus id> * - <device id> - 8899 - 000000000000.
	 */
	device_info->path_id = device->deviceInstance.data[3] << 24 |
			     device->deviceInstance.data[2] << 16 |
			     device->deviceInstance.data[1] << 8  |
			     device->deviceInstance.data[0];

	device_info->target_id = device->deviceInstance.data[5] << 8 |
			       device->deviceInstance.data[4];

	return ret;
}

int blk_vsc_initialize(struct hv_driver *driver)
{
	struct storvsc_driver_object *stor_driver;
	int ret = 0;

	stor_driver = (struct storvsc_driver_object *)driver;

	/* Make sure we are at least 2 pages since 1 page is used for control */
	/* ASSERT(stor_driver->RingBufferSize >= (PAGE_SIZE << 1)); */

	driver->name = g_blk_driver_name;
	memcpy(&driver->deviceType, &g_blk_device_type, sizeof(struct hv_guid));

	stor_driver->request_ext_size = sizeof(struct storvsc_request_extension);

	/*
	 * Divide the ring buffer data size (which is 1 page less than the ring
	 * buffer size since that page is reserved for the ring buffer indices)
	 * by the max request size (which is
	 * vmbus_channel_packet_multipage_buffer + struct vstor_packet + u64)
	 */
	stor_driver->max_outstanding_req_per_channel =
		((stor_driver->ring_buffer_size - PAGE_SIZE) /
		  ALIGN_UP(MAX_MULTIPAGE_BUFFER_PACKET +
			   sizeof(struct vstor_packet) + sizeof(u64),
			   sizeof(u64)));

	DPRINT_INFO(BLKVSC, "max io outstd %u",
		    stor_driver->max_outstanding_req_per_channel);

	/* Setup the dispatch table */
	stor_driver->base.OnDeviceAdd = blk_vsc_on_device_add;
	stor_driver->base.OnDeviceRemove = stor_vsc_on_device_remove;
	stor_driver->base.OnCleanup = stor_vsc_on_cleanup;
	stor_driver->on_io_request = stor_vsc_on_io_request;

	return ret;
}
