/*
 * Virtual network driver for conversing with remote driver backends.
 *
 * Copyright (c) 2002-2005, K A Fraser
 * Copyright (c) 2005, XenSource Ltd
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation; or, when distributed
 * separately from the Linux kernel or incorporated into other
 * software packages, subject to the following license:
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this source file (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy, modify,
 * merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/ethtool.h>
#include <linux/if_ether.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/moduleparam.h>
#include <linux/mm.h>
#include <net/ip.h>

#include <xen/xen.h>
#include <xen/xenbus.h>
#include <xen/events.h>
#include <xen/page.h>
#include <xen/grant_table.h>

#include <xen/interface/io/netif.h>
#include <xen/interface/memory.h>
#include <xen/interface/grant_table.h>

static const struct ethtool_ops xennet_ethtool_ops;

struct netfront_cb {
	struct page *page;
	unsigned offset;
};

#define NETFRONT_SKB_CB(skb)	((struct netfront_cb *)((skb)->cb))

#define RX_COPY_THRESHOLD 256

#define GRANT_INVALID_REF	0

#define NET_TX_RING_SIZE __RING_SIZE((struct xen_netif_tx_sring *)0, PAGE_SIZE)
#define NET_RX_RING_SIZE __RING_SIZE((struct xen_netif_rx_sring *)0, PAGE_SIZE)
#define TX_MAX_TARGET min_t(int, NET_RX_RING_SIZE, 256)

struct netfront_info {
	struct list_head list;
	struct net_device *netdev;

	struct napi_struct napi;

	unsigned int evtchn;
	struct xenbus_device *xbdev;

	spinlock_t   tx_lock;
	struct xen_netif_tx_front_ring tx;
	int tx_ring_ref;

	/*
	 * {tx,rx}_skbs store outstanding skbuffs. Free tx_skb entries
	 * are linked from tx_skb_freelist through skb_entry.link.
	 *
	 *  NB. Freelist index entries are always going to be less than
	 *  PAGE_OFFSET, whereas pointers to skbs will always be equal or
	 *  greater than PAGE_OFFSET: we use this property to distinguish
	 *  them.
	 */
	union skb_entry {
		struct sk_buff *skb;
		unsigned long link;
	} tx_skbs[NET_TX_RING_SIZE];
	grant_ref_t gref_tx_head;
	grant_ref_t grant_tx_ref[NET_TX_RING_SIZE];
	unsigned tx_skb_freelist;

	spinlock_t   rx_lock ____cacheline_aligned_in_smp;
	struct xen_netif_rx_front_ring rx;
	int rx_ring_ref;

	/* Receive-ring batched refills. */
#define RX_MIN_TARGET 8
#define RX_DFL_MIN_TARGET 64
#define RX_MAX_TARGET min_t(int, NET_RX_RING_SIZE, 256)
	unsigned rx_min_target, rx_max_target, rx_target;
	struct sk_buff_head rx_batch;

	struct timer_list rx_refill_timer;

	struct sk_buff *rx_skbs[NET_RX_RING_SIZE];
	grant_ref_t gref_rx_head;
	grant_ref_t grant_rx_ref[NET_RX_RING_SIZE];

	unsigned long rx_pfn_array[NET_RX_RING_SIZE];
	struct multicall_entry rx_mcl[NET_RX_RING_SIZE+1];
	struct mmu_update rx_mmu[NET_RX_RING_SIZE];
};

struct netfront_rx_info {
	struct xen_netif_rx_response rx;
	struct xen_netif_extra_info extras[XEN_NETIF_EXTRA_TYPE_MAX - 1];
};

static void skb_entry_set_link(union skb_entry *list, unsigned short id)
{
	list->link = id;
}

static int skb_entry_is_link(const union skb_entry *list)
{
	BUILD_BUG_ON(sizeof(list->skb) != sizeof(list->link));
	return ((unsigned long)list->skb < PAGE_OFFSET);
}

/*
 * Access macros for acquiring freeing slots in tx_skbs[].
 */

static void add_id_to_freelist(unsigned *head, union skb_entry *list,
			       unsigned short id)
{
	skb_entry_set_link(&list[id], *head);
	*head = id;
}

static unsigned short get_id_from_freelist(unsigned *head,
					   union skb_entry *list)
{
	unsigned int id = *head;
	*head = list[id].link;
	return id;
}

static int xennet_rxidx(RING_IDX idx)
{
	return idx & (NET_RX_RING_SIZE - 1);
}

static struct sk_buff *xennet_get_rx_skb(struct netfront_info *np,
					 RING_IDX ri)
{
	int i = xennet_rxidx(ri);
	struct sk_buff *skb = np->rx_skbs[i];
	np->rx_skbs[i] = NULL;
	return skb;
}

static grant_ref_t xennet_get_rx_ref(struct netfront_info *np,
					    RING_IDX ri)
{
	int i = xennet_rxidx(ri);
	grant_ref_t ref = np->grant_rx_ref[i];
	np->grant_rx_ref[i] = GRANT_INVALID_REF;
	return ref;
}

#ifdef CONFIG_SYSFS
static int xennet_sysfs_addif(struct net_device *netdev);
static void xennet_sysfs_delif(struct net_device *netdev);
#else /* !CONFIG_SYSFS */
#define xennet_sysfs_addif(dev) (0)
#define xennet_sysfs_delif(dev) do { } while (0)
#endif

static int xennet_can_sg(struct net_device *dev)
{
	return dev->features & NETIF_F_SG;
}


static void rx_refill_timeout(unsigned long data)
{
	struct net_device *dev = (struct net_device *)data;
	struct netfront_info *np = netdev_priv(dev);
	napi_schedule(&np->napi);
}

static int netfront_tx_slot_available(struct netfront_info *np)
{
	return ((np->tx.req_prod_pvt - np->tx.rsp_cons) <
		(TX_MAX_TARGET - MAX_SKB_FRAGS - 2));
}

static void xennet_maybe_wake_tx(struct net_device *dev)
{
	struct netfront_info *np = netdev_priv(dev);

	if (unlikely(netif_queue_stopped(dev)) &&
	    netfront_tx_slot_available(np) &&
	    likely(netif_running(dev)))
		netif_wake_queue(dev);
}

