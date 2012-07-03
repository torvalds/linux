/*
 * Copyright (C) 2011 Red Hat, Inc.
 *
 * This file is released under the GPL.
 */

#include "dm-space-map-checker.h"

#include <linux/device-mapper.h>
#include <linux/export.h>

#ifdef CONFIG_DM_DEBUG_SPACE_MAPS

#define DM_MSG_PREFIX "space map checker"

/*----------------------------------------------------------------*/

struct count_array {
	dm_block_t nr;
	dm_block_t nr_free;

	uint32_t *counts;
};

static int ca_get_count(struct count_array *ca, dm_block_t b, uint32_t *count)
{
	if (b >= ca->nr)
		return -EINVAL;

	*count = ca->counts[b];
	return 0;
}

static int ca_count_more_than_one(struct count_array *ca, dm_block_t b, int *r)
{
	if (b >= ca->nr)
		return -EINVAL;

	*r = ca->counts[b] > 1;
	return 0;
}

static int ca_set_count(struct count_array *ca, dm_block_t b, uint32_t count)
{
	uint32_t old_count;

	if (b >= ca->nr)
		return -EINVAL;

	old_count = ca->counts[b];

	if (!count && old_count)
		ca->nr_free++;

	else if (count && !old_count)
		ca->nr_free--;

	ca->counts[b] = count;
	return 0;
}

static int ca_inc_block(struct count_array *ca, dm_block_t b)
{
	if (b >= ca->nr)
		return -EINVAL;

	ca_set_count(ca, b, ca->counts[b] + 1);
	return 0;
}

static int ca_dec_block(struct count_array *ca, dm_block_t b)
{
	if (b >= ca->nr)
		return -EINVAL;

	BUG_ON(ca->counts[b] == 0);
	ca_set_count(ca, b, ca->counts[b] - 1);
	return 0;
}

static int ca_create(struct count_array *ca, struct dm_space_map *sm)
{
	int r;
	dm_block_t nr_blocks;

	r = dm_sm_get_nr_blocks(sm, &nr_blocks);
	if (r)
		return r;

	ca->nr = nr_blocks;
	ca->nr_free = nr_blocks;
	ca->counts = kzalloc(sizeof(*ca->counts) * nr_blocks, GFP_KERNEL);
	if (!ca->counts)
		return -ENOMEM;

	return 0;
}

static int ca_load(struct count_array *ca, struct dm_space_map *sm)
{
	int r;
	uint32_t count;
	dm_block_t nr_blocks, i;

	r = dm_sm_get_nr_blocks(sm, &nr_blocks);
	if (r)
		return r;

	BUG_ON(ca->nr != nr_blocks);

	DMWARN("Loading debug space map from disk.  This may take some time");
	for (i = 0; i < nr_blocks; i++) {
		r = dm_sm_get_count(sm, i, &count);
		if (r) {
			DMERR("load failed");
			return r;
		}

		ca_set_count(ca, i, count);
	}
	DMWARN("Load complete");

	return 0;
}

static int ca_extend(struct count_array *ca, dm_block_t extra_blocks)
{
	dm_block_t nr_blocks = ca->nr + extra_blocks;
	uint32_t *counts = kzalloc(sizeof(*counts) * nr_blocks, GFP_KERNEL);
	if (!counts)
		return -ENOMEM;

	memcpy(counts, ca->counts, sizeof(*counts) * ca->nr);
	kfree(ca->counts);
	ca->nr = nr_blocks;
	ca->nr_free += extra_blocks;
	ca->counts = counts;
	return 0;
}

static int ca_commit(struct count_array *old, struct count_array *new)
{
	if (old->nr != new->nr) {
		BUG_ON(old->nr > new->nr);
		ca_extend(old, new->nr - old->nr);
	}

	BUG_ON(old->nr != new->nr);
	old->nr_free = new->nr_free;
	memcpy(old->counts, new->counts, sizeof(*old->counts) * old->nr);
	return 0;
}

static void ca_destroy(struct count_array *ca)
{
	kfree(ca->counts);
}

/*----------------------------------------------------------------*/

struct sm_checker {
	struct dm_space_map sm;

	struct count_array old_counts;
	struct count_array counts;

	struct dm_space_map *real_sm;
};

