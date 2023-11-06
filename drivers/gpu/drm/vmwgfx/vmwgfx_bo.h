/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/**************************************************************************
 *
 * Copyright 2023 VMware, Inc., Palo Alto, CA., USA
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

struct vmw_bo_dirty;
struct vmw_fence_obj;
struct vmw_private;
struct vmw_resource;

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
	size_t size;
	bool pin;
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
 * @cpu_writers: Number of synccpu write grabs. Protected by reservation when
 * increased. May be decreased without reservation.
 * @dx_query_ctx: DX context if this buffer object is used as a DX query MOB
 * @dirty: structure for user-space dirty-tracking
 */
struct vmw_bo {
	struct ttm_buffer_object tbo;

	struct ttm_placement placement;
	struct ttm_place places[5];
	struct ttm_place busy_places[5];

	/* Protected by reservation */
	struct ttm_bo_kmap_obj map;

	struct rb_root res_tree;
	u32 res_prios[TTM_MAX_BO_PRIORITY];

	atomic_t cpu_writers;
	/* Not ref-counted.  Protected by binding_mutex */
	struct vmw_resource *dx_query_ctx;
	struct vmw_bo_dirty *dirty;
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
void vmw_bo_unmap(struct vmw_bo *vbo);

void vmw_bo_move_notify(struct ttm_buffer_object *bo,
			struct ttm_resource *mem);
void vmw_bo_swap_notify(struct ttm_buffer_object *bo);

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
		ttm_bo_put(&tmp_buf->tbo);
}

static inline struct vmw_bo *vmw_bo_reference(struct vmw_bo *buf)
{
	ttm_bo_get(&buf->tbo);
	return buf;
}

static inline void vmw_user_bo_unref(struct vmw_bo *vbo)
{
	if (vbo) {
		ttm_bo_put(&vbo->tbo);
		drm_gem_object_put(&vbo->tbo.base);
	}
}

static inline struct vmw_bo *to_vmw_bo(struct drm_gem_object *gobj)
{
	return container_of((gobj), struct vmw_bo, tbo.base);
}

#endif // VMWGFX_BO_H
