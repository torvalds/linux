/*
 * Copyright (C) 2009-2010 B.A.T.M.A.N. contributors:
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

/* increase the reference counter for this originator */

#include "main.h"
#include "originator.h"
#include "hash.h"
#include "translation-table.h"
#include "routing.h"
#include "hard-interface.h"

static DECLARE_DELAYED_WORK(purge_orig_wq, purge_orig);

static void start_purge_timer(void)
{
	queue_delayed_work(bat_event_workqueue, &purge_orig_wq, 1 * HZ);
}

int originator_init(void)
{
	unsigned long flags;
	if (orig_hash)
		return 1;

	spin_lock_irqsave(&orig_hash_lock, flags);
	orig_hash = hash_new(128, compare_orig, choose_orig);

	if (!orig_hash)
		goto err;

	spin_unlock_irqrestore(&orig_hash_lock, flags);
	start_purge_timer();
	return 1;

err:
	spin_unlock_irqrestore(&orig_hash_lock, flags);
	return 0;
}

void originator_free(void)
{
	unsigned long flags;

	if (!orig_hash)
		return;

	cancel_delayed_work_sync(&purge_orig_wq);

	spin_lock_irqsave(&orig_hash_lock, flags);
	hash_delete(orig_hash, free_orig_node);
	orig_hash = NULL;
	spin_unlock_irqrestore(&orig_hash_lock, flags);
}

