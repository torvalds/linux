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
#include "log.h"
#include "send.h"
#include "soft-interface.h"
#include "hard-interface.h"
#include "device.h"
#include "translation-table.h"
#include "types.h"
#include "hash.h"
#include "ring_buffer.h"
#include "vis.h"
#include "aggregation.h"
#include "compat.h"



DECLARE_WAIT_QUEUE_HEAD(thread_wait);
static DECLARE_DELAYED_WORK(purge_orig_wq, purge_orig);

static atomic_t data_ready_cond;
atomic_t exit_cond;

static void start_purge_timer(void)
{
	queue_delayed_work(bat_event_workqueue, &purge_orig_wq, 1 * HZ);
}

int originator_init(void)
{
	if (orig_hash)
		return 1;

	spin_lock(&orig_hash_lock);
	orig_hash = hash_new(128, compare_orig, choose_orig);

	if (!orig_hash)
		goto err;

	spin_unlock(&orig_hash_lock);
	start_purge_timer();
	return 1;

err:
	spin_unlock(&orig_hash_lock);
	return 0;
}

void originator_free(void)
{
	if (!orig_hash)
		return;

	cancel_delayed_work_sync(&purge_orig_wq);

	spin_lock(&orig_hash_lock);
	hash_delete(orig_hash, free_orig_node);
	orig_hash = NULL;
	spin_unlock(&orig_hash_lock);
}

static struct neigh_node *create_neighbor(struct orig_node *orig_node, struct orig_node *orig_neigh_node, uint8_t *neigh, struct batman_if *if_incoming)
{
	struct neigh_node *neigh_node;

	debug_log(LOG_TYPE_BATMAN, "Creating new last-hop neighbour of originator\n");

	neigh_node = kmalloc(sizeof(struct neigh_node), GFP_ATOMIC);
	memset(neigh_node, 0, sizeof(struct neigh_node));
	INIT_LIST_HEAD(&neigh_node->list);

	memcpy(neigh_node->addr, neigh, ETH_ALEN);
	neigh_node->orig_node = orig_neigh_node;
	neigh_node->if_incoming = if_incoming;

	list_add_tail(&neigh_node->list, &orig_node->neigh_list);
	return neigh_node;
}

void free_orig_node(void *data)
{
	struct list_head *list_pos, *list_pos_tmp;
	struct neigh_node *neigh_node;
	struct orig_node *orig_node = (struct orig_node *)data;

	/* for all neighbours towards this originator ... */
	list_for_each_safe(list_pos, list_pos_tmp, &orig_node->neigh_list) {
		neigh_node = list_entry(list_pos, struct neigh_node, list);

		list_del(list_pos);
		kfree(neigh_node);
	}

	hna_global_del_orig(orig_node, "originator timed out");

	kfree(orig_node->bcast_own);
	kfree(orig_node->bcast_own_sum);
	kfree(orig_node);
}

/* this function finds or creates an originator entry for the given address if it does not exits */
static struct orig_node *get_orig_node(uint8_t *addr)
{
	struct orig_node *orig_node;
	struct hashtable_t *swaphash;
	char orig_str[ETH_STR_LEN];

	orig_node = ((struct orig_node *)hash_find(orig_hash, addr));

	if (orig_node != NULL)
		return orig_node;

	addr_to_string(orig_str, addr);
	debug_log(LOG_TYPE_BATMAN, "Creating new originator: %s \n", orig_str);

	orig_node = kmalloc(sizeof(struct orig_node), GFP_ATOMIC);
	memset(orig_node, 0, sizeof(struct orig_node));
	INIT_LIST_HEAD(&orig_node->neigh_list);

	memcpy(orig_node->orig, addr, ETH_ALEN);
	orig_node->router = NULL;
	orig_node->batman_if = NULL;
	orig_node->hna_buff = NULL;

	orig_node->bcast_own = kmalloc(num_ifs * sizeof(TYPE_OF_WORD) * NUM_WORDS, GFP_ATOMIC);
	memset(orig_node->bcast_own, 0, num_ifs * sizeof(TYPE_OF_WORD) * NUM_WORDS);

	orig_node->bcast_own_sum = kmalloc(num_ifs * sizeof(uint8_t), GFP_ATOMIC);
	memset(orig_node->bcast_own_sum, 0, num_ifs * sizeof(uint8_t));

	hash_add(orig_hash, orig_node);

	if (orig_hash->elements * 4 > orig_hash->size) {
		swaphash = hash_resize(orig_hash, orig_hash->size * 2);

		if (swaphash == NULL)
			debug_log(LOG_TYPE_CRIT, "Couldn't resize orig hash table \n");
		else
			orig_hash = swaphash;
	}

	return orig_node;
}

