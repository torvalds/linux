/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHE_BSET_H
#define _BCACHE_BSET_H

#include <linux/bcache.h>
#include <linux/kernel.h>
#include <linux/types.h>

#include "util.h" /* for time_stats */

/*
 * BKEYS:
 *
 * A bkey contains a key, a size field, a variable number of pointers, and some
 * ancillary flag bits.
 *
 * We use two different functions for validating bkeys, bch_ptr_invalid and
 * bch_ptr_bad().
 *
 * bch_ptr_invalid() primarily filters out keys and pointers that would be
 * invalid due to some sort of bug, whereas bch_ptr_bad() filters out keys and
 * pointer that occur in normal practice but don't point to real data.
 *
 * The one exception to the rule that ptr_invalid() filters out invalid keys is
 * that it also filters out keys of size 0 - these are keys that have been
 * completely overwritten. It'd be safe to delete these in memory while leaving
 * them on disk, just unnecessary work - so we filter them out when resorting
 * instead.
 *
 * We can't filter out stale keys when we're resorting, because garbage
 * collection needs to find them to ensure bucket gens don't wrap around -
 * unless we're rewriting the btree node those stale keys still exist on disk.
 *
 * We also implement functions here for removing some number of sectors from the
 * front or the back of a bkey - this is mainly used for fixing overlapping
 * extents, by removing the overlapping sectors from the older key.
 *
 * BSETS:
 *
 * A bset is an array of bkeys laid out contiguously in memory in sorted order,
 * along with a header. A btree node is made up of a number of these, written at
 * different times.
 *
 * There could be many of them on disk, but we never allow there to be more than
 * 4 in memory - we lazily resort as needed.
 *
 * We implement code here for creating and maintaining auxiliary search trees
 * (described below) for searching an individial bset, and on top of that we
 * implement a btree iterator.
 *
 * BTREE ITERATOR:
 *
 * Most of the code in bcache doesn't care about an individual bset - it needs
 * to search entire btree nodes and iterate over them in sorted order.
 *
 * The btree iterator code serves both functions; it iterates through the keys
 * in a btree node in sorted order, starting from either keys after a specific
 * point (if you pass it a search key) or the start of the btree node.
 *
 * AUXILIARY SEARCH TREES:
 *
 * Since keys are variable length, we can't use a binary search on a bset - we
 * wouldn't be able to find the start of the next key. But binary searches are
 * slow anyways, due to terrible cache behaviour; bcache originally used binary
 * searches and that code topped out at under 50k lookups/second.
 *
 * So we need to construct some sort of lookup table. Since we only insert keys
 * into the last (unwritten) set, most of the keys within a given btree node are
 * usually in sets that are mostly constant. We use two different types of
 * lookup tables to take advantage of this.
 *
 * Both lookup tables share in common that they don't index every key in the
 * set; they index one key every BSET_CACHELINE bytes, and then a linear search
 * is used for the rest.
 *
 * For sets that have been written to disk and are no longer being inserted
 * into, we construct a binary search tree in an array - traversing a binary
 * search tree in an array gives excellent locality of reference and is very
 * fast, since both children of any node are adjacent to each other in memory
 * (and their grandchildren, and great grandchildren...) - this means
 * prefetching can be used to great effect.
 *
 * It's quite useful performance wise to keep these nodes small - not just
 * because they're more likely to be in L2, but also because we can prefetch
 * more nodes on a single cacheline and thus prefetch more iterations in advance
 * when traversing this tree.
 *
 * Nodes in the auxiliary search tree must contain both a key to compare against
 * (we don't want to fetch the key from the set, that would defeat the purpose),
 * and a pointer to the key. We use a few tricks to compress both of these.
 *
 * To compress the pointer, we take advantage of the fact that one node in the
 * search tree corresponds to precisely BSET_CACHELINE bytes in the set. We have
 * a function (to_inorder()) that takes the index of a node in a binary tree and
 * returns what its index would be in an inorder traversal, so we only have to
 * store the low bits of the offset.
 *
 * The key is 84 bits (KEY_DEV + key->key, the offset on the device). To
 * compress that,  we take advantage of the fact that when we're traversing the
 * search tree at every iteration we know that both our search key and the key
 * we're looking for lie within some range - bounded by our previous
 * comparisons. (We special case the start of a search so that this is true even
 * at the root of the tree).
 *
 * So we know the key we're looking for is between a and b, and a and b don't
 * differ higher than bit 50, we don't need to check anything higher than bit
 * 50.
 *
 * We don't usually need the rest of the bits, either; we only need enough bits
 * to partition the key range we're currently checking.  Consider key n - the
 * key our auxiliary search tree node corresponds to, and key p, the key
 * immediately preceding n.  The lowest bit we need to store in the auxiliary
 * search tree is the highest bit that differs between n and p.
 *
 * Note that this could be bit 0 - we might sometimes need all 80 bits to do the
 * comparison. But we'd really like our nodes in the auxiliary search tree to be
 * of fixed size.
 *
 * The solution is to make them fixed size, and when we're constructing a node
 * check if p and n differed in the bits we needed them to. If they don't we
 * flag that node, and when doing lookups we fallback to comparing against the
 * real key. As long as this doesn't happen to often (and it seems to reliably
 * happen a bit less than 1% of the time), we win - even on failures, that key
 * is then more likely to be in cache than if we were doing binary searches all
 * the way, since we're touching so much less memory.
 *
 * The keys in the auxiliary search tree are stored in (software) floating
 * point, with an exponent and a mantissa. The exponent needs to be big enough
 * to address all the bits in the original key, but the number of bits in the
 * mantissa is somewhat arbitrary; more bits just gets us fewer failures.
 *
 * We need 7 bits for the exponent and 3 bits for the key's offset (since keys
 * are 8 byte aligned); using 22 bits for the mantissa means a node is 4 bytes.
 * We need one node per 128 bytes in the btree node, which means the auxiliary
 * search trees take up 3% as much memory as the btree itself.
 *
 * Constructing these auxiliary search trees is moderately expensive, and we
 * don't want to be constantly rebuilding the search tree for the last set
 * whenever we insert another key into it. For the unwritten set, we use a much
 * simpler lookup table - it's just a flat array, so index i in the lookup table
 * corresponds to the i range of BSET_CACHELINE bytes in the set. Indexing
 * within each byte range works the same as with the auxiliary search trees.
 *
 * These are much easier to keep up to date when we insert a key - we do it
 * somewhat lazily; when we shift a key up we usually just increment the pointer
 * to it, only when it would overflow do we go to the trouble of finding the
 * first key in that range of bytes again.
 */

