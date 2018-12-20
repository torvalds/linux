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

extern const struct dma_map_ops *dma_ops;
extern struct ia64_machine_vector ia64_mv;
extern void set_iommu_machvec(void);

static inline const struct dma_map_ops *get_arch_dma_ops(struct bus_type *bus)
{
	return platform_dma_get_ops(NULL);
}

#endif /* _ASM_IA64_DMA_MAPPING_H */
