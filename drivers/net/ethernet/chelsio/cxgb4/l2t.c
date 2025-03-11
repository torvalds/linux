/*
 * This file is part of the Chelsio T4 Ethernet driver for Linux.
 *
 * Copyright (c) 2003-2014 Chelsio Communications, Inc. All rights reserved.
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
#include <linux/module.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <net/neighbour.h>
#include "cxgb4.h"
#include "l2t.h"
#include "t4_msg.h"
#include "t4fw_api.h"
#include "t4_regs.h"
#include "t4_values.h"

/* identifies sync vs async L2T_WRITE_REQs */
#define SYNC_WR_S    12
#define SYNC_WR_V(x) ((x) << SYNC_WR_S)
#define SYNC_WR_F    SYNC_WR_V(1)

struct l2t_data {
	unsigned int l2t_start;     /* start index of our piece of the L2T */
	unsigned int l2t_size;      /* number of entries in l2tab */
	rwlock_t lock;
	atomic_t nfree;             /* number of free entries */
	struct l2t_entry *rover;    /* starting point for next allocation */
	struct l2t_entry l2tab[] __counted_by(l2t_size);  /* MUST BE LAST */
};

static inline unsigned int vlan_prio(const struct l2t_entry *e)
{
	return e->vlan >> VLAN_PRIO_SHIFT;
}

static inline void l2t_hold(struct l2t_data *d, struct l2t_entry *e)
{
	if (atomic_add_return(1, &e->refcnt) == 1)  /* 0 -> 1 transition */
		atomic_dec(&d->nfree);
}

/*
 * To avoid having to check address families we do not allow v4 and v6
 * neighbors to be on the same hash chain.  We keep v4 entries in the first
 * half of available hash buckets and v6 in the second.  We need at least two
 * entries in our L2T for this scheme to work.
 */
enum {
	L2T_MIN_HASH_BUCKETS = 2,
};

static inline unsigned int arp_hash(struct l2t_data *d, const u32 *key,
				    int ifindex)
{
	unsigned int l2t_size_half = d->l2t_size / 2;

	return jhash_2words(*key, ifindex, 0) % l2t_size_half;
}

static inline unsigned int ipv6_hash(struct l2t_data *d, const u32 *key,
				     int ifindex)
{
	unsigned int l2t_size_half = d->l2t_size / 2;
	u32 xor = key[0] ^ key[1] ^ key[2] ^ key[3];

	return (l2t_size_half +
		(jhash_2words(xor, ifindex, 0) % l2t_size_half));
}

static unsigned int addr_hash(struct l2t_data *d, const u32 *addr,
			      int addr_len, int ifindex)
{
	return addr_len == 4 ? arp_hash(d, addr, ifindex) :
			       ipv6_hash(d, addr, ifindex);
}

/*
 * Checks if an L2T entry is for the given IP/IPv6 address.  It does not check
 * whether the L2T entry and the address are of the same address family.
 * Callers ensure an address is only checked against L2T entries of the same
 * family, something made trivial by the separation of IP and IPv6 hash chains
 * mentioned above.  Returns 0 if there's a match,
 */
static int addreq(const struct l2t_entry *e, const u32 *addr)
{
	if (e->v6)
		return (e->addr[0] ^ addr[0]) | (e->addr[1] ^ addr[1]) |
		       (e->addr[2] ^ addr[2]) | (e->addr[3] ^ addr[3]);
	return e->addr[0] ^ addr[0];
}

static void neigh_replace(struct l2t_entry *e, struct neighbour *n)
{
	neigh_hold(n);
	if (e->neigh)
		neigh_release(e->neigh);
	e->neigh = n;
}

/*
 * Write an L2T entry.  Must be called with the entry locked.
 * The write may be synchronous or asynchronous.
 */
