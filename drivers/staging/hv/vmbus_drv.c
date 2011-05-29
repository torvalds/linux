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
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/sysctl.h>
#include <linux/pci.h>
#include <linux/dmi.h>
#include <linux/slab.h>
#include <linux/completion.h>
#include "version_info.h"
#include "hv_api.h"
#include "logging.h"
#include "vmbus.h"
#include "channel.h"
#include "vmbus_private.h"


/* FIXME! We need to do this dynamically for PIC and APIC system */
#define VMBUS_IRQ		0x5
#define VMBUS_IRQ_VECTOR	IRQ5_VECTOR

/* Main vmbus driver data structure */
struct vmbus_driver_context {

	struct bus_type bus;
	struct tasklet_struct msg_dpc;
	struct tasklet_struct event_dpc;

	/* The bus root device */
	struct hv_device device_ctx;
};

static int vmbus_match(struct device *device, struct device_driver *driver);
static int vmbus_probe(struct device *device);
static int vmbus_remove(struct device *device);
static void vmbus_shutdown(struct device *device);
static int vmbus_uevent(struct device *device, struct kobj_uevent_env *env);

static irqreturn_t vmbus_isr(int irq, void *dev_id);

static void vmbus_device_release(struct device *device);
static void vmbus_bus_release(struct device *device);

static ssize_t vmbus_show_device_attr(struct device *dev,
				      struct device_attribute *dev_attr,
				      char *buf);


unsigned int vmbus_loglevel = (ALL_MODULES << 16 | INFO_LVL);
EXPORT_SYMBOL(vmbus_loglevel);
	/* (ALL_MODULES << 16 | DEBUG_LVL_ENTEREXIT); */
	/* (((VMBUS | VMBUS_DRV)<<16) | DEBUG_LVL_ENTEREXIT); */

static int vmbus_irq = VMBUS_IRQ;

/* Set up per device attributes in /sys/bus/vmbus/devices/<bus device> */
static struct device_attribute vmbus_device_attrs[] = {
	__ATTR(id, S_IRUGO, vmbus_show_device_attr, NULL),
	__ATTR(state, S_IRUGO, vmbus_show_device_attr, NULL),
	__ATTR(class_id, S_IRUGO, vmbus_show_device_attr, NULL),
	__ATTR(device_id, S_IRUGO, vmbus_show_device_attr, NULL),
	__ATTR(monitor_id, S_IRUGO, vmbus_show_device_attr, NULL),

	__ATTR(server_monitor_pending, S_IRUGO, vmbus_show_device_attr, NULL),
	__ATTR(server_monitor_latency, S_IRUGO, vmbus_show_device_attr, NULL),
	__ATTR(server_monitor_conn_id, S_IRUGO, vmbus_show_device_attr, NULL),

	__ATTR(client_monitor_pending, S_IRUGO, vmbus_show_device_attr, NULL),
	__ATTR(client_monitor_latency, S_IRUGO, vmbus_show_device_attr, NULL),
	__ATTR(client_monitor_conn_id, S_IRUGO, vmbus_show_device_attr, NULL),

	__ATTR(out_intr_mask, S_IRUGO, vmbus_show_device_attr, NULL),
	__ATTR(out_read_index, S_IRUGO, vmbus_show_device_attr, NULL),
	__ATTR(out_write_index, S_IRUGO, vmbus_show_device_attr, NULL),
	__ATTR(out_read_bytes_avail, S_IRUGO, vmbus_show_device_attr, NULL),
	__ATTR(out_write_bytes_avail, S_IRUGO, vmbus_show_device_attr, NULL),

	__ATTR(in_intr_mask, S_IRUGO, vmbus_show_device_attr, NULL),
	__ATTR(in_read_index, S_IRUGO, vmbus_show_device_attr, NULL),
	__ATTR(in_write_index, S_IRUGO, vmbus_show_device_attr, NULL),
	__ATTR(in_read_bytes_avail, S_IRUGO, vmbus_show_device_attr, NULL),
	__ATTR(in_write_bytes_avail, S_IRUGO, vmbus_show_device_attr, NULL),
	__ATTR_NULL
};

/* The one and only one */
static struct vmbus_driver_context vmbus_drv = {
	.bus.name =		"vmbus",
	.bus.match =		vmbus_match,
	.bus.shutdown =		vmbus_shutdown,
	.bus.remove =		vmbus_remove,
	.bus.probe =		vmbus_probe,
	.bus.uevent =		vmbus_uevent,
	.bus.dev_attrs =	vmbus_device_attrs,
};