struct btree_keys;
struct btree_iter;
struct btree_iter_set;
struct bkey_float;

#define MAX_BSETS		4U

struct bset_tree {
	/*
	 * We construct a binary tree in an array as if the array
	 * started at 1, so that things line up on the same cachelines
	 * better: see comments in bset.c at cacheline_to_bkey() for
	 * details
	 */

	/* size of the binary tree and prev array */
	unsigned		size;

	/* function of size - precalculated for to_inorder() */
	unsigned		extra;

	/* copy of the last key in the set */
	struct bkey		end;
	struct bkey_float	*tree;

	/*
	 * The nodes in the bset tree point to specific keys - this
	 * array holds the sizes of the previous key.
	 *
	 * Conceptually it's a member of struct bkey_float, but we want
	 * to keep bkey_float to 4 bytes and prev isn't used in the fast
	 * path.
	 */
	uint8_t			*prev;

	/* The actual btree node, with pointers to each sorted set */
	struct bset		*data;
};

struct btree_keys_ops {
	bool		(*sort_cmp)(struct btree_iter_set,
				    struct btree_iter_set);
	struct bkey	*(*sort_fixup)(struct btree_iter *, struct bkey *);
	bool		(*insert_fixup)(struct btree_keys *, struct bkey *,
					struct btree_iter *, struct bkey *);
	bool		(*key_invalid)(struct btree_keys *,
				       const struct bkey *);
	bool		(*key_bad)(struct btree_keys *, const struct bkey *);
	bool		(*key_merge)(struct btree_keys *,
				     struct bkey *, struct bkey *);
	void		(*key_to_text)(char *, size_t, const struct bkey *);
	void		(*key_dump)(struct btree_keys *, const struct bkey *);

