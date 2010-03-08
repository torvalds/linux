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
#include "send.h"
#include "routing.h"
#include "translation-table.h"
#include "soft-interface.h"
#include "hard-interface.h"
#include "types.h"
#include "vis.h"
#include "aggregation.h"

/* apply hop penalty for a normal link */
static uint8_t hop_penalty(const uint8_t tq)
{
	return (tq * (TQ_MAX_VALUE - TQ_HOP_PENALTY)) / (TQ_MAX_VALUE);
}

/* when do we schedule our own packet to be sent */
static unsigned long own_send_time(void)
{
	return jiffies +
		(((atomic_read(&originator_interval) - JITTER +
		   (random32() % 2*JITTER)) * HZ) / 1000);
}

/* when do we schedule a forwarded packet to be sent */
static unsigned long forward_send_time(void)
{
	unsigned long send_time = jiffies; /* Starting now plus... */

	if (atomic_read(&aggregation_enabled))
		send_time += (((MAX_AGGREGATION_MS - (JITTER/2) +
				(random32() % JITTER)) * HZ) / 1000);
	else
		send_time += (((random32() % (JITTER/2)) * HZ) / 1000);

	return send_time;
}

/* send out an already prepared packet to the given address via the
 * specified batman interface */
int send_skb_packet(struct sk_buff *skb,
				struct batman_if *batman_if,
				uint8_t *dst_addr)
{
	struct ethhdr *ethhdr;

	if (batman_if->if_active != IF_ACTIVE)
		goto send_skb_err;

	if (unlikely(!batman_if->net_dev))
		goto send_skb_err;

	if (!(batman_if->net_dev->flags & IFF_UP)) {
		printk(KERN_WARNING
		       "batman-adv:Interface %s is not up - can't send packet via that interface!\n",
		       batman_if->dev);
		goto send_skb_err;
	}

	/* push to the ethernet header. */
	if (my_skb_push(skb, sizeof(struct ethhdr)) < 0)
		goto send_skb_err;

	skb_reset_mac_header(skb);

	ethhdr = (struct ethhdr *) skb_mac_header(skb);
	memcpy(ethhdr->h_source, batman_if->net_dev->dev_addr, ETH_ALEN);
	memcpy(ethhdr->h_dest, dst_addr, ETH_ALEN);
	ethhdr->h_proto = __constant_htons(ETH_P_BATMAN);

	skb_set_network_header(skb, ETH_HLEN);
	skb->priority = TC_PRIO_CONTROL;
	skb->protocol = __constant_htons(ETH_P_BATMAN);

	skb->dev = batman_if->net_dev;

	/* dev_queue_xmit() returns a negative result on error.	 However on
	 * congestion and traffic shaping, it drops and returns NET_XMIT_DROP
	 * (which is > 0). This will not be treated as an error. */

	return dev_queue_xmit(skb);
send_skb_err:
	kfree_skb(skb);
	return NET_XMIT_DROP;
}

/* sends a raw packet. */
void send_raw_packet(unsigned char *pack_buff, int pack_buff_len,
		     struct batman_if *batman_if, uint8_t *dst_addr)
{
	struct sk_buff *skb;
	char *data;

	skb = dev_alloc_skb(pack_buff_len + sizeof(struct ethhdr));
	if (!skb)
		return;
	data = skb_put(skb, pack_buff_len + sizeof(struct ethhdr));
	memcpy(data + sizeof(struct ethhdr), pack_buff, pack_buff_len);
	/* pull back to the batman "network header" */
	skb_pull(skb, sizeof(struct ethhdr));
	send_skb_packet(skb, batman_if, dst_addr);
}

/* Send a packet to a given interface */
static void send_packet_to_if(struct forw_packet *forw_packet,
			      struct batman_if *batman_if)
{
	char *fwd_str;
	uint8_t packet_num;
	int16_t buff_pos;
	struct batman_packet *batman_packet;

	if (batman_if->if_active != IF_ACTIVE)
		return;

	packet_num = buff_pos = 0;
	batman_packet = (struct batman_packet *)
		(forw_packet->packet_buff);

