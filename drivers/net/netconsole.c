// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  linux/drivers/net/netconsole.c
 *
 *  Copyright (C) 2001  Ingo Molnar <mingo@redhat.com>
 *
 *  This file contains the implementation of an IRQ-safe, crash-safe
 *  kernel console implementation that outputs kernel messages to the
 *  network.
 *
 * Modification history:
 *
 * 2001-09-17    started by Ingo Molnar.
 * 2003-08-11    2.6 port by Matt Mackall
 *               simplified options
 *               generic card hooks
 *               works non-modular
 * 2003-09-07    rewritten with netpoll api
 */

/****************************************************************
 *
 ****************************************************************/

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/mm.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/console.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/netpoll.h>
#include <linux/inet.h>
#include <linux/configfs.h>
#include <linux/etherdevice.h>

MODULE_AUTHOR("Maintainer: Matt Mackall <mpm@selenic.com>");
MODULE_DESCRIPTION("Console driver for network interfaces");
MODULE_LICENSE("GPL");

#define MAX_PARAM_LENGTH	256
#define MAX_PRINT_CHUNK		1000

static char config[MAX_PARAM_LENGTH];
module_param_string(netconsole, config, MAX_PARAM_LENGTH, 0);
MODULE_PARM_DESC(netconsole, " netconsole=[src-port]@[src-ip]/[dev],[tgt-port]@<tgt-ip>/[tgt-macaddr]");

static bool oops_only = false;
module_param(oops_only, bool, 0600);
MODULE_PARM_DESC(oops_only, "Only log oops messages");

#ifndef	MODULE
static int __init option_setup(char *opt)
{
	strlcpy(config, opt, MAX_PARAM_LENGTH);
	return 1;
}
__setup("netconsole=", option_setup);
#endif	/* MODULE */

/* Linked list of all configured targets */
static LIST_HEAD(target_list);

/* This needs to be a spinlock because write_msg() cannot sleep */
static DEFINE_SPINLOCK(target_list_lock);

/*
 * Console driver for extended netconsoles.  Registered on the first use to
 * avoid unnecessarily enabling ext message formatting.
 */
static struct console netconsole_ext;

/**
 * struct netconsole_target - Represents a configured netconsole target.
 * @list:	Links this target into the target_list.
 * @item:	Links us into the configfs subsystem hierarchy.
 * @enabled:	On / off knob to enable / disable target.
 *		Visible from userspace (read-write).
 *		We maintain a strict 1:1 correspondence between this and
 *		whether the corresponding netpoll is active or inactive.
 *		Also, other parameters of a target may be modified at
 *		runtime only when it is disabled (enabled == 0).
 * @extended:	Denotes whether console is extended or not.
 * @np:		The netpoll structure for this target.
 *		Contains the other userspace visible parameters:
 *		dev_name	(read-write)
 *		local_port	(read-write)
 *		remote_port	(read-write)
 *		local_ip	(read-write)
 *		remote_ip	(read-write)
 *		local_mac	(read-only)
 *		remote_mac	(read-write)
 */
struct netconsole_target {
	struct list_head	list;
#ifdef	CONFIG_NETCONSOLE_DYNAMIC
	struct config_item	item;
#endif
	bool			enabled;
	bool			extended;
	struct netpoll		np;
};

#ifdef	CONFIG_NETCONSOLE_DYNAMIC

static struct configfs_subsystem netconsole_subsys;
static DEFINE_MUTEX(dynamic_netconsole_mutex);

static int __init dynamic_netconsole_init(void)
{
	config_group_init(&netconsole_subsys.su_group);
	mutex_init(&netconsole_subsys.su_mutex);
	return configfs_register_subsystem(&netconsole_subsys);
}

static void __exit dynamic_netconsole_exit(void)
{
	configfs_unregister_subsystem(&netconsole_subsys);
}

/*
 * Targets that were created by parsing the boot/module option string
 * do not exist in the configfs hierarchy (and have NULL names) and will
 * never go away, so make these a no-op for them.
 */
static void netconsole_target_get(struct netconsole_target *nt)
{
	if (config_item_name(&nt->item))
		config_item_get(&nt->item);
}

