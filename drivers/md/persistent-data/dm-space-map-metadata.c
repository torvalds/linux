/*
 * Copyright (C) 2011 Red Hat, Inc.
 *
 * This file is released under the GPL.
 */

#include "dm-space-map.h"
#include "dm-space-map-common.h"
#include "dm-space-map-metadata.h"

#include <linux/list.h>
#include <linux/slab.h>
#include <linux/device-mapper.h>
#include <linux/kernel.h>

#define DM_MSG_PREFIX "space map metadata"

/*----------------------------------------------------------------*/

/*
 * An edge triggered threshold.
 */
struct threshold {
	bool threshold_set;
	bool value_set;
	dm_block_t threshold;
	dm_block_t current_value;
	dm_sm_threshold_fn fn;
	void *context;
};

static void threshold_init(struct threshold *t)
{
	t->threshold_set = false;
	t->value_set = false;
}

static void set_threshold(struct threshold *t, dm_block_t value,
			  dm_sm_threshold_fn fn, void *context)
{
	t->threshold_set = true;
	t->threshold = value;
	t->fn = fn;
	t->context = context;
}

static bool below_threshold(struct threshold *t, dm_block_t value)
{
	return t->threshold_set && value <= t->threshold;
}

static bool threshold_already_triggered(struct threshold *t)
{
	return t->value_set && below_threshold(t, t->current_value);
}

static void check_threshold(struct threshold *t, dm_block_t value)
{
	if (below_threshold(t, value) &&
	    !threshold_already_triggered(t))
		t->fn(t->context);

	t->value_set = true;
	t->current_value = value;
}

/*----------------------------------------------------------------*/

/*
 * Space map interface.
 *
 * The low level disk format is written using the standard btree and
 * transaction manager.  This means that performing disk operations may
 * cause us to recurse into the space map in order to allocate new blocks.
 * For this reason we have a pool of pre-allocated blocks large enough to
 * service any metadata_ll_disk operation.
 */

/*
 * FIXME: we should calculate this based on the size of the device.
 * Only the metadata space map needs this functionality.
 */
#define MAX_RECURSIVE_ALLOCATIONS 1024

enum block_op_type {
	BOP_INC,
	BOP_DEC
};

struct block_op {
	enum block_op_type type;
	dm_block_t block;
};

struct bop_ring_buffer {
	unsigned begin;
	unsigned end;
	struct block_op bops[MAX_RECURSIVE_ALLOCATIONS + 1];
};

static void brb_init(struct bop_ring_buffer *brb)
{
	brb->begin = 0;
	brb->end = 0;
}

static bool brb_empty(struct bop_ring_buffer *brb)
{
	return brb->begin == brb->end;
}

static unsigned brb_next(struct bop_ring_buffer *brb, unsigned old)
{
	unsigned r = old + 1;
	return r >= ARRAY_SIZE(brb->bops) ? 0 : r;
}

static int brb_push(struct bop_ring_buffer *brb,
		    enum block_op_type type, dm_block_t b)
{
	struct block_op *bop;
	unsigned next = brb_next(brb, brb->end);

	/*
	 * We don't allow the last bop to be filled, this way we can
	 * differentiate between full and empty.
	 */
	if (next == brb->begin)
		return -ENOMEM;

	bop = brb->bops + brb->end;
	bop->type = type;
	bop->block = b;

	brb->end = next;

	return 0;
}

static int brb_peek(struct bop_ring_buffer *brb, struct block_op *result)
{
	struct block_op *bop;

	if (brb_empty(brb))
		return -ENODATA;

	bop = brb->bops + brb->begin;
	result->type = bop->type;
	result->block = bop->block;

	return 0;
}

static int brb_pop(struct bop_ring_buffer *brb)
{
	if (brb_empty(brb))
		return -ENODATA;

	brb->begin = brb_next(brb, brb->begin);

	return 0;
}

/*----------------------------------------------------------------*/

struct sm_metadata {
	struct dm_space_map sm;

	struct ll_disk ll;
	struct ll_disk old_ll;

	dm_block_t begin;

	unsigned recursion_count;
	unsigned allocated_this_transaction;
	struct bop_ring_buffer uncommitted;

	struct threshold threshold;
};

static int add_bop(struct sm_metadata *smm, enum block_op_type type, dm_block_t b)
{
	int r = brb_push(&smm->uncommitted, type, b);

	if (r) {
		DMERR("too many recursive allocations");
		return -ENOMEM;
	}

	return 0;
}