static int write_l2e(struct adapter *adap, struct l2t_entry *e, int sync)
{
	struct l2t_data *d = adap->l2t;
	unsigned int l2t_idx = e->idx + d->l2t_start;
	struct sk_buff *skb;
	struct cpl_l2t_write_req *req;

	skb = alloc_skb(sizeof(*req), GFP_ATOMIC);
	if (!skb)
		return -ENOMEM;

	req = __skb_put(skb, sizeof(*req));
	INIT_TP_WR(req, 0);

	OPCODE_TID(req) = htonl(MK_OPCODE_TID(CPL_L2T_WRITE_REQ,
					l2t_idx | (sync ? SYNC_WR_F : 0) |
					TID_QID_V(adap->sge.fw_evtq.abs_id)));
	req->params = htons(L2T_W_PORT_V(e->lport) | L2T_W_NOREPLY_V(!sync));
	req->l2t_idx = htons(l2t_idx);
	req->vlan = htons(e->vlan);
	if (e->neigh && !(e->neigh->dev->flags & IFF_LOOPBACK))
		memcpy(e->dmac, e->neigh->ha, sizeof(e->dmac));
	memcpy(req->dst_mac, e->dmac, sizeof(req->dst_mac));

	t4_mgmt_tx(adap, skb);

	if (sync && e->state != L2T_STATE_SWITCHING)
		e->state = L2T_STATE_SYNC_WRITE;
	return 0;
}

/*
 * Send packets waiting in an L2T entry's ARP queue.  Must be called with the
 * entry locked.
 */
static void send_pending(struct adapter *adap, struct l2t_entry *e)
{
	struct sk_buff *skb;

	while ((skb = __skb_dequeue(&e->arpq)) != NULL)
		t4_ofld_send(adap, skb);
}

/*
 * Process a CPL_L2T_WRITE_RPL.  Wake up the ARP queue if it completes a
 * synchronous L2T_WRITE.  Note that the TID in the reply is really the L2T
 * index it refers to.
 */
void do_l2t_write_rpl(struct adapter *adap, const struct cpl_l2t_write_rpl *rpl)
{
	struct l2t_data *d = adap->l2t;
	unsigned int tid = GET_TID(rpl);
	unsigned int l2t_idx = tid % L2T_SIZE;

	if (unlikely(rpl->status != CPL_ERR_NONE)) {
		dev_err(adap->pdev_dev,
			"Unexpected L2T_WRITE_RPL status %u for entry %u\n",
			rpl->status, l2t_idx);
		return;
	}

	if (tid & SYNC_WR_F) {
		struct l2t_entry *e = &d->l2tab[l2t_idx - d->l2t_start];

		spin_lock(&e->lock);
		if (e->state != L2T_STATE_SWITCHING) {
			send_pending(adap, e);
			e->state = (e->neigh->nud_state & NUD_STALE) ?
					L2T_STATE_STALE : L2T_STATE_VALID;
		}
		spin_unlock(&e->lock);
	}
}

/*
 * Add a packet to an L2T entry's queue of packets awaiting resolution.
 * Must be called with the entry's lock held.
 */
static inline void arpq_enqueue(struct l2t_entry *e, struct sk_buff *skb)
{
	__skb_queue_tail(&e->arpq, skb);
}

int cxgb4_l2t_send(struct net_device *dev, struct sk_buff *skb,
		   struct l2t_entry *e)
{
	struct adapter *adap = netdev2adap(dev);

again:
	switch (e->state) {
	case L2T_STATE_STALE:     /* entry is stale, kick off revalidation */
		neigh_event_send(e->neigh, NULL);
		spin_lock_bh(&e->lock);
		if (e->state == L2T_STATE_STALE)
			e->state = L2T_STATE_VALID;
		spin_unlock_bh(&e->lock);
		fallthrough;
	case L2T_STATE_VALID:     /* fast-path, send the packet on */
		return t4_ofld_send(adap, skb);
	case L2T_STATE_RESOLVING:
	case L2T_STATE_SYNC_WRITE:
		spin_lock_bh(&e->lock);
		if (e->state != L2T_STATE_SYNC_WRITE &&
		    e->state != L2T_STATE_RESOLVING) {
			spin_unlock_bh(&e->lock);
			goto again;
		}
		arpq_enqueue(e, skb);
		spin_unlock_bh(&e->lock);

		if (e->state == L2T_STATE_RESOLVING &&
		    !neigh_event_send(e->neigh, NULL)) {
			spin_lock_bh(&e->lock);
			if (e->state == L2T_STATE_RESOLVING &&
			    !skb_queue_empty(&e->arpq))
				write_l2e(adap, e, 1);
			spin_unlock_bh(&e->lock);
		}
	}
	return 0;
}
EXPORT_SYMBOL(cxgb4_l2t_send);

