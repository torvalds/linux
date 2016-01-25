/*
 * Copyright (C) 2015 Etnaviv Project
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __ETNAVIV_MMU_H__
#define __ETNAVIV_MMU_H__

#include <linux/iommu.h>

enum etnaviv_iommu_version {
	ETNAVIV_IOMMU_V1 = 0,
	ETNAVIV_IOMMU_V2,
};

struct etnaviv_gpu;
struct etnaviv_vram_mapping;

struct etnaviv_iommu_ops {
	struct iommu_ops ops;
	size_t (*dump_size)(struct iommu_domain *);
	void (*dump)(struct iommu_domain *, void *);
};

struct etnaviv_iommu {
	struct etnaviv_gpu *gpu;
	struct iommu_domain *domain;

	enum etnaviv_iommu_version version;

	/* memory manager for GPU address area */
	struct mutex lock;
	struct list_head mappings;
	struct drm_mm mm;
	u32 last_iova;
	bool need_flush;
};

struct etnaviv_gem_object;

int etnaviv_iommu_attach(struct etnaviv_iommu *iommu, const char **names,
	int cnt);
int etnaviv_iommu_map(struct etnaviv_iommu *iommu, u32 iova,
	struct sg_table *sgt, unsigned len, int prot);
int etnaviv_iommu_unmap(struct etnaviv_iommu *iommu, u32 iova,
	struct sg_table *sgt, unsigned len);
int etnaviv_iommu_map_gem(struct etnaviv_iommu *mmu,
	struct etnaviv_gem_object *etnaviv_obj, u32 memory_base,
	struct etnaviv_vram_mapping *mapping);
void etnaviv_iommu_unmap_gem(struct etnaviv_iommu *mmu,
	struct etnaviv_vram_mapping *mapping);
void etnaviv_iommu_destroy(struct etnaviv_iommu *iommu);

size_t etnaviv_iommu_dump_size(struct etnaviv_iommu *iommu);
void etnaviv_iommu_dump(struct etnaviv_iommu *iommu, void *buf);

struct etnaviv_iommu *etnaviv_iommu_new(struct etnaviv_gpu *gpu,
	struct iommu_domain *domain, enum etnaviv_iommu_version version);

#endif /* __ETNAVIV_MMU_H__ */
