/*
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
#include <linux/slab.h>
#include "osd.h"
#include "logging.h"
#include "version_info.h"
#include "vmbus_private.h"

static const char *gDriverName = "vmbus";

/*
 * Windows vmbus does not defined this.
 * We defined this to be consistent with other devices
 */
/* {c5295816-f63a-4d5f-8d1a-4daf999ca185} */
static const struct hv_guid gVmbusDeviceType = {
	.data = {
		0x16, 0x58, 0x29, 0xc5, 0x3a, 0xf6, 0x5f, 0x4d,
		0x8d, 0x1a, 0x4d, 0xaf, 0x99, 0x9c, 0xa1, 0x85
	}
};

/* {ac3760fc-9adf-40aa-9427-a70ed6de95c5} */
static const struct hv_guid gVmbusDeviceId = {
	.data = {
		0xfc, 0x60, 0x37, 0xac, 0xdf, 0x9a, 0xaa, 0x40,
		0x94, 0x27, 0xa7, 0x0e, 0xd6, 0xde, 0x95, 0xc5
	}
};

static struct hv_driver *gDriver; /* vmbus driver object */
static struct hv_device *gDevice; /* vmbus root device */

/*
 * VmbusGetChannelOffers - Retrieve the channel offers from the parent partition
 */
static void VmbusGetChannelOffers(void)
{
	vmbus_request_offers();
}

/*
 * VmbusChildDeviceAdd - Registers the child device with the vmbus
 */
int VmbusChildDeviceAdd(struct hv_device *ChildDevice)
{
	struct vmbus_driver *vmbusDriver = (struct vmbus_driver *)gDriver;

	return vmbusDriver->OnChildDeviceAdd(gDevice, ChildDevice);
}

/*
 * VmbusOnDeviceAdd - Callback when the root bus device is added
 */
static int VmbusOnDeviceAdd(struct hv_device *dev, void *AdditionalInfo)
{
	u32 *irqvector = AdditionalInfo;
	int ret;

	gDevice = dev;

	memcpy(&gDevice->deviceType, &gVmbusDeviceType, sizeof(struct hv_guid));
	memcpy(&gDevice->deviceInstance, &gVmbusDeviceId,
	       sizeof(struct hv_guid));

	/* strcpy(dev->name, "vmbus"); */
	/* SynIC setup... */
	on_each_cpu(hv_synic_init, (void *)irqvector, 1);

	/* Connect to VMBus in the root partition */
	ret = VmbusConnect();

	/* VmbusSendEvent(device->localPortId+1); */
	return ret;
}

/*
 * VmbusOnDeviceRemove - Callback when the root bus device is removed
 */
static int VmbusOnDeviceRemove(struct hv_device *dev)
{
	int ret = 0;

	vmbus_release_unattached_channels();
	VmbusDisconnect();
	on_each_cpu(hv_synic_cleanup, NULL, 1);
	return ret;
}

/*
 * VmbusOnCleanup - Perform any cleanup when the driver is removed
 */
static void VmbusOnCleanup(struct hv_driver *drv)
{
	/* struct vmbus_driver *driver = (struct vmbus_driver *)drv; */

	hv_cleanup();
}

/*
 * vmbus_on_msg_dpc - DPC routine to handle messages from the hypervisior
 */
