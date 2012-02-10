/*
 * net/drivers/team/team.c - Network team device driver
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
#include <linux/if_vlan.h>
#include <linux/if_arp.h>
#include <linux/socket.h>
#include <linux/etherdevice.h>
#include <linux/rtnetlink.h>
#include <net/rtnetlink.h>
#include <net/genetlink.h>
#include <net/netlink.h>
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
 * Since the ability to change mac address for open port device is tested in
 * team_port_add, this function can be called without control of return value
 */
static int __set_port_mac(struct net_device *port_dev,
			  const unsigned char *dev_addr)
{
	struct sockaddr addr;

	memcpy(addr.sa_data, dev_addr, ETH_ALEN);
	addr.sa_family = ARPHRD_ETHER;
	return dev_set_mac_address(port_dev, &addr);
}

int team_port_set_orig_mac(struct team_port *port)
{
	return __set_port_mac(port->dev, port->orig.dev_addr);
}

int team_port_set_team_mac(struct team_port *port)
{
	return __set_port_mac(port->dev, port->team->dev->dev_addr);
}
EXPORT_SYMBOL(team_port_set_team_mac);


/*******************
 * Options handling
 *******************/

struct team_option *__team_find_option(struct team *team, const char *opt_name)
{
	struct team_option *option;

	list_for_each_entry(option, &team->option_list, list) {
		if (strcmp(option->name, opt_name) == 0)
			return option;
	}
	return NULL;
}

int __team_options_register(struct team *team,
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
			goto rollback;
		}
		dst_opts[i] = kmemdup(option, sizeof(*option), GFP_KERNEL);
		if (!dst_opts[i]) {
			err = -ENOMEM;
			goto rollback;
		}
	}

	for (i = 0; i < option_count; i++) {
		dst_opts[i]->changed = true;
		dst_opts[i]->removed = false;
		list_add_tail(&dst_opts[i]->list, &team->option_list);
	}

	kfree(dst_opts);
	return 0;

rollback:
	for (i = 0; i < option_count; i++)
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
		if (del_opt) {
			del_opt->changed = true;
			del_opt->removed = true;
		}
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

static int team_option_get(struct team *team, struct team_option *option,
			   void *arg)
{
	return option->getter(team, arg);
}

static int team_option_set(struct team *team, struct team_option *option,
			   void *arg)
{
	int err;

	err = option->setter(team, arg);
	if (err)
		return err;

	option->changed = true;
	__team_options_change_check(team);
	return err;
}

/****************
 * Mode handling
 ****************/

static LIST_HEAD(mode_list);
static DEFINE_SPINLOCK(mode_list_lock);

static struct team_mode *__find_mode(const char *kind)
{
	struct team_mode *mode;

