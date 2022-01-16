// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2009, Microsoft Corporation.
 *
 * Authors:
 *   Haiyang Zhang <haiyangz@microsoft.com>
 *   Hank Janssen  <hjanssen@microsoft.com>
 *   K. Y. Srinivasan <kys@microsoft.com>
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
#include <linux/sched/task_stack.h>

#include <linux/delay.h>
#include <linux/notifier.h>
#include <linux/panic_notifier.h>
#include <linux/ptrace.h>
#include <linux/screen_info.h>
#include <linux/kdebug.h>
#include <linux/efi.h>
#include <linux/random.h>
#include <linux/kernel.h>
#include <linux/syscore_ops.h>
#include <linux/dma-map-ops.h>
#include <clocksource/hyperv_timer.h>
#include "hyperv_vmbus.h"

struct vmbus_dynid {
	struct list_head node;
	struct hv_vmbus_device_id id;
};

static struct acpi_device  *hv_acpi_dev;

static struct completion probe_event;

static int hyperv_cpuhp_online;

static void *hv_panic_page;

static long __percpu *vmbus_evt;

/* Values parsed from ACPI DSDT */
int vmbus_irq;
int vmbus_interrupt;

/*
 * Boolean to control whether to report panic messages over Hyper-V.
 *
 * It can be set via /proc/sys/kernel/hyperv_record_panic_msg
 */
static int sysctl_record_panic_msg = 1;

static int hyperv_report_reg(void)
{
	return !sysctl_record_panic_msg || !hv_panic_page;
}

static int hyperv_panic_event(struct notifier_block *nb, unsigned long val,
			      void *args)
{
	struct pt_regs *regs;

	vmbus_initiate_unload(true);

	/*
	 * Hyper-V should be notified only once about a panic.  If we will be
	 * doing hyperv_report_panic_msg() later with kmsg data, don't do
	 * the notification here.
	 */
	if (ms_hyperv.misc_features & HV_FEATURE_GUEST_CRASH_MSR_AVAILABLE
	    && hyperv_report_reg()) {
		regs = current_pt_regs();
		hyperv_report_panic(regs, val, false);
	}
	return NOTIFY_DONE;
}

static int hyperv_die_event(struct notifier_block *nb, unsigned long val,
			    void *args)
{
	struct die_args *die = args;
	struct pt_regs *regs = die->regs;

	/* Don't notify Hyper-V if the die event is other than oops */
	if (val != DIE_OOPS)
		return NOTIFY_DONE;

	/*
	 * Hyper-V should be notified only once about a panic.  If we will be
	 * doing hyperv_report_panic_msg() later with kmsg data, don't do
	 * the notification here.
	 */
	if (hyperv_report_reg())
		hyperv_report_panic(regs, val, true);
	return NOTIFY_DONE;
}

static struct notifier_block hyperv_die_block = {
	.notifier_call = hyperv_die_event,
};
static struct notifier_block hyperv_panic_block = {
	.notifier_call = hyperv_panic_event,
};

static const char *fb_mmio_name = "fb_range";
static struct resource *fb_mmio;
static struct resource *hyperv_mmio;
static DEFINE_MUTEX(hyperv_mmio_lock);

static int vmbus_exists(void)
{
	if (hv_acpi_dev == NULL)
		return -ENODEV;

	return 0;
}

static u8 channel_monitor_group(const struct vmbus_channel *channel)
{
	return (u8)channel->offermsg.monitorid / 32;
}

static u8 channel_monitor_offset(const struct vmbus_channel *channel)
{
	return (u8)channel->offermsg.monitorid % 32;
}

static u32 channel_pending(const struct vmbus_channel *channel,
			   const struct hv_monitor_page *monitor_page)
{
	u8 monitor_group = channel_monitor_group(channel);

	return monitor_page->trigger_group[monitor_group].pending;
}

static u32 channel_latency(const struct vmbus_channel *channel,
			   const struct hv_monitor_page *monitor_page)
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
		       &hv_dev->channel->offermsg.offer.if_type);
}
static DEVICE_ATTR_RO(class_id);

static ssize_t device_id_show(struct device *dev,
			      struct device_attribute *dev_attr, char *buf)
{
	struct hv_device *hv_dev = device_to_hv_device(dev);

	if (!hv_dev->channel)
		return -ENODEV;
	return sprintf(buf, "{%pUl}\n",
		       &hv_dev->channel->offermsg.offer.if_instance);
}
static DEVICE_ATTR_RO(device_id);

static ssize_t modalias_show(struct device *dev,
			     struct device_attribute *dev_attr, char *buf)
{
	struct hv_device *hv_dev = device_to_hv_device(dev);

	return sprintf(buf, "vmbus:%*phN\n", UUID_SIZE, &hv_dev->dev_type);
}
static DEVICE_ATTR_RO(modalias);

#ifdef CONFIG_NUMA
static ssize_t numa_node_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct hv_device *hv_dev = device_to_hv_device(dev);

	if (!hv_dev->channel)
		return -ENODEV;

	return sprintf(buf, "%d\n", cpu_to_node(hv_dev->channel->target_cpu));
}
static DEVICE_ATTR_RO(numa_node);
#endif

static ssize_t server_monitor_pending_show(struct device *dev,
					   struct device_attribute *dev_attr,
					   char *buf)
{
	struct hv_device *hv_dev = device_to_hv_device(dev);

	if (!hv_dev->channel)
		return -ENODEV;
	return sprintf(buf, "%d\n",
		       channel_pending(hv_dev->channel,
				       vmbus_connection.monitor_pages[0]));
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
	int ret;

	if (!hv_dev->channel)
		return -ENODEV;

	ret = hv_ringbuffer_get_debuginfo(&hv_dev->channel->outbound,
					  &outbound);
	if (ret < 0)
		return ret;

	return sprintf(buf, "%d\n", outbound.current_interrupt_mask);
}
static DEVICE_ATTR_RO(out_intr_mask);

static ssize_t out_read_index_show(struct device *dev,
				   struct device_attribute *dev_attr, char *buf)
{
	struct hv_device *hv_dev = device_to_hv_device(dev);
	struct hv_ring_buffer_debug_info outbound;
	int ret;

	if (!hv_dev->channel)
		return -ENODEV;

	ret = hv_ringbuffer_get_debuginfo(&hv_dev->channel->outbound,
					  &outbound);
	if (ret < 0)
		return ret;
	return sprintf(buf, "%d\n", outbound.current_read_index);
}
static DEVICE_ATTR_RO(out_read_index);

static ssize_t out_write_index_show(struct device *dev,
				    struct device_attribute *dev_attr,
				    char *buf)
{
	struct hv_device *hv_dev = device_to_hv_device(dev);
	struct hv_ring_buffer_debug_info outbound;
	int ret;

	if (!hv_dev->channel)
		return -ENODEV;

	ret = hv_ringbuffer_get_debuginfo(&hv_dev->channel->outbound,
					  &outbound);
	if (ret < 0)
		return ret;
	return sprintf(buf, "%d\n", outbound.current_write_index);
}
static DEVICE_ATTR_RO(out_write_index);

static ssize_t out_read_bytes_avail_show(struct device *dev,
					 struct device_attribute *dev_attr,
					 char *buf)
{
	struct hv_device *hv_dev = device_to_hv_device(dev);
	struct hv_ring_buffer_debug_info outbound;
	int ret;

	if (!hv_dev->channel)
		return -ENODEV;

	ret = hv_ringbuffer_get_debuginfo(&hv_dev->channel->outbound,
					  &outbound);
	if (ret < 0)
		return ret;
	return sprintf(buf, "%d\n", outbound.bytes_avail_toread);
}
static DEVICE_ATTR_RO(out_read_bytes_avail);

static ssize_t out_write_bytes_avail_show(struct device *dev,
					  struct device_attribute *dev_attr,
					  char *buf)
{
	struct hv_device *hv_dev = device_to_hv_device(dev);
	struct hv_ring_buffer_debug_info outbound;
	int ret;

	if (!hv_dev->channel)
		return -ENODEV;

	ret = hv_ringbuffer_get_debuginfo(&hv_dev->channel->outbound,
					  &outbound);
	if (ret < 0)
		return ret;
	return sprintf(buf, "%d\n", outbound.bytes_avail_towrite);
}
static DEVICE_ATTR_RO(out_write_bytes_avail);

static ssize_t in_intr_mask_show(struct device *dev,
				 struct device_attribute *dev_attr, char *buf)
{
	struct hv_device *hv_dev = device_to_hv_device(dev);
	struct hv_ring_buffer_debug_info inbound;
	int ret;

	if (!hv_dev->channel)
		return -ENODEV;

	ret = hv_ringbuffer_get_debuginfo(&hv_dev->channel->inbound, &inbound);
	if (ret < 0)
		return ret;

	return sprintf(buf, "%d\n", inbound.current_interrupt_mask);
}
static DEVICE_ATTR_RO(in_intr_mask);

static ssize_t in_read_index_show(struct device *dev,
				  struct device_attribute *dev_attr, char *buf)
{
	struct hv_device *hv_dev = device_to_hv_device(dev);
	struct hv_ring_buffer_debug_info inbound;
	int ret;

	if (!hv_dev->channel)
		return -ENODEV;

	ret = hv_ringbuffer_get_debuginfo(&hv_dev->channel->inbound, &inbound);
	if (ret < 0)
		return ret;

	return sprintf(buf, "%d\n", inbound.current_read_index);
}
static DEVICE_ATTR_RO(in_read_index);

static ssize_t in_write_index_show(struct device *dev,
				   struct device_attribute *dev_attr, char *buf)
{
	struct hv_device *hv_dev = device_to_hv_device(dev);
	struct hv_ring_buffer_debug_info inbound;
	int ret;

	if (!hv_dev->channel)
		return -ENODEV;

	ret = hv_ringbuffer_get_debuginfo(&hv_dev->channel->inbound, &inbound);
	if (ret < 0)
		return ret;

	return sprintf(buf, "%d\n", inbound.current_write_index);
}
static DEVICE_ATTR_RO(in_write_index);

