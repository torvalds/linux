/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHE_H
#define _BCACHE_H

/*
 * SOME HIGH LEVEL CODE DOCUMENTATION:
 *
 * Bcache mostly works with cache sets, cache devices, and backing devices.
 *
 * Support for multiple cache devices hasn't quite been finished off yet, but
 * it's about 95% plumbed through. A cache set and its cache devices is sort of
 * like a md raid array and its component devices. Most of the code doesn't care
 * about individual cache devices, the main abstraction is the cache set.
 *
 * Multiple cache devices is intended to give us the ability to mirror dirty
 * cached data and metadata, without mirroring clean cached data.
 *
 * Backing devices are different, in that they have a lifetime independent of a
 * cache set. When you register a newly formatted backing device it'll come up
 * in passthrough mode, and then you can attach and detach a backing device from
 * a cache set at runtime - while it's mounted and in use. Detaching implicitly
 * invalidates any cached data for that backing device.
 *
 * A cache set can have multiple (many) backing devices attached to it.
 *
 * There's also flash only volumes - this is the reason for the distinction
 * between struct cached_dev and struct bcache_device. A flash only volume
 * works much like a bcache device that has a backing device, except the
 * "cached" data is always dirty. The end result is that we get thin
 * provisioning with very little additional code.
 *
 * Flash only volumes work but they're not production ready because the moving
 * garbage collector needs more work. More on that later.
 *
 * BUCKETS/ALLOCATION:
 *
 * Bcache is primarily designed for caching, which means that in normal
 * operation all of our available space will be allocated. Thus, we need an
 * efficient way of deleting things from the cache so we can write new things to
 * it.
 *
 * To do this, we first divide the cache device up into buckets. A bucket is the
 * unit of allocation; they're typically around 1 mb - anywhere from 128k to 2M+
 * works efficiently.
 *
 * Each bucket has a 16 bit priority, and an 8 bit generation associated with
 * it. The gens and priorities for all the buckets are stored contiguously and
 * packed on disk (in a linked list of buckets - aside from the superblock, all
 * of bcache's metadata is stored in buckets).
 *
 * The priority is used to implement an LRU. We reset a bucket's priority when
 * we allocate it or on cache it, and every so often we decrement the priority
 * of each bucket. It could be used to implement something more sophisticated,
 * if anyone ever gets around to it.
 *
 * The generation is used for invalidating buckets. Each pointer also has an 8
 * bit generation embedded in it; for a pointer to be considered valid, its gen
 * must match the gen of the bucket it points into.  Thus, to reuse a bucket all
 * we have to do is increment its gen (and write its new gen to disk; we batch
 * this up).
 *
 * Bcache is entirely COW - we never write twice to a bucket, even buckets that
 * contain metadata (including btree nodes).
 *
 * THE BTREE:
 *
 * Bcache is in large part design around the btree.
 *
 * At a high level, the btree is just an index of key -> ptr tuples.
 *
 * Keys represent extents, and thus have a size field. Keys also have a variable
 * number of pointers attached to them (potentially zero, which is handy for
 * invalidating the cache).
 *
 * The key itself is an inode:offset pair. The inode number corresponds to a
 * backing device or a flash only volume. The offset is the ending offset of the
 * extent within the inode - not the starting offset; this makes lookups
 * slightly more convenient.
 *
 * Pointers contain the cache device id, the offset on that device, and an 8 bit
 * generation number. More on the gen later.
 *
 * Index lookups are not fully abstracted - cache lookups in particular are
 * still somewhat mixed in with the btree code, but things are headed in that
 * direction.
 *
 * Updates are fairly well abstracted, though. There are two different ways of
 * updating the btree; insert and replace.
 *
 * BTREE_INSERT will just take a list of keys and insert them into the btree -
 * overwriting (possibly only partially) any extents they overlap with. This is
 * used to update the index after a write.
 *
 * BTREE_REPLACE is really cmpxchg(); it inserts a key into the btree iff it is
 * overwriting a key that matches another given key. This is used for inserting
 * data into the cache after a cache miss, and for background writeback, and for
 * the moving garbage collector.
 *
 * There is no "delete" operation; deleting things from the index is
 * accomplished by either by invalidating pointers (by incrementing a bucket's
 * gen) or by inserting a key with 0 pointers - which will overwrite anything
 * previously present at that location in the index.
 *
 * This means that there are always stale/invalid keys in the btree. They're
 * filtered out by the code that iterates through a btree node, and removed when
 * a btree node is rewritten.
 *
 * BTREE NODES:
 *
 * Our unit of allocation is a bucket, and we can't arbitrarily allocate and
 * free smaller than a bucket - so, that's how big our btree nodes are.
 *
 * (If buckets are really big we'll only use part of the bucket for a btree node
 * - no less than 1/4th - but a bucket still contains no more than a single
 * btree node. I'd actually like to change this, but for now we rely on the
 * bucket's gen for deleting btree nodes when we rewrite/split a node.)
 *
 * Anyways, btree nodes are big - big enough to be inefficient with a textbook
 * btree implementation.
 *
 * The way this is solved is that btree nodes are internally log structured; we
 * can append new keys to an existing btree node without rewriting it. This
 * means each set of keys we write is sorted, but the node is not.
 *
 * We maintain this log structure in memory - keeping 1Mb of keys sorted would
 * be expensive, and we have to distinguish between the keys we have written and
 * the keys we haven't. So to do a lookup in a btree node, we have to search
 * each sorted set. But we do merge written sets together lazily, so the cost of
 * these extra searches is quite low (normally most of the keys in a btree node
 * will be in one big set, and then there'll be one or two sets that are much
 * smaller).
 *
 * This log structure makes bcache's btree more of a hybrid between a
 * conventional btree and a compacting data structure, with some of the
 * advantages of both.
 *
 * GARBAGE COLLECTION:
 *
 * We can't just invalidate any bucket - it might contain dirty data or
 * metadata. If it once contained dirty data, other writes might overwrite it
 * later, leaving no valid pointers into that bucket in the index.
 *
 * Thus, the primary purpose of garbage collection is to find buckets to reuse.
 * It also counts how much valid data it each bucket currently contains, so that
 * allocation can reuse buckets sooner when they've been mostly overwritten.
 *
 * It also does some things that are really internal to the btree
 * implementation. If a btree node contains pointers that are stale by more than
 * some threshold, it rewrites the btree node to avoid the bucket's generation
 * wrapping around. It also merges adjacent btree nodes if they're empty enough.
 *
 * THE JOURNAL:
 *
 * Bcache's journal is not necessary for consistency; we always strictly
 * order metadata writes so that the btree and everything else is consistent on
 * disk in the event of an unclean shutdown, and in fact bcache had writeback
 * caching (with recovery from unclean shutdown) before journalling was
 * implemented.
 *
 * Rather, the journal is purely a performance optimization; we can't complete a
 * write until we've updated the index on disk, otherwise the cache would be
 * inconsistent in the event of an unclean shutdown. This means that without the
 * journal, on random write workloads we constantly have to update all the leaf
 * nodes in the btree, and those writes will be mostly empty (appending at most
 * a few keys each) - highly inefficient in terms of amount of metadata writes,
 * and it puts more strain on the various btree resorting/compacting code.
 *
 * The journal is just a log of keys we've inserted; on startup we just reinsert
 * all the keys in the open journal entries. That means that when we're updating
 * a node in the btree, we can wait until a 4k block of keys fills up before
 * writing them out.
 *
 * For simplicity, we only journal updates to leaf nodes; updates to parent
 * nodes are rare enough (since our leaf nodes are huge) that it wasn't worth
 * the complexity to deal with journalling them (in particular, journal replay)
 * - updates to non leaf nodes just happen synchronously (see btree_split()).
 */

