/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2003 - 2005 Tensilica Inc.
 * Copyright (C) 2015 Cadence Design Systems Inc.
 */

#ifndef _XTENSA_DMA_MAPPING_H
#define _XTENSA_DMA_MAPPING_H

#include <asm/cache.h>
#include <asm/io.h>

#include <linux/mm.h>
#include <linux/scatterlist.h>

extern const struct dma_map_ops xtensa_dma_map_ops;

static inline const struct dma_map_ops *get_arch_dma_ops(struct bus_type *bus)
{
	return &xtensa_dma_map_ops;
}

#endif	/* _XTENSA_DMA_MAPPING_H */
