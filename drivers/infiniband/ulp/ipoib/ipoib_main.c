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
#include <linux/pci.h>

#define DRV_VERSION "1.0.0"

const char ipoib_driver_version[] = DRV_VERSION;

MODULE_AUTHOR("Roland Dreier");
MODULE_DESCRIPTION("IP-over-InfiniBand net driver");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_VERSION(DRV_VERSION);

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

static void ipoib_add_one(struct ib_device *device);
static void ipoib_remove_one(struct ib_device *device, void *client_data);
static void ipoib_neigh_reclaim(struct rcu_head *rp);
static struct net_device *ipoib_get_net_dev_by_params(
		struct ib_device *dev, u8 port, u16 pkey,
		const union ib_gid *gid, const struct sockaddr *addr,
		void *client_data);
static int ipoib_set_mac(struct net_device *dev, void *addr);

static struct ib_client ipoib_client = {
	.name   = "ipoib",
	.add    = ipoib_add_one,
	.remove = ipoib_remove_one,
	.get_net_dev_by_params = ipoib_get_net_dev_by_params,
};

int ipoib_open(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);

	ipoib_dbg(priv, "bringing up interface\n");

	netif_carrier_off(dev);

	set_bit(IPOIB_FLAG_ADMIN_UP, &priv->flags);

	priv->sm_fullmember_sendonly_support = false;

	if (ipoib_ib_dev_open(dev)) {
		if (!test_bit(IPOIB_PKEY_ASSIGNED, &priv->flags))
			return 0;
		goto err_disable;
	}

	if (ipoib_ib_dev_up(dev))
		goto err_stop;

	if (!test_bit(IPOIB_FLAG_SUBINTERFACE, &priv->flags)) {
		struct ipoib_dev_priv *cpriv;

		/* Bring up any child interfaces too */
		down_read(&priv->vlan_rwsem);
		list_for_each_entry(cpriv, &priv->child_intfs, list) {
			int flags;

			flags = cpriv->dev->flags;
			if (flags & IFF_UP)
				continue;

			dev_change_flags(cpriv->dev, flags | IFF_UP);
		}
		up_read(&priv->vlan_rwsem);
	}

	netif_start_queue(dev);

	return 0;

err_stop:
	ipoib_ib_dev_stop(dev);

err_disable:
	clear_bit(IPOIB_FLAG_ADMIN_UP, &priv->flags);

	return -EINVAL;
}

static int ipoib_stop(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);

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

			dev_change_flags(cpriv->dev, flags & ~IFF_UP);
		}
		up_read(&priv->vlan_rwsem);
	}

	return 0;
}

static void ipoib_uninit(struct net_device *dev)
{
	ipoib_dev_cleanup(dev);
}

static netdev_features_t ipoib_fix_features(struct net_device *dev, netdev_features_t features)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);

	if (test_bit(IPOIB_FLAG_ADMIN_CM, &priv->flags))
		features &= ~(NETIF_F_IP_CSUM | NETIF_F_TSO);

	return features;
}

static int ipoib_change_mtu(struct net_device *dev, int new_mtu)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);

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

	if (new_mtu > IPOIB_UD_MTU(priv->max_ib_mtu))
		return -EINVAL;

	priv->admin_mtu = new_mtu;

	dev->mtu = min(priv->mcast_mtu, priv->admin_mtu);

	return 0;
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
	struct net_device *upper,
			  *result = NULL;
	struct list_head *iter;

	rcu_read_lock();
	if (ipoib_is_dev_match_addr_rcu(addr, dev)) {
		dev_hold(dev);
		result = dev;
		goto out;
	}

	netdev_for_each_all_upper_dev_rcu(dev, upper, iter) {
		if (ipoib_is_dev_match_addr_rcu(addr, upper)) {
			dev_hold(upper);
			result = upper;
			break;
		}
	}
out:
	rcu_read_unlock();
	return result;
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

	if (!dev_list)
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
		/* Fall through */
	case 1:
		return net_dev;
	}
}

int ipoib_set_mode(struct net_device *dev, const char *buf)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);

	/* flush paths if we switch modes so that connections are restarted */
	if (IPOIB_CM_SUPPORTED(dev->dev_addr) && !strcmp(buf, "connected\n")) {
		set_bit(IPOIB_FLAG_ADMIN_CM, &priv->flags);
		ipoib_warn(priv, "enabling connected mode "
			   "will cause multicast packet drops\n");
		netdev_update_features(dev);
		dev_set_mtu(dev, ipoib_cm_max_mtu(dev));
		rtnl_unlock();
		priv->tx_wr.wr.send_flags &= ~IB_SEND_IP_CSUM;

		ipoib_flush_paths(dev);
		rtnl_lock();
		return 0;
	}

	if (!strcmp(buf, "datagram\n")) {
		clear_bit(IPOIB_FLAG_ADMIN_CM, &priv->flags);
		netdev_update_features(dev);
		dev_set_mtu(dev, min(priv->mcast_mtu, dev->mtu));
		rtnl_unlock();
		ipoib_flush_paths(dev);
		rtnl_lock();
		return 0;
	}

	return -EINVAL;
}

