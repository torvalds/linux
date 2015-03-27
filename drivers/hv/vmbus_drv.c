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
 *   K. Y. Srinivasan <kys@microsoft.com>
 *
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/sysctl.h>
#include <linux/slab.h>
#include <linux/acpi.h>
#include <linux/completion.h>
#include <linux/hyperv.h>
#include <linux/kernel_stat.h>
#include <linux/clockchips.h>
#include <linux/cpu.h>
#include <asm/hyperv.h>
#include <asm/hypervisor.h>
#include <asm/mshyperv.h>
#include <linux/notifier.h>
#include <linux/ptrace.h>
#include "hyperv_vmbus.h"

static struct acpi_device  *hv_acpi_dev;

static struct tasklet_struct msg_dpc;
static struct completion probe_event;
static int irq;


static int hyperv_panic_event(struct notifier_block *nb,
			unsigned long event, void *ptr)
{
	struct pt_regs *regs;

	regs = current_pt_regs();

	wrmsrl(HV_X64_MSR_CRASH_P0, regs->ip);
	wrmsrl(HV_X64_MSR_CRASH_P1, regs->ax);
	wrmsrl(HV_X64_MSR_CRASH_P2, regs->bx);
	wrmsrl(HV_X64_MSR_CRASH_P3, regs->cx);
	wrmsrl(HV_X64_MSR_CRASH_P4, regs->dx);

	/*
	 * Let Hyper-V know there is crash data available
	 */
	wrmsrl(HV_X64_MSR_CRASH_CTL, HV_CRASH_CTL_CRASH_NOTIFY);
	return NOTIFY_DONE;
}

static struct notifier_block hyperv_panic_block = {
	.notifier_call = hyperv_panic_event,
};

struct resource hyperv_mmio = {
	.name  = "hyperv mmio",
	.flags = IORESOURCE_MEM,
};
EXPORT_SYMBOL_GPL(hyperv_mmio);

static int vmbus_exists(void)
{
	if (hv_acpi_dev == NULL)
		return -ENODEV;

	return 0;
}

#define VMBUS_ALIAS_LEN ((sizeof((struct hv_vmbus_device_id *)0)->guid) * 2)
static void print_alias_name(struct hv_device *hv_dev, char *alias_name)
{
	int i;
	for (i = 0; i < VMBUS_ALIAS_LEN; i += 2)
		sprintf(&alias_name[i], "%02x", hv_dev->dev_type.b[i/2]);
}

static u8 channel_monitor_group(struct vmbus_channel *channel)
{
	return (u8)channel->offermsg.monitorid / 32;
}

static u8 channel_monitor_offset(struct vmbus_channel *channel)
{
	return (u8)channel->offermsg.monitorid % 32;
}

static u32 channel_pending(struct vmbus_channel *channel,
			   struct hv_monitor_page *monitor_page)
{
	u8 monitor_group = channel_monitor_group(channel);
	return monitor_page->trigger_group[monitor_group].pending;
}

static u32 channel_latency(struct vmbus_channel *channel,
			   struct hv_monitor_page *monitor_page)
{
	u8 monitor_group = channel_monitor_group(channel);
	u8 monitor_offset = channel_monitor_offset(channel);
	return monitor_page->latency[monitor_group][monitor_offset];
}

static u32 channel_conn_id(struct vmbus_channel *channel,
			   struct hv_monitor_page *monitor_page)
{
	u8 monitor_group = channel_monitor_group(channel);
	u8 monitor_offset = channel_monitor_offset(channel);
	return monitor_page->parameter[monitor_group][monitor_offset].connectionid.u.id;
}

static ssize_t id_show(struct device *dev, struct device_attribute *dev_attr,
		       char *buf)
{
	struct hv_device *hv_dev = device_to_hv_device(dev);

	if (!hv_dev->channel)
		return -ENODEV;
	return sprintf(buf, "%d\n", hv_dev->channel->offermsg.child_relid);
}
static DEVICE_ATTR_RO(id);

