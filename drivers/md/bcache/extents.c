/*
 * Copyright (C) 2010 Kent Overstreet <kent.overstreet@gmail.com>
 *
 * Uses a block device as cache for other block devices; optimized for SSDs.
 * All allocation is done in buckets, which should match the erase block size
 * of the device.
 *
 * Buckets containing cached data are kept on a heap sorted by priority;
 * bucket priority is increased on cache hit, and periodically all the buckets
 * on the heap have their priority scaled down. This currently is just used as
 * an LRU but in the future should allow for more intelligent heuristics.
 *
 * Buckets have an 8 bit counter; freeing is accomplished by incrementing the
 * counter. Garbage collection is used to remove stale pointers.
 *
 * Indexing is done via a btree; nodes are not necessarily fully sorted, rather
 * as keys are inserted we only sort the pages that have not yet been written.
 * When garbage collection is run, we resort the entire node.
 *
 * All configuration is done via sysfs; see Documentation/bcache.txt.
 */

#include "bcache.h"
#include "btree.h"
#include "debug.h"
#include "extents.h"
#include "writeback.h"

static void sort_key_next(struct btree_iter *iter,
			  struct btree_iter_set *i)
{
	i->k = bkey_next(i->k);

	if (i->k == i->end)
		*i = iter->data[--iter->used];
}

static bool bch_key_sort_cmp(struct btree_iter_set l,
			     struct btree_iter_set r)
{
	int64_t c = bkey_cmp(l.k, r.k);

	return c ? c > 0 : l.k < r.k;
}

static bool __ptr_invalid(struct cache_set *c, const struct bkey *k)
{
	unsigned i;

	for (i = 0; i < KEY_PTRS(k); i++)
		if (ptr_available(c, k, i)) {
			struct cache *ca = PTR_CACHE(c, k, i);
			size_t bucket = PTR_BUCKET_NR(c, k, i);
			size_t r = bucket_remainder(c, PTR_OFFSET(k, i));

			if (KEY_SIZE(k) + r > c->sb.bucket_size ||
			    bucket <  ca->sb.first_bucket ||
			    bucket >= ca->sb.nbuckets)
				return true;
		}

	return false;
}

/* Common among btree and extent ptrs */

static const char *bch_ptr_status(struct cache_set *c, const struct bkey *k)
{
	unsigned i;

	for (i = 0; i < KEY_PTRS(k); i++)
		if (ptr_available(c, k, i)) {
			struct cache *ca = PTR_CACHE(c, k, i);
			size_t bucket = PTR_BUCKET_NR(c, k, i);
			size_t r = bucket_remainder(c, PTR_OFFSET(k, i));

			if (KEY_SIZE(k) + r > c->sb.bucket_size)
				return "bad, length too big";
			if (bucket <  ca->sb.first_bucket)
				return "bad, short offset";
			if (bucket >= ca->sb.nbuckets)
				return "bad, offset past end of device";
			if (ptr_stale(c, k, i))
				return "stale";
		}

	if (!bkey_cmp(k, &ZERO_KEY))
		return "bad, null key";
	if (!KEY_PTRS(k))
		return "bad, no pointers";
	if (!KEY_SIZE(k))
		return "zeroed key";
	return "";
}

void bch_extent_to_text(char *buf, size_t size, const struct bkey *k)
{
	unsigned i = 0;
	char *out = buf, *end = buf + size;

#define p(...)	(out += scnprintf(out, end - out, __VA_ARGS__))

	p("%llu:%llu len %llu -> [", KEY_INODE(k), KEY_START(k), KEY_SIZE(k));

	for (i = 0; i < KEY_PTRS(k); i++) {
		if (i)
			p(", ");

		if (PTR_DEV(k, i) == PTR_CHECK_DEV)
			p("check dev");
		else
			p("%llu:%llu gen %llu", PTR_DEV(k, i),
			  PTR_OFFSET(k, i), PTR_GEN(k, i));
	}

	p("]");

	if (KEY_DIRTY(k))
		p(" dirty");
	if (KEY_CSUM(k))
		p(" cs%llu %llx", KEY_CSUM(k), k->ptr[1]);
#undef p
}

