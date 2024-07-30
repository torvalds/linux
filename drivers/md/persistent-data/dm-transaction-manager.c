// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2011 Red Hat, Inc.
 *
 * This file is released under the GPL.
 */
#include "dm-transaction-manager.h"
#include "dm-space-map.h"
#include "dm-space-map-disk.h"
#include "dm-space-map-metadata.h"
#include "dm-persistent-data-internal.h"

#include <linux/export.h>
#include <linux/mutex.h>
#include <linux/hash.h>
#include <linux/slab.h>
#include <linux/device-mapper.h>

#define DM_MSG_PREFIX "transaction manager"

/*----------------------------------------------------------------*/

#define PREFETCH_SIZE 128
#define PREFETCH_BITS 7
#define PREFETCH_SENTINEL ((dm_block_t) -1ULL)

struct prefetch_set {
	struct mutex lock;
	dm_block_t blocks[PREFETCH_SIZE];
};

static unsigned int prefetch_hash(dm_block_t b)
{
	return hash_64(b, PREFETCH_BITS);
}

static void prefetch_wipe(struct prefetch_set *p)
{
	unsigned int i;

	for (i = 0; i < PREFETCH_SIZE; i++)
		p->blocks[i] = PREFETCH_SENTINEL;
}

static void prefetch_init(struct prefetch_set *p)
{
	mutex_init(&p->lock);
	prefetch_wipe(p);
}

static void prefetch_add(struct prefetch_set *p, dm_block_t b)
{
	unsigned int h = prefetch_hash(b);

	mutex_lock(&p->lock);
	if (p->blocks[h] == PREFETCH_SENTINEL)
		p->blocks[h] = b;

	mutex_unlock(&p->lock);
}

static void prefetch_issue(struct prefetch_set *p, struct dm_block_manager *bm)
{
	unsigned int i;

	mutex_lock(&p->lock);

	for (i = 0; i < PREFETCH_SIZE; i++)
		if (p->blocks[i] != PREFETCH_SENTINEL) {
			dm_bm_prefetch(bm, p->blocks[i]);
			p->blocks[i] = PREFETCH_SENTINEL;
		}

	mutex_unlock(&p->lock);
}

/*----------------------------------------------------------------*/

struct shadow_info {
	struct hlist_node hlist;
	dm_block_t where;
};

/*
 * It would be nice if we scaled with the size of transaction.
 */
#define DM_HASH_SIZE 256
#define DM_HASH_MASK (DM_HASH_SIZE - 1)

struct dm_transaction_manager {
	int is_clone;
	struct dm_transaction_manager *real;

	struct dm_block_manager *bm;
	struct dm_space_map *sm;

	spinlock_t lock;
	struct hlist_head buckets[DM_HASH_SIZE];

	struct prefetch_set prefetches;
};

/*----------------------------------------------------------------*/

static int is_shadow(struct dm_transaction_manager *tm, dm_block_t b)
{
	int r = 0;
	unsigned int bucket = dm_hash_block(b, DM_HASH_MASK);
	struct shadow_info *si;

	spin_lock(&tm->lock);
	hlist_for_each_entry(si, tm->buckets + bucket, hlist)
		if (si->where == b) {
			r = 1;
			break;
		}
	spin_unlock(&tm->lock);

	return r;
}

/*
 * This can silently fail if there's no memory.  We're ok with this since
 * creating redundant shadows causes no harm.
 */
static void insert_shadow(struct dm_transaction_manager *tm, dm_block_t b)
{
	unsigned int bucket;
	struct shadow_info *si;

	si = kmalloc(sizeof(*si), GFP_NOIO);
	if (si) {
		si->where = b;
		bucket = dm_hash_block(b, DM_HASH_MASK);
		spin_lock(&tm->lock);
		hlist_add_head(&si->hlist, tm->buckets + bucket);
		spin_unlock(&tm->lock);
	}
}

static void wipe_shadow_table(struct dm_transaction_manager *tm)
{
	struct shadow_info *si;
	struct hlist_node *tmp;
	struct hlist_head *bucket;
	int i;

	spin_lock(&tm->lock);
	for (i = 0; i < DM_HASH_SIZE; i++) {
		bucket = tm->buckets + i;
		hlist_for_each_entry_safe(si, tmp, bucket, hlist)
			kfree(si);

		INIT_HLIST_HEAD(bucket);
	}

	spin_unlock(&tm->lock);
}

/*----------------------------------------------------------------*/

static struct dm_transaction_manager *dm_tm_create(struct dm_block_manager *bm,
						   struct dm_space_map *sm)
{
	int i;
	struct dm_transaction_manager *tm;

	tm = kmalloc(sizeof(*tm), GFP_KERNEL);
	if (!tm)
		return ERR_PTR(-ENOMEM);

	tm->is_clone = 0;
	tm->real = NULL;
	tm->bm = bm;
	tm->sm = sm;