static ssize_t state_show(struct device *dev, struct device_attribute *dev_attr,
			  char *buf)
{
	struct hv_device *hv_dev = device_to_hv_device(dev);

	if (!hv_dev->channel)
		return -ENODEV;
	return sprintf(buf, "%d\n", hv_dev->channel->state);
}
static DEVICE_ATTR_RO(state);

static ssize_t monitor_id_show(struct device *dev,
			       struct device_attribute *dev_attr, char *buf)
{
	struct hv_device *hv_dev = device_to_hv_device(dev);

	if (!hv_dev->channel)
		return -ENODEV;
	return sprintf(buf, "%d\n", hv_dev->channel->offermsg.monitorid);
}
static DEVICE_ATTR_RO(monitor_id);

static ssize_t class_id_show(struct device *dev,
			       struct device_attribute *dev_attr, char *buf)
{
	struct hv_device *hv_dev = device_to_hv_device(dev);

	if (!hv_dev->channel)
		return -ENODEV;
	return sprintf(buf, "{%pUl}\n",
		       hv_dev->channel->offermsg.offer.if_type.b);
}
static DEVICE_ATTR_RO(class_id);

static ssize_t device_id_show(struct device *dev,
			      struct device_attribute *dev_attr, char *buf)
{
	struct hv_device *hv_dev = device_to_hv_device(dev);

	if (!hv_dev->channel)
		return -ENODEV;
	return sprintf(buf, "{%pUl}\n",
		       hv_dev->channel->offermsg.offer.if_instance.b);
}
static DEVICE_ATTR_RO(device_id);

static ssize_t modalias_show(struct device *dev,
			     struct device_attribute *dev_attr, char *buf)
{
	struct hv_device *hv_dev = device_to_hv_device(dev);
	char alias_name[VMBUS_ALIAS_LEN + 1];

	print_alias_name(hv_dev, alias_name);
	return sprintf(buf, "vmbus:%s\n", alias_name);
}
static DEVICE_ATTR_RO(modalias);

static ssize_t server_monitor_pending_show(struct device *dev,
					   struct device_attribute *dev_attr,
					   char *buf)
{
	struct hv_device *hv_dev = device_to_hv_device(dev);

	if (!hv_dev->channel)
		return -ENODEV;
	return sprintf(buf, "%d\n",
		       channel_pending(hv_dev->channel,
				       vmbus_connection.monitor_pages[1]));
}
static DEVICE_ATTR_RO(server_monitor_pending);

static ssize_t client_monitor_pending_show(struct device *dev,
					   struct device_attribute *dev_attr,
					   char *buf)
{
	struct hv_device *hv_dev = device_to_hv_device(dev);

	if (!hv_dev->channel)
		return -ENODEV;
	return sprintf(buf, "%d\n",
		       channel_pending(hv_dev->channel,
				       vmbus_connection.monitor_pages[1]));
}
static DEVICE_ATTR_RO(client_monitor_pending);

static ssize_t server_monitor_latency_show(struct device *dev,
					   struct device_attribute *dev_attr,
					   char *buf)
{
	struct hv_device *hv_dev = device_to_hv_device(dev);

	if (!hv_dev->channel)
		return -ENODEV;
	return sprintf(buf, "%d\n",
		       channel_latency(hv_dev->channel,
				       vmbus_connection.monitor_pages[0]));
}
static DEVICE_ATTR_RO(server_monitor_latency);

static ssize_t client_monitor_latency_show(struct device *dev,
					   struct device_attribute *dev_attr,
					   char *buf)
{
	struct hv_device *hv_dev = device_to_hv_device(dev);

	if (!hv_dev->channel)
		return -ENODEV;
	return sprintf(buf, "%d\n",
		       channel_latency(hv_dev->channel,
				       vmbus_connection.monitor_pages[1]));
}
static DEVICE_ATTR_RO(client_monitor_latency);

static ssize_t server_monitor_conn_id_show(struct device *dev,
					   struct device_attribute *dev_attr,
					   char *buf)
{
	struct hv_device *hv_dev = device_to_hv_device(dev);

	if (!hv_dev->channel)
		return -ENODEV;
	return sprintf(buf, "%d\n",
		       channel_conn_id(hv_dev->channel,
				       vmbus_connection.monitor_pages[0]));
}
static DEVICE_ATTR_RO(server_monitor_conn_id);

