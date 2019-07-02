/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *
 * Copyright (C) 2013 Freescale Semiconductor, Inc.
 */

#ifndef __FSL_PAMU_STASH_H
#define __FSL_PAMU_STASH_H

/* cache stash targets */
enum pamu_stash_target {
	PAMU_ATTR_CACHE_L1 = 1,
	PAMU_ATTR_CACHE_L2,
	PAMU_ATTR_CACHE_L3,
};

/*
 * This attribute allows configuring stashig specific parameters
 * in the PAMU hardware.
 */

struct pamu_stash_attribute {
	u32	cpu;	/* cpu number */
	u32	cache;	/* cache to stash to: L1,L2,L3 */
};

#endif  /* __FSL_PAMU_STASH_H */
