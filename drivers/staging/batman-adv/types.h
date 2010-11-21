/*
 * Copyright (C) 2007-2010 B.A.T.M.A.N. contributors:
 *
 * Marek Lindner, Simon Wunderlich
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA
 *
 */



#ifndef _NET_BATMAN_ADV_TYPES_H_
#define _NET_BATMAN_ADV_TYPES_H_

#include "packet.h"
#include "bitarray.h"

#define BAT_HEADER_LEN (sizeof(struct ethhdr) + \
	((sizeof(struct unicast_packet) > sizeof(struct bcast_packet) ? \
	 sizeof(struct unicast_packet) : \
	 sizeof(struct bcast_packet))))


struct batman_if {
	struct list_head list;
	int16_t if_num;
	char if_status;
	struct net_device *net_dev;
	atomic_t seqno;
	atomic_t frag_seqno;
	unsigned char *packet_buff;
	int packet_len;
	struct kobject *hardif_obj;
	struct kref refcount;
	struct packet_type batman_adv_ptype;
	struct net_device *soft_iface;
	struct rcu_head rcu;
};

/**
 *	orig_node - structure for orig_list maintaining nodes of mesh
 *	@primary_addr: hosts primary interface address
 *	@last_valid: when last packet from this node was received
 *	@bcast_seqno_reset: time when the broadcast seqno window was reset
 *	@batman_seqno_reset: time when the batman seqno window was reset
 *	@flags: for now only VIS_SERVER flag
 *	@last_real_seqno: last and best known squence number
 *	@last_ttl: ttl of last received packet
 *	@last_bcast_seqno: last broadcast sequence number received by this host
 *
 *	@candidates: how many candidates are available
 *	@selected: next bonding candidate
 */
struct orig_node {
	uint8_t orig[ETH_ALEN];
	uint8_t primary_addr[ETH_ALEN];
	struct neigh_node *router;
	TYPE_OF_WORD *bcast_own;
	uint8_t *bcast_own_sum;
	uint8_t tq_own;
	int tq_asym_penalty;
	unsigned long last_valid;
	unsigned long bcast_seqno_reset;
	unsigned long batman_seqno_reset;
	uint8_t  flags;
	unsigned char *hna_buff;
	int16_t hna_buff_len;
	uint32_t last_real_seqno;
	uint8_t last_ttl;
	TYPE_OF_WORD bcast_bits[NUM_WORDS];
	uint32_t last_bcast_seqno;
	struct list_head neigh_list;
	struct list_head frag_list;
	unsigned long last_frag_packet;
	struct {
		uint8_t candidates;
		struct neigh_node *selected;
	} bond;
};

/**
 *	neigh_node
 *	@last_valid: when last packet via this neighbor was received
 */
struct neigh_node {
	struct list_head list;
	uint8_t addr[ETH_ALEN];
	uint8_t real_packet_count;
	uint8_t tq_recv[TQ_GLOBAL_WINDOW_SIZE];
	uint8_t tq_index;
	uint8_t tq_avg;
	uint8_t last_ttl;
	struct neigh_node *next_bond_candidate;
	unsigned long last_valid;
	TYPE_OF_WORD real_bits[NUM_WORDS];
	struct orig_node *orig_node;
	struct batman_if *if_incoming;
};


