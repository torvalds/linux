/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _M68K_DMA_MAPPING_H
#define _M68K_DMA_MAPPING_H

extern const struct dma_map_ops m68k_dma_ops;

static inline const struct dma_map_ops *get_arch_dma_ops(struct bus_type *bus)
{
        return &m68k_dma_ops;
}

#endif  /* _M68K_DMA_MAPPING_H */