	list_for_each_entry(mode, &mode_list, list) {
		if (strcmp(mode->kind, kind) == 0)
			return mode;
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

int team_mode_register(struct team_mode *mode)
{
	int err = 0;

	if (!is_good_mode_name(mode->kind) ||
	    mode->priv_size > TEAM_MODE_PRIV_SIZE)
		return -EINVAL;
	spin_lock(&mode_list_lock);
	if (__find_mode(mode->kind)) {
		err = -EEXIST;
		goto unlock;
	}
	list_add_tail(&mode->list, &mode_list);
unlock:
	spin_unlock(&mode_list_lock);
	return err;
}
EXPORT_SYMBOL(team_mode_register);

int team_mode_unregister(struct team_mode *mode)
{
	spin_lock(&mode_list_lock);
	list_del_init(&mode->list);
	spin_unlock(&mode_list_lock);
	return 0;
}
EXPORT_SYMBOL(team_mode_unregister);

static struct team_mode *team_mode_get(const char *kind)
{
	struct team_mode *mode;

	spin_lock(&mode_list_lock);
	mode = __find_mode(kind);
	if (!mode) {
		spin_unlock(&mode_list_lock);
		request_module("team-mode-%s", kind);
		spin_lock(&mode_list_lock);
		mode = __find_mode(kind);
	}
	if (mode)
		if (!try_module_get(mode->owner))
			mode = NULL;

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

rx_handler_result_t team_dummy_receive(struct team *team,
				       struct team_port *port,
				       struct sk_buff *skb)
{
	return RX_HANDLER_ANOTHER;
}

static void team_adjust_ops(struct team *team)
{
	/*
	 * To avoid checks in rx/tx skb paths, ensure here that non-null and
	 * correct ops are always set.
	 */

	if (list_empty(&team->port_list) ||
	    !team->mode || !team->mode->ops->transmit)
		team->ops.transmit = team_dummy_transmit;
	else
		team->ops.transmit = team->mode->ops->transmit;

	if (list_empty(&team->port_list) ||
	    !team->mode || !team->mode->ops->receive)
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
	if (team->mode) {
		void (*exit_op)(struct team *team) = team->ops.exit;

		/* Clear ops area so no callback is called any longer */
		memset(&team->ops, 0, sizeof(struct team_mode_ops));
		team_adjust_ops(team);

		if (exit_op)
			exit_op(team);
		team_mode_put(team->mode);
		team->mode = NULL;
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
	struct team_mode *new_mode;
	struct net_device *dev = team->dev;
	int err;

	if (!list_empty(&team->port_list)) {
		netdev_err(dev, "No ports can be present during mode change\n");
		return -EBUSY;
	}

	if (team->mode && strcmp(team->mode->kind, kind) == 0) {
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

	res = team->ops.receive(team, port, skb);
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
 * Add/delete port to the team port list. Write guarded by rtnl_lock.
 * Takes care of correct port->index setup (might be racy).
 */
static void team_port_list_add_port(struct team *team,
				    struct team_port *port)
{
	port->index = team->port_count++;
	hlist_add_head_rcu(&port->hlist,
			   team_port_index_hash(team, port->index));
	list_add_tail_rcu(&port->list, &team->port_list);
}

static void __reconstruct_port_hlist(struct team *team, int rm_index)
{
	int i;
	struct team_port *port;

	for (i = rm_index + 1; i < team->port_count; i++) {
		port = team_get_port_by_index(team, i);
		hlist_del_rcu(&port->hlist);
		port->index--;
		hlist_add_head_rcu(&port->hlist,
				   team_port_index_hash(team, port->index));
	}
}

static void team_port_list_del_port(struct team *team,
				   struct team_port *port)
{
	int rm_index = port->index;

	hlist_del_rcu(&port->hlist);
	list_del_rcu(&port->list);
	__reconstruct_port_hlist(team, rm_index);
	team->port_count--;
}

#define TEAM_VLAN_FEATURES (NETIF_F_ALL_CSUM | NETIF_F_SG | \
			    NETIF_F_FRAGLIST | NETIF_F_ALL_TSO | \
			    NETIF_F_HIGHDMA | NETIF_F_LRO)

static void __team_compute_features(struct team *team)
{
	struct team_port *port;
	u32 vlan_features = TEAM_VLAN_FEATURES;
	unsigned short max_hard_header_len = ETH_HLEN;

	list_for_each_entry(port, &team->port_list, list) {
		vlan_features = netdev_increment_features(vlan_features,
					port->dev->vlan_features,
					TEAM_VLAN_FEATURES);

		if (port->dev->hard_header_len > max_hard_header_len)
			max_hard_header_len = port->dev->hard_header_len;
	}

	team->dev->vlan_features = vlan_features;
	team->dev->hard_header_len = max_hard_header_len;

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
	port->dev->priv_flags |= IFF_TEAM_PORT;
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
	port->dev->priv_flags &= ~IFF_TEAM_PORT;
	dev_put(team->dev);

	return err;
}

static void team_port_leave(struct team *team, struct team_port *port)
{
	if (team->ops.port_leave)
		team->ops.port_leave(team, port);
	port->dev->priv_flags &= ~IFF_TEAM_PORT;
	dev_put(team->dev);
}

static void __team_port_change_check(struct team_port *port, bool linkup);

static int team_port_add(struct team *team, struct net_device *port_dev)
{
	struct net_device *dev = team->dev;
	struct team_port *port;
	char *portname = port_dev->name;
	int err;

	if (port_dev->flags & IFF_LOOPBACK ||
	    port_dev->type != ARPHRD_ETHER) {
		netdev_err(dev, "Device %s is of an unsupported type\n",
			   portname);
		return -EINVAL;
	}

	if (team_port_exists(port_dev)) {
		netdev_err(dev, "Device %s is already a port "
				"of a team device\n", portname);
		return -EBUSY;
	}

	if (port_dev->flags & IFF_UP) {
		netdev_err(dev, "Device %s is up. Set it down before adding it as a team port\n",
			   portname);
		return -EBUSY;
	}

	port = kzalloc(sizeof(struct team_port), GFP_KERNEL);
	if (!port)
		return -ENOMEM;

	port->dev = port_dev;
	port->team = team;

	port->orig.mtu = port_dev->mtu;
	err = dev_set_mtu(port_dev, dev->mtu);
	if (err) {
		netdev_dbg(dev, "Error %d calling dev_set_mtu\n", err);
		goto err_set_mtu;
	}

	memcpy(port->orig.dev_addr, port_dev->dev_addr, ETH_ALEN);

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

	err = netdev_set_master(port_dev, dev);
	if (err) {
		netdev_err(dev, "Device %s failed to set master\n", portname);
		goto err_set_master;
	}

	err = netdev_rx_handler_register(port_dev, team_handle_frame,
					 port);
	if (err) {
		netdev_err(dev, "Device %s failed to register rx_handler\n",
			   portname);
		goto err_handler_register;
	}

	team_port_list_add_port(team, port);
	team_adjust_ops(team);
	__team_compute_features(team);
	__team_port_change_check(port, !!netif_carrier_ok(port_dev));

	netdev_info(dev, "Port device %s added\n", portname);

	return 0;

err_handler_register:
	netdev_set_master(port_dev, NULL);

err_set_master:
	vlan_vids_del_by_dev(port_dev, dev);

err_vids_add:
	dev_close(port_dev);

err_dev_open:
	team_port_leave(team, port);
	team_port_set_orig_mac(port);

err_port_enter:
	dev_set_mtu(port_dev, port->orig.mtu);

err_set_mtu:
	kfree(port);

	return err;
}

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

	port->removed = true;
	__team_port_change_check(port, false);
	team_port_list_del_port(team, port);
	team_adjust_ops(team);
	netdev_rx_handler_unregister(port_dev);
	netdev_set_master(port_dev, NULL);
	vlan_vids_del_by_dev(port_dev, dev);
	dev_close(port_dev);
	team_port_leave(team, port);
	team_port_set_orig_mac(port);
	dev_set_mtu(port_dev, port->orig.mtu);
	synchronize_rcu();
	kfree(port);
	netdev_info(dev, "Port device %s removed\n", portname);
	__team_compute_features(team);

	return 0;
}


/*****************
 * Net device ops
 *****************/

static const char team_no_mode_kind[] = "*NOMODE*";

static int team_mode_option_get(struct team *team, void *arg)
{
	const char **str = arg;

	*str = team->mode ? team->mode->kind : team_no_mode_kind;
	return 0;
}

static int team_mode_option_set(struct team *team, void *arg)
{
	const char **str = arg;

	return team_change_mode(team, *str);
}

static const struct team_option team_options[] = {
	{
		.name = "mode",
		.type = TEAM_OPTION_TYPE_STRING,
		.getter = team_mode_option_get,
		.setter = team_mode_option_set,
	},
};

static int team_init(struct net_device *dev)
{
	struct team *team = netdev_priv(dev);
	int i;
	int err;

	team->dev = dev;
	mutex_init(&team->lock);

	team->pcpu_stats = alloc_percpu(struct team_pcpu_stats);
	if (!team->pcpu_stats)
		return -ENOMEM;

	for (i = 0; i < TEAM_PORT_HASHENTRIES; i++)
		INIT_HLIST_HEAD(&team->port_hlist[i]);
	INIT_LIST_HEAD(&team->port_list);

	team_adjust_ops(team);

	INIT_LIST_HEAD(&team->option_list);
	err = team_options_register(team, team_options, ARRAY_SIZE(team_options));
	if (err)
		goto err_options_register;
	netif_carrier_off(dev);

	return 0;

err_options_register:
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
	netif_carrier_on(dev);
	return 0;
}

static int team_close(struct net_device *dev)
{
	netif_carrier_off(dev);
	return 0;
}

/*
 * note: already called with rcu_read_lock
 */
static netdev_tx_t team_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct team *team = netdev_priv(dev);
	bool tx_success = false;
	unsigned int len = skb->len;

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
		dev_uc_sync(port->dev, dev);
		dev_mc_sync(port->dev, dev);
	}
	rcu_read_unlock();
}

static int team_set_mac_address(struct net_device *dev, void *p)
{
	struct team *team = netdev_priv(dev);
	struct team_port *port;
	struct sockaddr *addr = p;

	memcpy(dev->dev_addr, addr->sa_data, ETH_ALEN);
	rcu_read_lock();
	list_for_each_entry_rcu(port, &team->port_list, list)
		if (team->ops.port_change_mac)
			team->ops.port_change_mac(team, port);
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
	list_for_each_entry(port, &team->port_list, list) {
		err = dev_set_mtu(port->dev, new_mtu);
		if (err) {
			netdev_err(dev, "Device %s failed to change mtu",
				   port->dev->name);
			goto unwind;
		}
	}
	mutex_unlock(&team->lock);

	dev->mtu = new_mtu;

	return 0;

unwind:
	list_for_each_entry_continue_reverse(port, &team->port_list, list)
		dev_set_mtu(port->dev, dev->mtu);
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
			start = u64_stats_fetch_begin_bh(&p->syncp);
			rx_packets	= p->rx_packets;
			rx_bytes	= p->rx_bytes;
			rx_multicast	= p->rx_multicast;
			tx_packets	= p->tx_packets;
			tx_bytes	= p->tx_bytes;
		} while (u64_stats_fetch_retry_bh(&p->syncp, start));

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

static int team_vlan_rx_add_vid(struct net_device *dev, uint16_t vid)
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
		err = vlan_vid_add(port->dev, vid);
		if (err)
			goto unwind;
	}
	mutex_unlock(&team->lock);

