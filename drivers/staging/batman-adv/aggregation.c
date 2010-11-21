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

#include "main.h"
#include "aggregation.h"
#include "send.h"
#include "routing.h"

/* calculate the size of the hna information for a given packet */
static int hna_len(struct batman_packet *batman_packet)
{
	return batman_packet->num_hna * ETH_ALEN;
}

/* return true if new_packet can be aggregated with forw_packet */
static bool can_aggregate_with(struct batman_packet *new_batman_packet,
			       int packet_len,
			       unsigned long send_time,
			       bool directlink,
			       struct batman_if *if_incoming,
			       struct forw_packet *forw_packet)
{
	struct batman_packet *batman_packet =
		(struct batman_packet *)forw_packet->skb->data;
	int aggregated_bytes = forw_packet->packet_len + packet_len;

	/**
	 * we can aggregate the current packet to this aggregated packet
	 * if:
	 *
	 * - the send time is within our MAX_AGGREGATION_MS time
	 * - the resulting packet wont be bigger than
	 *   MAX_AGGREGATION_BYTES
	 */

	if (time_before(send_time, forw_packet->send_time) &&
	    time_after_eq(send_time + msecs_to_jiffies(MAX_AGGREGATION_MS),
					forw_packet->send_time) &&
	    (aggregated_bytes <= MAX_AGGREGATION_BYTES)) {

		/**
		 * check aggregation compatibility
		 * -> direct link packets are broadcasted on
		 *    their interface only
		 * -> aggregate packet if the current packet is
		 *    a "global" packet as well as the base
		 *    packet
		 */

		/* packets without direct link flag and high TTL
		 * are flooded through the net  */
		if ((!directlink) &&
		    (!(batman_packet->flags & DIRECTLINK)) &&
		    (batman_packet->ttl != 1) &&

		    /* own packets originating non-primary
		     * interfaces leave only that interface */
		    ((!forw_packet->own) ||
		     (forw_packet->if_incoming->if_num == 0)))
			return true;

		/* if the incoming packet is sent via this one
		 * interface only - we still can aggregate */
		if ((directlink) &&
		    (new_batman_packet->ttl == 1) &&
		    (forw_packet->if_incoming == if_incoming) &&

		    /* packets from direct neighbors or
		     * own secondary interface packets
		     * (= secondary interface packets in general) */
		    (batman_packet->flags & DIRECTLINK ||
		     (forw_packet->own &&
		      forw_packet->if_incoming->if_num != 0)))
			return true;
	}

	return false;
}

#define atomic_dec_not_zero(v)          atomic_add_unless((v), -1, 0)
/* create a new aggregated packet and add this packet to it */
static void new_aggregated_packet(unsigned char *packet_buff, int packet_len,
				  unsigned long send_time, bool direct_link,
				  struct batman_if *if_incoming,
				  int own_packet)
{
	struct bat_priv *bat_priv = netdev_priv(if_incoming->soft_iface);
	struct forw_packet *forw_packet_aggr;
	unsigned long flags;
	unsigned char *skb_buff;

	/* own packet should always be scheduled */
	if (!own_packet) {
		if (!atomic_dec_not_zero(&bat_priv->batman_queue_left)) {
			bat_dbg(DBG_BATMAN, bat_priv,
				"batman packet queue full\n");
			return;
		}
	}

	forw_packet_aggr = kmalloc(sizeof(struct forw_packet), GFP_ATOMIC);
	if (!forw_packet_aggr) {
		if (!own_packet)
			atomic_inc(&bat_priv->batman_queue_left);
		return;
	}

	if ((atomic_read(&bat_priv->aggregated_ogms)) &&
	    (packet_len < MAX_AGGREGATION_BYTES))
		forw_packet_aggr->skb = dev_alloc_skb(MAX_AGGREGATION_BYTES +
						      sizeof(struct ethhdr));
	else
		forw_packet_aggr->skb = dev_alloc_skb(packet_len +
						      sizeof(struct ethhdr));

	if (!forw_packet_aggr->skb) {
		if (!own_packet)
			atomic_inc(&bat_priv->batman_queue_left);
		kfree(forw_packet_aggr);
		return;
	}
	skb_reserve(forw_packet_aggr->skb, sizeof(struct ethhdr));

	INIT_HLIST_NODE(&forw_packet_aggr->list);

	skb_buff = skb_put(forw_packet_aggr->skb, packet_len);
	forw_packet_aggr->packet_len = packet_len;
	memcpy(skb_buff, packet_buff, packet_len);

	forw_packet_aggr->own = own_packet;
	forw_packet_aggr->if_incoming = if_incoming;
	forw_packet_aggr->num_packets = 0;
	forw_packet_aggr->direct_link_flags = 0;
	forw_packet_aggr->send_time = send_time;

	/* save packet direct link flag status */
	if (direct_link)
		forw_packet_aggr->direct_link_flags |= 1;

	/* add new packet to packet list */
	spin_lock_irqsave(&bat_priv->forw_bat_list_lock, flags);
	hlist_add_head(&forw_packet_aggr->list, &bat_priv->forw_bat_list);
	spin_unlock_irqrestore(&bat_priv->forw_bat_list_lock, flags);

	/* start timer for this packet */
	INIT_DELAYED_WORK(&forw_packet_aggr->delayed_work,
			  send_outstanding_bat_packet);
	queue_delayed_work(bat_event_workqueue,
			   &forw_packet_aggr->delayed_work,
			   send_time - jiffies);
}

