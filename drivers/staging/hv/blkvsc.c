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

static const char *gBlkDriverName = "blkvsc";

/* {32412632-86cb-44a2-9b5c-50d1417354f5} */
static const struct hv_guid gBlkVscDeviceType = {
	.data = {
		0x32, 0x26, 0x41, 0x32, 0xcb, 0x86, 0xa2, 0x44,
		0x9b, 0x5c, 0x50, 0xd1, 0x41, 0x73, 0x54, 0xf5
	}
};

static int BlkVscOnDeviceAdd(struct hv_device *Device, void *AdditionalInfo)
{
	struct storvsc_device_info *deviceInfo;
	int ret = 0;

	deviceInfo = (struct storvsc_device_info *)AdditionalInfo;

	ret = StorVscOnDeviceAdd(Device, AdditionalInfo);
	if (ret != 0)
		return ret;

	/*
	 * We need to use the device instance guid to set the path and target
	 * id. For IDE devices, the device instance id is formatted as
	 * <bus id> * - <device id> - 8899 - 000000000000.
	 */
	deviceInfo->PathId = Device->deviceInstance.data[3] << 24 |
			     Device->deviceInstance.data[2] << 16 |
			     Device->deviceInstance.data[1] << 8  |
			     Device->deviceInstance.data[0];

	deviceInfo->TargetId = Device->deviceInstance.data[5] << 8 |
			       Device->deviceInstance.data[4];

	return ret;
}

int BlkVscInitialize(struct hv_driver *Driver)
{
	struct storvsc_driver_object *storDriver;
	int ret = 0;

	storDriver = (struct storvsc_driver_object *)Driver;

	/* Make sure we are at least 2 pages since 1 page is used for control */
	/* ASSERT(storDriver->RingBufferSize >= (PAGE_SIZE << 1)); */

	Driver->name = gBlkDriverName;
	memcpy(&Driver->deviceType, &gBlkVscDeviceType, sizeof(struct hv_guid));

	storDriver->RequestExtSize = sizeof(struct storvsc_request_extension);

	/*
	 * Divide the ring buffer data size (which is 1 page less than the ring
	 * buffer size since that page is reserved for the ring buffer indices)
	 * by the max request size (which is
	 * VMBUS_CHANNEL_PACKET_MULITPAGE_BUFFER + struct vstor_packet + u64)
	 */
	storDriver->MaxOutstandingRequestsPerChannel =
		((storDriver->RingBufferSize - PAGE_SIZE) /
		  ALIGN_UP(MAX_MULTIPAGE_BUFFER_PACKET +
			   sizeof(struct vstor_packet) + sizeof(u64),
			   sizeof(u64)));

	DPRINT_INFO(BLKVSC, "max io outstd %u",
		    storDriver->MaxOutstandingRequestsPerChannel);

	/* Setup the dispatch table */
	storDriver->Base.OnDeviceAdd = BlkVscOnDeviceAdd;
	storDriver->Base.OnDeviceRemove = StorVscOnDeviceRemove;
	storDriver->Base.OnCleanup = StorVscOnCleanup;
	storDriver->OnIORequest	= StorVscOnIORequest;

	return ret;
}
