// SPDX-License-Identifier: GPL-2.0
/*
 * Code for working with individual keys, and sorted sets of keys with in a
 * btree node
 *
 * Copyright 2012 Google, Inc.
 */

#define pr_fmt(fmt) "bcache: %s() " fmt, __func__

#include "util.h"
#include "bset.h"

#include <linux/console.h>
#include <linux/sched/clock.h>
#include <linux/random.h>
#include <linux/prefetch.h>

#ifdef CONFIG_BCACHE_DEBUG

void bch_dump_bset(struct btree_keys *b, struct bset *i, unsigned int set)
{
	struct bkey *k, *next;

	for (k = i->start; k < bset_bkey_last(i); k = next) {
		next = bkey_next(k);

		pr_err("block %u key %u/%u: ", set,
		       (unsigned int) ((u64 *) k - i->d), i->keys);

		if (b->ops->key_dump)
			b->ops->key_dump(b, k);
		else
			pr_cont("%llu:%llu\n", KEY_INODE(k), KEY_OFFSET(k));

		if (next < bset_bkey_last(i) &&
		    bkey_cmp(k, b->ops->is_extents ?
			     &START_KEY(next) : next) > 0)
			pr_err("Key skipped backwards\n");
	}
}

void bch_dump_bucket(struct btree_keys *b)
{
	unsigned int i;

	console_lock();
	for (i = 0; i <= b->nsets; i++)
		bch_dump_bset(b, b->set[i].data,
			      bset_sector_offset(b, b->set[i].data));
	console_unlock();
}

int __bch_count_data(struct btree_keys *b)
{
	unsigned int ret = 0;
	struct btree_iter iter;
	struct bkey *k;

	if (b->ops->is_extents)
		for_each_key(b, k, &iter)
			ret += KEY_SIZE(k);
	return ret;
}

void __bch_check_keys(struct btree_keys *b, const char *fmt, ...)
{
	va_list args;
	struct bkey *k, *p = NULL;
	struct btree_iter iter;
	const char *err;

	for_each_key(b, k, &iter) {
		if (b->ops->is_extents) {
			err = "Keys out of order";
			if (p && bkey_cmp(&START_KEY(p), &START_KEY(k)) > 0)
				goto bug;

			if (bch_ptr_invalid(b, k))
				continue;

			err =  "Overlapping keys";
			if (p && bkey_cmp(p, &START_KEY(k)) > 0)
				goto bug;
		} else {
			if (bch_ptr_bad(b, k))
				continue;

			err = "Duplicate keys";
			if (p && !bkey_cmp(p, k))
				goto bug;
		}
		p = k;
	}
#if 0
	err = "Key larger than btree node key";
	if (p && bkey_cmp(p, &b->key) > 0)
		goto bug;
#endif
	return;
bug:
	bch_dump_bucket(b);

	va_start(args, fmt);
	vprintk(fmt, args);
	va_end(args);

	panic("bch_check_keys error:  %s:\n", err);
}

static void bch_btree_iter_next_check(struct btree_iter *iter)
{
	struct bkey *k = iter->data->k, *next = bkey_next(k);

	if (next < iter->data->end &&
	    bkey_cmp(k, iter->b->ops->is_extents ?
		     &START_KEY(next) : next) > 0) {
		bch_dump_bucket(iter->b);
		panic("Key skipped backwards\n");
	}
}

#else

static inline void bch_btree_iter_next_check(struct btree_iter *iter) {}

#endif

/* Keylists */

int __bch_keylist_realloc(struct keylist *l, unsigned int u64s)
{
	size_t oldsize = bch_keylist_nkeys(l);
	size_t newsize = oldsize + u64s;
	uint64_t *old_keys = l->keys_p == l->inline_keys ? NULL : l->keys_p;
	uint64_t *new_keys;

	newsize = roundup_pow_of_two(newsize);

	if (newsize <= KEYLIST_INLINE ||
	    roundup_pow_of_two(oldsize) == newsize)
		return 0;

	new_keys = krealloc(old_keys, sizeof(uint64_t) * newsize, GFP_NOIO);

	if (!new_keys)
		return -ENOMEM;

	if (!old_keys)
		memcpy(new_keys, l->inline_keys, sizeof(uint64_t) * oldsize);

	l->keys_p = new_keys;
	l->top_p = new_keys + oldsize;

	return 0;
}

/* Pop the top key of keylist by pointing l->top to its previous key */
struct bkey *bch_keylist_pop(struct keylist *l)
{
	struct bkey *k = l->keys;

	if (k == l->top)
		return NULL;

	while (bkey_next(k) != l->top)
		k = bkey_next(k);

	return l->top = k;
}

/* Pop the bottom key of keylist and update l->top_p */
void bch_keylist_pop_front(struct keylist *l)
{
	l->top_p -= bkey_u64s(l->keys);

	memmove(l->keys,
		bkey_next(l->keys),
		bch_keylist_bytes(l));
}

/* Key/pointer manipulation */

