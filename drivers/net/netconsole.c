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
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/udp.h>
#include <linux/netpoll.h>
#include <linux/inet.h>
#include <linux/inetdevice.h>
#include <linux/unaligned.h>
#include <net/ip6_checksum.h>
#include <net/addrconf.h>
#include <linux/configfs.h>
#include <linux/etherdevice.h>
#include <linux/hex.h>
#include <linux/u64_stats_sync.h>
#include <linux/utsname.h>
#include <linux/rtnetlink.h>
#include <linux/workqueue.h>
#include <linux/delay.h>

MODULE_AUTHOR("Matt Mackall <mpm@selenic.com>");
MODULE_DESCRIPTION("Console driver for network interfaces");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("NETDEV_INTERNAL");

#define MAX_PARAM_LENGTH		256
#define MAX_EXTRADATA_ENTRY_LEN		256
#define MAX_EXTRADATA_VALUE_LEN	200
/* The number 3 comes from userdata entry format characters (' ', '=', '\n') */
#define MAX_EXTRADATA_NAME_LEN		(MAX_EXTRADATA_ENTRY_LEN - \
					MAX_EXTRADATA_VALUE_LEN - 3)
#define MAX_USERDATA_ITEMS		256
#define MAX_PRINT_CHUNK			1000

/*
 * Sizing for the per-target fallback skb pool consulted by find_skb()
 * when its GFP_ATOMIC allocation fails so messages still get out under
 * memory pressure.
 */
#define MAX_UDP_CHUNK			1460
#define MAX_SKBS			32
#define MAX_SKB_SIZE							\
	(sizeof(struct ethhdr) +					\
	 sizeof(struct iphdr) +						\
	 sizeof(struct udphdr) +					\
	 MAX_UDP_CHUNK)

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
 * struct netcons_userdata - Formatted userdata payload of a target.
 * @rcu:	Used to free the payload after a grace period.
 * @length:	Length of @data, excluding the NUL terminator.
 * @data:	Formatted " key=value\n" entries, NUL terminated.
 *
 * Immutable once published, so the transmit path never observes @data and
 * @length disagreeing.
 */
struct netcons_userdata {
	struct rcu_head		rcu;
	size_t			length;
	char			data[];
};

/**
 * struct netconsole_target - Represents a configured netconsole target.
 * @list:	Links this target into the target_list.
 * @group:	Links us into the configfs subsystem hierarchy.
 * @userdata_group:	Links to the userdata configfs hierarchy
 * @userdata:		Cached, formatted userdata payload. RCU protected.
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
 *		local_mac	(read-only)
 * @local_ip:	Source IP address of the target (read-write).
 * @remote_ip:	Destination IP address of the target (read-write).
 * @ipv6:	Whether the target addresses are IPv6 (read-write).
 * @local_port:	Source UDP port of the target (read-write).
 * @remote_port: Destination UDP port of the target (read-write).
 * @remote_mac:	Destination ethernet address of the target (read-write).
 * @buf:	The buffer used to send the full msg to the network stack
 * @resume_wq:	Workqueue to resume deactivated target
 * @skb_pool:	Per-target fallback skb pool consulted by find_skb() when
 *		its GFP_ATOMIC allocation fails. Lifetime brackets a
 *		successful netpoll_setup() / netpoll_cleanup() pair on @np.
 * @refill_wq:	Work item that asynchronously tops @skb_pool back up to
 *		MAX_SKBS after find_skb() drains an entry.
 */