	/* adjust all flags and log packets */
	while (aggregated_packet(buff_pos,
				 forw_packet->packet_len,
				 batman_packet->num_hna)) {

		/* we might have aggregated direct link packets with an
		 * ordinary base packet */
		if ((forw_packet->direct_link_flags & (1 << packet_num)) &&
		    (forw_packet->if_incoming == batman_if))
			batman_packet->flags |= DIRECTLINK;
		else
			batman_packet->flags &= ~DIRECTLINK;

		fwd_str = (packet_num > 0 ? "Forwarding" : (forw_packet->own ?
							    "Sending own" :
							    "Forwarding"));
		bat_dbg(DBG_BATMAN,
			"%s %spacket (originator %pM, seqno %d, TQ %d, TTL %d, IDF %s) on interface %s [%s]\n",
			fwd_str,
			(packet_num > 0 ? "aggregated " : ""),
			batman_packet->orig, ntohs(batman_packet->seqno),
			batman_packet->tq, batman_packet->ttl,
			(batman_packet->flags & DIRECTLINK ?
			 "on" : "off"),
			batman_if->dev, batman_if->addr_str);

		buff_pos += sizeof(struct batman_packet) +
			(batman_packet->num_hna * ETH_ALEN);
		packet_num++;
		batman_packet = (struct batman_packet *)
			(forw_packet->packet_buff + buff_pos);
	}

	send_raw_packet(forw_packet->packet_buff,
			forw_packet->packet_len,
			batman_if, broadcastAddr);
}

/* send a batman packet */
static void send_packet(struct forw_packet *forw_packet)
{
	struct batman_if *batman_if;
	struct batman_packet *batman_packet =
		(struct batman_packet *)(forw_packet->packet_buff);
	unsigned char directlink = (batman_packet->flags & DIRECTLINK ? 1 : 0);

	if (!forw_packet->if_incoming) {
		printk(KERN_ERR "batman-adv: Error - can't forward packet: incoming iface not specified\n");
		return;
	}

	if (forw_packet->if_incoming->if_active != IF_ACTIVE)
		return;

	/* multihomed peer assumed */
	/* non-primary OGMs are only broadcasted on their interface */
	if ((directlink && (batman_packet->ttl == 1)) ||
	    (forw_packet->own && (forw_packet->if_incoming->if_num > 0))) {

		/* FIXME: what about aggregated packets ? */
		bat_dbg(DBG_BATMAN,
			"%s packet (originator %pM, seqno %d, TTL %d) on interface %s [%s]\n",
			(forw_packet->own ? "Sending own" : "Forwarding"),
			batman_packet->orig, ntohs(batman_packet->seqno),
			batman_packet->ttl, forw_packet->if_incoming->dev,
			forw_packet->if_incoming->addr_str);

		send_raw_packet(forw_packet->packet_buff,
				forw_packet->packet_len,
				forw_packet->if_incoming,
				broadcastAddr);
		return;
	}

	/* broadcast on every interface */
	rcu_read_lock();
	list_for_each_entry_rcu(batman_if, &if_list, list)
		send_packet_to_if(forw_packet, batman_if);
	rcu_read_unlock();
}

static void rebuild_batman_packet(struct batman_if *batman_if)
{
	int new_len;
	unsigned char *new_buff;
	struct batman_packet *batman_packet;

	new_len = sizeof(struct batman_packet) + (num_hna * ETH_ALEN);
	new_buff = kmalloc(new_len, GFP_ATOMIC);

	/* keep old buffer if kmalloc should fail */
	if (new_buff) {
		memcpy(new_buff, batman_if->packet_buff,
		       sizeof(struct batman_packet));
		batman_packet = (struct batman_packet *)new_buff;

		batman_packet->num_hna = hna_local_fill_buffer(
			new_buff + sizeof(struct batman_packet),
			new_len - sizeof(struct batman_packet));

		kfree(batman_if->packet_buff);
		batman_if->packet_buff = new_buff;
		batman_if->packet_len = new_len;
	}
}