void bch_bkey_copy_single_ptr(struct bkey *dest, const struct bkey *src,
			      unsigned int i)
{
	BUG_ON(i > KEY_PTRS(src));

	/* Only copy the header, key, and one pointer. */
	memcpy(dest, src, 2 * sizeof(uint64_t));
	dest->ptr[0] = src->ptr[i];
	SET_KEY_PTRS(dest, 1);
	/* We didn't copy the checksum so clear that bit. */
	SET_KEY_CSUM(dest, 0);
}

bool __bch_cut_front(const struct bkey *where, struct bkey *k)
{
	unsigned int i, len = 0;

	if (bkey_cmp(where, &START_KEY(k)) <= 0)
		return false;

	if (bkey_cmp(where, k) < 0)
		len = KEY_OFFSET(k) - KEY_OFFSET(where);
	else
		bkey_copy_key(k, where);

	for (i = 0; i < KEY_PTRS(k); i++)
		SET_PTR_OFFSET(k, i, PTR_OFFSET(k, i) + KEY_SIZE(k) - len);

	BUG_ON(len > KEY_SIZE(k));
	SET_KEY_SIZE(k, len);
	return true;
}

bool __bch_cut_back(const struct bkey *where, struct bkey *k)
{
	unsigned int len = 0;

	if (bkey_cmp(where, k) >= 0)
		return false;

	BUG_ON(KEY_INODE(where) != KEY_INODE(k));

	if (bkey_cmp(where, &START_KEY(k)) > 0)
		len = KEY_OFFSET(where) - KEY_START(k);

	bkey_copy_key(k, where);

	BUG_ON(len > KEY_SIZE(k));
	SET_KEY_SIZE(k, len);
	return true;
}

/* Auxiliary search trees */

/* 32 bits total: */
#define BKEY_MID_BITS		3
#define BKEY_EXPONENT_BITS	7
#define BKEY_MANTISSA_BITS	(32 - BKEY_MID_BITS - BKEY_EXPONENT_BITS)
#define BKEY_MANTISSA_MASK	((1 << BKEY_MANTISSA_BITS) - 1)