#define pr_fmt(fmt) "bcache: %s() " fmt, __func__

#include <linux/bio.h>
#include <linux/kobject.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/rbtree.h>
#include <linux/rwsem.h>
#include <linux/refcount.h>
#include <linux/types.h>
#include <linux/workqueue.h>
#include <linux/kthread.h>

#include "bcache_ondisk.h"
#include "bset.h"
#include "util.h"
#include "closure.h"

struct bucket {
	atomic_t	pin;
	uint16_t	prio;
	uint8_t		gen;
	uint8_t		last_gc; /* Most out of date gen in the btree */
	uint16_t	gc_mark; /* Bitfield used by GC. See below for field */
};

/*
 * I'd use bitfields for these, but I don't trust the compiler not to screw me
 * as multiple threads touch struct bucket without locking
 */

BITMASK(GC_MARK,	 struct bucket, gc_mark, 0, 2);
#define GC_MARK_RECLAIMABLE	1
#define GC_MARK_DIRTY		2
#define GC_MARK_METADATA	3
#define GC_SECTORS_USED_SIZE	13
#define MAX_GC_SECTORS_USED	(~(~0ULL << GC_SECTORS_USED_SIZE))
BITMASK(GC_SECTORS_USED, struct bucket, gc_mark, 2, GC_SECTORS_USED_SIZE);
BITMASK(GC_MOVE, struct bucket, gc_mark, 15, 1);

#include "journal.h"
#include "stats.h"
struct search;
struct btree;
struct keybuf;

