/*
 * Copyright (C) 2008-2010 B.A.T.M.A.N. contributors:
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

/* Returns the smallest signed integer in two's complement with the sizeof x */
#define smallest_signed_int(x) (1u << (7u + 8u * (sizeof(x) - 1u)))

/* Checks if a sequence number x is a predecessor/successor of y.
   they handle overflows/underflows and can correctly check for a
   predecessor/successor unless the variable sequence number has grown by
   more then 2**(bitwidth(x)-1)-1.
   This means that for a uint8_t with the maximum value 255, it would think:
    * when adding nothing - it is neither a predecessor nor a successor
    * before adding more than 127 to the starting value - it is a predecessor,
    * when adding 128 - it is neither a predecessor nor a successor,
    * after adding more than 127 to the starting value - it is a successor */
#define seq_before(x, y) ({typeof(x) _dummy = (x - y); \
			_dummy > smallest_signed_int(_dummy); })
#define seq_after(x, y) seq_before(y, x)

static struct hashtable_t *vis_hash;
static DEFINE_SPINLOCK(vis_hash_lock);
static DEFINE_SPINLOCK(recv_list_lock);
static struct vis_info *my_vis_info;
static struct list_head send_list;	/* always locked with vis_hash_lock */

static void start_vis_timer(void);

