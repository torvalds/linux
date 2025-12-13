// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2009-2011 Red Hat, Inc.
 *
 * Author: Mikulas Patocka <mpatocka@redhat.com>
 *
 * This file is released under the GPL.
 */

#include <linux/dm-bufio.h>

#include <linux/device-mapper.h>
#include <linux/dm-io.h>
#include <linux/slab.h>
#include <linux/sched/mm.h>
#include <linux/jiffies.h>
#include <linux/vmalloc.h>
#include <linux/shrinker.h>
#include <linux/module.h>
#include <linux/rbtree.h>
#include <linux/stacktrace.h>
#include <linux/jump_label.h>

#include "dm.h"

#define DM_MSG_PREFIX "bufio"

/*
 * Memory management policy:
 *	Limit the number of buffers to DM_BUFIO_MEMORY_PERCENT of main memory
 *	or DM_BUFIO_VMALLOC_PERCENT of vmalloc memory (whichever is lower).
 *	Always allocate at least DM_BUFIO_MIN_BUFFERS buffers.
 *	Start background writeback when there are DM_BUFIO_WRITEBACK_PERCENT
 *	dirty buffers.
 */
#define DM_BUFIO_MIN_BUFFERS		8

#define DM_BUFIO_MEMORY_PERCENT		2
#define DM_BUFIO_VMALLOC_PERCENT	25
#define DM_BUFIO_WRITEBACK_RATIO	3
#define DM_BUFIO_LOW_WATERMARK_RATIO	16

/*
 * The nr of bytes of cached data to keep around.
 */
#define DM_BUFIO_DEFAULT_RETAIN_BYTES   (256 * 1024)

/*
 * Align buffer writes to this boundary.
 * Tests show that SSDs have the highest IOPS when using 4k writes.
 */
#define DM_BUFIO_WRITE_ALIGN		4096

/*
 * dm_buffer->list_mode
 */
#define LIST_CLEAN	0
#define LIST_DIRTY	1
#define LIST_SIZE	2

#define SCAN_RESCHED_CYCLE	16

/*--------------------------------------------------------------*/

/*
 * Rather than use an LRU list, we use a clock algorithm where entries
 * are held in a circular list.  When an entry is 'hit' a reference bit
 * is set.  The least recently used entry is approximated by running a
 * cursor around the list selecting unreferenced entries. Referenced
 * entries have their reference bit cleared as the cursor passes them.
 */
struct lru_entry {
	struct list_head list;
	atomic_t referenced;
};

struct lru_iter {
	struct lru *lru;
	struct list_head list;
	struct lru_entry *stop;
	struct lru_entry *e;
};

struct lru {
	struct list_head *cursor;
	unsigned long count;

	struct list_head iterators;
};

/*--------------*/

static void lru_init(struct lru *lru)
{
	lru->cursor = NULL;
	lru->count = 0;
	INIT_LIST_HEAD(&lru->iterators);
}

static void lru_destroy(struct lru *lru)
{
	WARN_ON_ONCE(lru->cursor);
	WARN_ON_ONCE(!list_empty(&lru->iterators));
}

/*
 * Insert a new entry into the lru.
 */
static void lru_insert(struct lru *lru, struct lru_entry *le)
{
	/*
	 * Don't be tempted to set to 1, makes the lru aspect
	 * perform poorly.
	 */
	atomic_set(&le->referenced, 0);

	if (lru->cursor) {
		list_add_tail(&le->list, lru->cursor);
	} else {
		INIT_LIST_HEAD(&le->list);
		lru->cursor = &le->list;
	}
	lru->count++;
}

/*--------------*/

/*
 * Convert a list_head pointer to an lru_entry pointer.
 */
static inline struct lru_entry *to_le(struct list_head *l)
{
	return container_of(l, struct lru_entry, list);
}

/*
 * Initialize an lru_iter and add it to the list of cursors in the lru.
 */
static void lru_iter_begin(struct lru *lru, struct lru_iter *it)
{
	it->lru = lru;
	it->stop = lru->cursor ? to_le(lru->cursor->prev) : NULL;
	it->e = lru->cursor ? to_le(lru->cursor) : NULL;
	list_add(&it->list, &lru->iterators);
}

/*
 * Remove an lru_iter from the list of cursors in the lru.
 */
static inline void lru_iter_end(struct lru_iter *it)
{
	list_del(&it->list);
}

/* Predicate function type to be used with lru_iter_next */
typedef bool (*iter_predicate)(struct lru_entry *le, void *context);

/*
 * Advance the cursor to the next entry that passes the
 * predicate, and return that entry.  Returns NULL if the
 * iteration is complete.
 */
static struct lru_entry *lru_iter_next(struct lru_iter *it,
				       iter_predicate pred, void *context)
{
	struct lru_entry *e;

	while (it->e) {
		e = it->e;

		/* advance the cursor */
		if (it->e == it->stop)
			it->e = NULL;
		else
			it->e = to_le(it->e->list.next);

		if (pred(e, context))
			return e;
	}

	return NULL;
}

/*
 * Invalidate a specific lru_entry and update all cursors in
 * the lru accordingly.
 */
static void lru_iter_invalidate(struct lru *lru, struct lru_entry *e)
{
	struct lru_iter *it;

	list_for_each_entry(it, &lru->iterators, list) {
		/* Move c->e forwards if necc. */
		if (it->e == e) {
			it->e = to_le(it->e->list.next);
			if (it->e == e)
				it->e = NULL;
		}

		/* Move it->stop backwards if necc. */
		if (it->stop == e) {
			it->stop = to_le(it->stop->list.prev);
			if (it->stop == e)
				it->stop = NULL;
		}
	}
}

/*--------------*/

/*
 * Remove a specific entry from the lru.
 */
static void lru_remove(struct lru *lru, struct lru_entry *le)
{
	lru_iter_invalidate(lru, le);
	if (lru->count == 1) {
		lru->cursor = NULL;
	} else {
		if (lru->cursor == &le->list)
			lru->cursor = lru->cursor->next;
		list_del(&le->list);
	}
	lru->count--;
}

/*
 * Mark as referenced.
 */
static inline void lru_reference(struct lru_entry *le)
{
	atomic_set(&le->referenced, 1);
}

/*--------------*/

/*
 * Remove the least recently used entry (approx), that passes the predicate.
 * Returns NULL on failure.
 */
enum evict_result {
	ER_EVICT,
	ER_DONT_EVICT,
	ER_STOP, /* stop looking for something to evict */
};

typedef enum evict_result (*le_predicate)(struct lru_entry *le, void *context);

static struct lru_entry *lru_evict(struct lru *lru, le_predicate pred, void *context, bool no_sleep)
{
	unsigned long tested = 0;
	struct list_head *h = lru->cursor;
	struct lru_entry *le;

	if (!h)
		return NULL;
	/*
	 * In the worst case we have to loop around twice. Once to clear
	 * the reference flags, and then again to discover the predicate
	 * fails for all entries.
	 */
	while (tested < lru->count) {
		le = container_of(h, struct lru_entry, list);

		if (atomic_read(&le->referenced)) {
			atomic_set(&le->referenced, 0);
		} else {
			tested++;
			switch (pred(le, context)) {
			case ER_EVICT:
				/*
				 * Adjust the cursor, so we start the next
				 * search from here.
				 */
				lru->cursor = le->list.next;
				lru_remove(lru, le);
				return le;

			case ER_DONT_EVICT:
				break;

			case ER_STOP:
				lru->cursor = le->list.next;
				return NULL;
			}
		}

		h = h->next;

		if (!no_sleep)
			cond_resched();
	}

	return NULL;
}

/*--------------------------------------------------------------*/

/*
 * Buffer state bits.
 */
#define B_READING	0
#define B_WRITING	1
#define B_DIRTY		2

/*
 * Describes how the block was allocated:
 * kmem_cache_alloc(), __get_free_pages() or vmalloc().
 * See the comment at alloc_buffer_data.
 */
enum data_mode {
	DATA_MODE_SLAB = 0,
	DATA_MODE_KMALLOC = 1,
	DATA_MODE_GET_FREE_PAGES = 2,
	DATA_MODE_VMALLOC = 3,
	DATA_MODE_LIMIT = 4
};

struct dm_buffer {
	/* protected by the locks in dm_buffer_cache */
	struct rb_node node;

	/* immutable, so don't need protecting */
	sector_t block;
	void *data;
	unsigned char data_mode;		/* DATA_MODE_* */

	/*
	 * These two fields are used in isolation, so do not need
	 * a surrounding lock.
	 */
	atomic_t hold_count;
	unsigned long last_accessed;

	/*
	 * Everything else is protected by the mutex in
	 * dm_bufio_client
	 */
	unsigned long state;
	struct lru_entry lru;
	unsigned char list_mode;		/* LIST_* */
	blk_status_t read_error;
	blk_status_t write_error;
	unsigned int dirty_start;
	unsigned int dirty_end;
	unsigned int write_start;
	unsigned int write_end;
	struct list_head write_list;
	struct dm_bufio_client *c;
	void (*end_io)(struct dm_buffer *b, blk_status_t bs);
#ifdef CONFIG_DM_DEBUG_BLOCK_STACK_TRACING
#define MAX_STACK 10
	unsigned int stack_len;
	unsigned long stack_entries[MAX_STACK];
#endif
};

/*--------------------------------------------------------------*/

/*
 * The buffer cache manages buffers, particularly:
 *  - inc/dec of holder count
 *  - setting the last_accessed field
 *  - maintains clean/dirty state along with lru
 *  - selecting buffers that match predicates
 *
 * It does *not* handle:
 *  - allocation/freeing of buffers.
 *  - IO
 *  - Eviction or cache sizing.
 *
 * cache_get() and cache_put() are threadsafe, you do not need to
 * protect these calls with a surrounding mutex.  All the other
 * methods are not threadsafe; they do use locking primitives, but
 * only enough to ensure get/put are threadsafe.
 */

struct buffer_tree {
	union {
		struct rw_semaphore lock;
		rwlock_t spinlock;
	} u;
	struct rb_root root;
} ____cacheline_aligned_in_smp;

struct dm_buffer_cache {
	struct lru lru[LIST_SIZE];
	/*
	 * We spread entries across multiple trees to reduce contention
	 * on the locks.
	 */
	unsigned int num_locks;
	bool no_sleep;
	struct buffer_tree trees[];
};

static DEFINE_STATIC_KEY_FALSE(no_sleep_enabled);

static inline unsigned int cache_index(sector_t block, unsigned int num_locks)
{
	return dm_hash_locks_index(block, num_locks);
}

static inline void cache_read_lock(struct dm_buffer_cache *bc, sector_t block)
{
	if (static_branch_unlikely(&no_sleep_enabled) && bc->no_sleep)
		read_lock_bh(&bc->trees[cache_index(block, bc->num_locks)].u.spinlock);
	else
		down_read(&bc->trees[cache_index(block, bc->num_locks)].u.lock);
}

