/*
 * event.c - exporting ACPI events via procfs
 *
 *  Copyright (C) 2001, 2002 Andy Grover <andrew.grover@intel.com>
 *  Copyright (C) 2001, 2002 Paul Diefenbaugh <paul.s.diefenbaugh@intel.com>
 *
 */

#include <linux/spinlock.h>
#include <linux/export.h>
#include <linux/proc_fs.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/gfp.h>
#include <linux/acpi.h>
#include <net/netlink.h>
#include <net/genetlink.h>

#include "internal.h"

#define _COMPONENT		ACPI_SYSTEM_COMPONENT
ACPI_MODULE_NAME("event");

/* ACPI notifier chain */
static BLOCKING_NOTIFIER_HEAD(acpi_chain_head);

int acpi_notifier_call_chain(struct acpi_device *dev, u32 type, u32 data)
{
	struct acpi_bus_event event;

	strcpy(event.device_class, dev->pnp.device_class);
	strcpy(event.bus_id, dev->pnp.bus_id);
	event.type = type;
	event.data = data;
	return (blocking_notifier_call_chain(&acpi_chain_head, 0, (void *)&event)
                        == NOTIFY_BAD) ? -EINVAL : 0;
}
EXPORT_SYMBOL(acpi_notifier_call_chain);

int register_acpi_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&acpi_chain_head, nb);
}
EXPORT_SYMBOL(register_acpi_notifier);

int unregister_acpi_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&acpi_chain_head, nb);
}
EXPORT_SYMBOL(unregister_acpi_notifier);

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

static const struct genl_multicast_group acpi_event_mcgrps[] = {
	{ .name = ACPI_GENL_MCAST_GROUP_NAME, },
};

static struct genl_family acpi_event_genl_family = {
	.id = GENL_ID_GENERATE,
	.name = ACPI_GENL_FAMILY_NAME,
	.version = ACPI_GENL_VERSION,
	.maxattr = ACPI_GENL_ATTR_MAX,
	.mcgrps = acpi_event_mcgrps,
	.n_mcgrps = ARRAY_SIZE(acpi_event_mcgrps),
};

int acpi_bus_generate_netlink_event(const char *device_class,
				      const char *bus_id,
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
	memset(event, 0, sizeof(struct acpi_genl_event));

	strcpy(event->device_class, device_class);
	strcpy(event->bus_id, bus_id);
	event->type = type;
	event->data = data;

	/* send multicast genetlink message */
	result = genlmsg_end(skb, msg_header);
	if (result < 0) {
		nlmsg_free(skb);
		return result;
	}

	genlmsg_multicast(&acpi_event_genl_family, skb, 0, 0, GFP_ATOMIC);
	return 0;
}

EXPORT_SYMBOL(acpi_bus_generate_netlink_event);

static int acpi_event_genetlink_init(void)
{
	return genl_register_family(&acpi_event_genl_family);
}

#else
int acpi_bus_generate_netlink_event(const char *device_class,
				      const char *bus_id,
				      u8 type, int data)
{
	return 0;
}

EXPORT_SYMBOL(acpi_bus_generate_netlink_event);

static int acpi_event_genetlink_init(void)
{
	return -ENODEV;
}
#endif

static int __init acpi_event_init(void)
{
	int error = 0;

	if (acpi_disabled)
		return 0;

	/* create genetlink for acpi event */
	error = acpi_event_genetlink_init();
	if (error)
		printk(KERN_WARNING PREFIX
		       "Failed to create genetlink family for ACPI event\n");
	return 0;
}

fs_initcall(acpi_event_init);