	spin_lock_init(&tm->lock);
	for (i = 0; i < DM_HASH_SIZE; i++)
		INIT_HLIST_HEAD(tm->buckets + i);

	prefetch_init(&tm->prefetches);

	return tm;
}

struct dm_transaction_manager *dm_tm_create_non_blocking_clone(struct dm_transaction_manager *real)
{
	struct dm_transaction_manager *tm;

	tm = kmalloc(sizeof(*tm), GFP_KERNEL);
	if (tm) {
		tm->is_clone = 1;
		tm->real = real;
	}

	return tm;
}
EXPORT_SYMBOL_GPL(dm_tm_create_non_blocking_clone);

void dm_tm_destroy(struct dm_transaction_manager *tm)
{
	if (!tm)
		return;

	if (!tm->is_clone)
		wipe_shadow_table(tm);

	kfree(tm);
}
EXPORT_SYMBOL_GPL(dm_tm_destroy);

int dm_tm_pre_commit(struct dm_transaction_manager *tm)
{
	int r;

	if (tm->is_clone)
		return -EWOULDBLOCK;

	r = dm_sm_commit(tm->sm);
	if (r < 0)
		return r;

	return dm_bm_flush(tm->bm);
}
EXPORT_SYMBOL_GPL(dm_tm_pre_commit);

int dm_tm_commit(struct dm_transaction_manager *tm, struct dm_block *root)
{
	if (tm->is_clone)
		return -EWOULDBLOCK;

	wipe_shadow_table(tm);
	dm_bm_unlock(root);

	return dm_bm_flush(tm->bm);
}
EXPORT_SYMBOL_GPL(dm_tm_commit);

int dm_tm_new_block(struct dm_transaction_manager *tm,
		    const struct dm_block_validator *v,
		    struct dm_block **result)
{
	int r;
	dm_block_t new_block;

	if (tm->is_clone)
		return -EWOULDBLOCK;

	r = dm_sm_new_block(tm->sm, &new_block);
	if (r < 0)
		return r;

	r = dm_bm_write_lock_zero(tm->bm, new_block, v, result);
	if (r < 0) {
		dm_sm_dec_block(tm->sm, new_block);
		return r;
	}

	/*
	 * New blocks count as shadows in that they don't need to be
	 * shadowed again.
	 */
	insert_shadow(tm, new_block);

	return 0;
}

static int __shadow_block(struct dm_transaction_manager *tm, dm_block_t orig,
			  const struct dm_block_validator *v,
			  struct dm_block **result)
{
	int r;
	dm_block_t new;
	struct dm_block *orig_block;

	r = dm_sm_new_block(tm->sm, &new);
	if (r < 0)
		return r;

	r = dm_sm_dec_block(tm->sm, orig);
	if (r < 0)
		return r;

	r = dm_bm_read_lock(tm->bm, orig, v, &orig_block);
	if (r < 0)
		return r;

	/*
	 * It would be tempting to use dm_bm_unlock_move here, but some
	 * code, such as the space maps, keeps using the old data structures
	 * secure in the knowledge they won't be changed until the next
	 * transaction.  Using unlock_move would force a synchronous read
	 * since the old block would no longer be in the cache.
	 */
	r = dm_bm_write_lock_zero(tm->bm, new, v, result);
	if (r) {
		dm_bm_unlock(orig_block);
		return r;
	}

	memcpy(dm_block_data(*result), dm_block_data(orig_block),
	       dm_bm_block_size(tm->bm));

	dm_bm_unlock(orig_block);
	return r;
}

int dm_tm_shadow_block(struct dm_transaction_manager *tm, dm_block_t orig,
		       const struct dm_block_validator *v, struct dm_block **result,
		       int *inc_children)
{
	int r;

	if (tm->is_clone)
		return -EWOULDBLOCK;

	r = dm_sm_count_is_more_than_one(tm->sm, orig, inc_children);
	if (r < 0)
		return r;

	if (is_shadow(tm, orig) && !*inc_children)
		return dm_bm_write_lock(tm->bm, orig, v, result);

	r = __shadow_block(tm, orig, v, result);
	if (r < 0)
		return r;
	insert_shadow(tm, dm_block_location(*result));

	return r;
}
EXPORT_SYMBOL_GPL(dm_tm_shadow_block);

int dm_tm_read_lock(struct dm_transaction_manager *tm, dm_block_t b,
		    const struct dm_block_validator *v,
		    struct dm_block **blk)
{
	if (tm->is_clone) {
		int r = dm_bm_read_try_lock(tm->real->bm, b, v, blk);

		if (r == -EWOULDBLOCK)
			prefetch_add(&tm->real->prefetches, b);

		return r;
	}

	return dm_bm_read_lock(tm->bm, b, v, blk);
}
EXPORT_SYMBOL_GPL(dm_tm_read_lock);

void dm_tm_unlock(struct dm_transaction_manager *tm, struct dm_block *b)
{
	dm_bm_unlock(b);
}
EXPORT_SYMBOL_GPL(dm_tm_unlock);