struct bkey_float {
	unsigned int	exponent:BKEY_EXPONENT_BITS;
	unsigned int	m:BKEY_MID_BITS;
	unsigned int	mantissa:BKEY_MANTISSA_BITS;
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

/* Space required for the btree node keys */
static inline size_t btree_keys_bytes(struct btree_keys *b)
{
	return PAGE_SIZE << b->page_order;
}

static inline size_t btree_keys_cachelines(struct btree_keys *b)
{
	return btree_keys_bytes(b) / BSET_CACHELINE;
}

/* Space required for the auxiliary search trees */
static inline size_t bset_tree_bytes(struct btree_keys *b)
{
	return btree_keys_cachelines(b) * sizeof(struct bkey_float);
}

/* Space required for the prev pointers */
static inline size_t bset_prev_bytes(struct btree_keys *b)
{
	return btree_keys_cachelines(b) * sizeof(uint8_t);
}

/* Memory allocation */

void bch_btree_keys_free(struct btree_keys *b)
{
	struct bset_tree *t = b->set;

	if (bset_prev_bytes(b) < PAGE_SIZE)
		kfree(t->prev);
	else
		free_pages((unsigned long) t->prev,
			   get_order(bset_prev_bytes(b)));

	if (bset_tree_bytes(b) < PAGE_SIZE)
		kfree(t->tree);
	else
		free_pages((unsigned long) t->tree,
			   get_order(bset_tree_bytes(b)));

	free_pages((unsigned long) t->data, b->page_order);

	t->prev = NULL;
	t->tree = NULL;
	t->data = NULL;
}

int bch_btree_keys_alloc(struct btree_keys *b,
			 unsigned int page_order,
			 gfp_t gfp)
{
	struct bset_tree *t = b->set;

	BUG_ON(t->data);

	b->page_order = page_order;

	t->data = (void *) __get_free_pages(gfp, b->page_order);
	if (!t->data)
		goto err;

	t->tree = bset_tree_bytes(b) < PAGE_SIZE
		? kmalloc(bset_tree_bytes(b), gfp)
		: (void *) __get_free_pages(gfp, get_order(bset_tree_bytes(b)));
	if (!t->tree)
		goto err;

	t->prev = bset_prev_bytes(b) < PAGE_SIZE
		? kmalloc(bset_prev_bytes(b), gfp)
		: (void *) __get_free_pages(gfp, get_order(bset_prev_bytes(b)));
	if (!t->prev)
		goto err;

	return 0;
err:
	bch_btree_keys_free(b);
	return -ENOMEM;
}

void bch_btree_keys_init(struct btree_keys *b, const struct btree_keys_ops *ops,
			 bool *expensive_debug_checks)
{
	b->ops = ops;
	b->expensive_debug_checks = expensive_debug_checks;
	b->nsets = 0;
	b->last_set_unwritten = 0;

	/*
	 * struct btree_keys in embedded in struct btree, and struct
	 * bset_tree is embedded into struct btree_keys. They are all
	 * initialized as 0 by kzalloc() in mca_bucket_alloc(), and
	 * b->set[0].data is allocated in bch_btree_keys_alloc(), so we
	 * don't have to initiate b->set[].size and b->set[].data here
	 * any more.
	 */
}

/* Binary tree stuff for auxiliary search trees */

/*
 * return array index next to j when does in-order traverse
 * of a binary tree which is stored in a linear array
 */
static unsigned int inorder_next(unsigned int j, unsigned int size)
{
	if (j * 2 + 1 < size) {
		j = j * 2 + 1;

		while (j * 2 < size)
			j *= 2;
	} else
		j >>= ffz(j) + 1;

	return j;
}

/*
 * return array index previous to j when does in-order traverse
 * of a binary tree which is stored in a linear array
 */
static unsigned int inorder_prev(unsigned int j, unsigned int size)
{
	if (j * 2 < size) {
		j = j * 2;

		while (j * 2 + 1 < size)
			j = j * 2 + 1;
	} else
		j >>= ffs(j);

	return j;
}

/*
 * I have no idea why this code works... and I'm the one who wrote it
 *
 * However, I do know what it does:
 * Given a binary tree constructed in an array (i.e. how you normally implement
 * a heap), it converts a node in the tree - referenced by array index - to the
 * index it would have if you did an inorder traversal.
 *
 * Also tested for every j, size up to size somewhere around 6 million.
 *
 * The binary tree starts at array index 1, not 0
 * extra is a function of size:
 *   extra = (size - rounddown_pow_of_two(size - 1)) << 1;
 */
static unsigned int __to_inorder(unsigned int j,
				  unsigned int size,
				  unsigned int extra)
{
	unsigned int b = fls(j);
	unsigned int shift = fls(size - 1) - b;

	j  ^= 1U << (b - 1);
	j <<= 1;
	j  |= 1;
	j <<= shift;

	if (j > extra)
		j -= (j - extra) >> 1;

	return j;
}

/*
 * Return the cacheline index in bset_tree->data, where j is index
 * from a linear array which stores the auxiliar binary tree
 */
static unsigned int to_inorder(unsigned int j, struct bset_tree *t)
{
	return __to_inorder(j, t->size, t->extra);
}

static unsigned int __inorder_to_tree(unsigned int j,
				      unsigned int size,
				      unsigned int extra)
{
	unsigned int shift;

	if (j > extra)
		j += j - extra;

	shift = ffs(j);

	j >>= shift;
	j  |= roundup_pow_of_two(size) >> shift;

	return j;
}

/*
 * Return an index from a linear array which stores the auxiliar binary
 * tree, j is the cacheline index of t->data.
 */
static unsigned int inorder_to_tree(unsigned int j, struct bset_tree *t)
{
	return __inorder_to_tree(j, t->size, t->extra);
}

#if 0
void inorder_test(void)
{
	unsigned long done = 0;
	ktime_t start = ktime_get();

	for (unsigned int size = 2;
	     size < 65536000;
	     size++) {
		unsigned int extra =
			(size - rounddown_pow_of_two(size - 1)) << 1;
		unsigned int i = 1, j = rounddown_pow_of_two(size - 1);

		if (!(size % 4096))
			pr_notice("loop %u, %llu per us\n", size,
			       done / ktime_us_delta(ktime_get(), start));

		while (1) {
			if (__inorder_to_tree(i, size, extra) != j)
				panic("size %10u j %10u i %10u", size, j, i);

			if (__to_inorder(j, size, extra) != i)
				panic("size %10u j %10u i %10u", size, j, i);

			if (j == rounddown_pow_of_two(size) - 1)
				break;

			BUG_ON(inorder_prev(inorder_next(j, size), size) != j);

			j = inorder_next(j, size);
			i++;
		}

		done += size - 1;
	}
}
#endif

/*
 * Cacheline/offset <-> bkey pointer arithmetic:
 *
 * t->tree is a binary search tree in an array; each node corresponds to a key
 * in one cacheline in t->set (BSET_CACHELINE bytes).
 *
 * This means we don't have to store the full index of the key that a node in
 * the binary tree points to; to_inorder() gives us the cacheline, and then
 * bkey_float->m gives us the offset within that cacheline, in units of 8 bytes.
 *
 * cacheline_to_bkey() and friends abstract out all the pointer arithmetic to
 * make this work.
 *
 * To construct the bfloat for an arbitrary key we need to know what the key
 * immediately preceding it is: we have to check if the two keys differ in the
 * bits we're going to store in bkey_float->mantissa. t->prev[j] stores the size
 * of the previous key so we can walk backwards to it from t->tree[j]'s key.
 */

static struct bkey *cacheline_to_bkey(struct bset_tree *t,
				      unsigned int cacheline,
				      unsigned int offset)
{
	return ((void *) t->data) + cacheline * BSET_CACHELINE + offset * 8;
}

static unsigned int bkey_to_cacheline(struct bset_tree *t, struct bkey *k)
{
	return ((void *) k - (void *) t->data) / BSET_CACHELINE;
}

static unsigned int bkey_to_cacheline_offset(struct bset_tree *t,
					 unsigned int cacheline,
					 struct bkey *k)
{
	return (u64 *) k - (u64 *) cacheline_to_bkey(t, cacheline, 0);
}

static struct bkey *tree_to_bkey(struct bset_tree *t, unsigned int j)
{
	return cacheline_to_bkey(t, to_inorder(j, t), t->tree[j].m);
}

static struct bkey *tree_to_prev_bkey(struct bset_tree *t, unsigned int j)
{
	return (void *) (((uint64_t *) tree_to_bkey(t, j)) - t->prev[j]);
}

/*
 * For the write set - the one we're currently inserting keys into - we don't
 * maintain a full search tree, we just keep a simple lookup table in t->prev.
 */
static struct bkey *table_to_bkey(struct bset_tree *t, unsigned int cacheline)
{
	return cacheline_to_bkey(t, cacheline, t->prev[cacheline]);
}

static inline uint64_t shrd128(uint64_t high, uint64_t low, uint8_t shift)
{
	low >>= shift;
	low  |= (high << 1) << (63U - shift);
	return low;
}

/*
 * Calculate mantissa value for struct bkey_float.
 * If most significant bit of f->exponent is not set, then
 *  - f->exponent >> 6 is 0
 *  - p[0] points to bkey->low
 *  - p[-1] borrows bits from KEY_INODE() of bkey->high
 * if most isgnificant bits of f->exponent is set, then
 *  - f->exponent >> 6 is 1
 *  - p[0] points to bits from KEY_INODE() of bkey->high
 *  - p[-1] points to other bits from KEY_INODE() of
 *    bkey->high too.
 * See make_bfloat() to check when most significant bit of f->exponent
 * is set or not.
 */
static inline unsigned int bfloat_mantissa(const struct bkey *k,
				       struct bkey_float *f)
{
	const uint64_t *p = &k->low - (f->exponent >> 6);