static void xennet_alloc_rx_buffers(struct net_device *dev)
{
	unsigned short id;
	struct netfront_info *np = netdev_priv(dev);
	struct sk_buff *skb;
	struct page *page;
	int i, batch_target, notify;
	RING_IDX req_prod = np->rx.req_prod_pvt;
	grant_ref_t ref;
	unsigned long pfn;
	void *vaddr;
	struct xen_netif_rx_request *req;

	if (unlikely(!netif_carrier_ok(dev)))
		return;

	/*
	 * Allocate skbuffs greedily, even though we batch updates to the
	 * receive ring. This creates a less bursty demand on the memory
	 * allocator, so should reduce the chance of failed allocation requests
	 * both for ourself and for other kernel subsystems.
	 */
	batch_target = np->rx_target - (req_prod - np->rx.rsp_cons);
	for (i = skb_queue_len(&np->rx_batch); i < batch_target; i++) {
		skb = __netdev_alloc_skb(dev, RX_COPY_THRESHOLD + NET_IP_ALIGN,
					 GFP_ATOMIC | __GFP_NOWARN);
		if (unlikely(!skb))
			goto no_skb;

		/* Align ip header to a 16 bytes boundary */
		skb_reserve(skb, NET_IP_ALIGN);

		page = alloc_page(GFP_ATOMIC | __GFP_NOWARN);
		if (!page) {
			kfree_skb(skb);
no_skb:
			/* Any skbuffs queued for refill? Force them out. */
			if (i != 0)
				goto refill;
			/* Could not allocate any skbuffs. Try again later. */
			mod_timer(&np->rx_refill_timer,
				  jiffies + (HZ/10));
			break;
		}

		skb_shinfo(skb)->frags[0].page = page;
		skb_shinfo(skb)->nr_frags = 1;
		__skb_queue_tail(&np->rx_batch, skb);
	}

	/* Is the batch large enough to be worthwhile? */
	if (i < (np->rx_target/2)) {
		if (req_prod > np->rx.sring->req_prod)
			goto push;
		return;
	}

	/* Adjust our fill target if we risked running out of buffers. */
	if (((req_prod - np->rx.sring->rsp_prod) < (np->rx_target / 4)) &&
	    ((np->rx_target *= 2) > np->rx_max_target))
		np->rx_target = np->rx_max_target;

 refill:
	for (i = 0; ; i++) {
		skb = __skb_dequeue(&np->rx_batch);
		if (skb == NULL)
			break;

		skb->dev = dev;

		id = xennet_rxidx(req_prod + i);

		BUG_ON(np->rx_skbs[id]);
		np->rx_skbs[id] = skb;

		ref = gnttab_claim_grant_reference(&np->gref_rx_head);
		BUG_ON((signed short)ref < 0);
		np->grant_rx_ref[id] = ref;

		pfn = page_to_pfn(skb_shinfo(skb)->frags[0].page);
		vaddr = page_address(skb_shinfo(skb)->frags[0].page);

		req = RING_GET_REQUEST(&np->rx, req_prod + i);
		gnttab_grant_foreign_access_ref(ref,
						np->xbdev->otherend_id,
						pfn_to_mfn(pfn),
						0);

		req->id = id;
		req->gref = ref;
	}

	wmb();		/* barrier so backend seens requests */

	/* Above is a suitable barrier to ensure backend will see requests. */
	np->rx.req_prod_pvt = req_prod + i;
 push:
	RING_PUSH_REQUESTS_AND_CHECK_NOTIFY(&np->rx, notify);
	if (notify)
		notify_remote_via_irq(np->netdev->irq);
}

static int xennet_open(struct net_device *dev)
{
	struct netfront_info *np = netdev_priv(dev);

	napi_enable(&np->napi);

	spin_lock_bh(&np->rx_lock);
	if (netif_carrier_ok(dev)) {
		xennet_alloc_rx_buffers(dev);
		np->rx.sring->rsp_event = np->rx.rsp_cons + 1;
		if (RING_HAS_UNCONSUMED_RESPONSES(&np->rx))
			napi_schedule(&np->napi);
	}
	spin_unlock_bh(&np->rx_lock);

	netif_start_queue(dev);

	return 0;
}

static void xennet_tx_buf_gc(struct net_device *dev)
{
	RING_IDX cons, prod;
	unsigned short id;
	struct netfront_info *np = netdev_priv(dev);
	struct sk_buff *skb;

	BUG_ON(!netif_carrier_ok(dev));

	do {
		prod = np->tx.sring->rsp_prod;
		rmb(); /* Ensure we see responses up to 'rp'. */

		for (cons = np->tx.rsp_cons; cons != prod; cons++) {
			struct xen_netif_tx_response *txrsp;

			txrsp = RING_GET_RESPONSE(&np->tx, cons);
			if (txrsp->status == NETIF_RSP_NULL)
				continue;

			id  = txrsp->id;
			skb = np->tx_skbs[id].skb;
			if (unlikely(gnttab_query_foreign_access(
				np->grant_tx_ref[id]) != 0)) {
				printk(KERN_ALERT "xennet_tx_buf_gc: warning "
				       "-- grant still in use by backend "
				       "domain.\n");
				BUG();
			}
			gnttab_end_foreign_access_ref(
				np->grant_tx_ref[id], GNTMAP_readonly);
			gnttab_release_grant_reference(
				&np->gref_tx_head, np->grant_tx_ref[id]);
			np->grant_tx_ref[id] = GRANT_INVALID_REF;
			add_id_to_freelist(&np->tx_skb_freelist, np->tx_skbs, id);
			dev_kfree_skb_irq(skb);
		}

		np->tx.rsp_cons = prod;

		/*
		 * Set a new event, then check for race with update of tx_cons.
		 * Note that it is essential to schedule a callback, no matter
		 * how few buffers are pending. Even if there is space in the
		 * transmit ring, higher layers may be blocked because too much
		 * data is outstanding: in such cases notification from Xen is
		 * likely to be the only kick that we'll get.
		 */
		np->tx.sring->rsp_event =
			prod + ((np->tx.sring->req_prod - prod) >> 1) + 1;
		mb();		/* update shared area */
	} while ((cons == prod) && (prod != np->tx.sring->rsp_prod));

	xennet_maybe_wake_tx(dev);
}

static void xennet_make_frags(struct sk_buff *skb, struct net_device *dev,
			      struct xen_netif_tx_request *tx)
{
	struct netfront_info *np = netdev_priv(dev);
	char *data = skb->data;
	unsigned long mfn;
	RING_IDX prod = np->tx.req_prod_pvt;
	int frags = skb_shinfo(skb)->nr_frags;
	unsigned int offset = offset_in_page(data);
	unsigned int len = skb_headlen(skb);
	unsigned int id;
	grant_ref_t ref;
	int i;

	/* While the header overlaps a page boundary (including being
	   larger than a page), split it it into page-sized chunks. */
	while (len > PAGE_SIZE - offset) {
		tx->size = PAGE_SIZE - offset;
		tx->flags |= NETTXF_more_data;
		len -= tx->size;
		data += tx->size;
		offset = 0;

		id = get_id_from_freelist(&np->tx_skb_freelist, np->tx_skbs);
		np->tx_skbs[id].skb = skb_get(skb);
		tx = RING_GET_REQUEST(&np->tx, prod++);
		tx->id = id;
		ref = gnttab_claim_grant_reference(&np->gref_tx_head);
		BUG_ON((signed short)ref < 0);

		mfn = virt_to_mfn(data);
		gnttab_grant_foreign_access_ref(ref, np->xbdev->otherend_id,
						mfn, GNTMAP_readonly);

		tx->gref = np->grant_tx_ref[id] = ref;
		tx->offset = offset;
		tx->size = len;
		tx->flags = 0;
	}

	/* Grant backend access to each skb fragment page. */
	for (i = 0; i < frags; i++) {
		skb_frag_t *frag = skb_shinfo(skb)->frags + i;

		tx->flags |= NETTXF_more_data;

		id = get_id_from_freelist(&np->tx_skb_freelist, np->tx_skbs);
		np->tx_skbs[id].skb = skb_get(skb);
		tx = RING_GET_REQUEST(&np->tx, prod++);
		tx->id = id;
		ref = gnttab_claim_grant_reference(&np->gref_tx_head);
		BUG_ON((signed short)ref < 0);

		mfn = pfn_to_mfn(page_to_pfn(frag->page));
		gnttab_grant_foreign_access_ref(ref, np->xbdev->otherend_id,
						mfn, GNTMAP_readonly);

		tx->gref = np->grant_tx_ref[id] = ref;
		tx->offset = frag->page_offset;
		tx->size = frag->size;
		tx->flags = 0;
	}

