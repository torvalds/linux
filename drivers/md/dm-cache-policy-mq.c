/*
 * Copyright (C) 2012 Red Hat. All rights reserved.
 *
 * This file is released under the GPL.
 */

#include "dm-cache-policy.h"
#include "dm.h"

#include <linux/hash.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

#define DM_MSG_PREFIX "cache-policy-mq"

static struct kmem_cache *mq_entry_cache;

/*----------------------------------------------------------------*/

static unsigned next_power(unsigned n, unsigned min)
{
	return roundup_pow_of_two(max(n, min));
}

/*----------------------------------------------------------------*/

/*
 * Large, sequential ios are probably better left on the origin device since
 * spindles tend to have good bandwidth.
 *
 * The io_tracker tries to spot when the io is in one of these sequential
 * modes.
 *
 * Two thresholds to switch between random and sequential io mode are defaulting
 * as follows and can be adjusted via the constructor and message interfaces.
 */
#define RANDOM_THRESHOLD_DEFAULT 4
#define SEQUENTIAL_THRESHOLD_DEFAULT 512

enum io_pattern {
	PATTERN_SEQUENTIAL,
	PATTERN_RANDOM
};

struct io_tracker {
	enum io_pattern pattern;

	unsigned nr_seq_samples;
	unsigned nr_rand_samples;
	unsigned thresholds[2];

	dm_oblock_t last_end_oblock;
};

static void iot_init(struct io_tracker *t,
		     int sequential_threshold, int random_threshold)
{
	t->pattern = PATTERN_RANDOM;
	t->nr_seq_samples = 0;
	t->nr_rand_samples = 0;
	t->last_end_oblock = 0;
	t->thresholds[PATTERN_RANDOM] = random_threshold;
	t->thresholds[PATTERN_SEQUENTIAL] = sequential_threshold;
}

static enum io_pattern iot_pattern(struct io_tracker *t)
{
	return t->pattern;
}

static void iot_update_stats(struct io_tracker *t, struct bio *bio)
{
	if (bio->bi_iter.bi_sector == from_oblock(t->last_end_oblock) + 1)
		t->nr_seq_samples++;
	else {
		/*
		 * Just one non-sequential IO is enough to reset the
		 * counters.
		 */
		if (t->nr_seq_samples) {
			t->nr_seq_samples = 0;
			t->nr_rand_samples = 0;
		}

		t->nr_rand_samples++;
	}

	t->last_end_oblock = to_oblock(bio_end_sector(bio) - 1);
}

static void iot_check_for_pattern_switch(struct io_tracker *t)
{
	switch (t->pattern) {
	case PATTERN_SEQUENTIAL:
		if (t->nr_rand_samples >= t->thresholds[PATTERN_RANDOM]) {
			t->pattern = PATTERN_RANDOM;
			t->nr_seq_samples = t->nr_rand_samples = 0;
		}
		break;

	case PATTERN_RANDOM:
		if (t->nr_seq_samples >= t->thresholds[PATTERN_SEQUENTIAL]) {
			t->pattern = PATTERN_SEQUENTIAL;
			t->nr_seq_samples = t->nr_rand_samples = 0;
		}
		break;
	}
}

static void iot_examine_bio(struct io_tracker *t, struct bio *bio)
{
	iot_update_stats(t, bio);
	iot_check_for_pattern_switch(t);
}

/*----------------------------------------------------------------*/


/*
 * This queue is divided up into different levels.  Allowing us to push
 * entries to the back of any of the levels.  Think of it as a partially
 * sorted queue.
 */
#define NR_QUEUE_LEVELS 16u

struct queue {
	struct list_head qs[NR_QUEUE_LEVELS];
};

static void queue_init(struct queue *q)
{
	unsigned i;

	for (i = 0; i < NR_QUEUE_LEVELS; i++)
		INIT_LIST_HEAD(q->qs + i);
}

/*
 * Checks to see if the queue is empty.
 * FIXME: reduce cpu usage.
 */
static bool queue_empty(struct queue *q)
{
	unsigned i;

	for (i = 0; i < NR_QUEUE_LEVELS; i++)
		if (!list_empty(q->qs + i))
			return false;

	return true;
}

/*
 * Insert an entry to the back of the given level.
 */
static void queue_push(struct queue *q, unsigned level, struct list_head *elt)
{
	list_add_tail(elt, q->qs + level);
}

static void queue_remove(struct list_head *elt)
{
	list_del(elt);
}

/*
 * Shifts all regions down one level.  This has no effect on the order of
 * the queue.
 */
static void queue_shift_down(struct queue *q)
{
	unsigned level;

	for (level = 1; level < NR_QUEUE_LEVELS; level++)
		list_splice_init(q->qs + level, q->qs + level - 1);
}

/*
 * Gives us the oldest entry of the lowest popoulated level.  If the first
 * level is emptied then we shift down one level.
 */
