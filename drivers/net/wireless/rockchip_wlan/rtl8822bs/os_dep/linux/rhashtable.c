/*
 * Resizable, Scalable, Concurrent Hash Table
 *
 * Copyright (c) 2015 Herbert Xu <herbert@gondor.apana.org.au>
 * Copyright (c) 2014-2015 Thomas Graf <tgraf@suug.ch>
 * Copyright (c) 2008-2014 Patrick McHardy <kaber@trash.net>
 *
 * Code partially derived from nft_hash
 * Rewritten with rehash code from br_multicast plus single list
 * pointer as suggested by Josh Triplett
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/atomic.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/log2.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <linux/jhash.h>
#include <linux/random.h>
#include <linux/err.h>
#include <linux/export.h>

#define HASH_DEFAULT_SIZE	64UL
#define HASH_MIN_SIZE		4U
#define BUCKET_LOCKS_PER_CPU   128UL

static u32 head_hashfn(struct rhashtable *ht,
		       const struct bucket_table *tbl,
		       const struct rhash_head *he)
{
	return rht_head_hashfn(ht, tbl, he, ht->p);
}

#ifdef CONFIG_PROVE_LOCKING
#define ASSERT_RHT_MUTEX(HT) BUG_ON(!lockdep_rht_mutex_is_held(HT))

int lockdep_rht_mutex_is_held(struct rhashtable *ht)
{
	return (debug_locks) ? lockdep_is_held(&ht->mutex) : 1;
}

int lockdep_rht_bucket_is_held(const struct bucket_table *tbl, u32 hash)
{
	spinlock_t *lock = rht_bucket_lock(tbl, hash);

	return (debug_locks) ? lockdep_is_held(lock) : 1;
}
#else
#define ASSERT_RHT_MUTEX(HT)
#endif


static int alloc_bucket_locks(struct rhashtable *ht, struct bucket_table *tbl,
			      gfp_t gfp)
{
	unsigned int i, size;
#if defined(CONFIG_PROVE_LOCKING)
	unsigned int nr_pcpus = 2;
#else
	unsigned int nr_pcpus = num_possible_cpus();
#endif

	nr_pcpus = min_t(unsigned int, nr_pcpus, 32UL);
	size = roundup_pow_of_two(nr_pcpus * ht->p.locks_mul);

	/* Never allocate more than 0.5 locks per bucket */
	size = min_t(unsigned int, size, tbl->size >> 1);

	if (sizeof(spinlock_t) != 0) {
#ifdef CONFIG_NUMA
		if (size * sizeof(spinlock_t) > PAGE_SIZE &&
		    gfp == GFP_KERNEL)
			tbl->locks = vmalloc(size * sizeof(spinlock_t));
		else
#endif
		tbl->locks = kmalloc_array(size, sizeof(spinlock_t),
					   gfp);
		if (!tbl->locks)
			return -ENOMEM;
		for (i = 0; i < size; i++)
			spin_lock_init(&tbl->locks[i]);
	}
	tbl->locks_mask = size - 1;

	return 0;
}

static void bucket_table_free(const struct bucket_table *tbl)
{
	if (tbl)
		kvfree(tbl->locks);

	kvfree(tbl);
}

static void bucket_table_free_rcu(struct rcu_head *head)
{
	bucket_table_free(container_of(head, struct bucket_table, rcu));
}

static struct bucket_table *bucket_table_alloc(struct rhashtable *ht,
					       size_t nbuckets,
					       gfp_t gfp)
{
	struct bucket_table *tbl = NULL;
	size_t size;
	int i;

	size = sizeof(*tbl) + nbuckets * sizeof(tbl->buckets[0]);
	if (size <= (PAGE_SIZE << PAGE_ALLOC_COSTLY_ORDER) ||
	    gfp != GFP_KERNEL)
		tbl = kzalloc(size, gfp | __GFP_NOWARN | __GFP_NORETRY);
	if (tbl == NULL && gfp == GFP_KERNEL)
		tbl = vzalloc(size);
	if (tbl == NULL)
		return NULL;

	tbl->size = nbuckets;

	if (alloc_bucket_locks(ht, tbl, gfp) < 0) {
		bucket_table_free(tbl);
		return NULL;
	}

	INIT_LIST_HEAD(&tbl->walkers);

	get_random_bytes(&tbl->hash_rnd, sizeof(tbl->hash_rnd));

	for (i = 0; i < nbuckets; i++)
		INIT_RHT_NULLS_HEAD(tbl->buckets[i], ht, i);

	return tbl;
}

