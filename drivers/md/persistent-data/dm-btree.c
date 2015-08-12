/*
 * Copyright (C) 2011 Red Hat, Inc.
 *
 * This file is released under the GPL.
 */

#include "dm-btree-internal.h"
#include "dm-space-map.h"
#include "dm-transaction-manager.h"

#include <linux/export.h>
#include <linux/device-mapper.h>

#define DM_MSG_PREFIX "btree"

/*----------------------------------------------------------------
 * Array manipulation
 *--------------------------------------------------------------*/
static void memcpy_disk(void *dest, const void *src, size_t len)
	__dm_written_to_disk(src)
{
	memcpy(dest, src, len);
	__dm_unbless_for_disk(src);
}

static void array_insert(void *base, size_t elt_size, unsigned nr_elts,
			 unsigned index, void *elt)
	__dm_written_to_disk(elt)
{
	if (index < nr_elts)
		memmove(base + (elt_size * (index + 1)),
			base + (elt_size * index),
			(nr_elts - index) * elt_size);

	memcpy_disk(base + (elt_size * index), elt, elt_size);
}

/*----------------------------------------------------------------*/

/* makes the assumption that no two keys are the same. */
static int bsearch(struct btree_node *n, uint64_t key, int want_hi)
{
	int lo = -1, hi = le32_to_cpu(n->header.nr_entries);

	while (hi - lo > 1) {
		int mid = lo + ((hi - lo) / 2);
		uint64_t mid_key = le64_to_cpu(n->keys[mid]);

		if (mid_key == key)
			return mid;

		if (mid_key < key)
			lo = mid;
		else
			hi = mid;
	}

	return want_hi ? hi : lo;
}

int lower_bound(struct btree_node *n, uint64_t key)
{
	return bsearch(n, key, 0);
}

void inc_children(struct dm_transaction_manager *tm, struct btree_node *n,
		  struct dm_btree_value_type *vt)
{
	unsigned i;
	uint32_t nr_entries = le32_to_cpu(n->header.nr_entries);

	if (le32_to_cpu(n->header.flags) & INTERNAL_NODE)
		for (i = 0; i < nr_entries; i++)
			dm_tm_inc(tm, value64(n, i));
	else if (vt->inc)
		for (i = 0; i < nr_entries; i++)
			vt->inc(vt->context, value_ptr(n, i));
}

static int insert_at(size_t value_size, struct btree_node *node, unsigned index,
		      uint64_t key, void *value)
		      __dm_written_to_disk(value)
{
	uint32_t nr_entries = le32_to_cpu(node->header.nr_entries);
	__le64 key_le = cpu_to_le64(key);

	if (index > nr_entries ||
	    index >= le32_to_cpu(node->header.max_entries)) {
		DMERR("too many entries in btree node for insert");
		__dm_unbless_for_disk(value);
		return -ENOMEM;
	}

	__dm_bless_for_disk(&key_le);

	array_insert(node->keys, sizeof(*node->keys), nr_entries, index, &key_le);
	array_insert(value_base(node), value_size, nr_entries, index, value);
	node->header.nr_entries = cpu_to_le32(nr_entries + 1);

	return 0;
}

/*----------------------------------------------------------------*/

/*
 * We want 3n entries (for some n).  This works more nicely for repeated
 * insert remove loops than (2n + 1).
 */
static uint32_t calc_max_entries(size_t value_size, size_t block_size)
{
	uint32_t total, n;
	size_t elt_size = sizeof(uint64_t) + value_size; /* key + value */

	block_size -= sizeof(struct node_header);
	total = block_size / elt_size;
	n = total / 3;		/* rounds down */

	return 3 * n;
}

int dm_btree_empty(struct dm_btree_info *info, dm_block_t *root)
{
	int r;
	struct dm_block *b;
	struct btree_node *n;
	size_t block_size;
	uint32_t max_entries;

	r = new_block(info, &b);
	if (r < 0)
		return r;

	block_size = dm_bm_block_size(dm_tm_get_bm(info->tm));
	max_entries = calc_max_entries(info->value_type.size, block_size);

	n = dm_block_data(b);
	memset(n, 0, block_size);
	n->header.flags = cpu_to_le32(LEAF_NODE);
	n->header.nr_entries = cpu_to_le32(0);
	n->header.max_entries = cpu_to_le32(max_entries);
	n->header.value_size = cpu_to_le32(info->value_type.size);

	*root = dm_block_location(b);
	return unlock_block(info, b);
}
EXPORT_SYMBOL_GPL(dm_btree_empty);

