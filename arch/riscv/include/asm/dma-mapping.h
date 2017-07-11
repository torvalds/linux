/*
 * Copyright (C) 2003-2004 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 * Copyright (C) 2012 ARM Ltd.
 * Copyright (C) 2016 SiFive, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef __ASM_RISCV_DMA_MAPPING_H
#define __ASM_RISCV_DMA_MAPPING_H

/* Use ops->dma_mapping_error (if it exists) or assume success */
// #undef DMA_ERROR_CODE

static inline const struct dma_map_ops *get_arch_dma_ops(struct bus_type *bus)
{
	return &dma_noop_ops;
}

static inline bool dma_capable(struct device *dev, dma_addr_t addr, size_t size)
{
	if (!dev->dma_mask)
		return false;

	return addr + size - 1 <= *dev->dma_mask;
}

#endif	/* __ASM_RISCV_DMA_MAPPING_H */
