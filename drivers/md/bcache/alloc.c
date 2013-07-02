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

#include <linux/kthread.h>
#include <linux/random.h>
#include <trace/events/bcache.h>

#define MAX_IN_FLIGHT_DISCARDS		8U

/* Bucket heap / gen */

uint8_t bch_inc_gen(struct cache *ca, struct bucket *b)
{
	uint8_t ret = ++b->gen;

	ca->set->need_gc = max(ca->set->need_gc, bucket_gc_gen(b));
	WARN_ON_ONCE(ca->set->need_gc > BUCKET_GC_GEN_MAX);

	if (CACHE_SYNC(&ca->set->sb)) {
		ca->need_save_prio = max(ca->need_save_prio,
					 bucket_disk_gen(b));
		WARN_ON_ONCE(ca->need_save_prio > BUCKET_DISK_GEN_MAX);
	}

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

/* Discard/TRIM */

struct discard {
	struct list_head	list;
	struct work_struct	work;
	struct cache		*ca;
	long			bucket;

	struct bio		bio;
	struct bio_vec		bv;
};

static void discard_finish(struct work_struct *w)
{
	struct discard *d = container_of(w, struct discard, work);
	struct cache *ca = d->ca;
	char buf[BDEVNAME_SIZE];

	if (!test_bit(BIO_UPTODATE, &d->bio.bi_flags)) {
		pr_notice("discard error on %s, disabling",
			 bdevname(ca->bdev, buf));
		d->ca->discard = 0;
	}

	mutex_lock(&ca->set->bucket_lock);

	fifo_push(&ca->free, d->bucket);
	list_add(&d->list, &ca->discards);
	atomic_dec(&ca->discards_in_flight);

	mutex_unlock(&ca->set->bucket_lock);

	closure_wake_up(&ca->set->bucket_wait);
	wake_up_process(ca->alloc_thread);

	closure_put(&ca->set->cl);
}

static void discard_endio(struct bio *bio, int error)
{
	struct discard *d = container_of(bio, struct discard, bio);
	schedule_work(&d->work);
}

static void do_discard(struct cache *ca, long bucket)
{
	struct discard *d = list_first_entry(&ca->discards,
					     struct discard, list);

	list_del(&d->list);
	d->bucket = bucket;

	atomic_inc(&ca->discards_in_flight);
	closure_get(&ca->set->cl);

	bio_init(&d->bio);

	d->bio.bi_sector	= bucket_to_sector(ca->set, d->bucket);
	d->bio.bi_bdev		= ca->bdev;
	d->bio.bi_rw		= REQ_WRITE|REQ_DISCARD;
	d->bio.bi_max_vecs	= 1;
	d->bio.bi_io_vec	= d->bio.bi_inline_vecs;
	d->bio.bi_size		= bucket_bytes(ca);
	d->bio.bi_end_io	= discard_endio;
	bio_set_prio(&d->bio, IOPRIO_PRIO_VALUE(IOPRIO_CLASS_IDLE, 0));

	submit_bio(0, &d->bio);
}

/* Allocation */

static inline bool can_inc_bucket_gen(struct bucket *b)
{
	return bucket_gc_gen(b) < BUCKET_GC_GEN_MAX &&
		bucket_disk_gen(b) < BUCKET_DISK_GEN_MAX;
}

bool bch_bucket_add_unused(struct cache *ca, struct bucket *b)
{
	BUG_ON(GC_MARK(b) || GC_SECTORS_USED(b));

	if (fifo_used(&ca->free) > ca->watermark[WATERMARK_MOVINGGC] &&
	    CACHE_REPLACEMENT(&ca->sb) == CACHE_REPLACEMENT_FIFO)
		return false;

	b->prio = 0;

	if (can_inc_bucket_gen(b) &&
	    fifo_push(&ca->unused, b - ca->buckets)) {
		atomic_inc(&b->pin);
		return true;
	}

	return false;
}

static bool can_invalidate_bucket(struct cache *ca, struct bucket *b)
{
	return GC_MARK(b) == GC_MARK_RECLAIMABLE &&
		!atomic_read(&b->pin) &&
		can_inc_bucket_gen(b);
}

static void invalidate_one_bucket(struct cache *ca, struct bucket *b)
{
	bch_inc_gen(ca, b);
	b->prio = INITIAL_PRIO;
	atomic_inc(&b->pin);
	fifo_push(&ca->free_inc, b - ca->buckets);
}

#define bucket_prio(b)				\
	(((unsigned) (b->prio - ca->set->min_prio)) * GC_SECTORS_USED(b))

#define bucket_max_cmp(l, r)	(bucket_prio(l) < bucket_prio(r))
#define bucket_min_cmp(l, r)	(bucket_prio(l) > bucket_prio(r))

static void invalidate_buckets_lru(struct cache *ca)
{
	struct bucket *b;
	ssize_t i;

	ca->heap.used = 0;

	for_each_bucket(b, ca) {
		/*
		 * If we fill up the unused list, if we then return before
		 * adding anything to the free_inc list we'll skip writing
		 * prios/gens and just go back to allocating from the unused
		 * list:
		 */
		if (fifo_full(&ca->unused))
			return;

		if (!can_invalidate_bucket(ca, b))
			continue;

		if (!GC_SECTORS_USED(b) &&
		    bch_bucket_add_unused(ca, b))
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
			bch_queue_gc(ca->set);
			return;
		}

		invalidate_one_bucket(ca, b);
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

		if (can_invalidate_bucket(ca, b))
			invalidate_one_bucket(ca, b);

		if (++checked >= ca->sb.nbuckets) {
			ca->invalidate_needs_gc = 1;
			bch_queue_gc(ca->set);
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

		if (can_invalidate_bucket(ca, b))
			invalidate_one_bucket(ca, b);

		if (++checked >= ca->sb.nbuckets / 2) {
			ca->invalidate_needs_gc = 1;
			bch_queue_gc(ca->set);
			return;
		}
	}
}

static void invalidate_buckets(struct cache *ca)
{
	if (ca->invalidate_needs_gc)
		return;

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

	trace_bcache_alloc_invalidate(ca);
}

#define allocator_wait(ca, cond)					\
do {									\
	while (1) {							\
		set_current_state(TASK_INTERRUPTIBLE);			\
		if (cond)						\
			break;						\
									\
		mutex_unlock(&(ca)->set->bucket_lock);			\
		if (test_bit(CACHE_SET_STOPPING_2, &ca->set->flags)) {	\
			closure_put(&ca->set->cl);			\
			return 0;					\
		}							\
									\
		schedule();						\
		mutex_lock(&(ca)->set->bucket_lock);			\
	}								\
	__set_current_state(TASK_RUNNING);				\
} while (0)

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
		while (1) {
			long bucket;

			if ((!atomic_read(&ca->set->prio_blocked) ||
			     !CACHE_SYNC(&ca->set->sb)) &&
			    !fifo_empty(&ca->unused))
				fifo_pop(&ca->unused, bucket);
			else if (!fifo_empty(&ca->free_inc))
				fifo_pop(&ca->free_inc, bucket);
			else
				break;

			allocator_wait(ca, (int) fifo_free(&ca->free) >
				       atomic_read(&ca->discards_in_flight));

			if (ca->discard) {
				allocator_wait(ca, !list_empty(&ca->discards));
				do_discard(ca, bucket);
			} else {
				fifo_push(&ca->free, bucket);
				closure_wake_up(&ca->set->bucket_wait);
			}
		}

		/*
		 * We've run out of free buckets, we need to find some buckets
		 * we can invalidate. First, invalidate them in memory and add
		 * them to the free_inc list:
		 */

		allocator_wait(ca, ca->set->gc_mark_valid &&
			       (ca->need_save_prio > 64 ||
				!ca->invalidate_needs_gc));
		invalidate_buckets(ca);

		/*
		 * Now, we write their new gens to disk so we can start writing
		 * new stuff to them:
		 */
		allocator_wait(ca, !atomic_read(&ca->set->prio_blocked));
		if (CACHE_SYNC(&ca->set->sb) &&
		    (!fifo_empty(&ca->free_inc) ||
		     ca->need_save_prio > 64))
			bch_prio_write(ca);
	}
}

