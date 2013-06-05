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
 * Our unit of allocation is a bucket, and we we can't arbitrarily allocate and
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

#define pr_fmt(fmt) "bcache: %s() " fmt "\n", __func__

#include <linux/bio.h>
#include <linux/kobject.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/rbtree.h>
#include <linux/rwsem.h>
#include <linux/types.h>
#include <linux/workqueue.h>

#include "util.h"
#include "closure.h"

struct bucket {
	atomic_t	pin;
	uint16_t	prio;
	uint8_t		gen;
	uint8_t		disk_gen;
	uint8_t		last_gc; /* Most out of date gen in the btree */
	uint8_t		gc_gen;
	uint16_t	gc_mark;
};

/*
 * I'd use bitfields for these, but I don't trust the compiler not to screw me
 * as multiple threads touch struct bucket without locking
 */

BITMASK(GC_MARK,	 struct bucket, gc_mark, 0, 2);
#define GC_MARK_RECLAIMABLE	0
#define GC_MARK_DIRTY		1
#define GC_MARK_METADATA	2
BITMASK(GC_SECTORS_USED, struct bucket, gc_mark, 2, 14);

struct bkey {
	uint64_t	high;
	uint64_t	low;
	uint64_t	ptr[];
};

/* Enough for a key with 6 pointers */
#define BKEY_PAD		8

#define BKEY_PADDED(key)					\
	union { struct bkey key; uint64_t key ## _pad[BKEY_PAD]; }

/* Version 0: Cache device
 * Version 1: Backing device
 * Version 2: Seed pointer into btree node checksum
 * Version 3: Cache device with new UUID format
 * Version 4: Backing device with data offset
 */
#define BCACHE_SB_VERSION_CDEV			0
#define BCACHE_SB_VERSION_BDEV			1
#define BCACHE_SB_VERSION_CDEV_WITH_UUID	3
#define BCACHE_SB_VERSION_BDEV_WITH_OFFSET	4
#define BCACHE_SB_MAX_VERSION			4

#define SB_SECTOR		8
#define SB_SIZE			4096
#define SB_LABEL_SIZE		32
#define SB_JOURNAL_BUCKETS	256U
/* SB_JOURNAL_BUCKETS must be divisible by BITS_PER_LONG */
#define MAX_CACHES_PER_SET	8

#define BDEV_DATA_START_DEFAULT	16	/* sectors */

struct cache_sb {
	uint64_t		csum;
	uint64_t		offset;	/* sector where this sb was written */
	uint64_t		version;

	uint8_t			magic[16];

	uint8_t			uuid[16];
	union {
		uint8_t		set_uuid[16];
		uint64_t	set_magic;
	};
	uint8_t			label[SB_LABEL_SIZE];

	uint64_t		flags;
	uint64_t		seq;
	uint64_t		pad[8];

	union {
	struct {
		/* Cache devices */
		uint64_t	nbuckets;	/* device size */

		uint16_t	block_size;	/* sectors */
		uint16_t	bucket_size;	/* sectors */

		uint16_t	nr_in_set;
		uint16_t	nr_this_dev;
	};
	struct {
		/* Backing devices */
		uint64_t	data_offset;

		/*
		 * block_size from the cache device section is still used by
		 * backing devices, so don't add anything here until we fix
		 * things to not need it for backing devices anymore
		 */
	};
	};

	uint32_t		last_mount;	/* time_t */

	uint16_t		first_bucket;
	union {
		uint16_t	njournal_buckets;
		uint16_t	keys;
	};
	uint64_t		d[SB_JOURNAL_BUCKETS];	/* journal buckets */
};

BITMASK(CACHE_SYNC,		struct cache_sb, flags, 0, 1);
BITMASK(CACHE_DISCARD,		struct cache_sb, flags, 1, 1);
BITMASK(CACHE_REPLACEMENT,	struct cache_sb, flags, 2, 3);
#define CACHE_REPLACEMENT_LRU	0U
#define CACHE_REPLACEMENT_FIFO	1U
#define CACHE_REPLACEMENT_RANDOM 2U

BITMASK(BDEV_CACHE_MODE,	struct cache_sb, flags, 0, 4);
#define CACHE_MODE_WRITETHROUGH	0U
#define CACHE_MODE_WRITEBACK	1U
#define CACHE_MODE_WRITEAROUND	2U
#define CACHE_MODE_NONE		3U
BITMASK(BDEV_STATE,		struct cache_sb, flags, 61, 2);
#define BDEV_STATE_NONE		0U
#define BDEV_STATE_CLEAN	1U
#define BDEV_STATE_DIRTY	2U
#define BDEV_STATE_STALE	3U

/* Version 1: Seed pointer into btree node checksum
 */
#define BCACHE_BSET_VERSION	1

/*
 * This is the on disk format for btree nodes - a btree node on disk is a list
 * of these; within each set the keys are sorted
 */
struct bset {
	uint64_t		csum;
	uint64_t		magic;
	uint64_t		seq;
	uint32_t		version;
	uint32_t		keys;

	union {
		struct bkey	start[0];
		uint64_t	d[0];
	};
};

/*
 * On disk format for priorities and gens - see super.c near prio_write() for
 * more.
 */
struct prio_set {
	uint64_t		csum;
	uint64_t		magic;
	uint64_t		seq;
	uint32_t		version;
	uint32_t		pad;

	uint64_t		next_bucket;

	struct bucket_disk {
		uint16_t	prio;
		uint8_t		gen;
	} __attribute((packed)) data[];
};

struct uuid_entry {
	union {
		struct {
			uint8_t		uuid[16];
			uint8_t		label[32];
			uint32_t	first_reg;
			uint32_t	last_reg;
			uint32_t	invalidated;

			uint32_t	flags;
			/* Size of flash only volumes */
			uint64_t	sectors;
		};

		uint8_t	pad[128];
	};
};

BITMASK(UUID_FLASH_ONLY,	struct uuid_entry, flags, 0, 1);

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

typedef bool (keybuf_pred_fn)(struct keybuf *, struct bkey *);

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

#define KEYBUF_NR		100
	DECLARE_ARRAY_ALLOCATOR(struct keybuf_key, freelist, KEYBUF_NR);
};