struct bat_priv {
	atomic_t mesh_state;
	struct net_device_stats stats;
	atomic_t aggregated_ogms;	/* boolean */
	atomic_t bonding;		/* boolean */
	atomic_t fragmentation;		/* boolean */
	atomic_t vis_mode;		/* VIS_TYPE_* */
	atomic_t orig_interval;		/* uint */
	atomic_t hop_penalty;		/* uint */
	atomic_t log_level;		/* uint */
	atomic_t bcast_seqno;
	atomic_t bcast_queue_left;
	atomic_t batman_queue_left;
	char num_ifaces;
	struct hlist_head softif_neigh_list;
	struct softif_neigh *softif_neigh;
	struct debug_log *debug_log;
	struct batman_if *primary_if;
	struct kobject *mesh_obj;
	struct dentry *debug_dir;
	struct hlist_head forw_bat_list;
	struct hlist_head forw_bcast_list;
	struct list_head vis_send_list;
	struct hashtable_t *orig_hash;
	struct hashtable_t *hna_local_hash;
	struct hashtable_t *hna_global_hash;
	struct hashtable_t *vis_hash;
	spinlock_t orig_hash_lock; /* protects orig_hash */
	spinlock_t forw_bat_list_lock; /* protects forw_bat_list */
	spinlock_t forw_bcast_list_lock; /* protects  */
	spinlock_t hna_lhash_lock; /* protects hna_local_hash */
	spinlock_t hna_ghash_lock; /* protects hna_global_hash */
	spinlock_t vis_hash_lock; /* protects vis_hash */
	spinlock_t vis_list_lock; /* protects vis_info::recv_list */
	spinlock_t softif_neigh_lock; /* protects soft-interface neigh list */
	int16_t num_local_hna;
	atomic_t hna_local_changed;
	struct delayed_work hna_work;
	struct delayed_work orig_work;
	struct delayed_work vis_work;
	struct vis_info *my_vis_info;
};

struct socket_client {
	struct list_head queue_list;
	unsigned int queue_len;
	unsigned char index;
	spinlock_t lock; /* protects queue_list, queue_len, index */
	wait_queue_head_t queue_wait;
	struct bat_priv *bat_priv;
};

struct socket_packet {
	struct list_head list;
	size_t icmp_len;
	struct icmp_packet_rr icmp_packet;
};

struct hna_local_entry {
	uint8_t addr[ETH_ALEN];
	unsigned long last_seen;
	char never_purge;
};

struct hna_global_entry {
	uint8_t addr[ETH_ALEN];
	struct orig_node *orig_node;
};

/**
 *	forw_packet - structure for forw_list maintaining packets to be
 *	              send/forwarded
 */
struct forw_packet {
	struct hlist_node list;
	unsigned long send_time;
	uint8_t own;
	struct sk_buff *skb;
	uint16_t packet_len;
	uint32_t direct_link_flags;
	uint8_t num_packets;
	struct delayed_work delayed_work;
	struct batman_if *if_incoming;
};

/* While scanning for vis-entries of a particular vis-originator
 * this list collects its interfaces to create a subgraph/cluster
 * out of them later
 */
struct if_list_entry {
	uint8_t addr[ETH_ALEN];
	bool primary;
	struct hlist_node list;
};

struct debug_log {
	char log_buff[LOG_BUF_LEN];
	unsigned long log_start;
	unsigned long log_end;
	spinlock_t lock; /* protects log_buff, log_start and log_end */
	wait_queue_head_t queue_wait;
};

struct frag_packet_list_entry {
	struct list_head list;
	uint16_t seqno;
	struct sk_buff *skb;
};

struct vis_info {
	unsigned long       first_seen;
	struct list_head    recv_list;
			    /* list of server-neighbors we received a vis-packet
			     * from.  we should not reply to them. */
	struct list_head send_list;
	struct kref refcount;
	struct bat_priv *bat_priv;
	/* this packet might be part of the vis send queue. */
	struct sk_buff *skb_packet;
	/* vis_info may follow here*/
} __attribute__((packed));

struct vis_info_entry {
	uint8_t  src[ETH_ALEN];
	uint8_t  dest[ETH_ALEN];
	uint8_t  quality;	/* quality = 0 means HNA */
} __attribute__((packed));

struct recvlist_node {
	struct list_head list;
	uint8_t mac[ETH_ALEN];
};

struct softif_neigh {
	struct hlist_node list;
	uint8_t addr[ETH_ALEN];
	unsigned long last_seen;
	short vid;
	struct kref refcount;
	struct rcu_head rcu;
};

#endif /* _NET_BATMAN_ADV_TYPES_H_ */