static struct bucket_table *rhashtable_last_table(struct rhashtable *ht,
						  struct bucket_table *tbl)
{
	struct bucket_table *new_tbl;

	do {
		new_tbl = tbl;
		tbl = rht_dereference_rcu(tbl->future_tbl, ht);
	} while (tbl);

	return new_tbl;
}

static int rhashtable_rehash_one(struct rhashtable *ht, unsigned int old_hash)
{
	struct bucket_table *old_tbl = rht_dereference(ht->tbl, ht);
	struct bucket_table *new_tbl = rhashtable_last_table(ht,
		rht_dereference_rcu(old_tbl->future_tbl, ht));
	struct rhash_head __rcu **pprev = &old_tbl->buckets[old_hash];
	int err = -ENOENT;
	struct rhash_head *head, *next, *entry;
	spinlock_t *new_bucket_lock;
	unsigned int new_hash;

	rht_for_each(entry, old_tbl, old_hash) {
		err = 0;
		next = rht_dereference_bucket(entry->next, old_tbl, old_hash);

		if (rht_is_a_nulls(next))
			break;

		pprev = &entry->next;
	}

	if (err)
		goto out;

	new_hash = head_hashfn(ht, new_tbl, entry);

	new_bucket_lock = rht_bucket_lock(new_tbl, new_hash);

	spin_lock_nested(new_bucket_lock, SINGLE_DEPTH_NESTING);
	head = rht_dereference_bucket(new_tbl->buckets[new_hash],
				      new_tbl, new_hash);

	RCU_INIT_POINTER(entry->next, head);

	rcu_assign_pointer(new_tbl->buckets[new_hash], entry);
	spin_unlock(new_bucket_lock);

	rcu_assign_pointer(*pprev, next);

out:
	return err;
}

static void rhashtable_rehash_chain(struct rhashtable *ht,
				    unsigned int old_hash)
{
	struct bucket_table *old_tbl = rht_dereference(ht->tbl, ht);
	spinlock_t *old_bucket_lock;

	old_bucket_lock = rht_bucket_lock(old_tbl, old_hash);

	spin_lock_bh(old_bucket_lock);
	while (!rhashtable_rehash_one(ht, old_hash))
		;
	old_tbl->rehash++;
	spin_unlock_bh(old_bucket_lock);
}

static int rhashtable_rehash_attach(struct rhashtable *ht,
				    struct bucket_table *old_tbl,
				    struct bucket_table *new_tbl)
{
	/* Protect future_tbl using the first bucket lock. */
	spin_lock_bh(old_tbl->locks);

	/* Did somebody beat us to it? */
	if (rcu_access_pointer(old_tbl->future_tbl)) {
		spin_unlock_bh(old_tbl->locks);
		return -EEXIST;
	}

	/* Make insertions go into the new, empty table right away. Deletions
	 * and lookups will be attempted in both tables until we synchronize.
	 */
	rcu_assign_pointer(old_tbl->future_tbl, new_tbl);

	/* Ensure the new table is visible to readers. */
	smp_wmb();

	spin_unlock_bh(old_tbl->locks);

	return 0;
}

static int rhashtable_rehash_table(struct rhashtable *ht)
{
	struct bucket_table *old_tbl = rht_dereference(ht->tbl, ht);
	struct bucket_table *new_tbl;
	struct rhashtable_walker *walker;
	unsigned int old_hash;

	new_tbl = rht_dereference(old_tbl->future_tbl, ht);
	if (!new_tbl)
		return 0;

	for (old_hash = 0; old_hash < old_tbl->size; old_hash++)
		rhashtable_rehash_chain(ht, old_hash);

	/* Publish the new table pointer. */
	rcu_assign_pointer(ht->tbl, new_tbl);

	spin_lock(&ht->lock);
	list_for_each_entry(walker, &old_tbl->walkers, list)
		walker->tbl = NULL;
	spin_unlock(&ht->lock);

	/* Wait for readers. All new readers will see the new
	 * table, and thus no references to the old table will
	 * remain.
	 */
	call_rcu(&old_tbl->rcu, bucket_table_free_rcu);

	return rht_dereference(new_tbl->future_tbl, ht) ? -EAGAIN : 0;
}