	return shrd128(p[-1], p[0], f->exponent & 63) & BKEY_MANTISSA_MASK;
}

static void make_bfloat(struct bset_tree *t, unsigned int j)
{
	struct bkey_float *f = &t->tree[j];
	struct bkey *m = tree_to_bkey(t, j);
	struct bkey *p = tree_to_prev_bkey(t, j);

	struct bkey *l = is_power_of_2(j)
		? t->data->start
		: tree_to_prev_bkey(t, j >> ffs(j));

	struct bkey *r = is_power_of_2(j + 1)
		? bset_bkey_idx(t->data, t->data->keys - bkey_u64s(&t->end))
		: tree_to_bkey(t, j >> (ffz(j) + 1));

	BUG_ON(m < l || m > r);
	BUG_ON(bkey_next(p) != m);

	/*
	 * If l and r have different KEY_INODE values (different backing
	 * device), f->exponent records how many least significant bits
	 * are different in KEY_INODE values and sets most significant
	 * bits to 1 (by +64).
	 * If l and r have same KEY_INODE value, f->exponent records
	 * how many different bits in least significant bits of bkey->low.
	 * See bfloat_mantiss() how the most significant bit of
	 * f->exponent is used to calculate bfloat mantissa value.
	 */
	if (KEY_INODE(l) != KEY_INODE(r))
		f->exponent = fls64(KEY_INODE(r) ^ KEY_INODE(l)) + 64;
	else
		f->exponent = fls64(r->low ^ l->low);

	f->exponent = max_t(int, f->exponent - BKEY_MANTISSA_BITS, 0);

	/*
	 * Setting f->exponent = 127 flags this node as failed, and causes the
	 * lookup code to fall back to comparing against the original key.
	 */

	if (bfloat_mantissa(m, f) != bfloat_mantissa(p, f))
		f->mantissa = bfloat_mantissa(m, f) - 1;
	else
		f->exponent = 127;
}

static void bset_alloc_tree(struct btree_keys *b, struct bset_tree *t)
{
	if (t != b->set) {
		unsigned int j = roundup(t[-1].size,
				     64 / sizeof(struct bkey_float));

		t->tree = t[-1].tree + j;
		t->prev = t[-1].prev + j;
	}

	while (t < b->set + MAX_BSETS)
		t++->size = 0;
}

static void bch_bset_build_unwritten_tree(struct btree_keys *b)
{
	struct bset_tree *t = bset_tree_last(b);

	BUG_ON(b->last_set_unwritten);
	b->last_set_unwritten = 1;

	bset_alloc_tree(b, t);

	if (t->tree != b->set->tree + btree_keys_cachelines(b)) {
		t->prev[0] = bkey_to_cacheline_offset(t, 0, t->data->start);
		t->size = 1;
	}
}

void bch_bset_init_next(struct btree_keys *b, struct bset *i, uint64_t magic)
{
	if (i != b->set->data) {
		b->set[++b->nsets].data = i;
		i->seq = b->set->data->seq;
	} else
		get_random_bytes(&i->seq, sizeof(uint64_t));

	i->magic	= magic;
	i->version	= 0;
	i->keys		= 0;

	bch_bset_build_unwritten_tree(b);
}

/*
 * Build auxiliary binary tree 'struct bset_tree *t', this tree is used to
 * accelerate bkey search in a btree node (pointed by bset_tree->data in
 * memory). After search in the auxiliar tree by calling bset_search_tree(),
 * a struct bset_search_iter is returned which indicates range [l, r] from
 * bset_tree->data where the searching bkey might be inside. Then a followed
 * linear comparison does the exact search, see __bch_bset_search() for how
 * the auxiliary tree is used.
 */
void bch_bset_build_written_tree(struct btree_keys *b)
{
	struct bset_tree *t = bset_tree_last(b);
	struct bkey *prev = NULL, *k = t->data->start;
	unsigned int j, cacheline = 1;

	b->last_set_unwritten = 0;

	bset_alloc_tree(b, t);

	t->size = min_t(unsigned int,
			bkey_to_cacheline(t, bset_bkey_last(t->data)),
			b->set->tree + btree_keys_cachelines(b) - t->tree);

	if (t->size < 2) {
		t->size = 0;
		return;
	}

	t->extra = (t->size - rounddown_pow_of_two(t->size - 1)) << 1;

	/* First we figure out where the first key in each cacheline is */
	for (j = inorder_next(0, t->size);
	     j;
	     j = inorder_next(j, t->size)) {
		while (bkey_to_cacheline(t, k) < cacheline)
			prev = k, k = bkey_next(k);

		t->prev[j] = bkey_u64s(prev);
		t->tree[j].m = bkey_to_cacheline_offset(t, cacheline++, k);
	}

	while (bkey_next(k) != bset_bkey_last(t->data))
		k = bkey_next(k);

	t->end = *k;

	/* Then we build the tree */
	for (j = inorder_next(0, t->size);
	     j;
	     j = inorder_next(j, t->size))
		make_bfloat(t, j);
}

/* Insert */

void bch_bset_fix_invalidated_key(struct btree_keys *b, struct bkey *k)
{
	struct bset_tree *t;
	unsigned int inorder, j = 1;

	for (t = b->set; t <= bset_tree_last(b); t++)
		if (k < bset_bkey_last(t->data))
			goto found_set;

	BUG();
found_set:
	if (!t->size || !bset_written(b, t))
		return;

	inorder = bkey_to_cacheline(t, k);

	if (k == t->data->start)
		goto fix_left;

	if (bkey_next(k) == bset_bkey_last(t->data)) {
		t->end = *k;
		goto fix_right;
	}

	j = inorder_to_tree(inorder, t);

	if (j &&
	    j < t->size &&
	    k == tree_to_bkey(t, j))
fix_left:	do {
			make_bfloat(t, j);
			j = j * 2;
		} while (j < t->size);

	j = inorder_to_tree(inorder + 1, t);

	if (j &&
	    j < t->size &&
	    k == tree_to_prev_bkey(t, j))
fix_right:	do {
			make_bfloat(t, j);
			j = j * 2 + 1;
		} while (j < t->size);
}

static void bch_bset_fix_lookup_table(struct btree_keys *b,
				      struct bset_tree *t,
				      struct bkey *k)
{
	unsigned int shift = bkey_u64s(k);
	unsigned int j = bkey_to_cacheline(t, k);

