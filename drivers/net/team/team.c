/*
 * drivers/net/team/team.c - Network team device driver
 * Copyright (c) 2011 Jiri Pirko <jpirko@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/rcupdate.h>
#include <linux/errno.h>
#include <linux/ctype.h>
#include <linux/notifier.h>
#include <linux/netdevice.h>
#include <linux/netpoll.h>
#include <linux/if_vlan.h>
#include <linux/if_arp.h>
#include <linux/socket.h>
#include <linux/etherdevice.h>
#include <linux/rtnetlink.h>
#include <net/rtnetlink.h>
#include <net/genetlink.h>
#include <net/netlink.h>
#include <net/sch_generic.h>
#include <generated/utsrelease.h>
#include <linux/if_team.h>

#define DRV_NAME "team"


/**********
 * Helpers
 **********/

#define team_port_exists(dev) (dev->priv_flags & IFF_TEAM_PORT)

static struct team_port *team_port_get_rcu(const struct net_device *dev)
{
	struct team_port *port = rcu_dereference(dev->rx_handler_data);

	return team_port_exists(dev) ? port : NULL;
}

static struct team_port *team_port_get_rtnl(const struct net_device *dev)
{
	struct team_port *port = rtnl_dereference(dev->rx_handler_data);

	return team_port_exists(dev) ? port : NULL;
}

/*
 * Since the ability to change device address for open port device is tested in
 * team_port_add, this function can be called without control of return value
 */
static int __set_port_dev_addr(struct net_device *port_dev,
			       const unsigned char *dev_addr)
{
	struct sockaddr addr;

	memcpy(addr.sa_data, dev_addr, port_dev->addr_len);
	addr.sa_family = port_dev->type;
	return dev_set_mac_address(port_dev, &addr);
}

static int team_port_set_orig_dev_addr(struct team_port *port)
{
	return __set_port_dev_addr(port->dev, port->orig.dev_addr);
}

static int team_port_set_team_dev_addr(struct team *team,
				       struct team_port *port)
{
	return __set_port_dev_addr(port->dev, team->dev->dev_addr);
}

int team_modeop_port_enter(struct team *team, struct team_port *port)
{
	return team_port_set_team_dev_addr(team, port);
}
EXPORT_SYMBOL(team_modeop_port_enter);

void team_modeop_port_change_dev_addr(struct team *team,
				      struct team_port *port)
{
	team_port_set_team_dev_addr(team, port);
}
EXPORT_SYMBOL(team_modeop_port_change_dev_addr);

static void team_refresh_port_linkup(struct team_port *port)
{
	port->linkup = port->user.linkup_enabled ? port->user.linkup :
						   port->state.linkup;
}


/*******************
 * Options handling
 *******************/

struct team_option_inst { /* One for each option instance */
	struct list_head list;
	struct list_head tmp_list;
	struct team_option *option;
	struct team_option_inst_info info;
	bool changed;
	bool removed;
};

static struct team_option *__team_find_option(struct team *team,
					      const char *opt_name)
{
	struct team_option *option;

	list_for_each_entry(option, &team->option_list, list) {
		if (strcmp(option->name, opt_name) == 0)
			return option;
	}
	return NULL;
}

static void __team_option_inst_del(struct team_option_inst *opt_inst)
{
	list_del(&opt_inst->list);
	kfree(opt_inst);
}

static void __team_option_inst_del_option(struct team *team,
					  struct team_option *option)
{
	struct team_option_inst *opt_inst, *tmp;

	list_for_each_entry_safe(opt_inst, tmp, &team->option_inst_list, list) {
		if (opt_inst->option == option)
			__team_option_inst_del(opt_inst);
	}
}

static int __team_option_inst_add(struct team *team, struct team_option *option,
				  struct team_port *port)
{
	struct team_option_inst *opt_inst;
	unsigned int array_size;
	unsigned int i;
	int err;

	array_size = option->array_size;
	if (!array_size)
		array_size = 1; /* No array but still need one instance */

	for (i = 0; i < array_size; i++) {
		opt_inst = kmalloc(sizeof(*opt_inst), GFP_KERNEL);
		if (!opt_inst)
			return -ENOMEM;
		opt_inst->option = option;
		opt_inst->info.port = port;
		opt_inst->info.array_index = i;
		opt_inst->changed = true;
		opt_inst->removed = false;
		list_add_tail(&opt_inst->list, &team->option_inst_list);
		if (option->init) {
			err = option->init(team, &opt_inst->info);
			if (err)
				return err;
		}

	}
	return 0;
}

static int __team_option_inst_add_option(struct team *team,
					 struct team_option *option)
{
	struct team_port *port;
	int err;

	if (!option->per_port) {
		err = __team_option_inst_add(team, option, NULL);
		if (err)
			goto inst_del_option;
	}

	list_for_each_entry(port, &team->port_list, list) {
		err = __team_option_inst_add(team, option, port);
		if (err)
			goto inst_del_option;
	}
	return 0;

inst_del_option:
	__team_option_inst_del_option(team, option);
	return err;
}

static void __team_option_inst_mark_removed_option(struct team *team,
						   struct team_option *option)
{
	struct team_option_inst *opt_inst;

	list_for_each_entry(opt_inst, &team->option_inst_list, list) {
		if (opt_inst->option == option) {
			opt_inst->changed = true;
			opt_inst->removed = true;
		}
	}
}

static void __team_option_inst_del_port(struct team *team,
					struct team_port *port)
{
	struct team_option_inst *opt_inst, *tmp;

	list_for_each_entry_safe(opt_inst, tmp, &team->option_inst_list, list) {
		if (opt_inst->option->per_port &&
		    opt_inst->info.port == port)
			__team_option_inst_del(opt_inst);
	}
}

static int __team_option_inst_add_port(struct team *team,
				       struct team_port *port)
{
	struct team_option *option;
	int err;

	list_for_each_entry(option, &team->option_list, list) {
		if (!option->per_port)
			continue;
		err = __team_option_inst_add(team, option, port);
		if (err)
			goto inst_del_port;
	}
	return 0;

inst_del_port:
	__team_option_inst_del_port(team, port);
	return err;
}

static void __team_option_inst_mark_removed_port(struct team *team,
						 struct team_port *port)
{
	struct team_option_inst *opt_inst;

	list_for_each_entry(opt_inst, &team->option_inst_list, list) {
		if (opt_inst->info.port == port) {
			opt_inst->changed = true;
			opt_inst->removed = true;
		}
	}
}

static int __team_options_register(struct team *team,
				   const struct team_option *option,
				   size_t option_count)
{
	int i;
	struct team_option **dst_opts;
	int err;

	dst_opts = kzalloc(sizeof(struct team_option *) * option_count,
			   GFP_KERNEL);
	if (!dst_opts)
		return -ENOMEM;
	for (i = 0; i < option_count; i++, option++) {
		if (__team_find_option(team, option->name)) {
			err = -EEXIST;
			goto alloc_rollback;
		}
		dst_opts[i] = kmemdup(option, sizeof(*option), GFP_KERNEL);
		if (!dst_opts[i]) {
			err = -ENOMEM;
			goto alloc_rollback;
		}
	}

	for (i = 0; i < option_count; i++) {
		err = __team_option_inst_add_option(team, dst_opts[i]);
		if (err)
			goto inst_rollback;
		list_add_tail(&dst_opts[i]->list, &team->option_list);
	}

	kfree(dst_opts);
	return 0;

inst_rollback:
	for (i--; i >= 0; i--)
		__team_option_inst_del_option(team, dst_opts[i]);

	i = option_count - 1;
alloc_rollback:
	for (i--; i >= 0; i--)
		kfree(dst_opts[i]);

	kfree(dst_opts);
	return err;
}

static void __team_options_mark_removed(struct team *team,
					const struct team_option *option,
					size_t option_count)
{
	int i;

	for (i = 0; i < option_count; i++, option++) {
		struct team_option *del_opt;

		del_opt = __team_find_option(team, option->name);
		if (del_opt)
			__team_option_inst_mark_removed_option(team, del_opt);
	}
}

static void __team_options_unregister(struct team *team,
				      const struct team_option *option,
				      size_t option_count)
{
	int i;

	for (i = 0; i < option_count; i++, option++) {
		struct team_option *del_opt;

		del_opt = __team_find_option(team, option->name);
		if (del_opt) {
			__team_option_inst_del_option(team, del_opt);
			list_del(&del_opt->list);
			kfree(del_opt);
		}
	}
}

static void __team_options_change_check(struct team *team);

int team_options_register(struct team *team,
			  const struct team_option *option,
			  size_t option_count)
{
	int err;

	err = __team_options_register(team, option, option_count);
	if (err)
		return err;
	__team_options_change_check(team);
	return 0;
}
EXPORT_SYMBOL(team_options_register);

void team_options_unregister(struct team *team,
			     const struct team_option *option,
			     size_t option_count)
{
	__team_options_mark_removed(team, option, option_count);
	__team_options_change_check(team);
	__team_options_unregister(team, option, option_count);
}
EXPORT_SYMBOL(team_options_unregister);

static int team_option_get(struct team *team,
			   struct team_option_inst *opt_inst,
			   struct team_gsetter_ctx *ctx)
{
	if (!opt_inst->option->getter)
		return -EOPNOTSUPP;
	return opt_inst->option->getter(team, ctx);
}

static int team_option_set(struct team *team,
			   struct team_option_inst *opt_inst,
			   struct team_gsetter_ctx *ctx)
{
	if (!opt_inst->option->setter)
		return -EOPNOTSUPP;
	return opt_inst->option->setter(team, ctx);
}

void team_option_inst_set_change(struct team_option_inst_info *opt_inst_info)
{
	struct team_option_inst *opt_inst;

	opt_inst = container_of(opt_inst_info, struct team_option_inst, info);
	opt_inst->changed = true;
}
EXPORT_SYMBOL(team_option_inst_set_change);

void team_options_change_check(struct team *team)
{
	__team_options_change_check(team);
}
EXPORT_SYMBOL(team_options_change_check);


/****************
 * Mode handling
 ****************/

static LIST_HEAD(mode_list);
static DEFINE_SPINLOCK(mode_list_lock);

struct team_mode_item {
	struct list_head list;
	const struct team_mode *mode;
};

static struct team_mode_item *__find_mode(const char *kind)
{
	struct team_mode_item *mitem;

	list_for_each_entry(mitem, &mode_list, list) {
		if (strcmp(mitem->mode->kind, kind) == 0)
			return mitem;
	}
	return NULL;
}

