/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2004 IBM
 */
#ifndef _ASM_DMA_MAPPING_H
#define _ASM_DMA_MAPPING_H

static inline const struct dma_map_ops *get_arch_dma_ops(struct bus_type *bus)
{
	/* We don't handle the NULL dev case for ISA for now. We could
	 * do it via an out of line call but it is not needed for now. The
	 * only ISA DMA device we support is the floppy and we have a hack
	 * in the floppy driver directly to get a device for us.
	 */
	return NULL;
}

#endif	/* _ASM_DMA_MAPPING_H */
