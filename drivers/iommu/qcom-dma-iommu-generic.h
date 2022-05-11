/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 */

#ifndef __QCOM_DMA_IOMMU_GENERIC_H
#define __QCOM_DMA_IOMMU_GENERIC_H

#include <linux/device.h>
#include <linux/dma-direction.h>
#include <linux/pci.h>

#ifdef CONFIG_IOMMU_IO_PGTABLE_FAST

bool qcom_dma_iommu_is_ready(void);
extern int __init qcom_dma_iommu_generic_driver_init(void);
extern void qcom_dma_iommu_generic_driver_exit(void);

struct pci_host_bridge *qcom_pci_find_host_bridge(struct pci_bus *bus);

void qcom_arch_sync_dma_for_device(phys_addr_t paddr, size_t size,
		enum dma_data_direction dir);
void qcom_arch_sync_dma_for_cpu(phys_addr_t paddr, size_t size,
		enum dma_data_direction dir);
void qcom_arch_dma_prep_coherent(struct page *page, size_t size);

/* kernel/dma/contiguous.c */
struct page *qcom_dma_alloc_from_contiguous(struct device *dev, size_t count,
	unsigned int align, bool no_warn);
bool qcom_dma_release_from_contiguous(struct device *dev, struct page *pages,
	int count);
struct page *qcom_dma_alloc_contiguous(struct device *dev, size_t size,
	gfp_t gfp);
void qcom_dma_free_contiguous(struct device *dev, struct page *page,
	size_t size);

/* kernel/dma/remap.c */
struct page **qcom_dma_common_find_pages(void *cpu_addr);
void *qcom_dma_common_pages_remap(struct page **pages, size_t size,
	 pgprot_t prot, const void *caller);
void *qcom_dma_common_contiguous_remap(struct page *page, size_t size,
	pgprot_t prot, const void *caller);
void qcom_dma_common_free_remap(void *cpu_addr, size_t size);
void *qcom_dma_alloc_from_pool(struct device *dev, size_t size,
	struct page **ret_page, gfp_t flags);
bool qcom_dma_free_from_pool(struct device *dev, void *start, size_t size);

int qcom_dma_mmap_from_dev_coherent(struct device *dev,
	struct vm_area_struct *vma, void *vaddr, size_t size, int *ret);

/* kernel/dma/mapping.c */
pgprot_t qcom_dma_pgprot(struct device *dev, pgprot_t prot,
	unsigned long attrs);

/* DMA-IOMMU utilities */
int qcom_dma_info_to_prot(enum dma_data_direction dir, bool coherent,
		     unsigned long attrs);
size_t qcom_iommu_dma_prepare_map_sg(struct device *dev, struct iova_domain *iovad,
				struct scatterlist *sg, int nents);
int qcom_iommu_dma_finalise_sg(struct device *dev, struct scatterlist *sg, int nents,
		dma_addr_t dma_addr);
void qcom_iommu_dma_invalidate_sg(struct scatterlist *sg, int nents);
int qcom_iommu_dma_mmap(struct device *dev, struct vm_area_struct *vma,
		void *cpu_addr, dma_addr_t dma_addr, size_t size,
		unsigned long attrs);
int qcom_iommu_dma_get_sgtable(struct device *dev, struct sg_table *sgt,
		void *cpu_addr, dma_addr_t dma_addr, size_t size,
		unsigned long attrs);

#else /*CONFIG_IOMMU_IO_PGTABLE_FAST*/

static inline bool qcom_dma_iommu_is_ready(void)
{
	return true;
}

static inline int __init qcom_dma_iommu_generic_driver_init(void)
{
	return 0;
}

static inline void qcom_dma_iommu_generic_driver_exit(void) {}

#endif /*CONFIG_IOMMU_IO_PGTABLE_FAST*/
#endif	/* __QCOM_DMA_IOMMU_GENERIC_H */