static void bch_bkey_dump(struct btree_keys *keys, const struct bkey *k)
{
	struct btree *b = container_of(keys, struct btree, keys);
	unsigned j;
	char buf[80];

	bch_extent_to_text(buf, sizeof(buf), k);
	printk(" %s", buf);

	for (j = 0; j < KEY_PTRS(k); j++) {
		size_t n = PTR_BUCKET_NR(b->c, k, j);
		printk(" bucket %zu", n);

		if (n >= b->c->sb.first_bucket && n < b->c->sb.nbuckets)
			printk(" prio %i",
			       PTR_BUCKET(b->c, k, j)->prio);
	}

	printk(" %s\n", bch_ptr_status(b->c, k));
}

/* Btree ptrs */

bool __bch_btree_ptr_invalid(struct cache_set *c, const struct bkey *k)
{
	char buf[80];

	if (!KEY_PTRS(k) || !KEY_SIZE(k) || KEY_DIRTY(k))
		goto bad;

	if (__ptr_invalid(c, k))
		goto bad;

	return false;
bad:
	bch_extent_to_text(buf, sizeof(buf), k);
	cache_bug(c, "spotted btree ptr %s: %s", buf, bch_ptr_status(c, k));
	return true;
}

static bool bch_btree_ptr_invalid(struct btree_keys *bk, const struct bkey *k)
{
	struct btree *b = container_of(bk, struct btree, keys);
	return __bch_btree_ptr_invalid(b->c, k);
}

static bool btree_ptr_bad_expensive(struct btree *b, const struct bkey *k)
{
	unsigned i;
	char buf[80];
	struct bucket *g;

	if (mutex_trylock(&b->c->bucket_lock)) {
		for (i = 0; i < KEY_PTRS(k); i++)
			if (ptr_available(b->c, k, i)) {
				g = PTR_BUCKET(b->c, k, i);

				if (KEY_DIRTY(k) ||
				    g->prio != BTREE_PRIO ||
				    (b->c->gc_mark_valid &&
				     GC_MARK(g) != GC_MARK_METADATA))
					goto err;
			}

		mutex_unlock(&b->c->bucket_lock);
	}

	return false;
err:
	mutex_unlock(&b->c->bucket_lock);
	bch_extent_to_text(buf, sizeof(buf), k);
	btree_bug(b,
"inconsistent btree pointer %s: bucket %li pin %i prio %i gen %i last_gc %i mark %llu gc_gen %i",
		  buf, PTR_BUCKET_NR(b->c, k, i), atomic_read(&g->pin),
		  g->prio, g->gen, g->last_gc, GC_MARK(g), g->gc_gen);
	return true;
}

static bool bch_btree_ptr_bad(struct btree_keys *bk, const struct bkey *k)
{
	struct btree *b = container_of(bk, struct btree, keys);
	unsigned i;

	if (!bkey_cmp(k, &ZERO_KEY) ||
	    !KEY_PTRS(k) ||
	    bch_ptr_invalid(bk, k))
		return true;

	for (i = 0; i < KEY_PTRS(k); i++)
		if (!ptr_available(b->c, k, i) ||
		    ptr_stale(b->c, k, i))
			return true;

	if (expensive_debug_checks(b->c) &&
	    btree_ptr_bad_expensive(b, k))
		return true;

	return false;
}

static bool bch_btree_ptr_insert_fixup(struct btree_keys *bk,
				       struct bkey *insert,
				       struct btree_iter *iter,
				       struct bkey *replace_key)
{
	struct btree *b = container_of(bk, struct btree, keys);

	if (!KEY_OFFSET(insert))
		btree_current_write(b)->prio_blocked++;

	return false;
}

const struct btree_keys_ops bch_btree_keys_ops = {
	.sort_cmp	= bch_key_sort_cmp,
	.insert_fixup	= bch_btree_ptr_insert_fixup,
	.key_invalid	= bch_btree_ptr_invalid,
	.key_bad	= bch_btree_ptr_bad,
	.key_to_text	= bch_extent_to_text,
	.key_dump	= bch_bkey_dump,
};

