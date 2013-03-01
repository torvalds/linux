/*
 * Copyright (C) 2012 Red Hat, Inc.
 *
 * This file is released under the GPL.
 */

#include "dm.h"
#include "dm-bio-prison.h"

#include <linux/spinlock.h>
#include <linux/mempool.h>
#include <linux/module.h>
#include <linux/slab.h>

/*----------------------------------------------------------------*/

struct dm_bio_prison {
	spinlock_t lock;
	mempool_t *cell_pool;

	unsigned nr_buckets;
	unsigned hash_mask;
	struct hlist_head *cells;
};

/*----------------------------------------------------------------*/

static uint32_t calc_nr_buckets(unsigned nr_cells)
{
	uint32_t n = 128;

	nr_cells /= 4;
	nr_cells = min(nr_cells, 8192u);

	while (n < nr_cells)
		n <<= 1;

	return n;
}

static struct kmem_cache *_cell_cache;

/*
 * @nr_cells should be the number of cells you want in use _concurrently_.
 * Don't confuse it with the number of distinct keys.
 */
struct dm_bio_prison *dm_bio_prison_create(unsigned nr_cells)
{
	unsigned i;
	uint32_t nr_buckets = calc_nr_buckets(nr_cells);
	size_t len = sizeof(struct dm_bio_prison) +
		(sizeof(struct hlist_head) * nr_buckets);
	struct dm_bio_prison *prison = kmalloc(len, GFP_KERNEL);

	if (!prison)
		return NULL;

	spin_lock_init(&prison->lock);
	prison->cell_pool = mempool_create_slab_pool(nr_cells, _cell_cache);
	if (!prison->cell_pool) {
		kfree(prison);
		return NULL;
	}

	prison->nr_buckets = nr_buckets;
	prison->hash_mask = nr_buckets - 1;
	prison->cells = (struct hlist_head *) (prison + 1);
	for (i = 0; i < nr_buckets; i++)
		INIT_HLIST_HEAD(prison->cells + i);

	return prison;
}
EXPORT_SYMBOL_GPL(dm_bio_prison_create);

void dm_bio_prison_destroy(struct dm_bio_prison *prison)
{
	mempool_destroy(prison->cell_pool);
	kfree(prison);
}
EXPORT_SYMBOL_GPL(dm_bio_prison_destroy);

struct dm_bio_prison_cell *dm_bio_prison_alloc_cell(struct dm_bio_prison *prison, gfp_t gfp)
{
	return mempool_alloc(prison->cell_pool, gfp);
}
EXPORT_SYMBOL_GPL(dm_bio_prison_alloc_cell);

void dm_bio_prison_free_cell(struct dm_bio_prison *prison,
			     struct dm_bio_prison_cell *cell)
{
	mempool_free(cell, prison->cell_pool);
}
EXPORT_SYMBOL_GPL(dm_bio_prison_free_cell);

static uint32_t hash_key(struct dm_bio_prison *prison, struct dm_cell_key *key)
{
	const unsigned long BIG_PRIME = 4294967291UL;
	uint64_t hash = key->block * BIG_PRIME;

	return (uint32_t) (hash & prison->hash_mask);
}

static int keys_equal(struct dm_cell_key *lhs, struct dm_cell_key *rhs)
{
	       return (lhs->virtual == rhs->virtual) &&
		       (lhs->dev == rhs->dev) &&
		       (lhs->block == rhs->block);
}

static struct dm_bio_prison_cell *__search_bucket(struct hlist_head *bucket,
						  struct dm_cell_key *key)
{
	struct dm_bio_prison_cell *cell;

	hlist_for_each_entry(cell, bucket, list)
		if (keys_equal(&cell->key, key))
			return cell;

	return NULL;
}

static void __setup_new_cell(struct dm_bio_prison *prison,
			     struct dm_cell_key *key,
			     struct bio *holder,
			     uint32_t hash,
			     struct dm_bio_prison_cell *cell)
{
	memcpy(&cell->key, key, sizeof(cell->key));
	cell->holder = holder;
	bio_list_init(&cell->bios);
	hlist_add_head(&cell->list, prison->cells + hash);
}

