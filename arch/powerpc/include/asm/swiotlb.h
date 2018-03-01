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

extern const struct dma_map_ops powerpc_swiotlb_dma_ops;

extern unsigned int ppc_swiotlb_enable;
int __init swiotlb_setup_bus_notifier(void);

extern void pci_dma_dev_setup_swiotlb(struct pci_dev *pdev);

#ifdef CONFIG_SWIOTLB
void swiotlb_detect_4g(void);
#else
static inline void swiotlb_detect_4g(void) {}
#endif

#endif /* __ASM_SWIOTLB_H */