/* Extents */

/*
 * Returns true if l > r - unless l == r, in which case returns true if l is
 * older than r.
 *
 * Necessary for btree_sort_fixup() - if there are multiple keys that compare
 * equal in different sets, we have to process them newest to oldest.
 */
static bool bch_extent_sort_cmp(struct btree_iter_set l,
				struct btree_iter_set r)
{
	int64_t c = bkey_cmp(&START_KEY(l.k), &START_KEY(r.k));

	return c ? c > 0 : l.k < r.k;
}

static struct bkey *bch_extent_sort_fixup(struct btree_iter *iter,
					  struct bkey *tmp)
{
	while (iter->used > 1) {
		struct btree_iter_set *top = iter->data, *i = top + 1;

		if (iter->used > 2 &&
		    bch_extent_sort_cmp(i[0], i[1]))
			i++;

		if (bkey_cmp(top->k, &START_KEY(i->k)) <= 0)
			break;

		if (!KEY_SIZE(i->k)) {
			sort_key_next(iter, i);
			heap_sift(iter, i - top, bch_extent_sort_cmp);
			continue;
		}

		if (top->k > i->k) {
			if (bkey_cmp(top->k, i->k) >= 0)
				sort_key_next(iter, i);
			else
				bch_cut_front(top->k, i->k);

			heap_sift(iter, i - top, bch_extent_sort_cmp);
		} else {
			/* can't happen because of comparison func */
			BUG_ON(!bkey_cmp(&START_KEY(top->k), &START_KEY(i->k)));

			if (bkey_cmp(i->k, top->k) < 0) {
				bkey_copy(tmp, top->k);

				bch_cut_back(&START_KEY(i->k), tmp);
				bch_cut_front(i->k, top->k);
				heap_sift(iter, 0, bch_extent_sort_cmp);

				return tmp;
			} else {
				bch_cut_back(&START_KEY(i->k), top->k);
			}
		}
	}

	return NULL;
}

static bool bch_extent_insert_fixup(struct btree_keys *b,
				    struct bkey *insert,
				    struct btree_iter *iter,
				    struct bkey *replace_key)
{
	struct cache_set *c = container_of(b, struct btree, keys)->c;

	void subtract_dirty(struct bkey *k, uint64_t offset, int sectors)
	{
		if (KEY_DIRTY(k))
			bcache_dev_sectors_dirty_add(c, KEY_INODE(k),
						     offset, -sectors);
	}

	uint64_t old_offset;
	unsigned old_size, sectors_found = 0;

	BUG_ON(!KEY_OFFSET(insert));
	BUG_ON(!KEY_SIZE(insert));