static ssize_t client_monitor_conn_id_show(struct device *dev,
					   struct device_attribute *dev_attr,
					   char *buf)
{
	struct hv_device *hv_dev = device_to_hv_device(dev);

	if (!hv_dev->channel)
		return -ENODEV;
	return sprintf(buf, "%d\n",
		       channel_conn_id(hv_dev->channel,
				       vmbus_connection.monitor_pages[1]));
}
static DEVICE_ATTR_RO(client_monitor_conn_id);

static ssize_t out_intr_mask_show(struct device *dev,
				  struct device_attribute *dev_attr, char *buf)
{
	struct hv_device *hv_dev = device_to_hv_device(dev);
	struct hv_ring_buffer_debug_info outbound;

	if (!hv_dev->channel)
		return -ENODEV;
	hv_ringbuffer_get_debuginfo(&hv_dev->channel->outbound, &outbound);
	return sprintf(buf, "%d\n", outbound.current_interrupt_mask);
}
static DEVICE_ATTR_RO(out_intr_mask);

static ssize_t out_read_index_show(struct device *dev,
				   struct device_attribute *dev_attr, char *buf)
{
	struct hv_device *hv_dev = device_to_hv_device(dev);
	struct hv_ring_buffer_debug_info outbound;

	if (!hv_dev->channel)
		return -ENODEV;
	hv_ringbuffer_get_debuginfo(&hv_dev->channel->outbound, &outbound);
	return sprintf(buf, "%d\n", outbound.current_read_index);
}
static DEVICE_ATTR_RO(out_read_index);

static ssize_t out_write_index_show(struct device *dev,
				    struct device_attribute *dev_attr,
				    char *buf)
{
	struct hv_device *hv_dev = device_to_hv_device(dev);
	struct hv_ring_buffer_debug_info outbound;

	if (!hv_dev->channel)
		return -ENODEV;
	hv_ringbuffer_get_debuginfo(&hv_dev->channel->outbound, &outbound);
	return sprintf(buf, "%d\n", outbound.current_write_index);
}
static DEVICE_ATTR_RO(out_write_index);

static ssize_t out_read_bytes_avail_show(struct device *dev,
					 struct device_attribute *dev_attr,
					 char *buf)
{
	struct hv_device *hv_dev = device_to_hv_device(dev);
	struct hv_ring_buffer_debug_info outbound;

	if (!hv_dev->channel)
		return -ENODEV;
	hv_ringbuffer_get_debuginfo(&hv_dev->channel->outbound, &outbound);
	return sprintf(buf, "%d\n", outbound.bytes_avail_toread);
}
static DEVICE_ATTR_RO(out_read_bytes_avail);

static ssize_t out_write_bytes_avail_show(struct device *dev,
					  struct device_attribute *dev_attr,
					  char *buf)
{
	struct hv_device *hv_dev = device_to_hv_device(dev);
	struct hv_ring_buffer_debug_info outbound;

	if (!hv_dev->channel)
		return -ENODEV;
	hv_ringbuffer_get_debuginfo(&hv_dev->channel->outbound, &outbound);
	return sprintf(buf, "%d\n", outbound.bytes_avail_towrite);
}
static DEVICE_ATTR_RO(out_write_bytes_avail);

static ssize_t in_intr_mask_show(struct device *dev,
				 struct device_attribute *dev_attr, char *buf)
{
	struct hv_device *hv_dev = device_to_hv_device(dev);
	struct hv_ring_buffer_debug_info inbound;

	if (!hv_dev->channel)
		return -ENODEV;
	hv_ringbuffer_get_debuginfo(&hv_dev->channel->inbound, &inbound);
	return sprintf(buf, "%d\n", inbound.current_interrupt_mask);
}
static DEVICE_ATTR_RO(in_intr_mask);

static ssize_t in_read_index_show(struct device *dev,
				  struct device_attribute *dev_attr, char *buf)
{
	struct hv_device *hv_dev = device_to_hv_device(dev);
	struct hv_ring_buffer_debug_info inbound;

