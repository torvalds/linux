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
#include "translation-table.h"
#include "soft-interface.h"
#include "types.h"
#include "hash.h"

struct hashtable_t *hna_local_hash;
static struct hashtable_t *hna_global_hash;
atomic_t hna_local_changed;

DEFINE_SPINLOCK(hna_local_hash_lock);
static DEFINE_SPINLOCK(hna_global_hash_lock);

static void hna_local_purge(struct work_struct *work);
static DECLARE_DELAYED_WORK(hna_local_purge_wq, hna_local_purge);
static void _hna_global_del_orig(struct hna_global_entry *hna_global_entry,
				 char *message);

static void hna_local_start_timer(void)
{
	queue_delayed_work(bat_event_workqueue, &hna_local_purge_wq, 10 * HZ);
}

int hna_local_init(void)
{
	if (hna_local_hash)
		return 1;

	hna_local_hash = hash_new(128, compare_orig, choose_orig);

	if (!hna_local_hash)
		return 0;

	atomic_set(&hna_local_changed, 0);
	hna_local_start_timer();

	return 1;
}

void hna_local_add(uint8_t *addr)
{
	/* FIXME: each orig_node->batman_if will be attached to a softif */
	struct bat_priv *bat_priv = netdev_priv(soft_device);
	struct hna_local_entry *hna_local_entry;
	struct hna_global_entry *hna_global_entry;
	struct hashtable_t *swaphash;
	unsigned long flags;

	spin_lock_irqsave(&hna_local_hash_lock, flags);
	hna_local_entry =
		((struct hna_local_entry *)hash_find(hna_local_hash, addr));
	spin_unlock_irqrestore(&hna_local_hash_lock, flags);

	if (hna_local_entry != NULL) {
		hna_local_entry->last_seen = jiffies;
		return;
	}

	/* only announce as many hosts as possible in the batman-packet and
	   space in batman_packet->num_hna That also should give a limit to
	   MAC-flooding. */
	if ((num_hna + 1 > (ETH_DATA_LEN - BAT_PACKET_LEN) / ETH_ALEN) ||
	    (num_hna + 1 > 255)) {
		bat_dbg(DBG_ROUTES, bat_priv,
			"Can't add new local hna entry (%pM): "
			"number of local hna entries exceeds packet size\n",
			addr);
		return;
	}

	bat_dbg(DBG_ROUTES, bat_priv,
		"Creating new local hna entry: %pM\n", addr);

	hna_local_entry = kmalloc(sizeof(struct hna_local_entry), GFP_ATOMIC);
	if (!hna_local_entry)
		return;

	memcpy(hna_local_entry->addr, addr, ETH_ALEN);
	hna_local_entry->last_seen = jiffies;

	/* the batman interface mac address should never be purged */
	if (compare_orig(addr, soft_device->dev_addr))
		hna_local_entry->never_purge = 1;
	else
		hna_local_entry->never_purge = 0;

	spin_lock_irqsave(&hna_local_hash_lock, flags);

	hash_add(hna_local_hash, hna_local_entry);
	num_hna++;
	atomic_set(&hna_local_changed, 1);

	if (hna_local_hash->elements * 4 > hna_local_hash->size) {
		swaphash = hash_resize(hna_local_hash,
				       hna_local_hash->size * 2);

		if (swaphash == NULL)
			pr_err("Couldn't resize local hna hash table\n");
		else
			hna_local_hash = swaphash;
	}

	spin_unlock_irqrestore(&hna_local_hash_lock, flags);

	/* remove address from global hash if present */
	spin_lock_irqsave(&hna_global_hash_lock, flags);

	hna_global_entry =
		((struct hna_global_entry *)hash_find(hna_global_hash, addr));

	if (hna_global_entry != NULL)
		_hna_global_del_orig(hna_global_entry, "local hna received");

	spin_unlock_irqrestore(&hna_global_hash_lock, flags);
}

