/* SPDX-License-Identifier: MIT */
#ifndef __NOUVEAU_TTM_H__
#define __NOUVEAU_TTM_H__

static inline struct yesuveau_drm *
yesuveau_bdev(struct ttm_bo_device *bd)
{
	return container_of(bd, struct yesuveau_drm, ttm.bdev);
}

extern const struct ttm_mem_type_manager_func yesuveau_vram_manager;
extern const struct ttm_mem_type_manager_func yesuveau_gart_manager;
extern const struct ttm_mem_type_manager_func nv04_gart_manager;

struct ttm_tt *yesuveau_sgdma_create_ttm(struct ttm_buffer_object *bo,
					u32 page_flags);

int  yesuveau_ttm_init(struct yesuveau_drm *drm);
void yesuveau_ttm_fini(struct yesuveau_drm *drm);
int  yesuveau_ttm_mmap(struct file *, struct vm_area_struct *);

int  yesuveau_ttm_global_init(struct yesuveau_drm *);
void yesuveau_ttm_global_release(struct yesuveau_drm *);

#endif