struct keybuf_key {
	struct rb_node		node;
	BKEY_PADDED(key);
	void			*private;
};

struct keybuf {
	struct bkey		last_scanned;
	spinlock_t		lock;

	/*
	 * Beginning and end of range in rb tree - so that we can skip taking
	 * lock and checking the rb tree when we need to check for overlapping
	 * keys.
	 */
	struct bkey		start;
	struct bkey		end;

	struct rb_root		keys;

#define KEYBUF_NR		500
	DECLARE_ARRAY_ALLOCATOR(struct keybuf_key, freelist, KEYBUF_NR);
};

struct bcache_device {
	struct closure		cl;

	struct kobject		kobj;

	struct cache_set	*c;
	unsigned int		id;
#define BCACHEDEVNAME_SIZE	12
	char			name[BCACHEDEVNAME_SIZE];

	struct gendisk		*disk;

	unsigned long		flags;
#define BCACHE_DEV_CLOSING		0
#define BCACHE_DEV_DETACHING		1
#define BCACHE_DEV_UNLINK_DONE		2
#define BCACHE_DEV_WB_RUNNING		3
#define BCACHE_DEV_RATE_DW_RUNNING	4
	int			nr_stripes;
#define BCH_MIN_STRIPE_SZ		((4 << 20) >> SECTOR_SHIFT)
	unsigned int		stripe_size;
	atomic_t		*stripe_sectors_dirty;
	unsigned long		*full_dirty_stripes;

	struct bio_set		bio_split;

	unsigned int		data_csum:1;

	int (*cache_miss)(struct btree *b, struct search *s,
			  struct bio *bio, unsigned int sectors);
	int (*ioctl)(struct bcache_device *d, blk_mode_t mode,
		     unsigned int cmd, unsigned long arg);
};

struct io {
	/* Used to track sequential IO so it can be skipped */
	struct hlist_node	hash;
	struct list_head	lru;

	unsigned long		jiffies;
	unsigned int		sequential;
	sector_t		last;
};

enum stop_on_failure {
	BCH_CACHED_DEV_STOP_AUTO = 0,
	BCH_CACHED_DEV_STOP_ALWAYS,
	BCH_CACHED_DEV_STOP_MODE_MAX,
};

struct cached_dev {
	struct list_head	list;
	struct bcache_device	disk;
	struct block_device	*bdev;

	struct cache_sb		sb;
	struct cache_sb_disk	*sb_disk;
	struct bio		sb_bio;
	struct bio_vec		sb_bv[1];
	struct closure		sb_write;
	struct semaphore	sb_write_mutex;

	/* Refcount on the cache set. Always nonzero when we're caching. */
	refcount_t		count;
	struct work_struct	detach;

	/*
	 * Device might not be running if it's dirty and the cache set hasn't
	 * showed up yet.
	 */
	atomic_t		running;

	/*
	 * Writes take a shared lock from start to finish; scanning for dirty
	 * data to refill the rb tree requires an exclusive lock.
	 */
	struct rw_semaphore	writeback_lock;

	/*
	 * Nonzero, and writeback has a refcount (d->count), iff there is dirty
	 * data in the cache. Protected by writeback_lock; must have an
	 * shared lock to set and exclusive lock to clear.
	 */
	atomic_t		has_dirty;

#define BCH_CACHE_READA_ALL		0
#define BCH_CACHE_READA_META_ONLY	1
	unsigned int		cache_readahead_policy;
	struct bch_ratelimit	writeback_rate;
	struct delayed_work	writeback_rate_update;

	/* Limit number of writeback bios in flight */
	struct semaphore	in_flight;
	struct task_struct	*writeback_thread;
	struct workqueue_struct	*writeback_write_wq;

	struct keybuf		writeback_keys;

	struct task_struct	*status_update_thread;
	/*
	 * Order the write-half of writeback operations strongly in dispatch
	 * order.  (Maintain LBA order; don't allow reads completing out of
	 * order to re-order the writes...)
	 */
	struct closure_waitlist writeback_ordering_wait;
	atomic_t		writeback_sequence_next;

	/* For tracking sequential IO */
#define RECENT_IO_BITS	7
#define RECENT_IO	(1 << RECENT_IO_BITS)
	struct io		io[RECENT_IO];
	struct hlist_head	io_hash[RECENT_IO + 1];
	struct list_head	io_lru;
	spinlock_t		io_lock;

	struct cache_accounting	accounting;

	/* The rest of this all shows up in sysfs */
	unsigned int		sequential_cutoff;

	unsigned int		io_disable:1;
	unsigned int		verify:1;
	unsigned int		bypass_torture_test:1;

