/*
 * DMA Mapping glue for ARC
 *
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef ASM_ARC_DMA_MAPPING_H
#define ASM_ARC_DMA_MAPPING_H

extern struct dma_map_ops arc_dma_ops;

static inline struct dma_map_ops *get_dma_ops(struct device *dev)
{
	return &arc_dma_ops;
}

#endif
