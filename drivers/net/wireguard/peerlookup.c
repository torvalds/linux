// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2015-2019 Jason A. Donenfeld <Jason@zx2c4.com>. All Rights Reserved.
 */

#include "peerlookup.h"
#include "peer.h"
#include "noise.h"

static struct hlist_head *pubkey_bucket(struct pubkey_hashtable *table,
					const u8 pubkey[NOISE_PUBLIC_KEY_LEN])
{
	/* siphash gives us a secure 64bit number based on a random key. Since
	 * the bits are uniformly distributed, we can then mask off to get the
	 * bits we need.
	 */
	const u64 hash = siphash(pubkey, NOISE_PUBLIC_KEY_LEN, &table->key);

	return &table->hashtable[hash & (HASH_SIZE(table->hashtable) - 1)];
}

struct pubkey_hashtable *wg_pubkey_hashtable_alloc(void)
{
	struct pubkey_hashtable *table = kvmalloc(sizeof(*table), GFP_KERNEL);

	if (!table)
		return NULL;

	get_random_bytes(&table->key, sizeof(table->key));
	hash_init(table->hashtable);
	mutex_init(&table->lock);
	return table;
}

void wg_pubkey_hashtable_add(struct pubkey_hashtable *table,
			     struct wg_peer *peer)
{
	mutex_lock(&table->lock);
	hlist_add_head_rcu(&peer->pubkey_hash,
			   pubkey_bucket(table, peer->handshake.remote_static));
	mutex_unlock(&table->lock);
}

void wg_pubkey_hashtable_remove(struct pubkey_hashtable *table,
				struct wg_peer *peer)
{
	mutex_lock(&table->lock);
	hlist_del_init_rcu(&peer->pubkey_hash);
	mutex_unlock(&table->lock);
}

/* Returns a strong reference to a peer */
struct wg_peer *
wg_pubkey_hashtable_lookup(struct pubkey_hashtable *table,
			   const u8 pubkey[NOISE_PUBLIC_KEY_LEN])
{
	struct wg_peer *iter_peer, *peer = NULL;

	rcu_read_lock_bh();
	hlist_for_each_entry_rcu_bh(iter_peer, pubkey_bucket(table, pubkey),
				    pubkey_hash) {
		if (!memcmp(pubkey, iter_peer->handshake.remote_static,
			    NOISE_PUBLIC_KEY_LEN)) {
			peer = iter_peer;
			break;
		}
	}
	peer = wg_peer_get_maybe_zero(peer);
	rcu_read_unlock_bh();
	return peer;
}

static struct hlist_head *index_bucket(struct index_hashtable *table,
				       const __le32 index)
{
	/* Since the indices are random and thus all bits are uniformly
	 * distributed, we can find its bucket simply by masking.
	 */
	return &table->hashtable[(__force u32)index &
				 (HASH_SIZE(table->hashtable) - 1)];
}

struct index_hashtable *wg_index_hashtable_alloc(void)
{
	struct index_hashtable *table = kvmalloc(sizeof(*table), GFP_KERNEL);

	if (!table)
		return NULL;

	hash_init(table->hashtable);
	spin_lock_init(&table->lock);
	return table;
}

/* At the moment, we limit ourselves to 2^20 total peers, which generally might
 * amount to 2^20*3 items in this hashtable. The algorithm below works by
 * picking a random number and testing it. We can see that these limits mean we
 * usually succeed pretty quickly:
 *
 * >>> def calculation(tries, size):
 * ...     return (size / 2**32)**(tries - 1) *  (1 - (size / 2**32))
 * ...
 * >>> calculation(1, 2**20 * 3)
 * 0.999267578125
 * >>> calculation(2, 2**20 * 3)
 * 0.0007318854331970215
 * >>> calculation(3, 2**20 * 3)
 * 5.360489012673497e-07
 * >>> calculation(4, 2**20 * 3)
 * 3.9261394135792216e-10
 *
 * At the moment, we don't do any masking, so this algorithm isn't exactly
 * constant time in either the random guessing or in the hash list lookup. We
 * could require a minimum of 3 tries, which would successfully mask the
 * guessing. this would not, however, help with the growing hash lengths, which
 * is another thing to consider moving forward.
 */

__le32 wg_index_hashtable_insert(struct index_hashtable *table,
				 struct index_hashtable_entry *entry)
{
	struct index_hashtable_entry *existing_entry;

	spin_lock_bh(&table->lock);
	hlist_del_init_rcu(&entry->index_hash);
	spin_unlock_bh(&table->lock);

	rcu_read_lock_bh();

search_unused_slot:
	/* First we try to find an unused slot, randomly, while unlocked. */
	entry->index = (__force __le32)get_random_u32();
	hlist_for_each_entry_rcu_bh(existing_entry,
				    index_bucket(table, entry->index),
				    index_hash) {
		if (existing_entry->index == entry->index)
			/* If it's already in use, we continue searching. */
			goto search_unused_slot;
	}

	/* Once we've found an unused slot, we lock it, and then double-check
	 * that nobody else stole it from us.
	 */
	spin_lock_bh(&table->lock);
	hlist_for_each_entry_rcu_bh(existing_entry,
				    index_bucket(table, entry->index),
				    index_hash) {
		if (existing_entry->index == entry->index) {
			spin_unlock_bh(&table->lock);
			/* If it was stolen, we start over. */
			goto search_unused_slot;
		}
	}
	/* Otherwise, we know we have it exclusively (since we're locked),
	 * so we insert.
	 */
	hlist_add_head_rcu(&entry->index_hash,
			   index_bucket(table, entry->index));
	spin_unlock_bh(&table->lock);

	rcu_read_unlock_bh();

	return entry->index;
}

bool wg_index_hashtable_replace(struct index_hashtable *table,
				struct index_hashtable_entry *old,
				struct index_hashtable_entry *new)
{
	if (unlikely(hlist_unhashed(&old->index_hash)))
		return false;
	spin_lock_bh(&table->lock);
	new->index = old->index;
	hlist_replace_rcu(&old->index_hash, &new->index_hash);

	/* Calling init here NULLs out index_hash, and in fact after this
	 * function returns, it's theoretically possible for this to get
	 * reinserted elsewhere. That means the RCU lookup below might either
	 * terminate early or jump between buckets, in which case the packet
	 * simply gets dropped, which isn't terrible.
	 */
	INIT_HLIST_NODE(&old->index_hash);
	spin_unlock_bh(&table->lock);
	return true;
}

void wg_index_hashtable_remove(struct index_hashtable *table,
			       struct index_hashtable_entry *entry)
{
	spin_lock_bh(&table->lock);
	hlist_del_init_rcu(&entry->index_hash);
	spin_unlock_bh(&table->lock);
}

/* Returns a strong reference to a entry->peer */
struct index_hashtable_entry *
wg_index_hashtable_lookup(struct index_hashtable *table,
			  const enum index_hashtable_type type_mask,
			  const __le32 index, struct wg_peer **peer)
{
	struct index_hashtable_entry *iter_entry, *entry = NULL;

	rcu_read_lock_bh();
	hlist_for_each_entry_rcu_bh(iter_entry, index_bucket(table, index),
				    index_hash) {
		if (iter_entry->index == index) {
			if (likely(iter_entry->type & type_mask))
				entry = iter_entry;
			break;
		}
	}
	if (likely(entry)) {
		entry->peer = wg_peer_get_maybe_zero(entry->peer);
		if (likely(entry->peer))
			*peer = entry->peer;
		else
			entry = NULL;
	}
	rcu_read_unlock_bh();
	return entry;
}
