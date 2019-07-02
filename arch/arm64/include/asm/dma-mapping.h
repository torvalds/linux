/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 ARM Ltd.
 */
#ifndef __ASM_DMA_MAPPING_H
#define __ASM_DMA_MAPPING_H

#ifdef __KERNEL__

#include <linux/types.h>
#include <linux/vmalloc.h>

#include <xen/xen.h>
#include <asm/xen/hypervisor.h>

static inline const struct dma_map_ops *get_arch_dma_ops(struct bus_type *bus)
{
	return NULL;
}

/*
 * Do not use this function in a driver, it is only provided for
 * arch/arm/mm/xen.c, which is used by arm64 as well.
 */
static inline bool is_device_dma_coherent(struct device *dev)
{
	return dev->dma_coherent;
}

#endif	/* __KERNEL__ */
#endif	/* __ASM_DMA_MAPPING_H */