static inline void cache_read_unlock(struct dm_buffer_cache *bc, sector_t block)
{
	if (static_branch_unlikely(&no_sleep_enabled) && bc->no_sleep)
		read_unlock_bh(&bc->trees[cache_index(block, bc->num_locks)].u.spinlock);
	else
		up_read(&bc->trees[cache_index(block, bc->num_locks)].u.lock);
}

static inline void cache_write_lock(struct dm_buffer_cache *bc, sector_t block)
{
	if (static_branch_unlikely(&no_sleep_enabled) && bc->no_sleep)
		write_lock_bh(&bc->trees[cache_index(block, bc->num_locks)].u.spinlock);
	else
		down_write(&bc->trees[cache_index(block, bc->num_locks)].u.lock);
}

static inline void cache_write_unlock(struct dm_buffer_cache *bc, sector_t block)
{
	if (static_branch_unlikely(&no_sleep_enabled) && bc->no_sleep)
		write_unlock_bh(&bc->trees[cache_index(block, bc->num_locks)].u.spinlock);
	else
		up_write(&bc->trees[cache_index(block, bc->num_locks)].u.lock);
}

/*
 * Sometimes we want to repeatedly get and drop locks as part of an iteration.
 * This struct helps avoid redundant drop and gets of the same lock.
 */
struct lock_history {
	struct dm_buffer_cache *cache;
	bool write;
	unsigned int previous;
	unsigned int no_previous;
};

static void lh_init(struct lock_history *lh, struct dm_buffer_cache *cache, bool write)
{
	lh->cache = cache;
	lh->write = write;
	lh->no_previous = cache->num_locks;
	lh->previous = lh->no_previous;
}

static void __lh_lock(struct lock_history *lh, unsigned int index)
{
	if (lh->write) {
		if (static_branch_unlikely(&no_sleep_enabled) && lh->cache->no_sleep)
			write_lock_bh(&lh->cache->trees[index].u.spinlock);
		else
			down_write(&lh->cache->trees[index].u.lock);
	} else {
		if (static_branch_unlikely(&no_sleep_enabled) && lh->cache->no_sleep)
			read_lock_bh(&lh->cache->trees[index].u.spinlock);
		else
			down_read(&lh->cache->trees[index].u.lock);
	}
}

static void __lh_unlock(struct lock_history *lh, unsigned int index)
{
	if (lh->write) {
		if (static_branch_unlikely(&no_sleep_enabled) && lh->cache->no_sleep)
			write_unlock_bh(&lh->cache->trees[index].u.spinlock);
		else
			up_write(&lh->cache->trees[index].u.lock);
	} else {
		if (static_branch_unlikely(&no_sleep_enabled) && lh->cache->no_sleep)
			read_unlock_bh(&lh->cache->trees[index].u.spinlock);
		else
			up_read(&lh->cache->trees[index].u.lock);
	}
}

/*
 * Make sure you call this since it will unlock the final lock.
 */
static void lh_exit(struct lock_history *lh)
{
	if (lh->previous != lh->no_previous) {
		__lh_unlock(lh, lh->previous);
		lh->previous = lh->no_previous;
	}
}

/*
 * Named 'next' because there is no corresponding
 * 'up/unlock' call since it's done automatically.
 */
static void lh_next(struct lock_history *lh, sector_t b)
{
	unsigned int index = cache_index(b, lh->no_previous); /* no_previous is num_locks */

	if (lh->previous != lh->no_previous) {
		if (lh->previous != index) {
			__lh_unlock(lh, lh->previous);
			__lh_lock(lh, index);
			lh->previous = index;
		}
	} else {
		__lh_lock(lh, index);
		lh->previous = index;
	}
}

static inline struct dm_buffer *le_to_buffer(struct lru_entry *le)
{
	return container_of(le, struct dm_buffer, lru);
}

static struct dm_buffer *list_to_buffer(struct list_head *l)
{
	struct lru_entry *le = list_entry(l, struct lru_entry, list);

	return le_to_buffer(le);
}

static void cache_init(struct dm_buffer_cache *bc, unsigned int num_locks, bool no_sleep)
{
	unsigned int i;

	bc->num_locks = num_locks;
	bc->no_sleep = no_sleep;

	for (i = 0; i < bc->num_locks; i++) {
		if (no_sleep)
			rwlock_init(&bc->trees[i].u.spinlock);
		else
			init_rwsem(&bc->trees[i].u.lock);
		bc->trees[i].root = RB_ROOT;
	}

	lru_init(&bc->lru[LIST_CLEAN]);
	lru_init(&bc->lru[LIST_DIRTY]);
}

static void cache_destroy(struct dm_buffer_cache *bc)
{
	unsigned int i;

	for (i = 0; i < bc->num_locks; i++)
		WARN_ON_ONCE(!RB_EMPTY_ROOT(&bc->trees[i].root));

	lru_destroy(&bc->lru[LIST_CLEAN]);
	lru_destroy(&bc->lru[LIST_DIRTY]);
}

/*--------------*/

/*
 * not threadsafe, or racey depending how you look at it
 */
static inline unsigned long cache_count(struct dm_buffer_cache *bc, int list_mode)
{
	return bc->lru[list_mode].count;
}

static inline unsigned long cache_total(struct dm_buffer_cache *bc)
{
	return cache_count(bc, LIST_CLEAN) + cache_count(bc, LIST_DIRTY);
}

/*--------------*/

/*
 * Gets a specific buffer, indexed by block.
 * If the buffer is found then its holder count will be incremented and
 * lru_reference will be called.
 *
 * threadsafe
 */
static struct dm_buffer *__cache_get(const struct rb_root *root, sector_t block)
{
	struct rb_node *n = root->rb_node;
	struct dm_buffer *b;

	while (n) {
		b = container_of(n, struct dm_buffer, node);

		if (b->block == block)
			return b;

		n = block < b->block ? n->rb_left : n->rb_right;
	}

	return NULL;
}

static void __cache_inc_buffer(struct dm_buffer *b)
{
	atomic_inc(&b->hold_count);
	WRITE_ONCE(b->last_accessed, jiffies);
}

static struct dm_buffer *cache_get(struct dm_buffer_cache *bc, sector_t block)
{
	struct dm_buffer *b;

	cache_read_lock(bc, block);
	b = __cache_get(&bc->trees[cache_index(block, bc->num_locks)].root, block);
	if (b) {
		lru_reference(&b->lru);
		__cache_inc_buffer(b);
	}
	cache_read_unlock(bc, block);

	return b;
}

/*--------------*/

/*
 * Returns true if the hold count hits zero.
 * threadsafe
 */
static bool cache_put(struct dm_buffer_cache *bc, struct dm_buffer *b)
{
	bool r;

	cache_read_lock(bc, b->block);
	BUG_ON(!atomic_read(&b->hold_count));
	r = atomic_dec_and_test(&b->hold_count);
	cache_read_unlock(bc, b->block);

	return r;
}

/*--------------*/

typedef enum evict_result (*b_predicate)(struct dm_buffer *, void *);

/*
 * Evicts a buffer based on a predicate.  The oldest buffer that
 * matches the predicate will be selected.  In addition to the
 * predicate the hold_count of the selected buffer will be zero.
 */
struct evict_wrapper {
	struct lock_history *lh;
	b_predicate pred;
	void *context;
};

/*
 * Wraps the buffer predicate turning it into an lru predicate.  Adds
 * extra test for hold_count.
 */
static enum evict_result __evict_pred(struct lru_entry *le, void *context)
{
	struct evict_wrapper *w = context;
	struct dm_buffer *b = le_to_buffer(le);

	lh_next(w->lh, b->block);

	if (atomic_read(&b->hold_count))
		return ER_DONT_EVICT;

	return w->pred(b, w->context);
}

static struct dm_buffer *__cache_evict(struct dm_buffer_cache *bc, int list_mode,
				       b_predicate pred, void *context,
				       struct lock_history *lh)
{
	struct evict_wrapper w = {.lh = lh, .pred = pred, .context = context};
	struct lru_entry *le;
	struct dm_buffer *b;

	le = lru_evict(&bc->lru[list_mode], __evict_pred, &w, bc->no_sleep);
	if (!le)
		return NULL;

	b = le_to_buffer(le);
	/* __evict_pred will have locked the appropriate tree. */
	rb_erase(&b->node, &bc->trees[cache_index(b->block, bc->num_locks)].root);

	return b;
}

static struct dm_buffer *cache_evict(struct dm_buffer_cache *bc, int list_mode,
				     b_predicate pred, void *context)
{
	struct dm_buffer *b;
	struct lock_history lh;

	lh_init(&lh, bc, true);
	b = __cache_evict(bc, list_mode, pred, context, &lh);
	lh_exit(&lh);

	return b;
}

/*--------------*/

/*
 * Mark a buffer as clean or dirty. Not threadsafe.
 */
static void cache_mark(struct dm_buffer_cache *bc, struct dm_buffer *b, int list_mode)
{
	cache_write_lock(bc, b->block);
	if (list_mode != b->list_mode) {
		lru_remove(&bc->lru[b->list_mode], &b->lru);
		b->list_mode = list_mode;
		lru_insert(&bc->lru[b->list_mode], &b->lru);
	}
	cache_write_unlock(bc, b->block);
}

/*--------------*/

/*
 * Runs through the lru associated with 'old_mode', if the predicate matches then
 * it moves them to 'new_mode'.  Not threadsafe.
 */
static void __cache_mark_many(struct dm_buffer_cache *bc, int old_mode, int new_mode,
			      b_predicate pred, void *context, struct lock_history *lh)
{
	struct lru_entry *le;
	struct dm_buffer *b;
	struct evict_wrapper w = {.lh = lh, .pred = pred, .context = context};

	while (true) {
		le = lru_evict(&bc->lru[old_mode], __evict_pred, &w, bc->no_sleep);
		if (!le)
			break;

		b = le_to_buffer(le);
		b->list_mode = new_mode;
		lru_insert(&bc->lru[b->list_mode], &b->lru);
	}
}

static void cache_mark_many(struct dm_buffer_cache *bc, int old_mode, int new_mode,
			    b_predicate pred, void *context)
{
	struct lock_history lh;

	lh_init(&lh, bc, true);
	__cache_mark_many(bc, old_mode, new_mode, pred, context, &lh);
	lh_exit(&lh);
}

/*--------------*/

/*
 * Iterates through all clean or dirty entries calling a function for each
 * entry.  The callback may terminate the iteration early.  Not threadsafe.
 */

/*
 * Iterator functions should return one of these actions to indicate
 * how the iteration should proceed.
 */
enum it_action {
	IT_NEXT,
	IT_COMPLETE,
};

typedef enum it_action (*iter_fn)(struct dm_buffer *b, void *context);

static void __cache_iterate(struct dm_buffer_cache *bc, int list_mode,
			    iter_fn fn, void *context, struct lock_history *lh)
{
	struct lru *lru = &bc->lru[list_mode];
	struct lru_entry *le, *first;

	if (!lru->cursor)
		return;

	first = le = to_le(lru->cursor);
	do {
		struct dm_buffer *b = le_to_buffer(le);

		lh_next(lh, b->block);

		switch (fn(b, context)) {
		case IT_NEXT:
			break;

		case IT_COMPLETE:
			return;
		}
		cond_resched();

		le = to_le(le->list.next);
	} while (le != first);
}

