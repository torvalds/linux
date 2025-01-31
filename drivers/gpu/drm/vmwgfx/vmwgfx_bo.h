/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/**************************************************************************
 *
 * Copyright (c) 2023-2024 Broadcom. All Rights Reserved. The term
 * “Broadcom” refers to Broadcom Inc. and/or its subsidiaries.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/

#ifndef VMWGFX_BO_H
#define VMWGFX_BO_H

#include "device_include/svga_reg.h"

#include <drm/ttm/ttm_bo.h>
#include <drm/ttm/ttm_placement.h>

#include <linux/rbtree_types.h>
#include <linux/types.h>
#include <linux/xarray.h>

struct vmw_bo_dirty;
struct vmw_fence_obj;
struct vmw_private;
struct vmw_resource;
struct vmw_surface;

enum vmw_bo_domain {
	VMW_BO_DOMAIN_SYS           = BIT(0),
	VMW_BO_DOMAIN_WAITABLE_SYS  = BIT(1),
	VMW_BO_DOMAIN_VRAM          = BIT(2),
	VMW_BO_DOMAIN_GMR           = BIT(3),
	VMW_BO_DOMAIN_MOB           = BIT(4),
};

struct vmw_bo_params {
	u32 domain;
	u32 busy_domain;
	enum ttm_bo_type bo_type;
	bool pin;
	bool keep_resv;
	size_t size;
	struct dma_resv *resv;
	struct sg_table *sg;
};

/**
 * struct vmw_bo - TTM buffer object with vmwgfx additions
 * @tbo: The TTM buffer object
 * @placement: The preferred placement for this buffer object
 * @places: The chosen places for the preferred placement.
 * @busy_places: Chosen busy places for the preferred placement
 * @map: Kmap object for semi-persistent mappings
 * @res_tree: RB tree of resources using this buffer object as a backing MOB
 * @res_prios: Eviction priority counts for attached resources
 * @map_count: The number of currently active maps. Will differ from the
 * cpu_writers because it includes kernel maps.
 * @cpu_writers: Number of synccpu write grabs. Protected by reservation when
 * increased. May be decreased without reservation.
 * @dx_query_ctx: DX context if this buffer object is used as a DX query MOB
 * @dirty: structure for user-space dirty-tracking
 */
struct vmw_bo {
	struct ttm_buffer_object tbo;

	struct ttm_placement placement;
	struct ttm_place places[5];

	/* Protected by reservation */
	struct ttm_bo_kmap_obj map;

	struct rb_root res_tree;
	u32 res_prios[TTM_MAX_BO_PRIORITY];
	struct xarray detached_resources;

	atomic_t map_count;
	atomic_t cpu_writers;
	/* Not ref-counted.  Protected by binding_mutex */
	struct vmw_resource *dx_query_ctx;
	struct vmw_bo_dirty *dirty;

	bool is_dumb;
	struct vmw_surface *dumb_surface;
};

void vmw_bo_placement_set(struct vmw_bo *bo, u32 domain, u32 busy_domain);
void vmw_bo_placement_set_default_accelerated(struct vmw_bo *bo);

int vmw_bo_create(struct vmw_private *dev_priv,
		  struct vmw_bo_params *params,
		  struct vmw_bo **p_bo);

int vmw_bo_unref_ioctl(struct drm_device *dev, void *data,
		       struct drm_file *file_priv);

int vmw_bo_pin_in_vram(struct vmw_private *dev_priv,
		       struct vmw_bo *buf,
		       bool interruptible);
int vmw_bo_pin_in_vram_or_gmr(struct vmw_private *dev_priv,
			      struct vmw_bo *buf,
			      bool interruptible);
int vmw_bo_pin_in_start_of_vram(struct vmw_private *vmw_priv,
				struct vmw_bo *bo,
				bool interruptible);