struct bio_split_pool {
	struct bio_set		*bio_split;
	mempool_t		*bio_split_hook;
};

struct bio_split_hook {
	struct closure		cl;
	struct bio_split_pool	*p;
	struct bio		*bio;
	bio_end_io_t		*bi_end_io;
	void			*bi_private;
};

struct bcache_device {
	struct closure		cl;

	struct kobject		kobj;

	struct cache_set	*c;
	unsigned		id;
#define BCACHEDEVNAME_SIZE	12
	char			name[BCACHEDEVNAME_SIZE];

	struct gendisk		*disk;

	/* If nonzero, we're closing */
	atomic_t		closing;

	/* If nonzero, we're detaching/unregistering from cache set */
	atomic_t		detaching;

	uint64_t		nr_stripes;
	unsigned		stripe_size_bits;
	atomic_t		*stripe_sectors_dirty;

	unsigned long		sectors_dirty_last;
	long			sectors_dirty_derivative;

	mempool_t		*unaligned_bvec;
	struct bio_set		*bio_split;

	unsigned		data_csum:1;

	int (*cache_miss)(struct btree *, struct search *,
			  struct bio *, unsigned);
	int (*ioctl) (struct bcache_device *, fmode_t, unsigned, unsigned long);

	struct bio_split_pool	bio_split_hook;
};

struct io {
	/* Used to track sequential IO so it can be skipped */
	struct hlist_node	hash;
	struct list_head	lru;

	unsigned long		jiffies;
	unsigned		sequential;
	sector_t		last;
};

struct cached_dev {
	struct list_head	list;
	struct bcache_device	disk;
	struct block_device	*bdev;

	struct cache_sb		sb;
	struct bio		sb_bio;
	struct bio_vec		sb_bv[1];
	struct closure_with_waitlist sb_write;

	/* Refcount on the cache set. Always nonzero when we're caching. */
	atomic_t		count;
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

	struct ratelimit	writeback_rate;
	struct delayed_work	writeback_rate_update;

	/*
	 * Internal to the writeback code, so read_dirty() can keep track of
	 * where it's at.
	 */
	sector_t		last_read;