static struct list_head *queue_peek(struct queue *q)
{
	unsigned level;

	for (level = 0; level < NR_QUEUE_LEVELS; level++)
		if (!list_empty(q->qs + level))
			return q->qs[level].next;

	return NULL;
}

static struct list_head *queue_pop(struct queue *q)
{
	struct list_head *r = queue_peek(q);

	if (r) {
		list_del(r);

		/* have we just emptied the bottom level? */
		if (list_empty(q->qs))
			queue_shift_down(q);
	}

	return r;
}

static struct list_head *list_pop(struct list_head *lh)
{
	struct list_head *r = lh->next;

	BUG_ON(!r);
	list_del_init(r);

	return r;
}

/*----------------------------------------------------------------*/

/*
 * Describes a cache entry.  Used in both the cache and the pre_cache.
 */
struct entry {
	struct hlist_node hlist;
	struct list_head list;
	dm_oblock_t oblock;

	/*
	 * FIXME: pack these better
	 */
	bool dirty:1;
	unsigned hit_count;
	unsigned generation;
	unsigned tick;
};

/*
 * Rather than storing the cblock in an entry, we allocate all entries in
 * an array, and infer the cblock from the entry position.
 *
 * Free entries are linked together into a list.
 */
struct entry_pool {
	struct entry *entries, *entries_end;
	struct list_head free;
	unsigned nr_allocated;
};

static int epool_init(struct entry_pool *ep, unsigned nr_entries)
{
	unsigned i;

	ep->entries = vzalloc(sizeof(struct entry) * nr_entries);
	if (!ep->entries)
		return -ENOMEM;

	ep->entries_end = ep->entries + nr_entries;

	INIT_LIST_HEAD(&ep->free);
	for (i = 0; i < nr_entries; i++)
		list_add(&ep->entries[i].list, &ep->free);

	ep->nr_allocated = 0;

	return 0;
}

static void epool_exit(struct entry_pool *ep)
{
	vfree(ep->entries);
}

static struct entry *alloc_entry(struct entry_pool *ep)
{
	struct entry *e;

	if (list_empty(&ep->free))
		return NULL;

	e = list_entry(list_pop(&ep->free), struct entry, list);
	INIT_LIST_HEAD(&e->list);
	INIT_HLIST_NODE(&e->hlist);
	ep->nr_allocated++;

	return e;
}

/*
 * This assumes the cblock hasn't already been allocated.
 */
static struct entry *alloc_particular_entry(struct entry_pool *ep, dm_cblock_t cblock)
{
	struct entry *e = ep->entries + from_cblock(cblock);

	list_del_init(&e->list);
	INIT_HLIST_NODE(&e->hlist);
	ep->nr_allocated++;

	return e;
}

static void free_entry(struct entry_pool *ep, struct entry *e)
{
	BUG_ON(!ep->nr_allocated);
	ep->nr_allocated--;
	INIT_HLIST_NODE(&e->hlist);
	list_add(&e->list, &ep->free);
}

/*
 * Returns NULL if the entry is free.
 */
static struct entry *epool_find(struct entry_pool *ep, dm_cblock_t cblock)
{
	struct entry *e = ep->entries + from_cblock(cblock);
	return !hlist_unhashed(&e->hlist) ? e : NULL;
}

static bool epool_empty(struct entry_pool *ep)
{
	return list_empty(&ep->free);
}

static bool in_pool(struct entry_pool *ep, struct entry *e)
{
	return e >= ep->entries && e < ep->entries_end;
}

static dm_cblock_t infer_cblock(struct entry_pool *ep, struct entry *e)
{
	return to_cblock(e - ep->entries);
}

/*----------------------------------------------------------------*/

struct mq_policy {
	struct dm_cache_policy policy;

	/* protects everything */
	struct mutex lock;
	dm_cblock_t cache_size;
	struct io_tracker tracker;

	/*
	 * Entries come from two pools, one of pre-cache entries, and one
	 * for the cache proper.
	 */
	struct entry_pool pre_cache_pool;
	struct entry_pool cache_pool;

	/*
	 * We maintain three queues of entries.  The cache proper,
	 * consisting of a clean and dirty queue, contains the currently
	 * active mappings.  Whereas the pre_cache tracks blocks that
	 * are being hit frequently and potential candidates for promotion
	 * to the cache.
	 */
	struct queue pre_cache;
	struct queue cache_clean;
	struct queue cache_dirty;

	/*
	 * Keeps track of time, incremented by the core.  We use this to
	 * avoid attributing multiple hits within the same tick.
	 *
	 * Access to tick_protected should be done with the spin lock held.
	 * It's copied to tick at the start of the map function (within the
	 * mutex).
	 */
	spinlock_t tick_lock;
	unsigned tick_protected;
	unsigned tick;