	if (!hv_dev->channel)
		return -ENODEV;
	hv_ringbuffer_get_debuginfo(&hv_dev->channel->inbound, &inbound);
	return sprintf(buf, "%d\n", inbound.current_read_index);
}
static DEVICE_ATTR_RO(in_read_index);

static ssize_t in_write_index_show(struct device *dev,
				   struct device_attribute *dev_attr, char *buf)
{
	struct hv_device *hv_dev = device_to_hv_device(dev);
	struct hv_ring_buffer_debug_info inbound;

	if (!hv_dev->channel)
		return -ENODEV;
	hv_ringbuffer_get_debuginfo(&hv_dev->channel->inbound, &inbound);
	return sprintf(buf, "%d\n", inbound.current_write_index);
}
static DEVICE_ATTR_RO(in_write_index);

static ssize_t in_read_bytes_avail_show(struct device *dev,
					struct device_attribute *dev_attr,
					char *buf)
{
	struct hv_device *hv_dev = device_to_hv_device(dev);
	struct hv_ring_buffer_debug_info inbound;

	if (!hv_dev->channel)
		return -ENODEV;
	hv_ringbuffer_get_debuginfo(&hv_dev->channel->inbound, &inbound);
	return sprintf(buf, "%d\n", inbound.bytes_avail_toread);
}
static DEVICE_ATTR_RO(in_read_bytes_avail);

static ssize_t in_write_bytes_avail_show(struct device *dev,
					 struct device_attribute *dev_attr,
					 char *buf)
{
	struct hv_device *hv_dev = device_to_hv_device(dev);
	struct hv_ring_buffer_debug_info inbound;

	if (!hv_dev->channel)
		return -ENODEV;
	hv_ringbuffer_get_debuginfo(&hv_dev->channel->inbound, &inbound);
	return sprintf(buf, "%d\n", inbound.bytes_avail_towrite);
}
static DEVICE_ATTR_RO(in_write_bytes_avail);

/* Set up per device attributes in /sys/bus/vmbus/devices/<bus device> */
static struct attribute *vmbus_attrs[] = {
	&dev_attr_id.attr,
	&dev_attr_state.attr,
	&dev_attr_monitor_id.attr,
	&dev_attr_class_id.attr,
	&dev_attr_device_id.attr,
	&dev_attr_modalias.attr,
	&dev_attr_server_monitor_pending.attr,
	&dev_attr_client_monitor_pending.attr,
	&dev_attr_server_monitor_latency.attr,
	&dev_attr_client_monitor_latency.attr,
	&dev_attr_server_monitor_conn_id.attr,
	&dev_attr_client_monitor_conn_id.attr,
	&dev_attr_out_intr_mask.attr,
	&dev_attr_out_read_index.attr,
	&dev_attr_out_write_index.attr,
	&dev_attr_out_read_bytes_avail.attr,
	&dev_attr_out_write_bytes_avail.attr,
	&dev_attr_in_intr_mask.attr,
	&dev_attr_in_read_index.attr,
	&dev_attr_in_write_index.attr,
	&dev_attr_in_read_bytes_avail.attr,
	&dev_attr_in_write_bytes_avail.attr,
	NULL,
};
ATTRIBUTE_GROUPS(vmbus);

/*
 * vmbus_uevent - add uevent for our device
 *
 * This routine is invoked when a device is added or removed on the vmbus to
 * generate a uevent to udev in the userspace. The udev will then look at its
 * rule and the uevent generated here to load the appropriate driver
 *
 * The alias string will be of the form vmbus:guid where guid is the string
 * representation of the device guid (each byte of the guid will be
 * represented with two hex characters.
 */
static int vmbus_uevent(struct device *device, struct kobj_uevent_env *env)
{
	struct hv_device *dev = device_to_hv_device(device);
	int ret;
	char alias_name[VMBUS_ALIAS_LEN + 1];

	print_alias_name(dev, alias_name);
	ret = add_uevent_var(env, "MODALIAS=vmbus:%s", alias_name);
	return ret;
}

static const uuid_le null_guid;

static inline bool is_null_guid(const __u8 *guid)
{
	if (memcmp(guid, &null_guid, sizeof(uuid_le)))
		return false;
	return true;
}

