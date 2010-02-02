/*
 * Copyright (C) 2007-2009 B.A.T.M.A.N. contributors:
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





#ifndef TYPES_H
#define TYPES_H

#include "packet.h"
#include "bitarray.h"

#define BAT_HEADER_LEN (sizeof(struct ethhdr) + ((sizeof(struct unicast_packet) > sizeof(struct bcast_packet) ? sizeof(struct unicast_packet) : sizeof(struct bcast_packet))))


struct batman_if {
	struct list_head list;
	int16_t if_num;
	char *dev;
	char if_active;
	char addr_str[ETH_STR_LEN];
	struct net_device *net_dev;
	struct socket *raw_sock;
	atomic_t seqno;
	unsigned char *packet_buff;
	int packet_len;
	struct rcu_head rcu;

};

struct orig_node {               /* structure for orig_list maintaining nodes of mesh */
	uint8_t orig[ETH_ALEN];
	struct neigh_node *router;
	struct batman_if *batman_if;
	TYPE_OF_WORD *bcast_own;
	uint8_t *bcast_own_sum;
	uint8_t tq_own;
	int tq_asym_penalty;
	unsigned long last_valid;        /* when last packet from this node was received */
/*	uint8_t  gwflags;      * flags related to gateway functions: gateway class */
	uint8_t  flags;    		/* for now only VIS_SERVER flag. */
	unsigned char *hna_buff;
	int16_t  hna_buff_len;
	uint16_t last_real_seqno;   /* last and best known squence number */
	uint8_t last_ttl;         /* ttl of last received packet */
	TYPE_OF_WORD bcast_bits[NUM_WORDS];
	uint16_t last_bcast_seqno;  /* last broadcast sequence number received by this host */
	struct list_head neigh_list;
};

struct neigh_node {
	struct list_head list;
	uint8_t addr[ETH_ALEN];
	uint8_t real_packet_count;
	uint8_t tq_recv[TQ_GLOBAL_WINDOW_SIZE];
	uint8_t tq_index;
	uint8_t tq_avg;
	uint8_t last_ttl;
	unsigned long last_valid;            /* when last packet via this neighbour was received */
	TYPE_OF_WORD real_bits[NUM_WORDS];
	struct orig_node *orig_node;
	struct batman_if *if_incoming;
};

struct bat_priv {
	struct net_device_stats stats;
};

struct device_client {
	struct list_head queue_list;
	unsigned int queue_len;
	unsigned char index;
	spinlock_t lock;
	wait_queue_head_t queue_wait;
};

struct device_packet {
	struct list_head list;
	struct icmp_packet icmp_packet;
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

struct forw_packet {               /* structure for forw_list maintaining packets to be send/forwarded */
	struct hlist_node list;
	unsigned long send_time;
	uint8_t own;
	unsigned char *packet_buff;
	uint16_t packet_len;
	uint32_t direct_link_flags;
	uint8_t num_packets;
	struct delayed_work delayed_work;
	struct batman_if *if_incoming;
};

#endif
