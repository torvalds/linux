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
#include <linux/vmalloc.h>

#include <linux/if_arp.h>	/* For ARPHRD_xxx */

#include <linux/ip.h>
#include <linux/in.h>

MODULE_AUTHOR("Roland Dreier");
MODULE_DESCRIPTION("IP-over-InfiniBand net driver");
MODULE_LICENSE("Dual BSD/GPL");

#ifdef CONFIG_INFINIBAND_IPOIB_DEBUG
int ipoib_debug_level;

module_param_named(debug_level, ipoib_debug_level, int, 0644);
MODULE_PARM_DESC(debug_level, "Enable debug tracing if > 0");
#endif

static const u8 ipv4_bcast_addr[] = {
	0x00, 0xff, 0xff, 0xff,
	0xff, 0x12, 0x40, 0x1b,	0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00,	0xff, 0xff, 0xff, 0xff
};

struct workqueue_struct *ipoib_workqueue;

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

	if (ipoib_ib_dev_up(dev))
		return -EINVAL;

	if (!test_bit(IPOIB_FLAG_SUBINTERFACE, &priv->flags)) {
		struct ipoib_dev_priv *cpriv;

		/* Bring up any child interfaces too */
		down(&priv->vlan_mutex);
		list_for_each_entry(cpriv, &priv->child_intfs, list) {
			int flags;

			flags = cpriv->dev->flags;
			if (flags & IFF_UP)
				continue;

			dev_change_flags(cpriv->dev, flags | IFF_UP);
		}
		up(&priv->vlan_mutex);
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

	ipoib_ib_dev_down(dev);
	ipoib_ib_dev_stop(dev);

	if (!test_bit(IPOIB_FLAG_SUBINTERFACE, &priv->flags)) {
		struct ipoib_dev_priv *cpriv;

		/* Bring down any child interfaces too */
		down(&priv->vlan_mutex);
		list_for_each_entry(cpriv, &priv->child_intfs, list) {
			int flags;

			flags = cpriv->dev->flags;
			if (!(flags & IFF_UP))
				continue;

			dev_change_flags(cpriv->dev, flags & ~IFF_UP);
		}
		up(&priv->vlan_mutex);
	}

	return 0;
}

static int ipoib_change_mtu(struct net_device *dev, int new_mtu)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);

	if (new_mtu > IPOIB_PACKET_SIZE - IPOIB_ENCAP_LEN)
		return -EINVAL;

	priv->admin_mtu = new_mtu;

	dev->mtu = min(priv->mcast_mtu, priv->admin_mtu);

	return 0;
}

static struct ipoib_path *__path_find(struct net_device *dev,
				      union ib_gid *gid)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);
	struct rb_node *n = priv->path_tree.rb_node;
	struct ipoib_path *path;
	int ret;

	while (n) {
		path = rb_entry(n, struct ipoib_path, rb_node);

		ret = memcmp(gid->raw, path->pathrec.dgid.raw,
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
		*to_ipoib_neigh(neigh->neighbour) = NULL;
		neigh->neighbour->ops->destructor = NULL;
		kfree(neigh);
	}

	spin_unlock_irqrestore(&priv->lock, flags);

	if (path->ah)
		ipoib_put_ah(path->ah);

	kfree(path);
}

void ipoib_flush_paths(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);
	struct ipoib_path *path, *tp;
	LIST_HEAD(remove_list);
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);

	list_splice(&priv->path_list, &remove_list);
	INIT_LIST_HEAD(&priv->path_list);

	list_for_each_entry(path, &remove_list, list)
		rb_erase(&path->rb_node, &priv->path_tree);

	spin_unlock_irqrestore(&priv->lock, flags);

	list_for_each_entry_safe(path, tp, &remove_list, list) {
		if (path->query)
			ib_sa_cancel_query(path->query_id, path->query);
		wait_for_completion(&path->done);
		path_free(dev, path);
	}
}