static int __bio_detain(struct dm_bio_prison *prison,
			struct dm_cell_key *key,
			struct bio *inmate,
			struct dm_bio_prison_cell *cell_prealloc,
			struct dm_bio_prison_cell **cell_result)
{
	uint32_t hash = hash_key(prison, key);
	struct dm_bio_prison_cell *cell;

	cell = __search_bucket(prison->cells + hash, key);
	if (cell) {
		if (inmate)
			bio_list_add(&cell->bios, inmate);
		*cell_result = cell;
		return 1;
	}

	__setup_new_cell(prison, key, inmate, hash, cell_prealloc);
	*cell_result = cell_prealloc;
	return 0;
}

static int bio_detain(struct dm_bio_prison *prison,
		      struct dm_cell_key *key,
		      struct bio *inmate,
		      struct dm_bio_prison_cell *cell_prealloc,
		      struct dm_bio_prison_cell **cell_result)
{
	int r;
	unsigned long flags;

	spin_lock_irqsave(&prison->lock, flags);
	r = __bio_detain(prison, key, inmate, cell_prealloc, cell_result);
	spin_unlock_irqrestore(&prison->lock, flags);

	return r;
}

int dm_bio_detain(struct dm_bio_prison *prison,
		  struct dm_cell_key *key,
		  struct bio *inmate,
		  struct dm_bio_prison_cell *cell_prealloc,
		  struct dm_bio_prison_cell **cell_result)
{
	return bio_detain(prison, key, inmate, cell_prealloc, cell_result);
}
EXPORT_SYMBOL_GPL(dm_bio_detain);

/*
 * @inmates must have been initialised prior to this call
 */
static void __cell_release(struct dm_bio_prison_cell *cell,
			   struct bio_list *inmates)
{
	hlist_del(&cell->list);

	if (inmates) {
		if (cell->holder)
			bio_list_add(inmates, cell->holder);
		bio_list_merge(inmates, &cell->bios);
	}
}

void dm_cell_release(struct dm_bio_prison *prison,
		     struct dm_bio_prison_cell *cell,
		     struct bio_list *bios)
{
	unsigned long flags;

	spin_lock_irqsave(&prison->lock, flags);
	__cell_release(cell, bios);
	spin_unlock_irqrestore(&prison->lock, flags);
}
EXPORT_SYMBOL_GPL(dm_cell_release);

/*
 * Sometimes we don't want the holder, just the additional bios.
 */
static void __cell_release_no_holder(struct dm_bio_prison_cell *cell,
				     struct bio_list *inmates)
{
	hlist_del(&cell->list);
	bio_list_merge(inmates, &cell->bios);
}

void dm_cell_release_no_holder(struct dm_bio_prison *prison,
			       struct dm_bio_prison_cell *cell,
			       struct bio_list *inmates)
{
	unsigned long flags;

	spin_lock_irqsave(&prison->lock, flags);
	__cell_release_no_holder(cell, inmates);
	spin_unlock_irqrestore(&prison->lock, flags);
}
EXPORT_SYMBOL_GPL(dm_cell_release_no_holder);

void dm_cell_error(struct dm_bio_prison *prison,
		   struct dm_bio_prison_cell *cell)
{
	struct bio_list bios;
	struct bio *bio;
	unsigned long flags;

	bio_list_init(&bios);

	spin_lock_irqsave(&prison->lock, flags);
	__cell_release(cell, &bios);
	spin_unlock_irqrestore(&prison->lock, flags);

	while ((bio = bio_list_pop(&bios)))
		bio_io_error(bio);
}
EXPORT_SYMBOL_GPL(dm_cell_error);

/*----------------------------------------------------------------*/

#define DEFERRED_SET_SIZE 64

struct dm_deferred_entry {
	struct dm_deferred_set *ds;
	unsigned count;
	struct list_head work_items;
};