static struct ipoib_path *__path_find(struct net_device *dev, void *gid)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);
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
	struct ipoib_dev_priv *priv = netdev_priv(dev);
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

	ipoib_dbg(netdev_priv(dev), "path_free\n");

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

	iter = kmalloc(sizeof *iter, GFP_KERNEL);
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
	struct ipoib_dev_priv *priv = netdev_priv(iter->dev);
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
	struct ipoib_dev_priv *priv = netdev_priv(dev);
	struct ipoib_path *path, *tp;

	spin_lock_irq(&priv->lock);

	list_for_each_entry_safe(path, tp, &priv->path_list, list) {
		ipoib_dbg(priv, "mark path LID 0x%04x GID %pI6 invalid\n",
			be16_to_cpu(path->pathrec.dlid),
			path->pathrec.dgid.raw);
		path->valid =  0;
	}

	spin_unlock_irq(&priv->lock);
}

struct classport_info_context {
	struct ipoib_dev_priv	*priv;
	struct completion	done;
	struct ib_sa_query	*sa_query;
};

static void classport_info_query_cb(int status, struct ib_class_port_info *rec,
				    void *context)
{
	struct classport_info_context *cb_ctx = context;
	struct ipoib_dev_priv *priv;

	WARN_ON(!context);

	priv = cb_ctx->priv;

	if (status || !rec) {
		pr_debug("device: %s failed query classport_info status: %d\n",
			 priv->dev->name, status);
		/* keeps the default, will try next mcast_restart */
		priv->sm_fullmember_sendonly_support = false;
		goto out;
	}

	if (ib_get_cpi_capmask2(rec) &
	    IB_SA_CAP_MASK2_SENDONLY_FULL_MEM_SUPPORT) {
		pr_debug("device: %s enabled fullmember-sendonly for sendonly MCG\n",
			 priv->dev->name);
		priv->sm_fullmember_sendonly_support = true;
	} else {
		pr_debug("device: %s disabled fullmember-sendonly for sendonly MCG\n",
			 priv->dev->name);
		priv->sm_fullmember_sendonly_support = false;
	}

out:
	complete(&cb_ctx->done);
}

int ipoib_check_sm_sendonly_fullmember_support(struct ipoib_dev_priv *priv)
{
	struct classport_info_context *callback_context;
	int ret;

	callback_context = kmalloc(sizeof(*callback_context), GFP_KERNEL);
	if (!callback_context)
		return -ENOMEM;

	callback_context->priv = priv;
	init_completion(&callback_context->done);

	ret = ib_sa_classport_info_rec_query(&ipoib_sa_client,
					     priv->ca, priv->port, 3000,
					     GFP_KERNEL,
					     classport_info_query_cb,
					     callback_context,
					     &callback_context->sa_query);
	if (ret < 0) {
		pr_info("%s failed to send ib_sa_classport_info query, ret: %d\n",
			priv->dev->name, ret);
		kfree(callback_context);
		return ret;
	}

	/* waiting for the callback to finish before returnning */
	wait_for_completion(&callback_context->done);
	kfree(callback_context);

	return ret;
}

