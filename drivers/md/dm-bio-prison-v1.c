// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2012 Red Hat, Inc.
 *
 * This file is released under the GPL.
 */

#include "dm.h"
#include "dm-bio-prison-v1.h"
#include "dm-bio-prison-v2.h"

#include <linux/spinlock.h>
#include <linux/mempool.h>
#include <linux/module.h>
#include <linux/slab.h>

/*----------------------------------------------------------------*/

#define MIN_CELLS 1024

struct prison_region {
	spinlock_t lock;
	struct rb_root cell;
} ____cacheline_aligned_in_smp;

struct dm_bio_prison {
	mempool_t cell_pool;
	unsigned int num_locks;
	struct prison_region regions[] __counted_by(num_locks);
};

static struct kmem_cache *_cell_cache;

/*----------------------------------------------------------------*/

/*
 * @nr_cells should be the number of cells you want in use _concurrently_.
 * Don't confuse it with the number of distinct keys.
 */
struct dm_bio_prison *dm_bio_prison_create(void)
{
	int ret;
	unsigned int i, num_locks;
	struct dm_bio_prison *prison;

	num_locks = dm_num_hash_locks();
	prison = kzalloc(struct_size(prison, regions, num_locks), GFP_KERNEL);
	if (!prison)
		return NULL;
	prison->num_locks = num_locks;

	for (i = 0; i < prison->num_locks; i++) {
		spin_lock_init(&prison->regions[i].lock);
		prison->regions[i].cell = RB_ROOT;
	}

	ret = mempool_init_slab_pool(&prison->cell_pool, MIN_CELLS, _cell_cache);
	if (ret) {
		kfree(prison);
		return NULL;
	}

	return prison;
}
EXPORT_SYMBOL_GPL(dm_bio_prison_create);

void dm_bio_prison_destroy(struct dm_bio_prison *prison)
{
	mempool_exit(&prison->cell_pool);
	kfree(prison);
}
EXPORT_SYMBOL_GPL(dm_bio_prison_destroy);

struct dm_bio_prison_cell *dm_bio_prison_alloc_cell(struct dm_bio_prison *prison, gfp_t gfp)
{
	return mempool_alloc(&prison->cell_pool, gfp);
}
EXPORT_SYMBOL_GPL(dm_bio_prison_alloc_cell);

void dm_bio_prison_free_cell(struct dm_bio_prison *prison,
			     struct dm_bio_prison_cell *cell)
{
	mempool_free(cell, &prison->cell_pool);
}
EXPORT_SYMBOL_GPL(dm_bio_prison_free_cell);

static void __setup_new_cell(struct dm_cell_key *key,
			     struct bio *holder,
			     struct dm_bio_prison_cell *cell)
{
	memcpy(&cell->key, key, sizeof(cell->key));
	cell->holder = holder;
	bio_list_init(&cell->bios);
}

static int cmp_keys(struct dm_cell_key *lhs,
		    struct dm_cell_key *rhs)
{
	if (lhs->virtual < rhs->virtual)
		return -1;

	if (lhs->virtual > rhs->virtual)
		return 1;

	if (lhs->dev < rhs->dev)
		return -1;

	if (lhs->dev > rhs->dev)
		return 1;

	if (lhs->block_end <= rhs->block_begin)
		return -1;

	if (lhs->block_begin >= rhs->block_end)
		return 1;

	return 0;
}

static inline unsigned int lock_nr(struct dm_cell_key *key, unsigned int num_locks)
{
	return dm_hash_locks_index((key->block_begin >> BIO_PRISON_MAX_RANGE_SHIFT),
				   num_locks);
}

bool dm_cell_key_has_valid_range(struct dm_cell_key *key)
{
	if (WARN_ON_ONCE(key->block_end - key->block_begin > BIO_PRISON_MAX_RANGE))
		return false;
	if (WARN_ON_ONCE((key->block_begin >> BIO_PRISON_MAX_RANGE_SHIFT) !=
			 (key->block_end - 1) >> BIO_PRISON_MAX_RANGE_SHIFT))
		return false;

	return true;
}
EXPORT_SYMBOL(dm_cell_key_has_valid_range);

static int __bio_detain(struct rb_root *root,
			struct dm_cell_key *key,
			struct bio *inmate,
			struct dm_bio_prison_cell *cell_prealloc,
			struct dm_bio_prison_cell **cell_result)
{
	int r;
	struct rb_node **new = &root->rb_node, *parent = NULL;

	while (*new) {
		struct dm_bio_prison_cell *cell =
			rb_entry(*new, struct dm_bio_prison_cell, node);

		r = cmp_keys(key, &cell->key);

		parent = *new;
		if (r < 0)
			new = &((*new)->rb_left);
		else if (r > 0)
			new = &((*new)->rb_right);
		else {
			if (inmate)
				bio_list_add(&cell->bios, inmate);
			*cell_result = cell;
			return 1;
		}
	}

	__setup_new_cell(key, inmate, cell_prealloc);
	*cell_result = cell_prealloc;

	rb_link_node(&cell_prealloc->node, parent, new);
	rb_insert_color(&cell_prealloc->node, root);

	return 0;
}

