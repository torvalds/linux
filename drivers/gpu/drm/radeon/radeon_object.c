/*
 * Copyright 2009 Jerome Glisse.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 */
/*
 * Authors:
 *    Jerome Glisse <glisse@freedesktop.org>
 *    Thomas Hellstrom <thomas-at-tungstengraphics-dot-com>
 *    Dave Airlie
 */
#include <linux/list.h>
#include <drm/drmP.h>
#include "radeon_drm.h"
#include "radeon.h"

struct radeon_object {
	struct ttm_buffer_object	tobj;
	struct list_head		list;
	struct radeon_device		*rdev;
	struct drm_gem_object		*gobj;
	struct ttm_bo_kmap_obj		kmap;
	unsigned			pin_count;
	uint64_t			gpu_addr;
	void				*kptr;
	bool				is_iomem;
	uint32_t			tiling_flags;
	uint32_t			pitch;
	int				surface_reg;
};

int radeon_ttm_init(struct radeon_device *rdev);
void radeon_ttm_fini(struct radeon_device *rdev);

/*
 * To exclude mutual BO access we rely on bo_reserve exclusion, as all
 * function are calling it.
 */

static int radeon_object_reserve(struct radeon_object *robj, bool interruptible)
{
	return ttm_bo_reserve(&robj->tobj, interruptible, false, false, 0);
}

static void radeon_object_unreserve(struct radeon_object *robj)
{
	ttm_bo_unreserve(&robj->tobj);
}

static void radeon_ttm_object_object_destroy(struct ttm_buffer_object *tobj)
{
	struct radeon_object *robj;

	robj = container_of(tobj, struct radeon_object, tobj);
	list_del_init(&robj->list);
	radeon_object_clear_surface_reg(robj);
	kfree(robj);
}

static inline void radeon_object_gpu_addr(struct radeon_object *robj)
{
	/* Default gpu address */
	robj->gpu_addr = 0xFFFFFFFFFFFFFFFFULL;
	if (robj->tobj.mem.mm_node == NULL) {
		return;
	}
	robj->gpu_addr = ((u64)robj->tobj.mem.mm_node->start) << PAGE_SHIFT;
	switch (robj->tobj.mem.mem_type) {
	case TTM_PL_VRAM:
		robj->gpu_addr += (u64)robj->rdev->mc.vram_location;
		break;
	case TTM_PL_TT:
		robj->gpu_addr += (u64)robj->rdev->mc.gtt_location;
		break;
	default:
		DRM_ERROR("Unknown placement %d\n", robj->tobj.mem.mem_type);
		robj->gpu_addr = 0xFFFFFFFFFFFFFFFFULL;
		return;
	}
}

static inline uint32_t radeon_object_flags_from_domain(uint32_t domain)
{
	uint32_t flags = 0;
	if (domain & RADEON_GEM_DOMAIN_VRAM) {
		flags |= TTM_PL_FLAG_VRAM | TTM_PL_FLAG_WC | TTM_PL_FLAG_UNCACHED;
	}
	if (domain & RADEON_GEM_DOMAIN_GTT) {
		flags |= TTM_PL_FLAG_TT | TTM_PL_FLAG_WC | TTM_PL_FLAG_UNCACHED;
	}
	if (domain & RADEON_GEM_DOMAIN_CPU) {
		flags |= TTM_PL_FLAG_SYSTEM | TTM_PL_MASK_CACHING;
	}
	if (!flags) {
		flags |= TTM_PL_FLAG_SYSTEM | TTM_PL_MASK_CACHING;
	}
	return flags;
}

int radeon_object_create(struct radeon_device *rdev,
			 struct drm_gem_object *gobj,
			 unsigned long size,
			 bool kernel,
			 uint32_t domain,
			 bool interruptible,
			 struct radeon_object **robj_ptr)
{
	struct radeon_object *robj;
	enum ttm_bo_type type;
	uint32_t flags;
	int r;

	if (unlikely(rdev->mman.bdev.dev_mapping == NULL)) {
		rdev->mman.bdev.dev_mapping = rdev->ddev->dev_mapping;
	}
	if (kernel) {
		type = ttm_bo_type_kernel;
	} else {
		type = ttm_bo_type_device;
	}
	*robj_ptr = NULL;
	robj = kzalloc(sizeof(struct radeon_object), GFP_KERNEL);
	if (robj == NULL) {
		return -ENOMEM;
	}
	robj->rdev = rdev;
	robj->gobj = gobj;
	robj->surface_reg = -1;
	INIT_LIST_HEAD(&robj->list);

