/*
 * Copyright © 2008 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Authors:
 *    Eric Anholt <eric@anholt.net>
 *
 */

#include <linux/string.h>
#include <linux/bitops.h>
#include <drm/drmP.h>
#include <drm/i915_drm.h>
#include "i915_drv.h"

/**
 * DOC: buffer object tiling
 *
 * i915_gem_set_tiling() and i915_gem_get_tiling() is the userspace interface to
 * declare fence register requirements.
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

/* Check pitch constriants for all chips & tiling formats */
static bool
i915_tiling_ok(struct drm_device *dev, int stride, int size, int tiling_mode)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	int tile_width;

	/* Linear is always fine */
	if (tiling_mode == I915_TILING_NONE)
		return true;

	if (tiling_mode > I915_TILING_LAST)
		return false;

	if (IS_GEN2(dev_priv) ||
	    (tiling_mode == I915_TILING_Y && HAS_128_BYTE_Y_TILING(dev_priv)))
		tile_width = 128;
	else
		tile_width = 512;

	/* check maximum stride & object size */
	/* i965+ stores the end address of the gtt mapping in the fence
	 * reg, so dont bother to check the size */
	if (INTEL_INFO(dev)->gen >= 7) {
		if (stride / 128 > GEN7_FENCE_MAX_PITCH_VAL)
			return false;
	} else if (INTEL_INFO(dev)->gen >= 4) {
		if (stride / 128 > I965_FENCE_MAX_PITCH_VAL)
			return false;
	} else {
		if (stride > 8192)
			return false;

		if (IS_GEN3(dev_priv)) {
			if (size > I830_FENCE_MAX_SIZE_VAL << 20)
				return false;
		} else {
			if (size > I830_FENCE_MAX_SIZE_VAL << 19)
				return false;
		}
	}

	if (stride < tile_width)
		return false;

	/* 965+ just needs multiples of tile width */
	if (INTEL_INFO(dev)->gen >= 4) {
		if (stride & (tile_width - 1))
			return false;
		return true;
	}

	/* Pre-965 needs power of two tile widths */
	if (stride & (stride - 1))
		return false;

	return true;
}

static bool i915_vma_fence_prepare(struct i915_vma *vma, int tiling_mode)
{
	struct drm_i915_private *dev_priv = to_i915(vma->vm->dev);
	u32 size;

	if (!i915_vma_is_map_and_fenceable(vma))
		return true;

	if (INTEL_GEN(dev_priv) == 3) {
		if (vma->node.start & ~I915_FENCE_START_MASK)
			return false;
	} else {
		if (vma->node.start & ~I830_FENCE_START_MASK)
			return false;
	}

	size = i915_gem_get_ggtt_size(dev_priv, vma->size, tiling_mode);
	if (vma->node.size < size)
		return false;

	if (vma->node.start & (size - 1))
		return false;

	return true;
}

/* Make the current GTT allocation valid for the change in tiling. */
static int
i915_gem_object_fence_prepare(struct drm_i915_gem_object *obj, int tiling_mode)
{
	struct drm_i915_private *dev_priv = to_i915(obj->base.dev);
	struct i915_vma *vma;
	int ret;

	if (tiling_mode == I915_TILING_NONE)
		return 0;

	if (INTEL_GEN(dev_priv) >= 4)
		return 0;

	list_for_each_entry(vma, &obj->vma_list, obj_link) {
		if (i915_vma_fence_prepare(vma, tiling_mode))
			continue;

		ret = i915_vma_unbind(vma);
		if (ret)
			return ret;
	}

	return 0;
}

