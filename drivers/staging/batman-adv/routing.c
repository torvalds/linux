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

#include "main.h"
#include "routing.h"
#include "send.h"
#include "hash.h"
#include "soft-interface.h"
#include "hard-interface.h"
#include "device.h"
#include "translation-table.h"
#include "originator.h"
#include "types.h"
#include "ring_buffer.h"
#include "vis.h"
#include "aggregation.h"
#include "compat.h"

DECLARE_WAIT_QUEUE_HEAD(thread_wait);

atomic_t exit_cond;

void slide_own_bcast_window(struct batman_if *batman_if)
{
	HASHIT(hashit);
	struct orig_node *orig_node;
	TYPE_OF_WORD *word;
	unsigned long flags;

	spin_lock_irqsave(&orig_hash_lock, flags);

	while (hash_iterate(orig_hash, &hashit)) {
		orig_node = hashit.bucket->data;
		word = &(orig_node->bcast_own[batman_if->if_num * NUM_WORDS]);

		bit_get_packet(word, 1, 0);
		orig_node->bcast_own_sum[batman_if->if_num] =
			bit_packet_count(word);
	}

	spin_unlock_irqrestore(&orig_hash_lock, flags);
}

static void update_HNA(struct orig_node *orig_node,
		       unsigned char *hna_buff, int hna_buff_len)
{
	if ((hna_buff_len != orig_node->hna_buff_len) ||
	    ((hna_buff_len > 0) &&
	     (orig_node->hna_buff_len > 0) &&
	     (memcmp(orig_node->hna_buff, hna_buff, hna_buff_len) != 0))) {

		if (orig_node->hna_buff_len > 0)
			hna_global_del_orig(orig_node,
					    "originator changed hna");

		if ((hna_buff_len > 0) && (hna_buff != NULL))
			hna_global_add_orig(orig_node, hna_buff, hna_buff_len);
	}
}

static void update_route(struct orig_node *orig_node,
			 struct neigh_node *neigh_node,
			 unsigned char *hna_buff, int hna_buff_len)
{
	char orig_str[ETH_STR_LEN], neigh_str[ETH_STR_LEN];
	char router_str[ETH_STR_LEN];

	addr_to_string(orig_str, orig_node->orig);

	/* route deleted */
	if ((orig_node->router != NULL) && (neigh_node == NULL)) {

		bat_dbg(DBG_ROUTES, "Deleting route towards: %s\n",
			orig_str);
		hna_global_del_orig(orig_node, "originator timed out");

		/* route added */
	} else if ((orig_node->router == NULL) && (neigh_node != NULL)) {

		addr_to_string(neigh_str, neigh_node->addr);
		bat_dbg(DBG_ROUTES,
			"Adding route towards: %s (via %s)\n",
			orig_str, neigh_str);
		hna_global_add_orig(orig_node, hna_buff, hna_buff_len);

		/* route changed */
	} else {
		addr_to_string(neigh_str, neigh_node->addr);
		addr_to_string(router_str, orig_node->router->addr);
		bat_dbg(DBG_ROUTES, "Changing route towards: %s (now via %s - was via %s)\n", orig_str, neigh_str, router_str);
	}

	if (neigh_node != NULL)
		orig_node->batman_if = neigh_node->if_incoming;
	else
		orig_node->batman_if = NULL;

	orig_node->router = neigh_node;
}


void update_routes(struct orig_node *orig_node,
			  struct neigh_node *neigh_node,
			  unsigned char *hna_buff, int hna_buff_len)
{

	if (orig_node == NULL)
		return;

	if (orig_node->router != neigh_node)
		update_route(orig_node, neigh_node, hna_buff, hna_buff_len);
	/* may be just HNA changed */
	else
		update_HNA(orig_node, hna_buff, hna_buff_len);
}

static int isBidirectionalNeigh(struct orig_node *orig_node,
				struct orig_node *orig_neigh_node,
				struct batman_packet *batman_packet,
				struct batman_if *if_incoming)
{
	struct neigh_node *neigh_node = NULL, *tmp_neigh_node = NULL;
	char orig_str[ETH_STR_LEN], neigh_str[ETH_STR_LEN];
	unsigned char total_count;

	addr_to_string(orig_str, orig_node->orig);
	addr_to_string(neigh_str, orig_neigh_node->orig);