/*----------------------------------------------------------------*/

/*
 * Deletion uses a recursive algorithm, since we have limited stack space
 * we explicitly manage our own stack on the heap.
 */
#define MAX_SPINE_DEPTH 64
struct frame {
	struct dm_block *b;
	struct btree_node *n;
	unsigned level;
	unsigned nr_children;
	unsigned current_child;
};

struct del_stack {
	struct dm_btree_info *info;
	struct dm_transaction_manager *tm;
	int top;
	struct frame spine[MAX_SPINE_DEPTH];
};

static int top_frame(struct del_stack *s, struct frame **f)
{
	if (s->top < 0) {
		DMERR("btree deletion stack empty");
		return -EINVAL;
	}

	*f = s->spine + s->top;

	return 0;
}

static int unprocessed_frames(struct del_stack *s)
{
	return s->top >= 0;
}

static void prefetch_children(struct del_stack *s, struct frame *f)
{
	unsigned i;
	struct dm_block_manager *bm = dm_tm_get_bm(s->tm);

	for (i = 0; i < f->nr_children; i++)
		dm_bm_prefetch(bm, value64(f->n, i));
}

static bool is_internal_level(struct dm_btree_info *info, struct frame *f)
{
	return f->level < (info->levels - 1);
}

static int push_frame(struct del_stack *s, dm_block_t b, unsigned level)
{
	int r;
	uint32_t ref_count;

	if (s->top >= MAX_SPINE_DEPTH - 1) {
		DMERR("btree deletion stack out of memory");
		return -ENOMEM;
	}

	r = dm_tm_ref(s->tm, b, &ref_count);
	if (r)
		return r;

	if (ref_count > 1)
		/*
		 * This is a shared node, so we can just decrement it's
		 * reference counter and leave the children.
		 */
		dm_tm_dec(s->tm, b);

	else {
		uint32_t flags;
		struct frame *f = s->spine + ++s->top;

		r = dm_tm_read_lock(s->tm, b, &btree_node_validator, &f->b);
		if (r) {
			s->top--;
			return r;
		}

		f->n = dm_block_data(f->b);
		f->level = level;
		f->nr_children = le32_to_cpu(f->n->header.nr_entries);
		f->current_child = 0;

		flags = le32_to_cpu(f->n->header.flags);
		if (flags & INTERNAL_NODE || is_internal_level(s->info, f))
			prefetch_children(s, f);
	}

	return 0;
}

static void pop_frame(struct del_stack *s)
{
	struct frame *f = s->spine + s->top--;

	dm_tm_dec(s->tm, dm_block_location(f->b));
	dm_tm_unlock(s->tm, f->b);
}

int dm_btree_del(struct dm_btree_info *info, dm_block_t root)
{
	int r;
	struct del_stack *s;

	s = kmalloc(sizeof(*s), GFP_NOIO);
	if (!s)
		return -ENOMEM;
	s->info = info;
	s->tm = info->tm;
	s->top = -1;

	r = push_frame(s, root, 0);
	if (r)
		goto out;

	while (unprocessed_frames(s)) {
		uint32_t flags;
		struct frame *f;
		dm_block_t b;

		r = top_frame(s, &f);
		if (r)
			goto out;

		if (f->current_child >= f->nr_children) {
			pop_frame(s);
			continue;
		}

		flags = le32_to_cpu(f->n->header.flags);
		if (flags & INTERNAL_NODE) {
			b = value64(f->n, f->current_child);
			f->current_child++;
			r = push_frame(s, b, f->level);
			if (r)
				goto out;

		} else if (is_internal_level(info, f)) {
			b = value64(f->n, f->current_child);
			f->current_child++;
			r = push_frame(s, b, f->level + 1);
			if (r)
				goto out;

		} else {
			if (info->value_type.dec) {
				unsigned i;

				for (i = 0; i < f->nr_children; i++)
					info->value_type.dec(info->value_type.context,
							     value_ptr(f->n, i));
			}
			pop_frame(s);
		}
	}

out:
	kfree(s);
	return r;
}
EXPORT_SYMBOL_GPL(dm_btree_del);