	np->tx.req_prod_pvt = prod;
}

static int xennet_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	unsigned short id;
	struct netfront_info *np = netdev_priv(dev);
	struct xen_netif_tx_request *tx;
	struct xen_netif_extra_info *extra;
	char *data = skb->data;
	RING_IDX i;
	grant_ref_t ref;
	unsigned long mfn;
	int notify;
	int frags = skb_shinfo(skb)->nr_frags;
	unsigned int offset = offset_in_page(data);
	unsigned int len = skb_headlen(skb);

	frags += DIV_ROUND_UP(offset + len, PAGE_SIZE);
	if (unlikely(frags > MAX_SKB_FRAGS + 1)) {
		printk(KERN_ALERT "xennet: skb rides the rocket: %d frags\n",
		       frags);
		dump_stack();
		goto drop;
	}

	spin_lock_irq(&np->tx_lock);

	if (unlikely(!netif_carrier_ok(dev) ||
		     (frags > 1 && !xennet_can_sg(dev)) ||
		     netif_needs_gso(dev, skb))) {
		spin_unlock_irq(&np->tx_lock);
		goto drop;
	}

	i = np->tx.req_prod_pvt;

	id = get_id_from_freelist(&np->tx_skb_freelist, np->tx_skbs);
	np->tx_skbs[id].skb = skb;

	tx = RING_GET_REQUEST(&np->tx, i);

	tx->id   = id;
	ref = gnttab_claim_grant_reference(&np->gref_tx_head);
	BUG_ON((signed short)ref < 0);
	mfn = virt_to_mfn(data);
	gnttab_grant_foreign_access_ref(
		ref, np->xbdev->otherend_id, mfn, GNTMAP_readonly);
	tx->gref = np->grant_tx_ref[id] = ref;
	tx->offset = offset;
	tx->size = len;
	extra = NULL;

	tx->flags = 0;
	if (skb->ip_summed == CHECKSUM_PARTIAL)
		/* local packet? */
		tx->flags |= NETTXF_csum_blank | NETTXF_data_validated;
	else if (skb->ip_summed == CHECKSUM_UNNECESSARY)
		/* remote but checksummed. */
		tx->flags |= NETTXF_data_validated;

	if (skb_shinfo(skb)->gso_size) {
		struct xen_netif_extra_info *gso;

		gso = (struct xen_netif_extra_info *)
			RING_GET_REQUEST(&np->tx, ++i);

		if (extra)
			extra->flags |= XEN_NETIF_EXTRA_FLAG_MORE;
		else
			tx->flags |= NETTXF_extra_info;

		gso->u.gso.size = skb_shinfo(skb)->gso_size;
		gso->u.gso.type = XEN_NETIF_GSO_TYPE_TCPV4;
		gso->u.gso.pad = 0;
		gso->u.gso.features = 0;

		gso->type = XEN_NETIF_EXTRA_TYPE_GSO;
		gso->flags = 0;
		extra = gso;
	}

	np->tx.req_prod_pvt = i + 1;

	xennet_make_frags(skb, dev, tx);
	tx->size = skb->len;

	RING_PUSH_REQUESTS_AND_CHECK_NOTIFY(&np->tx, notify);
	if (notify)
		notify_remote_via_irq(np->netdev->irq);

	dev->stats.tx_bytes += skb->len;
	dev->stats.tx_packets++;

	/* Note: It is not safe to access skb after xennet_tx_buf_gc()! */
	xennet_tx_buf_gc(dev);

	if (!netfront_tx_slot_available(np))
		netif_stop_queue(dev);

	spin_unlock_irq(&np->tx_lock);

	return NETDEV_TX_OK;

 drop:
	dev->stats.tx_dropped++;
	dev_kfree_skb(skb);
	return NETDEV_TX_OK;
}

static int xennet_close(struct net_device *dev)
{
	struct netfront_info *np = netdev_priv(dev);
	netif_stop_queue(np->netdev);
	napi_disable(&np->napi);
	return 0;
}

static void xennet_move_rx_slot(struct netfront_info *np, struct sk_buff *skb,
				grant_ref_t ref)
{
	int new = xennet_rxidx(np->rx.req_prod_pvt);

	BUG_ON(np->rx_skbs[new]);
	np->rx_skbs[new] = skb;
	np->grant_rx_ref[new] = ref;
	RING_GET_REQUEST(&np->rx, np->rx.req_prod_pvt)->id = new;
	RING_GET_REQUEST(&np->rx, np->rx.req_prod_pvt)->gref = ref;
	np->rx.req_prod_pvt++;
}

static int xennet_get_extras(struct netfront_info *np,
			     struct xen_netif_extra_info *extras,
			     RING_IDX rp)

{
	struct xen_netif_extra_info *extra;
	struct device *dev = &np->netdev->dev;
	RING_IDX cons = np->rx.rsp_cons;
	int err = 0;

	do {
		struct sk_buff *skb;
		grant_ref_t ref;

		if (unlikely(cons + 1 == rp)) {
			if (net_ratelimit())
				dev_warn(dev, "Missing extra info\n");
			err = -EBADR;
			break;
		}

		extra = (struct xen_netif_extra_info *)
			RING_GET_RESPONSE(&np->rx, ++cons);

		if (unlikely(!extra->type ||
			     extra->type >= XEN_NETIF_EXTRA_TYPE_MAX)) {
			if (net_ratelimit())
				dev_warn(dev, "Invalid extra type: %d\n",
					extra->type);
			err = -EINVAL;
		} else {
			memcpy(&extras[extra->type - 1], extra,
			       sizeof(*extra));
		}

		skb = xennet_get_rx_skb(np, cons);
		ref = xennet_get_rx_ref(np, cons);
		xennet_move_rx_slot(np, skb, ref);
	} while (extra->flags & XEN_NETIF_EXTRA_FLAG_MORE);

	np->rx.rsp_cons = cons;
	return err;
}

static int xennet_get_responses(struct netfront_info *np,
				struct netfront_rx_info *rinfo, RING_IDX rp,
				struct sk_buff_head *list)
{
	struct xen_netif_rx_response *rx = &rinfo->rx;
	struct xen_netif_extra_info *extras = rinfo->extras;
	struct device *dev = &np->netdev->dev;
	RING_IDX cons = np->rx.rsp_cons;
	struct sk_buff *skb = xennet_get_rx_skb(np, cons);
	grant_ref_t ref = xennet_get_rx_ref(np, cons);
	int max = MAX_SKB_FRAGS + (rx->status <= RX_COPY_THRESHOLD);
	int frags = 1;
	int err = 0;
	unsigned long ret;

	if (rx->flags & NETRXF_extra_info) {
		err = xennet_get_extras(np, extras, rp);
		cons = np->rx.rsp_cons;
	}