	return 0;

unwind:
	list_for_each_entry_continue_reverse(port, &team->port_list, list)
		vlan_vid_del(port->dev, vid);
	mutex_unlock(&team->lock);

	return err;
}

static int team_vlan_rx_kill_vid(struct net_device *dev, uint16_t vid)
{
	struct team *team = netdev_priv(dev);
	struct team_port *port;

	rcu_read_lock();
	list_for_each_entry_rcu(port, &team->port_list, list)
		vlan_vid_del(port->dev, vid);
	rcu_read_unlock();

	return 0;
}

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

static const struct net_device_ops team_netdev_ops = {
	.ndo_init		= team_init,
	.ndo_uninit		= team_uninit,
	.ndo_open		= team_open,
	.ndo_stop		= team_close,
	.ndo_start_xmit		= team_xmit,
	.ndo_change_rx_flags	= team_change_rx_flags,
	.ndo_set_rx_mode	= team_set_rx_mode,
	.ndo_set_mac_address	= team_set_mac_address,
	.ndo_change_mtu		= team_change_mtu,
	.ndo_get_stats64	= team_get_stats64,
	.ndo_vlan_rx_add_vid	= team_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid	= team_vlan_rx_kill_vid,
	.ndo_add_slave		= team_add_slave,
	.ndo_del_slave		= team_del_slave,
	.ndo_fix_features	= team_fix_features,
};