/*----------------------------------------------------------------*/

static int btree_lookup_raw(struct ro_spine *s, dm_block_t block, uint64_t key,
			    int (*search_fn)(struct btree_node *, uint64_t),
			    uint64_t *result_key, void *v, size_t value_size)
{
	int i, r;
	uint32_t flags, nr_entries;

	do {
		r = ro_step(s, block);
		if (r < 0)
			return r;

		i = search_fn(ro_node(s), key);

		flags = le32_to_cpu(ro_node(s)->header.flags);
		nr_entries = le32_to_cpu(ro_node(s)->header.nr_entries);
		if (i < 0 || i >= nr_entries)
			return -ENODATA;

		if (flags & INTERNAL_NODE)
			block = value64(ro_node(s), i);

	} while (!(flags & LEAF_NODE));

	*result_key = le64_to_cpu(ro_node(s)->keys[i]);
	memcpy(v, value_ptr(ro_node(s), i), value_size);

	return 0;
}

int dm_btree_lookup(struct dm_btree_info *info, dm_block_t root,
		    uint64_t *keys, void *value_le)
{
	unsigned level, last_level = info->levels - 1;
	int r = -ENODATA;
	uint64_t rkey;
	__le64 internal_value_le;
	struct ro_spine spine;

	init_ro_spine(&spine, info);
	for (level = 0; level < info->levels; level++) {
		size_t size;
		void *value_p;

		if (level == last_level) {
			value_p = value_le;
			size = info->value_type.size;

		} else {
			value_p = &internal_value_le;
			size = sizeof(uint64_t);
		}

		r = btree_lookup_raw(&spine, root, keys[level],
				     lower_bound, &rkey,
				     value_p, size);

		if (!r) {
			if (rkey != keys[level]) {
				exit_ro_spine(&spine);
				return -ENODATA;
			}
		} else {
			exit_ro_spine(&spine);
			return r;
		}

		root = le64_to_cpu(internal_value_le);
	}
	exit_ro_spine(&spine);

	return r;
}
EXPORT_SYMBOL_GPL(dm_btree_lookup);

/*
 * Splits a node by creating a sibling node and shifting half the nodes
 * contents across.  Assumes there is a parent node, and it has room for
 * another child.
 *
 * Before:
 *	  +--------+
 *	  | Parent |
 *	  +--------+
 *	     |
 *	     v
 *	+----------+
 *	| A ++++++ |
 *	+----------+
 *
 *
 * After:
 *		+--------+
 *		| Parent |
 *		+--------+
 *		  |	|
 *		  v	+------+
 *	    +---------+	       |
 *	    | A* +++  |	       v
 *	    +---------+	  +-------+
 *			  | B +++ |
 *			  +-------+
 *
 * Where A* is a shadow of A.
 */
static int btree_split_sibling(struct shadow_spine *s, dm_block_t root,
			       unsigned parent_index, uint64_t key)
{
	int r;
	size_t size;
	unsigned nr_left, nr_right;
	struct dm_block *left, *right, *parent;
	struct btree_node *ln, *rn, *pn;
	__le64 location;

	left = shadow_current(s);

	r = new_block(s->info, &right);
	if (r < 0)
		return r;

	ln = dm_block_data(left);
	rn = dm_block_data(right);

	nr_left = le32_to_cpu(ln->header.nr_entries) / 2;
	nr_right = le32_to_cpu(ln->header.nr_entries) - nr_left;

	ln->header.nr_entries = cpu_to_le32(nr_left);

	rn->header.flags = ln->header.flags;
	rn->header.nr_entries = cpu_to_le32(nr_right);
	rn->header.max_entries = ln->header.max_entries;
	rn->header.value_size = ln->header.value_size;
	memcpy(rn->keys, ln->keys + nr_left, nr_right * sizeof(rn->keys[0]));

	size = le32_to_cpu(ln->header.flags) & INTERNAL_NODE ?
		sizeof(uint64_t) : s->info->value_type.size;
	memcpy(value_ptr(rn, 0), value_ptr(ln, nr_left),
	       size * nr_right);

	/*
	 * Patch up the parent
	 */
	parent = shadow_parent(s);

	pn = dm_block_data(parent);
	location = cpu_to_le64(dm_block_location(left));
	__dm_bless_for_disk(&location);
	memcpy_disk(value_ptr(pn, parent_index),
		    &location, sizeof(__le64));

	location = cpu_to_le64(dm_block_location(right));
	__dm_bless_for_disk(&location);

	r = insert_at(sizeof(__le64), pn, parent_index + 1,
		      le64_to_cpu(rn->keys[0]), &location);
	if (r)
		return r;

	if (key < le64_to_cpu(rn->keys[0])) {
		unlock_block(s->info, right);
		s->nodes[1] = left;
	} else {
		unlock_block(s->info, left);
		s->nodes[1] = right;
	}

	return 0;
}