void slide_own_bcast_window(struct batman_if *batman_if)
{
	struct hash_it_t *hashit = NULL;
	struct orig_node *orig_node;

	spin_lock(&orig_hash_lock);

	while (NULL != (hashit = hash_iterate(orig_hash, hashit))) {
		orig_node = hashit->bucket->data;

		bit_get_packet((TYPE_OF_WORD *)&(orig_node->bcast_own[batman_if->if_num * NUM_WORDS]), 1, 0);
		orig_node->bcast_own_sum[batman_if->if_num] = bit_packet_count((TYPE_OF_WORD *)&(orig_node->bcast_own[batman_if->if_num * NUM_WORDS]));
	}

	spin_unlock(&orig_hash_lock);
}

static void update_routes(struct orig_node *orig_node, struct neigh_node *neigh_node, unsigned char *hna_buff, int hna_buff_len)
{
	char orig_str[ETH_STR_LEN], neigh_str[ETH_STR_LEN], router_str[ETH_STR_LEN];

	if (orig_node == NULL)
		return;

	if (orig_node->router != neigh_node) {
		addr_to_string(orig_str, orig_node->orig);

		/* route deleted */
		if ((orig_node->router != NULL) && (neigh_node == NULL)) {

			debug_log(LOG_TYPE_ROUTES, "Deleting route towards: %s\n", orig_str);
			hna_global_del_orig(orig_node, "originator timed out");

		/* route added */
		} else if ((orig_node->router == NULL) && (neigh_node != NULL)) {

			addr_to_string(neigh_str, neigh_node->addr);
			debug_log(LOG_TYPE_ROUTES, "Adding route towards: %s (via %s)\n", orig_str, neigh_str);
			hna_global_add_orig(orig_node, hna_buff, hna_buff_len);

		/* route changed */
		} else {

			addr_to_string(neigh_str, neigh_node->addr);
			addr_to_string(router_str, orig_node->router->addr);
			debug_log(LOG_TYPE_ROUTES, "Changing route towards: %s (now via %s - was via %s)\n", orig_str, neigh_str, router_str);

		}

		if (neigh_node != NULL)
			orig_node->batman_if = neigh_node->if_incoming;
		else
			orig_node->batman_if = NULL;

		orig_node->router = neigh_node;

	/* may be just HNA changed */
	} else {

		if ((hna_buff_len != orig_node->hna_buff_len) || ((hna_buff_len > 0) && (orig_node->hna_buff_len > 0) && (memcmp(orig_node->hna_buff, hna_buff, hna_buff_len) != 0))) {

			if (orig_node->hna_buff_len > 0)
				hna_global_del_orig(orig_node, "originator changed hna");

			if ((hna_buff_len > 0) && (hna_buff != NULL))
				hna_global_add_orig(orig_node, hna_buff, hna_buff_len);

		}

	}
}

static int isBidirectionalNeigh(struct orig_node *orig_node, struct orig_node *orig_neigh_node, struct batman_packet *batman_packet, struct batman_if *if_incoming)
{
	struct neigh_node *neigh_node = NULL, *tmp_neigh_node = NULL;
	char orig_str[ETH_STR_LEN], neigh_str[ETH_STR_LEN];
	unsigned char total_count;

	addr_to_string(orig_str, orig_node->orig);
	addr_to_string(neigh_str, orig_neigh_node->orig);

	if (orig_node == orig_neigh_node) {
		list_for_each_entry(tmp_neigh_node, &orig_node->neigh_list, list) {

			if (compare_orig(tmp_neigh_node->addr, orig_neigh_node->orig) && (tmp_neigh_node->if_incoming == if_incoming))
				neigh_node = tmp_neigh_node;
		}

		if (neigh_node == NULL)
			neigh_node = create_neighbor(orig_node, orig_neigh_node, orig_neigh_node->orig, if_incoming);

		neigh_node->last_valid = jiffies;
	} else {
		/* find packet count of corresponding one hop neighbor */
		list_for_each_entry(tmp_neigh_node, &orig_neigh_node->neigh_list, list) {

			if (compare_orig(tmp_neigh_node->addr, orig_neigh_node->orig) && (tmp_neigh_node->if_incoming == if_incoming))
				neigh_node = tmp_neigh_node;
		}

		if (neigh_node == NULL)
			neigh_node = create_neighbor(orig_neigh_node, orig_neigh_node, orig_neigh_node->orig, if_incoming);
	}

	orig_node->last_valid = jiffies;

	/* pay attention to not get a value bigger than 100 % */
	total_count = (orig_neigh_node->bcast_own_sum[if_incoming->if_num] > neigh_node->real_packet_count ? neigh_node->real_packet_count : orig_neigh_node->bcast_own_sum[if_incoming->if_num]);

	/* if we have too few packets (too less data) we set tq_own to zero */
	/* if we receive too few packets it is not considered bidirectional */
	if ((total_count < TQ_LOCAL_BIDRECT_SEND_MINIMUM) || (neigh_node->real_packet_count < TQ_LOCAL_BIDRECT_RECV_MINIMUM))
		orig_neigh_node->tq_own = 0;
	else
		/* neigh_node->real_packet_count is never zero as we only purge old information when getting new information */
		orig_neigh_node->tq_own = (TQ_MAX_VALUE * total_count) / neigh_node->real_packet_count;

	/*
	 * 1 - ((1-x) ** 3), normalized to TQ_MAX_VALUE
	 * this does affect the nearly-symmetric links only a little,
	 * but punishes asymmetric links more.
	 * this will give a value between 0 and TQ_MAX_VALUE
	 */
	orig_neigh_node->tq_asym_penalty = TQ_MAX_VALUE - (TQ_MAX_VALUE *
			(TQ_LOCAL_WINDOW_SIZE - neigh_node->real_packet_count) *
			(TQ_LOCAL_WINDOW_SIZE - neigh_node->real_packet_count) *
			(TQ_LOCAL_WINDOW_SIZE - neigh_node->real_packet_count)) /
			(TQ_LOCAL_WINDOW_SIZE * TQ_LOCAL_WINDOW_SIZE * TQ_LOCAL_WINDOW_SIZE);

	batman_packet->tq = ((batman_packet->tq * orig_neigh_node->tq_own * orig_neigh_node->tq_asym_penalty) / (TQ_MAX_VALUE *  TQ_MAX_VALUE));

	debug_log(LOG_TYPE_BATMAN, "bidirectional: orig = %-15s neigh = %-15s => own_bcast = %2i, real recv = %2i, local tq: %3i, asym_penalty: %3i, total tq: %3i \n",
		  orig_str, neigh_str, total_count, neigh_node->real_packet_count, orig_neigh_node->tq_own, orig_neigh_node->tq_asym_penalty, batman_packet->tq);

	/* if link has the minimum required transmission quality consider it bidirectional */
	if (batman_packet->tq >= TQ_TOTAL_BIDRECT_LIMIT)
		return 1;

	return 0;
}