void dm_tm_inc(struct dm_transaction_manager *tm, dm_block_t b)
{
	/*
	 * The non-blocking clone doesn't support this.
	 */
	BUG_ON(tm->is_clone);

	dm_sm_inc_block(tm->sm, b);
}
EXPORT_SYMBOL_GPL(dm_tm_inc);

void dm_tm_inc_range(struct dm_transaction_manager *tm, dm_block_t b, dm_block_t e)
{
	/*
	 * The non-blocking clone doesn't support this.
	 */
	BUG_ON(tm->is_clone);

	dm_sm_inc_blocks(tm->sm, b, e);
}
EXPORT_SYMBOL_GPL(dm_tm_inc_range);

void dm_tm_dec(struct dm_transaction_manager *tm, dm_block_t b)
{
	/*
	 * The non-blocking clone doesn't support this.
	 */
	BUG_ON(tm->is_clone);

	dm_sm_dec_block(tm->sm, b);
}
EXPORT_SYMBOL_GPL(dm_tm_dec);

void dm_tm_dec_range(struct dm_transaction_manager *tm, dm_block_t b, dm_block_t e)
{
	/*
	 * The non-blocking clone doesn't support this.
	 */
	BUG_ON(tm->is_clone);

	dm_sm_dec_blocks(tm->sm, b, e);
}
EXPORT_SYMBOL_GPL(dm_tm_dec_range);

void dm_tm_with_runs(struct dm_transaction_manager *tm,
		     const __le64 *value_le, unsigned int count, dm_tm_run_fn fn)
{
	uint64_t b, begin, end;
	bool in_run = false;
	unsigned int i;

	for (i = 0; i < count; i++, value_le++) {
		b = le64_to_cpu(*value_le);

		if (in_run) {
			if (b == end)
				end++;
			else {
				fn(tm, begin, end);
				begin = b;
				end = b + 1;
			}
		} else {
			in_run = true;
			begin = b;
			end = b + 1;
		}
	}

	if (in_run)
		fn(tm, begin, end);
}
EXPORT_SYMBOL_GPL(dm_tm_with_runs);

int dm_tm_ref(struct dm_transaction_manager *tm, dm_block_t b,
	      uint32_t *result)
{
	if (tm->is_clone)
		return -EWOULDBLOCK;

	return dm_sm_get_count(tm->sm, b, result);
}

int dm_tm_block_is_shared(struct dm_transaction_manager *tm, dm_block_t b,
			  int *result)
{
	if (tm->is_clone)
		return -EWOULDBLOCK;

	return dm_sm_count_is_more_than_one(tm->sm, b, result);
}

struct dm_block_manager *dm_tm_get_bm(struct dm_transaction_manager *tm)
{
	return tm->bm;
}

void dm_tm_issue_prefetches(struct dm_transaction_manager *tm)
{
	prefetch_issue(&tm->prefetches, tm->bm);
}
EXPORT_SYMBOL_GPL(dm_tm_issue_prefetches);

/*----------------------------------------------------------------*/

static int dm_tm_create_internal(struct dm_block_manager *bm,
				 dm_block_t sb_location,
				 struct dm_transaction_manager **tm,
				 struct dm_space_map **sm,
				 int create,
				 void *sm_root, size_t sm_len)
{
	int r;

	*sm = dm_sm_metadata_init();
	if (IS_ERR(*sm))
		return PTR_ERR(*sm);

	*tm = dm_tm_create(bm, *sm);
	if (IS_ERR(*tm)) {
		dm_sm_destroy(*sm);
		return PTR_ERR(*tm);
	}

	if (create) {
		r = dm_sm_metadata_create(*sm, *tm, dm_bm_nr_blocks(bm),
					  sb_location);
		if (r) {
			DMERR("couldn't create metadata space map");
			goto bad;
		}

	} else {
		r = dm_sm_metadata_open(*sm, *tm, sm_root, sm_len);
		if (r) {
			DMERR("couldn't open metadata space map");
			goto bad;
		}
	}

	return 0;

bad:
	dm_tm_destroy(*tm);
	dm_sm_destroy(*sm);
	return r;
}

int dm_tm_create_with_sm(struct dm_block_manager *bm, dm_block_t sb_location,
			 struct dm_transaction_manager **tm,
			 struct dm_space_map **sm)
{
	return dm_tm_create_internal(bm, sb_location, tm, sm, 1, NULL, 0);
}
EXPORT_SYMBOL_GPL(dm_tm_create_with_sm);

int dm_tm_open_with_sm(struct dm_block_manager *bm, dm_block_t sb_location,
		       void *sm_root, size_t root_len,
		       struct dm_transaction_manager **tm,
		       struct dm_space_map **sm)
{
	return dm_tm_create_internal(bm, sb_location, tm, sm, 0, sm_root, root_len);
}
EXPORT_SYMBOL_GPL(dm_tm_open_with_sm);

/*----------------------------------------------------------------*/