	for (;;) {
		if (unlikely(rx->status < 0 ||
			     rx->offset + rx->status > PAGE_SIZE)) {
			if (net_ratelimit())
				dev_warn(dev, "rx->offset: %x, size: %u\n",
					 rx->offset, rx->status);
			xennet_move_rx_slot(np, skb, ref);
			err = -EINVAL;
			goto next;
		}

		/*
		 * This definitely indicates a bug, either in this driver or in
		 * the backend driver. In future this should flag the bad
		 * situation to the system controller to reboot the backed.
		 */
		if (ref == GRANT_INVALID_REF) {
			if (net_ratelimit())
				dev_warn(dev, "Bad rx response id %d.\n",
					 rx->id);
			err = -EINVAL;
			goto next;
		}

		ret = gnttab_end_foreign_access_ref(ref, 0);
		BUG_ON(!ret);

		gnttab_release_grant_reference(&np->gref_rx_head, ref);

		__skb_queue_tail(list, skb);

next:
		if (!(rx->flags & NETRXF_more_data))
			break;

		if (cons + frags == rp) {
			if (net_ratelimit())
				dev_warn(dev, "Need more frags\n");
			err = -ENOENT;
			break;
		}

		rx = RING_GET_RESPONSE(&np->rx, cons + frags);
		skb = xennet_get_rx_skb(np, cons + frags);
		ref = xennet_get_rx_ref(np, cons + frags);
		frags++;
	}

	if (unlikely(frags > max)) {
		if (net_ratelimit())
			dev_warn(dev, "Too many frags\n");
		err = -E2BIG;
	}

	if (unlikely(err))
		np->rx.rsp_cons = cons + frags;

	return err;
}

static int xennet_set_skb_gso(struct sk_buff *skb,
			      struct xen_netif_extra_info *gso)
{
	if (!gso->u.gso.size) {
		if (net_ratelimit())
			printk(KERN_WARNING "GSO size must not be zero.\n");
		return -EINVAL;
	}

	/* Currently only TCPv4 S.O. is supported. */
	if (gso->u.gso.type != XEN_NETIF_GSO_TYPE_TCPV4) {
		if (net_ratelimit())
			printk(KERN_WARNING "Bad GSO type %d.\n", gso->u.gso.type);
		return -EINVAL;
	}

	skb_shinfo(skb)->gso_size = gso->u.gso.size;
	skb_shinfo(skb)->gso_type = SKB_GSO_TCPV4;

	/* Header must be checked, and gso_segs computed. */
	skb_shinfo(skb)->gso_type |= SKB_GSO_DODGY;
	skb_shinfo(skb)->gso_segs = 0;

	return 0;
}

static RING_IDX xennet_fill_frags(struct netfront_info *np,
				  struct sk_buff *skb,
				  struct sk_buff_head *list)
{
	struct skb_shared_info *shinfo = skb_shinfo(skb);
	int nr_frags = shinfo->nr_frags;
	RING_IDX cons = np->rx.rsp_cons;
	skb_frag_t *frag = shinfo->frags + nr_frags;
	struct sk_buff *nskb;

	while ((nskb = __skb_dequeue(list))) {
		struct xen_netif_rx_response *rx =
			RING_GET_RESPONSE(&np->rx, ++cons);

		frag->page = skb_shinfo(nskb)->frags[0].page;
		frag->page_offset = rx->offset;
		frag->size = rx->status;

		skb->data_len += rx->status;

		skb_shinfo(nskb)->nr_frags = 0;
		kfree_skb(nskb);

		frag++;
		nr_frags++;
	}

	shinfo->nr_frags = nr_frags;
	return cons;
}

static int skb_checksum_setup(struct sk_buff *skb)
{
	struct iphdr *iph;
	unsigned char *th;
	int err = -EPROTO;

	if (skb->protocol != htons(ETH_P_IP))
		goto out;

	iph = (void *)skb->data;
	th = skb->data + 4 * iph->ihl;
	if (th >= skb_tail_pointer(skb))
		goto out;

	skb->csum_start = th - skb->head;
	switch (iph->protocol) {
	case IPPROTO_TCP:
		skb->csum_offset = offsetof(struct tcphdr, check);
		break;
	case IPPROTO_UDP:
		skb->csum_offset = offsetof(struct udphdr, check);
		break;
	default:
		if (net_ratelimit())
			printk(KERN_ERR "Attempting to checksum a non-"
			       "TCP/UDP packet, dropping a protocol"
			       " %d packet", iph->protocol);
		goto out;
	}

	if ((th + skb->csum_offset + 2) > skb_tail_pointer(skb))
		goto out;

	err = 0;

out:
	return err;
}

static int handle_incoming_queue(struct net_device *dev,
				 struct sk_buff_head *rxq)
{
	int packets_dropped = 0;
	struct sk_buff *skb;

	while ((skb = __skb_dequeue(rxq)) != NULL) {
		struct page *page = NETFRONT_SKB_CB(skb)->page;
		void *vaddr = page_address(page);
		unsigned offset = NETFRONT_SKB_CB(skb)->offset;

		memcpy(skb->data, vaddr + offset,
		       skb_headlen(skb));

		if (page != skb_shinfo(skb)->frags[0].page)
			__free_page(page);

		/* Ethernet work: Delayed to here as it peeks the header. */
		skb->protocol = eth_type_trans(skb, dev);

		if (skb->ip_summed == CHECKSUM_PARTIAL) {
			if (skb_checksum_setup(skb)) {
				kfree_skb(skb);
				packets_dropped++;
				dev->stats.rx_errors++;
				continue;
			}
		}

		dev->stats.rx_packets++;
		dev->stats.rx_bytes += skb->len;

		/* Pass it up. */
		netif_receive_skb(skb);
	}

	return packets_dropped;
}

