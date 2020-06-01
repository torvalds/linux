/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2008 Intel Corporation
 */

#include <linux/string.h>
#include <linux/bitops.h>

#include "i915_drv.h"
#include "i915_gem.h"
#include "i915_gem_ioctls.h"
#include "i915_gem_mman.h"
#include "i915_gem_object.h"

/**
 * DOC: buffer object tiling
 *
 * i915_gem_set_tiling_ioctl() and i915_gem_get_tiling_ioctl() is the userspace
 * interface to declare fence register requirements.
 *
 * In principle GEM doesn't care at all about the internal data layout of an
 * object, and hence it also doesn't care about tiling or swizzling. There's two
 * exceptions:
 *
 * - For X and Y tiling the hardware provides detilers for CPU access, so called
 *   fences. Since there's only a limited amount of them the kernel must manage
 *   these, and therefore userspace must tell the kernel the object tiling if it
 *   wants to use fences for detiling.
 * - On gen3 and gen4 platforms have a swizzling pattern for tiled objects which
 *   depends upon the physical page frame number. When swapping such objects the
 *   page frame number might change and the kernel must be able to fix this up
 *   and hence now the tiling. Note that on a subset of platforms with
 *   asymmetric memory channel population the swizzling pattern changes in an
 *   unknown way, and for those the kernel simply forbids swapping completely.
 *
 * Since neither of this applies for new tiling layouts on modern platforms like
 * W, Ys and Yf tiling GEM only allows object tiling to be set to X or Y tiled.
 * Anything else can be handled in userspace entirely without the kernel's
 * invovlement.
 */

/**
 * i915_gem_fence_size - required global GTT size for a fence
 * @i915: i915 device
 * @size: object size
 * @tiling: tiling mode
 * @stride: tiling stride
 *
 * Return the required global GTT size for a fence (view of a tiled object),
 * taking into account potential fence register mapping.
 */
u32 i915_gem_fence_size(struct drm_i915_private *i915,
			u32 size, unsigned int tiling, unsigned int stride)
{
	u32 ggtt_size;

	GEM_BUG_ON(!size);

	if (tiling == I915_TILING_NONE)
		return size;

	GEM_BUG_ON(!stride);

	if (INTEL_GEN(i915) >= 4) {
		stride *= i915_gem_tile_height(tiling);
		GEM_BUG_ON(!IS_ALIGNED(stride, I965_FENCE_PAGE));
		return roundup(size, stride);
	}

	/* Previous chips need a power-of-two fence region when tiling */
	if (IS_GEN(i915, 3))
		ggtt_size = 1024*1024;
	else
		ggtt_size = 512*1024;

	while (ggtt_size < size)
		ggtt_size <<= 1;

	return ggtt_size;
}

/**
 * i915_gem_fence_alignment - required global GTT alignment for a fence
 * @i915: i915 device
 * @size: object size
 * @tiling: tiling mode
 * @stride: tiling stride
 *
 * Return the required global GTT alignment for a fence (a view of a tiled
 * object), taking into account potential fence register mapping.
 */
u32 i915_gem_fence_alignment(struct drm_i915_private *i915, u32 size,
			     unsigned int tiling, unsigned int stride)
{
	GEM_BUG_ON(!size);

	/*
	 * Minimum alignment is 4k (GTT page size), but might be greater
	 * if a fence register is needed for the object.
	 */
	if (tiling == I915_TILING_NONE)
		return I915_GTT_MIN_ALIGNMENT;

	if (INTEL_GEN(i915) >= 4)
		return I965_FENCE_PAGE;

	/*
	 * Previous chips need to be aligned to the size of the smallest
	 * fence register that can contain the object.
	 */
	return i915_gem_fence_size(i915, size, tiling, stride);
}

/* Check pitch constriants for all chips & tiling formats */
static bool
i915_tiling_ok(struct drm_i915_gem_object *obj,
	       unsigned int tiling, unsigned int stride)
{
	struct drm_i915_private *i915 = to_i915(obj->base.dev);
	unsigned int tile_width;

	/* Linear is always fine */
	if (tiling == I915_TILING_NONE)
		return true;

	if (tiling > I915_TILING_LAST)
		return false;

	/* check maximum stride & object size */
	/* i965+ stores the end address of the gtt mapping in the fence
	 * reg, so dont bother to check the size */
	if (INTEL_GEN(i915) >= 7) {
		if (stride / 128 > GEN7_FENCE_MAX_PITCH_VAL)
			return false;
	} else if (INTEL_GEN(i915) >= 4) {
		if (stride / 128 > I965_FENCE_MAX_PITCH_VAL)
			return false;
	} else {
		if (stride > 8192)
			return false;

		if (!is_power_of_2(stride))
			return false;
	}

	if (IS_GEN(i915, 2) ||
	    (tiling == I915_TILING_Y && HAS_128_BYTE_Y_TILING(i915)))
		tile_width = 128;
	else
		tile_width = 512;

	if (!stride || !IS_ALIGNED(stride, tile_width))
		return false;

	return true;
}