/***********************
 * rt netlink interface
 ***********************/

static void team_setup(struct net_device *dev)
{
	ether_setup(dev);

	dev->netdev_ops = &team_netdev_ops;
	dev->destructor	= team_destructor;
	dev->tx_queue_len = 0;
	dev->flags |= IFF_MULTICAST;
	dev->priv_flags &= ~(IFF_XMIT_DST_RELEASE | IFF_TX_SKB_SHARING);

	/*
	 * Indicate we support unicast address filtering. That way core won't
	 * bring us to promisc mode in case a unicast addr is added.
	 * Let this up to underlay drivers.
	 */
	dev->priv_flags |= IFF_UNICAST_FLT;

	dev->features |= NETIF_F_LLTX;
	dev->features |= NETIF_F_GRO;
	dev->hw_features = NETIF_F_HW_VLAN_TX |
			   NETIF_F_HW_VLAN_RX |
			   NETIF_F_HW_VLAN_FILTER;

	dev->features |= dev->hw_features;
}

static int team_newlink(struct net *src_net, struct net_device *dev,
			struct nlattr *tb[], struct nlattr *data[])
{
	int err;

	if (tb[IFLA_ADDRESS] == NULL)
		random_ether_addr(dev->dev_addr);

	err = register_netdevice(dev);
	if (err)
		return err;

