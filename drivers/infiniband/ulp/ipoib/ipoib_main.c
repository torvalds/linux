/*
 * Copyright (c) 2004 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005 Sun Microsystems, Inc. All rights reserved.
 * Copyright (c) 2004 Voltaire, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "ipoib.h"

#include <linux/module.h>

#include <linux/init.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/vmalloc.h>

#include <linux/if_arp.h>	/* For ARPHRD_xxx */

#include <linux/ip.h>
#include <linux/in.h>

#include <linux/jhash.h>
#include <net/arp.h>
#include <net/addrconf.h>
#include <linux/inetdevice.h>
#include <rdma/ib_cache.h>

MODULE_AUTHOR("Roland Dreier");
MODULE_DESCRIPTION("IP-over-InfiniBand net driver");
MODULE_LICENSE("Dual BSD/GPL");

int ipoib_sendq_size __read_mostly = IPOIB_TX_RING_SIZE;
int ipoib_recvq_size __read_mostly = IPOIB_RX_RING_SIZE;

module_param_named(send_queue_size, ipoib_sendq_size, int, 0444);
MODULE_PARM_DESC(send_queue_size, "Number of descriptors in send queue");
module_param_named(recv_queue_size, ipoib_recvq_size, int, 0444);
MODULE_PARM_DESC(recv_queue_size, "Number of descriptors in receive queue");

#ifdef CONFIG_INFINIBAND_IPOIB_DEBUG
int ipoib_debug_level;

module_param_named(debug_level, ipoib_debug_level, int, 0644);
MODULE_PARM_DESC(debug_level, "Enable debug tracing if > 0");
#endif

struct ipoib_path_iter {
	struct net_device *dev;
	struct ipoib_path  path;
};

static const u8 ipv4_bcast_addr[] = {
	0x00, 0xff, 0xff, 0xff,
	0xff, 0x12, 0x40, 0x1b,	0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00,	0xff, 0xff, 0xff, 0xff
};

struct workqueue_struct *ipoib_workqueue;

struct ib_sa_client ipoib_sa_client;

static int ipoib_add_one(struct ib_device *device);
static void ipoib_remove_one(struct ib_device *device, void *client_data);
static void ipoib_neigh_reclaim(struct rcu_head *rp);
static struct net_device *ipoib_get_net_dev_by_params(
		struct ib_device *dev, u8 port, u16 pkey,
		const union ib_gid *gid, const struct sockaddr *addr,
		void *client_data);
static int ipoib_set_mac(struct net_device *dev, void *addr);
static int ipoib_ioctl(struct net_device *dev, struct ifreq *ifr,
		       int cmd);

static struct ib_client ipoib_client = {
	.name   = "ipoib",
	.add    = ipoib_add_one,
	.remove = ipoib_remove_one,
	.get_net_dev_by_params = ipoib_get_net_dev_by_params,
};

#ifdef CONFIG_INFINIBAND_IPOIB_DEBUG
static int ipoib_netdev_event(struct notifier_block *this,
			      unsigned long event, void *ptr)
{
	struct netdev_notifier_info *ni = ptr;
	struct net_device *dev = ni->dev;

	if (dev->netdev_ops->ndo_open != ipoib_open)
		return NOTIFY_DONE;

	switch (event) {
	case NETDEV_REGISTER:
		ipoib_create_debug_files(dev);
		break;
	case NETDEV_CHANGENAME:
		ipoib_delete_debug_files(dev);
		ipoib_create_debug_files(dev);
		break;
	case NETDEV_UNREGISTER:
		ipoib_delete_debug_files(dev);
		break;
	}

	return NOTIFY_DONE;
}
#endif

int ipoib_open(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);

	ipoib_dbg(priv, "bringing up interface\n");

	netif_carrier_off(dev);

	set_bit(IPOIB_FLAG_ADMIN_UP, &priv->flags);

	priv->sm_fullmember_sendonly_support = false;

	if (ipoib_ib_dev_open(dev)) {
		if (!test_bit(IPOIB_PKEY_ASSIGNED, &priv->flags))
			return 0;
		goto err_disable;
	}

	ipoib_ib_dev_up(dev);

	if (!test_bit(IPOIB_FLAG_SUBINTERFACE, &priv->flags)) {
		struct ipoib_dev_priv *cpriv;

		/* Bring up any child interfaces too */
		down_read(&priv->vlan_rwsem);
		list_for_each_entry(cpriv, &priv->child_intfs, list) {
			int flags;

			flags = cpriv->dev->flags;
			if (flags & IFF_UP)
				continue;

			dev_change_flags(cpriv->dev, flags | IFF_UP, NULL);
		}
		up_read(&priv->vlan_rwsem);
	}

	netif_start_queue(dev);

	return 0;

err_disable:
	clear_bit(IPOIB_FLAG_ADMIN_UP, &priv->flags);

	return -EINVAL;
}

static int ipoib_stop(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);

	ipoib_dbg(priv, "stopping interface\n");

	clear_bit(IPOIB_FLAG_ADMIN_UP, &priv->flags);

	netif_stop_queue(dev);

	ipoib_ib_dev_down(dev);
	ipoib_ib_dev_stop(dev);

	if (!test_bit(IPOIB_FLAG_SUBINTERFACE, &priv->flags)) {
		struct ipoib_dev_priv *cpriv;

		/* Bring down any child interfaces too */
		down_read(&priv->vlan_rwsem);
		list_for_each_entry(cpriv, &priv->child_intfs, list) {
			int flags;

			flags = cpriv->dev->flags;
			if (!(flags & IFF_UP))
				continue;

			dev_change_flags(cpriv->dev, flags & ~IFF_UP, NULL);
		}
		up_read(&priv->vlan_rwsem);
	}

	return 0;
}

static netdev_features_t ipoib_fix_features(struct net_device *dev, netdev_features_t features)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);

	if (test_bit(IPOIB_FLAG_ADMIN_CM, &priv->flags))
		features &= ~(NETIF_F_IP_CSUM | NETIF_F_TSO);

	return features;
}

static int ipoib_change_mtu(struct net_device *dev, int new_mtu)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);
	int ret = 0;

	/* dev->mtu > 2K ==> connected mode */
	if (ipoib_cm_admin_enabled(dev)) {
		if (new_mtu > ipoib_cm_max_mtu(dev))
			return -EINVAL;

		if (new_mtu > priv->mcast_mtu)
			ipoib_warn(priv, "mtu > %d will cause multicast packet drops.\n",
				   priv->mcast_mtu);

		dev->mtu = new_mtu;
		return 0;
	}

	if (new_mtu < (ETH_MIN_MTU + IPOIB_ENCAP_LEN) ||
	    new_mtu > IPOIB_UD_MTU(priv->max_ib_mtu))
		return -EINVAL;

	priv->admin_mtu = new_mtu;

	if (priv->mcast_mtu < priv->admin_mtu)
		ipoib_dbg(priv, "MTU must be smaller than the underlying "
				"link layer MTU - 4 (%u)\n", priv->mcast_mtu);

	new_mtu = min(priv->mcast_mtu, priv->admin_mtu);

	if (priv->rn_ops->ndo_change_mtu) {
		bool carrier_status = netif_carrier_ok(dev);

		netif_carrier_off(dev);

		/* notify lower level on the real mtu */
		ret = priv->rn_ops->ndo_change_mtu(dev, new_mtu);

		if (carrier_status)
			netif_carrier_on(dev);
	} else {
		dev->mtu = new_mtu;
	}

	return ret;
}

static void ipoib_get_stats(struct net_device *dev,
			    struct rtnl_link_stats64 *stats)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);

	if (priv->rn_ops->ndo_get_stats64)
		priv->rn_ops->ndo_get_stats64(dev, stats);
	else
		netdev_stats_to_stats64(stats, &dev->stats);
}

/* Called with an RCU read lock taken */
static bool ipoib_is_dev_match_addr_rcu(const struct sockaddr *addr,
					struct net_device *dev)
{
	struct net *net = dev_net(dev);
	struct in_device *in_dev;
	struct sockaddr_in *addr_in = (struct sockaddr_in *)addr;
	struct sockaddr_in6 *addr_in6 = (struct sockaddr_in6 *)addr;
	__be32 ret_addr;

	switch (addr->sa_family) {
	case AF_INET:
		in_dev = in_dev_get(dev);
		if (!in_dev)
			return false;

		ret_addr = inet_confirm_addr(net, in_dev, 0,
					     addr_in->sin_addr.s_addr,
					     RT_SCOPE_HOST);
		in_dev_put(in_dev);
		if (ret_addr)
			return true;

		break;
	case AF_INET6:
		if (IS_ENABLED(CONFIG_IPV6) &&
		    ipv6_chk_addr(net, &addr_in6->sin6_addr, dev, 1))
			return true;

		break;
	}
	return false;
}

/**
 * Find the master net_device on top of the given net_device.
 * @dev: base IPoIB net_device
 *
 * Returns the master net_device with a reference held, or the same net_device
 * if no master exists.
 */
static struct net_device *ipoib_get_master_net_dev(struct net_device *dev)
{
	struct net_device *master;

	rcu_read_lock();
	master = netdev_master_upper_dev_get_rcu(dev);
	if (master)
		dev_hold(master);
	rcu_read_unlock();

	if (master)
		return master;

	dev_hold(dev);
	return dev;
}

struct ipoib_walk_data {
	const struct sockaddr *addr;
	struct net_device *result;
};

static int ipoib_upper_walk(struct net_device *upper,
			    struct netdev_nested_priv *priv)
{
	struct ipoib_walk_data *data = (struct ipoib_walk_data *)priv->data;
	int ret = 0;

	if (ipoib_is_dev_match_addr_rcu(data->addr, upper)) {
		dev_hold(upper);
		data->result = upper;
		ret = 1;
	}

	return ret;
}

/**
 * Find a net_device matching the given address, which is an upper device of
 * the given net_device.
 * @addr: IP address to look for.
 * @dev: base IPoIB net_device
 *
 * If found, returns the net_device with a reference held. Otherwise return
 * NULL.
 */
static struct net_device *ipoib_get_net_dev_match_addr(
		const struct sockaddr *addr, struct net_device *dev)
{
	struct netdev_nested_priv priv;
	struct ipoib_walk_data data = {
		.addr = addr,
	};

	priv.data = (void *)&data;
	rcu_read_lock();
	if (ipoib_is_dev_match_addr_rcu(addr, dev)) {
		dev_hold(dev);
		data.result = dev;
		goto out;
	}

	netdev_walk_all_upper_dev_rcu(dev, ipoib_upper_walk, &priv);
out:
	rcu_read_unlock();
	return data.result;
}

