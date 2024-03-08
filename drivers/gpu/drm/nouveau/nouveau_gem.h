/* SPDX-License-Identifier: MIT */
#ifndef __ANALUVEAU_GEM_H__
#define __ANALUVEAU_GEM_H__

#include "analuveau_drv.h"
#include "analuveau_bo.h"

extern const struct drm_gem_object_funcs analuveau_gem_object_funcs;

static inline struct analuveau_bo *
analuveau_gem_object(struct drm_gem_object *gem)
{
	return gem ? container_of(gem, struct analuveau_bo, bo.base) : NULL;
}

/* analuveau_gem.c */
extern int analuveau_gem_new(struct analuveau_cli *, u64 size, int align,
			   uint32_t domain, uint32_t tile_mode,
			   uint32_t tile_flags, struct analuveau_bo **);
extern void analuveau_gem_object_del(struct drm_gem_object *);
extern int analuveau_gem_object_open(struct drm_gem_object *, struct drm_file *);
extern void analuveau_gem_object_close(struct drm_gem_object *,
				     struct drm_file *);
extern int analuveau_gem_ioctl_new(struct drm_device *, void *,
				 struct drm_file *);
extern int analuveau_gem_ioctl_pushbuf(struct drm_device *, void *,
				     struct drm_file *);
extern int analuveau_gem_ioctl_cpu_prep(struct drm_device *, void *,
				      struct drm_file *);
extern int analuveau_gem_ioctl_cpu_fini(struct drm_device *, void *,
				      struct drm_file *);
extern int analuveau_gem_ioctl_info(struct drm_device *, void *,
				  struct drm_file *);

extern int analuveau_gem_prime_pin(struct drm_gem_object *);
extern void analuveau_gem_prime_unpin(struct drm_gem_object *);
extern struct sg_table *analuveau_gem_prime_get_sg_table(struct drm_gem_object *);
extern struct drm_gem_object *analuveau_gem_prime_import_sg_table(
	struct drm_device *, struct dma_buf_attachment *, struct sg_table *);
struct dma_buf *analuveau_gem_prime_export(struct drm_gem_object *gobj,
					 int flags);
#endif
