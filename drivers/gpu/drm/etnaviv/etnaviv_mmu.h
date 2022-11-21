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
struct etnaviv_iommu_global;
struct etnaviv_iommu_context;

struct etnaviv_iommu_ops {
	struct etnaviv_iommu_context *(*init)(struct etnaviv_iommu_global *);
	void (*free)(struct etnaviv_iommu_context *);
	int (*map)(struct etnaviv_iommu_context *context, unsigned long iova,
		   phys_addr_t paddr, size_t size, int prot);
	size_t (*unmap)(struct etnaviv_iommu_context *context, unsigned long iova,
			size_t size);
	size_t (*dump_size)(struct etnaviv_iommu_context *);
	void (*dump)(struct etnaviv_iommu_context *, void *);
	void (*restore)(struct etnaviv_gpu *, struct etnaviv_iommu_context *);
};

extern const struct etnaviv_iommu_ops etnaviv_iommuv1_ops;
extern const struct etnaviv_iommu_ops etnaviv_iommuv2_ops;

#define ETNAVIV_PTA_SIZE	SZ_4K
#define ETNAVIV_PTA_ENTRIES	(ETNAVIV_PTA_SIZE / sizeof(u64))

struct etnaviv_iommu_global {
	struct device *dev;
	enum etnaviv_iommu_version version;
	const struct etnaviv_iommu_ops *ops;
	unsigned int use;
	struct mutex lock;

	void *bad_page_cpu;
	dma_addr_t bad_page_dma;

	u32 memory_base;

	/*
	 * This union holds members needed by either MMUv1 or MMUv2, which
	 * can not exist at the same time.
	 */
	union {
		struct {
			struct etnaviv_iommu_context *shared_context;
		} v1;
		struct {
			/* P(age) T(able) A(rray) */
			u64 *pta_cpu;
			dma_addr_t pta_dma;
			struct spinlock pta_lock;
			DECLARE_BITMAP(pta_alloc, ETNAVIV_PTA_ENTRIES);
		} v2;
	};
};

struct etnaviv_iommu_context {
	struct kref refcount;
	struct etnaviv_iommu_global *global;

	/* memory manager for GPU address area */
	struct mutex lock;
	struct list_head mappings;
	struct drm_mm mm;
	unsigned int flush_seq;

	/* Not part of the context, but needs to have the same lifetime */
	struct etnaviv_vram_mapping cmdbuf_mapping;
};

int etnaviv_iommu_global_init(struct etnaviv_gpu *gpu);
void etnaviv_iommu_global_fini(struct etnaviv_gpu *gpu);

struct etnaviv_gem_object;

int etnaviv_iommu_map_gem(struct etnaviv_iommu_context *context,
	struct etnaviv_gem_object *etnaviv_obj, u32 memory_base,
	struct etnaviv_vram_mapping *mapping, u64 va);
void etnaviv_iommu_unmap_gem(struct etnaviv_iommu_context *context,
	struct etnaviv_vram_mapping *mapping);

int etnaviv_iommu_get_suballoc_va(struct etnaviv_iommu_context *ctx,
				  struct etnaviv_vram_mapping *mapping,
				  u32 memory_base, dma_addr_t paddr,
				  size_t size);
void etnaviv_iommu_put_suballoc_va(struct etnaviv_iommu_context *ctx,
				   struct etnaviv_vram_mapping *mapping);

size_t etnaviv_iommu_dump_size(struct etnaviv_iommu_context *ctx);
void etnaviv_iommu_dump(struct etnaviv_iommu_context *ctx, void *buf);

struct etnaviv_iommu_context *
etnaviv_iommu_context_init(struct etnaviv_iommu_global *global,
			   struct etnaviv_cmdbuf_suballoc *suballoc);
static inline struct etnaviv_iommu_context *
etnaviv_iommu_context_get(struct etnaviv_iommu_context *ctx)
{
	kref_get(&ctx->refcount);
	return ctx;
}
void etnaviv_iommu_context_put(struct etnaviv_iommu_context *ctx);
void etnaviv_iommu_restore(struct etnaviv_gpu *gpu,
			   struct etnaviv_iommu_context *ctx);

struct etnaviv_iommu_context *
etnaviv_iommuv1_context_alloc(struct etnaviv_iommu_global *global);
struct etnaviv_iommu_context *
etnaviv_iommuv2_context_alloc(struct etnaviv_iommu_global *global);

u32 etnaviv_iommuv2_get_mtlb_addr(struct etnaviv_iommu_context *context);
unsigned short etnaviv_iommuv2_get_pta_id(struct etnaviv_iommu_context *context);

#endif /* __ETNAVIV_MMU_H__ */