/* returns the number of IPoIB netdevs on top a given ipoib device matching a
 * pkey_index and address, if one exists.
 *
 * @found_net_dev: contains a matching net_device if the return value >= 1,
 * with a reference held. */
static int ipoib_match_gid_pkey_addr(struct ipoib_dev_priv *priv,
				     const union ib_gid *gid,
				     u16 pkey_index,
				     const struct sockaddr *addr,
				     int nesting,
				     struct net_device **found_net_dev)
{
	struct ipoib_dev_priv *child_priv;
	struct net_device *net_dev = NULL;
	int matches = 0;

	if (priv->pkey_index == pkey_index &&
	    (!gid || !memcmp(gid, &priv->local_gid, sizeof(*gid)))) {
		if (!addr) {
			net_dev = ipoib_get_master_net_dev(priv->dev);
		} else {
			/* Verify the net_device matches the IP address, as
			 * IPoIB child devices currently share a GID. */
			net_dev = ipoib_get_net_dev_match_addr(addr, priv->dev);
		}
		if (net_dev) {
			if (!*found_net_dev)
				*found_net_dev = net_dev;
			else
				dev_put(net_dev);
			++matches;
		}
	}

	/* Check child interfaces */
	down_read_nested(&priv->vlan_rwsem, nesting);
	list_for_each_entry(child_priv, &priv->child_intfs, list) {
		matches += ipoib_match_gid_pkey_addr(child_priv, gid,
						    pkey_index, addr,
						    nesting + 1,
						    found_net_dev);
		if (matches > 1)
			break;
	}
	up_read(&priv->vlan_rwsem);

	return matches;
}

/* Returns the number of matching net_devs found (between 0 and 2). Also
 * return the matching net_device in the @net_dev parameter, holding a
 * reference to the net_device, if the number of matches >= 1 */
static int __ipoib_get_net_dev_by_params(struct list_head *dev_list, u8 port,
					 u16 pkey_index,
					 const union ib_gid *gid,
					 const struct sockaddr *addr,
					 struct net_device **net_dev)
{
	struct ipoib_dev_priv *priv;
	int matches = 0;

	*net_dev = NULL;

	list_for_each_entry(priv, dev_list, list) {
		if (priv->port != port)
			continue;

		matches += ipoib_match_gid_pkey_addr(priv, gid, pkey_index,
						     addr, 0, net_dev);
		if (matches > 1)
			break;
	}

	return matches;
}

static struct net_device *ipoib_get_net_dev_by_params(
		struct ib_device *dev, u8 port, u16 pkey,
		const union ib_gid *gid, const struct sockaddr *addr,
		void *client_data)
{
	struct net_device *net_dev;
	struct list_head *dev_list = client_data;
	u16 pkey_index;
	int matches;
	int ret;

	if (!rdma_protocol_ib(dev, port))
		return NULL;

	ret = ib_find_cached_pkey(dev, port, pkey, &pkey_index);
	if (ret)
		return NULL;

	/* See if we can find a unique device matching the L2 parameters */
	matches = __ipoib_get_net_dev_by_params(dev_list, port, pkey_index,
						gid, NULL, &net_dev);

	switch (matches) {
	case 0:
		return NULL;
	case 1:
		return net_dev;
	}

	dev_put(net_dev);

	/* Couldn't find a unique device with L2 parameters only. Use L3
	 * address to uniquely match the net device */
	matches = __ipoib_get_net_dev_by_params(dev_list, port, pkey_index,
						gid, addr, &net_dev);
	switch (matches) {
	case 0:
		return NULL;
	default:
		dev_warn_ratelimited(&dev->dev,
				     "duplicate IP address detected\n");
		fallthrough;
	case 1:
		return net_dev;
	}
}

int ipoib_set_mode(struct net_device *dev, const char *buf)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);

	if ((test_bit(IPOIB_FLAG_ADMIN_CM, &priv->flags) &&
	     !strcmp(buf, "connected\n")) ||
	     (!test_bit(IPOIB_FLAG_ADMIN_CM, &priv->flags) &&
	     !strcmp(buf, "datagram\n"))) {
		return 0;
	}

	/* flush paths if we switch modes so that connections are restarted */
	if (IPOIB_CM_SUPPORTED(dev->dev_addr) && !strcmp(buf, "connected\n")) {
		set_bit(IPOIB_FLAG_ADMIN_CM, &priv->flags);
		ipoib_warn(priv, "enabling connected mode "
			   "will cause multicast packet drops\n");
		netdev_update_features(dev);
		dev_set_mtu(dev, ipoib_cm_max_mtu(dev));
		netif_set_real_num_tx_queues(dev, 1);
		rtnl_unlock();
		priv->tx_wr.wr.send_flags &= ~IB_SEND_IP_CSUM;

		ipoib_flush_paths(dev);
		return (!rtnl_trylock()) ? -EBUSY : 0;
	}

	if (!strcmp(buf, "datagram\n")) {
		clear_bit(IPOIB_FLAG_ADMIN_CM, &priv->flags);
		netdev_update_features(dev);
		dev_set_mtu(dev, min(priv->mcast_mtu, dev->mtu));
		netif_set_real_num_tx_queues(dev, dev->num_tx_queues);
		rtnl_unlock();
		ipoib_flush_paths(dev);
		return (!rtnl_trylock()) ? -EBUSY : 0;
	}

	return -EINVAL;
}

struct ipoib_path *__path_find(struct net_device *dev, void *gid)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);
	struct rb_node *n = priv->path_tree.rb_node;
	struct ipoib_path *path;
	int ret;

	while (n) {
		path = rb_entry(n, struct ipoib_path, rb_node);

		ret = memcmp(gid, path->pathrec.dgid.raw,
			     sizeof (union ib_gid));

		if (ret < 0)
			n = n->rb_left;
		else if (ret > 0)
			n = n->rb_right;
		else
			return path;
	}

	return NULL;
}

static int __path_add(struct net_device *dev, struct ipoib_path *path)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);
	struct rb_node **n = &priv->path_tree.rb_node;
	struct rb_node *pn = NULL;
	struct ipoib_path *tpath;
	int ret;

	while (*n) {
		pn = *n;
		tpath = rb_entry(pn, struct ipoib_path, rb_node);

		ret = memcmp(path->pathrec.dgid.raw, tpath->pathrec.dgid.raw,
			     sizeof (union ib_gid));
		if (ret < 0)
			n = &pn->rb_left;
		else if (ret > 0)
			n = &pn->rb_right;
		else
			return -EEXIST;
	}

	rb_link_node(&path->rb_node, pn, n);
	rb_insert_color(&path->rb_node, &priv->path_tree);

	list_add_tail(&path->list, &priv->path_list);

	return 0;
}

static void path_free(struct net_device *dev, struct ipoib_path *path)
{
	struct sk_buff *skb;

	while ((skb = __skb_dequeue(&path->queue)))
		dev_kfree_skb_irq(skb);

	ipoib_dbg(ipoib_priv(dev), "%s\n", __func__);

	/* remove all neigh connected to this path */
	ipoib_del_neighs_by_gid(dev, path->pathrec.dgid.raw);

	if (path->ah)
		ipoib_put_ah(path->ah);

	kfree(path);
}

#ifdef CONFIG_INFINIBAND_IPOIB_DEBUG

struct ipoib_path_iter *ipoib_path_iter_init(struct net_device *dev)
{
	struct ipoib_path_iter *iter;

	iter = kmalloc(sizeof(*iter), GFP_KERNEL);
	if (!iter)
		return NULL;

	iter->dev = dev;
	memset(iter->path.pathrec.dgid.raw, 0, 16);

	if (ipoib_path_iter_next(iter)) {
		kfree(iter);
		return NULL;
	}

	return iter;
}

int ipoib_path_iter_next(struct ipoib_path_iter *iter)
{
	struct ipoib_dev_priv *priv = ipoib_priv(iter->dev);
	struct rb_node *n;
	struct ipoib_path *path;
	int ret = 1;

	spin_lock_irq(&priv->lock);

	n = rb_first(&priv->path_tree);

	while (n) {
		path = rb_entry(n, struct ipoib_path, rb_node);

		if (memcmp(iter->path.pathrec.dgid.raw, path->pathrec.dgid.raw,
			   sizeof (union ib_gid)) < 0) {
			iter->path = *path;
			ret = 0;
			break;
		}

		n = rb_next(n);
	}

	spin_unlock_irq(&priv->lock);

	return ret;
}

void ipoib_path_iter_read(struct ipoib_path_iter *iter,
			  struct ipoib_path *path)
{
	*path = iter->path;
}

#endif /* CONFIG_INFINIBAND_IPOIB_DEBUG */

void ipoib_mark_paths_invalid(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);
	struct ipoib_path *path, *tp;

	spin_lock_irq(&priv->lock);

	list_for_each_entry_safe(path, tp, &priv->path_list, list) {
		ipoib_dbg(priv, "mark path LID 0x%08x GID %pI6 invalid\n",
			  be32_to_cpu(sa_path_get_dlid(&path->pathrec)),
			  path->pathrec.dgid.raw);
		if (path->ah)
			path->ah->valid = 0;
	}

	spin_unlock_irq(&priv->lock);
}

static void push_pseudo_header(struct sk_buff *skb, const char *daddr)
{
	struct ipoib_pseudo_header *phdr;

	phdr = skb_push(skb, sizeof(*phdr));
	memcpy(phdr->hwaddr, daddr, INFINIBAND_ALEN);
}

void ipoib_flush_paths(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);
	struct ipoib_path *path, *tp;
	LIST_HEAD(remove_list);
	unsigned long flags;

	netif_tx_lock_bh(dev);
	spin_lock_irqsave(&priv->lock, flags);

	list_splice_init(&priv->path_list, &remove_list);

	list_for_each_entry(path, &remove_list, list)
		rb_erase(&path->rb_node, &priv->path_tree);

	list_for_each_entry_safe(path, tp, &remove_list, list) {
		if (path->query)
			ib_sa_cancel_query(path->query_id, path->query);
		spin_unlock_irqrestore(&priv->lock, flags);
		netif_tx_unlock_bh(dev);
		wait_for_completion(&path->done);
		path_free(dev, path);
		netif_tx_lock_bh(dev);
		spin_lock_irqsave(&priv->lock, flags);
	}

	spin_unlock_irqrestore(&priv->lock, flags);
	netif_tx_unlock_bh(dev);
}