struct netconsole_target {
	struct list_head	list;
#ifdef	CONFIG_NETCONSOLE_DYNAMIC
	struct config_group	group;
	struct config_group	userdata_group;
	struct netcons_userdata __rcu *userdata;
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
	union inet_addr		local_ip, remote_ip;
	bool			ipv6;
	u16			local_port, remote_port;
	u8			remote_mac[ETH_ALEN];
	/* protected by target_list_lock; +1 gives scnprintf() room for its
	 * NUL terminator so a full MAX_PRINT_CHUNK payload is not truncated
	 */
	char			buf[MAX_PRINT_CHUNK + 1];
	struct work_struct	resume_wq;
	struct sk_buff_head	skb_pool;
	struct work_struct	refill_wq;
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

static void netcons_release_dev(struct netconsole_target *nt)
{
	do_netpoll_cleanup(&nt->np);
	if (bound_by_mac(nt))
		memset(&nt->np.dev_name, 0, IFNAMSIZ);
}

static void refill_skbs(struct netconsole_target *nt)
{
	struct sk_buff_head *skb_pool = &nt->skb_pool;
	struct sk_buff *skb;

	while (READ_ONCE(skb_pool->qlen) < MAX_SKBS) {
		skb = alloc_skb(MAX_SKB_SIZE, GFP_ATOMIC | __GFP_NOWARN);
		if (!skb)
			break;

		skb_queue_tail(skb_pool, skb);
	}
}

static void refill_skbs_work_handler(struct work_struct *work)
{
	struct netconsole_target *nt =
		container_of(work, struct netconsole_target, refill_wq);

	refill_skbs(nt);
}

/* Seed the per-target skb pool that find_skb() falls back to. The queue
 * head and refill work are set up once in alloc_and_init(); this only
 * (re)fills the pool. Pair with netconsole_skb_pool_flush().
 */
static void netconsole_skb_pool_init(struct netconsole_target *nt)
{
	refill_skbs(nt);
}

static void netconsole_skb_pool_flush(struct netconsole_target *nt)
{
	cancel_work_sync(&nt->refill_wq);
	skb_queue_purge_reason(&nt->skb_pool, SKB_CONSUMED);
}

static void netcons_wait_carrier(struct netpoll *np, struct net_device *ndev)
{
	unsigned long atmost;

	atmost = jiffies + netpoll_get_carrier_timeout() * HZ;
	while (!netif_carrier_ok(ndev)) {
		if (time_after(jiffies, atmost)) {
			np_notice(np, "timeout waiting for carrier\n");
			break;
		}
		msleep(1);
	}
}

/*
 * Returns a pointer to a string representation of the identifier used
 * to select the egress interface for the given netpoll instance. buf
 * is used to format np->dev_mac when np->dev_name is empty; bufsz must
 * be at least MAC_ADDR_STR_LEN + 1 to fit the formatted MAC address
 * and its NUL terminator.
 */
static char *netcons_egress_dev(struct netpoll *np, char *buf, size_t bufsz)
{
	if (np->dev_name[0])
		return np->dev_name;

	snprintf(buf, bufsz, "%pM", np->dev_mac);
	return buf;
}

/*
 * Populate the target's local_ip with the IPv6 address from ndev.
 */
static int netcons_take_ipv6(struct netconsole_target *nt,
			     struct net_device *ndev)
{
	char buf[MAC_ADDR_STR_LEN + 1];
	struct netpoll *np = &nt->np;
	int err = -EDESTADDRREQ;
	struct inet6_dev *idev;

	if (!IS_ENABLED(CONFIG_IPV6)) {
		np_err(np, "IPv6 is not supported %s, aborting\n",
		       netcons_egress_dev(np, buf, sizeof(buf)));
		return -EINVAL;
	}

	idev = __in6_dev_get(ndev);
	if (idev) {
		struct inet6_ifaddr *ifp;

		read_lock_bh(&idev->lock);
		list_for_each_entry(ifp, &idev->addr_list, if_list) {
			if (!!(ipv6_addr_type(&ifp->addr) & IPV6_ADDR_LINKLOCAL) !=
				!!(ipv6_addr_type(&nt->remote_ip.in6) & IPV6_ADDR_LINKLOCAL))
				continue;
			/* Got the IP, let's return */
			nt->local_ip.in6 = ifp->addr;
			err = 0;
			break;
		}
		read_unlock_bh(&idev->lock);
	}
	if (err) {
		np_err(np, "no IPv6 address for %s, aborting\n",
		       netcons_egress_dev(np, buf, sizeof(buf)));
		return err;
	}

	np_info(np, "local IPv6 %pI6c\n", &nt->local_ip.in6);
	return 0;
}

/*
 * Populate the target's local_ip with the IPv4 address from ndev.
 */
static int netcons_take_ipv4(struct netconsole_target *nt,
			     struct net_device *ndev)
{
	char buf[MAC_ADDR_STR_LEN + 1];
	struct netpoll *np = &nt->np;
	const struct in_ifaddr *ifa;
	struct in_device *in_dev;

	in_dev = __in_dev_get_rtnl(ndev);
	if (!in_dev) {
		np_err(np, "no IP address for %s, aborting\n",
		       netcons_egress_dev(np, buf, sizeof(buf)));
		return -EDESTADDRREQ;
	}

	ifa = rtnl_dereference(in_dev->ifa_list);
	if (!ifa) {
		np_err(np, "no IP address for %s, aborting\n",
		       netcons_egress_dev(np, buf, sizeof(buf)));
		return -EDESTADDRREQ;
	}

	nt->local_ip.ip = ifa->ifa_local;
	np_info(np, "local IP %pI4\n", &nt->local_ip.ip);