void vmbus_on_msg_dpc(struct hv_driver *drv)
{
	int cpu = smp_processor_id();
	void *page_addr = hv_context.synic_message_page[cpu];
	struct hv_message *msg = (struct hv_message *)page_addr +
				  VMBUS_MESSAGE_SINT;
	struct hv_message *copied;

	while (1) {
		if (msg->header.message_type == HVMSG_NONE) {
			/* no msg */
			break;
		} else {
			copied = kmemdup(msg, sizeof(*copied), GFP_ATOMIC);
			if (copied == NULL)
				continue;

			osd_schedule_callback(gVmbusConnection.WorkQueue,
					      vmbus_onmessage,
					      (void *)copied);
		}

		msg->header.message_type = HVMSG_NONE;

		/*
		 * Make sure the write to MessageType (ie set to
		 * HVMSG_NONE) happens before we read the
		 * MessagePending and EOMing. Otherwise, the EOMing
		 * will not deliver any more messages since there is
		 * no empty slot
		 */
		mb();

		if (msg->header.message_flags.msg_pending) {
			/*
			 * This will cause message queue rescan to
			 * possibly deliver another msg from the
			 * hypervisor
			 */
			wrmsrl(HV_X64_MSR_EOM, 0);
		}
	}
}

/*
 * VmbusOnEventDPC - DPC routine to handle events from the hypervisior
 */
static void VmbusOnEventDPC(struct hv_driver *drv)
{
	/* TODO: Process any events */
	VmbusOnEvents();
}

/*
 * vmbus_on_isr - ISR routine
 */
int vmbus_on_isr(struct hv_driver *drv)
{
	int ret = 0;
	int cpu = smp_processor_id();
	void *page_addr;
	struct hv_message *msg;
	union hv_synic_event_flags *event;

	page_addr = hv_context.synic_message_page[cpu];
	msg = (struct hv_message *)page_addr + VMBUS_MESSAGE_SINT;

	/* Check if there are actual msgs to be process */
	if (msg->header.message_type != HVMSG_NONE) {
		DPRINT_DBG(VMBUS, "received msg type %d size %d",
				msg->header.message_type,
				msg->header.payload_size);
		ret |= 0x1;
	}

	/* TODO: Check if there are events to be process */
	page_addr = hv_context.synic_event_page[cpu];
	event = (union hv_synic_event_flags *)page_addr + VMBUS_MESSAGE_SINT;

	/* Since we are a child, we only need to check bit 0 */
	if (test_and_clear_bit(0, (unsigned long *) &event->flags32[0])) {
		DPRINT_DBG(VMBUS, "received event %d", event->flags32[0]);
		ret |= 0x2;
	}

	return ret;
}

/*
 * VmbusInitialize - Main entry point
 */
int VmbusInitialize(struct hv_driver *drv)
{
	struct vmbus_driver *driver = (struct vmbus_driver *)drv;
	int ret;

	DPRINT_INFO(VMBUS, "+++++++ HV Driver version = %s +++++++",
		    HV_DRV_VERSION);
	DPRINT_INFO(VMBUS, "+++++++ Vmbus supported version = %d +++++++",
			VMBUS_REVISION_NUMBER);
	DPRINT_INFO(VMBUS, "+++++++ Vmbus using SINT %d +++++++",
			VMBUS_MESSAGE_SINT);
	DPRINT_DBG(VMBUS, "sizeof(vmbus_channel_packet_page_buffer)=%zd, "
			"sizeof(VMBUS_CHANNEL_PACKET_MULITPAGE_BUFFER)=%zd",
			sizeof(struct vmbus_channel_packet_page_buffer),
			sizeof(struct vmbus_channel_packet_multipage_buffer));

	drv->name = gDriverName;
	memcpy(&drv->deviceType, &gVmbusDeviceType, sizeof(struct hv_guid));

	/* Setup dispatch table */
	driver->Base.OnDeviceAdd	= VmbusOnDeviceAdd;
	driver->Base.OnDeviceRemove	= VmbusOnDeviceRemove;
	driver->Base.OnCleanup		= VmbusOnCleanup;
	driver->OnEventDpc		= VmbusOnEventDPC;
	driver->GetChannelOffers	= VmbusGetChannelOffers;

	/* Hypervisor initialization...setup hypercall page..etc */
	ret = hv_init();
	if (ret != 0)
		DPRINT_ERR(VMBUS, "Unable to initialize the hypervisor - 0x%x",
				ret);
	gDriver = drv;

	return ret;
}