static ssize_t in_read_bytes_avail_show(struct device *dev,
					struct device_attribute *dev_attr,
					char *buf)
{
	struct hv_device *hv_dev = device_to_hv_device(dev);
	struct hv_ring_buffer_debug_info inbound;
	int ret;

	if (!hv_dev->channel)
		return -ENODEV;

	ret = hv_ringbuffer_get_debuginfo(&hv_dev->channel->inbound, &inbound);
	if (ret < 0)
		return ret;

	return sprintf(buf, "%d\n", inbound.bytes_avail_toread);
}
static DEVICE_ATTR_RO(in_read_bytes_avail);

static ssize_t in_write_bytes_avail_show(struct device *dev,
					 struct device_attribute *dev_attr,
					 char *buf)
{
	struct hv_device *hv_dev = device_to_hv_device(dev);
	struct hv_ring_buffer_debug_info inbound;
	int ret;

	if (!hv_dev->channel)
		return -ENODEV;

	ret = hv_ringbuffer_get_debuginfo(&hv_dev->channel->inbound, &inbound);
	if (ret < 0)
		return ret;

	return sprintf(buf, "%d\n", inbound.bytes_avail_towrite);
}
static DEVICE_ATTR_RO(in_write_bytes_avail);

static ssize_t channel_vp_mapping_show(struct device *dev,
				       struct device_attribute *dev_attr,
				       char *buf)
{
	struct hv_device *hv_dev = device_to_hv_device(dev);
	struct vmbus_channel *channel = hv_dev->channel, *cur_sc;
	int buf_size = PAGE_SIZE, n_written, tot_written;
	struct list_head *cur;

	if (!channel)
		return -ENODEV;

	mutex_lock(&vmbus_connection.channel_mutex);

	tot_written = snprintf(buf, buf_size, "%u:%u\n",
		channel->offermsg.child_relid, channel->target_cpu);

	list_for_each(cur, &channel->sc_list) {
		if (tot_written >= buf_size - 1)
			break;

		cur_sc = list_entry(cur, struct vmbus_channel, sc_list);
		n_written = scnprintf(buf + tot_written,
				     buf_size - tot_written,
				     "%u:%u\n",
				     cur_sc->offermsg.child_relid,
				     cur_sc->target_cpu);
		tot_written += n_written;
	}

	mutex_unlock(&vmbus_connection.channel_mutex);

	return tot_written;
}
static DEVICE_ATTR_RO(channel_vp_mapping);

static ssize_t vendor_show(struct device *dev,
			   struct device_attribute *dev_attr,
			   char *buf)
{
	struct hv_device *hv_dev = device_to_hv_device(dev);

	return sprintf(buf, "0x%x\n", hv_dev->vendor_id);
}
static DEVICE_ATTR_RO(vendor);

static ssize_t device_show(struct device *dev,
			   struct device_attribute *dev_attr,
			   char *buf)
{
	struct hv_device *hv_dev = device_to_hv_device(dev);

	return sprintf(buf, "0x%x\n", hv_dev->device_id);
}
static DEVICE_ATTR_RO(device);

static ssize_t driver_override_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct hv_device *hv_dev = device_to_hv_device(dev);
	char *driver_override, *old, *cp;

	/* We need to keep extra room for a newline */
	if (count >= (PAGE_SIZE - 1))
		return -EINVAL;

	driver_override = kstrndup(buf, count, GFP_KERNEL);
	if (!driver_override)
		return -ENOMEM;

	cp = strchr(driver_override, '\n');
	if (cp)
		*cp = '\0';

	device_lock(dev);
	old = hv_dev->driver_override;
	if (strlen(driver_override)) {
		hv_dev->driver_override = driver_override;
	} else {
		kfree(driver_override);
		hv_dev->driver_override = NULL;
	}
	device_unlock(dev);

	kfree(old);

	return count;
}

static ssize_t driver_override_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct hv_device *hv_dev = device_to_hv_device(dev);
	ssize_t len;

	device_lock(dev);
	len = snprintf(buf, PAGE_SIZE, "%s\n", hv_dev->driver_override);
	device_unlock(dev);

	return len;
}
static DEVICE_ATTR_RW(driver_override);

/* Set up per device attributes in /sys/bus/vmbus/devices/<bus device> */
static struct attribute *vmbus_dev_attrs[] = {
	&dev_attr_id.attr,
	&dev_attr_state.attr,
	&dev_attr_monitor_id.attr,
	&dev_attr_class_id.attr,
	&dev_attr_device_id.attr,
	&dev_attr_modalias.attr,
#ifdef CONFIG_NUMA
	&dev_attr_numa_node.attr,
#endif
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
	&dev_attr_channel_vp_mapping.attr,
	&dev_attr_vendor.attr,
	&dev_attr_device.attr,
	&dev_attr_driver_override.attr,
	NULL,
};

/*
 * Device-level attribute_group callback function. Returns the permission for
 * each attribute, and returns 0 if an attribute is not visible.
 */
static umode_t vmbus_dev_attr_is_visible(struct kobject *kobj,
					 struct attribute *attr, int idx)
{
	struct device *dev = kobj_to_dev(kobj);
	const struct hv_device *hv_dev = device_to_hv_device(dev);

	/* Hide the monitor attributes if the monitor mechanism is not used. */
	if (!hv_dev->channel->offermsg.monitor_allocated &&
	    (attr == &dev_attr_monitor_id.attr ||
	     attr == &dev_attr_server_monitor_pending.attr ||
	     attr == &dev_attr_client_monitor_pending.attr ||
	     attr == &dev_attr_server_monitor_latency.attr ||
	     attr == &dev_attr_client_monitor_latency.attr ||
	     attr == &dev_attr_server_monitor_conn_id.attr ||
	     attr == &dev_attr_client_monitor_conn_id.attr))
		return 0;

	return attr->mode;
}

static const struct attribute_group vmbus_dev_group = {
	.attrs = vmbus_dev_attrs,
	.is_visible = vmbus_dev_attr_is_visible
};
__ATTRIBUTE_GROUPS(vmbus_dev);

/* Set up the attribute for /sys/bus/vmbus/hibernation */
static ssize_t hibernation_show(struct bus_type *bus, char *buf)
{
	return sprintf(buf, "%d\n", !!hv_is_hibernation_supported());
}

static BUS_ATTR_RO(hibernation);

static struct attribute *vmbus_bus_attrs[] = {
	&bus_attr_hibernation.attr,
	NULL,
};
static const struct attribute_group vmbus_bus_group = {
	.attrs = vmbus_bus_attrs,
};
__ATTRIBUTE_GROUPS(vmbus_bus);

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
	const char *format = "MODALIAS=vmbus:%*phN";

	return add_uevent_var(env, format, UUID_SIZE, &dev->dev_type);
}

static const struct hv_vmbus_device_id *
hv_vmbus_dev_match(const struct hv_vmbus_device_id *id, const guid_t *guid)
{
	if (id == NULL)
		return NULL; /* empty device table */

	for (; !guid_is_null(&id->guid); id++)
		if (guid_equal(&id->guid, guid))
			return id;

	return NULL;
}

static const struct hv_vmbus_device_id *
hv_vmbus_dynid_match(struct hv_driver *drv, const guid_t *guid)
{
	const struct hv_vmbus_device_id *id = NULL;
	struct vmbus_dynid *dynid;

	spin_lock(&drv->dynids.lock);
	list_for_each_entry(dynid, &drv->dynids.list, node) {
		if (guid_equal(&dynid->id.guid, guid)) {
			id = &dynid->id;
			break;
		}
	}
	spin_unlock(&drv->dynids.lock);

	return id;
}

static const struct hv_vmbus_device_id vmbus_device_null;

/*
 * Return a matching hv_vmbus_device_id pointer.
 * If there is no match, return NULL.
 */
static const struct hv_vmbus_device_id *hv_vmbus_get_id(struct hv_driver *drv,
							struct hv_device *dev)
{
	const guid_t *guid = &dev->dev_type;
	const struct hv_vmbus_device_id *id;

	/* When driver_override is set, only bind to the matching driver */
	if (dev->driver_override && strcmp(dev->driver_override, drv->name))
		return NULL;

	/* Look at the dynamic ids first, before the static ones */
	id = hv_vmbus_dynid_match(drv, guid);
	if (!id)
		id = hv_vmbus_dev_match(drv->id_table, guid);

	/* driver_override will always match, send a dummy id */
	if (!id && dev->driver_override)
		id = &vmbus_device_null;

	return id;
}

/* vmbus_add_dynid - add a new device ID to this driver and re-probe devices */
static int vmbus_add_dynid(struct hv_driver *drv, guid_t *guid)
{
	struct vmbus_dynid *dynid;

	dynid = kzalloc(sizeof(*dynid), GFP_KERNEL);
	if (!dynid)
		return -ENOMEM;

	dynid->id.guid = *guid;

	spin_lock(&drv->dynids.lock);
	list_add_tail(&dynid->node, &drv->dynids.list);
	spin_unlock(&drv->dynids.lock);

	return driver_attach(&drv->driver);
}

static void vmbus_free_dynids(struct hv_driver *drv)
{
	struct vmbus_dynid *dynid, *n;

	spin_lock(&drv->dynids.lock);
	list_for_each_entry_safe(dynid, n, &drv->dynids.list, node) {
		list_del(&dynid->node);
		kfree(dynid);
	}
	spin_unlock(&drv->dynids.lock);
}

/*
 * store_new_id - sysfs frontend to vmbus_add_dynid()
 *
 * Allow GUIDs to be added to an existing driver via sysfs.
 */
static ssize_t new_id_store(struct device_driver *driver, const char *buf,
			    size_t count)
{
	struct hv_driver *drv = drv_to_hv_drv(driver);
	guid_t guid;
	ssize_t retval;

	retval = guid_parse(buf, &guid);
	if (retval)
		return retval;

	if (hv_vmbus_dynid_match(drv, &guid))
		return -EEXIST;

	retval = vmbus_add_dynid(drv, &guid);
	if (retval)
		return retval;
	return count;
}
static DRIVER_ATTR_WO(new_id);

/*
 * store_remove_id - remove a PCI device ID from this driver
 *
 * Removes a dynamic pci device ID to this driver.
 */
