#ifndef _BCACHE_BTREE_H
#define _BCACHE_BTREE_H

/*
 * THE BTREE:
 *
 * At a high level, bcache's btree is relatively standard b+ tree. All keys and
 * pointers are in the leaves; interior nodes only have pointers to the child
 * nodes.
 *
 * In the interior nodes, a struct bkey always points to a child btree node, and
 * the key is the highest key in the child node - except that the highest key in
 * an interior node is always MAX_KEY. The size field refers to the size on disk
 * of the child node - this would allow us to have variable sized btree nodes
 * (handy for keeping the depth of the btree 1 by expanding just the root).
 *
 * Btree nodes are themselves log structured, but this is hidden fairly
 * thoroughly. Btree nodes on disk will in practice have extents that overlap
 * (because they were written at different times), but in memory we never have
 * overlapping extents - when we read in a btree node from disk, the first thing
 * we do is resort all the sets of keys with a mergesort, and in the same pass
 * we check for overlapping extents and adjust them appropriately.
 *
 * struct btree_op is a central interface to the btree code. It's used for
 * specifying read vs. write locking, and the embedded closure is used for
 * waiting on IO or reserve memory.
 *
 * BTREE CACHE:
 *
 * Btree nodes are cached in memory; traversing the btree might require reading
 * in btree nodes which is handled mostly transparently.
 *
 * bch_btree_node_get() looks up a btree node in the cache and reads it in from
 * disk if necessary. This function is almost never called directly though - the
 * btree() macro is used to get a btree node, call some function on it, and
 * unlock the node after the function returns.
 *
 * The root is special cased - it's taken out of the cache's lru (thus pinning
 * it in memory), so we can find the root of the btree by just dereferencing a
 * pointer instead of looking it up in the cache. This makes locking a bit
 * tricky, since the root pointer is protected by the lock in the btree node it
 * points to - the btree_root() macro handles this.
 *
 * In various places we must be able to allocate memory for multiple btree nodes
 * in order to make forward progress. To do this we use the btree cache itself
 * as a reserve; if __get_free_pages() fails, we'll find a node in the btree
 * cache we can reuse. We can't allow more than one thread to be doing this at a
 * time, so there's a lock, implemented by a pointer to the btree_op closure -
 * this allows the btree_root() macro to implicitly release this lock.
 *
 * BTREE IO:
 *
 * Btree nodes never have to be explicitly read in; bch_btree_node_get() handles
 * this.
 *
 * For writing, we have two btree_write structs embeddded in struct btree - one
 * write in flight, and one being set up, and we toggle between them.
 *
 * Writing is done with a single function -  bch_btree_write() really serves two
 * different purposes and should be broken up into two different functions. When
 * passing now = false, it merely indicates that the node is now dirty - calling
 * it ensures that the dirty keys will be written at some point in the future.
 *
 * When passing now = true, bch_btree_write() causes a write to happen
 * "immediately" (if there was already a write in flight, it'll cause the write
 * to happen as soon as the previous write completes). It returns immediately
 * though - but it takes a refcount on the closure in struct btree_op you passed
 * to it, so a closure_sync() later can be used to wait for the write to
 * complete.
 *
 * This is handy because btree_split() and garbage collection can issue writes
 * in parallel, reducing the amount of time they have to hold write locks.
 *
 * LOCKING:
 *
 * When traversing the btree, we may need write locks starting at some level -
 * inserting a key into the btree will typically only require a write lock on
 * the leaf node.
 *
 * This is specified with the lock field in struct btree_op; lock = 0 means we
 * take write locks at level <= 0, i.e. only leaf nodes. bch_btree_node_get()
 * checks this field and returns the node with the appropriate lock held.
 *
 * If, after traversing the btree, the insertion code discovers it has to split
 * then it must restart from the root and take new locks - to do this it changes
 * the lock field and returns -EINTR, which causes the btree_root() macro to
 * loop.
 *
 * Handling cache misses require a different mechanism for upgrading to a write
 * lock. We do cache lookups with only a read lock held, but if we get a cache
 * miss and we wish to insert this data into the cache, we have to insert a
 * placeholder key to detect races - otherwise, we could race with a write and
 * overwrite the data that was just written to the cache with stale data from
 * the backing device.
 *
 * For this we use a sequence number that write locks and unlocks increment - to
 * insert the check key it unlocks the btree node and then takes a write lock,
 * and fails if the sequence number doesn't match.
 */

#include "bset.h"
#include "debug.h"

struct btree_write {
	atomic_t		*journal;

	/* If btree_split() frees a btree node, it writes a new pointer to that
	 * btree node indicating it was freed; it takes a refcount on
	 * c->prio_blocked because we can't write the gens until the new
	 * pointer is on disk. This allows btree_write_endio() to release the
	 * refcount that btree_split() took.
	 */
	int			prio_blocked;
};