	flags = radeon_object_flags_from_domain(domain);
	r = ttm_buffer_object_init(&rdev->mman.bdev, &robj->tobj, size, type, flags,
				   0, 0, false, NULL, size,
				   &radeon_ttm_object_object_destroy);
	if (unlikely(r != 0)) {
		/* ttm call radeon_ttm_object_object_destroy if error happen */
		DRM_ERROR("Failed to allocate TTM object (%ld, 0x%08X, %u)\n",
			  size, flags, 0);
		return r;
	}
	*robj_ptr = robj;
	if (gobj) {
		list_add_tail(&robj->list, &rdev->gem.objects);
	}
	return 0;
}

int radeon_object_kmap(struct radeon_object *robj, void **ptr)
{
	int r;

	spin_lock(&robj->tobj.lock);
	if (robj->kptr) {
		if (ptr) {
			*ptr = robj->kptr;
		}
		spin_unlock(&robj->tobj.lock);
		return 0;
	}
	spin_unlock(&robj->tobj.lock);
	r = ttm_bo_kmap(&robj->tobj, 0, robj->tobj.num_pages, &robj->kmap);
	if (r) {
		return r;
	}
	spin_lock(&robj->tobj.lock);
	robj->kptr = ttm_kmap_obj_virtual(&robj->kmap, &robj->is_iomem);
	spin_unlock(&robj->tobj.lock);
	if (ptr) {
		*ptr = robj->kptr;
	}
	return 0;
}

void radeon_object_kunmap(struct radeon_object *robj)
{
	spin_lock(&robj->tobj.lock);
	if (robj->kptr == NULL) {
		spin_unlock(&robj->tobj.lock);
		return;
	}
	robj->kptr = NULL;
	spin_unlock(&robj->tobj.lock);
	ttm_bo_kunmap(&robj->kmap);
}

void radeon_object_unref(struct radeon_object **robj)
{
	struct ttm_buffer_object *tobj;

	if ((*robj) == NULL) {
		return;
	}
	tobj = &((*robj)->tobj);
	ttm_bo_unref(&tobj);
	if (tobj == NULL) {
		*robj = NULL;
	}
}

int radeon_object_mmap(struct radeon_object *robj, uint64_t *offset)
{
	*offset = robj->tobj.addr_space_offset;
	return 0;
}

int radeon_object_pin(struct radeon_object *robj, uint32_t domain,
		      uint64_t *gpu_addr)
{
	uint32_t flags;
	uint32_t tmp;
	int r;

	flags = radeon_object_flags_from_domain(domain);
	spin_lock(&robj->tobj.lock);
	if (robj->pin_count) {
		robj->pin_count++;
		if (gpu_addr != NULL) {
			*gpu_addr = robj->gpu_addr;
		}
		spin_unlock(&robj->tobj.lock);
		return 0;
	}
	spin_unlock(&robj->tobj.lock);
	r = radeon_object_reserve(robj, false);
	if (unlikely(r != 0)) {
		DRM_ERROR("radeon: failed to reserve object for pinning it.\n");
		return r;
	}
	tmp = robj->tobj.mem.placement;
	ttm_flag_masked(&tmp, flags, TTM_PL_MASK_MEM);
	robj->tobj.proposed_placement = tmp | TTM_PL_FLAG_NO_EVICT | TTM_PL_MASK_CACHING;
	r = ttm_buffer_object_validate(&robj->tobj,
				       robj->tobj.proposed_placement,
				       false, false);
	radeon_object_gpu_addr(robj);
	if (gpu_addr != NULL) {
		*gpu_addr = robj->gpu_addr;
	}
	robj->pin_count = 1;
	if (unlikely(r != 0)) {
		DRM_ERROR("radeon: failed to pin object.\n");
	}
	radeon_object_unreserve(robj);
	return r;
}

void radeon_object_unpin(struct radeon_object *robj)
{
	uint32_t flags;
	int r;

	spin_lock(&robj->tobj.lock);
	if (!robj->pin_count) {
		spin_unlock(&robj->tobj.lock);
		printk(KERN_WARNING "Unpin not necessary for %p !\n", robj);
		return;
	}
	robj->pin_count--;
	if (robj->pin_count) {
		spin_unlock(&robj->tobj.lock);
		return;
	}
	spin_unlock(&robj->tobj.lock);
	r = radeon_object_reserve(robj, false);
	if (unlikely(r != 0)) {
		DRM_ERROR("radeon: failed to reserve object for unpinning it.\n");
		return;
	}
	flags = robj->tobj.mem.placement;
	robj->tobj.proposed_placement = flags & ~TTM_PL_FLAG_NO_EVICT;
	r = ttm_buffer_object_validate(&robj->tobj,
				       robj->tobj.proposed_placement,
				       false, false);
	if (unlikely(r != 0)) {
		DRM_ERROR("radeon: failed to unpin buffer.\n");
	}
	radeon_object_unreserve(robj);
}