/*
 * Return a matching hv_vmbus_device_id pointer.
 * If there is no match, return NULL.
 */
static const struct hv_vmbus_device_id *hv_vmbus_get_id(
					const struct hv_vmbus_device_id *id,
					const __u8 *guid)
{
	for (; !is_null_guid(id->guid); id++)
		if (!memcmp(&id->guid, guid, sizeof(uuid_le)))
			return id;

	return NULL;
}



/*
 * vmbus_match - Attempt to match the specified device to the specified driver
 */
static int vmbus_match(struct device *device, struct device_driver *driver)
{
	struct hv_driver *drv = drv_to_hv_drv(driver);
	struct hv_device *hv_dev = device_to_hv_device(device);

	if (hv_vmbus_get_id(drv->id_table, hv_dev->dev_type.b))
		return 1;

	return 0;
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
	const struct hv_vmbus_device_id *dev_id;

	dev_id = hv_vmbus_get_id(drv->id_table, dev->dev_type.b);
	if (drv->probe) {
		ret = drv->probe(dev, dev_id);
		if (ret != 0)
			pr_err("probe failed for device %s (%d)\n",
			       dev_name(child_device), ret);

	} else {
		pr_err("probe not set for driver %s\n",
		       dev_name(child_device));
		ret = -ENODEV;
	}
	return ret;
}

/*
 * vmbus_remove - Remove a vmbus device
 */
static int vmbus_remove(struct device *child_device)
{
	struct hv_driver *drv;
	struct hv_device *dev = device_to_hv_device(child_device);
	u32 relid = dev->channel->offermsg.child_relid;

	if (child_device->driver) {
		drv = drv_to_hv_drv(child_device->driver);
		if (drv->remove)
			drv->remove(dev);
		else {
			hv_process_channel_removal(dev->channel, relid);
			pr_err("remove not set for driver %s\n",
				dev_name(child_device));
		}
	} else {
		/*
		 * We don't have a driver for this device; deal with the
		 * rescind message by removing the channel.
		 */
		hv_process_channel_removal(dev->channel, relid);
	}

	return 0;
}


/*
 * vmbus_shutdown - Shutdown a vmbus device
 */
static void vmbus_shutdown(struct device *child_device)
{
	struct hv_driver *drv;
	struct hv_device *dev = device_to_hv_device(child_device);


	/* The device may not be attached yet */
	if (!child_device->driver)
		return;

	drv = drv_to_hv_drv(child_device->driver);

	if (drv->shutdown)
		drv->shutdown(dev);

	return;
}


/*
 * vmbus_device_release - Final callback release of the vmbus child device
 */
static void vmbus_device_release(struct device *device)
{
	struct hv_device *hv_dev = device_to_hv_device(device);

	kfree(hv_dev);

}

/* The one and only one */
static struct bus_type  hv_bus = {
	.name =		"vmbus",
	.match =		vmbus_match,
	.shutdown =		vmbus_shutdown,
	.remove =		vmbus_remove,
	.probe =		vmbus_probe,
	.uevent =		vmbus_uevent,
	.dev_groups =		vmbus_groups,
};

struct onmessage_work_context {
	struct work_struct work;
	struct hv_message msg;
};

static void vmbus_onmessage_work(struct work_struct *work)
{
	struct onmessage_work_context *ctx;

	/* Do not process messages if we're in DISCONNECTED state */
	if (vmbus_connection.conn_state == DISCONNECTED)
		return;

	ctx = container_of(work, struct onmessage_work_context,
			   work);
	vmbus_onmessage(&ctx->msg);
	kfree(ctx);
}