static int bio_detain(struct dm_bio_prison *prison,
		      struct dm_cell_key *key,
		      struct bio *inmate,
		      struct dm_bio_prison_cell *cell_prealloc,
		      struct dm_bio_prison_cell **cell_result)
{
	int r;
	unsigned l = lock_nr(key, prison->num_locks);

	spin_lock_irq(&prison->regions[l].lock);
	r = __bio_detain(&prison->regions[l].cell, key, inmate, cell_prealloc, cell_result);
	spin_unlock_irq(&prison->regions[l].lock);

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
static void __cell_release(struct rb_root *root,
			   struct dm_bio_prison_cell *cell,
			   struct bio_list *inmates)
{
	rb_erase(&cell->node, root);

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
	unsigned l = lock_nr(&cell->key, prison->num_locks);

	spin_lock_irq(&prison->regions[l].lock);
	__cell_release(&prison->regions[l].cell, cell, bios);
	spin_unlock_irq(&prison->regions[l].lock);
}
EXPORT_SYMBOL_GPL(dm_cell_release);

/*
 * Sometimes we don't want the holder, just the additional bios.
 */
static void __cell_release_no_holder(struct rb_root *root,
				     struct dm_bio_prison_cell *cell,
				     struct bio_list *inmates)
{
	rb_erase(&cell->node, root);
	bio_list_merge(inmates, &cell->bios);
}

void dm_cell_release_no_holder(struct dm_bio_prison *prison,
			       struct dm_bio_prison_cell *cell,
			       struct bio_list *inmates)
{
	unsigned l = lock_nr(&cell->key, prison->num_locks);
	unsigned long flags;

	spin_lock_irqsave(&prison->regions[l].lock, flags);
	__cell_release_no_holder(&prison->regions[l].cell, cell, inmates);
	spin_unlock_irqrestore(&prison->regions[l].lock, flags);
}
EXPORT_SYMBOL_GPL(dm_cell_release_no_holder);

void dm_cell_error(struct dm_bio_prison *prison,
		   struct dm_bio_prison_cell *cell, blk_status_t error)
{
	struct bio_list bios;
	struct bio *bio;

	bio_list_init(&bios);
	dm_cell_release(prison, cell, &bios);

	while ((bio = bio_list_pop(&bios))) {
		bio->bi_status = error;
		bio_endio(bio);
	}
}
EXPORT_SYMBOL_GPL(dm_cell_error);

void dm_cell_visit_release(struct dm_bio_prison *prison,
			   void (*visit_fn)(void *, struct dm_bio_prison_cell *),
			   void *context,
			   struct dm_bio_prison_cell *cell)
{
	unsigned l = lock_nr(&cell->key, prison->num_locks);
	spin_lock_irq(&prison->regions[l].lock);
	visit_fn(context, cell);
	rb_erase(&cell->node, &prison->regions[l].cell);
	spin_unlock_irq(&prison->regions[l].lock);
}
EXPORT_SYMBOL_GPL(dm_cell_visit_release);

/*----------------------------------------------------------------*/

#define DEFERRED_SET_SIZE 64

struct dm_deferred_entry {
	struct dm_deferred_set *ds;
	unsigned int count;
	struct list_head work_items;
};

struct dm_deferred_set {
	spinlock_t lock;
	unsigned int current_entry;
	unsigned int sweeper;
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

static unsigned int ds_next(unsigned int index)
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
	unsigned int next_entry;

	spin_lock_irq(&ds->lock);
	if ((ds->sweeper == ds->current_entry) &&
	    !ds->entries[ds->current_entry].count)
		r = 0;
	else {
		list_add(work, &ds->entries[ds->current_entry].work_items);
		next_entry = ds_next(ds->current_entry);
		if (!ds->entries[next_entry].count)
			ds->current_entry = next_entry;
	}
	spin_unlock_irq(&ds->lock);

	return r;
}
EXPORT_SYMBOL_GPL(dm_deferred_set_add_work);

/*----------------------------------------------------------------*/

static int __init dm_bio_prison_init_v1(void)
{
	_cell_cache = KMEM_CACHE(dm_bio_prison_cell, 0);
	if (!_cell_cache)
		return -ENOMEM;

	return 0;
}

static void dm_bio_prison_exit_v1(void)
{
	kmem_cache_destroy(_cell_cache);
	_cell_cache = NULL;
}

static int (*_inits[])(void) __initdata = {
	dm_bio_prison_init_v1,
	dm_bio_prison_init_v2,
};

static void (*_exits[])(void) = {
	dm_bio_prison_exit_v1,
	dm_bio_prison_exit_v2,
};

static int __init dm_bio_prison_init(void)
{
	const int count = ARRAY_SIZE(_inits);

	int r, i;

	for (i = 0; i < count; i++) {
		r = _inits[i]();
		if (r)
			goto bad;
	}

	return 0;

bad:
	while (i--)
		_exits[i]();

	return r;
}

static void __exit dm_bio_prison_exit(void)
{
	int i = ARRAY_SIZE(_exits);

	while (i--)
		_exits[i]();
}

/*
 * module hooks
 */
module_init(dm_bio_prison_init);
module_exit(dm_bio_prison_exit);

MODULE_DESCRIPTION(DM_NAME " bio prison");
MODULE_AUTHOR("Joe Thornber <dm-devel@lists.linux.dev>");
MODULE_LICENSE("GPL");
