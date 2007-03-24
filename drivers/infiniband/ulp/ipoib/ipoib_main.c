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
 *
 * $Id: ipoib_main.c 1377 2004-12-23 19:57:12Z roland $
 */

#include "ipoib.h"

#include <linux/module.h>

#include <linux/init.h>
#include <linux/slab.h>
#include <linux/kernel.h>

#include <linux/if_arp.h>	/* For ARPHRD_xxx */

#include <linux/ip.h>
#include <linux/in.h>

#include <net/dst.h>

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

static void ipoib_add_one(struct ib_device *device);
static void ipoib_remove_one(struct ib_device *device);

static struct ib_client ipoib_client = {
	.name   = "ipoib",
	.add    = ipoib_add_one,
	.remove = ipoib_remove_one
};

int ipoib_open(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);

	ipoib_dbg(priv, "bringing up interface\n");

	set_bit(IPOIB_FLAG_ADMIN_UP, &priv->flags);

	if (ipoib_pkey_dev_delay_open(dev))
		return 0;

	if (ipoib_ib_dev_open(dev))
		return -EINVAL;

	if (ipoib_ib_dev_up(dev)) {
		ipoib_ib_dev_stop(dev);
		return -EINVAL;
	}

	if (!test_bit(IPOIB_FLAG_SUBINTERFACE, &priv->flags)) {
		struct ipoib_dev_priv *cpriv;

		/* Bring up any child interfaces too */
		mutex_lock(&priv->vlan_mutex);
		list_for_each_entry(cpriv, &priv->child_intfs, list) {
			int flags;

			flags = cpriv->dev->flags;
			if (flags & IFF_UP)
				continue;

			dev_change_flags(cpriv->dev, flags | IFF_UP);
		}
		mutex_unlock(&priv->vlan_mutex);
	}

	netif_start_queue(dev);

	return 0;
}

static int ipoib_stop(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);

	ipoib_dbg(priv, "stopping interface\n");

	clear_bit(IPOIB_FLAG_ADMIN_UP, &priv->flags);

	netif_stop_queue(dev);

	clear_bit(IPOIB_FLAG_NETIF_STOPPED, &priv->flags);

	/*
	 * Now flush workqueue to make sure a scheduled task doesn't
	 * bring our internal state back up.
	 */
	flush_workqueue(ipoib_workqueue);

	ipoib_ib_dev_down(dev, 1);
	ipoib_ib_dev_stop(dev);

	if (!test_bit(IPOIB_FLAG_SUBINTERFACE, &priv->flags)) {
		struct ipoib_dev_priv *cpriv;

		/* Bring down any child interfaces too */
		mutex_lock(&priv->vlan_mutex);
		list_for_each_entry(cpriv, &priv->child_intfs, list) {
			int flags;

			flags = cpriv->dev->flags;
			if (!(flags & IFF_UP))
				continue;

			dev_change_flags(cpriv->dev, flags & ~IFF_UP);
		}
		mutex_unlock(&priv->vlan_mutex);
	}

	return 0;
}

static int ipoib_change_mtu(struct net_device *dev, int new_mtu)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);

	/* dev->mtu > 2K ==> connected mode */
	if (ipoib_cm_admin_enabled(dev) && new_mtu <= IPOIB_CM_MTU) {
		if (new_mtu > priv->mcast_mtu)
			ipoib_warn(priv, "mtu > %d will cause multicast packet drops.\n",
				   priv->mcast_mtu);
		dev->mtu = new_mtu;
		return 0;
	}

	if (new_mtu > IPOIB_PACKET_SIZE - IPOIB_ENCAP_LEN) {
		return -EINVAL;
	}

	priv->admin_mtu = new_mtu;

	dev->mtu = min(priv->mcast_mtu, priv->admin_mtu);

	return 0;
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
	struct ipoib_dev_priv *priv = netdev_priv(dev);
	struct ipoib_neigh *neigh, *tn;
	struct sk_buff *skb;
	unsigned long flags;

	while ((skb = __skb_dequeue(&path->queue)))
		dev_kfree_skb_irq(skb);

	spin_lock_irqsave(&priv->lock, flags);

	list_for_each_entry_safe(neigh, tn, &path->neigh_list, list) {
		/*
		 * It's safe to call ipoib_put_ah() inside priv->lock
		 * here, because we know that path->ah will always
		 * hold one more reference, so ipoib_put_ah() will
		 * never do more than decrement the ref count.
		 */
		if (neigh->ah)
			ipoib_put_ah(neigh->ah);

		ipoib_neigh_free(dev, neigh);
	}

	spin_unlock_irqrestore(&priv->lock, flags);

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