	/*
	 * A count of the number of times the map function has been called
	 * and found an entry in the pre_cache or cache.  Currently used to
	 * calculate the generation.
	 */
	unsigned hit_count;

	/*
	 * A generation is a longish period that is used to trigger some
	 * book keeping effects.  eg, decrementing hit counts on entries.
	 * This is needed to allow the cache to evolve as io patterns
	 * change.
	 */
	unsigned generation;
	unsigned generation_period; /* in lookups (will probably change) */

	unsigned discard_promote_adjustment;
	unsigned read_promote_adjustment;
	unsigned write_promote_adjustment;

	/*
	 * The hash table allows us to quickly find an entry by origin
	 * block.  Both pre_cache and cache entries are in here.
	 */
	unsigned nr_buckets;
	dm_block_t hash_bits;
	struct hlist_head *table;
};

#define DEFAULT_DISCARD_PROMOTE_ADJUSTMENT 1
#define DEFAULT_READ_PROMOTE_ADJUSTMENT 4
#define DEFAULT_WRITE_PROMOTE_ADJUSTMENT 8
#define DISCOURAGE_DEMOTING_DIRTY_THRESHOLD 128

/*----------------------------------------------------------------*/

/*
 * Simple hash table implementation.  Should replace with the standard hash
 * table that's making its way upstream.
 */
static void hash_insert(struct mq_policy *mq, struct entry *e)
{
	unsigned h = hash_64(from_oblock(e->oblock), mq->hash_bits);

	hlist_add_head(&e->hlist, mq->table + h);
}

static struct entry *hash_lookup(struct mq_policy *mq, dm_oblock_t oblock)
{
	unsigned h = hash_64(from_oblock(oblock), mq->hash_bits);
	struct hlist_head *bucket = mq->table + h;
	struct entry *e;

	hlist_for_each_entry(e, bucket, hlist)
		if (e->oblock == oblock) {
			hlist_del(&e->hlist);
			hlist_add_head(&e->hlist, bucket);
			return e;
		}

	return NULL;
}

static void hash_remove(struct entry *e)
{
	hlist_del(&e->hlist);
}

/*----------------------------------------------------------------*/

static bool any_free_cblocks(struct mq_policy *mq)
{
	return !epool_empty(&mq->cache_pool);
}

static bool any_clean_cblocks(struct mq_policy *mq)
{
	return !queue_empty(&mq->cache_clean);
}

/*----------------------------------------------------------------*/

/*
 * Now we get to the meat of the policy.  This section deals with deciding
 * when to to add entries to the pre_cache and cache, and move between
 * them.
 */

/*
 * The queue level is based on the log2 of the hit count.
 */
static unsigned queue_level(struct entry *e)
{
	return min((unsigned) ilog2(e->hit_count), NR_QUEUE_LEVELS - 1u);
}

static bool in_cache(struct mq_policy *mq, struct entry *e)
{
	return in_pool(&mq->cache_pool, e);
}

/*
 * Inserts the entry into the pre_cache or the cache.  Ensures the cache
 * block is marked as allocated if necc.  Inserts into the hash table.
 * Sets the tick which records when the entry was last moved about.
 */
static void push(struct mq_policy *mq, struct entry *e)
{
	e->tick = mq->tick;
	hash_insert(mq, e);

	if (in_cache(mq, e))
		queue_push(e->dirty ? &mq->cache_dirty : &mq->cache_clean,
			   queue_level(e), &e->list);
	else
		queue_push(&mq->pre_cache, queue_level(e), &e->list);
}

/*
 * Removes an entry from pre_cache or cache.  Removes from the hash table.
 */
static void del(struct mq_policy *mq, struct entry *e)
{
	queue_remove(&e->list);
	hash_remove(e);
}

/*
 * Like del, except it removes the first entry in the queue (ie. the least
 * recently used).
 */
static struct entry *pop(struct mq_policy *mq, struct queue *q)
{
	struct entry *e;
	struct list_head *h = queue_pop(q);

	if (!h)
		return NULL;

	e = container_of(h, struct entry, list);
	hash_remove(e);

	return e;
}

static struct entry *peek(struct queue *q)
{
	struct list_head *h = queue_peek(q);
	return h ? container_of(h, struct entry, list) : NULL;
}

/*
 * Has this entry already been updated?
 */
static bool updated_this_tick(struct mq_policy *mq, struct entry *e)
{
	return mq->tick == e->tick;
}

/*
 * The promotion threshold is adjusted every generation.  As are the counts
 * of the entries.
 *
 * At the moment the threshold is taken by averaging the hit counts of some
 * of the entries in the cache (the first 20 entries across all levels in
 * ascending order, giving preference to the clean entries at each level).
 *
 * We can be much cleverer than this though.  For example, each promotion
 * could bump up the threshold helping to prevent churn.  Much more to do
 * here.
 */

