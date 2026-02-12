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
#include <linux/hex.h>
#include <linux/u64_stats_sync.h>
#include <linux/utsname.h>
#include <linux/rtnetlink.h>
#include <linux/workqueue.h>

MODULE_AUTHOR("Matt Mackall <mpm@selenic.com>");
MODULE_DESCRIPTION("Console driver for network interfaces");
MODULE_LICENSE("GPL");

#define MAX_PARAM_LENGTH		256
#define MAX_EXTRADATA_ENTRY_LEN		256
#define MAX_EXTRADATA_VALUE_LEN	200
/* The number 3 comes from userdata entry format characters (' ', '=', '\n') */
#define MAX_EXTRADATA_NAME_LEN		(MAX_EXTRADATA_ENTRY_LEN - \
					MAX_EXTRADATA_VALUE_LEN - 3)
#define MAX_USERDATA_ITEMS		256
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

static struct workqueue_struct *netconsole_wq;

/*
 * Console driver for netconsoles.  Register only consoles that have
 * an associated target of the same type.
 */
static struct console netconsole_ext, netconsole;

struct netconsole_target_stats  {
	u64_stats_t xmit_drop_count;
	u64_stats_t enomem_count;
	struct u64_stats_sync syncp;
};

enum console_type {
	CONS_BASIC = BIT(0),
	CONS_EXTENDED = BIT(1),
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
	/* Include a per-target message ID as part of sysdata */
	SYSDATA_MSGID = BIT(3),
	/* Sentinel: highest bit position */
	MAX_SYSDATA_ITEMS = 4,
};

enum target_state {
	STATE_DISABLED,
	STATE_ENABLED,
	STATE_DEACTIVATED,
};

/**
 * struct netconsole_target - Represents a configured netconsole target.
 * @list:	Links this target into the target_list.
 * @group:	Links us into the configfs subsystem hierarchy.
 * @userdata_group:	Links to the userdata configfs hierarchy
 * @userdata:		Cached, formatted string of append
 * @userdata_length:	String length of userdata.
 * @sysdata:		Cached, formatted string of append
 * @sysdata_fields:	Sysdata features enabled.
 * @msgcounter:	Message sent counter.
 * @stats:	Packet send stats for the target. Used for debugging.
 * @state:	State of the target.
 *		Visible from userspace (read-write).
 *		From a userspace perspective, the target is either enabled or
 *		disabled. Internally, although both STATE_DISABLED and
 *		STATE_DEACTIVATED correspond to inactive targets, the latter is
 *		due to automatic interface state changes and will try
 *		recover automatically, if the interface comes back
 *		online.
 *		Also, other parameters of a target may be modified at
 *		runtime only when it is disabled (state != STATE_ENABLED).
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
 * @resume_wq:	Workqueue to resume deactivated target
 */
struct netconsole_target {
	struct list_head	list;
#ifdef	CONFIG_NETCONSOLE_DYNAMIC
	struct config_group	group;
	struct config_group	userdata_group;
	char			*userdata;
	size_t			userdata_length;
	char			sysdata[MAX_EXTRADATA_ENTRY_LEN * MAX_SYSDATA_ITEMS];