static bool i915_vma_fence_prepare(struct i915_vma *vma,
				   int tiling_mode, unsigned int stride)
{
	struct drm_i915_private *i915 = vma->vm->i915;
	u32 size, alignment;

	if (!i915_vma_is_map_and_fenceable(vma))
		return true;

	size = i915_gem_fence_size(i915, vma->size, tiling_mode, stride);
	if (vma->node.size < size)
		return false;

	alignment = i915_gem_fence_alignment(i915, vma->size, tiling_mode, stride);
	if (!IS_ALIGNED(vma->node.start, alignment))
		return false;

	return true;
}

/* Make the current GTT allocation valid for the change in tiling. */
static int
i915_gem_object_fence_prepare(struct drm_i915_gem_object *obj,
			      int tiling_mode, unsigned int stride)
{
	struct i915_ggtt *ggtt = &to_i915(obj->base.dev)->ggtt;
	struct i915_vma *vma;
	int ret = 0;

	if (tiling_mode == I915_TILING_NONE)
		return 0;

	mutex_lock(&ggtt->vm.mutex);
	for_each_ggtt_vma(vma, obj) {
		if (i915_vma_fence_prepare(vma, tiling_mode, stride))
			continue;

		ret = __i915_vma_unbind(vma);
		if (ret)
			break;
	}
	mutex_unlock(&ggtt->vm.mutex);

	return ret;
}

int
i915_gem_object_set_tiling(struct drm_i915_gem_object *obj,
			   unsigned int tiling, unsigned int stride)
{
	struct drm_i915_private *i915 = to_i915(obj->base.dev);
	struct i915_vma *vma;
	int err;

	/* Make sure we don't cross-contaminate obj->tiling_and_stride */
	BUILD_BUG_ON(I915_TILING_LAST & STRIDE_MASK);

	GEM_BUG_ON(!i915_tiling_ok(obj, tiling, stride));
	GEM_BUG_ON(!stride ^ (tiling == I915_TILING_NONE));

	if ((tiling | stride) == obj->tiling_and_stride)
		return 0;

	if (i915_gem_object_is_framebuffer(obj))
		return -EBUSY;

	/* We need to rebind the object if its current allocation
	 * no longer meets the alignment restrictions for its new
	 * tiling mode. Otherwise we can just leave it alone, but
	 * need to ensure that any fence register is updated before
	 * the next fenced (either through the GTT or by the BLT unit
	 * on older GPUs) access.
	 *
	 * After updating the tiling parameters, we then flag whether
	 * we need to update an associated fence register. Note this
	 * has to also include the unfenced register the GPU uses
	 * whilst executing a fenced command for an untiled object.
	 */

	i915_gem_object_lock(obj);
	if (i915_gem_object_is_framebuffer(obj)) {
		i915_gem_object_unlock(obj);
		return -EBUSY;
	}

	err = i915_gem_object_fence_prepare(obj, tiling, stride);
	if (err) {
		i915_gem_object_unlock(obj);
		return err;
	}

	/* If the memory has unknown (i.e. varying) swizzling, we pin the
	 * pages to prevent them being swapped out and causing corruption
	 * due to the change in swizzling.
	 */
	mutex_lock(&obj->mm.lock);
	if (i915_gem_object_has_pages(obj) &&
	    obj->mm.madv == I915_MADV_WILLNEED &&
	    i915->quirks & QUIRK_PIN_SWIZZLED_PAGES) {
		if (tiling == I915_TILING_NONE) {
			GEM_BUG_ON(!obj->mm.quirked);
			__i915_gem_object_unpin_pages(obj);
			obj->mm.quirked = false;
		}
		if (!i915_gem_object_is_tiled(obj)) {
			GEM_BUG_ON(obj->mm.quirked);
			__i915_gem_object_pin_pages(obj);
			obj->mm.quirked = true;
		}
	}
	mutex_unlock(&obj->mm.lock);

	for_each_ggtt_vma(vma, obj) {
		vma->fence_size =
			i915_gem_fence_size(i915, vma->size, tiling, stride);
		vma->fence_alignment =
			i915_gem_fence_alignment(i915,
						 vma->size, tiling, stride);

		if (vma->fence)
			vma->fence->dirty = true;
	}

	obj->tiling_and_stride = tiling | stride;
	i915_gem_object_unlock(obj);

	/* Force the fence to be reacquired for GTT access */
	i915_gem_object_release_mmap(obj);

	/* Try to preallocate memory required to save swizzling on put-pages */
	if (i915_gem_object_needs_bit17_swizzle(obj)) {
		if (!obj->bit_17) {
			obj->bit_17 = bitmap_zalloc(obj->base.size >> PAGE_SHIFT,
						    GFP_KERNEL);
		}
	} else {
		bitmap_free(obj->bit_17);
		obj->bit_17 = NULL;
	}

	return 0;
}