/*
 * Splits a node by creating two new children beneath the given node.
 *
 * Before:
 *	  +----------+
 *	  | A ++++++ |
 *	  +----------+
 *
 *
 * After:
 *	+------------+
 *	| A (shadow) |
 *	+------------+
 *	    |	|
 *   +------+	+----+
 *   |		     |
 *   v		     v
 * +-------+	 +-------+
 * | B +++ |	 | C +++ |
 * +-------+	 +-------+
 */
static int btree_split_beneath(struct shadow_spine *s, uint64_t key)
{
	int r;
	size_t size;
	unsigned nr_left, nr_right;
	struct dm_block *left, *right, *new_parent;
	struct btree_node *pn, *ln, *rn;
	__le64 val;

	new_parent = shadow_current(s);

	r = new_block(s->info, &left);
	if (r < 0)
		return r;

	r = new_block(s->info, &right);
	if (r < 0) {
		/* FIXME: put left */
		return r;
	}

	pn = dm_block_data(new_parent);
	ln = dm_block_data(left);
	rn = dm_block_data(right);

	nr_left = le32_to_cpu(pn->header.nr_entries) / 2;
	nr_right = le32_to_cpu(pn->header.nr_entries) - nr_left;

	ln->header.flags = pn->header.flags;
	ln->header.nr_entries = cpu_to_le32(nr_left);
	ln->header.max_entries = pn->header.max_entries;
	ln->header.value_size = pn->header.value_size;

	rn->header.flags = pn->header.flags;
	rn->header.nr_entries = cpu_to_le32(nr_right);
	rn->header.max_entries = pn->header.max_entries;
	rn->header.value_size = pn->header.value_size;

	memcpy(ln->keys, pn->keys, nr_left * sizeof(pn->keys[0]));
	memcpy(rn->keys, pn->keys + nr_left, nr_right * sizeof(pn->keys[0]));

	size = le32_to_cpu(pn->header.flags) & INTERNAL_NODE ?
		sizeof(__le64) : s->info->value_type.size;
	memcpy(value_ptr(ln, 0), value_ptr(pn, 0), nr_left * size);
	memcpy(value_ptr(rn, 0), value_ptr(pn, nr_left),
	       nr_right * size);

	/* new_parent should just point to l and r now */
	pn->header.flags = cpu_to_le32(INTERNAL_NODE);
	pn->header.nr_entries = cpu_to_le32(2);
	pn->header.max_entries = cpu_to_le32(
		calc_max_entries(sizeof(__le64),
				 dm_bm_block_size(
					 dm_tm_get_bm(s->info->tm))));
	pn->header.value_size = cpu_to_le32(sizeof(__le64));

	val = cpu_to_le64(dm_block_location(left));
	__dm_bless_for_disk(&val);
	pn->keys[0] = ln->keys[0];
	memcpy_disk(value_ptr(pn, 0), &val, sizeof(__le64));

	val = cpu_to_le64(dm_block_location(right));
	__dm_bless_for_disk(&val);
	pn->keys[1] = rn->keys[0];
	memcpy_disk(value_ptr(pn, 1), &val, sizeof(__le64));

	/*
	 * rejig the spine.  This is ugly, since it knows too
	 * much about the spine
	 */
	if (s->nodes[0] != new_parent) {
		unlock_block(s->info, s->nodes[0]);
		s->nodes[0] = new_parent;
	}
	if (key < le64_to_cpu(rn->keys[0])) {
		unlock_block(s->info, right);
		s->nodes[1] = left;
	} else {
		unlock_block(s->info, left);
		s->nodes[1] = right;
	}
	s->count = 2;

	return 0;
}