static int commit_bop(struct sm_metadata *smm, struct block_op *op)
{
	int r = 0;
	enum allocation_event ev;

	switch (op->type) {
	case BOP_INC:
		r = sm_ll_inc(&smm->ll, op->block, &ev);
		break;

	case BOP_DEC:
		r = sm_ll_dec(&smm->ll, op->block, &ev);
		break;
	}

	return r;
}

static void in(struct sm_metadata *smm)
{
	smm->recursion_count++;
}

static int apply_bops(struct sm_metadata *smm)
{
	int r = 0;

	while (!brb_empty(&smm->uncommitted)) {
		struct block_op bop;

		r = brb_peek(&smm->uncommitted, &bop);
		if (r) {
			DMERR("bug in bop ring buffer");
			break;
		}

		r = commit_bop(smm, &bop);
		if (r)
			break;

		brb_pop(&smm->uncommitted);
	}

	return r;
}

static int out(struct sm_metadata *smm)
{
	int r = 0;

	/*
	 * If we're not recursing then very bad things are happening.
	 */
	if (!smm->recursion_count) {
		DMERR("lost track of recursion depth");
		return -ENOMEM;
	}

	if (smm->recursion_count == 1)
		apply_bops(smm);

	smm->recursion_count--;

	return r;
}

/*
 * When using the out() function above, we often want to combine an error
 * code for the operation run in the recursive context with that from
 * out().
 */
static int combine_errors(int r1, int r2)
{
	return r1 ? r1 : r2;
}

static int recursing(struct sm_metadata *smm)
{
	return smm->recursion_count;
}

static void sm_metadata_destroy(struct dm_space_map *sm)
{
	struct sm_metadata *smm = container_of(sm, struct sm_metadata, sm);

	kfree(smm);
}

static int sm_metadata_get_nr_blocks(struct dm_space_map *sm, dm_block_t *count)
{
	struct sm_metadata *smm = container_of(sm, struct sm_metadata, sm);

	*count = smm->ll.nr_blocks;

	return 0;
}

static int sm_metadata_get_nr_free(struct dm_space_map *sm, dm_block_t *count)
{
	struct sm_metadata *smm = container_of(sm, struct sm_metadata, sm);

	*count = smm->old_ll.nr_blocks - smm->old_ll.nr_allocated -
		 smm->allocated_this_transaction;

	return 0;
}

static int sm_metadata_get_count(struct dm_space_map *sm, dm_block_t b,
				 uint32_t *result)
{
	int r;
	unsigned i;
	struct sm_metadata *smm = container_of(sm, struct sm_metadata, sm);
	unsigned adjustment = 0;

	/*
	 * We may have some uncommitted adjustments to add.  This list
	 * should always be really short.
	 */
	for (i = smm->uncommitted.begin;
	     i != smm->uncommitted.end;
	     i = brb_next(&smm->uncommitted, i)) {
		struct block_op *op = smm->uncommitted.bops + i;

		if (op->block != b)
			continue;

		switch (op->type) {
		case BOP_INC:
			adjustment++;
			break;

		case BOP_DEC:
			adjustment--;
			break;
		}
	}

	r = sm_ll_lookup(&smm->ll, b, result);
	if (r)
		return r;

	*result += adjustment;

	return 0;
}

static int sm_metadata_count_is_more_than_one(struct dm_space_map *sm,
					      dm_block_t b, int *result)
{
	int r, adjustment = 0;
	unsigned i;
	struct sm_metadata *smm = container_of(sm, struct sm_metadata, sm);
	uint32_t rc;

	/*
	 * We may have some uncommitted adjustments to add.  This list
	 * should always be really short.
	 */
	for (i = smm->uncommitted.begin;
	     i != smm->uncommitted.end;
	     i = brb_next(&smm->uncommitted, i)) {

		struct block_op *op = smm->uncommitted.bops + i;

		if (op->block != b)
			continue;

		switch (op->type) {
		case BOP_INC:
			adjustment++;
			break;

		case BOP_DEC:
			adjustment--;
			break;
		}
	}

	if (adjustment > 1) {
		*result = 1;
		return 0;
	}

	r = sm_ll_lookup_bitmap(&smm->ll, b, &rc);
	if (r)
		return r;

	if (rc == 3)
		/*
		 * We err on the side of caution, and always return true.
		 */
		*result = 1;
	else
		*result = rc + adjustment > 1;

	return 0;
}

