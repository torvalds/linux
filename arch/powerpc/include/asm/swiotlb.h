/*
 * Copyright (C) 2009 Becky Bruce, Freescale Semiconductor
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

#ifndef __ASM_SWIOTLB_H
#define __ASM_SWIOTLB_H

#include <linux/swiotlb.h>

extern struct dma_mapping_ops swiotlb_dma_ops;
extern struct dma_mapping_ops swiotlb_pci_dma_ops;

int swiotlb_arch_address_needs_mapping(struct device *, dma_addr_t,
				       size_t size);

static inline void dma_mark_clean(void *addr, size_t size) {}

extern unsigned int ppc_swiotlb_enable;
int __init swiotlb_setup_bus_notifier(void);

#endif /* __ASM_SWIOTLB_H */