/**
 * rhashtable_expand - Expand hash table while allowing concurrent lookups
 * @ht:		the hash table to expand
 *
 * A secondary bucket array is allocated and the hash entries are migrated.
 *
 * This function may only be called in a context where it is safe to call
 * synchronize_rcu(), e.g. not within a rcu_read_lock() section.
 *
 * The caller must ensure that no concurrent resizing occurs by holding
 * ht->mutex.
 *
 * It is valid to have concurrent insertions and deletions protected by per
 * bucket locks or concurrent RCU protected lookups and traversals.
 */
static int rhashtable_expand(struct rhashtable *ht)
{
	struct bucket_table *new_tbl, *old_tbl = rht_dereference(ht->tbl, ht);
	int err;

	ASSERT_RHT_MUTEX(ht);

	old_tbl = rhashtable_last_table(ht, old_tbl);

	new_tbl = bucket_table_alloc(ht, old_tbl->size * 2, GFP_KERNEL);
	if (new_tbl == NULL)
		return -ENOMEM;

	err = rhashtable_rehash_attach(ht, old_tbl, new_tbl);
	if (err)
		bucket_table_free(new_tbl);

	return err;
}

/**
 * rhashtable_shrink - Shrink hash table while allowing concurrent lookups
 * @ht:		the hash table to shrink
 *
 * This function shrinks the hash table to fit, i.e., the smallest
 * size would not cause it to expand right away automatically.
 *
 * The caller must ensure that no concurrent resizing occurs by holding
 * ht->mutex.
 *
 * The caller must ensure that no concurrent table mutations take place.
 * It is however valid to have concurrent lookups if they are RCU protected.
 *
 * It is valid to have concurrent insertions and deletions protected by per
 * bucket locks or concurrent RCU protected lookups and traversals.
 */
static int rhashtable_shrink(struct rhashtable *ht)
{
	struct bucket_table *new_tbl, *old_tbl = rht_dereference(ht->tbl, ht);
	unsigned int size;
	int err;

	ASSERT_RHT_MUTEX(ht);

	size = roundup_pow_of_two(atomic_read(&ht->nelems) * 3 / 2);
	if (size < ht->p.min_size)
		size = ht->p.min_size;

	if (old_tbl->size <= size)
		return 0;

	if (rht_dereference(old_tbl->future_tbl, ht))
		return -EEXIST;

	new_tbl = bucket_table_alloc(ht, size, GFP_KERNEL);
	if (new_tbl == NULL)
		return -ENOMEM;

	err = rhashtable_rehash_attach(ht, old_tbl, new_tbl);
	if (err)
		bucket_table_free(new_tbl);

	return err;
}

static void rht_deferred_worker(struct work_struct *work)
{
	struct rhashtable *ht;
	struct bucket_table *tbl;
	int err = 0;

	ht = container_of(work, struct rhashtable, run_work);
	mutex_lock(&ht->mutex);

	tbl = rht_dereference(ht->tbl, ht);
	tbl = rhashtable_last_table(ht, tbl);

	if (rht_grow_above_75(ht, tbl))
		rhashtable_expand(ht);
	else if (ht->p.automatic_shrinking && rht_shrink_below_30(ht, tbl))
		rhashtable_shrink(ht);

	err = rhashtable_rehash_table(ht);

	mutex_unlock(&ht->mutex);

	if (err)
		schedule_work(&ht->run_work);
}

static bool rhashtable_check_elasticity(struct rhashtable *ht,
					struct bucket_table *tbl,
					unsigned int hash)
{
	unsigned int elasticity = ht->elasticity;
	struct rhash_head *head;

	rht_for_each(head, tbl, hash)
		if (!--elasticity)
			return true;

	return false;
}

int rhashtable_insert_rehash(struct rhashtable *ht,
			     struct bucket_table *tbl)
{
	struct bucket_table *old_tbl;
	struct bucket_table *new_tbl;
	unsigned int size;
	int err;

	old_tbl = rht_dereference_rcu(ht->tbl, ht);

	size = tbl->size;

	err = -EBUSY;

	if (rht_grow_above_75(ht, tbl))
		size *= 2;
	/* Do not schedule more than one rehash */
	else if (old_tbl != tbl)
		goto fail;

	err = -ENOMEM;