static int xennet_poll(struct napi_struct *napi, int budget)
{
	struct netfront_info *np = container_of(napi, struct netfront_info, napi);
	struct net_device *dev = np->netdev;
	struct sk_buff *skb;
	struct netfront_rx_info rinfo;
	struct xen_netif_rx_response *rx = &rinfo.rx;
	struct xen_netif_extra_info *extras = rinfo.extras;
	RING_IDX i, rp;
	int work_done;
	struct sk_buff_head rxq;
	struct sk_buff_head errq;
	struct sk_buff_head tmpq;
	unsigned long flags;
	unsigned int len;
	int err;

	spin_lock(&np->rx_lock);

	skb_queue_head_init(&rxq);
	skb_queue_head_init(&errq);
	skb_queue_head_init(&tmpq);

	rp = np->rx.sring->rsp_prod;
	rmb(); /* Ensure we see queued responses up to 'rp'. */

	i = np->rx.rsp_cons;
	work_done = 0;
	while ((i != rp) && (work_done < budget)) {
		memcpy(rx, RING_GET_RESPONSE(&np->rx, i), sizeof(*rx));
		memset(extras, 0, sizeof(rinfo.extras));

		err = xennet_get_responses(np, &rinfo, rp, &tmpq);

		if (unlikely(err)) {
err:
			while ((skb = __skb_dequeue(&tmpq)))
				__skb_queue_tail(&errq, skb);
			dev->stats.rx_errors++;
			i = np->rx.rsp_cons;
			continue;
		}

		skb = __skb_dequeue(&tmpq);

		if (extras[XEN_NETIF_EXTRA_TYPE_GSO - 1].type) {
			struct xen_netif_extra_info *gso;
			gso = &extras[XEN_NETIF_EXTRA_TYPE_GSO - 1];

			if (unlikely(xennet_set_skb_gso(skb, gso))) {
				__skb_queue_head(&tmpq, skb);
				np->rx.rsp_cons += skb_queue_len(&tmpq);
				goto err;
			}
		}

		NETFRONT_SKB_CB(skb)->page = skb_shinfo(skb)->frags[0].page;
		NETFRONT_SKB_CB(skb)->offset = rx->offset;

		len = rx->status;
		if (len > RX_COPY_THRESHOLD)
			len = RX_COPY_THRESHOLD;
		skb_put(skb, len);

		if (rx->status > len) {
			skb_shinfo(skb)->frags[0].page_offset =
				rx->offset + len;
			skb_shinfo(skb)->frags[0].size = rx->status - len;
			skb->data_len = rx->status - len;
		} else {
			skb_shinfo(skb)->frags[0].page = NULL;
			skb_shinfo(skb)->nr_frags = 0;
		}

		i = xennet_fill_frags(np, skb, &tmpq);

		/*
		 * Truesize approximates the size of true data plus
		 * any supervisor overheads. Adding hypervisor
		 * overheads has been shown to significantly reduce
		 * achievable bandwidth with the default receive
		 * buffer size. It is therefore not wise to account
		 * for it here.
		 *
		 * After alloc_skb(RX_COPY_THRESHOLD), truesize is set
		 * to RX_COPY_THRESHOLD + the supervisor
		 * overheads. Here, we add the size of the data pulled
		 * in xennet_fill_frags().
		 *
		 * We also adjust for any unused space in the main
		 * data area by subtracting (RX_COPY_THRESHOLD -
		 * len). This is especially important with drivers
		 * which split incoming packets into header and data,
		 * using only 66 bytes of the main data area (see the
		 * e1000 driver for example.)  On such systems,
		 * without this last adjustement, our achievable
		 * receive throughout using the standard receive
		 * buffer size was cut by 25%(!!!).
		 */
		skb->truesize += skb->data_len - (RX_COPY_THRESHOLD - len);
		skb->len += skb->data_len;

		if (rx->flags & NETRXF_csum_blank)
			skb->ip_summed = CHECKSUM_PARTIAL;
		else if (rx->flags & NETRXF_data_validated)
			skb->ip_summed = CHECKSUM_UNNECESSARY;

		__skb_queue_tail(&rxq, skb);

		np->rx.rsp_cons = ++i;
		work_done++;
	}

	__skb_queue_purge(&errq);

	work_done -= handle_incoming_queue(dev, &rxq);

	/* If we get a callback with very few responses, reduce fill target. */
	/* NB. Note exponential increase, linear decrease. */
	if (((np->rx.req_prod_pvt - np->rx.sring->rsp_prod) >
	     ((3*np->rx_target) / 4)) &&
	    (--np->rx_target < np->rx_min_target))
		np->rx_target = np->rx_min_target;

	xennet_alloc_rx_buffers(dev);

	if (work_done < budget) {
		int more_to_do = 0;

		local_irq_save(flags);

		RING_FINAL_CHECK_FOR_RESPONSES(&np->rx, more_to_do);
		if (!more_to_do)
			__napi_complete(napi);

		local_irq_restore(flags);
	}

	spin_unlock(&np->rx_lock);

	return work_done;
}

static int xennet_change_mtu(struct net_device *dev, int mtu)
{
	int max = xennet_can_sg(dev) ? 65535 - ETH_HLEN : ETH_DATA_LEN;

	if (mtu > max)
		return -EINVAL;
	dev->mtu = mtu;
	return 0;
}

static void xennet_release_tx_bufs(struct netfront_info *np)
{
	struct sk_buff *skb;
	int i;

	for (i = 0; i < NET_TX_RING_SIZE; i++) {
		/* Skip over entries which are actually freelist references */
		if (skb_entry_is_link(&np->tx_skbs[i]))
			continue;

		skb = np->tx_skbs[i].skb;
		gnttab_end_foreign_access_ref(np->grant_tx_ref[i],
					      GNTMAP_readonly);
		gnttab_release_grant_reference(&np->gref_tx_head,
					       np->grant_tx_ref[i]);
		np->grant_tx_ref[i] = GRANT_INVALID_REF;
		add_id_to_freelist(&np->tx_skb_freelist, np->tx_skbs, i);
		dev_kfree_skb_irq(skb);
	}
}

static void xennet_release_rx_bufs(struct netfront_info *np)
{
	struct mmu_update      *mmu = np->rx_mmu;
	struct multicall_entry *mcl = np->rx_mcl;
	struct sk_buff_head free_list;
	struct sk_buff *skb;
	unsigned long mfn;
	int xfer = 0, noxfer = 0, unused = 0;
	int id, ref;

	dev_warn(&np->netdev->dev, "%s: fix me for copying receiver.\n",
			 __func__);
	return;

	skb_queue_head_init(&free_list);

	spin_lock_bh(&np->rx_lock);

	for (id = 0; id < NET_RX_RING_SIZE; id++) {
		ref = np->grant_rx_ref[id];
		if (ref == GRANT_INVALID_REF) {
			unused++;
			continue;
		}

		skb = np->rx_skbs[id];
		mfn = gnttab_end_foreign_transfer_ref(ref);
		gnttab_release_grant_reference(&np->gref_rx_head, ref);
		np->grant_rx_ref[id] = GRANT_INVALID_REF;

		if (0 == mfn) {
			skb_shinfo(skb)->nr_frags = 0;
			dev_kfree_skb(skb);
			noxfer++;
			continue;
		}

		if (!xen_feature(XENFEAT_auto_translated_physmap)) {
			/* Remap the page. */
			struct page *page = skb_shinfo(skb)->frags[0].page;
			unsigned long pfn = page_to_pfn(page);
			void *vaddr = page_address(page);

			MULTI_update_va_mapping(mcl, (unsigned long)vaddr,
						mfn_pte(mfn, PAGE_KERNEL),
						0);
			mcl++;
			mmu->ptr = ((u64)mfn << PAGE_SHIFT)
				| MMU_MACHPHYS_UPDATE;
			mmu->val = pfn;
			mmu++;

			set_phys_to_machine(pfn, mfn);
		}
		__skb_queue_tail(&free_list, skb);
		xfer++;
	}

	dev_info(&np->netdev->dev, "%s: %d xfer, %d noxfer, %d unused\n",
		 __func__, xfer, noxfer, unused);

	if (xfer) {
		if (!xen_feature(XENFEAT_auto_translated_physmap)) {
			/* Do all the remapping work and M2P updates. */
			MULTI_mmu_update(mcl, np->rx_mmu, mmu - np->rx_mmu,
					 NULL, DOMID_SELF);
			mcl++;
			HYPERVISOR_multicall(np->rx_mcl, mcl - np->rx_mcl);
		}
	}

	__skb_queue_purge(&free_list);

	spin_unlock_bh(&np->rx_lock);
}

static void xennet_uninit(struct net_device *dev)
{
	struct netfront_info *np = netdev_priv(dev);
	xennet_release_tx_bufs(np);
	xennet_release_rx_bufs(np);
	gnttab_free_grant_references(np->gref_tx_head);
	gnttab_free_grant_references(np->gref_rx_head);
}