/* aggregate a new packet into the existing aggregation */
static void aggregate(struct forw_packet *forw_packet_aggr,
		      unsigned char *packet_buff,
		      int packet_len,
		      bool direct_link)
{
	unsigned char *skb_buff;

	skb_buff = skb_put(forw_packet_aggr->skb, packet_len);
	memcpy(skb_buff, packet_buff, packet_len);
	forw_packet_aggr->packet_len += packet_len;
	forw_packet_aggr->num_packets++;

	/* save packet direct link flag status */
	if (direct_link)
		forw_packet_aggr->direct_link_flags |=
			(1 << forw_packet_aggr->num_packets);
}

void add_bat_packet_to_list(struct bat_priv *bat_priv,
			    unsigned char *packet_buff, int packet_len,
			    struct batman_if *if_incoming, char own_packet,
			    unsigned long send_time)
{
	/**
	 * _aggr -> pointer to the packet we want to aggregate with
	 * _pos -> pointer to the position in the queue
	 */
	struct forw_packet *forw_packet_aggr = NULL, *forw_packet_pos = NULL;
	struct hlist_node *tmp_node;
	struct batman_packet *batman_packet =
		(struct batman_packet *)packet_buff;
	bool direct_link = batman_packet->flags & DIRECTLINK ? 1 : 0;
	unsigned long flags;

	/* find position for the packet in the forward queue */
	spin_lock_irqsave(&bat_priv->forw_bat_list_lock, flags);
	/* own packets are not to be aggregated */
	if ((atomic_read(&bat_priv->aggregated_ogms)) && (!own_packet)) {
		hlist_for_each_entry(forw_packet_pos, tmp_node,
				     &bat_priv->forw_bat_list, list) {
			if (can_aggregate_with(batman_packet,
					       packet_len,
					       send_time,
					       direct_link,
					       if_incoming,
					       forw_packet_pos)) {
				forw_packet_aggr = forw_packet_pos;
				break;
			}
		}
	}

	/* nothing to aggregate with - either aggregation disabled or no
	 * suitable aggregation packet found */
	if (forw_packet_aggr == NULL) {
		/* the following section can run without the lock */
		spin_unlock_irqrestore(&bat_priv->forw_bat_list_lock, flags);

		/**
		 * if we could not aggregate this packet with one of the others
		 * we hold it back for a while, so that it might be aggregated
		 * later on
		 */
		if ((!own_packet) &&
		    (atomic_read(&bat_priv->aggregated_ogms)))
			send_time += msecs_to_jiffies(MAX_AGGREGATION_MS);

		new_aggregated_packet(packet_buff, packet_len,
				      send_time, direct_link,
				      if_incoming, own_packet);
	} else {
		aggregate(forw_packet_aggr,
			  packet_buff, packet_len,
			  direct_link);
		spin_unlock_irqrestore(&bat_priv->forw_bat_list_lock, flags);
	}
}

/* unpack the aggregated packets and process them one by one */
void receive_aggr_bat_packet(struct ethhdr *ethhdr, unsigned char *packet_buff,
			     int packet_len, struct batman_if *if_incoming)
{
	struct batman_packet *batman_packet;
	int buff_pos = 0;
	unsigned char *hna_buff;

	batman_packet = (struct batman_packet *)packet_buff;

	do {
		/* network to host order for our 32bit seqno, and the
		   orig_interval. */
		batman_packet->seqno = ntohl(batman_packet->seqno);

		hna_buff = packet_buff + buff_pos + BAT_PACKET_LEN;
		receive_bat_packet(ethhdr, batman_packet,
				   hna_buff, hna_len(batman_packet),
				   if_incoming);

		buff_pos += BAT_PACKET_LEN + hna_len(batman_packet);
		batman_packet = (struct batman_packet *)
			(packet_buff + buff_pos);
	} while (aggregated_packet(buff_pos, packet_len,
				   batman_packet->num_hna));
}