static bool is_good_mode_name(const char *name)
{
	while (*name != '\0') {
		if (!isalpha(*name) && !isdigit(*name) && *name != '_')
			return false;
		name++;
	}
	return true;
}

int team_mode_register(const struct team_mode *mode)
{
	int err = 0;
	struct team_mode_item *mitem;

	if (!is_good_mode_name(mode->kind) ||
	    mode->priv_size > TEAM_MODE_PRIV_SIZE)
		return -EINVAL;

	mitem = kmalloc(sizeof(*mitem), GFP_KERNEL);
	if (!mitem)
		return -ENOMEM;

	spin_lock(&mode_list_lock);
	if (__find_mode(mode->kind)) {
		err = -EEXIST;
		kfree(mitem);
		goto unlock;
	}
	mitem->mode = mode;
	list_add_tail(&mitem->list, &mode_list);
unlock:
	spin_unlock(&mode_list_lock);
	return err;
}
EXPORT_SYMBOL(team_mode_register);

void team_mode_unregister(const struct team_mode *mode)
{
	struct team_mode_item *mitem;

	spin_lock(&mode_list_lock);
	mitem = __find_mode(mode->kind);
	if (mitem) {
		list_del_init(&mitem->list);
		kfree(mitem);
	}
	spin_unlock(&mode_list_lock);
}
EXPORT_SYMBOL(team_mode_unregister);

static const struct team_mode *team_mode_get(const char *kind)
{
	struct team_mode_item *mitem;
	const struct team_mode *mode = NULL;

	spin_lock(&mode_list_lock);
	mitem = __find_mode(kind);
	if (!mitem) {
		spin_unlock(&mode_list_lock);
		request_module("team-mode-%s", kind);
		spin_lock(&mode_list_lock);
		mitem = __find_mode(kind);
	}
	if (mitem) {
		mode = mitem->mode;
		if (!try_module_get(mode->owner))
			mode = NULL;
	}

	spin_unlock(&mode_list_lock);
	return mode;
}

static void team_mode_put(const struct team_mode *mode)
{
	module_put(mode->owner);
}

static bool team_dummy_transmit(struct team *team, struct sk_buff *skb)
{
	dev_kfree_skb_any(skb);
	return false;
}

static rx_handler_result_t team_dummy_receive(struct team *team,
					      struct team_port *port,
					      struct sk_buff *skb)
{
	return RX_HANDLER_ANOTHER;
}

static const struct team_mode __team_no_mode = {
	.kind		= "*NOMODE*",
};

static bool team_is_mode_set(struct team *team)
{
	return team->mode != &__team_no_mode;
}

static void team_set_no_mode(struct team *team)
{
	team->user_carrier_enabled = false;
	team->mode = &__team_no_mode;
}

static void team_adjust_ops(struct team *team)
{
	/*
	 * To avoid checks in rx/tx skb paths, ensure here that non-null and
	 * correct ops are always set.
	 */

	if (!team->en_port_count || !team_is_mode_set(team) ||
	    !team->mode->ops->transmit)
		team->ops.transmit = team_dummy_transmit;
	else
		team->ops.transmit = team->mode->ops->transmit;

	if (!team->en_port_count || !team_is_mode_set(team) ||
	    !team->mode->ops->receive)
		team->ops.receive = team_dummy_receive;
	else
		team->ops.receive = team->mode->ops->receive;
}

/*
 * We can benefit from the fact that it's ensured no port is present
 * at the time of mode change. Therefore no packets are in fly so there's no
 * need to set mode operations in any special way.
 */
static int __team_change_mode(struct team *team,
			      const struct team_mode *new_mode)
{
	/* Check if mode was previously set and do cleanup if so */
	if (team_is_mode_set(team)) {
		void (*exit_op)(struct team *team) = team->ops.exit;

		/* Clear ops area so no callback is called any longer */
		memset(&team->ops, 0, sizeof(struct team_mode_ops));
		team_adjust_ops(team);

		if (exit_op)
			exit_op(team);
		team_mode_put(team->mode);
		team_set_no_mode(team);
		/* zero private data area */
		memset(&team->mode_priv, 0,
		       sizeof(struct team) - offsetof(struct team, mode_priv));
	}

	if (!new_mode)
		return 0;

	if (new_mode->ops->init) {
		int err;

		err = new_mode->ops->init(team);
		if (err)
			return err;
	}

	team->mode = new_mode;
	memcpy(&team->ops, new_mode->ops, sizeof(struct team_mode_ops));
	team_adjust_ops(team);

	return 0;
}

static int team_change_mode(struct team *team, const char *kind)
{
	const struct team_mode *new_mode;
	struct net_device *dev = team->dev;
	int err;

	if (!list_empty(&team->port_list)) {
		netdev_err(dev, "No ports can be present during mode change\n");
		return -EBUSY;
	}

	if (team_is_mode_set(team) && strcmp(team->mode->kind, kind) == 0) {
		netdev_err(dev, "Unable to change to the same mode the team is in\n");
		return -EINVAL;
	}

	new_mode = team_mode_get(kind);
	if (!new_mode) {
		netdev_err(dev, "Mode \"%s\" not found\n", kind);
		return -EINVAL;
	}

	err = __team_change_mode(team, new_mode);
	if (err) {
		netdev_err(dev, "Failed to change to mode \"%s\"\n", kind);
		team_mode_put(new_mode);
		return err;
	}

	netdev_info(dev, "Mode changed to \"%s\"\n", kind);
	return 0;
}


/*********************
 * Peers notification
 *********************/

static void team_notify_peers_work(struct work_struct *work)
{
	struct team *team;

	team = container_of(work, struct team, notify_peers.dw.work);

	if (!rtnl_trylock()) {
		schedule_delayed_work(&team->notify_peers.dw, 0);
		return;
	}
	call_netdevice_notifiers(NETDEV_NOTIFY_PEERS, team->dev);
	rtnl_unlock();
	if (!atomic_dec_and_test(&team->notify_peers.count_pending))
		schedule_delayed_work(&team->notify_peers.dw,
				      msecs_to_jiffies(team->notify_peers.interval));
}

static void team_notify_peers(struct team *team)
{
	if (!team->notify_peers.count || !netif_running(team->dev))
		return;
	atomic_set(&team->notify_peers.count_pending, team->notify_peers.count);
	schedule_delayed_work(&team->notify_peers.dw, 0);
}

static void team_notify_peers_init(struct team *team)
{
	INIT_DELAYED_WORK(&team->notify_peers.dw, team_notify_peers_work);
}

static void team_notify_peers_fini(struct team *team)
{
	cancel_delayed_work_sync(&team->notify_peers.dw);
}


/*******************************
 * Send multicast group rejoins
 *******************************/

static void team_mcast_rejoin_work(struct work_struct *work)
{
	struct team *team;

	team = container_of(work, struct team, mcast_rejoin.dw.work);

	if (!rtnl_trylock()) {
		schedule_delayed_work(&team->mcast_rejoin.dw, 0);
		return;
	}
	call_netdevice_notifiers(NETDEV_RESEND_IGMP, team->dev);
	rtnl_unlock();
	if (!atomic_dec_and_test(&team->mcast_rejoin.count_pending))
		schedule_delayed_work(&team->mcast_rejoin.dw,
				      msecs_to_jiffies(team->mcast_rejoin.interval));
}

static void team_mcast_rejoin(struct team *team)
{
	if (!team->mcast_rejoin.count || !netif_running(team->dev))
		return;
	atomic_set(&team->mcast_rejoin.count_pending, team->mcast_rejoin.count);
	schedule_delayed_work(&team->mcast_rejoin.dw, 0);
}

static void team_mcast_rejoin_init(struct team *team)
{
	INIT_DELAYED_WORK(&team->mcast_rejoin.dw, team_mcast_rejoin_work);
}

static void team_mcast_rejoin_fini(struct team *team)
{
	cancel_delayed_work_sync(&team->mcast_rejoin.dw);
}


/************************
 * Rx path frame handler
 ************************/

/* note: already called with rcu_read_lock */
static rx_handler_result_t team_handle_frame(struct sk_buff **pskb)
{
	struct sk_buff *skb = *pskb;
	struct team_port *port;
	struct team *team;
	rx_handler_result_t res;

	skb = skb_share_check(skb, GFP_ATOMIC);
	if (!skb)
		return RX_HANDLER_CONSUMED;

	*pskb = skb;

	port = team_port_get_rcu(skb->dev);
	team = port->team;
	if (!team_port_enabled(port)) {
		/* allow exact match delivery for disabled ports */
		res = RX_HANDLER_EXACT;
	} else {
		res = team->ops.receive(team, port, skb);
	}
	if (res == RX_HANDLER_ANOTHER) {
		struct team_pcpu_stats *pcpu_stats;

		pcpu_stats = this_cpu_ptr(team->pcpu_stats);
		u64_stats_update_begin(&pcpu_stats->syncp);
		pcpu_stats->rx_packets++;
		pcpu_stats->rx_bytes += skb->len;
		if (skb->pkt_type == PACKET_MULTICAST)
			pcpu_stats->rx_multicast++;
		u64_stats_update_end(&pcpu_stats->syncp);

		skb->dev = team->dev;
	} else {
		this_cpu_inc(team->pcpu_stats->rx_dropped);
	}

	return res;
}


/*************************************
 * Multiqueue Tx port select override
 *************************************/

static int team_queue_override_init(struct team *team)
{
	struct list_head *listarr;
	unsigned int queue_cnt = team->dev->num_tx_queues - 1;
	unsigned int i;

	if (!queue_cnt)
		return 0;
	listarr = kmalloc(sizeof(struct list_head) * queue_cnt, GFP_KERNEL);
	if (!listarr)
		return -ENOMEM;
	team->qom_lists = listarr;
	for (i = 0; i < queue_cnt; i++)
		INIT_LIST_HEAD(listarr++);
	return 0;
}

static void team_queue_override_fini(struct team *team)
{
	kfree(team->qom_lists);
}

static struct list_head *__team_get_qom_list(struct team *team, u16 queue_id)
{
	return &team->qom_lists[queue_id - 1];
}

/*
 * note: already called with rcu_read_lock
 */
static bool team_queue_override_transmit(struct team *team, struct sk_buff *skb)
{
	struct list_head *qom_list;
	struct team_port *port;

	if (!team->queue_override_enabled || !skb->queue_mapping)
		return false;
	qom_list = __team_get_qom_list(team, skb->queue_mapping);
	list_for_each_entry_rcu(port, qom_list, qom_list) {
		if (!team_dev_queue_xmit(team, port, skb))
			return true;
	}
	return false;
}

