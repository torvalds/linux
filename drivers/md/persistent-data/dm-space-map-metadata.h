/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2011 Red Hat, Inc.
 *
 * This file is released under the GPL.
 */

#ifndef DM_SPACE_MAP_METADATA_H
#define DM_SPACE_MAP_METADATA_H

#include "dm-transaction-manager.h"

#define DM_SM_METADATA_BLOCK_SIZE (4096 >> SECTOR_SHIFT)

/*
 * The metadata device is currently limited in size.
 *
 * We have one block of index, which can hold 255 index entries.  Each
 * index entry contains allocation info about ~16k metadata blocks.
 */
#define DM_SM_METADATA_MAX_BLOCKS (255 * ((1 << 14) - 64))
#define DM_SM_METADATA_MAX_SECTORS (DM_SM_METADATA_MAX_BLOCKS * DM_SM_METADATA_BLOCK_SIZE)

/*
 * Unfortunately we have to use two-phase construction due to the cycle
 * between the tm and sm.
 */
struct dm_space_map *dm_sm_metadata_init(void);

/*
 * Create a fresh space map.
 */
int dm_sm_metadata_create(struct dm_space_map *sm,
			  struct dm_transaction_manager *tm,
			  dm_block_t nr_blocks,
			  dm_block_t superblock);

/*
 * Open from a previously-recorded root.
 */
int dm_sm_metadata_open(struct dm_space_map *sm,
			struct dm_transaction_manager *tm,
			void *root_le, size_t len);

#endif	/* DM_SPACE_MAP_METADATA_H */