static int btree_insert_raw(struct shadow_spine *s, dm_block_t root,
			    struct dm_btree_value_type *vt,
			    uint64_t key, unsigned *index)
{
	int r, i = *index, top = 1;
	struct btree_node *node;

	for (;;) {
		r = shadow_step(s, root, vt);
		if (r < 0)
			return r;

		node = dm_block_data(shadow_current(s));

		/*
		 * We have to patch up the parent node, ugly, but I don't
		 * see a way to do this automatically as part of the spine
		 * op.
		 */
		if (shadow_has_parent(s) && i >= 0) { /* FIXME: second clause unness. */
			__le64 location = cpu_to_le64(dm_block_location(shadow_current(s)));

			__dm_bless_for_disk(&location);
			memcpy_disk(value_ptr(dm_block_data(shadow_parent(s)), i),
				    &location, sizeof(__le64));
		}

		node = dm_block_data(shadow_current(s));

		if (node->header.nr_entries == node->header.max_entries) {
			if (top)
				r = btree_split_beneath(s, key);
			else
				r = btree_split_sibling(s, root, i, key);

			if (r < 0)
				return r;
		}

		node = dm_block_data(shadow_current(s));

		i = lower_bound(node, key);

		if (le32_to_cpu(node->header.flags) & LEAF_NODE)
			break;

		if (i < 0) {
			/* change the bounds on the lowest key */
			node->keys[0] = cpu_to_le64(key);
			i = 0;
		}

		root = value64(node, i);
		top = 0;
	}

	if (i < 0 || le64_to_cpu(node->keys[i]) != key)
		i++;

	*index = i;
	return 0;
}

static int insert(struct dm_btree_info *info, dm_block_t root,
		  uint64_t *keys, void *value, dm_block_t *new_root,
		  int *inserted)
		  __dm_written_to_disk(value)
{
	int r, need_insert;
	unsigned level, index = -1, last_level = info->levels - 1;
	dm_block_t block = root;
	struct shadow_spine spine;
	struct btree_node *n;
	struct dm_btree_value_type le64_type;

	init_le64_type(info->tm, &le64_type);
	init_shadow_spine(&spine, info);

	for (level = 0; level < (info->levels - 1); level++) {
		r = btree_insert_raw(&spine, block, &le64_type, keys[level], &index);
		if (r < 0)
			goto bad;

		n = dm_block_data(shadow_current(&spine));
		need_insert = ((index >= le32_to_cpu(n->header.nr_entries)) ||
			       (le64_to_cpu(n->keys[index]) != keys[level]));

		if (need_insert) {
			dm_block_t new_tree;
			__le64 new_le;

			r = dm_btree_empty(info, &new_tree);
			if (r < 0)
				goto bad;

			new_le = cpu_to_le64(new_tree);
			__dm_bless_for_disk(&new_le);

			r = insert_at(sizeof(uint64_t), n, index,
				      keys[level], &new_le);
			if (r)
				goto bad;
		}

		if (level < last_level)
			block = value64(n, index);
	}

	r = btree_insert_raw(&spine, block, &info->value_type,
			     keys[level], &index);
	if (r < 0)
		goto bad;

	n = dm_block_data(shadow_current(&spine));
	need_insert = ((index >= le32_to_cpu(n->header.nr_entries)) ||
		       (le64_to_cpu(n->keys[index]) != keys[level]));

	if (need_insert) {
		if (inserted)
			*inserted = 1;

		r = insert_at(info->value_type.size, n, index,
			      keys[level], value);
		if (r)
			goto bad_unblessed;
	} else {
		if (inserted)
			*inserted = 0;

		if (info->value_type.dec &&
		    (!info->value_type.equal ||
		     !info->value_type.equal(
			     info->value_type.context,
			     value_ptr(n, index),
			     value))) {
			info->value_type.dec(info->value_type.context,
					     value_ptr(n, index));
		}
		memcpy_disk(value_ptr(n, index),
			    value, info->value_type.size);
	}

	*new_root = shadow_root(&spine);
	exit_shadow_spine(&spine);

	return 0;

bad:
	__dm_unbless_for_disk(value);
bad_unblessed:
	exit_shadow_spine(&spine);
	return r;
}

