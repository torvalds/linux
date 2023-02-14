/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_IA64_DMA_MAPPING_H
#define _ASM_IA64_DMA_MAPPING_H

/*
 * Copyright (C) 2003-2004 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 */
extern const struct dma_map_ops *dma_ops;

static inline const struct dma_map_ops *get_arch_dma_ops(void)
{
	return dma_ops;
}

#endif /* _ASM_IA64_DMA_MAPPING_H */