static void cache_iterate(struct dm_buffer_cache *bc, int list_mode,
			  iter_fn fn, void *context)
{
	struct lock_history lh;

	lh_init(&lh, bc, false);
	__cache_iterate(bc, list_mode, fn, context, &lh);
	lh_exit(&lh);
}

/*--------------*/

/*
 * Passes ownership of the buffer to the cache. Returns false if the
 * buffer was already present (in which case ownership does not pass).
 * eg, a race with another thread.
 *
 * Holder count should be 1 on insertion.
 *
 * Not threadsafe.
 */
static bool __cache_insert(struct rb_root *root, struct dm_buffer *b)
{
	struct rb_node **new = &root->rb_node, *parent = NULL;
	struct dm_buffer *found;

	while (*new) {
		found = container_of(*new, struct dm_buffer, node);

		if (found->block == b->block)
			return false;

		parent = *new;
		new = b->block < found->block ?
			&found->node.rb_left : &found->node.rb_right;
	}

	rb_link_node(&b->node, parent, new);
	rb_insert_color(&b->node, root);

	return true;
}

static bool cache_insert(struct dm_buffer_cache *bc, struct dm_buffer *b)
{
	bool r;

	if (WARN_ON_ONCE(b->list_mode >= LIST_SIZE))
		return false;

	cache_write_lock(bc, b->block);
	BUG_ON(atomic_read(&b->hold_count) != 1);
	r = __cache_insert(&bc->trees[cache_index(b->block, bc->num_locks)].root, b);
	if (r)
		lru_insert(&bc->lru[b->list_mode], &b->lru);
	cache_write_unlock(bc, b->block);

	return r;
}

/*--------------*/

/*
 * Removes buffer from cache, ownership of the buffer passes back to the caller.
 * Fails if the hold_count is not one (ie. the caller holds the only reference).
 *
 * Not threadsafe.
 */
static bool cache_remove(struct dm_buffer_cache *bc, struct dm_buffer *b)
{
	bool r;

	cache_write_lock(bc, b->block);

	if (atomic_read(&b->hold_count) != 1) {
		r = false;
	} else {
		r = true;
		rb_erase(&b->node, &bc->trees[cache_index(b->block, bc->num_locks)].root);
		lru_remove(&bc->lru[b->list_mode], &b->lru);
	}

	cache_write_unlock(bc, b->block);

	return r;
}

/*--------------*/

typedef void (*b_release)(struct dm_buffer *);

static struct dm_buffer *__find_next(struct rb_root *root, sector_t block)
{
	struct rb_node *n = root->rb_node;
	struct dm_buffer *b;
	struct dm_buffer *best = NULL;

	while (n) {
		b = container_of(n, struct dm_buffer, node);

		if (b->block == block)
			return b;

		if (block <= b->block) {
			n = n->rb_left;
			best = b;
		} else {
			n = n->rb_right;
		}
	}

	return best;
}

static void __remove_range(struct dm_buffer_cache *bc,
			   struct rb_root *root,
			   sector_t begin, sector_t end,
			   b_predicate pred, b_release release)
{
	struct dm_buffer *b;

	while (true) {
		cond_resched();

		b = __find_next(root, begin);
		if (!b || (b->block >= end))
			break;

		begin = b->block + 1;

		if (atomic_read(&b->hold_count))
			continue;

		if (pred(b, NULL) == ER_EVICT) {
			rb_erase(&b->node, root);
			lru_remove(&bc->lru[b->list_mode], &b->lru);
			release(b);
		}
	}
}

static void cache_remove_range(struct dm_buffer_cache *bc,
			       sector_t begin, sector_t end,
			       b_predicate pred, b_release release)
{
	unsigned int i;

	BUG_ON(bc->no_sleep);
	for (i = 0; i < bc->num_locks; i++) {
		down_write(&bc->trees[i].u.lock);
		__remove_range(bc, &bc->trees[i].root, begin, end, pred, release);
		up_write(&bc->trees[i].u.lock);
	}
}

/*----------------------------------------------------------------*/

/*
 * Linking of buffers:
 *	All buffers are linked to buffer_cache with their node field.
 *
 *	Clean buffers that are not being written (B_WRITING not set)
 *	are linked to lru[LIST_CLEAN] with their lru_list field.
 *
 *	Dirty and clean buffers that are being written are linked to
 *	lru[LIST_DIRTY] with their lru_list field. When the write
 *	finishes, the buffer cannot be relinked immediately (because we
 *	are in an interrupt context and relinking requires process
 *	context), so some clean-not-writing buffers can be held on
 *	dirty_lru too.  They are later added to lru in the process
 *	context.
 */
struct dm_bufio_client {
	struct block_device *bdev;
	unsigned int block_size;
	s8 sectors_per_block_bits;

	bool no_sleep;
	struct mutex lock;
	spinlock_t spinlock;

	int async_write_error;

	void (*alloc_callback)(struct dm_buffer *buf);
	void (*write_callback)(struct dm_buffer *buf);
	struct kmem_cache *slab_buffer;
	struct kmem_cache *slab_cache;
	struct dm_io_client *dm_io;

	struct list_head reserved_buffers;
	unsigned int need_reserved_buffers;

	unsigned int minimum_buffers;

	sector_t start;

	struct shrinker *shrinker;
	struct work_struct shrink_work;
	atomic_long_t need_shrink;

	wait_queue_head_t free_buffer_wait;

	struct list_head client_list;

	/*
	 * Used by global_cleanup to sort the clients list.
	 */
	unsigned long oldest_buffer;

	struct dm_buffer_cache cache; /* must be last member */
};

/*----------------------------------------------------------------*/

#define dm_bufio_in_request()	(!!current->bio_list)

static void dm_bufio_lock(struct dm_bufio_client *c)
{
	if (static_branch_unlikely(&no_sleep_enabled) && c->no_sleep)
		spin_lock_bh(&c->spinlock);
	else
		mutex_lock_nested(&c->lock, dm_bufio_in_request());
}

static void dm_bufio_unlock(struct dm_bufio_client *c)
{
	if (static_branch_unlikely(&no_sleep_enabled) && c->no_sleep)
		spin_unlock_bh(&c->spinlock);
	else
		mutex_unlock(&c->lock);
}

/*----------------------------------------------------------------*/

/*
 * Default cache size: available memory divided by the ratio.
 */
static unsigned long dm_bufio_default_cache_size;

/*
 * Total cache size set by the user.
 */
static unsigned long dm_bufio_cache_size;

/*
 * A copy of dm_bufio_cache_size because dm_bufio_cache_size can change
 * at any time.  If it disagrees, the user has changed cache size.
 */
static unsigned long dm_bufio_cache_size_latch;

static DEFINE_SPINLOCK(global_spinlock);

static unsigned int dm_bufio_max_age; /* No longer does anything */

static unsigned long dm_bufio_retain_bytes = DM_BUFIO_DEFAULT_RETAIN_BYTES;

static unsigned long dm_bufio_peak_allocated;
static unsigned long dm_bufio_allocated_kmem_cache;
static unsigned long dm_bufio_allocated_kmalloc;
static unsigned long dm_bufio_allocated_get_free_pages;
static unsigned long dm_bufio_allocated_vmalloc;
static unsigned long dm_bufio_current_allocated;

/*----------------------------------------------------------------*/

/*
 * The current number of clients.
 */
static int dm_bufio_client_count;

/*
 * The list of all clients.
 */
static LIST_HEAD(dm_bufio_all_clients);

/*
 * This mutex protects dm_bufio_cache_size_latch and dm_bufio_client_count
 */
static DEFINE_MUTEX(dm_bufio_clients_lock);

static struct workqueue_struct *dm_bufio_wq;
static struct work_struct dm_bufio_replacement_work;


#ifdef CONFIG_DM_DEBUG_BLOCK_STACK_TRACING
static void buffer_record_stack(struct dm_buffer *b)
{
	b->stack_len = stack_trace_save(b->stack_entries, MAX_STACK, 2);
}
#endif

/*----------------------------------------------------------------*/

static void adjust_total_allocated(struct dm_buffer *b, bool unlink)
{
	unsigned char data_mode;
	long diff;

	static unsigned long * const class_ptr[DATA_MODE_LIMIT] = {
		&dm_bufio_allocated_kmem_cache,
		&dm_bufio_allocated_kmalloc,
		&dm_bufio_allocated_get_free_pages,
		&dm_bufio_allocated_vmalloc,
	};

	data_mode = b->data_mode;
	diff = (long)b->c->block_size;
	if (unlink)
		diff = -diff;

	spin_lock(&global_spinlock);

	*class_ptr[data_mode] += diff;

	dm_bufio_current_allocated += diff;

	if (dm_bufio_current_allocated > dm_bufio_peak_allocated)
		dm_bufio_peak_allocated = dm_bufio_current_allocated;

	if (!unlink) {
		if (dm_bufio_current_allocated > dm_bufio_cache_size)
			queue_work(dm_bufio_wq, &dm_bufio_replacement_work);
	}

	spin_unlock(&global_spinlock);
}

/*
 * Change the number of clients and recalculate per-client limit.
 */
static void __cache_size_refresh(void)
{
	if (WARN_ON(!mutex_is_locked(&dm_bufio_clients_lock)))
		return;
	if (WARN_ON(dm_bufio_client_count < 0))
		return;

	dm_bufio_cache_size_latch = READ_ONCE(dm_bufio_cache_size);

	/*
	 * Use default if set to 0 and report the actual cache size used.
	 */
	if (!dm_bufio_cache_size_latch) {
		(void)cmpxchg(&dm_bufio_cache_size, 0,
			      dm_bufio_default_cache_size);
		dm_bufio_cache_size_latch = dm_bufio_default_cache_size;
	}
}

/*
 * Allocating buffer data.
 *
 * Small buffers are allocated with kmem_cache, to use space optimally.
 *
 * For large buffers, we choose between get_free_pages and vmalloc.
 * Each has advantages and disadvantages.
 *
 * __get_free_pages can randomly fail if the memory is fragmented.
 * __vmalloc won't randomly fail, but vmalloc space is limited (it may be
 * as low as 128M) so using it for caching is not appropriate.
 *
 * If the allocation may fail we use __get_free_pages. Memory fragmentation
 * won't have a fatal effect here, but it just causes flushes of some other
 * buffers and more I/O will be performed. Don't use __get_free_pages if it
 * always fails (i.e. order > MAX_PAGE_ORDER).
 *
 * If the allocation shouldn't fail we use __vmalloc. This is only for the
 * initial reserve allocation, so there's no risk of wasting all vmalloc
 * space.
 */