static void update_orig(struct orig_node *orig_node, struct ethhdr *ethhdr, struct batman_packet *batman_packet, struct batman_if *if_incoming, unsigned char *hna_buff, int hna_buff_len, char is_duplicate)
{
	struct neigh_node *neigh_node = NULL, *tmp_neigh_node = NULL;
	int tmp_hna_buff_len;

	debug_log(LOG_TYPE_BATMAN, "update_originator(): Searching and updating originator entry of received packet \n");

	list_for_each_entry(tmp_neigh_node, &orig_node->neigh_list, list) {
		if (compare_orig(tmp_neigh_node->addr, ethhdr->h_source) && (tmp_neigh_node->if_incoming == if_incoming)) {
			neigh_node = tmp_neigh_node;
			continue;
		}

		if (is_duplicate)
			continue;

		ring_buffer_set(tmp_neigh_node->tq_recv, &tmp_neigh_node->tq_index, 0);
		tmp_neigh_node->tq_avg = ring_buffer_avg(tmp_neigh_node->tq_recv);
	}

	if (neigh_node == NULL)
		neigh_node = create_neighbor(orig_node, get_orig_node(ethhdr->h_source), ethhdr->h_source, if_incoming);
	else
		debug_log(LOG_TYPE_BATMAN, "Updating existing last-hop neighbour of originator\n");

	orig_node->flags = batman_packet->flags;
	neigh_node->last_valid = jiffies;

	ring_buffer_set(neigh_node->tq_recv, &neigh_node->tq_index, batman_packet->tq);
	neigh_node->tq_avg = ring_buffer_avg(neigh_node->tq_recv);

	if (!is_duplicate) {
		orig_node->last_ttl = batman_packet->ttl;
		neigh_node->last_ttl = batman_packet->ttl;
	}

	tmp_hna_buff_len = (hna_buff_len > batman_packet->num_hna * ETH_ALEN ? batman_packet->num_hna * ETH_ALEN : hna_buff_len);

	/* if this neighbor already is our next hop there is nothing to change */
	if (orig_node->router == neigh_node)
		goto update_hna;

	/* if this neighbor does not offer a better TQ we won't consider it */
	if ((orig_node->router) &&
	    (orig_node->router->tq_avg > neigh_node->tq_avg))
		goto update_hna;

	/* if the TQ is the same and the link not more symetric we won't consider it either */
	if ((orig_node->router) &&
	     ((neigh_node->tq_avg == orig_node->router->tq_avg) &&
	     (orig_node->router->orig_node->bcast_own_sum[if_incoming->if_num] >=
	      neigh_node->orig_node->bcast_own_sum[if_incoming->if_num])))
		goto update_hna;

	update_routes(orig_node, neigh_node, hna_buff, tmp_hna_buff_len);
	return;

update_hna:
	update_routes(orig_node, orig_node->router, hna_buff, tmp_hna_buff_len);
	return;
}

