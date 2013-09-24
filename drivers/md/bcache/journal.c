/*
 * bcache journalling code, for btree insertions
 *
 * Copyright 2012 Google, Inc.
 */

#include "bcache.h"
#include "btree.h"
#include "debug.h"
#include "request.h"

#include <trace/events/bcache.h>

/*
 * Journal replay/recovery:
 *
 * This code is all driven from run_cache_set(); we first read the journal
 * entries, do some other stuff, then we mark all the keys in the journal
 * entries (same as garbage collection would), then we replay them - reinserting
 * them into the cache in precisely the same order as they appear in the
 * journal.
 *
 * We only journal keys that go in leaf nodes, which simplifies things quite a
 * bit.
 */

static void journal_read_endio(struct bio *bio, int error)
{
	struct closure *cl = bio->bi_private;
	closure_put(cl);
}

static int journal_read_bucket(struct cache *ca, struct list_head *list,
			       struct btree_op *op, unsigned bucket_index)
{
	struct journal_device *ja = &ca->journal;
	struct bio *bio = &ja->bio;

	struct journal_replay *i;
	struct jset *j, *data = ca->set->journal.w[0].data;
	unsigned len, left, offset = 0;
	int ret = 0;
	sector_t bucket = bucket_to_sector(ca->set, ca->sb.d[bucket_index]);

	pr_debug("reading %llu", (uint64_t) bucket);

	while (offset < ca->sb.bucket_size) {
reread:		left = ca->sb.bucket_size - offset;
		len = min_t(unsigned, left, PAGE_SECTORS * 8);

		bio_reset(bio);
		bio->bi_sector	= bucket + offset;
		bio->bi_bdev	= ca->bdev;
		bio->bi_rw	= READ;
		bio->bi_size	= len << 9;

		bio->bi_end_io	= journal_read_endio;
		bio->bi_private = &op->cl;
		bch_bio_map(bio, data);

		closure_bio_submit(bio, &op->cl, ca);
		closure_sync(&op->cl);

		/* This function could be simpler now since we no longer write
		 * journal entries that overlap bucket boundaries; this means
		 * the start of a bucket will always have a valid journal entry
		 * if it has any journal entries at all.
		 */

		j = data;
		while (len) {
			struct list_head *where;
			size_t blocks, bytes = set_bytes(j);

			if (j->magic != jset_magic(ca->set))
				return ret;

			if (bytes > left << 9)
				return ret;

			if (bytes > len << 9)
				goto reread;

			if (j->csum != csum_set(j))
				return ret;

			blocks = set_blocks(j, ca->set);

			while (!list_empty(list)) {
				i = list_first_entry(list,
					struct journal_replay, list);
				if (i->j.seq >= j->last_seq)
					break;
				list_del(&i->list);
				kfree(i);
			}

			list_for_each_entry_reverse(i, list, list) {
				if (j->seq == i->j.seq)
					goto next_set;

				if (j->seq < i->j.last_seq)
					goto next_set;

				if (j->seq > i->j.seq) {
					where = &i->list;
					goto add;
				}
			}

			where = list;
add:
			i = kmalloc(offsetof(struct journal_replay, j) +
				    bytes, GFP_KERNEL);
			if (!i)
				return -ENOMEM;
			memcpy(&i->j, j, bytes);
			list_add(&i->list, where);
			ret = 1;

			ja->seq[bucket_index] = j->seq;
next_set:
			offset	+= blocks * ca->sb.block_size;
			len	-= blocks * ca->sb.block_size;
			j = ((void *) j) + blocks * block_bytes(ca);
		}
	}

	return ret;
}

int bch_journal_read(struct cache_set *c, struct list_head *list,
			struct btree_op *op)
{
#define read_bucket(b)							\
	({								\
		int ret = journal_read_bucket(ca, list, op, b);		\
		__set_bit(b, bitmap);					\
		if (ret < 0)						\
			return ret;					\
		ret;							\
	})

