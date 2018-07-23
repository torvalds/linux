// SPDX-License-Identifier: GPL-2.0
/*
 * Primary bucket allocation code
 *
 * Copyright 2012 Google, Inc.
 *
 * Allocation in bcache is done in terms of buckets:
 *
 * Each bucket has associated an 8 bit gen; this gen corresponds to the gen in
 * btree pointers - they must match for the pointer to be considered valid.
 *
 * Thus (assuming a bucket has no dirty data or metadata in it) we can reuse a
 * bucket simply by incrementing its gen.
 *
 * The gens (along with the priorities; it's really the gens are important but
 * the code is named as if it's the priorities) are written in an arbitrary list
 * of buckets on disk, with a pointer to them in the journal header.
 *
 * When we invalidate a bucket, we have to write its new gen to disk and wait
 * for that write to complete before we use it - otherwise after a crash we
 * could have pointers that appeared to be good but pointed to data that had
 * been overwritten.
 *
 * Since the gens and priorities are all stored contiguously on disk, we can
 * batch this up: We fill up the free_inc list with freshly invalidated buckets,
 * call prio_write(), and when prio_write() finishes we pull buckets off the
 * free_inc list and optionally discard them.
 *
 * free_inc isn't the only freelist - if it was, we'd often to sleep while
 * priorities and gens were being written before we could allocate. c->free is a
 * smaller freelist, and buckets on that list are always ready to be used.
 *
 * If we've got discards enabled, that happens when a bucket moves from the
 * free_inc list to the free list.
 *
 * There is another freelist, because sometimes we have buckets that we know
 * have nothing pointing into them - these we can reuse without waiting for
 * priorities to be rewritten. These come from freed btree nodes and buckets
 * that garbage collection discovered no longer had valid keys pointing into
 * them (because they were overwritten). That's the unused list - buckets on the
 * unused list move to the free list, optionally being discarded in the process.
 *
 * It's also important to ensure that gens don't wrap around - with respect to
 * either the oldest gen in the btree or the gen on disk. This is quite
 * difficult to do in practice, but we explicitly guard against it anyways - if
 * a bucket is in danger of wrapping around we simply skip invalidating it that
 * time around, and we garbage collect or rewrite the priorities sooner than we
 * would have otherwise.
 *
 * bch_bucket_alloc() allocates a single bucket from a specific cache.
 *
 * bch_bucket_alloc_set() allocates one or more buckets from different caches
 * out of a cache set.
 *
 * free_some_buckets() drives all the processes described above. It's called
 * from bch_bucket_alloc() and a few other places that need to make sure free
 * buckets are ready.
 *
 * invalidate_buckets_(lru|fifo)() find buckets that are available to be
 * invalidated, and then invalidate them and stick them on the free_inc list -
 * in either lru or fifo order.
 */

#include "bcache.h"
#include "btree.h"

#include <linux/blkdev.h>
#include <linux/kthread.h>
#include <linux/random.h>
#include <trace/events/bcache.h>

#define MAX_OPEN_BUCKETS 128

/* Bucket heap / gen */

uint8_t bch_inc_gen(struct cache *ca, struct bucket *b)
{
	uint8_t ret = ++b->gen;

	ca->set->need_gc = max(ca->set->need_gc, bucket_gc_gen(b));
	WARN_ON_ONCE(ca->set->need_gc > BUCKET_GC_GEN_MAX);

	return ret;
}

void bch_rescale_priorities(struct cache_set *c, int sectors)
{
	struct cache *ca;
	struct bucket *b;
	unsigned next = c->nbuckets * c->sb.bucket_size / 1024;
	unsigned i;
	int r;

	atomic_sub(sectors, &c->rescale);

	do {
		r = atomic_read(&c->rescale);

		if (r >= 0)
			return;
	} while (atomic_cmpxchg(&c->rescale, r, r + next) != r);

	mutex_lock(&c->bucket_lock);

	c->min_prio = USHRT_MAX;

	for_each_cache(ca, c, i)
		for_each_bucket(b, ca)
			if (b->prio &&
			    b->prio != BTREE_PRIO &&
			    !atomic_read(&b->pin)) {
				b->prio--;
				c->min_prio = min(c->min_prio, b->prio);
			}

	mutex_unlock(&c->bucket_lock);
}