static void path_rec_completion(int status,
				struct sa_path_rec *pathrec,
				void *path_ptr)
{
	struct ipoib_path *path = path_ptr;
	struct net_device *dev = path->dev;
	struct ipoib_dev_priv *priv = ipoib_priv(dev);
	struct ipoib_ah *ah = NULL;
	struct ipoib_ah *old_ah = NULL;
	struct ipoib_neigh *neigh, *tn;
	struct sk_buff_head skqueue;
	struct sk_buff *skb;
	unsigned long flags;

	if (!status)
		ipoib_dbg(priv, "PathRec LID 0x%04x for GID %pI6\n",
			  be32_to_cpu(sa_path_get_dlid(pathrec)),
			  pathrec->dgid.raw);
	else
		ipoib_dbg(priv, "PathRec status %d for GID %pI6\n",
			  status, path->pathrec.dgid.raw);

	skb_queue_head_init(&skqueue);

	if (!status) {
		struct rdma_ah_attr av;

		if (!ib_init_ah_attr_from_path(priv->ca, priv->port,
					       pathrec, &av, NULL)) {
			ah = ipoib_create_ah(dev, priv->pd, &av);
			rdma_destroy_ah_attr(&av);
		}
	}

	spin_lock_irqsave(&priv->lock, flags);

	if (!IS_ERR_OR_NULL(ah)) {
		/*
		 * pathrec.dgid is used as the database key from the LLADDR,
		 * it must remain unchanged even if the SA returns a different
		 * GID to use in the AH.
		 */
		if (memcmp(pathrec->dgid.raw, path->pathrec.dgid.raw,
			   sizeof(union ib_gid))) {
			ipoib_dbg(
				priv,
				"%s got PathRec for gid %pI6 while asked for %pI6\n",
				dev->name, pathrec->dgid.raw,
				path->pathrec.dgid.raw);
			memcpy(pathrec->dgid.raw, path->pathrec.dgid.raw,
			       sizeof(union ib_gid));
		}

		path->pathrec = *pathrec;

		old_ah   = path->ah;
		path->ah = ah;

		ipoib_dbg(priv, "created address handle %p for LID 0x%04x, SL %d\n",
			  ah, be32_to_cpu(sa_path_get_dlid(pathrec)),
			  pathrec->sl);

		while ((skb = __skb_dequeue(&path->queue)))
			__skb_queue_tail(&skqueue, skb);

		list_for_each_entry_safe(neigh, tn, &path->neigh_list, list) {
			if (neigh->ah) {
				WARN_ON(neigh->ah != old_ah);
				/*
				 * Dropping the ah reference inside
				 * priv->lock is safe here, because we
				 * will hold one more reference from
				 * the original value of path->ah (ie
				 * old_ah).
				 */
				ipoib_put_ah(neigh->ah);
			}
			kref_get(&path->ah->ref);
			neigh->ah = path->ah;

			if (ipoib_cm_enabled(dev, neigh->daddr)) {
				if (!ipoib_cm_get(neigh))
					ipoib_cm_set(neigh, ipoib_cm_create_tx(dev,
									       path,
									       neigh));
				if (!ipoib_cm_get(neigh)) {
					ipoib_neigh_free(neigh);
					continue;
				}
			}

			while ((skb = __skb_dequeue(&neigh->queue)))
				__skb_queue_tail(&skqueue, skb);
		}
		path->ah->valid = 1;
	}

	path->query = NULL;
	complete(&path->done);

	spin_unlock_irqrestore(&priv->lock, flags);

	if (IS_ERR_OR_NULL(ah))
		ipoib_del_neighs_by_gid(dev, path->pathrec.dgid.raw);

	if (old_ah)
		ipoib_put_ah(old_ah);

	while ((skb = __skb_dequeue(&skqueue))) {
		int ret;
		skb->dev = dev;
		ret = dev_queue_xmit(skb);
		if (ret)
			ipoib_warn(priv, "%s: dev_queue_xmit failed to re-queue packet, ret:%d\n",
				   __func__, ret);
	}
}

static void init_path_rec(struct ipoib_dev_priv *priv, struct ipoib_path *path,
			  void *gid)
{
	path->dev = priv->dev;

	if (rdma_cap_opa_ah(priv->ca, priv->port))
		path->pathrec.rec_type = SA_PATH_REC_TYPE_OPA;
	else
		path->pathrec.rec_type = SA_PATH_REC_TYPE_IB;

	memcpy(path->pathrec.dgid.raw, gid, sizeof(union ib_gid));
	path->pathrec.sgid	    = priv->local_gid;
	path->pathrec.pkey	    = cpu_to_be16(priv->pkey);
	path->pathrec.numb_path     = 1;
	path->pathrec.traffic_class = priv->broadcast->mcmember.traffic_class;
}

static struct ipoib_path *path_rec_create(struct net_device *dev, void *gid)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);
	struct ipoib_path *path;

	if (!priv->broadcast)
		return NULL;

	path = kzalloc(sizeof(*path), GFP_ATOMIC);
	if (!path)
		return NULL;

	skb_queue_head_init(&path->queue);

	INIT_LIST_HEAD(&path->neigh_list);

	init_path_rec(priv, path, gid);

	return path;
}

static int path_rec_start(struct net_device *dev,
			  struct ipoib_path *path)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);

	ipoib_dbg(priv, "Start path record lookup for %pI6\n",
		  path->pathrec.dgid.raw);

	init_completion(&path->done);

	path->query_id =
		ib_sa_path_rec_get(&ipoib_sa_client, priv->ca, priv->port,
				   &path->pathrec,
				   IB_SA_PATH_REC_DGID		|
				   IB_SA_PATH_REC_SGID		|
				   IB_SA_PATH_REC_NUMB_PATH	|
				   IB_SA_PATH_REC_TRAFFIC_CLASS |
				   IB_SA_PATH_REC_PKEY,
				   1000, GFP_ATOMIC,
				   path_rec_completion,
				   path, &path->query);
	if (path->query_id < 0) {
		ipoib_warn(priv, "ib_sa_path_rec_get failed: %d\n", path->query_id);
		path->query = NULL;
		complete(&path->done);
		return path->query_id;
	}

	return 0;
}

static void neigh_refresh_path(struct ipoib_neigh *neigh, u8 *daddr,
			       struct net_device *dev)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);
	struct ipoib_path *path;
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);

	path = __path_find(dev, daddr + 4);
	if (!path)
		goto out;
	if (!path->query)
		path_rec_start(dev, path);
out:
	spin_unlock_irqrestore(&priv->lock, flags);
}

static struct ipoib_neigh *neigh_add_path(struct sk_buff *skb, u8 *daddr,
					  struct net_device *dev)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);
	struct rdma_netdev *rn = netdev_priv(dev);
	struct ipoib_path *path;
	struct ipoib_neigh *neigh;
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);
	neigh = ipoib_neigh_alloc(daddr, dev);
	if (!neigh) {
		spin_unlock_irqrestore(&priv->lock, flags);
		++dev->stats.tx_dropped;
		dev_kfree_skb_any(skb);
		return NULL;
	}

	/* To avoid race condition, make sure that the
	 * neigh will be added only once.
	 */
	if (unlikely(!list_empty(&neigh->list))) {
		spin_unlock_irqrestore(&priv->lock, flags);
		return neigh;
	}

	path = __path_find(dev, daddr + 4);
	if (!path) {
		path = path_rec_create(dev, daddr + 4);
		if (!path)
			goto err_path;

		__path_add(dev, path);
	}

	list_add_tail(&neigh->list, &path->neigh_list);

	if (path->ah && path->ah->valid) {
		kref_get(&path->ah->ref);
		neigh->ah = path->ah;

		if (ipoib_cm_enabled(dev, neigh->daddr)) {
			if (!ipoib_cm_get(neigh))
				ipoib_cm_set(neigh, ipoib_cm_create_tx(dev, path, neigh));
			if (!ipoib_cm_get(neigh)) {
				ipoib_neigh_free(neigh);
				goto err_drop;
			}
			if (skb_queue_len(&neigh->queue) <
			    IPOIB_MAX_PATH_REC_QUEUE) {
				push_pseudo_header(skb, neigh->daddr);
				__skb_queue_tail(&neigh->queue, skb);
			} else {
				ipoib_warn(priv, "queue length limit %d. Packet drop.\n",
					   skb_queue_len(&neigh->queue));
				goto err_drop;
			}
		} else {
			spin_unlock_irqrestore(&priv->lock, flags);
			path->ah->last_send = rn->send(dev, skb, path->ah->ah,
						       IPOIB_QPN(daddr));
			ipoib_neigh_put(neigh);
			return NULL;
		}
	} else {
		neigh->ah  = NULL;

		if (!path->query && path_rec_start(dev, path))
			goto err_path;
		if (skb_queue_len(&neigh->queue) < IPOIB_MAX_PATH_REC_QUEUE) {
			push_pseudo_header(skb, neigh->daddr);
			__skb_queue_tail(&neigh->queue, skb);
		} else {
			goto err_drop;
		}
	}

	spin_unlock_irqrestore(&priv->lock, flags);
	ipoib_neigh_put(neigh);
	return NULL;

err_path:
	ipoib_neigh_free(neigh);
err_drop:
	++dev->stats.tx_dropped;
	dev_kfree_skb_any(skb);

	spin_unlock_irqrestore(&priv->lock, flags);
	ipoib_neigh_put(neigh);

	return NULL;
}

static void unicast_arp_send(struct sk_buff *skb, struct net_device *dev,
			     struct ipoib_pseudo_header *phdr)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);
	struct rdma_netdev *rn = netdev_priv(dev);
	struct ipoib_path *path;
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);

	/* no broadcast means that all paths are (going to be) not valid */
	if (!priv->broadcast)
		goto drop_and_unlock;

	path = __path_find(dev, phdr->hwaddr + 4);
	if (!path || !path->ah || !path->ah->valid) {
		if (!path) {
			path = path_rec_create(dev, phdr->hwaddr + 4);
			if (!path)
				goto drop_and_unlock;
			__path_add(dev, path);
		} else {
			/*
			 * make sure there are no changes in the existing
			 * path record
			 */
			init_path_rec(priv, path, phdr->hwaddr + 4);
		}
		if (!path->query && path_rec_start(dev, path)) {
			goto drop_and_unlock;
		}

		if (skb_queue_len(&path->queue) < IPOIB_MAX_PATH_REC_QUEUE) {
			push_pseudo_header(skb, phdr->hwaddr);
			__skb_queue_tail(&path->queue, skb);
			goto unlock;
		} else {
			goto drop_and_unlock;
		}
	}

	spin_unlock_irqrestore(&priv->lock, flags);
	ipoib_dbg(priv, "Send unicast ARP to %08x\n",
		  be32_to_cpu(sa_path_get_dlid(&path->pathrec)));
	path->ah->last_send = rn->send(dev, skb, path->ah->ah,
				       IPOIB_QPN(phdr->hwaddr));
	return;

drop_and_unlock:
	++dev->stats.tx_dropped;
	dev_kfree_skb_any(skb);
unlock:
	spin_unlock_irqrestore(&priv->lock, flags);
}

