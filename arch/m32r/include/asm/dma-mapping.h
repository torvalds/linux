/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_M32R_DMA_MAPPING_H
#define _ASM_M32R_DMA_MAPPING_H

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/scatterlist.h>
#include <linux/dma-debug.h>
#include <linux/io.h>

static inline const struct dma_map_ops *get_arch_dma_ops(struct bus_type *bus)
{
	return &dma_noop_ops;
}

#endif /* _ASM_M32R_DMA_MAPPING_H */