struct neigh_node *
create_neighbor(struct orig_node *orig_node, struct orig_node *orig_neigh_node,
		uint8_t *neigh, struct batman_if *if_incoming)
{
	struct neigh_node *neigh_node;

	bat_dbg(DBG_BATMAN, "Creating new last-hop neighbor of originator\n");

	neigh_node = kzalloc(sizeof(struct neigh_node), GFP_ATOMIC);
	if (!neigh_node)
		return NULL;

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

	/* for all neighbors towards this originator ... */
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

/* this function finds or creates an originator entry for the given
 * address if it does not exits */
struct orig_node *get_orig_node(uint8_t *addr)
{
	/* FIXME: each batman_if will be attached to a softif */
	struct bat_priv *bat_priv = netdev_priv(soft_device);
	struct orig_node *orig_node;
	struct hashtable_t *swaphash;
	int size;

	orig_node = ((struct orig_node *)hash_find(orig_hash, addr));

	if (orig_node != NULL)
		return orig_node;

	bat_dbg(DBG_BATMAN, "Creating new originator: %pM\n", addr);

	orig_node = kzalloc(sizeof(struct orig_node), GFP_ATOMIC);
	if (!orig_node)
		return NULL;

	INIT_LIST_HEAD(&orig_node->neigh_list);

	memcpy(orig_node->orig, addr, ETH_ALEN);
	orig_node->router = NULL;
	orig_node->hna_buff = NULL;
	orig_node->bcast_seqno_reset = jiffies - 1
					- msecs_to_jiffies(RESET_PROTECTION_MS);
	orig_node->batman_seqno_reset = jiffies - 1
					- msecs_to_jiffies(RESET_PROTECTION_MS);

	size = bat_priv->num_ifaces * sizeof(TYPE_OF_WORD) * NUM_WORDS;

	orig_node->bcast_own = kzalloc(size, GFP_ATOMIC);
	if (!orig_node->bcast_own)
		goto free_orig_node;

	size = bat_priv->num_ifaces * sizeof(uint8_t);
	orig_node->bcast_own_sum = kzalloc(size, GFP_ATOMIC);
	if (!orig_node->bcast_own_sum)
		goto free_bcast_own;

	if (hash_add(orig_hash, orig_node) < 0)
		goto free_bcast_own_sum;

	if (orig_hash->elements * 4 > orig_hash->size) {
		swaphash = hash_resize(orig_hash, orig_hash->size * 2);

		if (swaphash == NULL)
			printk(KERN_ERR
			       "batman-adv:Couldn't resize orig hash table\n");
		else
			orig_hash = swaphash;
	}

	return orig_node;
free_bcast_own_sum:
	kfree(orig_node->bcast_own_sum);
free_bcast_own:
	kfree(orig_node->bcast_own);
free_orig_node:
	kfree(orig_node);
	return NULL;
}

static bool purge_orig_neighbors(struct orig_node *orig_node,
				 struct neigh_node **best_neigh_node)
{
	struct list_head *list_pos, *list_pos_tmp;
	struct neigh_node *neigh_node;
	bool neigh_purged = false;

	*best_neigh_node = NULL;

	/* for all neighbors towards this originator ... */
	list_for_each_safe(list_pos, list_pos_tmp, &orig_node->neigh_list) {
		neigh_node = list_entry(list_pos, struct neigh_node, list);

		if ((time_after(jiffies,
			       (neigh_node->last_valid +
				((PURGE_TIMEOUT * HZ) / 1000)))) ||
		    (neigh_node->if_incoming->if_status ==
						IF_TO_BE_REMOVED)) {

			if (neigh_node->if_incoming->if_status ==
							IF_TO_BE_REMOVED)
				bat_dbg(DBG_BATMAN,
					"neighbor purge: originator %pM, "
					"neighbor: %pM, iface: %s\n",
					orig_node->orig, neigh_node->addr,
					neigh_node->if_incoming->dev);
			else
				bat_dbg(DBG_BATMAN,
					"neighbor timeout: originator %pM, "
					"neighbor: %pM, last_valid: %lu\n",
					orig_node->orig, neigh_node->addr,
					(neigh_node->last_valid / HZ));

			neigh_purged = true;
			list_del(list_pos);
			kfree(neigh_node);
		} else {
			if ((*best_neigh_node == NULL) ||
			    (neigh_node->tq_avg > (*best_neigh_node)->tq_avg))
				*best_neigh_node = neigh_node;
		}
	}
	return neigh_purged;
}

static bool purge_orig_node(struct orig_node *orig_node)
{
	struct neigh_node *best_neigh_node;

	if (time_after(jiffies,
		       (orig_node->last_valid +
			((2 * PURGE_TIMEOUT * HZ) / 1000)))) {

		bat_dbg(DBG_BATMAN,
			"Originator timeout: originator %pM, last_valid %lu\n",
			orig_node->orig, (orig_node->last_valid / HZ));
		return true;
	} else {
		if (purge_orig_neighbors(orig_node, &best_neigh_node))
			update_routes(orig_node, best_neigh_node,
				      orig_node->hna_buff,
				      orig_node->hna_buff_len);
	}

	return false;
}

void purge_orig(struct work_struct *work)
{
	HASHIT(hashit);
	struct orig_node *orig_node;
	unsigned long flags;

	spin_lock_irqsave(&orig_hash_lock, flags);

	/* for all origins... */
	while (hash_iterate(orig_hash, &hashit)) {
		orig_node = hashit.bucket->data;
		if (purge_orig_node(orig_node)) {
			hash_remove_bucket(orig_hash, &hashit);
			free_orig_node(orig_node);
		}
	}

	spin_unlock_irqrestore(&orig_hash_lock, flags);

	/* if work == NULL we were not called by the timer
	 * and thus do not need to re-arm the timer */
	if (work)
		start_purge_timer();
}

ssize_t orig_fill_buffer_text(struct net_device *net_dev, char *buff,
			      size_t count, loff_t off)
{
	HASHIT(hashit);
	struct bat_priv *bat_priv = netdev_priv(net_dev);
	struct orig_node *orig_node;
	struct neigh_node *neigh_node;
	size_t hdr_len, tmp_len;
	int batman_count = 0, bytes_written = 0;
	unsigned long flags;
	char orig_str[ETH_STR_LEN], router_str[ETH_STR_LEN];

	if (!bat_priv->primary_if) {
		if (off == 0)
			return sprintf(buff,
				     "BATMAN mesh %s disabled - "
				     "please specify interfaces to enable it\n",
				     net_dev->name);

		return 0;
	}

	if (bat_priv->primary_if->if_status != IF_ACTIVE && off == 0)
		return sprintf(buff,
			       "BATMAN mesh %s "
			       "disabled - primary interface not active\n",
			       net_dev->name);
	else if (bat_priv->primary_if->if_status != IF_ACTIVE)
		return 0;

	rcu_read_lock();
	hdr_len = sprintf(buff,
		   "  %-14s (%s/%i) %17s [%10s]: %20s "
		   "... [B.A.T.M.A.N. adv %s%s, MainIF/MAC: %s/%s (%s)]\n",
		   "Originator", "#", TQ_MAX_VALUE, "Nexthop", "outgoingIF",
		   "Potential nexthops", SOURCE_VERSION, REVISION_VERSION_STR,
		   bat_priv->primary_if->dev, bat_priv->primary_if->addr_str,
		   net_dev->name);
	rcu_read_unlock();

	if (off < hdr_len)
		bytes_written = hdr_len;

	spin_lock_irqsave(&orig_hash_lock, flags);

	while (hash_iterate(orig_hash, &hashit)) {

		orig_node = hashit.bucket->data;

		if (!orig_node->router)
			continue;

		if (orig_node->router->tq_avg == 0)
			continue;

		/* estimated line length */
		if (count < bytes_written + 200)
			break;

		addr_to_string(orig_str, orig_node->orig);
		addr_to_string(router_str, orig_node->router->addr);

		tmp_len = sprintf(buff + bytes_written,
				  "%-17s  (%3i) %17s [%10s]:",
				   orig_str, orig_node->router->tq_avg,
				   router_str,
				   orig_node->router->if_incoming->dev);

		list_for_each_entry(neigh_node, &orig_node->neigh_list, list) {
			addr_to_string(orig_str, neigh_node->addr);
			tmp_len += sprintf(buff + bytes_written + tmp_len,
					   " %17s (%3i)", orig_str,
					   neigh_node->tq_avg);
		}

		tmp_len += sprintf(buff + bytes_written + tmp_len, "\n");

		batman_count++;
		hdr_len += tmp_len;

		if (off >= hdr_len)
			continue;

		bytes_written += tmp_len;
	}

	spin_unlock_irqrestore(&orig_hash_lock, flags);

	if ((batman_count == 0) && (off == 0))
		bytes_written += sprintf(buff + bytes_written,
					"No batman nodes in range ...\n");

	return bytes_written;
}

static int orig_node_add_if(struct orig_node *orig_node, int max_if_num)
{
	void *data_ptr;

	data_ptr = kmalloc(max_if_num * sizeof(TYPE_OF_WORD) * NUM_WORDS,
			   GFP_ATOMIC);
	if (!data_ptr) {
		printk(KERN_ERR
		       "batman-adv:Can't resize orig: out of memory\n");
		return -1;
	}

	memcpy(data_ptr, orig_node->bcast_own,
	       (max_if_num - 1) * sizeof(TYPE_OF_WORD) * NUM_WORDS);
	kfree(orig_node->bcast_own);
	orig_node->bcast_own = data_ptr;

	data_ptr = kmalloc(max_if_num * sizeof(uint8_t), GFP_ATOMIC);
	if (!data_ptr) {
		printk(KERN_ERR
		       "batman-adv:Can't resize orig: out of memory\n");
		return -1;
	}

	memcpy(data_ptr, orig_node->bcast_own_sum,
	       (max_if_num - 1) * sizeof(uint8_t));
	kfree(orig_node->bcast_own_sum);
	orig_node->bcast_own_sum = data_ptr;

	return 0;
}

int orig_hash_add_if(struct batman_if *batman_if, int max_if_num)
{
	struct orig_node *orig_node;
	HASHIT(hashit);

	/* resize all orig nodes because orig_node->bcast_own(_sum) depend on
	 * if_num */
	spin_lock(&orig_hash_lock);

	while (hash_iterate(orig_hash, &hashit)) {
		orig_node = hashit.bucket->data;

		if (orig_node_add_if(orig_node, max_if_num) == -1)
			goto err;
	}

	spin_unlock(&orig_hash_lock);
	return 0;

err:
	spin_unlock(&orig_hash_lock);
	return -ENOMEM;
}

static int orig_node_del_if(struct orig_node *orig_node,
		     int max_if_num, int del_if_num)
{
	void *data_ptr = NULL;
	int chunk_size;

	/* last interface was removed */
	if (max_if_num == 0)
		goto free_bcast_own;

	chunk_size = sizeof(TYPE_OF_WORD) * NUM_WORDS;
	data_ptr = kmalloc(max_if_num * chunk_size, GFP_ATOMIC);
	if (!data_ptr) {
		printk(KERN_ERR
		       "batman-adv:Can't resize orig: out of memory\n");
		return -1;
	}

	/* copy first part */
	memcpy(data_ptr, orig_node->bcast_own, del_if_num * chunk_size);

	/* copy second part */
	memcpy(data_ptr,
	       orig_node->bcast_own + ((del_if_num + 1) * chunk_size),
	       (max_if_num - del_if_num) * chunk_size);

free_bcast_own:
	kfree(orig_node->bcast_own);
	orig_node->bcast_own = data_ptr;

	if (max_if_num == 0)
		goto free_own_sum;

	data_ptr = kmalloc(max_if_num * sizeof(uint8_t), GFP_ATOMIC);
	if (!data_ptr) {
		printk(KERN_ERR
		       "batman-adv:Can't resize orig: out of memory\n");
		return -1;
	}

	memcpy(data_ptr, orig_node->bcast_own_sum,
	       del_if_num * sizeof(uint8_t));

	memcpy(data_ptr,
	       orig_node->bcast_own_sum + ((del_if_num + 1) * sizeof(uint8_t)),
	       (max_if_num - del_if_num) * sizeof(uint8_t));

free_own_sum:
	kfree(orig_node->bcast_own_sum);
	orig_node->bcast_own_sum = data_ptr;

	return 0;
}

int orig_hash_del_if(struct batman_if *batman_if, int max_if_num)
{
	struct batman_if *batman_if_tmp;
	struct orig_node *orig_node;
	HASHIT(hashit);
	int ret;

	/* resize all orig nodes because orig_node->bcast_own(_sum) depend on
	 * if_num */
	spin_lock(&orig_hash_lock);

	while (hash_iterate(orig_hash, &hashit)) {
		orig_node = hashit.bucket->data;

		ret = orig_node_del_if(orig_node, max_if_num,
				       batman_if->if_num);

		if (ret == -1)
			goto err;
	}

	/* renumber remaining batman interfaces _inside_ of orig_hash_lock */
	rcu_read_lock();
	list_for_each_entry_rcu(batman_if_tmp, &if_list, list) {
		if (batman_if_tmp->if_status == IF_NOT_IN_USE)
			continue;

		if (batman_if == batman_if_tmp)
			continue;

		if (batman_if_tmp->if_num > batman_if->if_num)
			batman_if_tmp->if_num--;
	}
	rcu_read_unlock();

	batman_if->if_num = -1;
	spin_unlock(&orig_hash_lock);
	return 0;

err:
	spin_unlock(&orig_hash_lock);
	return -ENOMEM;
}