static netdev_tx_t ipoib_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);
	struct rdma_netdev *rn = netdev_priv(dev);
	struct ipoib_neigh *neigh;
	struct ipoib_pseudo_header *phdr;
	struct ipoib_header *header;
	unsigned long flags;

	phdr = (struct ipoib_pseudo_header *) skb->data;
	skb_pull(skb, sizeof(*phdr));
	header = (struct ipoib_header *) skb->data;

	if (unlikely(phdr->hwaddr[4] == 0xff)) {
		/* multicast, arrange "if" according to probability */
		if ((header->proto != htons(ETH_P_IP)) &&
		    (header->proto != htons(ETH_P_IPV6)) &&
		    (header->proto != htons(ETH_P_ARP)) &&
		    (header->proto != htons(ETH_P_RARP)) &&
		    (header->proto != htons(ETH_P_TIPC))) {
			/* ethertype not supported by IPoIB */
			++dev->stats.tx_dropped;
			dev_kfree_skb_any(skb);
			return NETDEV_TX_OK;
		}
		/* Add in the P_Key for multicast*/
		phdr->hwaddr[8] = (priv->pkey >> 8) & 0xff;
		phdr->hwaddr[9] = priv->pkey & 0xff;

		neigh = ipoib_neigh_get(dev, phdr->hwaddr);
		if (likely(neigh))
			goto send_using_neigh;
		ipoib_mcast_send(dev, phdr->hwaddr, skb);
		return NETDEV_TX_OK;
	}

	/* unicast, arrange "switch" according to probability */
	switch (header->proto) {
	case htons(ETH_P_IP):
	case htons(ETH_P_IPV6):
	case htons(ETH_P_TIPC):
		neigh = ipoib_neigh_get(dev, phdr->hwaddr);
		if (unlikely(!neigh)) {
			neigh = neigh_add_path(skb, phdr->hwaddr, dev);
			if (likely(!neigh))
				return NETDEV_TX_OK;
		}
		break;
	case htons(ETH_P_ARP):
	case htons(ETH_P_RARP):
		/* for unicast ARP and RARP should always perform path find */
		unicast_arp_send(skb, dev, phdr);
		return NETDEV_TX_OK;
	default:
		/* ethertype not supported by IPoIB */
		++dev->stats.tx_dropped;
		dev_kfree_skb_any(skb);
		return NETDEV_TX_OK;
	}

send_using_neigh:
	/* note we now hold a ref to neigh */
	if (ipoib_cm_get(neigh)) {
		if (ipoib_cm_up(neigh)) {
			ipoib_cm_send(dev, skb, ipoib_cm_get(neigh));
			goto unref;
		}
	} else if (neigh->ah && neigh->ah->valid) {
		neigh->ah->last_send = rn->send(dev, skb, neigh->ah->ah,
						IPOIB_QPN(phdr->hwaddr));
		goto unref;
	} else if (neigh->ah) {
		neigh_refresh_path(neigh, phdr->hwaddr, dev);
	}

	if (skb_queue_len(&neigh->queue) < IPOIB_MAX_PATH_REC_QUEUE) {
		push_pseudo_header(skb, phdr->hwaddr);
		spin_lock_irqsave(&priv->lock, flags);
		__skb_queue_tail(&neigh->queue, skb);
		spin_unlock_irqrestore(&priv->lock, flags);
	} else {
		++dev->stats.tx_dropped;
		dev_kfree_skb_any(skb);
	}

unref:
	ipoib_neigh_put(neigh);

	return NETDEV_TX_OK;
}

static void ipoib_timeout(struct net_device *dev, unsigned int txqueue)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);

	ipoib_warn(priv, "transmit timeout: latency %d msecs\n",
		   jiffies_to_msecs(jiffies - dev_trans_start(dev)));
	ipoib_warn(priv,
		   "queue stopped %d, tx_head %u, tx_tail %u, global_tx_head %u, global_tx_tail %u\n",
		   netif_queue_stopped(dev), priv->tx_head, priv->tx_tail,
		   priv->global_tx_head, priv->global_tx_tail);

	/* XXX reset QP, etc. */
}

static int ipoib_hard_header(struct sk_buff *skb,
			     struct net_device *dev,
			     unsigned short type,
			     const void *daddr,
			     const void *saddr,
			     unsigned int len)
{
	struct ipoib_header *header;

	header = skb_push(skb, sizeof(*header));

	header->proto = htons(type);
	header->reserved = 0;

	/*
	 * we don't rely on dst_entry structure,  always stuff the
	 * destination address into skb hard header so we can figure out where
	 * to send the packet later.
	 */
	push_pseudo_header(skb, daddr);

	return IPOIB_HARD_LEN;
}

static void ipoib_set_mcast_list(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);

	if (!test_bit(IPOIB_FLAG_OPER_UP, &priv->flags)) {
		ipoib_dbg(priv, "IPOIB_FLAG_OPER_UP not set");
		return;
	}

	queue_work(priv->wq, &priv->restart_task);
}

static int ipoib_get_iflink(const struct net_device *dev)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);

	/* parent interface */
	if (!test_bit(IPOIB_FLAG_SUBINTERFACE, &priv->flags))
		return dev->ifindex;

	/* child/vlan interface */
	return priv->parent->ifindex;
}

static u32 ipoib_addr_hash(struct ipoib_neigh_hash *htbl, u8 *daddr)
{
	/*
	 * Use only the address parts that contributes to spreading
	 * The subnet prefix is not used as one can not connect to
	 * same remote port (GUID) using the same remote QPN via two
	 * different subnets.
	 */
	 /* qpn octets[1:4) & port GUID octets[12:20) */
	u32 *d32 = (u32 *) daddr;
	u32 hv;

	hv = jhash_3words(d32[3], d32[4], IPOIB_QPN_MASK & d32[0], 0);
	return hv & htbl->mask;
}

struct ipoib_neigh *ipoib_neigh_get(struct net_device *dev, u8 *daddr)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);
	struct ipoib_neigh_table *ntbl = &priv->ntbl;
	struct ipoib_neigh_hash *htbl;
	struct ipoib_neigh *neigh = NULL;
	u32 hash_val;

	rcu_read_lock_bh();

	htbl = rcu_dereference_bh(ntbl->htbl);

	if (!htbl)
		goto out_unlock;

	hash_val = ipoib_addr_hash(htbl, daddr);
	for (neigh = rcu_dereference_bh(htbl->buckets[hash_val]);
	     neigh != NULL;
	     neigh = rcu_dereference_bh(neigh->hnext)) {
		if (memcmp(daddr, neigh->daddr, INFINIBAND_ALEN) == 0) {
			/* found, take one ref on behalf of the caller */
			if (!atomic_inc_not_zero(&neigh->refcnt)) {
				/* deleted */
				neigh = NULL;
				goto out_unlock;
			}

			if (likely(skb_queue_len(&neigh->queue) < IPOIB_MAX_PATH_REC_QUEUE))
				neigh->alive = jiffies;
			goto out_unlock;
		}
	}

out_unlock:
	rcu_read_unlock_bh();
	return neigh;
}

static void __ipoib_reap_neigh(struct ipoib_dev_priv *priv)
{
	struct ipoib_neigh_table *ntbl = &priv->ntbl;
	struct ipoib_neigh_hash *htbl;
	unsigned long neigh_obsolete;
	unsigned long dt;
	unsigned long flags;
	int i;
	LIST_HEAD(remove_list);

	spin_lock_irqsave(&priv->lock, flags);

	htbl = rcu_dereference_protected(ntbl->htbl,
					 lockdep_is_held(&priv->lock));

	if (!htbl)
		goto out_unlock;

	/* neigh is obsolete if it was idle for two GC periods */
	dt = 2 * arp_tbl.gc_interval;
	neigh_obsolete = jiffies - dt;

	for (i = 0; i < htbl->size; i++) {
		struct ipoib_neigh *neigh;
		struct ipoib_neigh __rcu **np = &htbl->buckets[i];

		while ((neigh = rcu_dereference_protected(*np,
							  lockdep_is_held(&priv->lock))) != NULL) {
			/* was the neigh idle for two GC periods */
			if (time_after(neigh_obsolete, neigh->alive)) {

				ipoib_check_and_add_mcast_sendonly(priv, neigh->daddr + 4, &remove_list);

				rcu_assign_pointer(*np,
						   rcu_dereference_protected(neigh->hnext,
									     lockdep_is_held(&priv->lock)));
				/* remove from path/mc list */
				list_del_init(&neigh->list);
				call_rcu(&neigh->rcu, ipoib_neigh_reclaim);
			} else {
				np = &neigh->hnext;
			}

		}
	}

out_unlock:
	spin_unlock_irqrestore(&priv->lock, flags);
	ipoib_mcast_remove_list(&remove_list);
}

static void ipoib_reap_neigh(struct work_struct *work)
{
	struct ipoib_dev_priv *priv =
		container_of(work, struct ipoib_dev_priv, neigh_reap_task.work);

	__ipoib_reap_neigh(priv);

	queue_delayed_work(priv->wq, &priv->neigh_reap_task,
			   arp_tbl.gc_interval);
}


static struct ipoib_neigh *ipoib_neigh_ctor(u8 *daddr,
				      struct net_device *dev)
{
	struct ipoib_neigh *neigh;

	neigh = kzalloc(sizeof(*neigh), GFP_ATOMIC);
	if (!neigh)
		return NULL;

	neigh->dev = dev;
	memcpy(&neigh->daddr, daddr, sizeof(neigh->daddr));
	skb_queue_head_init(&neigh->queue);
	INIT_LIST_HEAD(&neigh->list);
	ipoib_cm_set(neigh, NULL);
	/* one ref on behalf of the caller */
	atomic_set(&neigh->refcnt, 1);

	return neigh;
}

struct ipoib_neigh *ipoib_neigh_alloc(u8 *daddr,
				      struct net_device *dev)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);
	struct ipoib_neigh_table *ntbl = &priv->ntbl;
	struct ipoib_neigh_hash *htbl;
	struct ipoib_neigh *neigh;
	u32 hash_val;

	htbl = rcu_dereference_protected(ntbl->htbl,
					 lockdep_is_held(&priv->lock));
	if (!htbl) {
		neigh = NULL;
		goto out_unlock;
	}

	/* need to add a new neigh, but maybe some other thread succeeded?
	 * recalc hash, maybe hash resize took place so we do a search
	 */
	hash_val = ipoib_addr_hash(htbl, daddr);
	for (neigh = rcu_dereference_protected(htbl->buckets[hash_val],
					       lockdep_is_held(&priv->lock));
	     neigh != NULL;
	     neigh = rcu_dereference_protected(neigh->hnext,
					       lockdep_is_held(&priv->lock))) {
		if (memcmp(daddr, neigh->daddr, INFINIBAND_ALEN) == 0) {
			/* found, take one ref on behalf of the caller */
			if (!atomic_inc_not_zero(&neigh->refcnt)) {
				/* deleted */
				neigh = NULL;
				break;
			}
			neigh->alive = jiffies;
			goto out_unlock;
		}
	}

	neigh = ipoib_neigh_ctor(daddr, dev);
	if (!neigh)
		goto out_unlock;

	/* one ref on behalf of the hash table */
	atomic_inc(&neigh->refcnt);
	neigh->alive = jiffies;
	/* put in hash */
	rcu_assign_pointer(neigh->hnext,
			   rcu_dereference_protected(htbl->buckets[hash_val],
						     lockdep_is_held(&priv->lock)));
	rcu_assign_pointer(htbl->buckets[hash_val], neigh);
	atomic_inc(&ntbl->entries);

