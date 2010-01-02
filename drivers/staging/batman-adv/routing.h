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

#include "types.h"

extern wait_queue_head_t thread_wait;
extern atomic_t exit_cond;

void slide_own_bcast_window(struct batman_if *batman_if);
void receive_bat_packet(struct ethhdr *ethhdr,
				struct batman_packet *batman_packet,
				unsigned char *hna_buff, int hna_buff_len,
				struct batman_if *if_incoming);
void update_routes(struct orig_node *orig_node,
				struct neigh_node *neigh_node,
				unsigned char *hna_buff, int hna_buff_len);
int recv_icmp_packet(struct sk_buff *skb);
int recv_unicast_packet(struct sk_buff *skb);
int recv_bcast_packet(struct sk_buff *skb);
int recv_vis_packet(struct sk_buff *skb);
int recv_bat_packet(struct sk_buff *skb,
				struct batman_if *batman_if);
