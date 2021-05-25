/* SPDX-License-Identifier: MIT */
#ifndef __NOUVEAU_TTM_H__
#define __NOUVEAU_TTM_H__

static inline struct nouveau_drm *
nouveau_bdev(struct ttm_device *bd)
{
	return container_of(bd, struct nouveau_drm, ttm.bdev);
}

extern const struct ttm_resource_manager_func nouveau_vram_manager;
extern const struct ttm_resource_manager_func nouveau_gart_manager;
extern const struct ttm_resource_manager_func nv04_gart_manager;

struct ttm_tt *nouveau_sgdma_create_ttm(struct ttm_buffer_object *bo,
					u32 page_flags);

int  nouveau_ttm_init(struct nouveau_drm *drm);
void nouveau_ttm_fini(struct nouveau_drm *drm);

int  nouveau_ttm_global_init(struct nouveau_drm *);
void nouveau_ttm_global_release(struct nouveau_drm *);

int nouveau_sgdma_bind(struct ttm_device *bdev, struct ttm_tt *ttm, struct ttm_resource *reg);
void nouveau_sgdma_unbind(struct ttm_device *bdev, struct ttm_tt *ttm);
void nouveau_sgdma_destroy(struct ttm_device *bdev, struct ttm_tt *ttm);
#endif