int hna_local_fill_buffer(unsigned char *buff, int buff_len)
{
	struct hna_local_entry *hna_local_entry;
	HASHIT(hashit);
	int i = 0;
	unsigned long flags;

	spin_lock_irqsave(&hna_local_hash_lock, flags);

	while (hash_iterate(hna_local_hash, &hashit)) {

		if (buff_len < (i + 1) * ETH_ALEN)
			break;

		hna_local_entry = hashit.bucket->data;
		memcpy(buff + (i * ETH_ALEN), hna_local_entry->addr, ETH_ALEN);

		i++;
	}

	/* if we did not get all new local hnas see you next time  ;-) */
	if (i == num_hna)
		atomic_set(&hna_local_changed, 0);

	spin_unlock_irqrestore(&hna_local_hash_lock, flags);

	return i;
}

int hna_local_seq_print_text(struct seq_file *seq, void *offset)
{
	struct net_device *net_dev = (struct net_device *)seq->private;
	struct bat_priv *bat_priv = netdev_priv(net_dev);
	struct hna_local_entry *hna_local_entry;
	HASHIT(hashit);
	HASHIT(hashit_count);
	unsigned long flags;
	size_t buf_size, pos;
	char *buff;

	if (!bat_priv->primary_if) {
		return seq_printf(seq, "BATMAN mesh %s disabled - "
			       "please specify interfaces to enable it\n",
			       net_dev->name);
	}

	seq_printf(seq, "Locally retrieved addresses (from %s) "
		   "announced via HNA:\n",
		   net_dev->name);

	spin_lock_irqsave(&hna_local_hash_lock, flags);

	buf_size = 1;
	/* Estimate length for: " * xx:xx:xx:xx:xx:xx\n" */
	while (hash_iterate(hna_local_hash, &hashit_count))
		buf_size += 21;

	buff = kmalloc(buf_size, GFP_ATOMIC);
	if (!buff) {
		spin_unlock_irqrestore(&hna_local_hash_lock, flags);
		return -ENOMEM;
	}
	buff[0] = '\0';
	pos = 0;

	while (hash_iterate(hna_local_hash, &hashit)) {
		hna_local_entry = hashit.bucket->data;

		pos += snprintf(buff + pos, 22, " * %pM\n",
				hna_local_entry->addr);
	}

	spin_unlock_irqrestore(&hna_local_hash_lock, flags);

	seq_printf(seq, "%s", buff);
	kfree(buff);
	return 0;
}

static void _hna_local_del(void *data)
{
	kfree(data);
	num_hna--;
	atomic_set(&hna_local_changed, 1);
}

static void hna_local_del(struct hna_local_entry *hna_local_entry,
			  char *message)
{
	/* FIXME: each orig_node->batman_if will be attached to a softif */
	struct bat_priv *bat_priv = netdev_priv(soft_device);
	bat_dbg(DBG_ROUTES, bat_priv, "Deleting local hna entry (%pM): %s\n",
		hna_local_entry->addr, message);

	hash_remove(hna_local_hash, hna_local_entry->addr);
	_hna_local_del(hna_local_entry);
}

void hna_local_remove(uint8_t *addr, char *message)
{
	struct hna_local_entry *hna_local_entry;
	unsigned long flags;

	spin_lock_irqsave(&hna_local_hash_lock, flags);

	hna_local_entry = (struct hna_local_entry *)
		hash_find(hna_local_hash, addr);
	if (hna_local_entry)
		hna_local_del(hna_local_entry, message);

	spin_unlock_irqrestore(&hna_local_hash_lock, flags);
}

static void hna_local_purge(struct work_struct *work)
{
	struct hna_local_entry *hna_local_entry;
	HASHIT(hashit);
	unsigned long flags;
	unsigned long timeout;

	spin_lock_irqsave(&hna_local_hash_lock, flags);

	while (hash_iterate(hna_local_hash, &hashit)) {
		hna_local_entry = hashit.bucket->data;

		timeout = hna_local_entry->last_seen + LOCAL_HNA_TIMEOUT * HZ;
		if ((!hna_local_entry->never_purge) &&
		    time_after(jiffies, timeout))
			hna_local_del(hna_local_entry, "address timed out");
	}

	spin_unlock_irqrestore(&hna_local_hash_lock, flags);
	hna_local_start_timer();
}