/*
 * Allocate a free L2T entry.  Must be called with l2t_data.lock held.
 */
static struct l2t_entry *alloc_l2e(struct l2t_data *d)
{
	struct l2t_entry *end, *e, **p;

	if (!atomic_read(&d->nfree))
		return NULL;

	/* there's definitely a free entry */
	for (e = d->rover, end = &d->l2tab[d->l2t_size]; e != end; ++e)
		if (atomic_read(&e->refcnt) == 0)
			goto found;

	for (e = d->l2tab; atomic_read(&e->refcnt); ++e)
		;
found:
	d->rover = e + 1;
	atomic_dec(&d->nfree);

	/*
	 * The entry we found may be an inactive entry that is
	 * presently in the hash table.  We need to remove it.
	 */
	if (e->state < L2T_STATE_SWITCHING)
		for (p = &d->l2tab[e->hash].first; *p; p = &(*p)->next)
			if (*p == e) {
				*p = e->next;
				e->next = NULL;
				break;
			}

	e->state = L2T_STATE_UNUSED;
	return e;
}

static struct l2t_entry *find_or_alloc_l2e(struct l2t_data *d, u16 vlan,
					   u8 port, u8 *dmac)
{
	struct l2t_entry *end, *e, **p;
	struct l2t_entry *first_free = NULL;

	for (e = &d->l2tab[0], end = &d->l2tab[d->l2t_size]; e != end; ++e) {
		if (atomic_read(&e->refcnt) == 0) {
			if (!first_free)
				first_free = e;
		} else {
			if (e->state == L2T_STATE_SWITCHING) {
				if (ether_addr_equal(e->dmac, dmac) &&
				    (e->vlan == vlan) && (e->lport == port))
					goto exists;
			}
		}
	}

	if (first_free) {
		e = first_free;
		goto found;
	}

	return NULL;

found:
	/* The entry we found may be an inactive entry that is
	 * presently in the hash table.  We need to remove it.
	 */
	if (e->state < L2T_STATE_SWITCHING)
		for (p = &d->l2tab[e->hash].first; *p; p = &(*p)->next)
			if (*p == e) {
				*p = e->next;
				e->next = NULL;
				break;
			}
	e->state = L2T_STATE_UNUSED;

exists:
	return e;
}

/* Called when an L2T entry has no more users.  The entry is left in the hash
 * table since it is likely to be reused but we also bump nfree to indicate
 * that the entry can be reallocated for a different neighbor.  We also drop
 * the existing neighbor reference in case the neighbor is going away and is
 * waiting on our reference.
 *
 * Because entries can be reallocated to other neighbors once their ref count
 * drops to 0 we need to take the entry's lock to avoid races with a new
 * incarnation.
 */
static void _t4_l2e_free(struct l2t_entry *e)
{
	struct l2t_data *d;

	if (atomic_read(&e->refcnt) == 0) {  /* hasn't been recycled */
		if (e->neigh) {
			neigh_release(e->neigh);
			e->neigh = NULL;
		}
		__skb_queue_purge(&e->arpq);
	}

	d = container_of(e, struct l2t_data, l2tab[e->idx]);
	atomic_inc(&d->nfree);
}

/* Locked version of _t4_l2e_free */
static void t4_l2e_free(struct l2t_entry *e)
{
	struct l2t_data *d;

	spin_lock_bh(&e->lock);
	if (atomic_read(&e->refcnt) == 0) {  /* hasn't been recycled */
		if (e->neigh) {
			neigh_release(e->neigh);
			e->neigh = NULL;
		}
		__skb_queue_purge(&e->arpq);
	}
	spin_unlock_bh(&e->lock);

	d = container_of(e, struct l2t_data, l2tab[e->idx]);
	atomic_inc(&d->nfree);
}

