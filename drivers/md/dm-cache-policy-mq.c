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

static unsigned long *alloc_bitset(unsigned nr_entries)
{
	size_t s = sizeof(unsigned long) * dm_div_up(nr_entries, BITS_PER_LONG);
	return vzalloc(s);
}

static void free_bitset(unsigned long *bits)
{
	vfree(bits);
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
	if (bio->bi_sector == from_oblock(t->last_end_oblock) + 1)
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

	t->last_end_oblock = to_oblock(bio->bi_sector + bio_sectors(bio) - 1);
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
static struct list_head *queue_pop(struct queue *q)
{
	unsigned level;
	struct list_head *r;

	for (level = 0; level < NR_QUEUE_LEVELS; level++)
		if (!list_empty(q->qs + level)) {
			r = q->qs[level].next;
			list_del(r);

			/* have we just emptied the bottom level? */
			if (level == 0 && list_empty(q->qs))
				queue_shift_down(q);

			return r;
		}

	return NULL;
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
	dm_cblock_t cblock;	/* valid iff in_cache */

	/*
	 * FIXME: pack these better
	 */
	bool in_cache:1;
	unsigned hit_count;
	unsigned generation;
	unsigned tick;
};

struct mq_policy {
	struct dm_cache_policy policy;

	/* protects everything */
	struct mutex lock;
	dm_cblock_t cache_size;
	struct io_tracker tracker;

	/*
	 * We maintain two queues of entries.  The cache proper contains
	 * the currently active mappings.  Whereas the pre_cache tracks
	 * blocks that are being hit frequently and potential candidates
	 * for promotion to the cache.
	 */
	struct queue pre_cache;
	struct queue cache;

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

	/*
	 * Entries in the pre_cache whose hit count passes the promotion
	 * threshold move to the cache proper.  Working out the correct
	 * value for the promotion_threshold is crucial to this policy.
	 */
	unsigned promote_threshold;

	/*
	 * We need cache_size entries for the cache, and choose to have
	 * cache_size entries for the pre_cache too.  One motivation for
	 * using the same size is to make the hit counts directly
	 * comparable between pre_cache and cache.
	 */
	unsigned nr_entries;
	unsigned nr_entries_allocated;
	struct list_head free;

	/*
	 * Cache blocks may be unallocated.  We store this info in a
	 * bitset.
	 */
	unsigned long *allocation_bitset;
	unsigned nr_cblocks_allocated;
	unsigned find_free_nr_words;
	unsigned find_free_last_word;

	/*
	 * The hash table allows us to quickly find an entry by origin
	 * block.  Both pre_cache and cache entries are in here.
	 */
	unsigned nr_buckets;
	dm_block_t hash_bits;
	struct hlist_head *table;
};

/*----------------------------------------------------------------*/
/* Free/alloc mq cache entry structures. */
static void takeout_queue(struct list_head *lh, struct queue *q)
{
	unsigned level;

	for (level = 0; level < NR_QUEUE_LEVELS; level++)
		list_splice(q->qs + level, lh);
}

static void free_entries(struct mq_policy *mq)
{
	struct entry *e, *tmp;

	takeout_queue(&mq->free, &mq->pre_cache);
	takeout_queue(&mq->free, &mq->cache);

	list_for_each_entry_safe(e, tmp, &mq->free, list)
		kmem_cache_free(mq_entry_cache, e);
}

static int alloc_entries(struct mq_policy *mq, unsigned elts)
{
	unsigned u = mq->nr_entries;

	INIT_LIST_HEAD(&mq->free);
	mq->nr_entries_allocated = 0;

	while (u--) {
		struct entry *e = kmem_cache_zalloc(mq_entry_cache, GFP_KERNEL);

		if (!e) {
			free_entries(mq);
			return -ENOMEM;
		}


		list_add(&e->list, &mq->free);
	}

	return 0;
}

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

/*
 * Allocates a new entry structure.  The memory is allocated in one lump,
 * so we just handing it out here.  Returns NULL if all entries have
 * already been allocated.  Cannot fail otherwise.
 */
static struct entry *alloc_entry(struct mq_policy *mq)
{
	struct entry *e;

	if (mq->nr_entries_allocated >= mq->nr_entries) {
		BUG_ON(!list_empty(&mq->free));
		return NULL;
	}

	e = list_entry(list_pop(&mq->free), struct entry, list);
	INIT_LIST_HEAD(&e->list);
	INIT_HLIST_NODE(&e->hlist);

	mq->nr_entries_allocated++;
	return e;
}

/*----------------------------------------------------------------*/

/*
 * Mark cache blocks allocated or not in the bitset.
 */
static void alloc_cblock(struct mq_policy *mq, dm_cblock_t cblock)
{
	BUG_ON(from_cblock(cblock) > from_cblock(mq->cache_size));
	BUG_ON(test_bit(from_cblock(cblock), mq->allocation_bitset));

	set_bit(from_cblock(cblock), mq->allocation_bitset);
	mq->nr_cblocks_allocated++;
}

static void free_cblock(struct mq_policy *mq, dm_cblock_t cblock)
{
	BUG_ON(from_cblock(cblock) > from_cblock(mq->cache_size));
	BUG_ON(!test_bit(from_cblock(cblock), mq->allocation_bitset));

	clear_bit(from_cblock(cblock), mq->allocation_bitset);
	mq->nr_cblocks_allocated--;
}

static bool any_free_cblocks(struct mq_policy *mq)
{
	return mq->nr_cblocks_allocated < from_cblock(mq->cache_size);
}

/*
 * Fills result out with a cache block that isn't in use, or return
 * -ENOSPC.  This does _not_ mark the cblock as allocated, the caller is
 * reponsible for that.
 */
static int __find_free_cblock(struct mq_policy *mq, unsigned begin, unsigned end,
			      dm_cblock_t *result, unsigned *last_word)
{
	int r = -ENOSPC;
	unsigned w;

	for (w = begin; w < end; w++) {
		/*
		 * ffz is undefined if no zero exists
		 */
		if (mq->allocation_bitset[w] != ~0UL) {
			*last_word = w;
			*result = to_cblock((w * BITS_PER_LONG) + ffz(mq->allocation_bitset[w]));
			if (from_cblock(*result) < from_cblock(mq->cache_size))
				r = 0;

			break;
		}
	}

	return r;
}

static int find_free_cblock(struct mq_policy *mq, dm_cblock_t *result)
{
	int r;

	if (!any_free_cblocks(mq))
		return -ENOSPC;

	r = __find_free_cblock(mq, mq->find_free_last_word, mq->find_free_nr_words, result, &mq->find_free_last_word);
	if (r == -ENOSPC && mq->find_free_last_word)
		r = __find_free_cblock(mq, 0, mq->find_free_last_word, result, &mq->find_free_last_word);

	return r;
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

/*
 * Inserts the entry into the pre_cache or the cache.  Ensures the cache
 * block is marked as allocated if necc.  Inserts into the hash table.  Sets the
 * tick which records when the entry was last moved about.
 */
static void push(struct mq_policy *mq, struct entry *e)
{
	e->tick = mq->tick;
	hash_insert(mq, e);

	if (e->in_cache) {
		alloc_cblock(mq, e->cblock);
		queue_push(&mq->cache, queue_level(e), &e->list);
	} else
		queue_push(&mq->pre_cache, queue_level(e), &e->list);
}

/*
 * Removes an entry from pre_cache or cache.  Removes from the hash table.
 * Frees off the cache block if necc.
 */
static void del(struct mq_policy *mq, struct entry *e)
{
	queue_remove(&e->list);
	hash_remove(e);
	if (e->in_cache)
		free_cblock(mq, e->cblock);
}

/*
 * Like del, except it removes the first entry in the queue (ie. the least
 * recently used).
 */
static struct entry *pop(struct mq_policy *mq, struct queue *q)
{
	struct entry *e = container_of(queue_pop(q), struct entry, list);

	if (e) {
		hash_remove(e);

		if (e->in_cache)
			free_cblock(mq, e->cblock);
	}

	return e;
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
 * of the entries in the cache (the first 20 entries of the first level).
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

	if ((mq->hit_count >= mq->generation_period) &&
	    (mq->nr_cblocks_allocated == from_cblock(mq->cache_size))) {

		mq->hit_count = 0;
		mq->generation++;

		for (level = 0; level < NR_QUEUE_LEVELS && count < MAX_TO_AVERAGE; level++) {
			head = mq->cache.qs + level;
			list_for_each_entry(e, head, list) {
				nr++;
				total += e->hit_count;

				if (++count >= MAX_TO_AVERAGE)
					break;
			}
		}

		mq->promote_threshold = nr ? total / nr : 1;
		if (mq->promote_threshold * nr < total)
			mq->promote_threshold++;
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
static dm_cblock_t demote_cblock(struct mq_policy *mq, dm_oblock_t *oblock)
{
	dm_cblock_t result;
	struct entry *demoted = pop(mq, &mq->cache);

	BUG_ON(!demoted);
	result = demoted->cblock;
	*oblock = demoted->oblock;
	demoted->in_cache = false;
	demoted->hit_count = 1;
	push(mq, demoted);

	return result;
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
#define DISCARDED_PROMOTE_THRESHOLD 1
#define READ_PROMOTE_THRESHOLD 4
#define WRITE_PROMOTE_THRESHOLD 8

static unsigned adjusted_promote_threshold(struct mq_policy *mq,
					   bool discarded_oblock, int data_dir)
{
	if (discarded_oblock && any_free_cblocks(mq) && data_dir == WRITE)
		/*
		 * We don't need to do any copying at all, so give this a
		 * very low threshold.  In practice this only triggers
		 * during initial population after a format.
		 */
		return DISCARDED_PROMOTE_THRESHOLD;

	return data_dir == READ ?
		(mq->promote_threshold + READ_PROMOTE_THRESHOLD) :
		(mq->promote_threshold + WRITE_PROMOTE_THRESHOLD);
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

	if (e->in_cache) {
		result->op = POLICY_HIT;
		result->cblock = e->cblock;
	}

	return 0;
}

/*
 * Moves and entry from the pre_cache to the cache.  The main work is
 * finding which cache block to use.
 */
static int pre_cache_to_cache(struct mq_policy *mq, struct entry *e,
			      struct policy_result *result)
{
	dm_cblock_t cblock;

	if (find_free_cblock(mq, &cblock) == -ENOSPC) {
		result->op = POLICY_REPLACE;
		cblock = demote_cblock(mq, &result->old_oblock);
	} else
		result->op = POLICY_NEW;

	result->cblock = e->cblock = cblock;

	del(mq, e);
	e->in_cache = true;
	push(mq, e);

	return 0;
}

static int pre_cache_entry_found(struct mq_policy *mq, struct entry *e,
				 bool can_migrate, bool discarded_oblock,
				 int data_dir, struct policy_result *result)
{
	int r = 0;
	bool updated = updated_this_tick(mq, e);

	requeue_and_update_tick(mq, e);

	if ((!discarded_oblock && updated) ||
	    !should_promote(mq, e, discarded_oblock, data_dir))
		result->op = POLICY_MISS;
	else if (!can_migrate)
		r = -EWOULDBLOCK;
	else
		r = pre_cache_to_cache(mq, e, result);

	return r;
}

static void insert_in_pre_cache(struct mq_policy *mq,
				dm_oblock_t oblock)
{
	struct entry *e = alloc_entry(mq);

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

	e->in_cache = false;
	e->oblock = oblock;
	e->hit_count = 1;
	e->generation = mq->generation;
	push(mq, e);
}

static void insert_in_cache(struct mq_policy *mq, dm_oblock_t oblock,
			    struct policy_result *result)
{
	struct entry *e;
	dm_cblock_t cblock;

	if (find_free_cblock(mq, &cblock) == -ENOSPC) {
		result->op = POLICY_MISS;
		insert_in_pre_cache(mq, oblock);
		return;
	}

	e = alloc_entry(mq);
	if (unlikely(!e)) {
		result->op = POLICY_MISS;
		return;
	}

	e->oblock = oblock;
	e->cblock = cblock;
	e->in_cache = true;
	e->hit_count = 1;
	e->generation = mq->generation;
	push(mq, e);

	result->op = POLICY_NEW;
	result->cblock = e->cblock;
}

static int no_entry_found(struct mq_policy *mq, dm_oblock_t oblock,
			  bool can_migrate, bool discarded_oblock,
			  int data_dir, struct policy_result *result)
{
	if (adjusted_promote_threshold(mq, discarded_oblock, data_dir) == 1) {
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

	if (e && e->in_cache)
		r = cache_entry_found(mq, e, result);
	else if (iot_pattern(&mq->tracker) == PATTERN_SEQUENTIAL)
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

	free_bitset(mq->allocation_bitset);
	kfree(mq->table);
	free_entries(mq);
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
	if (e && e->in_cache) {
		*cblock = e->cblock;
		r = 0;
	} else
		r = -ENOENT;

	mutex_unlock(&mq->lock);

	return r;
}

static int mq_load_mapping(struct dm_cache_policy *p,
			   dm_oblock_t oblock, dm_cblock_t cblock,
			   uint32_t hint, bool hint_valid)
{
	struct mq_policy *mq = to_mq_policy(p);
	struct entry *e;

	e = alloc_entry(mq);
	if (!e)
		return -ENOMEM;

	e->cblock = cblock;
	e->oblock = oblock;
	e->in_cache = true;
	e->hit_count = hint_valid ? hint : 1;
	e->generation = mq->generation;
	push(mq, e);

	return 0;
}

static int mq_walk_mappings(struct dm_cache_policy *p, policy_walk_fn fn,
			    void *context)
{
	struct mq_policy *mq = to_mq_policy(p);
	int r = 0;
	struct entry *e;
	unsigned level;

	mutex_lock(&mq->lock);

	for (level = 0; level < NR_QUEUE_LEVELS; level++)
		list_for_each_entry(e, &mq->cache.qs[level], list) {
			r = fn(context, e->cblock, e->oblock, e->hit_count);
			if (r)
				goto out;
		}

out:
	mutex_unlock(&mq->lock);

	return r;
}

static void mq_remove_mapping(struct dm_cache_policy *p, dm_oblock_t oblock)
{
	struct mq_policy *mq = to_mq_policy(p);
	struct entry *e;

	mutex_lock(&mq->lock);

	e = hash_lookup(mq, oblock);

	BUG_ON(!e || !e->in_cache);

	del(mq, e);
	e->in_cache = false;
	push(mq, e);

	mutex_unlock(&mq->lock);
}

static void force_mapping(struct mq_policy *mq,
			  dm_oblock_t current_oblock, dm_oblock_t new_oblock)
{
	struct entry *e = hash_lookup(mq, current_oblock);

	BUG_ON(!e || !e->in_cache);

	del(mq, e);
	e->oblock = new_oblock;
	push(mq, e);
}

static void mq_force_mapping(struct dm_cache_policy *p,
			     dm_oblock_t current_oblock, dm_oblock_t new_oblock)
{
	struct mq_policy *mq = to_mq_policy(p);

	mutex_lock(&mq->lock);
	force_mapping(mq, current_oblock, new_oblock);
	mutex_unlock(&mq->lock);
}

static dm_cblock_t mq_residency(struct dm_cache_policy *p)
{
	struct mq_policy *mq = to_mq_policy(p);

	/* FIXME: lock mutex, not sure we can block here */
	return to_cblock(mq->nr_cblocks_allocated);
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
	enum io_pattern pattern;
	unsigned long tmp;

	if (!strcasecmp(key, "random_threshold"))
		pattern = PATTERN_RANDOM;
	else if (!strcasecmp(key, "sequential_threshold"))
		pattern = PATTERN_SEQUENTIAL;
	else
		return -EINVAL;

	if (kstrtoul(value, 10, &tmp))
		return -EINVAL;

	mq->tracker.thresholds[pattern] = tmp;

	return 0;
}

static int mq_emit_config_values(struct dm_cache_policy *p, char *result, unsigned maxlen)
{
	ssize_t sz = 0;
	struct mq_policy *mq = to_mq_policy(p);

	DMEMIT("4 random_threshold %u sequential_threshold %u",
	       mq->tracker.thresholds[PATTERN_RANDOM],
	       mq->tracker.thresholds[PATTERN_SEQUENTIAL]);

	return 0;
}

/* Init the policy plugin interface function pointers. */
static void init_policy_functions(struct mq_policy *mq)
{
	mq->policy.destroy = mq_destroy;
	mq->policy.map = mq_map;
	mq->policy.lookup = mq_lookup;
	mq->policy.load_mapping = mq_load_mapping;
	mq->policy.walk_mappings = mq_walk_mappings;
	mq->policy.remove_mapping = mq_remove_mapping;
	mq->policy.writeback_work = NULL;
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
	int r;
	struct mq_policy *mq = kzalloc(sizeof(*mq), GFP_KERNEL);

	if (!mq)
		return NULL;

	init_policy_functions(mq);
	iot_init(&mq->tracker, SEQUENTIAL_THRESHOLD_DEFAULT, RANDOM_THRESHOLD_DEFAULT);

	mq->cache_size = cache_size;
	mq->tick_protected = 0;
	mq->tick = 0;
	mq->hit_count = 0;
	mq->generation = 0;
	mq->promote_threshold = 0;
	mutex_init(&mq->lock);
	spin_lock_init(&mq->tick_lock);
	mq->find_free_nr_words = dm_div_up(from_cblock(mq->cache_size), BITS_PER_LONG);
	mq->find_free_last_word = 0;

	queue_init(&mq->pre_cache);
	queue_init(&mq->cache);
	mq->generation_period = max((unsigned) from_cblock(cache_size), 1024U);

	mq->nr_entries = 2 * from_cblock(cache_size);
	r = alloc_entries(mq, mq->nr_entries);
	if (r)
		goto bad_cache_alloc;

	mq->nr_entries_allocated = 0;
	mq->nr_cblocks_allocated = 0;

	mq->nr_buckets = next_power(from_cblock(cache_size) / 2, 16);
	mq->hash_bits = ffs(mq->nr_buckets) - 1;
	mq->table = kzalloc(sizeof(*mq->table) * mq->nr_buckets, GFP_KERNEL);
	if (!mq->table)
		goto bad_alloc_table;

	mq->allocation_bitset = alloc_bitset(from_cblock(cache_size));
	if (!mq->allocation_bitset)
		goto bad_alloc_bitset;

	return &mq->policy;

bad_alloc_bitset:
	kfree(mq->table);
bad_alloc_table:
	free_entries(mq);
bad_cache_alloc:
	kfree(mq);

	return NULL;
}

/*----------------------------------------------------------------*/

static struct dm_cache_policy_type mq_policy_type = {
	.name = "mq",
	.version = {1, 0, 0},
	.hint_size = 4,
	.owner = THIS_MODULE,
	.create = mq_create
};

static struct dm_cache_policy_type default_policy_type = {
	.name = "default",
	.version = {1, 0, 0},
	.hint_size = 4,
	.owner = THIS_MODULE,
	.create = mq_create
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