static void *alloc_buffer_data(struct dm_bufio_client *c, gfp_t gfp_mask,
			       unsigned char *data_mode)
{
	if (unlikely(c->slab_cache != NULL)) {
		*data_mode = DATA_MODE_SLAB;
		return kmem_cache_alloc(c->slab_cache, gfp_mask);
	}

	if (unlikely(c->block_size < PAGE_SIZE)) {
		*data_mode = DATA_MODE_KMALLOC;
		return kmalloc(c->block_size, gfp_mask | __GFP_RECLAIMABLE);
	}

	if (c->block_size <= KMALLOC_MAX_SIZE &&
	    gfp_mask & __GFP_NORETRY) {
		*data_mode = DATA_MODE_GET_FREE_PAGES;
		return (void *)__get_free_pages(gfp_mask,
						c->sectors_per_block_bits - (PAGE_SHIFT - SECTOR_SHIFT));
	}

	*data_mode = DATA_MODE_VMALLOC;

	return __vmalloc(c->block_size, gfp_mask);
}

/*
 * Free buffer's data.
 */
static void free_buffer_data(struct dm_bufio_client *c,
			     void *data, unsigned char data_mode)
{
	switch (data_mode) {
	case DATA_MODE_SLAB:
		kmem_cache_free(c->slab_cache, data);
		break;

	case DATA_MODE_KMALLOC:
		kfree(data);
		break;

	case DATA_MODE_GET_FREE_PAGES:
		free_pages((unsigned long)data,
			   c->sectors_per_block_bits - (PAGE_SHIFT - SECTOR_SHIFT));
		break;

	case DATA_MODE_VMALLOC:
		vfree(data);
		break;

	default:
		DMCRIT("dm_bufio_free_buffer_data: bad data mode: %d",
		       data_mode);
		BUG();
	}
}

/*
 * Allocate buffer and its data.
 */
static struct dm_buffer *alloc_buffer(struct dm_bufio_client *c, gfp_t gfp_mask)
{
	struct dm_buffer *b = kmem_cache_alloc(c->slab_buffer, gfp_mask);

	if (!b)
		return NULL;

	b->c = c;

	b->data = alloc_buffer_data(c, gfp_mask, &b->data_mode);
	if (!b->data) {
		kmem_cache_free(c->slab_buffer, b);
		return NULL;
	}
	adjust_total_allocated(b, false);

#ifdef CONFIG_DM_DEBUG_BLOCK_STACK_TRACING
	b->stack_len = 0;
#endif
	return b;
}

/*
 * Free buffer and its data.
 */
static void free_buffer(struct dm_buffer *b)
{
	struct dm_bufio_client *c = b->c;

	adjust_total_allocated(b, true);
	free_buffer_data(c, b->data, b->data_mode);
	kmem_cache_free(c->slab_buffer, b);
}

/*
 *--------------------------------------------------------------------------
 * Submit I/O on the buffer.
 *
 * Bio interface is faster but it has some problems:
 *	the vector list is limited (increasing this limit increases
 *	memory-consumption per buffer, so it is not viable);
 *
 *	the memory must be direct-mapped, not vmalloced;
 *
 * If the buffer is small enough (up to DM_BUFIO_INLINE_VECS pages) and
 * it is not vmalloced, try using the bio interface.
 *
 * If the buffer is big, if it is vmalloced or if the underlying device
 * rejects the bio because it is too large, use dm-io layer to do the I/O.
 * The dm-io layer splits the I/O into multiple requests, avoiding the above
 * shortcomings.
 *--------------------------------------------------------------------------
 */

/*
 * dm-io completion routine. It just calls b->bio.bi_end_io, pretending
 * that the request was handled directly with bio interface.
 */
static void dmio_complete(unsigned long error, void *context)
{
	struct dm_buffer *b = context;

	b->end_io(b, unlikely(error != 0) ? BLK_STS_IOERR : 0);
}

static void use_dmio(struct dm_buffer *b, enum req_op op, sector_t sector,
		     unsigned int n_sectors, unsigned int offset,
		     unsigned short ioprio)
{
	int r;
	struct dm_io_request io_req = {
		.bi_opf = op,
		.notify.fn = dmio_complete,
		.notify.context = b,
		.client = b->c->dm_io,
	};
	struct dm_io_region region = {
		.bdev = b->c->bdev,
		.sector = sector,
		.count = n_sectors,
	};

	if (b->data_mode != DATA_MODE_VMALLOC) {
		io_req.mem.type = DM_IO_KMEM;
		io_req.mem.ptr.addr = (char *)b->data + offset;
	} else {
		io_req.mem.type = DM_IO_VMA;
		io_req.mem.ptr.vma = (char *)b->data + offset;
	}

	r = dm_io(&io_req, 1, &region, NULL, ioprio);
	if (unlikely(r))
		b->end_io(b, errno_to_blk_status(r));
}

static void bio_complete(struct bio *bio)
{
	struct dm_buffer *b = bio->bi_private;
	blk_status_t status = bio->bi_status;

	bio_uninit(bio);
	kfree(bio);
	b->end_io(b, status);
}

static void use_bio(struct dm_buffer *b, enum req_op op, sector_t sector,
		    unsigned int n_sectors, unsigned int offset,
		    unsigned short ioprio)
{
	struct bio *bio;
	char *ptr;
	unsigned int len;

	bio = bio_kmalloc(1, GFP_NOWAIT);
	if (!bio) {
		use_dmio(b, op, sector, n_sectors, offset, ioprio);
		return;
	}
	bio_init_inline(bio, b->c->bdev, 1, op);
	bio->bi_iter.bi_sector = sector;
	bio->bi_end_io = bio_complete;
	bio->bi_private = b;
	bio->bi_ioprio = ioprio;

	ptr = (char *)b->data + offset;
	len = n_sectors << SECTOR_SHIFT;

	bio_add_virt_nofail(bio, ptr, len);

	submit_bio(bio);
}

static inline sector_t block_to_sector(struct dm_bufio_client *c, sector_t block)
{
	sector_t sector;

	if (likely(c->sectors_per_block_bits >= 0))
		sector = block << c->sectors_per_block_bits;
	else
		sector = block * (c->block_size >> SECTOR_SHIFT);
	sector += c->start;

	return sector;
}

static void submit_io(struct dm_buffer *b, enum req_op op, unsigned short ioprio,
		      void (*end_io)(struct dm_buffer *, blk_status_t))
{
	unsigned int n_sectors;
	sector_t sector;
	unsigned int offset, end;

	b->end_io = end_io;

	sector = block_to_sector(b->c, b->block);

	if (op != REQ_OP_WRITE) {
		n_sectors = b->c->block_size >> SECTOR_SHIFT;
		offset = 0;
	} else {
		if (b->c->write_callback)
			b->c->write_callback(b);
		offset = b->write_start;
		end = b->write_end;
		offset &= -DM_BUFIO_WRITE_ALIGN;
		end += DM_BUFIO_WRITE_ALIGN - 1;
		end &= -DM_BUFIO_WRITE_ALIGN;
		if (unlikely(end > b->c->block_size))
			end = b->c->block_size;

		sector += offset >> SECTOR_SHIFT;
		n_sectors = (end - offset) >> SECTOR_SHIFT;
	}

	if (b->data_mode != DATA_MODE_VMALLOC)
		use_bio(b, op, sector, n_sectors, offset, ioprio);
	else
		use_dmio(b, op, sector, n_sectors, offset, ioprio);
}

/*
 *--------------------------------------------------------------
 * Writing dirty buffers
 *--------------------------------------------------------------
 */

/*
 * The endio routine for write.
 *
 * Set the error, clear B_WRITING bit and wake anyone who was waiting on
 * it.
 */
static void write_endio(struct dm_buffer *b, blk_status_t status)
{
	b->write_error = status;
	if (unlikely(status)) {
		struct dm_bufio_client *c = b->c;

		(void)cmpxchg(&c->async_write_error, 0,
				blk_status_to_errno(status));
	}

	BUG_ON(!test_bit(B_WRITING, &b->state));

	smp_mb__before_atomic();
	clear_bit(B_WRITING, &b->state);
	smp_mb__after_atomic();

	wake_up_bit(&b->state, B_WRITING);
}

/*
 * Initiate a write on a dirty buffer, but don't wait for it.
 *
 * - If the buffer is not dirty, exit.
 * - If there some previous write going on, wait for it to finish (we can't
 *   have two writes on the same buffer simultaneously).
 * - Submit our write and don't wait on it. We set B_WRITING indicating
 *   that there is a write in progress.
 */
static void __write_dirty_buffer(struct dm_buffer *b,
				 struct list_head *write_list)
{
	if (!test_bit(B_DIRTY, &b->state))
		return;

	clear_bit(B_DIRTY, &b->state);
	wait_on_bit_lock_io(&b->state, B_WRITING, TASK_UNINTERRUPTIBLE);

	b->write_start = b->dirty_start;
	b->write_end = b->dirty_end;

	if (!write_list)
		submit_io(b, REQ_OP_WRITE, IOPRIO_DEFAULT, write_endio);
	else
		list_add_tail(&b->write_list, write_list);
}

static void __flush_write_list(struct list_head *write_list)
{
	struct blk_plug plug;

	blk_start_plug(&plug);
	while (!list_empty(write_list)) {
		struct dm_buffer *b =
			list_entry(write_list->next, struct dm_buffer, write_list);
		list_del(&b->write_list);
		submit_io(b, REQ_OP_WRITE, IOPRIO_DEFAULT, write_endio);
		cond_resched();
	}
	blk_finish_plug(&plug);
}

/*
 * Wait until any activity on the buffer finishes.  Possibly write the
 * buffer if it is dirty.  When this function finishes, there is no I/O
 * running on the buffer and the buffer is not dirty.
 */
static void __make_buffer_clean(struct dm_buffer *b)
{
	BUG_ON(atomic_read(&b->hold_count));

	/* smp_load_acquire() pairs with read_endio()'s smp_mb__before_atomic() */
	if (!smp_load_acquire(&b->state))	/* fast case */
		return;

	wait_on_bit_io(&b->state, B_READING, TASK_UNINTERRUPTIBLE);
	__write_dirty_buffer(b, NULL);
	wait_on_bit_io(&b->state, B_WRITING, TASK_UNINTERRUPTIBLE);
}

static enum evict_result is_clean(struct dm_buffer *b, void *context)
{
	struct dm_bufio_client *c = context;

	/* These should never happen */
	if (WARN_ON_ONCE(test_bit(B_WRITING, &b->state)))
		return ER_DONT_EVICT;
	if (WARN_ON_ONCE(test_bit(B_DIRTY, &b->state)))
		return ER_DONT_EVICT;
	if (WARN_ON_ONCE(b->list_mode != LIST_CLEAN))
		return ER_DONT_EVICT;

	if (static_branch_unlikely(&no_sleep_enabled) && c->no_sleep &&
	    unlikely(test_bit(B_READING, &b->state)))
		return ER_DONT_EVICT;

	return ER_EVICT;
}

static enum evict_result is_dirty(struct dm_buffer *b, void *context)
{
	/* These should never happen */
	if (WARN_ON_ONCE(test_bit(B_READING, &b->state)))
		return ER_DONT_EVICT;
	if (WARN_ON_ONCE(b->list_mode != LIST_DIRTY))
		return ER_DONT_EVICT;

