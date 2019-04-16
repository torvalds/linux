/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ALPHA_DMA_MAPPING_H
#define _ALPHA_DMA_MAPPING_H

extern const struct dma_map_ops alpha_pci_ops;

static inline const struct dma_map_ops *get_arch_dma_ops(struct bus_type *bus)
{
#ifdef CONFIG_ALPHA_JENSEN
	return NULL;
#else
	return &alpha_pci_ops;
#endif
}

#endif	/* _ALPHA_DMA_MAPPING_H */
