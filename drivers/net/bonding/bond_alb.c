// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright(c) 1999 - 2004 Intel Corporation. All rights reserved.
 */

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
#include <net/bonding.h>
#include <net/bond_alb.h>

static const u8 mac_v6_allmcast[ETH_ALEN + 2] __long_aligned = {
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
static void alb_send_learning_packets(struct slave *slave, u8 mac_addr[],
				      bool strict_match);
static void rlb_purge_src_ip(struct bonding *bond, struct arp_pkt *arp);
static void rlb_src_unlink(struct bonding *bond, u32 index);
static void rlb_src_link(struct bonding *bond, u32 ip_src_hash,
			 u32 ip_dst_hash);

static inline u8 _simple_hash(const u8 *hash_start, int hash_size)
{
	int i;
	u8 hash = 0;

	for (i = 0; i < hash_size; i++)
		hash ^= hash_start[i];

	return hash;
}

/*********************** tlb specific functions ***************************/

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

static void __tlb_clear_slave(struct bonding *bond, struct slave *slave,
			 int save_load)
{
	struct tlb_client_info *tx_hash_table;
	u32 index;

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
}

static void tlb_clear_slave(struct bonding *bond, struct slave *slave,
			 int save_load)
{
	spin_lock_bh(&bond->mode_lock);
	__tlb_clear_slave(bond, slave, save_load);
	spin_unlock_bh(&bond->mode_lock);
}

/* Must be called before starting the monitor timer */
static int tlb_initialize(struct bonding *bond)
{
	struct alb_bond_info *bond_info = &(BOND_ALB_INFO(bond));
	int size = TLB_HASH_TABLE_SIZE * sizeof(struct tlb_client_info);
	struct tlb_client_info *new_hashtbl;
	int i;

	new_hashtbl = kzalloc(size, GFP_KERNEL);
	if (!new_hashtbl)
		return -ENOMEM;

	spin_lock_bh(&bond->mode_lock);

	bond_info->tx_hashtbl = new_hashtbl;

	for (i = 0; i < TLB_HASH_TABLE_SIZE; i++)
		tlb_init_table_entry(&bond_info->tx_hashtbl[i], 0);

	spin_unlock_bh(&bond->mode_lock);

	return 0;
}

/* Must be called only after all slaves have been released */
static void tlb_deinitialize(struct bonding *bond)
{
	struct alb_bond_info *bond_info = &(BOND_ALB_INFO(bond));

	spin_lock_bh(&bond->mode_lock);

	kfree(bond_info->tx_hashtbl);
	bond_info->tx_hashtbl = NULL;

	spin_unlock_bh(&bond->mode_lock);
}

static long long compute_gap(struct slave *slave)
{
	return (s64) (slave->speed << 20) - /* Convert to Megabit per sec */
	       (s64) (SLAVE_TLB_INFO(slave).load << 3); /* Bytes to bits */
}

static struct slave *tlb_get_least_loaded_slave(struct bonding *bond)
{
	struct slave *slave, *least_loaded;
	struct list_head *iter;
	long long max_gap;

	least_loaded = NULL;
	max_gap = LLONG_MIN;

	/* Find the slave with the largest gap */
	bond_for_each_slave_rcu(bond, slave, iter) {
		if (bond_slave_can_tx(slave)) {
			long long gap = compute_gap(slave);

			if (max_gap < gap) {
				least_loaded = slave;
				max_gap = gap;
			}
		}
	}

	return least_loaded;
}

static struct slave *__tlb_choose_channel(struct bonding *bond, u32 hash_index,
						u32 skb_len)
{
	struct alb_bond_info *bond_info = &(BOND_ALB_INFO(bond));
	struct tlb_client_info *hash_table;
	struct slave *assigned_slave;

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

			if (next_index != TLB_NULL_INDEX)
				hash_table[next_index].prev = hash_index;

			slave_info->head = hash_index;
			slave_info->load +=
				hash_table[hash_index].load_history;
		}
	}

	if (assigned_slave)
		hash_table[hash_index].tx_bytes += skb_len;

	return assigned_slave;
}

static struct slave *tlb_choose_channel(struct bonding *bond, u32 hash_index,
					u32 skb_len)
{
	struct slave *tx_slave;

	/* We don't need to disable softirq here, becase
	 * tlb_choose_channel() is only called by bond_alb_xmit()
	 * which already has softirq disabled.
	 */
	spin_lock(&bond->mode_lock);
	tx_slave = __tlb_choose_channel(bond, hash_index, skb_len);
	spin_unlock(&bond->mode_lock);

	return tx_slave;
}

/*********************** rlb specific functions ***************************/

/* when an ARP REPLY is received from a client update its info
 * in the rx_hashtbl
 */
static void rlb_update_entry_from_arp(struct bonding *bond, struct arp_pkt *arp)
{
	struct alb_bond_info *bond_info = &(BOND_ALB_INFO(bond));
	struct rlb_client_info *client_info;
	u32 hash_index;

	spin_lock_bh(&bond->mode_lock);

	hash_index = _simple_hash((u8 *)&(arp->ip_src), sizeof(arp->ip_src));
	client_info = &(bond_info->rx_hashtbl[hash_index]);

	if ((client_info->assigned) &&
	    (client_info->ip_src == arp->ip_dst) &&
	    (client_info->ip_dst == arp->ip_src) &&
	    (!ether_addr_equal_64bits(client_info->mac_dst, arp->mac_src))) {
		/* update the clients MAC address */
		ether_addr_copy(client_info->mac_dst, arp->mac_src);
		client_info->ntt = 1;
		bond_info->rx_ntt = 1;
	}

	spin_unlock_bh(&bond->mode_lock);
}

static int rlb_arp_recv(const struct sk_buff *skb, struct bonding *bond,
			struct slave *slave)
{
	struct arp_pkt *arp, _arp;

	if (skb->protocol != cpu_to_be16(ETH_P_ARP))
		goto out;

	arp = skb_header_pointer(skb, 0, sizeof(_arp), &_arp);
	if (!arp)
		goto out;

	/* We received an ARP from arp->ip_src.
	 * We might have used this IP address previously (on the bonding host
	 * itself or on a system that is bridged together with the bond).
	 * However, if arp->mac_src is different than what is stored in
	 * rx_hashtbl, some other host is now using the IP and we must prevent
	 * sending out client updates with this IP address and the old MAC
	 * address.
	 * Clean up all hash table entries that have this address as ip_src but
	 * have a different mac_src.
	 */
	rlb_purge_src_ip(bond, arp);

