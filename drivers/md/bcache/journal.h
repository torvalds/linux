/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHE_JOURNAL_H
#define _BCACHE_JOURNAL_H

/*
 * THE JOURNAL:
 *
 * The journal is treated as a circular buffer of buckets - a journal entry
 * never spans two buckets. This means (not implemented yet) we can resize the
 * journal at runtime, and will be needed for bcache on raw flash support.
 *
 * Journal entries contain a list of keys, ordered by the time they were
 * inserted; thus journal replay just has to reinsert the keys.
 *
 * We also keep some things in the journal header that are logically part of the
 * superblock - all the things that are frequently updated. This is for future
 * bcache on raw flash support; the superblock (which will become another
 * journal) can't be moved or wear leveled, so it contains just enough
 * information to find the main journal, and the superblock only has to be
 * rewritten when we want to move/wear level the main journal.
 *
 * Currently, we don't journal BTREE_REPLACE operations - this will hopefully be
 * fixed eventually. This isn't a bug - BTREE_REPLACE is used for insertions
 * from cache misses, which don't have to be journaled, and for writeback and
 * moving gc we work around it by flushing the btree to disk before updating the
 * gc information. But it is a potential issue with incremental garbage
 * collection, and it's fragile.
 *
 * OPEN JOURNAL ENTRIES:
 *
 * Each journal entry contains, in the header, the sequence number of the last
 * journal entry still open - i.e. that has keys that haven't been flushed to
 * disk in the btree.
 *
 * We track this by maintaining a refcount for every open journal entry, in a
 * fifo; each entry in the fifo corresponds to a particular journal
 * entry/sequence number. When the refcount at the tail of the fifo goes to
 * zero, we pop it off - thus, the size of the fifo tells us the number of open
 * journal entries
 *
 * We take a refcount on a journal entry when we add some keys to a journal
 * entry that we're going to insert (held by struct btree_op), and then when we
 * insert those keys into the btree the btree write we're setting up takes a
 * copy of that refcount (held by struct btree_write). That refcount is dropped
 * when the btree write completes.
 *
 * A struct btree_write can only hold a refcount on a single journal entry, but
 * might contain keys for many journal entries - we handle this by making sure
 * it always has a refcount on the _oldest_ journal entry of all the journal
 * entries it has keys for.
 *
 * JOURNAL RECLAIM:
 *
 * As mentioned previously, our fifo of refcounts tells us the number of open
 * journal entries; from that and the current journal sequence number we compute
 * last_seq - the oldest journal entry we still need. We write last_seq in each
 * journal entry, and we also have to keep track of where it exists on disk so
 * we don't overwrite it when we loop around the journal.
 *
 * To do that we track, for each journal bucket, the sequence number of the
 * newest journal entry it contains - if we don't need that journal entry we
 * don't need anything in that bucket anymore. From that we track the last
 * journal bucket we still need; all this is tracked in struct journal_device
 * and updated by journal_reclaim().
 *
 * JOURNAL FILLING UP:
 *
 * There are two ways the journal could fill up; either we could run out of
 * space to write to, or we could have too many open journal entries and run out
 * of room in the fifo of refcounts. Since those refcounts are decremented
 * without any locking we can't safely resize that fifo, so we handle it the
 * same way.
 *
 * If the journal fills up, we start flushing dirty btree nodes until we can
 * allocate space for a journal write again - preferentially flushing btree
 * nodes that are pinning the oldest journal entries first.
 */

/*
 * Only used for holding the journal entries we read in btree_journal_read()
 * during cache_registration
 */
struct journal_replay {
	struct list_head	list;
	atomic_t		*pin;
	struct jset		j;
};

/*
 * We put two of these in struct journal; we used them for writes to the
 * journal that are being staged or in flight.
 */
struct journal_write {
	struct jset		*data;
#define JSET_BITS		3

	struct cache_set	*c;
	struct closure_waitlist	wait;
	bool			dirty;
	bool			need_write;
};

/* Embedded in struct cache_set */
struct journal {
	spinlock_t		lock;
	spinlock_t		flush_write_lock;
	bool			btree_flushing;
	bool			do_reserve;
	/* used when waiting because the journal was full */
	struct closure_waitlist	wait;
	struct closure		io;
	int			io_in_flight;
	struct delayed_work	work;

	/* Number of blocks free in the bucket(s) we're currently writing to */
	unsigned int		blocks_free;
	uint64_t		seq;
	DECLARE_FIFO(atomic_t, pin);

	BKEY_PADDED(key);

	struct journal_write	w[2], *cur;
};

/*
 * Embedded in struct cache. First three fields refer to the array of journal
 * buckets, in cache_sb.
 */
struct journal_device {
	/*
	 * For each journal bucket, contains the max sequence number of the
	 * journal writes it contains - so we know when a bucket can be reused.
	 */
	uint64_t		seq[SB_JOURNAL_BUCKETS];

	/* Journal bucket we're currently writing to */
	unsigned int		cur_idx;

	/* Last journal bucket that still contains an open journal entry */
	unsigned int		last_idx;

	/* Next journal bucket to be discarded */
	unsigned int		discard_idx;

#define DISCARD_READY		0
#define DISCARD_IN_FLIGHT	1
#define DISCARD_DONE		2
	/* 1 - discard in flight, -1 - discard completed */
	atomic_t		discard_in_flight;

	struct work_struct	discard_work;
	struct bio		discard_bio;
	struct bio_vec		discard_bv;

	/* Bio for journal reads/writes to this device */
	struct bio		bio;
	struct bio_vec		bv[8];
};

#define BTREE_FLUSH_NR	8

#define journal_pin_cmp(c, l, r)				\
	(fifo_idx(&(c)->journal.pin, (l)) > fifo_idx(&(c)->journal.pin, (r)))

#define JOURNAL_PIN	20000

#define journal_full(j)						\
	(!(j)->blocks_free || fifo_free(&(j)->pin) <= 1)

struct closure;
struct cache_set;
struct btree_op;
struct keylist;

atomic_t *bch_journal(struct cache_set *c,
		      struct keylist *keys,
		      struct closure *parent);
void bch_journal_next(struct journal *j);
void bch_journal_mark(struct cache_set *c, struct list_head *list);
void bch_journal_meta(struct cache_set *c, struct closure *cl);
int bch_journal_read(struct cache_set *c, struct list_head *list);
int bch_journal_replay(struct cache_set *c, struct list_head *list);

void bch_journal_free(struct cache_set *c);
int bch_journal_alloc(struct cache_set *c);
void bch_journal_space_reserve(struct journal *j);

#endif /* _BCACHE_JOURNAL_H */