int dm_btree_insert(struct dm_btree_info *info, dm_block_t root,
		    uint64_t *keys, void *value, dm_block_t *new_root)
		    __dm_written_to_disk(value)
{
	return insert(info, root, keys, value, new_root, NULL);
}
EXPORT_SYMBOL_GPL(dm_btree_insert);

int dm_btree_insert_notify(struct dm_btree_info *info, dm_block_t root,
			   uint64_t *keys, void *value, dm_block_t *new_root,
			   int *inserted)
			   __dm_written_to_disk(value)
{
	return insert(info, root, keys, value, new_root, inserted);
}
EXPORT_SYMBOL_GPL(dm_btree_insert_notify);

/*----------------------------------------------------------------*/

static int find_key(struct ro_spine *s, dm_block_t block, bool find_highest,
		    uint64_t *result_key, dm_block_t *next_block)
{
	int i, r;
	uint32_t flags;

	do {
		r = ro_step(s, block);
		if (r < 0)
			return r;

		flags = le32_to_cpu(ro_node(s)->header.flags);
		i = le32_to_cpu(ro_node(s)->header.nr_entries);
		if (!i)
			return -ENODATA;
		else
			i--;

		if (find_highest)
			*result_key = le64_to_cpu(ro_node(s)->keys[i]);
		else
			*result_key = le64_to_cpu(ro_node(s)->keys[0]);

		if (next_block || flags & INTERNAL_NODE)
			block = value64(ro_node(s), i);

	} while (flags & INTERNAL_NODE);

	if (next_block)
		*next_block = block;
	return 0;
}

static int dm_btree_find_key(struct dm_btree_info *info, dm_block_t root,
			     bool find_highest, uint64_t *result_keys)
{
	int r = 0, count = 0, level;
	struct ro_spine spine;

	init_ro_spine(&spine, info);
	for (level = 0; level < info->levels; level++) {
		r = find_key(&spine, root, find_highest, result_keys + level,
			     level == info->levels - 1 ? NULL : &root);
		if (r == -ENODATA) {
			r = 0;
			break;

		} else if (r)
			break;

		count++;
	}
	exit_ro_spine(&spine);

	return r ? r : count;
}

int dm_btree_find_highest_key(struct dm_btree_info *info, dm_block_t root,
			      uint64_t *result_keys)
{
	return dm_btree_find_key(info, root, true, result_keys);
}
EXPORT_SYMBOL_GPL(dm_btree_find_highest_key);

int dm_btree_find_lowest_key(struct dm_btree_info *info, dm_block_t root,
			     uint64_t *result_keys)
{
	return dm_btree_find_key(info, root, false, result_keys);
}
EXPORT_SYMBOL_GPL(dm_btree_find_lowest_key);

/*----------------------------------------------------------------*/

/*
 * FIXME: We shouldn't use a recursive algorithm when we have limited stack
 * space.  Also this only works for single level trees.
 */
static int walk_node(struct dm_btree_info *info, dm_block_t block,
		     int (*fn)(void *context, uint64_t *keys, void *leaf),
		     void *context)
{
	int r;
	unsigned i, nr;
	struct dm_block *node;
	struct btree_node *n;
	uint64_t keys;

	r = bn_read_lock(info, block, &node);
	if (r)
		return r;

	n = dm_block_data(node);

	nr = le32_to_cpu(n->header.nr_entries);
	for (i = 0; i < nr; i++) {
		if (le32_to_cpu(n->header.flags) & INTERNAL_NODE) {
			r = walk_node(info, value64(n, i), fn, context);
			if (r)
				goto out;
		} else {
			keys = le64_to_cpu(*key_ptr(n, i));
			r = fn(context, &keys, value_ptr(n, i));
			if (r)
				goto out;
		}
	}

out:
	dm_tm_unlock(info->tm, node);
	return r;
}

int dm_btree_walk(struct dm_btree_info *info, dm_block_t root,
		  int (*fn)(void *context, uint64_t *keys, void *leaf),
		  void *context)
{
	BUG_ON(info->levels > 1);
	return walk_node(info, root, fn, context);
}
EXPORT_SYMBOL_GPL(dm_btree_walk);
