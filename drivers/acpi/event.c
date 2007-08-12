/*
 * event.c - exporting ACPI events via procfs
 *
 *  Copyright (C) 2001, 2002 Andy Grover <andrew.grover@intel.com>
 *  Copyright (C) 2001, 2002 Paul Diefenbaugh <paul.s.diefenbaugh@intel.com>
 *
 */

#include <linux/spinlock.h>
#include <linux/proc_fs.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <acpi/acpi_drivers.h>
#include <net/netlink.h>
#include <net/genetlink.h>

#define _COMPONENT		ACPI_SYSTEM_COMPONENT
ACPI_MODULE_NAME("event");

/* Global vars for handling event proc entry */
static DEFINE_SPINLOCK(acpi_system_event_lock);
int event_is_open = 0;
extern struct list_head acpi_bus_event_list;
extern wait_queue_head_t acpi_bus_event_queue;

static int acpi_system_open_event(struct inode *inode, struct file *file)
{
	spin_lock_irq(&acpi_system_event_lock);

	if (event_is_open)
		goto out_busy;

	event_is_open = 1;

	spin_unlock_irq(&acpi_system_event_lock);
	return 0;

      out_busy:
	spin_unlock_irq(&acpi_system_event_lock);
	return -EBUSY;
}

static ssize_t
acpi_system_read_event(struct file *file, char __user * buffer, size_t count,
		       loff_t * ppos)
{
	int result = 0;
	struct acpi_bus_event event;
	static char str[ACPI_MAX_STRING];
	static int chars_remaining = 0;
	static char *ptr;

	if (!chars_remaining) {
		memset(&event, 0, sizeof(struct acpi_bus_event));

		if ((file->f_flags & O_NONBLOCK)
		    && (list_empty(&acpi_bus_event_list)))
			return -EAGAIN;

		result = acpi_bus_receive_event(&event);
		if (result)
			return result;

		chars_remaining = sprintf(str, "%s %s %08x %08x\n",
					  event.device_class ? event.
					  device_class : "<unknown>",
					  event.bus_id ? event.
					  bus_id : "<unknown>", event.type,
					  event.data);
		ptr = str;
	}

	if (chars_remaining < count) {
		count = chars_remaining;
	}

	if (copy_to_user(buffer, ptr, count))
		return -EFAULT;

	*ppos += count;
	chars_remaining -= count;
	ptr += count;

	return count;
}

static int acpi_system_close_event(struct inode *inode, struct file *file)
{
	spin_lock_irq(&acpi_system_event_lock);
	event_is_open = 0;
	spin_unlock_irq(&acpi_system_event_lock);
	return 0;
}

static unsigned int acpi_system_poll_event(struct file *file, poll_table * wait)
{
	poll_wait(file, &acpi_bus_event_queue, wait);
	if (!list_empty(&acpi_bus_event_list))
		return POLLIN | POLLRDNORM;
	return 0;
}

static const struct file_operations acpi_system_event_ops = {
	.open = acpi_system_open_event,
	.read = acpi_system_read_event,
	.release = acpi_system_close_event,
	.poll = acpi_system_poll_event,
};

#ifdef CONFIG_NET
static unsigned int acpi_event_seqnum;
struct acpi_genl_event {
	acpi_device_class device_class;
	char bus_id[15];
	u32 type;
	u32 data;
};

/* attributes of acpi_genl_family */
enum {
	ACPI_GENL_ATTR_UNSPEC,
	ACPI_GENL_ATTR_EVENT,	/* ACPI event info needed by user space */
	__ACPI_GENL_ATTR_MAX,
};
#define ACPI_GENL_ATTR_MAX (__ACPI_GENL_ATTR_MAX - 1)

/* commands supported by the acpi_genl_family */
enum {
	ACPI_GENL_CMD_UNSPEC,
	ACPI_GENL_CMD_EVENT,	/* kernel->user notifications for ACPI events */
	__ACPI_GENL_CMD_MAX,
};
#define ACPI_GENL_CMD_MAX (__ACPI_GENL_CMD_MAX - 1)

#define ACPI_GENL_FAMILY_NAME		"acpi_event"
#define ACPI_GENL_VERSION		0x01
#define ACPI_GENL_MCAST_GROUP_NAME 	"acpi_mc_group"

static struct genl_family acpi_event_genl_family = {
	.id = GENL_ID_GENERATE,
	.name = ACPI_GENL_FAMILY_NAME,
	.version = ACPI_GENL_VERSION,
	.maxattr = ACPI_GENL_ATTR_MAX,
};

static struct genl_multicast_group acpi_event_mcgrp = {
	.name = ACPI_GENL_MCAST_GROUP_NAME,
};

int acpi_bus_generate_genetlink_event(struct acpi_device *device,
				      u8 type, int data)
{
	struct sk_buff *skb;
	struct nlattr *attr;
	struct acpi_genl_event *event;
	void *msg_header;
	int size;
	int result;

	/* allocate memory */
	size = nla_total_size(sizeof(struct acpi_genl_event)) +
	    nla_total_size(0);

	skb = genlmsg_new(size, GFP_ATOMIC);
	if (!skb)
		return -ENOMEM;

	/* add the genetlink message header */
	msg_header = genlmsg_put(skb, 0, acpi_event_seqnum++,
				 &acpi_event_genl_family, 0,
				 ACPI_GENL_CMD_EVENT);
	if (!msg_header) {
		nlmsg_free(skb);
		return -ENOMEM;
	}

	/* fill the data */
	attr =
	    nla_reserve(skb, ACPI_GENL_ATTR_EVENT,
			sizeof(struct acpi_genl_event));
	if (!attr) {
		nlmsg_free(skb);
		return -EINVAL;
	}

	event = nla_data(attr);
	if (!event) {
		nlmsg_free(skb);
		return -EINVAL;
	}

	memset(event, 0, sizeof(struct acpi_genl_event));

	strcpy(event->device_class, device->pnp.device_class);
	strcpy(event->bus_id, device->dev.bus_id);
	event->type = type;
	event->data = data;

	/* send multicast genetlink message */
	result = genlmsg_end(skb, msg_header);
	if (result < 0) {
		nlmsg_free(skb);
		return result;
	}

	result =
	    genlmsg_multicast(skb, 0, acpi_event_mcgrp.id, GFP_ATOMIC);
	if (result)
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				  "Failed to send a Genetlink message!\n"));
	return 0;
}

static int acpi_event_genetlink_init(void)
{
	int result;

	result = genl_register_family(&acpi_event_genl_family);
	if (result)
		return result;

	result = genl_register_mc_group(&acpi_event_genl_family,
					&acpi_event_mcgrp);
	if (result)
		genl_unregister_family(&acpi_event_genl_family);

	return result;
}

#else
int acpi_bus_generate_genetlink_event(struct acpi_device *device, u8 type,
				      int data)
{
	return 0;
}

static int acpi_event_genetlink_init(void)
{
	return -ENODEV;
}
#endif

static int __init acpi_event_init(void)
{
	struct proc_dir_entry *entry;
	int error = 0;

	if (acpi_disabled)
		return 0;

	/* create genetlink for acpi event */
	error = acpi_event_genetlink_init();
	if (error)
		printk(KERN_WARNING PREFIX
		       "Failed to create genetlink family for ACPI event\n");

	/* 'event' [R] */
	entry = create_proc_entry("event", S_IRUSR, acpi_root_dir);
	if (entry)
		entry->proc_fops = &acpi_system_event_ops;
	else
		return -ENODEV;

	return 0;
}

fs_initcall(acpi_event_init);
