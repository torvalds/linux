/*
 * Copyright(c) 1999 - 2004 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/pkt_sched.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/timer.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/if_arp.h>
#include <linux/if_ether.h>
#include <linux/if_bonding.h>
#include <linux/if_vlan.h>
#include <linux/in.h>
#include <net/ipx.h>
#include <net/arp.h>
#include <net/ipv6.h>
#include <asm/byteorder.h>
#include "bonding.h"
#include "bond_alb.h"



#ifndef __long_aligned
#define __long_aligned __attribute__((aligned((sizeof(long)))))
#endif
static const u8 mac_bcast[ETH_ALEN] __long_aligned = {
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};
static const u8 mac_v6_allmcast[ETH_ALEN] __long_aligned = {
	0x33, 0x33, 0x00, 0x00, 0x00, 0x01
};
static const int alb_delta_in_ticks = HZ / ALB_TIMER_TICKS_PER_SEC;

#pragma pack(1)
struct learning_pkt {
	u8 mac_dst[ETH_ALEN];
	u8 mac_src[ETH_ALEN];
	__be16 type;
	u8 padding[ETH_ZLEN - ETH_HLEN];
};

struct arp_pkt {
	__be16  hw_addr_space;
	__be16  prot_addr_space;
	u8      hw_addr_len;
	u8      prot_addr_len;
	__be16  op_code;
	u8      mac_src[ETH_ALEN];	/* sender hardware address */
	__be32  ip_src;			/* sender IP address */
	u8      mac_dst[ETH_ALEN];	/* target hardware address */
	__be32  ip_dst;			/* target IP address */
};
#pragma pack()

static inline struct arp_pkt *arp_pkt(const struct sk_buff *skb)
{
	return (struct arp_pkt *)skb_network_header(skb);
}

/* Forward declaration */
static void alb_send_learning_packets(struct slave *slave, u8 mac_addr[]);

static inline u8 _simple_hash(const u8 *hash_start, int hash_size)
{
	int i;
	u8 hash = 0;

	for (i = 0; i < hash_size; i++) {
		hash ^= hash_start[i];
	}

	return hash;
}

/*********************** tlb specific functions ***************************/

static inline void _lock_tx_hashtbl(struct bonding *bond)
{
	spin_lock_bh(&(BOND_ALB_INFO(bond).tx_hashtbl_lock));
}

static inline void _unlock_tx_hashtbl(struct bonding *bond)
{
	spin_unlock_bh(&(BOND_ALB_INFO(bond).tx_hashtbl_lock));
}

/* Caller must hold tx_hashtbl lock */
static inline void tlb_init_table_entry(struct tlb_client_info *entry, int save_load)
{
	if (save_load) {
		entry->load_history = 1 + entry->tx_bytes /
				      BOND_TLB_REBALANCE_INTERVAL;
		entry->tx_bytes = 0;
	}

	entry->tx_slave = NULL;
	entry->next = TLB_NULL_INDEX;
	entry->prev = TLB_NULL_INDEX;
}

static inline void tlb_init_slave(struct slave *slave)
{
	SLAVE_TLB_INFO(slave).load = 0;
	SLAVE_TLB_INFO(slave).head = TLB_NULL_INDEX;
}

/* Caller must hold bond lock for read */
static void tlb_clear_slave(struct bonding *bond, struct slave *slave, int save_load)
{
	struct tlb_client_info *tx_hash_table;
	u32 index;

	_lock_tx_hashtbl(bond);

	/* clear slave from tx_hashtbl */
	tx_hash_table = BOND_ALB_INFO(bond).tx_hashtbl;

	/* skip this if we've already freed the tx hash table */
	if (tx_hash_table) {
		index = SLAVE_TLB_INFO(slave).head;
		while (index != TLB_NULL_INDEX) {
			u32 next_index = tx_hash_table[index].next;
			tlb_init_table_entry(&tx_hash_table[index], save_load);
			index = next_index;
		}
	}

	tlb_init_slave(slave);

	_unlock_tx_hashtbl(bond);
}

/* Must be called before starting the monitor timer */
static int tlb_initialize(struct bonding *bond)
{
	struct alb_bond_info *bond_info = &(BOND_ALB_INFO(bond));
	int size = TLB_HASH_TABLE_SIZE * sizeof(struct tlb_client_info);
	struct tlb_client_info *new_hashtbl;
	int i;

	new_hashtbl = kzalloc(size, GFP_KERNEL);
	if (!new_hashtbl) {
		pr_err("%s: Error: Failed to allocate TLB hash table\n",
		       bond->dev->name);
		return -1;
	}
	_lock_tx_hashtbl(bond);

	bond_info->tx_hashtbl = new_hashtbl;

	for (i = 0; i < TLB_HASH_TABLE_SIZE; i++) {
		tlb_init_table_entry(&bond_info->tx_hashtbl[i], 0);
	}

	_unlock_tx_hashtbl(bond);

	return 0;
}

/* Must be called only after all slaves have been released */
static void tlb_deinitialize(struct bonding *bond)
{
	struct alb_bond_info *bond_info = &(BOND_ALB_INFO(bond));

	_lock_tx_hashtbl(bond);

	kfree(bond_info->tx_hashtbl);
	bond_info->tx_hashtbl = NULL;

	_unlock_tx_hashtbl(bond);
}

static long long compute_gap(struct slave *slave)
{
	return (s64) (slave->speed << 20) - /* Convert to Megabit per sec */
	       (s64) (SLAVE_TLB_INFO(slave).load << 3); /* Bytes to bits */
}

/* Caller must hold bond lock for read */
static struct slave *tlb_get_least_loaded_slave(struct bonding *bond)
{
	struct slave *slave, *least_loaded;
	long long max_gap;
	int i;

	least_loaded = NULL;
	max_gap = LLONG_MIN;

	/* Find the slave with the largest gap */
	bond_for_each_slave(bond, slave, i) {
		if (SLAVE_IS_OK(slave)) {
			long long gap = compute_gap(slave);

			if (max_gap < gap) {
				least_loaded = slave;
				max_gap = gap;
			}
		}
	}

	return least_loaded;
}

/* Caller must hold bond lock for read */
static struct slave *tlb_choose_channel(struct bonding *bond, u32 hash_index, u32 skb_len)
{
	struct alb_bond_info *bond_info = &(BOND_ALB_INFO(bond));
	struct tlb_client_info *hash_table;
	struct slave *assigned_slave;

	_lock_tx_hashtbl(bond);