void ipoib_flush_paths(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);
	struct ipoib_path *path, *tp;
	LIST_HEAD(remove_list);

	spin_lock_irq(&priv->tx_lock);
	spin_lock(&priv->lock);

	list_splice(&priv->path_list, &remove_list);
	INIT_LIST_HEAD(&priv->path_list);

	list_for_each_entry(path, &remove_list, list)
		rb_erase(&path->rb_node, &priv->path_tree);

	list_for_each_entry_safe(path, tp, &remove_list, list) {
		if (path->query)
			ib_sa_cancel_query(path->query_id, path->query);
		spin_unlock(&priv->lock);
		spin_unlock_irq(&priv->tx_lock);
		wait_for_completion(&path->done);
		path_free(dev, path);
		spin_lock_irq(&priv->tx_lock);
		spin_lock(&priv->lock);
	}
	spin_unlock(&priv->lock);
	spin_unlock_irq(&priv->tx_lock);
}

static void path_rec_completion(int status,
				struct ib_sa_path_rec *pathrec,
				void *path_ptr)
{
	struct ipoib_path *path = path_ptr;
	struct net_device *dev = path->dev;
	struct ipoib_dev_priv *priv = netdev_priv(dev);
	struct ipoib_ah *ah = NULL;
	struct ipoib_neigh *neigh, *tn;
	struct sk_buff_head skqueue;
	struct sk_buff *skb;
	unsigned long flags;

	if (!status)
		ipoib_dbg(priv, "PathRec LID 0x%04x for GID " IPOIB_GID_FMT "\n",
			  be16_to_cpu(pathrec->dlid), IPOIB_GID_ARG(pathrec->dgid));
	else
		ipoib_dbg(priv, "PathRec status %d for GID " IPOIB_GID_FMT "\n",
			  status, IPOIB_GID_ARG(path->pathrec.dgid));

	skb_queue_head_init(&skqueue);

	if (!status) {
		struct ib_ah_attr av = {
			.dlid 	       = be16_to_cpu(pathrec->dlid),
			.sl 	       = pathrec->sl,
			.port_num      = priv->port,
			.static_rate   = pathrec->rate
		};

		ah = ipoib_create_ah(dev, priv->pd, &av);
	}

	spin_lock_irqsave(&priv->lock, flags);

	path->ah = ah;

	if (ah) {
		path->pathrec = *pathrec;

		ipoib_dbg(priv, "created address handle %p for LID 0x%04x, SL %d\n",
			  ah, be16_to_cpu(pathrec->dlid), pathrec->sl);

		while ((skb = __skb_dequeue(&path->queue)))
			__skb_queue_tail(&skqueue, skb);

		list_for_each_entry_safe(neigh, tn, &path->neigh_list, list) {
			kref_get(&path->ah->ref);
			neigh->ah = path->ah;
			memcpy(&neigh->dgid.raw, &path->pathrec.dgid.raw,
			       sizeof(union ib_gid));

			if (ipoib_cm_enabled(dev, neigh->neighbour)) {
				if (!ipoib_cm_get(neigh))
					ipoib_cm_set(neigh, ipoib_cm_create_tx(dev,
									       path,
									       neigh));
				if (!ipoib_cm_get(neigh)) {
					list_del(&neigh->list);
					if (neigh->ah)
						ipoib_put_ah(neigh->ah);
					ipoib_neigh_free(dev, neigh);
					continue;
				}
			}

			while ((skb = __skb_dequeue(&neigh->queue)))
				__skb_queue_tail(&skqueue, skb);
		}
	}

	path->query = NULL;
	complete(&path->done);

	spin_unlock_irqrestore(&priv->lock, flags);

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

	path = kzalloc(sizeof *path, GFP_ATOMIC);
	if (!path)
		return NULL;

	path->dev = dev;

	skb_queue_head_init(&path->queue);

	INIT_LIST_HEAD(&path->neigh_list);

	memcpy(path->pathrec.dgid.raw, gid, sizeof (union ib_gid));
	path->pathrec.sgid      = priv->local_gid;
	path->pathrec.pkey      = cpu_to_be16(priv->pkey);
	path->pathrec.numb_path = 1;

	return path;
}