out_unlock:

	return neigh;
}

void ipoib_neigh_dtor(struct ipoib_neigh *neigh)
{
	/* neigh reference count was dropprd to zero */
	struct net_device *dev = neigh->dev;
	struct ipoib_dev_priv *priv = ipoib_priv(dev);
	struct sk_buff *skb;
	if (neigh->ah)
		ipoib_put_ah(neigh->ah);
	while ((skb = __skb_dequeue(&neigh->queue))) {
		++dev->stats.tx_dropped;
		dev_kfree_skb_any(skb);
	}
	if (ipoib_cm_get(neigh))
		ipoib_cm_destroy_tx(ipoib_cm_get(neigh));
	ipoib_dbg(ipoib_priv(dev),
		  "neigh free for %06x %pI6\n",
		  IPOIB_QPN(neigh->daddr),
		  neigh->daddr + 4);
	kfree(neigh);
	if (atomic_dec_and_test(&priv->ntbl.entries)) {
		if (test_bit(IPOIB_NEIGH_TBL_FLUSH, &priv->flags))
			complete(&priv->ntbl.flushed);
	}
}

static void ipoib_neigh_reclaim(struct rcu_head *rp)
{
	/* Called as a result of removal from hash table */
	struct ipoib_neigh *neigh = container_of(rp, struct ipoib_neigh, rcu);
	/* note TX context may hold another ref */
	ipoib_neigh_put(neigh);
}

void ipoib_neigh_free(struct ipoib_neigh *neigh)
{
	struct net_device *dev = neigh->dev;
	struct ipoib_dev_priv *priv = ipoib_priv(dev);
	struct ipoib_neigh_table *ntbl = &priv->ntbl;
	struct ipoib_neigh_hash *htbl;
	struct ipoib_neigh __rcu **np;
	struct ipoib_neigh *n;
	u32 hash_val;

	htbl = rcu_dereference_protected(ntbl->htbl,
					lockdep_is_held(&priv->lock));
	if (!htbl)
		return;

	hash_val = ipoib_addr_hash(htbl, neigh->daddr);
	np = &htbl->buckets[hash_val];
	for (n = rcu_dereference_protected(*np,
					    lockdep_is_held(&priv->lock));
	     n != NULL;
	     n = rcu_dereference_protected(*np,
					lockdep_is_held(&priv->lock))) {
		if (n == neigh) {
			/* found */
			rcu_assign_pointer(*np,
					   rcu_dereference_protected(neigh->hnext,
								     lockdep_is_held(&priv->lock)));
			/* remove from parent list */
			list_del_init(&neigh->list);
			call_rcu(&neigh->rcu, ipoib_neigh_reclaim);
			return;
		} else {
			np = &n->hnext;
		}
	}
}

static int ipoib_neigh_hash_init(struct ipoib_dev_priv *priv)
{
	struct ipoib_neigh_table *ntbl = &priv->ntbl;
	struct ipoib_neigh_hash *htbl;
	struct ipoib_neigh __rcu **buckets;
	u32 size;

	clear_bit(IPOIB_NEIGH_TBL_FLUSH, &priv->flags);
	ntbl->htbl = NULL;
	htbl = kzalloc(sizeof(*htbl), GFP_KERNEL);
	if (!htbl)
		return -ENOMEM;
	size = roundup_pow_of_two(arp_tbl.gc_thresh3);
	buckets = kvcalloc(size, sizeof(*buckets), GFP_KERNEL);
	if (!buckets) {
		kfree(htbl);
		return -ENOMEM;
	}
	htbl->size = size;
	htbl->mask = (size - 1);
	htbl->buckets = buckets;
	RCU_INIT_POINTER(ntbl->htbl, htbl);
	htbl->ntbl = ntbl;
	atomic_set(&ntbl->entries, 0);

	/* start garbage collection */
	queue_delayed_work(priv->wq, &priv->neigh_reap_task,
			   arp_tbl.gc_interval);

	return 0;
}

static void neigh_hash_free_rcu(struct rcu_head *head)
{
	struct ipoib_neigh_hash *htbl = container_of(head,
						    struct ipoib_neigh_hash,
						    rcu);
	struct ipoib_neigh __rcu **buckets = htbl->buckets;
	struct ipoib_neigh_table *ntbl = htbl->ntbl;

	kvfree(buckets);
	kfree(htbl);
	complete(&ntbl->deleted);
}

void ipoib_del_neighs_by_gid(struct net_device *dev, u8 *gid)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);
	struct ipoib_neigh_table *ntbl = &priv->ntbl;
	struct ipoib_neigh_hash *htbl;
	unsigned long flags;
	int i;

	/* remove all neigh connected to a given path or mcast */
	spin_lock_irqsave(&priv->lock, flags);

	htbl = rcu_dereference_protected(ntbl->htbl,
					 lockdep_is_held(&priv->lock));

	if (!htbl)
		goto out_unlock;

	for (i = 0; i < htbl->size; i++) {
		struct ipoib_neigh *neigh;
		struct ipoib_neigh __rcu **np = &htbl->buckets[i];

		while ((neigh = rcu_dereference_protected(*np,
							  lockdep_is_held(&priv->lock))) != NULL) {
			/* delete neighs belong to this parent */
			if (!memcmp(gid, neigh->daddr + 4, sizeof (union ib_gid))) {
				rcu_assign_pointer(*np,
						   rcu_dereference_protected(neigh->hnext,
									     lockdep_is_held(&priv->lock)));
				/* remove from parent list */
				list_del_init(&neigh->list);
				call_rcu(&neigh->rcu, ipoib_neigh_reclaim);
			} else {
				np = &neigh->hnext;
			}

		}
	}
out_unlock:
	spin_unlock_irqrestore(&priv->lock, flags);
}

static void ipoib_flush_neighs(struct ipoib_dev_priv *priv)
{
	struct ipoib_neigh_table *ntbl = &priv->ntbl;
	struct ipoib_neigh_hash *htbl;
	unsigned long flags;
	int i, wait_flushed = 0;

	init_completion(&priv->ntbl.flushed);
	set_bit(IPOIB_NEIGH_TBL_FLUSH, &priv->flags);

	spin_lock_irqsave(&priv->lock, flags);

	htbl = rcu_dereference_protected(ntbl->htbl,
					lockdep_is_held(&priv->lock));
	if (!htbl)
		goto out_unlock;

	wait_flushed = atomic_read(&priv->ntbl.entries);
	if (!wait_flushed)
		goto free_htbl;

	for (i = 0; i < htbl->size; i++) {
		struct ipoib_neigh *neigh;
		struct ipoib_neigh __rcu **np = &htbl->buckets[i];

		while ((neigh = rcu_dereference_protected(*np,
				       lockdep_is_held(&priv->lock))) != NULL) {
			rcu_assign_pointer(*np,
					   rcu_dereference_protected(neigh->hnext,
								     lockdep_is_held(&priv->lock)));
			/* remove from path/mc list */
			list_del_init(&neigh->list);
			call_rcu(&neigh->rcu, ipoib_neigh_reclaim);
		}
	}

free_htbl:
	rcu_assign_pointer(ntbl->htbl, NULL);
	call_rcu(&htbl->rcu, neigh_hash_free_rcu);

out_unlock:
	spin_unlock_irqrestore(&priv->lock, flags);
	if (wait_flushed)
		wait_for_completion(&priv->ntbl.flushed);
}

static void ipoib_neigh_hash_uninit(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);

	ipoib_dbg(priv, "%s\n", __func__);
	init_completion(&priv->ntbl.deleted);

	cancel_delayed_work_sync(&priv->neigh_reap_task);

	ipoib_flush_neighs(priv);

	wait_for_completion(&priv->ntbl.deleted);
}

static void ipoib_napi_add(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);

	netif_napi_add(dev, &priv->recv_napi, ipoib_rx_poll, IPOIB_NUM_WC);
	netif_napi_add(dev, &priv->send_napi, ipoib_tx_poll, MAX_SEND_CQE);
}

static void ipoib_napi_del(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);

	netif_napi_del(&priv->recv_napi);
	netif_napi_del(&priv->send_napi);
}

static void ipoib_dev_uninit_default(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);

	ipoib_transport_dev_cleanup(dev);

	ipoib_napi_del(dev);

	ipoib_cm_dev_cleanup(dev);

	kfree(priv->rx_ring);
	vfree(priv->tx_ring);

	priv->rx_ring = NULL;
	priv->tx_ring = NULL;
}

static int ipoib_dev_init_default(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);

	ipoib_napi_add(dev);

	/* Allocate RX/TX "rings" to hold queued skbs */
	priv->rx_ring =	kcalloc(ipoib_recvq_size,
				       sizeof(*priv->rx_ring),
				       GFP_KERNEL);
	if (!priv->rx_ring)
		goto out;

	priv->tx_ring = vzalloc(array_size(ipoib_sendq_size,
					   sizeof(*priv->tx_ring)));
	if (!priv->tx_ring) {
		pr_warn("%s: failed to allocate TX ring (%d entries)\n",
			priv->ca->name, ipoib_sendq_size);
		goto out_rx_ring_cleanup;
	}

	/* priv->tx_head, tx_tail and global_tx_tail/head are already 0 */

	if (ipoib_transport_dev_init(dev, priv->ca)) {
		pr_warn("%s: ipoib_transport_dev_init failed\n",
			priv->ca->name);
		goto out_tx_ring_cleanup;
	}

	/* after qp created set dev address */
	priv->dev->dev_addr[1] = (priv->qp->qp_num >> 16) & 0xff;
	priv->dev->dev_addr[2] = (priv->qp->qp_num >>  8) & 0xff;
	priv->dev->dev_addr[3] = (priv->qp->qp_num) & 0xff;

	return 0;

out_tx_ring_cleanup:
	vfree(priv->tx_ring);