void vmw_bo_pin_reserved(struct vmw_bo *bo, bool pin);
int vmw_bo_unpin(struct vmw_private *vmw_priv,
		 struct vmw_bo *bo,
		 bool interruptible);

void vmw_bo_get_guest_ptr(const struct ttm_buffer_object *buf,
			  SVGAGuestPtr *ptr);
int vmw_user_bo_synccpu_ioctl(struct drm_device *dev, void *data,
			      struct drm_file *file_priv);
void vmw_bo_fence_single(struct ttm_buffer_object *bo,
			 struct vmw_fence_obj *fence);

void *vmw_bo_map_and_cache(struct vmw_bo *vbo);
void *vmw_bo_map_and_cache_size(struct vmw_bo *vbo, size_t size);
void vmw_bo_unmap(struct vmw_bo *vbo);

void vmw_bo_move_notify(struct ttm_buffer_object *bo,
			struct ttm_resource *mem);
void vmw_bo_swap_notify(struct ttm_buffer_object *bo);

int vmw_bo_add_detached_resource(struct vmw_bo *vbo, struct vmw_resource *res);
void vmw_bo_del_detached_resource(struct vmw_bo *vbo, struct vmw_resource *res);
struct vmw_surface *vmw_bo_surface(struct vmw_bo *vbo);

int vmw_user_bo_lookup(struct drm_file *filp,
		       u32 handle,
		       struct vmw_bo **out);

/**
 * vmw_bo_adjust_prio - Adjust the buffer object eviction priority
 * according to attached resources
 * @vbo: The struct vmw_bo
 */
static inline void vmw_bo_prio_adjust(struct vmw_bo *vbo)
{
	int i = ARRAY_SIZE(vbo->res_prios);

	while (i--) {
		if (vbo->res_prios[i]) {
			vbo->tbo.priority = i;
			return;
		}
	}

	vbo->tbo.priority = 3;
}

/**
 * vmw_bo_prio_add - Notify a buffer object of a newly attached resource
 * eviction priority
 * @vbo: The struct vmw_bo
 * @prio: The resource priority
 *
 * After being notified, the code assigns the highest resource eviction priority
 * to the backing buffer object (mob).
 */
static inline void vmw_bo_prio_add(struct vmw_bo *vbo, int prio)
{
	if (vbo->res_prios[prio]++ == 0)
		vmw_bo_prio_adjust(vbo);
}

/**
 * vmw_bo_used_prio_del - Notify a buffer object of a resource with a certain
 * priority being removed
 * @vbo: The struct vmw_bo
 * @prio: The resource priority
 *
 * After being notified, the code assigns the highest resource eviction priority
 * to the backing buffer object (mob).
 */
static inline void vmw_bo_prio_del(struct vmw_bo *vbo, int prio)
{
	if (--vbo->res_prios[prio] == 0)
		vmw_bo_prio_adjust(vbo);
}

static inline void vmw_bo_unreference(struct vmw_bo **buf)
{
	struct vmw_bo *tmp_buf = *buf;

	*buf = NULL;
	if (tmp_buf)
		drm_gem_object_put(&tmp_buf->tbo.base);
}

static inline struct vmw_bo *vmw_bo_reference(struct vmw_bo *buf)
{
	drm_gem_object_get(&buf->tbo.base);
	return buf;
}

static inline struct vmw_bo *vmw_user_bo_ref(struct vmw_bo *vbo)
{
	drm_gem_object_get(&vbo->tbo.base);
	return vbo;
}

static inline void vmw_user_bo_unref(struct vmw_bo **buf)
{
	struct vmw_bo *tmp_buf = *buf;

	*buf = NULL;
	if (tmp_buf)
		drm_gem_object_put(&tmp_buf->tbo.base);
}

static inline struct vmw_bo *to_vmw_bo(struct drm_gem_object *gobj)
{
	return container_of((gobj), struct vmw_bo, tbo.base);
}

s32 vmw_bo_mobid(struct vmw_bo *vbo);

#endif // VMWGFX_BO_H