static int path_rec_start(struct net_device *dev,
			  struct ipoib_path *path)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);

	ipoib_dbg(priv, "Start path record lookup for " IPOIB_GID_FMT "\n",
		  IPOIB_GID_ARG(path->pathrec.dgid));

	init_completion(&path->done);

	path->query_id =
		ib_sa_path_rec_get(&ipoib_sa_client, priv->ca, priv->port,
				   &path->pathrec,
				   IB_SA_PATH_REC_DGID		|
				   IB_SA_PATH_REC_SGID		|
				   IB_SA_PATH_REC_NUMB_PATH	|
				   IB_SA_PATH_REC_PKEY,
				   1000, GFP_ATOMIC,
				   path_rec_completion,
				   path, &path->query);
	if (path->query_id < 0) {
		ipoib_warn(priv, "ib_sa_path_rec_get failed\n");
		path->query = NULL;
		return path->query_id;
	}

	return 0;
}

static void neigh_add_path(struct sk_buff *skb, struct net_device *dev)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);
	struct ipoib_path *path;
	struct ipoib_neigh *neigh;

	neigh = ipoib_neigh_alloc(skb->dst->neighbour);
	if (!neigh) {
		++priv->stats.tx_dropped;
		dev_kfree_skb_any(skb);
		return;
	}

	/*
	 * We can only be called from ipoib_start_xmit, so we're
	 * inside tx_lock -- no need to save/restore flags.
	 */
	spin_lock(&priv->lock);

	path = __path_find(dev, skb->dst->neighbour->ha + 4);
	if (!path) {
		path = path_rec_create(dev, skb->dst->neighbour->ha + 4);
		if (!path)
			goto err_path;

		__path_add(dev, path);
	}

	list_add_tail(&neigh->list, &path->neigh_list);

	if (path->ah) {
		kref_get(&path->ah->ref);
		neigh->ah = path->ah;
		memcpy(&neigh->dgid.raw, &path->pathrec.dgid.raw,
		       sizeof(union ib_gid));

		if (ipoib_cm_enabled(dev, neigh->neighbour)) {
			if (!ipoib_cm_get(neigh))
				ipoib_cm_set(neigh, ipoib_cm_create_tx(dev, path, neigh));
			if (!ipoib_cm_get(neigh)) {
				list_del(&neigh->list);
				if (neigh->ah)
					ipoib_put_ah(neigh->ah);
				ipoib_neigh_free(dev, neigh);
				goto err_drop;
			}
			if (skb_queue_len(&neigh->queue) < IPOIB_MAX_PATH_REC_QUEUE)
				__skb_queue_tail(&neigh->queue, skb);
			else {
				ipoib_warn(priv, "queue length limit %d. Packet drop.\n",
					   skb_queue_len(&neigh->queue));
				goto err_drop;
			}
		} else
			ipoib_send(dev, skb, path->ah, IPOIB_QPN(skb->dst->neighbour->ha));
	} else {
		neigh->ah  = NULL;

		if (!path->query && path_rec_start(dev, path))
			goto err_list;

		__skb_queue_tail(&neigh->queue, skb);
	}

	spin_unlock(&priv->lock);
	return;