int radeon_object_wait(struct radeon_object *robj)
{
	int r = 0;

	/* FIXME: should use block reservation instead */
	r = radeon_object_reserve(robj, true);
	if (unlikely(r != 0)) {
		DRM_ERROR("radeon: failed to reserve object for waiting.\n");
		return r;
	}
	spin_lock(&robj->tobj.lock);
	if (robj->tobj.sync_obj) {
		r = ttm_bo_wait(&robj->tobj, true, true, false);
	}
	spin_unlock(&robj->tobj.lock);
	radeon_object_unreserve(robj);
	return r;
}

int radeon_object_evict_vram(struct radeon_device *rdev)
{
	if (rdev->flags & RADEON_IS_IGP) {
		/* Useless to evict on IGP chips */
		return 0;
	}
	return ttm_bo_evict_mm(&rdev->mman.bdev, TTM_PL_VRAM);
}

void radeon_object_force_delete(struct radeon_device *rdev)
{
	struct radeon_object *robj, *n;
	struct drm_gem_object *gobj;

	if (list_empty(&rdev->gem.objects)) {
		return;
	}
	DRM_ERROR("Userspace still has active objects !\n");
	list_for_each_entry_safe(robj, n, &rdev->gem.objects, list) {
		mutex_lock(&rdev->ddev->struct_mutex);
		gobj = robj->gobj;
		DRM_ERROR("Force free for (%p,%p,%lu,%lu)\n",
			  gobj, robj, (unsigned long)gobj->size,
			  *((unsigned long *)&gobj->refcount));
		list_del_init(&robj->list);
		radeon_object_unref(&robj);
		gobj->driver_private = NULL;
		drm_gem_object_unreference(gobj);
		mutex_unlock(&rdev->ddev->struct_mutex);
	}
}

int radeon_object_init(struct radeon_device *rdev)
{
	return radeon_ttm_init(rdev);
}

void radeon_object_fini(struct radeon_device *rdev)
{
	radeon_ttm_fini(rdev);
}

void radeon_object_list_add_object(struct radeon_object_list *lobj,
				   struct list_head *head)
{
	if (lobj->wdomain) {
		list_add(&lobj->list, head);
	} else {
		list_add_tail(&lobj->list, head);
	}
}

int radeon_object_list_reserve(struct list_head *head)
{
	struct radeon_object_list *lobj;
	struct list_head *i;
	int r;

	list_for_each(i, head) {
		lobj = list_entry(i, struct radeon_object_list, list);
		if (!lobj->robj->pin_count) {
			r = radeon_object_reserve(lobj->robj, true);
			if (unlikely(r != 0)) {
				DRM_ERROR("radeon: failed to reserve object.\n");
				return r;
			}
		} else {
		}
	}
	return 0;
}

void radeon_object_list_unreserve(struct list_head *head)
{
	struct radeon_object_list *lobj;
	struct list_head *i;

	list_for_each(i, head) {
		lobj = list_entry(i, struct radeon_object_list, list);
		if (!lobj->robj->pin_count) {
			radeon_object_unreserve(lobj->robj);
		} else {
		}
	}
}

int radeon_object_list_validate(struct list_head *head, void *fence)
{
	struct radeon_object_list *lobj;
	struct radeon_object *robj;
	struct radeon_fence *old_fence = NULL;
	struct list_head *i;
	int r;

	r = radeon_object_list_reserve(head);
	if (unlikely(r != 0)) {
		radeon_object_list_unreserve(head);
		return r;
	}
	list_for_each(i, head) {
		lobj = list_entry(i, struct radeon_object_list, list);
		robj = lobj->robj;
		if (!robj->pin_count) {
			if (lobj->wdomain) {
				robj->tobj.proposed_placement =
					radeon_object_flags_from_domain(lobj->wdomain);
			} else {
				robj->tobj.proposed_placement =
					radeon_object_flags_from_domain(lobj->rdomain);
			}
			r = ttm_buffer_object_validate(&robj->tobj,
						       robj->tobj.proposed_placement,
						       true, false);
			if (unlikely(r)) {
				DRM_ERROR("radeon: failed to validate.\n");
				return r;
			}
			radeon_object_gpu_addr(robj);
		}
		lobj->gpu_offset = robj->gpu_addr;
		lobj->tiling_flags = robj->tiling_flags;
		if (fence) {
			old_fence = (struct radeon_fence *)robj->tobj.sync_obj;
			robj->tobj.sync_obj = radeon_fence_ref(fence);
			robj->tobj.sync_obj_arg = NULL;
		}
		if (old_fence) {
			radeon_fence_unref(&old_fence);
		}
	}
	return 0;
}

