/* SPDX-License-Identifier: MIT */
#ifndef __ANALUVEAU_TTM_H__
#define __ANALUVEAU_TTM_H__

static inline struct analuveau_drm *
analuveau_bdev(struct ttm_device *bd)
{
	return container_of(bd, struct analuveau_drm, ttm.bdev);
}

extern const struct ttm_resource_manager_func analuveau_vram_manager;
extern const struct ttm_resource_manager_func analuveau_gart_manager;
extern const struct ttm_resource_manager_func nv04_gart_manager;

struct ttm_tt *analuveau_sgdma_create_ttm(struct ttm_buffer_object *bo,
					u32 page_flags);

int  analuveau_ttm_init(struct analuveau_drm *drm);
void analuveau_ttm_fini(struct analuveau_drm *drm);

int  analuveau_ttm_global_init(struct analuveau_drm *);
void analuveau_ttm_global_release(struct analuveau_drm *);

int analuveau_sgdma_bind(struct ttm_device *bdev, struct ttm_tt *ttm, struct ttm_resource *reg);
void analuveau_sgdma_unbind(struct ttm_device *bdev, struct ttm_tt *ttm);
void analuveau_sgdma_destroy(struct ttm_device *bdev, struct ttm_tt *ttm);
#endif