	/*
	 * Only used for deciding whether to use START_KEY(k) or just the key
	 * itself in a couple places
	 */
	bool		is_extents;
};

struct btree_keys {
	const struct btree_keys_ops	*ops;
	uint8_t			page_order;
	uint8_t			nsets;
	unsigned		last_set_unwritten:1;
	bool			*expensive_debug_checks;

	/*
	 * Sets of sorted keys - the real btree node - plus a binary search tree
	 *
	 * set[0] is special; set[0]->tree, set[0]->prev and set[0]->data point
	 * to the memory we have allocated for this btree node. Additionally,
	 * set[0]->data points to the entire btree node as it exists on disk.
	 */
	struct bset_tree	set[MAX_BSETS];
};

static inline struct bset_tree *bset_tree_last(struct btree_keys *b)
{
	return b->set + b->nsets;
}

static inline bool bset_written(struct btree_keys *b, struct bset_tree *t)
{
	return t <= b->set + b->nsets - b->last_set_unwritten;
}

static inline bool bkey_written(struct btree_keys *b, struct bkey *k)
{
	return !b->last_set_unwritten || k < b->set[b->nsets].data->start;
}

static inline unsigned bset_byte_offset(struct btree_keys *b, struct bset *i)
{
	return ((size_t) i) - ((size_t) b->set->data);
}

static inline unsigned bset_sector_offset(struct btree_keys *b, struct bset *i)
{
	return bset_byte_offset(b, i) >> 9;
}

#define __set_bytes(i, k)	(sizeof(*(i)) + (k) * sizeof(uint64_t))
#define set_bytes(i)		__set_bytes(i, i->keys)

#define __set_blocks(i, k, block_bytes)				\
	DIV_ROUND_UP(__set_bytes(i, k), block_bytes)
#define set_blocks(i, block_bytes)				\
	__set_blocks(i, (i)->keys, block_bytes)

static inline size_t bch_btree_keys_u64s_remaining(struct btree_keys *b)
{
	struct bset_tree *t = bset_tree_last(b);

	BUG_ON((PAGE_SIZE << b->page_order) <
	       (bset_byte_offset(b, t->data) + set_bytes(t->data)));

	if (!b->last_set_unwritten)
		return 0;

	return ((PAGE_SIZE << b->page_order) -
		(bset_byte_offset(b, t->data) + set_bytes(t->data))) /
		sizeof(u64);
}

static inline struct bset *bset_next_set(struct btree_keys *b,
					 unsigned block_bytes)
{
	struct bset *i = bset_tree_last(b)->data;

	return ((void *) i) + roundup(set_bytes(i), block_bytes);
}

void bch_btree_keys_free(struct btree_keys *);
int bch_btree_keys_alloc(struct btree_keys *, unsigned, gfp_t);
void bch_btree_keys_init(struct btree_keys *, const struct btree_keys_ops *,
			 bool *);

void bch_bset_init_next(struct btree_keys *, struct bset *, uint64_t);
void bch_bset_build_written_tree(struct btree_keys *);
void bch_bset_fix_invalidated_key(struct btree_keys *, struct bkey *);
bool bch_bkey_try_merge(struct btree_keys *, struct bkey *, struct bkey *);
void bch_bset_insert(struct btree_keys *, struct bkey *, struct bkey *);
unsigned bch_btree_insert_key(struct btree_keys *, struct bkey *,
			      struct bkey *);

enum {
	BTREE_INSERT_STATUS_NO_INSERT = 0,
	BTREE_INSERT_STATUS_INSERT,
	BTREE_INSERT_STATUS_BACK_MERGE,
	BTREE_INSERT_STATUS_OVERWROTE,
	BTREE_INSERT_STATUS_FRONT_MERGE,
};

/* Btree key iteration */

