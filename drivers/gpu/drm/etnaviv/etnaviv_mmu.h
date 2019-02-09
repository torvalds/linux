/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2015-2018 Etnaviv Project
 */

#ifndef __ETNAVIV_MMU_H__
#define __ETNAVIV_MMU_H__

#define ETNAVIV_PROT_READ	(1 << 0)
#define ETNAVIV_PROT_WRITE	(1 << 1)

enum etnaviv_iommu_version {
	ETNAVIV_IOMMU_V1 = 0,
	ETNAVIV_IOMMU_V2,
};

struct etnaviv_gpu;
struct etnaviv_vram_mapping;
struct etnaviv_iommu_domain;

struct etnaviv_iommu_domain_ops {
	void (*free)(struct etnaviv_iommu_domain *);
	int (*map)(struct etnaviv_iommu_domain *domain, unsigned long iova,
		   phys_addr_t paddr, size_t size, int prot);
	size_t (*unmap)(struct etnaviv_iommu_domain *domain, unsigned long iova,
			size_t size);
	size_t (*dump_size)(struct etnaviv_iommu_domain *);
	void (*dump)(struct etnaviv_iommu_domain *, void *);
};

struct etnaviv_iommu_domain {
	struct device *dev;
	void *bad_page_cpu;
	dma_addr_t bad_page_dma;
	u64 base;
	u64 size;

	const struct etnaviv_iommu_domain_ops *ops;
};

struct etnaviv_iommu {
	struct etnaviv_gpu *gpu;
	struct etnaviv_iommu_domain *domain;

	enum etnaviv_iommu_version version;

	/* memory manager for GPU address area */
	struct mutex lock;
	struct list_head mappings;
	struct drm_mm mm;
	bool need_flush;
};

struct etnaviv_gem_object;

int etnaviv_iommu_map_gem(struct etnaviv_iommu *mmu,
	struct etnaviv_gem_object *etnaviv_obj, u32 memory_base,
	struct etnaviv_vram_mapping *mapping);
void etnaviv_iommu_unmap_gem(struct etnaviv_iommu *mmu,
	struct etnaviv_vram_mapping *mapping);

int etnaviv_iommu_get_suballoc_va(struct etnaviv_gpu *gpu, dma_addr_t paddr,
				  struct drm_mm_node *vram_node, size_t size,
				  u32 *iova);
void etnaviv_iommu_put_suballoc_va(struct etnaviv_gpu *gpu,
				   struct drm_mm_node *vram_node, size_t size,
				   u32 iova);

size_t etnaviv_iommu_dump_size(struct etnaviv_iommu *iommu);
void etnaviv_iommu_dump(struct etnaviv_iommu *iommu, void *buf);

struct etnaviv_iommu *etnaviv_iommu_new(struct etnaviv_gpu *gpu);
void etnaviv_iommu_destroy(struct etnaviv_iommu *iommu);
void etnaviv_iommu_restore(struct etnaviv_gpu *gpu);

#endif /* __ETNAVIV_MMU_H__ */