static char count_real_packets(struct ethhdr *ethhdr, struct batman_packet *batman_packet, struct batman_if *if_incoming)
{
	struct orig_node *orig_node;
	struct neigh_node *tmp_neigh_node;
	char is_duplicate = 0;


	orig_node = get_orig_node(batman_packet->orig);
	if (orig_node == NULL)
		return 0;


	list_for_each_entry(tmp_neigh_node, &orig_node->neigh_list, list) {

		if (!is_duplicate)
			is_duplicate = get_bit_status(tmp_neigh_node->real_bits, orig_node->last_real_seqno, batman_packet->seqno);

		if (compare_orig(tmp_neigh_node->addr, ethhdr->h_source) && (tmp_neigh_node->if_incoming == if_incoming))
			bit_get_packet(tmp_neigh_node->real_bits, batman_packet->seqno - orig_node->last_real_seqno, 1);
		else
			bit_get_packet(tmp_neigh_node->real_bits, batman_packet->seqno - orig_node->last_real_seqno, 0);

		tmp_neigh_node->real_packet_count = bit_packet_count(tmp_neigh_node->real_bits);
	}

	if (!is_duplicate) {
		debug_log(LOG_TYPE_BATMAN, "updating last_seqno: old %d, new %d \n", orig_node->last_real_seqno, batman_packet->seqno);
		orig_node->last_real_seqno = batman_packet->seqno;
	}

	return is_duplicate;
}