static void netconsole_target_put(struct netconsole_target *nt)
{
	if (config_item_name(&nt->item))
		config_item_put(&nt->item);
}

#else	/* !CONFIG_NETCONSOLE_DYNAMIC */

static int __init dynamic_netconsole_init(void)
{
	return 0;
}

static void __exit dynamic_netconsole_exit(void)
{
}

/*
 * No danger of targets going away from under us when dynamic
 * reconfigurability is off.
 */
static void netconsole_target_get(struct netconsole_target *nt)
{
}

static void netconsole_target_put(struct netconsole_target *nt)
{
}

#endif	/* CONFIG_NETCONSOLE_DYNAMIC */

/* Allocate new target (from boot/module param) and setup netpoll for it */
static struct netconsole_target *alloc_param_target(char *target_config)
{
	int err = -ENOMEM;
	struct netconsole_target *nt;

	/*
	 * Allocate and initialize with defaults.
	 * Note that these targets get their config_item fields zeroed-out.
	 */
	nt = kzalloc(sizeof(*nt), GFP_KERNEL);
	if (!nt)
		goto fail;

	nt->np.name = "netconsole";
	strlcpy(nt->np.dev_name, "eth0", IFNAMSIZ);
	nt->np.local_port = 6665;
	nt->np.remote_port = 6666;
	eth_broadcast_addr(nt->np.remote_mac);

	if (*target_config == '+') {
		nt->extended = true;
		target_config++;
	}

	/* Parse parameters and setup netpoll */
	err = netpoll_parse_options(&nt->np, target_config);
	if (err)
		goto fail;

	err = netpoll_setup(&nt->np);
	if (err)
		goto fail;

	nt->enabled = true;

	return nt;

fail:
	kfree(nt);
	return ERR_PTR(err);
}

/* Cleanup netpoll for given target (from boot/module param) and free it */
static void free_param_target(struct netconsole_target *nt)
{
	netpoll_cleanup(&nt->np);
	kfree(nt);
}

#ifdef	CONFIG_NETCONSOLE_DYNAMIC

/*
 * Our subsystem hierarchy is:
 *
 * /sys/kernel/config/netconsole/
 *				|
 *				<target>/
 *				|	enabled
 *				|	dev_name
 *				|	local_port
 *				|	remote_port
 *				|	local_ip
 *				|	remote_ip
 *				|	local_mac
 *				|	remote_mac
 *				|
 *				<target>/...
 */

static struct netconsole_target *to_target(struct config_item *item)
{
	return item ?
		container_of(item, struct netconsole_target, item) :
		NULL;
}

/*
 * Attribute operations for netconsole_target.
 */

static ssize_t enabled_show(struct config_item *item, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", to_target(item)->enabled);
}

static ssize_t extended_show(struct config_item *item, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", to_target(item)->extended);
}

static ssize_t dev_name_show(struct config_item *item, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%s\n", to_target(item)->np.dev_name);
}

static ssize_t local_port_show(struct config_item *item, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", to_target(item)->np.local_port);
}

static ssize_t remote_port_show(struct config_item *item, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", to_target(item)->np.remote_port);
}

static ssize_t local_ip_show(struct config_item *item, char *buf)
{
	struct netconsole_target *nt = to_target(item);

	if (nt->np.ipv6)
		return snprintf(buf, PAGE_SIZE, "%pI6c\n", &nt->np.local_ip.in6);
	else
		return snprintf(buf, PAGE_SIZE, "%pI4\n", &nt->np.local_ip);
}

static ssize_t remote_ip_show(struct config_item *item, char *buf)
{
	struct netconsole_target *nt = to_target(item);

	if (nt->np.ipv6)
		return snprintf(buf, PAGE_SIZE, "%pI6c\n", &nt->np.remote_ip.in6);
	else
		return snprintf(buf, PAGE_SIZE, "%pI4\n", &nt->np.remote_ip);
}

static ssize_t local_mac_show(struct config_item *item, char *buf)
{
	struct net_device *dev = to_target(item)->np.dev;
	static const u8 bcast[ETH_ALEN] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

	return snprintf(buf, PAGE_SIZE, "%pM\n", dev ? dev->dev_addr : bcast);
}