	/* Number of writeback bios in flight */
	atomic_t		in_flight;
	struct closure_with_timer writeback;
	struct closure_waitlist	writeback_wait;

	struct keybuf		writeback_keys;

	/* For tracking sequential IO */
#define RECENT_IO_BITS	7
#define RECENT_IO	(1 << RECENT_IO_BITS)
	struct io		io[RECENT_IO];
	struct hlist_head	io_hash[RECENT_IO + 1];
	struct list_head	io_lru;
	spinlock_t		io_lock;

	struct cache_accounting	accounting;

	/* The rest of this all shows up in sysfs */
	unsigned		sequential_cutoff;
	unsigned		readahead;

	unsigned		sequential_merge:1;
	unsigned		verify:1;

	unsigned		partial_stripes_expensive:1;
	unsigned		writeback_metadata:1;
	unsigned		writeback_running:1;
	unsigned char		writeback_percent;
	unsigned		writeback_delay;

	int			writeback_rate_change;
	int64_t			writeback_rate_derivative;
	uint64_t		writeback_rate_target;

	unsigned		writeback_rate_update_seconds;
	unsigned		writeback_rate_d_term;
	unsigned		writeback_rate_p_term_inverse;
	unsigned		writeback_rate_d_smooth;
};

enum alloc_watermarks {
	WATERMARK_PRIO,
	WATERMARK_METADATA,
	WATERMARK_MOVINGGC,
	WATERMARK_NONE,
	WATERMARK_MAX
};

struct cache {
	struct cache_set	*set;
	struct cache_sb		sb;
	struct bio		sb_bio;
	struct bio_vec		sb_bv[1];

	struct kobject		kobj;
	struct block_device	*bdev;

	unsigned		watermark[WATERMARK_MAX];

	struct task_struct	*alloc_thread;

	struct closure		prio;
	struct prio_set		*disk_buckets;

	/*
	 * When allocating new buckets, prio_write() gets first dibs - since we
	 * may not be allocate at all without writing priorities and gens.
	 * prio_buckets[] contains the last buckets we wrote priorities to (so
	 * gc can mark them as metadata), prio_next[] contains the buckets
	 * allocated for the next prio write.
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
	 *
	 * unused: GC found nothing pointing into these buckets (possibly
	 * because all the data they contained was overwritten), so we only
	 * need to discard them before they can be moved to the free list.
	 */
	DECLARE_FIFO(long, free);
	DECLARE_FIFO(long, free_inc);
	DECLARE_FIFO(long, unused);

	size_t			fifo_last_bucket;

	/* Allocation stuff: */
	struct bucket		*buckets;

	DECLARE_HEAP(struct bucket *, heap);

	/*
	 * max(gen - disk_gen) for all buckets. When it gets too big we have to
	 * call prio_write() to keep gens from wrapping.
	 */
	uint8_t			need_save_prio;
	unsigned		gc_move_threshold;

	/*
	 * If nonzero, we know we aren't going to find any buckets to invalidate
	 * until a gc finishes - otherwise we could pointlessly burn a ton of
	 * cpu
	 */
	unsigned		invalidate_needs_gc:1;

	bool			discard; /* Get rid of? */

	/*
	 * We preallocate structs for issuing discards to buckets, and keep them
	 * on this list when they're not in use; do_discard() issues discards
	 * whenever there's work to do and is called by free_some_buckets() and
	 * when a discard finishes.
	 */
	atomic_t		discards_in_flight;
	struct list_head	discards;

	struct journal_device	journal;

	/* The rest of this all shows up in sysfs */
#define IO_ERROR_SHIFT		20
	atomic_t		io_errors;
	atomic_t		io_count;

	atomic_long_t		meta_sectors_written;
	atomic_long_t		btree_sectors_written;
	atomic_long_t		sectors_written;

	struct bio_split_pool	bio_split_hook;
};

struct gc_stat {
	size_t			nodes;
	size_t			key_bytes;

	size_t			nkeys;
	uint64_t		data;	/* sectors */
	uint64_t		dirty;	/* sectors */
	unsigned		in_use; /* percent */
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
 * CACHE_SET_STOPPING_2 gets set at the last phase, when it's time to shut down
 * the allocation thread.
 */
#define CACHE_SET_UNREGISTERING		0
#define	CACHE_SET_STOPPING		1
#define	CACHE_SET_STOPPING_2		2

struct cache_set {
	struct closure		cl;

