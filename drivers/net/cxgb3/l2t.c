/*
 * Copyright (c) 2003-2007 Chelsio, Inc. All rights reserved.
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
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/if.h>
#include <linux/if_vlan.h>
#include <linux/jhash.h>
#include <net/neighbour.h>
#include "common.h"
#include "t3cdev.h"
#include "cxgb3_defs.h"
#include "l2t.h"
#include "t3_cpl.h"
#include "firmware_exports.h"

#define VLAN_NONE 0xfff

/*
 * Module locking notes:  There is a RW lock protecting the L2 table as a
 * whole plus a spinlock per L2T entry.  Entry lookups and allocations happen
 * under the protection of the table lock, individual entry changes happen
 * while holding that entry's spinlock.  The table lock nests outside the
 * entry locks.  Allocations of new entries take the table lock as writers so
 * no other lookups can happen while allocating new entries.  Entry updates
 * take the table lock as readers so multiple entries can be updated in
 * parallel.  An L2T entry can be dropped by decrementing its reference count
 * and therefore can happen in parallel with entry allocation but no entry
 * can change state or increment its ref count during allocation as both of
 * these perform lookups.
 */

static inline unsigned int vlan_prio(const struct l2t_entry *e)
{
	return e->vlan >> 13;
}

static inline unsigned int arp_hash(u32 key, int ifindex,
				    const struct l2t_data *d)
{
	return jhash_2words(key, ifindex, 0) & (d->nentries - 1);
}

static inline void neigh_replace(struct l2t_entry *e, struct neighbour *n)
{
	neigh_hold(n);
	if (e->neigh)
		neigh_release(e->neigh);
	e->neigh = n;
}

/*
 * Set up an L2T entry and send any packets waiting in the arp queue.  The
 * supplied skb is used for the CPL_L2T_WRITE_REQ.  Must be called with the
 * entry locked.
 */
static int setup_l2e_send_pending(struct t3cdev *dev, struct sk_buff *skb,
				  struct l2t_entry *e)
{
	struct cpl_l2t_write_req *req;

	if (!skb) {
		skb = alloc_skb(sizeof(*req), GFP_ATOMIC);
		if (!skb)
			return -ENOMEM;
	}

	req = (struct cpl_l2t_write_req *)__skb_put(skb, sizeof(*req));
	req->wr.wr_hi = htonl(V_WR_OP(FW_WROPCODE_FORWARD));
	OPCODE_TID(req) = htonl(MK_OPCODE_TID(CPL_L2T_WRITE_REQ, e->idx));
	req->params = htonl(V_L2T_W_IDX(e->idx) | V_L2T_W_IFF(e->smt_idx) |
			    V_L2T_W_VLAN(e->vlan & VLAN_VID_MASK) |
			    V_L2T_W_PRIO(vlan_prio(e)));
	memcpy(e->dmac, e->neigh->ha, sizeof(e->dmac));
	memcpy(req->dst_mac, e->dmac, sizeof(req->dst_mac));
	skb->priority = CPL_PRIORITY_CONTROL;
	cxgb3_ofld_send(dev, skb);
	while (e->arpq_head) {
		skb = e->arpq_head;
		e->arpq_head = skb->next;
		skb->next = NULL;
		cxgb3_ofld_send(dev, skb);
	}
	e->arpq_tail = NULL;
	e->state = L2T_STATE_VALID;

	return 0;
}

/*
 * Add a packet to the an L2T entry's queue of packets awaiting resolution.
 * Must be called with the entry's lock held.
 */
static inline void arpq_enqueue(struct l2t_entry *e, struct sk_buff *skb)
{
	skb->next = NULL;
	if (e->arpq_head)
		e->arpq_tail->next = skb;
	else
		e->arpq_head = skb;
	e->arpq_tail = skb;
}

int t3_l2t_send_slow(struct t3cdev *dev, struct sk_buff *skb,
		     struct l2t_entry *e)
{
again:
	switch (e->state) {
	case L2T_STATE_STALE:	/* entry is stale, kick off revalidation */
		neigh_event_send(e->neigh, NULL);
		spin_lock_bh(&e->lock);
		if (e->state == L2T_STATE_STALE)
			e->state = L2T_STATE_VALID;
		spin_unlock_bh(&e->lock);
	case L2T_STATE_VALID:	/* fast-path, send the packet on */
		return cxgb3_ofld_send(dev, skb);
	case L2T_STATE_RESOLVING:
		spin_lock_bh(&e->lock);
		if (e->state != L2T_STATE_RESOLVING) {
			/* ARP already completed */
			spin_unlock_bh(&e->lock);
			goto again;
		}
		arpq_enqueue(e, skb);
		spin_unlock_bh(&e->lock);

		/*
		 * Only the first packet added to the arpq should kick off
		 * resolution.  However, because the alloc_skb below can fail,
		 * we allow each packet added to the arpq to retry resolution
		 * as a way of recovering from transient memory exhaustion.
		 * A better way would be to use a work request to retry L2T
		 * entries when there's no memory.
		 */
		if (!neigh_event_send(e->neigh, NULL)) {
			skb = alloc_skb(sizeof(struct cpl_l2t_write_req),
					GFP_ATOMIC);
			if (!skb)
				break;

			spin_lock_bh(&e->lock);
			if (e->arpq_head)
				setup_l2e_send_pending(dev, skb, e);
			else	/* we lost the race */
				__kfree_skb(skb);
			spin_unlock_bh(&e->lock);
		}
	}
	return 0;
}