void radeon_object_list_unvalidate(struct list_head *head)
{
	struct radeon_object_list *lobj;
	struct radeon_fence *old_fence = NULL;
	struct list_head *i;

	list_for_each(i, head) {
		lobj = list_entry(i, struct radeon_object_list, list);
		old_fence = (struct radeon_fence *)lobj->robj->tobj.sync_obj;
		lobj->robj->tobj.sync_obj = NULL;
		if (old_fence) {
			radeon_fence_unref(&old_fence);
		}
	}
	radeon_object_list_unreserve(head);
}

void radeon_object_list_clean(struct list_head *head)
{
	radeon_object_list_unreserve(head);
}

int radeon_object_fbdev_mmap(struct radeon_object *robj,
			     struct vm_area_struct *vma)
{
	return ttm_fbdev_mmap(vma, &robj->tobj);
}

unsigned long radeon_object_size(struct radeon_object *robj)
{
	return robj->tobj.num_pages << PAGE_SHIFT;
}

int radeon_object_get_surface_reg(struct radeon_object *robj)
{
	struct radeon_device *rdev = robj->rdev;
	struct radeon_surface_reg *reg;
	struct radeon_object *old_object;
	int steal;
	int i;

	if (!robj->tiling_flags)
		return 0;

	if (robj->surface_reg >= 0) {
		reg = &rdev->surface_regs[robj->surface_reg];
		i = robj->surface_reg;
		goto out;
	}

	steal = -1;
	for (i = 0; i < RADEON_GEM_MAX_SURFACES; i++) {

		reg = &rdev->surface_regs[i];
		if (!reg->robj)
			break;

		old_object = reg->robj;
		if (old_object->pin_count == 0)
			steal = i;
	}

	/* if we are all out */
	if (i == RADEON_GEM_MAX_SURFACES) {
		if (steal == -1)
			return -ENOMEM;
		/* find someone with a surface reg and nuke their BO */
		reg = &rdev->surface_regs[steal];
		old_object = reg->robj;
		/* blow away the mapping */
		DRM_DEBUG("stealing surface reg %d from %p\n", steal, old_object);
		ttm_bo_unmap_virtual(&old_object->tobj);
		old_object->surface_reg = -1;
		i = steal;
	}

	robj->surface_reg = i;
	reg->robj = robj;

out:
	radeon_set_surface_reg(rdev, i, robj->tiling_flags, robj->pitch,
			       robj->tobj.mem.mm_node->start << PAGE_SHIFT,
			       robj->tobj.num_pages << PAGE_SHIFT);
	return 0;
}

void radeon_object_clear_surface_reg(struct radeon_object *robj)
{
	struct radeon_device *rdev = robj->rdev;
	struct radeon_surface_reg *reg;

	if (robj->surface_reg == -1)
		return;

	reg = &rdev->surface_regs[robj->surface_reg];
	radeon_clear_surface_reg(rdev, robj->surface_reg);

	reg->robj = NULL;
	robj->surface_reg = -1;
}

void radeon_object_set_tiling_flags(struct radeon_object *robj,
				    uint32_t tiling_flags, uint32_t pitch)
{
	robj->tiling_flags = tiling_flags;
	robj->pitch = pitch;
}

void radeon_object_get_tiling_flags(struct radeon_object *robj,
				    uint32_t *tiling_flags,
				    uint32_t *pitch)
{
	if (tiling_flags)
		*tiling_flags = robj->tiling_flags;
	if (pitch)
		*pitch = robj->pitch;
}

int radeon_object_check_tiling(struct radeon_object *robj, bool has_moved,
			       bool force_drop)
{
	if (!(robj->tiling_flags & RADEON_TILING_SURFACE))
		return 0;

	if (force_drop) {
		radeon_object_clear_surface_reg(robj);
		return 0;
	}

	if (robj->tobj.mem.mem_type != TTM_PL_VRAM) {
		if (!has_moved)
			return 0;

		if (robj->surface_reg >= 0)
			radeon_object_clear_surface_reg(robj);
		return 0;
	}

	if ((robj->surface_reg >= 0) && !has_moved)
		return 0;

	return radeon_object_get_surface_reg(robj);
}

void radeon_bo_move_notify(struct ttm_buffer_object *bo,
			  struct ttm_mem_reg *mem)
{
	struct radeon_object *robj = container_of(bo, struct radeon_object, tobj);
	radeon_object_check_tiling(robj, 0, 1);
}

void radeon_bo_fault_reserve_notify(struct ttm_buffer_object *bo)
{
	struct radeon_object *robj = container_of(bo, struct radeon_object, tobj);
	radeon_object_check_tiling(robj, 0, 0);
}