	struct list_head	list;
	struct kobject		kobj;
	struct kobject		internal;
	struct dentry		*debug;
	struct cache_accounting accounting;

	unsigned long		flags;

	struct cache_sb		sb;

	struct cache		*cache[MAX_CACHES_PER_SET];
	struct cache		*cache_by_alloc[MAX_CACHES_PER_SET];
	int			caches_loaded;

	struct bcache_device	**devices;
	struct list_head	cached_devs;
	uint64_t		cached_dev_sectors;
	struct closure		caching;

	struct closure_with_waitlist sb_write;

	mempool_t		*search;
	mempool_t		*bio_meta;
	struct bio_set		*bio_split;

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
	unsigned		btree_pages;

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
	unsigned		bucket_cache_used;

	/*
	 * If we need to allocate memory for a new btree node and that
	 * allocation fails, we can cannibalize another node in the btree cache
	 * to satisfy the allocation. However, only one thread can be doing this
	 * at a time, for obvious reasons - try_harder and try_wait are
	 * basically a lock for this that we can wait on asynchronously. The
	 * btree_root() macro releases the lock when it returns.
	 */
	struct closure		*try_harder;
	struct closure_waitlist	try_wait;
	uint64_t		try_harder_start;

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
	struct closure_waitlist	bucket_wait;

	/*
	 * For any bio we don't skip we subtract the number of sectors from
	 * rescale; when it hits 0 we rescale all the bucket priorities.
	 */
	atomic_t		rescale;
	/*
	 * When we invalidate buckets, we use both the priority and the amount
	 * of good data to determine which buckets to reuse first - to weight
	 * those together consistently we keep track of the smallest nonzero
	 * priority of any bucket.
	 */
	uint16_t		min_prio;

	/*
	 * max(gen - gc_gen) for all buckets. When it gets too big we have to gc
	 * to keep gens from wrapping around.
	 */
	uint8_t			need_gc;
	struct gc_stat		gc_stats;
	size_t			nbuckets;

	struct closure_with_waitlist gc;
	/* Where in the btree gc currently is */
	struct bkey		gc_done;

	/*
	 * The allocation code needs gc_mark in struct bucket to be correct, but
	 * it's not while a gc is in progress. Protected by bucket_lock.
	 */
	int			gc_mark_valid;

	/* Counts how many sectors bio_insert has added to the cache */
	atomic_t		sectors_to_gc;

	struct closure		moving_gc;
	struct closure_waitlist	moving_gc_wait;
	struct keybuf		moving_gc_keys;
	/* Number of moving GC bios in flight */
	atomic_t		in_flight;

	struct btree		*root;

#ifdef CONFIG_BCACHE_DEBUG
	struct btree		*verify_data;
	struct mutex		verify_lock;
#endif

	unsigned		nr_uuids;
	struct uuid_entry	*uuids;
	BKEY_PADDED(uuid_bucket);
	struct closure_with_waitlist uuid_write;

	/*
	 * A btree node on disk could have too many bsets for an iterator to fit
	 * on the stack - have to dynamically allocate them
	 */
	mempool_t		*fill_iter;

	/*
	 * btree_sort() is a merge sort and requires temporary space - single
	 * element mempool
	 */
	struct mutex		sort_lock;
	struct bset		*sort;
	unsigned		sort_crit_factor;

	/* List of buckets we're currently writing data to */
	struct list_head	data_buckets;
	spinlock_t		data_bucket_lock;

	struct journal		journal;

#define CONGESTED_MAX		1024
	unsigned		congested_last_us;
	atomic_t		congested;

	/* The rest of this all shows up in sysfs */
	unsigned		congested_read_threshold_us;
	unsigned		congested_write_threshold_us;

	spinlock_t		sort_time_lock;
	struct time_stats	sort_time;
	struct time_stats	btree_gc_time;
	struct time_stats	btree_split_time;
	spinlock_t		btree_read_time_lock;
	struct time_stats	btree_read_time;
	struct time_stats	try_harder_time;