static void __team_queue_override_port_del(struct team *team,
					   struct team_port *port)
{
	if (!port->queue_id)
		return;
	list_del_rcu(&port->qom_list);
}

static bool team_queue_override_port_has_gt_prio_than(struct team_port *port,
						      struct team_port *cur)
{
	if (port->priority < cur->priority)
		return true;
	if (port->priority > cur->priority)
		return false;
	if (port->index < cur->index)
		return true;
	return false;
}

static void __team_queue_override_port_add(struct team *team,
					   struct team_port *port)
{
	struct team_port *cur;
	struct list_head *qom_list;
	struct list_head *node;

	if (!port->queue_id)
		return;
	qom_list = __team_get_qom_list(team, port->queue_id);
	node = qom_list;
	list_for_each_entry(cur, qom_list, qom_list) {
		if (team_queue_override_port_has_gt_prio_than(port, cur))
			break;
		node = &cur->qom_list;
	}
	list_add_tail_rcu(&port->qom_list, node);
}

static void __team_queue_override_enabled_check(struct team *team)
{
	struct team_port *port;
	bool enabled = false;

	list_for_each_entry(port, &team->port_list, list) {
		if (port->queue_id) {
			enabled = true;
			break;
		}
	}
	if (enabled == team->queue_override_enabled)
		return;
	netdev_dbg(team->dev, "%s queue override\n",
		   enabled ? "Enabling" : "Disabling");
	team->queue_override_enabled = enabled;
}

static void team_queue_override_port_prio_changed(struct team *team,
						  struct team_port *port)
{
	if (!port->queue_id || team_port_enabled(port))
		return;
	__team_queue_override_port_del(team, port);
	__team_queue_override_port_add(team, port);
	__team_queue_override_enabled_check(team);
}

static void team_queue_override_port_change_queue_id(struct team *team,
						     struct team_port *port,
						     u16 new_queue_id)
{
	if (team_port_enabled(port)) {
		__team_queue_override_port_del(team, port);
		port->queue_id = new_queue_id;
		__team_queue_override_port_add(team, port);
		__team_queue_override_enabled_check(team);
	} else {
		port->queue_id = new_queue_id;
	}
}

static void team_queue_override_port_add(struct team *team,
					 struct team_port *port)
{
	__team_queue_override_port_add(team, port);
	__team_queue_override_enabled_check(team);
}

static void team_queue_override_port_del(struct team *team,
					 struct team_port *port)
{
	__team_queue_override_port_del(team, port);
	__team_queue_override_enabled_check(team);
}


/****************
 * Port handling
 ****************/

static bool team_port_find(const struct team *team,
			   const struct team_port *port)
{
	struct team_port *cur;

	list_for_each_entry(cur, &team->port_list, list)
		if (cur == port)
			return true;
	return false;
}

/*
 * Enable/disable port by adding to enabled port hashlist and setting
 * port->index (Might be racy so reader could see incorrect ifindex when
 * processing a flying packet, but that is not a problem). Write guarded
 * by team->lock.
 */
static void team_port_enable(struct team *team,
			     struct team_port *port)
{
	if (team_port_enabled(port))
		return;
	port->index = team->en_port_count++;
	hlist_add_head_rcu(&port->hlist,
			   team_port_index_hash(team, port->index));
	team_adjust_ops(team);
	team_queue_override_port_add(team, port);
	if (team->ops.port_enabled)
		team->ops.port_enabled(team, port);
	team_notify_peers(team);
	team_mcast_rejoin(team);
}

static void __reconstruct_port_hlist(struct team *team, int rm_index)
{
	int i;
	struct team_port *port;

	for (i = rm_index + 1; i < team->en_port_count; i++) {
		port = team_get_port_by_index(team, i);
		hlist_del_rcu(&port->hlist);
		port->index--;
		hlist_add_head_rcu(&port->hlist,
				   team_port_index_hash(team, port->index));
	}
}

static void team_port_disable(struct team *team,
			      struct team_port *port)
{
	if (!team_port_enabled(port))
		return;
	if (team->ops.port_disabled)
		team->ops.port_disabled(team, port);
	hlist_del_rcu(&port->hlist);
	__reconstruct_port_hlist(team, port->index);
	port->index = -1;
	team->en_port_count--;
	team_queue_override_port_del(team, port);
	team_adjust_ops(team);
	team_notify_peers(team);
	team_mcast_rejoin(team);
}

#define TEAM_VLAN_FEATURES (NETIF_F_ALL_CSUM | NETIF_F_SG | \
			    NETIF_F_FRAGLIST | NETIF_F_ALL_TSO | \
			    NETIF_F_HIGHDMA | NETIF_F_LRO)

static void __team_compute_features(struct team *team)
{
	struct team_port *port;
	u32 vlan_features = TEAM_VLAN_FEATURES & NETIF_F_ALL_FOR_ALL;
	unsigned short max_hard_header_len = ETH_HLEN;
	unsigned int dst_release_flag = IFF_XMIT_DST_RELEASE |
					IFF_XMIT_DST_RELEASE_PERM;

	list_for_each_entry(port, &team->port_list, list) {
		vlan_features = netdev_increment_features(vlan_features,
					port->dev->vlan_features,
					TEAM_VLAN_FEATURES);

		dst_release_flag &= port->dev->priv_flags;
		if (port->dev->hard_header_len > max_hard_header_len)
			max_hard_header_len = port->dev->hard_header_len;
	}

	team->dev->vlan_features = vlan_features;
	team->dev->hard_header_len = max_hard_header_len;

	team->dev->priv_flags &= ~IFF_XMIT_DST_RELEASE;
	if (dst_release_flag == (IFF_XMIT_DST_RELEASE | IFF_XMIT_DST_RELEASE_PERM))
		team->dev->priv_flags |= IFF_XMIT_DST_RELEASE;

	netdev_change_features(team->dev);
}

static void team_compute_features(struct team *team)
{
	mutex_lock(&team->lock);
	__team_compute_features(team);
	mutex_unlock(&team->lock);
}

static int team_port_enter(struct team *team, struct team_port *port)
{
	int err = 0;

	dev_hold(team->dev);
	if (team->ops.port_enter) {
		err = team->ops.port_enter(team, port);
		if (err) {
			netdev_err(team->dev, "Device %s failed to enter team mode\n",
				   port->dev->name);
			goto err_port_enter;
		}
	}

	return 0;

err_port_enter:
	dev_put(team->dev);

	return err;
}

static void team_port_leave(struct team *team, struct team_port *port)
{
	if (team->ops.port_leave)
		team->ops.port_leave(team, port);
	dev_put(team->dev);
}

#ifdef CONFIG_NET_POLL_CONTROLLER
static int team_port_enable_netpoll(struct team *team, struct team_port *port)
{
	struct netpoll *np;
	int err;

	if (!team->dev->npinfo)
		return 0;

	np = kzalloc(sizeof(*np), GFP_KERNEL);
	if (!np)
		return -ENOMEM;

	err = __netpoll_setup(np, port->dev);
	if (err) {
		kfree(np);
		return err;
	}
	port->np = np;
	return err;
}

static void team_port_disable_netpoll(struct team_port *port)
{
	struct netpoll *np = port->np;

	if (!np)
		return;
	port->np = NULL;

	/* Wait for transmitting packets to finish before freeing. */
	synchronize_rcu_bh();
	__netpoll_cleanup(np);
	kfree(np);
}
#else
static int team_port_enable_netpoll(struct team *team, struct team_port *port)
{
	return 0;
}
static void team_port_disable_netpoll(struct team_port *port)
{
}
#endif

static int team_upper_dev_link(struct net_device *dev,
			       struct net_device *port_dev)
{
	int err;

	err = netdev_master_upper_dev_link(port_dev, dev);
	if (err)
		return err;
	port_dev->priv_flags |= IFF_TEAM_PORT;
	return 0;
}

static void team_upper_dev_unlink(struct net_device *dev,
				  struct net_device *port_dev)
{
	netdev_upper_dev_unlink(port_dev, dev);
	port_dev->priv_flags &= ~IFF_TEAM_PORT;
}

static void __team_port_change_port_added(struct team_port *port, bool linkup);
static int team_dev_type_check_change(struct net_device *dev,
				      struct net_device *port_dev);

static int team_port_add(struct team *team, struct net_device *port_dev)
{
	struct net_device *dev = team->dev;
	struct team_port *port;
	char *portname = port_dev->name;
	int err;

	if (port_dev->flags & IFF_LOOPBACK) {
		netdev_err(dev, "Device %s is loopback device. Loopback devices can't be added as a team port\n",
			   portname);
		return -EINVAL;
	}

	if (team_port_exists(port_dev)) {
		netdev_err(dev, "Device %s is already a port "
				"of a team device\n", portname);
		return -EBUSY;
	}

	if (port_dev->features & NETIF_F_VLAN_CHALLENGED &&
	    vlan_uses_dev(dev)) {
		netdev_err(dev, "Device %s is VLAN challenged and team device has VLAN set up\n",
			   portname);
		return -EPERM;
	}

	err = team_dev_type_check_change(dev, port_dev);
	if (err)
		return err;

	if (port_dev->flags & IFF_UP) {
		netdev_err(dev, "Device %s is up. Set it down before adding it as a team port\n",
			   portname);
		return -EBUSY;
	}

	port = kzalloc(sizeof(struct team_port) + team->mode->port_priv_size,
		       GFP_KERNEL);
	if (!port)
		return -ENOMEM;

	port->dev = port_dev;
	port->team = team;
	INIT_LIST_HEAD(&port->qom_list);

	port->orig.mtu = port_dev->mtu;
	err = dev_set_mtu(port_dev, dev->mtu);
	if (err) {
		netdev_dbg(dev, "Error %d calling dev_set_mtu\n", err);
		goto err_set_mtu;
	}

	memcpy(port->orig.dev_addr, port_dev->dev_addr, port_dev->addr_len);

	err = team_port_enter(team, port);
	if (err) {
		netdev_err(dev, "Device %s failed to enter team mode\n",
			   portname);
		goto err_port_enter;
	}

	err = dev_open(port_dev);
	if (err) {
		netdev_dbg(dev, "Device %s opening failed\n",
			   portname);
		goto err_dev_open;
	}

	err = vlan_vids_add_by_dev(port_dev, dev);
	if (err) {
		netdev_err(dev, "Failed to add vlan ids to device %s\n",
				portname);
		goto err_vids_add;
	}

	err = team_port_enable_netpoll(team, port);
	if (err) {
		netdev_err(dev, "Failed to enable netpoll on device %s\n",
			   portname);
		goto err_enable_netpoll;
	}

	err = netdev_rx_handler_register(port_dev, team_handle_frame,
					 port);
	if (err) {
		netdev_err(dev, "Device %s failed to register rx_handler\n",
			   portname);
		goto err_handler_register;
	}

	err = team_upper_dev_link(dev, port_dev);
	if (err) {
		netdev_err(dev, "Device %s failed to set upper link\n",
			   portname);
		goto err_set_upper_link;
	}

	err = __team_option_inst_add_port(team, port);
	if (err) {
		netdev_err(dev, "Device %s failed to add per-port options\n",
			   portname);
		goto err_option_port_add;
	}

	port->index = -1;
	list_add_tail_rcu(&port->list, &team->port_list);
	team_port_enable(team, port);
	__team_compute_features(team);
	__team_port_change_port_added(port, !!netif_carrier_ok(port_dev));
	__team_options_change_check(team);

	netdev_info(dev, "Port device %s added\n", portname);

	return 0;

err_option_port_add:
	team_upper_dev_unlink(dev, port_dev);

err_set_upper_link:
	netdev_rx_handler_unregister(port_dev);

err_handler_register:
	team_port_disable_netpoll(port);

err_enable_netpoll:
	vlan_vids_del_by_dev(port_dev, dev);

err_vids_add:
	dev_close(port_dev);

err_dev_open:
	team_port_leave(team, port);
	team_port_set_orig_dev_addr(port);

err_port_enter:
	dev_set_mtu(port_dev, port->orig.mtu);

err_set_mtu:
	kfree(port);

	return err;
}