	if (orig_node == orig_neigh_node) {
		list_for_each_entry(tmp_neigh_node,
				    &orig_node->neigh_list,
				    list) {

			if (compare_orig(tmp_neigh_node->addr,
					 orig_neigh_node->orig) &&
			    (tmp_neigh_node->if_incoming == if_incoming))
				neigh_node = tmp_neigh_node;
		}

		if (!neigh_node)
			neigh_node = create_neighbor(orig_node,
						     orig_neigh_node,
						     orig_neigh_node->orig,
						     if_incoming);
		/* create_neighbor failed, return 0 */
		if (!neigh_node)
			return 0;

		neigh_node->last_valid = jiffies;
	} else {
		/* find packet count of corresponding one hop neighbor */
		list_for_each_entry(tmp_neigh_node,
				    &orig_neigh_node->neigh_list, list) {

			if (compare_orig(tmp_neigh_node->addr,
					 orig_neigh_node->orig) &&
			    (tmp_neigh_node->if_incoming == if_incoming))
				neigh_node = tmp_neigh_node;
		}

		if (!neigh_node)
			neigh_node = create_neighbor(orig_neigh_node,
						     orig_neigh_node,
						     orig_neigh_node->orig,
						     if_incoming);
		/* create_neighbor failed, return 0 */
		if (!neigh_node)
			return 0;
	}

	orig_node->last_valid = jiffies;

	/* pay attention to not get a value bigger than 100 % */
	total_count = (orig_neigh_node->bcast_own_sum[if_incoming->if_num] >
		       neigh_node->real_packet_count ?
		       neigh_node->real_packet_count :
		       orig_neigh_node->bcast_own_sum[if_incoming->if_num]);

	/* if we have too few packets (too less data) we set tq_own to zero */
	/* if we receive too few packets it is not considered bidirectional */
	if ((total_count < TQ_LOCAL_BIDRECT_SEND_MINIMUM) ||
	    (neigh_node->real_packet_count < TQ_LOCAL_BIDRECT_RECV_MINIMUM))
		orig_neigh_node->tq_own = 0;
	else
		/* neigh_node->real_packet_count is never zero as we
		 * only purge old information when getting new
		 * information */
		orig_neigh_node->tq_own = (TQ_MAX_VALUE * total_count) /
			neigh_node->real_packet_count;

	/*
	 * 1 - ((1-x) ** 3), normalized to TQ_MAX_VALUE this does
	 * affect the nearly-symmetric links only a little, but
	 * punishes asymmetric links more.  This will give a value
	 * between 0 and TQ_MAX_VALUE
	 */
	orig_neigh_node->tq_asym_penalty =
		TQ_MAX_VALUE -
		(TQ_MAX_VALUE *
		 (TQ_LOCAL_WINDOW_SIZE - neigh_node->real_packet_count) *
		 (TQ_LOCAL_WINDOW_SIZE - neigh_node->real_packet_count) *
		 (TQ_LOCAL_WINDOW_SIZE - neigh_node->real_packet_count)) /
		(TQ_LOCAL_WINDOW_SIZE *
		 TQ_LOCAL_WINDOW_SIZE *
		 TQ_LOCAL_WINDOW_SIZE);

	batman_packet->tq = ((batman_packet->tq *
			      orig_neigh_node->tq_own *
			      orig_neigh_node->tq_asym_penalty) /
			     (TQ_MAX_VALUE *	 TQ_MAX_VALUE));

	bat_dbg(DBG_BATMAN, "bidirectional: orig = %-15s neigh = %-15s => own_bcast = %2i, real recv = %2i, local tq: %3i, asym_penalty: %3i, total tq: %3i \n",
		orig_str, neigh_str, total_count,
		neigh_node->real_packet_count, orig_neigh_node->tq_own,
		orig_neigh_node->tq_asym_penalty, batman_packet->tq);

	/* if link has the minimum required transmission quality
	 * consider it bidirectional */
	if (batman_packet->tq >= TQ_TOTAL_BIDRECT_LIMIT)
		return 1;

	return 0;
}