	return ER_EVICT;
}

/*
 * Find some buffer that is not held by anybody, clean it, unlink it and
 * return it.
 */
static struct dm_buffer *__get_unclaimed_buffer(struct dm_bufio_client *c)
{
	struct dm_buffer *b;

	b = cache_evict(&c->cache, LIST_CLEAN, is_clean, c);
	if (b) {
		/* this also waits for pending reads */
		__make_buffer_clean(b);
		return b;
	}

	if (static_branch_unlikely(&no_sleep_enabled) && c->no_sleep)
		return NULL;

	b = cache_evict(&c->cache, LIST_DIRTY, is_dirty, NULL);
	if (b) {
		__make_buffer_clean(b);
		return b;
	}

	return NULL;
}

/*
 * Wait until some other threads free some buffer or release hold count on
 * some buffer.
 *
 * This function is entered with c->lock held, drops it and regains it
 * before exiting.
 */
static void __wait_for_free_buffer(struct dm_bufio_client *c)
{
	DECLARE_WAITQUEUE(wait, current);

	add_wait_queue(&c->free_buffer_wait, &wait);
	set_current_state(TASK_UNINTERRUPTIBLE);
	dm_bufio_unlock(c);

	/*
	 * It's possible to miss a wake up event since we don't always
	 * hold c->lock when wake_up is called.  So we have a timeout here,
	 * just in case.
	 */
	io_schedule_timeout(5 * HZ);

	remove_wait_queue(&c->free_buffer_wait, &wait);

	dm_bufio_lock(c);
}

enum new_flag {
	NF_FRESH = 0,
	NF_READ = 1,
	NF_GET = 2,
	NF_PREFETCH = 3
};

/*
 * Allocate a new buffer. If the allocation is not possible, wait until
 * some other thread frees a buffer.
 *
 * May drop the lock and regain it.
 */
static struct dm_buffer *__alloc_buffer_wait_no_callback(struct dm_bufio_client *c, enum new_flag nf)
{
	struct dm_buffer *b;
	bool tried_noio_alloc = false;

	/*
	 * dm-bufio is resistant to allocation failures (it just keeps
	 * one buffer reserved in cases all the allocations fail).
	 * So set flags to not try too hard:
	 *	GFP_NOWAIT: don't wait and don't print a warning in case of
	 *		    failure; if we need to sleep we'll release our mutex
	 *		    and wait ourselves.
	 *	__GFP_NORETRY: don't retry and rather return failure
	 *	__GFP_NOMEMALLOC: don't use emergency reserves
	 *
	 * For debugging, if we set the cache size to 1, no new buffers will
	 * be allocated.
	 */
	while (1) {
		if (dm_bufio_cache_size_latch != 1) {
			b = alloc_buffer(c, GFP_NOWAIT | __GFP_NORETRY | __GFP_NOMEMALLOC);
			if (b)
				return b;
		}

		if (nf == NF_PREFETCH)
			return NULL;

		if (dm_bufio_cache_size_latch != 1 && !tried_noio_alloc) {
			dm_bufio_unlock(c);
			b = alloc_buffer(c, GFP_NOIO | __GFP_NORETRY | __GFP_NOMEMALLOC | __GFP_NOWARN);
			dm_bufio_lock(c);
			if (b)
				return b;
			tried_noio_alloc = true;
		}

		if (!list_empty(&c->reserved_buffers)) {
			b = list_to_buffer(c->reserved_buffers.next);
			list_del(&b->lru.list);
			c->need_reserved_buffers++;

			return b;
		}

		b = __get_unclaimed_buffer(c);
		if (b)
			return b;

		__wait_for_free_buffer(c);
	}
}

static struct dm_buffer *__alloc_buffer_wait(struct dm_bufio_client *c, enum new_flag nf)
{
	struct dm_buffer *b = __alloc_buffer_wait_no_callback(c, nf);

	if (!b)
		return NULL;

	if (c->alloc_callback)
		c->alloc_callback(b);

	return b;
}

/*
 * Free a buffer and wake other threads waiting for free buffers.
 */
static void __free_buffer_wake(struct dm_buffer *b)
{
	struct dm_bufio_client *c = b->c;

	b->block = -1;
	if (!c->need_reserved_buffers)
		free_buffer(b);
	else {
		list_add(&b->lru.list, &c->reserved_buffers);
		c->need_reserved_buffers--;
	}

	/*
	 * We hold the bufio lock here, so no one can add entries to the
	 * wait queue anyway.
	 */
	if (unlikely(waitqueue_active(&c->free_buffer_wait)))
		wake_up(&c->free_buffer_wait);
}

static enum evict_result cleaned(struct dm_buffer *b, void *context)
{
	if (WARN_ON_ONCE(test_bit(B_READING, &b->state)))
		return ER_DONT_EVICT; /* should never happen */

	if (test_bit(B_DIRTY, &b->state) || test_bit(B_WRITING, &b->state))
		return ER_DONT_EVICT;
	else
		return ER_EVICT;
}

static void __move_clean_buffers(struct dm_bufio_client *c)
{
	cache_mark_many(&c->cache, LIST_DIRTY, LIST_CLEAN, cleaned, NULL);
}

struct write_context {
	int no_wait;
	struct list_head *write_list;
};

static enum it_action write_one(struct dm_buffer *b, void *context)
{
	struct write_context *wc = context;

	if (wc->no_wait && test_bit(B_WRITING, &b->state))
		return IT_COMPLETE;

	__write_dirty_buffer(b, wc->write_list);
	return IT_NEXT;
}

static void __write_dirty_buffers_async(struct dm_bufio_client *c, int no_wait,
					struct list_head *write_list)
{
	struct write_context wc = {.no_wait = no_wait, .write_list = write_list};

	__move_clean_buffers(c);
	cache_iterate(&c->cache, LIST_DIRTY, write_one, &wc);
}

/*
 * Check if we're over watermark.
 * If we are over threshold_buffers, start freeing buffers.
 * If we're over "limit_buffers", block until we get under the limit.
 */
static void __check_watermark(struct dm_bufio_client *c,
			      struct list_head *write_list)
{
	if (cache_count(&c->cache, LIST_DIRTY) >
	    cache_count(&c->cache, LIST_CLEAN) * DM_BUFIO_WRITEBACK_RATIO)
		__write_dirty_buffers_async(c, 1, write_list);
}

/*
 *--------------------------------------------------------------
 * Getting a buffer
 *--------------------------------------------------------------
 */

static void cache_put_and_wake(struct dm_bufio_client *c, struct dm_buffer *b)
{
	/*
	 * Relying on waitqueue_active() is racey, but we sleep
	 * with schedule_timeout anyway.
	 */
	if (cache_put(&c->cache, b) &&
	    unlikely(waitqueue_active(&c->free_buffer_wait)))
		wake_up(&c->free_buffer_wait);
}

/*
 * This assumes you have already checked the cache to see if the buffer
 * is already present (it will recheck after dropping the lock for allocation).
 */
static struct dm_buffer *__bufio_new(struct dm_bufio_client *c, sector_t block,
				     enum new_flag nf, int *need_submit,
				     struct list_head *write_list)
{
	struct dm_buffer *b, *new_b = NULL;

	*need_submit = 0;

	/* This can't be called with NF_GET */
	if (WARN_ON_ONCE(nf == NF_GET))
		return NULL;

	new_b = __alloc_buffer_wait(c, nf);
	if (!new_b)
		return NULL;

	/*
	 * We've had a period where the mutex was unlocked, so need to
	 * recheck the buffer tree.
	 */
	b = cache_get(&c->cache, block);
	if (b) {
		__free_buffer_wake(new_b);
		goto found_buffer;
	}

	__check_watermark(c, write_list);

	b = new_b;
	atomic_set(&b->hold_count, 1);
	WRITE_ONCE(b->last_accessed, jiffies);
	b->block = block;
	b->read_error = 0;
	b->write_error = 0;
	b->list_mode = LIST_CLEAN;

	if (nf == NF_FRESH)
		b->state = 0;
	else {
		b->state = 1 << B_READING;
		*need_submit = 1;
	}

	/*
	 * We mustn't insert into the cache until the B_READING state
	 * is set.  Otherwise another thread could get it and use
	 * it before it had been read.
	 */
	cache_insert(&c->cache, b);

	return b;

found_buffer:
	if (nf == NF_PREFETCH) {
		cache_put_and_wake(c, b);
		return NULL;
	}

	/*
	 * Note: it is essential that we don't wait for the buffer to be
	 * read if dm_bufio_get function is used. Both dm_bufio_get and
	 * dm_bufio_prefetch can be used in the driver request routine.
	 * If the user called both dm_bufio_prefetch and dm_bufio_get on
	 * the same buffer, it would deadlock if we waited.
	 */
	if (nf == NF_GET && unlikely(test_bit_acquire(B_READING, &b->state))) {
		cache_put_and_wake(c, b);
		return NULL;
	}

	return b;
}

/*
 * The endio routine for reading: set the error, clear the bit and wake up
 * anyone waiting on the buffer.
 */
static void read_endio(struct dm_buffer *b, blk_status_t status)
{
	b->read_error = status;

	BUG_ON(!test_bit(B_READING, &b->state));

	smp_mb__before_atomic();
	clear_bit(B_READING, &b->state);
	smp_mb__after_atomic();

	wake_up_bit(&b->state, B_READING);
}

/*
 * A common routine for dm_bufio_new and dm_bufio_read.  Operation of these
 * functions is similar except that dm_bufio_new doesn't read the
 * buffer from the disk (assuming that the caller overwrites all the data
 * and uses dm_bufio_mark_buffer_dirty to write new data back).
 */
static void *new_read(struct dm_bufio_client *c, sector_t block,
		      enum new_flag nf, struct dm_buffer **bp,
		      unsigned short ioprio)
{
	int need_submit = 0;
	struct dm_buffer *b;

	LIST_HEAD(write_list);

	*bp = NULL;

	/*
	 * Fast path, hopefully the block is already in the cache.  No need
	 * to get the client lock for this.
	 */
	b = cache_get(&c->cache, block);
	if (b) {
		if (nf == NF_PREFETCH) {
			cache_put_and_wake(c, b);
			return NULL;
		}

		/*
		 * Note: it is essential that we don't wait for the buffer to be
		 * read if dm_bufio_get function is used. Both dm_bufio_get and
		 * dm_bufio_prefetch can be used in the driver request routine.
		 * If the user called both dm_bufio_prefetch and dm_bufio_get on
		 * the same buffer, it would deadlock if we waited.
		 */
		if (nf == NF_GET && unlikely(test_bit_acquire(B_READING, &b->state))) {
			cache_put_and_wake(c, b);
			return NULL;
		}
	}

	if (!b) {
		if (nf == NF_GET)
			return NULL;

		dm_bufio_lock(c);
		b = __bufio_new(c, block, nf, &need_submit, &write_list);
		dm_bufio_unlock(c);
	}

#ifdef CONFIG_DM_DEBUG_BLOCK_STACK_TRACING
	if (b && (atomic_read(&b->hold_count) == 1))
		buffer_record_stack(b);
#endif

	__flush_write_list(&write_list);

	if (!b)
		return NULL;

	if (need_submit)
		submit_io(b, REQ_OP_READ, ioprio, read_endio);

	if (nf != NF_GET)	/* we already tested this condition above */
		wait_on_bit_io(&b->state, B_READING, TASK_UNINTERRUPTIBLE);

	if (b->read_error) {
		int error = blk_status_to_errno(b->read_error);

		dm_bufio_release(b);

		return ERR_PTR(error);
	}

	*bp = b;

	return b->data;
}