static ssize_t remove_id_store(struct device_driver *driver, const char *buf,
			       size_t count)
{
	struct hv_driver *drv = drv_to_hv_drv(driver);
	struct vmbus_dynid *dynid, *n;
	guid_t guid;
	ssize_t retval;

	retval = guid_parse(buf, &guid);
	if (retval)
		return retval;

	retval = -ENODEV;
	spin_lock(&drv->dynids.lock);
	list_for_each_entry_safe(dynid, n, &drv->dynids.list, node) {
		struct hv_vmbus_device_id *id = &dynid->id;

		if (guid_equal(&id->guid, &guid)) {
			list_del(&dynid->node);
			kfree(dynid);
			retval = count;
			break;
		}
	}
	spin_unlock(&drv->dynids.lock);

	return retval;
}
static DRIVER_ATTR_WO(remove_id);

static struct attribute *vmbus_drv_attrs[] = {
	&driver_attr_new_id.attr,
	&driver_attr_remove_id.attr,
	NULL,
};
ATTRIBUTE_GROUPS(vmbus_drv);


/*
 * vmbus_match - Attempt to match the specified device to the specified driver
 */
static int vmbus_match(struct device *device, struct device_driver *driver)
{
	struct hv_driver *drv = drv_to_hv_drv(driver);
	struct hv_device *hv_dev = device_to_hv_device(device);

	/* The hv_sock driver handles all hv_sock offers. */
	if (is_hvsock_channel(hv_dev->channel))
		return drv->hvsock;

	if (hv_vmbus_get_id(drv, hv_dev))
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

	dev_id = hv_vmbus_get_id(drv, dev);
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
static void vmbus_remove(struct device *child_device)
{
	struct hv_driver *drv;
	struct hv_device *dev = device_to_hv_device(child_device);

	if (child_device->driver) {
		drv = drv_to_hv_drv(child_device->driver);
		if (drv->remove)
			drv->remove(dev);
	}
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
}

#ifdef CONFIG_PM_SLEEP
/*
 * vmbus_suspend - Suspend a vmbus device
 */
static int vmbus_suspend(struct device *child_device)
{
	struct hv_driver *drv;
	struct hv_device *dev = device_to_hv_device(child_device);

	/* The device may not be attached yet */
	if (!child_device->driver)
		return 0;

	drv = drv_to_hv_drv(child_device->driver);
	if (!drv->suspend)
		return -EOPNOTSUPP;

	return drv->suspend(dev);
}

/*
 * vmbus_resume - Resume a vmbus device
 */
static int vmbus_resume(struct device *child_device)
{
	struct hv_driver *drv;
	struct hv_device *dev = device_to_hv_device(child_device);

	/* The device may not be attached yet */
	if (!child_device->driver)
		return 0;

	drv = drv_to_hv_drv(child_device->driver);
	if (!drv->resume)
		return -EOPNOTSUPP;

	return drv->resume(dev);
}
#else
#define vmbus_suspend NULL
#define vmbus_resume NULL
#endif /* CONFIG_PM_SLEEP */

/*
 * vmbus_device_release - Final callback release of the vmbus child device
 */
static void vmbus_device_release(struct device *device)
{
	struct hv_device *hv_dev = device_to_hv_device(device);
	struct vmbus_channel *channel = hv_dev->channel;

	hv_debug_rm_dev_dir(hv_dev);

	mutex_lock(&vmbus_connection.channel_mutex);
	hv_process_channel_removal(channel);
	mutex_unlock(&vmbus_connection.channel_mutex);
	kfree(hv_dev);
}

/*
 * Note: we must use the "noirq" ops: see the comment before vmbus_bus_pm.
 *
 * suspend_noirq/resume_noirq are set to NULL to support Suspend-to-Idle: we
 * shouldn't suspend the vmbus devices upon Suspend-to-Idle, otherwise there
 * is no way to wake up a Generation-2 VM.
 *
 * The other 4 ops are for hibernation.
 */

static const struct dev_pm_ops vmbus_pm = {
	.suspend_noirq	= NULL,
	.resume_noirq	= NULL,
	.freeze_noirq	= vmbus_suspend,
	.thaw_noirq	= vmbus_resume,
	.poweroff_noirq	= vmbus_suspend,
	.restore_noirq	= vmbus_resume,
};

/* The one and only one */
static struct bus_type  hv_bus = {
	.name =		"vmbus",
	.match =		vmbus_match,
	.shutdown =		vmbus_shutdown,
	.remove =		vmbus_remove,
	.probe =		vmbus_probe,
	.uevent =		vmbus_uevent,
	.dev_groups =		vmbus_dev_groups,
	.drv_groups =		vmbus_drv_groups,
	.bus_groups =		vmbus_bus_groups,
	.pm =			&vmbus_pm,
};

struct onmessage_work_context {
	struct work_struct work;
	struct {
		struct hv_message_header header;
		u8 payload[];
	} msg;
};

static void vmbus_onmessage_work(struct work_struct *work)
{
	struct onmessage_work_context *ctx;

	/* Do not process messages if we're in DISCONNECTED state */
	if (vmbus_connection.conn_state == DISCONNECTED)
		return;

	ctx = container_of(work, struct onmessage_work_context,
			   work);
	vmbus_onmessage((struct vmbus_channel_message_header *)
			&ctx->msg.payload);
	kfree(ctx);
}

void vmbus_on_msg_dpc(unsigned long data)
{
	struct hv_per_cpu_context *hv_cpu = (void *)data;
	void *page_addr = hv_cpu->synic_message_page;
	struct hv_message msg_copy, *msg = (struct hv_message *)page_addr +
				  VMBUS_MESSAGE_SINT;
	struct vmbus_channel_message_header *hdr;
	enum vmbus_channel_message_type msgtype;
	const struct vmbus_channel_message_table_entry *entry;
	struct onmessage_work_context *ctx;
	__u8 payload_size;
	u32 message_type;

	/*
	 * 'enum vmbus_channel_message_type' is supposed to always be 'u32' as
	 * it is being used in 'struct vmbus_channel_message_header' definition
	 * which is supposed to match hypervisor ABI.
	 */
	BUILD_BUG_ON(sizeof(enum vmbus_channel_message_type) != sizeof(u32));

	/*
	 * Since the message is in memory shared with the host, an erroneous or
	 * malicious Hyper-V could modify the message while vmbus_on_msg_dpc()
	 * or individual message handlers are executing; to prevent this, copy
	 * the message into private memory.
	 */
	memcpy(&msg_copy, msg, sizeof(struct hv_message));

	message_type = msg_copy.header.message_type;
	if (message_type == HVMSG_NONE)
		/* no msg */
		return;

	hdr = (struct vmbus_channel_message_header *)msg_copy.u.payload;
	msgtype = hdr->msgtype;

	trace_vmbus_on_msg_dpc(hdr);

	if (msgtype >= CHANNELMSG_COUNT) {
		WARN_ONCE(1, "unknown msgtype=%d\n", msgtype);
		goto msg_handled;
	}

	payload_size = msg_copy.header.payload_size;
	if (payload_size > HV_MESSAGE_PAYLOAD_BYTE_COUNT) {
		WARN_ONCE(1, "payload size is too large (%d)\n", payload_size);
		goto msg_handled;
	}

	entry = &channel_message_table[msgtype];

	if (!entry->message_handler)
		goto msg_handled;

	if (payload_size < entry->min_payload_len) {
		WARN_ONCE(1, "message too short: msgtype=%d len=%d\n", msgtype, payload_size);
		goto msg_handled;
	}

	if (entry->handler_type	== VMHT_BLOCKING) {
		ctx = kmalloc(sizeof(*ctx) + payload_size, GFP_ATOMIC);
		if (ctx == NULL)
			return;

		INIT_WORK(&ctx->work, vmbus_onmessage_work);
		memcpy(&ctx->msg, &msg_copy, sizeof(msg->header) + payload_size);

		/*
		 * The host can generate a rescind message while we
		 * may still be handling the original offer. We deal with
		 * this condition by relying on the synchronization provided
		 * by offer_in_progress and by channel_mutex.  See also the
		 * inline comments in vmbus_onoffer_rescind().
		 */
		switch (msgtype) {
		case CHANNELMSG_RESCIND_CHANNELOFFER:
			/*
			 * If we are handling the rescind message;
			 * schedule the work on the global work queue.
			 *
			 * The OFFER message and the RESCIND message should
			 * not be handled by the same serialized work queue,
			 * because the OFFER handler may call vmbus_open(),
			 * which tries to open the channel by sending an
			 * OPEN_CHANNEL message to the host and waits for
			 * the host's response; however, if the host has
			 * rescinded the channel before it receives the
			 * OPEN_CHANNEL message, the host just silently
			 * ignores the OPEN_CHANNEL message; as a result,
			 * the guest's OFFER handler hangs for ever, if we
			 * handle the RESCIND message in the same serialized
			 * work queue: the RESCIND handler can not start to
			 * run before the OFFER handler finishes.
			 */
			schedule_work(&ctx->work);
			break;

		case CHANNELMSG_OFFERCHANNEL:
			/*
			 * The host sends the offer message of a given channel
			 * before sending the rescind message of the same
			 * channel.  These messages are sent to the guest's
			 * connect CPU; the guest then starts processing them
			 * in the tasklet handler on this CPU:
			 *
			 * VMBUS_CONNECT_CPU
			 *
			 * [vmbus_on_msg_dpc()]
			 * atomic_inc()  // CHANNELMSG_OFFERCHANNEL
			 * queue_work()
			 * ...
			 * [vmbus_on_msg_dpc()]
			 * schedule_work()  // CHANNELMSG_RESCIND_CHANNELOFFER
			 *
			 * We rely on the memory-ordering properties of the
			 * queue_work() and schedule_work() primitives, which
			 * guarantee that the atomic increment will be visible
			 * to the CPUs which will execute the offer & rescind
			 * works by the time these works will start execution.
			 */
			atomic_inc(&vmbus_connection.offer_in_progress);
			fallthrough;

		default:
			queue_work(vmbus_connection.work_queue, &ctx->work);
		}
	} else
		entry->message_handler(hdr);

msg_handled:
	vmbus_signal_eom(msg, message_type);
}

#ifdef CONFIG_PM_SLEEP
/*
 * Fake RESCIND_CHANNEL messages to clean up hv_sock channels by force for
 * hibernation, because hv_sock connections can not persist across hibernation.
 */
static void vmbus_force_channel_rescinded(struct vmbus_channel *channel)
{
	struct onmessage_work_context *ctx;
	struct vmbus_channel_rescind_offer *rescind;

	WARN_ON(!is_hvsock_channel(channel));

	/*
	 * Allocation size is small and the allocation should really not fail,
	 * otherwise the state of the hv_sock connections ends up in limbo.
	 */
	ctx = kzalloc(sizeof(*ctx) + sizeof(*rescind),
		      GFP_KERNEL | __GFP_NOFAIL);

	/*
	 * So far, these are not really used by Linux. Just set them to the
	 * reasonable values conforming to the definitions of the fields.
	 */
	ctx->msg.header.message_type = 1;
	ctx->msg.header.payload_size = sizeof(*rescind);

	/* These values are actually used by Linux. */
	rescind = (struct vmbus_channel_rescind_offer *)ctx->msg.payload;
	rescind->header.msgtype = CHANNELMSG_RESCIND_CHANNELOFFER;
	rescind->child_relid = channel->offermsg.child_relid;

	INIT_WORK(&ctx->work, vmbus_onmessage_work);

	queue_work(vmbus_connection.work_queue, &ctx->work);
}
#endif /* CONFIG_PM_SLEEP */

/*
 * Schedule all channels with events pending
 */
static void vmbus_chan_sched(struct hv_per_cpu_context *hv_cpu)
{
	unsigned long *recv_int_page;
	u32 maxbits, relid;

	if (vmbus_proto_version < VERSION_WIN8) {
		maxbits = MAX_NUM_CHANNELS_SUPPORTED;
		recv_int_page = vmbus_connection.recv_int_page;
	} else {
		/*
		 * When the host is win8 and beyond, the event page
		 * can be directly checked to get the id of the channel
		 * that has the interrupt pending.
		 */
		void *page_addr = hv_cpu->synic_event_page;
		union hv_synic_event_flags *event
			= (union hv_synic_event_flags *)page_addr +
						 VMBUS_MESSAGE_SINT;

		maxbits = HV_EVENT_FLAGS_COUNT;
		recv_int_page = event->flags;
	}

	if (unlikely(!recv_int_page))
		return;

	for_each_set_bit(relid, recv_int_page, maxbits) {
		void (*callback_fn)(void *context);
		struct vmbus_channel *channel;

		if (!sync_test_and_clear_bit(relid, recv_int_page))
			continue;

		/* Special case - vmbus channel protocol msg */
		if (relid == 0)
			continue;

		/*
		 * Pairs with the kfree_rcu() in vmbus_chan_release().
		 * Guarantees that the channel data structure doesn't
		 * get freed while the channel pointer below is being
		 * dereferenced.
		 */
		rcu_read_lock();

		/* Find channel based on relid */
		channel = relid2channel(relid);
		if (channel == NULL)
			goto sched_unlock_rcu;

		if (channel->rescind)
			goto sched_unlock_rcu;

		/*
		 * Make sure that the ring buffer data structure doesn't get
		 * freed while we dereference the ring buffer pointer.  Test
		 * for the channel's onchannel_callback being NULL within a
		 * sched_lock critical section.  See also the inline comments
		 * in vmbus_reset_channel_cb().
		 */
		spin_lock(&channel->sched_lock);

		callback_fn = channel->onchannel_callback;
		if (unlikely(callback_fn == NULL))
			goto sched_unlock;

		trace_vmbus_chan_sched(channel);

		++channel->interrupts;

		switch (channel->callback_mode) {
		case HV_CALL_ISR:
			(*callback_fn)(channel->channel_callback_context);
			break;

		case HV_CALL_BATCHED:
			hv_begin_read(&channel->inbound);
			fallthrough;
		case HV_CALL_DIRECT:
			tasklet_schedule(&channel->callback_event);
		}

sched_unlock:
		spin_unlock(&channel->sched_lock);
sched_unlock_rcu:
		rcu_read_unlock();
	}
}

static void vmbus_isr(void)
{
	struct hv_per_cpu_context *hv_cpu
		= this_cpu_ptr(hv_context.cpu_context);
	void *page_addr = hv_cpu->synic_event_page;
	struct hv_message *msg;
	union hv_synic_event_flags *event;
	bool handled = false;

	if (unlikely(page_addr == NULL))
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
		if (sync_test_and_clear_bit(0, event->flags))
			handled = true;
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
		vmbus_chan_sched(hv_cpu);

	page_addr = hv_cpu->synic_message_page;
	msg = (struct hv_message *)page_addr + VMBUS_MESSAGE_SINT;

	/* Check if there are actual msgs to be processed */
	if (msg->header.message_type != HVMSG_NONE) {
		if (msg->header.message_type == HVMSG_TIMER_EXPIRED) {
			hv_stimer0_isr();
			vmbus_signal_eom(msg, HVMSG_TIMER_EXPIRED);
		} else
			tasklet_schedule(&hv_cpu->msg_dpc);
	}

	add_interrupt_randomness(vmbus_interrupt);
}

static irqreturn_t vmbus_percpu_isr(int irq, void *dev_id)
{
	vmbus_isr();
	return IRQ_HANDLED;
}

/*
 * Callback from kmsg_dump. Grab as much as possible from the end of the kmsg
 * buffer and call into Hyper-V to transfer the data.
 */
static void hv_kmsg_dump(struct kmsg_dumper *dumper,
			 enum kmsg_dump_reason reason)
{
	struct kmsg_dump_iter iter;
	size_t bytes_written;

	/* We are only interested in panics. */
	if ((reason != KMSG_DUMP_PANIC) || (!sysctl_record_panic_msg))
		return;

	/*
	 * Write dump contents to the page. No need to synchronize; panic should
	 * be single-threaded.
	 */
	kmsg_dump_rewind(&iter);
	kmsg_dump_get_buffer(&iter, false, hv_panic_page, HV_HYP_PAGE_SIZE,
			     &bytes_written);
	if (!bytes_written)
		return;
	/*
	 * P3 to contain the physical address of the panic page & P4 to
	 * contain the size of the panic data in that page. Rest of the
	 * registers are no-op when the NOTIFY_MSG flag is set.
	 */
	hv_set_register(HV_REGISTER_CRASH_P0, 0);
	hv_set_register(HV_REGISTER_CRASH_P1, 0);
	hv_set_register(HV_REGISTER_CRASH_P2, 0);
	hv_set_register(HV_REGISTER_CRASH_P3, virt_to_phys(hv_panic_page));
	hv_set_register(HV_REGISTER_CRASH_P4, bytes_written);

	/*
	 * Let Hyper-V know there is crash data available along with
	 * the panic message.
	 */
	hv_set_register(HV_REGISTER_CRASH_CTL,
	       (HV_CRASH_CTL_CRASH_NOTIFY | HV_CRASH_CTL_CRASH_NOTIFY_MSG));
}

static struct kmsg_dumper hv_kmsg_dumper = {
	.dump = hv_kmsg_dump,
};

static void hv_kmsg_dump_register(void)
{
	int ret;

	hv_panic_page = hv_alloc_hyperv_zeroed_page();
	if (!hv_panic_page) {
		pr_err("Hyper-V: panic message page memory allocation failed\n");
		return;
	}

	ret = kmsg_dump_register(&hv_kmsg_dumper);
	if (ret) {
		pr_err("Hyper-V: kmsg dump register error 0x%x\n", ret);
		hv_free_hyperv_page((unsigned long)hv_panic_page);
		hv_panic_page = NULL;
	}
}

static struct ctl_table_header *hv_ctl_table_hdr;

/*
 * sysctl option to allow the user to control whether kmsg data should be
 * reported to Hyper-V on panic.
 */
static struct ctl_table hv_ctl_table[] = {
	{
		.procname       = "hyperv_record_panic_msg",
		.data           = &sysctl_record_panic_msg,
		.maxlen         = sizeof(int),
		.mode           = 0644,
		.proc_handler   = proc_dointvec_minmax,
		.extra1		= SYSCTL_ZERO,
		.extra2		= SYSCTL_ONE
	},
	{}
};

static struct ctl_table hv_root_table[] = {
	{
		.procname	= "kernel",
		.mode		= 0555,
		.child		= hv_ctl_table
	},
	{}
};

/*
 * vmbus_bus_init -Main vmbus driver initialization routine.
 *
 * Here, we
 *	- initialize the vmbus driver context
 *	- invoke the vmbus hv main init routine
 *	- retrieve the channel offers
 */
static int vmbus_bus_init(void)
{
	int ret;

	ret = hv_init();
	if (ret != 0) {
		pr_err("Unable to initialize the hypervisor - 0x%x\n", ret);
		return ret;
	}

	ret = bus_register(&hv_bus);
	if (ret)
		return ret;

	/*
	 * VMbus interrupts are best modeled as per-cpu interrupts. If
	 * on an architecture with support for per-cpu IRQs (e.g. ARM64),
	 * allocate a per-cpu IRQ using standard Linux kernel functionality.
	 * If not on such an architecture (e.g., x86/x64), then rely on
	 * code in the arch-specific portion of the code tree to connect
	 * the VMbus interrupt handler.
	 */

	if (vmbus_irq == -1) {
		hv_setup_vmbus_handler(vmbus_isr);
	} else {
		vmbus_evt = alloc_percpu(long);
		ret = request_percpu_irq(vmbus_irq, vmbus_percpu_isr,
				"Hyper-V VMbus", vmbus_evt);
		if (ret) {
			pr_err("Can't request Hyper-V VMbus IRQ %d, Err %d",
					vmbus_irq, ret);
			free_percpu(vmbus_evt);
			goto err_setup;
		}
	}

	ret = hv_synic_alloc();
	if (ret)
		goto err_alloc;

	/*
	 * Initialize the per-cpu interrupt state and stimer state.
	 * Then connect to the host.
	 */
	ret = cpuhp_setup_state(CPUHP_AP_ONLINE_DYN, "hyperv/vmbus:online",
				hv_synic_init, hv_synic_cleanup);
	if (ret < 0)
		goto err_cpuhp;
	hyperv_cpuhp_online = ret;

	ret = vmbus_connect();
	if (ret)
		goto err_connect;

	/*
	 * Only register if the crash MSRs are available
	 */
	if (ms_hyperv.misc_features & HV_FEATURE_GUEST_CRASH_MSR_AVAILABLE) {
		u64 hyperv_crash_ctl;
		/*
		 * Sysctl registration is not fatal, since by default
		 * reporting is enabled.
		 */
		hv_ctl_table_hdr = register_sysctl_table(hv_root_table);
		if (!hv_ctl_table_hdr)
			pr_err("Hyper-V: sysctl table register error");

		/*
		 * Register for panic kmsg callback only if the right
		 * capability is supported by the hypervisor.
		 */
		hyperv_crash_ctl = hv_get_register(HV_REGISTER_CRASH_CTL);
		if (hyperv_crash_ctl & HV_CRASH_CTL_CRASH_NOTIFY_MSG)
			hv_kmsg_dump_register();

		register_die_notifier(&hyperv_die_block);
	}

	/*
	 * Always register the panic notifier because we need to unload
	 * the VMbus channel connection to prevent any VMbus
	 * activity after the VM panics.
	 */
	atomic_notifier_chain_register(&panic_notifier_list,
			       &hyperv_panic_block);

	vmbus_request_offers();

	return 0;

err_connect:
	cpuhp_remove_state(hyperv_cpuhp_online);
err_cpuhp:
	hv_synic_free();
err_alloc:
	if (vmbus_irq == -1) {
		hv_remove_vmbus_handler();
	} else {
		free_percpu_irq(vmbus_irq, vmbus_evt);
		free_percpu(vmbus_evt);
	}
err_setup:
	bus_unregister(&hv_bus);
	unregister_sysctl_table(hv_ctl_table_hdr);
	hv_ctl_table_hdr = NULL;
	return ret;
}

/**
 * __vmbus_child_driver_register() - Register a vmbus's driver
 * @hv_driver: Pointer to driver structure you want to register
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

	spin_lock_init(&hv_driver->dynids.lock);
	INIT_LIST_HEAD(&hv_driver->dynids.list);

	ret = driver_register(&hv_driver->driver);

	return ret;
}
EXPORT_SYMBOL_GPL(__vmbus_driver_register);

/**
 * vmbus_driver_unregister() - Unregister a vmbus's driver
 * @hv_driver: Pointer to driver structure you want to
 *             un-register
 *
 * Un-register the given driver that was previous registered with a call to
 * vmbus_driver_register()
 */
void vmbus_driver_unregister(struct hv_driver *hv_driver)
{
	pr_info("unregistering driver %s\n", hv_driver->name);

	if (!vmbus_exists()) {
		driver_unregister(&hv_driver->driver);
		vmbus_free_dynids(hv_driver);
	}
}
EXPORT_SYMBOL_GPL(vmbus_driver_unregister);


/*
 * Called when last reference to channel is gone.
 */
static void vmbus_chan_release(struct kobject *kobj)
{
	struct vmbus_channel *channel
		= container_of(kobj, struct vmbus_channel, kobj);

	kfree_rcu(channel, rcu);
}

struct vmbus_chan_attribute {
	struct attribute attr;
	ssize_t (*show)(struct vmbus_channel *chan, char *buf);
	ssize_t (*store)(struct vmbus_channel *chan,
			 const char *buf, size_t count);
};
#define VMBUS_CHAN_ATTR(_name, _mode, _show, _store) \
	struct vmbus_chan_attribute chan_attr_##_name \
		= __ATTR(_name, _mode, _show, _store)
#define VMBUS_CHAN_ATTR_RW(_name) \
	struct vmbus_chan_attribute chan_attr_##_name = __ATTR_RW(_name)
#define VMBUS_CHAN_ATTR_RO(_name) \
	struct vmbus_chan_attribute chan_attr_##_name = __ATTR_RO(_name)
#define VMBUS_CHAN_ATTR_WO(_name) \
	struct vmbus_chan_attribute chan_attr_##_name = __ATTR_WO(_name)

static ssize_t vmbus_chan_attr_show(struct kobject *kobj,
				    struct attribute *attr, char *buf)
{
	const struct vmbus_chan_attribute *attribute
		= container_of(attr, struct vmbus_chan_attribute, attr);
	struct vmbus_channel *chan
		= container_of(kobj, struct vmbus_channel, kobj);

	if (!attribute->show)
		return -EIO;

	return attribute->show(chan, buf);
}

static ssize_t vmbus_chan_attr_store(struct kobject *kobj,
				     struct attribute *attr, const char *buf,
				     size_t count)
{
	const struct vmbus_chan_attribute *attribute
		= container_of(attr, struct vmbus_chan_attribute, attr);
	struct vmbus_channel *chan
		= container_of(kobj, struct vmbus_channel, kobj);

	if (!attribute->store)
		return -EIO;

	return attribute->store(chan, buf, count);
}

static const struct sysfs_ops vmbus_chan_sysfs_ops = {
	.show = vmbus_chan_attr_show,
	.store = vmbus_chan_attr_store,
};

static ssize_t out_mask_show(struct vmbus_channel *channel, char *buf)
{
	struct hv_ring_buffer_info *rbi = &channel->outbound;
	ssize_t ret;

	mutex_lock(&rbi->ring_buffer_mutex);
	if (!rbi->ring_buffer) {
		mutex_unlock(&rbi->ring_buffer_mutex);
		return -EINVAL;
	}

	ret = sprintf(buf, "%u\n", rbi->ring_buffer->interrupt_mask);
	mutex_unlock(&rbi->ring_buffer_mutex);
	return ret;
}
static VMBUS_CHAN_ATTR_RO(out_mask);

static ssize_t in_mask_show(struct vmbus_channel *channel, char *buf)
{
	struct hv_ring_buffer_info *rbi = &channel->inbound;
	ssize_t ret;

	mutex_lock(&rbi->ring_buffer_mutex);
	if (!rbi->ring_buffer) {
		mutex_unlock(&rbi->ring_buffer_mutex);
		return -EINVAL;
	}

	ret = sprintf(buf, "%u\n", rbi->ring_buffer->interrupt_mask);
	mutex_unlock(&rbi->ring_buffer_mutex);
	return ret;
}
static VMBUS_CHAN_ATTR_RO(in_mask);

static ssize_t read_avail_show(struct vmbus_channel *channel, char *buf)
{
	struct hv_ring_buffer_info *rbi = &channel->inbound;
	ssize_t ret;

	mutex_lock(&rbi->ring_buffer_mutex);
	if (!rbi->ring_buffer) {
		mutex_unlock(&rbi->ring_buffer_mutex);
		return -EINVAL;
	}

	ret = sprintf(buf, "%u\n", hv_get_bytes_to_read(rbi));
	mutex_unlock(&rbi->ring_buffer_mutex);
	return ret;
}
static VMBUS_CHAN_ATTR_RO(read_avail);

static ssize_t write_avail_show(struct vmbus_channel *channel, char *buf)
{
	struct hv_ring_buffer_info *rbi = &channel->outbound;
	ssize_t ret;

	mutex_lock(&rbi->ring_buffer_mutex);
	if (!rbi->ring_buffer) {
		mutex_unlock(&rbi->ring_buffer_mutex);
		return -EINVAL;
	}

	ret = sprintf(buf, "%u\n", hv_get_bytes_to_write(rbi));
	mutex_unlock(&rbi->ring_buffer_mutex);
	return ret;
}
static VMBUS_CHAN_ATTR_RO(write_avail);

static ssize_t target_cpu_show(struct vmbus_channel *channel, char *buf)
{
	return sprintf(buf, "%u\n", channel->target_cpu);
}
static ssize_t target_cpu_store(struct vmbus_channel *channel,
				const char *buf, size_t count)
{
	u32 target_cpu, origin_cpu;
	ssize_t ret = count;

	if (vmbus_proto_version < VERSION_WIN10_V4_1)
		return -EIO;

	if (sscanf(buf, "%uu", &target_cpu) != 1)
		return -EIO;

	/* Validate target_cpu for the cpumask_test_cpu() operation below. */
	if (target_cpu >= nr_cpumask_bits)
		return -EINVAL;

	/* No CPUs should come up or down during this. */
	cpus_read_lock();

	if (!cpu_online(target_cpu)) {
		cpus_read_unlock();
		return -EINVAL;
	}

	/*
	 * Synchronizes target_cpu_store() and channel closure:
	 *
	 * { Initially: state = CHANNEL_OPENED }
	 *
	 * CPU1				CPU2
	 *
	 * [target_cpu_store()]		[vmbus_disconnect_ring()]
	 *
	 * LOCK channel_mutex		LOCK channel_mutex
	 * LOAD r1 = state		LOAD r2 = state
	 * IF (r1 == CHANNEL_OPENED)	IF (r2 == CHANNEL_OPENED)
	 *   SEND MODIFYCHANNEL		  STORE state = CHANNEL_OPEN
	 *   [...]			  SEND CLOSECHANNEL
	 * UNLOCK channel_mutex		UNLOCK channel_mutex
	 *
	 * Forbids: r1 == r2 == CHANNEL_OPENED (i.e., CPU1's LOCK precedes
	 * 		CPU2's LOCK) && CPU2's SEND precedes CPU1's SEND
	 *
	 * Note.  The host processes the channel messages "sequentially", in
	 * the order in which they are received on a per-partition basis.
	 */
	mutex_lock(&vmbus_connection.channel_mutex);

	/*
	 * Hyper-V will ignore MODIFYCHANNEL messages for "non-open" channels;
	 * avoid sending the message and fail here for such channels.
	 */
	if (channel->state != CHANNEL_OPENED_STATE) {
		ret = -EIO;
		goto cpu_store_unlock;
	}

	origin_cpu = channel->target_cpu;
	if (target_cpu == origin_cpu)
		goto cpu_store_unlock;

	if (vmbus_send_modifychannel(channel,
				     hv_cpu_number_to_vp_number(target_cpu))) {
		ret = -EIO;
		goto cpu_store_unlock;
	}

	/*
	 * For version before VERSION_WIN10_V5_3, the following warning holds:
	 *
	 * Warning.  At this point, there is *no* guarantee that the host will
	 * have successfully processed the vmbus_send_modifychannel() request.
	 * See the header comment of vmbus_send_modifychannel() for more info.
	 *
	 * Lags in the processing of the above vmbus_send_modifychannel() can
	 * result in missed interrupts if the "old" target CPU is taken offline
	 * before Hyper-V starts sending interrupts to the "new" target CPU.
	 * But apart from this offlining scenario, the code tolerates such
	 * lags.  It will function correctly even if a channel interrupt comes
	 * in on a CPU that is different from the channel target_cpu value.
	 */

	channel->target_cpu = target_cpu;

	/* See init_vp_index(). */
	if (hv_is_perf_channel(channel))
		hv_update_alloced_cpus(origin_cpu, target_cpu);

	/* Currently set only for storvsc channels. */
	if (channel->change_target_cpu_callback) {
		(*channel->change_target_cpu_callback)(channel,
				origin_cpu, target_cpu);
	}

cpu_store_unlock:
	mutex_unlock(&vmbus_connection.channel_mutex);
	cpus_read_unlock();
	return ret;
}
static VMBUS_CHAN_ATTR(cpu, 0644, target_cpu_show, target_cpu_store);

static ssize_t channel_pending_show(struct vmbus_channel *channel,
				    char *buf)
{
	return sprintf(buf, "%d\n",
		       channel_pending(channel,
				       vmbus_connection.monitor_pages[1]));
}
static VMBUS_CHAN_ATTR(pending, 0444, channel_pending_show, NULL);

static ssize_t channel_latency_show(struct vmbus_channel *channel,
				    char *buf)
{
	return sprintf(buf, "%d\n",
		       channel_latency(channel,
				       vmbus_connection.monitor_pages[1]));
}
static VMBUS_CHAN_ATTR(latency, 0444, channel_latency_show, NULL);

static ssize_t channel_interrupts_show(struct vmbus_channel *channel, char *buf)
{
	return sprintf(buf, "%llu\n", channel->interrupts);
}
static VMBUS_CHAN_ATTR(interrupts, 0444, channel_interrupts_show, NULL);

static ssize_t channel_events_show(struct vmbus_channel *channel, char *buf)
{
	return sprintf(buf, "%llu\n", channel->sig_events);
}
static VMBUS_CHAN_ATTR(events, 0444, channel_events_show, NULL);

static ssize_t channel_intr_in_full_show(struct vmbus_channel *channel,
					 char *buf)
{
	return sprintf(buf, "%llu\n",
		       (unsigned long long)channel->intr_in_full);
}
static VMBUS_CHAN_ATTR(intr_in_full, 0444, channel_intr_in_full_show, NULL);

static ssize_t channel_intr_out_empty_show(struct vmbus_channel *channel,
					   char *buf)
{
	return sprintf(buf, "%llu\n",
		       (unsigned long long)channel->intr_out_empty);
}
static VMBUS_CHAN_ATTR(intr_out_empty, 0444, channel_intr_out_empty_show, NULL);

static ssize_t channel_out_full_first_show(struct vmbus_channel *channel,
					   char *buf)
{
	return sprintf(buf, "%llu\n",
		       (unsigned long long)channel->out_full_first);
}
static VMBUS_CHAN_ATTR(out_full_first, 0444, channel_out_full_first_show, NULL);

static ssize_t channel_out_full_total_show(struct vmbus_channel *channel,
					   char *buf)
{
	return sprintf(buf, "%llu\n",
		       (unsigned long long)channel->out_full_total);
}
static VMBUS_CHAN_ATTR(out_full_total, 0444, channel_out_full_total_show, NULL);

static ssize_t subchannel_monitor_id_show(struct vmbus_channel *channel,
					  char *buf)
{
	return sprintf(buf, "%u\n", channel->offermsg.monitorid);
}
static VMBUS_CHAN_ATTR(monitor_id, 0444, subchannel_monitor_id_show, NULL);

static ssize_t subchannel_id_show(struct vmbus_channel *channel,
				  char *buf)
{
	return sprintf(buf, "%u\n",
		       channel->offermsg.offer.sub_channel_index);
}
static VMBUS_CHAN_ATTR_RO(subchannel_id);

static struct attribute *vmbus_chan_attrs[] = {
	&chan_attr_out_mask.attr,
	&chan_attr_in_mask.attr,
	&chan_attr_read_avail.attr,
	&chan_attr_write_avail.attr,
	&chan_attr_cpu.attr,
	&chan_attr_pending.attr,
	&chan_attr_latency.attr,
	&chan_attr_interrupts.attr,
	&chan_attr_events.attr,
	&chan_attr_intr_in_full.attr,
	&chan_attr_intr_out_empty.attr,
	&chan_attr_out_full_first.attr,
	&chan_attr_out_full_total.attr,
	&chan_attr_monitor_id.attr,
	&chan_attr_subchannel_id.attr,
	NULL
};

/*
 * Channel-level attribute_group callback function. Returns the permission for
 * each attribute, and returns 0 if an attribute is not visible.
 */
static umode_t vmbus_chan_attr_is_visible(struct kobject *kobj,
					  struct attribute *attr, int idx)
{
	const struct vmbus_channel *channel =
		container_of(kobj, struct vmbus_channel, kobj);

	/* Hide the monitor attributes if the monitor mechanism is not used. */
	if (!channel->offermsg.monitor_allocated &&
	    (attr == &chan_attr_pending.attr ||
	     attr == &chan_attr_latency.attr ||
	     attr == &chan_attr_monitor_id.attr))
		return 0;

	return attr->mode;
}

static struct attribute_group vmbus_chan_group = {
	.attrs = vmbus_chan_attrs,
	.is_visible = vmbus_chan_attr_is_visible
};

static struct kobj_type vmbus_chan_ktype = {
	.sysfs_ops = &vmbus_chan_sysfs_ops,
	.release = vmbus_chan_release,
};

/*
 * vmbus_add_channel_kobj - setup a sub-directory under device/channels
 */
int vmbus_add_channel_kobj(struct hv_device *dev, struct vmbus_channel *channel)
{
	const struct device *device = &dev->device;
	struct kobject *kobj = &channel->kobj;
	u32 relid = channel->offermsg.child_relid;
	int ret;

	kobj->kset = dev->channels_kset;
	ret = kobject_init_and_add(kobj, &vmbus_chan_ktype, NULL,
				   "%u", relid);
	if (ret)
		return ret;

	ret = sysfs_create_group(kobj, &vmbus_chan_group);

	if (ret) {
		/*
		 * The calling functions' error handling paths will cleanup the
		 * empty channel directory.
		 */
		dev_err(device, "Unable to set up channel sysfs files\n");
		return ret;
	}

	kobject_uevent(kobj, KOBJ_ADD);

	return 0;
}

/*
 * vmbus_remove_channel_attr_group - remove the channel's attribute group
 */
void vmbus_remove_channel_attr_group(struct vmbus_channel *channel)
{
	sysfs_remove_group(&channel->kobj, &vmbus_chan_group);
}

/*
 * vmbus_device_create - Creates and registers a new child device
 * on the vmbus.
 */
struct hv_device *vmbus_device_create(const guid_t *type,
				      const guid_t *instance,
				      struct vmbus_channel *channel)
{
	struct hv_device *child_device_obj;

	child_device_obj = kzalloc(sizeof(struct hv_device), GFP_KERNEL);
	if (!child_device_obj) {
		pr_err("Unable to allocate device object for child device\n");
		return NULL;
	}

	child_device_obj->channel = channel;
	guid_copy(&child_device_obj->dev_type, type);
	guid_copy(&child_device_obj->dev_instance, instance);
	child_device_obj->vendor_id = 0x1414; /* MSFT vendor ID */

	return child_device_obj;
}

static u64 vmbus_dma_mask = DMA_BIT_MASK(64);
/*
 * vmbus_device_register - Register the child device
 */
int vmbus_device_register(struct hv_device *child_device_obj)
{
	struct kobject *kobj = &child_device_obj->device.kobj;
	int ret;

	dev_set_name(&child_device_obj->device, "%pUl",
		     &child_device_obj->channel->offermsg.offer.if_instance);

	child_device_obj->device.bus = &hv_bus;
	child_device_obj->device.parent = &hv_acpi_dev->dev;
	child_device_obj->device.release = vmbus_device_release;

	/*
	 * Register with the LDM. This will kick off the driver/device
	 * binding...which will eventually call vmbus_match() and vmbus_probe()
	 */
	ret = device_register(&child_device_obj->device);
	if (ret) {
		pr_err("Unable to register child device\n");
		return ret;
	}

	child_device_obj->channels_kset = kset_create_and_add("channels",
							      NULL, kobj);
	if (!child_device_obj->channels_kset) {
		ret = -ENOMEM;
		goto err_dev_unregister;
	}

	ret = vmbus_add_channel_kobj(child_device_obj,
				     child_device_obj->channel);
	if (ret) {
		pr_err("Unable to register primary channeln");
		goto err_kset_unregister;
	}
	hv_debug_add_dev_dir(child_device_obj);

	child_device_obj->device.dma_mask = &vmbus_dma_mask;
	child_device_obj->device.dma_parms = &child_device_obj->dma_parms;
	return 0;

err_kset_unregister:
	kset_unregister(child_device_obj->channels_kset);

err_dev_unregister:
	device_unregister(&child_device_obj->device);
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

	kset_unregister(device_obj->channels_kset);

	/*
	 * Kick off the process of unregistering the device.
	 * This will call vmbus_remove() and eventually vmbus_device_release()
	 */
	device_unregister(&device_obj->device);
}


/*
 * VMBUS is an acpi enumerated device. Get the information we
 * need from DSDT.
 */
#define VTPM_BASE_ADDRESS 0xfed40000
static acpi_status vmbus_walk_resources(struct acpi_resource *res, void *ctx)
{
	resource_size_t start = 0;
	resource_size_t end = 0;
	struct resource *new_res;
	struct resource **old_res = &hyperv_mmio;
	struct resource **prev_res = NULL;
	struct resource r;

	switch (res->type) {

	/*
	 * "Address" descriptors are for bus windows. Ignore
	 * "memory" descriptors, which are for registers on
	 * devices.
	 */
	case ACPI_RESOURCE_TYPE_ADDRESS32:
		start = res->data.address32.address.minimum;
		end = res->data.address32.address.maximum;
		break;

	case ACPI_RESOURCE_TYPE_ADDRESS64:
		start = res->data.address64.address.minimum;
		end = res->data.address64.address.maximum;
		break;

	/*
	 * The IRQ information is needed only on ARM64, which Hyper-V
	 * sets up in the extended format. IRQ information is present
	 * on x86/x64 in the non-extended format but it is not used by
	 * Linux. So don't bother checking for the non-extended format.
	 */
	case ACPI_RESOURCE_TYPE_EXTENDED_IRQ:
		if (!acpi_dev_resource_interrupt(res, 0, &r)) {
			pr_err("Unable to parse Hyper-V ACPI interrupt\n");
			return AE_ERROR;
		}
		/* ARM64 INTID for VMbus */
		vmbus_interrupt = res->data.extended_irq.interrupts[0];
		/* Linux IRQ number */
		vmbus_irq = r.start;
		return AE_OK;

	default:
		/* Unused resource type */
		return AE_OK;

	}
	/*
	 * Ignore ranges that are below 1MB, as they're not
	 * necessary or useful here.
	 */
	if (end < 0x100000)
		return AE_OK;

	new_res = kzalloc(sizeof(*new_res), GFP_ATOMIC);
	if (!new_res)
		return AE_NO_MEMORY;

	/* If this range overlaps the virtual TPM, truncate it. */
	if (end > VTPM_BASE_ADDRESS && start < VTPM_BASE_ADDRESS)
		end = VTPM_BASE_ADDRESS;

	new_res->name = "hyperv mmio";
	new_res->flags = IORESOURCE_MEM;
	new_res->start = start;
	new_res->end = end;

	/*
	 * If two ranges are adjacent, merge them.
	 */
	do {
		if (!*old_res) {
			*old_res = new_res;
			break;
		}

		if (((*old_res)->end + 1) == new_res->start) {
			(*old_res)->end = new_res->end;
			kfree(new_res);
			break;
		}

		if ((*old_res)->start == new_res->end + 1) {
			(*old_res)->start = new_res->start;
			kfree(new_res);
			break;
		}

		if ((*old_res)->start > new_res->end) {
			new_res->sibling = *old_res;
			if (prev_res)
				(*prev_res)->sibling = new_res;
			*old_res = new_res;
			break;
		}

		prev_res = old_res;
		old_res = &(*old_res)->sibling;

	} while (1);

	return AE_OK;
}

static int vmbus_acpi_remove(struct acpi_device *device)
{
	struct resource *cur_res;
	struct resource *next_res;

	if (hyperv_mmio) {
		if (fb_mmio) {
			__release_region(hyperv_mmio, fb_mmio->start,
					 resource_size(fb_mmio));
			fb_mmio = NULL;
		}

		for (cur_res = hyperv_mmio; cur_res; cur_res = next_res) {
			next_res = cur_res->sibling;
			kfree(cur_res);
		}
	}

	return 0;
}

static void vmbus_reserve_fb(void)
{
	int size;
	/*
	 * Make a claim for the frame buffer in the resource tree under the
	 * first node, which will be the one below 4GB.  The length seems to
	 * be underreported, particularly in a Generation 1 VM.  So start out
	 * reserving a larger area and make it smaller until it succeeds.
	 */

	if (screen_info.lfb_base) {
		if (efi_enabled(EFI_BOOT))
			size = max_t(__u32, screen_info.lfb_size, 0x800000);
		else
			size = max_t(__u32, screen_info.lfb_size, 0x4000000);

		for (; !fb_mmio && (size >= 0x100000); size >>= 1) {
			fb_mmio = __request_region(hyperv_mmio,
						   screen_info.lfb_base, size,
						   fb_mmio_name, 0);
		}
	}
}

/**
 * vmbus_allocate_mmio() - Pick a memory-mapped I/O range.
 * @new:		If successful, supplied a pointer to the
 *			allocated MMIO space.
 * @device_obj:		Identifies the caller
 * @min:		Minimum guest physical address of the
 *			allocation
 * @max:		Maximum guest physical address
 * @size:		Size of the range to be allocated
 * @align:		Alignment of the range to be allocated
 * @fb_overlap_ok:	Whether this allocation can be allowed
 *			to overlap the video frame buffer.
 *
 * This function walks the resources granted to VMBus by the
 * _CRS object in the ACPI namespace underneath the parent
 * "bridge" whether that's a root PCI bus in the Generation 1
 * case or a Module Device in the Generation 2 case.  It then
 * attempts to allocate from the global MMIO pool in a way that
 * matches the constraints supplied in these parameters and by
 * that _CRS.
 *
 * Return: 0 on success, -errno on failure
 */
int vmbus_allocate_mmio(struct resource **new, struct hv_device *device_obj,
			resource_size_t min, resource_size_t max,
			resource_size_t size, resource_size_t align,
			bool fb_overlap_ok)
{
	struct resource *iter, *shadow;
	resource_size_t range_min, range_max, start;
	const char *dev_n = dev_name(&device_obj->device);
	int retval;

	retval = -ENXIO;
	mutex_lock(&hyperv_mmio_lock);

	/*
	 * If overlaps with frame buffers are allowed, then first attempt to
	 * make the allocation from within the reserved region.  Because it
	 * is already reserved, no shadow allocation is necessary.
	 */
	if (fb_overlap_ok && fb_mmio && !(min > fb_mmio->end) &&
	    !(max < fb_mmio->start)) {

		range_min = fb_mmio->start;
		range_max = fb_mmio->end;
		start = (range_min + align - 1) & ~(align - 1);
		for (; start + size - 1 <= range_max; start += align) {
			*new = request_mem_region_exclusive(start, size, dev_n);
			if (*new) {
				retval = 0;
				goto exit;
			}
		}
	}

	for (iter = hyperv_mmio; iter; iter = iter->sibling) {
		if ((iter->start >= max) || (iter->end <= min))
			continue;

		range_min = iter->start;
		range_max = iter->end;
		start = (range_min + align - 1) & ~(align - 1);
		for (; start + size - 1 <= range_max; start += align) {
			shadow = __request_region(iter, start, size, NULL,
						  IORESOURCE_BUSY);
			if (!shadow)
				continue;

			*new = request_mem_region_exclusive(start, size, dev_n);
			if (*new) {
				shadow->name = (char *)*new;
				retval = 0;
				goto exit;
			}

			__release_region(iter, start, size);
		}
	}

exit:
	mutex_unlock(&hyperv_mmio_lock);
	return retval;
}
EXPORT_SYMBOL_GPL(vmbus_allocate_mmio);

/**
 * vmbus_free_mmio() - Free a memory-mapped I/O range.
 * @start:		Base address of region to release.
 * @size:		Size of the range to be allocated
 *
 * This function releases anything requested by
 * vmbus_mmio_allocate().
 */
void vmbus_free_mmio(resource_size_t start, resource_size_t size)
{
	struct resource *iter;

	mutex_lock(&hyperv_mmio_lock);
	for (iter = hyperv_mmio; iter; iter = iter->sibling) {
		if ((iter->start >= start + size) || (iter->end <= start))
			continue;

		__release_region(iter, start, size);
	}
	release_mem_region(start, size);
	mutex_unlock(&hyperv_mmio_lock);

}
EXPORT_SYMBOL_GPL(vmbus_free_mmio);

static int vmbus_acpi_add(struct acpi_device *device)
{
	acpi_status result;
	int ret_val = -ENODEV;
	struct acpi_device *ancestor;

	hv_acpi_dev = device;

	result = acpi_walk_resources(device->handle, METHOD_NAME__CRS,
					vmbus_walk_resources, NULL);

	if (ACPI_FAILURE(result))
		goto acpi_walk_err;
	/*
	 * Some ancestor of the vmbus acpi device (Gen1 or Gen2
	 * firmware) is the VMOD that has the mmio ranges. Get that.
	 */
	for (ancestor = device->parent; ancestor; ancestor = ancestor->parent) {
		result = acpi_walk_resources(ancestor->handle, METHOD_NAME__CRS,
					     vmbus_walk_resources, NULL);

		if (ACPI_FAILURE(result))
			continue;
		if (hyperv_mmio) {
			vmbus_reserve_fb();
			break;
		}
	}
	ret_val = 0;

acpi_walk_err:
	complete(&probe_event);
	if (ret_val)
		vmbus_acpi_remove(device);
	return ret_val;
}

#ifdef CONFIG_PM_SLEEP
static int vmbus_bus_suspend(struct device *dev)
{
	struct vmbus_channel *channel, *sc;

	while (atomic_read(&vmbus_connection.offer_in_progress) != 0) {
		/*
		 * We wait here until the completion of any channel
		 * offers that are currently in progress.
		 */
		usleep_range(1000, 2000);
	}

	mutex_lock(&vmbus_connection.channel_mutex);
	list_for_each_entry(channel, &vmbus_connection.chn_list, listentry) {
		if (!is_hvsock_channel(channel))
			continue;

		vmbus_force_channel_rescinded(channel);
	}
	mutex_unlock(&vmbus_connection.channel_mutex);

	/*
	 * Wait until all the sub-channels and hv_sock channels have been
	 * cleaned up. Sub-channels should be destroyed upon suspend, otherwise
	 * they would conflict with the new sub-channels that will be created
	 * in the resume path. hv_sock channels should also be destroyed, but
	 * a hv_sock channel of an established hv_sock connection can not be
	 * really destroyed since it may still be referenced by the userspace
	 * application, so we just force the hv_sock channel to be rescinded
	 * by vmbus_force_channel_rescinded(), and the userspace application
	 * will thoroughly destroy the channel after hibernation.
	 *
	 * Note: the counter nr_chan_close_on_suspend may never go above 0 if
	 * the VM has no sub-channel and hv_sock channel, e.g. a 1-vCPU VM.
	 */
	if (atomic_read(&vmbus_connection.nr_chan_close_on_suspend) > 0)
		wait_for_completion(&vmbus_connection.ready_for_suspend_event);

	if (atomic_read(&vmbus_connection.nr_chan_fixup_on_resume) != 0) {
		pr_err("Can not suspend due to a previous failed resuming\n");
		return -EBUSY;
	}

	mutex_lock(&vmbus_connection.channel_mutex);

	list_for_each_entry(channel, &vmbus_connection.chn_list, listentry) {
		/*
		 * Remove the channel from the array of channels and invalidate
		 * the channel's relid.  Upon resume, vmbus_onoffer() will fix
		 * up the relid (and other fields, if necessary) and add the
		 * channel back to the array.
		 */
		vmbus_channel_unmap_relid(channel);
		channel->offermsg.child_relid = INVALID_RELID;

		if (is_hvsock_channel(channel)) {
			if (!channel->rescind) {
				pr_err("hv_sock channel not rescinded!\n");
				WARN_ON_ONCE(1);
			}
			continue;
		}

		list_for_each_entry(sc, &channel->sc_list, sc_list) {
			pr_err("Sub-channel not deleted!\n");
			WARN_ON_ONCE(1);
		}

		atomic_inc(&vmbus_connection.nr_chan_fixup_on_resume);
	}

	mutex_unlock(&vmbus_connection.channel_mutex);

	vmbus_initiate_unload(false);

	/* Reset the event for the next resume. */
	reinit_completion(&vmbus_connection.ready_for_resume_event);

	return 0;
}

static int vmbus_bus_resume(struct device *dev)
{
	struct vmbus_channel_msginfo *msginfo;
	size_t msgsize;
	int ret;

	/*
	 * We only use the 'vmbus_proto_version', which was in use before
	 * hibernation, to re-negotiate with the host.
	 */
	if (!vmbus_proto_version) {
		pr_err("Invalid proto version = 0x%x\n", vmbus_proto_version);
		return -EINVAL;
	}

	msgsize = sizeof(*msginfo) +
		  sizeof(struct vmbus_channel_initiate_contact);

	msginfo = kzalloc(msgsize, GFP_KERNEL);

	if (msginfo == NULL)
		return -ENOMEM;

	ret = vmbus_negotiate_version(msginfo, vmbus_proto_version);

	kfree(msginfo);

	if (ret != 0)
		return ret;

	WARN_ON(atomic_read(&vmbus_connection.nr_chan_fixup_on_resume) == 0);

	vmbus_request_offers();

	if (wait_for_completion_timeout(
		&vmbus_connection.ready_for_resume_event, 10 * HZ) == 0)
		pr_err("Some vmbus device is missing after suspending?\n");

	/* Reset the event for the next suspend. */
	reinit_completion(&vmbus_connection.ready_for_suspend_event);

	return 0;
}
#else
#define vmbus_bus_suspend NULL
#define vmbus_bus_resume NULL
#endif /* CONFIG_PM_SLEEP */

static const struct acpi_device_id vmbus_acpi_device_ids[] = {
	{"VMBUS", 0},
	{"VMBus", 0},
	{"", 0},
};
MODULE_DEVICE_TABLE(acpi, vmbus_acpi_device_ids);

/*
 * Note: we must use the "no_irq" ops, otherwise hibernation can not work with
 * PCI device assignment, because "pci_dev_pm_ops" uses the "noirq" ops: in
 * the resume path, the pci "noirq" restore op runs before "non-noirq" op (see
 * resume_target_kernel() -> dpm_resume_start(), and hibernation_restore() ->
 * dpm_resume_end()). This means vmbus_bus_resume() and the pci-hyperv's
 * resume callback must also run via the "noirq" ops.
 *
 * Set suspend_noirq/resume_noirq to NULL for Suspend-to-Idle: see the comment
 * earlier in this file before vmbus_pm.
 */

static const struct dev_pm_ops vmbus_bus_pm = {
	.suspend_noirq	= NULL,
	.resume_noirq	= NULL,
	.freeze_noirq	= vmbus_bus_suspend,
	.thaw_noirq	= vmbus_bus_resume,
	.poweroff_noirq	= vmbus_bus_suspend,
	.restore_noirq	= vmbus_bus_resume
};

static struct acpi_driver vmbus_acpi_driver = {
	.name = "vmbus",
	.ids = vmbus_acpi_device_ids,
	.ops = {
		.add = vmbus_acpi_add,
		.remove = vmbus_acpi_remove,
	},
	.drv.pm = &vmbus_bus_pm,
};

static void hv_kexec_handler(void)
{
	hv_stimer_global_cleanup();
	vmbus_initiate_unload(false);
	/* Make sure conn_state is set as hv_synic_cleanup checks for it */
	mb();
	cpuhp_remove_state(hyperv_cpuhp_online);
};

static void hv_crash_handler(struct pt_regs *regs)
{
	int cpu;

	vmbus_initiate_unload(true);
	/*
	 * In crash handler we can't schedule synic cleanup for all CPUs,
	 * doing the cleanup for current CPU only. This should be sufficient
	 * for kdump.
	 */
	cpu = smp_processor_id();
	hv_stimer_cleanup(cpu);
	hv_synic_disable_regs(cpu);
};

static int hv_synic_suspend(void)
{
	/*
	 * When we reach here, all the non-boot CPUs have been offlined.
	 * If we're in a legacy configuration where stimer Direct Mode is
	 * not enabled, the stimers on the non-boot CPUs have been unbound
	 * in hv_synic_cleanup() -> hv_stimer_legacy_cleanup() ->
	 * hv_stimer_cleanup() -> clockevents_unbind_device().
	 *
	 * hv_synic_suspend() only runs on CPU0 with interrupts disabled.
	 * Here we do not call hv_stimer_legacy_cleanup() on CPU0 because:
	 * 1) it's unnecessary as interrupts remain disabled between
	 * syscore_suspend() and syscore_resume(): see create_image() and
	 * resume_target_kernel()
	 * 2) the stimer on CPU0 is automatically disabled later by
	 * syscore_suspend() -> timekeeping_suspend() -> tick_suspend() -> ...
	 * -> clockevents_shutdown() -> ... -> hv_ce_shutdown()
	 * 3) a warning would be triggered if we call
	 * clockevents_unbind_device(), which may sleep, in an
	 * interrupts-disabled context.
	 */

	hv_synic_disable_regs(0);

	return 0;
}

static void hv_synic_resume(void)
{
	hv_synic_enable_regs(0);

	/*
	 * Note: we don't need to call hv_stimer_init(0), because the timer
	 * on CPU0 is not unbound in hv_synic_suspend(), and the timer is
	 * automatically re-enabled in timekeeping_resume().
	 */
}

/* The callbacks run only on CPU0, with irqs_disabled. */
static struct syscore_ops hv_synic_syscore_ops = {
	.suspend = hv_synic_suspend,
	.resume = hv_synic_resume,
};

static int __init hv_acpi_init(void)
{
	int ret, t;

	if (!hv_is_hyperv_initialized())
		return -ENODEV;

	if (hv_root_partition)
		return 0;

	init_completion(&probe_event);

	/*
	 * Get ACPI resources first.
	 */
	ret = acpi_bus_register_driver(&vmbus_acpi_driver);

	if (ret)
		return ret;

	t = wait_for_completion_timeout(&probe_event, 5*HZ);
	if (t == 0) {
		ret = -ETIMEDOUT;
		goto cleanup;
	}

	/*
	 * If we're on an architecture with a hardcoded hypervisor
	 * vector (i.e. x86/x64), override the VMbus interrupt found
	 * in the ACPI tables. Ensure vmbus_irq is not set since the
	 * normal Linux IRQ mechanism is not used in this case.
	 */
#ifdef HYPERVISOR_CALLBACK_VECTOR
	vmbus_interrupt = HYPERVISOR_CALLBACK_VECTOR;
	vmbus_irq = -1;
#endif

	hv_debug_init();

	ret = vmbus_bus_init();
	if (ret)
		goto cleanup;

	hv_setup_kexec_handler(hv_kexec_handler);
	hv_setup_crash_handler(hv_crash_handler);

	register_syscore_ops(&hv_synic_syscore_ops);

	return 0;

cleanup:
	acpi_bus_unregister_driver(&vmbus_acpi_driver);
	hv_acpi_dev = NULL;
	return ret;
}

static void __exit vmbus_exit(void)
{
	int cpu;

	unregister_syscore_ops(&hv_synic_syscore_ops);

	hv_remove_kexec_handler();
	hv_remove_crash_handler();
	vmbus_connection.conn_state = DISCONNECTED;
	hv_stimer_global_cleanup();
	vmbus_disconnect();
	if (vmbus_irq == -1) {
		hv_remove_vmbus_handler();
	} else {
		free_percpu_irq(vmbus_irq, vmbus_evt);
		free_percpu(vmbus_evt);
	}
	for_each_online_cpu(cpu) {
		struct hv_per_cpu_context *hv_cpu
			= per_cpu_ptr(hv_context.cpu_context, cpu);

		tasklet_kill(&hv_cpu->msg_dpc);
	}
	hv_debug_rm_all_dir();

	vmbus_free_channels();
	kfree(vmbus_connection.channels);

	if (ms_hyperv.misc_features & HV_FEATURE_GUEST_CRASH_MSR_AVAILABLE) {
		kmsg_dump_unregister(&hv_kmsg_dumper);
		unregister_die_notifier(&hyperv_die_block);
		atomic_notifier_chain_unregister(&panic_notifier_list,
						 &hyperv_panic_block);
	}

	free_page((unsigned long)hv_panic_page);
	unregister_sysctl_table(hv_ctl_table_hdr);
	hv_ctl_table_hdr = NULL;
	bus_unregister(&hv_bus);

	cpuhp_remove_state(hyperv_cpuhp_online);
	hv_synic_free();
	acpi_bus_unregister_driver(&vmbus_acpi_driver);
}


MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Microsoft Hyper-V VMBus Driver");

subsys_initcall(hv_acpi_init);
module_exit(vmbus_exit);