static void update_orig(struct orig_node *orig_node, struct ethhdr *ethhdr,
			struct batman_packet *batman_packet,
			struct batman_if *if_incoming,
			unsigned char *hna_buff, int hna_buff_len,
			char is_duplicate)
{
	struct neigh_node *neigh_node = NULL, *tmp_neigh_node = NULL;
	int tmp_hna_buff_len;

	bat_dbg(DBG_BATMAN, "update_originator(): Searching and updating originator entry of received packet \n");

	list_for_each_entry(tmp_neigh_node, &orig_node->neigh_list, list) {
		if (compare_orig(tmp_neigh_node->addr, ethhdr->h_source) &&
		    (tmp_neigh_node->if_incoming == if_incoming)) {
			neigh_node = tmp_neigh_node;
			continue;
		}

		if (is_duplicate)
			continue;

		ring_buffer_set(tmp_neigh_node->tq_recv,
				&tmp_neigh_node->tq_index, 0);
		tmp_neigh_node->tq_avg =
			ring_buffer_avg(tmp_neigh_node->tq_recv);
	}

	if (!neigh_node) {
		struct orig_node *orig_tmp;

		orig_tmp = get_orig_node(ethhdr->h_source);
		if (!orig_tmp)
			return;

		neigh_node = create_neighbor(orig_node,
					     orig_tmp,
					     ethhdr->h_source, if_incoming);
		if (!neigh_node)
			return;
	} else
		bat_dbg(DBG_BATMAN,
			"Updating existing last-hop neighbor of originator\n");

	orig_node->flags = batman_packet->flags;
	neigh_node->last_valid = jiffies;

	ring_buffer_set(neigh_node->tq_recv,
			&neigh_node->tq_index,
			batman_packet->tq);
	neigh_node->tq_avg = ring_buffer_avg(neigh_node->tq_recv);

	if (!is_duplicate) {
		orig_node->last_ttl = batman_packet->ttl;
		neigh_node->last_ttl = batman_packet->ttl;
	}

	tmp_hna_buff_len = (hna_buff_len > batman_packet->num_hna * ETH_ALEN ?
			    batman_packet->num_hna * ETH_ALEN : hna_buff_len);

	/* if this neighbor already is our next hop there is nothing
	 * to change */
	if (orig_node->router == neigh_node)
		goto update_hna;

	/* if this neighbor does not offer a better TQ we won't consider it */
	if ((orig_node->router) &&
	    (orig_node->router->tq_avg > neigh_node->tq_avg))
		goto update_hna;

	/* if the TQ is the same and the link not more symetric we
	 * won't consider it either */
	if ((orig_node->router) &&
	     ((neigh_node->tq_avg == orig_node->router->tq_avg) &&
	     (orig_node->router->orig_node->bcast_own_sum[if_incoming->if_num]
	      >= neigh_node->orig_node->bcast_own_sum[if_incoming->if_num])))
		goto update_hna;

	update_routes(orig_node, neigh_node, hna_buff, tmp_hna_buff_len);
	return;

update_hna:
	update_routes(orig_node, orig_node->router, hna_buff, tmp_hna_buff_len);
	return;
}

static char count_real_packets(struct ethhdr *ethhdr,
			       struct batman_packet *batman_packet,
			       struct batman_if *if_incoming)
{
	struct orig_node *orig_node;
	struct neigh_node *tmp_neigh_node;
	char is_duplicate = 0;
	uint16_t seq_diff;

	orig_node = get_orig_node(batman_packet->orig);
	if (orig_node == NULL)
		return 0;

	list_for_each_entry(tmp_neigh_node, &orig_node->neigh_list, list) {

		if (!is_duplicate)
			is_duplicate =
				get_bit_status(tmp_neigh_node->real_bits,
					       orig_node->last_real_seqno,
					       batman_packet->seqno);
		seq_diff = batman_packet->seqno - orig_node->last_real_seqno;
		if (compare_orig(tmp_neigh_node->addr, ethhdr->h_source) &&
		    (tmp_neigh_node->if_incoming == if_incoming))
			bit_get_packet(tmp_neigh_node->real_bits, seq_diff, 1);
		else
			bit_get_packet(tmp_neigh_node->real_bits, seq_diff, 0);

		tmp_neigh_node->real_packet_count =
			bit_packet_count(tmp_neigh_node->real_bits);
	}

	if (!is_duplicate) {
		bat_dbg(DBG_BATMAN, "updating last_seqno: old %d, new %d \n",
			orig_node->last_real_seqno, batman_packet->seqno);
		orig_node->last_real_seqno = batman_packet->seqno;
	}

	return is_duplicate;
}

void receive_bat_packet(struct ethhdr *ethhdr,
				struct batman_packet *batman_packet,
				unsigned char *hna_buff, int hna_buff_len,
				struct batman_if *if_incoming)
{
	struct batman_if *batman_if;
	struct orig_node *orig_neigh_node, *orig_node;
	char orig_str[ETH_STR_LEN], prev_sender_str[ETH_STR_LEN];
	char neigh_str[ETH_STR_LEN];
	char has_directlink_flag;
	char is_my_addr = 0, is_my_orig = 0, is_my_oldorig = 0;
	char is_broadcast = 0, is_bidirectional, is_single_hop_neigh;
	char is_duplicate;
	unsigned short if_incoming_seqno;