/* free the info */
static void free_info(struct kref *ref)
{
	struct vis_info *info = container_of(ref, struct vis_info, refcount);
	struct recvlist_node *entry, *tmp;
	unsigned long flags;

	list_del_init(&info->send_list);
	spin_lock_irqsave(&recv_list_lock, flags);
	list_for_each_entry_safe(entry, tmp, &info->recv_list, list) {
		list_del(&entry->list);
		kfree(entry);
	}
	spin_unlock_irqrestore(&recv_list_lock, flags);
	kfree(info);
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
static void vis_data_insert_interface(const uint8_t *interface,
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
	entry = kmalloc(sizeof(*entry), GFP_ATOMIC);
	if (!entry)
		return;
	memcpy(entry->addr, interface, ETH_ALEN);
	entry->primary = primary;
	hlist_add_head(&entry->list, if_list);
}

static ssize_t vis_data_read_prim_sec(char *buff, struct hlist_head *if_list)
{
	struct if_list_entry *entry;
	struct hlist_node *pos;
	char tmp_addr_str[ETH_STR_LEN];
	size_t len = 0;

	hlist_for_each_entry(entry, pos, if_list, list) {
		if (entry->primary)
			len += sprintf(buff + len, "PRIMARY, ");
		else {
			addr_to_string(tmp_addr_str, entry->addr);
			len += sprintf(buff + len,  "SEC %s, ", tmp_addr_str);
		}
	}

	return len;
}

static size_t vis_data_count_prim_sec(struct hlist_head *if_list)
{
	struct if_list_entry *entry;
	struct hlist_node *pos;
	size_t count = 0;

	hlist_for_each_entry(entry, pos, if_list, list) {
		if (entry->primary)
			count += 9;
		else
			count += 23;
	}

	return count;
}

/* read an entry  */
static ssize_t vis_data_read_entry(char *buff, struct vis_info_entry *entry,
				   uint8_t *src, bool primary)
{
	char to[18];

	/* maximal length: max(4+17+2, 3+17+1+3+2) == 26 */
	addr_to_string(to, entry->dest);
	if (primary && entry->quality == 0)
		return sprintf(buff, "HNA %s, ", to);
	else if (compare_orig(entry->src, src))
		return sprintf(buff, "TQ %s %d, ", to, entry->quality);

	return 0;
}

int vis_seq_print_text(struct seq_file *seq, void *offset)
{
	HASHIT(hashit);
	HASHIT(hashit_count);
	struct vis_info *info;
	struct vis_info_entry *entries;
	struct net_device *net_dev = (struct net_device *)seq->private;
	struct bat_priv *bat_priv = netdev_priv(net_dev);
	HLIST_HEAD(vis_if_list);
	struct if_list_entry *entry;
	struct hlist_node *pos, *n;
	int i;
	char tmp_addr_str[ETH_STR_LEN];
	unsigned long flags;
	int vis_server = atomic_read(&bat_priv->vis_mode);
	size_t buff_pos, buf_size;
	char *buff;

	if ((!bat_priv->primary_if) ||
	    (vis_server == VIS_TYPE_CLIENT_UPDATE))
		return 0;

	buf_size = 1;
	/* Estimate length */
	spin_lock_irqsave(&vis_hash_lock, flags);
	while (hash_iterate(vis_hash, &hashit_count)) {
		info = hashit_count.bucket->data;
		entries = (struct vis_info_entry *)
			((char *)info + sizeof(struct vis_info));

		for (i = 0; i < info->packet.entries; i++) {
			if (entries[i].quality == 0)
				continue;
			vis_data_insert_interface(entries[i].src, &vis_if_list,
				compare_orig(entries[i].src,
						info->packet.vis_orig));
		}

		hlist_for_each_entry(entry, pos, &vis_if_list, list) {
			buf_size += 18 + 26 * info->packet.entries;

			/* add primary/secondary records */
			if (compare_orig(entry->addr, info->packet.vis_orig))
				buf_size +=
					vis_data_count_prim_sec(&vis_if_list);

			buf_size += 1;
		}

		hlist_for_each_entry_safe(entry, pos, n, &vis_if_list, list) {
			hlist_del(&entry->list);
			kfree(entry);
		}
	}

	buff = kmalloc(buf_size, GFP_ATOMIC);
	if (!buff) {
		spin_unlock_irqrestore(&vis_hash_lock, flags);
		return -ENOMEM;
	}
	buff[0] = '\0';
	buff_pos = 0;

	while (hash_iterate(vis_hash, &hashit)) {
		info = hashit.bucket->data;
		entries = (struct vis_info_entry *)
			((char *)info + sizeof(struct vis_info));

		for (i = 0; i < info->packet.entries; i++) {
			if (entries[i].quality == 0)
				continue;
			vis_data_insert_interface(entries[i].src, &vis_if_list,
				compare_orig(entries[i].src,
						info->packet.vis_orig));
		}

		hlist_for_each_entry(entry, pos, &vis_if_list, list) {
			addr_to_string(tmp_addr_str, entry->addr);
			buff_pos += sprintf(buff + buff_pos, "%s,",
					    tmp_addr_str);

			for (i = 0; i < info->packet.entries; i++)
				buff_pos += vis_data_read_entry(buff + buff_pos,
								&entries[i],
								entry->addr,
								entry->primary);

			/* add primary/secondary records */
			if (compare_orig(entry->addr, info->packet.vis_orig))
				buff_pos +=
					vis_data_read_prim_sec(buff + buff_pos,
							       &vis_if_list);

			buff_pos += sprintf(buff + buff_pos, "\n");
		}

		hlist_for_each_entry_safe(entry, pos, n, &vis_if_list, list) {
			hlist_del(&entry->list);
			kfree(entry);
		}
	}

	spin_unlock_irqrestore(&vis_hash_lock, flags);

	seq_printf(seq, "%s", buff);
	kfree(buff);

	return 0;
}

/* add the info packet to the send list, if it was not
 * already linked in. */
static void send_list_add(struct vis_info *info)
{
	if (list_empty(&info->send_list)) {
		kref_get(&info->refcount);
		list_add_tail(&info->send_list, &send_list);
	}
}

/* delete the info packet from the send list, if it was
 * linked in. */
static void send_list_del(struct vis_info *info)
{
	if (!list_empty(&info->send_list)) {
		list_del_init(&info->send_list);
		kref_put(&info->refcount, free_info);
	}
}

/* tries to add one entry to the receive list. */
static void recv_list_add(struct list_head *recv_list, char *mac)
{
	struct recvlist_node *entry;
	unsigned long flags;

	entry = kmalloc(sizeof(struct recvlist_node), GFP_ATOMIC);
	if (!entry)
		return;

	memcpy(entry->mac, mac, ETH_ALEN);
	spin_lock_irqsave(&recv_list_lock, flags);
	list_add_tail(&entry->list, recv_list);
	spin_unlock_irqrestore(&recv_list_lock, flags);
}

/* returns 1 if this mac is in the recv_list */
static int recv_list_is_in(struct list_head *recv_list, char *mac)
{
	struct recvlist_node *entry;
	unsigned long flags;

	spin_lock_irqsave(&recv_list_lock, flags);
	list_for_each_entry(entry, recv_list, list) {
		if (memcmp(entry->mac, mac, ETH_ALEN) == 0) {
			spin_unlock_irqrestore(&recv_list_lock, flags);
			return 1;
		}
	}
	spin_unlock_irqrestore(&recv_list_lock, flags);
	return 0;
}

/* try to add the packet to the vis_hash. return NULL if invalid (e.g. too old,
 * broken.. ).	vis hash must be locked outside.  is_new is set when the packet
 * is newer than old entries in the hash. */
static struct vis_info *add_packet(struct vis_packet *vis_packet,
				   int vis_info_len, int *is_new,
				   int make_broadcast)
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
		if (!seq_after(ntohl(vis_packet->seqno),
				ntohl(old_info->packet.seqno))) {
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
		send_list_del(old_info);
		kref_put(&old_info->refcount, free_info);
	}

	info = kmalloc(sizeof(struct vis_info) + vis_info_len, GFP_ATOMIC);
	if (info == NULL)
		return NULL;

	kref_init(&info->refcount);
	INIT_LIST_HEAD(&info->send_list);
	INIT_LIST_HEAD(&info->recv_list);
	info->first_seen = jiffies;
	memcpy(&info->packet, vis_packet,
	       sizeof(struct vis_packet) + vis_info_len);

	/* initialize and add new packet. */
	*is_new = 1;

	/* Make it a broadcast packet, if required */
	if (make_broadcast)
		memcpy(info->packet.target_orig, broadcast_addr, ETH_ALEN);

	/* repair if entries is longer than packet. */
	if (info->packet.entries * sizeof(struct vis_info_entry) > vis_info_len)
		info->packet.entries = vis_info_len /
			sizeof(struct vis_info_entry);

	recv_list_add(&info->recv_list, info->packet.sender_orig);

	/* try to add it */
	if (hash_add(vis_hash, info) < 0) {
		/* did not work (for some reason) */
		kref_put(&old_info->refcount, free_info);
		info = NULL;
	}

	return info;
}

