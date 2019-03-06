/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ___ASM_SPARC_DMA_MAPPING_H
#define ___ASM_SPARC_DMA_MAPPING_H

#include <asm/cpu_type.h>

extern const struct dma_map_ops *dma_ops;

extern struct bus_type pci_bus_type;

static inline const struct dma_map_ops *get_arch_dma_ops(struct bus_type *bus)
{
#ifdef CONFIG_SPARC_LEON
	if (sparc_cpu_model == sparc_leon)
		return NULL;
#endif
#if defined(CONFIG_SPARC32) && defined(CONFIG_PCI)
	if (bus == &pci_bus_type)
		return NULL;
#endif
	return dma_ops;
}

#endif