err_list:
	list_del(&neigh->list);

err_path:
	ipoib_neigh_free(dev, neigh);
err_drop:
	++priv->stats.tx_dropped;
	dev_kfree_skb_any(skb);

	spin_unlock(&priv->lock);
}

static void ipoib_path_lookup(struct sk_buff *skb, struct net_device *dev)
{
	struct ipoib_dev_priv *priv = netdev_priv(skb->dev);

	/* Look up path record for unicasts */
	if (skb->dst->neighbour->ha[4] != 0xff) {
		neigh_add_path(skb, dev);
		return;
	}

	/* Add in the P_Key for multicasts */
	skb->dst->neighbour->ha[8] = (priv->pkey >> 8) & 0xff;
	skb->dst->neighbour->ha[9] = priv->pkey & 0xff;
	ipoib_mcast_send(dev, skb->dst->neighbour->ha + 4, skb);
}

static void unicast_arp_send(struct sk_buff *skb, struct net_device *dev,
			     struct ipoib_pseudoheader *phdr)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);
	struct ipoib_path *path;

	/*
	 * We can only be called from ipoib_start_xmit, so we're
	 * inside tx_lock -- no need to save/restore flags.
	 */
	spin_lock(&priv->lock);

	path = __path_find(dev, phdr->hwaddr + 4);
	if (!path) {
		path = path_rec_create(dev, phdr->hwaddr + 4);
		if (path) {
			/* put pseudoheader back on for next time */
			skb_push(skb, sizeof *phdr);
			__skb_queue_tail(&path->queue, skb);

			if (path_rec_start(dev, path)) {
				spin_unlock(&priv->lock);
				path_free(dev, path);
				return;
			} else
				__path_add(dev, path);
		} else {
			++priv->stats.tx_dropped;
			dev_kfree_skb_any(skb);
		}

		spin_unlock(&priv->lock);
		return;
	}

	if (path->ah) {
		ipoib_dbg(priv, "Send unicast ARP to %04x\n",
			  be16_to_cpu(path->pathrec.dlid));

		ipoib_send(dev, skb, path->ah, IPOIB_QPN(phdr->hwaddr));
	} else if ((path->query || !path_rec_start(dev, path)) &&
		   skb_queue_len(&path->queue) < IPOIB_MAX_PATH_REC_QUEUE) {
		/* put pseudoheader back on for next time */
		skb_push(skb, sizeof *phdr);
		__skb_queue_tail(&path->queue, skb);
	} else {
		++priv->stats.tx_dropped;
		dev_kfree_skb_any(skb);
	}

	spin_unlock(&priv->lock);
}

