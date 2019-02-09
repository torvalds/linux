/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_IA64_DMA_MAPPING_H
#define _ASM_IA64_DMA_MAPPING_H

/*
 * Copyright (C) 2003-2004 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 */
#include <asm/machvec.h>
#include <linux/scatterlist.h>
#include <linux/dma-debug.h>

#define ARCH_HAS_DMA_GET_REQUIRED_MASK

extern const struct dma_map_ops *dma_ops;
extern struct ia64_machine_vector ia64_mv;
extern void set_iommu_machvec(void);

extern void machvec_dma_sync_single(struct device *, dma_addr_t, size_t,
				    enum dma_data_direction);
extern void machvec_dma_sync_sg(struct device *, struct scatterlist *, int,
				enum dma_data_direction);

static inline const struct dma_map_ops *get_arch_dma_ops(struct bus_type *bus)
{
	return platform_dma_get_ops(NULL);
}

#endif /* _ASM_IA64_DMA_MAPPING_H */
