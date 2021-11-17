/*
 * Copyright (C) 2011 Red Hat, Inc.
 *
 * This file is released under the GPL.
 */

#ifndef _LINUX_DM_SPACE_MAP_H
#define _LINUX_DM_SPACE_MAP_H

#include "dm-block-manager.h"

typedef void (*dm_sm_threshold_fn)(void *context);

/*
 * struct dm_space_map keeps a record of how many times each block in a device
 * is referenced.  It needs to be fixed on disk as part of the transaction.
 */
struct dm_space_map {
	void (*destroy)(struct dm_space_map *sm);

	/*
	 * You must commit before allocating the newly added space.
	 */
	int (*extend)(struct dm_space_map *sm, dm_block_t extra_blocks);

	/*
	 * Extensions do not appear in this count until after commit has
	 * been called.
	 */
	int (*get_nr_blocks)(struct dm_space_map *sm, dm_block_t *count);

	/*
	 * Space maps must never allocate a block from the previous
	 * transaction, in case we need to rollback.  This complicates the
	 * semantics of get_nr_free(), it should return the number of blocks
	 * that are available for allocation _now_.  For instance you may
	 * have blocks with a zero reference count that will not be
	 * available for allocation until after the next commit.
	 */
	int (*get_nr_free)(struct dm_space_map *sm, dm_block_t *count);

	int (*get_count)(struct dm_space_map *sm, dm_block_t b, uint32_t *result);
	int (*count_is_more_than_one)(struct dm_space_map *sm, dm_block_t b,
				      int *result);
	int (*set_count)(struct dm_space_map *sm, dm_block_t b, uint32_t count);

	int (*commit)(struct dm_space_map *sm);

	int (*inc_blocks)(struct dm_space_map *sm, dm_block_t b, dm_block_t e);
	int (*dec_blocks)(struct dm_space_map *sm, dm_block_t b, dm_block_t e);

	/*
	 * new_block will increment the returned block.
	 */
	int (*new_block)(struct dm_space_map *sm, dm_block_t *b);

	/*
	 * The root contains all the information needed to fix the space map.
	 * Generally this info is small, so squirrel it away in a disk block
	 * along with other info.
	 */
	int (*root_size)(struct dm_space_map *sm, size_t *result);
	int (*copy_root)(struct dm_space_map *sm, void *copy_to_here_le, size_t len);

	/*
	 * You can register one threshold callback which is edge-triggered
	 * when the free space in the space map drops below the threshold.
	 */
	int (*register_threshold_callback)(struct dm_space_map *sm,
					   dm_block_t threshold,
					   dm_sm_threshold_fn fn,
					   void *context);
};

/*----------------------------------------------------------------*/

static inline void dm_sm_destroy(struct dm_space_map *sm)
{
	sm->destroy(sm);
}

static inline int dm_sm_extend(struct dm_space_map *sm, dm_block_t extra_blocks)
{
	return sm->extend(sm, extra_blocks);
}

static inline int dm_sm_get_nr_blocks(struct dm_space_map *sm, dm_block_t *count)
{
	return sm->get_nr_blocks(sm, count);
}

static inline int dm_sm_get_nr_free(struct dm_space_map *sm, dm_block_t *count)
{
	return sm->get_nr_free(sm, count);
}

static inline int dm_sm_get_count(struct dm_space_map *sm, dm_block_t b,
				  uint32_t *result)
{
	return sm->get_count(sm, b, result);
}

static inline int dm_sm_count_is_more_than_one(struct dm_space_map *sm,
					       dm_block_t b, int *result)
{
	return sm->count_is_more_than_one(sm, b, result);
}

static inline int dm_sm_set_count(struct dm_space_map *sm, dm_block_t b,
				  uint32_t count)
{
	return sm->set_count(sm, b, count);
}

static inline int dm_sm_commit(struct dm_space_map *sm)
{
	return sm->commit(sm);
}

static inline int dm_sm_inc_blocks(struct dm_space_map *sm, dm_block_t b, dm_block_t e)
{
	return sm->inc_blocks(sm, b, e);
}

static inline int dm_sm_inc_block(struct dm_space_map *sm, dm_block_t b)
{
	return dm_sm_inc_blocks(sm, b, b + 1);
}

static inline int dm_sm_dec_blocks(struct dm_space_map *sm, dm_block_t b, dm_block_t e)
{
	return sm->dec_blocks(sm, b, e);
}

static inline int dm_sm_dec_block(struct dm_space_map *sm, dm_block_t b)
{
	return dm_sm_dec_blocks(sm, b, b + 1);
}

static inline int dm_sm_new_block(struct dm_space_map *sm, dm_block_t *b)
{
	return sm->new_block(sm, b);
}

static inline int dm_sm_root_size(struct dm_space_map *sm, size_t *result)
{
	return sm->root_size(sm, result);
}

static inline int dm_sm_copy_root(struct dm_space_map *sm, void *copy_to_here_le, size_t len)
{
	return sm->copy_root(sm, copy_to_here_le, len);
}

static inline int dm_sm_register_threshold_callback(struct dm_space_map *sm,
						    dm_block_t threshold,
						    dm_sm_threshold_fn fn,
						    void *context)
{
	if (sm->register_threshold_callback)
		return sm->register_threshold_callback(sm, threshold, fn, context);

	return -EINVAL;
}


#endif	/* _LINUX_DM_SPACE_MAP_H */