/*
 * Background allocation thread: scans for buckets to be invalidated,
 * invalidates them, rewrites prios/gens (marking them as invalidated on disk),
 * then optionally issues discard commands to the newly free buckets, then puts
 * them on the various freelists.
 */

static inline bool can_inc_bucket_gen(struct bucket *b)
{
	return bucket_gc_gen(b) < BUCKET_GC_GEN_MAX;
}

bool bch_can_invalidate_bucket(struct cache *ca, struct bucket *b)
{
	BUG_ON(!ca->set->gc_mark_valid);

	return (!GC_MARK(b) ||
		GC_MARK(b) == GC_MARK_RECLAIMABLE) &&
		!atomic_read(&b->pin) &&
		can_inc_bucket_gen(b);
}

void __bch_invalidate_one_bucket(struct cache *ca, struct bucket *b)
{
	lockdep_assert_held(&ca->set->bucket_lock);
	BUG_ON(GC_MARK(b) && GC_MARK(b) != GC_MARK_RECLAIMABLE);

	if (GC_SECTORS_USED(b))
		trace_bcache_invalidate(ca, b - ca->buckets);

	bch_inc_gen(ca, b);
	b->prio = INITIAL_PRIO;
	atomic_inc(&b->pin);
}

static void bch_invalidate_one_bucket(struct cache *ca, struct bucket *b)
{
	__bch_invalidate_one_bucket(ca, b);

	fifo_push(&ca->free_inc, b - ca->buckets);
}

/*
 * Determines what order we're going to reuse buckets, smallest bucket_prio()
 * first: we also take into account the number of sectors of live data in that
 * bucket, and in order for that multiply to make sense we have to scale bucket
 *
 * Thus, we scale the bucket priorities so that the bucket with the smallest
 * prio is worth 1/8th of what INITIAL_PRIO is worth.
 */

#define bucket_prio(b)							\
({									\
	unsigned min_prio = (INITIAL_PRIO - ca->set->min_prio) / 8;	\
									\
	(b->prio - ca->set->min_prio + min_prio) * GC_SECTORS_USED(b);	\
})

#define bucket_max_cmp(l, r)	(bucket_prio(l) < bucket_prio(r))
#define bucket_min_cmp(l, r)	(bucket_prio(l) > bucket_prio(r))

static void invalidate_buckets_lru(struct cache *ca)
{
	struct bucket *b;
	ssize_t i;

	ca->heap.used = 0;

	for_each_bucket(b, ca) {
		if (!bch_can_invalidate_bucket(ca, b))
			continue;

		if (!heap_full(&ca->heap))
			heap_add(&ca->heap, b, bucket_max_cmp);
		else if (bucket_max_cmp(b, heap_peek(&ca->heap))) {
			ca->heap.data[0] = b;
			heap_sift(&ca->heap, 0, bucket_max_cmp);
		}
	}

	for (i = ca->heap.used / 2 - 1; i >= 0; --i)
		heap_sift(&ca->heap, i, bucket_min_cmp);

	while (!fifo_full(&ca->free_inc)) {
		if (!heap_pop(&ca->heap, b, bucket_min_cmp)) {
			/*
			 * We don't want to be calling invalidate_buckets()
			 * multiple times when it can't do anything
			 */
			ca->invalidate_needs_gc = 1;
			wake_up_gc(ca->set);
			return;
		}

		bch_invalidate_one_bucket(ca, b);
	}
}

