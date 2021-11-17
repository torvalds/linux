/*
 * Copyright (C) 2012-2017 Red Hat, Inc.
 *
 * This file is released under the GPL.
 */

#include "dm.h"
#include "dm-bio-prison-v2.h"

#include <linux/spinlock.h>
#include <linux/mempool.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/rwsem.h>

/*----------------------------------------------------------------*/

#define MIN_CELLS 1024

struct dm_bio_prison_v2 {
	struct workqueue_struct *wq;

	spinlock_t lock;
	struct rb_root cells;
	mempool_t cell_pool;
};

static struct kmem_cache *_cell_cache;

/*----------------------------------------------------------------*/

/*
 * @nr_cells should be the number of cells you want in use _concurrently_.
 * Don't confuse it with the number of distinct keys.
 */
struct dm_bio_prison_v2 *dm_bio_prison_create_v2(struct workqueue_struct *wq)
{
	struct dm_bio_prison_v2 *prison = kzalloc(sizeof(*prison), GFP_KERNEL);
	int ret;

	if (!prison)
		return NULL;

	prison->wq = wq;
	spin_lock_init(&prison->lock);

	ret = mempool_init_slab_pool(&prison->cell_pool, MIN_CELLS, _cell_cache);
	if (ret) {
		kfree(prison);
		return NULL;
	}

	prison->cells = RB_ROOT;

	return prison;
}
EXPORT_SYMBOL_GPL(dm_bio_prison_create_v2);

void dm_bio_prison_destroy_v2(struct dm_bio_prison_v2 *prison)
{
	mempool_exit(&prison->cell_pool);
	kfree(prison);
}
EXPORT_SYMBOL_GPL(dm_bio_prison_destroy_v2);

struct dm_bio_prison_cell_v2 *dm_bio_prison_alloc_cell_v2(struct dm_bio_prison_v2 *prison, gfp_t gfp)
{
	return mempool_alloc(&prison->cell_pool, gfp);
}
EXPORT_SYMBOL_GPL(dm_bio_prison_alloc_cell_v2);

void dm_bio_prison_free_cell_v2(struct dm_bio_prison_v2 *prison,
				struct dm_bio_prison_cell_v2 *cell)
{
	mempool_free(cell, &prison->cell_pool);
}
EXPORT_SYMBOL_GPL(dm_bio_prison_free_cell_v2);

static void __setup_new_cell(struct dm_cell_key_v2 *key,
			     struct dm_bio_prison_cell_v2 *cell)
{
	memset(cell, 0, sizeof(*cell));
	memcpy(&cell->key, key, sizeof(cell->key));
	bio_list_init(&cell->bios);
}

static int cmp_keys(struct dm_cell_key_v2 *lhs,
		    struct dm_cell_key_v2 *rhs)
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

/*
 * Returns true if node found, otherwise it inserts a new one.
 */
static bool __find_or_insert(struct dm_bio_prison_v2 *prison,
			     struct dm_cell_key_v2 *key,
			     struct dm_bio_prison_cell_v2 *cell_prealloc,
			     struct dm_bio_prison_cell_v2 **result)
{
	int r;
	struct rb_node **new = &prison->cells.rb_node, *parent = NULL;

	while (*new) {
		struct dm_bio_prison_cell_v2 *cell =
			rb_entry(*new, struct dm_bio_prison_cell_v2, node);

		r = cmp_keys(key, &cell->key);

		parent = *new;
		if (r < 0)
			new = &((*new)->rb_left);

		else if (r > 0)
			new = &((*new)->rb_right);

		else {
			*result = cell;
			return true;
		}
	}

	__setup_new_cell(key, cell_prealloc);
	*result = cell_prealloc;
	rb_link_node(&cell_prealloc->node, parent, new);
	rb_insert_color(&cell_prealloc->node, &prison->cells);

	return false;
}

static bool __get(struct dm_bio_prison_v2 *prison,
		  struct dm_cell_key_v2 *key,
		  unsigned lock_level,
		  struct bio *inmate,
		  struct dm_bio_prison_cell_v2 *cell_prealloc,
		  struct dm_bio_prison_cell_v2 **cell)
{
	if (__find_or_insert(prison, key, cell_prealloc, cell)) {
		if ((*cell)->exclusive_lock) {
			if (lock_level <= (*cell)->exclusive_level) {
				bio_list_add(&(*cell)->bios, inmate);
				return false;
			}
		}

		(*cell)->shared_count++;

	} else
		(*cell)->shared_count = 1;

	return true;
}

bool dm_cell_get_v2(struct dm_bio_prison_v2 *prison,
		    struct dm_cell_key_v2 *key,
		    unsigned lock_level,
		    struct bio *inmate,
		    struct dm_bio_prison_cell_v2 *cell_prealloc,
		    struct dm_bio_prison_cell_v2 **cell_result)
{
	int r;

	spin_lock_irq(&prison->lock);
	r = __get(prison, key, lock_level, inmate, cell_prealloc, cell_result);
	spin_unlock_irq(&prison->lock);

	return r;
}
EXPORT_SYMBOL_GPL(dm_cell_get_v2);

static bool __put(struct dm_bio_prison_v2 *prison,
		  struct dm_bio_prison_cell_v2 *cell)
{
	BUG_ON(!cell->shared_count);
	cell->shared_count--;

