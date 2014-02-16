#ifndef __NOUVEAU_GEM_H__
#define __NOUVEAU_GEM_H__

#include <drm/drmP.h>

#include "nouveau_drm.h"
#include "nouveau_bo.h"

#define nouveau_bo_tile_layout(nvbo)				\
	((nvbo)->tile_flags & NOUVEAU_GEM_TILE_LAYOUT_MASK)

static inline struct nouveau_bo *
nouveau_gem_object(struct drm_gem_object *gem)
{
	return gem ? container_of(gem, struct nouveau_bo, gem) : NULL;
}

/* nouveau_gem.c */
extern int nouveau_gem_new(struct drm_device *, int size, int align,
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
extern void nouveau_gem_prime_unpin(struct drm_gem_object *);
extern struct sg_table *nouveau_gem_prime_get_sg_table(struct drm_gem_object *);
extern struct drm_gem_object *nouveau_gem_prime_import_sg_table(
	struct drm_device *, size_t size, struct sg_table *);
extern void *nouveau_gem_prime_vmap(struct drm_gem_object *);
extern void nouveau_gem_prime_vunmap(struct drm_gem_object *, void *);

#endif
