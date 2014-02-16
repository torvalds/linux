/*
 * Copyright (C) 2012 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef ARMADA_GEM_H
#define ARMADA_GEM_H

/* GEM */
struct armada_gem_object {
	struct drm_gem_object	obj;
	void			*addr;
	phys_addr_t		phys_addr;
	resource_size_t		dev_addr;
	struct drm_mm_node	*linear;	/* for linear backed */
	struct page		*page;		/* for page backed */
	struct sg_table		*sgt;		/* for imported */
	void			(*update)(void *);
	void			*update_data;
};

extern const struct vm_operations_struct armada_gem_vm_ops;

#define drm_to_armada_gem(o) container_of(o, struct armada_gem_object, obj)

void armada_gem_free_object(struct drm_gem_object *);
int armada_gem_linear_back(struct drm_device *, struct armada_gem_object *);
void *armada_gem_map_object(struct drm_device *, struct armada_gem_object *);
struct armada_gem_object *armada_gem_alloc_private_object(struct drm_device *,
	size_t);
int armada_gem_dumb_create(struct drm_file *, struct drm_device *,
	struct drm_mode_create_dumb *);
int armada_gem_dumb_map_offset(struct drm_file *, struct drm_device *,
	uint32_t, uint64_t *);
int armada_gem_dumb_destroy(struct drm_file *, struct drm_device *,
	uint32_t);
struct dma_buf *armada_gem_prime_export(struct drm_device *dev,
	struct drm_gem_object *obj, int flags);
struct drm_gem_object *armada_gem_prime_import(struct drm_device *,
	struct dma_buf *);
int armada_gem_map_import(struct armada_gem_object *);

static inline struct armada_gem_object *armada_gem_object_lookup(
	struct drm_device *dev, struct drm_file *dfile, unsigned handle)
{
	struct drm_gem_object *obj = drm_gem_object_lookup(dev, dfile, handle);

	return obj ? drm_to_armada_gem(obj) : NULL;
}
#endif