	// FIXME: shared locks granted above the lock level could starve this
	if (!cell->shared_count) {
		if (cell->exclusive_lock){
			if (cell->quiesce_continuation) {
				queue_work(prison->wq, cell->quiesce_continuation);
				cell->quiesce_continuation = NULL;
			}
		} else {
			rb_erase(&cell->node, &prison->cells);
			return true;
		}
	}

	return false;
}

bool dm_cell_put_v2(struct dm_bio_prison_v2 *prison,
		    struct dm_bio_prison_cell_v2 *cell)
{
	bool r;
	unsigned long flags;

	spin_lock_irqsave(&prison->lock, flags);
	r = __put(prison, cell);
	spin_unlock_irqrestore(&prison->lock, flags);

	return r;
}
EXPORT_SYMBOL_GPL(dm_cell_put_v2);

static int __lock(struct dm_bio_prison_v2 *prison,
		  struct dm_cell_key_v2 *key,
		  unsigned lock_level,
		  struct dm_bio_prison_cell_v2 *cell_prealloc,
		  struct dm_bio_prison_cell_v2 **cell_result)
{
	struct dm_bio_prison_cell_v2 *cell;

	if (__find_or_insert(prison, key, cell_prealloc, &cell)) {
		if (cell->exclusive_lock)
			return -EBUSY;

		cell->exclusive_lock = true;
		cell->exclusive_level = lock_level;
		*cell_result = cell;

		// FIXME: we don't yet know what level these shared locks
		// were taken at, so have to quiesce them all.
		return cell->shared_count > 0;

	} else {
		cell = cell_prealloc;
		cell->shared_count = 0;
		cell->exclusive_lock = true;
		cell->exclusive_level = lock_level;
		*cell_result = cell;
	}

	return 0;
}

int dm_cell_lock_v2(struct dm_bio_prison_v2 *prison,
		    struct dm_cell_key_v2 *key,
		    unsigned lock_level,
		    struct dm_bio_prison_cell_v2 *cell_prealloc,
		    struct dm_bio_prison_cell_v2 **cell_result)
{
	int r;

	spin_lock_irq(&prison->lock);
	r = __lock(prison, key, lock_level, cell_prealloc, cell_result);
	spin_unlock_irq(&prison->lock);

	return r;
}
EXPORT_SYMBOL_GPL(dm_cell_lock_v2);

static void __quiesce(struct dm_bio_prison_v2 *prison,
		      struct dm_bio_prison_cell_v2 *cell,
		      struct work_struct *continuation)
{
	if (!cell->shared_count)
		queue_work(prison->wq, continuation);
	else
		cell->quiesce_continuation = continuation;
}

void dm_cell_quiesce_v2(struct dm_bio_prison_v2 *prison,
			struct dm_bio_prison_cell_v2 *cell,
			struct work_struct *continuation)
{
	spin_lock_irq(&prison->lock);
	__quiesce(prison, cell, continuation);
	spin_unlock_irq(&prison->lock);
}
EXPORT_SYMBOL_GPL(dm_cell_quiesce_v2);

static int __promote(struct dm_bio_prison_v2 *prison,
		     struct dm_bio_prison_cell_v2 *cell,
		     unsigned new_lock_level)
{
	if (!cell->exclusive_lock)
		return -EINVAL;

	cell->exclusive_level = new_lock_level;
	return cell->shared_count > 0;
}

int dm_cell_lock_promote_v2(struct dm_bio_prison_v2 *prison,
			    struct dm_bio_prison_cell_v2 *cell,
			    unsigned new_lock_level)
{
	int r;

	spin_lock_irq(&prison->lock);
	r = __promote(prison, cell, new_lock_level);
	spin_unlock_irq(&prison->lock);

	return r;
}
EXPORT_SYMBOL_GPL(dm_cell_lock_promote_v2);

static bool __unlock(struct dm_bio_prison_v2 *prison,
		     struct dm_bio_prison_cell_v2 *cell,
		     struct bio_list *bios)
{
	BUG_ON(!cell->exclusive_lock);

	bio_list_merge(bios, &cell->bios);
	bio_list_init(&cell->bios);

	if (cell->shared_count) {
		cell->exclusive_lock = false;
		return false;
	}

	rb_erase(&cell->node, &prison->cells);
	return true;
}

bool dm_cell_unlock_v2(struct dm_bio_prison_v2 *prison,
		       struct dm_bio_prison_cell_v2 *cell,
		       struct bio_list *bios)
{
	bool r;

	spin_lock_irq(&prison->lock);
	r = __unlock(prison, cell, bios);
	spin_unlock_irq(&prison->lock);

	return r;
}
EXPORT_SYMBOL_GPL(dm_cell_unlock_v2);

/*----------------------------------------------------------------*/

int __init dm_bio_prison_init_v2(void)
{
	_cell_cache = KMEM_CACHE(dm_bio_prison_cell_v2, 0);
	if (!_cell_cache)
		return -ENOMEM;

	return 0;
}

void dm_bio_prison_exit_v2(void)
{
	kmem_cache_destroy(_cell_cache);
	_cell_cache = NULL;
}