	new_tbl = bucket_table_alloc(ht, size, GFP_ATOMIC);
	if (new_tbl == NULL)
		goto fail;

	err = rhashtable_rehash_attach(ht, tbl, new_tbl);
	if (err) {
		bucket_table_free(new_tbl);
		if (err == -EEXIST)
			err = 0;
	} else
		schedule_work(&ht->run_work);

	return err;

fail:
	/* Do not fail the insert if someone else did a rehash. */
	if (likely(rcu_dereference_raw(tbl->future_tbl)))
		return 0;

	/* Schedule async rehash to retry allocation in process context. */
	if (err == -ENOMEM)
		schedule_work(&ht->run_work);

	return err;
}

struct bucket_table *rhashtable_insert_slow(struct rhashtable *ht,
					    const void *key,
					    struct rhash_head *obj,
					    struct bucket_table *tbl)
{
	struct rhash_head *head;
	unsigned int hash;
	int err;

	tbl = rhashtable_last_table(ht, tbl);
	hash = head_hashfn(ht, tbl, obj);
	spin_lock_nested(rht_bucket_lock(tbl, hash), SINGLE_DEPTH_NESTING);

	err = -EEXIST;
	if (key && rhashtable_lookup_fast(ht, key, ht->p))
		goto exit;

	err = -E2BIG;
	if (unlikely(rht_grow_above_max(ht, tbl)))
		goto exit;

	err = -EAGAIN;
	if (rhashtable_check_elasticity(ht, tbl, hash) ||
	    rht_grow_above_100(ht, tbl))
		goto exit;

	err = 0;

	head = rht_dereference_bucket(tbl->buckets[hash], tbl, hash);

	RCU_INIT_POINTER(obj->next, head);

	rcu_assign_pointer(tbl->buckets[hash], obj);

	atomic_inc(&ht->nelems);

exit:
	spin_unlock(rht_bucket_lock(tbl, hash));

	if (err == 0)
		return NULL;
	else if (err == -EAGAIN)
		return tbl;
	else
		return ERR_PTR(err);
}

/**
 * rhashtable_walk_init - Initialise an iterator
 * @ht:		Table to walk over
 * @iter:	Hash table Iterator
 *
 * This function prepares a hash table walk.
 *
 * Note that if you restart a walk after rhashtable_walk_stop you
 * may see the same object twice.  Also, you may miss objects if
 * there are removals in between rhashtable_walk_stop and the next
 * call to rhashtable_walk_start.
 *
 * For a completely stable walk you should construct your own data
 * structure outside the hash table.
 *
 * This function may sleep so you must not call it from interrupt
 * context or with spin locks held.
 *
 * You must call rhashtable_walk_exit if this function returns
 * successfully.
 */
int rhashtable_walk_init(struct rhashtable *ht, struct rhashtable_iter *iter)
{
	iter->ht = ht;
	iter->p = NULL;
	iter->slot = 0;
	iter->skip = 0;

	iter->walker = kmalloc(sizeof(*iter->walker), GFP_KERNEL);
	if (!iter->walker)
		return -ENOMEM;

	spin_lock(&ht->lock);
	iter->walker->tbl =
		rcu_dereference_protected(ht->tbl, lockdep_is_held(&ht->lock));
	list_add(&iter->walker->list, &iter->walker->tbl->walkers);
	spin_unlock(&ht->lock);

	return 0;
}

/**
 * rhashtable_walk_exit - Free an iterator
 * @iter:	Hash table Iterator
 *
 * This function frees resources allocated by rhashtable_walk_init.
 */
void rhashtable_walk_exit(struct rhashtable_iter *iter)
{
	spin_lock(&iter->ht->lock);
	if (iter->walker->tbl)
		list_del(&iter->walker->list);
	spin_unlock(&iter->ht->lock);
	kfree(iter->walker);
}

/**
 * rhashtable_walk_start - Start a hash table walk
 * @iter:	Hash table iterator
 *
 * Start a hash table walk.  Note that we take the RCU lock in all
 * cases including when we return an error.  So you must always call
 * rhashtable_walk_stop to clean up.
 *
 * Returns zero if successful.
 *
 * Returns -EAGAIN if resize event occured.  Note that the iterator
 * will rewind back to the beginning and you may use it immediately
 * by calling rhashtable_walk_next.
 */