static int sm_metadata_set_count(struct dm_space_map *sm, dm_block_t b,
				 uint32_t count)
{
	int r, r2;
	enum allocation_event ev;
	struct sm_metadata *smm = container_of(sm, struct sm_metadata, sm);

	if (smm->recursion_count) {
		DMERR("cannot recurse set_count()");
		return -EINVAL;
	}

	in(smm);
	r = sm_ll_insert(&smm->ll, b, count, &ev);
	r2 = out(smm);

	return combine_errors(r, r2);
}

static int sm_metadata_inc_block(struct dm_space_map *sm, dm_block_t b)
{
	int r, r2 = 0;
	enum allocation_event ev;
	struct sm_metadata *smm = container_of(sm, struct sm_metadata, sm);

	if (recursing(smm))
		r = add_bop(smm, BOP_INC, b);
	else {
		in(smm);
		r = sm_ll_inc(&smm->ll, b, &ev);
		r2 = out(smm);
	}

	return combine_errors(r, r2);
}

static int sm_metadata_dec_block(struct dm_space_map *sm, dm_block_t b)
{
	int r, r2 = 0;
	enum allocation_event ev;
	struct sm_metadata *smm = container_of(sm, struct sm_metadata, sm);

	if (recursing(smm))
		r = add_bop(smm, BOP_DEC, b);
	else {
		in(smm);
		r = sm_ll_dec(&smm->ll, b, &ev);
		r2 = out(smm);
	}

	return combine_errors(r, r2);
}

static int sm_metadata_new_block_(struct dm_space_map *sm, dm_block_t *b)
{
	int r, r2 = 0;
	enum allocation_event ev;
	struct sm_metadata *smm = container_of(sm, struct sm_metadata, sm);

	r = sm_ll_find_free_block(&smm->old_ll, smm->begin, smm->old_ll.nr_blocks, b);
	if (r)
		return r;

	smm->begin = *b + 1;

	if (recursing(smm))
		r = add_bop(smm, BOP_INC, *b);
	else {
		in(smm);
		r = sm_ll_inc(&smm->ll, *b, &ev);
		r2 = out(smm);
	}

	if (!r)
		smm->allocated_this_transaction++;

	return combine_errors(r, r2);
}

static int sm_metadata_new_block(struct dm_space_map *sm, dm_block_t *b)
{
	dm_block_t count;
	struct sm_metadata *smm = container_of(sm, struct sm_metadata, sm);

	int r = sm_metadata_new_block_(sm, b);
	if (r) {
		DMERR_LIMIT("unable to allocate new metadata block");
		return r;
	}

	r = sm_metadata_get_nr_free(sm, &count);
	if (r) {
		DMERR_LIMIT("couldn't get free block count");
		return r;
	}

	check_threshold(&smm->threshold, count);

	return r;
}

static int sm_metadata_commit(struct dm_space_map *sm)
{
	int r;
	struct sm_metadata *smm = container_of(sm, struct sm_metadata, sm);

	r = sm_ll_commit(&smm->ll);
	if (r)
		return r;

	memcpy(&smm->old_ll, &smm->ll, sizeof(smm->old_ll));
	smm->begin = 0;
	smm->allocated_this_transaction = 0;

	return 0;
}

static int sm_metadata_register_threshold_callback(struct dm_space_map *sm,
						   dm_block_t threshold,
						   dm_sm_threshold_fn fn,
						   void *context)
{
	struct sm_metadata *smm = container_of(sm, struct sm_metadata, sm);

	set_threshold(&smm->threshold, threshold, fn, context);

	return 0;
}

static int sm_metadata_root_size(struct dm_space_map *sm, size_t *result)
{
	*result = sizeof(struct disk_sm_root);

	return 0;
}

static int sm_metadata_copy_root(struct dm_space_map *sm, void *where_le, size_t max)
{
	struct sm_metadata *smm = container_of(sm, struct sm_metadata, sm);
	struct disk_sm_root root_le;

	root_le.nr_blocks = cpu_to_le64(smm->ll.nr_blocks);
	root_le.nr_allocated = cpu_to_le64(smm->ll.nr_allocated);
	root_le.bitmap_root = cpu_to_le64(smm->ll.bitmap_root);
	root_le.ref_count_root = cpu_to_le64(smm->ll.ref_count_root);

	if (max < sizeof(root_le))
		return -ENOSPC;

	memcpy(where_le, &root_le, sizeof(root_le));

	return 0;
}