	hash_table = bond_info->tx_hashtbl;
	assigned_slave = hash_table[hash_index].tx_slave;
	if (!assigned_slave) {
		assigned_slave = tlb_get_least_loaded_slave(bond);

		if (assigned_slave) {
			struct tlb_slave_info *slave_info =
				&(SLAVE_TLB_INFO(assigned_slave));
			u32 next_index = slave_info->head;

			hash_table[hash_index].tx_slave = assigned_slave;
			hash_table[hash_index].next = next_index;
			hash_table[hash_index].prev = TLB_NULL_INDEX;

			if (next_index != TLB_NULL_INDEX) {
				hash_table[next_index].prev = hash_index;
			}

			slave_info->head = hash_index;
			slave_info->load +=
				hash_table[hash_index].load_history;
		}
	}

	if (assigned_slave) {
		hash_table[hash_index].tx_bytes += skb_len;
	}

	_unlock_tx_hashtbl(bond);

	return assigned_slave;
}

/*********************** rlb specific functions ***************************/
static inline void _lock_rx_hashtbl(struct bonding *bond)
{
	spin_lock_bh(&(BOND_ALB_INFO(bond).rx_hashtbl_lock));
}

static inline void _unlock_rx_hashtbl(struct bonding *bond)
{
	spin_unlock_bh(&(BOND_ALB_INFO(bond).rx_hashtbl_lock));
}

/* when an ARP REPLY is received from a client update its info
 * in the rx_hashtbl
 */
static void rlb_update_entry_from_arp(struct bonding *bond, struct arp_pkt *arp)
{
	struct alb_bond_info *bond_info = &(BOND_ALB_INFO(bond));
	struct rlb_client_info *client_info;
	u32 hash_index;

	_lock_rx_hashtbl(bond);

	hash_index = _simple_hash((u8*)&(arp->ip_src), sizeof(arp->ip_src));
	client_info = &(bond_info->rx_hashtbl[hash_index]);

	if ((client_info->assigned) &&
	    (client_info->ip_src == arp->ip_dst) &&
	    (client_info->ip_dst == arp->ip_src) &&
	    (compare_ether_addr_64bits(client_info->mac_dst, arp->mac_src))) {
		/* update the clients MAC address */
		memcpy(client_info->mac_dst, arp->mac_src, ETH_ALEN);
		client_info->ntt = 1;
		bond_info->rx_ntt = 1;
	}

	_unlock_rx_hashtbl(bond);
}

static void rlb_arp_recv(struct sk_buff *skb, struct bonding *bond,
			 struct slave *slave)
{
	struct arp_pkt *arp;

	if (skb->protocol != cpu_to_be16(ETH_P_ARP))
		return;

	arp = (struct arp_pkt *) skb->data;
	if (!arp) {
		pr_debug("Packet has no ARP data\n");
		return;
	}

	if (!pskb_may_pull(skb, arp_hdr_len(bond->dev)))
		return;

	if (skb->len < sizeof(struct arp_pkt)) {
		pr_debug("Packet is too small to be an ARP\n");
		return;
	}

	if (arp->op_code == htons(ARPOP_REPLY)) {
		/* update rx hash table for this ARP */
		rlb_update_entry_from_arp(bond, arp);
		pr_debug("Server received an ARP Reply from client\n");
	}
}

/* Caller must hold bond lock for read */
static struct slave *rlb_next_rx_slave(struct bonding *bond)
{
	struct alb_bond_info *bond_info = &(BOND_ALB_INFO(bond));
	struct slave *rx_slave, *slave, *start_at;
	int i = 0;

	if (bond_info->next_rx_slave) {
		start_at = bond_info->next_rx_slave;
	} else {
		start_at = bond->first_slave;
	}

	rx_slave = NULL;

	bond_for_each_slave_from(bond, slave, i, start_at) {
		if (SLAVE_IS_OK(slave)) {
			if (!rx_slave) {
				rx_slave = slave;
			} else if (slave->speed > rx_slave->speed) {
				rx_slave = slave;
			}
		}
	}

	if (rx_slave) {
		bond_info->next_rx_slave = rx_slave->next;
	}

	return rx_slave;
}

/* teach the switch the mac of a disabled slave
 * on the primary for fault tolerance
 *
 * Caller must hold bond->curr_slave_lock for write or bond lock for write
 */
static void rlb_teach_disabled_mac_on_primary(struct bonding *bond, u8 addr[])
{
	if (!bond->curr_active_slave) {
		return;
	}

	if (!bond->alb_info.primary_is_promisc) {
		if (!dev_set_promiscuity(bond->curr_active_slave->dev, 1))
			bond->alb_info.primary_is_promisc = 1;
		else
			bond->alb_info.primary_is_promisc = 0;
	}

	bond->alb_info.rlb_promisc_timeout_counter = 0;

	alb_send_learning_packets(bond->curr_active_slave, addr);
}

/* slave being removed should not be active at this point
 *
 * Caller must hold bond lock for read
 */
static void rlb_clear_slave(struct bonding *bond, struct slave *slave)
{
	struct alb_bond_info *bond_info = &(BOND_ALB_INFO(bond));
	struct rlb_client_info *rx_hash_table;
	u32 index, next_index;

	/* clear slave from rx_hashtbl */
	_lock_rx_hashtbl(bond);

	rx_hash_table = bond_info->rx_hashtbl;
	index = bond_info->rx_hashtbl_head;
	for (; index != RLB_NULL_INDEX; index = next_index) {
		next_index = rx_hash_table[index].next;
		if (rx_hash_table[index].slave == slave) {
			struct slave *assigned_slave = rlb_next_rx_slave(bond);

			if (assigned_slave) {
				rx_hash_table[index].slave = assigned_slave;
				if (compare_ether_addr_64bits(rx_hash_table[index].mac_dst,
							      mac_bcast)) {
					bond_info->rx_hashtbl[index].ntt = 1;
					bond_info->rx_ntt = 1;
					/* A slave has been removed from the
					 * table because it is either disabled
					 * or being released. We must retry the
					 * update to avoid clients from not
					 * being updated & disconnecting when
					 * there is stress
					 */
					bond_info->rlb_update_retry_counter =
						RLB_UPDATE_RETRY;
				}
			} else {  /* there is no active slave */
				rx_hash_table[index].slave = NULL;
			}
		}
	}

	_unlock_rx_hashtbl(bond);

	write_lock_bh(&bond->curr_slave_lock);

	if (slave != bond->curr_active_slave) {
		rlb_teach_disabled_mac_on_primary(bond, slave->dev->dev_addr);
	}

	write_unlock_bh(&bond->curr_slave_lock);
}