static int ipoib_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);
	struct ipoib_neigh *neigh;
	unsigned long flags;

	if (unlikely(!spin_trylock_irqsave(&priv->tx_lock, flags)))
		return NETDEV_TX_LOCKED;

	/*
	 * Check if our queue is stopped.  Since we have the LLTX bit
	 * set, we can't rely on netif_stop_queue() preventing our
	 * xmit function from being called with a full queue.
	 */
	if (unlikely(netif_queue_stopped(dev))) {
		spin_unlock_irqrestore(&priv->tx_lock, flags);
		return NETDEV_TX_BUSY;
	}

	if (likely(skb->dst && skb->dst->neighbour)) {
		if (unlikely(!*to_ipoib_neigh(skb->dst->neighbour))) {
			ipoib_path_lookup(skb, dev);
			goto out;
		}

		neigh = *to_ipoib_neigh(skb->dst->neighbour);

		if (ipoib_cm_get(neigh)) {
			if (ipoib_cm_up(neigh)) {
				ipoib_cm_send(dev, skb, ipoib_cm_get(neigh));
				goto out;
			}
		} else if (neigh->ah) {
			if (unlikely(memcmp(&neigh->dgid.raw,
					    skb->dst->neighbour->ha + 4,
					    sizeof(union ib_gid)))) {
				spin_lock(&priv->lock);
				/*
				 * It's safe to call ipoib_put_ah() inside
				 * priv->lock here, because we know that
				 * path->ah will always hold one more reference,
				 * so ipoib_put_ah() will never do more than
				 * decrement the ref count.
				 */
				ipoib_put_ah(neigh->ah);
				list_del(&neigh->list);
				ipoib_neigh_free(dev, neigh);
				spin_unlock(&priv->lock);
				ipoib_path_lookup(skb, dev);
				goto out;
			}

			ipoib_send(dev, skb, neigh->ah, IPOIB_QPN(skb->dst->neighbour->ha));
			goto out;
		}

		if (skb_queue_len(&neigh->queue) < IPOIB_MAX_PATH_REC_QUEUE) {
			spin_lock(&priv->lock);
			__skb_queue_tail(&neigh->queue, skb);
			spin_unlock(&priv->lock);
		} else {
			++priv->stats.tx_dropped;
			dev_kfree_skb_any(skb);
		}
	} else {
		struct ipoib_pseudoheader *phdr =
			(struct ipoib_pseudoheader *) skb->data;
		skb_pull(skb, sizeof *phdr);

		if (phdr->hwaddr[4] == 0xff) {
			/* Add in the P_Key for multicast*/
			phdr->hwaddr[8] = (priv->pkey >> 8) & 0xff;
			phdr->hwaddr[9] = priv->pkey & 0xff;

			ipoib_mcast_send(dev, phdr->hwaddr + 4, skb);
		} else {
			/* unicast GID -- should be ARP or RARP reply */

			if ((be16_to_cpup((__be16 *) skb->data) != ETH_P_ARP) &&
			    (be16_to_cpup((__be16 *) skb->data) != ETH_P_RARP)) {
				ipoib_warn(priv, "Unicast, no %s: type %04x, QPN %06x "
					   IPOIB_GID_FMT "\n",
					   skb->dst ? "neigh" : "dst",
					   be16_to_cpup((__be16 *) skb->data),
					   IPOIB_QPN(phdr->hwaddr),
					   IPOIB_GID_RAW_ARG(phdr->hwaddr + 4));
				dev_kfree_skb_any(skb);
				++priv->stats.tx_dropped;
				goto out;
			}

			unicast_arp_send(skb, dev, phdr);
		}
	}

out:
	spin_unlock_irqrestore(&priv->tx_lock, flags);

	return NETDEV_TX_OK;
}

static struct net_device_stats *ipoib_get_stats(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);

	return &priv->stats;
}

static void ipoib_timeout(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);

	ipoib_warn(priv, "transmit timeout: latency %d msecs\n",
		   jiffies_to_msecs(jiffies - dev->trans_start));
	ipoib_warn(priv, "queue stopped %d, tx_head %u, tx_tail %u\n",
		   netif_queue_stopped(dev),
		   priv->tx_head, priv->tx_tail);
	/* XXX reset QP, etc. */
}

static int ipoib_hard_header(struct sk_buff *skb,
			     struct net_device *dev,
			     unsigned short type,
			     void *daddr, void *saddr, unsigned len)
{
	struct ipoib_header *header;

	header = (struct ipoib_header *) skb_push(skb, sizeof *header);

	header->proto = htons(type);
	header->reserved = 0;

	/*
	 * If we don't have a neighbour structure, stuff the
	 * destination address onto the front of the skb so we can
	 * figure out where to send the packet later.
	 */
	if ((!skb->dst || !skb->dst->neighbour) && daddr) {
		struct ipoib_pseudoheader *phdr =
			(struct ipoib_pseudoheader *) skb_push(skb, sizeof *phdr);
		memcpy(phdr->hwaddr, daddr, INFINIBAND_ALEN);
	}

	return 0;
}