long bch_bucket_alloc(struct cache *ca, unsigned watermark, struct closure *cl)
{
	long r = -1;
again:
	wake_up_process(ca->alloc_thread);

	if (fifo_used(&ca->free) > ca->watermark[watermark] &&
	    fifo_pop(&ca->free, r)) {
		struct bucket *b = ca->buckets + r;
#ifdef CONFIG_BCACHE_EDEBUG
		size_t iter;
		long i;

		for (iter = 0; iter < prio_buckets(ca) * 2; iter++)
			BUG_ON(ca->prio_buckets[iter] == (uint64_t) r);

		fifo_for_each(i, &ca->free, iter)
			BUG_ON(i == r);
		fifo_for_each(i, &ca->free_inc, iter)
			BUG_ON(i == r);
		fifo_for_each(i, &ca->unused, iter)
			BUG_ON(i == r);
#endif
		BUG_ON(atomic_read(&b->pin) != 1);

		SET_GC_SECTORS_USED(b, ca->sb.bucket_size);

		if (watermark <= WATERMARK_METADATA) {
			SET_GC_MARK(b, GC_MARK_METADATA);
			b->prio = BTREE_PRIO;
		} else {
			SET_GC_MARK(b, GC_MARK_RECLAIMABLE);
			b->prio = INITIAL_PRIO;
		}

		return r;
	}

	trace_bcache_alloc_fail(ca);

	if (cl) {
		closure_wait(&ca->set->bucket_wait, cl);

		if (closure_blocking(cl)) {
			mutex_unlock(&ca->set->bucket_lock);
			closure_sync(cl);
			mutex_lock(&ca->set->bucket_lock);
			goto again;
		}
	}

	return -1;
}