struct btree_iter {
	size_t size, used;
#ifdef CONFIG_BCACHE_DEBUG
	struct btree_keys *b;
#endif
	struct btree_iter_set {
		struct bkey *k, *end;
	} data[MAX_BSETS];
};

typedef bool (*ptr_filter_fn)(struct btree_keys *, const struct bkey *);

struct bkey *bch_btree_iter_next(struct btree_iter *);
struct bkey *bch_btree_iter_next_filter(struct btree_iter *,
					struct btree_keys *, ptr_filter_fn);

void bch_btree_iter_push(struct btree_iter *, struct bkey *, struct bkey *);
struct bkey *bch_btree_iter_init(struct btree_keys *, struct btree_iter *,
				 struct bkey *);

struct bkey *__bch_bset_search(struct btree_keys *, struct bset_tree *,
			       const struct bkey *);

/*
 * Returns the first key that is strictly greater than search
 */
static inline struct bkey *bch_bset_search(struct btree_keys *b,
					   struct bset_tree *t,
					   const struct bkey *search)
{
	return search ? __bch_bset_search(b, t, search) : t->data->start;
}

#define for_each_key_filter(b, k, iter, filter)				\
	for (bch_btree_iter_init((b), (iter), NULL);			\
	     ((k) = bch_btree_iter_next_filter((iter), (b), filter));)

#define for_each_key(b, k, iter)					\
	for (bch_btree_iter_init((b), (iter), NULL);			\
	     ((k) = bch_btree_iter_next(iter));)

/* Sorting */

struct bset_sort_state {
	mempool_t		pool;

	unsigned		page_order;
	unsigned		crit_factor;

	struct time_stats	time;
};

void bch_bset_sort_state_free(struct bset_sort_state *);
int bch_bset_sort_state_init(struct bset_sort_state *, unsigned);
void bch_btree_sort_lazy(struct btree_keys *, struct bset_sort_state *);
void bch_btree_sort_into(struct btree_keys *, struct btree_keys *,
			 struct bset_sort_state *);
void bch_btree_sort_and_fix_extents(struct btree_keys *, struct btree_iter *,
				    struct bset_sort_state *);
void bch_btree_sort_partial(struct btree_keys *, unsigned,
			    struct bset_sort_state *);

static inline void bch_btree_sort(struct btree_keys *b,
				  struct bset_sort_state *state)
{
	bch_btree_sort_partial(b, 0, state);
}

struct bset_stats {
	size_t sets_written, sets_unwritten;
	size_t bytes_written, bytes_unwritten;
	size_t floats, failed;
};

void bch_btree_keys_stats(struct btree_keys *, struct bset_stats *);

/* Bkey utility code */

#define bset_bkey_last(i)	bkey_idx((struct bkey *) (i)->d, (i)->keys)

static inline struct bkey *bset_bkey_idx(struct bset *i, unsigned idx)
{
	return bkey_idx(i->start, idx);
}

static inline void bkey_init(struct bkey *k)
{
	*k = ZERO_KEY;
}

static __always_inline int64_t bkey_cmp(const struct bkey *l,
					const struct bkey *r)
{
	return unlikely(KEY_INODE(l) != KEY_INODE(r))
		? (int64_t) KEY_INODE(l) - (int64_t) KEY_INODE(r)
		: (int64_t) KEY_OFFSET(l) - (int64_t) KEY_OFFSET(r);
}

void bch_bkey_copy_single_ptr(struct bkey *, const struct bkey *,
			      unsigned);
bool __bch_cut_front(const struct bkey *, struct bkey *);
bool __bch_cut_back(const struct bkey *, struct bkey *);

static inline bool bch_cut_front(const struct bkey *where, struct bkey *k)
{
	BUG_ON(bkey_cmp(where, k) > 0);
	return __bch_cut_front(where, k);
}

static inline bool bch_cut_back(const struct bkey *where, struct bkey *k)
{
	BUG_ON(bkey_cmp(where, &START_KEY(k)) < 0);
	return __bch_cut_back(where, k);
}