	/* We're getting called from btree_split() or btree_gc, just bail out */
	if (!t->size)
		return;

	/*
	 * k is the key we just inserted; we need to find the entry in the
	 * lookup table for the first key that is strictly greater than k:
	 * it's either k's cacheline or the next one
	 */
	while (j < t->size &&
	       table_to_bkey(t, j) <= k)
		j++;

	/*
	 * Adjust all the lookup table entries, and find a new key for any that
	 * have gotten too big
	 */
	for (; j < t->size; j++) {
		t->prev[j] += shift;

		if (t->prev[j] > 7) {
			k = table_to_bkey(t, j - 1);

			while (k < cacheline_to_bkey(t, j, 0))
				k = bkey_next(k);

			t->prev[j] = bkey_to_cacheline_offset(t, j, k);
		}
	}

	if (t->size == b->set->tree + btree_keys_cachelines(b) - t->tree)
		return;

	/* Possibly add a new entry to the end of the lookup table */

	for (k = table_to_bkey(t, t->size - 1);
	     k != bset_bkey_last(t->data);
	     k = bkey_next(k))
		if (t->size == bkey_to_cacheline(t, k)) {
			t->prev[t->size] =
				bkey_to_cacheline_offset(t, t->size, k);
			t->size++;
		}
}

/*
 * Tries to merge l and r: l should be lower than r
 * Returns true if we were able to merge. If we did merge, l will be the merged
 * key, r will be untouched.
 */
bool bch_bkey_try_merge(struct btree_keys *b, struct bkey *l, struct bkey *r)
{
	if (!b->ops->key_merge)
		return false;

	/*
	 * Generic header checks
	 * Assumes left and right are in order
	 * Left and right must be exactly aligned
	 */
	if (!bch_bkey_equal_header(l, r) ||
	     bkey_cmp(l, &START_KEY(r)))
		return false;

	return b->ops->key_merge(b, l, r);
}

void bch_bset_insert(struct btree_keys *b, struct bkey *where,
		     struct bkey *insert)
{
	struct bset_tree *t = bset_tree_last(b);

	BUG_ON(!b->last_set_unwritten);
	BUG_ON(bset_byte_offset(b, t->data) +
	       __set_bytes(t->data, t->data->keys + bkey_u64s(insert)) >
	       PAGE_SIZE << b->page_order);

