/*
 * Copyright (C) 2008-2009 B.A.T.M.A.N. contributors:
 *
 * Simon Wunderlich
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
#include "translation-table.h"
#include "vis.h"
#include "soft-interface.h"
#include "hard-interface.h"
#include "hash.h"

struct hashtable_t *vis_hash;
DEFINE_SPINLOCK(vis_hash_lock);
static struct vis_info *my_vis_info;
static struct list_head send_list;	/* always locked with vis_hash_lock */

static void start_vis_timer(void);

/* free the info */
static void free_info(void *data)
{
	struct vis_info *info = data;
	struct recvlist_node *entry, *tmp;

	list_del_init(&info->send_list);
	list_for_each_entry_safe(entry, tmp, &info->recv_list, list) {
		list_del(&entry->list);
		kfree(entry);
	}
	kfree(info);
}

/* set the mode of the visualization to client or server */
void vis_set_mode(int mode)
{
	unsigned long flags;
	spin_lock_irqsave(&vis_hash_lock, flags);

	if (my_vis_info != NULL)
		my_vis_info->packet.vis_type = mode;

	spin_unlock_irqrestore(&vis_hash_lock, flags);
}

/* is_vis_server(), locked outside */
static int is_vis_server_locked(void)
{
	if (my_vis_info != NULL)
		if (my_vis_info->packet.vis_type == VIS_TYPE_SERVER_SYNC)
			return 1;

	return 0;
}

/* get the current set mode */
int is_vis_server(void)
{
	int ret = 0;
	unsigned long flags;

	spin_lock_irqsave(&vis_hash_lock, flags);
	ret = is_vis_server_locked();
	spin_unlock_irqrestore(&vis_hash_lock, flags);

	return ret;
}

/* Compare two vis packets, used by the hashing algorithm */
static int vis_info_cmp(void *data1, void *data2)
{
	struct vis_info *d1, *d2;
	d1 = data1;
	d2 = data2;
	return compare_orig(d1->packet.vis_orig, d2->packet.vis_orig);
}

/* hash function to choose an entry in a hash table of given size */
/* hash algorithm from http://en.wikipedia.org/wiki/Hash_table */
static int vis_info_choose(void *data, int size)
{
	struct vis_info *vis_info = data;
	unsigned char *key;
	uint32_t hash = 0;
	size_t i;

	key = vis_info->packet.vis_orig;
	for (i = 0; i < ETH_ALEN; i++) {
		hash += key[i];
		hash += (hash << 10);
		hash ^= (hash >> 6);
	}

	hash += (hash << 3);
	hash ^= (hash >> 11);
	hash += (hash << 15);

	return hash % size;
}

/* insert interface to the list of interfaces of one originator, if it
 * does not already exist in the list */
static void proc_vis_insert_interface(const uint8_t *interface,
				      struct hlist_head *if_list,
				      bool primary)
{
	struct if_list_entry *entry;
	struct hlist_node *pos;

	hlist_for_each_entry(entry, pos, if_list, list) {
		if (compare_orig(entry->addr, (void *)interface))
			return;
	}

	/* its a new address, add it to the list */
	entry = kmalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return;
	memcpy(entry->addr, interface, ETH_ALEN);
	entry->primary = primary;
	hlist_add_head(&entry->list, if_list);
}

void proc_vis_read_prim_sec(struct seq_file *seq,
			    struct hlist_head *if_list)
{
	struct if_list_entry *entry;
	struct hlist_node *pos, *n;
	char tmp_addr_str[ETH_STR_LEN];

	hlist_for_each_entry_safe(entry, pos, n, if_list, list) {
		if (entry->primary) {
			seq_printf(seq, "PRIMARY, ");
		} else {
			addr_to_string(tmp_addr_str, entry->addr);
			seq_printf(seq, "SEC %s, ", tmp_addr_str);
		}

		hlist_del(&entry->list);
		kfree(entry);
	}
}