	/* Silently drop when the batman packet is actually not a
	 * correct packet.
	 *
	 * This might happen if a packet is padded (e.g. Ethernet has a
	 * minimum frame length of 64 byte) and the aggregation interprets
	 * it as an additional length.
	 *
	 * TODO: A more sane solution would be to have a bit in the
	 * batman_packet to detect whether the packet is the last
	 * packet in an aggregation.  Here we expect that the padding
	 * is always zero (or not 0x01)
	 */
	if (batman_packet->packet_type != BAT_PACKET)
		return;

	/* could be changed by schedule_own_packet() */
	if_incoming_seqno = atomic_read(&if_incoming->seqno);

	addr_to_string(orig_str, batman_packet->orig);
	addr_to_string(prev_sender_str, batman_packet->prev_sender);
	addr_to_string(neigh_str, ethhdr->h_source);

	has_directlink_flag = (batman_packet->flags & DIRECTLINK ? 1 : 0);

	is_single_hop_neigh = (compare_orig(ethhdr->h_source,
					    batman_packet->orig) ? 1 : 0);

	bat_dbg(DBG_BATMAN, "Received BATMAN packet via NB: %s, IF: %s [%s] (from OG: %s, via prev OG: %s, seqno %d, tq %d, TTL %d, V %d, IDF %d) \n",
		neigh_str, if_incoming->dev, if_incoming->addr_str,
		orig_str, prev_sender_str, batman_packet->seqno,
		batman_packet->tq, batman_packet->ttl, batman_packet->version,
		has_directlink_flag);

	list_for_each_entry_rcu(batman_if, &if_list, list) {
		if (batman_if->if_active != IF_ACTIVE)
			continue;

		if (compare_orig(ethhdr->h_source,
				 batman_if->net_dev->dev_addr))
			is_my_addr = 1;

		if (compare_orig(batman_packet->orig,
				 batman_if->net_dev->dev_addr))
			is_my_orig = 1;

		if (compare_orig(batman_packet->prev_sender,
				 batman_if->net_dev->dev_addr))
			is_my_oldorig = 1;

		if (compare_orig(ethhdr->h_source, broadcastAddr))
			is_broadcast = 1;
	}

	if (batman_packet->version != COMPAT_VERSION) {
		bat_dbg(DBG_BATMAN,
			"Drop packet: incompatible batman version (%i)\n",
			batman_packet->version);
		return;
	}

	if (is_my_addr) {
		bat_dbg(DBG_BATMAN,
			"Drop packet: received my own broadcast (sender: %s)\n",
			neigh_str);
		return;
	}

	if (is_broadcast) {
		bat_dbg(DBG_BATMAN, "Drop packet: ignoring all packets with broadcast source addr (sender: %s) \n", neigh_str);
		return;
	}

	if (is_my_orig) {
		TYPE_OF_WORD *word;
		int offset;

		orig_neigh_node = get_orig_node(ethhdr->h_source);

		if (!orig_neigh_node)
			return;

		/* neighbor has to indicate direct link and it has to
		 * come via the corresponding interface */
		/* if received seqno equals last send seqno save new
		 * seqno for bidirectional check */
		if (has_directlink_flag &&
		    compare_orig(if_incoming->net_dev->dev_addr,
				 batman_packet->orig) &&
		    (batman_packet->seqno - if_incoming_seqno + 2 == 0)) {
			offset = if_incoming->if_num * NUM_WORDS;
			word = &(orig_neigh_node->bcast_own[offset]);
			bit_mark(word, 0);
			orig_neigh_node->bcast_own_sum[if_incoming->if_num] =
				bit_packet_count(word);
		}

		bat_dbg(DBG_BATMAN, "Drop packet: originator packet from myself (via neighbor) \n");
		return;
	}

	if (batman_packet->tq == 0) {
		count_real_packets(ethhdr, batman_packet, if_incoming);

		bat_dbg(DBG_BATMAN, "Drop packet: originator packet with tq equal 0 \n");
		return;
	}

	if (is_my_oldorig) {
		bat_dbg(DBG_BATMAN, "Drop packet: ignoring all rebroadcast echos (sender: %s) \n", neigh_str);
		return;
	}

	is_duplicate = count_real_packets(ethhdr, batman_packet, if_incoming);

