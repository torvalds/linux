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
#include <linux/u64_stats_sync.h>
#include <linux/utsname.h>
#include <linux/rtnetlink.h>

MODULE_AUTHOR("Matt Mackall <mpm@selenic.com>");
MODULE_DESCRIPTION("Console driver for network interfaces");
MODULE_LICENSE("GPL");

#define MAX_PARAM_LENGTH		256
#define MAX_EXTRADATA_ENTRY_LEN		256
#define MAX_EXTRADATA_VALUE_LEN	200
/* The number 3 comes from userdata entry format characters (' ', '=', '\n') */
#define MAX_EXTRADATA_NAME_LEN		(MAX_EXTRADATA_ENTRY_LEN - \
					MAX_EXTRADATA_VALUE_LEN - 3)
#define MAX_EXTRADATA_ITEMS		16
#define MAX_PRINT_CHUNK			1000

static char config[MAX_PARAM_LENGTH];
module_param_string(netconsole, config, MAX_PARAM_LENGTH, 0);
MODULE_PARM_DESC(netconsole, " netconsole=[src-port]@[src-ip]/[dev],[tgt-port]@<tgt-ip>/[tgt-macaddr]");

static bool oops_only;
module_param(oops_only, bool, 0600);
MODULE_PARM_DESC(oops_only, "Only log oops messages");

#define NETCONSOLE_PARAM_TARGET_PREFIX "cmdline"

#ifndef	MODULE
static int __init option_setup(char *opt)
{
	strscpy(config, opt, MAX_PARAM_LENGTH);
	return 1;
}
__setup("netconsole=", option_setup);
#endif	/* MODULE */

/* Linked list of all configured targets */
static LIST_HEAD(target_list);
/* target_cleanup_list is used to track targets that need to be cleaned outside
 * of target_list_lock. It should be cleaned in the same function it is
 * populated.
 */
static LIST_HEAD(target_cleanup_list);

/* This needs to be a spinlock because write_msg() cannot sleep */
static DEFINE_SPINLOCK(target_list_lock);
/* This needs to be a mutex because netpoll_cleanup might sleep */
static DEFINE_MUTEX(target_cleanup_list_lock);

/*
 * Console driver for extended netconsoles.  Registered on the first use to
 * avoid unnecessarily enabling ext message formatting.
 */
static struct console netconsole_ext;

struct netconsole_target_stats  {
	u64_stats_t xmit_drop_count;
	u64_stats_t enomem_count;
	struct u64_stats_sync syncp;
};

/* Features enabled in sysdata. Contrary to userdata, this data is populated by
 * the kernel. The fields are designed as bitwise flags, allowing multiple
 * features to be set in sysdata_fields.
 */
enum sysdata_feature {
	/* Populate the CPU that sends the message */
	SYSDATA_CPU_NR = BIT(0),
	/* Populate the task name (as in current->comm) in sysdata */
	SYSDATA_TASKNAME = BIT(1),
	/* Kernel release/version as part of sysdata */
	SYSDATA_RELEASE = BIT(2),
};

/**
 * struct netconsole_target - Represents a configured netconsole target.
 * @list:	Links this target into the target_list.
 * @group:	Links us into the configfs subsystem hierarchy.
 * @userdata_group:	Links to the userdata configfs hierarchy
 * @extradata_complete:	Cached, formatted string of append
 * @userdata_length:	String length of usedata in extradata_complete.
 * @sysdata_fields:	Sysdata features enabled.
 * @stats:	Packet send stats for the target. Used for debugging.
 * @enabled:	On / off knob to enable / disable target.
 *		Visible from userspace (read-write).
 *		We maintain a strict 1:1 correspondence between this and
 *		whether the corresponding netpoll is active or inactive.
 *		Also, other parameters of a target may be modified at
 *		runtime only when it is disabled (enabled == 0).
 * @extended:	Denotes whether console is extended or not.
 * @release:	Denotes whether kernel release version should be prepended
 *		to the message. Depends on extended console.
 * @np:		The netpoll structure for this target.
 *		Contains the other userspace visible parameters:
 *		dev_name	(read-write)
 *		local_port	(read-write)
 *		remote_port	(read-write)
 *		local_ip	(read-write)
 *		remote_ip	(read-write)
 *		local_mac	(read-only)
 *		remote_mac	(read-write)
 * @buf:	The buffer used to send the full msg to the network stack
 */
struct netconsole_target {
	struct list_head	list;
#ifdef	CONFIG_NETCONSOLE_DYNAMIC
	struct config_group	group;
	struct config_group	userdata_group;
	char extradata_complete[MAX_EXTRADATA_ENTRY_LEN * MAX_EXTRADATA_ITEMS];
	size_t			userdata_length;
	/* bit-wise with sysdata_feature bits */
	u32			sysdata_fields;
#endif
	struct netconsole_target_stats stats;
	bool			enabled;
	bool			extended;
	bool			release;
	struct netpoll		np;
	/* protected by target_list_lock */
	char			buf[MAX_PRINT_CHUNK];
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
	if (config_item_name(&nt->group.cg_item))
		config_group_get(&nt->group);
}

