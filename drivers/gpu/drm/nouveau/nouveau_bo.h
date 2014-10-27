#ifndef __NOUVEAU_BO_H__
#define __NOUVEAU_BO_H__

#include <drm/drm_gem.h>

struct nouveau_channel;
struct nouveau_fence;
struct nouveau_vma;

struct nouveau_bo {
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
	unsigned page_shift;

	u32 tile_mode;
	u32 tile_flags;
	struct nouveau_drm_tile *tile;

	/* Only valid if allocated via nouveau_gem_new() and iff you hold a
	 * gem reference to it! For debugging, use gem.filp != NULL to test
	 * whether it is valid. */
	struct drm_gem_object gem;

	/* protect by the ttm reservation lock */
	int pin_refcnt;

	struct ttm_bo_kmap_obj dma_buf_vmap;
};

static inline struct nouveau_bo *
nouveau_bo(struct ttm_buffer_object *bo)
{
	return container_of(bo, struct nouveau_bo, bo);
}

static inline int
nouveau_bo_ref(struct nouveau_bo *ref, struct nouveau_bo **pnvbo)
{
	struct nouveau_bo *prev;

	if (!pnvbo)
		return -EINVAL;
	prev = *pnvbo;

	*pnvbo = ref ? nouveau_bo(ttm_bo_reference(&ref->bo)) : NULL;
	if (prev) {
		struct ttm_buffer_object *bo = &prev->bo;

		ttm_bo_unref(&bo);
	}

	return 0;
}

extern struct ttm_bo_driver nouveau_bo_driver;

void nouveau_bo_move_init(struct nouveau_drm *);
int  nouveau_bo_new(struct drm_device *, int size, int align, u32 flags,
		    u32 tile_mode, u32 tile_flags, struct sg_table *sg,
		    struct reservation_object *robj,
		    struct nouveau_bo **);
int  nouveau_bo_pin(struct nouveau_bo *, u32 flags);
int  nouveau_bo_unpin(struct nouveau_bo *);
int  nouveau_bo_map(struct nouveau_bo *);
void nouveau_bo_unmap(struct nouveau_bo *);
void nouveau_bo_placement_set(struct nouveau_bo *, u32 type, u32 busy);
u16  nouveau_bo_rd16(struct nouveau_bo *, unsigned index);
void nouveau_bo_wr16(struct nouveau_bo *, unsigned index, u16 val);
u32  nouveau_bo_rd32(struct nouveau_bo *, unsigned index);
void nouveau_bo_wr32(struct nouveau_bo *, unsigned index, u32 val);
void nouveau_bo_fence(struct nouveau_bo *, struct nouveau_fence *, bool exclusive);
int  nouveau_bo_validate(struct nouveau_bo *, bool interruptible,
			 bool no_wait_gpu);
void nouveau_bo_sync_for_device(struct nouveau_bo *nvbo);
void nouveau_bo_sync_for_cpu(struct nouveau_bo *nvbo);

struct nouveau_vma *
nouveau_bo_vma_find(struct nouveau_bo *, struct nouveau_vm *);

int  nouveau_bo_vma_add(struct nouveau_bo *, struct nouveau_vm *,
			struct nouveau_vma *);
void nouveau_bo_vma_del(struct nouveau_bo *, struct nouveau_vma *);

/* TODO: submit equivalent to TTM generic API upstream? */
static inline void __iomem *
nvbo_kmap_obj_iovirtual(struct nouveau_bo *nvbo)
{
	bool is_iomem;
	void __iomem *ioptr = (void __force __iomem *)ttm_kmap_obj_virtual(
						&nvbo->kmap, &is_iomem);
	WARN_ON_ONCE(ioptr && !is_iomem);
	return ioptr;
}

#endif
