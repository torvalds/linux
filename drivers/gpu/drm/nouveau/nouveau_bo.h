/* SPDX-License-Identifier: MIT */
#ifndef __NOUVEAU_BO_H__
#define __NOUVEAU_BO_H__
#include <drm/drm_gem.h>
#include <drm/ttm/ttm_bo.h>
#include <drm/ttm/ttm_placement.h>

struct nouveau_channel;
struct nouveau_cli;
struct nouveau_drm;
struct nouveau_fence;
struct nouveau_vma;

struct nouveau_bo {
	struct ttm_buffer_object bo;
	struct ttm_placement placement;
	u32 valid_domains;
	struct ttm_place placements[3];
	bool force_coherent;
	struct ttm_bo_kmap_obj kmap;
	struct list_head head;
	struct list_head io_reserve_lru;

	/* protected by ttm_bo_reserve() */
	struct drm_file *reserved_by;
	struct list_head entry;
	int pbbo_index;
	bool validate_mapped;

	/* Root GEM object we derive the dma_resv of in case this BO is not
	 * shared between VMs.
	 */
	struct drm_gem_object *r_obj;
	bool no_share;

	/* GPU address space is independent of CPU word size */
	uint64_t offset;

	struct list_head vma_list;

	unsigned contig:1;
	unsigned page:5;
	unsigned kind:8;
	unsigned comp:3;
	unsigned zeta:3;
	unsigned mode;

	struct nouveau_drm_tile *tile;
};

static inline struct nouveau_bo *
nouveau_bo(struct ttm_buffer_object *bo)
{
	return container_of(bo, struct nouveau_bo, bo);
}

static inline void
nouveau_bo_fini(struct nouveau_bo *bo)
{
	ttm_bo_put(&bo->bo);
}

extern struct ttm_device_funcs nouveau_bo_driver;

void nouveau_bo_move_init(struct nouveau_drm *);
struct nouveau_bo *nouveau_bo_alloc(struct nouveau_cli *, u64 *size, int *align,
				    u32 domain, u32 tile_mode, u32 tile_flags, bool internal);
int  nouveau_bo_init(struct nouveau_bo *, u64 size, int align, u32 domain,
		     struct sg_table *sg, struct dma_resv *robj);
int  nouveau_bo_new(struct nouveau_cli *, u64 size, int align, u32 domain,
		    u32 tile_mode, u32 tile_flags, struct sg_table *sg,
		    struct dma_resv *robj,
		    struct nouveau_bo **);
int  nouveau_bo_pin_locked(struct nouveau_bo *nvbo, uint32_t domain, bool contig);
void nouveau_bo_unpin_locked(struct nouveau_bo *nvbo);
int  nouveau_bo_pin(struct nouveau_bo *, u32 flags, bool contig);
int  nouveau_bo_unpin(struct nouveau_bo *);
int  nouveau_bo_map(struct nouveau_bo *);
void nouveau_bo_unmap(struct nouveau_bo *);
void nouveau_bo_placement_set(struct nouveau_bo *, u32 type, u32 busy);
void nouveau_bo_wr16(struct nouveau_bo *, unsigned index, u16 val);
u32  nouveau_bo_rd32(struct nouveau_bo *, unsigned index);
void nouveau_bo_wr32(struct nouveau_bo *, unsigned index, u32 val);
vm_fault_t nouveau_ttm_fault_reserve_notify(struct ttm_buffer_object *bo);
void nouveau_bo_fence(struct nouveau_bo *, struct nouveau_fence *, bool exclusive);
int  nouveau_bo_validate(struct nouveau_bo *, bool interruptible,
			 bool no_wait_gpu);
void nouveau_bo_sync_for_device(struct nouveau_bo *nvbo);
void nouveau_bo_sync_for_cpu(struct nouveau_bo *nvbo);
void nouveau_bo_add_io_reserve_lru(struct ttm_buffer_object *bo);
void nouveau_bo_del_io_reserve_lru(struct ttm_buffer_object *bo);

int nouveau_bo_new_pin(struct nouveau_cli *, u32 domain, u32 size, struct nouveau_bo **);
int nouveau_bo_new_map(struct nouveau_cli *, u32 domain, u32 size, struct nouveau_bo **);
int nouveau_bo_new_map_gpu(struct nouveau_cli *, u32 domain, u32 size,
			   struct nouveau_bo **, struct nouveau_vma **);
void nouveau_bo_unpin_del(struct nouveau_bo **);

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

int nv04_bo_move_init(struct nouveau_channel *, u32);
int nv04_bo_move_m2mf(struct nouveau_channel *, struct ttm_buffer_object *,
		      struct ttm_resource *, struct ttm_resource *);

int nv50_bo_move_init(struct nouveau_channel *, u32);
int nv50_bo_move_m2mf(struct nouveau_channel *, struct ttm_buffer_object *,
		      struct ttm_resource *, struct ttm_resource *);

int nv84_bo_move_exec(struct nouveau_channel *, struct ttm_buffer_object *,
		      struct ttm_resource *, struct ttm_resource *);

int nva3_bo_move_copy(struct nouveau_channel *, struct ttm_buffer_object *,
		      struct ttm_resource *, struct ttm_resource *);

int nvc0_bo_move_init(struct nouveau_channel *, u32);
int nvc0_bo_move_m2mf(struct nouveau_channel *, struct ttm_buffer_object *,
		      struct ttm_resource *, struct ttm_resource *);

int nvc0_bo_move_copy(struct nouveau_channel *, struct ttm_buffer_object *,
		      struct ttm_resource *, struct ttm_resource *);

int nve0_bo_move_init(struct nouveau_channel *, u32);
int nve0_bo_move_copy(struct nouveau_channel *, struct ttm_buffer_object *,
		      struct ttm_resource *, struct ttm_resource *);

#define NVBO_WR32_(b,o,dr,f) nouveau_bo_wr32((b), (o)/4 + (dr), (f))
#define NVBO_RD32_(b,o,dr)   nouveau_bo_rd32((b), (o)/4 + (dr))
#define NVBO_RD32(A...) DRF_RD(NVBO_RD32_,                  ##A)
#define NVBO_RV32(A...) DRF_RV(NVBO_RD32_,                  ##A)
#define NVBO_TV32(A...) DRF_TV(NVBO_RD32_,                  ##A)
#define NVBO_TD32(A...) DRF_TD(NVBO_RD32_,                  ##A)
#define NVBO_WR32(A...) DRF_WR(            NVBO_WR32_,      ##A)
#define NVBO_WV32(A...) DRF_WV(            NVBO_WR32_,      ##A)
#define NVBO_WD32(A...) DRF_WD(            NVBO_WR32_,      ##A)
#define NVBO_MR32(A...) DRF_MR(NVBO_RD32_, NVBO_WR32_, u32, ##A)
#define NVBO_MV32(A...) DRF_MV(NVBO_RD32_, NVBO_WR32_, u32, ##A)
#define NVBO_MD32(A...) DRF_MD(NVBO_RD32_, NVBO_WR32_, u32, ##A)
#endif