static ssize_t remote_mac_show(struct config_item *item, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%pM\n", to_target(item)->np.remote_mac);
}

/*
 * This one is special -- targets created through the configfs interface
 * are not enabled (and the corresponding netpoll activated) by default.
 * The user is expected to set the desired parameters first (which
 * would enable him to dynamically add new netpoll targets for new
 * network interfaces as and when they come up).
 */
static ssize_t enabled_store(struct config_item *item,
		const char *buf, size_t count)
{
	struct netconsole_target *nt = to_target(item);
	unsigned long flags;
	int enabled;
	int err;

	mutex_lock(&dynamic_netconsole_mutex);
	err = kstrtoint(buf, 10, &enabled);
	if (err < 0)
		goto out_unlock;

	err = -EINVAL;
	if (enabled < 0 || enabled > 1)
		goto out_unlock;
	if ((bool)enabled == nt->enabled) {
		pr_info("network logging has already %s\n",
			nt->enabled ? "started" : "stopped");
		goto out_unlock;
	}

	if (enabled) {	/* true */
		if (nt->extended && !(netconsole_ext.flags & CON_ENABLED)) {
			netconsole_ext.flags |= CON_ENABLED;
			register_console(&netconsole_ext);
		}

		/*
		 * Skip netpoll_parse_options() -- all the attributes are
		 * already configured via configfs. Just print them out.
		 */
		netpoll_print_options(&nt->np);

		err = netpoll_setup(&nt->np);
		if (err)
			goto out_unlock;

		pr_info("network logging started\n");
	} else {	/* false */
		/* We need to disable the netconsole before cleaning it up
		 * otherwise we might end up in write_msg() with
		 * nt->np.dev == NULL and nt->enabled == true
		 */
		spin_lock_irqsave(&target_list_lock, flags);
		nt->enabled = false;
		spin_unlock_irqrestore(&target_list_lock, flags);
		netpoll_cleanup(&nt->np);
	}

	nt->enabled = enabled;

	mutex_unlock(&dynamic_netconsole_mutex);
	return strnlen(buf, count);
out_unlock:
	mutex_unlock(&dynamic_netconsole_mutex);
	return err;
}

static ssize_t extended_store(struct config_item *item, const char *buf,
		size_t count)
{
	struct netconsole_target *nt = to_target(item);
	int extended;
	int err;

	mutex_lock(&dynamic_netconsole_mutex);
	if (nt->enabled) {
		pr_err("target (%s) is enabled, disable to update parameters\n",
		       config_item_name(&nt->item));
		err = -EINVAL;
		goto out_unlock;
	}

	err = kstrtoint(buf, 10, &extended);
	if (err < 0)
		goto out_unlock;
	if (extended < 0 || extended > 1) {
		err = -EINVAL;
		goto out_unlock;
	}

	nt->extended = extended;

	mutex_unlock(&dynamic_netconsole_mutex);
	return strnlen(buf, count);
out_unlock:
	mutex_unlock(&dynamic_netconsole_mutex);
	return err;
}

static ssize_t dev_name_store(struct config_item *item, const char *buf,
		size_t count)
{
	struct netconsole_target *nt = to_target(item);
	size_t len;

	mutex_lock(&dynamic_netconsole_mutex);
	if (nt->enabled) {
		pr_err("target (%s) is enabled, disable to update parameters\n",
		       config_item_name(&nt->item));
		mutex_unlock(&dynamic_netconsole_mutex);
		return -EINVAL;
	}

	strlcpy(nt->np.dev_name, buf, IFNAMSIZ);

	/* Get rid of possible trailing newline from echo(1) */
	len = strnlen(nt->np.dev_name, IFNAMSIZ);
	if (nt->np.dev_name[len - 1] == '\n')
		nt->np.dev_name[len - 1] = '\0';

	mutex_unlock(&dynamic_netconsole_mutex);
	return strnlen(buf, count);
}