static void __team_port_change_port_removed(struct team_port *port);

static int team_port_del(struct team *team, struct net_device *port_dev)
{
	struct net_device *dev = team->dev;
	struct team_port *port;
	char *portname = port_dev->name;

	port = team_port_get_rtnl(port_dev);
	if (!port || !team_port_find(team, port)) {
		netdev_err(dev, "Device %s does not act as a port of this team\n",
			   portname);
		return -ENOENT;
	}

	team_port_disable(team, port);
	list_del_rcu(&port->list);
	team_upper_dev_unlink(dev, port_dev);
	netdev_rx_handler_unregister(port_dev);
	team_port_disable_netpoll(port);
	vlan_vids_del_by_dev(port_dev, dev);
	dev_uc_unsync(port_dev, dev);
	dev_mc_unsync(port_dev, dev);
	dev_close(port_dev);
	team_port_leave(team, port);

	__team_option_inst_mark_removed_port(team, port);
	__team_options_change_check(team);
	__team_option_inst_del_port(team, port);
	__team_port_change_port_removed(port);

	team_port_set_orig_dev_addr(port);
	dev_set_mtu(port_dev, port->orig.mtu);
	kfree_rcu(port, rcu);
	netdev_info(dev, "Port device %s removed\n", portname);
	__team_compute_features(team);

	return 0;
}


/*****************
 * Net device ops
 *****************/

static int team_mode_option_get(struct team *team, struct team_gsetter_ctx *ctx)
{
	ctx->data.str_val = team->mode->kind;
	return 0;
}

static int team_mode_option_set(struct team *team, struct team_gsetter_ctx *ctx)
{
	return team_change_mode(team, ctx->data.str_val);
}

static int team_notify_peers_count_get(struct team *team,
				       struct team_gsetter_ctx *ctx)
{
	ctx->data.u32_val = team->notify_peers.count;
	return 0;
}

static int team_notify_peers_count_set(struct team *team,
				       struct team_gsetter_ctx *ctx)
{
	team->notify_peers.count = ctx->data.u32_val;
	return 0;
}

static int team_notify_peers_interval_get(struct team *team,
					  struct team_gsetter_ctx *ctx)
{
	ctx->data.u32_val = team->notify_peers.interval;
	return 0;
}

static int team_notify_peers_interval_set(struct team *team,
					  struct team_gsetter_ctx *ctx)
{
	team->notify_peers.interval = ctx->data.u32_val;
	return 0;
}

static int team_mcast_rejoin_count_get(struct team *team,
				       struct team_gsetter_ctx *ctx)
{
	ctx->data.u32_val = team->mcast_rejoin.count;
	return 0;
}

static int team_mcast_rejoin_count_set(struct team *team,
				       struct team_gsetter_ctx *ctx)
{
	team->mcast_rejoin.count = ctx->data.u32_val;
	return 0;
}

static int team_mcast_rejoin_interval_get(struct team *team,
					  struct team_gsetter_ctx *ctx)
{
	ctx->data.u32_val = team->mcast_rejoin.interval;
	return 0;
}

static int team_mcast_rejoin_interval_set(struct team *team,
					  struct team_gsetter_ctx *ctx)
{
	team->mcast_rejoin.interval = ctx->data.u32_val;
	return 0;
}

static int team_port_en_option_get(struct team *team,
				   struct team_gsetter_ctx *ctx)
{
	struct team_port *port = ctx->info->port;

	ctx->data.bool_val = team_port_enabled(port);
	return 0;
}

static int team_port_en_option_set(struct team *team,
				   struct team_gsetter_ctx *ctx)
{
	struct team_port *port = ctx->info->port;

	if (ctx->data.bool_val)
		team_port_enable(team, port);
	else
		team_port_disable(team, port);
	return 0;
}

static int team_user_linkup_option_get(struct team *team,
				       struct team_gsetter_ctx *ctx)
{
	struct team_port *port = ctx->info->port;

	ctx->data.bool_val = port->user.linkup;
	return 0;
}

static void __team_carrier_check(struct team *team);

static int team_user_linkup_option_set(struct team *team,
				       struct team_gsetter_ctx *ctx)
{
	struct team_port *port = ctx->info->port;

	port->user.linkup = ctx->data.bool_val;
	team_refresh_port_linkup(port);
	__team_carrier_check(port->team);
	return 0;
}

static int team_user_linkup_en_option_get(struct team *team,
					  struct team_gsetter_ctx *ctx)
{
	struct team_port *port = ctx->info->port;

	ctx->data.bool_val = port->user.linkup_enabled;
	return 0;
}

static int team_user_linkup_en_option_set(struct team *team,
					  struct team_gsetter_ctx *ctx)
{
	struct team_port *port = ctx->info->port;

	port->user.linkup_enabled = ctx->data.bool_val;
	team_refresh_port_linkup(port);
	__team_carrier_check(port->team);
	return 0;
}

static int team_priority_option_get(struct team *team,
				    struct team_gsetter_ctx *ctx)
{
	struct team_port *port = ctx->info->port;

	ctx->data.s32_val = port->priority;
	return 0;
}

static int team_priority_option_set(struct team *team,
				    struct team_gsetter_ctx *ctx)
{
	struct team_port *port = ctx->info->port;
	s32 priority = ctx->data.s32_val;

	if (port->priority == priority)
		return 0;
	port->priority = priority;
	team_queue_override_port_prio_changed(team, port);
	return 0;
}

static int team_queue_id_option_get(struct team *team,
				    struct team_gsetter_ctx *ctx)
{
	struct team_port *port = ctx->info->port;

	ctx->data.u32_val = port->queue_id;
	return 0;
}

static int team_queue_id_option_set(struct team *team,
				    struct team_gsetter_ctx *ctx)
{
	struct team_port *port = ctx->info->port;
	u16 new_queue_id = ctx->data.u32_val;

	if (port->queue_id == new_queue_id)
		return 0;
	if (new_queue_id >= team->dev->real_num_tx_queues)
		return -EINVAL;
	team_queue_override_port_change_queue_id(team, port, new_queue_id);
	return 0;
}

static const struct team_option team_options[] = {
	{
		.name = "mode",
		.type = TEAM_OPTION_TYPE_STRING,
		.getter = team_mode_option_get,
		.setter = team_mode_option_set,
	},
	{
		.name = "notify_peers_count",
		.type = TEAM_OPTION_TYPE_U32,
		.getter = team_notify_peers_count_get,
		.setter = team_notify_peers_count_set,
	},
	{
		.name = "notify_peers_interval",
		.type = TEAM_OPTION_TYPE_U32,
		.getter = team_notify_peers_interval_get,
		.setter = team_notify_peers_interval_set,
	},
	{
		.name = "mcast_rejoin_count",
		.type = TEAM_OPTION_TYPE_U32,
		.getter = team_mcast_rejoin_count_get,
		.setter = team_mcast_rejoin_count_set,
	},
	{
		.name = "mcast_rejoin_interval",
		.type = TEAM_OPTION_TYPE_U32,
		.getter = team_mcast_rejoin_interval_get,
		.setter = team_mcast_rejoin_interval_set,
	},
	{
		.name = "enabled",
		.type = TEAM_OPTION_TYPE_BOOL,
		.per_port = true,
		.getter = team_port_en_option_get,
		.setter = team_port_en_option_set,
	},
	{
		.name = "user_linkup",
		.type = TEAM_OPTION_TYPE_BOOL,
		.per_port = true,
		.getter = team_user_linkup_option_get,
		.setter = team_user_linkup_option_set,
	},
	{
		.name = "user_linkup_enabled",
		.type = TEAM_OPTION_TYPE_BOOL,
		.per_port = true,
		.getter = team_user_linkup_en_option_get,
		.setter = team_user_linkup_en_option_set,
	},
	{
		.name = "priority",
		.type = TEAM_OPTION_TYPE_S32,
		.per_port = true,
		.getter = team_priority_option_get,
		.setter = team_priority_option_set,
	},
	{
		.name = "queue_id",
		.type = TEAM_OPTION_TYPE_U32,
		.per_port = true,
		.getter = team_queue_id_option_get,
		.setter = team_queue_id_option_set,
	},
};

static struct lock_class_key team_netdev_xmit_lock_key;
static struct lock_class_key team_netdev_addr_lock_key;
static struct lock_class_key team_tx_busylock_key;

static void team_set_lockdep_class_one(struct net_device *dev,
				       struct netdev_queue *txq,
				       void *unused)
{
	lockdep_set_class(&txq->_xmit_lock, &team_netdev_xmit_lock_key);
}

static void team_set_lockdep_class(struct net_device *dev)
{
	lockdep_set_class(&dev->addr_list_lock, &team_netdev_addr_lock_key);
	netdev_for_each_tx_queue(dev, team_set_lockdep_class_one, NULL);
	dev->qdisc_tx_busylock = &team_tx_busylock_key;
}