	return 0;
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

static struct rtnl_link_ops team_link_ops __read_mostly = {
	.kind		= DRV_NAME,
	.priv_size	= sizeof(struct team),
	.setup		= team_setup,
	.newlink	= team_newlink,
	.validate	= team_validate,
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
	[TEAM_ATTR_OPTION_DATA] = {
		.type = NLA_BINARY,
		.len = TEAM_STRING_MAX_LEN,
	},
};

static int team_nl_cmd_noop(struct sk_buff *skb, struct genl_info *info)
{
	struct sk_buff *msg;
	void *hdr;
	int err;

	msg = nlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	hdr = genlmsg_put(msg, info->snd_pid, info->snd_seq,
			  &team_nl_family, 0, TEAM_CMD_NOOP);
	if (IS_ERR(hdr)) {
		err = PTR_ERR(hdr);
		goto err_msg_put;
	}

	genlmsg_end(msg, hdr);

	return genlmsg_unicast(genl_info_net(info), msg, info->snd_pid);

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

static int team_nl_send_generic(struct genl_info *info, struct team *team,
				int (*fill_func)(struct sk_buff *skb,
						 struct genl_info *info,
						 int flags, struct team *team))
{
	struct sk_buff *skb;
	int err;

	skb = nlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!skb)
		return -ENOMEM;

	err = fill_func(skb, info, NLM_F_ACK, team);
	if (err < 0)
		goto err_fill;

	err = genlmsg_unicast(genl_info_net(info), skb, info->snd_pid);
	return err;

err_fill:
	nlmsg_free(skb);
	return err;
}

static int team_nl_fill_options_get(struct sk_buff *skb,
				    u32 pid, u32 seq, int flags,
				    struct team *team, bool fillall)
{
	struct nlattr *option_list;
	void *hdr;
	struct team_option *option;

	hdr = genlmsg_put(skb, pid, seq, &team_nl_family, flags,
			  TEAM_CMD_OPTIONS_GET);
	if (IS_ERR(hdr))
		return PTR_ERR(hdr);

	NLA_PUT_U32(skb, TEAM_ATTR_TEAM_IFINDEX, team->dev->ifindex);
	option_list = nla_nest_start(skb, TEAM_ATTR_LIST_OPTION);
	if (!option_list)
		return -EMSGSIZE;

	list_for_each_entry(option, &team->option_list, list) {
		struct nlattr *option_item;
		long arg;

		/* Include only changed options if fill all mode is not on */
		if (!fillall && !option->changed)
			continue;
		option_item = nla_nest_start(skb, TEAM_ATTR_ITEM_OPTION);
		if (!option_item)
			goto nla_put_failure;
		NLA_PUT_STRING(skb, TEAM_ATTR_OPTION_NAME, option->name);
		if (option->changed) {
			NLA_PUT_FLAG(skb, TEAM_ATTR_OPTION_CHANGED);
			option->changed = false;
		}
		if (option->removed)
			NLA_PUT_FLAG(skb, TEAM_ATTR_OPTION_REMOVED);
		switch (option->type) {
		case TEAM_OPTION_TYPE_U32:
			NLA_PUT_U8(skb, TEAM_ATTR_OPTION_TYPE, NLA_U32);
			team_option_get(team, option, &arg);
			NLA_PUT_U32(skb, TEAM_ATTR_OPTION_DATA, arg);
			break;
		case TEAM_OPTION_TYPE_STRING:
			NLA_PUT_U8(skb, TEAM_ATTR_OPTION_TYPE, NLA_STRING);
			team_option_get(team, option, &arg);
			NLA_PUT_STRING(skb, TEAM_ATTR_OPTION_DATA,
				       (char *) arg);
			break;
		default:
			BUG();
		}
		nla_nest_end(skb, option_item);
	}

