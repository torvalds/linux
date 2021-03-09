/*
 * Copyright (C) 2011 Red Hat, Inc.
 *
 * This file is released under the GPL.
 */

#include "dm-space-map-common.h"
#include "dm-space-map-disk.h"
#include "dm-space-map.h"
#include "dm-transaction-manager.h"

#include <linux/list.h>
#include <linux/slab.h>
#include <linux/export.h>
#include <linux/device-mapper.h>

#define DM_MSG_PREFIX "space map disk"

/*----------------------------------------------------------------*/

/*
 * Space map interface.
 */
struct sm_disk {
	struct dm_space_map sm;

	struct ll_disk ll;
	struct ll_disk old_ll;

	dm_block_t begin;
	dm_block_t nr_allocated_this_transaction;
};

static void sm_disk_destroy(struct dm_space_map *sm)
{
	struct sm_disk *smd = container_of(sm, struct sm_disk, sm);

	kfree(smd);
}

static int sm_disk_extend(struct dm_space_map *sm, dm_block_t extra_blocks)
{
	struct sm_disk *smd = container_of(sm, struct sm_disk, sm);

	return sm_ll_extend(&smd->ll, extra_blocks);
}

static int sm_disk_get_nr_blocks(struct dm_space_map *sm, dm_block_t *count)
{
	struct sm_disk *smd = container_of(sm, struct sm_disk, sm);
	*count = smd->old_ll.nr_blocks;

	return 0;
}

static int sm_disk_get_nr_free(struct dm_space_map *sm, dm_block_t *count)
{
	struct sm_disk *smd = container_of(sm, struct sm_disk, sm);
	*count = (smd->old_ll.nr_blocks - smd->old_ll.nr_allocated) - smd->nr_allocated_this_transaction;

	return 0;
}

static int sm_disk_get_count(struct dm_space_map *sm, dm_block_t b,
			     uint32_t *result)
{
	struct sm_disk *smd = container_of(sm, struct sm_disk, sm);
	return sm_ll_lookup(&smd->ll, b, result);
}

static int sm_disk_count_is_more_than_one(struct dm_space_map *sm, dm_block_t b,
					  int *result)
{
	int r;
	uint32_t count;

	r = sm_disk_get_count(sm, b, &count);
	if (r)
		return r;

	*result = count > 1;

	return 0;
}

static int sm_disk_set_count(struct dm_space_map *sm, dm_block_t b,
			     uint32_t count)
{
	int r;
	uint32_t old_count;
	enum allocation_event ev;
	struct sm_disk *smd = container_of(sm, struct sm_disk, sm);

	r = sm_ll_insert(&smd->ll, b, count, &ev);
	if (!r) {
		switch (ev) {
		case SM_NONE:
			break;

		case SM_ALLOC:
			/*
			 * This _must_ be free in the prior transaction
			 * otherwise we've lost atomicity.
			 */
			smd->nr_allocated_this_transaction++;
			break;

		case SM_FREE:
			/*
			 * It's only free if it's also free in the last
			 * transaction.
			 */
			r = sm_ll_lookup(&smd->old_ll, b, &old_count);
			if (r)
				return r;

			if (!old_count)
				smd->nr_allocated_this_transaction--;
			break;
		}
	}

	return r;
}

static int sm_disk_inc_block(struct dm_space_map *sm, dm_block_t b)
{
	int r;
	enum allocation_event ev;
	struct sm_disk *smd = container_of(sm, struct sm_disk, sm);

	r = sm_ll_inc(&smd->ll, b, &ev);
	if (!r && (ev == SM_ALLOC))
		/*
		 * This _must_ be free in the prior transaction
		 * otherwise we've lost atomicity.
		 */
		smd->nr_allocated_this_transaction++;

	return r;
}

static int sm_disk_dec_block(struct dm_space_map *sm, dm_block_t b)
{
	int r;
	uint32_t old_count;
	enum allocation_event ev;
	struct sm_disk *smd = container_of(sm, struct sm_disk, sm);

	r = sm_ll_dec(&smd->ll, b, &ev);
	if (!r && (ev == SM_FREE)) {
		/*
		 * It's only free if it's also free in the last
		 * transaction.
		 */
		r = sm_ll_lookup(&smd->old_ll, b, &old_count);
		if (!r && !old_count)
			smd->nr_allocated_this_transaction--;
	}

	return r;
}

