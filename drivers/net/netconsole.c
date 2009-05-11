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
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2, or (at your option)
 *      any later version.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with this program; if not, write to the Free Software
 *      Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 ****************************************************************/

#include <linux/mm.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/console.h>
#include <linux/moduleparam.h>
#include <linux/string.h>
#include <linux/netpoll.h>
#include <linux/inet.h>
#include <linux/configfs.h>

MODULE_AUTHOR("Maintainer: Matt Mackall <mpm@selenic.com>");
MODULE_DESCRIPTION("Console driver for network interfaces");
MODULE_LICENSE("GPL");

#define MAX_PARAM_LENGTH	256
#define MAX_PRINT_CHUNK		1000

static char config[MAX_PARAM_LENGTH];
module_param_string(netconsole, config, MAX_PARAM_LENGTH, 0);
MODULE_PARM_DESC(netconsole, " netconsole=[src-port]@[src-ip]/[dev],[tgt-port]@<tgt-ip>/[tgt-macaddr]");

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
	int			enabled;
	struct netpoll		np;
};

#ifdef	CONFIG_NETCONSOLE_DYNAMIC

static struct configfs_subsystem netconsole_subsys;

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
	if (!nt) {
		printk(KERN_ERR "netconsole: failed to allocate memory\n");
		goto fail;
	}

	nt->np.name = "netconsole";
	strlcpy(nt->np.dev_name, "eth0", IFNAMSIZ);
	nt->np.local_port = 6665;
	nt->np.remote_port = 6666;
	memset(nt->np.remote_mac, 0xff, ETH_ALEN);

	/* Parse parameters and setup netpoll */
	err = netpoll_parse_options(&nt->np, target_config);
	if (err)
		goto fail;

	err = netpoll_setup(&nt->np);
	if (err)
		goto fail;

	nt->enabled = 1;

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

struct netconsole_target_attr {
	struct configfs_attribute	attr;
	ssize_t				(*show)(struct netconsole_target *nt,
						char *buf);
	ssize_t				(*store)(struct netconsole_target *nt,
						 const char *buf,
						 size_t count);
};

static struct netconsole_target *to_target(struct config_item *item)
{
	return item ?
		container_of(item, struct netconsole_target, item) :
		NULL;
}

/*
 * Wrapper over simple_strtol (base 10) with sanity and range checking.
 * We return (signed) long only because we may want to return errors.
 * Do not use this to convert numbers that are allowed to be negative.
 */
static long strtol10_check_range(const char *cp, long min, long max)
{
	long ret;
	char *p = (char *) cp;

	WARN_ON(min < 0);
	WARN_ON(max < min);

	ret = simple_strtol(p, &p, 10);

	if (*p && (*p != '\n')) {
		printk(KERN_ERR "netconsole: invalid input\n");
		return -EINVAL;
	}
	if ((ret < min) || (ret > max)) {
		printk(KERN_ERR "netconsole: input %ld must be between "
				"%ld and %ld\n", ret, min, max);
		return -EINVAL;
	}

	return ret;
}

/*
 * Attribute operations for netconsole_target.
 */

static ssize_t show_enabled(struct netconsole_target *nt, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", nt->enabled);
}

static ssize_t show_dev_name(struct netconsole_target *nt, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%s\n", nt->np.dev_name);
}

static ssize_t show_local_port(struct netconsole_target *nt, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", nt->np.local_port);
}

static ssize_t show_remote_port(struct netconsole_target *nt, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", nt->np.remote_port);
}

static ssize_t show_local_ip(struct netconsole_target *nt, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%pI4\n", &nt->np.local_ip);
}

static ssize_t show_remote_ip(struct netconsole_target *nt, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%pI4\n", &nt->np.remote_ip);
}

static ssize_t show_local_mac(struct netconsole_target *nt, char *buf)
{
	struct net_device *dev = nt->np.dev;
	static const u8 bcast[ETH_ALEN] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

	return snprintf(buf, PAGE_SIZE, "%pM\n", dev ? dev->dev_addr : bcast);
}

static ssize_t show_remote_mac(struct netconsole_target *nt, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%pM\n", nt->np.remote_mac);
}

/*
 * This one is special -- targets created through the configfs interface
 * are not enabled (and the corresponding netpoll activated) by default.
 * The user is expected to set the desired parameters first (which
 * would enable him to dynamically add new netpoll targets for new
 * network interfaces as and when they come up).
 */
static ssize_t store_enabled(struct netconsole_target *nt,
			     const char *buf,
			     size_t count)
{
	int err;
	long enabled;

	enabled = strtol10_check_range(buf, 0, 1);
	if (enabled < 0)
		return enabled;

	if (enabled) {	/* 1 */

		/*
		 * Skip netpoll_parse_options() -- all the attributes are
		 * already configured via configfs. Just print them out.
		 */
		netpoll_print_options(&nt->np);

		err = netpoll_setup(&nt->np);
		if (err)
			return err;

		printk(KERN_INFO "netconsole: network logging started\n");

	} else {	/* 0 */
		netpoll_cleanup(&nt->np);
	}

	nt->enabled = enabled;

	return strnlen(buf, count);
}