	orig_node = get_orig_node(batman_packet->orig);
	if (orig_node == NULL)
		return;

	/* avoid temporary routing loops */
	if ((orig_node->router) &&
	    (orig_node->router->orig_node->router) &&
	    (compare_orig(orig_node->router->addr,
			  batman_packet->prev_sender)) &&
	    !(compare_orig(batman_packet->orig, batman_packet->prev_sender)) &&
	    (compare_orig(orig_node->router->addr,
			  orig_node->router->orig_node->router->addr))) {
		bat_dbg(DBG_BATMAN, "Drop packet: ignoring all rebroadcast packets that may make me loop (sender: %s) \n", neigh_str);
		return;
	}

	/* if sender is a direct neighbor the sender mac equals
	 * originator mac */
	orig_neigh_node = (is_single_hop_neigh ?
			   orig_node : get_orig_node(ethhdr->h_source));
	if (orig_neigh_node == NULL)
		return;

	/* drop packet if sender is not a direct neighbor and if we
	 * don't route towards it */
	if (!is_single_hop_neigh &&
	    (orig_neigh_node->router == NULL)) {
		bat_dbg(DBG_BATMAN, "Drop packet: OGM via unknown neighbor!\n");
		return;
	}

	is_bidirectional = isBidirectionalNeigh(orig_node, orig_neigh_node,
						batman_packet, if_incoming);

	/* update ranking if it is not a duplicate or has the same
	 * seqno and similar ttl as the non-duplicate */
	if (is_bidirectional &&
	    (!is_duplicate ||
	     ((orig_node->last_real_seqno == batman_packet->seqno) &&
	      (orig_node->last_ttl - 3 <= batman_packet->ttl))))
		update_orig(orig_node, ethhdr, batman_packet,
			    if_incoming, hna_buff, hna_buff_len, is_duplicate);

	/* is single hop (direct) neighbor */
	if (is_single_hop_neigh) {

		/* mark direct link on incoming interface */
		schedule_forward_packet(orig_node, ethhdr, batman_packet,
					1, hna_buff_len, if_incoming);

		bat_dbg(DBG_BATMAN, "Forwarding packet: rebroadcast neighbor packet with direct link flag\n");
		return;
	}

	/* multihop originator */
	if (!is_bidirectional) {
		bat_dbg(DBG_BATMAN,
			"Drop packet: not received via bidirectional link\n");
		return;
	}

	if (is_duplicate) {
		bat_dbg(DBG_BATMAN, "Drop packet: duplicate packet received\n");
		return;
	}

	bat_dbg(DBG_BATMAN,
		"Forwarding packet: rebroadcast originator packet\n");
	schedule_forward_packet(orig_node, ethhdr, batman_packet,
				0, hna_buff_len, if_incoming);
}

int recv_bat_packet(struct sk_buff *skb,
				struct batman_if *batman_if)
{
	struct ethhdr *ethhdr;
	unsigned long flags;

	/* drop packet if it has not necessary minimum size */
	if (skb_headlen(skb) < sizeof(struct batman_packet))
		return NET_RX_DROP;

	ethhdr = (struct ethhdr *)skb_mac_header(skb);

	/* packet with broadcast indication but unicast recipient */
	if (!is_bcast(ethhdr->h_dest))
		return NET_RX_DROP;

	/* packet with broadcast sender address */
	if (is_bcast(ethhdr->h_source))
		return NET_RX_DROP;

	spin_lock_irqsave(&orig_hash_lock, flags);
	/* TODO: we use headlen instead of "length", because
	 * only this data is paged in. */
	/* TODO: is another skb_copy needed here? there will be
	 * written on the data, but nobody (?) should further use
	 * this data */
	receive_aggr_bat_packet(ethhdr,
				skb->data,
				skb_headlen(skb),
				batman_if);
	spin_unlock_irqrestore(&orig_hash_lock, flags);

	kfree_skb(skb);
	return NET_RX_SUCCESS;
}

