#ifndef _BCACHE_BSET_H
#define _BCACHE_BSET_H

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

/* Btree key comparison/iteration */

struct btree_iter {
	size_t size, used;
	struct btree_iter_set {
		struct bkey *k, *end;
	} data[MAX_BSETS];
};

struct bset_tree {
	/*
	 * We construct a binary tree in an array as if the array
	 * started at 1, so that things line up on the same cachelines
	 * better: see comments in bset.c at cacheline_to_bkey() for
	 * details
	 */

	/* size of the binary tree and prev array */
	unsigned	size;

	/* function of size - precalculated for to_inorder() */
	unsigned	extra;

	/* copy of the last key in the set */
	struct bkey	end;
	struct bkey_float *tree;

	/*
	 * The nodes in the bset tree point to specific keys - this
	 * array holds the sizes of the previous key.
	 *
	 * Conceptually it's a member of struct bkey_float, but we want
	 * to keep bkey_float to 4 bytes and prev isn't used in the fast
	 * path.
	 */
	uint8_t		*prev;

	/* The actual btree node, with pointers to each sorted set */
	struct bset	*data;
};

static __always_inline int64_t bkey_cmp(const struct bkey *l,
					const struct bkey *r)
{
	return unlikely(KEY_INODE(l) != KEY_INODE(r))
		? (int64_t) KEY_INODE(l) - (int64_t) KEY_INODE(r)
		: (int64_t) KEY_OFFSET(l) - (int64_t) KEY_OFFSET(r);
}

static inline size_t bkey_u64s(const struct bkey *k)
{
	BUG_ON(KEY_CSUM(k) > 1);
	return 2 + KEY_PTRS(k) + (KEY_CSUM(k) ? 1 : 0);
}

static inline size_t bkey_bytes(const struct bkey *k)
{
	return bkey_u64s(k) * sizeof(uint64_t);
}

static inline void bkey_copy(struct bkey *dest, const struct bkey *src)
{
	memcpy(dest, src, bkey_bytes(src));
}

static inline void bkey_copy_key(struct bkey *dest, const struct bkey *src)
{
	if (!src)
		src = &KEY(0, 0, 0);

	SET_KEY_INODE(dest, KEY_INODE(src));
	SET_KEY_OFFSET(dest, KEY_OFFSET(src));
}

static inline struct bkey *bkey_next(const struct bkey *k)
{
	uint64_t *d = (void *) k;
	return (struct bkey *) (d + bkey_u64s(k));
}

/* Keylists */

struct keylist {
	struct bkey		*top;
	union {
		uint64_t		*list;
		struct bkey		*bottom;
	};

	/* Enough room for btree_split's keys without realloc */
#define KEYLIST_INLINE		16
	uint64_t		d[KEYLIST_INLINE];
};

static inline void bch_keylist_init(struct keylist *l)
{
	l->top = (void *) (l->list = l->d);
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
	return l->top == (void *) l->list;
}

static inline void bch_keylist_free(struct keylist *l)
{
	if (l->list != l->d)
		kfree(l->list);
}

void bch_keylist_copy(struct keylist *, struct keylist *);
struct bkey *bch_keylist_pop(struct keylist *);
int bch_keylist_realloc(struct keylist *, int, struct cache_set *);

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

const char *bch_ptr_status(struct cache_set *, const struct bkey *);
bool __bch_ptr_invalid(struct cache_set *, int level, const struct bkey *);
bool bch_ptr_bad(struct btree *, const struct bkey *);

static inline uint8_t gen_after(uint8_t a, uint8_t b)
{
	uint8_t r = a - b;
	return r > 128U ? 0 : r;
}

static inline uint8_t ptr_stale(struct cache_set *c, const struct bkey *k,
				unsigned i)
{
	return gen_after(PTR_BUCKET(c, k, i)->gen, PTR_GEN(k, i));
}

static inline bool ptr_available(struct cache_set *c, const struct bkey *k,
				 unsigned i)
{
	return (PTR_DEV(k, i) < MAX_CACHES_PER_SET) && PTR_CACHE(c, k, i);
}


typedef bool (*ptr_filter_fn)(struct btree *, const struct bkey *);

struct bkey *bch_next_recurse_key(struct btree *, struct bkey *);
struct bkey *bch_btree_iter_next(struct btree_iter *);
struct bkey *bch_btree_iter_next_filter(struct btree_iter *,
					struct btree *, ptr_filter_fn);

void bch_btree_iter_push(struct btree_iter *, struct bkey *, struct bkey *);
struct bkey *__bch_btree_iter_init(struct btree *, struct btree_iter *,
				   struct bkey *, struct bset_tree *);

/* 32 bits total: */
#define BKEY_MID_BITS		3
#define BKEY_EXPONENT_BITS	7
#define BKEY_MANTISSA_BITS	22
#define BKEY_MANTISSA_MASK	((1 << BKEY_MANTISSA_BITS) - 1)

struct bkey_float {
	unsigned	exponent:BKEY_EXPONENT_BITS;
	unsigned	m:BKEY_MID_BITS;
	unsigned	mantissa:BKEY_MANTISSA_BITS;
} __packed;

/*
 * BSET_CACHELINE was originally intended to match the hardware cacheline size -
 * it used to be 64, but I realized the lookup code would touch slightly less
 * memory if it was 128.
 *
 * It definites the number of bytes (in struct bset) per struct bkey_float in
 * the auxiliar search tree - when we're done searching the bset_float tree we
 * have this many bytes left that we do a linear search over.
 *
 * Since (after level 5) every level of the bset_tree is on a new cacheline,
 * we're touching one fewer cacheline in the bset tree in exchange for one more
 * cacheline in the linear search - but the linear search might stop before it
 * gets to the second cacheline.
 */

#define BSET_CACHELINE		128
#define bset_tree_space(b)	(btree_data_space(b) / BSET_CACHELINE)

#define bset_tree_bytes(b)	(bset_tree_space(b) * sizeof(struct bkey_float))
#define bset_prev_bytes(b)	(bset_tree_space(b) * sizeof(uint8_t))

void bch_bset_init_next(struct btree *);

void bch_bset_fix_invalidated_key(struct btree *, struct bkey *);
void bch_bset_fix_lookup_table(struct btree *, struct bkey *);

struct bkey *__bch_bset_search(struct btree *, struct bset_tree *,
			   const struct bkey *);

static inline struct bkey *bch_bset_search(struct btree *b, struct bset_tree *t,
					   const struct bkey *search)
{
	return search ? __bch_bset_search(b, t, search) : t->data->start;
}

bool bch_bkey_try_merge(struct btree *, struct bkey *, struct bkey *);
void bch_btree_sort_lazy(struct btree *);
void bch_btree_sort_into(struct btree *, struct btree *);
void bch_btree_sort_and_fix_extents(struct btree *, struct btree_iter *);
void bch_btree_sort_partial(struct btree *, unsigned);

static inline void bch_btree_sort(struct btree *b)
{
	bch_btree_sort_partial(b, 0);
}

int bch_bset_print_stats(struct cache_set *, char *);

#endif