out_rx_ring_cleanup:
	kfree(priv->rx_ring);

out:
	ipoib_napi_del(dev);
	return -ENOMEM;
}

static int ipoib_ioctl(struct net_device *dev, struct ifreq *ifr,
		       int cmd)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);

	if (!priv->rn_ops->ndo_do_ioctl)
		return -EOPNOTSUPP;

	return priv->rn_ops->ndo_do_ioctl(dev, ifr, cmd);
}

static int ipoib_dev_init(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);
	int ret = -ENOMEM;

	priv->qp = NULL;

	/*
	 * the various IPoIB tasks assume they will never race against
	 * themselves, so always use a single thread workqueue
	 */
	priv->wq = alloc_ordered_workqueue("ipoib_wq", WQ_MEM_RECLAIM);
	if (!priv->wq) {
		pr_warn("%s: failed to allocate device WQ\n", dev->name);
		goto out;
	}

	/* create pd, which used both for control and datapath*/
	priv->pd = ib_alloc_pd(priv->ca, 0);
	if (IS_ERR(priv->pd)) {
		pr_warn("%s: failed to allocate PD\n", priv->ca->name);
		goto clean_wq;
	}

	ret = priv->rn_ops->ndo_init(dev);
	if (ret) {
		pr_warn("%s failed to init HW resource\n", dev->name);
		goto out_free_pd;
	}

	ret = ipoib_neigh_hash_init(priv);
	if (ret) {
		pr_warn("%s failed to init neigh hash\n", dev->name);
		goto out_dev_uninit;
	}

	if (dev->flags & IFF_UP) {
		if (ipoib_ib_dev_open(dev)) {
			pr_warn("%s failed to open device\n", dev->name);
			ret = -ENODEV;
			goto out_hash_uninit;
		}
	}

	return 0;

out_hash_uninit:
	ipoib_neigh_hash_uninit(dev);

out_dev_uninit:
	ipoib_ib_dev_cleanup(dev);

out_free_pd:
	if (priv->pd) {
		ib_dealloc_pd(priv->pd);
		priv->pd = NULL;
	}

clean_wq:
	if (priv->wq) {
		destroy_workqueue(priv->wq);
		priv->wq = NULL;
	}

out:
	return ret;
}

/*
 * This must be called before doing an unregister_netdev on a parent device to
 * shutdown the IB event handler.
 */
static void ipoib_parent_unregister_pre(struct net_device *ndev)
{
	struct ipoib_dev_priv *priv = ipoib_priv(ndev);

	/*
	 * ipoib_set_mac checks netif_running before pushing work, clearing
	 * running ensures the it will not add more work.
	 */
	rtnl_lock();
	dev_change_flags(priv->dev, priv->dev->flags & ~IFF_UP, NULL);
	rtnl_unlock();

	/* ipoib_event() cannot be running once this returns */
	ib_unregister_event_handler(&priv->event_handler);

	/*
	 * Work on the queue grabs the rtnl lock, so this cannot be done while
	 * also holding it.
	 */
	flush_workqueue(ipoib_workqueue);
}

static void ipoib_set_dev_features(struct ipoib_dev_priv *priv)
{
	priv->hca_caps = priv->ca->attrs.device_cap_flags;

	if (priv->hca_caps & IB_DEVICE_UD_IP_CSUM) {
		priv->dev->hw_features |= NETIF_F_IP_CSUM | NETIF_F_RXCSUM;

		if (priv->hca_caps & IB_DEVICE_UD_TSO)
			priv->dev->hw_features |= NETIF_F_TSO;

		priv->dev->features |= priv->dev->hw_features;
	}
}

static int ipoib_parent_init(struct net_device *ndev)
{
	struct ipoib_dev_priv *priv = ipoib_priv(ndev);
	struct ib_port_attr attr;
	int result;

	result = ib_query_port(priv->ca, priv->port, &attr);
	if (result) {
		pr_warn("%s: ib_query_port %d failed\n", priv->ca->name,
			priv->port);
		return result;
	}
	priv->max_ib_mtu = rdma_mtu_from_attr(priv->ca, priv->port, &attr);

	result = ib_query_pkey(priv->ca, priv->port, 0, &priv->pkey);
	if (result) {
		pr_warn("%s: ib_query_pkey port %d failed (ret = %d)\n",
			priv->ca->name, priv->port, result);
		return result;
	}

	result = rdma_query_gid(priv->ca, priv->port, 0, &priv->local_gid);
	if (result) {
		pr_warn("%s: rdma_query_gid port %d failed (ret = %d)\n",
			priv->ca->name, priv->port, result);
		return result;
	}
	memcpy(priv->dev->dev_addr + 4, priv->local_gid.raw,
	       sizeof(union ib_gid));

	SET_NETDEV_DEV(priv->dev, priv->ca->dev.parent);
	priv->dev->dev_port = priv->port - 1;
	/* Let's set this one too for backwards compatibility. */
	priv->dev->dev_id = priv->port - 1;

	return 0;
}

static void ipoib_child_init(struct net_device *ndev)
{
	struct ipoib_dev_priv *priv = ipoib_priv(ndev);
	struct ipoib_dev_priv *ppriv = ipoib_priv(priv->parent);

	priv->max_ib_mtu = ppriv->max_ib_mtu;
	set_bit(IPOIB_FLAG_SUBINTERFACE, &priv->flags);
	if (memchr_inv(priv->dev->dev_addr, 0, INFINIBAND_ALEN))
		memcpy(&priv->local_gid, priv->dev->dev_addr + 4,
		       sizeof(priv->local_gid));
	else {
		memcpy(priv->dev->dev_addr, ppriv->dev->dev_addr,
		       INFINIBAND_ALEN);
		memcpy(&priv->local_gid, &ppriv->local_gid,
		       sizeof(priv->local_gid));
	}
}

static int ipoib_ndo_init(struct net_device *ndev)
{
	struct ipoib_dev_priv *priv = ipoib_priv(ndev);
	int rc;
	struct rdma_netdev *rn = netdev_priv(ndev);

	if (priv->parent) {
		ipoib_child_init(ndev);
	} else {
		rc = ipoib_parent_init(ndev);
		if (rc)
			return rc;
	}

	/* MTU will be reset when mcast join happens */
	ndev->mtu = IPOIB_UD_MTU(priv->max_ib_mtu);
	priv->mcast_mtu = priv->admin_mtu = ndev->mtu;
	rn->mtu = priv->mcast_mtu;
	ndev->max_mtu = IPOIB_CM_MTU;

	ndev->neigh_priv_len = sizeof(struct ipoib_neigh);

	/*
	 * Set the full membership bit, so that we join the right
	 * broadcast group, etc.
	 */
	priv->pkey |= 0x8000;

	ndev->broadcast[8] = priv->pkey >> 8;
	ndev->broadcast[9] = priv->pkey & 0xff;
	set_bit(IPOIB_FLAG_DEV_ADDR_SET, &priv->flags);

	ipoib_set_dev_features(priv);

	rc = ipoib_dev_init(ndev);
	if (rc) {
		pr_warn("%s: failed to initialize device: %s port %d (ret = %d)\n",
			priv->ca->name, priv->dev->name, priv->port, rc);
		return rc;
	}

	if (priv->parent) {
		struct ipoib_dev_priv *ppriv = ipoib_priv(priv->parent);

		dev_hold(priv->parent);

		down_write(&ppriv->vlan_rwsem);
		list_add_tail(&priv->list, &ppriv->child_intfs);
		up_write(&ppriv->vlan_rwsem);
	}

	return 0;
}

static void ipoib_ndo_uninit(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);

	ASSERT_RTNL();

	/*
	 * ipoib_remove_one guarantees the children are removed before the
	 * parent, and that is the only place where a parent can be removed.
	 */
	WARN_ON(!list_empty(&priv->child_intfs));

	if (priv->parent) {
		struct ipoib_dev_priv *ppriv = ipoib_priv(priv->parent);

		down_write(&ppriv->vlan_rwsem);
		list_del(&priv->list);
		up_write(&ppriv->vlan_rwsem);
	}

	ipoib_neigh_hash_uninit(dev);

	ipoib_ib_dev_cleanup(dev);

	/* no more works over the priv->wq */
	if (priv->wq) {
		/* See ipoib_mcast_carrier_on_task() */
		WARN_ON(test_bit(IPOIB_FLAG_OPER_UP, &priv->flags));
		flush_workqueue(priv->wq);
		destroy_workqueue(priv->wq);
		priv->wq = NULL;
	}

	if (priv->parent)
		dev_put(priv->parent);
}

static int ipoib_set_vf_link_state(struct net_device *dev, int vf, int link_state)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);

	return ib_set_vf_link_state(priv->ca, vf, priv->port, link_state);
}

static int ipoib_get_vf_config(struct net_device *dev, int vf,
			       struct ifla_vf_info *ivf)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);
	int err;

	err = ib_get_vf_config(priv->ca, vf, priv->port, ivf);
	if (err)
		return err;

	ivf->vf = vf;
	memcpy(ivf->mac, dev->dev_addr, dev->addr_len);

	return 0;
}

static int ipoib_set_vf_guid(struct net_device *dev, int vf, u64 guid, int type)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);

	if (type != IFLA_VF_IB_NODE_GUID && type != IFLA_VF_IB_PORT_GUID)
		return -EINVAL;

	return ib_set_vf_guid(priv->ca, vf, priv->port, guid, type);
}

static int ipoib_get_vf_guid(struct net_device *dev, int vf,
			     struct ifla_vf_guid *node_guid,
			     struct ifla_vf_guid *port_guid)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);

	return ib_get_vf_guid(priv->ca, vf, priv->port, node_guid, port_guid);
}

static int ipoib_get_vf_stats(struct net_device *dev, int vf,
			      struct ifla_vf_stats *vf_stats)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);

	return ib_get_vf_stats(priv->ca, vf, priv->port, vf_stats);
}

static const struct header_ops ipoib_header_ops = {
	.create	= ipoib_hard_header,
};

static const struct net_device_ops ipoib_netdev_ops_pf = {
	.ndo_init		 = ipoib_ndo_init,
	.ndo_uninit		 = ipoib_ndo_uninit,
	.ndo_open		 = ipoib_open,
	.ndo_stop		 = ipoib_stop,
	.ndo_change_mtu		 = ipoib_change_mtu,
	.ndo_fix_features	 = ipoib_fix_features,
	.ndo_start_xmit		 = ipoib_start_xmit,
	.ndo_tx_timeout		 = ipoib_timeout,
	.ndo_set_rx_mode	 = ipoib_set_mcast_list,
	.ndo_get_iflink		 = ipoib_get_iflink,
	.ndo_set_vf_link_state	 = ipoib_set_vf_link_state,
	.ndo_get_vf_config	 = ipoib_get_vf_config,
	.ndo_get_vf_stats	 = ipoib_get_vf_stats,
	.ndo_get_vf_guid	 = ipoib_get_vf_guid,
	.ndo_set_vf_guid	 = ipoib_set_vf_guid,
	.ndo_set_mac_address	 = ipoib_set_mac,
	.ndo_get_stats64	 = ipoib_get_stats,
	.ndo_do_ioctl		 = ipoib_ioctl,
};