	nla_nest_end(skb, option_list);
	return genlmsg_end(skb, hdr);

nla_put_failure:
	genlmsg_cancel(skb, hdr);
	return -EMSGSIZE;
}

static int team_nl_fill_options_get_all(struct sk_buff *skb,
					struct genl_info *info, int flags,
					struct team *team)
{
	return team_nl_fill_options_get(skb, info->snd_pid,
					info->snd_seq, NLM_F_ACK,
					team, true);
}

static int team_nl_cmd_options_get(struct sk_buff *skb, struct genl_info *info)
{
	struct team *team;
	int err;

	team = team_nl_team_get(info);
	if (!team)
		return -EINVAL;

	err = team_nl_send_generic(info, team, team_nl_fill_options_get_all);

	team_nl_team_put(team);

	return err;
}

static int team_nl_cmd_options_set(struct sk_buff *skb, struct genl_info *info)
{
	struct team *team;
	int err = 0;
	int i;
	struct nlattr *nl_option;

	team = team_nl_team_get(info);
	if (!team)
		return -EINVAL;

	err = -EINVAL;
	if (!info->attrs[TEAM_ATTR_LIST_OPTION]) {
		err = -EINVAL;
		goto team_put;
	}

	nla_for_each_nested(nl_option, info->attrs[TEAM_ATTR_LIST_OPTION], i) {
		struct nlattr *mode_attrs[TEAM_ATTR_OPTION_MAX + 1];
		enum team_option_type opt_type;
		struct team_option *option;
		char *opt_name;
		bool opt_found = false;

		if (nla_type(nl_option) != TEAM_ATTR_ITEM_OPTION) {
			err = -EINVAL;
			goto team_put;
		}
		err = nla_parse_nested(mode_attrs, TEAM_ATTR_OPTION_MAX,
				       nl_option, team_nl_option_policy);
		if (err)
			goto team_put;
		if (!mode_attrs[TEAM_ATTR_OPTION_NAME] ||
		    !mode_attrs[TEAM_ATTR_OPTION_TYPE] ||
		    !mode_attrs[TEAM_ATTR_OPTION_DATA]) {
			err = -EINVAL;
			goto team_put;
		}
		switch (nla_get_u8(mode_attrs[TEAM_ATTR_OPTION_TYPE])) {
		case NLA_U32:
			opt_type = TEAM_OPTION_TYPE_U32;
			break;
		case NLA_STRING:
			opt_type = TEAM_OPTION_TYPE_STRING;
			break;
		default:
			goto team_put;
		}

		opt_name = nla_data(mode_attrs[TEAM_ATTR_OPTION_NAME]);
		list_for_each_entry(option, &team->option_list, list) {
			long arg;
			struct nlattr *opt_data_attr;

			if (option->type != opt_type ||
			    strcmp(option->name, opt_name))
				continue;
			opt_found = true;
			opt_data_attr = mode_attrs[TEAM_ATTR_OPTION_DATA];
			switch (opt_type) {
			case TEAM_OPTION_TYPE_U32:
				arg = nla_get_u32(opt_data_attr);
				break;
			case TEAM_OPTION_TYPE_STRING:
				arg = (long) nla_data(opt_data_attr);
				break;
			default:
				BUG();
			}
			err = team_option_set(team, option, &arg);
			if (err)
				goto team_put;
		}
		if (!opt_found) {
			err = -ENOENT;
			goto team_put;
		}
	}

team_put:
	team_nl_team_put(team);

	return err;
}

static int team_nl_fill_port_list_get(struct sk_buff *skb,
				      u32 pid, u32 seq, int flags,
				      struct team *team,
				      bool fillall)
{
	struct nlattr *port_list;
	void *hdr;
	struct team_port *port;

	hdr = genlmsg_put(skb, pid, seq, &team_nl_family, flags,
			  TEAM_CMD_PORT_LIST_GET);
	if (IS_ERR(hdr))
		return PTR_ERR(hdr);

	NLA_PUT_U32(skb, TEAM_ATTR_TEAM_IFINDEX, team->dev->ifindex);
	port_list = nla_nest_start(skb, TEAM_ATTR_LIST_PORT);
	if (!port_list)
		return -EMSGSIZE;

	list_for_each_entry(port, &team->port_list, list) {
		struct nlattr *port_item;

		/* Include only changed ports if fill all mode is not on */
		if (!fillall && !port->changed)
			continue;
		port_item = nla_nest_start(skb, TEAM_ATTR_ITEM_PORT);
		if (!port_item)
			goto nla_put_failure;
		NLA_PUT_U32(skb, TEAM_ATTR_PORT_IFINDEX, port->dev->ifindex);
		if (port->changed) {
			NLA_PUT_FLAG(skb, TEAM_ATTR_PORT_CHANGED);
			port->changed = false;
		}
		if (port->removed)
			NLA_PUT_FLAG(skb, TEAM_ATTR_PORT_REMOVED);
		if (port->linkup)
			NLA_PUT_FLAG(skb, TEAM_ATTR_PORT_LINKUP);
		NLA_PUT_U32(skb, TEAM_ATTR_PORT_SPEED, port->speed);
		NLA_PUT_U8(skb, TEAM_ATTR_PORT_DUPLEX, port->duplex);
		nla_nest_end(skb, port_item);
	}

	nla_nest_end(skb, port_list);
	return genlmsg_end(skb, hdr);

nla_put_failure:
	genlmsg_cancel(skb, hdr);
	return -EMSGSIZE;
}

static int team_nl_fill_port_list_get_all(struct sk_buff *skb,
					  struct genl_info *info, int flags,
					  struct team *team)
{
	return team_nl_fill_port_list_get(skb, info->snd_pid,
					  info->snd_seq, NLM_F_ACK,
					  team, true);
}

static int team_nl_cmd_port_list_get(struct sk_buff *skb,
				     struct genl_info *info)
{
	struct team *team;
	int err;

