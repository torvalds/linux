/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ASMARM_DMA_MAPPING_H
#define ASMARM_DMA_MAPPING_H

#ifdef __KERNEL__

#include <linux/mm_types.h>
#include <linux/scatterlist.h>

#include <xen/xen.h>
#include <asm/xen/hypervisor.h>

extern const struct dma_map_ops arm_dma_ops;
extern const struct dma_map_ops arm_coherent_dma_ops;

static inline const struct dma_map_ops *get_arch_dma_ops(struct bus_type *bus)
{
	if (IS_ENABLED(CONFIG_MMU) && !IS_ENABLED(CONFIG_ARM_LPAE))
		return &arm_dma_ops;
	return NULL;
}

#endif /* __KERNEL__ */
#endif