int rhashtable_walk_start(struct rhashtable_iter *iter)
	__acquires(RCU)
{
	struct rhashtable *ht = iter->ht;

	rcu_read_lock();

	spin_lock(&ht->lock);
	if (iter->walker->tbl)
		list_del(&iter->walker->list);
	spin_unlock(&ht->lock);

	if (!iter->walker->tbl) {
		iter->walker->tbl = rht_dereference_rcu(ht->tbl, ht);
		return -EAGAIN;
	}

	return 0;
}

/**
 * rhashtable_walk_next - Return the next object and advance the iterator
 * @iter:	Hash table iterator
 *
 * Note that you must call rhashtable_walk_stop when you are finished
 * with the walk.
 *
 * Returns the next object or NULL when the end of the table is reached.
 *
 * Returns -EAGAIN if resize event occured.  Note that the iterator
 * will rewind back to the beginning and you may continue to use it.
 */
void *rhashtable_walk_next(struct rhashtable_iter *iter)
{
	struct bucket_table *tbl = iter->walker->tbl;
	struct rhashtable *ht = iter->ht;
	struct rhash_head *p = iter->p;

	if (p) {
		p = rht_dereference_bucket_rcu(p->next, tbl, iter->slot);
		goto next;
	}

	for (; iter->slot < tbl->size; iter->slot++) {
		int skip = iter->skip;

		rht_for_each_rcu(p, tbl, iter->slot) {
			if (!skip)
				break;
			skip--;
		}

next:
		if (!rht_is_a_nulls(p)) {
			iter->skip++;
			iter->p = p;
			return rht_obj(ht, p);
		}

		iter->skip = 0;
	}

	iter->p = NULL;

	/* Ensure we see any new tables. */
	smp_rmb();

	iter->walker->tbl = rht_dereference_rcu(tbl->future_tbl, ht);
	if (iter->walker->tbl) {
		iter->slot = 0;
		iter->skip = 0;
		return ERR_PTR(-EAGAIN);
	}

	return NULL;
}

/**
 * rhashtable_walk_stop - Finish a hash table walk
 * @iter:	Hash table iterator
 *
 * Finish a hash table walk.
 */
void rhashtable_walk_stop(struct rhashtable_iter *iter)
	__releases(RCU)
{
	struct rhashtable *ht;
	struct bucket_table *tbl = iter->walker->tbl;

	if (!tbl)
		goto out;

	ht = iter->ht;

	spin_lock(&ht->lock);
	if (tbl->rehash < tbl->size)
		list_add(&iter->walker->list, &tbl->walkers);
	else
		iter->walker->tbl = NULL;
	spin_unlock(&ht->lock);

	iter->p = NULL;

out:
	rcu_read_unlock();
}

static size_t rounded_hashtable_size(const struct rhashtable_params *params)
{
	return max(roundup_pow_of_two(params->nelem_hint * 4 / 3),
		   (unsigned long)params->min_size);
}

static u32 rhashtable_jhash2(const void *key, u32 length, u32 seed)
{
	return jhash2(key, length, seed);
}

/**
 * rhashtable_init - initialize a new hash table
 * @ht:		hash table to be initialized
 * @params:	configuration parameters
 *
 * Initializes a new hash table based on the provided configuration
 * parameters. A table can be configured either with a variable or
 * fixed length key:
 *
 * Configuration Example 1: Fixed length keys
 * struct test_obj {
 *	int			key;
 *	void *			my_member;
 *	struct rhash_head	node;
 * };
 *
 * struct rhashtable_params params = {
 *	.head_offset = offsetof(struct test_obj, node),
 *	.key_offset = offsetof(struct test_obj, key),
 *	.key_len = sizeof(int),
 *	.hashfn = jhash,
 *	.nulls_base = (1U << RHT_BASE_SHIFT),
 * };
 *
 * Configuration Example 2: Variable length keys
 * struct test_obj {
 *	[...]
 *	struct rhash_head	node;
 * };
 *
 * u32 my_hash_fn(const void *data, u32 len, u32 seed)
 * {
 *	struct test_obj *obj = data;
 *
 *	return [... hash ...];
 * }
 *
 * struct rhashtable_params params = {
 *	.head_offset = offsetof(struct test_obj, node),
 *	.hashfn = jhash,
 *	.obj_hashfn = my_hash_fn,
 * };
 */