/* read an entry  */
void proc_vis_read_entry(struct seq_file *seq,
				struct vis_info_entry *entry,
				struct hlist_head *if_list,
				uint8_t *vis_orig)
{
	char to[40];

	addr_to_string(to, entry->dest);
	if (entry->quality == 0) {
		proc_vis_insert_interface(vis_orig, if_list, true);
		seq_printf(seq, "HNA %s, ", to);
	} else {
		proc_vis_insert_interface(entry->src, if_list,
					  compare_orig(entry->src, vis_orig));
		seq_printf(seq, "TQ %s %d, ", to, entry->quality);
	}
}

/* tries to add one entry to the receive list. */
static void recv_list_add(struct list_head *recv_list, char *mac)
{
	struct recvlist_node *entry;
	entry = kmalloc(sizeof(struct recvlist_node), GFP_ATOMIC);
	if (!entry)
		return;

	memcpy(entry->mac, mac, ETH_ALEN);
	list_add_tail(&entry->list, recv_list);
}

/* returns 1 if this mac is in the recv_list */
static int recv_list_is_in(struct list_head *recv_list, char *mac)
{
	struct recvlist_node *entry;

	list_for_each_entry(entry, recv_list, list) {
		if (memcmp(entry->mac, mac, ETH_ALEN) == 0)
			return 1;
	}

	return 0;
}

/* try to add the packet to the vis_hash. return NULL if invalid (e.g. too old,
 * broken.. ).  vis hash must be locked outside.  is_new is set when the packet
 * is newer than old entries in the hash. */
static struct vis_info *add_packet(struct vis_packet *vis_packet,
				   int vis_info_len, int *is_new)
{
	struct vis_info *info, *old_info;
	struct vis_info search_elem;

	*is_new = 0;
	/* sanity check */
	if (vis_hash == NULL)
		return NULL;

	/* see if the packet is already in vis_hash */
	memcpy(search_elem.packet.vis_orig, vis_packet->vis_orig, ETH_ALEN);
	old_info = hash_find(vis_hash, &search_elem);

	if (old_info != NULL) {
		if (vis_packet->seqno - old_info->packet.seqno <= 0) {
			if (old_info->packet.seqno == vis_packet->seqno) {
				recv_list_add(&old_info->recv_list,
					      vis_packet->sender_orig);
				return old_info;
			} else {
				/* newer packet is already in hash. */
				return NULL;
			}
		}
		/* remove old entry */
		hash_remove(vis_hash, old_info);
		free_info(old_info);
	}

	info = kmalloc(sizeof(struct vis_info) + vis_info_len, GFP_ATOMIC);
	if (info == NULL)
		return NULL;

	INIT_LIST_HEAD(&info->send_list);
	INIT_LIST_HEAD(&info->recv_list);
	info->first_seen = jiffies;
	memcpy(&info->packet, vis_packet,
	       sizeof(struct vis_packet) + vis_info_len);

	/* initialize and add new packet. */
	*is_new = 1;

	/* repair if entries is longer than packet. */
	if (info->packet.entries * sizeof(struct vis_info_entry) > vis_info_len)
		info->packet.entries = vis_info_len / sizeof(struct vis_info_entry);

	recv_list_add(&info->recv_list, info->packet.sender_orig);

	/* try to add it */
	if (hash_add(vis_hash, info) < 0) {
		/* did not work (for some reason) */
		free_info(info);
		info = NULL;
	}

	return info;
}

/* handle the server sync packet, forward if needed. */
void receive_server_sync_packet(struct vis_packet *vis_packet, int vis_info_len)
{
	struct vis_info *info;
	int is_new;
	unsigned long flags;

	spin_lock_irqsave(&vis_hash_lock, flags);
	info = add_packet(vis_packet, vis_info_len, &is_new);
	if (info == NULL)
		goto end;

	/* only if we are server ourselves and packet is newer than the one in
	 * hash.*/
	if (is_vis_server_locked() && is_new) {
		memcpy(info->packet.target_orig, broadcastAddr, ETH_ALEN);
		if (list_empty(&info->send_list))
			list_add_tail(&info->send_list, &send_list);
	}
end:
	spin_unlock_irqrestore(&vis_hash_lock, flags);
}