static const char *driver_name = "hyperv";

/*
 * Windows vmbus does not defined this.
 * We defined this to be consistent with other devices
 */
/* {c5295816-f63a-4d5f-8d1a-4daf999ca185} */
static const struct hv_guid device_type = {
	.data = {
		0x16, 0x58, 0x29, 0xc5, 0x3a, 0xf6, 0x5f, 0x4d,
		0x8d, 0x1a, 0x4d, 0xaf, 0x99, 0x9c, 0xa1, 0x85
	}
};

/* {ac3760fc-9adf-40aa-9427-a70ed6de95c5} */
static const struct hv_guid device_id = {
	.data = {
		0xfc, 0x60, 0x37, 0xac, 0xdf, 0x9a, 0xaa, 0x40,
		0x94, 0x27, 0xa7, 0x0e, 0xd6, 0xde, 0x95, 0xc5
	}
};

static struct hv_device *vmbus_device; /* vmbus root device */


/*
 * vmbus_dev_add - Callback when the root bus device is added
 */
static int vmbus_dev_add(struct hv_device *dev, void *info)
{
	u32 *irqvector = info;
	int ret;

	vmbus_device = dev;

	memcpy(&vmbus_device->dev_type, &device_type, sizeof(struct hv_guid));
	memcpy(&vmbus_device->dev_instance, &device_id,
	       sizeof(struct hv_guid));

	/* strcpy(dev->name, "vmbus"); */
	/* SynIC setup... */
	on_each_cpu(hv_synic_init, (void *)irqvector, 1);

	/* Connect to VMBus in the root partition */
	ret = vmbus_connect();

	/* VmbusSendEvent(device->localPortId+1); */
	return ret;
}


struct onmessage_work_context {
	struct work_struct work;
	struct hv_message msg;
};

static void vmbus_onmessage_work(struct work_struct *work)
{
	struct onmessage_work_context *ctx;

	ctx = container_of(work, struct onmessage_work_context,
			   work);
	vmbus_onmessage(&ctx->msg);
	kfree(ctx);
}

/*
 * vmbus_on_msg_dpc - DPC routine to handle messages from the hypervisior
 */