static int recv_my_icmp_packet(struct sk_buff *skb)
{
	struct orig_node *orig_node;
	struct icmp_packet *icmp_packet;
	struct ethhdr *ethhdr;
	struct sk_buff *skb_old;
	struct batman_if *batman_if;
	int ret;
	unsigned long flags;
	uint8_t dstaddr[ETH_ALEN];

	icmp_packet = (struct icmp_packet *) skb->data;
	ethhdr = (struct ethhdr *) skb_mac_header(skb);

	/* add data to device queue */
	if (icmp_packet->msg_type != ECHO_REQUEST) {
		bat_device_receive_packet(icmp_packet);
		return NET_RX_DROP;
	}

	/* answer echo request (ping) */
	/* get routing information */
	spin_lock_irqsave(&orig_hash_lock, flags);
	orig_node = ((struct orig_node *)hash_find(orig_hash,
						   icmp_packet->orig));
	ret = NET_RX_DROP;

	if ((orig_node != NULL) &&
	    (orig_node->batman_if != NULL) &&
	    (orig_node->router != NULL)) {

		/* don't lock while sending the packets ... we therefore
		 * copy the required data before sending */
		batman_if = orig_node->batman_if;
		memcpy(dstaddr, orig_node->router->addr, ETH_ALEN);
		spin_unlock_irqrestore(&orig_hash_lock, flags);

		/* create a copy of the skb, if needed, to modify it. */
		skb_old = NULL;
		if (!skb_clone_writable(skb, sizeof(struct icmp_packet))) {
			skb_old = skb;
			skb = skb_copy(skb, GFP_ATOMIC);
			if (!skb)
				return NET_RX_DROP;
			icmp_packet = (struct icmp_packet *) skb->data;
			kfree_skb(skb_old);
		}

		memcpy(icmp_packet->dst, icmp_packet->orig, ETH_ALEN);
		memcpy(icmp_packet->orig, ethhdr->h_dest, ETH_ALEN);
		icmp_packet->msg_type = ECHO_REPLY;
		icmp_packet->ttl = TTL;

		send_skb_packet(skb, batman_if, dstaddr);
		ret = NET_RX_SUCCESS;

	} else
		spin_unlock_irqrestore(&orig_hash_lock, flags);

	return ret;
}

static int recv_icmp_ttl_exceeded(struct sk_buff *skb)
{
	unsigned char src_str[ETH_STR_LEN], dst_str[ETH_STR_LEN];
	struct orig_node *orig_node;
	struct icmp_packet *icmp_packet;
	struct ethhdr *ethhdr;
	struct sk_buff *skb_old;
	struct batman_if *batman_if;
	int ret;
	unsigned long flags;
	uint8_t dstaddr[ETH_ALEN];

	icmp_packet = (struct icmp_packet *) skb->data;
	ethhdr = (struct ethhdr *) skb_mac_header(skb);

	addr_to_string(src_str, icmp_packet->orig);
	addr_to_string(dst_str, icmp_packet->dst);

	printk(KERN_WARNING "batman-adv:Warning - can't send packet from %s to %s: ttl exceeded\n", src_str, dst_str);

	/* send TTL exceeded if packet is an echo request (traceroute) */
	if (icmp_packet->msg_type != ECHO_REQUEST)
		return NET_RX_DROP;

	/* get routing information */
	spin_lock_irqsave(&orig_hash_lock, flags);
	orig_node = ((struct orig_node *)
		     hash_find(orig_hash, icmp_packet->orig));
	ret = NET_RX_DROP;

	if ((orig_node != NULL) &&
	    (orig_node->batman_if != NULL) &&
	    (orig_node->router != NULL)) {

		/* don't lock while sending the packets ... we therefore
		 * copy the required data before sending */
		batman_if = orig_node->batman_if;
		memcpy(dstaddr, orig_node->router->addr, ETH_ALEN);
		spin_unlock_irqrestore(&orig_hash_lock, flags);

		/* create a copy of the skb, if needed, to modify it. */
		if (!skb_clone_writable(skb, sizeof(struct icmp_packet))) {
			skb_old = skb;
			skb = skb_copy(skb, GFP_ATOMIC);
			if (!skb)
				return NET_RX_DROP;
			icmp_packet = (struct icmp_packet *) skb->data;
			kfree_skb(skb_old);
		}

		memcpy(icmp_packet->dst, icmp_packet->orig, ETH_ALEN);
		memcpy(icmp_packet->orig, ethhdr->h_dest, ETH_ALEN);
		icmp_packet->msg_type = TTL_EXCEEDED;
		icmp_packet->ttl = TTL;

		send_skb_packet(skb, batman_if, dstaddr);
		ret = NET_RX_SUCCESS;

	} else
		spin_unlock_irqrestore(&orig_hash_lock, flags);

	return ret;
}