void *dm_bufio_get(struct dm_bufio_client *c, sector_t block,
		   struct dm_buffer **bp)
{
	return new_read(c, block, NF_GET, bp, IOPRIO_DEFAULT);
}
EXPORT_SYMBOL_GPL(dm_bufio_get);

static void *__dm_bufio_read(struct dm_bufio_client *c, sector_t block,
			struct dm_buffer **bp, unsigned short ioprio)
{
	if (WARN_ON_ONCE(dm_bufio_in_request()))
		return ERR_PTR(-EINVAL);

	return new_read(c, block, NF_READ, bp, ioprio);
}

void *dm_bufio_read(struct dm_bufio_client *c, sector_t block,
		    struct dm_buffer **bp)
{
	return __dm_bufio_read(c, block, bp, IOPRIO_DEFAULT);
}
EXPORT_SYMBOL_GPL(dm_bufio_read);

void *dm_bufio_read_with_ioprio(struct dm_bufio_client *c, sector_t block,
				struct dm_buffer **bp, unsigned short ioprio)
{
	return __dm_bufio_read(c, block, bp, ioprio);
}
EXPORT_SYMBOL_GPL(dm_bufio_read_with_ioprio);

void *dm_bufio_new(struct dm_bufio_client *c, sector_t block,
		   struct dm_buffer **bp)
{
	if (WARN_ON_ONCE(dm_bufio_in_request()))
		return ERR_PTR(-EINVAL);

	return new_read(c, block, NF_FRESH, bp, IOPRIO_DEFAULT);
}
EXPORT_SYMBOL_GPL(dm_bufio_new);

static void __dm_bufio_prefetch(struct dm_bufio_client *c,
			sector_t block, unsigned int n_blocks,
			unsigned short ioprio)
{
	struct blk_plug plug;

	LIST_HEAD(write_list);

	if (WARN_ON_ONCE(dm_bufio_in_request()))
		return; /* should never happen */

	blk_start_plug(&plug);

	for (; n_blocks--; block++) {
		int need_submit;
		struct dm_buffer *b;

		b = cache_get(&c->cache, block);
		if (b) {
			/* already in cache */
			cache_put_and_wake(c, b);
			continue;
		}

		dm_bufio_lock(c);
		b = __bufio_new(c, block, NF_PREFETCH, &need_submit,
				&write_list);
		if (unlikely(!list_empty(&write_list))) {
			dm_bufio_unlock(c);
			blk_finish_plug(&plug);
			__flush_write_list(&write_list);
			blk_start_plug(&plug);
			dm_bufio_lock(c);
		}
		if (unlikely(b != NULL)) {
			dm_bufio_unlock(c);

			if (need_submit)
				submit_io(b, REQ_OP_READ, ioprio, read_endio);
			dm_bufio_release(b);

			cond_resched();

			if (!n_blocks)
				goto flush_plug;
			dm_bufio_lock(c);
		}
		dm_bufio_unlock(c);
	}

flush_plug:
	blk_finish_plug(&plug);
}

void dm_bufio_prefetch(struct dm_bufio_client *c, sector_t block, unsigned int n_blocks)
{
	return __dm_bufio_prefetch(c, block, n_blocks, IOPRIO_DEFAULT);
}
EXPORT_SYMBOL_GPL(dm_bufio_prefetch);

void dm_bufio_prefetch_with_ioprio(struct dm_bufio_client *c, sector_t block,
				unsigned int n_blocks, unsigned short ioprio)
{
	return __dm_bufio_prefetch(c, block, n_blocks, ioprio);
}
EXPORT_SYMBOL_GPL(dm_bufio_prefetch_with_ioprio);

void dm_bufio_release(struct dm_buffer *b)
{
	struct dm_bufio_client *c = b->c;

	/*
	 * If there were errors on the buffer, and the buffer is not
	 * to be written, free the buffer. There is no point in caching
	 * invalid buffer.
	 */
	if ((b->read_error || b->write_error) &&
	    !test_bit_acquire(B_READING, &b->state) &&
	    !test_bit(B_WRITING, &b->state) &&
	    !test_bit(B_DIRTY, &b->state)) {
		dm_bufio_lock(c);

		/* cache remove can fail if there are other holders */
		if (cache_remove(&c->cache, b)) {
			__free_buffer_wake(b);
			dm_bufio_unlock(c);
			return;
		}

		dm_bufio_unlock(c);
	}

	cache_put_and_wake(c, b);
}
EXPORT_SYMBOL_GPL(dm_bufio_release);

void dm_bufio_mark_partial_buffer_dirty(struct dm_buffer *b,
					unsigned int start, unsigned int end)
{
	struct dm_bufio_client *c = b->c;

	BUG_ON(start >= end);
	BUG_ON(end > b->c->block_size);

	dm_bufio_lock(c);

	BUG_ON(test_bit(B_READING, &b->state));

	if (!test_and_set_bit(B_DIRTY, &b->state)) {
		b->dirty_start = start;
		b->dirty_end = end;
		cache_mark(&c->cache, b, LIST_DIRTY);
	} else {
		if (start < b->dirty_start)
			b->dirty_start = start;
		if (end > b->dirty_end)
			b->dirty_end = end;
	}

	dm_bufio_unlock(c);
}
EXPORT_SYMBOL_GPL(dm_bufio_mark_partial_buffer_dirty);

void dm_bufio_mark_buffer_dirty(struct dm_buffer *b)
{
	dm_bufio_mark_partial_buffer_dirty(b, 0, b->c->block_size);
}
EXPORT_SYMBOL_GPL(dm_bufio_mark_buffer_dirty);

void dm_bufio_write_dirty_buffers_async(struct dm_bufio_client *c)
{
	LIST_HEAD(write_list);

	if (WARN_ON_ONCE(dm_bufio_in_request()))
		return; /* should never happen */

	dm_bufio_lock(c);
	__write_dirty_buffers_async(c, 0, &write_list);
	dm_bufio_unlock(c);
	__flush_write_list(&write_list);
}
EXPORT_SYMBOL_GPL(dm_bufio_write_dirty_buffers_async);

/*
 * For performance, it is essential that the buffers are written asynchronously
 * and simultaneously (so that the block layer can merge the writes) and then
 * waited upon.
 *
 * Finally, we flush hardware disk cache.
 */
static bool is_writing(struct lru_entry *e, void *context)
{
	struct dm_buffer *b = le_to_buffer(e);

	return test_bit(B_WRITING, &b->state);
}

int dm_bufio_write_dirty_buffers(struct dm_bufio_client *c)
{
	int a, f;
	unsigned long nr_buffers;
	struct lru_entry *e;
	struct lru_iter it;

	LIST_HEAD(write_list);

	dm_bufio_lock(c);
	__write_dirty_buffers_async(c, 0, &write_list);
	dm_bufio_unlock(c);
	__flush_write_list(&write_list);
	dm_bufio_lock(c);

	nr_buffers = cache_count(&c->cache, LIST_DIRTY);
	lru_iter_begin(&c->cache.lru[LIST_DIRTY], &it);
	while ((e = lru_iter_next(&it, is_writing, c))) {
		struct dm_buffer *b = le_to_buffer(e);
		__cache_inc_buffer(b);

		BUG_ON(test_bit(B_READING, &b->state));

		if (nr_buffers) {
			nr_buffers--;
			dm_bufio_unlock(c);
			wait_on_bit_io(&b->state, B_WRITING, TASK_UNINTERRUPTIBLE);
			dm_bufio_lock(c);
		} else {
			wait_on_bit_io(&b->state, B_WRITING, TASK_UNINTERRUPTIBLE);
		}

		if (!test_bit(B_DIRTY, &b->state) && !test_bit(B_WRITING, &b->state))
			cache_mark(&c->cache, b, LIST_CLEAN);

		cache_put_and_wake(c, b);

		cond_resched();
	}
	lru_iter_end(&it);

	wake_up(&c->free_buffer_wait);
	dm_bufio_unlock(c);

	a = xchg(&c->async_write_error, 0);
	f = dm_bufio_issue_flush(c);
	if (a)
		return a;

	return f;
}
EXPORT_SYMBOL_GPL(dm_bufio_write_dirty_buffers);

/*
 * Use dm-io to send an empty barrier to flush the device.
 */
int dm_bufio_issue_flush(struct dm_bufio_client *c)
{
	struct dm_io_request io_req = {
		.bi_opf = REQ_OP_WRITE | REQ_PREFLUSH | REQ_SYNC,
		.mem.type = DM_IO_KMEM,
		.mem.ptr.addr = NULL,
		.client = c->dm_io,
	};
	struct dm_io_region io_reg = {
		.bdev = c->bdev,
		.sector = 0,
		.count = 0,
	};

	if (WARN_ON_ONCE(dm_bufio_in_request()))
		return -EINVAL;

	return dm_io(&io_req, 1, &io_reg, NULL, IOPRIO_DEFAULT);
}
EXPORT_SYMBOL_GPL(dm_bufio_issue_flush);

/*
 * Use dm-io to send a discard request to flush the device.
 */
int dm_bufio_issue_discard(struct dm_bufio_client *c, sector_t block, sector_t count)
{
	struct dm_io_request io_req = {
		.bi_opf = REQ_OP_DISCARD | REQ_SYNC,
		.mem.type = DM_IO_KMEM,
		.mem.ptr.addr = NULL,
		.client = c->dm_io,
	};
	struct dm_io_region io_reg = {
		.bdev = c->bdev,
		.sector = block_to_sector(c, block),
		.count = block_to_sector(c, count),
	};

	if (WARN_ON_ONCE(dm_bufio_in_request()))
		return -EINVAL; /* discards are optional */

	return dm_io(&io_req, 1, &io_reg, NULL, IOPRIO_DEFAULT);
}
EXPORT_SYMBOL_GPL(dm_bufio_issue_discard);

static void forget_buffer(struct dm_bufio_client *c, sector_t block)
{
	struct dm_buffer *b;

	b = cache_get(&c->cache, block);
	if (b) {
		if (likely(!smp_load_acquire(&b->state))) {
			if (cache_remove(&c->cache, b))
				__free_buffer_wake(b);
			else
				cache_put_and_wake(c, b);
		} else {
			cache_put_and_wake(c, b);
		}
	}
}

/*
 * Free the given buffer.
 *
 * This is just a hint, if the buffer is in use or dirty, this function
 * does nothing.
 */