static void invalidate_buckets_fifo(struct cache *ca)
{
	struct bucket *b;
	size_t checked = 0;

	while (!fifo_full(&ca->free_inc)) {
		if (ca->fifo_last_bucket <  ca->sb.first_bucket ||
		    ca->fifo_last_bucket >= ca->sb.nbuckets)
			ca->fifo_last_bucket = ca->sb.first_bucket;

		b = ca->buckets + ca->fifo_last_bucket++;

		if (bch_can_invalidate_bucket(ca, b))
			bch_invalidate_one_bucket(ca, b);

		if (++checked >= ca->sb.nbuckets) {
			ca->invalidate_needs_gc = 1;
			wake_up_gc(ca->set);
			return;
		}
	}
}

static void invalidate_buckets_random(struct cache *ca)
{
	struct bucket *b;
	size_t checked = 0;

	while (!fifo_full(&ca->free_inc)) {
		size_t n;
		get_random_bytes(&n, sizeof(n));

		n %= (size_t) (ca->sb.nbuckets - ca->sb.first_bucket);
		n += ca->sb.first_bucket;

		b = ca->buckets + n;

		if (bch_can_invalidate_bucket(ca, b))
			bch_invalidate_one_bucket(ca, b);

		if (++checked >= ca->sb.nbuckets / 2) {
			ca->invalidate_needs_gc = 1;
			wake_up_gc(ca->set);
			return;
		}
	}
}

static void invalidate_buckets(struct cache *ca)
{
	BUG_ON(ca->invalidate_needs_gc);

	switch (CACHE_REPLACEMENT(&ca->sb)) {
	case CACHE_REPLACEMENT_LRU:
		invalidate_buckets_lru(ca);
		break;
	case CACHE_REPLACEMENT_FIFO:
		invalidate_buckets_fifo(ca);
		break;
	case CACHE_REPLACEMENT_RANDOM:
		invalidate_buckets_random(ca);
		break;
	}
}

#define allocator_wait(ca, cond)					\
do {									\
	while (1) {							\
		set_current_state(TASK_INTERRUPTIBLE);			\
		if (cond)						\
			break;						\
									\
		mutex_unlock(&(ca)->set->bucket_lock);			\
		if (kthread_should_stop() ||				\
		    test_bit(CACHE_SET_IO_DISABLE, &ca->set->flags)) {	\
			set_current_state(TASK_RUNNING);		\
			goto out;					\
		}							\
									\
		schedule();						\
		mutex_lock(&(ca)->set->bucket_lock);			\
	}								\
	__set_current_state(TASK_RUNNING);				\
} while (0)

static int bch_allocator_push(struct cache *ca, long bucket)
{
	unsigned i;

	/* Prios/gens are actually the most important reserve */
	if (fifo_push(&ca->free[RESERVE_PRIO], bucket))
		return true;

	for (i = 0; i < RESERVE_NR; i++)
		if (fifo_push(&ca->free[i], bucket))
			return true;

	return false;
}

static int bch_allocator_thread(void *arg)
{
	struct cache *ca = arg;

	mutex_lock(&ca->set->bucket_lock);

	while (1) {
		/*
		 * First, we pull buckets off of the unused and free_inc lists,
		 * possibly issue discards to them, then we add the bucket to
		 * the free list:
		 */
		while (!fifo_empty(&ca->free_inc)) {
			long bucket;

			fifo_pop(&ca->free_inc, bucket);

			if (ca->discard) {
				mutex_unlock(&ca->set->bucket_lock);
				blkdev_issue_discard(ca->bdev,
					bucket_to_sector(ca->set, bucket),
					ca->sb.bucket_size, GFP_KERNEL, 0);
				mutex_lock(&ca->set->bucket_lock);
			}

			allocator_wait(ca, bch_allocator_push(ca, bucket));
			wake_up(&ca->set->btree_cache_wait);
			wake_up(&ca->set->bucket_wait);
		}

		/*
		 * We've run out of free buckets, we need to find some buckets
		 * we can invalidate. First, invalidate them in memory and add
		 * them to the free_inc list:
		 */

retry_invalidate:
		allocator_wait(ca, ca->set->gc_mark_valid &&
			       !ca->invalidate_needs_gc);
		invalidate_buckets(ca);

		/*
		 * Now, we write their new gens to disk so we can start writing
		 * new stuff to them:
		 */
		allocator_wait(ca, !atomic_read(&ca->set->prio_blocked));
		if (CACHE_SYNC(&ca->set->sb)) {
			/*
			 * This could deadlock if an allocation with a btree
			 * node locked ever blocked - having the btree node
			 * locked would block garbage collection, but here we're
			 * waiting on garbage collection before we invalidate
			 * and free anything.
			 *
			 * But this should be safe since the btree code always
			 * uses btree_check_reserve() before allocating now, and
			 * if it fails it blocks without btree nodes locked.
			 */
			if (!fifo_full(&ca->free_inc))
				goto retry_invalidate;

			bch_prio_write(ca);
		}
	}
out:
	wait_for_kthread_stop();
	return 0;
}

