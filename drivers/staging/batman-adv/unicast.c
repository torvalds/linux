/*
 * Copyright (C) 2010 B.A.T.M.A.N. contributors:
 *
 * Andreas Langer
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
#include "unicast.h"
#include "send.h"
#include "soft-interface.h"
#include "hash.h"
#include "translation-table.h"
#include "routing.h"
#include "hard-interface.h"

int unicast_send_skb(struct sk_buff *skb, struct bat_priv *bat_priv)
{
	struct ethhdr *ethhdr = (struct ethhdr *)skb->data;
	struct unicast_packet *unicast_packet;
	struct orig_node *orig_node;
	struct batman_if *batman_if;
	struct neigh_node *router;
	uint8_t dstaddr[6];
	unsigned long flags;

	spin_lock_irqsave(&orig_hash_lock, flags);

	/* get routing information */
	orig_node = ((struct orig_node *)hash_find(orig_hash, ethhdr->h_dest));

	/* check for hna host */
	if (!orig_node)
		orig_node = transtable_search(ethhdr->h_dest);

	router = find_router(orig_node, NULL);

	if (!router)
		goto unlock;

	/* don't lock while sending the packets ... we therefore
		* copy the required data before sending */

	batman_if = router->if_incoming;
	memcpy(dstaddr, router->addr, ETH_ALEN);

	spin_unlock_irqrestore(&orig_hash_lock, flags);

	if (batman_if->if_status != IF_ACTIVE)
		goto dropped;

	if (my_skb_push(skb, sizeof(struct unicast_packet)) < 0)
		goto dropped;

	unicast_packet = (struct unicast_packet *)skb->data;

	unicast_packet->version = COMPAT_VERSION;
	/* batman packet type: unicast */
	unicast_packet->packet_type = BAT_UNICAST;
	/* set unicast ttl */
	unicast_packet->ttl = TTL;
	/* copy the destination for faster routing */
	memcpy(unicast_packet->dest, orig_node->orig, ETH_ALEN);

	send_skb_packet(skb, batman_if, dstaddr);
	return 0;

unlock:
	spin_unlock_irqrestore(&orig_hash_lock, flags);
dropped:
	kfree_skb(skb);
	return 1;
}