static int sm_metadata_extend(struct dm_space_map *sm, dm_block_t extra_blocks);

static const struct dm_space_map ops = {
	.destroy = sm_metadata_destroy,
	.extend = sm_metadata_extend,
	.get_nr_blocks = sm_metadata_get_nr_blocks,
	.get_nr_free = sm_metadata_get_nr_free,
	.get_count = sm_metadata_get_count,
	.count_is_more_than_one = sm_metadata_count_is_more_than_one,
	.set_count = sm_metadata_set_count,
	.inc_block = sm_metadata_inc_block,
	.dec_block = sm_metadata_dec_block,
	.new_block = sm_metadata_new_block,
	.commit = sm_metadata_commit,
	.root_size = sm_metadata_root_size,
	.copy_root = sm_metadata_copy_root,
	.register_threshold_callback = sm_metadata_register_threshold_callback
};

/*----------------------------------------------------------------*/

/*
 * When a new space map is created that manages its own space.  We use
 * this tiny bootstrap allocator.
 */
static void sm_bootstrap_destroy(struct dm_space_map *sm)
{
}

static int sm_bootstrap_extend(struct dm_space_map *sm, dm_block_t extra_blocks)
{
	DMERR("bootstrap doesn't support extend");

	return -EINVAL;
}

static int sm_bootstrap_get_nr_blocks(struct dm_space_map *sm, dm_block_t *count)
{
	struct sm_metadata *smm = container_of(sm, struct sm_metadata, sm);

	*count = smm->ll.nr_blocks;

	return 0;
}

static int sm_bootstrap_get_nr_free(struct dm_space_map *sm, dm_block_t *count)
{
	struct sm_metadata *smm = container_of(sm, struct sm_metadata, sm);

	*count = smm->ll.nr_blocks - smm->begin;

	return 0;
}

static int sm_bootstrap_get_count(struct dm_space_map *sm, dm_block_t b,
				  uint32_t *result)
{
	struct sm_metadata *smm = container_of(sm, struct sm_metadata, sm);

	*result = (b < smm->begin) ? 1 : 0;

	return 0;
}

static int sm_bootstrap_count_is_more_than_one(struct dm_space_map *sm,
					       dm_block_t b, int *result)
{
	*result = 0;

	return 0;
}

static int sm_bootstrap_set_count(struct dm_space_map *sm, dm_block_t b,
				  uint32_t count)
{
	DMERR("bootstrap doesn't support set_count");

	return -EINVAL;
}

static int sm_bootstrap_new_block(struct dm_space_map *sm, dm_block_t *b)
{
	struct sm_metadata *smm = container_of(sm, struct sm_metadata, sm);

	/*
	 * We know the entire device is unused.
	 */
	if (smm->begin == smm->ll.nr_blocks)
		return -ENOSPC;

	*b = smm->begin++;

	return 0;
}

static int sm_bootstrap_inc_block(struct dm_space_map *sm, dm_block_t b)
{
	struct sm_metadata *smm = container_of(sm, struct sm_metadata, sm);

	return add_bop(smm, BOP_INC, b);
}

static int sm_bootstrap_dec_block(struct dm_space_map *sm, dm_block_t b)
{
	struct sm_metadata *smm = container_of(sm, struct sm_metadata, sm);

	return add_bop(smm, BOP_DEC, b);
}

static int sm_bootstrap_commit(struct dm_space_map *sm)
{
	return 0;
}

static int sm_bootstrap_root_size(struct dm_space_map *sm, size_t *result)
{
	DMERR("bootstrap doesn't support root_size");

	return -EINVAL;
}

static int sm_bootstrap_copy_root(struct dm_space_map *sm, void *where,
				  size_t max)
{
	DMERR("bootstrap doesn't support copy_root");

	return -EINVAL;
}

