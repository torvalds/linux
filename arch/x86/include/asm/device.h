/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_DEVICE_H
#define _ASM_X86_DEVICE_H

struct dev_archdata {
#if defined(CONFIG_INTEL_IOMMU) || defined(CONFIG_AMD_IOMMU)
	void *iommu; /* hook for IOMMU specific extension */
#endif
};

#if defined(CONFIG_X86_DEV_DMA_OPS) && defined(CONFIG_PCI_DOMAINS)
struct dma_domain {
	struct list_head node;
	const struct dma_map_ops *dma_ops;
	int domain_nr;
};
void add_dma_domain(struct dma_domain *domain);
void del_dma_domain(struct dma_domain *domain);
#endif

struct pdev_archdata {
};

#endif /* _ASM_X86_DEVICE_H */