static void rlb_update_client(struct rlb_client_info *client_info)
{
	int i;

	if (!client_info->slave) {
		return;
	}

	for (i = 0; i < RLB_ARP_BURST_SIZE; i++) {
		struct sk_buff *skb;

		skb = arp_create(ARPOP_REPLY, ETH_P_ARP,
				 client_info->ip_dst,
				 client_info->slave->dev,
				 client_info->ip_src,
				 client_info->mac_dst,
				 client_info->slave->dev->dev_addr,
				 client_info->mac_dst);
		if (!skb) {
			pr_err("%s: Error: failed to create an ARP packet\n",
			       client_info->slave->dev->master->name);
			continue;
		}

		skb->dev = client_info->slave->dev;

		if (client_info->tag) {
			skb = vlan_put_tag(skb, client_info->vlan_id);
			if (!skb) {
				pr_err("%s: Error: failed to insert VLAN tag\n",
				       client_info->slave->dev->master->name);
				continue;
			}
		}

		arp_xmit(skb);
	}
}

/* sends ARP REPLIES that update the clients that need updating */
static void rlb_update_rx_clients(struct bonding *bond)
{
	struct alb_bond_info *bond_info = &(BOND_ALB_INFO(bond));
	struct rlb_client_info *client_info;
	u32 hash_index;

	_lock_rx_hashtbl(bond);

	hash_index = bond_info->rx_hashtbl_head;
	for (; hash_index != RLB_NULL_INDEX; hash_index = client_info->next) {
		client_info = &(bond_info->rx_hashtbl[hash_index]);
		if (client_info->ntt) {
			rlb_update_client(client_info);
			if (bond_info->rlb_update_retry_counter == 0) {
				client_info->ntt = 0;
			}
		}
	}

	/* do not update the entries again until this counter is zero so that
	 * not to confuse the clients.
	 */
	bond_info->rlb_update_delay_counter = RLB_UPDATE_DELAY;

	_unlock_rx_hashtbl(bond);
}

/* The slave was assigned a new mac address - update the clients */
static void rlb_req_update_slave_clients(struct bonding *bond, struct slave *slave)
{
	struct alb_bond_info *bond_info = &(BOND_ALB_INFO(bond));
	struct rlb_client_info *client_info;
	int ntt = 0;
	u32 hash_index;

	_lock_rx_hashtbl(bond);

	hash_index = bond_info->rx_hashtbl_head;
	for (; hash_index != RLB_NULL_INDEX; hash_index = client_info->next) {
		client_info = &(bond_info->rx_hashtbl[hash_index]);

		if ((client_info->slave == slave) &&
		    compare_ether_addr_64bits(client_info->mac_dst, mac_bcast)) {
			client_info->ntt = 1;
			ntt = 1;
		}
	}

	// update the team's flag only after the whole iteration
	if (ntt) {
		bond_info->rx_ntt = 1;
		//fasten the change
		bond_info->rlb_update_retry_counter = RLB_UPDATE_RETRY;
	}

	_unlock_rx_hashtbl(bond);
}

/* mark all clients using src_ip to be updated */
static void rlb_req_update_subnet_clients(struct bonding *bond, __be32 src_ip)
{
	struct alb_bond_info *bond_info = &(BOND_ALB_INFO(bond));
	struct rlb_client_info *client_info;
	u32 hash_index;

	_lock_rx_hashtbl(bond);

	hash_index = bond_info->rx_hashtbl_head;
	for (; hash_index != RLB_NULL_INDEX; hash_index = client_info->next) {
		client_info = &(bond_info->rx_hashtbl[hash_index]);

		if (!client_info->slave) {
			pr_err("%s: Error: found a client with no channel in the client's hash table\n",
			       bond->dev->name);
			continue;
		}
		/*update all clients using this src_ip, that are not assigned
		 * to the team's address (curr_active_slave) and have a known
		 * unicast mac address.
		 */
		if ((client_info->ip_src == src_ip) &&
		    compare_ether_addr_64bits(client_info->slave->dev->dev_addr,
			   bond->dev->dev_addr) &&
		    compare_ether_addr_64bits(client_info->mac_dst, mac_bcast)) {
			client_info->ntt = 1;
			bond_info->rx_ntt = 1;
		}
	}

	_unlock_rx_hashtbl(bond);
}

/* Caller must hold both bond and ptr locks for read */
static struct slave *rlb_choose_channel(struct sk_buff *skb, struct bonding *bond)
{
	struct alb_bond_info *bond_info = &(BOND_ALB_INFO(bond));
	struct arp_pkt *arp = arp_pkt(skb);
	struct slave *assigned_slave;
	struct rlb_client_info *client_info;
	u32 hash_index = 0;

	_lock_rx_hashtbl(bond);

	hash_index = _simple_hash((u8 *)&arp->ip_dst, sizeof(arp->ip_dst));
	client_info = &(bond_info->rx_hashtbl[hash_index]);

	if (client_info->assigned) {
		if ((client_info->ip_src == arp->ip_src) &&
		    (client_info->ip_dst == arp->ip_dst)) {
			/* the entry is already assigned to this client */
			if (compare_ether_addr_64bits(arp->mac_dst, mac_bcast)) {
				/* update mac address from arp */
				memcpy(client_info->mac_dst, arp->mac_dst, ETH_ALEN);
			}

			assigned_slave = client_info->slave;
			if (assigned_slave) {
				_unlock_rx_hashtbl(bond);
				return assigned_slave;
			}
		} else {
			/* the entry is already assigned to some other client,
			 * move the old client to primary (curr_active_slave) so
			 * that the new client can be assigned to this entry.
			 */
			if (bond->curr_active_slave &&
			    client_info->slave != bond->curr_active_slave) {
				client_info->slave = bond->curr_active_slave;
				rlb_update_client(client_info);
			}
		}
	}
	/* assign a new slave */
	assigned_slave = rlb_next_rx_slave(bond);

	if (assigned_slave) {
		client_info->ip_src = arp->ip_src;
		client_info->ip_dst = arp->ip_dst;
		/* arp->mac_dst is broadcast for arp reqeusts.
		 * will be updated with clients actual unicast mac address
		 * upon receiving an arp reply.
		 */
		memcpy(client_info->mac_dst, arp->mac_dst, ETH_ALEN);
		client_info->slave = assigned_slave;

		if (compare_ether_addr_64bits(client_info->mac_dst, mac_bcast)) {
			client_info->ntt = 1;
			bond->alb_info.rx_ntt = 1;
		} else {
			client_info->ntt = 0;
		}

		if (bond_vlan_used(bond)) {
			if (!vlan_get_tag(skb, &client_info->vlan_id))
				client_info->tag = 1;
		}

		if (!client_info->assigned) {
			u32 prev_tbl_head = bond_info->rx_hashtbl_head;
			bond_info->rx_hashtbl_head = hash_index;
			client_info->next = prev_tbl_head;
			if (prev_tbl_head != RLB_NULL_INDEX) {
				bond_info->rx_hashtbl[prev_tbl_head].prev =
					hash_index;
			}
			client_info->assigned = 1;
		}
	}