	memmove((uint64_t *) where + bkey_u64s(insert),
		where,
		(void *) bset_bkey_last(t->data) - (void *) where);

	t->data->keys += bkey_u64s(insert);
	bkey_copy(where, insert);
	bch_bset_fix_lookup_table(b, t, where);
}

unsigned int bch_btree_insert_key(struct btree_keys *b, struct bkey *k,
			      struct bkey *replace_key)
{
	unsigned int status = BTREE_INSERT_STATUS_NO_INSERT;
	struct bset *i = bset_tree_last(b)->data;
	struct bkey *m, *prev = NULL;
	struct btree_iter iter;
	struct bkey preceding_key_on_stack = ZERO_KEY;
	struct bkey *preceding_key_p = &preceding_key_on_stack;

	BUG_ON(b->ops->is_extents && !KEY_SIZE(k));

	/*
	 * If k has preceding key, preceding_key_p will be set to address
	 *  of k's preceding key; otherwise preceding_key_p will be set
	 * to NULL inside preceding_key().
	 */
	if (b->ops->is_extents)
		preceding_key(&START_KEY(k), &preceding_key_p);
	else
		preceding_key(k, &preceding_key_p);

	m = bch_btree_iter_init(b, &iter, preceding_key_p);

	if (b->ops->insert_fixup(b, k, &iter, replace_key))
		return status;

	status = BTREE_INSERT_STATUS_INSERT;

	while (m != bset_bkey_last(i) &&
	       bkey_cmp(k, b->ops->is_extents ? &START_KEY(m) : m) > 0)
		prev = m, m = bkey_next(m);

	/* prev is in the tree, if we merge we're done */
	status = BTREE_INSERT_STATUS_BACK_MERGE;
	if (prev &&
	    bch_bkey_try_merge(b, prev, k))
		goto merged;
#if 0
	status = BTREE_INSERT_STATUS_OVERWROTE;
	if (m != bset_bkey_last(i) &&
	    KEY_PTRS(m) == KEY_PTRS(k) && !KEY_SIZE(m))
		goto copy;
#endif
	status = BTREE_INSERT_STATUS_FRONT_MERGE;
	if (m != bset_bkey_last(i) &&
	    bch_bkey_try_merge(b, k, m))
		goto copy;

	bch_bset_insert(b, m, k);
copy:	bkey_copy(m, k);
merged:
	return status;
}

/* Lookup */

struct bset_search_iter {
	struct bkey *l, *r;
};

static struct bset_search_iter bset_search_write_set(struct bset_tree *t,
						     const struct bkey *search)
{
	unsigned int li = 0, ri = t->size;

	while (li + 1 != ri) {
		unsigned int m = (li + ri) >> 1;

		if (bkey_cmp(table_to_bkey(t, m), search) > 0)
			ri = m;
		else
			li = m;
	}

	return (struct bset_search_iter) {
		table_to_bkey(t, li),
		ri < t->size ? table_to_bkey(t, ri) : bset_bkey_last(t->data)
	};
}

static struct bset_search_iter bset_search_tree(struct bset_tree *t,
						const struct bkey *search)
{
	struct bkey *l, *r;
	struct bkey_float *f;
	unsigned int inorder, j, n = 1;

	do {
		unsigned int p = n << 4;

		if (p < t->size)
			prefetch(&t->tree[p]);

		j = n;
		f = &t->tree[j];

		if (likely(f->exponent != 127)) {
			if (f->mantissa >= bfloat_mantissa(search, f))
				n = j * 2;
			else
				n = j * 2 + 1;
		} else {
			if (bkey_cmp(tree_to_bkey(t, j), search) > 0)
				n = j * 2;
			else
				n = j * 2 + 1;
		}
	} while (n < t->size);

	inorder = to_inorder(j, t);

	/*
	 * n would have been the node we recursed to - the low bit tells us if
	 * we recursed left or recursed right.
	 */
	if (n & 1) {
		l = cacheline_to_bkey(t, inorder, f->m);

		if (++inorder != t->size) {
			f = &t->tree[inorder_next(j, t->size)];
			r = cacheline_to_bkey(t, inorder, f->m);
		} else
			r = bset_bkey_last(t->data);
	} else {
		r = cacheline_to_bkey(t, inorder, f->m);

		if (--inorder) {
			f = &t->tree[inorder_prev(j, t->size)];
			l = cacheline_to_bkey(t, inorder, f->m);
		} else
			l = t->data->start;
	}

	return (struct bset_search_iter) {l, r};
}

struct bkey *__bch_bset_search(struct btree_keys *b, struct bset_tree *t,
			       const struct bkey *search)
{
	struct bset_search_iter i;

	/*
	 * First, we search for a cacheline, then lastly we do a linear search
	 * within that cacheline.
	 *
	 * To search for the cacheline, there's three different possibilities:
	 *  * The set is too small to have a search tree, so we just do a linear
	 *    search over the whole set.
	 *  * The set is the one we're currently inserting into; keeping a full
	 *    auxiliary search tree up to date would be too expensive, so we
	 *    use a much simpler lookup table to do a binary search -
	 *    bset_search_write_set().
	 *  * Or we use the auxiliary search tree we constructed earlier -
	 *    bset_search_tree()
	 */

	if (unlikely(!t->size)) {
		i.l = t->data->start;
		i.r = bset_bkey_last(t->data);
	} else if (bset_written(b, t)) {
		/*
		 * Each node in the auxiliary search tree covers a certain range
		 * of bits, and keys above and below the set it covers might
		 * differ outside those bits - so we have to special case the
		 * start and end - handle that here:
		 */

		if (unlikely(bkey_cmp(search, &t->end) >= 0))
			return bset_bkey_last(t->data);

		if (unlikely(bkey_cmp(search, t->data->start) < 0))
			return t->data->start;

		i = bset_search_tree(t, search);
	} else {
		BUG_ON(!b->nsets &&
		       t->size < bkey_to_cacheline(t, bset_bkey_last(t->data)));

		i = bset_search_write_set(t, search);
	}

	if (btree_keys_expensive_checks(b)) {
		BUG_ON(bset_written(b, t) &&
		       i.l != t->data->start &&
		       bkey_cmp(tree_to_prev_bkey(t,
			  inorder_to_tree(bkey_to_cacheline(t, i.l), t)),
				search) > 0);

		BUG_ON(i.r != bset_bkey_last(t->data) &&
		       bkey_cmp(i.r, search) <= 0);
	}

	while (likely(i.l != i.r) &&
	       bkey_cmp(i.l, search) <= 0)
		i.l = bkey_next(i.l);

	return i.l;
}

/* Btree iterator */

typedef bool (btree_iter_cmp_fn)(struct btree_iter_set,
				 struct btree_iter_set);

static inline bool btree_iter_cmp(struct btree_iter_set l,
				  struct btree_iter_set r)
{
	return bkey_cmp(l.k, r.k) > 0;
}

static inline bool btree_iter_end(struct btree_iter *iter)
{
	return !iter->used;
}

void bch_btree_iter_push(struct btree_iter *iter, struct bkey *k,
			 struct bkey *end)
{
	if (k != end)
		BUG_ON(!heap_add(iter,
				 ((struct btree_iter_set) { k, end }),
				 btree_iter_cmp));
}

static struct bkey *__bch_btree_iter_init(struct btree_keys *b,
					  struct btree_iter *iter,
					  struct bkey *search,
					  struct bset_tree *start)
{
	struct bkey *ret = NULL;