	atomic_long_t		cache_read_races;
	atomic_long_t		writeback_keys_done;
	atomic_long_t		writeback_keys_failed;
	unsigned		error_limit;
	unsigned		error_decay;
	unsigned short		journal_delay_ms;
	unsigned		verify:1;
	unsigned		key_merging_disabled:1;
	unsigned		gc_always_rewrite:1;
	unsigned		shrinker_disabled:1;
	unsigned		copy_gc_enabled:1;

#define BUCKET_HASH_BITS	12
	struct hlist_head	bucket_hash[1 << BUCKET_HASH_BITS];
};

static inline bool key_merging_disabled(struct cache_set *c)
{
#ifdef CONFIG_BCACHE_DEBUG
	return c->key_merging_disabled;
#else
	return 0;
#endif
}

static inline bool SB_IS_BDEV(const struct cache_sb *sb)
{
	return sb->version == BCACHE_SB_VERSION_BDEV
		|| sb->version == BCACHE_SB_VERSION_BDEV_WITH_OFFSET;
}

struct bbio {
	unsigned		submit_time_us;
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

static inline unsigned local_clock_us(void)
{
	return local_clock() >> 10;
}

#define BTREE_PRIO		USHRT_MAX
#define INITIAL_PRIO		32768

#define btree_bytes(c)		((c)->btree_pages * PAGE_SIZE)
#define btree_blocks(b)							\
	((unsigned) (KEY_SIZE(&b->key) >> (b)->c->block_bits))

#define btree_default_blocks(c)						\
	((unsigned) ((PAGE_SECTORS * (c)->btree_pages) >> (c)->block_bits))

#define bucket_pages(c)		((c)->sb.bucket_size / PAGE_SECTORS)
#define bucket_bytes(c)		((c)->sb.bucket_size << 9)
#define block_bytes(c)		((c)->sb.block_size << 9)

#define __set_bytes(i, k)	(sizeof(*(i)) + (k) * sizeof(uint64_t))
#define set_bytes(i)		__set_bytes(i, i->keys)

#define __set_blocks(i, k, c)	DIV_ROUND_UP(__set_bytes(i, k), block_bytes(c))
#define set_blocks(i, c)	__set_blocks(i, (i)->keys, c)

#define node(i, j)		((struct bkey *) ((i)->d + (j)))
#define end(i)			node(i, (i)->keys)

#define index(i, b)							\
	((size_t) (((void *) i - (void *) (b)->sets[0].data) /		\
		   block_bytes(b->c)))

#define btree_data_space(b)	(PAGE_SIZE << (b)->page_order)

#define prios_per_bucket(c)				\
	((bucket_bytes(c) - sizeof(struct prio_set)) /	\
	 sizeof(struct bucket_disk))
#define prio_buckets(c)					\
	DIV_ROUND_UP((size_t) (c)->sb.nbuckets, prios_per_bucket(c))

#define JSET_MAGIC		0x245235c1a3625032ULL
#define PSET_MAGIC		0x6750e15f87337f91ULL
#define BSET_MAGIC		0x90135c78b99e07f5ULL

#define jset_magic(c)		((c)->sb.set_magic ^ JSET_MAGIC)
#define pset_magic(c)		((c)->sb.set_magic ^ PSET_MAGIC)
#define bset_magic(c)		((c)->sb.set_magic ^ BSET_MAGIC)

/* Bkey fields: all units are in sectors */

#define KEY_FIELD(name, field, offset, size)				\
	BITMASK(name, struct bkey, field, offset, size)

#define PTR_FIELD(name, offset, size)					\
	static inline uint64_t name(const struct bkey *k, unsigned i)	\
	{ return (k->ptr[i] >> offset) & ~(((uint64_t) ~0) << size); }	\
									\
	static inline void SET_##name(struct bkey *k, unsigned i, uint64_t v)\
	{								\
		k->ptr[i] &= ~(~((uint64_t) ~0 << size) << offset);	\
		k->ptr[i] |= v << offset;				\
	}

KEY_FIELD(KEY_PTRS,	high, 60, 3)
KEY_FIELD(HEADER_SIZE,	high, 58, 2)
KEY_FIELD(KEY_CSUM,	high, 56, 2)
KEY_FIELD(KEY_PINNED,	high, 55, 1)
KEY_FIELD(KEY_DIRTY,	high, 36, 1)

KEY_FIELD(KEY_SIZE,	high, 20, 16)
KEY_FIELD(KEY_INODE,	high, 0,  20)

/* Next time I change the on disk format, KEY_OFFSET() won't be 64 bits */

static inline uint64_t KEY_OFFSET(const struct bkey *k)
{
	return k->low;
}

static inline void SET_KEY_OFFSET(struct bkey *k, uint64_t v)
{
	k->low = v;
}

PTR_FIELD(PTR_DEV,		51, 12)
PTR_FIELD(PTR_OFFSET,		8,  43)
PTR_FIELD(PTR_GEN,		0,  8)

#define PTR_CHECK_DEV		((1 << 12) - 1)

#define PTR(gen, offset, dev)						\
	((((uint64_t) dev) << 51) | ((uint64_t) offset) << 8 | gen)

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
	return s & (c->sb.bucket_size - 1);
}

static inline struct cache *PTR_CACHE(struct cache_set *c,
				      const struct bkey *k,
				      unsigned ptr)
{
	return c->cache[PTR_DEV(k, ptr)];
}

static inline size_t PTR_BUCKET_NR(struct cache_set *c,
				   const struct bkey *k,
				   unsigned ptr)
{
	return sector_to_bucket(c, PTR_OFFSET(k, ptr));
}

static inline struct bucket *PTR_BUCKET(struct cache_set *c,
					const struct bkey *k,
					unsigned ptr)
{
	return PTR_CACHE(c, k, ptr)->buckets + PTR_BUCKET_NR(c, k, ptr);
}

/* Btree key macros */

/*
 * The high bit being set is a relic from when we used it to do binary
 * searches - it told you where a key started. It's not used anymore,
 * and can probably be safely dropped.
 */
#define KEY(dev, sector, len)						\
((struct bkey) {							\
	.high = (1ULL << 63) | ((uint64_t) (len) << 20) | (dev),	\
	.low = (sector)							\
})

static inline void bkey_init(struct bkey *k)
{
	*k = KEY(0, 0, 0);
}

#define KEY_START(k)		(KEY_OFFSET(k) - KEY_SIZE(k))
#define START_KEY(k)		KEY(KEY_INODE(k), KEY_START(k), 0)
#define MAX_KEY			KEY(~(~0 << 20), ((uint64_t) ~0) >> 1, 0)
#define ZERO_KEY		KEY(0, 0, 0)

/*
 * This is used for various on disk data structures - cache_sb, prio_set, bset,
 * jset: The checksum is _always_ the first 8 bytes of these structs
 */
#define csum_set(i)							\
	bch_crc64(((void *) (i)) + sizeof(uint64_t),			\
	      ((void *) end(i)) - (((void *) (i)) + sizeof(uint64_t)))

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

#define for_each_cache(ca, cs, iter)					\
	for (iter = 0; ca = cs->cache[iter], iter < (cs)->sb.nr_in_set; iter++)

#define for_each_bucket(b, ca)						\
	for (b = (ca)->buckets + (ca)->sb.first_bucket;			\
	     b < (ca)->buckets + (ca)->sb.nbuckets; b++)

static inline void __bkey_put(struct cache_set *c, struct bkey *k)
{
	unsigned i;

	for (i = 0; i < KEY_PTRS(k); i++)
		atomic_dec_bug(&PTR_BUCKET(c, k, i)->pin);
}

static inline void cached_dev_put(struct cached_dev *dc)
{
	if (atomic_dec_and_test(&dc->count))
		schedule_work(&dc->detach);
}

static inline bool cached_dev_get(struct cached_dev *dc)
{
	if (!atomic_inc_not_zero(&dc->count))
		return false;

	/* Paired with the mb in cached_dev_attach */
	smp_mb__after_atomic_inc();
	return true;
}

/*
 * bucket_gc_gen() returns the difference between the bucket's current gen and
 * the oldest gen of any pointer into that bucket in the btree (last_gc).
 *
 * bucket_disk_gen() returns the difference between the current gen and the gen
 * on disk; they're both used to make sure gens don't wrap around.
 */

static inline uint8_t bucket_gc_gen(struct bucket *b)
{
	return b->gen - b->last_gc;
}

static inline uint8_t bucket_disk_gen(struct bucket *b)
{
	return b->gen - b->disk_gen;
}

#define BUCKET_GC_GEN_MAX	96U
#define BUCKET_DISK_GEN_MAX	64U

#define kobj_attribute_write(n, fn)					\
	static struct kobj_attribute ksysfs_##n = __ATTR(n, S_IWUSR, NULL, fn)

#define kobj_attribute_rw(n, show, store)				\
	static struct kobj_attribute ksysfs_##n =			\
		__ATTR(n, S_IWUSR|S_IRUSR, show, store)

static inline void wake_up_allocators(struct cache_set *c)
{
	struct cache *ca;
	unsigned i;

	for_each_cache(ca, c, i)
		wake_up_process(ca->alloc_thread);
}

/* Forward declarations */

void bch_count_io_errors(struct cache *, int, const char *);
void bch_bbio_count_io_errors(struct cache_set *, struct bio *,
			      int, const char *);
void bch_bbio_endio(struct cache_set *, struct bio *, int, const char *);
void bch_bbio_free(struct bio *, struct cache_set *);
struct bio *bch_bbio_alloc(struct cache_set *);

struct bio *bch_bio_split(struct bio *, int, gfp_t, struct bio_set *);
void bch_generic_make_request(struct bio *, struct bio_split_pool *);
void __bch_submit_bbio(struct bio *, struct cache_set *);
void bch_submit_bbio(struct bio *, struct cache_set *, struct bkey *, unsigned);

uint8_t bch_inc_gen(struct cache *, struct bucket *);
void bch_rescale_priorities(struct cache_set *, int);
bool bch_bucket_add_unused(struct cache *, struct bucket *);

long bch_bucket_alloc(struct cache *, unsigned, struct closure *);
void bch_bucket_free(struct cache_set *, struct bkey *);

int __bch_bucket_alloc_set(struct cache_set *, unsigned,
			   struct bkey *, int, struct closure *);
int bch_bucket_alloc_set(struct cache_set *, unsigned,
			 struct bkey *, int, struct closure *);

__printf(2, 3)
bool bch_cache_set_error(struct cache_set *, const char *, ...);

void bch_prio_write(struct cache *);
void bch_write_bdev_super(struct cached_dev *, struct closure *);

extern struct workqueue_struct *bcache_wq, *bch_gc_wq;
extern const char * const bch_cache_modes[];
extern struct mutex bch_register_lock;
extern struct list_head bch_cache_sets;

extern struct kobj_type bch_cached_dev_ktype;
extern struct kobj_type bch_flash_dev_ktype;
extern struct kobj_type bch_cache_set_ktype;
extern struct kobj_type bch_cache_set_internal_ktype;
extern struct kobj_type bch_cache_ktype;

void bch_cached_dev_release(struct kobject *);
void bch_flash_dev_release(struct kobject *);
void bch_cache_set_release(struct kobject *);
void bch_cache_release(struct kobject *);

int bch_uuid_write(struct cache_set *);
void bcache_write_super(struct cache_set *);

int bch_flash_dev_create(struct cache_set *c, uint64_t size);

int bch_cached_dev_attach(struct cached_dev *, struct cache_set *);
void bch_cached_dev_detach(struct cached_dev *);
void bch_cached_dev_run(struct cached_dev *);
void bcache_device_stop(struct bcache_device *);

void bch_cache_set_unregister(struct cache_set *);
void bch_cache_set_stop(struct cache_set *);

struct cache_set *bch_cache_set_alloc(struct cache_sb *);
void bch_btree_cache_free(struct cache_set *);
int bch_btree_cache_alloc(struct cache_set *);
void bch_moving_init_cache_set(struct cache_set *);

int bch_cache_allocator_start(struct cache *ca);
void bch_cache_allocator_exit(struct cache *ca);
int bch_cache_allocator_init(struct cache *ca);

void bch_debug_exit(void);
int bch_debug_init(struct kobject *);
void bch_writeback_exit(void);
int bch_writeback_init(void);
void bch_request_exit(void);
int bch_request_init(void);
void bch_btree_exit(void);
int bch_btree_init(void);

#endif /* _BCACHE_H */