static const struct net_device_ops xennet_netdev_ops = {
	.ndo_open            = xennet_open,
	.ndo_uninit          = xennet_uninit,
	.ndo_stop            = xennet_close,
	.ndo_start_xmit      = xennet_start_xmit,
	.ndo_change_mtu	     = xennet_change_mtu,
	.ndo_set_mac_address = eth_mac_addr,
	.ndo_validate_addr   = eth_validate_addr,
};

static struct net_device * __devinit xennet_create_dev(struct xenbus_device *dev)
{
	int i, err;
	struct net_device *netdev;
	struct netfront_info *np;

	netdev = alloc_etherdev(sizeof(struct netfront_info));
	if (!netdev) {
		printk(KERN_WARNING "%s> alloc_etherdev failed.\n",
		       __func__);
		return ERR_PTR(-ENOMEM);
	}

	np                   = netdev_priv(netdev);
	np->xbdev            = dev;

	spin_lock_init(&np->tx_lock);
	spin_lock_init(&np->rx_lock);

	skb_queue_head_init(&np->rx_batch);
	np->rx_target     = RX_DFL_MIN_TARGET;
	np->rx_min_target = RX_DFL_MIN_TARGET;
	np->rx_max_target = RX_MAX_TARGET;

	init_timer(&np->rx_refill_timer);
	np->rx_refill_timer.data = (unsigned long)netdev;
	np->rx_refill_timer.function = rx_refill_timeout;

	/* Initialise tx_skbs as a free chain containing every entry. */
	np->tx_skb_freelist = 0;
	for (i = 0; i < NET_TX_RING_SIZE; i++) {
		skb_entry_set_link(&np->tx_skbs[i], i+1);
		np->grant_tx_ref[i] = GRANT_INVALID_REF;
	}

	/* Clear out rx_skbs */
	for (i = 0; i < NET_RX_RING_SIZE; i++) {
		np->rx_skbs[i] = NULL;
		np->grant_rx_ref[i] = GRANT_INVALID_REF;
	}

	/* A grant for every tx ring slot */
	if (gnttab_alloc_grant_references(TX_MAX_TARGET,
					  &np->gref_tx_head) < 0) {
		printk(KERN_ALERT "#### netfront can't alloc tx grant refs\n");
		err = -ENOMEM;
		goto exit;
	}
	/* A grant for every rx ring slot */
	if (gnttab_alloc_grant_references(RX_MAX_TARGET,
					  &np->gref_rx_head) < 0) {
		printk(KERN_ALERT "#### netfront can't alloc rx grant refs\n");
		err = -ENOMEM;
		goto exit_free_tx;
	}

	netdev->netdev_ops	= &xennet_netdev_ops;

	netif_napi_add(netdev, &np->napi, xennet_poll, 64);
	netdev->features        = NETIF_F_IP_CSUM;

	SET_ETHTOOL_OPS(netdev, &xennet_ethtool_ops);
	SET_NETDEV_DEV(netdev, &dev->dev);

	np->netdev = netdev;

	netif_carrier_off(netdev);

	return netdev;

 exit_free_tx:
	gnttab_free_grant_references(np->gref_tx_head);
 exit:
	free_netdev(netdev);
	return ERR_PTR(err);
}

/**
 * Entry point to this code when a new device is created.  Allocate the basic
 * structures and the ring buffers for communication with the backend, and
 * inform the backend of the appropriate details for those.
 */
static int __devinit netfront_probe(struct xenbus_device *dev,
				    const struct xenbus_device_id *id)
{
	int err;
	struct net_device *netdev;
	struct netfront_info *info;

	netdev = xennet_create_dev(dev);
	if (IS_ERR(netdev)) {
		err = PTR_ERR(netdev);
		xenbus_dev_fatal(dev, err, "creating netdev");
		return err;
	}

	info = netdev_priv(netdev);
	dev_set_drvdata(&dev->dev, info);

	err = register_netdev(info->netdev);
	if (err) {
		printk(KERN_WARNING "%s: register_netdev err=%d\n",
		       __func__, err);
		goto fail;
	}

	err = xennet_sysfs_addif(info->netdev);
	if (err) {
		unregister_netdev(info->netdev);
		printk(KERN_WARNING "%s: add sysfs failed err=%d\n",
		       __func__, err);
		goto fail;
	}

	return 0;

 fail:
	free_netdev(netdev);
	dev_set_drvdata(&dev->dev, NULL);
	return err;
}

static void xennet_end_access(int ref, void *page)
{
	/* This frees the page as a side-effect */
	if (ref != GRANT_INVALID_REF)
		gnttab_end_foreign_access(ref, 0, (unsigned long)page);
}

static void xennet_disconnect_backend(struct netfront_info *info)
{
	/* Stop old i/f to prevent errors whilst we rebuild the state. */
	spin_lock_bh(&info->rx_lock);
	spin_lock_irq(&info->tx_lock);
	netif_carrier_off(info->netdev);
	spin_unlock_irq(&info->tx_lock);
	spin_unlock_bh(&info->rx_lock);

	if (info->netdev->irq)
		unbind_from_irqhandler(info->netdev->irq, info->netdev);
	info->evtchn = info->netdev->irq = 0;

	/* End access and free the pages */
	xennet_end_access(info->tx_ring_ref, info->tx.sring);
	xennet_end_access(info->rx_ring_ref, info->rx.sring);

	info->tx_ring_ref = GRANT_INVALID_REF;
	info->rx_ring_ref = GRANT_INVALID_REF;
	info->tx.sring = NULL;
	info->rx.sring = NULL;
}

/**
 * We are reconnecting to the backend, due to a suspend/resume, or a backend
 * driver restart.  We tear down our netif structure and recreate it, but
 * leave the device-layer structures intact so that this is transparent to the
 * rest of the kernel.
 */
static int netfront_resume(struct xenbus_device *dev)
{
	struct netfront_info *info = dev_get_drvdata(&dev->dev);

	dev_dbg(&dev->dev, "%s\n", dev->nodename);

	xennet_disconnect_backend(info);
	return 0;
}

static int xen_net_read_mac(struct xenbus_device *dev, u8 mac[])
{
	char *s, *e, *macstr;
	int i;

	macstr = s = xenbus_read(XBT_NIL, dev->nodename, "mac", NULL);
	if (IS_ERR(macstr))
		return PTR_ERR(macstr);

	for (i = 0; i < ETH_ALEN; i++) {
		mac[i] = simple_strtoul(s, &e, 16);
		if ((s == e) || (*e != ((i == ETH_ALEN-1) ? '\0' : ':'))) {
			kfree(macstr);
			return -ENOENT;
		}
		s = e+1;
	}

	kfree(macstr);
	return 0;
}

static irqreturn_t xennet_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;
	struct netfront_info *np = netdev_priv(dev);
	unsigned long flags;

	spin_lock_irqsave(&np->tx_lock, flags);

	if (likely(netif_carrier_ok(dev))) {
		xennet_tx_buf_gc(dev);
		/* Under tx_lock: protects access to rx shared-ring indexes. */
		if (RING_HAS_UNCONSUMED_RESPONSES(&np->rx))
			napi_schedule(&np->napi);
	}

	spin_unlock_irqrestore(&np->tx_lock, flags);

	return IRQ_HANDLED;
}