	unsigned int		partial_stripes_expensive:1;
	unsigned int		writeback_metadata:1;
	unsigned int		writeback_running:1;
	unsigned int		writeback_consider_fragment:1;
	unsigned char		writeback_percent;
	unsigned int		writeback_delay;

	uint64_t		writeback_rate_target;
	int64_t			writeback_rate_proportional;
	int64_t			writeback_rate_integral;
	int64_t			writeback_rate_integral_scaled;
	int32_t			writeback_rate_change;

	unsigned int		writeback_rate_update_seconds;
	unsigned int		writeback_rate_i_term_inverse;
	unsigned int		writeback_rate_p_term_inverse;
	unsigned int		writeback_rate_fp_term_low;
	unsigned int		writeback_rate_fp_term_mid;
	unsigned int		writeback_rate_fp_term_high;
	unsigned int		writeback_rate_minimum;

	enum stop_on_failure	stop_when_cache_set_failed;
#define DEFAULT_CACHED_DEV_ERROR_LIMIT	64
	atomic_t		io_errors;
	unsigned int		error_limit;
	unsigned int		offline_seconds;

	/*
	 * Retry to update writeback_rate if contention happens for
	 * down_read(dc->writeback_lock) in update_writeback_rate()
	 */
#define BCH_WBRATE_UPDATE_MAX_SKIPS	15
	unsigned int		rate_update_retry;
};

enum alloc_reserve {
	RESERVE_BTREE,
	RESERVE_PRIO,
	RESERVE_MOVINGGC,
	RESERVE_NONE,
	RESERVE_NR,
};

struct cache {
	struct cache_set	*set;
	struct cache_sb		sb;
	struct cache_sb_disk	*sb_disk;
	struct bio		sb_bio;
	struct bio_vec		sb_bv[1];

	struct kobject		kobj;
	struct block_device	*bdev;

	struct task_struct	*alloc_thread;

	struct closure		prio;
	struct prio_set		*disk_buckets;

	/*
	 * When allocating new buckets, prio_write() gets first dibs - since we
	 * may not be allocate at all without writing priorities and gens.
	 * prio_last_buckets[] contains the last buckets we wrote priorities to
	 * (so gc can mark them as metadata), prio_buckets[] contains the
	 * buckets allocated for the next prio write.
	 */
	uint64_t		*prio_buckets;
	uint64_t		*prio_last_buckets;

	/*
	 * free: Buckets that are ready to be used
	 *
	 * free_inc: Incoming buckets - these are buckets that currently have
	 * cached data in them, and we can't reuse them until after we write
	 * their new gen to disk. After prio_write() finishes writing the new
	 * gens/prios, they'll be moved to the free list (and possibly discarded
	 * in the process)
	 */
	DECLARE_FIFO(long, free)[RESERVE_NR];
	DECLARE_FIFO(long, free_inc);

	size_t			fifo_last_bucket;

	/* Allocation stuff: */
	struct bucket		*buckets;

	DECLARE_HEAP(struct bucket *, heap);

	/*
	 * If nonzero, we know we aren't going to find any buckets to invalidate
	 * until a gc finishes - otherwise we could pointlessly burn a ton of
	 * cpu
	 */
	unsigned int		invalidate_needs_gc;

	bool			discard; /* Get rid of? */

	struct journal_device	journal;

	/* The rest of this all shows up in sysfs */
#define IO_ERROR_SHIFT		20
	atomic_t		io_errors;
	atomic_t		io_count;

	atomic_long_t		meta_sectors_written;
	atomic_long_t		btree_sectors_written;
	atomic_long_t		sectors_written;
};

struct gc_stat {
	size_t			nodes;
	size_t			nodes_pre;
	size_t			key_bytes;

	size_t			nkeys;
	uint64_t		data;	/* sectors */
	unsigned int		in_use; /* percent */
};

/*
 * Flag bits, for how the cache set is shutting down, and what phase it's at:
 *
 * CACHE_SET_UNREGISTERING means we're not just shutting down, we're detaching
 * all the backing devices first (their cached data gets invalidated, and they
 * won't automatically reattach).
 *
 * CACHE_SET_STOPPING always gets set first when we're closing down a cache set;
 * we'll continue to run normally for awhile with CACHE_SET_STOPPING set (i.e.
 * flushing dirty data).
 *
 * CACHE_SET_RUNNING means all cache devices have been registered and journal
 * replay is complete.
 *
 * CACHE_SET_IO_DISABLE is set when bcache is stopping the whold cache set, all
 * external and internal I/O should be denied when this flag is set.
 *
 */
