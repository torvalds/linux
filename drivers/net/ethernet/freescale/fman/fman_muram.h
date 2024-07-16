/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0-or-later */
/*
 * Copyright 2008 - 2015 Freescale Semiconductor Inc.
 */

#ifndef __FM_MURAM_EXT
#define __FM_MURAM_EXT

#include <linux/types.h>

#define FM_MURAM_INVALID_ALLOCATION	-1

/* Structure for FM MURAM information */
struct muram_info;

struct muram_info *fman_muram_init(phys_addr_t base, size_t size);

unsigned long fman_muram_offset_to_vbase(struct muram_info *muram,
					 unsigned long offset);

unsigned long fman_muram_alloc(struct muram_info *muram, size_t size);

void fman_muram_free_mem(struct muram_info *muram, unsigned long offset,
			 size_t size);

#endif /* __FM_MURAM_EXT */