static ssize_t local_port_store(struct config_item *item, const char *buf,
		size_t count)
{
	struct netconsole_target *nt = to_target(item);
	int rv = -EINVAL;

	mutex_lock(&dynamic_netconsole_mutex);
	if (nt->enabled) {
		pr_err("target (%s) is enabled, disable to update parameters\n",
		       config_item_name(&nt->item));
		goto out_unlock;
	}

	rv = kstrtou16(buf, 10, &nt->np.local_port);
	if (rv < 0)
		goto out_unlock;
	mutex_unlock(&dynamic_netconsole_mutex);
	return strnlen(buf, count);
out_unlock:
	mutex_unlock(&dynamic_netconsole_mutex);
	return rv;
}

static ssize_t remote_port_store(struct config_item *item,
		const char *buf, size_t count)
{
	struct netconsole_target *nt = to_target(item);
	int rv = -EINVAL;

	mutex_lock(&dynamic_netconsole_mutex);
	if (nt->enabled) {
		pr_err("target (%s) is enabled, disable to update parameters\n",
		       config_item_name(&nt->item));
		goto out_unlock;
	}

	rv = kstrtou16(buf, 10, &nt->np.remote_port);
	if (rv < 0)
		goto out_unlock;
	mutex_unlock(&dynamic_netconsole_mutex);
	return strnlen(buf, count);
out_unlock:
	mutex_unlock(&dynamic_netconsole_mutex);
	return rv;
}

static ssize_t local_ip_store(struct config_item *item, const char *buf,
		size_t count)
{
	struct netconsole_target *nt = to_target(item);

	mutex_lock(&dynamic_netconsole_mutex);
	if (nt->enabled) {
		pr_err("target (%s) is enabled, disable to update parameters\n",
		       config_item_name(&nt->item));
		goto out_unlock;
	}

	if (strnchr(buf, count, ':')) {
		const char *end;
		if (in6_pton(buf, count, nt->np.local_ip.in6.s6_addr, -1, &end) > 0) {
			if (*end && *end != '\n') {
				pr_err("invalid IPv6 address at: <%c>\n", *end);
				goto out_unlock;
			}
			nt->np.ipv6 = true;
		} else
			goto out_unlock;
	} else {
		if (!nt->np.ipv6) {
			nt->np.local_ip.ip = in_aton(buf);
		} else
			goto out_unlock;
	}

	mutex_unlock(&dynamic_netconsole_mutex);
	return strnlen(buf, count);
out_unlock:
	mutex_unlock(&dynamic_netconsole_mutex);
	return -EINVAL;
}

static ssize_t remote_ip_store(struct config_item *item, const char *buf,
	       size_t count)
{
	struct netconsole_target *nt = to_target(item);

	mutex_lock(&dynamic_netconsole_mutex);
	if (nt->enabled) {
		pr_err("target (%s) is enabled, disable to update parameters\n",
		       config_item_name(&nt->item));
		goto out_unlock;
	}

	if (strnchr(buf, count, ':')) {
		const char *end;
		if (in6_pton(buf, count, nt->np.remote_ip.in6.s6_addr, -1, &end) > 0) {
			if (*end && *end != '\n') {
				pr_err("invalid IPv6 address at: <%c>\n", *end);
				goto out_unlock;
			}
			nt->np.ipv6 = true;
		} else
			goto out_unlock;
	} else {
		if (!nt->np.ipv6) {
			nt->np.remote_ip.ip = in_aton(buf);
		} else
			goto out_unlock;
	}

	mutex_unlock(&dynamic_netconsole_mutex);
	return strnlen(buf, count);
out_unlock:
	mutex_unlock(&dynamic_netconsole_mutex);
	return -EINVAL;
}

static ssize_t remote_mac_store(struct config_item *item, const char *buf,
		size_t count)
{
	struct netconsole_target *nt = to_target(item);
	u8 remote_mac[ETH_ALEN];

	mutex_lock(&dynamic_netconsole_mutex);
	if (nt->enabled) {
		pr_err("target (%s) is enabled, disable to update parameters\n",
		       config_item_name(&nt->item));
		goto out_unlock;
	}

	if (!mac_pton(buf, remote_mac))
		goto out_unlock;
	if (buf[3 * ETH_ALEN - 1] && buf[3 * ETH_ALEN - 1] != '\n')
		goto out_unlock;
	memcpy(nt->np.remote_mac, remote_mac, ETH_ALEN);

	mutex_unlock(&dynamic_netconsole_mutex);
	return strnlen(buf, count);
out_unlock:
	mutex_unlock(&dynamic_netconsole_mutex);
	return -EINVAL;
}