static void sm_checker_destroy(struct dm_space_map *sm)
{
	struct sm_checker *smc = container_of(sm, struct sm_checker, sm);

	dm_sm_destroy(smc->real_sm);
	ca_destroy(&smc->old_counts);
	ca_destroy(&smc->counts);
	kfree(smc);
}

static int sm_checker_get_nr_blocks(struct dm_space_map *sm, dm_block_t *count)
{
	struct sm_checker *smc = container_of(sm, struct sm_checker, sm);
	int r = dm_sm_get_nr_blocks(smc->real_sm, count);
	if (!r)
		BUG_ON(smc->old_counts.nr != *count);
	return r;
}

static int sm_checker_get_nr_free(struct dm_space_map *sm, dm_block_t *count)
{
	struct sm_checker *smc = container_of(sm, struct sm_checker, sm);
	int r = dm_sm_get_nr_free(smc->real_sm, count);
	if (!r) {
		/*
		 * Slow, but we know it's correct.
		 */
		dm_block_t b, n = 0;
		for (b = 0; b < smc->old_counts.nr; b++)
			if (smc->old_counts.counts[b] == 0 &&
			    smc->counts.counts[b] == 0)
				n++;

		if (n != *count)
			DMERR("free block counts differ, checker %u, sm-disk:%u",
			      (unsigned) n, (unsigned) *count);
	}
	return r;
}

static int sm_checker_new_block(struct dm_space_map *sm, dm_block_t *b)
{
	struct sm_checker *smc = container_of(sm, struct sm_checker, sm);
	int r = dm_sm_new_block(smc->real_sm, b);

	if (!r) {
		BUG_ON(*b >= smc->old_counts.nr);
		BUG_ON(smc->old_counts.counts[*b] != 0);
		BUG_ON(*b >= smc->counts.nr);
		BUG_ON(smc->counts.counts[*b] != 0);
		ca_set_count(&smc->counts, *b, 1);
	}

	return r;
}

static int sm_checker_inc_block(struct dm_space_map *sm, dm_block_t b)
{
	struct sm_checker *smc = container_of(sm, struct sm_checker, sm);
	int r = dm_sm_inc_block(smc->real_sm, b);
	int r2 = ca_inc_block(&smc->counts, b);
	BUG_ON(r != r2);
	return r;
}

static int sm_checker_dec_block(struct dm_space_map *sm, dm_block_t b)
{
	struct sm_checker *smc = container_of(sm, struct sm_checker, sm);
	int r = dm_sm_dec_block(smc->real_sm, b);
	int r2 = ca_dec_block(&smc->counts, b);
	BUG_ON(r != r2);
	return r;
}

static int sm_checker_get_count(struct dm_space_map *sm, dm_block_t b, uint32_t *result)
{
	struct sm_checker *smc = container_of(sm, struct sm_checker, sm);
	uint32_t result2 = 0;
	int r = dm_sm_get_count(smc->real_sm, b, result);
	int r2 = ca_get_count(&smc->counts, b, &result2);

	BUG_ON(r != r2);
	if (!r)
		BUG_ON(*result != result2);
	return r;
}

static int sm_checker_count_more_than_one(struct dm_space_map *sm, dm_block_t b, int *result)
{
	struct sm_checker *smc = container_of(sm, struct sm_checker, sm);
	int result2 = 0;
	int r = dm_sm_count_is_more_than_one(smc->real_sm, b, result);
	int r2 = ca_count_more_than_one(&smc->counts, b, &result2);

	BUG_ON(r != r2);
	if (!r)
		BUG_ON(!(*result) && result2);
	return r;
}

static int sm_checker_set_count(struct dm_space_map *sm, dm_block_t b, uint32_t count)
{
	struct sm_checker *smc = container_of(sm, struct sm_checker, sm);
	uint32_t old_rc;
	int r = dm_sm_set_count(smc->real_sm, b, count);
	int r2;

	BUG_ON(b >= smc->counts.nr);
	old_rc = smc->counts.counts[b];
	r2 = ca_set_count(&smc->counts, b, count);
	BUG_ON(r != r2);

	return r;
}

static int sm_checker_commit(struct dm_space_map *sm)
{
	struct sm_checker *smc = container_of(sm, struct sm_checker, sm);
	int r;

	r = dm_sm_commit(smc->real_sm);
	if (r)
		return r;

	r = ca_commit(&smc->old_counts, &smc->counts);
	if (r)
		return r;

	return 0;
}