	if (arp->op_code == htons(ARPOP_REPLY)) {
		/* update rx hash table for this ARP */
		rlb_update_entry_from_arp(bond, arp);
		slave_dbg(bond->dev, slave->dev, "Server received an ARP Reply from client\n");
	}
out:
	return RX_HANDLER_ANOTHER;
}

/* Caller must hold rcu_read_lock() */
static struct slave *__rlb_next_rx_slave(struct bonding *bond)
{
	struct alb_bond_info *bond_info = &(BOND_ALB_INFO(bond));
	struct slave *before = NULL, *rx_slave = NULL, *slave;
	struct list_head *iter;
	bool found = false;

	bond_for_each_slave_rcu(bond, slave, iter) {
		if (!bond_slave_can_tx(slave))
			continue;
		if (!found) {
			if (!before || before->speed < slave->speed)
				before = slave;
		} else {
			if (!rx_slave || rx_slave->speed < slave->speed)
				rx_slave = slave;
		}
		if (slave == bond_info->rx_slave)
			found = true;
	}
	/* we didn't find anything after the current or we have something
	 * better before and up to the current slave
	 */
	if (!rx_slave || (before && rx_slave->speed < before->speed))
		rx_slave = before;

	if (rx_slave)
		bond_info->rx_slave = rx_slave;

	return rx_slave;
}

/* Caller must hold RTNL, rcu_read_lock is obtained only to silence checkers */
static struct slave *rlb_next_rx_slave(struct bonding *bond)
{
	struct slave *rx_slave;

	ASSERT_RTNL();

	rcu_read_lock();
	rx_slave = __rlb_next_rx_slave(bond);
	rcu_read_unlock();

	return rx_slave;
}

/* teach the switch the mac of a disabled slave
 * on the primary for fault tolerance
 *
 * Caller must hold RTNL
 */
static void rlb_teach_disabled_mac_on_primary(struct bonding *bond, u8 addr[])
{
	struct slave *curr_active = rtnl_dereference(bond->curr_active_slave);

	if (!curr_active)
		return;

	if (!bond->alb_info.primary_is_promisc) {
		if (!dev_set_promiscuity(curr_active->dev, 1))
			bond->alb_info.primary_is_promisc = 1;
		else
			bond->alb_info.primary_is_promisc = 0;
	}

	bond->alb_info.rlb_promisc_timeout_counter = 0;

	alb_send_learning_packets(curr_active, addr, true);
}

/* slave being removed should not be active at this point
 *
 * Caller must hold rtnl.
 */