static void ipoib_set_mcast_list(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);

	if (!test_bit(IPOIB_FLAG_OPER_UP, &priv->flags)) {
		ipoib_dbg(priv, "IPOIB_FLAG_OPER_UP not set");
		return;
	}

	queue_work(ipoib_workqueue, &priv->restart_task);
}

static void ipoib_neigh_cleanup(struct neighbour *n)
{
	struct ipoib_neigh *neigh;
	struct ipoib_dev_priv *priv = netdev_priv(n->dev);
	unsigned long flags;
	struct ipoib_ah *ah = NULL;

	ipoib_dbg(priv,
		  "neigh_cleanup for %06x " IPOIB_GID_FMT "\n",
		  IPOIB_QPN(n->ha),
		  IPOIB_GID_RAW_ARG(n->ha + 4));

	spin_lock_irqsave(&priv->lock, flags);

	neigh = *to_ipoib_neigh(n);
	if (neigh) {
		if (neigh->ah)
			ah = neigh->ah;
		list_del(&neigh->list);
		ipoib_neigh_free(n->dev, neigh);
	}

	spin_unlock_irqrestore(&priv->lock, flags);

	if (ah)
		ipoib_put_ah(ah);
}

struct ipoib_neigh *ipoib_neigh_alloc(struct neighbour *neighbour)
{
	struct ipoib_neigh *neigh;

	neigh = kmalloc(sizeof *neigh, GFP_ATOMIC);
	if (!neigh)
		return NULL;

	neigh->neighbour = neighbour;
	*to_ipoib_neigh(neighbour) = neigh;
	skb_queue_head_init(&neigh->queue);
	ipoib_cm_set(neigh, NULL);

	return neigh;
}

void ipoib_neigh_free(struct net_device *dev, struct ipoib_neigh *neigh)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);
	struct sk_buff *skb;
	*to_ipoib_neigh(neigh->neighbour) = NULL;
	while ((skb = __skb_dequeue(&neigh->queue))) {
		++priv->stats.tx_dropped;
		dev_kfree_skb_any(skb);
	}
	if (ipoib_cm_get(neigh))
		ipoib_cm_destroy_tx(ipoib_cm_get(neigh));
	kfree(neigh);
}

static int ipoib_neigh_setup_dev(struct net_device *dev, struct neigh_parms *parms)
{
	parms->neigh_cleanup = ipoib_neigh_cleanup;

	return 0;
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

	priv->tx_ring = kzalloc(ipoib_sendq_size * sizeof *priv->tx_ring,
				GFP_KERNEL);
	if (!priv->tx_ring) {
		printk(KERN_WARNING "%s: failed to allocate TX ring (%d entries)\n",
		       ca->name, ipoib_sendq_size);
		goto out_rx_ring_cleanup;
	}

	/* priv->tx_head & tx_tail are already 0 */

	if (ipoib_ib_dev_init(dev, ca, port))
		goto out_tx_ring_cleanup;

	return 0;

out_tx_ring_cleanup:
	kfree(priv->tx_ring);

out_rx_ring_cleanup:
	kfree(priv->rx_ring);

out:
	return -ENOMEM;
}

void ipoib_dev_cleanup(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev), *cpriv, *tcpriv;

	ipoib_delete_debug_files(dev);

	/* Delete any child interfaces first */
	list_for_each_entry_safe(cpriv, tcpriv, &priv->child_intfs, list) {
		unregister_netdev(cpriv->dev);
		ipoib_dev_cleanup(cpriv->dev);
		free_netdev(cpriv->dev);
	}

	ipoib_ib_dev_cleanup(dev);

	kfree(priv->rx_ring);
	kfree(priv->tx_ring);

	priv->rx_ring = NULL;
	priv->tx_ring = NULL;
}