static int team_init(struct net_device *dev)
{
	struct team *team = netdev_priv(dev);
	int i;
	int err;

	team->dev = dev;
	mutex_init(&team->lock);
	team_set_no_mode(team);

	team->pcpu_stats = netdev_alloc_pcpu_stats(struct team_pcpu_stats);
	if (!team->pcpu_stats)
		return -ENOMEM;

	for (i = 0; i < TEAM_PORT_HASHENTRIES; i++)
		INIT_HLIST_HEAD(&team->en_port_hlist[i]);
	INIT_LIST_HEAD(&team->port_list);
	err = team_queue_override_init(team);
	if (err)
		goto err_team_queue_override_init;

	team_adjust_ops(team);

	INIT_LIST_HEAD(&team->option_list);
	INIT_LIST_HEAD(&team->option_inst_list);

	team_notify_peers_init(team);
	team_mcast_rejoin_init(team);

	err = team_options_register(team, team_options, ARRAY_SIZE(team_options));
	if (err)
		goto err_options_register;
	netif_carrier_off(dev);

	team_set_lockdep_class(dev);

	return 0;

err_options_register:
	team_mcast_rejoin_fini(team);
	team_notify_peers_fini(team);
	team_queue_override_fini(team);
err_team_queue_override_init:
	free_percpu(team->pcpu_stats);

	return err;
}

static void team_uninit(struct net_device *dev)
{
	struct team *team = netdev_priv(dev);
	struct team_port *port;
	struct team_port *tmp;

	mutex_lock(&team->lock);
	list_for_each_entry_safe(port, tmp, &team->port_list, list)
		team_port_del(team, port->dev);

	__team_change_mode(team, NULL); /* cleanup */
	__team_options_unregister(team, team_options, ARRAY_SIZE(team_options));
	team_mcast_rejoin_fini(team);
	team_notify_peers_fini(team);
	team_queue_override_fini(team);
	mutex_unlock(&team->lock);
}

static void team_destructor(struct net_device *dev)
{
	struct team *team = netdev_priv(dev);

	free_percpu(team->pcpu_stats);
	free_netdev(dev);
}

static int team_open(struct net_device *dev)
{
	return 0;
}

static int team_close(struct net_device *dev)
{
	return 0;
}

/*
 * note: already called with rcu_read_lock
 */
static netdev_tx_t team_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct team *team = netdev_priv(dev);
	bool tx_success;
	unsigned int len = skb->len;

	tx_success = team_queue_override_transmit(team, skb);
	if (!tx_success)
		tx_success = team->ops.transmit(team, skb);
	if (tx_success) {
		struct team_pcpu_stats *pcpu_stats;

		pcpu_stats = this_cpu_ptr(team->pcpu_stats);
		u64_stats_update_begin(&pcpu_stats->syncp);
		pcpu_stats->tx_packets++;
		pcpu_stats->tx_bytes += len;
		u64_stats_update_end(&pcpu_stats->syncp);
	} else {
		this_cpu_inc(team->pcpu_stats->tx_dropped);
	}

	return NETDEV_TX_OK;
}

static u16 team_select_queue(struct net_device *dev, struct sk_buff *skb,
			     void *accel_priv, select_queue_fallback_t fallback)
{
	/*
	 * This helper function exists to help dev_pick_tx get the correct
	 * destination queue.  Using a helper function skips a call to
	 * skb_tx_hash and will put the skbs in the queue we expect on their
	 * way down to the team driver.
	 */
	u16 txq = skb_rx_queue_recorded(skb) ? skb_get_rx_queue(skb) : 0;

	/*
	 * Save the original txq to restore before passing to the driver
	 */
	qdisc_skb_cb(skb)->slave_dev_queue_mapping = skb->queue_mapping;

	if (unlikely(txq >= dev->real_num_tx_queues)) {
		do {
			txq -= dev->real_num_tx_queues;
		} while (txq >= dev->real_num_tx_queues);
	}
	return txq;
}

static void team_change_rx_flags(struct net_device *dev, int change)
{
	struct team *team = netdev_priv(dev);
	struct team_port *port;
	int inc;

	rcu_read_lock();
	list_for_each_entry_rcu(port, &team->port_list, list) {
		if (change & IFF_PROMISC) {
			inc = dev->flags & IFF_PROMISC ? 1 : -1;
			dev_set_promiscuity(port->dev, inc);
		}
		if (change & IFF_ALLMULTI) {
			inc = dev->flags & IFF_ALLMULTI ? 1 : -1;
			dev_set_allmulti(port->dev, inc);
		}
	}
	rcu_read_unlock();
}

static void team_set_rx_mode(struct net_device *dev)
{
	struct team *team = netdev_priv(dev);
	struct team_port *port;

	rcu_read_lock();
	list_for_each_entry_rcu(port, &team->port_list, list) {
		dev_uc_sync_multiple(port->dev, dev);
		dev_mc_sync_multiple(port->dev, dev);
	}
	rcu_read_unlock();
}