static void vmbus_on_msg_dpc(unsigned long data)
{
	int cpu = smp_processor_id();
	void *page_addr = hv_context.synic_message_page[cpu];
	struct hv_message *msg = (struct hv_message *)page_addr +
				  VMBUS_MESSAGE_SINT;
	struct onmessage_work_context *ctx;

	while (1) {
		if (msg->header.message_type == HVMSG_NONE) {
			/* no msg */
			break;
		} else {
			ctx = kmalloc(sizeof(*ctx), GFP_ATOMIC);
			if (ctx == NULL)
				continue;
			INIT_WORK(&ctx->work, vmbus_onmessage_work);
			memcpy(&ctx->msg, msg, sizeof(*msg));
			queue_work(vmbus_connection.work_queue, &ctx->work);
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
 * vmbus_on_isr - ISR routine
 */
static int vmbus_on_isr(void)
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
	if (sync_test_and_clear_bit(0, (unsigned long *) &event->flags32[0])) {
		DPRINT_DBG(VMBUS, "received event %d", event->flags32[0]);
		ret |= 0x2;
	}

	return ret;
}

static void get_channel_info(struct hv_device *device,
			     struct hv_device_info *info)
{
	struct vmbus_channel_debug_info debug_info;

	if (!device->channel)
		return;

	vmbus_get_debug_info(device->channel, &debug_info);

	info->chn_id = debug_info.relid;
	info->chn_state = debug_info.state;
	memcpy(&info->chn_type, &debug_info.interfacetype,
	       sizeof(struct hv_guid));
	memcpy(&info->chn_instance, &debug_info.interface_instance,
	       sizeof(struct hv_guid));

	info->monitor_id = debug_info.monitorid;

	info->server_monitor_pending = debug_info.servermonitor_pending;
	info->server_monitor_latency = debug_info.servermonitor_latency;
	info->server_monitor_conn_id = debug_info.servermonitor_connectionid;

	info->client_monitor_pending = debug_info.clientmonitor_pending;
	info->client_monitor_latency = debug_info.clientmonitor_latency;
	info->client_monitor_conn_id = debug_info.clientmonitor_connectionid;

	info->inbound.int_mask = debug_info.inbound.current_interrupt_mask;
	info->inbound.read_idx = debug_info.inbound.current_read_index;
	info->inbound.write_idx = debug_info.inbound.current_write_index;
	info->inbound.bytes_avail_toread =
		debug_info.inbound.bytes_avail_toread;
	info->inbound.bytes_avail_towrite =
		debug_info.inbound.bytes_avail_towrite;

	info->outbound.int_mask =
		debug_info.outbound.current_interrupt_mask;
	info->outbound.read_idx = debug_info.outbound.current_read_index;
	info->outbound.write_idx = debug_info.outbound.current_write_index;
	info->outbound.bytes_avail_toread =
		debug_info.outbound.bytes_avail_toread;
	info->outbound.bytes_avail_towrite =
		debug_info.outbound.bytes_avail_towrite;
}

/*
 * vmbus_show_device_attr - Show the device attribute in sysfs.
 *
 * This is invoked when user does a
 * "cat /sys/bus/vmbus/devices/<busdevice>/<attr name>"
 */
static ssize_t vmbus_show_device_attr(struct device *dev,
				      struct device_attribute *dev_attr,
				      char *buf)
{
	struct hv_device *device_ctx = device_to_hv_device(dev);
	struct hv_device_info device_info;

	memset(&device_info, 0, sizeof(struct hv_device_info));

	get_channel_info(device_ctx, &device_info);

	if (!strcmp(dev_attr->attr.name, "class_id")) {
		return sprintf(buf, "{%02x%02x%02x%02x-%02x%02x-%02x%02x-"
			       "%02x%02x%02x%02x%02x%02x%02x%02x}\n",
			       device_info.chn_type.data[3],
			       device_info.chn_type.data[2],
			       device_info.chn_type.data[1],
			       device_info.chn_type.data[0],
			       device_info.chn_type.data[5],
			       device_info.chn_type.data[4],
			       device_info.chn_type.data[7],
			       device_info.chn_type.data[6],
			       device_info.chn_type.data[8],
			       device_info.chn_type.data[9],
			       device_info.chn_type.data[10],
			       device_info.chn_type.data[11],
			       device_info.chn_type.data[12],
			       device_info.chn_type.data[13],
			       device_info.chn_type.data[14],
			       device_info.chn_type.data[15]);
	} else if (!strcmp(dev_attr->attr.name, "device_id")) {
		return sprintf(buf, "{%02x%02x%02x%02x-%02x%02x-%02x%02x-"
			       "%02x%02x%02x%02x%02x%02x%02x%02x}\n",
			       device_info.chn_instance.data[3],
			       device_info.chn_instance.data[2],
			       device_info.chn_instance.data[1],
			       device_info.chn_instance.data[0],
			       device_info.chn_instance.data[5],
			       device_info.chn_instance.data[4],
			       device_info.chn_instance.data[7],
			       device_info.chn_instance.data[6],
			       device_info.chn_instance.data[8],
			       device_info.chn_instance.data[9],
			       device_info.chn_instance.data[10],
			       device_info.chn_instance.data[11],
			       device_info.chn_instance.data[12],
			       device_info.chn_instance.data[13],
			       device_info.chn_instance.data[14],
			       device_info.chn_instance.data[15]);
	} else if (!strcmp(dev_attr->attr.name, "state")) {
		return sprintf(buf, "%d\n", device_info.chn_state);
	} else if (!strcmp(dev_attr->attr.name, "id")) {
		return sprintf(buf, "%d\n", device_info.chn_id);
	} else if (!strcmp(dev_attr->attr.name, "out_intr_mask")) {
		return sprintf(buf, "%d\n", device_info.outbound.int_mask);
	} else if (!strcmp(dev_attr->attr.name, "out_read_index")) {
		return sprintf(buf, "%d\n", device_info.outbound.read_idx);
	} else if (!strcmp(dev_attr->attr.name, "out_write_index")) {
		return sprintf(buf, "%d\n", device_info.outbound.write_idx);
	} else if (!strcmp(dev_attr->attr.name, "out_read_bytes_avail")) {
		return sprintf(buf, "%d\n",
			       device_info.outbound.bytes_avail_toread);
	} else if (!strcmp(dev_attr->attr.name, "out_write_bytes_avail")) {
		return sprintf(buf, "%d\n",
			       device_info.outbound.bytes_avail_towrite);
	} else if (!strcmp(dev_attr->attr.name, "in_intr_mask")) {
		return sprintf(buf, "%d\n", device_info.inbound.int_mask);
	} else if (!strcmp(dev_attr->attr.name, "in_read_index")) {
		return sprintf(buf, "%d\n", device_info.inbound.read_idx);
	} else if (!strcmp(dev_attr->attr.name, "in_write_index")) {
		return sprintf(buf, "%d\n", device_info.inbound.write_idx);
	} else if (!strcmp(dev_attr->attr.name, "in_read_bytes_avail")) {
		return sprintf(buf, "%d\n",
			       device_info.inbound.bytes_avail_toread);
	} else if (!strcmp(dev_attr->attr.name, "in_write_bytes_avail")) {
		return sprintf(buf, "%d\n",
			       device_info.inbound.bytes_avail_towrite);
	} else if (!strcmp(dev_attr->attr.name, "monitor_id")) {
		return sprintf(buf, "%d\n", device_info.monitor_id);
	} else if (!strcmp(dev_attr->attr.name, "server_monitor_pending")) {
		return sprintf(buf, "%d\n", device_info.server_monitor_pending);
	} else if (!strcmp(dev_attr->attr.name, "server_monitor_latency")) {
		return sprintf(buf, "%d\n", device_info.server_monitor_latency);
	} else if (!strcmp(dev_attr->attr.name, "server_monitor_conn_id")) {
		return sprintf(buf, "%d\n",
			       device_info.server_monitor_conn_id);
	} else if (!strcmp(dev_attr->attr.name, "client_monitor_pending")) {
		return sprintf(buf, "%d\n", device_info.client_monitor_pending);
	} else if (!strcmp(dev_attr->attr.name, "client_monitor_latency")) {
		return sprintf(buf, "%d\n", device_info.client_monitor_latency);
	} else if (!strcmp(dev_attr->attr.name, "client_monitor_conn_id")) {
		return sprintf(buf, "%d\n",
			       device_info.client_monitor_conn_id);
	} else {
		return 0;
	}
}

/*
 * vmbus_bus_init -Main vmbus driver initialization routine.
 *
 * Here, we
 *	- initialize the vmbus driver context
 *	- setup various driver entry points
 *	- invoke the vmbus hv main init routine
 *	- get the irq resource
 *	- invoke the vmbus to add the vmbus root device
 *	- setup the vmbus root device
 *	- retrieve the channel offers
 */
static int vmbus_bus_init(void)
{
	struct vmbus_driver_context *vmbus_drv_ctx = &vmbus_drv;
	struct hv_device *dev_ctx = &vmbus_drv.device_ctx;
	int ret;
	unsigned int vector;

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


	/* Hypervisor initialization...setup hypercall page..etc */
	ret = hv_init();
	if (ret != 0) {
		DPRINT_ERR(VMBUS, "Unable to initialize the hypervisor - 0x%x",
				ret);
		goto cleanup;
	}


	vmbus_drv_ctx->bus.name = driver_name;

	/* Initialize the bus context */
	tasklet_init(&vmbus_drv_ctx->msg_dpc, vmbus_on_msg_dpc,
		     (unsigned long)NULL);
	tasklet_init(&vmbus_drv_ctx->event_dpc, vmbus_on_event,
		     (unsigned long)NULL);

	/* Now, register the bus  with LDM */
	ret = bus_register(&vmbus_drv_ctx->bus);
	if (ret) {
		ret = -1;
		goto cleanup;
	}

	/* Get the interrupt resource */
	ret = request_irq(vmbus_irq, vmbus_isr, IRQF_SAMPLE_RANDOM,
			  driver_name, NULL);

	if (ret != 0) {
		DPRINT_ERR(VMBUS_DRV, "ERROR - Unable to request IRQ %d",
			   vmbus_irq);

		bus_unregister(&vmbus_drv_ctx->bus);

		ret = -1;
		goto cleanup;
	}
	vector = VMBUS_IRQ_VECTOR;

	DPRINT_INFO(VMBUS_DRV, "irq 0x%x vector 0x%x", vmbus_irq, vector);

	/* Add the root device */
	memset(dev_ctx, 0, sizeof(struct hv_device));

	ret = vmbus_dev_add(dev_ctx, &vector);
	if (ret != 0) {
		DPRINT_ERR(VMBUS_DRV,
			   "ERROR - Unable to add vmbus root device");

		free_irq(vmbus_irq, NULL);

		bus_unregister(&vmbus_drv_ctx->bus);

		ret = -1;
		goto cleanup;
	}
	/* strcpy(dev_ctx->device.bus_id, dev_ctx->device_obj.name); */
	dev_set_name(&dev_ctx->device, "vmbus_0_0");

	/* No need to bind a driver to the root device. */
	dev_ctx->device.parent = NULL;
	/* NULL; vmbus_remove() does not get invoked */
	dev_ctx->device.bus = &vmbus_drv_ctx->bus;

	/* Setup the device dispatch table */
	dev_ctx->device.release = vmbus_bus_release;

	/* register the  root device */
	ret = device_register(&dev_ctx->device);
	if (ret) {
		DPRINT_ERR(VMBUS_DRV,
			   "ERROR - Unable to register vmbus root device");

		free_irq(vmbus_irq, NULL);
		bus_unregister(&vmbus_drv_ctx->bus);

		ret = -1;
		goto cleanup;
	}

	vmbus_request_offers();
	wait_for_completion(&hv_channel_ready);

cleanup:
	return ret;
}

/*
 * vmbus_bus_exit - Terminate the vmbus driver.
 *
 * This routine is opposite of vmbus_bus_init()
 */
static void vmbus_bus_exit(void)
{
	struct vmbus_driver_context *vmbus_drv_ctx = &vmbus_drv;

	struct hv_device *dev_ctx = &vmbus_drv.device_ctx;

	vmbus_release_unattached_channels();
	vmbus_disconnect();
	on_each_cpu(hv_synic_cleanup, NULL, 1);

	hv_cleanup();

	/* Unregister the root bus device */
	device_unregister(&dev_ctx->device);

	bus_unregister(&vmbus_drv_ctx->bus);

	free_irq(vmbus_irq, NULL);

	tasklet_kill(&vmbus_drv_ctx->msg_dpc);
	tasklet_kill(&vmbus_drv_ctx->event_dpc);
}


/**
 * vmbus_child_driver_register() - Register a vmbus's child driver
 * @drv:        Pointer to driver structure you want to register
 *
 *
 * Registers the given driver with Linux through the 'driver_register()' call
 * And sets up the hyper-v vmbus handling for this driver.
 * It will return the state of the 'driver_register()' call.
 *
 * Mainly used by Hyper-V drivers.
 */
int vmbus_child_driver_register(struct device_driver *drv)
{
	int ret;

	DPRINT_INFO(VMBUS_DRV, "child driver (%p) registering - name %s",
		    drv, drv->name);

	/* The child driver on this vmbus */
	drv->bus = &vmbus_drv.bus;

	ret = driver_register(drv);

	vmbus_request_offers();

	return ret;
}
EXPORT_SYMBOL(vmbus_child_driver_register);

/**
 * vmbus_child_driver_unregister() - Unregister a vmbus's child driver
 * @drv:        Pointer to driver structure you want to un-register
 *
 *
 * Un-register the given driver with Linux through the 'driver_unregister()'
 * call. And ungegisters the driver from the Hyper-V vmbus handler.
 *
 * Mainly used by Hyper-V drivers.
 */
void vmbus_child_driver_unregister(struct device_driver *drv)
{
	DPRINT_INFO(VMBUS_DRV, "child driver (%p) unregistering - name %s",
		    drv, drv->name);

	driver_unregister(drv);

	drv->bus = NULL;
}
EXPORT_SYMBOL(vmbus_child_driver_unregister);

/*
 * vmbus_child_device_create - Creates and registers a new child device
 * on the vmbus.
 */
struct hv_device *vmbus_child_device_create(struct hv_guid *type,
					    struct hv_guid *instance,
					    struct vmbus_channel *channel)
{
	struct hv_device *child_device_obj;

	/* Allocate the new child device */
	child_device_obj = kzalloc(sizeof(struct hv_device), GFP_KERNEL);
	if (!child_device_obj) {
		DPRINT_ERR(VMBUS_DRV,
			"unable to allocate device_context for child device");
		return NULL;
	}

	DPRINT_DBG(VMBUS_DRV, "child device (%p) allocated - "
		"type {%02x%02x%02x%02x-%02x%02x-%02x%02x-"
		"%02x%02x%02x%02x%02x%02x%02x%02x},"
		"id {%02x%02x%02x%02x-%02x%02x-%02x%02x-"
		"%02x%02x%02x%02x%02x%02x%02x%02x}",
		&child_device_obj->device,
		type->data[3], type->data[2], type->data[1], type->data[0],
		type->data[5], type->data[4], type->data[7], type->data[6],
		type->data[8], type->data[9], type->data[10], type->data[11],
		type->data[12], type->data[13], type->data[14], type->data[15],
		instance->data[3], instance->data[2],
		instance->data[1], instance->data[0],
		instance->data[5], instance->data[4],
		instance->data[7], instance->data[6],
		instance->data[8], instance->data[9],
		instance->data[10], instance->data[11],
		instance->data[12], instance->data[13],
		instance->data[14], instance->data[15]);

	child_device_obj->channel = channel;
	memcpy(&child_device_obj->dev_type, type, sizeof(struct hv_guid));
	memcpy(&child_device_obj->dev_instance, instance,
	       sizeof(struct hv_guid));


	return child_device_obj;
}

/*
 * vmbus_child_device_register - Register the child device
 */
int vmbus_child_device_register(struct hv_device *child_device_obj)
{
	int ret = 0;

	static atomic_t device_num = ATOMIC_INIT(0);

	DPRINT_DBG(VMBUS_DRV, "child device (%p) registering",
		   child_device_obj);

	/* Set the device name. Otherwise, device_register() will fail. */
	dev_set_name(&child_device_obj->device, "vmbus_0_%d",
		     atomic_inc_return(&device_num));

	/* The new device belongs to this bus */
	child_device_obj->device.bus = &vmbus_drv.bus; /* device->dev.bus; */
	child_device_obj->device.parent = &vmbus_device->device;
	child_device_obj->device.release = vmbus_device_release;

	/*
	 * Register with the LDM. This will kick off the driver/device
	 * binding...which will eventually call vmbus_match() and vmbus_probe()
	 */
	ret = device_register(&child_device_obj->device);

	/* vmbus_probe() error does not get propergate to device_register(). */
	ret = child_device_obj->probe_error;

	if (ret)
		DPRINT_ERR(VMBUS_DRV, "unable to register child device (%p)",
			   &child_device_obj->device);
	else
		DPRINT_INFO(VMBUS_DRV, "child device (%p) registered",
			    &child_device_obj->device);

	return ret;
}

/*
 * vmbus_child_device_unregister - Remove the specified child device
 * from the vmbus.
 */
void vmbus_child_device_unregister(struct hv_device *device_obj)
{

	DPRINT_INFO(VMBUS_DRV, "unregistering child device (%p)",
		    &device_obj->device);

	/*
	 * Kick off the process of unregistering the device.
	 * This will call vmbus_remove() and eventually vmbus_device_release()
	 */
	device_unregister(&device_obj->device);

	DPRINT_INFO(VMBUS_DRV, "child device (%p) unregistered",
		    &device_obj->device);
}

/*
 * vmbus_uevent - add uevent for our device
 *
 * This routine is invoked when a device is added or removed on the vmbus to
 * generate a uevent to udev in the userspace. The udev will then look at its
 * rule and the uevent generated here to load the appropriate driver
 */
static int vmbus_uevent(struct device *device, struct kobj_uevent_env *env)
{
	struct hv_device *dev = device_to_hv_device(device);
	int ret;

	DPRINT_INFO(VMBUS_DRV, "generating uevent - VMBUS_DEVICE_CLASS_GUID={"
		    "%02x%02x%02x%02x-%02x%02x-%02x%02x-"
		    "%02x%02x%02x%02x%02x%02x%02x%02x}",
		    dev->dev_type.data[3], dev->dev_type.data[2],
		    dev->dev_type.data[1], dev->dev_type.data[0],
		    dev->dev_type.data[5], dev->dev_type.data[4],
		    dev->dev_type.data[7], dev->dev_type.data[6],
		    dev->dev_type.data[8], dev->dev_type.data[9],
		    dev->dev_type.data[10],
		    dev->dev_type.data[11],
		    dev->dev_type.data[12],
		    dev->dev_type.data[13],
		    dev->dev_type.data[14],
		    dev->dev_type.data[15]);

	ret = add_uevent_var(env, "VMBUS_DEVICE_CLASS_GUID={"
			     "%02x%02x%02x%02x-%02x%02x-%02x%02x-"
			     "%02x%02x%02x%02x%02x%02x%02x%02x}",
			     dev->dev_type.data[3],
			     dev->dev_type.data[2],
			     dev->dev_type.data[1],
			     dev->dev_type.data[0],
			     dev->dev_type.data[5],
			     dev->dev_type.data[4],
			     dev->dev_type.data[7],
			     dev->dev_type.data[6],
			     dev->dev_type.data[8],
			     dev->dev_type.data[9],
			     dev->dev_type.data[10],
			     dev->dev_type.data[11],
			     dev->dev_type.data[12],
			     dev->dev_type.data[13],
			     dev->dev_type.data[14],
			     dev->dev_type.data[15]);

	if (ret)
		return ret;

	ret = add_uevent_var(env, "VMBUS_DEVICE_DEVICE_GUID={"
			     "%02x%02x%02x%02x-%02x%02x-%02x%02x-"
			     "%02x%02x%02x%02x%02x%02x%02x%02x}",
			     dev->dev_instance.data[3],
			     dev->dev_instance.data[2],
			     dev->dev_instance.data[1],
			     dev->dev_instance.data[0],
			     dev->dev_instance.data[5],
			     dev->dev_instance.data[4],
			     dev->dev_instance.data[7],
			     dev->dev_instance.data[6],
			     dev->dev_instance.data[8],
			     dev->dev_instance.data[9],
			     dev->dev_instance.data[10],
			     dev->dev_instance.data[11],
			     dev->dev_instance.data[12],
			     dev->dev_instance.data[13],
			     dev->dev_instance.data[14],
			     dev->dev_instance.data[15]);
	if (ret)
		return ret;

	return 0;
}

/*
 * vmbus_match - Attempt to match the specified device to the specified driver
 */
static int vmbus_match(struct device *device, struct device_driver *driver)
{
	int match = 0;
	struct hv_driver *drv = drv_to_hv_drv(driver);
	struct hv_device *device_ctx = device_to_hv_device(device);

	/* We found our driver ? */
	if (memcmp(&device_ctx->dev_type, &drv->dev_type,
		   sizeof(struct hv_guid)) == 0) {

		device_ctx->drv = drv->priv;
		DPRINT_INFO(VMBUS_DRV,
			    "device object (%p) set to driver object (%p)",
			    &device_ctx,
			    device_ctx->drv);

		match = 1;
	}
	return match;
}

/*
 * vmbus_probe_failed_cb - Callback when a driver probe failed in vmbus_probe()
 *
 * We need a callback because we cannot invoked device_unregister() inside
 * vmbus_probe() since vmbus_probe() may be invoked inside device_register()
 * i.e. we cannot call device_unregister() inside device_register()
 */
static void vmbus_probe_failed_cb(struct work_struct *context)
{
	struct hv_device *device_ctx = (struct hv_device *)context;

	/*
	 * Kick off the process of unregistering the device.
	 * This will call vmbus_remove() and eventually vmbus_device_release()
	 */
	device_unregister(&device_ctx->device);

	/* put_device(&device_ctx->device); */
}

/*
 * vmbus_probe - Add the new vmbus's child device
 */
static int vmbus_probe(struct device *child_device)
{
	int ret = 0;
	struct hv_driver *drv =
			drv_to_hv_drv(child_device->driver);
	struct hv_device *dev = device_to_hv_device(child_device);

	/* Let the specific open-source driver handles the probe if it can */
	if (drv->driver.probe) {
		ret = dev->probe_error =
		drv->driver.probe(child_device);
		if (ret != 0) {
			DPRINT_ERR(VMBUS_DRV, "probe() failed for device %s "
				   "(%p) on driver %s (%d)...",
				   dev_name(child_device), child_device,
				   child_device->driver->name, ret);

			INIT_WORK(&dev->probe_failed_work_item,
				  vmbus_probe_failed_cb);
			schedule_work(&dev->probe_failed_work_item);
		}
	} else {
		DPRINT_ERR(VMBUS_DRV, "probe() method not set for driver - %s",
			   child_device->driver->name);
		ret = -1;
	}
	return ret;
}

/*
 * vmbus_remove - Remove a vmbus device
 */
static int vmbus_remove(struct device *child_device)
{
	int ret;
	struct hv_driver *drv;

	/* Special case root bus device */
	if (child_device->parent == NULL) {
		/*
		 * No-op since it is statically defined and handle in
		 * vmbus_bus_exit()
		 */
		return 0;
	}

	if (child_device->driver) {
		drv = drv_to_hv_drv(child_device->driver);

		/*
		 * Let the specific open-source driver handles the removal if
		 * it can
		 */
		if (drv->driver.remove) {
			ret = drv->driver.remove(child_device);
		} else {
			DPRINT_ERR(VMBUS_DRV,
				   "remove() method not set for driver - %s",
				   child_device->driver->name);
			ret = -1;
		}
	}

	return 0;
}

/*
 * vmbus_shutdown - Shutdown a vmbus device
 */
static void vmbus_shutdown(struct device *child_device)
{
	struct hv_driver *drv;

	/* Special case root bus device */
	if (child_device->parent == NULL) {
		/*
		 * No-op since it is statically defined and handle in
		 * vmbus_bus_exit()
		 */
		return;
	}

	/* The device may not be attached yet */
	if (!child_device->driver)
		return;

	drv = drv_to_hv_drv(child_device->driver);

	/* Let the specific open-source driver handles the removal if it can */
	if (drv->driver.shutdown)
		drv->driver.shutdown(child_device);

	return;
}

/*
 * vmbus_bus_release - Final callback release of the vmbus root device
 */
static void vmbus_bus_release(struct device *device)
{
	/* FIXME */
	/* Empty release functions are a bug, or a major sign
	 * of a problem design, this MUST BE FIXED! */
	dev_err(device, "%s needs to be fixed!\n", __func__);
	WARN_ON(1);
}

/*
 * vmbus_device_release - Final callback release of the vmbus child device
 */
static void vmbus_device_release(struct device *device)
{
	struct hv_device *device_ctx = device_to_hv_device(device);

	kfree(device_ctx);

	/* !!DO NOT REFERENCE device_ctx anymore at this point!! */
}



static irqreturn_t vmbus_isr(int irq, void *dev_id)
{
	int ret;

	ret = vmbus_on_isr();

	/* Schedules a dpc if necessary */
	if (ret > 0) {
		if (test_bit(0, (unsigned long *)&ret))
			tasklet_schedule(&vmbus_drv.msg_dpc);

		if (test_bit(1, (unsigned long *)&ret))
			tasklet_schedule(&vmbus_drv.event_dpc);

		return IRQ_HANDLED;
	} else {
		return IRQ_NONE;
	}
}

static struct dmi_system_id __initdata microsoft_hv_dmi_table[] = {
	{
		.ident = "Hyper-V",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Microsoft Corporation"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Virtual Machine"),
			DMI_MATCH(DMI_BOARD_NAME, "Virtual Machine"),
		},
	},
	{ },
};
MODULE_DEVICE_TABLE(dmi, microsoft_hv_dmi_table);