	_unlock_rx_hashtbl(bond);

	return assigned_slave;
}

/* chooses (and returns) transmit channel for arp reply
 * does not choose channel for other arp types since they are
 * sent on the curr_active_slave
 */
static struct slave *rlb_arp_xmit(struct sk_buff *skb, struct bonding *bond)
{
	struct arp_pkt *arp = arp_pkt(skb);
	struct slave *tx_slave = NULL;

	if (arp->op_code == htons(ARPOP_REPLY)) {
		/* the arp must be sent on the selected
		* rx channel
		*/
		tx_slave = rlb_choose_channel(skb, bond);
		if (tx_slave) {
			memcpy(arp->mac_src,tx_slave->dev->dev_addr, ETH_ALEN);
		}
		pr_debug("Server sent ARP Reply packet\n");
	} else if (arp->op_code == htons(ARPOP_REQUEST)) {
		/* Create an entry in the rx_hashtbl for this client as a
		 * place holder.
		 * When the arp reply is received the entry will be updated
		 * with the correct unicast address of the client.
		 */
		rlb_choose_channel(skb, bond);

		/* The ARP reply packets must be delayed so that
		 * they can cancel out the influence of the ARP request.
		 */
		bond->alb_info.rlb_update_delay_counter = RLB_UPDATE_DELAY;

		/* arp requests are broadcast and are sent on the primary
		 * the arp request will collapse all clients on the subnet to
		 * the primary slave. We must register these clients to be
		 * updated with their assigned mac.
		 */
		rlb_req_update_subnet_clients(bond, arp->ip_src);
		pr_debug("Server sent ARP Request packet\n");
	}

	return tx_slave;
}

/* Caller must hold bond lock for read */
static void rlb_rebalance(struct bonding *bond)
{
	struct alb_bond_info *bond_info = &(BOND_ALB_INFO(bond));
	struct slave *assigned_slave;
	struct rlb_client_info *client_info;
	int ntt;
	u32 hash_index;

	_lock_rx_hashtbl(bond);

	ntt = 0;
	hash_index = bond_info->rx_hashtbl_head;
	for (; hash_index != RLB_NULL_INDEX; hash_index = client_info->next) {
		client_info = &(bond_info->rx_hashtbl[hash_index]);
		assigned_slave = rlb_next_rx_slave(bond);
		if (assigned_slave && (client_info->slave != assigned_slave)) {
			client_info->slave = assigned_slave;
			client_info->ntt = 1;
			ntt = 1;
		}
	}

	/* update the team's flag only after the whole iteration */
	if (ntt) {
		bond_info->rx_ntt = 1;
	}
	_unlock_rx_hashtbl(bond);
}

/* Caller must hold rx_hashtbl lock */
static void rlb_init_table_entry(struct rlb_client_info *entry)
{
	memset(entry, 0, sizeof(struct rlb_client_info));
	entry->next = RLB_NULL_INDEX;
	entry->prev = RLB_NULL_INDEX;
}

static int rlb_initialize(struct bonding *bond)
{
	struct alb_bond_info *bond_info = &(BOND_ALB_INFO(bond));
	struct rlb_client_info	*new_hashtbl;
	int size = RLB_HASH_TABLE_SIZE * sizeof(struct rlb_client_info);
	int i;

	new_hashtbl = kmalloc(size, GFP_KERNEL);
	if (!new_hashtbl) {
		pr_err("%s: Error: Failed to allocate RLB hash table\n",
		       bond->dev->name);
		return -1;
	}
	_lock_rx_hashtbl(bond);

	bond_info->rx_hashtbl = new_hashtbl;

	bond_info->rx_hashtbl_head = RLB_NULL_INDEX;

	for (i = 0; i < RLB_HASH_TABLE_SIZE; i++) {
		rlb_init_table_entry(bond_info->rx_hashtbl + i);
	}

	_unlock_rx_hashtbl(bond);

	/* register to receive ARPs */
	bond->recv_probe = rlb_arp_recv;

	return 0;
}

static void rlb_deinitialize(struct bonding *bond)
{
	struct alb_bond_info *bond_info = &(BOND_ALB_INFO(bond));

	_lock_rx_hashtbl(bond);

	kfree(bond_info->rx_hashtbl);
	bond_info->rx_hashtbl = NULL;
	bond_info->rx_hashtbl_head = RLB_NULL_INDEX;

	_unlock_rx_hashtbl(bond);
}

static void rlb_clear_vlan(struct bonding *bond, unsigned short vlan_id)
{
	struct alb_bond_info *bond_info = &(BOND_ALB_INFO(bond));
	u32 curr_index;

	_lock_rx_hashtbl(bond);

	curr_index = bond_info->rx_hashtbl_head;
	while (curr_index != RLB_NULL_INDEX) {
		struct rlb_client_info *curr = &(bond_info->rx_hashtbl[curr_index]);
		u32 next_index = bond_info->rx_hashtbl[curr_index].next;
		u32 prev_index = bond_info->rx_hashtbl[curr_index].prev;

		if (curr->tag && (curr->vlan_id == vlan_id)) {
			if (curr_index == bond_info->rx_hashtbl_head) {
				bond_info->rx_hashtbl_head = next_index;
			}
			if (prev_index != RLB_NULL_INDEX) {
				bond_info->rx_hashtbl[prev_index].next = next_index;
			}
			if (next_index != RLB_NULL_INDEX) {
				bond_info->rx_hashtbl[next_index].prev = prev_index;
			}

			rlb_init_table_entry(curr);
		}

		curr_index = next_index;
	}

	_unlock_rx_hashtbl(bond);
}

/*********************** tlb/rlb shared functions *********************/