static void path_rec_completion(int status,
				struct ib_sa_path_rec *pathrec,
				void *path_ptr)
{
	struct ipoib_path *path = path_ptr;
	struct net_device *dev = path->dev;
	struct ipoib_dev_priv *priv = netdev_priv(dev);
	struct ipoib_ah *ah = NULL;
	struct ipoib_neigh *neigh;
	struct sk_buff_head skqueue;
	struct sk_buff *skb;
	unsigned long flags;

	if (pathrec)
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
			.port_num      = priv->port
		};
		int path_rate = ib_sa_rate_enum_to_int(pathrec->rate);

		if (path_rate > 0 && priv->local_rate > path_rate)
			av.static_rate = (priv->local_rate - 1) / path_rate;

		ipoib_dbg(priv, "static_rate %d for local port %dX, path %dX\n",
			  av.static_rate, priv->local_rate,
			  ib_sa_rate_enum_to_int(pathrec->rate));

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

		list_for_each_entry(neigh, &path->neigh_list, list) {
			kref_get(&path->ah->ref);
			neigh->ah = path->ah;

			while ((skb = __skb_dequeue(&neigh->queue)))
				__skb_queue_tail(&skqueue, skb);
		}
	} else
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

static struct ipoib_path *path_rec_create(struct net_device *dev,
					  union ib_gid *gid)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);
	struct ipoib_path *path;

	path = kmalloc(sizeof *path, GFP_ATOMIC);
	if (!path)
		return NULL;

	path->dev          = dev;
	path->pathrec.dlid = 0;
	path->ah           = NULL;

	skb_queue_head_init(&path->queue);

	INIT_LIST_HEAD(&path->neigh_list);
	path->query = NULL;
	init_completion(&path->done);

	memcpy(path->pathrec.dgid.raw, gid->raw, sizeof (union ib_gid));
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

	path->query_id =
		ib_sa_path_rec_get(priv->ca, priv->port,
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

	neigh = kmalloc(sizeof *neigh, GFP_ATOMIC);
	if (!neigh) {
		++priv->stats.tx_dropped;
		dev_kfree_skb_any(skb);
		return;
	}

	skb_queue_head_init(&neigh->queue);
	neigh->neighbour = skb->dst->neighbour;
	*to_ipoib_neigh(skb->dst->neighbour) = neigh;

	/*
	 * We can only be called from ipoib_start_xmit, so we're
	 * inside tx_lock -- no need to save/restore flags.
	 */
	spin_lock(&priv->lock);

	path = __path_find(dev, (union ib_gid *) (skb->dst->neighbour->ha + 4));
	if (!path) {
		path = path_rec_create(dev,
				       (union ib_gid *) (skb->dst->neighbour->ha + 4));
		if (!path)
			goto err;

		__path_add(dev, path);
	}

	list_add_tail(&neigh->list, &path->neigh_list);

	if (path->pathrec.dlid) {
		kref_get(&path->ah->ref);
		neigh->ah = path->ah;

		ipoib_send(dev, skb, path->ah,
			   be32_to_cpup((__be32 *) skb->dst->neighbour->ha));
	} else {
		neigh->ah  = NULL;
		if (skb_queue_len(&neigh->queue) < IPOIB_MAX_PATH_REC_QUEUE) {
			__skb_queue_tail(&neigh->queue, skb);
		} else {
			++priv->stats.tx_dropped;
			dev_kfree_skb_any(skb);
		}

		if (!path->query && path_rec_start(dev, path))
			goto err;
	}

	spin_unlock(&priv->lock);
	return;

err:
	*to_ipoib_neigh(skb->dst->neighbour) = NULL;
	list_del(&neigh->list);
	neigh->neighbour->ops->destructor = NULL;
	kfree(neigh);

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
	ipoib_mcast_send(dev, (union ib_gid *) (skb->dst->neighbour->ha + 4), skb);
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

	path = __path_find(dev, (union ib_gid *) (phdr->hwaddr + 4));
	if (!path) {
		path = path_rec_create(dev,
				       (union ib_gid *) (phdr->hwaddr + 4));
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

	if (path->pathrec.dlid) {
		ipoib_dbg(priv, "Send unicast ARP to %04x\n",
			  be16_to_cpu(path->pathrec.dlid));

		ipoib_send(dev, skb, path->ah,
			   be32_to_cpup((__be32 *) phdr->hwaddr));
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

	local_irq_save(flags);
	if (!spin_trylock(&priv->tx_lock)) {
		local_irq_restore(flags);
		return NETDEV_TX_LOCKED;
	}

	/*
	 * Check if our queue is stopped.  Since we have the LLTX bit
	 * set, we can't rely on netif_stop_queue() preventing our
	 * xmit function from being called with a full queue.
	 */
	if (unlikely(netif_queue_stopped(dev))) {
		spin_unlock_irqrestore(&priv->tx_lock, flags);
		return NETDEV_TX_BUSY;
	}

	if (skb->dst && skb->dst->neighbour) {
		if (unlikely(!*to_ipoib_neigh(skb->dst->neighbour))) {
			ipoib_path_lookup(skb, dev);
			goto out;
		}

		neigh = *to_ipoib_neigh(skb->dst->neighbour);

		if (likely(neigh->ah)) {
			ipoib_send(dev, skb, neigh->ah,
				   be32_to_cpup((__be32 *) skb->dst->neighbour->ha));
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

			ipoib_mcast_send(dev, (union ib_gid *) (phdr->hwaddr + 4), skb);
		} else {
			/* unicast GID -- should be ARP or RARP reply */

			if ((be16_to_cpup((__be16 *) skb->data) != ETH_P_ARP) &&
			    (be16_to_cpup((__be16 *) skb->data) != ETH_P_RARP)) {
				ipoib_warn(priv, "Unicast, no %s: type %04x, QPN %06x "
					   IPOIB_GID_FMT "\n",
					   skb->dst ? "neigh" : "dst",
					   be16_to_cpup((__be16 *) skb->data),
					   be32_to_cpup((__be32 *) phdr->hwaddr),
					   IPOIB_GID_ARG(*(union ib_gid *) (phdr->hwaddr + 4)));
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
	if (!skb->dst || !skb->dst->neighbour) {
		struct ipoib_pseudoheader *phdr =
			(struct ipoib_pseudoheader *) skb_push(skb, sizeof *phdr);
		memcpy(phdr->hwaddr, daddr, INFINIBAND_ALEN);
	}

	return 0;
}

static void ipoib_set_mcast_list(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);

	queue_work(ipoib_workqueue, &priv->restart_task);
}

static void ipoib_neigh_destructor(struct neighbour *n)
{
	struct ipoib_neigh *neigh;
	struct ipoib_dev_priv *priv = netdev_priv(n->dev);
	unsigned long flags;
	struct ipoib_ah *ah = NULL;

	ipoib_dbg(priv,
		  "neigh_destructor for %06x " IPOIB_GID_FMT "\n",
		  be32_to_cpup((__be32 *) n->ha),
		  IPOIB_GID_ARG(*((union ib_gid *) (n->ha + 4))));

	spin_lock_irqsave(&priv->lock, flags);

	neigh = *to_ipoib_neigh(n);
	if (neigh) {
		if (neigh->ah)
			ah = neigh->ah;
		list_del(&neigh->list);
		*to_ipoib_neigh(n) = NULL;
		kfree(neigh);
	}

	spin_unlock_irqrestore(&priv->lock, flags);

	if (ah)
		ipoib_put_ah(ah);
}

static int ipoib_neigh_setup(struct neighbour *neigh)
{
	/*
	 * Is this kosher?  I can't find anybody in the kernel that
	 * sets neigh->destructor, so we should be able to set it here
	 * without trouble.
	 */
	neigh->ops->destructor = ipoib_neigh_destructor;

	return 0;
}

static int ipoib_neigh_setup_dev(struct net_device *dev, struct neigh_parms *parms)
{
	parms->neigh_setup = ipoib_neigh_setup;

	return 0;
}

int ipoib_dev_init(struct net_device *dev, struct ib_device *ca, int port)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);

	/* Allocate RX/TX "rings" to hold queued skbs */

	priv->rx_ring =	kmalloc(IPOIB_RX_RING_SIZE * sizeof (struct ipoib_rx_buf),
				GFP_KERNEL);
	if (!priv->rx_ring) {
		printk(KERN_WARNING "%s: failed to allocate RX ring (%d entries)\n",
		       ca->name, IPOIB_RX_RING_SIZE);
		goto out;
	}
	memset(priv->rx_ring, 0,
	       IPOIB_RX_RING_SIZE * sizeof (struct ipoib_rx_buf));

	priv->tx_ring = kmalloc(IPOIB_TX_RING_SIZE * sizeof (struct ipoib_tx_buf),
				GFP_KERNEL);
	if (!priv->tx_ring) {
		printk(KERN_WARNING "%s: failed to allocate TX ring (%d entries)\n",
		       ca->name, IPOIB_TX_RING_SIZE);
		goto out_rx_ring_cleanup;
	}
	memset(priv->tx_ring, 0,
	       IPOIB_TX_RING_SIZE * sizeof (struct ipoib_tx_buf));

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

	ipoib_delete_debug_file(dev);

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

	dev->rebuild_header 	 = NULL;
	dev->set_mac_address 	 = NULL;
	dev->header_cache_update = NULL;

	dev->flags              |= IFF_BROADCAST | IFF_MULTICAST;

	/*
	 * We add in INFINIBAND_ALEN to allow for the destination
	 * address "pseudoheader" for skbs without neighbour struct.
	 */
	dev->hard_header_len 	 = IPOIB_ENCAP_LEN + INFINIBAND_ALEN;
	dev->addr_len 		 = INFINIBAND_ALEN;
	dev->type 		 = ARPHRD_INFINIBAND;
	dev->tx_queue_len 	 = IPOIB_TX_RING_SIZE * 2;
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

	init_MUTEX(&priv->mcast_mutex);
	init_MUTEX(&priv->vlan_mutex);

	INIT_LIST_HEAD(&priv->path_list);
	INIT_LIST_HEAD(&priv->child_intfs);
	INIT_LIST_HEAD(&priv->dead_ahs);
	INIT_LIST_HEAD(&priv->multicast_list);

	INIT_WORK(&priv->pkey_task,    ipoib_pkey_poll,          priv->dev);
	INIT_WORK(&priv->mcast_task,   ipoib_mcast_join_task,    priv->dev);
	INIT_WORK(&priv->flush_task,   ipoib_ib_dev_flush,       priv->dev);
	INIT_WORK(&priv->restart_task, ipoib_mcast_restart_task, priv->dev);
	INIT_WORK(&priv->ah_reap_task, ipoib_reap_ah,            priv->dev);
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

static ssize_t show_pkey(struct class_device *cdev, char *buf)
{
	struct ipoib_dev_priv *priv =
		netdev_priv(container_of(cdev, struct net_device, class_dev));

	return sprintf(buf, "0x%04x\n", priv->pkey);
}
static CLASS_DEVICE_ATTR(pkey, S_IRUGO, show_pkey, NULL);

static ssize_t create_child(struct class_device *cdev,
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

	ret = ipoib_vlan_add(container_of(cdev, struct net_device, class_dev),
			     pkey);

	return ret ? ret : count;
}
static CLASS_DEVICE_ATTR(create_child, S_IWUGO, NULL, create_child);

static ssize_t delete_child(struct class_device *cdev,
			    const char *buf, size_t count)
{
	int pkey;
	int ret;

	if (sscanf(buf, "%i", &pkey) != 1)
		return -EINVAL;

	if (pkey < 0 || pkey > 0xffff)
		return -EINVAL;

	ret = ipoib_vlan_delete(container_of(cdev, struct net_device, class_dev),
				pkey);

	return ret ? ret : count;

}
static CLASS_DEVICE_ATTR(delete_child, S_IWUGO, NULL, delete_child);

int ipoib_add_pkey_attr(struct net_device *dev)
{
	return class_device_create_file(&dev->class_dev,
					&class_device_attr_pkey);
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

	if (ipoib_create_debug_file(priv->dev))
		goto debug_failed;

	if (ipoib_add_pkey_attr(priv->dev))
		goto sysfs_failed;
	if (class_device_create_file(&priv->dev->class_dev,
				     &class_device_attr_create_child))
		goto sysfs_failed;
	if (class_device_create_file(&priv->dev->class_dev,
				     &class_device_attr_delete_child))
		goto sysfs_failed;

	return priv->dev;

sysfs_failed:
	ipoib_delete_debug_file(priv->dev);

debug_failed:
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

	dev_list = kmalloc(sizeof *dev_list, GFP_KERNEL);
	if (!dev_list)
		return;

	INIT_LIST_HEAD(dev_list);

	if (device->node_type == IB_NODE_SWITCH) {
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

	ret = ib_register_client(&ipoib_client);
	if (ret)
		goto err_wq;

	return 0;

err_wq:
	destroy_workqueue(ipoib_workqueue);

err_fs:
	ipoib_unregister_debugfs();

	return ret;
}

static void __exit ipoib_cleanup_module(void)
{
	ib_unregister_client(&ipoib_client);
	ipoib_unregister_debugfs();
	destroy_workqueue(ipoib_workqueue);
}

module_init(ipoib_init_module);
module_exit(ipoib_cleanup_module);