	iter->size = ARRAY_SIZE(iter->data);
	iter->used = 0;

#ifdef CONFIG_BCACHE_DEBUG
	iter->b = b;
#endif

	for (; start <= bset_tree_last(b); start++) {
		ret = bch_bset_search(b, start, search);
		bch_btree_iter_push(iter, ret, bset_bkey_last(start->data));
	}

	return ret;
}

struct bkey *bch_btree_iter_init(struct btree_keys *b,
				 struct btree_iter *iter,
				 struct bkey *search)
{
	return __bch_btree_iter_init(b, iter, search, b->set);
}

static inline struct bkey *__bch_btree_iter_next(struct btree_iter *iter,
						 btree_iter_cmp_fn *cmp)
{
	struct btree_iter_set b __maybe_unused;
	struct bkey *ret = NULL;

	if (!btree_iter_end(iter)) {
		bch_btree_iter_next_check(iter);

		ret = iter->data->k;
		iter->data->k = bkey_next(iter->data->k);

		if (iter->data->k > iter->data->end) {
			WARN_ONCE(1, "bset was corrupt!\n");
			iter->data->k = iter->data->end;
		}

		if (iter->data->k == iter->data->end)
			heap_pop(iter, b, cmp);
		else
			heap_sift(iter, 0, cmp);
	}

	return ret;
}

struct bkey *bch_btree_iter_next(struct btree_iter *iter)
{
	return __bch_btree_iter_next(iter, btree_iter_cmp);

}

struct bkey *bch_btree_iter_next_filter(struct btree_iter *iter,
					struct btree_keys *b, ptr_filter_fn fn)
{
	struct bkey *ret;

	do {
		ret = bch_btree_iter_next(iter);
	} while (ret && fn(b, ret));