#define MAX_TO_AVERAGE 20

static void check_generation(struct mq_policy *mq)
{
	unsigned total = 0, nr = 0, count = 0, level;
	struct list_head *head;
	struct entry *e;

	if ((mq->hit_count >= mq->generation_period) && (epool_empty(&mq->cache_pool))) {
		mq->hit_count = 0;
		mq->generation++;

		for (level = 0; level < NR_QUEUE_LEVELS && count < MAX_TO_AVERAGE; level++) {
			head = mq->cache_clean.qs + level;
			list_for_each_entry(e, head, list) {
				nr++;
				total += e->hit_count;

				if (++count >= MAX_TO_AVERAGE)
					break;
			}

			head = mq->cache_dirty.qs + level;
			list_for_each_entry(e, head, list) {
				nr++;
				total += e->hit_count;

				if (++count >= MAX_TO_AVERAGE)
					break;
			}
		}
	}
}

/*
 * Whenever we use an entry we bump up it's hit counter, and push it to the
 * back to it's current level.
 */
static void requeue_and_update_tick(struct mq_policy *mq, struct entry *e)
{
	if (updated_this_tick(mq, e))
		return;

	e->hit_count++;
	mq->hit_count++;
	check_generation(mq);

	/* generation adjustment, to stop the counts increasing forever. */
	/* FIXME: divide? */
	/* e->hit_count -= min(e->hit_count - 1, mq->generation - e->generation); */
	e->generation = mq->generation;

	del(mq, e);
	push(mq, e);
}

/*
 * Demote the least recently used entry from the cache to the pre_cache.
 * Returns the new cache entry to use, and the old origin block it was
 * mapped to.
 *
 * We drop the hit count on the demoted entry back to 1 to stop it bouncing
 * straight back into the cache if it's subsequently hit.  There are
 * various options here, and more experimentation would be good:
 *
 * - just forget about the demoted entry completely (ie. don't insert it
     into the pre_cache).
 * - divide the hit count rather that setting to some hard coded value.
 * - set the hit count to a hard coded value other than 1, eg, is it better
 *   if it goes in at level 2?
 */
static int demote_cblock(struct mq_policy *mq, dm_oblock_t *oblock)
{
	struct entry *demoted = pop(mq, &mq->cache_clean);

	if (!demoted)
		/*
		 * We could get a block from mq->cache_dirty, but that
		 * would add extra latency to the triggering bio as it
		 * waits for the writeback.  Better to not promote this
		 * time and hope there's a clean block next time this block
		 * is hit.
		 */
		return -ENOSPC;

	*oblock = demoted->oblock;
	free_entry(&mq->cache_pool, demoted);

	/*
	 * We used to put the demoted block into the pre-cache, but I think
	 * it's simpler to just let it work it's way up from zero again.
	 * Stops blocks flickering in and out of the cache.
	 */

	return 0;
}

/*
 * Entries in the pre_cache whose hit count passes the promotion
 * threshold move to the cache proper.  Working out the correct
 * value for the promotion_threshold is crucial to this policy.
 */
static unsigned promote_threshold(struct mq_policy *mq)
{
	struct entry *e;

	if (any_free_cblocks(mq))
		return 0;

	e = peek(&mq->cache_clean);
	if (e)
		return e->hit_count;

	e = peek(&mq->cache_dirty);
	if (e)
		return e->hit_count + DISCOURAGE_DEMOTING_DIRTY_THRESHOLD;

	/* This should never happen */
	return 0;
}

/*
 * We modify the basic promotion_threshold depending on the specific io.
 *
 * If the origin block has been discarded then there's no cost to copy it
 * to the cache.
 *
 * We bias towards reads, since they can be demoted at no cost if they
 * haven't been dirtied.
 */
static unsigned adjusted_promote_threshold(struct mq_policy *mq,
					   bool discarded_oblock, int data_dir)
{
	if (data_dir == READ)
		return promote_threshold(mq) + mq->read_promote_adjustment;

	if (discarded_oblock && (any_free_cblocks(mq) || any_clean_cblocks(mq))) {
		/*
		 * We don't need to do any copying at all, so give this a
		 * very low threshold.
		 */
		return mq->discard_promote_adjustment;
	}

	return promote_threshold(mq) + mq->write_promote_adjustment;
}

static bool should_promote(struct mq_policy *mq, struct entry *e,
			   bool discarded_oblock, int data_dir)
{
	return e->hit_count >=
		adjusted_promote_threshold(mq, discarded_oblock, data_dir);
}

static int cache_entry_found(struct mq_policy *mq,
			     struct entry *e,
			     struct policy_result *result)
{
	requeue_and_update_tick(mq, e);

	if (in_cache(mq, e)) {
		result->op = POLICY_HIT;
		result->cblock = infer_cblock(&mq->cache_pool, e);
	}