static const struct dm_space_map bootstrap_ops = {
	.destroy = sm_bootstrap_destroy,
	.extend = sm_bootstrap_extend,
	.get_nr_blocks = sm_bootstrap_get_nr_blocks,
	.get_nr_free = sm_bootstrap_get_nr_free,
	.get_count = sm_bootstrap_get_count,
	.count_is_more_than_one = sm_bootstrap_count_is_more_than_one,
	.set_count = sm_bootstrap_set_count,
	.inc_block = sm_bootstrap_inc_block,
	.dec_block = sm_bootstrap_dec_block,
	.new_block = sm_bootstrap_new_block,
	.commit = sm_bootstrap_commit,
	.root_size = sm_bootstrap_root_size,
	.copy_root = sm_bootstrap_copy_root,
	.register_threshold_callback = NULL
};

/*----------------------------------------------------------------*/

static int sm_metadata_extend(struct dm_space_map *sm, dm_block_t extra_blocks)
{
	int r, i;
	struct sm_metadata *smm = container_of(sm, struct sm_metadata, sm);
	dm_block_t old_len = smm->ll.nr_blocks;

	/*
	 * Flick into a mode where all blocks get allocated in the new area.
	 */
	smm->begin = old_len;
	memcpy(sm, &bootstrap_ops, sizeof(*sm));

	/*
	 * Extend.
	 */
	r = sm_ll_extend(&smm->ll, extra_blocks);
	if (r)
		goto out;

	/*
	 * We repeatedly increment then commit until the commit doesn't
	 * allocate any new blocks.
	 */
	do {
		for (i = old_len; !r && i < smm->begin; i++)
			r = add_bop(smm, BOP_INC, i);

		if (r)
			goto out;

		old_len = smm->begin;

		r = apply_bops(smm);
		if (r) {
			DMERR("%s: apply_bops failed", __func__);
			goto out;
		}

		r = sm_ll_commit(&smm->ll);
		if (r)
			goto out;

	} while (old_len != smm->begin);

out:
	/*
	 * Switch back to normal behaviour.
	 */
	memcpy(sm, &ops, sizeof(*sm));
	return r;
}

/*----------------------------------------------------------------*/

struct dm_space_map *dm_sm_metadata_init(void)
{
	struct sm_metadata *smm;

	smm = kmalloc(sizeof(*smm), GFP_KERNEL);
	if (!smm)
		return ERR_PTR(-ENOMEM);

	memcpy(&smm->sm, &ops, sizeof(smm->sm));

	return &smm->sm;
}

int dm_sm_metadata_create(struct dm_space_map *sm,
			  struct dm_transaction_manager *tm,
			  dm_block_t nr_blocks,
			  dm_block_t superblock)
{
	int r;
	dm_block_t i;
	struct sm_metadata *smm = container_of(sm, struct sm_metadata, sm);

	smm->begin = superblock + 1;
	smm->recursion_count = 0;
	smm->allocated_this_transaction = 0;
	brb_init(&smm->uncommitted);
	threshold_init(&smm->threshold);

	memcpy(&smm->sm, &bootstrap_ops, sizeof(smm->sm));

	r = sm_ll_new_metadata(&smm->ll, tm);
	if (!r) {
		if (nr_blocks > DM_SM_METADATA_MAX_BLOCKS)
			nr_blocks = DM_SM_METADATA_MAX_BLOCKS;
		r = sm_ll_extend(&smm->ll, nr_blocks);
	}
	memcpy(&smm->sm, &ops, sizeof(smm->sm));
	if (r)
		return r;

	/*
	 * Now we need to update the newly created data structures with the
	 * allocated blocks that they were built from.
	 */
	for (i = superblock; !r && i < smm->begin; i++)
		r = add_bop(smm, BOP_INC, i);

	if (r)
		return r;

	r = apply_bops(smm);
	if (r) {
		DMERR("%s: apply_bops failed", __func__);
		return r;
	}

	return sm_metadata_commit(sm);
}

int dm_sm_metadata_open(struct dm_space_map *sm,
			struct dm_transaction_manager *tm,
			void *root_le, size_t len)
{
	int r;
	struct sm_metadata *smm = container_of(sm, struct sm_metadata, sm);

	r = sm_ll_open_metadata(&smm->ll, tm, root_le, len);
	if (r)
		return r;

	smm->begin = 0;
	smm->recursion_count = 0;
	smm->allocated_this_transaction = 0;
	brb_init(&smm->uncommitted);
	threshold_init(&smm->threshold);

	memcpy(&smm->old_ll, &smm->ll, sizeof(smm->old_ll));
	return 0;
}
