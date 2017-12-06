/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NOUVEAU_GEM_H__
#define __NOUVEAU_GEM_H__

#include <drm/drmP.h>

#include "nouveau_drv.h"
#include "nouveau_bo.h"

static inline struct nouveau_bo *
nouveau_gem_object(struct drm_gem_object *gem)
{
	return gem ? container_of(gem, struct nouveau_bo, gem) : NULL;
}

/* nouveau_gem.c */
extern int nouveau_gem_new(struct nouveau_cli *, u64 size, int align,
			   uint32_t domain, uint32_t tile_mode,
			   uint32_t tile_flags, struct nouveau_bo **);
extern void nouveau_gem_object_del(struct drm_gem_object *);
extern int nouveau_gem_object_open(struct drm_gem_object *, struct drm_file *);
extern void nouveau_gem_object_close(struct drm_gem_object *,
				     struct drm_file *);
extern int nouveau_gem_ioctl_new(struct drm_device *, void *,
				 struct drm_file *);
extern int nouveau_gem_ioctl_pushbuf(struct drm_device *, void *,
				     struct drm_file *);
extern int nouveau_gem_ioctl_cpu_prep(struct drm_device *, void *,
				      struct drm_file *);
extern int nouveau_gem_ioctl_cpu_fini(struct drm_device *, void *,
				      struct drm_file *);
extern int nouveau_gem_ioctl_info(struct drm_device *, void *,
				  struct drm_file *);

extern int nouveau_gem_prime_pin(struct drm_gem_object *);
struct reservation_object *nouveau_gem_prime_res_obj(struct drm_gem_object *);
extern void nouveau_gem_prime_unpin(struct drm_gem_object *);
extern struct sg_table *nouveau_gem_prime_get_sg_table(struct drm_gem_object *);
extern struct drm_gem_object *nouveau_gem_prime_import_sg_table(
	struct drm_device *, struct dma_buf_attachment *, struct sg_table *);
extern void *nouveau_gem_prime_vmap(struct drm_gem_object *);
extern void nouveau_gem_prime_vunmap(struct drm_gem_object *, void *);

#endif