static void netconsole_target_put(struct netconsole_target *nt)
{
	if (config_item_name(&nt->group.cg_item))
		config_group_put(&nt->group);
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

static void populate_configfs_item(struct netconsole_target *nt,
				   int cmdline_count)
{
}
#endif	/* CONFIG_NETCONSOLE_DYNAMIC */

/* Allocate and initialize with defaults.
 * Note that these targets get their config_item fields zeroed-out.
 */
static struct netconsole_target *alloc_and_init(void)
{
	struct netconsole_target *nt;

	nt = kzalloc(sizeof(*nt), GFP_KERNEL);
	if (!nt)
		return nt;

	if (IS_ENABLED(CONFIG_NETCONSOLE_EXTENDED_LOG))
		nt->extended = true;
	if (IS_ENABLED(CONFIG_NETCONSOLE_PREPEND_RELEASE))
		nt->release = true;

	nt->np.name = "netconsole";
	strscpy(nt->np.dev_name, "eth0", IFNAMSIZ);
	nt->np.local_port = 6665;
	nt->np.remote_port = 6666;
	eth_broadcast_addr(nt->np.remote_mac);

	return nt;
}

/* Clean up every target in the cleanup_list and move the clean targets back to
 * the main target_list.
 */
static void netconsole_process_cleanups_core(void)
{
	struct netconsole_target *nt, *tmp;
	unsigned long flags;

	/* The cleanup needs RTNL locked */
	ASSERT_RTNL();

	mutex_lock(&target_cleanup_list_lock);
	list_for_each_entry_safe(nt, tmp, &target_cleanup_list, list) {
		/* all entries in the cleanup_list needs to be disabled */
		WARN_ON_ONCE(nt->enabled);
		do_netpoll_cleanup(&nt->np);
		/* moved the cleaned target to target_list. Need to hold both
		 * locks
		 */
		spin_lock_irqsave(&target_list_lock, flags);
		list_move(&nt->list, &target_list);
		spin_unlock_irqrestore(&target_list_lock, flags);
	}
	WARN_ON_ONCE(!list_empty(&target_cleanup_list));
	mutex_unlock(&target_cleanup_list_lock);
}

#ifdef	CONFIG_NETCONSOLE_DYNAMIC

/*
 * Our subsystem hierarchy is:
 *
 * /sys/kernel/config/netconsole/
 *				|
 *				<target>/
 *				|	enabled
 *				|	release
 *				|	dev_name
 *				|	local_port
 *				|	remote_port
 *				|	local_ip
 *				|	remote_ip
 *				|	local_mac
 *				|	remote_mac
 *				|	transmit_errors
 *				|	userdata/
 *				|		<key>/
 *				|			value
 *				|		...
 *				|
 *				<target>/...
 */

static struct netconsole_target *to_target(struct config_item *item)
{
	struct config_group *cfg_group;

	cfg_group = to_config_group(item);
	if (!cfg_group)
		return NULL;
	return container_of(to_config_group(item),
			    struct netconsole_target, group);
}

/* Do the list cleanup with the rtnl lock hold.  rtnl lock is necessary because
 * netdev might be cleaned-up by calling __netpoll_cleanup(),
 */
static void netconsole_process_cleanups(void)
{
	/* rtnl lock is called here, because it has precedence over
	 * target_cleanup_list_lock mutex and target_cleanup_list
	 */
	rtnl_lock();
	netconsole_process_cleanups_core();
	rtnl_unlock();
}

/* Get rid of possible trailing newline, returning the new length */
static void trim_newline(char *s, size_t maxlen)
{
	size_t len;

	len = strnlen(s, maxlen);
	if (s[len - 1] == '\n')
		s[len - 1] = '\0';
}

/*
 * Attribute operations for netconsole_target.
 */

static ssize_t enabled_show(struct config_item *item, char *buf)
{
	return sysfs_emit(buf, "%d\n", to_target(item)->enabled);
}

static ssize_t extended_show(struct config_item *item, char *buf)
{
	return sysfs_emit(buf, "%d\n", to_target(item)->extended);
}

static ssize_t release_show(struct config_item *item, char *buf)
{
	return sysfs_emit(buf, "%d\n", to_target(item)->release);
}

static ssize_t dev_name_show(struct config_item *item, char *buf)
{
	return sysfs_emit(buf, "%s\n", to_target(item)->np.dev_name);
}

static ssize_t local_port_show(struct config_item *item, char *buf)
{
	return sysfs_emit(buf, "%d\n", to_target(item)->np.local_port);
}

static ssize_t remote_port_show(struct config_item *item, char *buf)
{
	return sysfs_emit(buf, "%d\n", to_target(item)->np.remote_port);
}

static ssize_t local_ip_show(struct config_item *item, char *buf)
{
	struct netconsole_target *nt = to_target(item);

	if (nt->np.ipv6)
		return sysfs_emit(buf, "%pI6c\n", &nt->np.local_ip.in6);
	else
		return sysfs_emit(buf, "%pI4\n", &nt->np.local_ip);
}

static ssize_t remote_ip_show(struct config_item *item, char *buf)
{
	struct netconsole_target *nt = to_target(item);

	if (nt->np.ipv6)
		return sysfs_emit(buf, "%pI6c\n", &nt->np.remote_ip.in6);
	else
		return sysfs_emit(buf, "%pI4\n", &nt->np.remote_ip);
}

static ssize_t local_mac_show(struct config_item *item, char *buf)
{
	struct net_device *dev = to_target(item)->np.dev;
	static const u8 bcast[ETH_ALEN] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

	return sysfs_emit(buf, "%pM\n", dev ? dev->dev_addr : bcast);
}

static ssize_t remote_mac_show(struct config_item *item, char *buf)
{
	return sysfs_emit(buf, "%pM\n", to_target(item)->np.remote_mac);
}

static ssize_t transmit_errors_show(struct config_item *item, char *buf)
{
	struct netconsole_target *nt = to_target(item);
	u64 xmit_drop_count, enomem_count;
	unsigned int start;

	do {
		start = u64_stats_fetch_begin(&nt->stats.syncp);
		xmit_drop_count = u64_stats_read(&nt->stats.xmit_drop_count);
		enomem_count = u64_stats_read(&nt->stats.enomem_count);
	} while (u64_stats_fetch_retry(&nt->stats.syncp, start));

	return sysfs_emit(buf, "%llu\n", xmit_drop_count + enomem_count);
}

/* configfs helper to display if cpu_nr sysdata feature is enabled */
static ssize_t sysdata_cpu_nr_enabled_show(struct config_item *item, char *buf)
{
	struct netconsole_target *nt = to_target(item->ci_parent);
	bool cpu_nr_enabled;

	mutex_lock(&dynamic_netconsole_mutex);
	cpu_nr_enabled = !!(nt->sysdata_fields & SYSDATA_CPU_NR);
	mutex_unlock(&dynamic_netconsole_mutex);

	return sysfs_emit(buf, "%d\n", cpu_nr_enabled);
}

/* configfs helper to display if taskname sysdata feature is enabled */
static ssize_t sysdata_taskname_enabled_show(struct config_item *item,
					     char *buf)
{
	struct netconsole_target *nt = to_target(item->ci_parent);
	bool taskname_enabled;

	mutex_lock(&dynamic_netconsole_mutex);
	taskname_enabled = !!(nt->sysdata_fields & SYSDATA_TASKNAME);
	mutex_unlock(&dynamic_netconsole_mutex);

	return sysfs_emit(buf, "%d\n", taskname_enabled);
}

static ssize_t sysdata_release_enabled_show(struct config_item *item,
					    char *buf)
{
	struct netconsole_target *nt = to_target(item->ci_parent);
	bool release_enabled;

	mutex_lock(&dynamic_netconsole_mutex);
	release_enabled = !!(nt->sysdata_fields & SYSDATA_TASKNAME);
	mutex_unlock(&dynamic_netconsole_mutex);

	return sysfs_emit(buf, "%d\n", release_enabled);
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
	bool enabled;
	ssize_t ret;

	mutex_lock(&dynamic_netconsole_mutex);
	ret = kstrtobool(buf, &enabled);
	if (ret)
		goto out_unlock;

	ret = -EINVAL;
	if (enabled == nt->enabled) {
		pr_info("network logging has already %s\n",
			nt->enabled ? "started" : "stopped");
		goto out_unlock;
	}

	if (enabled) {	/* true */
		if (nt->release && !nt->extended) {
			pr_err("Not enabling netconsole. Release feature requires extended log message");
			goto out_unlock;
		}

		if (nt->extended && !console_is_registered(&netconsole_ext))
			register_console(&netconsole_ext);

		/*
		 * Skip netpoll_parse_options() -- all the attributes are
		 * already configured via configfs. Just print them out.
		 */
		netpoll_print_options(&nt->np);

		ret = netpoll_setup(&nt->np);
		if (ret)
			goto out_unlock;

		nt->enabled = true;
		pr_info("network logging started\n");
	} else {	/* false */
		/* We need to disable the netconsole before cleaning it up
		 * otherwise we might end up in write_msg() with
		 * nt->np.dev == NULL and nt->enabled == true
		 */
		mutex_lock(&target_cleanup_list_lock);
		spin_lock_irqsave(&target_list_lock, flags);
		nt->enabled = false;
		/* Remove the target from the list, while holding
		 * target_list_lock
		 */
		list_move(&nt->list, &target_cleanup_list);
		spin_unlock_irqrestore(&target_list_lock, flags);
		mutex_unlock(&target_cleanup_list_lock);
	}

	ret = strnlen(buf, count);
	/* Deferred cleanup */
	netconsole_process_cleanups();
out_unlock:
	mutex_unlock(&dynamic_netconsole_mutex);
	return ret;
}

static ssize_t release_store(struct config_item *item, const char *buf,
			     size_t count)
{
	struct netconsole_target *nt = to_target(item);
	bool release;
	ssize_t ret;

	mutex_lock(&dynamic_netconsole_mutex);
	if (nt->enabled) {
		pr_err("target (%s) is enabled, disable to update parameters\n",
		       config_item_name(&nt->group.cg_item));
		ret = -EINVAL;
		goto out_unlock;
	}

	ret = kstrtobool(buf, &release);
	if (ret)
		goto out_unlock;

	nt->release = release;

	ret = strnlen(buf, count);
out_unlock:
	mutex_unlock(&dynamic_netconsole_mutex);
	return ret;
}

static ssize_t extended_store(struct config_item *item, const char *buf,
		size_t count)
{
	struct netconsole_target *nt = to_target(item);
	bool extended;
	ssize_t ret;

	mutex_lock(&dynamic_netconsole_mutex);
	if (nt->enabled) {
		pr_err("target (%s) is enabled, disable to update parameters\n",
		       config_item_name(&nt->group.cg_item));
		ret = -EINVAL;
		goto out_unlock;
	}

	ret = kstrtobool(buf, &extended);
	if (ret)
		goto out_unlock;

	nt->extended = extended;
	ret = strnlen(buf, count);
out_unlock:
	mutex_unlock(&dynamic_netconsole_mutex);
	return ret;
}

static ssize_t dev_name_store(struct config_item *item, const char *buf,
		size_t count)
{
	struct netconsole_target *nt = to_target(item);

	mutex_lock(&dynamic_netconsole_mutex);
	if (nt->enabled) {
		pr_err("target (%s) is enabled, disable to update parameters\n",
		       config_item_name(&nt->group.cg_item));
		mutex_unlock(&dynamic_netconsole_mutex);
		return -EINVAL;
	}

	strscpy(nt->np.dev_name, buf, IFNAMSIZ);
	trim_newline(nt->np.dev_name, IFNAMSIZ);

	mutex_unlock(&dynamic_netconsole_mutex);
	return strnlen(buf, count);
}

static ssize_t local_port_store(struct config_item *item, const char *buf,
		size_t count)
{
	struct netconsole_target *nt = to_target(item);
	ssize_t ret = -EINVAL;

	mutex_lock(&dynamic_netconsole_mutex);
	if (nt->enabled) {
		pr_err("target (%s) is enabled, disable to update parameters\n",
		       config_item_name(&nt->group.cg_item));
		goto out_unlock;
	}

	ret = kstrtou16(buf, 10, &nt->np.local_port);
	if (ret < 0)
		goto out_unlock;
	ret = strnlen(buf, count);
out_unlock:
	mutex_unlock(&dynamic_netconsole_mutex);
	return ret;
}

static ssize_t remote_port_store(struct config_item *item,
		const char *buf, size_t count)
{
	struct netconsole_target *nt = to_target(item);
	ssize_t ret = -EINVAL;

	mutex_lock(&dynamic_netconsole_mutex);
	if (nt->enabled) {
		pr_err("target (%s) is enabled, disable to update parameters\n",
		       config_item_name(&nt->group.cg_item));
		goto out_unlock;
	}

	ret = kstrtou16(buf, 10, &nt->np.remote_port);
	if (ret < 0)
		goto out_unlock;
	ret = strnlen(buf, count);
out_unlock:
	mutex_unlock(&dynamic_netconsole_mutex);
	return ret;
}

static ssize_t local_ip_store(struct config_item *item, const char *buf,
		size_t count)
{
	struct netconsole_target *nt = to_target(item);
	ssize_t ret = -EINVAL;

	mutex_lock(&dynamic_netconsole_mutex);
	if (nt->enabled) {
		pr_err("target (%s) is enabled, disable to update parameters\n",
		       config_item_name(&nt->group.cg_item));
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
		if (!nt->np.ipv6)
			nt->np.local_ip.ip = in_aton(buf);
		else
			goto out_unlock;
	}

	ret = strnlen(buf, count);
out_unlock:
	mutex_unlock(&dynamic_netconsole_mutex);
	return ret;
}

static ssize_t remote_ip_store(struct config_item *item, const char *buf,
	       size_t count)
{
	struct netconsole_target *nt = to_target(item);
	ssize_t ret = -EINVAL;

	mutex_lock(&dynamic_netconsole_mutex);
	if (nt->enabled) {
		pr_err("target (%s) is enabled, disable to update parameters\n",
		       config_item_name(&nt->group.cg_item));
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
		if (!nt->np.ipv6)
			nt->np.remote_ip.ip = in_aton(buf);
		else
			goto out_unlock;
	}

	ret = strnlen(buf, count);
out_unlock:
	mutex_unlock(&dynamic_netconsole_mutex);
	return ret;
}

/* Count number of entries we have in extradata.
 * This is important because the extradata_complete only supports
 * MAX_EXTRADATA_ITEMS entries. Before enabling any new {user,sys}data
 * feature, number of entries needs to checked for available space.
 */
static size_t count_extradata_entries(struct netconsole_target *nt)
{
	size_t entries;

	/* Userdata entries */
	entries = list_count_nodes(&nt->userdata_group.cg_children);
	/* Plus sysdata entries */
	if (nt->sysdata_fields & SYSDATA_CPU_NR)
		entries += 1;
	if (nt->sysdata_fields & SYSDATA_TASKNAME)
		entries += 1;
	if (nt->sysdata_fields & SYSDATA_RELEASE)
		entries += 1;

	return entries;
}

static ssize_t remote_mac_store(struct config_item *item, const char *buf,
		size_t count)
{
	struct netconsole_target *nt = to_target(item);
	u8 remote_mac[ETH_ALEN];
	ssize_t ret = -EINVAL;

	mutex_lock(&dynamic_netconsole_mutex);
	if (nt->enabled) {
		pr_err("target (%s) is enabled, disable to update parameters\n",
		       config_item_name(&nt->group.cg_item));
		goto out_unlock;
	}

	if (!mac_pton(buf, remote_mac))
		goto out_unlock;
	if (buf[MAC_ADDR_STR_LEN] && buf[MAC_ADDR_STR_LEN] != '\n')
		goto out_unlock;
	memcpy(nt->np.remote_mac, remote_mac, ETH_ALEN);

	ret = strnlen(buf, count);
out_unlock:
	mutex_unlock(&dynamic_netconsole_mutex);
	return ret;
}

struct userdatum {
	struct config_item item;
	char value[MAX_EXTRADATA_VALUE_LEN];
};

static struct userdatum *to_userdatum(struct config_item *item)
{
	return container_of(item, struct userdatum, item);
}

struct userdata {
	struct config_group group;
};

static struct userdata *to_userdata(struct config_item *item)
{
	return container_of(to_config_group(item), struct userdata, group);
}

static struct netconsole_target *userdata_to_target(struct userdata *ud)
{
	struct config_group *netconsole_group;

	netconsole_group = to_config_group(ud->group.cg_item.ci_parent);
	return to_target(&netconsole_group->cg_item);
}

static ssize_t userdatum_value_show(struct config_item *item, char *buf)
{
	return sysfs_emit(buf, "%s\n", &(to_userdatum(item)->value[0]));
}

static void update_userdata(struct netconsole_target *nt)
{
	int complete_idx = 0, child_count = 0;
	struct list_head *entry;

	/* Clear the current string in case the last userdatum was deleted */
	nt->userdata_length = 0;
	nt->extradata_complete[0] = 0;

	list_for_each(entry, &nt->userdata_group.cg_children) {
		struct userdatum *udm_item;
		struct config_item *item;

		if (WARN_ON_ONCE(child_count >= MAX_EXTRADATA_ITEMS))
			break;
		child_count++;

		item = container_of(entry, struct config_item, ci_entry);
		udm_item = to_userdatum(item);

		/* Skip userdata with no value set */
		if (strnlen(udm_item->value, MAX_EXTRADATA_VALUE_LEN) == 0)
			continue;

		/* This doesn't overflow extradata_complete since it will write
		 * one entry length (1/MAX_EXTRADATA_ITEMS long), entry count is
		 * checked to not exceed MAX items with child_count above
		 */
		complete_idx += scnprintf(&nt->extradata_complete[complete_idx],
					  MAX_EXTRADATA_ENTRY_LEN, " %s=%s\n",
					  item->ci_name, udm_item->value);
	}
	nt->userdata_length = strnlen(nt->extradata_complete,
				      sizeof(nt->extradata_complete));
}

static ssize_t userdatum_value_store(struct config_item *item, const char *buf,
				     size_t count)
{
	struct userdatum *udm = to_userdatum(item);
	struct netconsole_target *nt;
	struct userdata *ud;
	ssize_t ret;

	if (count > MAX_EXTRADATA_VALUE_LEN)
		return -EMSGSIZE;

	mutex_lock(&dynamic_netconsole_mutex);

	ret = strscpy(udm->value, buf, sizeof(udm->value));
	if (ret < 0)
		goto out_unlock;
	trim_newline(udm->value, sizeof(udm->value));

	ud = to_userdata(item->ci_parent);
	nt = userdata_to_target(ud);
	update_userdata(nt);
	ret = count;
out_unlock:
	mutex_unlock(&dynamic_netconsole_mutex);
	return ret;
}

/* disable_sysdata_feature - Disable sysdata feature and clean sysdata
 * @nt: target that is disabling the feature
 * @feature: feature being disabled
 */
static void disable_sysdata_feature(struct netconsole_target *nt,
				    enum sysdata_feature feature)
{
	nt->sysdata_fields &= ~feature;
	nt->extradata_complete[nt->userdata_length] = 0;
}

static ssize_t sysdata_release_enabled_store(struct config_item *item,
					     const char *buf, size_t count)
{
	struct netconsole_target *nt = to_target(item->ci_parent);
	bool release_enabled, curr;
	ssize_t ret;

	ret = kstrtobool(buf, &release_enabled);
	if (ret)
		return ret;

	mutex_lock(&dynamic_netconsole_mutex);
	curr = !!(nt->sysdata_fields & SYSDATA_RELEASE);
	if (release_enabled == curr)
		goto unlock_ok;

	if (release_enabled &&
	    count_extradata_entries(nt) >= MAX_EXTRADATA_ITEMS) {
		ret = -ENOSPC;
		goto unlock;
	}

	if (release_enabled)
		nt->sysdata_fields |= SYSDATA_RELEASE;
	else
		disable_sysdata_feature(nt, SYSDATA_RELEASE);

unlock_ok:
	ret = strnlen(buf, count);
unlock:
	mutex_unlock(&dynamic_netconsole_mutex);
	return ret;
}

static ssize_t sysdata_taskname_enabled_store(struct config_item *item,
					      const char *buf, size_t count)
{
	struct netconsole_target *nt = to_target(item->ci_parent);
	bool taskname_enabled, curr;
	ssize_t ret;

	ret = kstrtobool(buf, &taskname_enabled);
	if (ret)
		return ret;

	mutex_lock(&dynamic_netconsole_mutex);
	curr = !!(nt->sysdata_fields & SYSDATA_TASKNAME);
	if (taskname_enabled == curr)
		goto unlock_ok;

	if (taskname_enabled &&
	    count_extradata_entries(nt) >= MAX_EXTRADATA_ITEMS) {
		ret = -ENOSPC;
		goto unlock;
	}

	if (taskname_enabled)
		nt->sysdata_fields |= SYSDATA_TASKNAME;
	else
		disable_sysdata_feature(nt, SYSDATA_TASKNAME);

unlock_ok:
	ret = strnlen(buf, count);
unlock:
	mutex_unlock(&dynamic_netconsole_mutex);
	return ret;
}

/* configfs helper to sysdata cpu_nr feature */
static ssize_t sysdata_cpu_nr_enabled_store(struct config_item *item,
					    const char *buf, size_t count)
{
	struct netconsole_target *nt = to_target(item->ci_parent);
	bool cpu_nr_enabled, curr;
	ssize_t ret;

	ret = kstrtobool(buf, &cpu_nr_enabled);
	if (ret)
		return ret;

	mutex_lock(&dynamic_netconsole_mutex);
	curr = !!(nt->sysdata_fields & SYSDATA_CPU_NR);
	if (cpu_nr_enabled == curr)
		/* no change requested */
		goto unlock_ok;

	if (cpu_nr_enabled &&
	    count_extradata_entries(nt) >= MAX_EXTRADATA_ITEMS) {
		/* user wants the new feature, but there is no space in the
		 * buffer.
		 */
		ret = -ENOSPC;
		goto unlock;
	}

	if (cpu_nr_enabled)
		nt->sysdata_fields |= SYSDATA_CPU_NR;
	else
		/* This is special because extradata_complete might have
		 * remaining data from previous sysdata, and it needs to be
		 * cleaned.
		 */
		disable_sysdata_feature(nt, SYSDATA_CPU_NR);

unlock_ok:
	ret = strnlen(buf, count);
unlock:
	mutex_unlock(&dynamic_netconsole_mutex);
	return ret;
}

CONFIGFS_ATTR(userdatum_, value);
CONFIGFS_ATTR(sysdata_, cpu_nr_enabled);
CONFIGFS_ATTR(sysdata_, taskname_enabled);
CONFIGFS_ATTR(sysdata_, release_enabled);

static struct configfs_attribute *userdatum_attrs[] = {
	&userdatum_attr_value,
	NULL,
};

static void userdatum_release(struct config_item *item)
{
	kfree(to_userdatum(item));
}

static struct configfs_item_operations userdatum_ops = {
	.release = userdatum_release,
};

static const struct config_item_type userdatum_type = {
	.ct_item_ops	= &userdatum_ops,
	.ct_attrs	= userdatum_attrs,
	.ct_owner	= THIS_MODULE,
};

static struct config_item *userdatum_make_item(struct config_group *group,
					       const char *name)
{
	struct netconsole_target *nt;
	struct userdatum *udm;
	struct userdata *ud;

	if (strlen(name) > MAX_EXTRADATA_NAME_LEN)
		return ERR_PTR(-ENAMETOOLONG);

	ud = to_userdata(&group->cg_item);
	nt = userdata_to_target(ud);
	if (count_extradata_entries(nt) >= MAX_EXTRADATA_ITEMS)
		return ERR_PTR(-ENOSPC);

	udm = kzalloc(sizeof(*udm), GFP_KERNEL);
	if (!udm)
		return ERR_PTR(-ENOMEM);

	config_item_init_type_name(&udm->item, name, &userdatum_type);
	return &udm->item;
}

static void userdatum_drop(struct config_group *group, struct config_item *item)
{
	struct netconsole_target *nt;
	struct userdata *ud;

	ud = to_userdata(&group->cg_item);
	nt = userdata_to_target(ud);

	mutex_lock(&dynamic_netconsole_mutex);
	update_userdata(nt);
	config_item_put(item);
	mutex_unlock(&dynamic_netconsole_mutex);
}

static struct configfs_attribute *userdata_attrs[] = {
	&sysdata_attr_cpu_nr_enabled,
	&sysdata_attr_taskname_enabled,
	&sysdata_attr_release_enabled,
	NULL,
};

static struct configfs_group_operations userdata_ops = {
	.make_item		= userdatum_make_item,
	.drop_item		= userdatum_drop,
};

static const struct config_item_type userdata_type = {
	.ct_item_ops	= &userdatum_ops,
	.ct_group_ops	= &userdata_ops,
	.ct_attrs	= userdata_attrs,
	.ct_owner	= THIS_MODULE,
};

CONFIGFS_ATTR(, enabled);
CONFIGFS_ATTR(, extended);
CONFIGFS_ATTR(, dev_name);
CONFIGFS_ATTR(, local_port);
CONFIGFS_ATTR(, remote_port);
CONFIGFS_ATTR(, local_ip);
CONFIGFS_ATTR(, remote_ip);
CONFIGFS_ATTR_RO(, local_mac);
CONFIGFS_ATTR(, remote_mac);
CONFIGFS_ATTR(, release);
CONFIGFS_ATTR_RO(, transmit_errors);

static struct configfs_attribute *netconsole_target_attrs[] = {
	&attr_enabled,
	&attr_extended,
	&attr_release,
	&attr_dev_name,
	&attr_local_port,
	&attr_remote_port,
	&attr_local_ip,
	&attr_remote_ip,
	&attr_local_mac,
	&attr_remote_mac,
	&attr_transmit_errors,
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

static void init_target_config_group(struct netconsole_target *nt,
				     const char *name)
{
	config_group_init_type_name(&nt->group, name, &netconsole_target_type);
	config_group_init_type_name(&nt->userdata_group, "userdata",
				    &userdata_type);
	configfs_add_default_group(&nt->userdata_group, &nt->group);
}

static struct netconsole_target *find_cmdline_target(const char *name)
{
	struct netconsole_target *nt, *ret = NULL;
	unsigned long flags;

	spin_lock_irqsave(&target_list_lock, flags);
	list_for_each_entry(nt, &target_list, list) {
		if (!strcmp(nt->group.cg_item.ci_name, name)) {
			ret = nt;
			break;
		}
	}
	spin_unlock_irqrestore(&target_list_lock, flags);

	return ret;
}

/*
 * Group operations and type for netconsole_subsys.
 */

static struct config_group *make_netconsole_target(struct config_group *group,
						   const char *name)
{
	struct netconsole_target *nt;
	unsigned long flags;

	/* Checking if a target by this name was created at boot time.  If so,
	 * attach a configfs entry to that target.  This enables dynamic
	 * control.
	 */
	if (!strncmp(name, NETCONSOLE_PARAM_TARGET_PREFIX,
		     strlen(NETCONSOLE_PARAM_TARGET_PREFIX))) {
		nt = find_cmdline_target(name);
		if (nt) {
			init_target_config_group(nt, name);
			return &nt->group;
		}
	}

	nt = alloc_and_init();
	if (!nt)
		return ERR_PTR(-ENOMEM);

	/* Initialize the config_group member */
	init_target_config_group(nt, name);

	/* Adding, but it is disabled */
	spin_lock_irqsave(&target_list_lock, flags);
	list_add(&nt->list, &target_list);
	spin_unlock_irqrestore(&target_list_lock, flags);

	return &nt->group;
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

	config_item_put(&nt->group.cg_item);
}

static struct configfs_group_operations netconsole_subsys_group_ops = {
	.make_group	= make_netconsole_target,
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

static void populate_configfs_item(struct netconsole_target *nt,
				   int cmdline_count)
{
	char target_name[16];

	snprintf(target_name, sizeof(target_name), "%s%d",
		 NETCONSOLE_PARAM_TARGET_PREFIX, cmdline_count);
	init_target_config_group(nt, target_name);
}

static int sysdata_append_cpu_nr(struct netconsole_target *nt, int offset)
{
	/* Append cpu=%d at extradata_complete after userdata str */
	return scnprintf(&nt->extradata_complete[offset],
			 MAX_EXTRADATA_ENTRY_LEN, " cpu=%u\n",
			 raw_smp_processor_id());
}

static int sysdata_append_taskname(struct netconsole_target *nt, int offset)
{
	return scnprintf(&nt->extradata_complete[offset],
			 MAX_EXTRADATA_ENTRY_LEN, " taskname=%s\n",
			 current->comm);
}

static int sysdata_append_release(struct netconsole_target *nt, int offset)
{
	return scnprintf(&nt->extradata_complete[offset],
			 MAX_EXTRADATA_ENTRY_LEN, " release=%s\n",
			 init_utsname()->release);
}

/*
 * prepare_extradata - append sysdata at extradata_complete in runtime
 * @nt: target to send message to
 */
static int prepare_extradata(struct netconsole_target *nt)
{
	u32 fields = SYSDATA_CPU_NR | SYSDATA_TASKNAME;
	int extradata_len;

	/* userdata was appended when configfs write helper was called
	 * by update_userdata().
	 */
	extradata_len = nt->userdata_length;

	if (!(nt->sysdata_fields & fields))
		goto out;

	if (nt->sysdata_fields & SYSDATA_CPU_NR)
		extradata_len += sysdata_append_cpu_nr(nt, extradata_len);
	if (nt->sysdata_fields & SYSDATA_TASKNAME)
		extradata_len += sysdata_append_taskname(nt, extradata_len);
	if (nt->sysdata_fields & SYSDATA_RELEASE)
		extradata_len += sysdata_append_release(nt, extradata_len);

	WARN_ON_ONCE(extradata_len >
		     MAX_EXTRADATA_ENTRY_LEN * MAX_EXTRADATA_ITEMS);

out:
	return extradata_len;
}
#else /* CONFIG_NETCONSOLE_DYNAMIC not set */
static int prepare_extradata(struct netconsole_target *nt)
{
	return 0;
}
#endif	/* CONFIG_NETCONSOLE_DYNAMIC */

/* Handle network interface device notifications */
static int netconsole_netdev_event(struct notifier_block *this,
				   unsigned long event, void *ptr)
{
	unsigned long flags;
	struct netconsole_target *nt, *tmp;
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);
	bool stopped = false;

	if (!(event == NETDEV_CHANGENAME || event == NETDEV_UNREGISTER ||
	      event == NETDEV_RELEASE || event == NETDEV_JOIN))
		goto done;

	mutex_lock(&target_cleanup_list_lock);
	spin_lock_irqsave(&target_list_lock, flags);
	list_for_each_entry_safe(nt, tmp, &target_list, list) {
		netconsole_target_get(nt);
		if (nt->np.dev == dev) {
			switch (event) {
			case NETDEV_CHANGENAME:
				strscpy(nt->np.dev_name, dev->name, IFNAMSIZ);
				break;
			case NETDEV_RELEASE:
			case NETDEV_JOIN:
			case NETDEV_UNREGISTER:
				nt->enabled = false;
				list_move(&nt->list, &target_cleanup_list);
				stopped = true;
			}
		}
		netconsole_target_put(nt);
	}
	spin_unlock_irqrestore(&target_list_lock, flags);
	mutex_unlock(&target_cleanup_list_lock);

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

	/* Process target_cleanup_list entries. By the end, target_cleanup_list
	 * should be empty
	 */
	netconsole_process_cleanups_core();

done:
	return NOTIFY_DONE;
}

static struct notifier_block netconsole_netdev_notifier = {
	.notifier_call  = netconsole_netdev_event,
};

/**
 * send_udp - Wrapper for netpoll_send_udp that counts errors
 * @nt: target to send message to
 * @msg: message to send
 * @len: length of message
 *
 * Calls netpoll_send_udp and classifies the return value. If an error
 * occurred it increments statistics in nt->stats accordingly.
 * Only calls netpoll_send_udp if CONFIG_NETCONSOLE_DYNAMIC is disabled.
 */
static void send_udp(struct netconsole_target *nt, const char *msg, int len)
{
	int result = netpoll_send_udp(&nt->np, msg, len);

	if (IS_ENABLED(CONFIG_NETCONSOLE_DYNAMIC)) {
		if (result == NET_XMIT_DROP) {
			u64_stats_update_begin(&nt->stats.syncp);
			u64_stats_inc(&nt->stats.xmit_drop_count);
			u64_stats_update_end(&nt->stats.syncp);
		} else if (result == -ENOMEM) {
			u64_stats_update_begin(&nt->stats.syncp);
			u64_stats_inc(&nt->stats.enomem_count);
			u64_stats_update_end(&nt->stats.syncp);
		}
	}
}

static void send_msg_no_fragmentation(struct netconsole_target *nt,
				      const char *msg,
				      int msg_len,
				      int release_len)
{
	const char *extradata = NULL;
	const char *release;

#ifdef CONFIG_NETCONSOLE_DYNAMIC
	extradata = nt->extradata_complete;
#endif

	if (release_len) {
		release = init_utsname()->release;

		scnprintf(nt->buf, MAX_PRINT_CHUNK, "%s,%s", release, msg);
		msg_len += release_len;
	} else {
		memcpy(nt->buf, msg, msg_len);
	}

	if (extradata)
		msg_len += scnprintf(&nt->buf[msg_len],
				     MAX_PRINT_CHUNK - msg_len,
				     "%s", extradata);

	send_udp(nt, nt->buf, msg_len);
}

static void append_release(char *buf)
{
	const char *release;

	release = init_utsname()->release;
	scnprintf(buf, MAX_PRINT_CHUNK, "%s,", release);
}

static void send_fragmented_body(struct netconsole_target *nt,
				 const char *msgbody, int header_len,
				 int msgbody_len, int extradata_len)
{
	int sent_extradata, preceding_bytes;
	const char *extradata = NULL;
	int body_len, offset = 0;

#ifdef CONFIG_NETCONSOLE_DYNAMIC
	extradata = nt->extradata_complete;
#endif

	/* body_len represents the number of bytes that will be sent. This is
	 * bigger than MAX_PRINT_CHUNK, thus, it will be split in multiple
	 * packets
	 */
	body_len = msgbody_len + extradata_len;

	/* In each iteration of the while loop below, we send a packet
	 * containing the header and a portion of the body. The body is
	 * composed of two parts: msgbody and extradata. We keep track of how
	 * many bytes have been sent so far using the offset variable, which
	 * ranges from 0 to the total length of the body.
	 */
	while (offset < body_len) {
		int this_header = header_len;
		bool msgbody_written = false;
		int this_offset = 0;
		int this_chunk = 0;

		this_header += scnprintf(nt->buf + this_header,
					 MAX_PRINT_CHUNK - this_header,
					 ",ncfrag=%d/%d;", offset,
					 body_len);

		/* Not all msgbody data has been written yet */
		if (offset < msgbody_len) {
			this_chunk = min(msgbody_len - offset,
					 MAX_PRINT_CHUNK - this_header);
			if (WARN_ON_ONCE(this_chunk <= 0))
				return;
			memcpy(nt->buf + this_header, msgbody + offset,
			       this_chunk);
			this_offset += this_chunk;
		}

		/* msgbody was finally written, either in the previous
		 * messages and/or in the current buf. Time to write
		 * the extradata.
		 */
		msgbody_written |= offset + this_offset >= msgbody_len;

		/* Msg body is fully written and there is pending extradata to
		 * write, append extradata in this chunk
		 */
		if (msgbody_written && offset + this_offset < body_len) {
			/* Track how much user data was already sent. First
			 * time here, sent_userdata is zero
			 */
			sent_extradata = (offset + this_offset) - msgbody_len;
			/* offset of bytes used in current buf */
			preceding_bytes = this_chunk + this_header;

			if (WARN_ON_ONCE(sent_extradata < 0))
				return;

			this_chunk = min(extradata_len - sent_extradata,
					 MAX_PRINT_CHUNK - preceding_bytes);
			if (WARN_ON_ONCE(this_chunk < 0))
				/* this_chunk could be zero if all the previous
				 * message used all the buffer. This is not a
				 * problem, extradata will be sent in the next
				 * iteration
				 */
				return;

			memcpy(nt->buf + this_header + this_offset,
			       extradata + sent_extradata,
			       this_chunk);
			this_offset += this_chunk;
		}

		send_udp(nt, nt->buf, this_header + this_offset);
		offset += this_offset;
	}
}

static void send_msg_fragmented(struct netconsole_target *nt,
				const char *msg,
				int msg_len,
				int release_len,
				int extradata_len)
{
	int header_len, msgbody_len;
	const char *msgbody;

	/* need to insert extra header fields, detect header and msgbody */
	msgbody = memchr(msg, ';', msg_len);
	if (WARN_ON_ONCE(!msgbody))
		return;

	header_len = msgbody - msg;
	msgbody_len = msg_len - header_len - 1;
	msgbody++;

	/*
	 * Transfer multiple chunks with the following extra header.
	 * "ncfrag=<byte-offset>/<total-bytes>"
	 */
	if (release_len)
		append_release(nt->buf);

	/* Copy the header into the buffer */
	memcpy(nt->buf + release_len, msg, header_len);
	header_len += release_len;

	/* for now on, the header will be persisted, and the msgbody
	 * will be replaced
	 */
	send_fragmented_body(nt, msgbody, header_len, msgbody_len,
			     extradata_len);
}

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
	int release_len = 0;
	int extradata_len;

	extradata_len = prepare_extradata(nt);

	if (nt->release)
		release_len = strlen(init_utsname()->release) + 1;

	if (msg_len + release_len + extradata_len <= MAX_PRINT_CHUNK)
		return send_msg_no_fragmentation(nt, msg, msg_len, release_len);

	return send_msg_fragmented(nt, msg, msg_len, release_len,
				   extradata_len);
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
				send_udp(nt, tmp, frag);
				tmp += frag;
				left -= frag;
			}
		}
	}
	spin_unlock_irqrestore(&target_list_lock, flags);
}

