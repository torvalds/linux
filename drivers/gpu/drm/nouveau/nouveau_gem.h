/* SPDX-License-Identifier: MIT */
#ifndef __NOUVEAU_GEM_H__
#define __NOUVEAU_GEM_H__

#include "yesuveau_drv.h"
#include "yesuveau_bo.h"

static inline struct yesuveau_bo *
yesuveau_gem_object(struct drm_gem_object *gem)
{
	return gem ? container_of(gem, struct yesuveau_bo, bo.base) : NULL;
}

/* yesuveau_gem.c */
extern int yesuveau_gem_new(struct yesuveau_cli *, u64 size, int align,
			   uint32_t domain, uint32_t tile_mode,
			   uint32_t tile_flags, struct yesuveau_bo **);
extern void yesuveau_gem_object_del(struct drm_gem_object *);
extern int yesuveau_gem_object_open(struct drm_gem_object *, struct drm_file *);
extern void yesuveau_gem_object_close(struct drm_gem_object *,
				     struct drm_file *);
extern int yesuveau_gem_ioctl_new(struct drm_device *, void *,
				 struct drm_file *);
extern int yesuveau_gem_ioctl_pushbuf(struct drm_device *, void *,
				     struct drm_file *);
extern int yesuveau_gem_ioctl_cpu_prep(struct drm_device *, void *,
				      struct drm_file *);
extern int yesuveau_gem_ioctl_cpu_fini(struct drm_device *, void *,
				      struct drm_file *);
extern int yesuveau_gem_ioctl_info(struct drm_device *, void *,
				  struct drm_file *);

extern int yesuveau_gem_prime_pin(struct drm_gem_object *);
extern void yesuveau_gem_prime_unpin(struct drm_gem_object *);
extern struct sg_table *yesuveau_gem_prime_get_sg_table(struct drm_gem_object *);
extern struct drm_gem_object *yesuveau_gem_prime_import_sg_table(
	struct drm_device *, struct dma_buf_attachment *, struct sg_table *);
extern void *yesuveau_gem_prime_vmap(struct drm_gem_object *);
extern void yesuveau_gem_prime_vunmap(struct drm_gem_object *, void *);

#endif
