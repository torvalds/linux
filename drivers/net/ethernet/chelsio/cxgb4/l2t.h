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

#ifndef __CXGB4_L2T_H
#define __CXGB4_L2T_H

#include <linux/spinlock.h>
#include <linux/if_ether.h>
#include <linux/atomic.h>

#define VLAN_NONE 0xfff

enum { L2T_SIZE = 4096 };     /* # of L2T entries */

enum {
	L2T_STATE_VALID,      /* entry is up to date */
	L2T_STATE_STALE,      /* entry may be used but needs revalidation */
	L2T_STATE_RESOLVING,  /* entry needs address resolution */
	L2T_STATE_SYNC_WRITE, /* synchronous write of entry underway */
	L2T_STATE_NOARP,      /* Netdev down or removed*/

	/* when state is one of the below the entry is not hashed */
	L2T_STATE_SWITCHING,  /* entry is being used by a switching filter */
	L2T_STATE_UNUSED      /* entry not in use */
};

struct adapter;
struct l2t_data;
struct neighbour;
struct net_device;
struct file_operations;
struct cpl_l2t_write_rpl;

/*
 * Each L2T entry plays multiple roles.  First of all, it keeps state for the
 * corresponding entry of the HW L2 table and maintains a queue of offload
 * packets awaiting address resolution.  Second, it is a node of a hash table
 * chain, where the nodes of the chain are linked together through their next
 * pointer.  Finally, each node is a bucket of a hash table, pointing to the
 * first element in its chain through its first pointer.
 */
struct l2t_entry {
	u16 state;                  /* entry state */
	u16 idx;                    /* entry index within in-memory table */
	u32 addr[4];                /* next hop IP or IPv6 address */
	int ifindex;                /* neighbor's net_device's ifindex */
	struct neighbour *neigh;    /* associated neighbour */
	struct l2t_entry *first;    /* start of hash chain */
	struct l2t_entry *next;     /* next l2t_entry on chain */
	struct sk_buff_head arpq;   /* packet queue awaiting resolution */
	spinlock_t lock;
	atomic_t refcnt;            /* entry reference count */
	u16 hash;                   /* hash bucket the entry is on */
	u16 vlan;                   /* VLAN TCI (id: bits 0-11, prio: 13-15 */
	u8 v6;                      /* whether entry is for IPv6 */
	u8 lport;                   /* associated offload logical interface */
	u8 dmac[ETH_ALEN];          /* neighbour's MAC address */
};

typedef void (*arp_err_handler_t)(void *handle, struct sk_buff *skb);

/*
 * Callback stored in an skb to handle address resolution failure.
 */
struct l2t_skb_cb {
	void *handle;
	arp_err_handler_t arp_err_handler;
};

#define L2T_SKB_CB(skb) ((struct l2t_skb_cb *)(skb)->cb)

static inline void t4_set_arp_err_handler(struct sk_buff *skb, void *handle,
					  arp_err_handler_t handler)
{
	L2T_SKB_CB(skb)->handle = handle;
	L2T_SKB_CB(skb)->arp_err_handler = handler;
}

void cxgb4_l2t_release(struct l2t_entry *e);
int cxgb4_l2t_send(struct net_device *dev, struct sk_buff *skb,
		   struct l2t_entry *e);
struct l2t_entry *cxgb4_l2t_get(struct l2t_data *d, struct neighbour *neigh,
				const struct net_device *physdev,
				unsigned int priority);
u64 cxgb4_select_ntuple(struct net_device *dev,
			const struct l2t_entry *l2t);
struct l2t_entry *cxgb4_l2t_alloc_switching(struct net_device *dev, u16 vlan,
					    u8 port, u8 *dmac);
void t4_l2t_update(struct adapter *adap, struct neighbour *neigh);
struct l2t_entry *t4_l2t_alloc_switching(struct adapter *adap, u16 vlan,
					 u8 port, u8 *dmac);
struct l2t_data *t4_init_l2t(unsigned int l2t_start, unsigned int l2t_end);
void do_l2t_write_rpl(struct adapter *p, const struct cpl_l2t_write_rpl *rpl);

extern const struct file_operations t4_l2t_fops;
#endif  /* __CXGB4_L2T_H */