int rhashtable_init(struct rhashtable *ht,
		    const struct rhashtable_params *params)
{
	struct bucket_table *tbl;
	size_t size;

	size = HASH_DEFAULT_SIZE;

	if ((!params->key_len && !params->obj_hashfn) ||
	    (params->obj_hashfn && !params->obj_cmpfn))
		return -EINVAL;

	if (params->nulls_base && params->nulls_base < (1U << RHT_BASE_SHIFT))
		return -EINVAL;

	memset(ht, 0, sizeof(*ht));
	mutex_init(&ht->mutex);
	spin_lock_init(&ht->lock);
	memcpy(&ht->p, params, sizeof(*params));

	if (params->min_size)
		ht->p.min_size = roundup_pow_of_two(params->min_size);

	if (params->max_size)
		ht->p.max_size = rounddown_pow_of_two(params->max_size);

	if (params->insecure_max_entries)
		ht->p.insecure_max_entries =
			rounddown_pow_of_two(params->insecure_max_entries);
	else
		ht->p.insecure_max_entries = ht->p.max_size * 2;

	ht->p.min_size = max(ht->p.min_size, HASH_MIN_SIZE);

	if (params->nelem_hint)
		size = rounded_hashtable_size(&ht->p);

	/* The maximum (not average) chain length grows with the
	 * size of the hash table, at a rate of (log N)/(log log N).
	 * The value of 16 is selected so that even if the hash
	 * table grew to 2^32 you would not expect the maximum
	 * chain length to exceed it unless we are under attack
	 * (or extremely unlucky).
	 *
	 * As this limit is only to detect attacks, we don't need
	 * to set it to a lower value as you'd need the chain
	 * length to vastly exceed 16 to have any real effect
	 * on the system.
	 */
	if (!params->insecure_elasticity)
		ht->elasticity = 16;

	if (params->locks_mul)
		ht->p.locks_mul = roundup_pow_of_two(params->locks_mul);
	else
		ht->p.locks_mul = BUCKET_LOCKS_PER_CPU;

	ht->key_len = ht->p.key_len;
	if (!params->hashfn) {
		ht->p.hashfn = jhash;

		if (!(ht->key_len & (sizeof(u32) - 1))) {
			ht->key_len /= sizeof(u32);
			ht->p.hashfn = rhashtable_jhash2;
		}
	}

	tbl = bucket_table_alloc(ht, size, GFP_KERNEL);
	if (tbl == NULL)
		return -ENOMEM;

	atomic_set(&ht->nelems, 0);

	RCU_INIT_POINTER(ht->tbl, tbl);

	INIT_WORK(&ht->run_work, rht_deferred_worker);

	return 0;
}

/**
 * rhashtable_free_and_destroy - free elements and destroy hash table
 * @ht:		the hash table to destroy
 * @free_fn:	callback to release resources of element
 * @arg:	pointer passed to free_fn
 *
 * Stops an eventual async resize. If defined, invokes free_fn for each
 * element to releasal resources. Please note that RCU protected
 * readers may still be accessing the elements. Releasing of resources
 * must occur in a compatible manner. Then frees the bucket array.
 *
 * This function will eventually sleep to wait for an async resize
 * to complete. The caller is responsible that no further write operations
 * occurs in parallel.
 */
void rhashtable_free_and_destroy(struct rhashtable *ht,
				 void (*free_fn)(void *ptr, void *arg),
				 void *arg)
{
	const struct bucket_table *tbl;
	unsigned int i;

	cancel_work_sync(&ht->run_work);

	mutex_lock(&ht->mutex);
	tbl = rht_dereference(ht->tbl, ht);
	if (free_fn) {
		for (i = 0; i < tbl->size; i++) {
			struct rhash_head *pos, *next;

			for (pos = rht_dereference(tbl->buckets[i], ht),
			     next = !rht_is_a_nulls(pos) ?
					rht_dereference(pos->next, ht) : NULL;
			     !rht_is_a_nulls(pos);
			     pos = next,
			     next = !rht_is_a_nulls(pos) ?
					rht_dereference(pos->next, ht) : NULL)
				free_fn(rht_obj(ht, pos), arg);
		}
	}

	bucket_table_free(tbl);
	mutex_unlock(&ht->mutex);
}

void rhashtable_destroy(struct rhashtable *ht)
{
	return rhashtable_free_and_destroy(ht, NULL, NULL);
}