CONFIGFS_ATTR(, enabled);
CONFIGFS_ATTR(, extended);
CONFIGFS_ATTR(, dev_name);
CONFIGFS_ATTR(, local_port);
CONFIGFS_ATTR(, remote_port);
CONFIGFS_ATTR(, local_ip);
CONFIGFS_ATTR(, remote_ip);
CONFIGFS_ATTR_RO(, local_mac);
CONFIGFS_ATTR(, remote_mac);

static struct configfs_attribute *netconsole_target_attrs[] = {
	&attr_enabled,
	&attr_extended,
	&attr_dev_name,
	&attr_local_port,
	&attr_remote_port,
	&attr_local_ip,
	&attr_remote_ip,
	&attr_local_mac,
	&attr_remote_mac,
	NULL,
};

/*
 * Item operations and type for netconsole_target.
 */

static void netconsole_target_release(struct config_item *item)
{
	kfree(to_target(item));
}

static struct configfs_item_operations netconsole_target_item_ops = {
	.release		= netconsole_target_release,
};

static const struct config_item_type netconsole_target_type = {
	.ct_attrs		= netconsole_target_attrs,
	.ct_item_ops		= &netconsole_target_item_ops,
	.ct_owner		= THIS_MODULE,
};

/*
 * Group operations and type for netconsole_subsys.
 */

static struct config_item *make_netconsole_target(struct config_group *group,
						  const char *name)
{
	unsigned long flags;
	struct netconsole_target *nt;

	/*
	 * Allocate and initialize with defaults.
	 * Target is disabled at creation (!enabled).
	 */
	nt = kzalloc(sizeof(*nt), GFP_KERNEL);
	if (!nt)
		return ERR_PTR(-ENOMEM);

	nt->np.name = "netconsole";
	strlcpy(nt->np.dev_name, "eth0", IFNAMSIZ);
	nt->np.local_port = 6665;
	nt->np.remote_port = 6666;
	eth_broadcast_addr(nt->np.remote_mac);

	/* Initialize the config_item member */
	config_item_init_type_name(&nt->item, name, &netconsole_target_type);

	/* Adding, but it is disabled */
	spin_lock_irqsave(&target_list_lock, flags);
	list_add(&nt->list, &target_list);
	spin_unlock_irqrestore(&target_list_lock, flags);

	return &nt->item;
}

static void drop_netconsole_target(struct config_group *group,
				   struct config_item *item)
{
	unsigned long flags;
	struct netconsole_target *nt = to_target(item);

	spin_lock_irqsave(&target_list_lock, flags);
	list_del(&nt->list);
	spin_unlock_irqrestore(&target_list_lock, flags);

	/*
	 * The target may have never been enabled, or was manually disabled
	 * before being removed so netpoll may have already been cleaned up.
	 */
	if (nt->enabled)
		netpoll_cleanup(&nt->np);

	config_item_put(&nt->item);
}

static struct configfs_group_operations netconsole_subsys_group_ops = {
	.make_item	= make_netconsole_target,
	.drop_item	= drop_netconsole_target,
};

static const struct config_item_type netconsole_subsys_type = {
	.ct_group_ops	= &netconsole_subsys_group_ops,
	.ct_owner	= THIS_MODULE,
};

/* The netconsole configfs subsystem */
static struct configfs_subsystem netconsole_subsys = {
	.su_group	= {
		.cg_item	= {
			.ci_namebuf	= "netconsole",
			.ci_type	= &netconsole_subsys_type,
		},
	},
};

#endif	/* CONFIG_NETCONSOLE_DYNAMIC */

