/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2011 Red Hat, Inc.
 *
 * This file is released under the GPL.
 */

#ifndef _LINUX_DM_BLOCK_MANAGER_H
#define _LINUX_DM_BLOCK_MANAGER_H

#include <linux/types.h>
#include <linux/blkdev.h>

/*----------------------------------------------------------------*/

/*
 * Block number.
 */
typedef uint64_t dm_block_t;
struct dm_block;

dm_block_t dm_block_location(struct dm_block *b);
void *dm_block_data(struct dm_block *b);

/*----------------------------------------------------------------*/

/*
 * @name should be a unique identifier for the block manager, no longer
 * than 32 chars.
 *
 * @max_held_per_thread should be the maximum number of locks, read or
 * write, that an individual thread holds at any one time.
 */
struct dm_block_manager;
struct dm_block_manager *dm_block_manager_create(
	struct block_device *bdev, unsigned int block_size,
	unsigned int max_held_per_thread);
void dm_block_manager_destroy(struct dm_block_manager *bm);
void dm_block_manager_reset(struct dm_block_manager *bm);

unsigned int dm_bm_block_size(struct dm_block_manager *bm);
dm_block_t dm_bm_nr_blocks(struct dm_block_manager *bm);

/*----------------------------------------------------------------*/

/*
 * The validator allows the caller to verify newly-read data and modify
 * the data just before writing, e.g. to calculate checksums.  It's
 * important to be consistent with your use of validators.  The only time
 * you can change validators is if you call dm_bm_write_lock_zero.
 */
struct dm_block_validator {
	const char *name;
	void (*prepare_for_write)(const struct dm_block_validator *v,
				  struct dm_block *b, size_t block_size);

	/*
	 * Return 0 if the checksum is valid or < 0 on error.
	 */
	int (*check)(const struct dm_block_validator *v,
		     struct dm_block *b, size_t block_size);
};

/*----------------------------------------------------------------*/

/*
 * You can have multiple concurrent readers or a single writer holding a
 * block lock.
 */

/*
 * dm_bm_lock() locks a block and returns through @result a pointer to
 * memory that holds a copy of that block.  If you have write-locked the
 * block then any changes you make to memory pointed to by @result will be
 * written back to the disk sometime after dm_bm_unlock is called.
 */
int dm_bm_read_lock(struct dm_block_manager *bm, dm_block_t b,
		    const struct dm_block_validator *v,
		    struct dm_block **result);

int dm_bm_write_lock(struct dm_block_manager *bm, dm_block_t b,
		     const struct dm_block_validator *v,
		     struct dm_block **result);

/*
 * The *_try_lock variants return -EWOULDBLOCK if the block isn't
 * available immediately.
 */
int dm_bm_read_try_lock(struct dm_block_manager *bm, dm_block_t b,
			const struct dm_block_validator *v,
			struct dm_block **result);

/*
 * Use dm_bm_write_lock_zero() when you know you're going to
 * overwrite the block completely.  It saves a disk read.
 */
int dm_bm_write_lock_zero(struct dm_block_manager *bm, dm_block_t b,
			  const struct dm_block_validator *v,
			  struct dm_block **result);

void dm_bm_unlock(struct dm_block *b);

/*
 * It's a common idiom to have a superblock that should be committed last.
 *
 * @superblock should be write-locked on entry. It will be unlocked during
 * this function.  All dirty blocks are guaranteed to be written and flushed
 * before the superblock.
 *
 * This method always blocks.
 */
int dm_bm_flush(struct dm_block_manager *bm);

/*
 * Request data is prefetched into the cache.
 */
void dm_bm_prefetch(struct dm_block_manager *bm, dm_block_t b);

/*
 * Switches the bm to a read only mode.  Once read-only mode
 * has been entered the following functions will return -EPERM.
 *
 *   dm_bm_write_lock
 *   dm_bm_write_lock_zero
 *   dm_bm_flush_and_unlock
 *
 * Additionally you should not use dm_bm_unlock_move, however no error will
 * be returned if you do.
 */
bool dm_bm_is_read_only(struct dm_block_manager *bm);
void dm_bm_set_read_only(struct dm_block_manager *bm);
void dm_bm_set_read_write(struct dm_block_manager *bm);

u32 dm_bm_checksum(const void *data, size_t len, u32 init_xor);

/*----------------------------------------------------------------*/

#endif	/* _LINUX_DM_BLOCK_MANAGER_H */