void schedule_own_packet(struct batman_if *batman_if)
{
	unsigned long send_time;
	struct batman_packet *batman_packet;
	int vis_server = atomic_read(&vis_mode);

	/**
	 * the interface gets activated here to avoid race conditions between
	 * the moment of activating the interface in
	 * hardif_activate_interface() where the originator mac is set and
	 * outdated packets (especially uninitialized mac addresses) in the
	 * packet queue
	 */
	if (batman_if->if_active == IF_TO_BE_ACTIVATED)
		batman_if->if_active = IF_ACTIVE;

	/* if local hna has changed and interface is a primary interface */
	if ((atomic_read(&hna_local_changed)) && (batman_if->if_num == 0))
		rebuild_batman_packet(batman_if);

	/**
	 * NOTE: packet_buff might just have been re-allocated in
	 * rebuild_batman_packet()
	 */
	batman_packet = (struct batman_packet *)batman_if->packet_buff;

	/* change sequence number to network order */
	batman_packet->seqno = htons((uint16_t)atomic_read(&batman_if->seqno));

	if (vis_server == VIS_TYPE_SERVER_SYNC)
		batman_packet->flags = VIS_SERVER;
	else
		batman_packet->flags = 0;

	/* could be read by receive_bat_packet() */
	atomic_inc(&batman_if->seqno);

	slide_own_bcast_window(batman_if);
	send_time = own_send_time();
	add_bat_packet_to_list(batman_if->packet_buff,
			       batman_if->packet_len, batman_if, 1, send_time);
}

void schedule_forward_packet(struct orig_node *orig_node,
			     struct ethhdr *ethhdr,
			     struct batman_packet *batman_packet,
			     uint8_t directlink, int hna_buff_len,
			     struct batman_if *if_incoming)
{
	unsigned char in_tq, in_ttl, tq_avg = 0;
	unsigned long send_time;

	if (batman_packet->ttl <= 1) {
		bat_dbg(DBG_BATMAN, "ttl exceeded \n");
		return;
	}

	in_tq = batman_packet->tq;
	in_ttl = batman_packet->ttl;

	batman_packet->ttl--;
	memcpy(batman_packet->prev_sender, ethhdr->h_source, ETH_ALEN);

	/* rebroadcast tq of our best ranking neighbor to ensure the rebroadcast
	 * of our best tq value */
	if ((orig_node->router) && (orig_node->router->tq_avg != 0)) {

		/* rebroadcast ogm of best ranking neighbor as is */
		if (!compare_orig(orig_node->router->addr, ethhdr->h_source)) {
			batman_packet->tq = orig_node->router->tq_avg;

			if (orig_node->router->last_ttl)
				batman_packet->ttl = orig_node->router->last_ttl - 1;
		}

		tq_avg = orig_node->router->tq_avg;
	}

	/* apply hop penalty */
	batman_packet->tq = hop_penalty(batman_packet->tq);

	bat_dbg(DBG_BATMAN, "Forwarding packet: tq_orig: %i, tq_avg: %i, tq_forw: %i, ttl_orig: %i, ttl_forw: %i \n",
		in_tq, tq_avg, batman_packet->tq, in_ttl - 1,
		batman_packet->ttl);

	batman_packet->seqno = htons(batman_packet->seqno);

	if (directlink)
		batman_packet->flags |= DIRECTLINK;
	else
		batman_packet->flags &= ~DIRECTLINK;

	send_time = forward_send_time();
	add_bat_packet_to_list((unsigned char *)batman_packet,
			       sizeof(struct batman_packet) + hna_buff_len,
			       if_incoming, 0, send_time);
}

static void forw_packet_free(struct forw_packet *forw_packet)
{
	if (forw_packet->skb)
		kfree_skb(forw_packet->skb);
	kfree(forw_packet->packet_buff);
	kfree(forw_packet);
}

static void _add_bcast_packet_to_list(struct forw_packet *forw_packet,
				      unsigned long send_time)
{
	unsigned long flags;
	INIT_HLIST_NODE(&forw_packet->list);

	/* add new packet to packet list */
	spin_lock_irqsave(&forw_bcast_list_lock, flags);
	hlist_add_head(&forw_packet->list, &forw_bcast_list);
	spin_unlock_irqrestore(&forw_bcast_list_lock, flags);

	/* start timer for this packet */
	INIT_DELAYED_WORK(&forw_packet->delayed_work,
			  send_outstanding_bcast_packet);
	queue_delayed_work(bat_event_workqueue, &forw_packet->delayed_work,
			   send_time);
}