#define CACHE_SET_UNREGISTERING		0
#define	CACHE_SET_STOPPING		1
#define	CACHE_SET_RUNNING		2
#define CACHE_SET_IO_DISABLE		3

struct cache_set {
	struct closure		cl;

	struct list_head	list;
	struct kobject		kobj;
	struct kobject		internal;
	struct dentry		*debug;
	struct cache_accounting accounting;

	unsigned long		flags;
	atomic_t		idle_counter;
	atomic_t		at_max_writeback_rate;

	struct cache		*cache;

	struct bcache_device	**devices;
	unsigned int		devices_max_used;
	atomic_t		attached_dev_nr;
	struct list_head	cached_devs;
	uint64_t		cached_dev_sectors;
	atomic_long_t		flash_dev_dirty_sectors;
	struct closure		caching;

	struct closure		sb_write;
	struct semaphore	sb_write_mutex;

	mempool_t		search;
	mempool_t		bio_meta;
	struct bio_set		bio_split;

	/* For the btree cache */
	struct shrinker		shrink;

	/* For the btree cache and anything allocation related */
	struct mutex		bucket_lock;

	/* log2(bucket_size), in sectors */
	unsigned short		bucket_bits;

	/* log2(block_size), in sectors */
	unsigned short		block_bits;

	/*
	 * Default number of pages for a new btree node - may be less than a
	 * full bucket
	 */
	unsigned int		btree_pages;

	/*
	 * Lists of struct btrees; lru is the list for structs that have memory
	 * allocated for actual btree node, freed is for structs that do not.
	 *
	 * We never free a struct btree, except on shutdown - we just put it on
	 * the btree_cache_freed list and reuse it later. This simplifies the
	 * code, and it doesn't cost us much memory as the memory usage is
	 * dominated by buffers that hold the actual btree node data and those
	 * can be freed - and the number of struct btrees allocated is
	 * effectively bounded.
	 *
	 * btree_cache_freeable effectively is a small cache - we use it because
	 * high order page allocations can be rather expensive, and it's quite
	 * common to delete and allocate btree nodes in quick succession. It
	 * should never grow past ~2-3 nodes in practice.
	 */
	struct list_head	btree_cache;
	struct list_head	btree_cache_freeable;
	struct list_head	btree_cache_freed;

	/* Number of elements in btree_cache + btree_cache_freeable lists */
	unsigned int		btree_cache_used;

	/*
	 * If we need to allocate memory for a new btree node and that
	 * allocation fails, we can cannibalize another node in the btree cache
	 * to satisfy the allocation - lock to guarantee only one thread does
	 * this at a time:
	 */
	wait_queue_head_t	btree_cache_wait;
	struct task_struct	*btree_cache_alloc_lock;
	spinlock_t		btree_cannibalize_lock;

	/*
	 * When we free a btree node, we increment the gen of the bucket the
	 * node is in - but we can't rewrite the prios and gens until we
	 * finished whatever it is we were doing, otherwise after a crash the
	 * btree node would be freed but for say a split, we might not have the
	 * pointers to the new nodes inserted into the btree yet.
	 *
	 * This is a refcount that blocks prio_write() until the new keys are
	 * written.
	 */
	atomic_t		prio_blocked;
	wait_queue_head_t	bucket_wait;

	/*
	 * For any bio we don't skip we subtract the number of sectors from
	 * rescale; when it hits 0 we rescale all the bucket priorities.
	 */
	atomic_t		rescale;
	/*
	 * used for GC, identify if any front side I/Os is inflight
	 */
	atomic_t		search_inflight;
	/*
	 * When we invalidate buckets, we use both the priority and the amount
	 * of good data to determine which buckets to reuse first - to weight
	 * those together consistently we keep track of the smallest nonzero
	 * priority of any bucket.
	 */
	uint16_t		min_prio;

	/*
	 * max(gen - last_gc) for all buckets. When it gets too big we have to
	 * gc to keep gens from wrapping around.
	 */
	uint8_t			need_gc;
	struct gc_stat		gc_stats;
	size_t			nbuckets;
	size_t			avail_nbuckets;

	struct task_struct	*gc_thread;
	/* Where in the btree gc currently is */
	struct bkey		gc_done;

	/*
	 * For automatical garbage collection after writeback completed, this
	 * varialbe is used as bit fields,
	 * - 0000 0001b (BCH_ENABLE_AUTO_GC): enable gc after writeback
	 * - 0000 0010b (BCH_DO_AUTO_GC):     do gc after writeback
	 * This is an optimization for following write request after writeback
	 * finished, but read hit rate dropped due to clean data on cache is
	 * discarded. Unless user explicitly sets it via sysfs, it won't be
	 * enabled.
	 */
#define BCH_ENABLE_AUTO_GC	1
#define BCH_DO_AUTO_GC		2
	uint8_t			gc_after_writeback;