	return 0;
}

/*
 * Moves an entry from the pre_cache to the cache.  The main work is
 * finding which cache block to use.
 */
static int pre_cache_to_cache(struct mq_policy *mq, struct entry *e,
			      struct policy_result *result)
{
	int r;
	struct entry *new_e;

	/* Ensure there's a free cblock in the cache */
	if (epool_empty(&mq->cache_pool)) {
		result->op = POLICY_REPLACE;
		r = demote_cblock(mq, &result->old_oblock);
		if (r) {
			result->op = POLICY_MISS;
			return 0;
		}
	} else
		result->op = POLICY_NEW;

	new_e = alloc_entry(&mq->cache_pool);
	BUG_ON(!new_e);

	new_e->oblock = e->oblock;
	new_e->dirty = false;
	new_e->hit_count = e->hit_count;
	new_e->generation = e->generation;
	new_e->tick = e->tick;

	del(mq, e);
	free_entry(&mq->pre_cache_pool, e);
	push(mq, new_e);

	result->cblock = infer_cblock(&mq->cache_pool, new_e);

	return 0;
}

static int pre_cache_entry_found(struct mq_policy *mq, struct entry *e,
				 bool can_migrate, bool discarded_oblock,
				 int data_dir, struct policy_result *result)
{
	int r = 0;
	bool updated = updated_this_tick(mq, e);

	if ((!discarded_oblock && updated) ||
	    !should_promote(mq, e, discarded_oblock, data_dir)) {
		requeue_and_update_tick(mq, e);
		result->op = POLICY_MISS;

	} else if (!can_migrate)
		r = -EWOULDBLOCK;

	else {
		requeue_and_update_tick(mq, e);
		r = pre_cache_to_cache(mq, e, result);
	}

	return r;
}

static void insert_in_pre_cache(struct mq_policy *mq,
				dm_oblock_t oblock)
{
	struct entry *e = alloc_entry(&mq->pre_cache_pool);

	if (!e)
		/*
		 * There's no spare entry structure, so we grab the least
		 * used one from the pre_cache.
		 */
		e = pop(mq, &mq->pre_cache);

	if (unlikely(!e)) {
		DMWARN("couldn't pop from pre cache");
		return;
	}

	e->dirty = false;
	e->oblock = oblock;
	e->hit_count = 1;
	e->generation = mq->generation;
	push(mq, e);
}

static void insert_in_cache(struct mq_policy *mq, dm_oblock_t oblock,
			    struct policy_result *result)
{
	int r;
	struct entry *e;

	if (epool_empty(&mq->cache_pool)) {
		result->op = POLICY_REPLACE;
		r = demote_cblock(mq, &result->old_oblock);
		if (unlikely(r)) {
			result->op = POLICY_MISS;
			insert_in_pre_cache(mq, oblock);
			return;
		}

		/*
		 * This will always succeed, since we've just demoted.
		 */
		e = alloc_entry(&mq->cache_pool);
		BUG_ON(!e);

	} else {
		e = alloc_entry(&mq->cache_pool);
		result->op = POLICY_NEW;
	}

	e->oblock = oblock;
	e->dirty = false;
	e->hit_count = 1;
	e->generation = mq->generation;
	push(mq, e);

	result->cblock = infer_cblock(&mq->cache_pool, e);
}

static int no_entry_found(struct mq_policy *mq, dm_oblock_t oblock,
			  bool can_migrate, bool discarded_oblock,
			  int data_dir, struct policy_result *result)
{
	if (adjusted_promote_threshold(mq, discarded_oblock, data_dir) <= 1) {
		if (can_migrate)
			insert_in_cache(mq, oblock, result);
		else
			return -EWOULDBLOCK;
	} else {
		insert_in_pre_cache(mq, oblock);
		result->op = POLICY_MISS;
	}

	return 0;
}

/*
 * Looks the oblock up in the hash table, then decides whether to put in
 * pre_cache, or cache etc.
 */
static int map(struct mq_policy *mq, dm_oblock_t oblock,
	       bool can_migrate, bool discarded_oblock,
	       int data_dir, struct policy_result *result)
{
	int r = 0;
	struct entry *e = hash_lookup(mq, oblock);

	if (e && in_cache(mq, e))
		r = cache_entry_found(mq, e, result);

	else if (mq->tracker.thresholds[PATTERN_SEQUENTIAL] &&
		 iot_pattern(&mq->tracker) == PATTERN_SEQUENTIAL)
		result->op = POLICY_MISS;

	else if (e)
		r = pre_cache_entry_found(mq, e, can_migrate, discarded_oblock,
					  data_dir, result);

	else
		r = no_entry_found(mq, oblock, can_migrate, discarded_oblock,
				   data_dir, result);

	if (r == -EWOULDBLOCK)
		result->op = POLICY_MISS;

	return r;
}