void cxgb4_l2t_release(struct l2t_entry *e)
{
	if (atomic_dec_and_test(&e->refcnt))
		t4_l2e_free(e);
}
EXPORT_SYMBOL(cxgb4_l2t_release);

/*
 * Update an L2T entry that was previously used for the same next hop as neigh.
 * Must be called with softirqs disabled.
 */
static void reuse_entry(struct l2t_entry *e, struct neighbour *neigh)
{
	unsigned int nud_state;

	spin_lock(&e->lock);                /* avoid race with t4_l2t_free */
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

struct l2t_entry *cxgb4_l2t_get(struct l2t_data *d, struct neighbour *neigh,
				const struct net_device *physdev,
				unsigned int priority)
{
	u8 lport;
	u16 vlan;
	struct l2t_entry *e;
	unsigned int addr_len = neigh->tbl->key_len;
	u32 *addr = (u32 *)neigh->primary_key;
	int ifidx = neigh->dev->ifindex;
	int hash = addr_hash(d, addr, addr_len, ifidx);

	if (neigh->dev->flags & IFF_LOOPBACK)
		lport = netdev2pinfo(physdev)->tx_chan + 4;
	else
		lport = netdev2pinfo(physdev)->lport;

	if (is_vlan_dev(neigh->dev)) {
		vlan = vlan_dev_vlan_id(neigh->dev);
		vlan |= vlan_dev_get_egress_qos_mask(neigh->dev, priority);
	} else {
		vlan = VLAN_NONE;
	}

	write_lock_bh(&d->lock);
	for (e = d->l2tab[hash].first; e; e = e->next)
		if (!addreq(e, addr) && e->ifindex == ifidx &&
		    e->vlan == vlan && e->lport == lport) {
			l2t_hold(d, e);
			if (atomic_read(&e->refcnt) == 1)
				reuse_entry(e, neigh);
			goto done;
		}

	/* Need to allocate a new entry */
	e = alloc_l2e(d);
	if (e) {
		spin_lock(&e->lock);          /* avoid race with t4_l2t_free */
		e->state = L2T_STATE_RESOLVING;
		if (neigh->dev->flags & IFF_LOOPBACK)
			memcpy(e->dmac, physdev->dev_addr, sizeof(e->dmac));
		memcpy(e->addr, addr, addr_len);
		e->ifindex = ifidx;
		e->hash = hash;
		e->lport = lport;
		e->v6 = addr_len == 16;
		atomic_set(&e->refcnt, 1);
		neigh_replace(e, neigh);
		e->vlan = vlan;
		e->next = d->l2tab[hash].first;
		d->l2tab[hash].first = e;
		spin_unlock(&e->lock);
	}
done:
	write_unlock_bh(&d->lock);
	return e;
}
EXPORT_SYMBOL(cxgb4_l2t_get);

u64 cxgb4_select_ntuple(struct net_device *dev,
			const struct l2t_entry *l2t)
{
	struct adapter *adap = netdev2adap(dev);
	struct tp_params *tp = &adap->params.tp;
	u64 ntuple = 0;

	/* Initialize each of the fields which we care about which are present
	 * in the Compressed Filter Tuple.
	 */
	if (tp->vlan_shift >= 0 && l2t->vlan != VLAN_NONE)
		ntuple |= (u64)(FT_VLAN_VLD_F | l2t->vlan) << tp->vlan_shift;

	if (tp->port_shift >= 0)
		ntuple |= (u64)l2t->lport << tp->port_shift;

	if (tp->protocol_shift >= 0)
		ntuple |= (u64)IPPROTO_TCP << tp->protocol_shift;

	if (tp->vnic_shift >= 0 && (tp->ingress_config & VNIC_F)) {
		struct port_info *pi = (struct port_info *)netdev_priv(dev);

		ntuple |= (u64)(FT_VNID_ID_VF_V(pi->vin) |
				FT_VNID_ID_PF_V(adap->pf) |
				FT_VNID_ID_VLD_V(pi->vivld)) << tp->vnic_shift;
	}

	return ntuple;
}
EXPORT_SYMBOL(cxgb4_select_ntuple);

/*
 * Called when the host's neighbor layer makes a change to some entry that is
 * loaded into the HW L2 table.
 */
void t4_l2t_update(struct adapter *adap, struct neighbour *neigh)
{
	unsigned int addr_len = neigh->tbl->key_len;
	u32 *addr = (u32 *) neigh->primary_key;
	int hash, ifidx = neigh->dev->ifindex;
	struct sk_buff_head *arpq = NULL;
	struct l2t_data *d = adap->l2t;
	struct l2t_entry *e;

	hash = addr_hash(d, addr, addr_len, ifidx);
	read_lock_bh(&d->lock);
	for (e = d->l2tab[hash].first; e; e = e->next)
		if (!addreq(e, addr) && e->ifindex == ifidx) {
			spin_lock(&e->lock);
			if (atomic_read(&e->refcnt))
				goto found;
			spin_unlock(&e->lock);
			break;
		}
	read_unlock_bh(&d->lock);
	return;

 found:
	read_unlock(&d->lock);

	if (neigh != e->neigh)
		neigh_replace(e, neigh);

	if (e->state == L2T_STATE_RESOLVING) {
		if (neigh->nud_state & NUD_FAILED) {
			arpq = &e->arpq;
		} else if ((neigh->nud_state & (NUD_CONNECTED | NUD_STALE)) &&
			   !skb_queue_empty(&e->arpq)) {
			write_l2e(adap, e, 1);
		}
	} else {
		e->state = neigh->nud_state & NUD_CONNECTED ?
			L2T_STATE_VALID : L2T_STATE_STALE;
		if (memcmp(e->dmac, neigh->ha, sizeof(e->dmac)))
			write_l2e(adap, e, 0);
	}

	if (arpq) {
		struct sk_buff *skb;

		/* Called when address resolution fails for an L2T
		 * entry to handle packets on the arpq head. If a
		 * packet specifies a failure handler it is invoked,
		 * otherwise the packet is sent to the device.
		 */
		while ((skb = __skb_dequeue(&e->arpq)) != NULL) {
			const struct l2t_skb_cb *cb = L2T_SKB_CB(skb);

			spin_unlock(&e->lock);
			if (cb->arp_err_handler)
				cb->arp_err_handler(cb->handle, skb);
			else
				t4_ofld_send(adap, skb);
			spin_lock(&e->lock);
		}
	}
	spin_unlock_bh(&e->lock);
}

/* Allocate an L2T entry for use by a switching rule.  Such need to be
 * explicitly freed and while busy they are not on any hash chain, so normal
 * address resolution updates do not see them.
 */
struct l2t_entry *t4_l2t_alloc_switching(struct adapter *adap, u16 vlan,
					 u8 port, u8 *eth_addr)
{
	struct l2t_data *d = adap->l2t;
	struct l2t_entry *e;
	int ret;

	write_lock_bh(&d->lock);
	e = find_or_alloc_l2e(d, vlan, port, eth_addr);
	if (e) {
		spin_lock(&e->lock);          /* avoid race with t4_l2t_free */
		if (!atomic_read(&e->refcnt)) {
			e->state = L2T_STATE_SWITCHING;
			e->vlan = vlan;
			e->lport = port;
			ether_addr_copy(e->dmac, eth_addr);
			atomic_set(&e->refcnt, 1);
			ret = write_l2e(adap, e, 0);
			if (ret < 0) {
				_t4_l2e_free(e);
				spin_unlock(&e->lock);
				write_unlock_bh(&d->lock);
				return NULL;
			}
		} else {
			atomic_inc(&e->refcnt);
		}

		spin_unlock(&e->lock);
	}
	write_unlock_bh(&d->lock);
	return e;
}

struct l2t_data *t4_init_l2t(unsigned int l2t_start, unsigned int l2t_end)
{
	unsigned int l2t_size;
	int i;
	struct l2t_data *d;

	if (l2t_start >= l2t_end || l2t_end >= L2T_SIZE)
		return NULL;
	l2t_size = l2t_end - l2t_start + 1;
	if (l2t_size < L2T_MIN_HASH_BUCKETS)
		return NULL;

	d = kvzalloc(struct_size(d, l2tab, l2t_size), GFP_KERNEL);
	if (!d)
		return NULL;

	d->l2t_start = l2t_start;
	d->l2t_size = l2t_size;

	d->rover = d->l2tab;
	atomic_set(&d->nfree, l2t_size);
	rwlock_init(&d->lock);

	for (i = 0; i < d->l2t_size; ++i) {
		d->l2tab[i].idx = i;
		d->l2tab[i].state = L2T_STATE_UNUSED;
		spin_lock_init(&d->l2tab[i].lock);
		atomic_set(&d->l2tab[i].refcnt, 0);
		skb_queue_head_init(&d->l2tab[i].arpq);
	}
	return d;
}

static inline void *l2t_get_idx(struct seq_file *seq, loff_t pos)
{
	struct l2t_data *d = seq->private;

	return pos >= d->l2t_size ? NULL : &d->l2tab[pos];
}

static void *l2t_seq_start(struct seq_file *seq, loff_t *pos)
{
	return *pos ? l2t_get_idx(seq, *pos - 1) : SEQ_START_TOKEN;
}

static void *l2t_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	v = l2t_get_idx(seq, *pos);
	++(*pos);
	return v;
}