/* handle an incoming client update packet and schedule forward if needed. */
void receive_client_update_packet(struct vis_packet *vis_packet,
				  int vis_info_len)
{
	struct vis_info *info;
	int is_new;
	unsigned long flags;

	/* clients shall not broadcast. */
	if (is_bcast(vis_packet->target_orig))
		return;

	spin_lock_irqsave(&vis_hash_lock, flags);
	info = add_packet(vis_packet, vis_info_len, &is_new);
	if (info == NULL)
		goto end;
	/* note that outdated packets will be dropped at this point. */


	/* send only if we're the target server or ... */
	if (is_vis_server_locked() &&
	    is_my_mac(info->packet.target_orig) &&
	    is_new) {
		info->packet.vis_type = VIS_TYPE_SERVER_SYNC;	/* upgrade! */
		memcpy(info->packet.target_orig, broadcastAddr, ETH_ALEN);
		if (list_empty(&info->send_list))
			list_add_tail(&info->send_list, &send_list);

		/* ... we're not the recipient (and thus need to forward). */
	} else if (!is_my_mac(info->packet.target_orig)) {
		if (list_empty(&info->send_list))
			list_add_tail(&info->send_list, &send_list);
	}
end:
	spin_unlock_irqrestore(&vis_hash_lock, flags);
}

/* Walk the originators and find the VIS server with the best tq. Set the packet
 * address to its address and return the best_tq.
 *
 * Must be called with the originator hash locked */
static int find_best_vis_server(struct vis_info *info)
{
	HASHIT(hashit);
	struct orig_node *orig_node;
	int best_tq = -1;

	while (hash_iterate(orig_hash, &hashit)) {
		orig_node = hashit.bucket->data;
		if ((orig_node != NULL) &&
		    (orig_node->router != NULL) &&
		    (orig_node->flags & VIS_SERVER) &&
		    (orig_node->router->tq_avg > best_tq)) {
			best_tq = orig_node->router->tq_avg;
			memcpy(info->packet.target_orig, orig_node->orig,
			       ETH_ALEN);
		}
	}
	return best_tq;
}

/* Return true if the vis packet is full. */
static bool vis_packet_full(struct vis_info *info)
{
	if (info->packet.entries + 1 >
	    (1000 - sizeof(struct vis_info)) / sizeof(struct vis_info_entry))
		return true;
	return false;
}

/* generates a packet of own vis data,
 * returns 0 on success, -1 if no packet could be generated */
