/*
 * bcache journalling code, for btree insertions
 *
 * Copyright 2012 Google, Inc.
 */

#include "bcache.h"
#include "btree.h"
#include "debug.h"
#include "extents.h"

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

static void journal_read_endio(struct bio *bio)
{
	struct closure *cl = bio->bi_private;
	closure_put(cl);
}

static int journal_read_bucket(struct cache *ca, struct list_head *list,
			       unsigned bucket_index)
{
	struct journal_device *ja = &ca->journal;
	struct bio *bio = &ja->bio;

	struct journal_replay *i;
	struct jset *j, *data = ca->set->journal.w[0].data;
	struct closure cl;
	unsigned len, left, offset = 0;
	int ret = 0;
	sector_t bucket = bucket_to_sector(ca->set, ca->sb.d[bucket_index]);

	closure_init_stack(&cl);

	pr_debug("reading %u", bucket_index);

	while (offset < ca->sb.bucket_size) {
reread:		left = ca->sb.bucket_size - offset;
		len = min_t(unsigned, left, PAGE_SECTORS << JSET_BITS);

		bio_reset(bio);
		bio->bi_iter.bi_sector	= bucket + offset;
		bio->bi_bdev	= ca->bdev;
		bio->bi_iter.bi_size	= len << 9;

		bio->bi_end_io	= journal_read_endio;
		bio->bi_private = &cl;
		bio_set_op_attrs(bio, REQ_OP_READ, 0);
		bch_bio_map(bio, data);

		closure_bio_submit(bio, &cl);
		closure_sync(&cl);

		/* This function could be simpler now since we no longer write
		 * journal entries that overlap bucket boundaries; this means
		 * the start of a bucket will always have a valid journal entry
		 * if it has any journal entries at all.
		 */

		j = data;
		while (len) {
			struct list_head *where;
			size_t blocks, bytes = set_bytes(j);

			if (j->magic != jset_magic(&ca->sb)) {
				pr_debug("%u: bad magic", bucket_index);
				return ret;
			}

			if (bytes > left << 9 ||
			    bytes > PAGE_SIZE << JSET_BITS) {
				pr_info("%u: too big, %zu bytes, offset %u",
					bucket_index, bytes, offset);
				return ret;
			}

			if (bytes > len << 9)
				goto reread;

			if (j->csum != csum_set(j)) {
				pr_info("%u: bad csum, %zu bytes, offset %u",
					bucket_index, bytes, offset);
				return ret;
			}

			blocks = set_blocks(j, block_bytes(ca->set));

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

int bch_journal_read(struct cache_set *c, struct list_head *list)
{
#define read_bucket(b)							\
	({								\
		int ret = journal_read_bucket(ca, list, b);		\
		__set_bit(b, bitmap);					\
		if (ret < 0)						\
			return ret;					\
		ret;							\
	})

	struct cache *ca;
	unsigned iter;

	for_each_cache(ca, c, iter) {
		struct journal_device *ja = &ca->journal;
		DECLARE_BITMAP(bitmap, SB_JOURNAL_BUCKETS);
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

		/* no journal entries on this device? */
		if (l == ca->sb.njournal_buckets)
			continue;
bsearch:
		BUG_ON(list_empty(list));

		/* Binary search */
		m = l;
		r = find_next_bit(bitmap, ca->sb.njournal_buckets, l + 1);
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
				/*
				 * When journal_reclaim() goes to allocate for
				 * the first time, it'll use the bucket after
				 * ja->cur_idx
				 */
				ja->cur_idx = i;
				ja->last_idx = ja->discard_idx = (i + 1) %
					ca->sb.njournal_buckets;

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
		     k < bset_bkey_last(&i->j);
		     k = bkey_next(k))
			if (!__bch_extent_invalid(c, k)) {
				unsigned j;

				for (j = 0; j < KEY_PTRS(k); j++)
					if (ptr_available(c, k, j))
						atomic_inc(&PTR_BUCKET(c, k, j)->pin);

				bch_initial_mark_key(c, 0, k);
			}
	}
}

int bch_journal_replay(struct cache_set *s, struct list_head *list)
{
	int ret = 0, keys = 0, entries = 0;
	struct bkey *k;
	struct journal_replay *i =
		list_entry(list->prev, struct journal_replay, list);

	uint64_t start = i->j.last_seq, end = i->j.seq, n = start;
	struct keylist keylist;

	list_for_each_entry(i, list, list) {
		BUG_ON(i->pin && atomic_read(i->pin) != 1);

		cache_set_err_on(n != i->j.seq, s,
"bcache: journal entries %llu-%llu missing! (replaying %llu-%llu)",
				 n, i->j.seq - 1, start, end);

		for (k = i->j.start;
		     k < bset_bkey_last(&i->j);
		     k = bkey_next(k)) {
			trace_bcache_journal_replay_key(k);

			bch_keylist_init_single(&keylist, k);

			ret = bch_btree_insert(s, &keylist, i->pin, NULL);
			if (ret)
				goto err;

			BUG_ON(!bch_keylist_empty(&keylist));
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
err:
	while (!list_empty(list)) {
		i = list_first_entry(list, struct journal_replay, list);
		list_del(&i->list);
		kfree(i);
	}

	return ret;
}

/* Journalling */

static void btree_flush_write(struct cache_set *c)
{
	/*
	 * Try to find the btree node with that references the oldest journal
	 * entry, best is our current candidate and is locked if non NULL:
	 */
	struct btree *b, *best;
	unsigned i;
retry:
	best = NULL;

	for_each_cached_btree(b, c, i)
		if (btree_current_write(b)->journal) {
			if (!best)
				best = b;
			else if (journal_pin_cmp(c,
					btree_current_write(best)->journal,
					btree_current_write(b)->journal)) {
				best = b;
			}
		}

	b = best;
	if (b) {
		mutex_lock(&b->write_lock);
		if (!btree_current_write(b)->journal) {
			mutex_unlock(&b->write_lock);
			/* We raced */
			goto retry;
		}

		__bch_btree_node_write(b, NULL);
		mutex_unlock(&b->write_lock);
	}
}

#define last_seq(j)	((j)->seq - fifo_used(&(j)->pin) + 1)

static void journal_discard_endio(struct bio *bio)
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

	submit_bio(&ja->discard_bio);
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

		bio_init(bio, bio->bi_inline_vecs, 1);
		bio_set_op_attrs(bio, REQ_OP_DISCARD, 0);
		bio->bi_iter.bi_sector	= bucket_to_sector(ca->set,
						ca->sb.d[ja->discard_idx]);
		bio->bi_bdev		= ca->bdev;
		bio->bi_iter.bi_size	= bucket_bytes(ca);
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
		goto out;

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
out:
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
	j->cur->dirty		= false;
	j->cur->need_write	= false;
	j->cur->data->keys	= 0;

	if (fifo_full(&j->pin))
		pr_debug("journal_pin full (%zu)", fifo_used(&j->pin));
}

static void journal_write_endio(struct bio *bio)
{
	struct journal_write *w = bio->bi_private;

	cache_set_err_on(bio->bi_status, w->c, "journal io error");
	closure_put(&w->c->journal.io);
}

static void journal_write(struct closure *);

static void journal_write_done(struct closure *cl)
{
	struct journal *j = container_of(cl, struct journal, io);
	struct journal_write *w = (j->cur == j->w)
		? &j->w[1]
		: &j->w[0];

	__closure_wake_up(&w->wait);
	continue_at_nobarrier(cl, journal_write, system_wq);
}

static void journal_write_unlock(struct closure *cl)
{
	struct cache_set *c = container_of(cl, struct cache_set, journal.io);

	c->journal.io_in_flight = 0;
	spin_unlock(&c->journal.lock);
}

static void journal_write_unlocked(struct closure *cl)
	__releases(c->journal.lock)
{
	struct cache_set *c = container_of(cl, struct cache_set, journal.io);
	struct cache *ca;
	struct journal_write *w = c->journal.cur;
	struct bkey *k = &c->journal.key;
	unsigned i, sectors = set_blocks(w->data, block_bytes(c)) *
		c->sb.block_size;

	struct bio *bio;
	struct bio_list list;
	bio_list_init(&list);

	if (!w->need_write) {
		closure_return_with_destructor(cl, journal_write_unlock);
		return;
	} else if (journal_full(&c->journal)) {
		journal_reclaim(c);
		spin_unlock(&c->journal.lock);

		btree_flush_write(c);
		continue_at(cl, journal_write, system_wq);
		return;
	}

	c->journal.blocks_free -= set_blocks(w->data, block_bytes(c));

	w->data->btree_level = c->root->level;

	bkey_copy(&w->data->btree_root, &c->root->key);
	bkey_copy(&w->data->uuid_bucket, &c->uuid_bucket);

	for_each_cache(ca, c, i)
		w->data->prio_bucket[ca->sb.nr_this_dev] = ca->prio_buckets[0];

	w->data->magic		= jset_magic(&c->sb);
	w->data->version	= BCACHE_JSET_VERSION;
	w->data->last_seq	= last_seq(&c->journal);
	w->data->csum		= csum_set(w->data);

	for (i = 0; i < KEY_PTRS(k); i++) {
		ca = PTR_CACHE(c, k, i);
		bio = &ca->journal.bio;

		atomic_long_add(sectors, &ca->meta_sectors_written);

		bio_reset(bio);
		bio->bi_iter.bi_sector	= PTR_OFFSET(k, i);
		bio->bi_bdev	= ca->bdev;
		bio->bi_iter.bi_size = sectors << 9;

		bio->bi_end_io	= journal_write_endio;
		bio->bi_private = w;
		bio_set_op_attrs(bio, REQ_OP_WRITE,
				 REQ_SYNC|REQ_META|REQ_PREFLUSH|REQ_FUA);
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
		closure_bio_submit(bio, cl);

	continue_at(cl, journal_write_done, NULL);
}

static void journal_write(struct closure *cl)
{
	struct cache_set *c = container_of(cl, struct cache_set, journal.io);

	spin_lock(&c->journal.lock);
	journal_write_unlocked(cl);
}

static void journal_try_write(struct cache_set *c)
	__releases(c->journal.lock)
{
	struct closure *cl = &c->journal.io;
	struct journal_write *w = c->journal.cur;

	w->need_write = true;

	if (!c->journal.io_in_flight) {
		c->journal.io_in_flight = 1;
		closure_call(cl, journal_write_unlocked, NULL, &c->cl);
	} else {
		spin_unlock(&c->journal.lock);
	}
}

static struct journal_write *journal_wait_for_write(struct cache_set *c,
						    unsigned nkeys)
{
	size_t sectors;
	struct closure cl;
	bool wait = false;

	closure_init_stack(&cl);

	spin_lock(&c->journal.lock);

	while (1) {
		struct journal_write *w = c->journal.cur;

		sectors = __set_blocks(w->data, w->data->keys + nkeys,
				       block_bytes(c)) * c->sb.block_size;

		if (sectors <= min_t(size_t,
				     c->journal.blocks_free * c->sb.block_size,
				     PAGE_SECTORS << JSET_BITS))
			return w;

		if (wait)
			closure_wait(&c->journal.wait, &cl);

		if (!journal_full(&c->journal)) {
			if (wait)
				trace_bcache_journal_entry_full(c);

			/*
			 * XXX: If we were inserting so many keys that they
			 * won't fit in an _empty_ journal write, we'll
			 * deadlock. For now, handle this in
			 * bch_keylist_realloc() - but something to think about.
			 */
			BUG_ON(!w->data->keys);

			journal_try_write(c); /* unlocks */
		} else {
			if (wait)
				trace_bcache_journal_full(c);

			journal_reclaim(c);
			spin_unlock(&c->journal.lock);

			btree_flush_write(c);
		}

		closure_sync(&cl);
		spin_lock(&c->journal.lock);
		wait = true;
	}
}

static void journal_write_work(struct work_struct *work)
{
	struct cache_set *c = container_of(to_delayed_work(work),
					   struct cache_set,
					   journal.work);
	spin_lock(&c->journal.lock);
	if (c->journal.cur->dirty)
		journal_try_write(c);
	else
		spin_unlock(&c->journal.lock);
}

/*
 * Entry point to the journalling code - bio_insert() and btree_invalidate()
 * pass bch_journal() a list of keys to be journalled, and then
 * bch_journal() hands those same keys off to btree_insert_async()
 */

atomic_t *bch_journal(struct cache_set *c,
		      struct keylist *keys,
		      struct closure *parent)
{
	struct journal_write *w;
	atomic_t *ret;

	if (!CACHE_SYNC(&c->sb))
		return NULL;

	w = journal_wait_for_write(c, bch_keylist_nkeys(keys));

	memcpy(bset_bkey_last(w->data), keys->keys, bch_keylist_bytes(keys));
	w->data->keys += bch_keylist_nkeys(keys);

	ret = &fifo_back(&c->journal.pin);
	atomic_inc(ret);

	if (parent) {
		closure_wait(&w->wait, parent);
		journal_try_write(c);
	} else if (!w->dirty) {
		w->dirty = true;
		schedule_delayed_work(&c->journal.work,
				      msecs_to_jiffies(c->journal_delay_ms));
		spin_unlock(&c->journal.lock);
	} else {
		spin_unlock(&c->journal.lock);
	}


	return ret;
}

void bch_journal_meta(struct cache_set *c, struct closure *cl)
{
	struct keylist keys;
	atomic_t *ref;

	bch_keylist_init(&keys);

	ref = bch_journal(c, &keys, cl);
	if (ref)
		atomic_dec_bug(ref);
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

	spin_lock_init(&j->lock);
	INIT_DELAYED_WORK(&j->work, journal_write_work);

	c->journal_delay_ms = 100;

	j->w[0].c = c;
	j->w[1].c = c;

	if (!(init_fifo(&j->pin, JOURNAL_PIN, GFP_KERNEL)) ||
	    !(j->w[0].data = (void *) __get_free_pages(GFP_KERNEL, JSET_BITS)) ||
	    !(j->w[1].data = (void *) __get_free_pages(GFP_KERNEL, JSET_BITS)))
		return -ENOMEM;

	return 0;
}