static int setup_netfront(struct xenbus_device *dev, struct netfront_info *info)
{
	struct xen_netif_tx_sring *txs;
	struct xen_netif_rx_sring *rxs;
	int err;
	struct net_device *netdev = info->netdev;

	info->tx_ring_ref = GRANT_INVALID_REF;
	info->rx_ring_ref = GRANT_INVALID_REF;
	info->rx.sring = NULL;
	info->tx.sring = NULL;
	netdev->irq = 0;

	err = xen_net_read_mac(dev, netdev->dev_addr);
	if (err) {
		xenbus_dev_fatal(dev, err, "parsing %s/mac", dev->nodename);
		goto fail;
	}

	txs = (struct xen_netif_tx_sring *)get_zeroed_page(GFP_NOIO | __GFP_HIGH);
	if (!txs) {
		err = -ENOMEM;
		xenbus_dev_fatal(dev, err, "allocating tx ring page");
		goto fail;
	}
	SHARED_RING_INIT(txs);
	FRONT_RING_INIT(&info->tx, txs, PAGE_SIZE);

	err = xenbus_grant_ring(dev, virt_to_mfn(txs));
	if (err < 0) {
		free_page((unsigned long)txs);
		goto fail;
	}

	info->tx_ring_ref = err;
	rxs = (struct xen_netif_rx_sring *)get_zeroed_page(GFP_NOIO | __GFP_HIGH);
	if (!rxs) {
		err = -ENOMEM;
		xenbus_dev_fatal(dev, err, "allocating rx ring page");
		goto fail;
	}
	SHARED_RING_INIT(rxs);
	FRONT_RING_INIT(&info->rx, rxs, PAGE_SIZE);

	err = xenbus_grant_ring(dev, virt_to_mfn(rxs));
	if (err < 0) {
		free_page((unsigned long)rxs);
		goto fail;
	}
	info->rx_ring_ref = err;

	err = xenbus_alloc_evtchn(dev, &info->evtchn);
	if (err)
		goto fail;

	err = bind_evtchn_to_irqhandler(info->evtchn, xennet_interrupt,
					IRQF_SAMPLE_RANDOM, netdev->name,
					netdev);
	if (err < 0)
		goto fail;
	netdev->irq = err;
	return 0;

 fail:
	return err;
}

/* Common code used when first setting up, and when resuming. */
static int talk_to_backend(struct xenbus_device *dev,
			   struct netfront_info *info)
{
	const char *message;
	struct xenbus_transaction xbt;
	int err;

	/* Create shared ring, alloc event channel. */
	err = setup_netfront(dev, info);
	if (err)
		goto out;

again:
	err = xenbus_transaction_start(&xbt);
	if (err) {
		xenbus_dev_fatal(dev, err, "starting transaction");
		goto destroy_ring;
	}

	err = xenbus_printf(xbt, dev->nodename, "tx-ring-ref", "%u",
			    info->tx_ring_ref);
	if (err) {
		message = "writing tx ring-ref";
		goto abort_transaction;
	}
	err = xenbus_printf(xbt, dev->nodename, "rx-ring-ref", "%u",
			    info->rx_ring_ref);
	if (err) {
		message = "writing rx ring-ref";
		goto abort_transaction;
	}
	err = xenbus_printf(xbt, dev->nodename,
			    "event-channel", "%u", info->evtchn);
	if (err) {
		message = "writing event-channel";
		goto abort_transaction;
	}

	err = xenbus_printf(xbt, dev->nodename, "request-rx-copy", "%u",
			    1);
	if (err) {
		message = "writing request-rx-copy";
		goto abort_transaction;
	}

	err = xenbus_printf(xbt, dev->nodename, "feature-rx-notify", "%d", 1);
	if (err) {
		message = "writing feature-rx-notify";
		goto abort_transaction;
	}

	err = xenbus_printf(xbt, dev->nodename, "feature-sg", "%d", 1);
	if (err) {
		message = "writing feature-sg";
		goto abort_transaction;
	}

	err = xenbus_printf(xbt, dev->nodename, "feature-gso-tcpv4", "%d", 1);
	if (err) {
		message = "writing feature-gso-tcpv4";
		goto abort_transaction;
	}

	err = xenbus_transaction_end(xbt, 0);
	if (err) {
		if (err == -EAGAIN)
			goto again;
		xenbus_dev_fatal(dev, err, "completing transaction");
		goto destroy_ring;
	}

	return 0;

 abort_transaction:
	xenbus_transaction_end(xbt, 1);
	xenbus_dev_fatal(dev, err, "%s", message);
 destroy_ring:
	xennet_disconnect_backend(info);
 out:
	return err;
}

static int xennet_set_sg(struct net_device *dev, u32 data)
{
	if (data) {
		struct netfront_info *np = netdev_priv(dev);
		int val;

		if (xenbus_scanf(XBT_NIL, np->xbdev->otherend, "feature-sg",
				 "%d", &val) < 0)
			val = 0;
		if (!val)
			return -ENOSYS;
	} else if (dev->mtu > ETH_DATA_LEN)
		dev->mtu = ETH_DATA_LEN;

	return ethtool_op_set_sg(dev, data);
}

static int xennet_set_tso(struct net_device *dev, u32 data)
{
	if (data) {
		struct netfront_info *np = netdev_priv(dev);
		int val;

		if (xenbus_scanf(XBT_NIL, np->xbdev->otherend,
				 "feature-gso-tcpv4", "%d", &val) < 0)
			val = 0;
		if (!val)
			return -ENOSYS;
	}

	return ethtool_op_set_tso(dev, data);
}

static void xennet_set_features(struct net_device *dev)
{
	/* Turn off all GSO bits except ROBUST. */
	dev->features &= ~NETIF_F_GSO_MASK;
	dev->features |= NETIF_F_GSO_ROBUST;
	xennet_set_sg(dev, 0);

	/* We need checksum offload to enable scatter/gather and TSO. */
	if (!(dev->features & NETIF_F_IP_CSUM))
		return;

	if (!xennet_set_sg(dev, 1))
		xennet_set_tso(dev, 1);
}

