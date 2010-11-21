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
#include "unicast.h"
#include "soft-interface.h"

static void purge_orig(struct work_struct *work);

static void start_purge_timer(struct bat_priv *bat_priv)
{
	INIT_DELAYED_WORK(&bat_priv->orig_work, purge_orig);
	queue_delayed_work(bat_event_workqueue, &bat_priv->orig_work, 1 * HZ);
}

int originator_init(struct bat_priv *bat_priv)
{
	unsigned long flags;
	if (bat_priv->orig_hash)
		return 1;

	spin_lock_irqsave(&bat_priv->orig_hash_lock, flags);
	bat_priv->orig_hash = hash_new(128, choose_orig);

	if (!bat_priv->orig_hash)
		goto err;

	spin_unlock_irqrestore(&bat_priv->orig_hash_lock, flags);
	start_purge_timer(bat_priv);
	return 1;

err:
	spin_unlock_irqrestore(&bat_priv->orig_hash_lock, flags);
	return 0;
}

struct neigh_node *
create_neighbor(struct orig_node *orig_node, struct orig_node *orig_neigh_node,
		uint8_t *neigh, struct batman_if *if_incoming)
{
	struct bat_priv *bat_priv = netdev_priv(if_incoming->soft_iface);
	struct neigh_node *neigh_node;

	bat_dbg(DBG_BATMAN, bat_priv,
		"Creating new last-hop neighbor of originator\n");

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

static void free_orig_node(void *data, void *arg)
{
	struct list_head *list_pos, *list_pos_tmp;
	struct neigh_node *neigh_node;
	struct orig_node *orig_node = (struct orig_node *)data;
	struct bat_priv *bat_priv = (struct bat_priv *)arg;

	/* for all neighbors towards this originator ... */
	list_for_each_safe(list_pos, list_pos_tmp, &orig_node->neigh_list) {
		neigh_node = list_entry(list_pos, struct neigh_node, list);

		list_del(list_pos);
		kfree(neigh_node);
	}

	frag_list_free(&orig_node->frag_list);
	hna_global_del_orig(bat_priv, orig_node, "originator timed out");

	kfree(orig_node->bcast_own);
	kfree(orig_node->bcast_own_sum);
	kfree(orig_node);
}

void originator_free(struct bat_priv *bat_priv)
{
	unsigned long flags;

	if (!bat_priv->orig_hash)
		return;

	cancel_delayed_work_sync(&bat_priv->orig_work);

	spin_lock_irqsave(&bat_priv->orig_hash_lock, flags);
	hash_delete(bat_priv->orig_hash, free_orig_node, bat_priv);
	bat_priv->orig_hash = NULL;
	spin_unlock_irqrestore(&bat_priv->orig_hash_lock, flags);
}

/* this function finds or creates an originator entry for the given
 * address if it does not exits */
struct orig_node *get_orig_node(struct bat_priv *bat_priv, uint8_t *addr)
{
	struct orig_node *orig_node;
	struct hashtable_t *swaphash;
	int size;

	orig_node = ((struct orig_node *)hash_find(bat_priv->orig_hash,
						   compare_orig, addr));

	if (orig_node)
		return orig_node;

	bat_dbg(DBG_BATMAN, bat_priv,
		"Creating new originator: %pM\n", addr);

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

	INIT_LIST_HEAD(&orig_node->frag_list);
	orig_node->last_frag_packet = 0;

	if (!orig_node->bcast_own_sum)
		goto free_bcast_own;

	if (hash_add(bat_priv->orig_hash, compare_orig, orig_node) < 0)
		goto free_bcast_own_sum;

	if (bat_priv->orig_hash->elements * 4 > bat_priv->orig_hash->size) {
		swaphash = hash_resize(bat_priv->orig_hash, compare_orig,
				       bat_priv->orig_hash->size * 2);

		if (!swaphash)
			bat_dbg(DBG_BATMAN, bat_priv,
				"Couldn't resize orig hash table\n");
		else
			bat_priv->orig_hash = swaphash;
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

static bool purge_orig_neighbors(struct bat_priv *bat_priv,
				 struct orig_node *orig_node,
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
			neigh_node->last_valid + PURGE_TIMEOUT * HZ)) ||
		    (neigh_node->if_incoming->if_status == IF_INACTIVE) ||
		    (neigh_node->if_incoming->if_status == IF_TO_BE_REMOVED)) {

			if (neigh_node->if_incoming->if_status ==
							IF_TO_BE_REMOVED)
				bat_dbg(DBG_BATMAN, bat_priv,
					"neighbor purge: originator %pM, "
					"neighbor: %pM, iface: %s\n",
					orig_node->orig, neigh_node->addr,
					neigh_node->if_incoming->net_dev->name);
			else
				bat_dbg(DBG_BATMAN, bat_priv,
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

static bool purge_orig_node(struct bat_priv *bat_priv,
			    struct orig_node *orig_node)
{
	struct neigh_node *best_neigh_node;

	if (time_after(jiffies,
		orig_node->last_valid + 2 * PURGE_TIMEOUT * HZ)) {

		bat_dbg(DBG_BATMAN, bat_priv,
			"Originator timeout: originator %pM, last_valid %lu\n",
			orig_node->orig, (orig_node->last_valid / HZ));
		return true;
	} else {
		if (purge_orig_neighbors(bat_priv, orig_node,
							&best_neigh_node)) {
			update_routes(bat_priv, orig_node,
				      best_neigh_node,
				      orig_node->hna_buff,
				      orig_node->hna_buff_len);
			/* update bonding candidates, we could have lost
			 * some candidates. */
			update_bonding_candidates(bat_priv, orig_node);
		}
	}

	return false;
}

static void _purge_orig(struct bat_priv *bat_priv)
{
	HASHIT(hashit);
	struct orig_node *orig_node;
	unsigned long flags;

	spin_lock_irqsave(&bat_priv->orig_hash_lock, flags);

	/* for all origins... */
	while (hash_iterate(bat_priv->orig_hash, &hashit)) {
		orig_node = hashit.bucket->data;

		if (purge_orig_node(bat_priv, orig_node)) {
			hash_remove_bucket(bat_priv->orig_hash, &hashit);
			free_orig_node(orig_node, bat_priv);
		}

		if (time_after(jiffies, (orig_node->last_frag_packet +
					msecs_to_jiffies(FRAG_TIMEOUT))))
			frag_list_free(&orig_node->frag_list);
	}

	spin_unlock_irqrestore(&bat_priv->orig_hash_lock, flags);

	softif_neigh_purge(bat_priv);
}

static void purge_orig(struct work_struct *work)
{
	struct delayed_work *delayed_work =
		container_of(work, struct delayed_work, work);
	struct bat_priv *bat_priv =
		container_of(delayed_work, struct bat_priv, orig_work);

	_purge_orig(bat_priv);
	start_purge_timer(bat_priv);
}

void purge_orig_ref(struct bat_priv *bat_priv)
{
	_purge_orig(bat_priv);
}

int orig_seq_print_text(struct seq_file *seq, void *offset)
{
	HASHIT(hashit);
	struct net_device *net_dev = (struct net_device *)seq->private;
	struct bat_priv *bat_priv = netdev_priv(net_dev);
	struct orig_node *orig_node;
	struct neigh_node *neigh_node;
	int batman_count = 0;
	int last_seen_secs;
	int last_seen_msecs;
	unsigned long flags;

	if ((!bat_priv->primary_if) ||
	    (bat_priv->primary_if->if_status != IF_ACTIVE)) {
		if (!bat_priv->primary_if)
			return seq_printf(seq, "BATMAN mesh %s disabled - "
				     "please specify interfaces to enable it\n",
				     net_dev->name);

		return seq_printf(seq, "BATMAN mesh %s "
				  "disabled - primary interface not active\n",
				  net_dev->name);
	}

	seq_printf(seq, "[B.A.T.M.A.N. adv %s%s, MainIF/MAC: %s/%pM (%s)]\n",
		   SOURCE_VERSION, REVISION_VERSION_STR,
		   bat_priv->primary_if->net_dev->name,
		   bat_priv->primary_if->net_dev->dev_addr, net_dev->name);
	seq_printf(seq, "  %-15s %s (%s/%i) %17s [%10s]: %20s ...\n",
		   "Originator", "last-seen", "#", TQ_MAX_VALUE, "Nexthop",
		   "outgoingIF", "Potential nexthops");

	spin_lock_irqsave(&bat_priv->orig_hash_lock, flags);

	while (hash_iterate(bat_priv->orig_hash, &hashit)) {

		orig_node = hashit.bucket->data;

		if (!orig_node->router)
			continue;

		if (orig_node->router->tq_avg == 0)
			continue;

		last_seen_secs = jiffies_to_msecs(jiffies -
						orig_node->last_valid) / 1000;
		last_seen_msecs = jiffies_to_msecs(jiffies -
						orig_node->last_valid) % 1000;

		seq_printf(seq, "%pM %4i.%03is   (%3i) %pM [%10s]:",
			   orig_node->orig, last_seen_secs, last_seen_msecs,
			   orig_node->router->tq_avg, orig_node->router->addr,
			   orig_node->router->if_incoming->net_dev->name);

		list_for_each_entry(neigh_node, &orig_node->neigh_list, list) {
			seq_printf(seq, " %pM (%3i)", neigh_node->addr,
					   neigh_node->tq_avg);
		}

		seq_printf(seq, "\n");
		batman_count++;
	}

	spin_unlock_irqrestore(&bat_priv->orig_hash_lock, flags);

	if ((batman_count == 0))
		seq_printf(seq, "No batman nodes in range ...\n");

	return 0;
}

static int orig_node_add_if(struct orig_node *orig_node, int max_if_num)
{
	void *data_ptr;

	data_ptr = kmalloc(max_if_num * sizeof(TYPE_OF_WORD) * NUM_WORDS,
			   GFP_ATOMIC);
	if (!data_ptr) {
		pr_err("Can't resize orig: out of memory\n");
		return -1;
	}

	memcpy(data_ptr, orig_node->bcast_own,
	       (max_if_num - 1) * sizeof(TYPE_OF_WORD) * NUM_WORDS);
	kfree(orig_node->bcast_own);
	orig_node->bcast_own = data_ptr;

	data_ptr = kmalloc(max_if_num * sizeof(uint8_t), GFP_ATOMIC);
	if (!data_ptr) {
		pr_err("Can't resize orig: out of memory\n");
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
	struct bat_priv *bat_priv = netdev_priv(batman_if->soft_iface);
	struct orig_node *orig_node;
	unsigned long flags;
	HASHIT(hashit);

	/* resize all orig nodes because orig_node->bcast_own(_sum) depend on
	 * if_num */
	spin_lock_irqsave(&bat_priv->orig_hash_lock, flags);

	while (hash_iterate(bat_priv->orig_hash, &hashit)) {
		orig_node = hashit.bucket->data;

		if (orig_node_add_if(orig_node, max_if_num) == -1)
			goto err;
	}

	spin_unlock_irqrestore(&bat_priv->orig_hash_lock, flags);
	return 0;

err:
	spin_unlock_irqrestore(&bat_priv->orig_hash_lock, flags);
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
		pr_err("Can't resize orig: out of memory\n");
		return -1;
	}

	/* copy first part */
	memcpy(data_ptr, orig_node->bcast_own, del_if_num * chunk_size);

	/* copy second part */
	memcpy(data_ptr + del_if_num * chunk_size,
	       orig_node->bcast_own + ((del_if_num + 1) * chunk_size),
	       (max_if_num - del_if_num) * chunk_size);

free_bcast_own:
	kfree(orig_node->bcast_own);
	orig_node->bcast_own = data_ptr;

	if (max_if_num == 0)
		goto free_own_sum;

	data_ptr = kmalloc(max_if_num * sizeof(uint8_t), GFP_ATOMIC);
	if (!data_ptr) {
		pr_err("Can't resize orig: out of memory\n");
		return -1;
	}

	memcpy(data_ptr, orig_node->bcast_own_sum,
	       del_if_num * sizeof(uint8_t));

	memcpy(data_ptr + del_if_num * sizeof(uint8_t),
	       orig_node->bcast_own_sum + ((del_if_num + 1) * sizeof(uint8_t)),
	       (max_if_num - del_if_num) * sizeof(uint8_t));

free_own_sum:
	kfree(orig_node->bcast_own_sum);
	orig_node->bcast_own_sum = data_ptr;

	return 0;
}

int orig_hash_del_if(struct batman_if *batman_if, int max_if_num)
{
	struct bat_priv *bat_priv = netdev_priv(batman_if->soft_iface);
	struct batman_if *batman_if_tmp;
	struct orig_node *orig_node;
	unsigned long flags;
	HASHIT(hashit);
	int ret;

	/* resize all orig nodes because orig_node->bcast_own(_sum) depend on
	 * if_num */
	spin_lock_irqsave(&bat_priv->orig_hash_lock, flags);

	while (hash_iterate(bat_priv->orig_hash, &hashit)) {
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

		if (batman_if->soft_iface != batman_if_tmp->soft_iface)
			continue;

		if (batman_if_tmp->if_num > batman_if->if_num)
			batman_if_tmp->if_num--;
	}
	rcu_read_unlock();

	batman_if->if_num = -1;
	spin_unlock_irqrestore(&bat_priv->orig_hash_lock, flags);
	return 0;

err:
	spin_unlock_irqrestore(&bat_priv->orig_hash_lock, flags);
	return -ENOMEM;
}