void receive_bat_packet(struct ethhdr *ethhdr, struct batman_packet *batman_packet, unsigned char *hna_buff, int hna_buff_len, struct batman_if *if_incoming)
{
	struct batman_if *batman_if;
	struct orig_node *orig_neigh_node, *orig_node;
	char orig_str[ETH_STR_LEN], prev_sender_str[ETH_STR_LEN], neigh_str[ETH_STR_LEN];
	char has_directlink_flag;
	char is_my_addr = 0, is_my_orig = 0, is_my_oldorig = 0, is_broadcast = 0, is_bidirectional, is_single_hop_neigh, is_duplicate;
	unsigned short if_incoming_seqno;

	/* Silently drop when the batman packet is actually not a correct packet.
	 *
	 * This might happen if a packet is padded (e.g. Ethernet has a
	 * minimum frame length of 64 byte) and the aggregation interprets
	 * it as an additional length.
	 *
	 * TODO: A more sane solution would be to have a bit in the batman_packet
	 * to detect whether the packet is the last packet in an aggregation.
	 * Here we expect that the padding is always zero (or not 0x01)
	 */
	if (batman_packet->packet_type != BAT_PACKET)
		return;

	/* could be changed by schedule_own_packet() */
	if_incoming_seqno = atomic_read(&if_incoming->seqno);

	addr_to_string(orig_str, batman_packet->orig);
	addr_to_string(prev_sender_str, batman_packet->prev_sender);
	addr_to_string(neigh_str, ethhdr->h_source);

	has_directlink_flag = (batman_packet->flags & DIRECTLINK ? 1 : 0);

	is_single_hop_neigh = (compare_orig(ethhdr->h_source, batman_packet->orig) ? 1 : 0);

	debug_log(LOG_TYPE_BATMAN, "Received BATMAN packet via NB: %s, IF: %s [%s] (from OG: %s, via prev OG: %s, seqno %d, tq %d, TTL %d, V %d, IDF %d) \n", neigh_str, if_incoming->dev, if_incoming->addr_str, orig_str, prev_sender_str, batman_packet->seqno, batman_packet->tq, batman_packet->ttl, batman_packet->version, has_directlink_flag);

	list_for_each_entry_rcu(batman_if, &if_list, list) {
		if (batman_if->if_active != IF_ACTIVE)
			continue;

		if (compare_orig(ethhdr->h_source, batman_if->net_dev->dev_addr))
			is_my_addr = 1;

		if (compare_orig(batman_packet->orig, batman_if->net_dev->dev_addr))
			is_my_orig = 1;

		if (compare_orig(batman_packet->prev_sender, batman_if->net_dev->dev_addr))
			is_my_oldorig = 1;

		if (compare_orig(ethhdr->h_source, broadcastAddr))
			is_broadcast = 1;
	}

	if (batman_packet->version != COMPAT_VERSION) {
		debug_log(LOG_TYPE_BATMAN, "Drop packet: incompatible batman version (%i) \n", batman_packet->version);
		return;
	}

	if (is_my_addr) {
		debug_log(LOG_TYPE_BATMAN, "Drop packet: received my own broadcast (sender: %s) \n", neigh_str);
		return;
	}

	if (is_broadcast) {
		debug_log(LOG_TYPE_BATMAN, "Drop packet: ignoring all packets with broadcast source addr (sender: %s) \n", neigh_str);
		return;
	}

	if (is_my_orig) {
		orig_neigh_node = get_orig_node(ethhdr->h_source);

		/* neighbour has to indicate direct link and it has to come via the corresponding interface */
		/* if received seqno equals last send seqno save new seqno for bidirectional check */
		if (has_directlink_flag && compare_orig(if_incoming->net_dev->dev_addr, batman_packet->orig) &&
				  (batman_packet->seqno - if_incoming_seqno + 2 == 0)) {
			bit_mark((TYPE_OF_WORD *)&(orig_neigh_node->bcast_own[if_incoming->if_num * NUM_WORDS]), 0);
			orig_neigh_node->bcast_own_sum[if_incoming->if_num] = bit_packet_count((TYPE_OF_WORD *)&(orig_neigh_node->bcast_own[if_incoming->if_num * NUM_WORDS]));
		}

		debug_log(LOG_TYPE_BATMAN, "Drop packet: originator packet from myself (via neighbour) \n");
		return;
	}

	if (batman_packet->tq == 0) {
		count_real_packets(ethhdr, batman_packet, if_incoming);

		debug_log(LOG_TYPE_BATMAN, "Drop packet: originator packet with tq equal 0 \n");
		return;
	}

	if (is_my_oldorig) {
		debug_log(LOG_TYPE_BATMAN, "Drop packet: ignoring all rebroadcast echos (sender: %s) \n", neigh_str);
		return;
	}

	is_duplicate = count_real_packets(ethhdr, batman_packet, if_incoming);

	orig_node = get_orig_node(batman_packet->orig);
	if (orig_node == NULL)
		return;

	/* avoid temporary routing loops */
	if ((orig_node->router) && (orig_node->router->orig_node->router) &&
	    (compare_orig(orig_node->router->addr, batman_packet->prev_sender)) &&
	    !(compare_orig(batman_packet->orig, batman_packet->prev_sender)) &&
	    (compare_orig(orig_node->router->addr, orig_node->router->orig_node->router->addr))) {
		debug_log(LOG_TYPE_BATMAN, "Drop packet: ignoring all rebroadcast packets that may make me loop (sender: %s) \n", neigh_str);
		return;
	}

	/* if sender is a direct neighbor the sender mac equals originator mac */
	orig_neigh_node = (is_single_hop_neigh ? orig_node : get_orig_node(ethhdr->h_source));
	if (orig_neigh_node == NULL)
		return;

	/* drop packet if sender is not a direct neighbor and if we don't route towards it */
	if (!is_single_hop_neigh && (orig_neigh_node->router == NULL)) {
		debug_log(LOG_TYPE_BATMAN, "Drop packet: OGM via unknown neighbor! \n");
		return;
	}

	is_bidirectional = isBidirectionalNeigh(orig_node, orig_neigh_node, batman_packet, if_incoming);

	/* update ranking if it is not a duplicate or has the same seqno and similar ttl as the non-duplicate */
	if (is_bidirectional && (!is_duplicate ||
		((orig_node->last_real_seqno == batman_packet->seqno) &&
			(orig_node->last_ttl - 3 <= batman_packet->ttl))))
		update_orig(orig_node, ethhdr, batman_packet, if_incoming, hna_buff, hna_buff_len, is_duplicate);

	/* is single hop (direct) neighbour */
	if (is_single_hop_neigh) {

		/* mark direct link on incoming interface */
		schedule_forward_packet(orig_node, ethhdr, batman_packet, 1, hna_buff_len, if_incoming);

		debug_log(LOG_TYPE_BATMAN, "Forwarding packet: rebroadcast neighbour packet with direct link flag \n");
		return;
	}

	/* multihop originator */
	if (!is_bidirectional) {
		debug_log(LOG_TYPE_BATMAN, "Drop packet: not received via bidirectional link\n");
		return;
	}

	if (is_duplicate) {
		debug_log(LOG_TYPE_BATMAN, "Drop packet: duplicate packet received\n");
		return;
	}

	debug_log(LOG_TYPE_BATMAN, "Forwarding packet: rebroadcast originator packet \n");
	schedule_forward_packet(orig_node, ethhdr, batman_packet, 0, hna_buff_len, if_incoming);
}