void ipoib_flush_paths(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);
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
				struct ib_sa_path_rec *pathrec,
				void *path_ptr)
{
	struct ipoib_path *path = path_ptr;
	struct net_device *dev = path->dev;
	struct ipoib_dev_priv *priv = netdev_priv(dev);
	struct ipoib_ah *ah = NULL;
	struct ipoib_ah *old_ah = NULL;
	struct ipoib_neigh *neigh, *tn;
	struct sk_buff_head skqueue;
	struct sk_buff *skb;
	unsigned long flags;

	if (!status)
		ipoib_dbg(priv, "PathRec LID 0x%04x for GID %pI6\n",
			  be16_to_cpu(pathrec->dlid), pathrec->dgid.raw);
	else
		ipoib_dbg(priv, "PathRec status %d for GID %pI6\n",
			  status, path->pathrec.dgid.raw);

	skb_queue_head_init(&skqueue);

	if (!status) {
		struct ib_ah_attr av;

		if (!ib_init_ah_from_path(priv->ca, priv->port, pathrec, &av))
			ah = ipoib_create_ah(dev, priv->pd, &av);
	}

	spin_lock_irqsave(&priv->lock, flags);

	if (!IS_ERR_OR_NULL(ah)) {
		path->pathrec = *pathrec;

		old_ah   = path->ah;
		path->ah = ah;

		ipoib_dbg(priv, "created address handle %p for LID 0x%04x, SL %d\n",
			  ah, be16_to_cpu(pathrec->dlid), pathrec->sl);

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
		path->valid = 1;
	}

	path->query = NULL;
	complete(&path->done);

	spin_unlock_irqrestore(&priv->lock, flags);

	if (IS_ERR_OR_NULL(ah))
		ipoib_del_neighs_by_gid(dev, path->pathrec.dgid.raw);

	if (old_ah)
		ipoib_put_ah(old_ah);

	while ((skb = __skb_dequeue(&skqueue))) {
		skb->dev = dev;
		if (dev_queue_xmit(skb))
			ipoib_warn(priv, "dev_queue_xmit failed "
				   "to requeue packet\n");
	}
}

static struct ipoib_path *path_rec_create(struct net_device *dev, void *gid)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);
	struct ipoib_path *path;

	if (!priv->broadcast)
		return NULL;

	path = kzalloc(sizeof *path, GFP_ATOMIC);
	if (!path)
		return NULL;

	path->dev = dev;

	skb_queue_head_init(&path->queue);

	INIT_LIST_HEAD(&path->neigh_list);

	memcpy(path->pathrec.dgid.raw, gid, sizeof (union ib_gid));
	path->pathrec.sgid	    = priv->local_gid;
	path->pathrec.pkey	    = cpu_to_be16(priv->pkey);
	path->pathrec.numb_path     = 1;
	path->pathrec.traffic_class = priv->broadcast->mcmember.traffic_class;

	return path;
}

static int path_rec_start(struct net_device *dev,
			  struct ipoib_path *path)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);

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

static void neigh_add_path(struct sk_buff *skb, u8 *daddr,
			   struct net_device *dev)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);
	struct ipoib_path *path;
	struct ipoib_neigh *neigh;
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);
	neigh = ipoib_neigh_alloc(daddr, dev);
	if (!neigh) {
		spin_unlock_irqrestore(&priv->lock, flags);
		++dev->stats.tx_dropped;
		dev_kfree_skb_any(skb);
		return;
	}

	path = __path_find(dev, daddr + 4);
	if (!path) {
		path = path_rec_create(dev, daddr + 4);
		if (!path)
			goto err_path;

		__path_add(dev, path);
	}

	list_add_tail(&neigh->list, &path->neigh_list);

	if (path->ah) {
		kref_get(&path->ah->ref);
		neigh->ah = path->ah;

		if (ipoib_cm_enabled(dev, neigh->daddr)) {
			if (!ipoib_cm_get(neigh))
				ipoib_cm_set(neigh, ipoib_cm_create_tx(dev, path, neigh));
			if (!ipoib_cm_get(neigh)) {
				ipoib_neigh_free(neigh);
				goto err_drop;
			}
			if (skb_queue_len(&neigh->queue) < IPOIB_MAX_PATH_REC_QUEUE)
				__skb_queue_tail(&neigh->queue, skb);
			else {
				ipoib_warn(priv, "queue length limit %d. Packet drop.\n",
					   skb_queue_len(&neigh->queue));
				goto err_drop;
			}
		} else {
			spin_unlock_irqrestore(&priv->lock, flags);
			ipoib_send(dev, skb, path->ah, IPOIB_QPN(daddr));
			ipoib_neigh_put(neigh);
			return;
		}
	} else {
		neigh->ah  = NULL;

		if (!path->query && path_rec_start(dev, path))
			goto err_path;
		if (skb_queue_len(&neigh->queue) < IPOIB_MAX_PATH_REC_QUEUE)
			__skb_queue_tail(&neigh->queue, skb);
		else
			goto err_drop;
	}

	spin_unlock_irqrestore(&priv->lock, flags);
	ipoib_neigh_put(neigh);
	return;

err_path:
	ipoib_neigh_free(neigh);
err_drop:
	++dev->stats.tx_dropped;
	dev_kfree_skb_any(skb);

	spin_unlock_irqrestore(&priv->lock, flags);
	ipoib_neigh_put(neigh);
}