	struct cache *ca;
	unsigned iter;

	for_each_cache(ca, c, iter) {
		struct journal_device *ja = &ca->journal;
		unsigned long bitmap[SB_JOURNAL_BUCKETS / BITS_PER_LONG];
		unsigned i, l, r, m;
		uint64_t seq;

		bitmap_zero(bitmap, SB_JOURNAL_BUCKETS);
		pr_debug("%u journal buckets", ca->sb.njournal_buckets);

		/*
		 * Read journal buckets ordered by golden ratio hash to quickly
		 * find a sequence of buckets with valid journal entries
		 */
		for (i = 0; i < ca->sb.njournal_buckets; i++) {
			l = (i * 2654435769U) % ca->sb.njournal_buckets;

			if (test_bit(l, bitmap))
				break;

			if (read_bucket(l))
				goto bsearch;
		}

		/*
		 * If that fails, check all the buckets we haven't checked
		 * already
		 */
		pr_debug("falling back to linear search");

		for (l = find_first_zero_bit(bitmap, ca->sb.njournal_buckets);
		     l < ca->sb.njournal_buckets;
		     l = find_next_zero_bit(bitmap, ca->sb.njournal_buckets, l + 1))
			if (read_bucket(l))
				goto bsearch;

		if (list_empty(list))
			continue;
bsearch:
		/* Binary search */
		m = r = find_next_bit(bitmap, ca->sb.njournal_buckets, l + 1);
		pr_debug("starting binary search, l %u r %u", l, r);

		while (l + 1 < r) {
			seq = list_entry(list->prev, struct journal_replay,
					 list)->j.seq;

			m = (l + r) >> 1;
			read_bucket(m);

			if (seq != list_entry(list->prev, struct journal_replay,
					      list)->j.seq)
				l = m;
			else
				r = m;
		}

		/*
		 * Read buckets in reverse order until we stop finding more
		 * journal entries
		 */
		pr_debug("finishing up: m %u njournal_buckets %u",
			 m, ca->sb.njournal_buckets);
		l = m;

		while (1) {
			if (!l--)
				l = ca->sb.njournal_buckets - 1;

			if (l == m)
				break;

			if (test_bit(l, bitmap))
				continue;

			if (!read_bucket(l))
				break;
		}

		seq = 0;

		for (i = 0; i < ca->sb.njournal_buckets; i++)
			if (ja->seq[i] > seq) {
				seq = ja->seq[i];
				ja->cur_idx = ja->discard_idx =
					ja->last_idx = i;

			}
	}

	if (!list_empty(list))
		c->journal.seq = list_entry(list->prev,
					    struct journal_replay,
					    list)->j.seq;

	return 0;
#undef read_bucket
}

void bch_journal_mark(struct cache_set *c, struct list_head *list)
{
	atomic_t p = { 0 };
	struct bkey *k;
	struct journal_replay *i;
	struct journal *j = &c->journal;
	uint64_t last = j->seq;

	/*
	 * journal.pin should never fill up - we never write a journal
	 * entry when it would fill up. But if for some reason it does, we
	 * iterate over the list in reverse order so that we can just skip that
	 * refcount instead of bugging.
	 */

	list_for_each_entry_reverse(i, list, list) {
		BUG_ON(last < i->j.seq);
		i->pin = NULL;

		while (last-- != i->j.seq)
			if (fifo_free(&j->pin) > 1) {
				fifo_push_front(&j->pin, p);
				atomic_set(&fifo_front(&j->pin), 0);
			}

		if (fifo_free(&j->pin) > 1) {
			fifo_push_front(&j->pin, p);
			i->pin = &fifo_front(&j->pin);
			atomic_set(i->pin, 1);
		}

		for (k = i->j.start;
		     k < end(&i->j);
		     k = bkey_next(k)) {
			unsigned j;

			for (j = 0; j < KEY_PTRS(k); j++) {
				struct bucket *g = PTR_BUCKET(c, k, j);
				atomic_inc(&g->pin);

				if (g->prio == BTREE_PRIO &&
				    !ptr_stale(c, k, j))
					g->prio = INITIAL_PRIO;
			}

			__bch_btree_mark_key(c, 0, k);
		}
	}
}

