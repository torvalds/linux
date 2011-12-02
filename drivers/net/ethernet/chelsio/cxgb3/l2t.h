/*
 * Copyright (c) 2003-2008 Chelsio, Inc. All rights reserved.
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
#ifndef _CHELSIO_L2T_H
#define _CHELSIO_L2T_H

#include <linux/spinlock.h>
#include "t3cdev.h"
#include <linux/atomic.h>

enum {
	L2T_STATE_VALID,	/* entry is up to date */
	L2T_STATE_STALE,	/* entry may be used but needs revalidation */
	L2T_STATE_RESOLVING,	/* entry needs address resolution */
	L2T_STATE_UNUSED	/* entry not in use */
};

struct neighbour;
struct sk_buff;

/*
 * Each L2T entry plays multiple roles.  First of all, it keeps state for the
 * corresponding entry of the HW L2 table and maintains a queue of offload
 * packets awaiting address resolution.  Second, it is a node of a hash table
 * chain, where the nodes of the chain are linked together through their next
 * pointer.  Finally, each node is a bucket of a hash table, pointing to the
 * first element in its chain through its first pointer.
 */
struct l2t_entry {
	u16 state;		/* entry state */
	u16 idx;		/* entry index */
	u32 addr;		/* dest IP address */
	int ifindex;		/* neighbor's net_device's ifindex */
	u16 smt_idx;		/* SMT index */
	u16 vlan;		/* VLAN TCI (id: bits 0-11, prio: 13-15 */
	struct neighbour *neigh;	/* associated neighbour */
	struct l2t_entry *first;	/* start of hash chain */
	struct l2t_entry *next;	/* next l2t_entry on chain */
	struct sk_buff_head arpq;	/* queue of packets awaiting resolution */
	spinlock_t lock;
	atomic_t refcnt;	/* entry reference count */
	u8 dmac[6];		/* neighbour's MAC address */
};

struct l2t_data {
	unsigned int nentries;	/* number of entries */
	struct l2t_entry *rover;	/* starting point for next allocation */
	atomic_t nfree;		/* number of free entries */
	rwlock_t lock;
	struct l2t_entry l2tab[0];
	struct rcu_head rcu_head;	/* to handle rcu cleanup */
};

typedef void (*arp_failure_handler_func)(struct t3cdev * dev,
					 struct sk_buff * skb);

/*
 * Callback stored in an skb to handle address resolution failure.
 */
struct l2t_skb_cb {
	arp_failure_handler_func arp_failure_handler;
};

#define L2T_SKB_CB(skb) ((struct l2t_skb_cb *)(skb)->cb)

static inline void set_arp_failure_handler(struct sk_buff *skb,
					   arp_failure_handler_func hnd)
{
	L2T_SKB_CB(skb)->arp_failure_handler = hnd;
}

/*
 * Getting to the L2 data from an offload device.
 */
#define L2DATA(cdev) (rcu_dereference((cdev)->l2opt))

#define W_TCB_L2T_IX    0
#define S_TCB_L2T_IX    7
#define M_TCB_L2T_IX    0x7ffULL
#define V_TCB_L2T_IX(x) ((x) << S_TCB_L2T_IX)

void t3_l2e_free(struct l2t_data *d, struct l2t_entry *e);
void t3_l2t_update(struct t3cdev *dev, struct neighbour *neigh);
struct l2t_entry *t3_l2t_get(struct t3cdev *cdev, struct dst_entry *dst,
			     struct net_device *dev);
int t3_l2t_send_slow(struct t3cdev *dev, struct sk_buff *skb,
		     struct l2t_entry *e);
void t3_l2t_send_event(struct t3cdev *dev, struct l2t_entry *e);
struct l2t_data *t3_init_l2t(unsigned int l2t_capacity);
void t3_free_l2t(struct l2t_data *d);

int cxgb3_ofld_send(struct t3cdev *dev, struct sk_buff *skb);

static inline int l2t_send(struct t3cdev *dev, struct sk_buff *skb,
			   struct l2t_entry *e)
{
	if (likely(e->state == L2T_STATE_VALID))
		return cxgb3_ofld_send(dev, skb);
	return t3_l2t_send_slow(dev, skb, e);
}

static inline void l2t_release(struct t3cdev *t, struct l2t_entry *e)
{
	struct l2t_data *d;

	rcu_read_lock();
	d = L2DATA(t);

	if (atomic_dec_and_test(&e->refcnt) && d)
		t3_l2e_free(d, e);

	rcu_read_unlock();
}

static inline void l2t_hold(struct l2t_data *d, struct l2t_entry *e)
{
	if (d && atomic_add_return(1, &e->refcnt) == 1)	/* 0 -> 1 transition */
		atomic_dec(&d->nfree);
}

#endif