void dm_bufio_forget(struct dm_bufio_client *c, sector_t block)
{
	dm_bufio_lock(c);
	forget_buffer(c, block);
	dm_bufio_unlock(c);
}
EXPORT_SYMBOL_GPL(dm_bufio_forget);

static enum evict_result idle(struct dm_buffer *b, void *context)
{
	return b->state ? ER_DONT_EVICT : ER_EVICT;
}

void dm_bufio_forget_buffers(struct dm_bufio_client *c, sector_t block, sector_t n_blocks)
{
	dm_bufio_lock(c);
	cache_remove_range(&c->cache, block, block + n_blocks, idle, __free_buffer_wake);
	dm_bufio_unlock(c);
}
EXPORT_SYMBOL_GPL(dm_bufio_forget_buffers);

void dm_bufio_set_minimum_buffers(struct dm_bufio_client *c, unsigned int n)
{
	c->minimum_buffers = n;
}
EXPORT_SYMBOL_GPL(dm_bufio_set_minimum_buffers);

unsigned int dm_bufio_get_block_size(struct dm_bufio_client *c)
{
	return c->block_size;
}
EXPORT_SYMBOL_GPL(dm_bufio_get_block_size);

sector_t dm_bufio_get_device_size(struct dm_bufio_client *c)
{
	sector_t s = bdev_nr_sectors(c->bdev);

	if (s >= c->start)
		s -= c->start;
	else
		s = 0;
	if (likely(c->sectors_per_block_bits >= 0))
		s >>= c->sectors_per_block_bits;
	else
		sector_div(s, c->block_size >> SECTOR_SHIFT);
	return s;
}
EXPORT_SYMBOL_GPL(dm_bufio_get_device_size);

struct dm_io_client *dm_bufio_get_dm_io_client(struct dm_bufio_client *c)
{
	return c->dm_io;
}
EXPORT_SYMBOL_GPL(dm_bufio_get_dm_io_client);

sector_t dm_bufio_get_block_number(struct dm_buffer *b)
{
	return b->block;
}
EXPORT_SYMBOL_GPL(dm_bufio_get_block_number);

void *dm_bufio_get_block_data(struct dm_buffer *b)
{
	return b->data;
}
EXPORT_SYMBOL_GPL(dm_bufio_get_block_data);

void *dm_bufio_get_aux_data(struct dm_buffer *b)
{
	return b + 1;
}
EXPORT_SYMBOL_GPL(dm_bufio_get_aux_data);

struct dm_bufio_client *dm_bufio_get_client(struct dm_buffer *b)
{
	return b->c;
}
EXPORT_SYMBOL_GPL(dm_bufio_get_client);

static enum it_action warn_leak(struct dm_buffer *b, void *context)
{
	bool *warned = context;

	WARN_ON(!(*warned));
	*warned = true;
	DMERR("leaked buffer %llx, hold count %u, list %d",
	      (unsigned long long)b->block, atomic_read(&b->hold_count), b->list_mode);
#ifdef CONFIG_DM_DEBUG_BLOCK_STACK_TRACING
	stack_trace_print(b->stack_entries, b->stack_len, 1);
	/* mark unclaimed to avoid WARN_ON at end of drop_buffers() */
	atomic_set(&b->hold_count, 0);
#endif
	return IT_NEXT;
}

static void drop_buffers(struct dm_bufio_client *c)
{
	int i;
	struct dm_buffer *b;

	if (WARN_ON(dm_bufio_in_request()))
		return; /* should never happen */

	/*
	 * An optimization so that the buffers are not written one-by-one.
	 */
	dm_bufio_write_dirty_buffers_async(c);

	dm_bufio_lock(c);

	while ((b = __get_unclaimed_buffer(c)))
		__free_buffer_wake(b);

	for (i = 0; i < LIST_SIZE; i++) {
		bool warned = false;

		cache_iterate(&c->cache, i, warn_leak, &warned);
	}

#ifdef CONFIG_DM_DEBUG_BLOCK_STACK_TRACING
	while ((b = __get_unclaimed_buffer(c)))
		__free_buffer_wake(b);
#endif

	for (i = 0; i < LIST_SIZE; i++)
		WARN_ON(cache_count(&c->cache, i));

	dm_bufio_unlock(c);
}

static unsigned long get_retain_buffers(struct dm_bufio_client *c)
{
	unsigned long retain_bytes = READ_ONCE(dm_bufio_retain_bytes);

	if (likely(c->sectors_per_block_bits >= 0))
		retain_bytes >>= c->sectors_per_block_bits + SECTOR_SHIFT;
	else
		retain_bytes /= c->block_size;

	return retain_bytes;
}

static void __scan(struct dm_bufio_client *c)
{
	int l;
	struct dm_buffer *b;
	unsigned long freed = 0;
	unsigned long retain_target = get_retain_buffers(c);
	unsigned long count = cache_total(&c->cache);

	for (l = 0; l < LIST_SIZE; l++) {
		while (true) {
			if (count - freed <= retain_target)
				atomic_long_set(&c->need_shrink, 0);
			if (!atomic_long_read(&c->need_shrink))
				break;

			b = cache_evict(&c->cache, l,
					l == LIST_CLEAN ? is_clean : is_dirty, c);
			if (!b)
				break;

			__make_buffer_clean(b);
			__free_buffer_wake(b);

			atomic_long_dec(&c->need_shrink);
			freed++;

			if (unlikely(freed % SCAN_RESCHED_CYCLE == 0)) {
				dm_bufio_unlock(c);
				cond_resched();
				dm_bufio_lock(c);
			}
		}
	}
}

static void shrink_work(struct work_struct *w)
{
	struct dm_bufio_client *c = container_of(w, struct dm_bufio_client, shrink_work);

	dm_bufio_lock(c);
	__scan(c);
	dm_bufio_unlock(c);
}

static unsigned long dm_bufio_shrink_scan(struct shrinker *shrink, struct shrink_control *sc)
{
	struct dm_bufio_client *c;

	c = shrink->private_data;
	atomic_long_add(sc->nr_to_scan, &c->need_shrink);
	queue_work(dm_bufio_wq, &c->shrink_work);

	return sc->nr_to_scan;
}

static unsigned long dm_bufio_shrink_count(struct shrinker *shrink, struct shrink_control *sc)
{
	struct dm_bufio_client *c = shrink->private_data;
	unsigned long count = cache_total(&c->cache);
	unsigned long retain_target = get_retain_buffers(c);
	unsigned long queued_for_cleanup = atomic_long_read(&c->need_shrink);

	if (unlikely(count < retain_target))
		count = 0;
	else
		count -= retain_target;

	if (unlikely(count < queued_for_cleanup))
		count = 0;
	else
		count -= queued_for_cleanup;

	return count;
}

/*
 * Create the buffering interface
 */
struct dm_bufio_client *dm_bufio_client_create(struct block_device *bdev, unsigned int block_size,
					       unsigned int reserved_buffers, unsigned int aux_size,
					       void (*alloc_callback)(struct dm_buffer *),
					       void (*write_callback)(struct dm_buffer *),
					       unsigned int flags)
{
	int r;
	unsigned int num_locks;
	struct dm_bufio_client *c;
	char slab_name[64];
	static atomic_t seqno = ATOMIC_INIT(0);

	if (!block_size || block_size & ((1 << SECTOR_SHIFT) - 1)) {
		DMERR("%s: block size not specified or is not multiple of 512b", __func__);
		r = -EINVAL;
		goto bad_client;
	}

	num_locks = dm_num_hash_locks();
	c = kzalloc(sizeof(*c) + (num_locks * sizeof(struct buffer_tree)), GFP_KERNEL);
	if (!c) {
		r = -ENOMEM;
		goto bad_client;
	}
	cache_init(&c->cache, num_locks, (flags & DM_BUFIO_CLIENT_NO_SLEEP) != 0);

	c->bdev = bdev;
	c->block_size = block_size;
	if (is_power_of_2(block_size))
		c->sectors_per_block_bits = __ffs(block_size) - SECTOR_SHIFT;
	else
		c->sectors_per_block_bits = -1;

	c->alloc_callback = alloc_callback;
	c->write_callback = write_callback;

	if (flags & DM_BUFIO_CLIENT_NO_SLEEP) {
		c->no_sleep = true;
		static_branch_inc(&no_sleep_enabled);
	}

	mutex_init(&c->lock);
	spin_lock_init(&c->spinlock);
	INIT_LIST_HEAD(&c->reserved_buffers);
	c->need_reserved_buffers = reserved_buffers;

	dm_bufio_set_minimum_buffers(c, DM_BUFIO_MIN_BUFFERS);

	init_waitqueue_head(&c->free_buffer_wait);
	c->async_write_error = 0;

	c->dm_io = dm_io_client_create();
	if (IS_ERR(c->dm_io)) {
		r = PTR_ERR(c->dm_io);
		goto bad_dm_io;
	}

	if (block_size <= KMALLOC_MAX_SIZE && !is_power_of_2(block_size)) {
		unsigned int align = min(1U << __ffs(block_size), (unsigned int)PAGE_SIZE);

		snprintf(slab_name, sizeof(slab_name), "dm_bufio_cache-%u-%u",
					block_size, atomic_inc_return(&seqno));
		c->slab_cache = kmem_cache_create(slab_name, block_size, align,
						  SLAB_RECLAIM_ACCOUNT, NULL);
		if (!c->slab_cache) {
			r = -ENOMEM;
			goto bad;
		}
	}
	if (aux_size)
		snprintf(slab_name, sizeof(slab_name), "dm_bufio_buffer-%u-%u",
					aux_size, atomic_inc_return(&seqno));
	else
		snprintf(slab_name, sizeof(slab_name), "dm_bufio_buffer-%u",
					atomic_inc_return(&seqno));
	c->slab_buffer = kmem_cache_create(slab_name, sizeof(struct dm_buffer) + aux_size,
					   0, SLAB_RECLAIM_ACCOUNT, NULL);
	if (!c->slab_buffer) {
		r = -ENOMEM;
		goto bad;
	}

	while (c->need_reserved_buffers) {
		struct dm_buffer *b = alloc_buffer(c, GFP_KERNEL);

		if (!b) {
			r = -ENOMEM;
			goto bad;
		}
		__free_buffer_wake(b);
	}

	INIT_WORK(&c->shrink_work, shrink_work);
	atomic_long_set(&c->need_shrink, 0);

	c->shrinker = shrinker_alloc(0, "dm-bufio:(%u:%u)",
				     MAJOR(bdev->bd_dev), MINOR(bdev->bd_dev));
	if (!c->shrinker) {
		r = -ENOMEM;
		goto bad;
	}

	c->shrinker->count_objects = dm_bufio_shrink_count;
	c->shrinker->scan_objects = dm_bufio_shrink_scan;
	c->shrinker->seeks = 1;
	c->shrinker->batch = 0;
	c->shrinker->private_data = c;

	shrinker_register(c->shrinker);

	mutex_lock(&dm_bufio_clients_lock);
	dm_bufio_client_count++;
	list_add(&c->client_list, &dm_bufio_all_clients);
	__cache_size_refresh();
	mutex_unlock(&dm_bufio_clients_lock);

	return c;

bad:
	while (!list_empty(&c->reserved_buffers)) {
		struct dm_buffer *b = list_to_buffer(c->reserved_buffers.next);

		list_del(&b->lru.list);
		free_buffer(b);
	}
	kmem_cache_destroy(c->slab_cache);
	kmem_cache_destroy(c->slab_buffer);
	dm_io_client_destroy(c->dm_io);
bad_dm_io:
	mutex_destroy(&c->lock);
	if (c->no_sleep)
		static_branch_dec(&no_sleep_enabled);
	kfree(c);
bad_client:
	return ERR_PTR(r);
}
EXPORT_SYMBOL_GPL(dm_bufio_client_create);