static int generate_vis_packet(void)
{
	HASHIT(hashit_local);
	HASHIT(hashit_global);
	struct orig_node *orig_node;
	struct vis_info *info = (struct vis_info *)my_vis_info;
	struct vis_info_entry *entry, *entry_array;
	struct hna_local_entry *hna_local_entry;
	int best_tq = -1;
	unsigned long flags;

	info->first_seen = jiffies;

	spin_lock_irqsave(&orig_hash_lock, flags);
	memcpy(info->packet.target_orig, broadcastAddr, ETH_ALEN);
	info->packet.ttl = TTL;
	info->packet.seqno++;
	info->packet.entries = 0;

	if (!is_vis_server_locked()) {
		best_tq = find_best_vis_server(info);
		if (best_tq < 0) {
			spin_unlock_irqrestore(&orig_hash_lock, flags);
			return -1;
		}
	}

	entry_array = (struct vis_info_entry *)
		((char *)info + sizeof(struct vis_info));

	while (hash_iterate(orig_hash, &hashit_global)) {
		orig_node = hashit_global.bucket->data;
		if (orig_node->router != NULL
			&& compare_orig(orig_node->router->addr, orig_node->orig)
			&& orig_node->batman_if
			&& (orig_node->batman_if->if_active == IF_ACTIVE)
		    && orig_node->router->tq_avg > 0) {

			/* fill one entry into buffer. */
			entry = &entry_array[info->packet.entries];
			memcpy(entry->src, orig_node->batman_if->net_dev->dev_addr, ETH_ALEN);
			memcpy(entry->dest, orig_node->orig, ETH_ALEN);
			entry->quality = orig_node->router->tq_avg;
			info->packet.entries++;

			if (vis_packet_full(info)) {
				spin_unlock_irqrestore(&orig_hash_lock, flags);
				return 0;
			}
		}
	}

	spin_unlock_irqrestore(&orig_hash_lock, flags);

	spin_lock_irqsave(&hna_local_hash_lock, flags);
	while (hash_iterate(hna_local_hash, &hashit_local)) {
		hna_local_entry = hashit_local.bucket->data;
		entry = &entry_array[info->packet.entries];
		memset(entry->src, 0, ETH_ALEN);
		memcpy(entry->dest, hna_local_entry->addr, ETH_ALEN);
		entry->quality = 0; /* 0 means HNA */
		info->packet.entries++;

		if (vis_packet_full(info)) {
			spin_unlock_irqrestore(&hna_local_hash_lock, flags);
			return 0;
		}
	}
	spin_unlock_irqrestore(&hna_local_hash_lock, flags);
	return 0;
}

static void purge_vis_packets(void)
{
	HASHIT(hashit);
	struct vis_info *info;

	while (hash_iterate(vis_hash, &hashit)) {
		info = hashit.bucket->data;
		if (info == my_vis_info)	/* never purge own data. */
			continue;
		if (time_after(jiffies,
			       info->first_seen + (VIS_TIMEOUT*HZ)/1000)) {
			hash_remove_bucket(vis_hash, &hashit);
			free_info(info);
		}
	}
}

static void broadcast_vis_packet(struct vis_info *info, int packet_length)
{
	HASHIT(hashit);
	struct orig_node *orig_node;
	unsigned long flags;

	spin_lock_irqsave(&orig_hash_lock, flags);

	/* send to all routers in range. */
	while (hash_iterate(orig_hash, &hashit)) {
		orig_node = hashit.bucket->data;

		/* if it's a vis server and reachable, send it. */
		if (orig_node &&
		    (orig_node->flags & VIS_SERVER) &&
		    orig_node->batman_if &&
		    orig_node->router) {

			/* don't send it if we already received the packet from
			 * this node. */
			if (recv_list_is_in(&info->recv_list, orig_node->orig))
				continue;

			memcpy(info->packet.target_orig,
			       orig_node->orig, ETH_ALEN);

			send_raw_packet((unsigned char *) &info->packet,
					packet_length,
					orig_node->batman_if,
					orig_node->router->addr);
		}
	}
	memcpy(info->packet.target_orig, broadcastAddr, ETH_ALEN);
	spin_unlock_irqrestore(&orig_hash_lock, flags);
}

static void unicast_vis_packet(struct vis_info *info, int packet_length)
{
	struct orig_node *orig_node;
	unsigned long flags;

	spin_lock_irqsave(&orig_hash_lock, flags);
	orig_node = ((struct orig_node *)
		     hash_find(orig_hash, info->packet.target_orig));

	if ((orig_node != NULL) &&
	    (orig_node->batman_if != NULL) &&
	    (orig_node->router != NULL)) {
		send_raw_packet((unsigned char *) &info->packet, packet_length,
				orig_node->batman_if,
				orig_node->router->addr);
	}
	spin_unlock_irqrestore(&orig_hash_lock, flags);
}