void purge_orig(struct work_struct *work)
{
	struct list_head *list_pos, *list_pos_tmp;
	struct hash_it_t *hashit = NULL;
	struct orig_node *orig_node;
	struct neigh_node *neigh_node, *best_neigh_node;
	char orig_str[ETH_STR_LEN], neigh_str[ETH_STR_LEN], neigh_purged;

	spin_lock(&orig_hash_lock);

	/* for all origins... */
	while (NULL != (hashit = hash_iterate(orig_hash, hashit))) {

		orig_node = hashit->bucket->data;
		addr_to_string(orig_str, orig_node->orig);

		if (time_after(jiffies, orig_node->last_valid + ((2 * PURGE_TIMEOUT * HZ) / 1000))) {

			debug_log(LOG_TYPE_BATMAN, "Originator timeout: originator %s, last_valid %u \n", orig_str, (orig_node->last_valid / HZ));

			hash_remove_bucket(orig_hash, hashit);
			free_orig_node(orig_node);

		} else {

			best_neigh_node = NULL;
			neigh_purged = 0;

			/* for all neighbours towards this originator ... */
			list_for_each_safe(list_pos, list_pos_tmp, &orig_node->neigh_list) {
				neigh_node = list_entry(list_pos, struct neigh_node, list);

				if (time_after(jiffies, neigh_node->last_valid + ((PURGE_TIMEOUT * HZ) / 1000))) {

					addr_to_string(neigh_str, neigh_node->addr);
					debug_log(LOG_TYPE_BATMAN, "Neighbour timeout: originator %s, neighbour: %s, last_valid %u \n", orig_str, neigh_str, (neigh_node->last_valid / HZ));

					neigh_purged = 1;
					list_del(list_pos);
					kfree(neigh_node);

				} else {

					if ((best_neigh_node == NULL) || (neigh_node->tq_avg > best_neigh_node->tq_avg))
						best_neigh_node = neigh_node;

				}

			}

			if (neigh_purged)
				update_routes(orig_node, best_neigh_node, orig_node->hna_buff, orig_node->hna_buff_len);

		}

	}

	spin_unlock(&orig_hash_lock);

	start_purge_timer();
}

static int receive_raw_packet(struct socket *raw_sock, unsigned char *packet_buff, int packet_buff_len)
{
	struct kvec iov;
	struct msghdr msg;

	iov.iov_base = packet_buff;
	iov.iov_len = packet_buff_len;

	msg.msg_flags = MSG_DONTWAIT;	/* non-blocking */
	msg.msg_name = NULL;
	msg.msg_namelen = 0;
	msg.msg_control = NULL;

	return kernel_recvmsg(raw_sock, &msg, &iov, 1, packet_buff_len, MSG_DONTWAIT);
}