struct btree {
	/* Hottest entries first */
	struct hlist_node	hash;

	/* Key/pointer for this btree node */
	BKEY_PADDED(key);

	/* Single bit - set when accessed, cleared by shrinker */
	unsigned long		accessed;
	unsigned long		seq;
	struct rw_semaphore	lock;
	struct cache_set	*c;

	unsigned long		flags;
	uint16_t		written;	/* would be nice to kill */
	uint8_t			level;
	uint8_t			nsets;
	uint8_t			page_order;

	/*
	 * Set of sorted keys - the real btree node - plus a binary search tree
	 *
	 * sets[0] is special; set[0]->tree, set[0]->prev and set[0]->data point
	 * to the memory we have allocated for this btree node. Additionally,
	 * set[0]->data points to the entire btree node as it exists on disk.
	 */
	struct bset_tree	sets[MAX_BSETS];

	/* For outstanding btree writes, used as a lock - protects write_idx */
	struct closure_with_waitlist	io;

	struct list_head	list;
	struct delayed_work	work;

	struct btree_write	writes[2];
	struct bio		*bio;
};

#define BTREE_FLAG(flag)						\
static inline bool btree_node_ ## flag(struct btree *b)			\
{	return test_bit(BTREE_NODE_ ## flag, &b->flags); }		\
									\
static inline void set_btree_node_ ## flag(struct btree *b)		\
{	set_bit(BTREE_NODE_ ## flag, &b->flags); }			\

enum btree_flags {
	BTREE_NODE_io_error,
	BTREE_NODE_dirty,
	BTREE_NODE_write_idx,
};

BTREE_FLAG(io_error);
BTREE_FLAG(dirty);
BTREE_FLAG(write_idx);

static inline struct btree_write *btree_current_write(struct btree *b)
{
	return b->writes + btree_node_write_idx(b);
}

static inline struct btree_write *btree_prev_write(struct btree *b)
{
	return b->writes + (btree_node_write_idx(b) ^ 1);
}

static inline unsigned bset_offset(struct btree *b, struct bset *i)
{
	return (((size_t) i) - ((size_t) b->sets->data)) >> 9;
}

static inline struct bset *write_block(struct btree *b)
{
	return ((void *) b->sets[0].data) + b->written * block_bytes(b->c);
}

static inline bool bset_written(struct btree *b, struct bset_tree *t)
{
	return t->data < write_block(b);
}

static inline bool bkey_written(struct btree *b, struct bkey *k)
{
	return k < write_block(b)->start;
}

static inline void set_gc_sectors(struct cache_set *c)
{
	atomic_set(&c->sectors_to_gc, c->sb.bucket_size * c->nbuckets / 8);
}

static inline bool bch_ptr_invalid(struct btree *b, const struct bkey *k)
{
	return __bch_ptr_invalid(b->c, b->level, k);
}

static inline struct bkey *bch_btree_iter_init(struct btree *b,
					       struct btree_iter *iter,
					       struct bkey *search)
{
	return __bch_btree_iter_init(b, iter, search, b->sets);
}

/* Looping macros */

#define for_each_cached_btree(b, c, iter)				\
	for (iter = 0;							\
	     iter < ARRAY_SIZE((c)->bucket_hash);			\
	     iter++)							\
		hlist_for_each_entry_rcu((b), (c)->bucket_hash + iter, hash)

#define for_each_key_filter(b, k, iter, filter)				\
	for (bch_btree_iter_init((b), (iter), NULL);			\
	     ((k) = bch_btree_iter_next_filter((iter), b, filter));)

#define for_each_key(b, k, iter)					\
	for (bch_btree_iter_init((b), (iter), NULL);			\
	     ((k) = bch_btree_iter_next(iter));)

/* Recursing down the btree */

struct btree_op {
	struct closure		cl;
	struct cache_set	*c;

	/* Journal entry we have a refcount on */
	atomic_t		*journal;

	/* Bio to be inserted into the cache */
	struct bio		*cache_bio;

	unsigned		inode;

	uint16_t		write_prio;

	/* Btree level at which we start taking write locks */
	short			lock;

	/* Btree insertion type */
	enum {
		BTREE_INSERT,
		BTREE_REPLACE
	} type:8;

	unsigned		csum:1;
	unsigned		skip:1;
	unsigned		flush_journal:1;

	unsigned		insert_data_done:1;
	unsigned		lookup_done:1;
	unsigned		insert_collision:1;

	/* Anything after this point won't get zeroed in do_bio_hook() */

	/* Keys to be inserted */
	struct keylist		keys;
	BKEY_PADDED(replace);
};

void bch_btree_op_init_stack(struct btree_op *);

static inline void rw_lock(bool w, struct btree *b, int level)
{
	w ? down_write_nested(&b->lock, level + 1)
	  : down_read_nested(&b->lock, level + 1);
	if (w)
		b->seq++;
}

static inline void rw_unlock(bool w, struct btree *b)
{
#ifdef CONFIG_BCACHE_EDEBUG
	unsigned i;

	if (w && b->key.ptr[0])
		for (i = 0; i <= b->nsets; i++)
			bch_check_key_order(b, b->sets[i].data);
#endif

	if (w)
		b->seq++;
	(w ? up_write : up_read)(&b->lock);
}

#define insert_lock(s, b)	((b)->level <= (s)->lock)

/*
 * These macros are for recursing down the btree - they handle the details of
 * locking and looking up nodes in the cache for you. They're best treated as
 * mere syntax when reading code that uses them.
 *
 * op->lock determines whether we take a read or a write lock at a given depth.
 * If you've got a read lock and find that you need a write lock (i.e. you're
 * going to have to split), set op->lock and return -EINTR; btree_root() will
 * call you again and you'll have the correct lock.
 */

/**
 * btree - recurse down the btree on a specified key
 * @fn:		function to call, which will be passed the child node
 * @key:	key to recurse on
 * @b:		parent btree node
 * @op:		pointer to struct btree_op
 */
#define btree(fn, key, b, op, ...)					\
({									\
	int _r, l = (b)->level - 1;					\
	bool _w = l <= (op)->lock;					\
	struct btree *_b = bch_btree_node_get((b)->c, key, l, op);	\
	if (!IS_ERR(_b)) {						\
		_r = bch_btree_ ## fn(_b, op, ##__VA_ARGS__);		\
		rw_unlock(_w, _b);					\
	} else								\
		_r = PTR_ERR(_b);					\
	_r;								\
})

/**
 * btree_root - call a function on the root of the btree
 * @fn:		function to call, which will be passed the child node
 * @c:		cache set
 * @op:		pointer to struct btree_op
 */
#define btree_root(fn, c, op, ...)					\
({									\
	int _r = -EINTR;						\
	do {								\
		struct btree *_b = (c)->root;				\
		bool _w = insert_lock(op, _b);				\
		rw_lock(_w, _b, _b->level);				\
		if (_b == (c)->root &&					\
		    _w == insert_lock(op, _b))				\
			_r = bch_btree_ ## fn(_b, op, ##__VA_ARGS__);	\
		rw_unlock(_w, _b);					\
		bch_cannibalize_unlock(c, &(op)->cl);		\
	} while (_r == -EINTR);						\
									\
	_r;								\
})

static inline bool should_split(struct btree *b)
{
	struct bset *i = write_block(b);
	return b->written >= btree_blocks(b) ||
		(i->seq == b->sets[0].data->seq &&
		 b->written + __set_blocks(i, i->keys + 15, b->c)
		 > btree_blocks(b));
}

void bch_btree_node_read(struct btree *);
void bch_btree_node_read_done(struct btree *);
void bch_btree_node_write(struct btree *, struct closure *);

void bch_cannibalize_unlock(struct cache_set *, struct closure *);
void bch_btree_set_root(struct btree *);
struct btree *bch_btree_node_alloc(struct cache_set *, int, struct closure *);
struct btree *bch_btree_node_get(struct cache_set *, struct bkey *,
				int, struct btree_op *);

bool bch_btree_insert_keys(struct btree *, struct btree_op *);
bool bch_btree_insert_check_key(struct btree *, struct btree_op *,
				   struct bio *);
int bch_btree_insert(struct btree_op *, struct cache_set *);

int bch_btree_search_recurse(struct btree *, struct btree_op *);

void bch_queue_gc(struct cache_set *);
size_t bch_btree_gc_finish(struct cache_set *);
void bch_moving_gc(struct closure *);
int bch_btree_check(struct cache_set *, struct btree_op *);
uint8_t __bch_btree_mark_key(struct cache_set *, int, struct bkey *);

void bch_keybuf_init(struct keybuf *, keybuf_pred_fn *);
void bch_refill_keybuf(struct cache_set *, struct keybuf *, struct bkey *);
bool bch_keybuf_check_overlapping(struct keybuf *, struct bkey *,
				  struct bkey *);
void bch_keybuf_del(struct keybuf *, struct keybuf_key *);
struct keybuf_key *bch_keybuf_next(struct keybuf *);
struct keybuf_key *bch_keybuf_next_rescan(struct cache_set *,
					  struct keybuf *, struct bkey *);

#endif