struct dm_deferred_set {
	spinlock_t lock;
	unsigned current_entry;
	unsigned sweeper;
	struct dm_deferred_entry entries[DEFERRED_SET_SIZE];
};

struct dm_deferred_set *dm_deferred_set_create(void)
{
	int i;
	struct dm_deferred_set *ds;

	ds = kmalloc(sizeof(*ds), GFP_KERNEL);
	if (!ds)
		return NULL;

	spin_lock_init(&ds->lock);
	ds->current_entry = 0;
	ds->sweeper = 0;
	for (i = 0; i < DEFERRED_SET_SIZE; i++) {
		ds->entries[i].ds = ds;
		ds->entries[i].count = 0;
		INIT_LIST_HEAD(&ds->entries[i].work_items);
	}

	return ds;
}
EXPORT_SYMBOL_GPL(dm_deferred_set_create);

void dm_deferred_set_destroy(struct dm_deferred_set *ds)
{
	kfree(ds);
}
EXPORT_SYMBOL_GPL(dm_deferred_set_destroy);

struct dm_deferred_entry *dm_deferred_entry_inc(struct dm_deferred_set *ds)
{
	unsigned long flags;
	struct dm_deferred_entry *entry;

	spin_lock_irqsave(&ds->lock, flags);
	entry = ds->entries + ds->current_entry;
	entry->count++;
	spin_unlock_irqrestore(&ds->lock, flags);

	return entry;
}
EXPORT_SYMBOL_GPL(dm_deferred_entry_inc);

static unsigned ds_next(unsigned index)
{
	return (index + 1) % DEFERRED_SET_SIZE;
}

static void __sweep(struct dm_deferred_set *ds, struct list_head *head)
{
	while ((ds->sweeper != ds->current_entry) &&
	       !ds->entries[ds->sweeper].count) {
		list_splice_init(&ds->entries[ds->sweeper].work_items, head);
		ds->sweeper = ds_next(ds->sweeper);
	}

	if ((ds->sweeper == ds->current_entry) && !ds->entries[ds->sweeper].count)
		list_splice_init(&ds->entries[ds->sweeper].work_items, head);
}

void dm_deferred_entry_dec(struct dm_deferred_entry *entry, struct list_head *head)
{
	unsigned long flags;

	spin_lock_irqsave(&entry->ds->lock, flags);
	BUG_ON(!entry->count);
	--entry->count;
	__sweep(entry->ds, head);
	spin_unlock_irqrestore(&entry->ds->lock, flags);
}
EXPORT_SYMBOL_GPL(dm_deferred_entry_dec);

/*
 * Returns 1 if deferred or 0 if no pending items to delay job.
 */
int dm_deferred_set_add_work(struct dm_deferred_set *ds, struct list_head *work)
{
	int r = 1;
	unsigned long flags;
	unsigned next_entry;

	spin_lock_irqsave(&ds->lock, flags);
	if ((ds->sweeper == ds->current_entry) &&
	    !ds->entries[ds->current_entry].count)
		r = 0;
	else {
		list_add(work, &ds->entries[ds->current_entry].work_items);
		next_entry = ds_next(ds->current_entry);
		if (!ds->entries[next_entry].count)
			ds->current_entry = next_entry;
	}
	spin_unlock_irqrestore(&ds->lock, flags);

	return r;
}
EXPORT_SYMBOL_GPL(dm_deferred_set_add_work);

/*----------------------------------------------------------------*/

static int __init dm_bio_prison_init(void)
{
	_cell_cache = KMEM_CACHE(dm_bio_prison_cell, 0);
	if (!_cell_cache)
		return -ENOMEM;

	return 0;
}

static void __exit dm_bio_prison_exit(void)
{
	kmem_cache_destroy(_cell_cache);
	_cell_cache = NULL;
}

/*
 * module hooks
 */
module_init(dm_bio_prison_init);
module_exit(dm_bio_prison_exit);

MODULE_DESCRIPTION(DM_NAME " bio prison");
MODULE_AUTHOR("Joe Thornber <dm-devel@redhat.com>");
MODULE_LICENSE("GPL");