	while (1) {
		struct bkey *k = bch_btree_iter_next(iter);
		if (!k)
			break;

		if (bkey_cmp(&START_KEY(k), insert) >= 0) {
			if (KEY_SIZE(k))
				break;
			else
				continue;
		}

		if (bkey_cmp(k, &START_KEY(insert)) <= 0)
			continue;

		old_offset = KEY_START(k);
		old_size = KEY_SIZE(k);

		/*
		 * We might overlap with 0 size extents; we can't skip these
		 * because if they're in the set we're inserting to we have to
		 * adjust them so they don't overlap with the key we're
		 * inserting. But we don't want to check them for replace
		 * operations.
		 */

		if (replace_key && KEY_SIZE(k)) {
			/*
			 * k might have been split since we inserted/found the
			 * key we're replacing
			 */
			unsigned i;
			uint64_t offset = KEY_START(k) -
				KEY_START(replace_key);

			/* But it must be a subset of the replace key */
			if (KEY_START(k) < KEY_START(replace_key) ||
			    KEY_OFFSET(k) > KEY_OFFSET(replace_key))
				goto check_failed;

			/* We didn't find a key that we were supposed to */
			if (KEY_START(k) > KEY_START(insert) + sectors_found)
				goto check_failed;

			if (!bch_bkey_equal_header(k, replace_key))
				goto check_failed;

			/* skip past gen */
			offset <<= 8;

			BUG_ON(!KEY_PTRS(replace_key));

			for (i = 0; i < KEY_PTRS(replace_key); i++)
				if (k->ptr[i] != replace_key->ptr[i] + offset)
					goto check_failed;

			sectors_found = KEY_OFFSET(k) - KEY_START(insert);
		}

		if (bkey_cmp(insert, k) < 0 &&
		    bkey_cmp(&START_KEY(insert), &START_KEY(k)) > 0) {
			/*
			 * We overlapped in the middle of an existing key: that
			 * means we have to split the old key. But we have to do
			 * slightly different things depending on whether the
			 * old key has been written out yet.
			 */

			struct bkey *top;

			subtract_dirty(k, KEY_START(insert), KEY_SIZE(insert));

			if (bkey_written(b, k)) {
				/*
				 * We insert a new key to cover the top of the
				 * old key, and the old key is modified in place
				 * to represent the bottom split.
				 *
				 * It's completely arbitrary whether the new key
				 * is the top or the bottom, but it has to match
				 * up with what btree_sort_fixup() does - it
				 * doesn't check for this kind of overlap, it
				 * depends on us inserting a new key for the top
				 * here.
				 */
				top = bch_bset_search(b, bset_tree_last(b),
						      insert);
				bch_bset_insert(b, top, k);
			} else {
				BKEY_PADDED(key) temp;
				bkey_copy(&temp.key, k);
				bch_bset_insert(b, k, &temp.key);
				top = bkey_next(k);
			}

			bch_cut_front(insert, top);
			bch_cut_back(&START_KEY(insert), k);
			bch_bset_fix_invalidated_key(b, k);
			goto out;
		}

		if (bkey_cmp(insert, k) < 0) {
			bch_cut_front(insert, k);
		} else {
			if (bkey_cmp(&START_KEY(insert), &START_KEY(k)) > 0)
				old_offset = KEY_START(insert);

			if (bkey_written(b, k) &&
			    bkey_cmp(&START_KEY(insert), &START_KEY(k)) <= 0) {
				/*
				 * Completely overwrote, so we don't have to
				 * invalidate the binary search tree
				 */
				bch_cut_front(k, k);
			} else {
				__bch_cut_back(&START_KEY(insert), k);
				bch_bset_fix_invalidated_key(b, k);
			}
		}

		subtract_dirty(k, old_offset, old_size - KEY_SIZE(k));
	}

check_failed:
	if (replace_key) {
		if (!sectors_found) {
			return true;
		} else if (sectors_found < KEY_SIZE(insert)) {
			SET_KEY_OFFSET(insert, KEY_OFFSET(insert) -
				       (KEY_SIZE(insert) - sectors_found));
			SET_KEY_SIZE(insert, sectors_found);
		}
	}
out:
	if (KEY_DIRTY(insert))
		bcache_dev_sectors_dirty_add(c, KEY_INODE(insert),
					     KEY_START(insert),
					     KEY_SIZE(insert));

	return false;
}

static bool bch_extent_invalid(struct btree_keys *bk, const struct bkey *k)
{
	struct btree *b = container_of(bk, struct btree, keys);
	char buf[80];

	if (!KEY_SIZE(k))
		return true;

	if (KEY_SIZE(k) > KEY_OFFSET(k))
		goto bad;

	if (__ptr_invalid(b->c, k))
		goto bad;

	return false;
bad:
	bch_extent_to_text(buf, sizeof(buf), k);
	cache_bug(b->c, "spotted extent %s: %s", buf, bch_ptr_status(b->c, k));
	return true;
}

