/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2006  Ralf Baechle <ralf@linux-mips.org>
 */
#ifndef __ASM_MACH_CAVIUM_OCTEON_DMA_COHERENCE_H
#define __ASM_MACH_CAVIUM_OCTEON_DMA_COHERENCE_H

#include <linux/bug.h>

struct device;

extern void octeon_pci_dma_init(void);
extern char *octeon_swiotlb;

#endif /* __ASM_MACH_CAVIUM_OCTEON_DMA_COHERENCE_H */