static void unicast_arp_send(struct sk_buff *skb, struct net_device *dev,
			     struct ipoib_cb *cb)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);
	struct ipoib_path *path;
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);

	path = __path_find(dev, cb->hwaddr + 4);
	if (!path || !path->valid) {
		int new_path = 0;

		if (!path) {
			path = path_rec_create(dev, cb->hwaddr + 4);
			new_path = 1;
		}
		if (path) {
			if (skb_queue_len(&path->queue) < IPOIB_MAX_PATH_REC_QUEUE) {
				__skb_queue_tail(&path->queue, skb);
			} else {
				++dev->stats.tx_dropped;
				dev_kfree_skb_any(skb);
			}

			if (!path->query && path_rec_start(dev, path)) {
				spin_unlock_irqrestore(&priv->lock, flags);
				if (new_path)
					path_free(dev, path);
				return;
			} else
				__path_add(dev, path);
		} else {
			++dev->stats.tx_dropped;
			dev_kfree_skb_any(skb);
		}

		spin_unlock_irqrestore(&priv->lock, flags);
		return;
	}

	if (path->ah) {
		ipoib_dbg(priv, "Send unicast ARP to %04x\n",
			  be16_to_cpu(path->pathrec.dlid));

		spin_unlock_irqrestore(&priv->lock, flags);
		ipoib_send(dev, skb, path->ah, IPOIB_QPN(cb->hwaddr));
		return;
	} else if ((path->query || !path_rec_start(dev, path)) &&
		   skb_queue_len(&path->queue) < IPOIB_MAX_PATH_REC_QUEUE) {
		__skb_queue_tail(&path->queue, skb);
	} else {
		++dev->stats.tx_dropped;
		dev_kfree_skb_any(skb);
	}

	spin_unlock_irqrestore(&priv->lock, flags);
}

static int ipoib_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);
	struct ipoib_neigh *neigh;
	struct ipoib_cb *cb = ipoib_skb_cb(skb);
	struct ipoib_header *header;
	unsigned long flags;

	header = (struct ipoib_header *) skb->data;

	if (unlikely(cb->hwaddr[4] == 0xff)) {
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
		cb->hwaddr[8] = (priv->pkey >> 8) & 0xff;
		cb->hwaddr[9] = priv->pkey & 0xff;

		neigh = ipoib_neigh_get(dev, cb->hwaddr);
		if (likely(neigh))
			goto send_using_neigh;
		ipoib_mcast_send(dev, cb->hwaddr, skb);
		return NETDEV_TX_OK;
	}

	/* unicast, arrange "switch" according to probability */
	switch (header->proto) {
	case htons(ETH_P_IP):
	case htons(ETH_P_IPV6):
	case htons(ETH_P_TIPC):
		neigh = ipoib_neigh_get(dev, cb->hwaddr);
		if (unlikely(!neigh)) {
			neigh_add_path(skb, cb->hwaddr, dev);
			return NETDEV_TX_OK;
		}
		break;
	case htons(ETH_P_ARP):
	case htons(ETH_P_RARP):
		/* for unicast ARP and RARP should always perform path find */
		unicast_arp_send(skb, dev, cb);
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
	} else if (neigh->ah) {
		ipoib_send(dev, skb, neigh->ah, IPOIB_QPN(cb->hwaddr));
		goto unref;
	}

	if (skb_queue_len(&neigh->queue) < IPOIB_MAX_PATH_REC_QUEUE) {
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

static void ipoib_timeout(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);

	ipoib_warn(priv, "transmit timeout: latency %d msecs\n",
		   jiffies_to_msecs(jiffies - dev_trans_start(dev)));
	ipoib_warn(priv, "queue stopped %d, tx_head %u, tx_tail %u\n",
		   netif_queue_stopped(dev),
		   priv->tx_head, priv->tx_tail);
	/* XXX reset QP, etc. */
}

static int ipoib_hard_header(struct sk_buff *skb,
			     struct net_device *dev,
			     unsigned short type,
			     const void *daddr, const void *saddr, unsigned len)
{
	struct ipoib_header *header;
	struct ipoib_cb *cb = ipoib_skb_cb(skb);

	header = (struct ipoib_header *) skb_push(skb, sizeof *header);

	header->proto = htons(type);
	header->reserved = 0;

	/*
	 * we don't rely on dst_entry structure,  always stuff the
	 * destination address into skb->cb so we can figure out where
	 * to send the packet later.
	 */
	memcpy(cb->hwaddr, daddr, INFINIBAND_ALEN);

	return sizeof *header;
}

static void ipoib_set_mcast_list(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);

	if (!test_bit(IPOIB_FLAG_OPER_UP, &priv->flags)) {
		ipoib_dbg(priv, "IPOIB_FLAG_OPER_UP not set");
		return;
	}

	queue_work(priv->wq, &priv->restart_task);
}