/* only send one vis packet. called from send_vis_packets() */
static void send_vis_packet(struct vis_info *info)
{
	int packet_length;

	if (info->packet.ttl < 2) {
		printk(KERN_WARNING "batman-adv: Error - can't send vis packet: ttl exceeded\n");
		return;
	}

	memcpy(info->packet.sender_orig, mainIfAddr, ETH_ALEN);
	info->packet.ttl--;

	packet_length = sizeof(struct vis_packet) +
		info->packet.entries * sizeof(struct vis_info_entry);

	if (is_bcast(info->packet.target_orig))
		broadcast_vis_packet(info, packet_length);
	else
		unicast_vis_packet(info, packet_length);
	info->packet.ttl++; /* restore TTL */
}

/* called from timer; send (and maybe generate) vis packet. */
static void send_vis_packets(struct work_struct *work)
{
	struct vis_info *info, *temp;
	unsigned long flags;

	spin_lock_irqsave(&vis_hash_lock, flags);
	purge_vis_packets();

	if (generate_vis_packet() == 0)
		/* schedule if generation was successful */
		list_add_tail(&my_vis_info->send_list, &send_list);

	list_for_each_entry_safe(info, temp, &send_list, send_list) {
		list_del_init(&info->send_list);
		send_vis_packet(info);
	}
	spin_unlock_irqrestore(&vis_hash_lock, flags);
	start_vis_timer();
}
static DECLARE_DELAYED_WORK(vis_timer_wq, send_vis_packets);

/* init the vis server. this may only be called when if_list is already
 * initialized (e.g. bat0 is initialized, interfaces have been added) */
int vis_init(void)
{
	unsigned long flags;
	if (vis_hash)
		return 1;

	spin_lock_irqsave(&vis_hash_lock, flags);

	vis_hash = hash_new(256, vis_info_cmp, vis_info_choose);
	if (!vis_hash) {
		printk(KERN_ERR "batman-adv:Can't initialize vis_hash\n");
		goto err;
	}

	my_vis_info = kmalloc(1000, GFP_ATOMIC);
	if (!my_vis_info) {
		printk(KERN_ERR "batman-adv:Can't initialize vis packet\n");
		goto err;
	}

	/* prefill the vis info */
	my_vis_info->first_seen = jiffies - atomic_read(&vis_interval);
	INIT_LIST_HEAD(&my_vis_info->recv_list);
	INIT_LIST_HEAD(&my_vis_info->send_list);
	my_vis_info->packet.version = COMPAT_VERSION;
	my_vis_info->packet.packet_type = BAT_VIS;
	my_vis_info->packet.vis_type = VIS_TYPE_CLIENT_UPDATE;
	my_vis_info->packet.ttl = TTL;
	my_vis_info->packet.seqno = 0;
	my_vis_info->packet.entries = 0;

	INIT_LIST_HEAD(&send_list);

	memcpy(my_vis_info->packet.vis_orig, mainIfAddr, ETH_ALEN);
	memcpy(my_vis_info->packet.sender_orig, mainIfAddr, ETH_ALEN);

	if (hash_add(vis_hash, my_vis_info) < 0) {
		printk(KERN_ERR
			  "batman-adv:Can't add own vis packet into hash\n");
		free_info(my_vis_info);	/* not in hash, need to remove it
					 * manually. */
		goto err;
	}

	spin_unlock_irqrestore(&vis_hash_lock, flags);
	start_vis_timer();
	return 1;

err:
	spin_unlock_irqrestore(&vis_hash_lock, flags);
	vis_quit();
	return 0;
}

/* shutdown vis-server */
void vis_quit(void)
{
	unsigned long flags;
	if (!vis_hash)
		return;

	cancel_delayed_work_sync(&vis_timer_wq);

	spin_lock_irqsave(&vis_hash_lock, flags);
	/* properly remove, kill timers ... */
	hash_delete(vis_hash, free_info);
	vis_hash = NULL;
	my_vis_info = NULL;
	spin_unlock_irqrestore(&vis_hash_lock, flags);
}

/* schedule packets for (re)transmission */
static void start_vis_timer(void)
{
	queue_delayed_work(bat_event_workqueue, &vis_timer_wq,
			   (atomic_read(&vis_interval) * HZ) / 1000);
}