static ssize_t store_dev_name(struct netconsole_target *nt,
			      const char *buf,
			      size_t count)
{
	size_t len;

	if (nt->enabled) {
		printk(KERN_ERR "netconsole: target (%s) is enabled, "
				"disable to update parameters\n",
				config_item_name(&nt->item));
		return -EINVAL;
	}

	strlcpy(nt->np.dev_name, buf, IFNAMSIZ);

	/* Get rid of possible trailing newline from echo(1) */
	len = strnlen(nt->np.dev_name, IFNAMSIZ);
	if (nt->np.dev_name[len - 1] == '\n')
		nt->np.dev_name[len - 1] = '\0';

	return strnlen(buf, count);
}

static ssize_t store_local_port(struct netconsole_target *nt,
				const char *buf,
				size_t count)
{
	long local_port;
#define __U16_MAX	((__u16) ~0U)

	if (nt->enabled) {
		printk(KERN_ERR "netconsole: target (%s) is enabled, "
				"disable to update parameters\n",
				config_item_name(&nt->item));
		return -EINVAL;
	}

	local_port = strtol10_check_range(buf, 0, __U16_MAX);
	if (local_port < 0)
		return local_port;

	nt->np.local_port = local_port;

	return strnlen(buf, count);
}

static ssize_t store_remote_port(struct netconsole_target *nt,
				 const char *buf,
				 size_t count)
{
	long remote_port;
#define __U16_MAX	((__u16) ~0U)

	if (nt->enabled) {
		printk(KERN_ERR "netconsole: target (%s) is enabled, "
				"disable to update parameters\n",
				config_item_name(&nt->item));
		return -EINVAL;
	}

	remote_port = strtol10_check_range(buf, 0, __U16_MAX);
	if (remote_port < 0)
		return remote_port;

	nt->np.remote_port = remote_port;

	return strnlen(buf, count);
}

static ssize_t store_local_ip(struct netconsole_target *nt,
			      const char *buf,
			      size_t count)
{
	if (nt->enabled) {
		printk(KERN_ERR "netconsole: target (%s) is enabled, "
				"disable to update parameters\n",
				config_item_name(&nt->item));
		return -EINVAL;
	}

	nt->np.local_ip = in_aton(buf);

	return strnlen(buf, count);
}

static ssize_t store_remote_ip(struct netconsole_target *nt,
			       const char *buf,
			       size_t count)
{
	if (nt->enabled) {
		printk(KERN_ERR "netconsole: target (%s) is enabled, "
				"disable to update parameters\n",
				config_item_name(&nt->item));
		return -EINVAL;
	}

	nt->np.remote_ip = in_aton(buf);

	return strnlen(buf, count);
}

static ssize_t store_remote_mac(struct netconsole_target *nt,
				const char *buf,
				size_t count)
{
	u8 remote_mac[ETH_ALEN];
	char *p = (char *) buf;
	int i;

	if (nt->enabled) {
		printk(KERN_ERR "netconsole: target (%s) is enabled, "
				"disable to update parameters\n",
				config_item_name(&nt->item));
		return -EINVAL;
	}

	for (i = 0; i < ETH_ALEN - 1; i++) {
		remote_mac[i] = simple_strtoul(p, &p, 16);
		if (*p != ':')
			goto invalid;
		p++;
	}
	remote_mac[ETH_ALEN - 1] = simple_strtoul(p, &p, 16);
	if (*p && (*p != '\n'))
		goto invalid;

	memcpy(nt->np.remote_mac, remote_mac, ETH_ALEN);

	return strnlen(buf, count);

invalid:
	printk(KERN_ERR "netconsole: invalid input\n");
	return -EINVAL;
}

/*
 * Attribute definitions for netconsole_target.
 */