/*
 * Free the buffering interface.
 * It is required that there are no references on any buffers.
 */
void dm_bufio_client_destroy(struct dm_bufio_client *c)
{
	unsigned int i;

	drop_buffers(c);

	shrinker_free(c->shrinker);
	flush_work(&c->shrink_work);

	mutex_lock(&dm_bufio_clients_lock);

	list_del(&c->client_list);
	dm_bufio_client_count--;
	__cache_size_refresh();

	mutex_unlock(&dm_bufio_clients_lock);

	WARN_ON(c->need_reserved_buffers);

	while (!list_empty(&c->reserved_buffers)) {
		struct dm_buffer *b = list_to_buffer(c->reserved_buffers.next);

		list_del(&b->lru.list);
		free_buffer(b);
	}

	for (i = 0; i < LIST_SIZE; i++)
		if (cache_count(&c->cache, i))
			DMERR("leaked buffer count %d: %lu", i, cache_count(&c->cache, i));

	for (i = 0; i < LIST_SIZE; i++)
		WARN_ON(cache_count(&c->cache, i));

	cache_destroy(&c->cache);
	kmem_cache_destroy(c->slab_cache);
	kmem_cache_destroy(c->slab_buffer);
	dm_io_client_destroy(c->dm_io);
	mutex_destroy(&c->lock);
	if (c->no_sleep)
		static_branch_dec(&no_sleep_enabled);
	kfree(c);
}
EXPORT_SYMBOL_GPL(dm_bufio_client_destroy);

void dm_bufio_client_reset(struct dm_bufio_client *c)
{
	drop_buffers(c);
	flush_work(&c->shrink_work);
}
EXPORT_SYMBOL_GPL(dm_bufio_client_reset);

void dm_bufio_set_sector_offset(struct dm_bufio_client *c, sector_t start)
{
	c->start = start;
}
EXPORT_SYMBOL_GPL(dm_bufio_set_sector_offset);

/*--------------------------------------------------------------*/

/*
 * Global cleanup tries to evict the oldest buffers from across _all_
 * the clients.  It does this by repeatedly evicting a few buffers from
 * the client that holds the oldest buffer.  It's approximate, but hopefully
 * good enough.
 */
static struct dm_bufio_client *__pop_client(void)
{
	struct list_head *h;

	if (list_empty(&dm_bufio_all_clients))
		return NULL;

	h = dm_bufio_all_clients.next;
	list_del(h);
	return container_of(h, struct dm_bufio_client, client_list);
}

/*
 * Inserts the client in the global client list based on its
 * 'oldest_buffer' field.
 */
static void __insert_client(struct dm_bufio_client *new_client)
{
	struct dm_bufio_client *c;
	struct list_head *h = dm_bufio_all_clients.next;

	while (h != &dm_bufio_all_clients) {
		c = container_of(h, struct dm_bufio_client, client_list);
		if (time_after_eq(c->oldest_buffer, new_client->oldest_buffer))
			break;
		h = h->next;
	}

	list_add_tail(&new_client->client_list, h);
}

static enum evict_result select_for_evict(struct dm_buffer *b, void *context)
{
	/* In no-sleep mode, we cannot wait on IO. */
	if (static_branch_unlikely(&no_sleep_enabled) && b->c->no_sleep) {
		if (test_bit_acquire(B_READING, &b->state) ||
		    test_bit(B_WRITING, &b->state) ||
		    test_bit(B_DIRTY, &b->state))
			return ER_DONT_EVICT;
	}
	return ER_EVICT;
}

static unsigned long __evict_a_few(unsigned long nr_buffers)
{
	struct dm_bufio_client *c;
	unsigned long oldest_buffer = jiffies;
	unsigned long last_accessed;
	unsigned long count;
	struct dm_buffer *b;

	c = __pop_client();
	if (!c)
		return 0;

	dm_bufio_lock(c);

	for (count = 0; count < nr_buffers; count++) {
		b = cache_evict(&c->cache, LIST_CLEAN, select_for_evict, NULL);
		if (!b)
			break;

		last_accessed = READ_ONCE(b->last_accessed);
		if (time_after_eq(oldest_buffer, last_accessed))
			oldest_buffer = last_accessed;

		__make_buffer_clean(b);
		__free_buffer_wake(b);

		if (need_resched()) {
			dm_bufio_unlock(c);
			cond_resched();
			dm_bufio_lock(c);
		}
	}

	dm_bufio_unlock(c);

	if (count)
		c->oldest_buffer = oldest_buffer;
	__insert_client(c);

	return count;
}

static void check_watermarks(void)
{
	LIST_HEAD(write_list);
	struct dm_bufio_client *c;

	mutex_lock(&dm_bufio_clients_lock);
	list_for_each_entry(c, &dm_bufio_all_clients, client_list) {
		dm_bufio_lock(c);
		__check_watermark(c, &write_list);
		dm_bufio_unlock(c);
	}
	mutex_unlock(&dm_bufio_clients_lock);

	__flush_write_list(&write_list);
}

static void evict_old(void)
{
	unsigned long threshold = dm_bufio_cache_size -
		dm_bufio_cache_size / DM_BUFIO_LOW_WATERMARK_RATIO;

	mutex_lock(&dm_bufio_clients_lock);
	while (dm_bufio_current_allocated > threshold) {
		if (!__evict_a_few(64))
			break;
		cond_resched();
	}
	mutex_unlock(&dm_bufio_clients_lock);
}

static void do_global_cleanup(struct work_struct *w)
{
	check_watermarks();
	evict_old();
}

/*
 *--------------------------------------------------------------
 * Module setup
 *--------------------------------------------------------------
 */

/*
 * This is called only once for the whole dm_bufio module.
 * It initializes memory limit.
 */
static int __init dm_bufio_init(void)
{
	__u64 mem;

	dm_bufio_allocated_kmem_cache = 0;
	dm_bufio_allocated_kmalloc = 0;
	dm_bufio_allocated_get_free_pages = 0;
	dm_bufio_allocated_vmalloc = 0;
	dm_bufio_current_allocated = 0;

	mem = (__u64)mult_frac(totalram_pages() - totalhigh_pages(),
			       DM_BUFIO_MEMORY_PERCENT, 100) << PAGE_SHIFT;

	if (mem > ULONG_MAX)
		mem = ULONG_MAX;

#ifdef CONFIG_MMU
	if (mem > mult_frac(VMALLOC_TOTAL, DM_BUFIO_VMALLOC_PERCENT, 100))
		mem = mult_frac(VMALLOC_TOTAL, DM_BUFIO_VMALLOC_PERCENT, 100);
#endif

	dm_bufio_default_cache_size = mem;

	mutex_lock(&dm_bufio_clients_lock);
	__cache_size_refresh();
	mutex_unlock(&dm_bufio_clients_lock);

	dm_bufio_wq = alloc_workqueue("dm_bufio_cache", WQ_MEM_RECLAIM, 0);
	if (!dm_bufio_wq)
		return -ENOMEM;

	INIT_WORK(&dm_bufio_replacement_work, do_global_cleanup);

	return 0;
}

/*
 * This is called once when unloading the dm_bufio module.
 */
static void __exit dm_bufio_exit(void)
{
	int bug = 0;

	destroy_workqueue(dm_bufio_wq);

	if (dm_bufio_client_count) {
		DMCRIT("%s: dm_bufio_client_count leaked: %d",
			__func__, dm_bufio_client_count);
		bug = 1;
	}

	if (dm_bufio_current_allocated) {
		DMCRIT("%s: dm_bufio_current_allocated leaked: %lu",
			__func__, dm_bufio_current_allocated);
		bug = 1;
	}

	if (dm_bufio_allocated_get_free_pages) {
		DMCRIT("%s: dm_bufio_allocated_get_free_pages leaked: %lu",
		       __func__, dm_bufio_allocated_get_free_pages);
		bug = 1;
	}

	if (dm_bufio_allocated_vmalloc) {
		DMCRIT("%s: dm_bufio_vmalloc leaked: %lu",
		       __func__, dm_bufio_allocated_vmalloc);
		bug = 1;
	}

	WARN_ON(bug); /* leaks are not worth crashing the system */
}

module_init(dm_bufio_init)
module_exit(dm_bufio_exit)

module_param_named(max_cache_size_bytes, dm_bufio_cache_size, ulong, 0644);
MODULE_PARM_DESC(max_cache_size_bytes, "Size of metadata cache");

module_param_named(max_age_seconds, dm_bufio_max_age, uint, 0644);
MODULE_PARM_DESC(max_age_seconds, "No longer does anything");

module_param_named(retain_bytes, dm_bufio_retain_bytes, ulong, 0644);
MODULE_PARM_DESC(retain_bytes, "Try to keep at least this many bytes cached in memory");

module_param_named(peak_allocated_bytes, dm_bufio_peak_allocated, ulong, 0644);
MODULE_PARM_DESC(peak_allocated_bytes, "Tracks the maximum allocated memory");

module_param_named(allocated_kmem_cache_bytes, dm_bufio_allocated_kmem_cache, ulong, 0444);
MODULE_PARM_DESC(allocated_kmem_cache_bytes, "Memory allocated with kmem_cache_alloc");

module_param_named(allocated_kmalloc_bytes, dm_bufio_allocated_kmalloc, ulong, 0444);
MODULE_PARM_DESC(allocated_kmalloc_bytes, "Memory allocated with kmalloc_alloc");

module_param_named(allocated_get_free_pages_bytes, dm_bufio_allocated_get_free_pages, ulong, 0444);
MODULE_PARM_DESC(allocated_get_free_pages_bytes, "Memory allocated with get_free_pages");

module_param_named(allocated_vmalloc_bytes, dm_bufio_allocated_vmalloc, ulong, 0444);
MODULE_PARM_DESC(allocated_vmalloc_bytes, "Memory allocated with vmalloc");

module_param_named(current_allocated_bytes, dm_bufio_current_allocated, ulong, 0444);
MODULE_PARM_DESC(current_allocated_bytes, "Memory currently used by the cache");

MODULE_AUTHOR("Mikulas Patocka <dm-devel@lists.linux.dev>");
MODULE_DESCRIPTION(DM_NAME " buffered I/O library");
MODULE_LICENSE("GPL");
