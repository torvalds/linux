/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2011 Red Hat, Inc.
 *
 * This file is released under the GPL.
 */

#ifndef DM_BTREE_INTERNAL_H
#define DM_BTREE_INTERNAL_H

#include "dm-btree.h"

/*----------------------------------------------------------------*/

/*
 * We'll need 2 accessor functions for n->csum and n->blocknr
 * to support dm-btree-spine.c in that case.
 */

enum node_flags {
	INTERNAL_NODE = 1,
	LEAF_NODE = 1 << 1
};

/*
 * Every btree node begins with this structure.  Make sure it's a multiple
 * of 8-bytes in size, otherwise the 64bit keys will be mis-aligned.
 */
struct node_header {
	__le32 csum;
	__le32 flags;
	__le64 blocknr; /* Block this node is supposed to live in. */

	__le32 nr_entries;
	__le32 max_entries;
	__le32 value_size;
	__le32 padding;
} __packed __aligned(8);

struct btree_node {
	struct node_header header;
	__le64 keys[];
} __packed __aligned(8);


/*
 * Locks a block using the btree node validator.
 */
int bn_read_lock(struct dm_btree_info *info, dm_block_t b,
		 struct dm_block **result);

void inc_children(struct dm_transaction_manager *tm, struct btree_node *n,
		  struct dm_btree_value_type *vt);

int new_block(struct dm_btree_info *info, struct dm_block **result);
void unlock_block(struct dm_btree_info *info, struct dm_block *b);

/*
 * Spines keep track of the rolling locks.  There are 2 variants, read-only
 * and one that uses shadowing.  These are separate structs to allow the
 * type checker to spot misuse, for example accidentally calling read_lock
 * on a shadow spine.
 */
struct ro_spine {
	struct dm_btree_info *info;

	int count;
	struct dm_block *nodes[2];
};

void init_ro_spine(struct ro_spine *s, struct dm_btree_info *info);
void exit_ro_spine(struct ro_spine *s);
int ro_step(struct ro_spine *s, dm_block_t new_child);
void ro_pop(struct ro_spine *s);
struct btree_node *ro_node(struct ro_spine *s);

struct shadow_spine {
	struct dm_btree_info *info;

	int count;
	struct dm_block *nodes[2];

	dm_block_t root;
};

void init_shadow_spine(struct shadow_spine *s, struct dm_btree_info *info);
void exit_shadow_spine(struct shadow_spine *s);

int shadow_step(struct shadow_spine *s, dm_block_t b,
		struct dm_btree_value_type *vt);

/*
 * The spine must have at least one entry before calling this.
 */
struct dm_block *shadow_current(struct shadow_spine *s);

/*
 * The spine must have at least two entries before calling this.
 */
struct dm_block *shadow_parent(struct shadow_spine *s);

int shadow_has_parent(struct shadow_spine *s);

dm_block_t shadow_root(struct shadow_spine *s);

/*
 * Some inlines.
 */
static inline __le64 *key_ptr(struct btree_node *n, uint32_t index)
{
	return n->keys + index;
}

static inline void *value_base(struct btree_node *n)
{
	return &n->keys[le32_to_cpu(n->header.max_entries)];
}

static inline void *value_ptr(struct btree_node *n, uint32_t index)
{
	uint32_t value_size = le32_to_cpu(n->header.value_size);

	return value_base(n) + (value_size * index);
}

/*
 * Assumes the values are suitably-aligned and converts to core format.
 */
static inline uint64_t value64(struct btree_node *n, uint32_t index)
{
	__le64 *values_le = value_base(n);

	return le64_to_cpu(values_le[index]);
}

/*
 * Searching for a key within a single node.
 */
int lower_bound(struct btree_node *n, uint64_t key);

extern struct dm_block_validator btree_node_validator;

/*
 * Value type for upper levels of multi-level btrees.
 */
extern void init_le64_type(struct dm_transaction_manager *tm,
			   struct dm_btree_value_type *vt);

/*
 * This returns a shadowed btree leaf that you may modify.  In practise
 * this means overwrites only, since an insert could cause a node to
 * be split.  Useful if you need access to the old value to calculate the
 * new one.
 *
 * This only works with single level btrees.  The given key must be present in
 * the tree, otherwise -EINVAL will be returned.
 */
int btree_get_overwrite_leaf(struct dm_btree_info *info, dm_block_t root,
			     uint64_t key, int *index,
			     dm_block_t *new_root, struct dm_block **leaf);

#endif	/* DM_BTREE_INTERNAL_H */