/* Allocation */

long bch_bucket_alloc(struct cache *ca, unsigned reserve, bool wait)
{
	DEFINE_WAIT(w);
	struct bucket *b;
	long r;

	/* fastpath */
	if (fifo_pop(&ca->free[RESERVE_NONE], r) ||
	    fifo_pop(&ca->free[reserve], r))
		goto out;

	if (!wait) {
		trace_bcache_alloc_fail(ca, reserve);
		return -1;
	}

	do {
		prepare_to_wait(&ca->set->bucket_wait, &w,
				TASK_UNINTERRUPTIBLE);

		mutex_unlock(&ca->set->bucket_lock);
		schedule();
		mutex_lock(&ca->set->bucket_lock);
	} while (!fifo_pop(&ca->free[RESERVE_NONE], r) &&
		 !fifo_pop(&ca->free[reserve], r));

	finish_wait(&ca->set->bucket_wait, &w);
out:
	if (ca->alloc_thread)
		wake_up_process(ca->alloc_thread);

	trace_bcache_alloc(ca, reserve);

	if (expensive_debug_checks(ca->set)) {
		size_t iter;
		long i;
		unsigned j;

		for (iter = 0; iter < prio_buckets(ca) * 2; iter++)
			BUG_ON(ca->prio_buckets[iter] == (uint64_t) r);

		for (j = 0; j < RESERVE_NR; j++)
			fifo_for_each(i, &ca->free[j], iter)
				BUG_ON(i == r);
		fifo_for_each(i, &ca->free_inc, iter)
			BUG_ON(i == r);
	}

	b = ca->buckets + r;

	BUG_ON(atomic_read(&b->pin) != 1);

	SET_GC_SECTORS_USED(b, ca->sb.bucket_size);

	if (reserve <= RESERVE_PRIO) {
		SET_GC_MARK(b, GC_MARK_METADATA);
		SET_GC_MOVE(b, 0);
		b->prio = BTREE_PRIO;
	} else {
		SET_GC_MARK(b, GC_MARK_RECLAIMABLE);
		SET_GC_MOVE(b, 0);
		b->prio = INITIAL_PRIO;
	}

	if (ca->set->avail_nbuckets > 0) {
		ca->set->avail_nbuckets--;
		bch_update_bucket_in_use(ca->set, &ca->set->gc_stats);
	}

	return r;
}

void __bch_bucket_free(struct cache *ca, struct bucket *b)
{
	SET_GC_MARK(b, 0);
	SET_GC_SECTORS_USED(b, 0);

	if (ca->set->avail_nbuckets < ca->set->nbuckets) {
		ca->set->avail_nbuckets++;
		bch_update_bucket_in_use(ca->set, &ca->set->gc_stats);
	}
}

void bch_bucket_free(struct cache_set *c, struct bkey *k)
{
	unsigned i;

	for (i = 0; i < KEY_PTRS(k); i++)
		__bch_bucket_free(PTR_CACHE(c, k, i),
				  PTR_BUCKET(c, k, i));
}

int __bch_bucket_alloc_set(struct cache_set *c, unsigned reserve,
			   struct bkey *k, int n, bool wait)
{
	int i;