void hna_local_free(void)
{
	if (!hna_local_hash)
		return;

	cancel_delayed_work_sync(&hna_local_purge_wq);
	hash_delete(hna_local_hash, _hna_local_del);
	hna_local_hash = NULL;
}

int hna_global_init(void)
{
	if (hna_global_hash)
		return 1;

	hna_global_hash = hash_new(128, compare_orig, choose_orig);

	if (!hna_global_hash)
		return 0;

	return 1;
}

void hna_global_add_orig(struct orig_node *orig_node,
			 unsigned char *hna_buff, int hna_buff_len)
{
	/* FIXME: each orig_node->batman_if will be attached to a softif */
	struct bat_priv *bat_priv = netdev_priv(soft_device);
	struct hna_global_entry *hna_global_entry;
	struct hna_local_entry *hna_local_entry;
	struct hashtable_t *swaphash;
	int hna_buff_count = 0;
	unsigned long flags;
	unsigned char *hna_ptr;

	while ((hna_buff_count + 1) * ETH_ALEN <= hna_buff_len) {
		spin_lock_irqsave(&hna_global_hash_lock, flags);

		hna_ptr = hna_buff + (hna_buff_count * ETH_ALEN);
		hna_global_entry = (struct hna_global_entry *)
			hash_find(hna_global_hash, hna_ptr);

		if (hna_global_entry == NULL) {
			spin_unlock_irqrestore(&hna_global_hash_lock, flags);

			hna_global_entry =
				kmalloc(sizeof(struct hna_global_entry),
					GFP_ATOMIC);

			if (!hna_global_entry)
				break;

			memcpy(hna_global_entry->addr, hna_ptr, ETH_ALEN);

			bat_dbg(DBG_ROUTES, bat_priv,
				"Creating new global hna entry: "
				"%pM (via %pM)\n",
				hna_global_entry->addr, orig_node->orig);

			spin_lock_irqsave(&hna_global_hash_lock, flags);
			hash_add(hna_global_hash, hna_global_entry);

		}

		hna_global_entry->orig_node = orig_node;
		spin_unlock_irqrestore(&hna_global_hash_lock, flags);

		/* remove address from local hash if present */
		spin_lock_irqsave(&hna_local_hash_lock, flags);

		hna_ptr = hna_buff + (hna_buff_count * ETH_ALEN);
		hna_local_entry = (struct hna_local_entry *)
			hash_find(hna_local_hash, hna_ptr);

		if (hna_local_entry != NULL)
			hna_local_del(hna_local_entry, "global hna received");

		spin_unlock_irqrestore(&hna_local_hash_lock, flags);

		hna_buff_count++;
	}

	/* initialize, and overwrite if malloc succeeds */
	orig_node->hna_buff = NULL;
	orig_node->hna_buff_len = 0;

	if (hna_buff_len > 0) {
		orig_node->hna_buff = kmalloc(hna_buff_len, GFP_ATOMIC);
		if (orig_node->hna_buff) {
			memcpy(orig_node->hna_buff, hna_buff, hna_buff_len);
			orig_node->hna_buff_len = hna_buff_len;
		}
	}

	spin_lock_irqsave(&hna_global_hash_lock, flags);

	if (hna_global_hash->elements * 4 > hna_global_hash->size) {
		swaphash = hash_resize(hna_global_hash,
				       hna_global_hash->size * 2);

		if (swaphash == NULL)
			pr_err("Couldn't resize global hna hash table\n");
		else
			hna_global_hash = swaphash;
	}

	spin_unlock_irqrestore(&hna_global_hash_lock, flags);
}