EXPORT_SYMBOL(t3_l2t_send_slow);

void t3_l2t_send_event(struct t3cdev *dev, struct l2t_entry *e)
{
again:
	switch (e->state) {
	case L2T_STATE_STALE:	/* entry is stale, kick off revalidation */
		neigh_event_send(e->neigh, NULL);
		spin_lock_bh(&e->lock);
		if (e->state == L2T_STATE_STALE) {
			e->state = L2T_STATE_VALID;
		}
		spin_unlock_bh(&e->lock);
		return;
	case L2T_STATE_VALID:	/* fast-path, send the packet on */
		return;
	case L2T_STATE_RESOLVING:
		spin_lock_bh(&e->lock);
		if (e->state != L2T_STATE_RESOLVING) {
			/* ARP already completed */
			spin_unlock_bh(&e->lock);
			goto again;
		}
		spin_unlock_bh(&e->lock);

		/*
		 * Only the first packet added to the arpq should kick off
		 * resolution.  However, because the alloc_skb below can fail,
		 * we allow each packet added to the arpq to retry resolution
		 * as a way of recovering from transient memory exhaustion.
		 * A better way would be to use a work request to retry L2T
		 * entries when there's no memory.
		 */
		neigh_event_send(e->neigh, NULL);
	}
	return;
}

EXPORT_SYMBOL(t3_l2t_send_event);

/*
 * Allocate a free L2T entry.  Must be called with l2t_data.lock held.
 */
static struct l2t_entry *alloc_l2e(struct l2t_data *d)
{
	struct l2t_entry *end, *e, **p;

	if (!atomic_read(&d->nfree))
		return NULL;

	/* there's definitely a free entry */
	for (e = d->rover, end = &d->l2tab[d->nentries]; e != end; ++e)
		if (atomic_read(&e->refcnt) == 0)
			goto found;

	for (e = &d->l2tab[1]; atomic_read(&e->refcnt); ++e) ;
found:
	d->rover = e + 1;
	atomic_dec(&d->nfree);

	/*
	 * The entry we found may be an inactive entry that is
	 * presently in the hash table.  We need to remove it.
	 */
	if (e->state != L2T_STATE_UNUSED) {
		int hash = arp_hash(e->addr, e->ifindex, d);

		for (p = &d->l2tab[hash].first; *p; p = &(*p)->next)
			if (*p == e) {
				*p = e->next;
				break;
			}
		e->state = L2T_STATE_UNUSED;
	}
	return e;
}

/*
 * Called when an L2T entry has no more users.  The entry is left in the hash
 * table since it is likely to be reused but we also bump nfree to indicate
 * that the entry can be reallocated for a different neighbor.  We also drop
 * the existing neighbor reference in case the neighbor is going away and is
 * waiting on our reference.
 *
 * Because entries can be reallocated to other neighbors once their ref count
 * drops to 0 we need to take the entry's lock to avoid races with a new
 * incarnation.
 */
void t3_l2e_free(struct l2t_data *d, struct l2t_entry *e)
{
	spin_lock_bh(&e->lock);
	if (atomic_read(&e->refcnt) == 0) {	/* hasn't been recycled */
		if (e->neigh) {
			neigh_release(e->neigh);
			e->neigh = NULL;
		}
	}
	spin_unlock_bh(&e->lock);
	atomic_inc(&d->nfree);
}

EXPORT_SYMBOL(t3_l2e_free);

/*
 * Update an L2T entry that was previously used for the same next hop as neigh.
 * Must be called with softirqs disabled.
 */
static inline void reuse_entry(struct l2t_entry *e, struct neighbour *neigh)
{
	unsigned int nud_state;

	spin_lock(&e->lock);	/* avoid race with t3_l2t_free */

	if (neigh != e->neigh)
		neigh_replace(e, neigh);
	nud_state = neigh->nud_state;
	if (memcmp(e->dmac, neigh->ha, sizeof(e->dmac)) ||
	    !(nud_state & NUD_VALID))
		e->state = L2T_STATE_RESOLVING;
	else if (nud_state & NUD_CONNECTED)
		e->state = L2T_STATE_VALID;
	else
		e->state = L2T_STATE_STALE;
	spin_unlock(&e->lock);
}