	lockdep_assert_held(&c->bucket_lock);
	BUG_ON(!n || n > c->caches_loaded || n > 8);

	bkey_init(k);

	/* sort by free space/prio of oldest data in caches */

	for (i = 0; i < n; i++) {
		struct cache *ca = c->cache_by_alloc[i];
		long b = bch_bucket_alloc(ca, reserve, wait);

		if (b == -1)
			goto err;

		k->ptr[i] = MAKE_PTR(ca->buckets[b].gen,
				bucket_to_sector(c, b),
				ca->sb.nr_this_dev);

		SET_KEY_PTRS(k, i + 1);
	}

	return 0;
err:
	bch_bucket_free(c, k);
	bkey_put(c, k);
	return -1;
}

int bch_bucket_alloc_set(struct cache_set *c, unsigned reserve,
			 struct bkey *k, int n, bool wait)
{
	int ret;
	mutex_lock(&c->bucket_lock);
	ret = __bch_bucket_alloc_set(c, reserve, k, n, wait);
	mutex_unlock(&c->bucket_lock);
	return ret;
}

/* Sector allocator */

struct open_bucket {
	struct list_head	list;
	unsigned		last_write_point;
	unsigned		sectors_free;
	BKEY_PADDED(key);
};

/*
 * We keep multiple buckets open for writes, and try to segregate different
 * write streams for better cache utilization: first we try to segregate flash
 * only volume write streams from cached devices, secondly we look for a bucket
 * where the last write to it was sequential with the current write, and
 * failing that we look for a bucket that was last used by the same task.
 *
 * The ideas is if you've got multiple tasks pulling data into the cache at the
 * same time, you'll get better cache utilization if you try to segregate their
 * data and preserve locality.
 *
 * For example, dirty sectors of flash only volume is not reclaimable, if their
 * dirty sectors mixed with dirty sectors of cached device, such buckets will
 * be marked as dirty and won't be reclaimed, though the dirty data of cached
 * device have been written back to backend device.
 *
 * And say you've starting Firefox at the same time you're copying a
 * bunch of files. Firefox will likely end up being fairly hot and stay in the
 * cache awhile, but the data you copied might not be; if you wrote all that
 * data to the same buckets it'd get invalidated at the same time.
 *
 * Both of those tasks will be doing fairly random IO so we can't rely on
 * detecting sequential IO to segregate their data, but going off of the task
 * should be a sane heuristic.
 */
static struct open_bucket *pick_data_bucket(struct cache_set *c,
					    const struct bkey *search,
					    unsigned write_point,
					    struct bkey *alloc)
{
	struct open_bucket *ret, *ret_task = NULL;

	list_for_each_entry_reverse(ret, &c->data_buckets, list)
		if (UUID_FLASH_ONLY(&c->uuids[KEY_INODE(&ret->key)]) !=
		    UUID_FLASH_ONLY(&c->uuids[KEY_INODE(search)]))
			continue;
		else if (!bkey_cmp(&ret->key, search))
			goto found;
		else if (ret->last_write_point == write_point)
			ret_task = ret;

	ret = ret_task ?: list_first_entry(&c->data_buckets,
					   struct open_bucket, list);
found:
	if (!ret->sectors_free && KEY_PTRS(alloc)) {
		ret->sectors_free = c->sb.bucket_size;
		bkey_copy(&ret->key, alloc);
		bkey_init(alloc);
	}

	if (!ret->sectors_free)
		ret = NULL;

	return ret;
}

/*
 * Allocates some space in the cache to write to, and k to point to the newly
 * allocated space, and updates KEY_SIZE(k) and KEY_OFFSET(k) (to point to the
 * end of the newly allocated space).
 *
 * May allocate fewer sectors than @sectors, KEY_SIZE(k) indicates how many
 * sectors were actually allocated.
 *
 * If s->writeback is true, will not fail.
 */