int hna_global_seq_print_text(struct seq_file *seq, void *offset)
{
	struct net_device *net_dev = (struct net_device *)seq->private;
	struct bat_priv *bat_priv = netdev_priv(net_dev);
	struct hna_global_entry *hna_global_entry;
	HASHIT(hashit);
	HASHIT(hashit_count);
	unsigned long flags;
	size_t buf_size, pos;
	char *buff;

	if (!bat_priv->primary_if) {
		return seq_printf(seq, "BATMAN mesh %s disabled - "
				  "please specify interfaces to enable it\n",
				  net_dev->name);
	}

	seq_printf(seq, "Globally announced HNAs received via the mesh %s\n",
		   net_dev->name);

	spin_lock_irqsave(&hna_global_hash_lock, flags);

	buf_size = 1;
	/* Estimate length for: " * xx:xx:xx:xx:xx:xx via xx:xx:xx:xx:xx:xx\n"*/
	while (hash_iterate(hna_global_hash, &hashit_count))
		buf_size += 43;

	buff = kmalloc(buf_size, GFP_ATOMIC);
	if (!buff) {
		spin_unlock_irqrestore(&hna_global_hash_lock, flags);
		return -ENOMEM;
	}
	buff[0] = '\0';
	pos = 0;

	while (hash_iterate(hna_global_hash, &hashit)) {
		hna_global_entry = hashit.bucket->data;

		pos += snprintf(buff + pos, 44,
				" * %pM via %pM\n", hna_global_entry->addr,
				hna_global_entry->orig_node->orig);
	}

	spin_unlock_irqrestore(&hna_global_hash_lock, flags);

	seq_printf(seq, "%s", buff);
	kfree(buff);
	return 0;
}

static void _hna_global_del_orig(struct hna_global_entry *hna_global_entry,
				 char *message)
{
	/* FIXME: each orig_node->batman_if will be attached to a softif */
	struct bat_priv *bat_priv = netdev_priv(soft_device);
	bat_dbg(DBG_ROUTES, bat_priv,
		"Deleting global hna entry %pM (via %pM): %s\n",
		hna_global_entry->addr, hna_global_entry->orig_node->orig,
		message);

	hash_remove(hna_global_hash, hna_global_entry->addr);
	kfree(hna_global_entry);
}

void hna_global_del_orig(struct orig_node *orig_node, char *message)
{
	struct hna_global_entry *hna_global_entry;
	int hna_buff_count = 0;
	unsigned long flags;
	unsigned char *hna_ptr;

	if (orig_node->hna_buff_len == 0)
		return;

	spin_lock_irqsave(&hna_global_hash_lock, flags);

	while ((hna_buff_count + 1) * ETH_ALEN <= orig_node->hna_buff_len) {
		hna_ptr = orig_node->hna_buff + (hna_buff_count * ETH_ALEN);
		hna_global_entry = (struct hna_global_entry *)
			hash_find(hna_global_hash, hna_ptr);

		if ((hna_global_entry != NULL) &&
		    (hna_global_entry->orig_node == orig_node))
			_hna_global_del_orig(hna_global_entry, message);

		hna_buff_count++;
	}

	spin_unlock_irqrestore(&hna_global_hash_lock, flags);

	orig_node->hna_buff_len = 0;
	kfree(orig_node->hna_buff);
	orig_node->hna_buff = NULL;
}

static void hna_global_del(void *data)
{
	kfree(data);
}

void hna_global_free(void)
{
	if (!hna_global_hash)
		return;

	hash_delete(hna_global_hash, hna_global_del);
	hna_global_hash = NULL;
}

struct orig_node *transtable_search(uint8_t *addr)
{
	struct hna_global_entry *hna_global_entry;
	unsigned long flags;

	spin_lock_irqsave(&hna_global_hash_lock, flags);
	hna_global_entry = (struct hna_global_entry *)
		hash_find(hna_global_hash, addr);
	spin_unlock_irqrestore(&hna_global_hash_lock, flags);

	if (hna_global_entry == NULL)
		return NULL;

	return hna_global_entry->orig_node;
}