struct l2t_entry *t3_l2t_get(struct t3cdev *cdev, struct neighbour *neigh,
			     struct net_device *dev)
{
	struct l2t_entry *e;
	struct l2t_data *d = L2DATA(cdev);
	u32 addr = *(u32 *) neigh->primary_key;
	int ifidx = neigh->dev->ifindex;
	int hash = arp_hash(addr, ifidx, d);
	struct port_info *p = netdev_priv(dev);
	int smt_idx = p->port_id;

	write_lock_bh(&d->lock);
	for (e = d->l2tab[hash].first; e; e = e->next)
		if (e->addr == addr && e->ifindex == ifidx &&
		    e->smt_idx == smt_idx) {
			l2t_hold(d, e);
			if (atomic_read(&e->refcnt) == 1)
				reuse_entry(e, neigh);
			goto done;
		}

	/* Need to allocate a new entry */
	e = alloc_l2e(d);
	if (e) {
		spin_lock(&e->lock);	/* avoid race with t3_l2t_free */
		e->next = d->l2tab[hash].first;
		d->l2tab[hash].first = e;
		e->state = L2T_STATE_RESOLVING;
		e->addr = addr;
		e->ifindex = ifidx;
		e->smt_idx = smt_idx;
		atomic_set(&e->refcnt, 1);
		neigh_replace(e, neigh);
		if (neigh->dev->priv_flags & IFF_802_1Q_VLAN)
			e->vlan = VLAN_DEV_INFO(neigh->dev)->vlan_id;
		else
			e->vlan = VLAN_NONE;
		spin_unlock(&e->lock);
	}
done:
	write_unlock_bh(&d->lock);
	return e;
}

EXPORT_SYMBOL(t3_l2t_get);

/*
 * Called when address resolution fails for an L2T entry to handle packets
 * on the arpq head.  If a packet specifies a failure handler it is invoked,
 * otherwise the packets is sent to the offload device.
 *
 * XXX: maybe we should abandon the latter behavior and just require a failure
 * handler.
 */
static void handle_failed_resolution(struct t3cdev *dev, struct sk_buff *arpq)
{
	while (arpq) {
		struct sk_buff *skb = arpq;
		struct l2t_skb_cb *cb = L2T_SKB_CB(skb);

		arpq = skb->next;
		skb->next = NULL;
		if (cb->arp_failure_handler)
			cb->arp_failure_handler(dev, skb);
		else
			cxgb3_ofld_send(dev, skb);
	}
}

/*
 * Called when the host's ARP layer makes a change to some entry that is
 * loaded into the HW L2 table.
 */
void t3_l2t_update(struct t3cdev *dev, struct neighbour *neigh)
{
	struct l2t_entry *e;
	struct sk_buff *arpq = NULL;
	struct l2t_data *d = L2DATA(dev);
	u32 addr = *(u32 *) neigh->primary_key;
	int ifidx = neigh->dev->ifindex;
	int hash = arp_hash(addr, ifidx, d);

	read_lock_bh(&d->lock);
	for (e = d->l2tab[hash].first; e; e = e->next)
		if (e->addr == addr && e->ifindex == ifidx) {
			spin_lock(&e->lock);
			goto found;
		}
	read_unlock_bh(&d->lock);
	return;

found:
	read_unlock(&d->lock);
	if (atomic_read(&e->refcnt)) {
		if (neigh != e->neigh)
			neigh_replace(e, neigh);

		if (e->state == L2T_STATE_RESOLVING) {
			if (neigh->nud_state & NUD_FAILED) {
				arpq = e->arpq_head;
				e->arpq_head = e->arpq_tail = NULL;
			} else if (neigh_is_connected(neigh))
				setup_l2e_send_pending(dev, NULL, e);
		} else {
			e->state = neigh_is_connected(neigh) ?
			    L2T_STATE_VALID : L2T_STATE_STALE;
			if (memcmp(e->dmac, neigh->ha, 6))
				setup_l2e_send_pending(dev, NULL, e);
		}
	}
	spin_unlock_bh(&e->lock);

	if (arpq)
		handle_failed_resolution(dev, arpq);
}

struct l2t_data *t3_init_l2t(unsigned int l2t_capacity)
{
	struct l2t_data *d;
	int i, size = sizeof(*d) + l2t_capacity * sizeof(struct l2t_entry);

	d = cxgb_alloc_mem(size);
	if (!d)
		return NULL;

	d->nentries = l2t_capacity;
	d->rover = &d->l2tab[1];	/* entry 0 is not used */
	atomic_set(&d->nfree, l2t_capacity - 1);
	rwlock_init(&d->lock);

	for (i = 0; i < l2t_capacity; ++i) {
		d->l2tab[i].idx = i;
		d->l2tab[i].state = L2T_STATE_UNUSED;
		spin_lock_init(&d->l2tab[i].lock);
		atomic_set(&d->l2tab[i].refcnt, 0);
	}
	return d;
}

void t3_free_l2t(struct l2t_data *d)
{
	cxgb_free_mem(d);
}