	return ret;
}

/* Mergesort */

void bch_bset_sort_state_free(struct bset_sort_state *state)
{
	mempool_exit(&state->pool);
}

int bch_bset_sort_state_init(struct bset_sort_state *state,
			     unsigned int page_order)
{
	spin_lock_init(&state->time.lock);

	state->page_order = page_order;
	state->crit_factor = int_sqrt(1 << page_order);

	return mempool_init_page_pool(&state->pool, 1, page_order);
}

static void btree_mergesort(struct btree_keys *b, struct bset *out,
			    struct btree_iter *iter,
			    bool fixup, bool remove_stale)
{
	int i;
	struct bkey *k, *last = NULL;
	BKEY_PADDED(k) tmp;
	bool (*bad)(struct btree_keys *, const struct bkey *) = remove_stale
		? bch_ptr_bad
		: bch_ptr_invalid;

	/* Heapify the iterator, using our comparison function */
	for (i = iter->used / 2 - 1; i >= 0; --i)
		heap_sift(iter, i, b->ops->sort_cmp);

	while (!btree_iter_end(iter)) {
		if (b->ops->sort_fixup && fixup)
			k = b->ops->sort_fixup(iter, &tmp.k);
		else
			k = NULL;

		if (!k)
			k = __bch_btree_iter_next(iter, b->ops->sort_cmp);

		if (bad(b, k))
			continue;

		if (!last) {
			last = out->start;
			bkey_copy(last, k);
		} else if (!bch_bkey_try_merge(b, last, k)) {
			last = bkey_next(last);
			bkey_copy(last, k);
		}
	}

	out->keys = last ? (uint64_t *) bkey_next(last) - out->d : 0;

	pr_debug("sorted %i keys\n", out->keys);
}

static void __btree_sort(struct btree_keys *b, struct btree_iter *iter,
			 unsigned int start, unsigned int order, bool fixup,
			 struct bset_sort_state *state)
{
	uint64_t start_time;
	bool used_mempool = false;
	struct bset *out = (void *) __get_free_pages(__GFP_NOWARN|GFP_NOWAIT,
						     order);
	if (!out) {
		struct page *outp;

		BUG_ON(order > state->page_order);

		outp = mempool_alloc(&state->pool, GFP_NOIO);
		out = page_address(outp);
		used_mempool = true;
		order = state->page_order;
	}

	start_time = local_clock();

	btree_mergesort(b, out, iter, fixup, false);
	b->nsets = start;

	if (!start && order == b->page_order) {
		/*
		 * Our temporary buffer is the same size as the btree node's
		 * buffer, we can just swap buffers instead of doing a big
		 * memcpy()
		 *
		 * Don't worry event 'out' is allocated from mempool, it can
		 * still be swapped here. Because state->pool is a page mempool
		 * creaated by by mempool_init_page_pool(), which allocates
		 * pages by alloc_pages() indeed.
		 */

		out->magic	= b->set->data->magic;
		out->seq	= b->set->data->seq;
		out->version	= b->set->data->version;
		swap(out, b->set->data);
	} else {
		b->set[start].data->keys = out->keys;
		memcpy(b->set[start].data->start, out->start,
		       (void *) bset_bkey_last(out) - (void *) out->start);
	}

	if (used_mempool)
		mempool_free(virt_to_page(out), &state->pool);
	else
		free_pages((unsigned long) out, order);

	bch_bset_build_written_tree(b);

	if (!start)
		bch_time_stats_update(&state->time, start_time);
}

void bch_btree_sort_partial(struct btree_keys *b, unsigned int start,
			    struct bset_sort_state *state)
{
	size_t order = b->page_order, keys = 0;
	struct btree_iter iter;
	int oldsize = bch_count_data(b);

	__bch_btree_iter_init(b, &iter, NULL, &b->set[start]);

	if (start) {
		unsigned int i;

		for (i = start; i <= b->nsets; i++)
			keys += b->set[i].data->keys;

		order = get_order(__set_bytes(b->set->data, keys));
	}

	__btree_sort(b, &iter, start, order, false, state);

	EBUG_ON(oldsize >= 0 && bch_count_data(b) != oldsize);
}

void bch_btree_sort_and_fix_extents(struct btree_keys *b,
				    struct btree_iter *iter,
				    struct bset_sort_state *state)
{
	__btree_sort(b, iter, 0, b->page_order, true, state);
}

void bch_btree_sort_into(struct btree_keys *b, struct btree_keys *new,
			 struct bset_sort_state *state)
{
	uint64_t start_time = local_clock();
	struct btree_iter iter;

	bch_btree_iter_init(b, &iter, NULL);

	btree_mergesort(b, new->set->data, &iter, false, true);

	bch_time_stats_update(&state->time, start_time);

	new->set->size = 0; // XXX: why?
}

#define SORT_CRIT	(4096 / sizeof(uint64_t))

void bch_btree_sort_lazy(struct btree_keys *b, struct bset_sort_state *state)
{
	unsigned int crit = SORT_CRIT;
	int i;

	/* Don't sort if nothing to do */
	if (!b->nsets)
		goto out;

	for (i = b->nsets - 1; i >= 0; --i) {
		crit *= state->crit_factor;

		if (b->set[i].data->keys < crit) {
			bch_btree_sort_partial(b, i, state);
			return;
		}
	}

	/* Sort if we'd overflow */
	if (b->nsets + 1 == MAX_BSETS) {
		bch_btree_sort(b, state);
		return;
	}

out:
	bch_bset_build_written_tree(b);
}

void bch_btree_keys_stats(struct btree_keys *b, struct bset_stats *stats)
{
	unsigned int i;

	for (i = 0; i <= b->nsets; i++) {
		struct bset_tree *t = &b->set[i];
		size_t bytes = t->data->keys * sizeof(uint64_t);
		size_t j;

		if (bset_written(b, t)) {
			stats->sets_written++;
			stats->bytes_written += bytes;

			stats->floats += t->size - 1;

			for (j = 1; j < t->size; j++)
				if (t->tree[j].exponent == 127)
					stats->failed++;
		} else {
			stats->sets_unwritten++;
			stats->bytes_unwritten += bytes;
		}
	}
}