void bch_bucket_free(struct cache_set *c, struct bkey *k)
{
	unsigned i;

	for (i = 0; i < KEY_PTRS(k); i++) {
		struct bucket *b = PTR_BUCKET(c, k, i);

		SET_GC_MARK(b, GC_MARK_RECLAIMABLE);
		SET_GC_SECTORS_USED(b, 0);
		bch_bucket_add_unused(PTR_CACHE(c, k, i), b);
	}
}

int __bch_bucket_alloc_set(struct cache_set *c, unsigned watermark,
			   struct bkey *k, int n, struct closure *cl)
{
	int i;

	lockdep_assert_held(&c->bucket_lock);
	BUG_ON(!n || n > c->caches_loaded || n > 8);

	bkey_init(k);

	/* sort by free space/prio of oldest data in caches */

	for (i = 0; i < n; i++) {
		struct cache *ca = c->cache_by_alloc[i];
		long b = bch_bucket_alloc(ca, watermark, cl);

		if (b == -1)
			goto err;

		k->ptr[i] = PTR(ca->buckets[b].gen,
				bucket_to_sector(c, b),
				ca->sb.nr_this_dev);

		SET_KEY_PTRS(k, i + 1);
	}

	return 0;
err:
	bch_bucket_free(c, k);
	__bkey_put(c, k);
	return -1;
}

int bch_bucket_alloc_set(struct cache_set *c, unsigned watermark,
			 struct bkey *k, int n, struct closure *cl)
{
	int ret;
	mutex_lock(&c->bucket_lock);
	ret = __bch_bucket_alloc_set(c, watermark, k, n, cl);
	mutex_unlock(&c->bucket_lock);
	return ret;
}

/* Init */

int bch_cache_allocator_start(struct cache *ca)
{
	ca->alloc_thread = kthread_create(bch_allocator_thread,
					  ca, "bcache_allocator");
	if (IS_ERR(ca->alloc_thread))
		return PTR_ERR(ca->alloc_thread);

	closure_get(&ca->set->cl);
	wake_up_process(ca->alloc_thread);

	return 0;
}

void bch_cache_allocator_exit(struct cache *ca)
{
	struct discard *d;

	while (!list_empty(&ca->discards)) {
		d = list_first_entry(&ca->discards, struct discard, list);
		cancel_work_sync(&d->work);
		list_del(&d->list);
		kfree(d);
	}
}

int bch_cache_allocator_init(struct cache *ca)
{
	unsigned i;

	/*
	 * Reserve:
	 * Prio/gen writes first
	 * Then 8 for btree allocations
	 * Then half for the moving garbage collector
	 */

	ca->watermark[WATERMARK_PRIO] = 0;

	ca->watermark[WATERMARK_METADATA] = prio_buckets(ca);

	ca->watermark[WATERMARK_MOVINGGC] = 8 +
		ca->watermark[WATERMARK_METADATA];

	ca->watermark[WATERMARK_NONE] = ca->free.size / 2 +
		ca->watermark[WATERMARK_MOVINGGC];

	for (i = 0; i < MAX_IN_FLIGHT_DISCARDS; i++) {
		struct discard *d = kzalloc(sizeof(*d), GFP_KERNEL);
		if (!d)
			return -ENOMEM;

		d->ca = ca;
		INIT_WORK(&d->work, discard_finish);
		list_add(&d->list, &ca->discards);
	}

	return 0;
}