	team = team_nl_team_get(info);
	if (!team)
		return -EINVAL;

	err = team_nl_send_generic(info, team, team_nl_fill_port_list_get_all);

	team_nl_team_put(team);

	return err;
}

static struct genl_ops team_nl_ops[] = {
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

static struct genl_multicast_group team_change_event_mcgrp = {
	.name = TEAM_GENL_CHANGE_EVENT_MC_GRP_NAME,
};

static int team_nl_send_event_options_get(struct team *team)
{
	struct sk_buff *skb;
	int err;
	struct net *net = dev_net(team->dev);

	skb = nlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!skb)
		return -ENOMEM;

	err = team_nl_fill_options_get(skb, 0, 0, 0, team, false);
	if (err < 0)
		goto err_fill;

	err = genlmsg_multicast_netns(net, skb, 0, team_change_event_mcgrp.id,
				      GFP_KERNEL);
	return err;

err_fill:
	nlmsg_free(skb);
	return err;
}

static int team_nl_send_event_port_list_get(struct team *team)
{
	struct sk_buff *skb;
	int err;
	struct net *net = dev_net(team->dev);

	skb = nlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!skb)
		return -ENOMEM;

	err = team_nl_fill_port_list_get(skb, 0, 0, 0, team, false);
	if (err < 0)
		goto err_fill;

	err = genlmsg_multicast_netns(net, skb, 0, team_change_event_mcgrp.id,
				      GFP_KERNEL);
	return err;

err_fill:
	nlmsg_free(skb);
	return err;
}

static int team_nl_init(void)
{
	int err;

	err = genl_register_family_with_ops(&team_nl_family, team_nl_ops,
					    ARRAY_SIZE(team_nl_ops));
	if (err)
		return err;

	err = genl_register_mc_group(&team_nl_family, &team_change_event_mcgrp);
	if (err)
		goto err_change_event_grp_reg;

	return 0;

err_change_event_grp_reg:
	genl_unregister_family(&team_nl_family);

	return err;
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

	err = team_nl_send_event_options_get(team);
	if (err)
		netdev_warn(team->dev, "Failed to send options change via netlink\n");
}

/* rtnl lock is held */
static void __team_port_change_check(struct team_port *port, bool linkup)
{
	int err;

	if (!port->removed && port->linkup == linkup)
		return;

	port->changed = true;
	port->linkup = linkup;
	if (linkup) {
		struct ethtool_cmd ecmd;

		err = __ethtool_get_settings(port->dev, &ecmd);
		if (!err) {
			port->speed = ethtool_cmd_speed(&ecmd);
			port->duplex = ecmd.duplex;
			goto send_event;
		}
	}
	port->speed = 0;
	port->duplex = 0;

send_event:
	err = team_nl_send_event_port_list_get(port->team);
	if (err)
		netdev_warn(port->team->dev, "Failed to send port change of device %s via netlink\n",
			    port->dev->name);

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
	struct net_device *dev = (struct net_device *) ptr;
	struct team_port *port;

	port = team_port_get_rtnl(dev);
	if (!port)
		return NOTIFY_DONE;

	switch (event) {
	case NETDEV_UP:
		if (netif_carrier_ok(dev))
			team_port_change_check(port, true);
	case NETDEV_DOWN:
		team_port_change_check(port, false);
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
	case NETDEV_CHANGEMTU:
		/* Forbid to change mtu of underlaying device */
		return NOTIFY_BAD;
	case NETDEV_PRE_TYPE_CHANGE:
		/* Forbid to change type of underlaying device */
		return NOTIFY_BAD;
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