int bch_journal_replay(struct cache_set *s, struct list_head *list,
			  struct btree_op *op)
{
	int ret = 0, keys = 0, entries = 0;
	struct bkey *k;
	struct journal_replay *i =
		list_entry(list->prev, struct journal_replay, list);

	uint64_t start = i->j.last_seq, end = i->j.seq, n = start;

	list_for_each_entry(i, list, list) {
		BUG_ON(i->pin && atomic_read(i->pin) != 1);

		if (n != i->j.seq)
			pr_err(
		"journal entries %llu-%llu missing! (replaying %llu-%llu)\n",
		n, i->j.seq - 1, start, end);

		for (k = i->j.start;
		     k < end(&i->j);
		     k = bkey_next(k)) {
			trace_bcache_journal_replay_key(k);

			bkey_copy(op->keys.top, k);
			bch_keylist_push(&op->keys);

			op->journal = i->pin;
			atomic_inc(op->journal);

			ret = bch_btree_insert(op, s);
			if (ret)
				goto err;

			BUG_ON(!bch_keylist_empty(&op->keys));
			keys++;

			cond_resched();
		}

		if (i->pin)
			atomic_dec(i->pin);
		n = i->j.seq + 1;
		entries++;
	}

	pr_info("journal replay done, %i keys in %i entries, seq %llu",
		keys, entries, end);

	while (!list_empty(list)) {
		i = list_first_entry(list, struct journal_replay, list);
		list_del(&i->list);
		kfree(i);
	}
err:
	closure_sync(&op->cl);
	return ret;
}

/* Journalling */

static void btree_flush_write(struct cache_set *c)
{
	/*
	 * Try to find the btree node with that references the oldest journal
	 * entry, best is our current candidate and is locked if non NULL:
	 */
	struct btree *b, *best = NULL;
	unsigned iter;

	for_each_cached_btree(b, c, iter) {
		if (!down_write_trylock(&b->lock))
			continue;

		if (!btree_node_dirty(b) ||
		    !btree_current_write(b)->journal) {
			rw_unlock(true, b);
			continue;
		}

		if (!best)
			best = b;
		else if (journal_pin_cmp(c,
					 btree_current_write(best),
					 btree_current_write(b))) {
			rw_unlock(true, best);
			best = b;
		} else
			rw_unlock(true, b);
	}

	if (best)
		goto out;

	/* We can't find the best btree node, just pick the first */
	list_for_each_entry(b, &c->btree_cache, list)
		if (!b->level && btree_node_dirty(b)) {
			best = b;
			rw_lock(true, best, best->level);
			goto found;
		}

out:
	if (!best)
		return;
found:
	if (btree_node_dirty(best))
		bch_btree_node_write(best, NULL);
	rw_unlock(true, best);
}

#define last_seq(j)	((j)->seq - fifo_used(&(j)->pin) + 1)

static void journal_discard_endio(struct bio *bio, int error)
{
	struct journal_device *ja =
		container_of(bio, struct journal_device, discard_bio);
	struct cache *ca = container_of(ja, struct cache, journal);

	atomic_set(&ja->discard_in_flight, DISCARD_DONE);

	closure_wake_up(&ca->set->journal.wait);
	closure_put(&ca->set->cl);
}

static void journal_discard_work(struct work_struct *work)
{
	struct journal_device *ja =
		container_of(work, struct journal_device, discard_work);

	submit_bio(0, &ja->discard_bio);
}

