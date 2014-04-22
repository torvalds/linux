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
	struct btree		*parent;

	struct mutex		write_lock;

	unsigned long		flags;
	uint16_t		written;	/* would be nice to kill */
	uint8_t			level;

	struct btree_keys	keys;

	/* For outstanding btree writes, used as a lock - protects write_idx */
	struct closure		io;
	struct semaphore	io_mutex;

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

static inline struct bset *btree_bset_first(struct btree *b)
{
	return b->keys.set->data;
}

static inline struct bset *btree_bset_last(struct btree *b)
{
	return bset_tree_last(&b->keys)->data;
}

static inline unsigned bset_block_offset(struct btree *b, struct bset *i)
{
	return bset_sector_offset(&b->keys, i) >> b->c->block_bits;
}

static inline void set_gc_sectors(struct cache_set *c)
{
	atomic_set(&c->sectors_to_gc, c->sb.bucket_size * c->nbuckets / 16);
}

void bkey_put(struct cache_set *c, struct bkey *k);

/* Looping macros */

#define for_each_cached_btree(b, c, iter)				\
	for (iter = 0;							\
	     iter < ARRAY_SIZE((c)->bucket_hash);			\
	     iter++)							\
		hlist_for_each_entry_rcu((b), (c)->bucket_hash + iter, hash)

/* Recursing down the btree */

struct btree_op {
	/* for waiting on btree reserve in btree_split() */
	wait_queue_t		wait;

	/* Btree level at which we start taking write locks */
	short			lock;

	unsigned		insert_collision:1;
};

static inline void bch_btree_op_init(struct btree_op *op, int write_lock_level)
{
	memset(op, 0, sizeof(struct btree_op));
	init_wait(&op->wait);
	op->lock = write_lock_level;
}

static inline void rw_lock(bool w, struct btree *b, int level)
{
	w ? down_write_nested(&b->lock, level + 1)
	  : down_read_nested(&b->lock, level + 1);
	if (w)
		b->seq++;
}

static inline void rw_unlock(bool w, struct btree *b)
{
	if (w)
		b->seq++;
	(w ? up_write : up_read)(&b->lock);
}

void bch_btree_node_read_done(struct btree *);
void __bch_btree_node_write(struct btree *, struct closure *);
void bch_btree_node_write(struct btree *, struct closure *);

void bch_btree_set_root(struct btree *);
struct btree *__bch_btree_node_alloc(struct cache_set *, struct btree_op *,
				     int, bool);
struct btree *bch_btree_node_get(struct cache_set *, struct btree_op *,
				 struct bkey *, int, bool);

int bch_btree_insert_check_key(struct btree *, struct btree_op *,
			       struct bkey *);
int bch_btree_insert(struct cache_set *, struct keylist *,
		     atomic_t *, struct bkey *);

int bch_gc_thread_start(struct cache_set *);
void bch_initial_gc_finish(struct cache_set *);
void bch_moving_gc(struct cache_set *);
int bch_btree_check(struct cache_set *);
void bch_initial_mark_key(struct cache_set *, int, struct bkey *);

static inline void wake_up_gc(struct cache_set *c)
{
	if (c->gc_thread)
		wake_up_process(c->gc_thread);
}

#define MAP_DONE	0
#define MAP_CONTINUE	1

#define MAP_ALL_NODES	0
#define MAP_LEAF_NODES	1

#define MAP_END_KEY	1

typedef int (btree_map_nodes_fn)(struct btree_op *, struct btree *);
int __bch_btree_map_nodes(struct btree_op *, struct cache_set *,
			  struct bkey *, btree_map_nodes_fn *, int);

static inline int bch_btree_map_nodes(struct btree_op *op, struct cache_set *c,
				      struct bkey *from, btree_map_nodes_fn *fn)
{
	return __bch_btree_map_nodes(op, c, from, fn, MAP_ALL_NODES);
}

static inline int bch_btree_map_leaf_nodes(struct btree_op *op,
					   struct cache_set *c,
					   struct bkey *from,
					   btree_map_nodes_fn *fn)
{
	return __bch_btree_map_nodes(op, c, from, fn, MAP_LEAF_NODES);
}

typedef int (btree_map_keys_fn)(struct btree_op *, struct btree *,
				struct bkey *);
int bch_btree_map_keys(struct btree_op *, struct cache_set *,
		       struct bkey *, btree_map_keys_fn *, int);

typedef bool (keybuf_pred_fn)(struct keybuf *, struct bkey *);

void bch_keybuf_init(struct keybuf *);
void bch_refill_keybuf(struct cache_set *, struct keybuf *,
		       struct bkey *, keybuf_pred_fn *);
bool bch_keybuf_check_overlapping(struct keybuf *, struct bkey *,
				  struct bkey *);
void bch_keybuf_del(struct keybuf *, struct keybuf_key *);
struct keybuf_key *bch_keybuf_next(struct keybuf *);
struct keybuf_key *bch_keybuf_next_rescan(struct cache_set *, struct keybuf *,
					  struct bkey *, keybuf_pred_fn *);

#endif