static int sm_disk_new_block(struct dm_space_map *sm, dm_block_t *b)
{
	int r;
	enum allocation_event ev;
	struct sm_disk *smd = container_of(sm, struct sm_disk, sm);

	/*
	 * Any block we allocate has to be free in both the old and current ll.
	 */
	r = sm_ll_find_common_free_block(&smd->old_ll, &smd->ll, smd->begin, smd->ll.nr_blocks, b);
	if (r)
		return r;

	smd->begin = *b + 1;
	r = sm_ll_inc(&smd->ll, *b, &ev);
	if (!r) {
		BUG_ON(ev != SM_ALLOC);
		smd->nr_allocated_this_transaction++;
	}

	return r;
}

static int sm_disk_commit(struct dm_space_map *sm)
{
	int r;
	struct sm_disk *smd = container_of(sm, struct sm_disk, sm);

	r = sm_ll_commit(&smd->ll);
	if (r)
		return r;

	memcpy(&smd->old_ll, &smd->ll, sizeof(smd->old_ll));
	smd->begin = 0;
	smd->nr_allocated_this_transaction = 0;

	return 0;
}

static int sm_disk_root_size(struct dm_space_map *sm, size_t *result)
{
	*result = sizeof(struct disk_sm_root);

	return 0;
}

static int sm_disk_copy_root(struct dm_space_map *sm, void *where_le, size_t max)
{
	struct sm_disk *smd = container_of(sm, struct sm_disk, sm);
	struct disk_sm_root root_le;

	root_le.nr_blocks = cpu_to_le64(smd->ll.nr_blocks);
	root_le.nr_allocated = cpu_to_le64(smd->ll.nr_allocated);
	root_le.bitmap_root = cpu_to_le64(smd->ll.bitmap_root);
	root_le.ref_count_root = cpu_to_le64(smd->ll.ref_count_root);

	if (max < sizeof(root_le))
		return -ENOSPC;

	memcpy(where_le, &root_le, sizeof(root_le));

	return 0;
}

/*----------------------------------------------------------------*/

static struct dm_space_map ops = {
	.destroy = sm_disk_destroy,
	.extend = sm_disk_extend,
	.get_nr_blocks = sm_disk_get_nr_blocks,
	.get_nr_free = sm_disk_get_nr_free,
	.get_count = sm_disk_get_count,
	.count_is_more_than_one = sm_disk_count_is_more_than_one,
	.set_count = sm_disk_set_count,
	.inc_block = sm_disk_inc_block,
	.dec_block = sm_disk_dec_block,
	.new_block = sm_disk_new_block,
	.commit = sm_disk_commit,
	.root_size = sm_disk_root_size,
	.copy_root = sm_disk_copy_root,
	.register_threshold_callback = NULL
};

struct dm_space_map *dm_sm_disk_create(struct dm_transaction_manager *tm,
				       dm_block_t nr_blocks)
{
	int r;
	struct sm_disk *smd;

	smd = kmalloc(sizeof(*smd), GFP_KERNEL);
	if (!smd)
		return ERR_PTR(-ENOMEM);

	smd->begin = 0;
	smd->nr_allocated_this_transaction = 0;
	memcpy(&smd->sm, &ops, sizeof(smd->sm));

	r = sm_ll_new_disk(&smd->ll, tm);
	if (r)
		goto bad;

	r = sm_ll_extend(&smd->ll, nr_blocks);
	if (r)
		goto bad;

	r = sm_disk_commit(&smd->sm);
	if (r)
		goto bad;

	return &smd->sm;

bad:
	kfree(smd);
	return ERR_PTR(r);
}
EXPORT_SYMBOL_GPL(dm_sm_disk_create);

struct dm_space_map *dm_sm_disk_open(struct dm_transaction_manager *tm,
				     void *root_le, size_t len)
{
	int r;
	struct sm_disk *smd;

	smd = kmalloc(sizeof(*smd), GFP_KERNEL);
	if (!smd)
		return ERR_PTR(-ENOMEM);

	smd->begin = 0;
	smd->nr_allocated_this_transaction = 0;
	memcpy(&smd->sm, &ops, sizeof(smd->sm));

	r = sm_ll_open_disk(&smd->ll, tm, root_le, len);
	if (r)
		goto bad;

	r = sm_disk_commit(&smd->sm);
	if (r)
		goto bad;

	return &smd->sm;

bad:
	kfree(smd);
	return ERR_PTR(r);
}
EXPORT_SYMBOL_GPL(dm_sm_disk_open);

/*----------------------------------------------------------------*/