static void hv_process_timer_expiration(struct hv_message *msg, int cpu)
{
	struct clock_event_device *dev = hv_context.clk_evt[cpu];

	if (dev->event_handler)
		dev->event_handler(dev);

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

static void vmbus_on_msg_dpc(unsigned long data)
{
	int cpu = smp_processor_id();
	void *page_addr = hv_context.synic_message_page[cpu];
	struct hv_message *msg = (struct hv_message *)page_addr +
				  VMBUS_MESSAGE_SINT;
	struct vmbus_channel_message_header *hdr;
	struct vmbus_channel_message_table_entry *entry;
	struct onmessage_work_context *ctx;

	while (1) {
		if (msg->header.message_type == HVMSG_NONE)
			/* no msg */
			break;

		hdr = (struct vmbus_channel_message_header *)msg->u.payload;

		if (hdr->msgtype >= CHANNELMSG_COUNT) {
			WARN_ONCE(1, "unknown msgtype=%d\n", hdr->msgtype);
			goto msg_handled;
		}

		entry = &channel_message_table[hdr->msgtype];
		if (entry->handler_type	== VMHT_BLOCKING) {
			ctx = kmalloc(sizeof(*ctx), GFP_ATOMIC);
			if (ctx == NULL)
				continue;

			INIT_WORK(&ctx->work, vmbus_onmessage_work);
			memcpy(&ctx->msg, msg, sizeof(*msg));

			queue_work(vmbus_connection.work_queue, &ctx->work);
		} else
			entry->message_handler(hdr);

msg_handled:
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

static void vmbus_isr(void)
{
	int cpu = smp_processor_id();
	void *page_addr;
	struct hv_message *msg;
	union hv_synic_event_flags *event;
	bool handled = false;

	page_addr = hv_context.synic_event_page[cpu];
	if (page_addr == NULL)
		return;

	event = (union hv_synic_event_flags *)page_addr +
					 VMBUS_MESSAGE_SINT;
	/*
	 * Check for events before checking for messages. This is the order
	 * in which events and messages are checked in Windows guests on
	 * Hyper-V, and the Windows team suggested we do the same.
	 */

	if ((vmbus_proto_version == VERSION_WS2008) ||
		(vmbus_proto_version == VERSION_WIN7)) {

		/* Since we are a child, we only need to check bit 0 */
		if (sync_test_and_clear_bit(0,
			(unsigned long *) &event->flags32[0])) {
			handled = true;
		}
	} else {
		/*
		 * Our host is win8 or above. The signaling mechanism
		 * has changed and we can directly look at the event page.
		 * If bit n is set then we have an interrup on the channel
		 * whose id is n.
		 */
		handled = true;
	}

	if (handled)
		tasklet_schedule(hv_context.event_dpc[cpu]);


	page_addr = hv_context.synic_message_page[cpu];
	msg = (struct hv_message *)page_addr + VMBUS_MESSAGE_SINT;

	/* Check if there are actual msgs to be processed */
	if (msg->header.message_type != HVMSG_NONE) {
		if (msg->header.message_type == HVMSG_TIMER_EXPIRED)
			hv_process_timer_expiration(msg, cpu);
		else
			tasklet_schedule(&msg_dpc);
	}
}

#ifdef CONFIG_HOTPLUG_CPU
static int hyperv_cpu_disable(void)
{
	return -ENOSYS;
}

static void hv_cpu_hotplug_quirk(bool vmbus_loaded)
{
	static void *previous_cpu_disable;

	/*
	 * Offlining a CPU when running on newer hypervisors (WS2012R2, Win8,
	 * ...) is not supported at this moment as channel interrupts are
	 * distributed across all of them.
	 */

	if ((vmbus_proto_version == VERSION_WS2008) ||
	    (vmbus_proto_version == VERSION_WIN7))
		return;

	if (vmbus_loaded) {
		previous_cpu_disable = smp_ops.cpu_disable;
		smp_ops.cpu_disable = hyperv_cpu_disable;
		pr_notice("CPU offlining is not supported by hypervisor\n");
	} else if (previous_cpu_disable)
		smp_ops.cpu_disable = previous_cpu_disable;
}
#else
static void hv_cpu_hotplug_quirk(bool vmbus_loaded)
{
}
#endif

/*
 * vmbus_bus_init -Main vmbus driver initialization routine.
 *
 * Here, we
 *	- initialize the vmbus driver context
 *	- invoke the vmbus hv main init routine
 *	- get the irq resource
 *	- retrieve the channel offers
 */
static int vmbus_bus_init(int irq)
{
	int ret;

	/* Hypervisor initialization...setup hypercall page..etc */
	ret = hv_init();
	if (ret != 0) {
		pr_err("Unable to initialize the hypervisor - 0x%x\n", ret);
		return ret;
	}

	tasklet_init(&msg_dpc, vmbus_on_msg_dpc, 0);

	ret = bus_register(&hv_bus);
	if (ret)
		goto err_cleanup;

	hv_setup_vmbus_irq(vmbus_isr);

	ret = hv_synic_alloc();
	if (ret)
		goto err_alloc;
	/*
	 * Initialize the per-cpu interrupt state and
	 * connect to the host.
	 */
	on_each_cpu(hv_synic_init, NULL, 1);
	ret = vmbus_connect();
	if (ret)
		goto err_alloc;

	hv_cpu_hotplug_quirk(true);

	/*
	 * Only register if the crash MSRs are available
	 */
	if (ms_hyperv.features & HV_FEATURE_GUEST_CRASH_MSR_AVAILABLE) {
		atomic_notifier_chain_register(&panic_notifier_list,
					       &hyperv_panic_block);
	}

	vmbus_request_offers();

	return 0;

err_alloc:
	hv_synic_free();
	hv_remove_vmbus_irq();

	bus_unregister(&hv_bus);

err_cleanup:
	hv_cleanup();

	return ret;
}

/**
 * __vmbus_child_driver_register - Register a vmbus's driver
 * @drv: Pointer to driver structure you want to register
 * @owner: owner module of the drv
 * @mod_name: module name string
 *
 * Registers the given driver with Linux through the 'driver_register()' call
 * and sets up the hyper-v vmbus handling for this driver.
 * It will return the state of the 'driver_register()' call.
 *
 */
int __vmbus_driver_register(struct hv_driver *hv_driver, struct module *owner, const char *mod_name)
{
	int ret;

	pr_info("registering driver %s\n", hv_driver->name);

	ret = vmbus_exists();
	if (ret < 0)
		return ret;

	hv_driver->driver.name = hv_driver->name;
	hv_driver->driver.owner = owner;
	hv_driver->driver.mod_name = mod_name;
	hv_driver->driver.bus = &hv_bus;

	ret = driver_register(&hv_driver->driver);

	return ret;
}
EXPORT_SYMBOL_GPL(__vmbus_driver_register);

/**
 * vmbus_driver_unregister() - Unregister a vmbus's driver
 * @drv: Pointer to driver structure you want to un-register
 *
 * Un-register the given driver that was previous registered with a call to
 * vmbus_driver_register()
 */
void vmbus_driver_unregister(struct hv_driver *hv_driver)
{
	pr_info("unregistering driver %s\n", hv_driver->name);

	if (!vmbus_exists())
		driver_unregister(&hv_driver->driver);
}
EXPORT_SYMBOL_GPL(vmbus_driver_unregister);

/*
 * vmbus_device_create - Creates and registers a new child device
 * on the vmbus.
 */
struct hv_device *vmbus_device_create(const uuid_le *type,
				      const uuid_le *instance,
				      struct vmbus_channel *channel)
{
	struct hv_device *child_device_obj;

	child_device_obj = kzalloc(sizeof(struct hv_device), GFP_KERNEL);
	if (!child_device_obj) {
		pr_err("Unable to allocate device object for child device\n");
		return NULL;
	}

	child_device_obj->channel = channel;
	memcpy(&child_device_obj->dev_type, type, sizeof(uuid_le));
	memcpy(&child_device_obj->dev_instance, instance,
	       sizeof(uuid_le));


	return child_device_obj;
}

/*
 * vmbus_device_register - Register the child device
 */
int vmbus_device_register(struct hv_device *child_device_obj)
{
	int ret = 0;

	dev_set_name(&child_device_obj->device, "vmbus_%d",
		     child_device_obj->channel->id);

	child_device_obj->device.bus = &hv_bus;
	child_device_obj->device.parent = &hv_acpi_dev->dev;
	child_device_obj->device.release = vmbus_device_release;

	/*
	 * Register with the LDM. This will kick off the driver/device
	 * binding...which will eventually call vmbus_match() and vmbus_probe()
	 */
	ret = device_register(&child_device_obj->device);

	if (ret)
		pr_err("Unable to register child device\n");
	else
		pr_debug("child device %s registered\n",
			dev_name(&child_device_obj->device));

	return ret;
}

/*
 * vmbus_device_unregister - Remove the specified child device
 * from the vmbus.
 */
void vmbus_device_unregister(struct hv_device *device_obj)
{
	pr_debug("child device %s unregistered\n",
		dev_name(&device_obj->device));

	/*
	 * Kick off the process of unregistering the device.
	 * This will call vmbus_remove() and eventually vmbus_device_release()
	 */
	device_unregister(&device_obj->device);
}


/*
 * VMBUS is an acpi enumerated device. Get the the information we
 * need from DSDT.
 */

static acpi_status vmbus_walk_resources(struct acpi_resource *res, void *ctx)
{
	switch (res->type) {
	case ACPI_RESOURCE_TYPE_IRQ:
		irq = res->data.irq.interrupts[0];
		break;

	case ACPI_RESOURCE_TYPE_ADDRESS64:
		hyperv_mmio.start = res->data.address64.address.minimum;
		hyperv_mmio.end = res->data.address64.address.maximum;
		break;
	}

	return AE_OK;
}

static int vmbus_acpi_add(struct acpi_device *device)
{
	acpi_status result;
	int ret_val = -ENODEV;

	hv_acpi_dev = device;

	result = acpi_walk_resources(device->handle, METHOD_NAME__CRS,
					vmbus_walk_resources, NULL);

	if (ACPI_FAILURE(result))
		goto acpi_walk_err;
	/*
	 * The parent of the vmbus acpi device (Gen2 firmware) is the VMOD that
	 * has the mmio ranges. Get that.
	 */
	if (device->parent) {
		result = acpi_walk_resources(device->parent->handle,
					METHOD_NAME__CRS,
					vmbus_walk_resources, NULL);

		if (ACPI_FAILURE(result))
			goto acpi_walk_err;
		if (hyperv_mmio.start && hyperv_mmio.end)
			request_resource(&iomem_resource, &hyperv_mmio);
	}
	ret_val = 0;

acpi_walk_err:
	complete(&probe_event);
	return ret_val;
}

static const struct acpi_device_id vmbus_acpi_device_ids[] = {
	{"VMBUS", 0},
	{"VMBus", 0},
	{"", 0},
};
MODULE_DEVICE_TABLE(acpi, vmbus_acpi_device_ids);

static struct acpi_driver vmbus_acpi_driver = {
	.name = "vmbus",
	.ids = vmbus_acpi_device_ids,
	.ops = {
		.add = vmbus_acpi_add,
	},
};

static int __init hv_acpi_init(void)
{
	int ret, t;

	if (x86_hyper != &x86_hyper_ms_hyperv)
		return -ENODEV;

	init_completion(&probe_event);

	/*
	 * Get irq resources first.
	 */
	ret = acpi_bus_register_driver(&vmbus_acpi_driver);

	if (ret)
		return ret;

	t = wait_for_completion_timeout(&probe_event, 5*HZ);
	if (t == 0) {
		ret = -ETIMEDOUT;
		goto cleanup;
	}

	if (irq <= 0) {
		ret = -ENODEV;
		goto cleanup;
	}

	ret = vmbus_bus_init(irq);
	if (ret)
		goto cleanup;

	return 0;

cleanup:
	acpi_bus_unregister_driver(&vmbus_acpi_driver);
	hv_acpi_dev = NULL;
	return ret;
}

static void __exit vmbus_exit(void)
{
	int cpu;

	vmbus_connection.conn_state = DISCONNECTED;
	hv_synic_clockevents_cleanup();
	hv_remove_vmbus_irq();
	vmbus_free_channels();
	bus_unregister(&hv_bus);
	hv_cleanup();
	for_each_online_cpu(cpu)
		smp_call_function_single(cpu, hv_synic_cleanup, NULL, 1);
	acpi_bus_unregister_driver(&vmbus_acpi_driver);
	hv_cpu_hotplug_quirk(false);
	vmbus_disconnect();
}


MODULE_LICENSE("GPL");

subsys_initcall(hv_acpi_init);
module_exit(vmbus_exit);