int packet_recv_thread(void *data)
{
	struct batman_if *batman_if;
	struct ethhdr *ethhdr;
	struct batman_packet *batman_packet;
	struct unicast_packet *unicast_packet;
	struct bcast_packet *bcast_packet;
	struct icmp_packet *icmp_packet;
	struct vis_packet *vis_packet;
	struct orig_node *orig_node;
	unsigned char *packet_buff, src_str[ETH_STR_LEN], dst_str[ETH_STR_LEN];
	int vis_info_len;
	int result;

	atomic_set(&data_ready_cond, 0);
	atomic_set(&exit_cond, 0);
	packet_buff = kmalloc(PACKBUFF_SIZE, GFP_KERNEL);
	if (!packet_buff) {
		debug_log(LOG_TYPE_CRIT, "Could allocate memory for the packet buffer. :(\n");
		return -1;
	}

	while ((!kthread_should_stop()) && (!atomic_read(&exit_cond))) {

		wait_event_interruptible(thread_wait, (atomic_read(&data_ready_cond) || atomic_read(&exit_cond)));

		atomic_set(&data_ready_cond, 0);

		if (kthread_should_stop() || atomic_read(&exit_cond))
			break;

		/* we only want to safely traverse the list, hard-interfaces
		 * won't be deleted anyway as long as this thread runs. */

		rcu_read_lock();
		list_for_each_entry_rcu(batman_if, &if_list, list) {
			rcu_read_unlock();

			result = -1;

			while (1) {
				if (batman_if->if_active != IF_ACTIVE) {
					if (batman_if->if_active != IF_TO_BE_ACTIVATED)
						debug_log(LOG_TYPE_NOTICE,
						          "Could not read from deactivated interface %s!\n",
						          batman_if->dev);

					if (batman_if->raw_sock)
						receive_raw_packet(batman_if->raw_sock, packet_buff, PACKBUFF_SIZE);
					result = 0;
					break;
				}

				result = receive_raw_packet(batman_if->raw_sock, packet_buff, PACKBUFF_SIZE);
				if (result <= 0)
					break;

				if (result < sizeof(struct ethhdr) + 2)
					continue;

				ethhdr = (struct ethhdr *)packet_buff;
				batman_packet = (struct batman_packet *)(packet_buff + sizeof(struct ethhdr));

				if (batman_packet->version != COMPAT_VERSION) {
					debug_log(LOG_TYPE_BATMAN, "Drop packet: incompatible batman version (%i) \n", batman_packet->version);
					continue;
				}

				switch (batman_packet->packet_type) {
				/* batman originator packet */
				case BAT_PACKET:
					/* packet with broadcast indication but unicast recipient */
					if (!is_bcast(ethhdr->h_dest))
						continue;

					/* packet with broadcast sender address */
					if (is_bcast(ethhdr->h_source))
						continue;

					/* drop packet if it has not at least one batman packet as payload */
					if (result < sizeof(struct ethhdr) + sizeof(struct batman_packet))
						continue;

					spin_lock(&orig_hash_lock);
					receive_aggr_bat_packet(ethhdr,
					                        packet_buff + sizeof(struct ethhdr),
					                        result - sizeof(struct ethhdr),
					                        batman_if);
					spin_unlock(&orig_hash_lock);

					break;

				/* batman icmp packet */
				case BAT_ICMP:
					/* packet with unicast indication but broadcast recipient */
					if (is_bcast(ethhdr->h_dest))
						continue;

					/* packet with broadcast sender address */
					if (is_bcast(ethhdr->h_source))
						continue;

					/* not for me */
					if (!is_my_mac(ethhdr->h_dest))
						continue;

					/* drop packet if it has not necessary minimum size */
					if (result < sizeof(struct ethhdr) + sizeof(struct icmp_packet))
						continue;

					icmp_packet = (struct icmp_packet *)(packet_buff + sizeof(struct ethhdr));

					/* packet for me */
					if (is_my_mac(icmp_packet->dst)) {

						/* add data to device queue */
						if (icmp_packet->msg_type != ECHO_REQUEST) {
							bat_device_receive_packet(icmp_packet);
							continue;
						}

						/* answer echo request (ping) */
						/* get routing information */
						spin_lock(&orig_hash_lock);
						orig_node = ((struct orig_node *)hash_find(orig_hash, icmp_packet->orig));

						if ((orig_node != NULL) && (orig_node->batman_if != NULL) && (orig_node->router != NULL)) {

							memcpy(icmp_packet->dst, icmp_packet->orig, ETH_ALEN);
							memcpy(icmp_packet->orig, ethhdr->h_dest, ETH_ALEN);
							icmp_packet->msg_type = ECHO_REPLY;
							icmp_packet->ttl = TTL;

							send_raw_packet(packet_buff + sizeof(struct ethhdr),
							                result - sizeof(struct ethhdr),
							                orig_node->batman_if,
							                orig_node->router->addr);

						}

						spin_unlock(&orig_hash_lock);
						continue;

					}

					/* TTL exceeded */
					if (icmp_packet->ttl < 2) {

						addr_to_string(src_str, icmp_packet->orig);
						addr_to_string(dst_str, icmp_packet->dst);

						debug_log(LOG_TYPE_NOTICE, "Error - can't send packet from %s to %s: ttl exceeded\n", src_str, dst_str);

						/* send TTL exceeded if packet is an echo request (traceroute) */
						if (icmp_packet->msg_type != ECHO_REQUEST)
							continue;

						/* get routing information */
						spin_lock(&orig_hash_lock);
						orig_node = ((struct orig_node *)hash_find(orig_hash, icmp_packet->orig));

						if ((orig_node != NULL) && (orig_node->batman_if != NULL) && (orig_node->router != NULL)) {

							memcpy(icmp_packet->dst, icmp_packet->orig, ETH_ALEN);
							memcpy(icmp_packet->orig, ethhdr->h_dest, ETH_ALEN);
							icmp_packet->msg_type = TTL_EXCEEDED;
							icmp_packet->ttl = TTL;

							send_raw_packet(packet_buff + sizeof(struct ethhdr),
							                result - sizeof(struct ethhdr),
							                orig_node->batman_if,
							                orig_node->router->addr);

						}

						spin_unlock(&orig_hash_lock);
						continue;

					}

					/* get routing information */
					spin_lock(&orig_hash_lock);
					orig_node = ((struct orig_node *)hash_find(orig_hash, icmp_packet->dst));

					if ((orig_node != NULL) && (orig_node->batman_if != NULL) && (orig_node->router != NULL)) {

						/* decrement ttl */
						icmp_packet->ttl--;

						/* route it */
						send_raw_packet(packet_buff + sizeof(struct ethhdr),
						                result - sizeof(struct ethhdr),
						                orig_node->batman_if,
						                orig_node->router->addr);
					}

					spin_unlock(&orig_hash_lock);
					break;

				/* unicast packet */
				case BAT_UNICAST:
					/* packet with unicast indication but broadcast recipient */
					if (is_bcast(ethhdr->h_dest))
						continue;

					/* packet with broadcast sender address */
					if (is_bcast(ethhdr->h_source))
						continue;

					/* not for me */
					if (!is_my_mac(ethhdr->h_dest))
						continue;

					/* drop packet if it has not necessary minimum size */
					if (result < sizeof(struct ethhdr) + sizeof(struct unicast_packet))
						continue;

					unicast_packet = (struct unicast_packet *)(packet_buff + sizeof(struct ethhdr));

					/* packet for me */
					if (is_my_mac(unicast_packet->dest)) {

						interface_rx(soft_device, packet_buff + sizeof(struct ethhdr) + sizeof(struct unicast_packet), result - sizeof(struct ethhdr) - sizeof(struct unicast_packet));
						continue;

					}

					/* TTL exceeded */
					if (unicast_packet->ttl < 2) {
						addr_to_string(src_str, ((struct ethhdr *)(unicast_packet + 1))->h_source);
						addr_to_string(dst_str, unicast_packet->dest);

						debug_log(LOG_TYPE_NOTICE, "Error - can't send packet from %s to %s: ttl exceeded\n", src_str, dst_str);
						continue;
					}

					/* get routing information */
					spin_lock(&orig_hash_lock);
					orig_node = ((struct orig_node *)hash_find(orig_hash, unicast_packet->dest));

					if ((orig_node != NULL) && (orig_node->batman_if != NULL) && (orig_node->router != NULL)) {
						/* decrement ttl */
						unicast_packet->ttl--;

						/* route it */
						send_raw_packet(packet_buff + sizeof(struct ethhdr),
						                result - sizeof(struct ethhdr),
						                orig_node->batman_if,
						                orig_node->router->addr);
					}

					spin_unlock(&orig_hash_lock);
					break;

				/* broadcast packet */
				case BAT_BCAST:
					/* packet with broadcast indication but unicast recipient */
					if (!is_bcast(ethhdr->h_dest))
						continue;

					/* packet with broadcast sender address */
					if (is_bcast(ethhdr->h_source))
						continue;

					/* drop packet if it has not necessary minimum size */
					if (result < sizeof(struct ethhdr) + sizeof(struct bcast_packet))
						continue;

					/* ignore broadcasts sent by myself */
					if (is_my_mac(ethhdr->h_source))
						continue;

					bcast_packet = (struct bcast_packet *)(packet_buff + sizeof(struct ethhdr));

					/* ignore broadcasts originated by myself */
					if (is_my_mac(bcast_packet->orig))
						continue;

					spin_lock(&orig_hash_lock);
					orig_node = ((struct orig_node *)hash_find(orig_hash, bcast_packet->orig));

					if (orig_node == NULL) {
						spin_unlock(&orig_hash_lock);
						continue;
					}

					/* check flood history */
					if (get_bit_status(orig_node->bcast_bits, orig_node->last_bcast_seqno, ntohs(bcast_packet->seqno))) {
						spin_unlock(&orig_hash_lock);
						continue;
					}

					/* mark broadcast in flood history */
					if (bit_get_packet(orig_node->bcast_bits, ntohs(bcast_packet->seqno) - orig_node->last_bcast_seqno, 1))
						orig_node->last_bcast_seqno = ntohs(bcast_packet->seqno);

					spin_unlock(&orig_hash_lock);

					/* broadcast for me */
					interface_rx(soft_device, packet_buff + sizeof(struct ethhdr) + sizeof(struct bcast_packet), result - sizeof(struct ethhdr) - sizeof(struct bcast_packet));

					/* rebroadcast packet */
					add_bcast_packet_to_list(packet_buff + sizeof(struct ethhdr),
									result - sizeof(struct ethhdr));

					break;

				/* vis packet */
				case BAT_VIS:
					/* drop if too short. */
					if (result < sizeof(struct ethhdr) + sizeof(struct vis_packet))
						continue;

					/* not for me */
					if (!is_my_mac(ethhdr->h_dest))
						continue;

					vis_packet = (struct vis_packet *)(packet_buff + sizeof(struct ethhdr));
					vis_info_len = result  - sizeof(struct ethhdr) - sizeof(struct vis_packet);

					/* ignore own packets */
					if (is_my_mac(vis_packet->vis_orig))
						continue;

					if (is_my_mac(vis_packet->sender_orig))
						continue;

					switch (vis_packet->vis_type) {
					case VIS_TYPE_SERVER_SYNC:
						receive_server_sync_packet(vis_packet, vis_info_len);
						break;

					case VIS_TYPE_CLIENT_UPDATE:
						receive_client_update_packet(vis_packet, vis_info_len);
						break;

					default:	/* ignore unknown packet */
						break;
					}

					break;
				}

			}

			if ((result < 0) && (result != -EAGAIN))
				debug_log(LOG_TYPE_CRIT, "Could not receive packet from interface %s: %i\n", batman_if->dev, result);

			/* lock for the next iteration */
			rcu_read_lock();
		}
		rcu_read_unlock();

	}
	kfree(packet_buff);

	/* do not exit until kthread_stop() is actually called, otherwise it will wait for us
	 * forever. */
	while (!kthread_should_stop())
		schedule();

	return 0;
}

void batman_data_ready(struct sock *sk, int len)
{
	void (*data_ready)(struct sock *, int) = sk->sk_user_data;

	data_ready(sk, len);

	atomic_set(&data_ready_cond, 1);
	wake_up_interruptible(&thread_wait);
}

