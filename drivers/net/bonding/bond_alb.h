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
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 */

#ifndef __BOND_ALB_H__
#define __BOND_ALB_H__

#include <linux/if_ether.h>

struct bonding;
struct slave;

#define BOND_ALB_INFO(bond)   ((bond)->alb_info)
#define SLAVE_TLB_INFO(slave) ((slave)->tlb_info)

#define ALB_TIMER_TICKS_PER_SEC	    10	/* should be a divisor of HZ */
#define BOND_TLB_REBALANCE_INTERVAL 10	/* In seconds, periodic re-balancing.
					 * Used for division - never set
					 * to zero !!!
					 */
#define BOND_ALB_DEFAULT_LP_INTERVAL 1
#define BOND_ALB_LP_INTERVAL(bond) (bond->params.lp_interval)	/* In seconds, periodic send of
								 * learning packets to the switch
								 */

#define BOND_TLB_REBALANCE_TICKS (BOND_TLB_REBALANCE_INTERVAL \
				  * ALB_TIMER_TICKS_PER_SEC)

#define BOND_ALB_LP_TICKS(bond) (BOND_ALB_LP_INTERVAL(bond) \
			   * ALB_TIMER_TICKS_PER_SEC)

#define TLB_HASH_TABLE_SIZE 256	/* The size of the clients hash table.
				 * Note that this value MUST NOT be smaller
				 * because the key hash table is BYTE wide !
				 */


#define TLB_NULL_INDEX		0xffffffff

/* rlb defs */
#define RLB_HASH_TABLE_SIZE	256
#define RLB_NULL_INDEX		0xffffffff
#define RLB_UPDATE_DELAY	(2*ALB_TIMER_TICKS_PER_SEC) /* 2 seconds */
#define RLB_ARP_BURST_SIZE	2
#define RLB_UPDATE_RETRY	3 /* 3-ticks - must be smaller than the rlb
				   * rebalance interval (5 min).
				   */
/* RLB_PROMISC_TIMEOUT = 10 sec equals the time that the current slave is
 * promiscuous after failover
 */
#define RLB_PROMISC_TIMEOUT	(10*ALB_TIMER_TICKS_PER_SEC)


struct tlb_client_info {
	struct slave *tx_slave;	/* A pointer to slave used for transmiting
				 * packets to a Client that the Hash function
				 * gave this entry index.
				 */
	u32 tx_bytes;		/* Each Client accumulates the BytesTx that
				 * were transmitted to it, and after each
				 * CallBack the LoadHistory is divided
				 * by the balance interval
				 */
	u32 load_history;	/* This field contains the amount of Bytes
				 * that were transmitted to this client by
				 * the server on the previous balance
				 * interval in Bps.
				 */
	u32 next;		/* The next Hash table entry index, assigned
				 * to use the same adapter for transmit.
				 */
	u32 prev;		/* The previous Hash table entry index,
				 * assigned to use the same
				 */
};

/* -------------------------------------------------------------------------
 * struct rlb_client_info contains all info related to a specific rx client
 * connection. This is the Clients Hash Table entry struct.
 * Note that this is not a proper hash table; if a new client's IP address
 * hash collides with an existing client entry, the old entry is replaced.
 *
 * There is a linked list (linked by the used_next and used_prev members)
 * linking all the used entries of the hash table. This allows updating
 * all the clients without walking over all the unused elements of the table.
 *
 * There are also linked lists of entries with identical hash(ip_src). These
 * allow cleaning up the table from ip_src<->mac_src associations that have
 * become outdated and would cause sending out invalid ARP updates to the
 * network. These are linked by the (src_next and src_prev members).
 * -------------------------------------------------------------------------
 */
struct rlb_client_info {
	__be32 ip_src;		/* the server IP address */
	__be32 ip_dst;		/* the client IP address */
	u8  mac_src[ETH_ALEN];	/* the server MAC address */
	u8  mac_dst[ETH_ALEN];	/* the client MAC address */

	/* list of used hash table entries, starting at rx_hashtbl_used_head */
	u32 used_next;
	u32 used_prev;

	/* ip_src based hashing */
	u32 src_next;	/* next entry with same hash(ip_src) */
	u32 src_prev;	/* prev entry with same hash(ip_src) */
	u32 src_first;	/* first entry with hash(ip_src) == this entry's index */

	u8  assigned;		/* checking whether this entry is assigned */
	u8  ntt;		/* flag - need to transmit client info */
	struct slave *slave;	/* the slave assigned to this client */
	unsigned short vlan_id;	/* VLAN tag associated with IP address */
};

struct tlb_slave_info {
	u32 head;	/* Index to the head of the bi-directional clients
			 * hash table entries list. The entries in the list
			 * are the entries that were assigned to use this
			 * slave for transmit.
			 */
	u32 load;	/* Each slave sums the loadHistory of all clients
			 * assigned to it
			 */
};

struct alb_bond_info {
	struct tlb_client_info	*tx_hashtbl; /* Dynamically allocated */
	u32			unbalanced_load;
	int			tx_rebalance_counter;
	int			lp_counter;
	/* -------- rlb parameters -------- */
	int rlb_enabled;
	struct rlb_client_info	*rx_hashtbl;	/* Receive hash table */
	u32			rx_hashtbl_used_head;
	u8			rx_ntt;	/* flag - need to transmit
					 * to all rx clients
					 */
	struct slave		*rx_slave;/* last slave to xmit from */
	u8			primary_is_promisc;	   /* boolean */
	u32			rlb_promisc_timeout_counter;/* counts primary
							     * promiscuity time
							     */
	u32			rlb_update_delay_counter;
	u32			rlb_update_retry_counter;/* counter of retries
							  * of client update
							  */
	u8			rlb_rebalance;	/* flag - indicates that the
						 * rx traffic should be
						 * rebalanced
						 */
};

int bond_alb_initialize(struct bonding *bond, int rlb_enabled);
void bond_alb_deinitialize(struct bonding *bond);
int bond_alb_init_slave(struct bonding *bond, struct slave *slave);
void bond_alb_deinit_slave(struct bonding *bond, struct slave *slave);
void bond_alb_handle_link_change(struct bonding *bond, struct slave *slave, char link);
void bond_alb_handle_active_change(struct bonding *bond, struct slave *new_slave);
int bond_alb_xmit(struct sk_buff *skb, struct net_device *bond_dev);
int bond_tlb_xmit(struct sk_buff *skb, struct net_device *bond_dev);
void bond_alb_monitor(struct work_struct *);
int bond_alb_set_mac_address(struct net_device *bond_dev, void *addr);
void bond_alb_clear_vlan(struct bonding *bond, unsigned short vlan_id);
#endif /* __BOND_ALB_H__ */