	/*
	 * The allocation code needs gc_mark in struct bucket to be correct, but
	 * it's not while a gc is in progress. Protected by bucket_lock.
	 */
	int			gc_mark_valid;

	/* Counts how many sectors bio_insert has added to the cache */
	atomic_t		sectors_to_gc;
	wait_queue_head_t	gc_wait;

	struct keybuf		moving_gc_keys;
	/* Number of moving GC bios in flight */
	struct semaphore	moving_in_flight;

	struct workqueue_struct	*moving_gc_wq;

	struct btree		*root;

#ifdef CONFIG_BCACHE_DEBUG
	struct btree		*verify_data;
	struct bset		*verify_ondisk;
	struct mutex		verify_lock;
#endif

	uint8_t			set_uuid[16];
	unsigned int		nr_uuids;
	struct uuid_entry	*uuids;
	BKEY_PADDED(uuid_bucket);
	struct closure		uuid_write;
	struct semaphore	uuid_write_mutex;

	/*
	 * A btree node on disk could have too many bsets for an iterator to fit
	 * on the stack - have to dynamically allocate them.
	 * bch_cache_set_alloc() will make sure the pool can allocate iterators
	 * equipped with enough room that can host
	 *     (sb.bucket_size / sb.block_size)
	 * btree_iter_sets, which is more than static MAX_BSETS.
	 */
	mempool_t		fill_iter;

	struct bset_sort_state	sort;

	/* List of buckets we're currently writing data to */
	struct list_head	data_buckets;
	spinlock_t		data_bucket_lock;

	struct journal		journal;

#define CONGESTED_MAX		1024
	unsigned int		congested_last_us;
	atomic_t		congested;

	/* The rest of this all shows up in sysfs */
	unsigned int		congested_read_threshold_us;
	unsigned int		congested_write_threshold_us;

	struct time_stats	btree_gc_time;
	struct time_stats	btree_split_time;
	struct time_stats	btree_read_time;

	atomic_long_t		cache_read_races;
	atomic_long_t		writeback_keys_done;
	atomic_long_t		writeback_keys_failed;

	atomic_long_t		reclaim;
	atomic_long_t		reclaimed_journal_buckets;
	atomic_long_t		flush_write;

	enum			{
		ON_ERROR_UNREGISTER,
		ON_ERROR_PANIC,
	}			on_error;
#define DEFAULT_IO_ERROR_LIMIT 8
	unsigned int		error_limit;
	unsigned int		error_decay;

	unsigned short		journal_delay_ms;
	bool			expensive_debug_checks;
	unsigned int		verify:1;
	unsigned int		key_merging_disabled:1;
	unsigned int		gc_always_rewrite:1;
	unsigned int		shrinker_disabled:1;
	unsigned int		copy_gc_enabled:1;
	unsigned int		idle_max_writeback_rate_enabled:1;

#define BUCKET_HASH_BITS	12
	struct hlist_head	bucket_hash[1 << BUCKET_HASH_BITS];
};

struct bbio {
	unsigned int		submit_time_us;
	union {
		struct bkey	key;
		uint64_t	_pad[3];
		/*
		 * We only need pad = 3 here because we only ever carry around a
		 * single pointer - i.e. the pointer we're doing io to/from.
		 */
	};
	struct bio		bio;
};

#define BTREE_PRIO		USHRT_MAX
#define INITIAL_PRIO		32768U

#define btree_bytes(c)		((c)->btree_pages * PAGE_SIZE)
#define btree_blocks(b)							\
	((unsigned int) (KEY_SIZE(&b->key) >> (b)->c->block_bits))

#define btree_default_blocks(c)						\
	((unsigned int) ((PAGE_SECTORS * (c)->btree_pages) >> (c)->block_bits))

#define bucket_bytes(ca)	((ca)->sb.bucket_size << 9)
#define block_bytes(ca)		((ca)->sb.block_size << 9)

static inline unsigned int meta_bucket_pages(struct cache_sb *sb)
{
	unsigned int n, max_pages;

	max_pages = min_t(unsigned int,
			  __rounddown_pow_of_two(USHRT_MAX) / PAGE_SECTORS,
			  MAX_ORDER_NR_PAGES);

	n = sb->bucket_size / PAGE_SECTORS;
	if (n > max_pages)
		n = max_pages;

	return n;
}

static inline unsigned int meta_bucket_bytes(struct cache_sb *sb)
{
	return meta_bucket_pages(sb) << PAGE_SHIFT;
}

