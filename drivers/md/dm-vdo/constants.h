/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2023 Red Hat
 */

#ifndef VDO_CONSTANTS_H
#define VDO_CONSTANTS_H

#include <linux/blkdev.h>

#include "types.h"

enum {
	/*
	 * The maximum number of contiguous PBNs which will go to a single bio submission queue,
	 * assuming there is more than one queue.
	 */
	VDO_BIO_ROTATION_INTERVAL_LIMIT = 1024,

	/* The number of entries on a block map page */
	VDO_BLOCK_MAP_ENTRIES_PER_PAGE = 812,

	/* The origin of the flat portion of the block map */
	VDO_BLOCK_MAP_FLAT_PAGE_ORIGIN = 1,

	/*
	 * The height of a block map tree. Assuming a root count of 60 and 812 entries per page,
	 * this is big enough to represent almost 95 PB of logical space.
	 */
	VDO_BLOCK_MAP_TREE_HEIGHT = 5,

	/* The default number of bio submission queues. */
	DEFAULT_VDO_BIO_SUBMIT_QUEUE_COUNT = 4,

	/* The number of contiguous PBNs to be submitted to a single bio queue. */
	DEFAULT_VDO_BIO_SUBMIT_QUEUE_ROTATE_INTERVAL = 64,

	/* The number of trees in the arboreal block map */
	DEFAULT_VDO_BLOCK_MAP_TREE_ROOT_COUNT = 60,

	/* The default size of the recovery journal, in blocks */
	DEFAULT_VDO_RECOVERY_JOURNAL_SIZE = 32 * 1024,

	/* The default size of each slab journal, in blocks */
	DEFAULT_VDO_SLAB_JOURNAL_SIZE = 224,

	/*
	 * The initial size of lbn_operations and pbn_operations, which is based upon the expected
	 * maximum number of outstanding VIOs. This value was chosen to make it highly unlikely
	 * that the maps would need to be resized.
	 */
	VDO_LOCK_MAP_CAPACITY = 10000,

	/* The maximum number of logical zones */
	MAX_VDO_LOGICAL_ZONES = 60,

	/* The maximum number of physical zones */
	MAX_VDO_PHYSICAL_ZONES = 16,

	/* The base-2 logarithm of the maximum blocks in one slab */
	MAX_VDO_SLAB_BITS = 23,

	/* The maximum number of slabs the slab depot supports */
	MAX_VDO_SLABS = 8192,

	/*
	 * The maximum number of block map pages to load simultaneously during recovery or rebuild.
	 */
	MAXIMUM_SIMULTANEOUS_VDO_BLOCK_MAP_RESTORATION_READS = 1024,

	/* The maximum number of entries in the slab summary */
	MAXIMUM_VDO_SLAB_SUMMARY_ENTRIES = MAX_VDO_SLABS * MAX_VDO_PHYSICAL_ZONES,

	/* The maximum number of total threads in a VDO thread configuration. */
	MAXIMUM_VDO_THREADS = 100,

	/* The maximum number of VIOs in the system at once */
	MAXIMUM_VDO_USER_VIOS = 2048,

	/* The only physical block size supported by VDO */
	VDO_BLOCK_SIZE = 4096,

	/* The number of sectors per block */
	VDO_SECTORS_PER_BLOCK = (VDO_BLOCK_SIZE >> SECTOR_SHIFT),

	/* The size of a sector that will not be torn */
	VDO_SECTOR_SIZE = 512,

	/* The physical block number reserved for storing the zero block */
	VDO_ZERO_BLOCK = 0,
};

#endif /* VDO_CONSTANTS_H */