static void alb_send_learning_packets(struct slave *slave, u8 mac_addr[])
{
	struct bonding *bond = bond_get_bond_by_slave(slave);
	struct learning_pkt pkt;
	int size = sizeof(struct learning_pkt);
	int i;

	memset(&pkt, 0, size);
	memcpy(pkt.mac_dst, mac_addr, ETH_ALEN);
	memcpy(pkt.mac_src, mac_addr, ETH_ALEN);
	pkt.type = cpu_to_be16(ETH_P_LOOP);

	for (i = 0; i < MAX_LP_BURST; i++) {
		struct sk_buff *skb;
		char *data;

		skb = dev_alloc_skb(size);
		if (!skb) {
			return;
		}

		data = skb_put(skb, size);
		memcpy(data, &pkt, size);

		skb_reset_mac_header(skb);
		skb->network_header = skb->mac_header + ETH_HLEN;
		skb->protocol = pkt.type;
		skb->priority = TC_PRIO_CONTROL;
		skb->dev = slave->dev;

		if (bond_vlan_used(bond)) {
			struct vlan_entry *vlan;

			vlan = bond_next_vlan(bond,
					      bond->alb_info.current_alb_vlan);

			bond->alb_info.current_alb_vlan = vlan;
			if (!vlan) {
				kfree_skb(skb);
				continue;
			}

			skb = vlan_put_tag(skb, vlan->vlan_id);
			if (!skb) {
				pr_err("%s: Error: failed to insert VLAN tag\n",
				       bond->dev->name);
				continue;
			}
		}

		dev_queue_xmit(skb);
	}
}

/* hw is a boolean parameter that determines whether we should try and
 * set the hw address of the device as well as the hw address of the
 * net_device
 */
static int alb_set_slave_mac_addr(struct slave *slave, u8 addr[], int hw)
{
	struct net_device *dev = slave->dev;
	struct sockaddr s_addr;

	if (!hw) {
		memcpy(dev->dev_addr, addr, dev->addr_len);
		return 0;
	}

	/* for rlb each slave must have a unique hw mac addresses so that */
	/* each slave will receive packets destined to a different mac */
	memcpy(s_addr.sa_data, addr, dev->addr_len);
	s_addr.sa_family = dev->type;
	if (dev_set_mac_address(dev, &s_addr)) {
		pr_err("%s: Error: dev_set_mac_address of dev %s failed!\n"
		       "ALB mode requires that the base driver support setting the hw address also when the network device's interface is open\n",
		       dev->master->name, dev->name);
		return -EOPNOTSUPP;
	}
	return 0;
}

/*
 * Swap MAC addresses between two slaves.
 *
 * Called with RTNL held, and no other locks.
 *
 */

static void alb_swap_mac_addr(struct bonding *bond, struct slave *slave1, struct slave *slave2)
{
	u8 tmp_mac_addr[ETH_ALEN];

	memcpy(tmp_mac_addr, slave1->dev->dev_addr, ETH_ALEN);
	alb_set_slave_mac_addr(slave1, slave2->dev->dev_addr, bond->alb_info.rlb_enabled);
	alb_set_slave_mac_addr(slave2, tmp_mac_addr, bond->alb_info.rlb_enabled);

}

/*
 * Send learning packets after MAC address swap.
 *
 * Called with RTNL and no other locks
 */
static void alb_fasten_mac_swap(struct bonding *bond, struct slave *slave1,
				struct slave *slave2)
{
	int slaves_state_differ = (SLAVE_IS_OK(slave1) != SLAVE_IS_OK(slave2));
	struct slave *disabled_slave = NULL;

	ASSERT_RTNL();

	/* fasten the change in the switch */
	if (SLAVE_IS_OK(slave1)) {
		alb_send_learning_packets(slave1, slave1->dev->dev_addr);
		if (bond->alb_info.rlb_enabled) {
			/* inform the clients that the mac address
			 * has changed
			 */
			rlb_req_update_slave_clients(bond, slave1);
		}
	} else {
		disabled_slave = slave1;
	}

	if (SLAVE_IS_OK(slave2)) {
		alb_send_learning_packets(slave2, slave2->dev->dev_addr);
		if (bond->alb_info.rlb_enabled) {
			/* inform the clients that the mac address
			 * has changed
			 */
			rlb_req_update_slave_clients(bond, slave2);
		}
	} else {
		disabled_slave = slave2;
	}

	if (bond->alb_info.rlb_enabled && slaves_state_differ) {
		/* A disabled slave was assigned an active mac addr */
		rlb_teach_disabled_mac_on_primary(bond,
						  disabled_slave->dev->dev_addr);
	}
}

/**
 * alb_change_hw_addr_on_detach
 * @bond: bonding we're working on
 * @slave: the slave that was just detached
 *
 * We assume that @slave was already detached from the slave list.
 *
 * If @slave's permanent hw address is different both from its current
 * address and from @bond's address, then somewhere in the bond there's
 * a slave that has @slave's permanet address as its current address.
 * We'll make sure that that slave no longer uses @slave's permanent address.
 *
 * Caller must hold RTNL and no other locks
 */
static void alb_change_hw_addr_on_detach(struct bonding *bond, struct slave *slave)
{
	int perm_curr_diff;
	int perm_bond_diff;

	perm_curr_diff = compare_ether_addr_64bits(slave->perm_hwaddr,
						   slave->dev->dev_addr);
	perm_bond_diff = compare_ether_addr_64bits(slave->perm_hwaddr,
						   bond->dev->dev_addr);

	if (perm_curr_diff && perm_bond_diff) {
		struct slave *tmp_slave;
		int i, found = 0;

		bond_for_each_slave(bond, tmp_slave, i) {
			if (!compare_ether_addr_64bits(slave->perm_hwaddr,
						       tmp_slave->dev->dev_addr)) {
				found = 1;
				break;
			}
		}

		if (found) {
			/* locking: needs RTNL and nothing else */
			alb_swap_mac_addr(bond, slave, tmp_slave);
			alb_fasten_mac_swap(bond, slave, tmp_slave);
		}
	}
}

/**
 * alb_handle_addr_collision_on_attach
 * @bond: bonding we're working on
 * @slave: the slave that was just attached
 *
 * checks uniqueness of slave's mac address and handles the case the
 * new slave uses the bonds mac address.
 *
 * If the permanent hw address of @slave is @bond's hw address, we need to
 * find a different hw address to give @slave, that isn't in use by any other
 * slave in the bond. This address must be, of course, one of the permanent
 * addresses of the other slaves.
 *
 * We go over the slave list, and for each slave there we compare its
 * permanent hw address with the current address of all the other slaves.
 * If no match was found, then we've found a slave with a permanent address
 * that isn't used by any other slave in the bond, so we can assign it to
 * @slave.
 *
 * assumption: this function is called before @slave is attached to the
 * 	       bond slave list.
 *
 * caller must hold the bond lock for write since the mac addresses are compared
 * and may be swapped.
 */
