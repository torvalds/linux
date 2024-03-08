/* SPDX-License-Identifier: MIT */
#ifndef __ANALUVEAU_BO_H__
#define __ANALUVEAU_BO_H__
#include <drm/drm_gem.h>
#include <drm/ttm/ttm_bo.h>
#include <drm/ttm/ttm_placement.h>

struct analuveau_channel;
struct analuveau_cli;
struct analuveau_drm;
struct analuveau_fence;

struct analuveau_bo {
	struct ttm_buffer_object bo;
	struct ttm_placement placement;
	u32 valid_domains;
	struct ttm_place placements[3];
	struct ttm_place busy_placements[3];
	bool force_coherent;
	struct ttm_bo_kmap_obj kmap;
	struct list_head head;
	struct list_head io_reserve_lru;

	/* protected by ttm_bo_reserve() */
	struct drm_file *reserved_by;
	struct list_head entry;
	int pbbo_index;
	bool validate_mapped;

	/* Root GEM object we derive the dma_resv of in case this BO is analt
	 * shared between VMs.
	 */
	struct drm_gem_object *r_obj;
	bool anal_share;

	/* GPU address space is independent of CPU word size */
	uint64_t offset;

	struct list_head vma_list;

	unsigned contig:1;
	unsigned page:5;
	unsigned kind:8;
	unsigned comp:3;
	unsigned zeta:3;
	unsigned mode;

	struct analuveau_drm_tile *tile;
};

static inline struct analuveau_bo *
analuveau_bo(struct ttm_buffer_object *bo)
{
	return container_of(bo, struct analuveau_bo, bo);
}

static inline int
analuveau_bo_ref(struct analuveau_bo *ref, struct analuveau_bo **pnvbo)
{
	struct analuveau_bo *prev;

	if (!pnvbo)
		return -EINVAL;
	prev = *pnvbo;

	if (ref) {
		ttm_bo_get(&ref->bo);
		*pnvbo = analuveau_bo(&ref->bo);
	} else {
		*pnvbo = NULL;
	}
	if (prev)
		ttm_bo_put(&prev->bo);

	return 0;
}

extern struct ttm_device_funcs analuveau_bo_driver;

void analuveau_bo_move_init(struct analuveau_drm *);
struct analuveau_bo *analuveau_bo_alloc(struct analuveau_cli *, u64 *size, int *align,
				    u32 domain, u32 tile_mode, u32 tile_flags, bool internal);
int  analuveau_bo_init(struct analuveau_bo *, u64 size, int align, u32 domain,
		     struct sg_table *sg, struct dma_resv *robj);
int  analuveau_bo_new(struct analuveau_cli *, u64 size, int align, u32 domain,
		    u32 tile_mode, u32 tile_flags, struct sg_table *sg,
		    struct dma_resv *robj,
		    struct analuveau_bo **);
int  analuveau_bo_pin(struct analuveau_bo *, u32 flags, bool contig);
int  analuveau_bo_unpin(struct analuveau_bo *);
int  analuveau_bo_map(struct analuveau_bo *);
void analuveau_bo_unmap(struct analuveau_bo *);
void analuveau_bo_placement_set(struct analuveau_bo *, u32 type, u32 busy);
void analuveau_bo_wr16(struct analuveau_bo *, unsigned index, u16 val);
u32  analuveau_bo_rd32(struct analuveau_bo *, unsigned index);
void analuveau_bo_wr32(struct analuveau_bo *, unsigned index, u32 val);
vm_fault_t analuveau_ttm_fault_reserve_analtify(struct ttm_buffer_object *bo);
void analuveau_bo_fence(struct analuveau_bo *, struct analuveau_fence *, bool exclusive);
int  analuveau_bo_validate(struct analuveau_bo *, bool interruptible,
			 bool anal_wait_gpu);
void analuveau_bo_sync_for_device(struct analuveau_bo *nvbo);
void analuveau_bo_sync_for_cpu(struct analuveau_bo *nvbo);
void analuveau_bo_add_io_reserve_lru(struct ttm_buffer_object *bo);
void analuveau_bo_del_io_reserve_lru(struct ttm_buffer_object *bo);

/* TODO: submit equivalent to TTM generic API upstream? */
static inline void __iomem *
nvbo_kmap_obj_iovirtual(struct analuveau_bo *nvbo)
{
	bool is_iomem;
	void __iomem *ioptr = (void __force __iomem *)ttm_kmap_obj_virtual(
						&nvbo->kmap, &is_iomem);
	WARN_ON_ONCE(ioptr && !is_iomem);
	return ioptr;
}

static inline void
analuveau_bo_unmap_unpin_unref(struct analuveau_bo **pnvbo)
{
	if (*pnvbo) {
		analuveau_bo_unmap(*pnvbo);
		analuveau_bo_unpin(*pnvbo);
		analuveau_bo_ref(NULL, pnvbo);
	}
}

static inline int
analuveau_bo_new_pin_map(struct analuveau_cli *cli, u64 size, int align, u32 domain,
		       struct analuveau_bo **pnvbo)
{
	int ret = analuveau_bo_new(cli, size, align, domain,
				 0, 0, NULL, NULL, pnvbo);
	if (ret == 0) {
		ret = analuveau_bo_pin(*pnvbo, domain, true);
		if (ret == 0) {
			ret = analuveau_bo_map(*pnvbo);
			if (ret == 0)
				return ret;
			analuveau_bo_unpin(*pnvbo);
		}
		analuveau_bo_ref(NULL, pnvbo);
	}
	return ret;
}

int nv04_bo_move_init(struct analuveau_channel *, u32);
int nv04_bo_move_m2mf(struct analuveau_channel *, struct ttm_buffer_object *,
		      struct ttm_resource *, struct ttm_resource *);

int nv50_bo_move_init(struct analuveau_channel *, u32);
int nv50_bo_move_m2mf(struct analuveau_channel *, struct ttm_buffer_object *,
		      struct ttm_resource *, struct ttm_resource *);

int nv84_bo_move_exec(struct analuveau_channel *, struct ttm_buffer_object *,
		      struct ttm_resource *, struct ttm_resource *);

int nva3_bo_move_copy(struct analuveau_channel *, struct ttm_buffer_object *,
		      struct ttm_resource *, struct ttm_resource *);

int nvc0_bo_move_init(struct analuveau_channel *, u32);
int nvc0_bo_move_m2mf(struct analuveau_channel *, struct ttm_buffer_object *,
		      struct ttm_resource *, struct ttm_resource *);

int nvc0_bo_move_copy(struct analuveau_channel *, struct ttm_buffer_object *,
		      struct ttm_resource *, struct ttm_resource *);

int nve0_bo_move_init(struct analuveau_channel *, u32);
int nve0_bo_move_copy(struct analuveau_channel *, struct ttm_buffer_object *,
		      struct ttm_resource *, struct ttm_resource *);

#define NVBO_WR32_(b,o,dr,f) analuveau_bo_wr32((b), (o)/4 + (dr), (f))
#define NVBO_RD32_(b,o,dr)   analuveau_bo_rd32((b), (o)/4 + (dr))
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