static void l2t_seq_stop(struct seq_file *seq, void *v)
{
}

static char l2e_state(const struct l2t_entry *e)
{
	switch (e->state) {
	case L2T_STATE_VALID: return 'V';
	case L2T_STATE_STALE: return 'S';
	case L2T_STATE_SYNC_WRITE: return 'W';
	case L2T_STATE_RESOLVING:
		return skb_queue_empty(&e->arpq) ? 'R' : 'A';
	case L2T_STATE_SWITCHING: return 'X';
	default:
		return 'U';
	}
}

bool cxgb4_check_l2t_valid(struct l2t_entry *e)
{
	bool valid;

	spin_lock(&e->lock);
	valid = (e->state == L2T_STATE_VALID);
	spin_unlock(&e->lock);
	return valid;
}
EXPORT_SYMBOL(cxgb4_check_l2t_valid);

static int l2t_seq_show(struct seq_file *seq, void *v)
{
	if (v == SEQ_START_TOKEN)
		seq_puts(seq, " Idx IP address                "
			 "Ethernet address  VLAN/P LP State Users Port\n");
	else {
		char ip[60];
		struct l2t_data *d = seq->private;
		struct l2t_entry *e = v;

		spin_lock_bh(&e->lock);
		if (e->state == L2T_STATE_SWITCHING)
			ip[0] = '\0';
		else
			sprintf(ip, e->v6 ? "%pI6c" : "%pI4", e->addr);
		seq_printf(seq, "%4u %-25s %17pM %4d %u %2u   %c   %5u %s\n",
			   e->idx + d->l2t_start, ip, e->dmac,
			   e->vlan & VLAN_VID_MASK, vlan_prio(e), e->lport,
			   l2e_state(e), atomic_read(&e->refcnt),
			   e->neigh ? e->neigh->dev->name : "");
		spin_unlock_bh(&e->lock);
	}
	return 0;
}

static const struct seq_operations l2t_seq_ops = {
	.start = l2t_seq_start,
	.next = l2t_seq_next,
	.stop = l2t_seq_stop,
	.show = l2t_seq_show
};

static int l2t_seq_open(struct inode *inode, struct file *file)
{
	int rc = seq_open(file, &l2t_seq_ops);

	if (!rc) {
		struct adapter *adap = inode->i_private;
		struct seq_file *seq = file->private_data;

		seq->private = adap->l2t;
	}
	return rc;
}

const struct file_operations t4_l2t_fops = {
	.owner = THIS_MODULE,
	.open = l2t_seq_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release,
};