static void ipoib_setup(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);

	dev->open 		 = ipoib_open;
	dev->stop 		 = ipoib_stop;
	dev->change_mtu 	 = ipoib_change_mtu;
	dev->hard_start_xmit 	 = ipoib_start_xmit;
	dev->get_stats 		 = ipoib_get_stats;
	dev->tx_timeout 	 = ipoib_timeout;
	dev->hard_header 	 = ipoib_hard_header;
	dev->set_multicast_list  = ipoib_set_mcast_list;
	dev->neigh_setup         = ipoib_neigh_setup_dev;

	dev->watchdog_timeo 	 = HZ;

	dev->flags              |= IFF_BROADCAST | IFF_MULTICAST;

	/*
	 * We add in INFINIBAND_ALEN to allow for the destination
	 * address "pseudoheader" for skbs without neighbour struct.
	 */
	dev->hard_header_len 	 = IPOIB_ENCAP_LEN + INFINIBAND_ALEN;
	dev->addr_len 		 = INFINIBAND_ALEN;
	dev->type 		 = ARPHRD_INFINIBAND;
	dev->tx_queue_len 	 = ipoib_sendq_size * 2;
	dev->features            = NETIF_F_VLAN_CHALLENGED | NETIF_F_LLTX;

	/* MTU will be reset when mcast join happens */
	dev->mtu 		 = IPOIB_PACKET_SIZE - IPOIB_ENCAP_LEN;
	priv->mcast_mtu 	 = priv->admin_mtu = dev->mtu;

	memcpy(dev->broadcast, ipv4_bcast_addr, INFINIBAND_ALEN);

	netif_carrier_off(dev);

	SET_MODULE_OWNER(dev);

	priv->dev = dev;

	spin_lock_init(&priv->lock);
	spin_lock_init(&priv->tx_lock);

	mutex_init(&priv->mcast_mutex);
	mutex_init(&priv->vlan_mutex);

	INIT_LIST_HEAD(&priv->path_list);
	INIT_LIST_HEAD(&priv->child_intfs);
	INIT_LIST_HEAD(&priv->dead_ahs);
	INIT_LIST_HEAD(&priv->multicast_list);

	INIT_DELAYED_WORK(&priv->pkey_task,    ipoib_pkey_poll);
	INIT_DELAYED_WORK(&priv->mcast_task,   ipoib_mcast_join_task);
	INIT_WORK(&priv->flush_task,   ipoib_ib_dev_flush);
	INIT_WORK(&priv->restart_task, ipoib_mcast_restart_task);
	INIT_DELAYED_WORK(&priv->ah_reap_task, ipoib_reap_ah);
}

struct ipoib_dev_priv *ipoib_intf_alloc(const char *name)
{
	struct net_device *dev;

	dev = alloc_netdev((int) sizeof (struct ipoib_dev_priv), name,
			   ipoib_setup);
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

static ssize_t create_child(struct device *dev,
			    struct device_attribute *attr,
			    const char *buf, size_t count)
{
	int pkey;
	int ret;

	if (sscanf(buf, "%i", &pkey) != 1)
		return -EINVAL;

	if (pkey < 0 || pkey > 0xffff)
		return -EINVAL;

	/*
	 * Set the full membership bit, so that we join the right
	 * broadcast group, etc.
	 */
	pkey |= 0x8000;

	ret = ipoib_vlan_add(to_net_dev(dev), pkey);

	return ret ? ret : count;
}
static DEVICE_ATTR(create_child, S_IWUGO, NULL, create_child);

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
static DEVICE_ATTR(delete_child, S_IWUGO, NULL, delete_child);

int ipoib_add_pkey_attr(struct net_device *dev)
{
	return device_create_file(&dev->dev, &dev_attr_pkey);
}

static struct net_device *ipoib_add_port(const char *format,
					 struct ib_device *hca, u8 port)
{
	struct ipoib_dev_priv *priv;
	int result = -ENOMEM;