/*----------------------------------------------------------------*/

/*
 * Public interface, via the policy struct.  See dm-cache-policy.h for a
 * description of these.
 */

static struct mq_policy *to_mq_policy(struct dm_cache_policy *p)
{
	return container_of(p, struct mq_policy, policy);
}

static void mq_destroy(struct dm_cache_policy *p)
{
	struct mq_policy *mq = to_mq_policy(p);

	vfree(mq->table);
	epool_exit(&mq->cache_pool);
	epool_exit(&mq->pre_cache_pool);
	kfree(mq);
}

static void copy_tick(struct mq_policy *mq)
{
	unsigned long flags;

	spin_lock_irqsave(&mq->tick_lock, flags);
	mq->tick = mq->tick_protected;
	spin_unlock_irqrestore(&mq->tick_lock, flags);
}

static int mq_map(struct dm_cache_policy *p, dm_oblock_t oblock,
		  bool can_block, bool can_migrate, bool discarded_oblock,
		  struct bio *bio, struct policy_result *result)
{
	int r;
	struct mq_policy *mq = to_mq_policy(p);

	result->op = POLICY_MISS;

	if (can_block)
		mutex_lock(&mq->lock);
	else if (!mutex_trylock(&mq->lock))
		return -EWOULDBLOCK;

	copy_tick(mq);

	iot_examine_bio(&mq->tracker, bio);
	r = map(mq, oblock, can_migrate, discarded_oblock,
		bio_data_dir(bio), result);

	mutex_unlock(&mq->lock);

	return r;
}

static int mq_lookup(struct dm_cache_policy *p, dm_oblock_t oblock, dm_cblock_t *cblock)
{
	int r;
	struct mq_policy *mq = to_mq_policy(p);
	struct entry *e;

	if (!mutex_trylock(&mq->lock))
		return -EWOULDBLOCK;

	e = hash_lookup(mq, oblock);
	if (e && in_cache(mq, e)) {
		*cblock = infer_cblock(&mq->cache_pool, e);
		r = 0;
	} else
		r = -ENOENT;

	mutex_unlock(&mq->lock);

	return r;
}

static void __mq_set_clear_dirty(struct mq_policy *mq, dm_oblock_t oblock, bool set)
{
	struct entry *e;

	e = hash_lookup(mq, oblock);
	BUG_ON(!e || !in_cache(mq, e));

	del(mq, e);
	e->dirty = set;
	push(mq, e);
}

static void mq_set_dirty(struct dm_cache_policy *p, dm_oblock_t oblock)
{
	struct mq_policy *mq = to_mq_policy(p);

	mutex_lock(&mq->lock);
	__mq_set_clear_dirty(mq, oblock, true);
	mutex_unlock(&mq->lock);
}

static void mq_clear_dirty(struct dm_cache_policy *p, dm_oblock_t oblock)
{
	struct mq_policy *mq = to_mq_policy(p);

	mutex_lock(&mq->lock);
	__mq_set_clear_dirty(mq, oblock, false);
	mutex_unlock(&mq->lock);
}

static int mq_load_mapping(struct dm_cache_policy *p,
			   dm_oblock_t oblock, dm_cblock_t cblock,
			   uint32_t hint, bool hint_valid)
{
	struct mq_policy *mq = to_mq_policy(p);
	struct entry *e;

	e = alloc_particular_entry(&mq->cache_pool, cblock);
	e->oblock = oblock;
	e->dirty = false;	/* this gets corrected in a minute */
	e->hit_count = hint_valid ? hint : 1;
	e->generation = mq->generation;
	push(mq, e);

	return 0;
}

static int mq_save_hints(struct mq_policy *mq, struct queue *q,
			 policy_walk_fn fn, void *context)
{
	int r;
	unsigned level;
	struct entry *e;

	for (level = 0; level < NR_QUEUE_LEVELS; level++)
		list_for_each_entry(e, q->qs + level, list) {
			r = fn(context, infer_cblock(&mq->cache_pool, e),
			       e->oblock, e->hit_count);
			if (r)
				return r;
		}

	return 0;
}

static int mq_walk_mappings(struct dm_cache_policy *p, policy_walk_fn fn,
			    void *context)
{
	struct mq_policy *mq = to_mq_policy(p);
	int r = 0;

	mutex_lock(&mq->lock);

	r = mq_save_hints(mq, &mq->cache_clean, fn, context);
	if (!r)
		r = mq_save_hints(mq, &mq->cache_dirty, fn, context);

	mutex_unlock(&mq->lock);

	return r;
}

static void __remove_mapping(struct mq_policy *mq, dm_oblock_t oblock)
{
	struct entry *e;

	e = hash_lookup(mq, oblock);
	BUG_ON(!e || !in_cache(mq, e));

	del(mq, e);
	free_entry(&mq->cache_pool, e);
}