/* Handle network interface device notifications */
static int netconsole_netdev_event(struct notifier_block *this,
				   unsigned long event, void *ptr)
{
	unsigned long flags;
	struct netconsole_target *nt;
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);
	bool stopped = false;

	if (!(event == NETDEV_CHANGENAME || event == NETDEV_UNREGISTER ||
	      event == NETDEV_RELEASE || event == NETDEV_JOIN))
		goto done;

	spin_lock_irqsave(&target_list_lock, flags);
restart:
	list_for_each_entry(nt, &target_list, list) {
		netconsole_target_get(nt);
		if (nt->np.dev == dev) {
			switch (event) {
			case NETDEV_CHANGENAME:
				strlcpy(nt->np.dev_name, dev->name, IFNAMSIZ);
				break;
			case NETDEV_RELEASE:
			case NETDEV_JOIN:
			case NETDEV_UNREGISTER:
				/* rtnl_lock already held
				 * we might sleep in __netpoll_cleanup()
				 */
				spin_unlock_irqrestore(&target_list_lock, flags);

				__netpoll_cleanup(&nt->np);

				spin_lock_irqsave(&target_list_lock, flags);
				dev_put(nt->np.dev);
				nt->np.dev = NULL;
				nt->enabled = false;
				stopped = true;
				netconsole_target_put(nt);
				goto restart;
			}
		}
		netconsole_target_put(nt);
	}
	spin_unlock_irqrestore(&target_list_lock, flags);
	if (stopped) {
		const char *msg = "had an event";
		switch (event) {
		case NETDEV_UNREGISTER:
			msg = "unregistered";
			break;
		case NETDEV_RELEASE:
			msg = "released slaves";
			break;
		case NETDEV_JOIN:
			msg = "is joining a master device";
			break;
		}
		pr_info("network logging stopped on interface %s as it %s\n",
			dev->name, msg);
	}

done:
	return NOTIFY_DONE;
}

static struct notifier_block netconsole_netdev_notifier = {
	.notifier_call  = netconsole_netdev_event,
};

/**
 * send_ext_msg_udp - send extended log message to target
 * @nt: target to send message to
 * @msg: extended log message to send
 * @msg_len: length of message
 *
 * Transfer extended log @msg to @nt.  If @msg is longer than
 * MAX_PRINT_CHUNK, it'll be split and transmitted in multiple chunks with
 * ncfrag header field added to identify them.
 */
static void send_ext_msg_udp(struct netconsole_target *nt, const char *msg,
			     int msg_len)
{
	static char buf[MAX_PRINT_CHUNK]; /* protected by target_list_lock */
	const char *header, *body;
	int offset = 0;
	int header_len, body_len;

	if (msg_len <= MAX_PRINT_CHUNK) {
		netpoll_send_udp(&nt->np, msg, msg_len);
		return;
	}

	/* need to insert extra header fields, detect header and body */
	header = msg;
	body = memchr(msg, ';', msg_len);
	if (WARN_ON_ONCE(!body))
		return;

	header_len = body - header;
	body_len = msg_len - header_len - 1;
	body++;

	/*
	 * Transfer multiple chunks with the following extra header.
	 * "ncfrag=<byte-offset>/<total-bytes>"
	 */
	memcpy(buf, header, header_len);

	while (offset < body_len) {
		int this_header = header_len;
		int this_chunk;

		this_header += scnprintf(buf + this_header,
					 sizeof(buf) - this_header,
					 ",ncfrag=%d/%d;", offset, body_len);

		this_chunk = min(body_len - offset,
				 MAX_PRINT_CHUNK - this_header);
		if (WARN_ON_ONCE(this_chunk <= 0))
			return;

		memcpy(buf + this_header, body + offset, this_chunk);

		netpoll_send_udp(&nt->np, buf, this_header + this_chunk);

		offset += this_chunk;
	}
}

static void write_ext_msg(struct console *con, const char *msg,
			  unsigned int len)
{
	struct netconsole_target *nt;
	unsigned long flags;

	if ((oops_only && !oops_in_progress) || list_empty(&target_list))
		return;

	spin_lock_irqsave(&target_list_lock, flags);
	list_for_each_entry(nt, &target_list, list)
		if (nt->extended && nt->enabled && netif_running(nt->np.dev))
			send_ext_msg_udp(nt, msg, len);
	spin_unlock_irqrestore(&target_list_lock, flags);
}