int recv_icmp_packet(struct sk_buff *skb)
{
	struct icmp_packet *icmp_packet;
	struct ethhdr *ethhdr;
	struct orig_node *orig_node;
	struct sk_buff *skb_old;
	struct batman_if *batman_if;
	int hdr_size = sizeof(struct icmp_packet);
	int ret;
	unsigned long flags;
	uint8_t dstaddr[ETH_ALEN];

	/* drop packet if it has not necessary minimum size */
	if (skb_headlen(skb) < hdr_size)
		return NET_RX_DROP;

	ethhdr = (struct ethhdr *)skb_mac_header(skb);

	/* packet with unicast indication but broadcast recipient */
	if (is_bcast(ethhdr->h_dest))
		return NET_RX_DROP;

	/* packet with broadcast sender address */
	if (is_bcast(ethhdr->h_source))
		return NET_RX_DROP;

	/* not for me */
	if (!is_my_mac(ethhdr->h_dest))
		return NET_RX_DROP;

	icmp_packet = (struct icmp_packet *) skb->data;

	/* packet for me */
	if (is_my_mac(icmp_packet->dst))
		return recv_my_icmp_packet(skb);

	/* TTL exceeded */
	if (icmp_packet->ttl < 2)
		return recv_icmp_ttl_exceeded(skb);

	ret = NET_RX_DROP;

	/* get routing information */
	spin_lock_irqsave(&orig_hash_lock, flags);
	orig_node = ((struct orig_node *)
		     hash_find(orig_hash, icmp_packet->dst));

	if ((orig_node != NULL) &&
	    (orig_node->batman_if != NULL) &&
	    (orig_node->router != NULL)) {

		/* don't lock while sending the packets ... we therefore
		 * copy the required data before sending */
		batman_if = orig_node->batman_if;
		memcpy(dstaddr, orig_node->router->addr, ETH_ALEN);
		spin_unlock_irqrestore(&orig_hash_lock, flags);

		/* create a copy of the skb, if needed, to modify it. */
		if (!skb_clone_writable(skb, sizeof(struct icmp_packet))) {
			skb_old = skb;
			skb = skb_copy(skb, GFP_ATOMIC);
			if (!skb)
				return NET_RX_DROP;
			icmp_packet = (struct icmp_packet *) skb->data;
			kfree_skb(skb_old);
		}

		/* decrement ttl */
		icmp_packet->ttl--;

		/* route it */
		send_skb_packet(skb, batman_if, dstaddr);
		ret = NET_RX_SUCCESS;

	} else
		spin_unlock_irqrestore(&orig_hash_lock, flags);

	return ret;
}

int recv_unicast_packet(struct sk_buff *skb)
{
	struct unicast_packet *unicast_packet;
	unsigned char src_str[ETH_STR_LEN], dst_str[ETH_STR_LEN];
	struct orig_node *orig_node;
	struct ethhdr *ethhdr;
	struct batman_if *batman_if;
	struct sk_buff *skb_old;
	uint8_t dstaddr[ETH_ALEN];
	int hdr_size = sizeof(struct unicast_packet);
	int ret;
	unsigned long flags;

	/* drop packet if it has not necessary minimum size */
	if (skb_headlen(skb) < hdr_size)
		return NET_RX_DROP;

	ethhdr = (struct ethhdr *) skb_mac_header(skb);

	/* packet with unicast indication but broadcast recipient */
	if (is_bcast(ethhdr->h_dest))
		return NET_RX_DROP;

	/* packet with broadcast sender address */
	if (is_bcast(ethhdr->h_source))
		return NET_RX_DROP;

	/* not for me */
	if (!is_my_mac(ethhdr->h_dest))
		return NET_RX_DROP;

	unicast_packet = (struct unicast_packet *) skb->data;

	/* packet for me */
	if (is_my_mac(unicast_packet->dest)) {
		interface_rx(skb, hdr_size);
		return NET_RX_SUCCESS;
	}

	/* TTL exceeded */
	if (unicast_packet->ttl < 2) {
		addr_to_string(src_str, ethhdr->h_source);
		addr_to_string(dst_str, ethhdr->h_dest);

		printk(KERN_WARNING "batman-adv:Warning - can't send packet from %s to %s: ttl exceeded\n", src_str, dst_str);
		return NET_RX_DROP;
	}

	ret = NET_RX_DROP;
	/* get routing information */
	spin_lock_irqsave(&orig_hash_lock, flags);
	orig_node = ((struct orig_node *)
		     hash_find(orig_hash, unicast_packet->dest));

	if ((orig_node != NULL) &&
	    (orig_node->batman_if != NULL) &&
	    (orig_node->router != NULL)) {

		/* don't lock while sending the packets ... we therefore
		 * copy the required data before sending */
		batman_if = orig_node->batman_if;
		memcpy(dstaddr, orig_node->router->addr, ETH_ALEN);
		spin_unlock_irqrestore(&orig_hash_lock, flags);

		/* create a copy of the skb, if needed, to modify it. */
		if (!skb_clone_writable(skb, sizeof(struct unicast_packet))) {
			skb_old = skb;
			skb = skb_copy(skb, GFP_ATOMIC);
			if (!skb)
				return NET_RX_DROP;
			unicast_packet = (struct unicast_packet *) skb->data;
			kfree_skb(skb_old);
		}
		/* decrement ttl */
		unicast_packet->ttl--;

		/* route it */
		send_skb_packet(skb, batman_if, dstaddr);
		ret = NET_RX_SUCCESS;

	} else
		spin_unlock_irqrestore(&orig_hash_lock, flags);

	return ret;
}