	/* bit-wise with sysdata_feature bits */
	u32			sysdata_fields;
	/* protected by target_list_lock */
	u32			msgcounter;
#endif
	struct netconsole_target_stats stats;
	enum target_state	state;
	bool			extended;
	bool			release;
	struct netpoll		np;
	/* protected by target_list_lock */
	char			buf[MAX_PRINT_CHUNK];
	struct work_struct	resume_wq;
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

static void dynamic_netconsole_mutex_lock(void)
{
	mutex_lock(&dynamic_netconsole_mutex);
}

static void dynamic_netconsole_mutex_unlock(void)
{
	mutex_unlock(&dynamic_netconsole_mutex);
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

static void dynamic_netconsole_mutex_lock(void)
{
}

static void dynamic_netconsole_mutex_unlock(void)
{
}

#endif	/* CONFIG_NETCONSOLE_DYNAMIC */

/* Check if the target was bound by mac address. */
static bool bound_by_mac(struct netconsole_target *nt)
{
	return is_valid_ether_addr(nt->np.dev_mac);
}

/* Attempts to resume logging to a deactivated target. */
static void resume_target(struct netconsole_target *nt)
{
	if (netpoll_setup(&nt->np)) {
		/* netpoll fails setup once, do not try again. */
		nt->state = STATE_DISABLED;
		return;
	}

	nt->state = STATE_ENABLED;
	pr_info("network logging resumed on interface %s\n", nt->np.dev_name);
}

/* Checks if a deactivated target matches a device. */
static bool deactivated_target_match(struct netconsole_target *nt,
				     struct net_device *ndev)
{
	if (nt->state != STATE_DEACTIVATED)
		return false;

	if (bound_by_mac(nt))
		return !memcmp(nt->np.dev_mac, ndev->dev_addr, ETH_ALEN);
	return !strncmp(nt->np.dev_name, ndev->name, IFNAMSIZ);
}

/* Process work scheduled for target resume. */
static void process_resume_target(struct work_struct *work)
{
	struct netconsole_target *nt;
	unsigned long flags;

	nt = container_of(work, struct netconsole_target, resume_wq);

	dynamic_netconsole_mutex_lock();

	spin_lock_irqsave(&target_list_lock, flags);
	/* Check if target is still deactivated as it may have been disabled
	 * while resume was being scheduled.
	 */
	if (nt->state != STATE_DEACTIVATED) {
		spin_unlock_irqrestore(&target_list_lock, flags);
		goto out_unlock;
	}

	/* resume_target is IRQ unsafe, remove target from
	 * target_list in order to resume it with IRQ enabled.
	 */
	list_del_init(&nt->list);
	spin_unlock_irqrestore(&target_list_lock, flags);

	resume_target(nt);

	/* At this point the target is either enabled or disabled and
	 * was cleaned up before getting deactivated. Either way, add it
	 * back to target list.
	 */
	spin_lock_irqsave(&target_list_lock, flags);
	list_add(&nt->list, &target_list);
	spin_unlock_irqrestore(&target_list_lock, flags);

out_unlock:
	dynamic_netconsole_mutex_unlock();
}

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
	nt->state = STATE_DISABLED;
	INIT_WORK(&nt->resume_wq, process_resume_target);

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
		WARN_ON_ONCE(nt->state == STATE_ENABLED);
		do_netpoll_cleanup(&nt->np);
		if (bound_by_mac(nt))
			memset(&nt->np.dev_name, 0, IFNAMSIZ);
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

static void netconsole_print_banner(struct netpoll *np)
{
	np_info(np, "local port %d\n", np->local_port);
	if (np->ipv6)
		np_info(np, "local IPv6 address %pI6c\n", &np->local_ip.in6);
	else
		np_info(np, "local IPv4 address %pI4\n", &np->local_ip.ip);
	np_info(np, "interface name '%s'\n", np->dev_name);
	np_info(np, "local ethernet address '%pM'\n", np->dev_mac);
	np_info(np, "remote port %d\n", np->remote_port);
	if (np->ipv6)
		np_info(np, "remote IPv6 address %pI6c\n", &np->remote_ip.in6);
	else
		np_info(np, "remote IPv4 address %pI4\n", &np->remote_ip.ip);
	np_info(np, "remote ethernet address %pM\n", np->remote_mac);
}

/* Parse the string and populate the `inet_addr` union. Return 0 if IPv4 is
 * populated, 1 if IPv6 is populated, and -1 upon failure.
 */
static int netpoll_parse_ip_addr(const char *str, union inet_addr *addr)
{
	const char *end = NULL;
	int len;

	len = strlen(str);
	if (!len)
		return -1;

	if (str[len - 1] == '\n')
		len -= 1;

	if (in4_pton(str, len, (void *)addr, -1, &end) > 0 &&
	    (!end || *end == 0 || *end == '\n'))
		return 0;

	if (IS_ENABLED(CONFIG_IPV6) &&
	    in6_pton(str, len, (void *)addr, -1, &end) > 0 &&
	    (!end || *end == 0 || *end == '\n'))
		return 1;

	return -1;
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
	return sysfs_emit(buf, "%d\n", to_target(item)->state == STATE_ENABLED);
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

	dynamic_netconsole_mutex_lock();
	cpu_nr_enabled = !!(nt->sysdata_fields & SYSDATA_CPU_NR);
	dynamic_netconsole_mutex_unlock();

	return sysfs_emit(buf, "%d\n", cpu_nr_enabled);
}

/* configfs helper to display if taskname sysdata feature is enabled */
static ssize_t sysdata_taskname_enabled_show(struct config_item *item,
					     char *buf)
{
	struct netconsole_target *nt = to_target(item->ci_parent);
	bool taskname_enabled;

	dynamic_netconsole_mutex_lock();
	taskname_enabled = !!(nt->sysdata_fields & SYSDATA_TASKNAME);
	dynamic_netconsole_mutex_unlock();

	return sysfs_emit(buf, "%d\n", taskname_enabled);
}

static ssize_t sysdata_release_enabled_show(struct config_item *item,
					    char *buf)
{
	struct netconsole_target *nt = to_target(item->ci_parent);
	bool release_enabled;

	dynamic_netconsole_mutex_lock();
	release_enabled = !!(nt->sysdata_fields & SYSDATA_TASKNAME);
	dynamic_netconsole_mutex_unlock();

	return sysfs_emit(buf, "%d\n", release_enabled);
}

/* Iterate in the list of target, and make sure we don't have any console
 * register without targets of the same type
 */
static void unregister_netcons_consoles(void)
{
	struct netconsole_target *nt;
	u32 console_type_needed = 0;
	unsigned long flags;

	spin_lock_irqsave(&target_list_lock, flags);
	list_for_each_entry(nt, &target_list, list) {
		if (nt->extended)
			console_type_needed |= CONS_EXTENDED;
		else
			console_type_needed |= CONS_BASIC;
	}
	spin_unlock_irqrestore(&target_list_lock, flags);

	if (!(console_type_needed & CONS_EXTENDED) &&
	    console_is_registered(&netconsole_ext))
		unregister_console(&netconsole_ext);

	if (!(console_type_needed & CONS_BASIC) &&
	    console_is_registered(&netconsole))
		unregister_console(&netconsole);
}

static ssize_t sysdata_msgid_enabled_show(struct config_item *item,
					  char *buf)
{
	struct netconsole_target *nt = to_target(item->ci_parent);
	bool msgid_enabled;

	dynamic_netconsole_mutex_lock();
	msgid_enabled = !!(nt->sysdata_fields & SYSDATA_MSGID);
	dynamic_netconsole_mutex_unlock();

	return sysfs_emit(buf, "%d\n", msgid_enabled);
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
	bool enabled, current_enabled;
	unsigned long flags;
	ssize_t ret;

	dynamic_netconsole_mutex_lock();
	ret = kstrtobool(buf, &enabled);
	if (ret)
		goto out_unlock;

	/* When the user explicitly enables or disables a target that is
	 * currently deactivated, reset its state to disabled. The DEACTIVATED
	 * state only tracks interface-driven deactivation and should _not_
	 * persist when the user manually changes the target's enabled state.
	 */
	if (nt->state == STATE_DEACTIVATED)
		nt->state = STATE_DISABLED;

	ret = -EINVAL;
	current_enabled = nt->state == STATE_ENABLED;
	if (enabled == current_enabled) {
		pr_info("network logging has already %s\n",
			current_enabled ? "started" : "stopped");
		goto out_unlock;
	}

	if (enabled) {	/* true */
		if (nt->release && !nt->extended) {
			pr_err("Not enabling netconsole. Release feature requires extended log message");
			goto out_unlock;
		}

		if (nt->extended && !console_is_registered(&netconsole_ext)) {
			netconsole_ext.flags |= CON_ENABLED;
			register_console(&netconsole_ext);
		}

		/* User might be enabling the basic format target for the very
		 * first time, make sure the console is registered.
		 */
		if (!nt->extended && !console_is_registered(&netconsole)) {
			netconsole.flags |= CON_ENABLED;
			register_console(&netconsole);
		}

		/*
		 * Skip netconsole_parser_cmdline() -- all the attributes are
		 * already configured via configfs. Just print them out.
		 */
		netconsole_print_banner(&nt->np);

		ret = netpoll_setup(&nt->np);
		if (ret)
			goto out_unlock;

		nt->state = STATE_ENABLED;
		pr_info("network logging started\n");
	} else {	/* false */
		/* We need to disable the netconsole before cleaning it up
		 * otherwise we might end up in write_msg() with
		 * nt->np.dev == NULL and nt->state == STATE_ENABLED
		 */
		mutex_lock(&target_cleanup_list_lock);
		spin_lock_irqsave(&target_list_lock, flags);
		nt->state = STATE_DISABLED;
		/* Remove the target from the list, while holding
		 * target_list_lock
		 */
		list_move(&nt->list, &target_cleanup_list);
		spin_unlock_irqrestore(&target_list_lock, flags);
		mutex_unlock(&target_cleanup_list_lock);
		/* Unregister consoles, whose the last target of that type got
		 * disabled.
		 */
		unregister_netcons_consoles();
	}

	ret = strnlen(buf, count);
	/* Deferred cleanup */
	netconsole_process_cleanups();
out_unlock:
	dynamic_netconsole_mutex_unlock();
	return ret;
}

static ssize_t release_store(struct config_item *item, const char *buf,
			     size_t count)
{
	struct netconsole_target *nt = to_target(item);
	bool release;
	ssize_t ret;

	dynamic_netconsole_mutex_lock();
	if (nt->state == STATE_ENABLED) {
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
	dynamic_netconsole_mutex_unlock();
	return ret;
}

static ssize_t extended_store(struct config_item *item, const char *buf,
		size_t count)
{
	struct netconsole_target *nt = to_target(item);
	bool extended;
	ssize_t ret;

	dynamic_netconsole_mutex_lock();
	if (nt->state == STATE_ENABLED)  {
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
	dynamic_netconsole_mutex_unlock();
	return ret;
}

static ssize_t dev_name_store(struct config_item *item, const char *buf,
		size_t count)
{
	struct netconsole_target *nt = to_target(item);

	dynamic_netconsole_mutex_lock();
	if (nt->state == STATE_ENABLED) {
		pr_err("target (%s) is enabled, disable to update parameters\n",
		       config_item_name(&nt->group.cg_item));
		dynamic_netconsole_mutex_unlock();
		return -EINVAL;
	}

	strscpy(nt->np.dev_name, buf, IFNAMSIZ);
	trim_newline(nt->np.dev_name, IFNAMSIZ);

	dynamic_netconsole_mutex_unlock();
	return strnlen(buf, count);
}

static ssize_t local_port_store(struct config_item *item, const char *buf,
		size_t count)
{
	struct netconsole_target *nt = to_target(item);
	ssize_t ret = -EINVAL;

	dynamic_netconsole_mutex_lock();
	if (nt->state == STATE_ENABLED) {
		pr_err("target (%s) is enabled, disable to update parameters\n",
		       config_item_name(&nt->group.cg_item));
		goto out_unlock;
	}

	ret = kstrtou16(buf, 10, &nt->np.local_port);
	if (ret < 0)
		goto out_unlock;
	ret = strnlen(buf, count);
out_unlock:
	dynamic_netconsole_mutex_unlock();
	return ret;
}

static ssize_t remote_port_store(struct config_item *item,
		const char *buf, size_t count)
{
	struct netconsole_target *nt = to_target(item);
	ssize_t ret = -EINVAL;

	dynamic_netconsole_mutex_lock();
	if (nt->state == STATE_ENABLED) {
		pr_err("target (%s) is enabled, disable to update parameters\n",
		       config_item_name(&nt->group.cg_item));
		goto out_unlock;
	}

	ret = kstrtou16(buf, 10, &nt->np.remote_port);
	if (ret < 0)
		goto out_unlock;
	ret = strnlen(buf, count);
out_unlock:
	dynamic_netconsole_mutex_unlock();
	return ret;
}

static ssize_t local_ip_store(struct config_item *item, const char *buf,
		size_t count)
{
	struct netconsole_target *nt = to_target(item);
	ssize_t ret = -EINVAL;
	int ipv6;

	dynamic_netconsole_mutex_lock();
	if (nt->state == STATE_ENABLED) {
		pr_err("target (%s) is enabled, disable to update parameters\n",
		       config_item_name(&nt->group.cg_item));
		goto out_unlock;
	}

	ipv6 = netpoll_parse_ip_addr(buf, &nt->np.local_ip);
	if (ipv6 == -1)
		goto out_unlock;
	nt->np.ipv6 = !!ipv6;

	ret = strnlen(buf, count);
out_unlock:
	dynamic_netconsole_mutex_unlock();
	return ret;
}

static ssize_t remote_ip_store(struct config_item *item, const char *buf,
	       size_t count)
{
	struct netconsole_target *nt = to_target(item);
	ssize_t ret = -EINVAL;
	int ipv6;

	dynamic_netconsole_mutex_lock();
	if (nt->state == STATE_ENABLED) {
		pr_err("target (%s) is enabled, disable to update parameters\n",
		       config_item_name(&nt->group.cg_item));
		goto out_unlock;
	}

	ipv6 = netpoll_parse_ip_addr(buf, &nt->np.remote_ip);
	if (ipv6 == -1)
		goto out_unlock;
	nt->np.ipv6 = !!ipv6;

	ret = strnlen(buf, count);
out_unlock:
	dynamic_netconsole_mutex_unlock();
	return ret;
}

/* Count number of entries we have in userdata.
 * This is important because userdata only supports MAX_USERDATA_ITEMS
 * entries. Before enabling any new userdata feature, number of entries needs
 * to checked for available space.
 */
static size_t count_userdata_entries(struct netconsole_target *nt)
{
	return list_count_nodes(&nt->userdata_group.cg_children);
}

static ssize_t remote_mac_store(struct config_item *item, const char *buf,
		size_t count)
{
	struct netconsole_target *nt = to_target(item);
	u8 remote_mac[ETH_ALEN];
	ssize_t ret = -EINVAL;

	dynamic_netconsole_mutex_lock();
	if (nt->state == STATE_ENABLED) {
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
	dynamic_netconsole_mutex_unlock();
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

/* Navigate configfs and calculate the lentgh of the formatted string
 * representing userdata.
 * Must be called holding netconsole_subsys.su_mutex
 */
static int calc_userdata_len(struct netconsole_target *nt)
{
	struct userdatum *udm_item;
	struct config_item *item;
	struct list_head *entry;
	int len = 0;

	list_for_each(entry, &nt->userdata_group.cg_children) {
		item = container_of(entry, struct config_item, ci_entry);
		udm_item = to_userdatum(item);
		/* Skip userdata with no value set */
		if (udm_item->value[0]) {
			len += snprintf(NULL, 0, " %s=%s\n", item->ci_name,
					udm_item->value);
		}
	}
	return len;
}

static int update_userdata(struct netconsole_target *nt)
{
	struct userdatum *udm_item;
	struct config_item *item;
	struct list_head *entry;
	char *old_buf = NULL;
	char *new_buf = NULL;
	unsigned long flags;
	int offset = 0;
	int len;

	/* Calculate required buffer size */
	len = calc_userdata_len(nt);

	if (WARN_ON_ONCE(len > MAX_EXTRADATA_ENTRY_LEN * MAX_USERDATA_ITEMS))
		return -ENOSPC;

	/* Allocate new buffer */
	if (len) {
		new_buf = kmalloc(len + 1, GFP_KERNEL);
		if (!new_buf)
			return -ENOMEM;
	}

	/* Write userdata to new buffer */
	list_for_each(entry, &nt->userdata_group.cg_children) {
		item = container_of(entry, struct config_item, ci_entry);
		udm_item = to_userdatum(item);
		/* Skip userdata with no value set */
		if (udm_item->value[0]) {
			offset += scnprintf(&new_buf[offset], len + 1 - offset,
					    " %s=%s\n", item->ci_name,
					    udm_item->value);
		}
	}

	WARN_ON_ONCE(offset != len);

	/* Switch to new buffer and free old buffer */
	spin_lock_irqsave(&target_list_lock, flags);
	old_buf = nt->userdata;
	nt->userdata = new_buf;
	nt->userdata_length = offset;
	spin_unlock_irqrestore(&target_list_lock, flags);

	kfree(old_buf);

	return 0;
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

	mutex_lock(&netconsole_subsys.su_mutex);
	dynamic_netconsole_mutex_lock();

	ret = strscpy(udm->value, buf, sizeof(udm->value));
	if (ret < 0)
		goto out_unlock;
	trim_newline(udm->value, sizeof(udm->value));

	ud = to_userdata(item->ci_parent);
	nt = userdata_to_target(ud);
	ret = update_userdata(nt);
	if (ret < 0)
		goto out_unlock;
	ret = count;
out_unlock:
	dynamic_netconsole_mutex_unlock();
	mutex_unlock(&netconsole_subsys.su_mutex);
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
	nt->sysdata[0] = 0;
}

static ssize_t sysdata_msgid_enabled_store(struct config_item *item,
					   const char *buf, size_t count)
{
	struct netconsole_target *nt = to_target(item->ci_parent);
	bool msgid_enabled, curr;
	ssize_t ret;

	ret = kstrtobool(buf, &msgid_enabled);
	if (ret)
		return ret;

	mutex_lock(&netconsole_subsys.su_mutex);
	dynamic_netconsole_mutex_lock();
	curr = !!(nt->sysdata_fields & SYSDATA_MSGID);
	if (msgid_enabled == curr)
		goto unlock_ok;

	if (msgid_enabled)
		nt->sysdata_fields |= SYSDATA_MSGID;
	else
		disable_sysdata_feature(nt, SYSDATA_MSGID);

unlock_ok:
	ret = strnlen(buf, count);
	dynamic_netconsole_mutex_unlock();
	mutex_unlock(&netconsole_subsys.su_mutex);
	return ret;
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

	mutex_lock(&netconsole_subsys.su_mutex);
	dynamic_netconsole_mutex_lock();
	curr = !!(nt->sysdata_fields & SYSDATA_RELEASE);
	if (release_enabled == curr)
		goto unlock_ok;

	if (release_enabled)
		nt->sysdata_fields |= SYSDATA_RELEASE;
	else
		disable_sysdata_feature(nt, SYSDATA_RELEASE);

unlock_ok:
	ret = strnlen(buf, count);
	dynamic_netconsole_mutex_unlock();
	mutex_unlock(&netconsole_subsys.su_mutex);
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

	mutex_lock(&netconsole_subsys.su_mutex);
	dynamic_netconsole_mutex_lock();
	curr = !!(nt->sysdata_fields & SYSDATA_TASKNAME);
	if (taskname_enabled == curr)
		goto unlock_ok;

	if (taskname_enabled)
		nt->sysdata_fields |= SYSDATA_TASKNAME;
	else
		disable_sysdata_feature(nt, SYSDATA_TASKNAME);

unlock_ok:
	ret = strnlen(buf, count);
	dynamic_netconsole_mutex_unlock();
	mutex_unlock(&netconsole_subsys.su_mutex);
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

	mutex_lock(&netconsole_subsys.su_mutex);
	dynamic_netconsole_mutex_lock();
	curr = !!(nt->sysdata_fields & SYSDATA_CPU_NR);
	if (cpu_nr_enabled == curr)
		/* no change requested */
		goto unlock_ok;

	if (cpu_nr_enabled)
		nt->sysdata_fields |= SYSDATA_CPU_NR;
	else
		/* This is special because sysdata might have remaining data
		 * from previous sysdata, and it needs to be cleaned.
		 */
		disable_sysdata_feature(nt, SYSDATA_CPU_NR);

unlock_ok:
	ret = strnlen(buf, count);
	dynamic_netconsole_mutex_unlock();
	mutex_unlock(&netconsole_subsys.su_mutex);
	return ret;
}

CONFIGFS_ATTR(userdatum_, value);
CONFIGFS_ATTR(sysdata_, cpu_nr_enabled);
CONFIGFS_ATTR(sysdata_, taskname_enabled);
CONFIGFS_ATTR(sysdata_, release_enabled);
CONFIGFS_ATTR(sysdata_, msgid_enabled);

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
	if (count_userdata_entries(nt) >= MAX_USERDATA_ITEMS)
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

	dynamic_netconsole_mutex_lock();
	update_userdata(nt);
	config_item_put(item);
	dynamic_netconsole_mutex_unlock();
}

static struct configfs_attribute *userdata_attrs[] = {
	&sysdata_attr_cpu_nr_enabled,
	&sysdata_attr_taskname_enabled,
	&sysdata_attr_release_enabled,
	&sysdata_attr_msgid_enabled,
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
	struct netconsole_target *nt = to_target(item);

	kfree(nt->userdata);
	kfree(nt);
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
	struct netconsole_target *nt = to_target(item);
	unsigned long flags;

	dynamic_netconsole_mutex_lock();

	spin_lock_irqsave(&target_list_lock, flags);
	/* Disable deactivated target to prevent races between resume attempt
	 * and target removal.
	 */
	if (nt->state == STATE_DEACTIVATED)
		nt->state = STATE_DISABLED;
	list_del(&nt->list);
	spin_unlock_irqrestore(&target_list_lock, flags);

	dynamic_netconsole_mutex_unlock();

	/* Now that the target has been marked disabled no further work
	 * can be scheduled. Existing work will skip as targets are not
	 * deactivated anymore. Cancel any scheduled resume and wait for
	 * completion.
	 */
	cancel_work_sync(&nt->resume_wq);

	/*
	 * The target may have never been enabled, or was manually disabled
	 * before being removed so netpoll may have already been cleaned up.
	 */
	if (nt->state == STATE_ENABLED)
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

static int sysdata_append_cpu_nr(struct netconsole_target *nt, int offset,
				 struct nbcon_write_context *wctxt)
{
	return scnprintf(&nt->sysdata[offset],
			 MAX_EXTRADATA_ENTRY_LEN, " cpu=%u\n",
			 wctxt->cpu);
}

static int sysdata_append_taskname(struct netconsole_target *nt, int offset,
				   struct nbcon_write_context *wctxt)
{
	return scnprintf(&nt->sysdata[offset],
			 MAX_EXTRADATA_ENTRY_LEN, " taskname=%s\n",
			 wctxt->comm);
}

static int sysdata_append_release(struct netconsole_target *nt, int offset)
{
	return scnprintf(&nt->sysdata[offset],
			 MAX_EXTRADATA_ENTRY_LEN, " release=%s\n",
			 init_utsname()->release);
}

static int sysdata_append_msgid(struct netconsole_target *nt, int offset)
{
	wrapping_assign_add(nt->msgcounter, 1);
	return scnprintf(&nt->sysdata[offset],
			 MAX_EXTRADATA_ENTRY_LEN, " msgid=%u\n",
			 nt->msgcounter);
}

/*
 * prepare_sysdata - append sysdata in runtime
 * @nt: target to send message to
 * @wctxt: nbcon write context containing message metadata
 */
static int prepare_sysdata(struct netconsole_target *nt,
			   struct nbcon_write_context *wctxt)
{
	int sysdata_len = 0;

	if (!nt->sysdata_fields)
		goto out;

	if (nt->sysdata_fields & SYSDATA_CPU_NR)
		sysdata_len += sysdata_append_cpu_nr(nt, sysdata_len, wctxt);
	if (nt->sysdata_fields & SYSDATA_TASKNAME)
		sysdata_len += sysdata_append_taskname(nt, sysdata_len, wctxt);
	if (nt->sysdata_fields & SYSDATA_RELEASE)
		sysdata_len += sysdata_append_release(nt, sysdata_len);
	if (nt->sysdata_fields & SYSDATA_MSGID)
		sysdata_len += sysdata_append_msgid(nt, sysdata_len);

	WARN_ON_ONCE(sysdata_len >
		     MAX_EXTRADATA_ENTRY_LEN * MAX_SYSDATA_ITEMS);

out:
	return sysdata_len;
}
#endif	/* CONFIG_NETCONSOLE_DYNAMIC */

/* Handle network interface device notifications */
static int netconsole_netdev_event(struct notifier_block *this,
				   unsigned long event, void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);
	struct netconsole_target *nt, *tmp;
	bool stopped = false;
	unsigned long flags;

	if (!(event == NETDEV_CHANGENAME || event == NETDEV_UNREGISTER ||
	      event == NETDEV_RELEASE || event == NETDEV_JOIN ||
	      event == NETDEV_REGISTER))
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
				/* transition target to DISABLED instead of
				 * DEACTIVATED when (de)enslaving devices as
				 * their targets should not be automatically
				 * resumed when the interface is brought up.
				 */
				nt->state = STATE_DISABLED;
				list_move(&nt->list, &target_cleanup_list);
				stopped = true;
				break;
			case NETDEV_UNREGISTER:
				nt->state = STATE_DEACTIVATED;
				list_move(&nt->list, &target_cleanup_list);
				stopped = true;
			}
		}
		if ((event == NETDEV_REGISTER || event == NETDEV_CHANGENAME) &&
		    deactivated_target_match(nt, dev))
			/* Schedule resume on a workqueue as it will attempt
			 * to UP the device, which can't be done as part of this
			 * notifier.
			 */
			queue_work(netconsole_wq, &nt->resume_wq);
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
	const char *userdata = NULL;
	const char *sysdata = NULL;
	const char *release;

#ifdef CONFIG_NETCONSOLE_DYNAMIC
	userdata = nt->userdata;
	sysdata = nt->sysdata;
#endif

	if (release_len) {
		release = init_utsname()->release;

		scnprintf(nt->buf, MAX_PRINT_CHUNK, "%s,%s", release, msg);
		msg_len += release_len;
	} else {
		memcpy(nt->buf, msg, msg_len);
	}

	if (userdata)
		msg_len += scnprintf(&nt->buf[msg_len],
				     MAX_PRINT_CHUNK - msg_len, "%s",
				     userdata);

	if (sysdata)
		msg_len += scnprintf(&nt->buf[msg_len],
				     MAX_PRINT_CHUNK - msg_len, "%s",
				     sysdata);

	send_udp(nt, nt->buf, msg_len);
}

static void append_release(char *buf)
{
	const char *release;

	release = init_utsname()->release;
	scnprintf(buf, MAX_PRINT_CHUNK, "%s,", release);
}

static void send_fragmented_body(struct netconsole_target *nt,
				 const char *msgbody_ptr, int header_len,
				 int msgbody_len, int sysdata_len)
{
	const char *userdata_ptr = NULL;
	const char *sysdata_ptr = NULL;
	int data_len, data_sent = 0;
	int userdata_offset = 0;
	int sysdata_offset = 0;
	int msgbody_offset = 0;
	int userdata_len = 0;

#ifdef CONFIG_NETCONSOLE_DYNAMIC
	userdata_ptr = nt->userdata;
	sysdata_ptr = nt->sysdata;
	userdata_len = nt->userdata_length;
#endif
	if (WARN_ON_ONCE(!userdata_ptr && userdata_len != 0))
		return;

	if (WARN_ON_ONCE(!sysdata_ptr && sysdata_len != 0))
		return;

	/* data_len represents the number of bytes that will be sent. This is
	 * bigger than MAX_PRINT_CHUNK, thus, it will be split in multiple
	 * packets
	 */
	data_len = msgbody_len + userdata_len + sysdata_len;

	/* In each iteration of the while loop below, we send a packet
	 * containing the header and a portion of the data. The data is
	 * composed of three parts: msgbody, userdata, and sysdata.
	 * We keep track of how many bytes have been sent from each part using
	 * the *_offset variables.
	 * We keep track of how many bytes have been sent overall using the
	 * data_sent variable, which ranges from 0 to the total bytes to be
	 * sent.
	 */
	while (data_sent < data_len) {
		int userdata_left = userdata_len - userdata_offset;
		int sysdata_left = sysdata_len - sysdata_offset;
		int msgbody_left = msgbody_len - msgbody_offset;
		int buf_offset = 0;
		int this_chunk = 0;

		/* header is already populated in nt->buf, just append to it */
		buf_offset = header_len;

		buf_offset += scnprintf(nt->buf + buf_offset,
					 MAX_PRINT_CHUNK - buf_offset,
					 ",ncfrag=%d/%d;", data_sent,
					 data_len);

		/* append msgbody first */
		this_chunk = min(msgbody_left, MAX_PRINT_CHUNK - buf_offset);
		memcpy(nt->buf + buf_offset, msgbody_ptr + msgbody_offset,
		       this_chunk);
		msgbody_offset += this_chunk;
		buf_offset += this_chunk;
		data_sent += this_chunk;

		/* after msgbody, append userdata */
		if (userdata_ptr && userdata_left) {
			this_chunk = min(userdata_left,
					 MAX_PRINT_CHUNK - buf_offset);
			memcpy(nt->buf + buf_offset,
			       userdata_ptr + userdata_offset, this_chunk);
			userdata_offset += this_chunk;
			buf_offset += this_chunk;
			data_sent += this_chunk;
		}

		/* after userdata, append sysdata */
		if (sysdata_ptr && sysdata_left) {
			this_chunk = min(sysdata_left,
					 MAX_PRINT_CHUNK - buf_offset);
			memcpy(nt->buf + buf_offset,
			       sysdata_ptr + sysdata_offset, this_chunk);
			sysdata_offset += this_chunk;
			buf_offset += this_chunk;
			data_sent += this_chunk;
		}

		/* if all is good, send the packet out */
		if (WARN_ON_ONCE(data_sent > data_len))
			return;

		send_udp(nt, nt->buf, buf_offset);
	}
}

static void send_msg_fragmented(struct netconsole_target *nt,
				const char *msg,
				int msg_len,
				int release_len,
				int sysdata_len)
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
			     sysdata_len);
}

/**
 * send_ext_msg_udp - send extended log message to target
 * @nt: target to send message to
 * @wctxt: nbcon write context containing message and metadata
 *
 * Transfer extended log message to @nt.  If message is longer than
 * MAX_PRINT_CHUNK, it'll be split and transmitted in multiple chunks with
 * ncfrag header field added to identify them.
 */
static void send_ext_msg_udp(struct netconsole_target *nt,
			     struct nbcon_write_context *wctxt)
{
	int userdata_len = 0;
	int release_len = 0;
	int sysdata_len = 0;
	int len;

#ifdef CONFIG_NETCONSOLE_DYNAMIC
	sysdata_len = prepare_sysdata(nt, wctxt);
	userdata_len = nt->userdata_length;
#endif
	if (nt->release)
		release_len = strlen(init_utsname()->release) + 1;