/**
 * i915_gem_set_tiling_ioctl - IOCTL handler to set tiling mode
 * @dev: DRM device
 * @data: data pointer for the ioctl
 * @file: DRM file for the ioctl call
 *
 * Sets the tiling mode of an object, returning the required swizzling of
 * bit 6 of addresses in the object.
 *
 * Called by the user via ioctl.
 *
 * Returns:
 * Zero on success, negative errno on failure.
 */
int
i915_gem_set_tiling_ioctl(struct drm_device *dev, void *data,
			  struct drm_file *file)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct drm_i915_gem_set_tiling *args = data;
	struct drm_i915_gem_object *obj;
	int err;

	if (!dev_priv->ggtt.num_fences)
		return -EOPNOTSUPP;

	obj = i915_gem_object_lookup(file, args->handle);
	if (!obj)
		return -ENOENT;

	/*
	 * The tiling mode of proxy objects is handled by its generator, and
	 * not allowed to be changed by userspace.
	 */
	if (i915_gem_object_is_proxy(obj)) {
		err = -ENXIO;
		goto err;
	}

	if (!i915_tiling_ok(obj, args->tiling_mode, args->stride)) {
		err = -EINVAL;
		goto err;
	}

	if (args->tiling_mode == I915_TILING_NONE) {
		args->swizzle_mode = I915_BIT_6_SWIZZLE_NONE;
		args->stride = 0;
	} else {
		if (args->tiling_mode == I915_TILING_X)
			args->swizzle_mode = to_i915(dev)->ggtt.bit_6_swizzle_x;
		else
			args->swizzle_mode = to_i915(dev)->ggtt.bit_6_swizzle_y;

		/* Hide bit 17 swizzling from the user.  This prevents old Mesa
		 * from aborting the application on sw fallbacks to bit 17,
		 * and we use the pread/pwrite bit17 paths to swizzle for it.
		 * If there was a user that was relying on the swizzle
		 * information for drm_intel_bo_map()ed reads/writes this would
		 * break it, but we don't have any of those.
		 */
		if (args->swizzle_mode == I915_BIT_6_SWIZZLE_9_17)
			args->swizzle_mode = I915_BIT_6_SWIZZLE_9;
		if (args->swizzle_mode == I915_BIT_6_SWIZZLE_9_10_17)
			args->swizzle_mode = I915_BIT_6_SWIZZLE_9_10;

		/* If we can't handle the swizzling, make it untiled. */
		if (args->swizzle_mode == I915_BIT_6_SWIZZLE_UNKNOWN) {
			args->tiling_mode = I915_TILING_NONE;
			args->swizzle_mode = I915_BIT_6_SWIZZLE_NONE;
			args->stride = 0;
		}
	}

	err = i915_gem_object_set_tiling(obj, args->tiling_mode, args->stride);

	/* We have to maintain this existing ABI... */
	args->stride = i915_gem_object_get_stride(obj);
	args->tiling_mode = i915_gem_object_get_tiling(obj);

err:
	i915_gem_object_put(obj);
	return err;
}

/**
 * i915_gem_get_tiling_ioctl - IOCTL handler to get tiling mode
 * @dev: DRM device
 * @data: data pointer for the ioctl
 * @file: DRM file for the ioctl call
 *
 * Returns the current tiling mode and required bit 6 swizzling for the object.
 *
 * Called by the user via ioctl.
 *
 * Returns:
 * Zero on success, negative errno on failure.
 */
int
i915_gem_get_tiling_ioctl(struct drm_device *dev, void *data,
			  struct drm_file *file)
{
	struct drm_i915_gem_get_tiling *args = data;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct drm_i915_gem_object *obj;
	int err = -ENOENT;

	if (!dev_priv->ggtt.num_fences)
		return -EOPNOTSUPP;

	rcu_read_lock();
	obj = i915_gem_object_lookup_rcu(file, args->handle);
	if (obj) {
		args->tiling_mode =
			READ_ONCE(obj->tiling_and_stride) & TILING_MASK;
		err = 0;
	}
	rcu_read_unlock();
	if (unlikely(err))
		return err;

	switch (args->tiling_mode) {
	case I915_TILING_X:
		args->swizzle_mode = dev_priv->ggtt.bit_6_swizzle_x;
		break;
	case I915_TILING_Y:
		args->swizzle_mode = dev_priv->ggtt.bit_6_swizzle_y;
		break;
	default:
	case I915_TILING_NONE:
		args->swizzle_mode = I915_BIT_6_SWIZZLE_NONE;
		break;
	}

	/* Hide bit 17 from the user -- see comment in i915_gem_set_tiling */
	if (dev_priv->quirks & QUIRK_PIN_SWIZZLED_PAGES)
		args->phys_swizzle_mode = I915_BIT_6_SWIZZLE_UNKNOWN;
	else
		args->phys_swizzle_mode = args->swizzle_mode;
	if (args->swizzle_mode == I915_BIT_6_SWIZZLE_9_17)
		args->swizzle_mode = I915_BIT_6_SWIZZLE_9;
	if (args->swizzle_mode == I915_BIT_6_SWIZZLE_9_10_17)
		args->swizzle_mode = I915_BIT_6_SWIZZLE_9_10;

	return 0;
}