static int ipoib_get_iflink(const struct net_device *dev)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);

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
	struct ipoib_dev_priv *priv = netdev_priv(dev);
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

	if (test_bit(IPOIB_STOP_NEIGH_GC, &priv->flags))
		return;

	spin_lock_irqsave(&priv->lock, flags);

	htbl = rcu_dereference_protected(ntbl->htbl,
					 lockdep_is_held(&priv->lock));

	if (!htbl)
		goto out_unlock;

	/* neigh is obsolete if it was idle for two GC periods */
	dt = 2 * arp_tbl.gc_interval;
	neigh_obsolete = jiffies - dt;
	/* handle possible race condition */
	if (test_bit(IPOIB_STOP_NEIGH_GC, &priv->flags))
		goto out_unlock;

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
				list_del(&neigh->list);
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

	if (!test_bit(IPOIB_STOP_NEIGH_GC, &priv->flags))
		queue_delayed_work(priv->wq, &priv->neigh_reap_task,
				   arp_tbl.gc_interval);
}


static struct ipoib_neigh *ipoib_neigh_ctor(u8 *daddr,
				      struct net_device *dev)
{
	struct ipoib_neigh *neigh;

	neigh = kzalloc(sizeof *neigh, GFP_ATOMIC);
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
	struct ipoib_dev_priv *priv = netdev_priv(dev);
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
	struct ipoib_dev_priv *priv = netdev_priv(dev);
	struct sk_buff *skb;
	if (neigh->ah)
		ipoib_put_ah(neigh->ah);
	while ((skb = __skb_dequeue(&neigh->queue))) {
		++dev->stats.tx_dropped;
		dev_kfree_skb_any(skb);
	}
	if (ipoib_cm_get(neigh))
		ipoib_cm_destroy_tx(ipoib_cm_get(neigh));
	ipoib_dbg(netdev_priv(dev),
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
	struct ipoib_dev_priv *priv = netdev_priv(dev);
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
			list_del(&neigh->list);
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
	set_bit(IPOIB_STOP_NEIGH_GC, &priv->flags);
	size = roundup_pow_of_two(arp_tbl.gc_thresh3);
	buckets = kzalloc(size * sizeof(*buckets), GFP_KERNEL);
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
	clear_bit(IPOIB_STOP_NEIGH_GC, &priv->flags);
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

	kfree(buckets);
	kfree(htbl);
	complete(&ntbl->deleted);
}

void ipoib_del_neighs_by_gid(struct net_device *dev, u8 *gid)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);
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
				list_del(&neigh->list);
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
			list_del(&neigh->list);
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
	struct ipoib_dev_priv *priv = netdev_priv(dev);
	int stopped;

	ipoib_dbg(priv, "ipoib_neigh_hash_uninit\n");
	init_completion(&priv->ntbl.deleted);
	set_bit(IPOIB_NEIGH_TBL_FLUSH, &priv->flags);

	/* Stop GC if called at init fail need to cancel work */
	stopped = test_and_set_bit(IPOIB_STOP_NEIGH_GC, &priv->flags);
	if (!stopped)
		cancel_delayed_work(&priv->neigh_reap_task);

	ipoib_flush_neighs(priv);

	wait_for_completion(&priv->ntbl.deleted);
}


int ipoib_dev_init(struct net_device *dev, struct ib_device *ca, int port)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);

	/* Allocate RX/TX "rings" to hold queued skbs */
	priv->rx_ring =	kzalloc(ipoib_recvq_size * sizeof *priv->rx_ring,
				GFP_KERNEL);
	if (!priv->rx_ring) {
		printk(KERN_WARNING "%s: failed to allocate RX ring (%d entries)\n",
		       ca->name, ipoib_recvq_size);
		goto out;
	}

	priv->tx_ring = vzalloc(ipoib_sendq_size * sizeof *priv->tx_ring);
	if (!priv->tx_ring) {
		printk(KERN_WARNING "%s: failed to allocate TX ring (%d entries)\n",
		       ca->name, ipoib_sendq_size);
		goto out_rx_ring_cleanup;
	}

	/* priv->tx_head, tx_tail & tx_outstanding are already 0 */

	if (ipoib_ib_dev_init(dev, ca, port))
		goto out_tx_ring_cleanup;

	/*
	 * Must be after ipoib_ib_dev_init so we can allocate a per
	 * device wq there and use it here
	 */
	if (ipoib_neigh_hash_init(priv) < 0)
		goto out_dev_uninit;

	return 0;

out_dev_uninit:
	ipoib_ib_dev_cleanup(dev);

out_tx_ring_cleanup:
	vfree(priv->tx_ring);