	len = wctxt->len + release_len + sysdata_len + userdata_len;
	if (len <= MAX_PRINT_CHUNK)
		return send_msg_no_fragmentation(nt, wctxt->outbuf,
						 wctxt->len, release_len);

	return send_msg_fragmented(nt, wctxt->outbuf, wctxt->len, release_len,
				   sysdata_len);
}

static void send_msg_udp(struct netconsole_target *nt, const char *msg,
			 unsigned int len)
{
	const char *tmp = msg;
	int frag, left = len;

	while (left > 0) {
		frag = min(left, MAX_PRINT_CHUNK);
		send_udp(nt, tmp, frag);
		tmp += frag;
		left -= frag;
	}
}

/**
 * netconsole_write - Generic function to send a msg to all targets
 * @wctxt: nbcon write context
 * @extended: "true" for extended console mode
 *
 * Given an nbcon write context, send the message to the netconsole targets
 */
static void netconsole_write(struct nbcon_write_context *wctxt, bool extended)
{
	struct netconsole_target *nt;

	if (oops_only && !oops_in_progress)
		return;

	list_for_each_entry(nt, &target_list, list) {
		if (nt->extended != extended || nt->state != STATE_ENABLED ||
		    !netif_running(nt->np.dev))
			continue;

		/* If nbcon_enter_unsafe() fails, just return given netconsole
		 * lost the ownership, and iterating over the targets will not
		 * be able to re-acquire.
		 */
		if (!nbcon_enter_unsafe(wctxt))
			return;

		if (extended)
			send_ext_msg_udp(nt, wctxt);
		else
			send_msg_udp(nt, wctxt->outbuf, wctxt->len);

		nbcon_exit_unsafe(wctxt);
	}
}

static void netconsole_write_ext(struct console *con __always_unused,
				 struct nbcon_write_context *wctxt)
{
	netconsole_write(wctxt, true);
}

static void netconsole_write_basic(struct console *con __always_unused,
				   struct nbcon_write_context *wctxt)
{
	netconsole_write(wctxt, false);
}

static void netconsole_device_lock(struct console *con __always_unused,
				   unsigned long *flags)
__acquires(&target_list_lock)
{
	spin_lock_irqsave(&target_list_lock, *flags);
}

static void netconsole_device_unlock(struct console *con __always_unused,
				     unsigned long flags)
__releases(&target_list_lock)
{
	spin_unlock_irqrestore(&target_list_lock, flags);
}

static int netconsole_parser_cmdline(struct netpoll *np, char *opt)
{
	bool ipversion_set = false;
	char *cur = opt;
	char *delim;
	int ipv6;

	if (*cur != '@') {
		delim = strchr(cur, '@');
		if (!delim)
			goto parse_failed;
		*delim = 0;
		if (kstrtou16(cur, 10, &np->local_port))
			goto parse_failed;
		cur = delim;
	}
	cur++;

	if (*cur != '/') {
		ipversion_set = true;
		delim = strchr(cur, '/');
		if (!delim)
			goto parse_failed;
		*delim = 0;
		ipv6 = netpoll_parse_ip_addr(cur, &np->local_ip);
		if (ipv6 < 0)
			goto parse_failed;
		else
			np->ipv6 = (bool)ipv6;
		cur = delim;
	}
	cur++;

	if (*cur != ',') {
		/* parse out dev_name or dev_mac */
		delim = strchr(cur, ',');
		if (!delim)
			goto parse_failed;
		*delim = 0;

		np->dev_name[0] = '\0';
		eth_broadcast_addr(np->dev_mac);
		if (!strchr(cur, ':'))
			strscpy(np->dev_name, cur, sizeof(np->dev_name));
		else if (!mac_pton(cur, np->dev_mac))
			goto parse_failed;

		cur = delim;
	}
	cur++;

	if (*cur != '@') {
		/* dst port */
		delim = strchr(cur, '@');
		if (!delim)
			goto parse_failed;
		*delim = 0;
		if (*cur == ' ' || *cur == '\t')
			np_info(np, "warning: whitespace is not allowed\n");
		if (kstrtou16(cur, 10, &np->remote_port))
			goto parse_failed;
		cur = delim;
	}
	cur++;

	/* dst ip */
	delim = strchr(cur, '/');
	if (!delim)
		goto parse_failed;
	*delim = 0;
	ipv6 = netpoll_parse_ip_addr(cur, &np->remote_ip);
	if (ipv6 < 0)
		goto parse_failed;
	else if (ipversion_set && np->ipv6 != (bool)ipv6)
		goto parse_failed;
	else
		np->ipv6 = (bool)ipv6;
	cur = delim + 1;

	if (*cur != 0) {
		/* MAC address */
		if (!mac_pton(cur, np->remote_mac))
			goto parse_failed;
	}

	netconsole_print_banner(np);

	return 0;

 parse_failed:
	np_info(np, "couldn't parse config at '%s'!\n", cur);
	return -1;
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
	err = netconsole_parser_cmdline(&nt->np, target_config);
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
		nt->state = STATE_ENABLED;
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
	cancel_work_sync(&nt->resume_wq);
	netpoll_cleanup(&nt->np);
#ifdef	CONFIG_NETCONSOLE_DYNAMIC
	kfree(nt->userdata);
#endif
	kfree(nt);
}

static struct console netconsole_ext = {
	.name = "netcon_ext",
	.flags = CON_ENABLED | CON_EXTENDED | CON_NBCON | CON_NBCON_ATOMIC_UNSAFE,
	.write_thread = netconsole_write_ext,
	.write_atomic = netconsole_write_ext,
	.device_lock = netconsole_device_lock,
	.device_unlock = netconsole_device_unlock,
};

static struct console netconsole = {
	.name = "netcon",
	.flags = CON_ENABLED | CON_NBCON | CON_NBCON_ATOMIC_UNSAFE,
	.write_thread = netconsole_write_basic,
	.write_atomic = netconsole_write_basic,
	.device_lock = netconsole_device_lock,
	.device_unlock = netconsole_device_unlock,
};

static int __init init_netconsole(void)
{
	int err;
	struct netconsole_target *nt, *tmp;
	u32 console_type_needed = 0;
	unsigned int count = 0;
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
				console_type_needed |= CONS_EXTENDED;
				netconsole_ext.flags |= CON_PRINTBUFFER;
			} else {
				console_type_needed |= CONS_BASIC;
				netconsole.flags |= CON_PRINTBUFFER;
			}