	return 0;
}

/*
 * Test whether the caller left nt->local_ip unset, so that
 * netcons_netpoll_setup() should auto-populate it from the egress device.
 *
 * nt->local_ip is a union of __be32 (IPv4) and struct in6_addr (IPv6),
 * so an IPv6 address whose first 4 bytes are zero (e.g. ::1, ::2,
 * IPv4-mapped ::ffff:a.b.c.d) must not be tested via the IPv4 arm —
 * doing so would misclassify a caller-supplied address as unset and
 * silently overwrite it with whatever address the device exposes.
 */
static bool netcons_local_ip_unset(const struct netconsole_target *nt)
{
	if (nt->ipv6)
		return ipv6_addr_any(&nt->local_ip.in6);
	return !nt->local_ip.ip;
}

static int netcons_netpoll_setup(struct netconsole_target *nt)
{
	struct net *net = current->nsproxy->net_ns;
	char buf[MAC_ADDR_STR_LEN + 1];
	struct net_device *ndev = NULL;
	struct netpoll *np = &nt->np;
	bool ip_overwritten = false;
	int err;

	rtnl_lock();
	if (np->dev_name[0])
		ndev = __dev_get_by_name(net, np->dev_name);
	else if (is_valid_ether_addr(np->dev_mac))
		ndev = dev_getbyhwaddr(net, ARPHRD_ETHER, np->dev_mac);

	if (!ndev) {
		np_err(np, "%s doesn't exist, aborting\n",
		       netcons_egress_dev(np, buf, sizeof(buf)));
		err = -ENODEV;
		goto unlock;
	}
	netdev_hold(ndev, &np->dev_tracker, GFP_KERNEL);

	if (netdev_master_upper_dev_get(ndev)) {
		np_err(np, "%s is a slave device, aborting\n",
		       netcons_egress_dev(np, buf, sizeof(buf)));
		err = -EBUSY;
		goto put;
	}

	if (!netif_running(ndev)) {
		np_info(np, "device %s not up yet, forcing it\n",
			netcons_egress_dev(np, buf, sizeof(buf)));

		err = dev_open(ndev, NULL);
		if (err) {
			np_err(np, "failed to open %s\n", ndev->name);
			goto put;
		}

		rtnl_unlock();
		netcons_wait_carrier(np, ndev);
		rtnl_lock();
	}

	if (netcons_local_ip_unset(nt)) {
		if (!nt->ipv6) {
			err = netcons_take_ipv4(nt, ndev);
			if (err)
				goto put;
		} else {
			err = netcons_take_ipv6(nt, ndev);
			if (err)
				goto put;
		}
		ip_overwritten = true;
	}

	err = __netpoll_setup(np, ndev);
	if (err)
		goto put;
	rtnl_unlock();

	/* Make sure all NAPI polls which started before dev->npinfo
	 * was visible have exited before we start calling NAPI poll.
	 * NAPI skips locking if dev->npinfo is NULL.
	 */
	synchronize_rcu();

	return 0;

put:
	DEBUG_NET_WARN_ON_ONCE(np->dev);
	if (ip_overwritten)
		memset(&nt->local_ip, 0, sizeof(nt->local_ip));
	netdev_put(ndev, &np->dev_tracker);
unlock:
	rtnl_unlock();
	return err;
}

/* Attempts to resume logging to a deactivated target. */
static void resume_target(struct netconsole_target *nt)
{
	/* Initialise the skb pool before netpoll_setup() makes nt->np.dev
	 * visible to target_list walkers (e.g. netconsole_netdev_event),
	 * which otherwise may move the target to the cleanup list and
	 * call netconsole_skb_pool_flush() on uninitialised state.
	 */
	netconsole_skb_pool_init(nt);

	if (netcons_netpoll_setup(nt)) {
		/* netpoll fails setup once, do not try again. */
		netconsole_skb_pool_flush(nt);
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

	/* netpoll_setup() took a net_device reference and dropped the RTNL
	 * before returning, all while this target was off target_list and
	 * thus invisible to netconsole_netdev_event(). If the device was
	 * unregistered in that window the NETDEV_UNREGISTER notifier could not
	 * tear this target down, which would leak the reference and hang
	 * unregister_netdevice(). Re-check under the RTNL before re-publishing:
	 * taking it across the check and the list_add() serialises against the
	 * notifier (which also runs under the RTNL), so the device is either
	 * still registered (the notifier will find the re-added target) or
	 * already unregistering (we drop the reference here).
	 */
	rtnl_lock();
	if (nt->state == STATE_ENABLED && nt->np.dev &&
	    nt->np.dev->reg_state != NETREG_REGISTERED) {
		netconsole_skb_pool_flush(nt);
		netcons_release_dev(nt);
		nt->state = STATE_DISABLED;
	}

	/* At this point the target is either enabled or disabled and
	 * was cleaned up before getting deactivated. Either way, add it
	 * back to target list.
	 */
	spin_lock_irqsave(&target_list_lock, flags);
	list_add(&nt->list, &target_list);
	spin_unlock_irqrestore(&target_list_lock, flags);
	rtnl_unlock();

out_unlock:
	dynamic_netconsole_mutex_unlock();
}

/* Allocate and initialize with defaults.
 * Note that these targets get their config_item fields zeroed-out.
 */
static struct netconsole_target *alloc_and_init(void)
{
	struct netconsole_target *nt;

	nt = kzalloc_obj(*nt);
	if (!nt)
		return nt;

	if (IS_ENABLED(CONFIG_NETCONSOLE_EXTENDED_LOG))
		nt->extended = true;
	if (IS_ENABLED(CONFIG_NETCONSOLE_PREPEND_RELEASE))
		nt->release = true;

	nt->np.name = "netconsole";
	strscpy(nt->np.dev_name, "eth0", IFNAMSIZ);
	nt->local_port = 6665;
	nt->remote_port = 6666;
	eth_broadcast_addr(nt->remote_mac);
	nt->state = STATE_DISABLED;
	INIT_WORK(&nt->resume_wq, process_resume_target);
	/* Set up the skb pool primitives once; enabling only refills it. */
	skb_queue_head_init(&nt->skb_pool);
	INIT_WORK(&nt->refill_wq, refill_skbs_work_handler);

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
		netconsole_skb_pool_flush(nt);
		netcons_release_dev(nt);
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

static void netconsole_print_banner(struct netconsole_target *nt)
{
	struct netpoll *np = &nt->np;

	np_info(np, "local port %d\n", nt->local_port);
	if (nt->ipv6)
		np_info(np, "local IPv6 address %pI6c\n", &nt->local_ip.in6);
	else
		np_info(np, "local IPv4 address %pI4\n", &nt->local_ip.ip);
	np_info(np, "interface name '%s'\n", np->dev_name);
	np_info(np, "local ethernet address '%pM'\n", np->dev_mac);
	np_info(np, "remote port %d\n", nt->remote_port);
	if (nt->ipv6)
		np_info(np, "remote IPv6 address %pI6c\n", &nt->remote_ip.in6);
	else
		np_info(np, "remote IPv4 address %pI4\n", &nt->remote_ip.ip);
	np_info(np, "remote ethernet address %pM\n", nt->remote_mac);
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
	if (!len)
		return;
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
	return sysfs_emit(buf, "%d\n", to_target(item)->local_port);
}

static ssize_t remote_port_show(struct config_item *item, char *buf)
{
	return sysfs_emit(buf, "%d\n", to_target(item)->remote_port);
}

static ssize_t local_ip_show(struct config_item *item, char *buf)
{
	struct netconsole_target *nt = to_target(item);

	if (nt->ipv6)
		return sysfs_emit(buf, "%pI6c\n", &nt->local_ip.in6);
	else
		return sysfs_emit(buf, "%pI4\n", &nt->local_ip);
}

static ssize_t remote_ip_show(struct config_item *item, char *buf)
{
	struct netconsole_target *nt = to_target(item);

	if (nt->ipv6)
		return sysfs_emit(buf, "%pI6c\n", &nt->remote_ip.in6);
	else
		return sysfs_emit(buf, "%pI4\n", &nt->remote_ip);
}

static ssize_t local_mac_show(struct config_item *item, char *buf)
{
	struct net_device *dev = to_target(item)->np.dev;
	static const u8 bcast[ETH_ALEN] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

	return sysfs_emit(buf, "%pM\n", dev ? dev->dev_addr : bcast);
}

static ssize_t remote_mac_show(struct config_item *item, char *buf)
{
	return sysfs_emit(buf, "%pM\n", to_target(item)->remote_mac);
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
	release_enabled = !!(nt->sysdata_fields & SYSDATA_RELEASE);
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
		netconsole_print_banner(nt);

		/* Initialise the skb pool before netpoll_setup() so the pool
		 * is valid as soon as nt->np.dev becomes visible to
		 * target_list walkers (netconsole_netdev_event), which would
		 * otherwise call netconsole_skb_pool_flush() on uninitialised
		 * state.
		 */
		netconsole_skb_pool_init(nt);

		ret = netcons_netpoll_setup(nt);
		if (ret) {
			netconsole_skb_pool_flush(nt);
			goto out_unlock;
		}

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

	ret = count;
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

	ret = count;
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
	ret = count;
out_unlock:
	dynamic_netconsole_mutex_unlock();
	return ret;
}

static ssize_t dev_name_store(struct config_item *item, const char *buf,
		size_t count)
{
	struct netconsole_target *nt = to_target(item);
	size_t len = count;

	/* Account for a trailing newline appended by tools like echo */
	if (len && buf[len - 1] == '\n')
		len--;
	if (len >= IFNAMSIZ)
		return -ENAMETOOLONG;

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
	return count;
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

	ret = kstrtou16(buf, 10, &nt->local_port);
	if (ret < 0)
		goto out_unlock;
	ret = count;
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

	ret = kstrtou16(buf, 10, &nt->remote_port);
	if (ret < 0)
		goto out_unlock;
	ret = count;
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

	ipv6 = netpoll_parse_ip_addr(buf, &nt->local_ip);
	if (ipv6 == -1)
		goto out_unlock;
	nt->ipv6 = !!ipv6;

	ret = count;
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

	ipv6 = netpoll_parse_ip_addr(buf, &nt->remote_ip);
	if (ipv6 == -1)
		goto out_unlock;
	nt->ipv6 = !!ipv6;

	ret = count;
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
	memcpy(nt->remote_mac, remote_mac, ETH_ALEN);

	ret = count;
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
	struct netcons_userdata *new = NULL;
	struct netcons_userdata *old;
	struct userdatum *udm_item;
	struct config_item *item;
	struct list_head *entry;
	int offset = 0;
	int len;

	/* Calculate required buffer size */
	len = calc_userdata_len(nt);

	if (WARN_ON_ONCE(len > MAX_EXTRADATA_ENTRY_LEN * MAX_USERDATA_ITEMS))
		return -ENOSPC;

	/* Allocate new buffer */
	if (len) {
		new = kmalloc_flex(*new, data, len + 1);
		if (!new)
			return -ENOMEM;
	}

	/* Write userdata to new buffer */
	list_for_each(entry, &nt->userdata_group.cg_children) {
		item = container_of(entry, struct config_item, ci_entry);
		udm_item = to_userdatum(item);
		/* Skip userdata with no value set */
		if (udm_item->value[0]) {
			offset += scnprintf(&new->data[offset],
					    len + 1 - offset,
					    " %s=%s\n", item->ci_name,
					    udm_item->value);
		}
	}

	WARN_ON_ONCE(offset != len);
	if (new)
		new->length = offset;

	/* Writers are serialized by dynamic_netconsole_mutex. */
	old = rcu_replace_pointer(nt->userdata, new,
				  lockdep_is_held(&dynamic_netconsole_mutex));
	kfree_rcu(old, rcu);

	return 0;
}

static ssize_t userdatum_value_store(struct config_item *item, const char *buf,
				     size_t count)
{
	struct userdatum *udm = to_userdatum(item);
	char old_value[MAX_EXTRADATA_VALUE_LEN];
	struct netconsole_target *nt;
	struct userdata *ud;
	ssize_t ret;

	if (count >= MAX_EXTRADATA_VALUE_LEN)
		return -EMSGSIZE;

	mutex_lock(&netconsole_subsys.su_mutex);
	dynamic_netconsole_mutex_lock();
	/* Snapshot for rollback if update_userdata() fails below */
	strscpy(old_value, udm->value, sizeof(old_value));
	/* count is bounded above, so strscpy() cannot truncate here */
	strscpy(udm->value, buf, sizeof(udm->value));
	trim_newline(udm->value, sizeof(udm->value));

	ud = to_userdata(item->ci_parent);
	nt = userdata_to_target(ud);
	ret = update_userdata(nt);
	if (ret < 0) {
		/* Restore the previous value so it matches the live payload */
		strscpy(udm->value, old_value, sizeof(udm->value));
		goto out_unlock;
	}
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
	ret = count;
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
	ret = count;
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
	ret = count;
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
	ret = count;
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

static const struct configfs_item_operations userdatum_ops = {
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

	udm = kzalloc_obj(*udm);
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

static const struct configfs_group_operations userdata_ops = {
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

	kfree(rcu_access_pointer(nt->userdata));
	kfree(nt);
}

static const struct configfs_item_operations netconsole_target_item_ops = {
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
	bool needs_cleanup;

	dynamic_netconsole_mutex_lock();

	mutex_lock(&target_cleanup_list_lock);
	spin_lock_irqsave(&target_list_lock, flags);
	/* A target moved to target_cleanup_list by netconsole_netdev_event()
	 * but not yet processed still owns a netpoll; unlinking it below hides
	 * it from the cleanup worker, so this path must tear it down itself.
	 * This covers NETDEV_UNREGISTER (STATE_DEACTIVATED) and
	 * NETDEV_RELEASE / NETDEV_JOIN (STATE_DISABLED); key off nt->np.dev,
	 * which stays set until the netpoll is cleaned up.
	 */
	needs_cleanup = nt->state == STATE_ENABLED ||
			nt->state == STATE_DEACTIVATED || nt->np.dev;
	/* Disable deactivated target to prevent races between resume attempt
	 * and target removal.
	 */
	if (nt->state == STATE_DEACTIVATED)
		nt->state = STATE_DISABLED;
	list_del(&nt->list);
	spin_unlock_irqrestore(&target_list_lock, flags);
	mutex_unlock(&target_cleanup_list_lock);

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
	 * netpoll_cleanup() is idempotent (it skips when np->dev is NULL), so
	 * it is safe even if the cleanup worker already tore the netpoll down.
	 */
	if (needs_cleanup) {
		netconsole_skb_pool_flush(nt);
		netpoll_cleanup(&nt->np);
	}

	config_item_put(&nt->group.cg_item);
}

static const struct configfs_group_operations netconsole_subsys_group_ops = {
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

/* Pop a pre-allocated skb from the pool and request a refill.
 *
 * The pool is refilled with MAX_SKB_SIZE buffers, so a pooled skb cannot
 * satisfy a larger request. Return NULL in that case rather than handing
 * back a too-small skb that would later trip skb_over_panic() in skb_put();
 * the caller still polls and retries, and alloc_skb() itself can satisfy the
 * oversized request once memory frees up.
 *
 * The refill is requested via schedule_work(), which takes the workqueue
 * pool locks and is therefore not NMI-safe. Skip the refill when called
 * from NMI context; the next non-NMI caller will top the pool back up.
 */
static struct sk_buff *netcons_skb_pop(struct netconsole_target *nt, int len)
{
	struct sk_buff *skb;

	if (len > MAX_SKB_SIZE) {
		/* net_warn_ratelimited() pulls in printk machinery that is not
		 * NMI-safe and could recurse into the nbcon console we are
		 * servicing, so only warn outside NMI.
		 */
		if (!in_nmi())
			net_warn_ratelimited("netconsole: dropping message, requested skb len %d exceeds pool buffer size %zu on %s\n",
					     len, (size_t)MAX_SKB_SIZE,
					     nt->np.dev->name);
		return NULL;
	}

	skb = skb_dequeue(&nt->skb_pool);
	if (!in_nmi())
		schedule_work(&nt->refill_wq);

	return skb;
}

static struct sk_buff *find_skb(struct netconsole_target *nt, int len,
				int reserve)
{
	struct netpoll *np = &nt->np;
	int count = 0;
	struct sk_buff *skb;

	netpoll_zap_completion_queue();
repeat:

	skb = alloc_skb(len, GFP_ATOMIC | __GFP_NOWARN);
	if (!skb)
		skb = netcons_skb_pop(nt, len);

	if (!skb) {
		if (++count < 10) {
			netpoll_poll_dev(np->dev);
			goto repeat;
		}
		return NULL;
	}

	refcount_set(&skb->users, 1);
	skb_reserve(skb, reserve);
	return skb;
}

static void netpoll_udp_checksum(struct netconsole_target *nt,
				 struct sk_buff *skb, int len)
{
	struct udphdr *udph;
	int udp_len;

	udp_len = len + sizeof(struct udphdr);
	udph = udp_hdr(skb);

	/* check needs to be set, since it will be consumed in csum_partial */
	udph->check = 0;
	if (nt->ipv6)
		udph->check = csum_ipv6_magic(&nt->local_ip.in6,
					      &nt->remote_ip.in6,
					      udp_len, IPPROTO_UDP,
					      csum_partial(udph, udp_len, 0));
	else
		udph->check = csum_tcpudp_magic(nt->local_ip.ip,
						nt->remote_ip.ip,
						udp_len, IPPROTO_UDP,
						csum_partial(udph, udp_len, 0));
	if (udph->check == 0)
		udph->check = CSUM_MANGLED_0;
}

static void push_udp(struct netconsole_target *nt, struct sk_buff *skb, int len)
{
	struct udphdr *udph;
	int udp_len;

	udp_len = len + sizeof(struct udphdr);

	skb_push(skb, sizeof(struct udphdr));
	skb_reset_transport_header(skb);

	udph = udp_hdr(skb);
	udph->source = htons(nt->local_port);
	udph->dest = htons(nt->remote_port);
	udp_set_len_short(udph, udp_len);

	netpoll_udp_checksum(nt, skb, len);
}

static void push_eth(struct netconsole_target *nt, struct sk_buff *skb)
{
	struct netpoll *np = &nt->np;
	struct ethhdr *eth;

	eth = skb_push(skb, ETH_HLEN);
	skb_reset_mac_header(skb);
	ether_addr_copy(eth->h_source, np->dev->dev_addr);
	ether_addr_copy(eth->h_dest, nt->remote_mac);
	if (nt->ipv6)
		eth->h_proto = htons(ETH_P_IPV6);
	else
		eth->h_proto = htons(ETH_P_IP);
}

static void push_ipv4(struct netconsole_target *nt, struct sk_buff *skb,
		      int len)
{
	static atomic_t ip_ident;
	struct iphdr *iph;
	int ip_len;

	ip_len = len + sizeof(struct udphdr) + sizeof(struct iphdr);

	skb_push(skb, sizeof(struct iphdr));
	skb_reset_network_header(skb);
	iph = ip_hdr(skb);

	/* iph->version = 4; iph->ihl = 5; */
	*(unsigned char *)iph = 0x45;
	iph->tos = 0;
	put_unaligned(htons(ip_len), &iph->tot_len);
	iph->id = htons(atomic_inc_return(&ip_ident));
	iph->frag_off = 0;
	iph->ttl = 64;
	iph->protocol = IPPROTO_UDP;
	iph->check = 0;
	put_unaligned(nt->local_ip.ip, &iph->saddr);
	put_unaligned(nt->remote_ip.ip, &iph->daddr);
	iph->check = ip_fast_csum((unsigned char *)iph, iph->ihl);
	skb->protocol = htons(ETH_P_IP);
}

static void push_ipv6(struct netconsole_target *nt, struct sk_buff *skb,
		      int len)
{
	struct ipv6hdr *ip6h;

	skb_push(skb, sizeof(struct ipv6hdr));
	skb_reset_network_header(skb);
	ip6h = ipv6_hdr(skb);

	/* ip6h->version = 6; ip6h->priority = 0; */
	*(unsigned char *)ip6h = 0x60;
	ip6h->flow_lbl[0] = 0;
	ip6h->flow_lbl[1] = 0;
	ip6h->flow_lbl[2] = 0;

	ip6h->payload_len = htons(sizeof(struct udphdr) + len);
	ip6h->nexthdr = IPPROTO_UDP;
	ip6h->hop_limit = 32;
	ip6h->saddr = nt->local_ip.in6;
	ip6h->daddr = nt->remote_ip.in6;

	skb->protocol = htons(ETH_P_IPV6);
}

static int netpoll_send_udp(struct netconsole_target *nt, const char *msg,
			    int len)
{
	struct netpoll *np = &nt->np;
	int total_len, ip_len, udp_len;
	struct sk_buff *skb;

	if (!IS_ENABLED(CONFIG_PREEMPT_RT))
		WARN_ON_ONCE(!irqs_disabled());

	udp_len = len + sizeof(struct udphdr);
	if (nt->ipv6)
		ip_len = udp_len + sizeof(struct ipv6hdr);
	else
		ip_len = udp_len + sizeof(struct iphdr);

	total_len = ip_len + LL_RESERVED_SPACE(np->dev);

	skb = find_skb(nt, total_len + np->dev->needed_tailroom,
		       total_len - len);
	if (!skb)
		return -ENOMEM;

	skb_copy_to_linear_data(skb, msg, len);
	skb_put(skb, len);

	push_udp(nt, skb, len);
	if (nt->ipv6)
		push_ipv6(nt, skb, len);
	else
		push_ipv4(nt, skb, len);
	push_eth(nt, skb);
	skb->dev = np->dev;

	return (int)netpoll_send_skb(np, skb);
}

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
	int result = netpoll_send_udp(nt, msg, len);

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
				      int release_len,
				      const struct netcons_userdata *userdata)
{
	const char *sysdata = NULL;
	const char *release;

#ifdef CONFIG_NETCONSOLE_DYNAMIC
	sysdata = nt->sysdata;
#endif

	if (release_len) {
		release = init_utsname()->release;

		scnprintf(nt->buf, sizeof(nt->buf), "%s,%.*s", release,
			  msg_len, msg);
		msg_len += release_len;
	} else {
		memcpy(nt->buf, msg, msg_len);
	}

	if (userdata)
		msg_len += scnprintf(&nt->buf[msg_len],
				     sizeof(nt->buf) - msg_len, "%s",
				     userdata->data);

	if (sysdata)
		msg_len += scnprintf(&nt->buf[msg_len],
				     sizeof(nt->buf) - msg_len, "%s",
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
				 int msgbody_len, int sysdata_len,
				 const struct netcons_userdata *userdata)
{
	const char *userdata_ptr = NULL;
	const char *sysdata_ptr = NULL;
	int data_len, data_sent = 0;
	int userdata_offset = 0;
	int sysdata_offset = 0;
	int msgbody_offset = 0;
	int userdata_len = 0;

#ifdef CONFIG_NETCONSOLE_DYNAMIC
	sysdata_ptr = nt->sysdata;
#endif
	if (userdata) {
		userdata_ptr = userdata->data;
		userdata_len = userdata->length;
	}

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
				int sysdata_len,
				const struct netcons_userdata *userdata)
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
			     sysdata_len, userdata);
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
	const struct netcons_userdata *userdata = NULL;
	int userdata_len = 0;
	int release_len = 0;
	int sysdata_len = 0;
	int len;

	/* Keeps the payload picked below alive until the last send_udp(). */
	rcu_read_lock();

#ifdef CONFIG_NETCONSOLE_DYNAMIC
	sysdata_len = prepare_sysdata(nt, wctxt);
	userdata = rcu_dereference(nt->userdata);
	if (userdata)
		userdata_len = userdata->length;
#endif
	if (nt->release)
		release_len = strlen(init_utsname()->release) + 1;

	len = wctxt->len + release_len + sysdata_len + userdata_len;
	if (len <= MAX_PRINT_CHUNK)
		send_msg_no_fragmentation(nt, wctxt->outbuf, wctxt->len,
					  release_len, userdata);
	else
		send_msg_fragmented(nt, wctxt->outbuf, wctxt->len, release_len,
				    sysdata_len, userdata);

	rcu_read_unlock();
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

static int netconsole_parser_cmdline(struct netconsole_target *nt, char *opt)
{
	struct netpoll *np = &nt->np;
	bool ipversion_set = false;
	char *cur = opt;
	char *delim;
	int ipv6;

	if (*cur != '@') {
		delim = strchr(cur, '@');
		if (!delim)
			goto parse_failed;
		*delim = 0;
		if (kstrtou16(cur, 10, &nt->local_port))
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
		ipv6 = netpoll_parse_ip_addr(cur, &nt->local_ip);
		if (ipv6 < 0)
			goto parse_failed;
		else
			nt->ipv6 = (bool)ipv6;
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
		if (kstrtou16(cur, 10, &nt->remote_port))
			goto parse_failed;
		cur = delim;
	}
	cur++;

	/* dst ip */
	delim = strchr(cur, '/');
	if (!delim)
		goto parse_failed;
	*delim = 0;
	ipv6 = netpoll_parse_ip_addr(cur, &nt->remote_ip);
	if (ipv6 < 0)
		goto parse_failed;
	else if (ipversion_set && nt->ipv6 != (bool)ipv6)
		goto parse_failed;
	else
		nt->ipv6 = (bool)ipv6;
	cur = delim + 1;

	if (*cur != 0) {
		/* MAC address */
		if (!mac_pton(cur, nt->remote_mac))
			goto parse_failed;
	}

	netconsole_print_banner(nt);

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
	err = netconsole_parser_cmdline(nt, target_config);
	if (err)
		goto fail;

	/* Initialise the skb pool before netpoll_setup() so the pool is
	 * valid as soon as nt->np.dev becomes visible. The target is not
	 * yet on target_list, so a netdev event cannot reach it here, but
	 * mirror the configfs path for symmetry.
	 */
	netconsole_skb_pool_init(nt);

	err = netcons_netpoll_setup(nt);
	if (err) {
		pr_err("Not enabling netconsole for %s%d. Netpoll setup failed\n",
		       NETCONSOLE_PARAM_TARGET_PREFIX, cmdline_count);
		netconsole_skb_pool_flush(nt);
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
	if (nt->state == STATE_ENABLED)
		netconsole_skb_pool_flush(nt);
	netpoll_cleanup(&nt->np);
#ifdef	CONFIG_NETCONSOLE_DYNAMIC
	kfree(rcu_access_pointer(nt->userdata));
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