#define prios_per_bucket(ca)						\
	((meta_bucket_bytes(&(ca)->sb) - sizeof(struct prio_set)) /	\
	 sizeof(struct bucket_disk))

#define prio_buckets(ca)						\
	DIV_ROUND_UP((size_t) (ca)->sb.nbuckets, prios_per_bucket(ca))

static inline size_t sector_to_bucket(struct cache_set *c, sector_t s)
{
	return s >> c->bucket_bits;
}

static inline sector_t bucket_to_sector(struct cache_set *c, size_t b)
{
	return ((sector_t) b) << c->bucket_bits;
}

static inline sector_t bucket_remainder(struct cache_set *c, sector_t s)
{
	return s & (c->cache->sb.bucket_size - 1);
}

static inline size_t PTR_BUCKET_NR(struct cache_set *c,
				   const struct bkey *k,
				   unsigned int ptr)
{
	return sector_to_bucket(c, PTR_OFFSET(k, ptr));
}

static inline struct bucket *PTR_BUCKET(struct cache_set *c,
					const struct bkey *k,
					unsigned int ptr)
{
	return c->cache->buckets + PTR_BUCKET_NR(c, k, ptr);
}

static inline uint8_t gen_after(uint8_t a, uint8_t b)
{
	uint8_t r = a - b;

	return r > 128U ? 0 : r;
}

static inline uint8_t ptr_stale(struct cache_set *c, const struct bkey *k,
				unsigned int i)
{
	return gen_after(PTR_BUCKET(c, k, i)->gen, PTR_GEN(k, i));
}

static inline bool ptr_available(struct cache_set *c, const struct bkey *k,
				 unsigned int i)
{
	return (PTR_DEV(k, i) < MAX_CACHES_PER_SET) && c->cache;
}

/* Btree key macros */

/*
 * This is used for various on disk data structures - cache_sb, prio_set, bset,
 * jset: The checksum is _always_ the first 8 bytes of these structs
 */
#define csum_set(i)							\
	bch_crc64(((void *) (i)) + sizeof(uint64_t),			\
		  ((void *) bset_bkey_last(i)) -			\
		  (((void *) (i)) + sizeof(uint64_t)))

/* Error handling macros */

#define btree_bug(b, ...)						\
do {									\
	if (bch_cache_set_error((b)->c, __VA_ARGS__))			\
		dump_stack();						\
} while (0)

#define cache_bug(c, ...)						\
do {									\
	if (bch_cache_set_error(c, __VA_ARGS__))			\
		dump_stack();						\
} while (0)

#define btree_bug_on(cond, b, ...)					\
do {									\
	if (cond)							\
		btree_bug(b, __VA_ARGS__);				\
} while (0)

#define cache_bug_on(cond, c, ...)					\
do {									\
	if (cond)							\
		cache_bug(c, __VA_ARGS__);				\
} while (0)

#define cache_set_err_on(cond, c, ...)					\
do {									\
	if (cond)							\
		bch_cache_set_error(c, __VA_ARGS__);			\
} while (0)

/* Looping macros */

#define for_each_bucket(b, ca)						\
	for (b = (ca)->buckets + (ca)->sb.first_bucket;			\
	     b < (ca)->buckets + (ca)->sb.nbuckets; b++)

static inline void cached_dev_put(struct cached_dev *dc)
{
	if (refcount_dec_and_test(&dc->count))
		schedule_work(&dc->detach);
}

static inline bool cached_dev_get(struct cached_dev *dc)
{
	if (!refcount_inc_not_zero(&dc->count))
		return false;

	/* Paired with the mb in cached_dev_attach */
	smp_mb__after_atomic();
	return true;
}

/*
 * bucket_gc_gen() returns the difference between the bucket's current gen and
 * the oldest gen of any pointer into that bucket in the btree (last_gc).
 */

static inline uint8_t bucket_gc_gen(struct bucket *b)
{
	return b->gen - b->last_gc;
}

#define BUCKET_GC_GEN_MAX	96U

#define kobj_attribute_write(n, fn)					\
	static struct kobj_attribute ksysfs_##n = __ATTR(n, 0200, NULL, fn)

#define kobj_attribute_rw(n, show, store)				\
	static struct kobj_attribute ksysfs_##n =			\
		__ATTR(n, 0600, show, store)

static inline void wake_up_allocators(struct cache_set *c)
{
	struct cache *ca = c->cache;

	wake_up_process(ca->alloc_thread);
}

static inline void closure_bio_submit(struct cache_set *c,
				      struct bio *bio,
				      struct closure *cl)
{
	closure_get(cl);
	if (unlikely(test_bit(CACHE_SET_IO_DISABLE, &c->flags))) {
		bio->bi_status = BLK_STS_IOERR;
		bio_endio(bio);
		return;
	}
	submit_bio_noacct(bio);
}