bool bch_alloc_sectors(struct cache_set *c, struct bkey *k, unsigned sectors,
		       unsigned write_point, unsigned write_prio, bool wait)
{
	struct open_bucket *b;
	BKEY_PADDED(key) alloc;
	unsigned i;

	/*
	 * We might have to allocate a new bucket, which we can't do with a
	 * spinlock held. So if we have to allocate, we drop the lock, allocate
	 * and then retry. KEY_PTRS() indicates whether alloc points to
	 * allocated bucket(s).
	 */

	bkey_init(&alloc.key);
	spin_lock(&c->data_bucket_lock);

	while (!(b = pick_data_bucket(c, k, write_point, &alloc.key))) {
		unsigned watermark = write_prio
			? RESERVE_MOVINGGC
			: RESERVE_NONE;

		spin_unlock(&c->data_bucket_lock);

		if (bch_bucket_alloc_set(c, watermark, &alloc.key, 1, wait))
			return false;

		spin_lock(&c->data_bucket_lock);
	}

	/*
	 * If we had to allocate, we might race and not need to allocate the
	 * second time we call pick_data_bucket(). If we allocated a bucket but
	 * didn't use it, drop the refcount bch_bucket_alloc_set() took:
	 */
	if (KEY_PTRS(&alloc.key))
		bkey_put(c, &alloc.key);

	for (i = 0; i < KEY_PTRS(&b->key); i++)
		EBUG_ON(ptr_stale(c, &b->key, i));

	/* Set up the pointer to the space we're allocating: */

	for (i = 0; i < KEY_PTRS(&b->key); i++)
		k->ptr[i] = b->key.ptr[i];

	sectors = min(sectors, b->sectors_free);

	SET_KEY_OFFSET(k, KEY_OFFSET(k) + sectors);
	SET_KEY_SIZE(k, sectors);
	SET_KEY_PTRS(k, KEY_PTRS(&b->key));

	/*
	 * Move b to the end of the lru, and keep track of what this bucket was
	 * last used for:
	 */
	list_move_tail(&b->list, &c->data_buckets);
	bkey_copy_key(&b->key, k);
	b->last_write_point = write_point;

	b->sectors_free	-= sectors;

	for (i = 0; i < KEY_PTRS(&b->key); i++) {
		SET_PTR_OFFSET(&b->key, i, PTR_OFFSET(&b->key, i) + sectors);

		atomic_long_add(sectors,
				&PTR_CACHE(c, &b->key, i)->sectors_written);
	}

	if (b->sectors_free < c->sb.block_size)
		b->sectors_free = 0;

	/*
	 * k takes refcounts on the buckets it points to until it's inserted
	 * into the btree, but if we're done with this bucket we just transfer
	 * get_data_bucket()'s refcount.
	 */
	if (b->sectors_free)
		for (i = 0; i < KEY_PTRS(&b->key); i++)
			atomic_inc(&PTR_BUCKET(c, &b->key, i)->pin);

	spin_unlock(&c->data_bucket_lock);
	return true;
}

/* Init */

void bch_open_buckets_free(struct cache_set *c)
{
	struct open_bucket *b;

	while (!list_empty(&c->data_buckets)) {
		b = list_first_entry(&c->data_buckets,
				     struct open_bucket, list);
		list_del(&b->list);
		kfree(b);
	}
}

int bch_open_buckets_alloc(struct cache_set *c)
{
	int i;

	spin_lock_init(&c->data_bucket_lock);

	for (i = 0; i < MAX_OPEN_BUCKETS; i++) {
		struct open_bucket *b = kzalloc(sizeof(*b), GFP_KERNEL);
		if (!b)
			return -ENOMEM;

		list_add(&b->list, &c->data_buckets);
	}

	return 0;
}

int bch_cache_allocator_start(struct cache *ca)
{
	struct task_struct *k = kthread_run(bch_allocator_thread,
					    ca, "bcache_allocator");
	if (IS_ERR(k))
		return PTR_ERR(k);

	ca->alloc_thread = k;
	return 0;
}