static void do_journal_discard(struct cache *ca)
{
	struct journal_device *ja = &ca->journal;
	struct bio *bio = &ja->discard_bio;

	if (!ca->discard) {
		ja->discard_idx = ja->last_idx;
		return;
	}

	switch (atomic_read(&ja->discard_in_flight)) {
	case DISCARD_IN_FLIGHT:
		return;

	case DISCARD_DONE:
		ja->discard_idx = (ja->discard_idx + 1) %
			ca->sb.njournal_buckets;

		atomic_set(&ja->discard_in_flight, DISCARD_READY);
		/* fallthrough */

	case DISCARD_READY:
		if (ja->discard_idx == ja->last_idx)
			return;

		atomic_set(&ja->discard_in_flight, DISCARD_IN_FLIGHT);

		bio_init(bio);
		bio->bi_sector		= bucket_to_sector(ca->set,
						ca->sb.d[ja->discard_idx]);
		bio->bi_bdev		= ca->bdev;
		bio->bi_rw		= REQ_WRITE|REQ_DISCARD;
		bio->bi_max_vecs	= 1;
		bio->bi_io_vec		= bio->bi_inline_vecs;
		bio->bi_size		= bucket_bytes(ca);
		bio->bi_end_io		= journal_discard_endio;

		closure_get(&ca->set->cl);
		INIT_WORK(&ja->discard_work, journal_discard_work);
		schedule_work(&ja->discard_work);
	}
}

static void journal_reclaim(struct cache_set *c)
{
	struct bkey *k = &c->journal.key;
	struct cache *ca;
	uint64_t last_seq;
	unsigned iter, n = 0;
	atomic_t p;

	while (!atomic_read(&fifo_front(&c->journal.pin)))
		fifo_pop(&c->journal.pin, p);

	last_seq = last_seq(&c->journal);

	/* Update last_idx */

	for_each_cache(ca, c, iter) {
		struct journal_device *ja = &ca->journal;

		while (ja->last_idx != ja->cur_idx &&
		       ja->seq[ja->last_idx] < last_seq)
			ja->last_idx = (ja->last_idx + 1) %
				ca->sb.njournal_buckets;
	}

	for_each_cache(ca, c, iter)
		do_journal_discard(ca);

	if (c->journal.blocks_free)
		return;

	/*
	 * Allocate:
	 * XXX: Sort by free journal space
	 */

	for_each_cache(ca, c, iter) {
		struct journal_device *ja = &ca->journal;
		unsigned next = (ja->cur_idx + 1) % ca->sb.njournal_buckets;

		/* No space available on this device */
		if (next == ja->discard_idx)
			continue;

		ja->cur_idx = next;
		k->ptr[n++] = PTR(0,
				  bucket_to_sector(c, ca->sb.d[ja->cur_idx]),
				  ca->sb.nr_this_dev);
	}

	bkey_init(k);
	SET_KEY_PTRS(k, n);

	if (n)
		c->journal.blocks_free = c->sb.bucket_size >> c->block_bits;

	if (!journal_full(&c->journal))
		__closure_wake_up(&c->journal.wait);
}

void bch_journal_next(struct journal *j)
{
	atomic_t p = { 1 };

	j->cur = (j->cur == j->w)
		? &j->w[1]
		: &j->w[0];

	/*
	 * The fifo_push() needs to happen at the same time as j->seq is
	 * incremented for last_seq() to be calculated correctly
	 */
	BUG_ON(!fifo_push(&j->pin, p));
	atomic_set(&fifo_back(&j->pin), 1);

	j->cur->data->seq	= ++j->seq;
	j->cur->need_write	= false;
	j->cur->data->keys	= 0;

	if (fifo_full(&j->pin))
		pr_debug("journal_pin full (%zu)", fifo_used(&j->pin));
}

static void journal_write_endio(struct bio *bio, int error)
{
	struct journal_write *w = bio->bi_private;

	cache_set_err_on(error, w->c, "journal io error");
	closure_put(&w->c->journal.io.cl);
}

static void journal_write(struct closure *);