out_rx_ring_cleanup:
	kfree(priv->rx_ring);

out:
	return -ENOMEM;
}

void ipoib_dev_cleanup(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev), *cpriv, *tcpriv;
	LIST_HEAD(head);

	ASSERT_RTNL();

	ipoib_delete_debug_files(dev);

	/* Delete any child interfaces first */
	list_for_each_entry_safe(cpriv, tcpriv, &priv->child_intfs, list) {
		/* Stop GC on child */
		set_bit(IPOIB_STOP_NEIGH_GC, &cpriv->flags);
		cancel_delayed_work(&cpriv->neigh_reap_task);
		unregister_netdevice_queue(cpriv->dev, &head);
	}
	unregister_netdevice_many(&head);

	/*
	 * Must be before ipoib_ib_dev_cleanup or we delete an in use
	 * work queue
	 */
	ipoib_neigh_hash_uninit(dev);

	ipoib_ib_dev_cleanup(dev);

	kfree(priv->rx_ring);
	vfree(priv->tx_ring);

	priv->rx_ring = NULL;
	priv->tx_ring = NULL;
}

static int ipoib_set_vf_link_state(struct net_device *dev, int vf, int link_state)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);

	return ib_set_vf_link_state(priv->ca, vf, priv->port, link_state);
}

static int ipoib_get_vf_config(struct net_device *dev, int vf,
			       struct ifla_vf_info *ivf)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);
	int err;

	err = ib_get_vf_config(priv->ca, vf, priv->port, ivf);
	if (err)
		return err;

	ivf->vf = vf;

	return 0;
}

static int ipoib_set_vf_guid(struct net_device *dev, int vf, u64 guid, int type)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);

	if (type != IFLA_VF_IB_NODE_GUID && type != IFLA_VF_IB_PORT_GUID)
		return -EINVAL;

	return ib_set_vf_guid(priv->ca, vf, priv->port, guid, type);
}

static int ipoib_get_vf_stats(struct net_device *dev, int vf,
			      struct ifla_vf_stats *vf_stats)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);

	return ib_get_vf_stats(priv->ca, vf, priv->port, vf_stats);
}

static const struct header_ops ipoib_header_ops = {
	.create	= ipoib_hard_header,
};

static const struct net_device_ops ipoib_netdev_ops_pf = {
	.ndo_uninit		 = ipoib_uninit,
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
	.ndo_set_vf_guid	 = ipoib_set_vf_guid,
	.ndo_set_mac_address	 = ipoib_set_mac,
};

static const struct net_device_ops ipoib_netdev_ops_vf = {
	.ndo_uninit		 = ipoib_uninit,
	.ndo_open		 = ipoib_open,
	.ndo_stop		 = ipoib_stop,
	.ndo_change_mtu		 = ipoib_change_mtu,
	.ndo_fix_features	 = ipoib_fix_features,
	.ndo_start_xmit	 	 = ipoib_start_xmit,
	.ndo_tx_timeout		 = ipoib_timeout,
	.ndo_set_rx_mode	 = ipoib_set_mcast_list,
	.ndo_get_iflink		 = ipoib_get_iflink,
};

void ipoib_setup(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);

	if (priv->hca_caps & IB_DEVICE_VIRTUAL_FUNCTION)
		dev->netdev_ops	= &ipoib_netdev_ops_vf;
	else
		dev->netdev_ops	= &ipoib_netdev_ops_pf;

	dev->header_ops		 = &ipoib_header_ops;

	ipoib_set_ethtool_ops(dev);

	netif_napi_add(dev, &priv->napi, ipoib_poll, NAPI_POLL_WEIGHT);

	dev->watchdog_timeo	 = HZ;

	dev->flags		|= IFF_BROADCAST | IFF_MULTICAST;

	dev->hard_header_len	 = IPOIB_ENCAP_LEN;
	dev->addr_len		 = INFINIBAND_ALEN;
	dev->type		 = ARPHRD_INFINIBAND;
	dev->tx_queue_len	 = ipoib_sendq_size * 2;
	dev->features		 = (NETIF_F_VLAN_CHALLENGED	|
				    NETIF_F_HIGHDMA);
	netif_keep_dst(dev);

	memcpy(dev->broadcast, ipv4_bcast_addr, INFINIBAND_ALEN);

	priv->dev = dev;

	spin_lock_init(&priv->lock);

	init_rwsem(&priv->vlan_rwsem);

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

struct ipoib_dev_priv *ipoib_intf_alloc(const char *name)
{
	struct net_device *dev;

	dev = alloc_netdev((int)sizeof(struct ipoib_dev_priv), name,
			   NET_NAME_UNKNOWN, ipoib_setup);
	if (!dev)
		return NULL;