/*
 * Prevent the kthread exits directly, and make sure when kthread_stop()
 * is called to stop a kthread, it is still alive. If a kthread might be
 * stopped by CACHE_SET_IO_DISABLE bit set, wait_for_kthread_stop() is
 * necessary before the kthread returns.
 */
static inline void wait_for_kthread_stop(void)
{
	while (!kthread_should_stop()) {
		set_current_state(TASK_INTERRUPTIBLE);
		schedule();
	}
}

/* Forward declarations */

void bch_count_backing_io_errors(struct cached_dev *dc, struct bio *bio);
void bch_count_io_errors(struct cache *ca, blk_status_t error,
			 int is_read, const char *m);
void bch_bbio_count_io_errors(struct cache_set *c, struct bio *bio,
			      blk_status_t error, const char *m);
void bch_bbio_endio(struct cache_set *c, struct bio *bio,
		    blk_status_t error, const char *m);
void bch_bbio_free(struct bio *bio, struct cache_set *c);
struct bio *bch_bbio_alloc(struct cache_set *c);

void __bch_submit_bbio(struct bio *bio, struct cache_set *c);
void bch_submit_bbio(struct bio *bio, struct cache_set *c,
		     struct bkey *k, unsigned int ptr);

uint8_t bch_inc_gen(struct cache *ca, struct bucket *b);
void bch_rescale_priorities(struct cache_set *c, int sectors);

bool bch_can_invalidate_bucket(struct cache *ca, struct bucket *b);
void __bch_invalidate_one_bucket(struct cache *ca, struct bucket *b);

void __bch_bucket_free(struct cache *ca, struct bucket *b);
void bch_bucket_free(struct cache_set *c, struct bkey *k);

long bch_bucket_alloc(struct cache *ca, unsigned int reserve, bool wait);
int __bch_bucket_alloc_set(struct cache_set *c, unsigned int reserve,
			   struct bkey *k, bool wait);
int bch_bucket_alloc_set(struct cache_set *c, unsigned int reserve,
			 struct bkey *k, bool wait);
bool bch_alloc_sectors(struct cache_set *c, struct bkey *k,
		       unsigned int sectors, unsigned int write_point,
		       unsigned int write_prio, bool wait);
bool bch_cached_dev_error(struct cached_dev *dc);

__printf(2, 3)
bool bch_cache_set_error(struct cache_set *c, const char *fmt, ...);

int bch_prio_write(struct cache *ca, bool wait);
void bch_write_bdev_super(struct cached_dev *dc, struct closure *parent);

extern struct workqueue_struct *bcache_wq;
extern struct workqueue_struct *bch_journal_wq;
extern struct workqueue_struct *bch_flush_wq;
extern struct mutex bch_register_lock;
extern struct list_head bch_cache_sets;

extern const struct kobj_type bch_cached_dev_ktype;
extern const struct kobj_type bch_flash_dev_ktype;
extern const struct kobj_type bch_cache_set_ktype;
extern const struct kobj_type bch_cache_set_internal_ktype;
extern const struct kobj_type bch_cache_ktype;

void bch_cached_dev_release(struct kobject *kobj);
void bch_flash_dev_release(struct kobject *kobj);
void bch_cache_set_release(struct kobject *kobj);
void bch_cache_release(struct kobject *kobj);

int bch_uuid_write(struct cache_set *c);
void bcache_write_super(struct cache_set *c);

int bch_flash_dev_create(struct cache_set *c, uint64_t size);

int bch_cached_dev_attach(struct cached_dev *dc, struct cache_set *c,
			  uint8_t *set_uuid);
void bch_cached_dev_detach(struct cached_dev *dc);
int bch_cached_dev_run(struct cached_dev *dc);
void bcache_device_stop(struct bcache_device *d);

void bch_cache_set_unregister(struct cache_set *c);
void bch_cache_set_stop(struct cache_set *c);

struct cache_set *bch_cache_set_alloc(struct cache_sb *sb);
void bch_btree_cache_free(struct cache_set *c);
int bch_btree_cache_alloc(struct cache_set *c);
void bch_moving_init_cache_set(struct cache_set *c);
int bch_open_buckets_alloc(struct cache_set *c);
void bch_open_buckets_free(struct cache_set *c);

int bch_cache_allocator_start(struct cache *ca);

void bch_debug_exit(void);
void bch_debug_init(void);
void bch_request_exit(void);
int bch_request_init(void);
void bch_btree_exit(void);
int bch_btree_init(void);

#endif /* _BCACHE_H */