static int sm_checker_extend(struct dm_space_map *sm, dm_block_t extra_blocks)
{
	struct sm_checker *smc = container_of(sm, struct sm_checker, sm);
	int r = dm_sm_extend(smc->real_sm, extra_blocks);
	if (r)
		return r;

	return ca_extend(&smc->counts, extra_blocks);
}

static int sm_checker_root_size(struct dm_space_map *sm, size_t *result)
{
	struct sm_checker *smc = container_of(sm, struct sm_checker, sm);
	return dm_sm_root_size(smc->real_sm, result);
}

static int sm_checker_copy_root(struct dm_space_map *sm, void *copy_to_here_le, size_t len)
{
	struct sm_checker *smc = container_of(sm, struct sm_checker, sm);
	return dm_sm_copy_root(smc->real_sm, copy_to_here_le, len);
}

/*----------------------------------------------------------------*/

static struct dm_space_map ops_ = {
	.destroy = sm_checker_destroy,
	.get_nr_blocks = sm_checker_get_nr_blocks,
	.get_nr_free = sm_checker_get_nr_free,
	.inc_block = sm_checker_inc_block,
	.dec_block = sm_checker_dec_block,
	.new_block = sm_checker_new_block,
	.get_count = sm_checker_get_count,
	.count_is_more_than_one = sm_checker_count_more_than_one,
	.set_count = sm_checker_set_count,
	.commit = sm_checker_commit,
	.extend = sm_checker_extend,
	.root_size = sm_checker_root_size,
	.copy_root = sm_checker_copy_root
};

struct dm_space_map *dm_sm_checker_create(struct dm_space_map *sm)
{
	int r;
	struct sm_checker *smc;

	if (IS_ERR_OR_NULL(sm))
		return ERR_PTR(-EINVAL);

	smc = kmalloc(sizeof(*smc), GFP_KERNEL);
	if (!smc)
		return ERR_PTR(-ENOMEM);

	memcpy(&smc->sm, &ops_, sizeof(smc->sm));
	r = ca_create(&smc->old_counts, sm);
	if (r) {
		kfree(smc);
		return ERR_PTR(r);
	}

	r = ca_create(&smc->counts, sm);
	if (r) {
		ca_destroy(&smc->old_counts);
		kfree(smc);
		return ERR_PTR(r);
	}

	smc->real_sm = sm;

	r = ca_load(&smc->counts, sm);
	if (r) {
		ca_destroy(&smc->counts);
		ca_destroy(&smc->old_counts);
		kfree(smc);
		return ERR_PTR(r);
	}

	r = ca_commit(&smc->old_counts, &smc->counts);
	if (r) {
		ca_destroy(&smc->counts);
		ca_destroy(&smc->old_counts);
		kfree(smc);
		return ERR_PTR(r);
	}

	return &smc->sm;
}
EXPORT_SYMBOL_GPL(dm_sm_checker_create);

struct dm_space_map *dm_sm_checker_create_fresh(struct dm_space_map *sm)
{
	int r;
	struct sm_checker *smc;

	if (IS_ERR_OR_NULL(sm))
		return ERR_PTR(-EINVAL);

	smc = kmalloc(sizeof(*smc), GFP_KERNEL);
	if (!smc)
		return ERR_PTR(-ENOMEM);

	memcpy(&smc->sm, &ops_, sizeof(smc->sm));
	r = ca_create(&smc->old_counts, sm);
	if (r) {
		kfree(smc);
		return ERR_PTR(r);
	}

	r = ca_create(&smc->counts, sm);
	if (r) {
		ca_destroy(&smc->old_counts);
		kfree(smc);
		return ERR_PTR(r);
	}

	smc->real_sm = sm;
	return &smc->sm;
}
EXPORT_SYMBOL_GPL(dm_sm_checker_create_fresh);

/*----------------------------------------------------------------*/

#else

struct dm_space_map *dm_sm_checker_create(struct dm_space_map *sm)
{
	return sm;
}
EXPORT_SYMBOL_GPL(dm_sm_checker_create);

struct dm_space_map *dm_sm_checker_create_fresh(struct dm_space_map *sm)
{
	return sm;
}
EXPORT_SYMBOL_GPL(dm_sm_checker_create_fresh);

/*----------------------------------------------------------------*/

#endif