static void mq_remove_mapping(struct dm_cache_policy *p, dm_oblock_t oblock)
{
	struct mq_policy *mq = to_mq_policy(p);

	mutex_lock(&mq->lock);
	__remove_mapping(mq, oblock);
	mutex_unlock(&mq->lock);
}

static int __remove_cblock(struct mq_policy *mq, dm_cblock_t cblock)
{
	struct entry *e = epool_find(&mq->cache_pool, cblock);

	if (!e)
		return -ENODATA;

	del(mq, e);
	free_entry(&mq->cache_pool, e);

	return 0;
}

static int mq_remove_cblock(struct dm_cache_policy *p, dm_cblock_t cblock)
{
	int r;
	struct mq_policy *mq = to_mq_policy(p);

	mutex_lock(&mq->lock);
	r = __remove_cblock(mq, cblock);
	mutex_unlock(&mq->lock);

	return r;
}

static int __mq_writeback_work(struct mq_policy *mq, dm_oblock_t *oblock,
			      dm_cblock_t *cblock)
{
	struct entry *e = pop(mq, &mq->cache_dirty);

	if (!e)
		return -ENODATA;

	*oblock = e->oblock;
	*cblock = infer_cblock(&mq->cache_pool, e);
	e->dirty = false;
	push(mq, e);

	return 0;
}

static int mq_writeback_work(struct dm_cache_policy *p, dm_oblock_t *oblock,
			     dm_cblock_t *cblock)
{
	int r;
	struct mq_policy *mq = to_mq_policy(p);

	mutex_lock(&mq->lock);
	r = __mq_writeback_work(mq, oblock, cblock);
	mutex_unlock(&mq->lock);

	return r;
}

static void __force_mapping(struct mq_policy *mq,
			    dm_oblock_t current_oblock, dm_oblock_t new_oblock)
{
	struct entry *e = hash_lookup(mq, current_oblock);

	if (e && in_cache(mq, e)) {
		del(mq, e);
		e->oblock = new_oblock;
		e->dirty = true;
		push(mq, e);
	}
}

static void mq_force_mapping(struct dm_cache_policy *p,
			     dm_oblock_t current_oblock, dm_oblock_t new_oblock)
{
	struct mq_policy *mq = to_mq_policy(p);

	mutex_lock(&mq->lock);
	__force_mapping(mq, current_oblock, new_oblock);
	mutex_unlock(&mq->lock);
}

static dm_cblock_t mq_residency(struct dm_cache_policy *p)
{
	dm_cblock_t r;
	struct mq_policy *mq = to_mq_policy(p);

	mutex_lock(&mq->lock);
	r = to_cblock(mq->cache_pool.nr_allocated);
	mutex_unlock(&mq->lock);

	return r;
}

static void mq_tick(struct dm_cache_policy *p)
{
	struct mq_policy *mq = to_mq_policy(p);
	unsigned long flags;

	spin_lock_irqsave(&mq->tick_lock, flags);
	mq->tick_protected++;
	spin_unlock_irqrestore(&mq->tick_lock, flags);
}

static int mq_set_config_value(struct dm_cache_policy *p,
			       const char *key, const char *value)
{
	struct mq_policy *mq = to_mq_policy(p);
	unsigned long tmp;

	if (kstrtoul(value, 10, &tmp))
		return -EINVAL;

	if (!strcasecmp(key, "random_threshold")) {
		mq->tracker.thresholds[PATTERN_RANDOM] = tmp;

	} else if (!strcasecmp(key, "sequential_threshold")) {
		mq->tracker.thresholds[PATTERN_SEQUENTIAL] = tmp;

	} else if (!strcasecmp(key, "discard_promote_adjustment"))
		mq->discard_promote_adjustment = tmp;

	else if (!strcasecmp(key, "read_promote_adjustment"))
		mq->read_promote_adjustment = tmp;

	else if (!strcasecmp(key, "write_promote_adjustment"))
		mq->write_promote_adjustment = tmp;

	else
		return -EINVAL;

	return 0;
}

static int mq_emit_config_values(struct dm_cache_policy *p, char *result, unsigned maxlen)
{
	ssize_t sz = 0;
	struct mq_policy *mq = to_mq_policy(p);

	DMEMIT("10 random_threshold %u "
	       "sequential_threshold %u "
	       "discard_promote_adjustment %u "
	       "read_promote_adjustment %u "
	       "write_promote_adjustment %u",
	       mq->tracker.thresholds[PATTERN_RANDOM],
	       mq->tracker.thresholds[PATTERN_SEQUENTIAL],
	       mq->discard_promote_adjustment,
	       mq->read_promote_adjustment,
	       mq->write_promote_adjustment);

	return 0;
}