void add_bcast_packet_to_list(struct sk_buff *skb)
{
	struct forw_packet *forw_packet;

	forw_packet = kmalloc(sizeof(struct forw_packet), GFP_ATOMIC);
	if (!forw_packet)
		return;

	skb = skb_copy(skb, GFP_ATOMIC);
	if (!skb) {
		kfree(forw_packet);
		return;
	}

	skb_reset_mac_header(skb);

	forw_packet->skb = skb;
	forw_packet->packet_buff = NULL;

	/* how often did we send the bcast packet ? */
	forw_packet->num_packets = 0;

	_add_bcast_packet_to_list(forw_packet, 1);
}

void send_outstanding_bcast_packet(struct work_struct *work)
{
	struct batman_if *batman_if;
	struct delayed_work *delayed_work =
		container_of(work, struct delayed_work, work);
	struct forw_packet *forw_packet =
		container_of(delayed_work, struct forw_packet, delayed_work);
	unsigned long flags;
	struct sk_buff *skb1;

	spin_lock_irqsave(&forw_bcast_list_lock, flags);
	hlist_del(&forw_packet->list);
	spin_unlock_irqrestore(&forw_bcast_list_lock, flags);

	/* rebroadcast packet */
	rcu_read_lock();
	list_for_each_entry_rcu(batman_if, &if_list, list) {
		/* send a copy of the saved skb */
		skb1 = skb_copy(forw_packet->skb, GFP_ATOMIC);
		if (skb1)
			send_skb_packet(skb1,
				batman_if, broadcastAddr);
	}
	rcu_read_unlock();

	forw_packet->num_packets++;

	/* if we still have some more bcasts to send and we are not shutting
	 * down */
	if ((forw_packet->num_packets < 3) &&
	    (atomic_read(&module_state) != MODULE_DEACTIVATING))
		_add_bcast_packet_to_list(forw_packet, ((5 * HZ) / 1000));
	else
		forw_packet_free(forw_packet);
}

void send_outstanding_bat_packet(struct work_struct *work)
{
	struct delayed_work *delayed_work =
		container_of(work, struct delayed_work, work);
	struct forw_packet *forw_packet =
		container_of(delayed_work, struct forw_packet, delayed_work);
	unsigned long flags;

	spin_lock_irqsave(&forw_bat_list_lock, flags);
	hlist_del(&forw_packet->list);
	spin_unlock_irqrestore(&forw_bat_list_lock, flags);

	send_packet(forw_packet);

	/**
	 * we have to have at least one packet in the queue
	 * to determine the queues wake up time unless we are
	 * shutting down
	 */
	if ((forw_packet->own) &&
	    (atomic_read(&module_state) != MODULE_DEACTIVATING))
		schedule_own_packet(forw_packet->if_incoming);

	forw_packet_free(forw_packet);
}

void purge_outstanding_packets(void)
{
	struct forw_packet *forw_packet;
	struct hlist_node *tmp_node, *safe_tmp_node;
	unsigned long flags;

	bat_dbg(DBG_BATMAN, "purge_outstanding_packets()\n");

	/* free bcast list */
	spin_lock_irqsave(&forw_bcast_list_lock, flags);
	hlist_for_each_entry_safe(forw_packet, tmp_node, safe_tmp_node,
				  &forw_bcast_list, list) {

		spin_unlock_irqrestore(&forw_bcast_list_lock, flags);

		/**
		 * send_outstanding_bcast_packet() will lock the list to
		 * delete the item from the list
		 */
		cancel_delayed_work_sync(&forw_packet->delayed_work);
		spin_lock_irqsave(&forw_bcast_list_lock, flags);
	}
	spin_unlock_irqrestore(&forw_bcast_list_lock, flags);

	/* free batman packet list */
	spin_lock_irqsave(&forw_bat_list_lock, flags);
	hlist_for_each_entry_safe(forw_packet, tmp_node, safe_tmp_node,
				  &forw_bat_list, list) {

		spin_unlock_irqrestore(&forw_bat_list_lock, flags);

		/**
		 * send_outstanding_bat_packet() will lock the list to
		 * delete the item from the list
		 */
		cancel_delayed_work_sync(&forw_packet->delayed_work);
		spin_lock_irqsave(&forw_bat_list_lock, flags);
	}
	spin_unlock_irqrestore(&forw_bat_list_lock, flags);
}