static const struct net_device_ops ipoib_netdev_ops_vf = {
	.ndo_init		 = ipoib_ndo_init,
	.ndo_uninit		 = ipoib_ndo_uninit,
	.ndo_open		 = ipoib_open,
	.ndo_stop		 = ipoib_stop,
	.ndo_change_mtu		 = ipoib_change_mtu,
	.ndo_fix_features	 = ipoib_fix_features,
	.ndo_start_xmit	 	 = ipoib_start_xmit,
	.ndo_tx_timeout		 = ipoib_timeout,
	.ndo_set_rx_mode	 = ipoib_set_mcast_list,
	.ndo_get_iflink		 = ipoib_get_iflink,
	.ndo_get_stats64	 = ipoib_get_stats,
	.ndo_do_ioctl		 = ipoib_ioctl,
};

static const struct net_device_ops ipoib_netdev_default_pf = {
	.ndo_init		 = ipoib_dev_init_default,
	.ndo_uninit		 = ipoib_dev_uninit_default,
	.ndo_open		 = ipoib_ib_dev_open_default,
	.ndo_stop		 = ipoib_ib_dev_stop_default,
};

void ipoib_setup_common(struct net_device *dev)
{
	dev->header_ops		 = &ipoib_header_ops;
	dev->netdev_ops          = &ipoib_netdev_default_pf;

	ipoib_set_ethtool_ops(dev);

	dev->watchdog_timeo	 = HZ;

	dev->flags		|= IFF_BROADCAST | IFF_MULTICAST;

	dev->hard_header_len	 = IPOIB_HARD_LEN;
	dev->addr_len		 = INFINIBAND_ALEN;
	dev->type		 = ARPHRD_INFINIBAND;
	dev->tx_queue_len	 = ipoib_sendq_size * 2;
	dev->features		 = (NETIF_F_VLAN_CHALLENGED	|
				    NETIF_F_HIGHDMA);
	netif_keep_dst(dev);

	memcpy(dev->broadcast, ipv4_bcast_addr, INFINIBAND_ALEN);

	/*
	 * unregister_netdev always frees the netdev, we use this mode
	 * consistently to unify all the various unregister paths, including
	 * those connected to rtnl_link_ops which require it.
	 */
	dev->needs_free_netdev = true;
}

static void ipoib_build_priv(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);

	priv->dev = dev;
	spin_lock_init(&priv->lock);
	init_rwsem(&priv->vlan_rwsem);
	mutex_init(&priv->mcast_mutex);

	INIT_LIST_HEAD(&priv->path_list);
	INIT_LIST_HEAD(&priv->child_intfs);
	INIT_LIST_HEAD(&priv->dead_ahs);
	INIT_LIST_HEAD(&priv->multicast_list);

	INIT_DELAYED_WORK(&priv->mcast_task,   ipoib_mcast_join_task);
	INIT_WORK(&priv->carrier_on_task, ipoib_mcast_carrier_on_task);
	INIT_WORK(&priv->flush_light,   ipoib_ib_dev_flush_light);
	INIT_WORK(&priv->flush_normal,   ipoib_ib_dev_flush_normal);
	INIT_WORK(&priv->flush_heavy,   ipoib_ib_dev_flush_heavy);
	INIT_WORK(&priv->restart_task, ipoib_mcast_restart_task);
	INIT_DELAYED_WORK(&priv->ah_reap_task, ipoib_reap_ah);
	INIT_DELAYED_WORK(&priv->neigh_reap_task, ipoib_reap_neigh);
}

static struct net_device *ipoib_alloc_netdev(struct ib_device *hca, u8 port,
					     const char *name)
{
	struct net_device *dev;

	dev = rdma_alloc_netdev(hca, port, RDMA_NETDEV_IPOIB, name,
				NET_NAME_UNKNOWN, ipoib_setup_common);
	if (!IS_ERR(dev) || PTR_ERR(dev) != -EOPNOTSUPP)
		return dev;

	dev = alloc_netdev(sizeof(struct rdma_netdev), name, NET_NAME_UNKNOWN,
			   ipoib_setup_common);
	if (!dev)
		return ERR_PTR(-ENOMEM);
	return dev;
}

int ipoib_intf_init(struct ib_device *hca, u8 port, const char *name,
		    struct net_device *dev)
{
	struct rdma_netdev *rn = netdev_priv(dev);
	struct ipoib_dev_priv *priv;
	int rc;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->ca = hca;
	priv->port = port;

	rc = rdma_init_netdev(hca, port, RDMA_NETDEV_IPOIB, name,
			      NET_NAME_UNKNOWN, ipoib_setup_common, dev);
	if (rc) {
		if (rc != -EOPNOTSUPP)
			goto out;

		rn->send = ipoib_send;
		rn->attach_mcast = ipoib_mcast_attach;
		rn->detach_mcast = ipoib_mcast_detach;
		rn->hca = hca;

		rc = netif_set_real_num_tx_queues(dev, 1);
		if (rc)
			goto out;

		rc = netif_set_real_num_rx_queues(dev, 1);
		if (rc)
			goto out;
	}

	priv->rn_ops = dev->netdev_ops;

	if (hca->attrs.device_cap_flags & IB_DEVICE_VIRTUAL_FUNCTION)
		dev->netdev_ops	= &ipoib_netdev_ops_vf;
	else
		dev->netdev_ops	= &ipoib_netdev_ops_pf;

	rn->clnt_priv = priv;
	/*
	 * Only the child register_netdev flows can handle priv_destructor
	 * being set, so we force it to NULL here and handle manually until it
	 * is safe to turn on.
	 */
	priv->next_priv_destructor = dev->priv_destructor;
	dev->priv_destructor = NULL;

	ipoib_build_priv(dev);

	return 0;

out:
	kfree(priv);
	return rc;
}

struct net_device *ipoib_intf_alloc(struct ib_device *hca, u8 port,
				    const char *name)
{
	struct net_device *dev;
	int rc;

	dev = ipoib_alloc_netdev(hca, port, name);
	if (IS_ERR(dev))
		return dev;

	rc = ipoib_intf_init(hca, port, name, dev);
	if (rc) {
		free_netdev(dev);
		return ERR_PTR(rc);
	}

	/*
	 * Upon success the caller must ensure ipoib_intf_free is called or
	 * register_netdevice succeed'd and priv_destructor is set to
	 * ipoib_intf_free.
	 */
	return dev;
}

void ipoib_intf_free(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);
	struct rdma_netdev *rn = netdev_priv(dev);

	dev->priv_destructor = priv->next_priv_destructor;
	if (dev->priv_destructor)
		dev->priv_destructor(dev);

	/*
	 * There are some error flows around register_netdev failing that may
	 * attempt to call priv_destructor twice, prevent that from happening.
	 */
	dev->priv_destructor = NULL;

	/* unregister/destroy is very complicated. Make bugs more obvious. */
	rn->clnt_priv = NULL;

	kfree(priv);
}

static ssize_t show_pkey(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	struct net_device *ndev = to_net_dev(dev);
	struct ipoib_dev_priv *priv = ipoib_priv(ndev);

	return sprintf(buf, "0x%04x\n", priv->pkey);
}
static DEVICE_ATTR(pkey, S_IRUGO, show_pkey, NULL);

static ssize_t show_umcast(struct device *dev,
			   struct device_attribute *attr, char *buf)
{
	struct net_device *ndev = to_net_dev(dev);
	struct ipoib_dev_priv *priv = ipoib_priv(ndev);

	return sprintf(buf, "%d\n", test_bit(IPOIB_FLAG_UMCAST, &priv->flags));
}

void ipoib_set_umcast(struct net_device *ndev, int umcast_val)
{
	struct ipoib_dev_priv *priv = ipoib_priv(ndev);

	if (umcast_val > 0) {
		set_bit(IPOIB_FLAG_UMCAST, &priv->flags);
		ipoib_warn(priv, "ignoring multicast groups joined directly "
				"by userspace\n");
	} else
		clear_bit(IPOIB_FLAG_UMCAST, &priv->flags);
}

static ssize_t set_umcast(struct device *dev,
			  struct device_attribute *attr,
			  const char *buf, size_t count)
{
	unsigned long umcast_val = simple_strtoul(buf, NULL, 0);

	ipoib_set_umcast(to_net_dev(dev), umcast_val);

	return count;
}
static DEVICE_ATTR(umcast, S_IWUSR | S_IRUGO, show_umcast, set_umcast);

int ipoib_add_umcast_attr(struct net_device *dev)
{
	return device_create_file(&dev->dev, &dev_attr_umcast);
}

static void set_base_guid(struct ipoib_dev_priv *priv, union ib_gid *gid)
{
	struct ipoib_dev_priv *child_priv;
	struct net_device *netdev = priv->dev;

	netif_addr_lock_bh(netdev);

	memcpy(&priv->local_gid.global.interface_id,
	       &gid->global.interface_id,
	       sizeof(gid->global.interface_id));
	memcpy(netdev->dev_addr + 4, &priv->local_gid, sizeof(priv->local_gid));
	clear_bit(IPOIB_FLAG_DEV_ADDR_SET, &priv->flags);

	netif_addr_unlock_bh(netdev);

	if (!test_bit(IPOIB_FLAG_SUBINTERFACE, &priv->flags)) {
		down_read(&priv->vlan_rwsem);
		list_for_each_entry(child_priv, &priv->child_intfs, list)
			set_base_guid(child_priv, gid);
		up_read(&priv->vlan_rwsem);
	}
}

static int ipoib_check_lladdr(struct net_device *dev,
			      struct sockaddr_storage *ss)
{
	union ib_gid *gid = (union ib_gid *)(ss->__data + 4);
	int ret = 0;

	netif_addr_lock_bh(dev);

	/* Make sure the QPN, reserved and subnet prefix match the current
	 * lladdr, it also makes sure the lladdr is unicast.
	 */
	if (memcmp(dev->dev_addr, ss->__data,
		   4 + sizeof(gid->global.subnet_prefix)) ||
	    gid->global.interface_id == 0)
		ret = -EINVAL;

	netif_addr_unlock_bh(dev);

	return ret;
}