/* handle the server sync packet, forward if needed. */
void receive_server_sync_packet(struct bat_priv *bat_priv,
				struct vis_packet *vis_packet,
				int vis_info_len)
{
	struct vis_info *info;
	int is_new, make_broadcast;
	unsigned long flags;
	int vis_server = atomic_read(&bat_priv->vis_mode);

	make_broadcast = (vis_server == VIS_TYPE_SERVER_SYNC);

	spin_lock_irqsave(&vis_hash_lock, flags);
	info = add_packet(vis_packet, vis_info_len, &is_new, make_broadcast);
	if (info == NULL)
		goto end;

	/* only if we are server ourselves and packet is newer than the one in
	 * hash.*/
	if (vis_server == VIS_TYPE_SERVER_SYNC && is_new)
		send_list_add(info);
end:
	spin_unlock_irqrestore(&vis_hash_lock, flags);
}

/* handle an incoming client update packet and schedule forward if needed. */
void receive_client_update_packet(struct bat_priv *bat_priv,
				  struct vis_packet *vis_packet,
				  int vis_info_len)
{
	struct vis_info *info;
	int is_new;
	unsigned long flags;
	int vis_server = atomic_read(&bat_priv->vis_mode);
	int are_target = 0;

	/* clients shall not broadcast. */
	if (is_bcast(vis_packet->target_orig))
		return;

	/* Are we the target for this VIS packet? */
	if (vis_server == VIS_TYPE_SERVER_SYNC	&&
	    is_my_mac(vis_packet->target_orig))
		are_target = 1;

	spin_lock_irqsave(&vis_hash_lock, flags);
	info = add_packet(vis_packet, vis_info_len, &is_new, are_target);
	if (info == NULL)
		goto end;
	/* note that outdated packets will be dropped at this point. */


	/* send only if we're the target server or ... */
	if (are_target && is_new) {
		info->packet.vis_type = VIS_TYPE_SERVER_SYNC;	/* upgrade! */
		send_list_add(info);

		/* ... we're not the recipient (and thus need to forward). */
	} else if (!is_my_mac(info->packet.target_orig)) {
		send_list_add(info);
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
static int generate_vis_packet(struct bat_priv *bat_priv)
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
	info->packet.vis_type = atomic_read(&bat_priv->vis_mode);

	spin_lock_irqsave(&orig_hash_lock, flags);
	memcpy(info->packet.target_orig, broadcast_addr, ETH_ALEN);
	info->packet.ttl = TTL;
	info->packet.seqno = htonl(ntohl(info->packet.seqno) + 1);
	info->packet.entries = 0;

	if (info->packet.vis_type == VIS_TYPE_CLIENT_UPDATE) {
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
			&& compare_orig(orig_node->router->addr,
					orig_node->orig)
			&& (orig_node->router->if_incoming->if_status ==
								IF_ACTIVE)
		    && orig_node->router->tq_avg > 0) {

			/* fill one entry into buffer. */
			entry = &entry_array[info->packet.entries];
			memcpy(entry->src,
			     orig_node->router->if_incoming->net_dev->dev_addr,
			       ETH_ALEN);
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

/* free old vis packets. Must be called with this vis_hash_lock
 * held */
static void purge_vis_packets(void)
{
	HASHIT(hashit);
	struct vis_info *info;

	while (hash_iterate(vis_hash, &hashit)) {
		info = hashit.bucket->data;
		if (info == my_vis_info)	/* never purge own data. */
			continue;
		if (time_after(jiffies,
			       info->first_seen + VIS_TIMEOUT * HZ)) {
			hash_remove_bucket(vis_hash, &hashit);
			send_list_del(info);
			kref_put(&info->refcount, free_info);
		}
	}
}

static void broadcast_vis_packet(struct vis_info *info, int packet_length)
{
	HASHIT(hashit);
	struct orig_node *orig_node;
	unsigned long flags;
	struct batman_if *batman_if;
	uint8_t dstaddr[ETH_ALEN];

	spin_lock_irqsave(&orig_hash_lock, flags);

	/* send to all routers in range. */
	while (hash_iterate(orig_hash, &hashit)) {
		orig_node = hashit.bucket->data;

		/* if it's a vis server and reachable, send it. */
		if ((!orig_node) || (!orig_node->router))
			continue;
		if (!(orig_node->flags & VIS_SERVER))
			continue;
		/* don't send it if we already received the packet from
		 * this node. */
		if (recv_list_is_in(&info->recv_list, orig_node->orig))
			continue;

		memcpy(info->packet.target_orig, orig_node->orig, ETH_ALEN);
		batman_if = orig_node->router->if_incoming;
		memcpy(dstaddr, orig_node->router->addr, ETH_ALEN);
		spin_unlock_irqrestore(&orig_hash_lock, flags);

		send_raw_packet((unsigned char *)&info->packet,
				packet_length, batman_if, dstaddr);

		spin_lock_irqsave(&orig_hash_lock, flags);

	}
	spin_unlock_irqrestore(&orig_hash_lock, flags);
	memcpy(info->packet.target_orig, broadcast_addr, ETH_ALEN);
}

static void unicast_vis_packet(struct vis_info *info, int packet_length)
{
	struct orig_node *orig_node;
	unsigned long flags;
	struct batman_if *batman_if;
	uint8_t dstaddr[ETH_ALEN];

	spin_lock_irqsave(&orig_hash_lock, flags);
	orig_node = ((struct orig_node *)
		     hash_find(orig_hash, info->packet.target_orig));

	if ((!orig_node) || (!orig_node->router))
		goto out;

	/* don't lock while sending the packets ... we therefore
	 * copy the required data before sending */
	batman_if = orig_node->router->if_incoming;
	memcpy(dstaddr, orig_node->router->addr, ETH_ALEN);
	spin_unlock_irqrestore(&orig_hash_lock, flags);

	send_raw_packet((unsigned char *)&info->packet,
			packet_length, batman_if, dstaddr);
	return;

out:
	spin_unlock_irqrestore(&orig_hash_lock, flags);
}

/* only send one vis packet. called from send_vis_packets() */
static void send_vis_packet(struct vis_info *info)
{
	int packet_length;

	if (info->packet.ttl < 2) {
		pr_warning("Error - can't send vis packet: ttl exceeded\n");
		return;
	}

	memcpy(info->packet.sender_orig, main_if_addr, ETH_ALEN);
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
	/* FIXME: each batman_if will be attached to a softif */
	struct bat_priv *bat_priv = netdev_priv(soft_device);

	spin_lock_irqsave(&vis_hash_lock, flags);

	purge_vis_packets();

	if (generate_vis_packet(bat_priv) == 0) {
		/* schedule if generation was successful */
		send_list_add(my_vis_info);
	}

	list_for_each_entry_safe(info, temp, &send_list, send_list) {

		kref_get(&info->refcount);
		spin_unlock_irqrestore(&vis_hash_lock, flags);

		send_vis_packet(info);

		spin_lock_irqsave(&vis_hash_lock, flags);
		send_list_del(info);
		kref_put(&info->refcount, free_info);
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
		pr_err("Can't initialize vis_hash\n");
		goto err;
	}

	my_vis_info = kmalloc(1000, GFP_ATOMIC);
	if (!my_vis_info) {
		pr_err("Can't initialize vis packet\n");
		goto err;
	}

	/* prefill the vis info */
	my_vis_info->first_seen = jiffies - msecs_to_jiffies(VIS_INTERVAL);
	INIT_LIST_HEAD(&my_vis_info->recv_list);
	INIT_LIST_HEAD(&my_vis_info->send_list);
	kref_init(&my_vis_info->refcount);
	my_vis_info->packet.version = COMPAT_VERSION;
	my_vis_info->packet.packet_type = BAT_VIS;
	my_vis_info->packet.ttl = TTL;
	my_vis_info->packet.seqno = 0;
	my_vis_info->packet.entries = 0;

	INIT_LIST_HEAD(&send_list);

	memcpy(my_vis_info->packet.vis_orig, main_if_addr, ETH_ALEN);
	memcpy(my_vis_info->packet.sender_orig, main_if_addr, ETH_ALEN);

	if (hash_add(vis_hash, my_vis_info) < 0) {
		pr_err("Can't add own vis packet into hash\n");
		/* not in hash, need to remove it manually. */
		kref_put(&my_vis_info->refcount, free_info);
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

/* Decrease the reference count on a hash item info */
static void free_info_ref(void *data)
{
	struct vis_info *info = data;

	send_list_del(info);
	kref_put(&info->refcount, free_info);
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
	hash_delete(vis_hash, free_info_ref);
	vis_hash = NULL;
	my_vis_info = NULL;
	spin_unlock_irqrestore(&vis_hash_lock, flags);
}

/* schedule packets for (re)transmission */
static void start_vis_timer(void)
{
	queue_delayed_work(bat_event_workqueue, &vis_timer_wq,
			   (VIS_INTERVAL * HZ) / 1000);
}