static int team_set_mac_address(struct net_device *dev, void *p)
{
	struct sockaddr *addr = p;
	struct team *team = netdev_priv(dev);
	struct team_port *port;

	if (dev->type == ARPHRD_ETHER && !is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;
	memcpy(dev->dev_addr, addr->sa_data, dev->addr_len);
	rcu_read_lock();
	list_for_each_entry_rcu(port, &team->port_list, list)
		if (team->ops.port_change_dev_addr)
			team->ops.port_change_dev_addr(team, port);
	rcu_read_unlock();
	return 0;
}

static int team_change_mtu(struct net_device *dev, int new_mtu)
{
	struct team *team = netdev_priv(dev);
	struct team_port *port;
	int err;

	/*
	 * Alhough this is reader, it's guarded by team lock. It's not possible
	 * to traverse list in reverse under rcu_read_lock
	 */
	mutex_lock(&team->lock);
	team->port_mtu_change_allowed = true;
	list_for_each_entry(port, &team->port_list, list) {
		err = dev_set_mtu(port->dev, new_mtu);
		if (err) {
			netdev_err(dev, "Device %s failed to change mtu",
				   port->dev->name);
			goto unwind;
		}
	}
	team->port_mtu_change_allowed = false;
	mutex_unlock(&team->lock);

	dev->mtu = new_mtu;

	return 0;

unwind:
	list_for_each_entry_continue_reverse(port, &team->port_list, list)
		dev_set_mtu(port->dev, dev->mtu);
	team->port_mtu_change_allowed = false;
	mutex_unlock(&team->lock);

	return err;
}

static struct rtnl_link_stats64 *
team_get_stats64(struct net_device *dev, struct rtnl_link_stats64 *stats)
{
	struct team *team = netdev_priv(dev);
	struct team_pcpu_stats *p;
	u64 rx_packets, rx_bytes, rx_multicast, tx_packets, tx_bytes;
	u32 rx_dropped = 0, tx_dropped = 0;
	unsigned int start;
	int i;

	for_each_possible_cpu(i) {
		p = per_cpu_ptr(team->pcpu_stats, i);
		do {
			start = u64_stats_fetch_begin_irq(&p->syncp);
			rx_packets	= p->rx_packets;
			rx_bytes	= p->rx_bytes;
			rx_multicast	= p->rx_multicast;
			tx_packets	= p->tx_packets;
			tx_bytes	= p->tx_bytes;
		} while (u64_stats_fetch_retry_irq(&p->syncp, start));

		stats->rx_packets	+= rx_packets;
		stats->rx_bytes		+= rx_bytes;
		stats->multicast	+= rx_multicast;
		stats->tx_packets	+= tx_packets;
		stats->tx_bytes		+= tx_bytes;
		/*
		 * rx_dropped & tx_dropped are u32, updated
		 * without syncp protection.
		 */
		rx_dropped	+= p->rx_dropped;
		tx_dropped	+= p->tx_dropped;
	}
	stats->rx_dropped	= rx_dropped;
	stats->tx_dropped	= tx_dropped;
	return stats;
}

static int team_vlan_rx_add_vid(struct net_device *dev, __be16 proto, u16 vid)
{
	struct team *team = netdev_priv(dev);
	struct team_port *port;
	int err;

	/*
	 * Alhough this is reader, it's guarded by team lock. It's not possible
	 * to traverse list in reverse under rcu_read_lock
	 */
	mutex_lock(&team->lock);
	list_for_each_entry(port, &team->port_list, list) {
		err = vlan_vid_add(port->dev, proto, vid);
		if (err)
			goto unwind;
	}
	mutex_unlock(&team->lock);

	return 0;

unwind:
	list_for_each_entry_continue_reverse(port, &team->port_list, list)
		vlan_vid_del(port->dev, proto, vid);
	mutex_unlock(&team->lock);

	return err;
}

static int team_vlan_rx_kill_vid(struct net_device *dev, __be16 proto, u16 vid)
{
	struct team *team = netdev_priv(dev);
	struct team_port *port;

	rcu_read_lock();
	list_for_each_entry_rcu(port, &team->port_list, list)
		vlan_vid_del(port->dev, proto, vid);
	rcu_read_unlock();

	return 0;
}

#ifdef CONFIG_NET_POLL_CONTROLLER
static void team_poll_controller(struct net_device *dev)
{
}

static void __team_netpoll_cleanup(struct team *team)
{
	struct team_port *port;

	list_for_each_entry(port, &team->port_list, list)
		team_port_disable_netpoll(port);
}

static void team_netpoll_cleanup(struct net_device *dev)
{
	struct team *team = netdev_priv(dev);

	mutex_lock(&team->lock);
	__team_netpoll_cleanup(team);
	mutex_unlock(&team->lock);
}

static int team_netpoll_setup(struct net_device *dev,
			      struct netpoll_info *npifo)
{
	struct team *team = netdev_priv(dev);
	struct team_port *port;
	int err = 0;

	mutex_lock(&team->lock);
	list_for_each_entry(port, &team->port_list, list) {
		err = team_port_enable_netpoll(team, port);
		if (err) {
			__team_netpoll_cleanup(team);
			break;
		}
	}
	mutex_unlock(&team->lock);
	return err;
}
#endif

static int team_add_slave(struct net_device *dev, struct net_device *port_dev)
{
	struct team *team = netdev_priv(dev);
	int err;

	mutex_lock(&team->lock);
	err = team_port_add(team, port_dev);
	mutex_unlock(&team->lock);
	return err;
}

static int team_del_slave(struct net_device *dev, struct net_device *port_dev)
{
	struct team *team = netdev_priv(dev);
	int err;

	mutex_lock(&team->lock);
	err = team_port_del(team, port_dev);
	mutex_unlock(&team->lock);
	return err;
}

static netdev_features_t team_fix_features(struct net_device *dev,
					   netdev_features_t features)
{
	struct team_port *port;
	struct team *team = netdev_priv(dev);
	netdev_features_t mask;

	mask = features;
	features &= ~NETIF_F_ONE_FOR_ALL;
	features |= NETIF_F_ALL_FOR_ALL;

	rcu_read_lock();
	list_for_each_entry_rcu(port, &team->port_list, list) {
		features = netdev_increment_features(features,
						     port->dev->features,
						     mask);
	}
	rcu_read_unlock();
	return features;
}

static int team_change_carrier(struct net_device *dev, bool new_carrier)
{
	struct team *team = netdev_priv(dev);

	team->user_carrier_enabled = true;

	if (new_carrier)
		netif_carrier_on(dev);
	else
		netif_carrier_off(dev);
	return 0;
}

static const struct net_device_ops team_netdev_ops = {
	.ndo_init		= team_init,
	.ndo_uninit		= team_uninit,
	.ndo_open		= team_open,
	.ndo_stop		= team_close,
	.ndo_start_xmit		= team_xmit,
	.ndo_select_queue	= team_select_queue,
	.ndo_change_rx_flags	= team_change_rx_flags,
	.ndo_set_rx_mode	= team_set_rx_mode,
	.ndo_set_mac_address	= team_set_mac_address,
	.ndo_change_mtu		= team_change_mtu,
	.ndo_get_stats64	= team_get_stats64,
	.ndo_vlan_rx_add_vid	= team_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid	= team_vlan_rx_kill_vid,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller	= team_poll_controller,
	.ndo_netpoll_setup	= team_netpoll_setup,
	.ndo_netpoll_cleanup	= team_netpoll_cleanup,
#endif
	.ndo_add_slave		= team_add_slave,
	.ndo_del_slave		= team_del_slave,
	.ndo_fix_features	= team_fix_features,
	.ndo_change_carrier     = team_change_carrier,
};

/***********************
 * ethtool interface
 ***********************/

static void team_ethtool_get_drvinfo(struct net_device *dev,
				     struct ethtool_drvinfo *drvinfo)
{
	strlcpy(drvinfo->driver, DRV_NAME, sizeof(drvinfo->driver));
	strlcpy(drvinfo->version, UTS_RELEASE, sizeof(drvinfo->version));
}

static const struct ethtool_ops team_ethtool_ops = {
	.get_drvinfo		= team_ethtool_get_drvinfo,
	.get_link		= ethtool_op_get_link,
};

/***********************
 * rt netlink interface
 ***********************/

static void team_setup_by_port(struct net_device *dev,
			       struct net_device *port_dev)
{
	dev->header_ops	= port_dev->header_ops;
	dev->type = port_dev->type;
	dev->hard_header_len = port_dev->hard_header_len;
	dev->addr_len = port_dev->addr_len;
	dev->mtu = port_dev->mtu;
	memcpy(dev->broadcast, port_dev->broadcast, port_dev->addr_len);
	eth_hw_addr_inherit(dev, port_dev);
}

static int team_dev_type_check_change(struct net_device *dev,
				      struct net_device *port_dev)
{
	struct team *team = netdev_priv(dev);
	char *portname = port_dev->name;
	int err;

	if (dev->type == port_dev->type)
		return 0;
	if (!list_empty(&team->port_list)) {
		netdev_err(dev, "Device %s is of different type\n", portname);
		return -EBUSY;
	}
	err = call_netdevice_notifiers(NETDEV_PRE_TYPE_CHANGE, dev);
	err = notifier_to_errno(err);
	if (err) {
		netdev_err(dev, "Refused to change device type\n");
		return err;
	}
	dev_uc_flush(dev);
	dev_mc_flush(dev);
	team_setup_by_port(dev, port_dev);
	call_netdevice_notifiers(NETDEV_POST_TYPE_CHANGE, dev);
	return 0;
}

static void team_setup(struct net_device *dev)
{
	ether_setup(dev);

	dev->netdev_ops = &team_netdev_ops;
	dev->ethtool_ops = &team_ethtool_ops;
	dev->destructor	= team_destructor;
	dev->tx_queue_len = 0;
	dev->flags |= IFF_MULTICAST;
	dev->priv_flags &= ~(IFF_XMIT_DST_RELEASE | IFF_TX_SKB_SHARING);

	/*
	 * Indicate we support unicast address filtering. That way core won't
	 * bring us to promisc mode in case a unicast addr is added.
	 * Let this up to underlay drivers.
	 */
	dev->priv_flags |= IFF_UNICAST_FLT | IFF_LIVE_ADDR_CHANGE;

	dev->features |= NETIF_F_LLTX;
	dev->features |= NETIF_F_GRO;

	/* Don't allow team devices to change network namespaces. */
	dev->features |= NETIF_F_NETNS_LOCAL;

	dev->hw_features = TEAM_VLAN_FEATURES |
			   NETIF_F_HW_VLAN_CTAG_TX |
			   NETIF_F_HW_VLAN_CTAG_RX |
			   NETIF_F_HW_VLAN_CTAG_FILTER;

	dev->hw_features &= ~(NETIF_F_ALL_CSUM & ~NETIF_F_HW_CSUM);
	dev->features |= dev->hw_features;
}

static int team_newlink(struct net *src_net, struct net_device *dev,
			struct nlattr *tb[], struct nlattr *data[])
{
	if (tb[IFLA_ADDRESS] == NULL)
		eth_hw_addr_random(dev);

	return register_netdevice(dev);
}

static int team_validate(struct nlattr *tb[], struct nlattr *data[])
{
	if (tb[IFLA_ADDRESS]) {
		if (nla_len(tb[IFLA_ADDRESS]) != ETH_ALEN)
			return -EINVAL;
		if (!is_valid_ether_addr(nla_data(tb[IFLA_ADDRESS])))
			return -EADDRNOTAVAIL;
	}
	return 0;
}

static unsigned int team_get_num_tx_queues(void)
{
	return TEAM_DEFAULT_NUM_TX_QUEUES;
}

static unsigned int team_get_num_rx_queues(void)
{
	return TEAM_DEFAULT_NUM_RX_QUEUES;
}

static struct rtnl_link_ops team_link_ops __read_mostly = {
	.kind			= DRV_NAME,
	.priv_size		= sizeof(struct team),
	.setup			= team_setup,
	.newlink		= team_newlink,
	.validate		= team_validate,
	.get_num_tx_queues	= team_get_num_tx_queues,
	.get_num_rx_queues	= team_get_num_rx_queues,
};


/***********************************
 * Generic netlink custom interface
 ***********************************/

static struct genl_family team_nl_family = {
	.id		= GENL_ID_GENERATE,
	.name		= TEAM_GENL_NAME,
	.version	= TEAM_GENL_VERSION,
	.maxattr	= TEAM_ATTR_MAX,
	.netnsok	= true,
};

static const struct nla_policy team_nl_policy[TEAM_ATTR_MAX + 1] = {
	[TEAM_ATTR_UNSPEC]			= { .type = NLA_UNSPEC, },
	[TEAM_ATTR_TEAM_IFINDEX]		= { .type = NLA_U32 },
	[TEAM_ATTR_LIST_OPTION]			= { .type = NLA_NESTED },
	[TEAM_ATTR_LIST_PORT]			= { .type = NLA_NESTED },
};

static const struct nla_policy
team_nl_option_policy[TEAM_ATTR_OPTION_MAX + 1] = {
	[TEAM_ATTR_OPTION_UNSPEC]		= { .type = NLA_UNSPEC, },
	[TEAM_ATTR_OPTION_NAME] = {
		.type = NLA_STRING,
		.len = TEAM_STRING_MAX_LEN,
	},
	[TEAM_ATTR_OPTION_CHANGED]		= { .type = NLA_FLAG },
	[TEAM_ATTR_OPTION_TYPE]			= { .type = NLA_U8 },
	[TEAM_ATTR_OPTION_DATA]			= { .type = NLA_BINARY },
};

static int team_nl_cmd_noop(struct sk_buff *skb, struct genl_info *info)
{
	struct sk_buff *msg;
	void *hdr;
	int err;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	hdr = genlmsg_put(msg, info->snd_portid, info->snd_seq,
			  &team_nl_family, 0, TEAM_CMD_NOOP);
	if (!hdr) {
		err = -EMSGSIZE;
		goto err_msg_put;
	}

	genlmsg_end(msg, hdr);

	return genlmsg_unicast(genl_info_net(info), msg, info->snd_portid);

err_msg_put:
	nlmsg_free(msg);

	return err;
}

/*
 * Netlink cmd functions should be locked by following two functions.
 * Since dev gets held here, that ensures dev won't disappear in between.
 */
static struct team *team_nl_team_get(struct genl_info *info)
{
	struct net *net = genl_info_net(info);
	int ifindex;
	struct net_device *dev;
	struct team *team;

	if (!info->attrs[TEAM_ATTR_TEAM_IFINDEX])
		return NULL;

	ifindex = nla_get_u32(info->attrs[TEAM_ATTR_TEAM_IFINDEX]);
	dev = dev_get_by_index(net, ifindex);
	if (!dev || dev->netdev_ops != &team_netdev_ops) {
		if (dev)
			dev_put(dev);
		return NULL;
	}

	team = netdev_priv(dev);
	mutex_lock(&team->lock);
	return team;
}

static void team_nl_team_put(struct team *team)
{
	mutex_unlock(&team->lock);
	dev_put(team->dev);
}

typedef int team_nl_send_func_t(struct sk_buff *skb,
				struct team *team, u32 portid);

static int team_nl_send_unicast(struct sk_buff *skb, struct team *team, u32 portid)
{
	return genlmsg_unicast(dev_net(team->dev), skb, portid);
}

static int team_nl_fill_one_option_get(struct sk_buff *skb, struct team *team,
				       struct team_option_inst *opt_inst)
{
	struct nlattr *option_item;
	struct team_option *option = opt_inst->option;
	struct team_option_inst_info *opt_inst_info = &opt_inst->info;
	struct team_gsetter_ctx ctx;
	int err;

	ctx.info = opt_inst_info;
	err = team_option_get(team, opt_inst, &ctx);
	if (err)
		return err;

	option_item = nla_nest_start(skb, TEAM_ATTR_ITEM_OPTION);
	if (!option_item)
		return -EMSGSIZE;

	if (nla_put_string(skb, TEAM_ATTR_OPTION_NAME, option->name))
		goto nest_cancel;
	if (opt_inst_info->port &&
	    nla_put_u32(skb, TEAM_ATTR_OPTION_PORT_IFINDEX,
			opt_inst_info->port->dev->ifindex))
		goto nest_cancel;
	if (opt_inst->option->array_size &&
	    nla_put_u32(skb, TEAM_ATTR_OPTION_ARRAY_INDEX,
			opt_inst_info->array_index))
		goto nest_cancel;

	switch (option->type) {
	case TEAM_OPTION_TYPE_U32:
		if (nla_put_u8(skb, TEAM_ATTR_OPTION_TYPE, NLA_U32))
			goto nest_cancel;
		if (nla_put_u32(skb, TEAM_ATTR_OPTION_DATA, ctx.data.u32_val))
			goto nest_cancel;
		break;
	case TEAM_OPTION_TYPE_STRING:
		if (nla_put_u8(skb, TEAM_ATTR_OPTION_TYPE, NLA_STRING))
			goto nest_cancel;
		if (nla_put_string(skb, TEAM_ATTR_OPTION_DATA,
				   ctx.data.str_val))
			goto nest_cancel;
		break;
	case TEAM_OPTION_TYPE_BINARY:
		if (nla_put_u8(skb, TEAM_ATTR_OPTION_TYPE, NLA_BINARY))
			goto nest_cancel;
		if (nla_put(skb, TEAM_ATTR_OPTION_DATA, ctx.data.bin_val.len,
			    ctx.data.bin_val.ptr))
			goto nest_cancel;
		break;
	case TEAM_OPTION_TYPE_BOOL:
		if (nla_put_u8(skb, TEAM_ATTR_OPTION_TYPE, NLA_FLAG))
			goto nest_cancel;
		if (ctx.data.bool_val &&
		    nla_put_flag(skb, TEAM_ATTR_OPTION_DATA))
			goto nest_cancel;
		break;
	case TEAM_OPTION_TYPE_S32:
		if (nla_put_u8(skb, TEAM_ATTR_OPTION_TYPE, NLA_S32))
			goto nest_cancel;
		if (nla_put_s32(skb, TEAM_ATTR_OPTION_DATA, ctx.data.s32_val))
			goto nest_cancel;
		break;
	default:
		BUG();
	}
	if (opt_inst->removed && nla_put_flag(skb, TEAM_ATTR_OPTION_REMOVED))
		goto nest_cancel;
	if (opt_inst->changed) {
		if (nla_put_flag(skb, TEAM_ATTR_OPTION_CHANGED))
			goto nest_cancel;
		opt_inst->changed = false;
	}
	nla_nest_end(skb, option_item);
	return 0;

nest_cancel:
	nla_nest_cancel(skb, option_item);
	return -EMSGSIZE;
}

static int __send_and_alloc_skb(struct sk_buff **pskb,
				struct team *team, u32 portid,
				team_nl_send_func_t *send_func)
{
	int err;

	if (*pskb) {
		err = send_func(*pskb, team, portid);
		if (err)
			return err;
	}
	*pskb = genlmsg_new(GENLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!*pskb)
		return -ENOMEM;
	return 0;
}

static int team_nl_send_options_get(struct team *team, u32 portid, u32 seq,
				    int flags, team_nl_send_func_t *send_func,
				    struct list_head *sel_opt_inst_list)
{
	struct nlattr *option_list;
	struct nlmsghdr *nlh;
	void *hdr;
	struct team_option_inst *opt_inst;
	int err;
	struct sk_buff *skb = NULL;
	bool incomplete;
	int i;

	opt_inst = list_first_entry(sel_opt_inst_list,
				    struct team_option_inst, tmp_list);

start_again:
	err = __send_and_alloc_skb(&skb, team, portid, send_func);
	if (err)
		return err;

	hdr = genlmsg_put(skb, portid, seq, &team_nl_family, flags | NLM_F_MULTI,
			  TEAM_CMD_OPTIONS_GET);
	if (!hdr)
		return -EMSGSIZE;

	if (nla_put_u32(skb, TEAM_ATTR_TEAM_IFINDEX, team->dev->ifindex))
		goto nla_put_failure;
	option_list = nla_nest_start(skb, TEAM_ATTR_LIST_OPTION);
	if (!option_list)
		goto nla_put_failure;

	i = 0;
	incomplete = false;
	list_for_each_entry_from(opt_inst, sel_opt_inst_list, tmp_list) {
		err = team_nl_fill_one_option_get(skb, team, opt_inst);
		if (err) {
			if (err == -EMSGSIZE) {
				if (!i)
					goto errout;
				incomplete = true;
				break;
			}
			goto errout;
		}
		i++;
	}

	nla_nest_end(skb, option_list);
	genlmsg_end(skb, hdr);
	if (incomplete)
		goto start_again;

send_done:
	nlh = nlmsg_put(skb, portid, seq, NLMSG_DONE, 0, flags | NLM_F_MULTI);
	if (!nlh) {
		err = __send_and_alloc_skb(&skb, team, portid, send_func);
		if (err)
			goto errout;
		goto send_done;
	}

	return send_func(skb, team, portid);

nla_put_failure:
	err = -EMSGSIZE;
errout:
	genlmsg_cancel(skb, hdr);
	nlmsg_free(skb);
	return err;
}

static int team_nl_cmd_options_get(struct sk_buff *skb, struct genl_info *info)
{
	struct team *team;
	struct team_option_inst *opt_inst;
	int err;
	LIST_HEAD(sel_opt_inst_list);

	team = team_nl_team_get(info);
	if (!team)
		return -EINVAL;

	list_for_each_entry(opt_inst, &team->option_inst_list, list)
		list_add_tail(&opt_inst->tmp_list, &sel_opt_inst_list);
	err = team_nl_send_options_get(team, info->snd_portid, info->snd_seq,
				       NLM_F_ACK, team_nl_send_unicast,
				       &sel_opt_inst_list);

	team_nl_team_put(team);

	return err;
}

static int team_nl_send_event_options_get(struct team *team,
					  struct list_head *sel_opt_inst_list);

static int team_nl_cmd_options_set(struct sk_buff *skb, struct genl_info *info)
{
	struct team *team;
	int err = 0;
	int i;
	struct nlattr *nl_option;
	LIST_HEAD(opt_inst_list);

	team = team_nl_team_get(info);
	if (!team)
		return -EINVAL;

	err = -EINVAL;
	if (!info->attrs[TEAM_ATTR_LIST_OPTION]) {
		err = -EINVAL;
		goto team_put;
	}

	nla_for_each_nested(nl_option, info->attrs[TEAM_ATTR_LIST_OPTION], i) {
		struct nlattr *opt_attrs[TEAM_ATTR_OPTION_MAX + 1];
		struct nlattr *attr;
		struct nlattr *attr_data;
		enum team_option_type opt_type;
		int opt_port_ifindex = 0; /* != 0 for per-port options */
		u32 opt_array_index = 0;
		bool opt_is_array = false;
		struct team_option_inst *opt_inst;
		char *opt_name;
		bool opt_found = false;

		if (nla_type(nl_option) != TEAM_ATTR_ITEM_OPTION) {
			err = -EINVAL;
			goto team_put;
		}
		err = nla_parse_nested(opt_attrs, TEAM_ATTR_OPTION_MAX,
				       nl_option, team_nl_option_policy);
		if (err)
			goto team_put;
		if (!opt_attrs[TEAM_ATTR_OPTION_NAME] ||
		    !opt_attrs[TEAM_ATTR_OPTION_TYPE]) {
			err = -EINVAL;
			goto team_put;
		}
		switch (nla_get_u8(opt_attrs[TEAM_ATTR_OPTION_TYPE])) {
		case NLA_U32:
			opt_type = TEAM_OPTION_TYPE_U32;
			break;
		case NLA_STRING:
			opt_type = TEAM_OPTION_TYPE_STRING;
			break;
		case NLA_BINARY:
			opt_type = TEAM_OPTION_TYPE_BINARY;
			break;
		case NLA_FLAG:
			opt_type = TEAM_OPTION_TYPE_BOOL;
			break;
		case NLA_S32:
			opt_type = TEAM_OPTION_TYPE_S32;
			break;
		default:
			goto team_put;
		}

		attr_data = opt_attrs[TEAM_ATTR_OPTION_DATA];
		if (opt_type != TEAM_OPTION_TYPE_BOOL && !attr_data) {
			err = -EINVAL;
			goto team_put;
		}

		opt_name = nla_data(opt_attrs[TEAM_ATTR_OPTION_NAME]);
		attr = opt_attrs[TEAM_ATTR_OPTION_PORT_IFINDEX];
		if (attr)
			opt_port_ifindex = nla_get_u32(attr);

		attr = opt_attrs[TEAM_ATTR_OPTION_ARRAY_INDEX];
		if (attr) {
			opt_is_array = true;
			opt_array_index = nla_get_u32(attr);
		}

		list_for_each_entry(opt_inst, &team->option_inst_list, list) {
			struct team_option *option = opt_inst->option;
			struct team_gsetter_ctx ctx;
			struct team_option_inst_info *opt_inst_info;
			int tmp_ifindex;

			opt_inst_info = &opt_inst->info;
			tmp_ifindex = opt_inst_info->port ?
				      opt_inst_info->port->dev->ifindex : 0;
			if (option->type != opt_type ||
			    strcmp(option->name, opt_name) ||
			    tmp_ifindex != opt_port_ifindex ||
			    (option->array_size && !opt_is_array) ||
			    opt_inst_info->array_index != opt_array_index)
				continue;
			opt_found = true;
			ctx.info = opt_inst_info;
			switch (opt_type) {
			case TEAM_OPTION_TYPE_U32:
				ctx.data.u32_val = nla_get_u32(attr_data);
				break;
			case TEAM_OPTION_TYPE_STRING:
				if (nla_len(attr_data) > TEAM_STRING_MAX_LEN) {
					err = -EINVAL;
					goto team_put;
				}
				ctx.data.str_val = nla_data(attr_data);
				break;
			case TEAM_OPTION_TYPE_BINARY:
				ctx.data.bin_val.len = nla_len(attr_data);
				ctx.data.bin_val.ptr = nla_data(attr_data);
				break;
			case TEAM_OPTION_TYPE_BOOL:
				ctx.data.bool_val = attr_data ? true : false;
				break;
			case TEAM_OPTION_TYPE_S32:
				ctx.data.s32_val = nla_get_s32(attr_data);
				break;
			default:
				BUG();
			}
			err = team_option_set(team, opt_inst, &ctx);
			if (err)
				goto team_put;
			opt_inst->changed = true;
			list_add(&opt_inst->tmp_list, &opt_inst_list);
		}
		if (!opt_found) {
			err = -ENOENT;
			goto team_put;
		}
	}

	err = team_nl_send_event_options_get(team, &opt_inst_list);

team_put:
	team_nl_team_put(team);

	return err;
}

static int team_nl_fill_one_port_get(struct sk_buff *skb,
				     struct team_port *port)
{
	struct nlattr *port_item;

	port_item = nla_nest_start(skb, TEAM_ATTR_ITEM_PORT);
	if (!port_item)
		goto nest_cancel;
	if (nla_put_u32(skb, TEAM_ATTR_PORT_IFINDEX, port->dev->ifindex))
		goto nest_cancel;
	if (port->changed) {
		if (nla_put_flag(skb, TEAM_ATTR_PORT_CHANGED))
			goto nest_cancel;
		port->changed = false;
	}
	if ((port->removed &&
	     nla_put_flag(skb, TEAM_ATTR_PORT_REMOVED)) ||
	    (port->state.linkup &&
	     nla_put_flag(skb, TEAM_ATTR_PORT_LINKUP)) ||
	    nla_put_u32(skb, TEAM_ATTR_PORT_SPEED, port->state.speed) ||
	    nla_put_u8(skb, TEAM_ATTR_PORT_DUPLEX, port->state.duplex))
		goto nest_cancel;
	nla_nest_end(skb, port_item);
	return 0;

nest_cancel:
	nla_nest_cancel(skb, port_item);
	return -EMSGSIZE;
}

static int team_nl_send_port_list_get(struct team *team, u32 portid, u32 seq,
				      int flags, team_nl_send_func_t *send_func,
				      struct team_port *one_port)
{
	struct nlattr *port_list;
	struct nlmsghdr *nlh;
	void *hdr;
	struct team_port *port;
	int err;
	struct sk_buff *skb = NULL;
	bool incomplete;
	int i;

	port = list_first_entry_or_null(&team->port_list,
					struct team_port, list);

start_again:
	err = __send_and_alloc_skb(&skb, team, portid, send_func);
	if (err)
		return err;

	hdr = genlmsg_put(skb, portid, seq, &team_nl_family, flags | NLM_F_MULTI,
			  TEAM_CMD_PORT_LIST_GET);
	if (!hdr)
		return -EMSGSIZE;

	if (nla_put_u32(skb, TEAM_ATTR_TEAM_IFINDEX, team->dev->ifindex))
		goto nla_put_failure;
	port_list = nla_nest_start(skb, TEAM_ATTR_LIST_PORT);
	if (!port_list)
		goto nla_put_failure;

	i = 0;
	incomplete = false;

	/* If one port is selected, called wants to send port list containing
	 * only this port. Otherwise go through all listed ports and send all
	 */
	if (one_port) {
		err = team_nl_fill_one_port_get(skb, one_port);
		if (err)
			goto errout;
	} else if (port) {
		list_for_each_entry_from(port, &team->port_list, list) {
			err = team_nl_fill_one_port_get(skb, port);
			if (err) {
				if (err == -EMSGSIZE) {
					if (!i)
						goto errout;
					incomplete = true;
					break;
				}
				goto errout;
			}
			i++;
		}
	}

	nla_nest_end(skb, port_list);
	genlmsg_end(skb, hdr);
	if (incomplete)
		goto start_again;

send_done:
	nlh = nlmsg_put(skb, portid, seq, NLMSG_DONE, 0, flags | NLM_F_MULTI);
	if (!nlh) {
		err = __send_and_alloc_skb(&skb, team, portid, send_func);
		if (err)
			goto errout;
		goto send_done;
	}

	return send_func(skb, team, portid);

nla_put_failure:
	err = -EMSGSIZE;
errout:
	genlmsg_cancel(skb, hdr);
	nlmsg_free(skb);
	return err;
}

static int team_nl_cmd_port_list_get(struct sk_buff *skb,
				     struct genl_info *info)
{
	struct team *team;
	int err;

	team = team_nl_team_get(info);
	if (!team)
		return -EINVAL;

	err = team_nl_send_port_list_get(team, info->snd_portid, info->snd_seq,
					 NLM_F_ACK, team_nl_send_unicast, NULL);

	team_nl_team_put(team);

	return err;
}

static const struct genl_ops team_nl_ops[] = {
	{
		.cmd = TEAM_CMD_NOOP,
		.doit = team_nl_cmd_noop,
		.policy = team_nl_policy,
	},
	{
		.cmd = TEAM_CMD_OPTIONS_SET,
		.doit = team_nl_cmd_options_set,
		.policy = team_nl_policy,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = TEAM_CMD_OPTIONS_GET,
		.doit = team_nl_cmd_options_get,
		.policy = team_nl_policy,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = TEAM_CMD_PORT_LIST_GET,
		.doit = team_nl_cmd_port_list_get,
		.policy = team_nl_policy,
		.flags = GENL_ADMIN_PERM,
	},
};

static const struct genl_multicast_group team_nl_mcgrps[] = {
	{ .name = TEAM_GENL_CHANGE_EVENT_MC_GRP_NAME, },
};

static int team_nl_send_multicast(struct sk_buff *skb,
				  struct team *team, u32 portid)
{
	return genlmsg_multicast_netns(&team_nl_family, dev_net(team->dev),
				       skb, 0, 0, GFP_KERNEL);
}

static int team_nl_send_event_options_get(struct team *team,
					  struct list_head *sel_opt_inst_list)
{
	return team_nl_send_options_get(team, 0, 0, 0, team_nl_send_multicast,
					sel_opt_inst_list);
}

static int team_nl_send_event_port_get(struct team *team,
				       struct team_port *port)
{
	return team_nl_send_port_list_get(team, 0, 0, 0, team_nl_send_multicast,
					  port);
}

static int team_nl_init(void)
{
	return genl_register_family_with_ops_groups(&team_nl_family, team_nl_ops,
						    team_nl_mcgrps);
}

static void team_nl_fini(void)
{
	genl_unregister_family(&team_nl_family);
}


/******************
 * Change checkers
 ******************/

static void __team_options_change_check(struct team *team)
{
	int err;
	struct team_option_inst *opt_inst;
	LIST_HEAD(sel_opt_inst_list);

	list_for_each_entry(opt_inst, &team->option_inst_list, list) {
		if (opt_inst->changed)
			list_add_tail(&opt_inst->tmp_list, &sel_opt_inst_list);
	}
	err = team_nl_send_event_options_get(team, &sel_opt_inst_list);
	if (err && err != -ESRCH)
		netdev_warn(team->dev, "Failed to send options change via netlink (err %d)\n",
			    err);
}

/* rtnl lock is held */

static void __team_port_change_send(struct team_port *port, bool linkup)
{
	int err;

	port->changed = true;
	port->state.linkup = linkup;
	team_refresh_port_linkup(port);
	if (linkup) {
		struct ethtool_cmd ecmd;

		err = __ethtool_get_settings(port->dev, &ecmd);
		if (!err) {
			port->state.speed = ethtool_cmd_speed(&ecmd);
			port->state.duplex = ecmd.duplex;
			goto send_event;
		}
	}
	port->state.speed = 0;
	port->state.duplex = 0;

send_event:
	err = team_nl_send_event_port_get(port->team, port);
	if (err && err != -ESRCH)
		netdev_warn(port->team->dev, "Failed to send port change of device %s via netlink (err %d)\n",
			    port->dev->name, err);

}

static void __team_carrier_check(struct team *team)
{
	struct team_port *port;
	bool team_linkup;

	if (team->user_carrier_enabled)
		return;

	team_linkup = false;
	list_for_each_entry(port, &team->port_list, list) {
		if (port->linkup) {
			team_linkup = true;
			break;
		}
	}

	if (team_linkup)
		netif_carrier_on(team->dev);
	else
		netif_carrier_off(team->dev);
}

static void __team_port_change_check(struct team_port *port, bool linkup)
{
	if (port->state.linkup != linkup)
		__team_port_change_send(port, linkup);
	__team_carrier_check(port->team);
}

static void __team_port_change_port_added(struct team_port *port, bool linkup)
{
	__team_port_change_send(port, linkup);
	__team_carrier_check(port->team);
}

static void __team_port_change_port_removed(struct team_port *port)
{
	port->removed = true;
	__team_port_change_send(port, false);
	__team_carrier_check(port->team);
}

static void team_port_change_check(struct team_port *port, bool linkup)
{
	struct team *team = port->team;

	mutex_lock(&team->lock);
	__team_port_change_check(port, linkup);
	mutex_unlock(&team->lock);
}


/************************************
 * Net device notifier event handler
 ************************************/

static int team_device_event(struct notifier_block *unused,
			     unsigned long event, void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);
	struct team_port *port;

	port = team_port_get_rtnl(dev);
	if (!port)
		return NOTIFY_DONE;

	switch (event) {
	case NETDEV_UP:
		if (netif_carrier_ok(dev))
			team_port_change_check(port, true);
		break;
	case NETDEV_DOWN:
		team_port_change_check(port, false);
		break;
	case NETDEV_CHANGE:
		if (netif_running(port->dev))
			team_port_change_check(port,
					       !!netif_carrier_ok(port->dev));
		break;
	case NETDEV_UNREGISTER:
		team_del_slave(port->team->dev, dev);
		break;
	case NETDEV_FEAT_CHANGE:
		team_compute_features(port->team);
		break;
	case NETDEV_PRECHANGEMTU:
		/* Forbid to change mtu of underlaying device */
		if (!port->team->port_mtu_change_allowed)
			return NOTIFY_BAD;
		break;
	case NETDEV_PRE_TYPE_CHANGE:
		/* Forbid to change type of underlaying device */
		return NOTIFY_BAD;
	case NETDEV_RESEND_IGMP:
		/* Propagate to master device */
		call_netdevice_notifiers(event, port->team->dev);
		break;
	}
	return NOTIFY_DONE;
}

static struct notifier_block team_notifier_block __read_mostly = {
	.notifier_call = team_device_event,
};


/***********************
 * Module init and exit
 ***********************/

static int __init team_module_init(void)
{
	int err;

	register_netdevice_notifier(&team_notifier_block);

	err = rtnl_link_register(&team_link_ops);
	if (err)
		goto err_rtnl_reg;

	err = team_nl_init();
	if (err)
		goto err_nl_init;

	return 0;

err_nl_init:
	rtnl_link_unregister(&team_link_ops);

err_rtnl_reg:
	unregister_netdevice_notifier(&team_notifier_block);

	return err;
}

static void __exit team_module_exit(void)
{
	team_nl_fini();
	rtnl_link_unregister(&team_link_ops);
	unregister_netdevice_notifier(&team_notifier_block);
}

module_init(team_module_init);
module_exit(team_module_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Jiri Pirko <jpirko@redhat.com>");
MODULE_DESCRIPTION("Ethernet team device driver");
MODULE_ALIAS_RTNL_LINK(DRV_NAME);