static int __init vmbus_init(void)
{
	DPRINT_INFO(VMBUS_DRV,
		"Vmbus initializing.... current log level 0x%x (%x,%x)",
		vmbus_loglevel, HIWORD(vmbus_loglevel), LOWORD(vmbus_loglevel));
	/* Todo: it is used for loglevel, to be ported to new kernel. */

	if (!dmi_check_system(microsoft_hv_dmi_table))
		return -ENODEV;

	return vmbus_bus_init();
}

static void __exit vmbus_exit(void)
{
	vmbus_bus_exit();
	/* Todo: it is used for loglevel, to be ported to new kernel. */
}

/*
 * We use a PCI table to determine if we should autoload this driver  This is
 * needed by distro tools to determine if the hyperv drivers should be
 * installed and/or configured.  We don't do anything else with the table, but
 * it needs to be present.
 */
static const struct pci_device_id microsoft_hv_pci_table[] = {
	{ PCI_DEVICE(0x1414, 0x5353) },	/* VGA compatible controller */
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, microsoft_hv_pci_table);

MODULE_LICENSE("GPL");
MODULE_VERSION(HV_DRV_VERSION);
module_param(vmbus_irq, int, S_IRUGO);
module_param(vmbus_loglevel, int, S_IRUGO);

module_init(vmbus_init);
module_exit(vmbus_exit);
