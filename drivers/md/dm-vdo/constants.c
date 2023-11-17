// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2023 Red Hat
 */

#include "types.h"

/* The maximum logical space is 4 petabytes, which is 1 terablock. */
const block_count_t MAXIMUM_VDO_LOGICAL_BLOCKS = 1024ULL * 1024 * 1024 * 1024;

/* The maximum physical space is 256 terabytes, which is 64 gigablocks. */
const block_count_t MAXIMUM_VDO_PHYSICAL_BLOCKS = 1024ULL * 1024 * 1024 * 64;

/* unit test minimum */
const block_count_t MINIMUM_VDO_SLAB_JOURNAL_BLOCKS = 2;