	return netdev_priv(dev);
}

static ssize_t show_pkey(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	struct ipoib_dev_priv *priv = netdev_priv(to_net_dev(dev));

	return sprintf(buf, "0x%04x\n", priv->pkey);
}
static DEVICE_ATTR(pkey, S_IRUGO, show_pkey, NULL);

static ssize_t show_umcast(struct device *dev,
			   struct device_attribute *attr, char *buf)
{
	struct ipoib_dev_priv *priv = netdev_priv(to_net_dev(dev));

	return sprintf(buf, "%d\n", test_bit(IPOIB_FLAG_UMCAST, &priv->flags));
}

void ipoib_set_umcast(struct net_device *ndev, int umcast_val)
{
	struct ipoib_dev_priv *priv = netdev_priv(ndev);

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

	netif_addr_lock(netdev);

	memcpy(&priv->local_gid.global.interface_id,
	       &gid->global.interface_id,
	       sizeof(gid->global.interface_id));
	memcpy(netdev->dev_addr + 4, &priv->local_gid, sizeof(priv->local_gid));
	clear_bit(IPOIB_FLAG_DEV_ADDR_SET, &priv->flags);

	netif_addr_unlock(netdev);

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

	netif_addr_lock(dev);

	/* Make sure the QPN, reserved and subnet prefix match the current
	 * lladdr, it also makes sure the lladdr is unicast.
	 */
	if (memcmp(dev->dev_addr, ss->__data,
		   4 + sizeof(gid->global.subnet_prefix)) ||
	    gid->global.interface_id == 0)
		ret = -EINVAL;

	netif_addr_unlock(dev);

	return ret;
}

static int ipoib_set_mac(struct net_device *dev, void *addr)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);
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

	/*
	 * Set the full membership bit, so that we join the right
	 * broadcast group, etc.
	 */
	pkey |= 0x8000;

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

int ipoib_set_dev_features(struct ipoib_dev_priv *priv, struct ib_device *hca)
{
	priv->hca_caps = hca->attrs.device_cap_flags;

	if (priv->hca_caps & IB_DEVICE_UD_IP_CSUM) {
		priv->dev->hw_features = NETIF_F_SG |
			NETIF_F_IP_CSUM | NETIF_F_RXCSUM;

		if (priv->hca_caps & IB_DEVICE_UD_TSO)
			priv->dev->hw_features |= NETIF_F_TSO;

		priv->dev->features |= priv->dev->hw_features;
	}

	return 0;
}

static struct net_device *ipoib_add_port(const char *format,
					 struct ib_device *hca, u8 port)
{
	struct ipoib_dev_priv *priv;
	struct ib_port_attr attr;
	int result = -ENOMEM;

	priv = ipoib_intf_alloc(format);
	if (!priv)
		goto alloc_mem_failed;

	SET_NETDEV_DEV(priv->dev, hca->dma_device);
	priv->dev->dev_id = port - 1;

	result = ib_query_port(hca, port, &attr);
	if (!result)
		priv->max_ib_mtu = ib_mtu_enum_to_int(attr.max_mtu);
	else {
		printk(KERN_WARNING "%s: ib_query_port %d failed\n",
		       hca->name, port);
		goto device_init_failed;
	}

	/* MTU will be reset when mcast join happens */
	priv->dev->mtu  = IPOIB_UD_MTU(priv->max_ib_mtu);
	priv->mcast_mtu  = priv->admin_mtu = priv->dev->mtu;

	priv->dev->neigh_priv_len = sizeof(struct ipoib_neigh);

	result = ib_query_pkey(hca, port, 0, &priv->pkey);
	if (result) {
		printk(KERN_WARNING "%s: ib_query_pkey port %d failed (ret = %d)\n",
		       hca->name, port, result);
		goto device_init_failed;
	}

	result = ipoib_set_dev_features(priv, hca);
	if (result)
		goto device_init_failed;

	/*
	 * Set the full membership bit, so that we join the right
	 * broadcast group, etc.
	 */
	priv->pkey |= 0x8000;

	priv->dev->broadcast[8] = priv->pkey >> 8;
	priv->dev->broadcast[9] = priv->pkey & 0xff;

	result = ib_query_gid(hca, port, 0, &priv->local_gid, NULL);
	if (result) {
		printk(KERN_WARNING "%s: ib_query_gid port %d failed (ret = %d)\n",
		       hca->name, port, result);
		goto device_init_failed;
	} else
		memcpy(priv->dev->dev_addr + 4, priv->local_gid.raw, sizeof (union ib_gid));
	set_bit(IPOIB_FLAG_DEV_ADDR_SET, &priv->flags);