#define PRECEDING_KEY(_k)					\
({								\
	struct bkey *_ret = NULL;				\
								\
	if (KEY_INODE(_k) || KEY_OFFSET(_k)) {			\
		_ret = &KEY(KEY_INODE(_k), KEY_OFFSET(_k), 0);	\
								\
		if (!_ret->low)					\
			_ret->high--;				\
		_ret->low--;					\
	}							\
								\
	_ret;							\
})

static inline bool bch_ptr_invalid(struct btree_keys *b, const struct bkey *k)
{
	return b->ops->key_invalid(b, k);
}

static inline bool bch_ptr_bad(struct btree_keys *b, const struct bkey *k)
{
	return b->ops->key_bad(b, k);
}

static inline void bch_bkey_to_text(struct btree_keys *b, char *buf,
				    size_t size, const struct bkey *k)
{
	return b->ops->key_to_text(buf, size, k);
}

static inline bool bch_bkey_equal_header(const struct bkey *l,
					 const struct bkey *r)
{
	return (KEY_DIRTY(l) == KEY_DIRTY(r) &&
		KEY_PTRS(l) == KEY_PTRS(r) &&
		KEY_CSUM(l) == KEY_CSUM(r));
}

/* Keylists */

struct keylist {
	union {
		struct bkey		*keys;
		uint64_t		*keys_p;
	};
	union {
		struct bkey		*top;
		uint64_t		*top_p;
	};

	/* Enough room for btree_split's keys without realloc */
#define KEYLIST_INLINE		16
	uint64_t		inline_keys[KEYLIST_INLINE];
};

static inline void bch_keylist_init(struct keylist *l)
{
	l->top_p = l->keys_p = l->inline_keys;
}

static inline void bch_keylist_init_single(struct keylist *l, struct bkey *k)
{
	l->keys = k;
	l->top = bkey_next(k);
}

static inline void bch_keylist_push(struct keylist *l)
{
	l->top = bkey_next(l->top);
}

static inline void bch_keylist_add(struct keylist *l, struct bkey *k)
{
	bkey_copy(l->top, k);
	bch_keylist_push(l);
}

static inline bool bch_keylist_empty(struct keylist *l)
{
	return l->top == l->keys;
}

static inline void bch_keylist_reset(struct keylist *l)
{
	l->top = l->keys;
}

static inline void bch_keylist_free(struct keylist *l)
{
	if (l->keys_p != l->inline_keys)
		kfree(l->keys_p);
}

static inline size_t bch_keylist_nkeys(struct keylist *l)
{
	return l->top_p - l->keys_p;
}

static inline size_t bch_keylist_bytes(struct keylist *l)
{
	return bch_keylist_nkeys(l) * sizeof(uint64_t);
}

struct bkey *bch_keylist_pop(struct keylist *);
void bch_keylist_pop_front(struct keylist *);
int __bch_keylist_realloc(struct keylist *, unsigned);

/* Debug stuff */

#ifdef CONFIG_BCACHE_DEBUG

int __bch_count_data(struct btree_keys *);
void __printf(2, 3) __bch_check_keys(struct btree_keys *, const char *, ...);
void bch_dump_bset(struct btree_keys *, struct bset *, unsigned);
void bch_dump_bucket(struct btree_keys *);

#else

static inline int __bch_count_data(struct btree_keys *b) { return -1; }
static inline void __printf(2, 3)
	__bch_check_keys(struct btree_keys *b, const char *fmt, ...) {}
static inline void bch_dump_bucket(struct btree_keys *b) {}
void bch_dump_bset(struct btree_keys *, struct bset *, unsigned);

#endif

static inline bool btree_keys_expensive_checks(struct btree_keys *b)
{
#ifdef CONFIG_BCACHE_DEBUG
	return *b->expensive_debug_checks;
#else
	return false;
#endif
}

static inline int bch_count_data(struct btree_keys *b)
{
	return btree_keys_expensive_checks(b) ? __bch_count_data(b) : -1;
}

#define bch_check_keys(b, ...)						\
do {									\
	if (btree_keys_expensive_checks(b))				\
		__bch_check_keys(b, __VA_ARGS__);			\
} while (0)

#endif