/**
 * i915_gem_set_tiling - IOCTL handler to set tiling mode
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
i915_gem_set_tiling(struct drm_device *dev, void *data,
		   struct drm_file *file)
{
	struct drm_i915_gem_set_tiling *args = data;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct drm_i915_gem_object *obj;
	int err = 0;

	/* Make sure we don't cross-contaminate obj->tiling_and_stride */
	BUILD_BUG_ON(I915_TILING_LAST & STRIDE_MASK);

	obj = i915_gem_object_lookup(file, args->handle);
	if (!obj)
		return -ENOENT;

	if (!i915_tiling_ok(dev,
			    args->stride, obj->base.size, args->tiling_mode)) {
		i915_gem_object_put_unlocked(obj);
		return -EINVAL;
	}

	mutex_lock(&dev->struct_mutex);
	if (obj->pin_display || obj->framebuffer_references) {
		err = -EBUSY;
		goto err;
	}

	if (args->tiling_mode == I915_TILING_NONE) {
		args->swizzle_mode = I915_BIT_6_SWIZZLE_NONE;
		args->stride = 0;
	} else {
		if (args->tiling_mode == I915_TILING_X)
			args->swizzle_mode = dev_priv->mm.bit_6_swizzle_x;
		else
			args->swizzle_mode = dev_priv->mm.bit_6_swizzle_y;

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

	if (args->tiling_mode != i915_gem_object_get_tiling(obj) ||
	    args->stride != i915_gem_object_get_stride(obj)) {
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

		err = i915_gem_object_fence_prepare(obj, args->tiling_mode);
		if (!err) {
			struct i915_vma *vma;

			mutex_lock(&obj->mm.lock);
			if (obj->mm.pages &&
			    obj->mm.madv == I915_MADV_WILLNEED &&
			    dev_priv->quirks & QUIRK_PIN_SWIZZLED_PAGES) {
				if (args->tiling_mode == I915_TILING_NONE)
					__i915_gem_object_unpin_pages(obj);
				if (!i915_gem_object_is_tiled(obj))
					__i915_gem_object_pin_pages(obj);
			}
			mutex_unlock(&obj->mm.lock);

			list_for_each_entry(vma, &obj->vma_list, obj_link) {
				if (!vma->fence)
					continue;

				vma->fence->dirty = true;
			}
			obj->tiling_and_stride =
				args->stride | args->tiling_mode;

			/* Force the fence to be reacquired for GTT access */
			i915_gem_release_mmap(obj);
		}
	}
	/* we have to maintain this existing ABI... */
	args->stride = i915_gem_object_get_stride(obj);
	args->tiling_mode = i915_gem_object_get_tiling(obj);

	/* Try to preallocate memory required to save swizzling on put-pages */
	if (i915_gem_object_needs_bit17_swizzle(obj)) {
		if (obj->bit_17 == NULL) {
			obj->bit_17 = kcalloc(BITS_TO_LONGS(obj->base.size >> PAGE_SHIFT),
					      sizeof(long), GFP_KERNEL);
		}
	} else {
		kfree(obj->bit_17);
		obj->bit_17 = NULL;
	}

err:
	i915_gem_object_put(obj);
	mutex_unlock(&dev->struct_mutex);

	return err;
}

/**
 * i915_gem_get_tiling - IOCTL handler to get tiling mode
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
i915_gem_get_tiling(struct drm_device *dev, void *data,
		   struct drm_file *file)
{
	struct drm_i915_gem_get_tiling *args = data;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct drm_i915_gem_object *obj;

	obj = i915_gem_object_lookup(file, args->handle);
	if (!obj)
		return -ENOENT;

	args->tiling_mode = READ_ONCE(obj->tiling_and_stride) & TILING_MASK;
	switch (args->tiling_mode) {
	case I915_TILING_X:
		args->swizzle_mode = dev_priv->mm.bit_6_swizzle_x;
		break;
	case I915_TILING_Y:
		args->swizzle_mode = dev_priv->mm.bit_6_swizzle_y;
		break;
	case I915_TILING_NONE:
		args->swizzle_mode = I915_BIT_6_SWIZZLE_NONE;
		break;
	default:
		DRM_ERROR("unknown tiling mode\n");
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

	i915_gem_object_put_unlocked(obj);
	return 0;
}
