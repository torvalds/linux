/*
 * Copyright (C) 2011 Red Hat, Inc.
 *
 * This file is released under the GPL.
 */

#ifndef _DM_PERSISTENT_DATA_INTERNAL_H
#define _DM_PERSISTENT_DATA_INTERNAL_H

#include "dm-block-manager.h"

static inline unsigned dm_hash_block(dm_block_t b, unsigned hash_mask)
{
	const unsigned BIG_PRIME = 4294967291UL;

	return (((unsigned) b) * BIG_PRIME) & hash_mask;
}

#endif	/* _PERSISTENT_DATA_INTERNAL_H */