static int alb_handle_addr_collision_on_attach(struct bonding *bond, struct slave *slave)
{
	struct slave *tmp_slave1, *tmp_slave2, *free_mac_slave;
	struct slave *has_bond_addr = bond->curr_active_slave;
	int i, j, found = 0;

	if (bond->slave_cnt == 0) {
		/* this is the first slave */
		return 0;
	}

	/* if slave's mac address differs from bond's mac address
	 * check uniqueness of slave's mac address against the other
	 * slaves in the bond.
	 */
	if (compare_ether_addr_64bits(slave->perm_hwaddr, bond->dev->dev_addr)) {
		bond_for_each_slave(bond, tmp_slave1, i) {
			if (!compare_ether_addr_64bits(tmp_slave1->dev->dev_addr,
						       slave->dev->dev_addr)) {
				found = 1;
				break;
			}
		}

		if (!found)
			return 0;

		/* Try setting slave mac to bond address and fall-through
		   to code handling that situation below... */
		alb_set_slave_mac_addr(slave, bond->dev->dev_addr,
				       bond->alb_info.rlb_enabled);
	}

	/* The slave's address is equal to the address of the bond.
	 * Search for a spare address in the bond for this slave.
	 */
	free_mac_slave = NULL;

	bond_for_each_slave(bond, tmp_slave1, i) {
		found = 0;
		bond_for_each_slave(bond, tmp_slave2, j) {
			if (!compare_ether_addr_64bits(tmp_slave1->perm_hwaddr,
						       tmp_slave2->dev->dev_addr)) {
				found = 1;
				break;
			}
		}

		if (!found) {
			/* no slave has tmp_slave1's perm addr
			 * as its curr addr
			 */
			free_mac_slave = tmp_slave1;
			break;
		}

		if (!has_bond_addr) {
			if (!compare_ether_addr_64bits(tmp_slave1->dev->dev_addr,
						       bond->dev->dev_addr)) {

				has_bond_addr = tmp_slave1;
			}
		}
	}

	if (free_mac_slave) {
		alb_set_slave_mac_addr(slave, free_mac_slave->perm_hwaddr,
				       bond->alb_info.rlb_enabled);

		pr_warning("%s: Warning: the hw address of slave %s is in use by the bond; giving it the hw address of %s\n",
			   bond->dev->name, slave->dev->name,
			   free_mac_slave->dev->name);

	} else if (has_bond_addr) {
		pr_err("%s: Error: the hw address of slave %s is in use by the bond; couldn't find a slave with a free hw address to give it (this should not have happened)\n",
		       bond->dev->name, slave->dev->name);
		return -EFAULT;
	}

	return 0;
}

/**
 * alb_set_mac_address
 * @bond:
 * @addr:
 *
 * In TLB mode all slaves are configured to the bond's hw address, but set
 * their dev_addr field to different addresses (based on their permanent hw
 * addresses).
 *
 * For each slave, this function sets the interface to the new address and then
 * changes its dev_addr field to its previous value.
 *
 * Unwinding assumes bond's mac address has not yet changed.
 */
static int alb_set_mac_address(struct bonding *bond, void *addr)
{
	struct sockaddr sa;
	struct slave *slave, *stop_at;
	char tmp_addr[ETH_ALEN];
	int res;
	int i;

	if (bond->alb_info.rlb_enabled) {
		return 0;
	}

	bond_for_each_slave(bond, slave, i) {
		/* save net_device's current hw address */
		memcpy(tmp_addr, slave->dev->dev_addr, ETH_ALEN);

		res = dev_set_mac_address(slave->dev, addr);

		/* restore net_device's hw address */
		memcpy(slave->dev->dev_addr, tmp_addr, ETH_ALEN);

		if (res)
			goto unwind;
	}

	return 0;

unwind:
	memcpy(sa.sa_data, bond->dev->dev_addr, bond->dev->addr_len);
	sa.sa_family = bond->dev->type;

	/* unwind from head to the slave that failed */
	stop_at = slave;
	bond_for_each_slave_from_to(bond, slave, i, bond->first_slave, stop_at) {
		memcpy(tmp_addr, slave->dev->dev_addr, ETH_ALEN);
		dev_set_mac_address(slave->dev, &sa);
		memcpy(slave->dev->dev_addr, tmp_addr, ETH_ALEN);
	}

	return res;
}

/************************ exported alb funcions ************************/

int bond_alb_initialize(struct bonding *bond, int rlb_enabled)
{
	int res;

	res = tlb_initialize(bond);
	if (res) {
		return res;
	}

	if (rlb_enabled) {
		bond->alb_info.rlb_enabled = 1;
		/* initialize rlb */
		res = rlb_initialize(bond);
		if (res) {
			tlb_deinitialize(bond);
			return res;
		}
	} else {
		bond->alb_info.rlb_enabled = 0;
	}

	return 0;
}

void bond_alb_deinitialize(struct bonding *bond)
{
	struct alb_bond_info *bond_info = &(BOND_ALB_INFO(bond));

	tlb_deinitialize(bond);

	if (bond_info->rlb_enabled) {
		rlb_deinitialize(bond);
	}
}