static int xennet_connect(struct net_device *dev)
{
	struct netfront_info *np = netdev_priv(dev);
	int i, requeue_idx, err;
	struct sk_buff *skb;
	grant_ref_t ref;
	struct xen_netif_rx_request *req;
	unsigned int feature_rx_copy;

	err = xenbus_scanf(XBT_NIL, np->xbdev->otherend,
			   "feature-rx-copy", "%u", &feature_rx_copy);
	if (err != 1)
		feature_rx_copy = 0;

	if (!feature_rx_copy) {
		dev_info(&dev->dev,
			 "backend does not support copying receive path\n");
		return -ENODEV;
	}

	err = talk_to_backend(np->xbdev, np);
	if (err)
		return err;

	xennet_set_features(dev);

	spin_lock_bh(&np->rx_lock);
	spin_lock_irq(&np->tx_lock);

	/* Step 1: Discard all pending TX packet fragments. */
	xennet_release_tx_bufs(np);

	/* Step 2: Rebuild the RX buffer freelist and the RX ring itself. */
	for (requeue_idx = 0, i = 0; i < NET_RX_RING_SIZE; i++) {
		if (!np->rx_skbs[i])
			continue;

		skb = np->rx_skbs[requeue_idx] = xennet_get_rx_skb(np, i);
		ref = np->grant_rx_ref[requeue_idx] = xennet_get_rx_ref(np, i);
		req = RING_GET_REQUEST(&np->rx, requeue_idx);

		gnttab_grant_foreign_access_ref(
			ref, np->xbdev->otherend_id,
			pfn_to_mfn(page_to_pfn(skb_shinfo(skb)->
					       frags->page)),
			0);
		req->gref = ref;
		req->id   = requeue_idx;

		requeue_idx++;
	}

	np->rx.req_prod_pvt = requeue_idx;

	/*
	 * Step 3: All public and private state should now be sane.  Get
	 * ready to start sending and receiving packets and give the driver
	 * domain a kick because we've probably just requeued some
	 * packets.
	 */
	netif_carrier_on(np->netdev);
	notify_remote_via_irq(np->netdev->irq);
	xennet_tx_buf_gc(dev);
	xennet_alloc_rx_buffers(dev);

	spin_unlock_irq(&np->tx_lock);
	spin_unlock_bh(&np->rx_lock);

	return 0;
}

/**
 * Callback received when the backend's state changes.
 */
static void backend_changed(struct xenbus_device *dev,
			    enum xenbus_state backend_state)
{
	struct netfront_info *np = dev_get_drvdata(&dev->dev);
	struct net_device *netdev = np->netdev;

	dev_dbg(&dev->dev, "%s\n", xenbus_strstate(backend_state));

	switch (backend_state) {
	case XenbusStateInitialising:
	case XenbusStateInitialised:
	case XenbusStateConnected:
	case XenbusStateUnknown:
	case XenbusStateClosed:
		break;

	case XenbusStateInitWait:
		if (dev->state != XenbusStateInitialising)
			break;
		if (xennet_connect(netdev) != 0)
			break;
		xenbus_switch_state(dev, XenbusStateConnected);
		break;

	case XenbusStateClosing:
		xenbus_frontend_closed(dev);
		break;
	}
}

static const struct ethtool_ops xennet_ethtool_ops =
{
	.set_tx_csum = ethtool_op_set_tx_csum,
	.set_sg = xennet_set_sg,
	.set_tso = xennet_set_tso,
	.get_link = ethtool_op_get_link,
};

#ifdef CONFIG_SYSFS
static ssize_t show_rxbuf_min(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct net_device *netdev = to_net_dev(dev);
	struct netfront_info *info = netdev_priv(netdev);

	return sprintf(buf, "%u\n", info->rx_min_target);
}

static ssize_t store_rxbuf_min(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t len)
{
	struct net_device *netdev = to_net_dev(dev);
	struct netfront_info *np = netdev_priv(netdev);
	char *endp;
	unsigned long target;

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	target = simple_strtoul(buf, &endp, 0);
	if (endp == buf)
		return -EBADMSG;

	if (target < RX_MIN_TARGET)
		target = RX_MIN_TARGET;
	if (target > RX_MAX_TARGET)
		target = RX_MAX_TARGET;

	spin_lock_bh(&np->rx_lock);
	if (target > np->rx_max_target)
		np->rx_max_target = target;
	np->rx_min_target = target;
	if (target > np->rx_target)
		np->rx_target = target;

	xennet_alloc_rx_buffers(netdev);

	spin_unlock_bh(&np->rx_lock);
	return len;
}

static ssize_t show_rxbuf_max(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct net_device *netdev = to_net_dev(dev);
	struct netfront_info *info = netdev_priv(netdev);

	return sprintf(buf, "%u\n", info->rx_max_target);
}

static ssize_t store_rxbuf_max(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t len)
{
	struct net_device *netdev = to_net_dev(dev);
	struct netfront_info *np = netdev_priv(netdev);
	char *endp;
	unsigned long target;

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	target = simple_strtoul(buf, &endp, 0);
	if (endp == buf)
		return -EBADMSG;

	if (target < RX_MIN_TARGET)
		target = RX_MIN_TARGET;
	if (target > RX_MAX_TARGET)
		target = RX_MAX_TARGET;

	spin_lock_bh(&np->rx_lock);
	if (target < np->rx_min_target)
		np->rx_min_target = target;
	np->rx_max_target = target;
	if (target < np->rx_target)
		np->rx_target = target;

	xennet_alloc_rx_buffers(netdev);

	spin_unlock_bh(&np->rx_lock);
	return len;
}

static ssize_t show_rxbuf_cur(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct net_device *netdev = to_net_dev(dev);
	struct netfront_info *info = netdev_priv(netdev);

	return sprintf(buf, "%u\n", info->rx_target);
}

static struct device_attribute xennet_attrs[] = {
	__ATTR(rxbuf_min, S_IRUGO|S_IWUSR, show_rxbuf_min, store_rxbuf_min),
	__ATTR(rxbuf_max, S_IRUGO|S_IWUSR, show_rxbuf_max, store_rxbuf_max),
	__ATTR(rxbuf_cur, S_IRUGO, show_rxbuf_cur, NULL),
};

static int xennet_sysfs_addif(struct net_device *netdev)
{
	int i;
	int err;

	for (i = 0; i < ARRAY_SIZE(xennet_attrs); i++) {
		err = device_create_file(&netdev->dev,
					   &xennet_attrs[i]);
		if (err)
			goto fail;
	}
	return 0;

 fail:
	while (--i >= 0)
		device_remove_file(&netdev->dev, &xennet_attrs[i]);
	return err;
}

static void xennet_sysfs_delif(struct net_device *netdev)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(xennet_attrs); i++)
		device_remove_file(&netdev->dev, &xennet_attrs[i]);
}

#endif /* CONFIG_SYSFS */

static struct xenbus_device_id netfront_ids[] = {
	{ "vif" },
	{ "" }
};


static int __devexit xennet_remove(struct xenbus_device *dev)
{
	struct netfront_info *info = dev_get_drvdata(&dev->dev);

	dev_dbg(&dev->dev, "%s\n", dev->nodename);

	unregister_netdev(info->netdev);

	xennet_disconnect_backend(info);

	del_timer_sync(&info->rx_refill_timer);

	xennet_sysfs_delif(info->netdev);

	free_netdev(info->netdev);

	return 0;
}

static struct xenbus_driver netfront_driver = {
	.name = "vif",
	.owner = THIS_MODULE,
	.ids = netfront_ids,
	.probe = netfront_probe,
	.remove = __devexit_p(xennet_remove),
	.resume = netfront_resume,
	.otherend_changed = backend_changed,
};

static int __init netif_init(void)
{
	if (!xen_domain())
		return -ENODEV;

	if (xen_initial_domain())
		return 0;

	printk(KERN_INFO "Initialising Xen virtual ethernet driver.\n");

	return xenbus_register_frontend(&netfront_driver);
}
module_init(netif_init);


static void __exit netif_exit(void)
{
	if (xen_initial_domain())
		return;

	xenbus_unregister_driver(&netfront_driver);
}
module_exit(netif_exit);

MODULE_DESCRIPTION("Xen virtual network device frontend");
MODULE_LICENSE("GPL");
MODULE_ALIAS("xen:vif");
MODULE_ALIAS("xennet");