	priv = ipoib_intf_alloc(format);
	if (!priv)
		goto alloc_mem_failed;

	SET_NETDEV_DEV(priv->dev, hca->dma_device);

	result = ib_query_pkey(hca, port, 0, &priv->pkey);
	if (result) {
		printk(KERN_WARNING "%s: ib_query_pkey port %d failed (ret = %d)\n",
		       hca->name, port, result);
		goto alloc_mem_failed;
	}

	/*
	 * Set the full membership bit, so that we join the right
	 * broadcast group, etc.
	 */
	priv->pkey |= 0x8000;

	priv->dev->broadcast[8] = priv->pkey >> 8;
	priv->dev->broadcast[9] = priv->pkey & 0xff;

	result = ib_query_gid(hca, port, 0, &priv->local_gid);
	if (result) {
		printk(KERN_WARNING "%s: ib_query_gid port %d failed (ret = %d)\n",
		       hca->name, port, result);
		goto alloc_mem_failed;
	} else
		memcpy(priv->dev->dev_addr + 4, priv->local_gid.raw, sizeof (union ib_gid));


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
	flush_scheduled_work();

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
	int s, e, p;

	if (rdma_node_get_transport(device->node_type) != RDMA_TRANSPORT_IB)
		return;

	dev_list = kmalloc(sizeof *dev_list, GFP_KERNEL);
	if (!dev_list)
		return;

	INIT_LIST_HEAD(dev_list);

	if (device->node_type == RDMA_NODE_IB_SWITCH) {
		s = 0;
		e = 0;
	} else {
		s = 1;
		e = device->phys_port_cnt;
	}

	for (p = s; p <= e; ++p) {
		dev = ipoib_add_port("ib%d", device, p);
		if (!IS_ERR(dev)) {
			priv = netdev_priv(dev);
			list_add_tail(&priv->list, dev_list);
		}
	}

	ib_set_client_data(device, &ipoib_client, dev_list);
}

static void ipoib_remove_one(struct ib_device *device)
{
	struct ipoib_dev_priv *priv, *tmp;
	struct list_head *dev_list;

	if (rdma_node_get_transport(device->node_type) != RDMA_TRANSPORT_IB)
		return;

	dev_list = ib_get_client_data(device, &ipoib_client);

	list_for_each_entry_safe(priv, tmp, dev_list, list) {
		ib_unregister_event_handler(&priv->event_handler);
		flush_scheduled_work();

		unregister_netdev(priv->dev);
		ipoib_dev_cleanup(priv->dev);
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
	ipoib_sendq_size = max(ipoib_sendq_size, IPOIB_MIN_QUEUE_SIZE);

	ret = ipoib_register_debugfs();
	if (ret)
		return ret;

	/*
	 * We create our own workqueue mainly because we want to be
	 * able to flush it when devices are being removed.  We can't
	 * use schedule_work()/flush_scheduled_work() because both
	 * unregister_netdev() and linkwatch_event take the rtnl lock,
	 * so flush_scheduled_work() can deadlock during device
	 * removal.
	 */
	ipoib_workqueue = create_singlethread_workqueue("ipoib");
	if (!ipoib_workqueue) {
		ret = -ENOMEM;
		goto err_fs;
	}

	ib_sa_register_client(&ipoib_sa_client);

	ret = ib_register_client(&ipoib_client);
	if (ret)
		goto err_sa;

	return 0;

err_sa:
	ib_sa_unregister_client(&ipoib_sa_client);
	destroy_workqueue(ipoib_workqueue);

err_fs:
	ipoib_unregister_debugfs();

	return ret;
}

static void __exit ipoib_cleanup_module(void)
{
	ib_unregister_client(&ipoib_client);
	ib_sa_unregister_client(&ipoib_sa_client);
	ipoib_unregister_debugfs();
	destroy_workqueue(ipoib_workqueue);
}

module_init(ipoib_init_module);
module_exit(ipoib_cleanup_module);