int recv_bcast_packet(struct sk_buff *skb)
{
	struct orig_node *orig_node;
	struct bcast_packet *bcast_packet;
	struct ethhdr *ethhdr;
	int hdr_size = sizeof(struct bcast_packet);
	unsigned long flags;

	/* drop packet if it has not necessary minimum size */
	if (skb_headlen(skb) < hdr_size)
		return NET_RX_DROP;

	ethhdr = (struct ethhdr *)skb_mac_header(skb);

	/* packet with broadcast indication but unicast recipient */
	if (!is_bcast(ethhdr->h_dest))
		return NET_RX_DROP;

	/* packet with broadcast sender address */
	if (is_bcast(ethhdr->h_source))
		return NET_RX_DROP;

	/* ignore broadcasts sent by myself */
	if (is_my_mac(ethhdr->h_source))
		return NET_RX_DROP;

	bcast_packet = (struct bcast_packet *) skb->data;

	/* ignore broadcasts originated by myself */
	if (is_my_mac(bcast_packet->orig))
		return NET_RX_DROP;

	spin_lock_irqsave(&orig_hash_lock, flags);
	orig_node = ((struct orig_node *)
		     hash_find(orig_hash, bcast_packet->orig));

	if (orig_node == NULL) {
		spin_unlock_irqrestore(&orig_hash_lock, flags);
		return NET_RX_DROP;
	}

	/* check flood history */
	if (get_bit_status(orig_node->bcast_bits,
			   orig_node->last_bcast_seqno,
			   ntohs(bcast_packet->seqno))) {
		spin_unlock_irqrestore(&orig_hash_lock, flags);
		return NET_RX_DROP;
	}

	/* mark broadcast in flood history */
	if (bit_get_packet(orig_node->bcast_bits,
			   ntohs(bcast_packet->seqno) -
			   orig_node->last_bcast_seqno, 1))
		orig_node->last_bcast_seqno = ntohs(bcast_packet->seqno);

	spin_unlock_irqrestore(&orig_hash_lock, flags);

	/* rebroadcast packet */
	add_bcast_packet_to_list(skb);

	/* broadcast for me */
	interface_rx(skb, hdr_size);

	return NET_RX_SUCCESS;
}

int recv_vis_packet(struct sk_buff *skb)
{
	struct vis_packet *vis_packet;
	struct ethhdr *ethhdr;
	int hdr_size = sizeof(struct vis_packet);
	int ret;

	if (skb_headlen(skb) < hdr_size)
		return NET_RX_DROP;

	vis_packet = (struct vis_packet *) skb->data;
	ethhdr = (struct ethhdr *)skb_mac_header(skb);

	/* not for me */
	if (!is_my_mac(ethhdr->h_dest))
		return NET_RX_DROP;

	/* ignore own packets */
	if (is_my_mac(vis_packet->vis_orig))
		return NET_RX_DROP;

	if (is_my_mac(vis_packet->sender_orig))
		return NET_RX_DROP;

	switch (vis_packet->vis_type) {
	case VIS_TYPE_SERVER_SYNC:
		/* TODO: handle fragmented skbs properly */
		receive_server_sync_packet(vis_packet, skb_headlen(skb));
		ret = NET_RX_SUCCESS;
		break;

	case VIS_TYPE_CLIENT_UPDATE:
		/* TODO: handle fragmented skbs properly */
		receive_client_update_packet(vis_packet, skb_headlen(skb));
		ret = NET_RX_SUCCESS;
		break;

	default:	/* ignore unknown packet */
		ret = NET_RX_DROP;
		break;
	}
	return ret;
}

