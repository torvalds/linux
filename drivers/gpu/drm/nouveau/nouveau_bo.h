/* SPDX-License-Identifier: MIT */
#ifndef __NOUVEAU_BO_H__
#define __NOUVEAU_BO_H__

#include <drm/drm_gem.h>

struct yesuveau_channel;
struct yesuveau_fence;
struct nvkm_vma;

struct yesuveau_bo {
	struct ttm_buffer_object bo;
	struct ttm_placement placement;
	u32 valid_domains;
	struct ttm_place placements[3];
	struct ttm_place busy_placements[3];
	bool force_coherent;
	struct ttm_bo_kmap_obj kmap;
	struct list_head head;

	/* protected by ttm_bo_reserve() */
	struct drm_file *reserved_by;
	struct list_head entry;
	int pbbo_index;
	bool validate_mapped;

	struct list_head vma_list;

	unsigned contig:1;
	unsigned page:5;
	unsigned kind:8;
	unsigned comp:3;
	unsigned zeta:3;
	unsigned mode;

	struct yesuveau_drm_tile *tile;

	/* protect by the ttm reservation lock */
	int pin_refcnt;

	struct ttm_bo_kmap_obj dma_buf_vmap;
};

static inline struct yesuveau_bo *
yesuveau_bo(struct ttm_buffer_object *bo)
{
	return container_of(bo, struct yesuveau_bo, bo);
}

static inline int
yesuveau_bo_ref(struct yesuveau_bo *ref, struct yesuveau_bo **pnvbo)
{
	struct yesuveau_bo *prev;

	if (!pnvbo)
		return -EINVAL;
	prev = *pnvbo;

	if (ref) {
		ttm_bo_get(&ref->bo);
		*pnvbo = yesuveau_bo(&ref->bo);
	} else {
		*pnvbo = NULL;
	}
	if (prev)
		ttm_bo_put(&prev->bo);

	return 0;
}

extern struct ttm_bo_driver yesuveau_bo_driver;

void yesuveau_bo_move_init(struct yesuveau_drm *);
struct yesuveau_bo *yesuveau_bo_alloc(struct yesuveau_cli *, u64 *size, int *align,
				    u32 flags, u32 tile_mode, u32 tile_flags);
int  yesuveau_bo_init(struct yesuveau_bo *, u64 size, int align, u32 flags,
		     struct sg_table *sg, struct dma_resv *robj);
int  yesuveau_bo_new(struct yesuveau_cli *, u64 size, int align, u32 flags,
		    u32 tile_mode, u32 tile_flags, struct sg_table *sg,
		    struct dma_resv *robj,
		    struct yesuveau_bo **);
int  yesuveau_bo_pin(struct yesuveau_bo *, u32 flags, bool contig);
int  yesuveau_bo_unpin(struct yesuveau_bo *);
int  yesuveau_bo_map(struct yesuveau_bo *);
void yesuveau_bo_unmap(struct yesuveau_bo *);
void yesuveau_bo_placement_set(struct yesuveau_bo *, u32 type, u32 busy);
void yesuveau_bo_wr16(struct yesuveau_bo *, unsigned index, u16 val);
u32  yesuveau_bo_rd32(struct yesuveau_bo *, unsigned index);
void yesuveau_bo_wr32(struct yesuveau_bo *, unsigned index, u32 val);
void yesuveau_bo_fence(struct yesuveau_bo *, struct yesuveau_fence *, bool exclusive);
int  yesuveau_bo_validate(struct yesuveau_bo *, bool interruptible,
			 bool yes_wait_gpu);
void yesuveau_bo_sync_for_device(struct yesuveau_bo *nvbo);
void yesuveau_bo_sync_for_cpu(struct yesuveau_bo *nvbo);

/* TODO: submit equivalent to TTM generic API upstream? */
static inline void __iomem *
nvbo_kmap_obj_iovirtual(struct yesuveau_bo *nvbo)
{
	bool is_iomem;
	void __iomem *ioptr = (void __force __iomem *)ttm_kmap_obj_virtual(
						&nvbo->kmap, &is_iomem);
	WARN_ON_ONCE(ioptr && !is_iomem);
	return ioptr;
}

static inline void
yesuveau_bo_unmap_unpin_unref(struct yesuveau_bo **pnvbo)
{
	if (*pnvbo) {
		yesuveau_bo_unmap(*pnvbo);
		yesuveau_bo_unpin(*pnvbo);
		yesuveau_bo_ref(NULL, pnvbo);
	}
}

static inline int
yesuveau_bo_new_pin_map(struct yesuveau_cli *cli, u64 size, int align, u32 flags,
		       struct yesuveau_bo **pnvbo)
{
	int ret = yesuveau_bo_new(cli, size, align, flags,
				 0, 0, NULL, NULL, pnvbo);
	if (ret == 0) {
		ret = yesuveau_bo_pin(*pnvbo, flags, true);
		if (ret == 0) {
			ret = yesuveau_bo_map(*pnvbo);
			if (ret == 0)
				return ret;
			yesuveau_bo_unpin(*pnvbo);
		}
		yesuveau_bo_ref(NULL, pnvbo);
	}
	return ret;
}
#endif