static void write_msg(struct console *con, const char *msg, unsigned int len)
{
	int frag, left;
	unsigned long flags;
	struct netconsole_target *nt;
	const char *tmp;

	if (oops_only && !oops_in_progress)
		return;
	/* Avoid taking lock and disabling interrupts unnecessarily */
	if (list_empty(&target_list))
		return;

	spin_lock_irqsave(&target_list_lock, flags);
	list_for_each_entry(nt, &target_list, list) {
		if (!nt->extended && nt->enabled && netif_running(nt->np.dev)) {
			/*
			 * We nest this inside the for-each-target loop above
			 * so that we're able to get as much logging out to
			 * at least one target if we die inside here, instead
			 * of unnecessarily keeping all targets in lock-step.
			 */
			tmp = msg;
			for (left = len; left;) {
				frag = min(left, MAX_PRINT_CHUNK);
				netpoll_send_udp(&nt->np, tmp, frag);
				tmp += frag;
				left -= frag;
			}
		}
	}
	spin_unlock_irqrestore(&target_list_lock, flags);
}

static struct console netconsole_ext = {
	.name	= "netcon_ext",
	.flags	= CON_EXTENDED,	/* starts disabled, registered on first use */
	.write	= write_ext_msg,
};

static struct console netconsole = {
	.name	= "netcon",
	.flags	= CON_ENABLED,
	.write	= write_msg,
};

static int __init init_netconsole(void)
{
	int err;
	struct netconsole_target *nt, *tmp;
	unsigned long flags;
	char *target_config;
	char *input = config;

	if (strnlen(input, MAX_PARAM_LENGTH)) {
		while ((target_config = strsep(&input, ";"))) {
			nt = alloc_param_target(target_config);
			if (IS_ERR(nt)) {
				err = PTR_ERR(nt);
				goto fail;
			}
			/* Dump existing printks when we register */
			if (nt->extended)
				netconsole_ext.flags |= CON_PRINTBUFFER |
							CON_ENABLED;
			else
				netconsole.flags |= CON_PRINTBUFFER;

			spin_lock_irqsave(&target_list_lock, flags);
			list_add(&nt->list, &target_list);
			spin_unlock_irqrestore(&target_list_lock, flags);
		}
	}

	err = register_netdevice_notifier(&netconsole_netdev_notifier);
	if (err)
		goto fail;

	err = dynamic_netconsole_init();
	if (err)
		goto undonotifier;

	if (netconsole_ext.flags & CON_ENABLED)
		register_console(&netconsole_ext);
	register_console(&netconsole);
	pr_info("network logging started\n");

	return err;

undonotifier:
	unregister_netdevice_notifier(&netconsole_netdev_notifier);

fail:
	pr_err("cleaning up\n");

	/*
	 * Remove all targets and destroy them (only targets created
	 * from the boot/module option exist here). Skipping the list
	 * lock is safe here, and netpoll_cleanup() will sleep.
	 */
	list_for_each_entry_safe(nt, tmp, &target_list, list) {
		list_del(&nt->list);
		free_param_target(nt);
	}

	return err;
}

static void __exit cleanup_netconsole(void)
{
	struct netconsole_target *nt, *tmp;

	unregister_console(&netconsole_ext);
	unregister_console(&netconsole);
	dynamic_netconsole_exit();
	unregister_netdevice_notifier(&netconsole_netdev_notifier);

	/*
	 * Targets created via configfs pin references on our module
	 * and would first be rmdir(2)'ed from userspace. We reach
	 * here only when they are already destroyed, and only those
	 * created from the boot/module option are left, so remove and
	 * destroy them. Skipping the list lock is safe here, and
	 * netpoll_cleanup() will sleep.
	 */
	list_for_each_entry_safe(nt, tmp, &target_list, list) {
		list_del(&nt->list);
		free_param_target(nt);
	}
}

/*
 * Use late_initcall to ensure netconsole is
 * initialized after network device driver if built-in.
 *
 * late_initcall() and module_init() are identical if built as module.
 */
late_initcall(init_netconsole);
module_exit(cleanup_netconsole);
