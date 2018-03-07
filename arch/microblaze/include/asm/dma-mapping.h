/*
 * Implements the generic device dma API for microblaze and the pci
 *
 * Copyright (C) 2009-2010 Michal Simek <monstr@monstr.eu>
 * Copyright (C) 2009-2010 PetaLogix
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License. See the file COPYING in the main directory of this
 * archive for more details.
 *
 * This file is base on powerpc and x86 dma-mapping.h versions
 * Copyright (C) 2004 IBM
 */

#ifndef _ASM_MICROBLAZE_DMA_MAPPING_H
#define _ASM_MICROBLAZE_DMA_MAPPING_H

/*
 * Available generic sets of operations
 */
extern const struct dma_map_ops dma_direct_ops;

static inline const struct dma_map_ops *get_arch_dma_ops(struct bus_type *bus)
{
	return &dma_direct_ops;
}

#endif	/* _ASM_MICROBLAZE_DMA_MAPPING_H */