static bool bch_extent_bad_expensive(struct btree *b, const struct bkey *k,
				     unsigned ptr)
{
	struct bucket *g = PTR_BUCKET(b->c, k, ptr);
	char buf[80];

	if (mutex_trylock(&b->c->bucket_lock)) {
		if (b->c->gc_mark_valid &&
		    ((GC_MARK(g) != GC_MARK_DIRTY &&
		      KEY_DIRTY(k)) ||
		     GC_MARK(g) == GC_MARK_METADATA))
			goto err;

		if (g->prio == BTREE_PRIO)
			goto err;

		mutex_unlock(&b->c->bucket_lock);
	}

	return false;
err:
	mutex_unlock(&b->c->bucket_lock);
	bch_extent_to_text(buf, sizeof(buf), k);
	btree_bug(b,
"inconsistent extent pointer %s:\nbucket %zu pin %i prio %i gen %i last_gc %i mark %llu gc_gen %i",
		  buf, PTR_BUCKET_NR(b->c, k, ptr), atomic_read(&g->pin),
		  g->prio, g->gen, g->last_gc, GC_MARK(g), g->gc_gen);
	return true;
}

static bool bch_extent_bad(struct btree_keys *bk, const struct bkey *k)
{
	struct btree *b = container_of(bk, struct btree, keys);
	struct bucket *g;
	unsigned i, stale;

	if (!KEY_PTRS(k) ||
	    bch_extent_invalid(bk, k))
		return true;

	for (i = 0; i < KEY_PTRS(k); i++)
		if (!ptr_available(b->c, k, i))
			return true;

	if (!expensive_debug_checks(b->c) && KEY_DIRTY(k))
		return false;

	for (i = 0; i < KEY_PTRS(k); i++) {
		g = PTR_BUCKET(b->c, k, i);
		stale = ptr_stale(b->c, k, i);

		btree_bug_on(stale > 96, b,
			     "key too stale: %i, need_gc %u",
			     stale, b->c->need_gc);

		btree_bug_on(stale && KEY_DIRTY(k) && KEY_SIZE(k),
			     b, "stale dirty pointer");

		if (stale)
			return true;

		if (expensive_debug_checks(b->c) &&
		    bch_extent_bad_expensive(b, k, i))
			return true;
	}

	return false;
}

static uint64_t merge_chksums(struct bkey *l, struct bkey *r)
{
	return (l->ptr[KEY_PTRS(l)] + r->ptr[KEY_PTRS(r)]) &
		~((uint64_t)1 << 63);
}

static bool bch_extent_merge(struct btree_keys *bk, struct bkey *l, struct bkey *r)
{
	struct btree *b = container_of(bk, struct btree, keys);
	unsigned i;

	if (key_merging_disabled(b->c))
		return false;

	for (i = 0; i < KEY_PTRS(l); i++)
		if (l->ptr[i] + PTR(0, KEY_SIZE(l), 0) != r->ptr[i] ||
		    PTR_BUCKET_NR(b->c, l, i) != PTR_BUCKET_NR(b->c, r, i))
			return false;

	/* Keys with no pointers aren't restricted to one bucket and could
	 * overflow KEY_SIZE
	 */
	if (KEY_SIZE(l) + KEY_SIZE(r) > USHRT_MAX) {
		SET_KEY_OFFSET(l, KEY_OFFSET(l) + USHRT_MAX - KEY_SIZE(l));
		SET_KEY_SIZE(l, USHRT_MAX);

		bch_cut_front(l, r);
		return false;
	}

	if (KEY_CSUM(l)) {
		if (KEY_CSUM(r))
			l->ptr[KEY_PTRS(l)] = merge_chksums(l, r);
		else
			SET_KEY_CSUM(l, 0);
	}

	SET_KEY_OFFSET(l, KEY_OFFSET(l) + KEY_SIZE(r));
	SET_KEY_SIZE(l, KEY_SIZE(l) + KEY_SIZE(r));

	return true;
}

const struct btree_keys_ops bch_extent_keys_ops = {
	.sort_cmp	= bch_extent_sort_cmp,
	.sort_fixup	= bch_extent_sort_fixup,
	.insert_fixup	= bch_extent_insert_fixup,
	.key_invalid	= bch_extent_invalid,
	.key_bad	= bch_extent_bad,
	.key_merge	= bch_extent_merge,
	.key_to_text	= bch_extent_to_text,
	.key_dump	= bch_bkey_dump,
	.is_extents	= true,
};