int bond_alb_xmit(struct sk_buff *skb, struct net_device *bond_dev)
{
	struct bonding *bond = netdev_priv(bond_dev);
	struct ethhdr *eth_data;
	struct alb_bond_info *bond_info = &(BOND_ALB_INFO(bond));
	struct slave *tx_slave = NULL;
	static const __be32 ip_bcast = htonl(0xffffffff);
	int hash_size = 0;
	int do_tx_balance = 1;
	u32 hash_index = 0;
	const u8 *hash_start = NULL;
	int res = 1;
	struct ipv6hdr *ip6hdr;

	skb_reset_mac_header(skb);
	eth_data = eth_hdr(skb);

	/* make sure that the curr_active_slave do not change during tx
	 */
	read_lock(&bond->curr_slave_lock);

	switch (ntohs(skb->protocol)) {
	case ETH_P_IP: {
		const struct iphdr *iph = ip_hdr(skb);

		if (!compare_ether_addr_64bits(eth_data->h_dest, mac_bcast) ||
		    (iph->daddr == ip_bcast) ||
		    (iph->protocol == IPPROTO_IGMP)) {
			do_tx_balance = 0;
			break;
		}
		hash_start = (char *)&(iph->daddr);
		hash_size = sizeof(iph->daddr);
	}
		break;
	case ETH_P_IPV6:
		/* IPv6 doesn't really use broadcast mac address, but leave
		 * that here just in case.
		 */
		if (!compare_ether_addr_64bits(eth_data->h_dest, mac_bcast)) {
			do_tx_balance = 0;
			break;
		}

		/* IPv6 uses all-nodes multicast as an equivalent to
		 * broadcasts in IPv4.
		 */
		if (!compare_ether_addr_64bits(eth_data->h_dest, mac_v6_allmcast)) {
			do_tx_balance = 0;
			break;
		}

		/* Additianally, DAD probes should not be tx-balanced as that
		 * will lead to false positives for duplicate addresses and
		 * prevent address configuration from working.
		 */
		ip6hdr = ipv6_hdr(skb);
		if (ipv6_addr_any(&ip6hdr->saddr)) {
			do_tx_balance = 0;
			break;
		}

		hash_start = (char *)&(ipv6_hdr(skb)->daddr);
		hash_size = sizeof(ipv6_hdr(skb)->daddr);
		break;
	case ETH_P_IPX:
		if (ipx_hdr(skb)->ipx_checksum != IPX_NO_CHECKSUM) {
			/* something is wrong with this packet */
			do_tx_balance = 0;
			break;
		}

		if (ipx_hdr(skb)->ipx_type != IPX_TYPE_NCP) {
			/* The only protocol worth balancing in
			 * this family since it has an "ARP" like
			 * mechanism
			 */
			do_tx_balance = 0;
			break;
		}

		hash_start = (char*)eth_data->h_dest;
		hash_size = ETH_ALEN;
		break;
	case ETH_P_ARP:
		do_tx_balance = 0;
		if (bond_info->rlb_enabled) {
			tx_slave = rlb_arp_xmit(skb, bond);
		}
		break;
	default:
		do_tx_balance = 0;
		break;
	}

	if (do_tx_balance) {
		hash_index = _simple_hash(hash_start, hash_size);
		tx_slave = tlb_choose_channel(bond, hash_index, skb->len);
	}

	if (!tx_slave) {
		/* unbalanced or unassigned, send through primary */
		tx_slave = bond->curr_active_slave;
		bond_info->unbalanced_load += skb->len;
	}

	if (tx_slave && SLAVE_IS_OK(tx_slave)) {
		if (tx_slave != bond->curr_active_slave) {
			memcpy(eth_data->h_source,
			       tx_slave->dev->dev_addr,
			       ETH_ALEN);
		}

		res = bond_dev_queue_xmit(bond, skb, tx_slave->dev);
	} else {
		if (tx_slave) {
			tlb_clear_slave(bond, tx_slave, 0);
		}
	}

	if (res) {
		/* no suitable interface, frame not sent */
		dev_kfree_skb(skb);
	}
	read_unlock(&bond->curr_slave_lock);

	return NETDEV_TX_OK;
}

void bond_alb_monitor(struct work_struct *work)
{
	struct bonding *bond = container_of(work, struct bonding,
					    alb_work.work);
	struct alb_bond_info *bond_info = &(BOND_ALB_INFO(bond));
	struct slave *slave;
	int i;

	read_lock(&bond->lock);

	if (bond->slave_cnt == 0) {
		bond_info->tx_rebalance_counter = 0;
		bond_info->lp_counter = 0;
		goto re_arm;
	}

	bond_info->tx_rebalance_counter++;
	bond_info->lp_counter++;

	/* send learning packets */
	if (bond_info->lp_counter >= BOND_ALB_LP_TICKS) {
		/* change of curr_active_slave involves swapping of mac addresses.
		 * in order to avoid this swapping from happening while
		 * sending the learning packets, the curr_slave_lock must be held for
		 * read.
		 */
		read_lock(&bond->curr_slave_lock);

		bond_for_each_slave(bond, slave, i) {
			alb_send_learning_packets(slave, slave->dev->dev_addr);
		}

		read_unlock(&bond->curr_slave_lock);

		bond_info->lp_counter = 0;
	}

	/* rebalance tx traffic */
	if (bond_info->tx_rebalance_counter >= BOND_TLB_REBALANCE_TICKS) {

		read_lock(&bond->curr_slave_lock);

		bond_for_each_slave(bond, slave, i) {
			tlb_clear_slave(bond, slave, 1);
			if (slave == bond->curr_active_slave) {
				SLAVE_TLB_INFO(slave).load =
					bond_info->unbalanced_load /
						BOND_TLB_REBALANCE_INTERVAL;
				bond_info->unbalanced_load = 0;
			}
		}

		read_unlock(&bond->curr_slave_lock);

		bond_info->tx_rebalance_counter = 0;
	}

	/* handle rlb stuff */
	if (bond_info->rlb_enabled) {
		if (bond_info->primary_is_promisc &&
		    (++bond_info->rlb_promisc_timeout_counter >= RLB_PROMISC_TIMEOUT)) {

			/*
			 * dev_set_promiscuity requires rtnl and
			 * nothing else.  Avoid race with bond_close.
			 */
			read_unlock(&bond->lock);
			if (!rtnl_trylock()) {
				read_lock(&bond->lock);
				goto re_arm;
			}

			bond_info->rlb_promisc_timeout_counter = 0;

			/* If the primary was set to promiscuous mode
			 * because a slave was disabled then
			 * it can now leave promiscuous mode.
			 */
			dev_set_promiscuity(bond->curr_active_slave->dev, -1);
			bond_info->primary_is_promisc = 0;

			rtnl_unlock();
			read_lock(&bond->lock);
		}

		if (bond_info->rlb_rebalance) {
			bond_info->rlb_rebalance = 0;
			rlb_rebalance(bond);
		}

		/* check if clients need updating */
		if (bond_info->rx_ntt) {
			if (bond_info->rlb_update_delay_counter) {
				--bond_info->rlb_update_delay_counter;
			} else {
				rlb_update_rx_clients(bond);
				if (bond_info->rlb_update_retry_counter) {
					--bond_info->rlb_update_retry_counter;
				} else {
					bond_info->rx_ntt = 0;
				}
			}
		}
	}

re_arm:
	queue_delayed_work(bond->wq, &bond->alb_work, alb_delta_in_ticks);

	read_unlock(&bond->lock);
}

/* assumption: called before the slave is attached to the bond
 * and not locked by the bond lock
 */
int bond_alb_init_slave(struct bonding *bond, struct slave *slave)
{
	int res;

	res = alb_set_slave_mac_addr(slave, slave->perm_hwaddr,
				     bond->alb_info.rlb_enabled);
	if (res) {
		return res;
	}

	/* caller must hold the bond lock for write since the mac addresses
	 * are compared and may be swapped.
	 */
	read_lock(&bond->lock);

	res = alb_handle_addr_collision_on_attach(bond, slave);

	read_unlock(&bond->lock);

	if (res) {
		return res;
	}

	tlb_init_slave(slave);

	/* order a rebalance ASAP */
	bond->alb_info.tx_rebalance_counter = BOND_TLB_REBALANCE_TICKS;

	if (bond->alb_info.rlb_enabled) {
		bond->alb_info.rlb_rebalance = 1;
	}

	return 0;
}

/*
 * Remove slave from tlb and rlb hash tables, and fix up MAC addresses
 * if necessary.
 *
 * Caller must hold RTNL and no other locks
 */