static void journal_write_done(struct closure *cl)
{
	struct journal *j = container_of(cl, struct journal, io.cl);
	struct cache_set *c = container_of(j, struct cache_set, journal);

	struct journal_write *w = (j->cur == j->w)
		? &j->w[1]
		: &j->w[0];

	__closure_wake_up(&w->wait);

	if (c->journal_delay_ms)
		closure_delay(&j->io, msecs_to_jiffies(c->journal_delay_ms));

	continue_at(cl, journal_write, system_wq);
}

static void journal_write_unlocked(struct closure *cl)
	__releases(c->journal.lock)
{
	struct cache_set *c = container_of(cl, struct cache_set, journal.io.cl);
	struct cache *ca;
	struct journal_write *w = c->journal.cur;
	struct bkey *k = &c->journal.key;
	unsigned i, sectors = set_blocks(w->data, c) * c->sb.block_size;

	struct bio *bio;
	struct bio_list list;
	bio_list_init(&list);

	if (!w->need_write) {
		/*
		 * XXX: have to unlock closure before we unlock journal lock,
		 * else we race with bch_journal(). But this way we race
		 * against cache set unregister. Doh.
		 */
		set_closure_fn(cl, NULL, NULL);
		closure_sub(cl, CLOSURE_RUNNING + 1);
		spin_unlock(&c->journal.lock);
		return;
	} else if (journal_full(&c->journal)) {
		journal_reclaim(c);
		spin_unlock(&c->journal.lock);

		btree_flush_write(c);
		continue_at(cl, journal_write, system_wq);
	}

	c->journal.blocks_free -= set_blocks(w->data, c);

	w->data->btree_level = c->root->level;

	bkey_copy(&w->data->btree_root, &c->root->key);
	bkey_copy(&w->data->uuid_bucket, &c->uuid_bucket);

	for_each_cache(ca, c, i)
		w->data->prio_bucket[ca->sb.nr_this_dev] = ca->prio_buckets[0];

	w->data->magic		= jset_magic(c);
	w->data->version	= BCACHE_JSET_VERSION;
	w->data->last_seq	= last_seq(&c->journal);
	w->data->csum		= csum_set(w->data);

	for (i = 0; i < KEY_PTRS(k); i++) {
		ca = PTR_CACHE(c, k, i);
		bio = &ca->journal.bio;

		atomic_long_add(sectors, &ca->meta_sectors_written);

		bio_reset(bio);
		bio->bi_sector	= PTR_OFFSET(k, i);
		bio->bi_bdev	= ca->bdev;
		bio->bi_rw	= REQ_WRITE|REQ_SYNC|REQ_META|REQ_FLUSH|REQ_FUA;
		bio->bi_size	= sectors << 9;

		bio->bi_end_io	= journal_write_endio;
		bio->bi_private = w;
		bch_bio_map(bio, w->data);

		trace_bcache_journal_write(bio);
		bio_list_add(&list, bio);

		SET_PTR_OFFSET(k, i, PTR_OFFSET(k, i) + sectors);

		ca->journal.seq[ca->journal.cur_idx] = w->data->seq;
	}

	atomic_dec_bug(&fifo_back(&c->journal.pin));
	bch_journal_next(&c->journal);
	journal_reclaim(c);

	spin_unlock(&c->journal.lock);

	while ((bio = bio_list_pop(&list)))
		closure_bio_submit(bio, cl, c->cache[0]);

	continue_at(cl, journal_write_done, NULL);
}

static void journal_write(struct closure *cl)
{
	struct cache_set *c = container_of(cl, struct cache_set, journal.io.cl);

	spin_lock(&c->journal.lock);
	journal_write_unlocked(cl);
}

static void __journal_try_write(struct cache_set *c, bool noflush)
	__releases(c->journal.lock)
{
	struct closure *cl = &c->journal.io.cl;

	if (!closure_trylock(cl, &c->cl))
		spin_unlock(&c->journal.lock);
	else if (noflush && journal_full(&c->journal)) {
		spin_unlock(&c->journal.lock);
		continue_at(cl, journal_write, system_wq);
	} else
		journal_write_unlocked(cl);
}