/* Allocate new target (from boot/module param) and setup netpoll for it */
static struct netconsole_target *alloc_param_target(char *target_config,
						    int cmdline_count)
{
	struct netconsole_target *nt;
	int err;

	nt = alloc_and_init();
	if (!nt) {
		err = -ENOMEM;
		goto fail;
	}

	if (*target_config == '+') {
		nt->extended = true;
		target_config++;
	}

	if (*target_config == 'r') {
		if (!nt->extended) {
			pr_err("Netconsole configuration error. Release feature requires extended log message");
			err = -EINVAL;
			goto fail;
		}
		nt->release = true;
		target_config++;
	}

	/* Parse parameters and setup netpoll */
	err = netpoll_parse_options(&nt->np, target_config);
	if (err)
		goto fail;

	err = netpoll_setup(&nt->np);
	if (err) {
		pr_err("Not enabling netconsole for %s%d. Netpoll setup failed\n",
		       NETCONSOLE_PARAM_TARGET_PREFIX, cmdline_count);
		if (!IS_ENABLED(CONFIG_NETCONSOLE_DYNAMIC))
			/* only fail if dynamic reconfiguration is set,
			 * otherwise, keep the target in the list, but disabled.
			 */
			goto fail;
	} else {
		nt->enabled = true;
	}
	populate_configfs_item(nt, cmdline_count);

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

static struct console netconsole_ext = {
	.name	= "netcon_ext",
	.flags	= CON_ENABLED | CON_EXTENDED,
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
	unsigned int count = 0;
	bool extended = false;
	unsigned long flags;
	char *target_config;
	char *input = config;

	if (strnlen(input, MAX_PARAM_LENGTH)) {
		while ((target_config = strsep(&input, ";"))) {
			nt = alloc_param_target(target_config, count);
			if (IS_ERR(nt)) {
				if (IS_ENABLED(CONFIG_NETCONSOLE_DYNAMIC))
					continue;
				err = PTR_ERR(nt);
				goto fail;
			}
			/* Dump existing printks when we register */
			if (nt->extended) {
				extended = true;
				netconsole_ext.flags |= CON_PRINTBUFFER;
			} else {
				netconsole.flags |= CON_PRINTBUFFER;
			}

			spin_lock_irqsave(&target_list_lock, flags);
			list_add(&nt->list, &target_list);
			spin_unlock_irqrestore(&target_list_lock, flags);
			count++;
		}
	}

	err = register_netdevice_notifier(&netconsole_netdev_notifier);
	if (err)
		goto fail;

	err = dynamic_netconsole_init();
	if (err)
		goto undonotifier;

	if (extended)
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

	if (console_is_registered(&netconsole_ext))
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