static int ipoib_set_mac(struct net_device *dev, void *addr)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);
	struct sockaddr_storage *ss = addr;
	int ret;

	if (!(dev->priv_flags & IFF_LIVE_ADDR_CHANGE) && netif_running(dev))
		return -EBUSY;

	ret = ipoib_check_lladdr(dev, ss);
	if (ret)
		return ret;

	set_base_guid(priv, (union ib_gid *)(ss->__data + 4));

	queue_work(ipoib_workqueue, &priv->flush_light);

	return 0;
}

static ssize_t create_child(struct device *dev,
			    struct device_attribute *attr,
			    const char *buf, size_t count)
{
	int pkey;
	int ret;

	if (sscanf(buf, "%i", &pkey) != 1)
		return -EINVAL;

	if (pkey <= 0 || pkey > 0xffff || pkey == 0x8000)
		return -EINVAL;

	ret = ipoib_vlan_add(to_net_dev(dev), pkey);

	return ret ? ret : count;
}
static DEVICE_ATTR(create_child, S_IWUSR, NULL, create_child);

static ssize_t delete_child(struct device *dev,
			    struct device_attribute *attr,
			    const char *buf, size_t count)
{
	int pkey;
	int ret;

	if (sscanf(buf, "%i", &pkey) != 1)
		return -EINVAL;

	if (pkey < 0 || pkey > 0xffff)
		return -EINVAL;

	ret = ipoib_vlan_delete(to_net_dev(dev), pkey);

	return ret ? ret : count;

}
static DEVICE_ATTR(delete_child, S_IWUSR, NULL, delete_child);

int ipoib_add_pkey_attr(struct net_device *dev)
{
	return device_create_file(&dev->dev, &dev_attr_pkey);
}

/*
 * We erroneously exposed the iface's port number in the dev_id
 * sysfs field long after dev_port was introduced for that purpose[1],
 * and we need to stop everyone from relying on that.
 * Let's overload the shower routine for the dev_id file here
 * to gently bring the issue up.
 *
 * [1] https://www.spinics.net/lists/netdev/msg272123.html
 */
static ssize_t dev_id_show(struct device *dev,
			   struct device_attribute *attr, char *buf)
{
	struct net_device *ndev = to_net_dev(dev);

	/*
	 * ndev->dev_port will be equal to 0 in old kernel prior to commit
	 * 9b8b2a323008 ("IB/ipoib: Use dev_port to expose network interface
	 * port numbers") Zero was chosen as special case for user space
	 * applications to fallback and query dev_id to check if it has
	 * different value or not.
	 *
	 * Don't print warning in such scenario.
	 *
	 * https://github.com/systemd/systemd/blob/master/src/udev/udev-builtin-net_id.c#L358
	 */
	if (ndev->dev_port && ndev->dev_id == ndev->dev_port)
		netdev_info_once(ndev,
			"\"%s\" wants to know my dev_id. Should it look at dev_port instead? See Documentation/ABI/testing/sysfs-class-net for more info.\n",
			current->comm);

	return sprintf(buf, "%#x\n", ndev->dev_id);
}
static DEVICE_ATTR_RO(dev_id);

static int ipoib_intercept_dev_id_attr(struct net_device *dev)
{
	device_remove_file(&dev->dev, &dev_attr_dev_id);
	return device_create_file(&dev->dev, &dev_attr_dev_id);
}

static struct net_device *ipoib_add_port(const char *format,
					 struct ib_device *hca, u8 port)
{
	struct rtnl_link_ops *ops = ipoib_get_link_ops();
	struct rdma_netdev_alloc_params params;
	struct ipoib_dev_priv *priv;
	struct net_device *ndev;
	int result;

	ndev = ipoib_intf_alloc(hca, port, format);
	if (IS_ERR(ndev)) {
		pr_warn("%s, %d: ipoib_intf_alloc failed %ld\n", hca->name, port,
			PTR_ERR(ndev));
		return ndev;
	}
	priv = ipoib_priv(ndev);

	INIT_IB_EVENT_HANDLER(&priv->event_handler,
			      priv->ca, ipoib_event);
	ib_register_event_handler(&priv->event_handler);

	/* call event handler to ensure pkey in sync */
	queue_work(ipoib_workqueue, &priv->flush_heavy);

	ndev->rtnl_link_ops = ipoib_get_link_ops();

	result = register_netdev(ndev);
	if (result) {
		pr_warn("%s: couldn't register ipoib port %d; error %d\n",
			hca->name, port, result);

		ipoib_parent_unregister_pre(ndev);
		ipoib_intf_free(ndev);
		free_netdev(ndev);

		return ERR_PTR(result);
	}

	if (hca->ops.rdma_netdev_get_params) {
		int rc = hca->ops.rdma_netdev_get_params(hca, port,
						     RDMA_NETDEV_IPOIB,
						     &params);

		if (!rc && ops->priv_size < params.sizeof_priv)
			ops->priv_size = params.sizeof_priv;
	}
	/*
	 * We cannot set priv_destructor before register_netdev because we
	 * need priv to be always valid during the error flow to execute
	 * ipoib_parent_unregister_pre(). Instead handle it manually and only
	 * enter priv_destructor mode once we are completely registered.
	 */
	ndev->priv_destructor = ipoib_intf_free;

	if (ipoib_intercept_dev_id_attr(ndev))
		goto sysfs_failed;
	if (ipoib_cm_add_mode_attr(ndev))
		goto sysfs_failed;
	if (ipoib_add_pkey_attr(ndev))
		goto sysfs_failed;
	if (ipoib_add_umcast_attr(ndev))
		goto sysfs_failed;
	if (device_create_file(&ndev->dev, &dev_attr_create_child))
		goto sysfs_failed;
	if (device_create_file(&ndev->dev, &dev_attr_delete_child))
		goto sysfs_failed;

	return ndev;

sysfs_failed:
	ipoib_parent_unregister_pre(ndev);
	unregister_netdev(ndev);
	return ERR_PTR(-ENOMEM);
}

static int ipoib_add_one(struct ib_device *device)
{
	struct list_head *dev_list;
	struct net_device *dev;
	struct ipoib_dev_priv *priv;
	unsigned int p;
	int count = 0;

	dev_list = kmalloc(sizeof(*dev_list), GFP_KERNEL);
	if (!dev_list)
		return -ENOMEM;

	INIT_LIST_HEAD(dev_list);

	rdma_for_each_port (device, p) {
		if (!rdma_protocol_ib(device, p))
			continue;
		dev = ipoib_add_port("ib%d", device, p);
		if (!IS_ERR(dev)) {
			priv = ipoib_priv(dev);
			list_add_tail(&priv->list, dev_list);
			count++;
		}
	}

	if (!count) {
		kfree(dev_list);
		return -EOPNOTSUPP;
	}

	ib_set_client_data(device, &ipoib_client, dev_list);
	return 0;
}

static void ipoib_remove_one(struct ib_device *device, void *client_data)
{
	struct ipoib_dev_priv *priv, *tmp, *cpriv, *tcpriv;
	struct list_head *dev_list = client_data;

	list_for_each_entry_safe(priv, tmp, dev_list, list) {
		LIST_HEAD(head);
		ipoib_parent_unregister_pre(priv->dev);

		rtnl_lock();

		list_for_each_entry_safe(cpriv, tcpriv, &priv->child_intfs,
					 list)
			unregister_netdevice_queue(cpriv->dev, &head);
		unregister_netdevice_queue(priv->dev, &head);
		unregister_netdevice_many(&head);

		rtnl_unlock();
	}

	kfree(dev_list);
}

#ifdef CONFIG_INFINIBAND_IPOIB_DEBUG
static struct notifier_block ipoib_netdev_notifier = {
	.notifier_call = ipoib_netdev_event,
};
#endif

static int __init ipoib_init_module(void)
{
	int ret;

	ipoib_recvq_size = roundup_pow_of_two(ipoib_recvq_size);
	ipoib_recvq_size = min(ipoib_recvq_size, IPOIB_MAX_QUEUE_SIZE);
	ipoib_recvq_size = max(ipoib_recvq_size, IPOIB_MIN_QUEUE_SIZE);

	ipoib_sendq_size = roundup_pow_of_two(ipoib_sendq_size);
	ipoib_sendq_size = min(ipoib_sendq_size, IPOIB_MAX_QUEUE_SIZE);
	ipoib_sendq_size = max3(ipoib_sendq_size, 2 * MAX_SEND_CQE, IPOIB_MIN_QUEUE_SIZE);
#ifdef CONFIG_INFINIBAND_IPOIB_CM
	ipoib_max_conn_qp = min(ipoib_max_conn_qp, IPOIB_CM_MAX_CONN_QP);
	ipoib_max_conn_qp = max(ipoib_max_conn_qp, 0);
#endif

	/*
	 * When copying small received packets, we only copy from the
	 * linear data part of the SKB, so we rely on this condition.
	 */
	BUILD_BUG_ON(IPOIB_CM_COPYBREAK > IPOIB_CM_HEAD_SIZE);

	ipoib_register_debugfs();

	/*
	 * We create a global workqueue here that is used for all flush
	 * operations.  However, if you attempt to flush a workqueue
	 * from a task on that same workqueue, it deadlocks the system.
	 * We want to be able to flush the tasks associated with a
	 * specific net device, so we also create a workqueue for each
	 * netdevice.  We queue up the tasks for that device only on
	 * its private workqueue, and we only queue up flush events
	 * on our global flush workqueue.  This avoids the deadlocks.
	 */
	ipoib_workqueue = alloc_ordered_workqueue("ipoib_flush", 0);
	if (!ipoib_workqueue) {
		ret = -ENOMEM;
		goto err_fs;
	}

	ib_sa_register_client(&ipoib_sa_client);

	ret = ib_register_client(&ipoib_client);
	if (ret)
		goto err_sa;

	ret = ipoib_netlink_init();
	if (ret)
		goto err_client;

#ifdef CONFIG_INFINIBAND_IPOIB_DEBUG
	register_netdevice_notifier(&ipoib_netdev_notifier);
#endif
	return 0;

err_client:
	ib_unregister_client(&ipoib_client);

err_sa:
	ib_sa_unregister_client(&ipoib_sa_client);
	destroy_workqueue(ipoib_workqueue);

err_fs:
	ipoib_unregister_debugfs();

	return ret;
}

static void __exit ipoib_cleanup_module(void)
{
#ifdef CONFIG_INFINIBAND_IPOIB_DEBUG
	unregister_netdevice_notifier(&ipoib_netdev_notifier);
#endif
	ipoib_netlink_fini();
	ib_unregister_client(&ipoib_client);
	ib_sa_unregister_client(&ipoib_sa_client);
	ipoib_unregister_debugfs();
	destroy_workqueue(ipoib_workqueue);
}

module_init(ipoib_init_module);
module_exit(ipoib_cleanup_module);