	result = ipoib_dev_init(priv->dev, hca, port);
	if (result < 0) {
		printk(KERN_WARNING "%s: failed to initialize port %d (ret = %d)\n",
		       hca->name, port, result);
		goto device_init_failed;
	}

	INIT_IB_EVENT_HANDLER(&priv->event_handler,
			      priv->ca, ipoib_event);
	result = ib_register_event_handler(&priv->event_handler);
	if (result < 0) {
		printk(KERN_WARNING "%s: ib_register_event_handler failed for "
		       "port %d (ret = %d)\n",
		       hca->name, port, result);
		goto event_failed;
	}

	result = register_netdev(priv->dev);
	if (result) {
		printk(KERN_WARNING "%s: couldn't register ipoib port %d; error %d\n",
		       hca->name, port, result);
		goto register_failed;
	}

	ipoib_create_debug_files(priv->dev);

	if (ipoib_cm_add_mode_attr(priv->dev))
		goto sysfs_failed;
	if (ipoib_add_pkey_attr(priv->dev))
		goto sysfs_failed;
	if (ipoib_add_umcast_attr(priv->dev))
		goto sysfs_failed;
	if (device_create_file(&priv->dev->dev, &dev_attr_create_child))
		goto sysfs_failed;
	if (device_create_file(&priv->dev->dev, &dev_attr_delete_child))
		goto sysfs_failed;

	return priv->dev;

sysfs_failed:
	ipoib_delete_debug_files(priv->dev);
	unregister_netdev(priv->dev);

register_failed:
	ib_unregister_event_handler(&priv->event_handler);
	flush_workqueue(ipoib_workqueue);
	/* Stop GC if started before flush */
	set_bit(IPOIB_STOP_NEIGH_GC, &priv->flags);
	cancel_delayed_work(&priv->neigh_reap_task);
	flush_workqueue(priv->wq);

event_failed:
	ipoib_dev_cleanup(priv->dev);

device_init_failed:
	free_netdev(priv->dev);

alloc_mem_failed:
	return ERR_PTR(result);
}

static void ipoib_add_one(struct ib_device *device)
{
	struct list_head *dev_list;
	struct net_device *dev;
	struct ipoib_dev_priv *priv;
	int p;
	int count = 0;

	dev_list = kmalloc(sizeof *dev_list, GFP_KERNEL);
	if (!dev_list)
		return;

	INIT_LIST_HEAD(dev_list);

	for (p = rdma_start_port(device); p <= rdma_end_port(device); ++p) {
		if (!rdma_protocol_ib(device, p))
			continue;
		dev = ipoib_add_port("ib%d", device, p);
		if (!IS_ERR(dev)) {
			priv = netdev_priv(dev);
			list_add_tail(&priv->list, dev_list);
			count++;
		}
	}

	if (!count) {
		kfree(dev_list);
		return;
	}

	ib_set_client_data(device, &ipoib_client, dev_list);
}

static void ipoib_remove_one(struct ib_device *device, void *client_data)
{
	struct ipoib_dev_priv *priv, *tmp;
	struct list_head *dev_list = client_data;

	if (!dev_list)
		return;

	list_for_each_entry_safe(priv, tmp, dev_list, list) {
		ib_unregister_event_handler(&priv->event_handler);
		flush_workqueue(ipoib_workqueue);

		rtnl_lock();
		dev_change_flags(priv->dev, priv->dev->flags & ~IFF_UP);
		rtnl_unlock();

		/* Stop GC */
		set_bit(IPOIB_STOP_NEIGH_GC, &priv->flags);
		cancel_delayed_work(&priv->neigh_reap_task);
		flush_workqueue(priv->wq);

		unregister_netdev(priv->dev);
		free_netdev(priv->dev);
	}

	kfree(dev_list);
}

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
#endif

	/*
	 * When copying small received packets, we only copy from the
	 * linear data part of the SKB, so we rely on this condition.
	 */
	BUILD_BUG_ON(IPOIB_CM_COPYBREAK > IPOIB_CM_HEAD_SIZE);

	ret = ipoib_register_debugfs();
	if (ret)
		return ret;

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
	ipoib_workqueue = create_singlethread_workqueue("ipoib_flush");
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
	ipoib_netlink_fini();
	ib_unregister_client(&ipoib_client);
	ib_sa_unregister_client(&ipoib_sa_client);
	ipoib_unregister_debugfs();
	destroy_workqueue(ipoib_workqueue);
}

module_init(ipoib_init_module);
module_exit(ipoib_cleanup_module);