static void rlb_clear_slave(struct bonding *bond, struct slave *slave)
{
	struct alb_bond_info *bond_info = &(BOND_ALB_INFO(bond));
	struct rlb_client_info *rx_hash_table;
	u32 index, next_index;

	/* clear slave from rx_hashtbl */
	spin_lock_bh(&bond->mode_lock);

	rx_hash_table = bond_info->rx_hashtbl;
	index = bond_info->rx_hashtbl_used_head;
	for (; index != RLB_NULL_INDEX; index = next_index) {
		next_index = rx_hash_table[index].used_next;
		if (rx_hash_table[index].slave == slave) {
			struct slave *assigned_slave = rlb_next_rx_slave(bond);

			if (assigned_slave) {
				rx_hash_table[index].slave = assigned_slave;
				if (is_valid_ether_addr(rx_hash_table[index].mac_dst)) {
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

	spin_unlock_bh(&bond->mode_lock);

	if (slave != rtnl_dereference(bond->curr_active_slave))
		rlb_teach_disabled_mac_on_primary(bond, slave->dev->dev_addr);
}

static void rlb_update_client(struct rlb_client_info *client_info)
{
	int i;

	if (!client_info->slave || !is_valid_ether_addr(client_info->mac_dst))
		return;

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
			slave_err(client_info->slave->bond->dev,
				  client_info->slave->dev,
				  "failed to create an ARP packet\n");
			continue;
		}

		skb->dev = client_info->slave->dev;

		if (client_info->vlan_id) {
			__vlan_hwaccel_put_tag(skb, htons(ETH_P_8021Q),
					       client_info->vlan_id);
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

	spin_lock_bh(&bond->mode_lock);

	hash_index = bond_info->rx_hashtbl_used_head;
	for (; hash_index != RLB_NULL_INDEX;
	     hash_index = client_info->used_next) {
		client_info = &(bond_info->rx_hashtbl[hash_index]);
		if (client_info->ntt) {
			rlb_update_client(client_info);
			if (bond_info->rlb_update_retry_counter == 0)
				client_info->ntt = 0;
		}
	}

	/* do not update the entries again until this counter is zero so that
	 * not to confuse the clients.
	 */
	bond_info->rlb_update_delay_counter = RLB_UPDATE_DELAY;

	spin_unlock_bh(&bond->mode_lock);
}

/* The slave was assigned a new mac address - update the clients */
static void rlb_req_update_slave_clients(struct bonding *bond, struct slave *slave)
{
	struct alb_bond_info *bond_info = &(BOND_ALB_INFO(bond));
	struct rlb_client_info *client_info;
	int ntt = 0;
	u32 hash_index;

	spin_lock_bh(&bond->mode_lock);

	hash_index = bond_info->rx_hashtbl_used_head;
	for (; hash_index != RLB_NULL_INDEX;
	     hash_index = client_info->used_next) {
		client_info = &(bond_info->rx_hashtbl[hash_index]);

		if ((client_info->slave == slave) &&
		    is_valid_ether_addr(client_info->mac_dst)) {
			client_info->ntt = 1;
			ntt = 1;
		}
	}

	/* update the team's flag only after the whole iteration */
	if (ntt) {
		bond_info->rx_ntt = 1;
		/* fasten the change */
		bond_info->rlb_update_retry_counter = RLB_UPDATE_RETRY;
	}

	spin_unlock_bh(&bond->mode_lock);
}

/* mark all clients using src_ip to be updated */
static void rlb_req_update_subnet_clients(struct bonding *bond, __be32 src_ip)
{
	struct alb_bond_info *bond_info = &(BOND_ALB_INFO(bond));
	struct rlb_client_info *client_info;
	u32 hash_index;

	spin_lock(&bond->mode_lock);

	hash_index = bond_info->rx_hashtbl_used_head;
	for (; hash_index != RLB_NULL_INDEX;
	     hash_index = client_info->used_next) {
		client_info = &(bond_info->rx_hashtbl[hash_index]);

		if (!client_info->slave) {
			netdev_err(bond->dev, "found a client with no channel in the client's hash table\n");
			continue;
		}
		/* update all clients using this src_ip, that are not assigned
		 * to the team's address (curr_active_slave) and have a known
		 * unicast mac address.
		 */
		if ((client_info->ip_src == src_ip) &&
		    !ether_addr_equal_64bits(client_info->slave->dev->dev_addr,
					     bond->dev->dev_addr) &&
		    is_valid_ether_addr(client_info->mac_dst)) {
			client_info->ntt = 1;
			bond_info->rx_ntt = 1;
		}
	}

	spin_unlock(&bond->mode_lock);
}

static struct slave *rlb_choose_channel(struct sk_buff *skb, struct bonding *bond)
{
	struct alb_bond_info *bond_info = &(BOND_ALB_INFO(bond));
	struct arp_pkt *arp = arp_pkt(skb);
	struct slave *assigned_slave, *curr_active_slave;
	struct rlb_client_info *client_info;
	u32 hash_index = 0;

	spin_lock(&bond->mode_lock);

	curr_active_slave = rcu_dereference(bond->curr_active_slave);

	hash_index = _simple_hash((u8 *)&arp->ip_dst, sizeof(arp->ip_dst));
	client_info = &(bond_info->rx_hashtbl[hash_index]);

	if (client_info->assigned) {
		if ((client_info->ip_src == arp->ip_src) &&
		    (client_info->ip_dst == arp->ip_dst)) {
			/* the entry is already assigned to this client */
			if (!is_broadcast_ether_addr(arp->mac_dst)) {
				/* update mac address from arp */
				ether_addr_copy(client_info->mac_dst, arp->mac_dst);
			}
			ether_addr_copy(client_info->mac_src, arp->mac_src);

			assigned_slave = client_info->slave;
			if (assigned_slave) {
				spin_unlock(&bond->mode_lock);
				return assigned_slave;
			}
		} else {
			/* the entry is already assigned to some other client,
			 * move the old client to primary (curr_active_slave) so
			 * that the new client can be assigned to this entry.
			 */
			if (curr_active_slave &&
			    client_info->slave != curr_active_slave) {
				client_info->slave = curr_active_slave;
				rlb_update_client(client_info);
			}
		}
	}
	/* assign a new slave */
	assigned_slave = __rlb_next_rx_slave(bond);

	if (assigned_slave) {
		if (!(client_info->assigned &&
		      client_info->ip_src == arp->ip_src)) {
			/* ip_src is going to be updated,
			 * fix the src hash list
			 */
			u32 hash_src = _simple_hash((u8 *)&arp->ip_src,
						    sizeof(arp->ip_src));
			rlb_src_unlink(bond, hash_index);
			rlb_src_link(bond, hash_src, hash_index);
		}

		client_info->ip_src = arp->ip_src;
		client_info->ip_dst = arp->ip_dst;
		/* arp->mac_dst is broadcast for arp reqeusts.
		 * will be updated with clients actual unicast mac address
		 * upon receiving an arp reply.
		 */
		ether_addr_copy(client_info->mac_dst, arp->mac_dst);
		ether_addr_copy(client_info->mac_src, arp->mac_src);
		client_info->slave = assigned_slave;

		if (is_valid_ether_addr(client_info->mac_dst)) {
			client_info->ntt = 1;
			bond->alb_info.rx_ntt = 1;
		} else {
			client_info->ntt = 0;
		}

		if (vlan_get_tag(skb, &client_info->vlan_id))
			client_info->vlan_id = 0;

		if (!client_info->assigned) {
			u32 prev_tbl_head = bond_info->rx_hashtbl_used_head;
			bond_info->rx_hashtbl_used_head = hash_index;
			client_info->used_next = prev_tbl_head;
			if (prev_tbl_head != RLB_NULL_INDEX) {
				bond_info->rx_hashtbl[prev_tbl_head].used_prev =
					hash_index;
			}
			client_info->assigned = 1;
		}
	}

	spin_unlock(&bond->mode_lock);

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

	/* Don't modify or load balance ARPs that do not originate locally
	 * (e.g.,arrive via a bridge).
	 */
	if (!bond_slave_has_mac_rx(bond, arp->mac_src))
		return NULL;

	if (arp->op_code == htons(ARPOP_REPLY)) {
		/* the arp must be sent on the selected rx channel */
		tx_slave = rlb_choose_channel(skb, bond);
		if (tx_slave)
			bond_hw_addr_copy(arp->mac_src, tx_slave->dev->dev_addr,
					  tx_slave->dev->addr_len);
		netdev_dbg(bond->dev, "(slave %s): Server sent ARP Reply packet\n",
			   tx_slave ? tx_slave->dev->name : "NULL");
	} else if (arp->op_code == htons(ARPOP_REQUEST)) {
		/* Create an entry in the rx_hashtbl for this client as a
		 * place holder.
		 * When the arp reply is received the entry will be updated
		 * with the correct unicast address of the client.
		 */
		tx_slave = rlb_choose_channel(skb, bond);

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
		netdev_dbg(bond->dev, "(slave %s): Server sent ARP Request packet\n",
			   tx_slave ? tx_slave->dev->name : "NULL");
	}

	return tx_slave;
}

static void rlb_rebalance(struct bonding *bond)
{
	struct alb_bond_info *bond_info = &(BOND_ALB_INFO(bond));
	struct slave *assigned_slave;
	struct rlb_client_info *client_info;
	int ntt;
	u32 hash_index;

	spin_lock_bh(&bond->mode_lock);

	ntt = 0;
	hash_index = bond_info->rx_hashtbl_used_head;
	for (; hash_index != RLB_NULL_INDEX;
	     hash_index = client_info->used_next) {
		client_info = &(bond_info->rx_hashtbl[hash_index]);
		assigned_slave = __rlb_next_rx_slave(bond);
		if (assigned_slave && (client_info->slave != assigned_slave)) {
			client_info->slave = assigned_slave;
			if (!is_zero_ether_addr(client_info->mac_dst)) {
				client_info->ntt = 1;
				ntt = 1;
			}
		}
	}

	/* update the team's flag only after the whole iteration */
	if (ntt)
		bond_info->rx_ntt = 1;
	spin_unlock_bh(&bond->mode_lock);
}

/* Caller must hold mode_lock */
static void rlb_init_table_entry_dst(struct rlb_client_info *entry)
{
	entry->used_next = RLB_NULL_INDEX;
	entry->used_prev = RLB_NULL_INDEX;
	entry->assigned = 0;
	entry->slave = NULL;
	entry->vlan_id = 0;
}
static void rlb_init_table_entry_src(struct rlb_client_info *entry)
{
	entry->src_first = RLB_NULL_INDEX;
	entry->src_prev = RLB_NULL_INDEX;
	entry->src_next = RLB_NULL_INDEX;
}

static void rlb_init_table_entry(struct rlb_client_info *entry)
{
	memset(entry, 0, sizeof(struct rlb_client_info));
	rlb_init_table_entry_dst(entry);
	rlb_init_table_entry_src(entry);
}

static void rlb_delete_table_entry_dst(struct bonding *bond, u32 index)
{
	struct alb_bond_info *bond_info = &(BOND_ALB_INFO(bond));
	u32 next_index = bond_info->rx_hashtbl[index].used_next;
	u32 prev_index = bond_info->rx_hashtbl[index].used_prev;

	if (index == bond_info->rx_hashtbl_used_head)
		bond_info->rx_hashtbl_used_head = next_index;
	if (prev_index != RLB_NULL_INDEX)
		bond_info->rx_hashtbl[prev_index].used_next = next_index;
	if (next_index != RLB_NULL_INDEX)
		bond_info->rx_hashtbl[next_index].used_prev = prev_index;
}

/* unlink a rlb hash table entry from the src list */
static void rlb_src_unlink(struct bonding *bond, u32 index)
{
	struct alb_bond_info *bond_info = &(BOND_ALB_INFO(bond));
	u32 next_index = bond_info->rx_hashtbl[index].src_next;
	u32 prev_index = bond_info->rx_hashtbl[index].src_prev;

	bond_info->rx_hashtbl[index].src_next = RLB_NULL_INDEX;
	bond_info->rx_hashtbl[index].src_prev = RLB_NULL_INDEX;

	if (next_index != RLB_NULL_INDEX)
		bond_info->rx_hashtbl[next_index].src_prev = prev_index;

	if (prev_index == RLB_NULL_INDEX)
		return;

	/* is prev_index pointing to the head of this list? */
	if (bond_info->rx_hashtbl[prev_index].src_first == index)
		bond_info->rx_hashtbl[prev_index].src_first = next_index;
	else
		bond_info->rx_hashtbl[prev_index].src_next = next_index;

}

static void rlb_delete_table_entry(struct bonding *bond, u32 index)
{
	struct alb_bond_info *bond_info = &(BOND_ALB_INFO(bond));
	struct rlb_client_info *entry = &(bond_info->rx_hashtbl[index]);

	rlb_delete_table_entry_dst(bond, index);
	rlb_init_table_entry_dst(entry);

	rlb_src_unlink(bond, index);
}

/* add the rx_hashtbl[ip_dst_hash] entry to the list
 * of entries with identical ip_src_hash
 */
static void rlb_src_link(struct bonding *bond, u32 ip_src_hash, u32 ip_dst_hash)
{
	struct alb_bond_info *bond_info = &(BOND_ALB_INFO(bond));
	u32 next;

	bond_info->rx_hashtbl[ip_dst_hash].src_prev = ip_src_hash;
	next = bond_info->rx_hashtbl[ip_src_hash].src_first;
	bond_info->rx_hashtbl[ip_dst_hash].src_next = next;
	if (next != RLB_NULL_INDEX)
		bond_info->rx_hashtbl[next].src_prev = ip_dst_hash;
	bond_info->rx_hashtbl[ip_src_hash].src_first = ip_dst_hash;
}

/* deletes all rx_hashtbl entries with arp->ip_src if their mac_src does
 * not match arp->mac_src
 */
static void rlb_purge_src_ip(struct bonding *bond, struct arp_pkt *arp)
{
	struct alb_bond_info *bond_info = &(BOND_ALB_INFO(bond));
	u32 ip_src_hash = _simple_hash((u8 *)&(arp->ip_src), sizeof(arp->ip_src));
	u32 index;

	spin_lock_bh(&bond->mode_lock);

	index = bond_info->rx_hashtbl[ip_src_hash].src_first;
	while (index != RLB_NULL_INDEX) {
		struct rlb_client_info *entry = &(bond_info->rx_hashtbl[index]);
		u32 next_index = entry->src_next;
		if (entry->ip_src == arp->ip_src &&
		    !ether_addr_equal_64bits(arp->mac_src, entry->mac_src))
				rlb_delete_table_entry(bond, index);
		index = next_index;
	}
	spin_unlock_bh(&bond->mode_lock);
}

static int rlb_initialize(struct bonding *bond)
{
	struct alb_bond_info *bond_info = &(BOND_ALB_INFO(bond));
	struct rlb_client_info	*new_hashtbl;
	int size = RLB_HASH_TABLE_SIZE * sizeof(struct rlb_client_info);
	int i;

	new_hashtbl = kmalloc(size, GFP_KERNEL);
	if (!new_hashtbl)
		return -1;

	spin_lock_bh(&bond->mode_lock);

	bond_info->rx_hashtbl = new_hashtbl;

	bond_info->rx_hashtbl_used_head = RLB_NULL_INDEX;

	for (i = 0; i < RLB_HASH_TABLE_SIZE; i++)
		rlb_init_table_entry(bond_info->rx_hashtbl + i);

	spin_unlock_bh(&bond->mode_lock);

	/* register to receive ARPs */
	bond->recv_probe = rlb_arp_recv;

	return 0;
}

static void rlb_deinitialize(struct bonding *bond)
{
	struct alb_bond_info *bond_info = &(BOND_ALB_INFO(bond));

	spin_lock_bh(&bond->mode_lock);

	kfree(bond_info->rx_hashtbl);
	bond_info->rx_hashtbl = NULL;
	bond_info->rx_hashtbl_used_head = RLB_NULL_INDEX;

	spin_unlock_bh(&bond->mode_lock);
}

static void rlb_clear_vlan(struct bonding *bond, unsigned short vlan_id)
{
	struct alb_bond_info *bond_info = &(BOND_ALB_INFO(bond));
	u32 curr_index;

	spin_lock_bh(&bond->mode_lock);

	curr_index = bond_info->rx_hashtbl_used_head;
	while (curr_index != RLB_NULL_INDEX) {
		struct rlb_client_info *curr = &(bond_info->rx_hashtbl[curr_index]);
		u32 next_index = bond_info->rx_hashtbl[curr_index].used_next;

		if (curr->vlan_id == vlan_id)
			rlb_delete_table_entry(bond, curr_index);

		curr_index = next_index;
	}

	spin_unlock_bh(&bond->mode_lock);
}

/*********************** tlb/rlb shared functions *********************/

static void alb_send_lp_vid(struct slave *slave, u8 mac_addr[],
			    __be16 vlan_proto, u16 vid)
{
	struct learning_pkt pkt;
	struct sk_buff *skb;
	int size = sizeof(struct learning_pkt);

	memset(&pkt, 0, size);
	ether_addr_copy(pkt.mac_dst, mac_addr);
	ether_addr_copy(pkt.mac_src, mac_addr);
	pkt.type = cpu_to_be16(ETH_P_LOOPBACK);

	skb = dev_alloc_skb(size);
	if (!skb)
		return;

	skb_put_data(skb, &pkt, size);

	skb_reset_mac_header(skb);
	skb->network_header = skb->mac_header + ETH_HLEN;
	skb->protocol = pkt.type;
	skb->priority = TC_PRIO_CONTROL;
	skb->dev = slave->dev;

	slave_dbg(slave->bond->dev, slave->dev,
		  "Send learning packet: mac %pM vlan %d\n", mac_addr, vid);

	if (vid)
		__vlan_hwaccel_put_tag(skb, vlan_proto, vid);

	dev_queue_xmit(skb);
}

struct alb_walk_data {
	struct bonding *bond;
	struct slave *slave;
	u8 *mac_addr;
	bool strict_match;
};

static int alb_upper_dev_walk(struct net_device *upper, void *_data)
{
	struct alb_walk_data *data = _data;
	bool strict_match = data->strict_match;
	struct bonding *bond = data->bond;
	struct slave *slave = data->slave;
	u8 *mac_addr = data->mac_addr;
	struct bond_vlan_tag *tags;

	if (is_vlan_dev(upper) &&
	    bond->nest_level == vlan_get_encap_level(upper) - 1) {
		if (upper->addr_assign_type == NET_ADDR_STOLEN) {
			alb_send_lp_vid(slave, mac_addr,
					vlan_dev_vlan_proto(upper),
					vlan_dev_vlan_id(upper));
		} else {
			alb_send_lp_vid(slave, upper->dev_addr,
					vlan_dev_vlan_proto(upper),
					vlan_dev_vlan_id(upper));
		}
	}

	/* If this is a macvlan device, then only send updates
	 * when strict_match is turned off.
	 */
	if (netif_is_macvlan(upper) && !strict_match) {
		tags = bond_verify_device_path(bond->dev, upper, 0);
		if (IS_ERR_OR_NULL(tags))
			BUG();
		alb_send_lp_vid(slave, upper->dev_addr,
				tags[0].vlan_proto, tags[0].vlan_id);
		kfree(tags);
	}

	return 0;
}

static void alb_send_learning_packets(struct slave *slave, u8 mac_addr[],
				      bool strict_match)
{
	struct bonding *bond = bond_get_bond_by_slave(slave);
	struct alb_walk_data data = {
		.strict_match = strict_match,
		.mac_addr = mac_addr,
		.slave = slave,
		.bond = bond,
	};

	/* send untagged */
	alb_send_lp_vid(slave, mac_addr, 0, 0);

	/* loop through all devices and see if we need to send a packet
	 * for that device.
	 */
	rcu_read_lock();
	netdev_walk_all_upper_dev_rcu(bond->dev, alb_upper_dev_walk, &data);
	rcu_read_unlock();
}

static int alb_set_slave_mac_addr(struct slave *slave, u8 addr[],
				  unsigned int len)
{
	struct net_device *dev = slave->dev;
	struct sockaddr_storage ss;

	if (BOND_MODE(slave->bond) == BOND_MODE_TLB) {
		memcpy(dev->dev_addr, addr, len);
		return 0;
	}

	/* for rlb each slave must have a unique hw mac addresses so that
	 * each slave will receive packets destined to a different mac
	 */
	memcpy(ss.__data, addr, len);
	ss.ss_family = dev->type;
	if (dev_set_mac_address(dev, (struct sockaddr *)&ss, NULL)) {
		slave_err(slave->bond->dev, dev, "dev_set_mac_address on slave failed! ALB mode requires that the base driver support setting the hw address also when the network device's interface is open\n");
		return -EOPNOTSUPP;
	}
	return 0;
}

/* Swap MAC addresses between two slaves.
 *
 * Called with RTNL held, and no other locks.
 */
static void alb_swap_mac_addr(struct slave *slave1, struct slave *slave2)
{
	u8 tmp_mac_addr[MAX_ADDR_LEN];

	bond_hw_addr_copy(tmp_mac_addr, slave1->dev->dev_addr,
			  slave1->dev->addr_len);
	alb_set_slave_mac_addr(slave1, slave2->dev->dev_addr,
			       slave2->dev->addr_len);
	alb_set_slave_mac_addr(slave2, tmp_mac_addr,
			       slave1->dev->addr_len);

}

/* Send learning packets after MAC address swap.
 *
 * Called with RTNL and no other locks
 */
static void alb_fasten_mac_swap(struct bonding *bond, struct slave *slave1,
				struct slave *slave2)
{
	int slaves_state_differ = (bond_slave_can_tx(slave1) != bond_slave_can_tx(slave2));
	struct slave *disabled_slave = NULL;

	ASSERT_RTNL();

	/* fasten the change in the switch */
	if (bond_slave_can_tx(slave1)) {
		alb_send_learning_packets(slave1, slave1->dev->dev_addr, false);
		if (bond->alb_info.rlb_enabled) {
			/* inform the clients that the mac address
			 * has changed
			 */
			rlb_req_update_slave_clients(bond, slave1);
		}
	} else {
		disabled_slave = slave1;
	}

	if (bond_slave_can_tx(slave2)) {
		alb_send_learning_packets(slave2, slave2->dev->dev_addr, false);
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
	struct slave *found_slave;

	perm_curr_diff = !ether_addr_equal_64bits(slave->perm_hwaddr,
						  slave->dev->dev_addr);
	perm_bond_diff = !ether_addr_equal_64bits(slave->perm_hwaddr,
						  bond->dev->dev_addr);

	if (perm_curr_diff && perm_bond_diff) {
		found_slave = bond_slave_has_mac(bond, slave->perm_hwaddr);

		if (found_slave) {
			alb_swap_mac_addr(slave, found_slave);
			alb_fasten_mac_swap(bond, slave, found_slave);
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
 *	       bond slave list.
 */
static int alb_handle_addr_collision_on_attach(struct bonding *bond, struct slave *slave)
{
	struct slave *has_bond_addr = rcu_access_pointer(bond->curr_active_slave);
	struct slave *tmp_slave1, *free_mac_slave = NULL;
	struct list_head *iter;

	if (!bond_has_slaves(bond)) {
		/* this is the first slave */
		return 0;
	}

	/* if slave's mac address differs from bond's mac address
	 * check uniqueness of slave's mac address against the other
	 * slaves in the bond.
	 */
	if (!ether_addr_equal_64bits(slave->perm_hwaddr, bond->dev->dev_addr)) {
		if (!bond_slave_has_mac(bond, slave->dev->dev_addr))
			return 0;

		/* Try setting slave mac to bond address and fall-through
		 * to code handling that situation below...
		 */
		alb_set_slave_mac_addr(slave, bond->dev->dev_addr,
				       bond->dev->addr_len);
	}

	/* The slave's address is equal to the address of the bond.
	 * Search for a spare address in the bond for this slave.
	 */
	bond_for_each_slave(bond, tmp_slave1, iter) {
		if (!bond_slave_has_mac(bond, tmp_slave1->perm_hwaddr)) {
			/* no slave has tmp_slave1's perm addr
			 * as its curr addr
			 */
			free_mac_slave = tmp_slave1;
			break;
		}

		if (!has_bond_addr) {
			if (ether_addr_equal_64bits(tmp_slave1->dev->dev_addr,
						    bond->dev->dev_addr)) {

				has_bond_addr = tmp_slave1;
			}
		}
	}

	if (free_mac_slave) {
		alb_set_slave_mac_addr(slave, free_mac_slave->perm_hwaddr,
				       free_mac_slave->dev->addr_len);

		slave_warn(bond->dev, slave->dev, "the slave hw address is in use by the bond; giving it the hw address of %s\n",
			   free_mac_slave->dev->name);

	} else if (has_bond_addr) {
		slave_err(bond->dev, slave->dev, "the slave hw address is in use by the bond; couldn't find a slave with a free hw address to give it (this should not have happened)\n");
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
	struct slave *slave, *rollback_slave;
	struct list_head *iter;
	struct sockaddr_storage ss;
	char tmp_addr[MAX_ADDR_LEN];
	int res;

	if (bond->alb_info.rlb_enabled)
		return 0;

	bond_for_each_slave(bond, slave, iter) {
		/* save net_device's current hw address */
		bond_hw_addr_copy(tmp_addr, slave->dev->dev_addr,
				  slave->dev->addr_len);

		res = dev_set_mac_address(slave->dev, addr, NULL);

		/* restore net_device's hw address */
		bond_hw_addr_copy(slave->dev->dev_addr, tmp_addr,
				  slave->dev->addr_len);

		if (res)
			goto unwind;
	}

	return 0;

unwind:
	memcpy(ss.__data, bond->dev->dev_addr, bond->dev->addr_len);
	ss.ss_family = bond->dev->type;

	/* unwind from head to the slave that failed */
	bond_for_each_slave(bond, rollback_slave, iter) {
		if (rollback_slave == slave)
			break;
		bond_hw_addr_copy(tmp_addr, rollback_slave->dev->dev_addr,
				  rollback_slave->dev->addr_len);
		dev_set_mac_address(rollback_slave->dev,
				    (struct sockaddr *)&ss, NULL);
		bond_hw_addr_copy(rollback_slave->dev->dev_addr, tmp_addr,
				  rollback_slave->dev->addr_len);
	}

	return res;
}

/************************ exported alb funcions ************************/

int bond_alb_initialize(struct bonding *bond, int rlb_enabled)
{
	int res;

	res = tlb_initialize(bond);
	if (res)
		return res;

	if (rlb_enabled) {
		bond->alb_info.rlb_enabled = 1;
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

	if (bond_info->rlb_enabled)
		rlb_deinitialize(bond);
}

static netdev_tx_t bond_do_alb_xmit(struct sk_buff *skb, struct bonding *bond,
				    struct slave *tx_slave)
{
	struct alb_bond_info *bond_info = &(BOND_ALB_INFO(bond));
	struct ethhdr *eth_data = eth_hdr(skb);

	if (!tx_slave) {
		/* unbalanced or unassigned, send through primary */
		tx_slave = rcu_dereference(bond->curr_active_slave);
		if (bond->params.tlb_dynamic_lb)
			bond_info->unbalanced_load += skb->len;
	}

	if (tx_slave && bond_slave_can_tx(tx_slave)) {
		if (tx_slave != rcu_access_pointer(bond->curr_active_slave)) {
			ether_addr_copy(eth_data->h_source,
					tx_slave->dev->dev_addr);
		}

		bond_dev_queue_xmit(bond, skb, tx_slave->dev);
		goto out;
	}

	if (tx_slave && bond->params.tlb_dynamic_lb) {
		spin_lock(&bond->mode_lock);
		__tlb_clear_slave(bond, tx_slave, 0);
		spin_unlock(&bond->mode_lock);
	}

	/* no suitable interface, frame not sent */
	bond_tx_drop(bond->dev, skb);
out:
	return NETDEV_TX_OK;
}

netdev_tx_t bond_tlb_xmit(struct sk_buff *skb, struct net_device *bond_dev)
{
	struct bonding *bond = netdev_priv(bond_dev);
	struct ethhdr *eth_data;
	struct slave *tx_slave = NULL;
	u32 hash_index;

	skb_reset_mac_header(skb);
	eth_data = eth_hdr(skb);

	/* Do not TX balance any multicast or broadcast */
	if (!is_multicast_ether_addr(eth_data->h_dest)) {
		switch (skb->protocol) {
		case htons(ETH_P_IP):
		case htons(ETH_P_IPX):
		    /* In case of IPX, it will falback to L2 hash */
		case htons(ETH_P_IPV6):
			hash_index = bond_xmit_hash(bond, skb);
			if (bond->params.tlb_dynamic_lb) {
				tx_slave = tlb_choose_channel(bond,
							      hash_index & 0xFF,
							      skb->len);
			} else {
				struct bond_up_slave *slaves;
				unsigned int count;

				slaves = rcu_dereference(bond->slave_arr);
				count = slaves ? READ_ONCE(slaves->count) : 0;
				if (likely(count))
					tx_slave = slaves->arr[hash_index %
							       count];
			}
			break;
		}
	}
	return bond_do_alb_xmit(skb, bond, tx_slave);
}

netdev_tx_t bond_alb_xmit(struct sk_buff *skb, struct net_device *bond_dev)
{
	struct bonding *bond = netdev_priv(bond_dev);
	struct ethhdr *eth_data;
	struct alb_bond_info *bond_info = &(BOND_ALB_INFO(bond));
	struct slave *tx_slave = NULL;
	static const __be32 ip_bcast = htonl(0xffffffff);
	int hash_size = 0;
	bool do_tx_balance = true;
	u32 hash_index = 0;
	const u8 *hash_start = NULL;
	struct ipv6hdr *ip6hdr;

	skb_reset_mac_header(skb);
	eth_data = eth_hdr(skb);

	switch (ntohs(skb->protocol)) {
	case ETH_P_IP: {
		const struct iphdr *iph = ip_hdr(skb);

		if (is_broadcast_ether_addr(eth_data->h_dest) ||
		    iph->daddr == ip_bcast ||
		    iph->protocol == IPPROTO_IGMP) {
			do_tx_balance = false;
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
		if (is_broadcast_ether_addr(eth_data->h_dest)) {
			do_tx_balance = false;
			break;
		}

		/* IPv6 uses all-nodes multicast as an equivalent to
		 * broadcasts in IPv4.
		 */
		if (ether_addr_equal_64bits(eth_data->h_dest, mac_v6_allmcast)) {
			do_tx_balance = false;
			break;
		}

		/* Additianally, DAD probes should not be tx-balanced as that
		 * will lead to false positives for duplicate addresses and
		 * prevent address configuration from working.
		 */
		ip6hdr = ipv6_hdr(skb);
		if (ipv6_addr_any(&ip6hdr->saddr)) {
			do_tx_balance = false;
			break;
		}

		hash_start = (char *)&(ipv6_hdr(skb)->daddr);
		hash_size = sizeof(ipv6_hdr(skb)->daddr);
		break;
	case ETH_P_IPX:
		if (ipx_hdr(skb)->ipx_checksum != IPX_NO_CHECKSUM) {
			/* something is wrong with this packet */
			do_tx_balance = false;
			break;
		}

		if (ipx_hdr(skb)->ipx_type != IPX_TYPE_NCP) {
			/* The only protocol worth balancing in
			 * this family since it has an "ARP" like
			 * mechanism
			 */
			do_tx_balance = false;
			break;
		}

		hash_start = (char *)eth_data->h_dest;
		hash_size = ETH_ALEN;
		break;
	case ETH_P_ARP:
		do_tx_balance = false;
		if (bond_info->rlb_enabled)
			tx_slave = rlb_arp_xmit(skb, bond);
		break;
	default:
		do_tx_balance = false;
		break;
	}

	if (do_tx_balance) {
		if (bond->params.tlb_dynamic_lb) {
			hash_index = _simple_hash(hash_start, hash_size);
			tx_slave = tlb_choose_channel(bond, hash_index, skb->len);
		} else {
			/*
			 * do_tx_balance means we are free to select the tx_slave
			 * So we do exactly what tlb would do for hash selection
			 */

			struct bond_up_slave *slaves;
			unsigned int count;

			slaves = rcu_dereference(bond->slave_arr);
			count = slaves ? READ_ONCE(slaves->count) : 0;
			if (likely(count))
				tx_slave = slaves->arr[bond_xmit_hash(bond, skb) %
						       count];
		}
	}

	return bond_do_alb_xmit(skb, bond, tx_slave);
}

void bond_alb_monitor(struct work_struct *work)
{
	struct bonding *bond = container_of(work, struct bonding,
					    alb_work.work);
	struct alb_bond_info *bond_info = &(BOND_ALB_INFO(bond));
	struct list_head *iter;
	struct slave *slave;

	if (!bond_has_slaves(bond)) {
		bond_info->tx_rebalance_counter = 0;
		bond_info->lp_counter = 0;
		goto re_arm;
	}

	rcu_read_lock();

	bond_info->tx_rebalance_counter++;
	bond_info->lp_counter++;

	/* send learning packets */
	if (bond_info->lp_counter >= BOND_ALB_LP_TICKS(bond)) {
		bool strict_match;

		bond_for_each_slave_rcu(bond, slave, iter) {
			/* If updating current_active, use all currently
			 * user mac addreses (!strict_match).  Otherwise, only
			 * use mac of the slave device.
			 * In RLB mode, we always use strict matches.
			 */
			strict_match = (slave != rcu_access_pointer(bond->curr_active_slave) ||
					bond_info->rlb_enabled);
			alb_send_learning_packets(slave, slave->dev->dev_addr,
						  strict_match);
		}
		bond_info->lp_counter = 0;
	}

	/* rebalance tx traffic */
	if (bond_info->tx_rebalance_counter >= BOND_TLB_REBALANCE_TICKS) {
		bond_for_each_slave_rcu(bond, slave, iter) {
			tlb_clear_slave(bond, slave, 1);
			if (slave == rcu_access_pointer(bond->curr_active_slave)) {
				SLAVE_TLB_INFO(slave).load =
					bond_info->unbalanced_load /
						BOND_TLB_REBALANCE_INTERVAL;
				bond_info->unbalanced_load = 0;
			}
		}
		bond_info->tx_rebalance_counter = 0;
	}

	if (bond_info->rlb_enabled) {
		if (bond_info->primary_is_promisc &&
		    (++bond_info->rlb_promisc_timeout_counter >= RLB_PROMISC_TIMEOUT)) {

			/* dev_set_promiscuity requires rtnl and
			 * nothing else.  Avoid race with bond_close.
			 */
			rcu_read_unlock();
			if (!rtnl_trylock())
				goto re_arm;

			bond_info->rlb_promisc_timeout_counter = 0;

			/* If the primary was set to promiscuous mode
			 * because a slave was disabled then
			 * it can now leave promiscuous mode.
			 */
			dev_set_promiscuity(rtnl_dereference(bond->curr_active_slave)->dev,
					    -1);
			bond_info->primary_is_promisc = 0;

			rtnl_unlock();
			rcu_read_lock();
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
				if (bond_info->rlb_update_retry_counter)
					--bond_info->rlb_update_retry_counter;
				else
					bond_info->rx_ntt = 0;
			}
		}
	}
	rcu_read_unlock();
re_arm:
	queue_delayed_work(bond->wq, &bond->alb_work, alb_delta_in_ticks);
}

/* assumption: called before the slave is attached to the bond
 * and not locked by the bond lock
 */
int bond_alb_init_slave(struct bonding *bond, struct slave *slave)
{
	int res;

	res = alb_set_slave_mac_addr(slave, slave->perm_hwaddr,
				     slave->dev->addr_len);
	if (res)
		return res;

	res = alb_handle_addr_collision_on_attach(bond, slave);
	if (res)
		return res;

	tlb_init_slave(slave);

	/* order a rebalance ASAP */
	bond->alb_info.tx_rebalance_counter = BOND_TLB_REBALANCE_TICKS;

	if (bond->alb_info.rlb_enabled)
		bond->alb_info.rlb_rebalance = 1;

	return 0;
}

/* Remove slave from tlb and rlb hash tables, and fix up MAC addresses
 * if necessary.
 *
 * Caller must hold RTNL and no other locks
 */
void bond_alb_deinit_slave(struct bonding *bond, struct slave *slave)
{
	if (bond_has_slaves(bond))
		alb_change_hw_addr_on_detach(bond, slave);

	tlb_clear_slave(bond, slave, 0);

	if (bond->alb_info.rlb_enabled) {
		bond->alb_info.rx_slave = NULL;
		rlb_clear_slave(bond, slave);
	}

}

void bond_alb_handle_link_change(struct bonding *bond, struct slave *slave, char link)
{
	struct alb_bond_info *bond_info = &(BOND_ALB_INFO(bond));

	if (link == BOND_LINK_DOWN) {
		tlb_clear_slave(bond, slave, 0);
		if (bond->alb_info.rlb_enabled)
			rlb_clear_slave(bond, slave);
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

	if (bond_is_nondyn_tlb(bond)) {
		if (bond_update_slave_arr(bond, NULL))
			pr_err("Failed to build slave-array for TLB mode.\n");
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
 * Caller must hold RTNL
 */
void bond_alb_handle_active_change(struct bonding *bond, struct slave *new_slave)
{
	struct slave *swap_slave;
	struct slave *curr_active;

	curr_active = rtnl_dereference(bond->curr_active_slave);
	if (curr_active == new_slave)
		return;

	if (curr_active && bond->alb_info.primary_is_promisc) {
		dev_set_promiscuity(curr_active->dev, -1);
		bond->alb_info.primary_is_promisc = 0;
		bond->alb_info.rlb_promisc_timeout_counter = 0;
	}

	swap_slave = curr_active;
	rcu_assign_pointer(bond->curr_active_slave, new_slave);

	if (!new_slave || !bond_has_slaves(bond))
		return;

	/* set the new curr_active_slave to the bonds mac address
	 * i.e. swap mac addresses of old curr_active_slave and new curr_active_slave
	 */
	if (!swap_slave)
		swap_slave = bond_slave_has_mac(bond, bond->dev->dev_addr);

	/* Arrange for swap_slave and new_slave to temporarily be
	 * ignored so we can mess with their MAC addresses without
	 * fear of interference from transmit activity.
	 */
	if (swap_slave)
		tlb_clear_slave(bond, swap_slave, 1);
	tlb_clear_slave(bond, new_slave, 1);

	/* in TLB mode, the slave might flip down/up with the old dev_addr,
	 * and thus filter bond->dev_addr's packets, so force bond's mac
	 */
	if (BOND_MODE(bond) == BOND_MODE_TLB) {
		struct sockaddr_storage ss;
		u8 tmp_addr[MAX_ADDR_LEN];

		bond_hw_addr_copy(tmp_addr, new_slave->dev->dev_addr,
				  new_slave->dev->addr_len);

		bond_hw_addr_copy(ss.__data, bond->dev->dev_addr,
				  bond->dev->addr_len);
		ss.ss_family = bond->dev->type;
		/* we don't care if it can't change its mac, best effort */
		dev_set_mac_address(new_slave->dev, (struct sockaddr *)&ss,
				    NULL);

		bond_hw_addr_copy(new_slave->dev->dev_addr, tmp_addr,
				  new_slave->dev->addr_len);
	}

	/* curr_active_slave must be set before calling alb_swap_mac_addr */
	if (swap_slave) {
		/* swap mac address */
		alb_swap_mac_addr(swap_slave, new_slave);
		alb_fasten_mac_swap(bond, swap_slave, new_slave);
	} else {
		/* set the new_slave to the bond mac address */
		alb_set_slave_mac_addr(new_slave, bond->dev->dev_addr,
				       bond->dev->addr_len);
		alb_send_learning_packets(new_slave, bond->dev->dev_addr,
					  false);
	}
}

/* Called with RTNL */
int bond_alb_set_mac_address(struct net_device *bond_dev, void *addr)
{
	struct bonding *bond = netdev_priv(bond_dev);
	struct sockaddr_storage *ss = addr;
	struct slave *curr_active;
	struct slave *swap_slave;
	int res;

	if (!is_valid_ether_addr(ss->__data))
		return -EADDRNOTAVAIL;

	res = alb_set_mac_address(bond, addr);
	if (res)
		return res;

	bond_hw_addr_copy(bond_dev->dev_addr, ss->__data, bond_dev->addr_len);

	/* If there is no curr_active_slave there is nothing else to do.
	 * Otherwise we'll need to pass the new address to it and handle
	 * duplications.
	 */
	curr_active = rtnl_dereference(bond->curr_active_slave);
	if (!curr_active)
		return 0;

	swap_slave = bond_slave_has_mac(bond, bond_dev->dev_addr);

	if (swap_slave) {
		alb_swap_mac_addr(swap_slave, curr_active);
		alb_fasten_mac_swap(bond, swap_slave, curr_active);
	} else {
		alb_set_slave_mac_addr(curr_active, bond_dev->dev_addr,
				       bond_dev->addr_len);

		alb_send_learning_packets(curr_active,
					  bond_dev->dev_addr, false);
		if (bond->alb_info.rlb_enabled) {
			/* inform clients mac address has changed */
			rlb_req_update_slave_clients(bond, curr_active);
		}
	}

	return 0;
}

void bond_alb_clear_vlan(struct bonding *bond, unsigned short vlan_id)
{
	if (bond->alb_info.rlb_enabled)
		rlb_clear_vlan(bond, vlan_id);
}