#define NETCONSOLE_TARGET_ATTR_RO(_name)				\
static struct netconsole_target_attr netconsole_target_##_name =	\
	__CONFIGFS_ATTR(_name, S_IRUGO, show_##_name, NULL)

#define NETCONSOLE_TARGET_ATTR_RW(_name)				\
static struct netconsole_target_attr netconsole_target_##_name =	\
	__CONFIGFS_ATTR(_name, S_IRUGO | S_IWUSR, show_##_name, store_##_name)

NETCONSOLE_TARGET_ATTR_RW(enabled);
NETCONSOLE_TARGET_ATTR_RW(dev_name);
NETCONSOLE_TARGET_ATTR_RW(local_port);
NETCONSOLE_TARGET_ATTR_RW(remote_port);
NETCONSOLE_TARGET_ATTR_RW(local_ip);
NETCONSOLE_TARGET_ATTR_RW(remote_ip);
NETCONSOLE_TARGET_ATTR_RO(local_mac);
NETCONSOLE_TARGET_ATTR_RW(remote_mac);

static struct configfs_attribute *netconsole_target_attrs[] = {
	&netconsole_target_enabled.attr,
	&netconsole_target_dev_name.attr,
	&netconsole_target_local_port.attr,
	&netconsole_target_remote_port.attr,
	&netconsole_target_local_ip.attr,
	&netconsole_target_remote_ip.attr,
	&netconsole_target_local_mac.attr,
	&netconsole_target_remote_mac.attr,
	NULL,
};

/*
 * Item operations and type for netconsole_target.
 */

static void netconsole_target_release(struct config_item *item)
{
	kfree(to_target(item));
}

static ssize_t netconsole_target_attr_show(struct config_item *item,
					   struct configfs_attribute *attr,
					   char *buf)
{
	ssize_t ret = -EINVAL;
	struct netconsole_target *nt = to_target(item);
	struct netconsole_target_attr *na =
		container_of(attr, struct netconsole_target_attr, attr);

	if (na->show)
		ret = na->show(nt, buf);

	return ret;
}

static ssize_t netconsole_target_attr_store(struct config_item *item,
					    struct configfs_attribute *attr,
					    const char *buf,
					    size_t count)
{
	ssize_t ret = -EINVAL;
	struct netconsole_target *nt = to_target(item);
	struct netconsole_target_attr *na =
		container_of(attr, struct netconsole_target_attr, attr);

	if (na->store)
		ret = na->store(nt, buf, count);

	return ret;
}

static struct configfs_item_operations netconsole_target_item_ops = {
	.release		= netconsole_target_release,
	.show_attribute		= netconsole_target_attr_show,
	.store_attribute	= netconsole_target_attr_store,
};

static struct config_item_type netconsole_target_type = {
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
	 * Target is disabled at creation (enabled == 0).
	 */
	nt = kzalloc(sizeof(*nt), GFP_KERNEL);
	if (!nt) {
		printk(KERN_ERR "netconsole: failed to allocate memory\n");
		return ERR_PTR(-ENOMEM);
	}

	nt->np.name = "netconsole";
	strlcpy(nt->np.dev_name, "eth0", IFNAMSIZ);
	nt->np.local_port = 6665;
	nt->np.remote_port = 6666;
	memset(nt->np.remote_mac, 0xff, ETH_ALEN);

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

static struct config_item_type netconsole_subsys_type = {
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
				   unsigned long event,
				   void *ptr)
{
	unsigned long flags;
	struct netconsole_target *nt;
	struct net_device *dev = ptr;

	if (!(event == NETDEV_CHANGENAME || event == NETDEV_UNREGISTER))
		goto done;

	spin_lock_irqsave(&target_list_lock, flags);
	list_for_each_entry(nt, &target_list, list) {
		netconsole_target_get(nt);
		if (nt->np.dev == dev) {
			switch (event) {
			case NETDEV_CHANGENAME:
				strlcpy(nt->np.dev_name, dev->name, IFNAMSIZ);
				break;
			case NETDEV_UNREGISTER:
				if (!nt->enabled)
					break;
				netpoll_cleanup(&nt->np);
				nt->enabled = 0;
				printk(KERN_INFO "netconsole: network logging stopped"
					", interface %s unregistered\n",
					dev->name);
				break;
			}
		}
		netconsole_target_put(nt);
	}
	spin_unlock_irqrestore(&target_list_lock, flags);

done:
	return NOTIFY_DONE;
}

static struct notifier_block netconsole_netdev_notifier = {
	.notifier_call  = netconsole_netdev_event,
};

static void write_msg(struct console *con, const char *msg, unsigned int len)
{
	int frag, left;
	unsigned long flags;
	struct netconsole_target *nt;
	const char *tmp;

	/* Avoid taking lock and disabling interrupts unnecessarily */
	if (list_empty(&target_list))
		return;

	spin_lock_irqsave(&target_list_lock, flags);
	list_for_each_entry(nt, &target_list, list) {
		netconsole_target_get(nt);
		if (nt->enabled && netif_running(nt->np.dev)) {
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
		netconsole_target_put(nt);
	}
	spin_unlock_irqrestore(&target_list_lock, flags);
}

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

	register_console(&netconsole);
	printk(KERN_INFO "netconsole: network logging started\n");

	return err;

undonotifier:
	unregister_netdevice_notifier(&netconsole_netdev_notifier);

fail:
	printk(KERN_ERR "netconsole: cleaning up\n");

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

module_init(init_netconsole);
module_exit(cleanup_netconsole);