#define journal_try_write(c)	__journal_try_write(c, false)

void bch_journal_meta(struct cache_set *c, struct closure *cl)
{
	struct journal_write *w;

	if (CACHE_SYNC(&c->sb)) {
		spin_lock(&c->journal.lock);

		w = c->journal.cur;
		w->need_write = true;

		if (cl)
			BUG_ON(!closure_wait(&w->wait, cl));

		closure_flush(&c->journal.io);
		__journal_try_write(c, true);
	}
}

/*
 * Entry point to the journalling code - bio_insert() and btree_invalidate()
 * pass bch_journal() a list of keys to be journalled, and then
 * bch_journal() hands those same keys off to btree_insert_async()
 */

void bch_journal(struct closure *cl)
{
	struct btree_op *op = container_of(cl, struct btree_op, cl);
	struct cache_set *c = op->c;
	struct journal_write *w;
	size_t b, n = ((uint64_t *) op->keys.top) - op->keys.list;

	if (op->type != BTREE_INSERT ||
	    !CACHE_SYNC(&c->sb))
		goto out;

	/*
	 * If we're looping because we errored, might already be waiting on
	 * another journal write:
	 */
	while (atomic_read(&cl->parent->remaining) & CLOSURE_WAITING)
		closure_sync(cl->parent);

	spin_lock(&c->journal.lock);

	if (journal_full(&c->journal)) {
		trace_bcache_journal_full(c);

		closure_wait(&c->journal.wait, cl);

		journal_reclaim(c);
		spin_unlock(&c->journal.lock);

		btree_flush_write(c);
		continue_at(cl, bch_journal, bcache_wq);
	}

	w = c->journal.cur;
	w->need_write = true;
	b = __set_blocks(w->data, w->data->keys + n, c);

	if (b * c->sb.block_size > PAGE_SECTORS << JSET_BITS ||
	    b > c->journal.blocks_free) {
		trace_bcache_journal_entry_full(c);

		/*
		 * XXX: If we were inserting so many keys that they won't fit in
		 * an _empty_ journal write, we'll deadlock. For now, handle
		 * this in bch_keylist_realloc() - but something to think about.
		 */
		BUG_ON(!w->data->keys);

		BUG_ON(!closure_wait(&w->wait, cl));

		closure_flush(&c->journal.io);

		journal_try_write(c);
		continue_at(cl, bch_journal, bcache_wq);
	}

	memcpy(end(w->data), op->keys.list, n * sizeof(uint64_t));
	w->data->keys += n;

	op->journal = &fifo_back(&c->journal.pin);
	atomic_inc(op->journal);

	if (op->flush_journal) {
		closure_flush(&c->journal.io);
		closure_wait(&w->wait, cl->parent);
	}

	journal_try_write(c);
out:
	bch_btree_insert_async(cl);
}

void bch_journal_free(struct cache_set *c)
{
	free_pages((unsigned long) c->journal.w[1].data, JSET_BITS);
	free_pages((unsigned long) c->journal.w[0].data, JSET_BITS);
	free_fifo(&c->journal.pin);
}

int bch_journal_alloc(struct cache_set *c)
{
	struct journal *j = &c->journal;

	closure_init_unlocked(&j->io);
	spin_lock_init(&j->lock);

	c->journal_delay_ms = 100;

	j->w[0].c = c;
	j->w[1].c = c;

	if (!(init_fifo(&j->pin, JOURNAL_PIN, GFP_KERNEL)) ||
	    !(j->w[0].data = (void *) __get_free_pages(GFP_KERNEL, JSET_BITS)) ||
	    !(j->w[1].data = (void *) __get_free_pages(GFP_KERNEL, JSET_BITS)))
		return -ENOMEM;

	return 0;
}