/* Init the policy plugin interface function pointers. */
static void init_policy_functions(struct mq_policy *mq)
{
	mq->policy.destroy = mq_destroy;
	mq->policy.map = mq_map;
	mq->policy.lookup = mq_lookup;
	mq->policy.set_dirty = mq_set_dirty;
	mq->policy.clear_dirty = mq_clear_dirty;
	mq->policy.load_mapping = mq_load_mapping;
	mq->policy.walk_mappings = mq_walk_mappings;
	mq->policy.remove_mapping = mq_remove_mapping;
	mq->policy.remove_cblock = mq_remove_cblock;
	mq->policy.writeback_work = mq_writeback_work;
	mq->policy.force_mapping = mq_force_mapping;
	mq->policy.residency = mq_residency;
	mq->policy.tick = mq_tick;
	mq->policy.emit_config_values = mq_emit_config_values;
	mq->policy.set_config_value = mq_set_config_value;
}

static struct dm_cache_policy *mq_create(dm_cblock_t cache_size,
					 sector_t origin_size,
					 sector_t cache_block_size)
{
	struct mq_policy *mq = kzalloc(sizeof(*mq), GFP_KERNEL);

	if (!mq)
		return NULL;

	init_policy_functions(mq);
	iot_init(&mq->tracker, SEQUENTIAL_THRESHOLD_DEFAULT, RANDOM_THRESHOLD_DEFAULT);
	mq->cache_size = cache_size;

	if (epool_init(&mq->pre_cache_pool, from_cblock(cache_size))) {
		DMERR("couldn't initialize pool of pre-cache entries");
		goto bad_pre_cache_init;
	}

	if (epool_init(&mq->cache_pool, from_cblock(cache_size))) {
		DMERR("couldn't initialize pool of cache entries");
		goto bad_cache_init;
	}

	mq->tick_protected = 0;
	mq->tick = 0;
	mq->hit_count = 0;
	mq->generation = 0;
	mq->discard_promote_adjustment = DEFAULT_DISCARD_PROMOTE_ADJUSTMENT;
	mq->read_promote_adjustment = DEFAULT_READ_PROMOTE_ADJUSTMENT;
	mq->write_promote_adjustment = DEFAULT_WRITE_PROMOTE_ADJUSTMENT;
	mutex_init(&mq->lock);
	spin_lock_init(&mq->tick_lock);

	queue_init(&mq->pre_cache);
	queue_init(&mq->cache_clean);
	queue_init(&mq->cache_dirty);

	mq->generation_period = max((unsigned) from_cblock(cache_size), 1024U);

	mq->nr_buckets = next_power(from_cblock(cache_size) / 2, 16);
	mq->hash_bits = ffs(mq->nr_buckets) - 1;
	mq->table = vzalloc(sizeof(*mq->table) * mq->nr_buckets);
	if (!mq->table)
		goto bad_alloc_table;

	return &mq->policy;

bad_alloc_table:
	epool_exit(&mq->cache_pool);
bad_cache_init:
	epool_exit(&mq->pre_cache_pool);
bad_pre_cache_init:
	kfree(mq);

	return NULL;
}

/*----------------------------------------------------------------*/

static struct dm_cache_policy_type mq_policy_type = {
	.name = "mq",
	.version = {1, 3, 0},
	.hint_size = 4,
	.owner = THIS_MODULE,
	.create = mq_create
};

static struct dm_cache_policy_type default_policy_type = {
	.name = "default",
	.version = {1, 3, 0},
	.hint_size = 4,
	.owner = THIS_MODULE,
	.create = mq_create,
	.real = &mq_policy_type
};

static int __init mq_init(void)
{
	int r;

	mq_entry_cache = kmem_cache_create("dm_mq_policy_cache_entry",
					   sizeof(struct entry),
					   __alignof__(struct entry),
					   0, NULL);
	if (!mq_entry_cache)
		goto bad;

	r = dm_cache_policy_register(&mq_policy_type);
	if (r) {
		DMERR("register failed %d", r);
		goto bad_register_mq;
	}

	r = dm_cache_policy_register(&default_policy_type);
	if (!r) {
		DMINFO("version %u.%u.%u loaded",
		       mq_policy_type.version[0],
		       mq_policy_type.version[1],
		       mq_policy_type.version[2]);
		return 0;
	}

	DMERR("register failed (as default) %d", r);

	dm_cache_policy_unregister(&mq_policy_type);
bad_register_mq:
	kmem_cache_destroy(mq_entry_cache);
bad:
	return -ENOMEM;
}

static void __exit mq_exit(void)
{
	dm_cache_policy_unregister(&mq_policy_type);
	dm_cache_policy_unregister(&default_policy_type);

	kmem_cache_destroy(mq_entry_cache);
}

module_init(mq_init);
module_exit(mq_exit);

MODULE_AUTHOR("Joe Thornber <dm-devel@redhat.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("mq cache policy");

MODULE_ALIAS("dm-cache-default");