			spin_lock_irqsave(&target_list_lock, flags);
			list_add(&nt->list, &target_list);
			spin_unlock_irqrestore(&target_list_lock, flags);
			count++;
		}
	}

	netconsole_wq = alloc_workqueue("netconsole", WQ_UNBOUND, 0);
	if (!netconsole_wq) {
		err = -ENOMEM;
		goto fail;
	}

	err = register_netdevice_notifier(&netconsole_netdev_notifier);
	if (err)
		goto fail;

	err = dynamic_netconsole_init();
	if (err)
		goto undonotifier;

	if (console_type_needed & CONS_EXTENDED)
		register_console(&netconsole_ext);
	if (console_type_needed & CONS_BASIC)
		register_console(&netconsole);
	pr_info("network logging started\n");

	return err;

undonotifier:
	unregister_netdevice_notifier(&netconsole_netdev_notifier);

fail:
	pr_err("cleaning up\n");

	if (netconsole_wq)
		flush_workqueue(netconsole_wq);
	/*
	 * Remove all targets and destroy them (only targets created
	 * from the boot/module option exist here). Skipping the list
	 * lock is safe here, and netpoll_cleanup() will sleep.
	 */
	list_for_each_entry_safe(nt, tmp, &target_list, list) {
		list_del(&nt->list);
		free_param_target(nt);
	}

	if (netconsole_wq)
		destroy_workqueue(netconsole_wq);

	return err;
}

static void __exit cleanup_netconsole(void)
{
	struct netconsole_target *nt, *tmp;

	if (console_is_registered(&netconsole_ext))
		unregister_console(&netconsole_ext);
	if (console_is_registered(&netconsole))
		unregister_console(&netconsole);
	dynamic_netconsole_exit();
	unregister_netdevice_notifier(&netconsole_netdev_notifier);
	flush_workqueue(netconsole_wq);

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

	destroy_workqueue(netconsole_wq);
}

/*
 * Use late_initcall to ensure netconsole is
 * initialized after network device driver if built-in.
 *
 * late_initcall() and module_init() are identical if built as module.
 */
late_initcall(init_netconsole);
module_exit(cleanup_netconsole);