void bond_alb_deinit_slave(struct bonding *bond, struct slave *slave)
{
	if (bond->slave_cnt > 1) {
		alb_change_hw_addr_on_detach(bond, slave);
	}

	tlb_clear_slave(bond, slave, 0);

	if (bond->alb_info.rlb_enabled) {
		bond->alb_info.next_rx_slave = NULL;
		rlb_clear_slave(bond, slave);
	}
}

/* Caller must hold bond lock for read */
void bond_alb_handle_link_change(struct bonding *bond, struct slave *slave, char link)
{
	struct alb_bond_info *bond_info = &(BOND_ALB_INFO(bond));

	if (link == BOND_LINK_DOWN) {
		tlb_clear_slave(bond, slave, 0);
		if (bond->alb_info.rlb_enabled) {
			rlb_clear_slave(bond, slave);
		}
	} else if (link == BOND_LINK_UP) {
		/* order a rebalance ASAP */
		bond_info->tx_rebalance_counter = BOND_TLB_REBALANCE_TICKS;
		if (bond->alb_info.rlb_enabled) {
			bond->alb_info.rlb_rebalance = 1;
			/* If the updelay module parameter is smaller than the
			 * forwarding delay of the switch the rebalance will
			 * not work because the rebalance arp replies will
			 * not be forwarded to the clients..
			 */
		}
	}
}

/**
 * bond_alb_handle_active_change - assign new curr_active_slave
 * @bond: our bonding struct
 * @new_slave: new slave to assign
 *
 * Set the bond->curr_active_slave to @new_slave and handle
 * mac address swapping and promiscuity changes as needed.
 *
 * If new_slave is NULL, caller must hold curr_slave_lock or
 * bond->lock for write.
 *
 * If new_slave is not NULL, caller must hold RTNL, bond->lock for
 * read and curr_slave_lock for write.  Processing here may sleep, so
 * no other locks may be held.
 */
void bond_alb_handle_active_change(struct bonding *bond, struct slave *new_slave)
	__releases(&bond->curr_slave_lock)
	__releases(&bond->lock)
	__acquires(&bond->lock)
	__acquires(&bond->curr_slave_lock)
{
	struct slave *swap_slave;
	int i;

	if (bond->curr_active_slave == new_slave) {
		return;
	}

	if (bond->curr_active_slave && bond->alb_info.primary_is_promisc) {
		dev_set_promiscuity(bond->curr_active_slave->dev, -1);
		bond->alb_info.primary_is_promisc = 0;
		bond->alb_info.rlb_promisc_timeout_counter = 0;
	}

	swap_slave = bond->curr_active_slave;
	bond->curr_active_slave = new_slave;

	if (!new_slave || (bond->slave_cnt == 0)) {
		return;
	}

	/* set the new curr_active_slave to the bonds mac address
	 * i.e. swap mac addresses of old curr_active_slave and new curr_active_slave
	 */
	if (!swap_slave) {
		struct slave *tmp_slave;
		/* find slave that is holding the bond's mac address */
		bond_for_each_slave(bond, tmp_slave, i) {
			if (!compare_ether_addr_64bits(tmp_slave->dev->dev_addr,
						       bond->dev->dev_addr)) {
				swap_slave = tmp_slave;
				break;
			}
		}
	}

	/*
	 * Arrange for swap_slave and new_slave to temporarily be
	 * ignored so we can mess with their MAC addresses without
	 * fear of interference from transmit activity.
	 */
	if (swap_slave) {
		tlb_clear_slave(bond, swap_slave, 1);
	}
	tlb_clear_slave(bond, new_slave, 1);

	write_unlock_bh(&bond->curr_slave_lock);
	read_unlock(&bond->lock);

	ASSERT_RTNL();

	/* curr_active_slave must be set before calling alb_swap_mac_addr */
	if (swap_slave) {
		/* swap mac address */
		alb_swap_mac_addr(bond, swap_slave, new_slave);
	} else {
		/* set the new_slave to the bond mac address */
		alb_set_slave_mac_addr(new_slave, bond->dev->dev_addr,
				       bond->alb_info.rlb_enabled);
	}

	if (swap_slave) {
		alb_fasten_mac_swap(bond, swap_slave, new_slave);
		read_lock(&bond->lock);
	} else {
		read_lock(&bond->lock);
		alb_send_learning_packets(new_slave, bond->dev->dev_addr);
	}

	write_lock_bh(&bond->curr_slave_lock);
}

/*
 * Called with RTNL
 */
int bond_alb_set_mac_address(struct net_device *bond_dev, void *addr)
	__acquires(&bond->lock)
	__releases(&bond->lock)
{
	struct bonding *bond = netdev_priv(bond_dev);
	struct sockaddr *sa = addr;
	struct slave *slave, *swap_slave;
	int res;
	int i;

	if (!is_valid_ether_addr(sa->sa_data)) {
		return -EADDRNOTAVAIL;
	}

	res = alb_set_mac_address(bond, addr);
	if (res) {
		return res;
	}

	memcpy(bond_dev->dev_addr, sa->sa_data, bond_dev->addr_len);

	/* If there is no curr_active_slave there is nothing else to do.
	 * Otherwise we'll need to pass the new address to it and handle
	 * duplications.
	 */
	if (!bond->curr_active_slave) {
		return 0;
	}

	swap_slave = NULL;

	bond_for_each_slave(bond, slave, i) {
		if (!compare_ether_addr_64bits(slave->dev->dev_addr,
					       bond_dev->dev_addr)) {
			swap_slave = slave;
			break;
		}
	}

	if (swap_slave) {
		alb_swap_mac_addr(bond, swap_slave, bond->curr_active_slave);
		alb_fasten_mac_swap(bond, swap_slave, bond->curr_active_slave);
	} else {
		alb_set_slave_mac_addr(bond->curr_active_slave, bond_dev->dev_addr,
				       bond->alb_info.rlb_enabled);

		read_lock(&bond->lock);
		alb_send_learning_packets(bond->curr_active_slave, bond_dev->dev_addr);
		if (bond->alb_info.rlb_enabled) {
			/* inform clients mac address has changed */
			rlb_req_update_slave_clients(bond, bond->curr_active_slave);
		}
		read_unlock(&bond->lock);
	}

	return 0;
}

void bond_alb_clear_vlan(struct bonding *bond, unsigned short vlan_id)
{
	if (bond->alb_info.current_alb_vlan &&
	    (bond->alb_info.current_alb_vlan->vlan_id == vlan_id)) {
		bond->alb_info.current_alb_vlan = NULL;
	}

	if (bond->alb_info.rlb_enabled) {
		rlb_clear_vlan(bond, vlan_id);
	}
}

