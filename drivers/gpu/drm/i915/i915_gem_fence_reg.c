/*
 * Copyright © 2008-2015 Intel Corporation
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
 */

#include <drm/drmP.h>
#include <drm/i915_drm.h>
#include "i915_drv.h"

/**
 * DOC: fence register handling
 *
 * Important to avoid confusions: "fences" in the i915 driver are not execution
 * fences used to track command completion but hardware detiler objects which
 * wrap a given range of the global GTT. Each platform has only a fairly limited
 * set of these objects.
 *
 * Fences are used to detile GTT memory mappings. They're also connected to the
 * hardware frontbuffer render tracking and hence interact with frontbuffer
 * compression. Furthermore on older platforms fences are required for tiled
 * objects used by the display engine. They can also be used by the render
 * engine - they're required for blitter commands and are optional for render
 * commands. But on gen4+ both display (with the exception of fbc) and rendering
 * have their own tiling state bits and don't need fences.
 *
 * Also note that fences only support X and Y tiling and hence can't be used for
 * the fancier new tiling formats like W, Ys and Yf.
 *
 * Finally note that because fences are such a restricted resource they're
 * dynamically associated with objects. Furthermore fence state is committed to
 * the hardware lazily to avoid unnecessary stalls on gen2/3. Therefore code must
 * explicitly call i915_gem_object_get_fence() to synchronize fencing status
 * for cpu access. Also note that some code wants an unfenced view, for those
 * cases the fence can be removed forcefully with i915_gem_object_put_fence().
 *
 * Internally these functions will synchronize with userspace access by removing
 * CPU ptes into GTT mmaps (not the GTT ptes themselves) as needed.
 */

#define pipelined 0

static void i965_write_fence_reg(struct drm_i915_fence_reg *fence,
				 struct i915_vma *vma)
{
	i915_reg_t fence_reg_lo, fence_reg_hi;
	int fence_pitch_shift;
	u64 val;

	if (INTEL_INFO(fence->i915)->gen >= 6) {
		fence_reg_lo = FENCE_REG_GEN6_LO(fence->id);
		fence_reg_hi = FENCE_REG_GEN6_HI(fence->id);
		fence_pitch_shift = GEN6_FENCE_PITCH_SHIFT;

	} else {
		fence_reg_lo = FENCE_REG_965_LO(fence->id);
		fence_reg_hi = FENCE_REG_965_HI(fence->id);
		fence_pitch_shift = I965_FENCE_PITCH_SHIFT;
	}

	val = 0;
	if (vma) {
		unsigned int stride = i915_gem_object_get_stride(vma->obj);

		GEM_BUG_ON(!i915_vma_is_map_and_fenceable(vma));
		GEM_BUG_ON(!IS_ALIGNED(vma->node.start, I965_FENCE_PAGE));
		GEM_BUG_ON(!IS_ALIGNED(vma->fence_size, I965_FENCE_PAGE));
		GEM_BUG_ON(!IS_ALIGNED(stride, 128));

		val = (vma->node.start + vma->fence_size - I965_FENCE_PAGE) << 32;
		val |= vma->node.start;
		val |= (u64)((stride / 128) - 1) << fence_pitch_shift;
		if (i915_gem_object_get_tiling(vma->obj) == I915_TILING_Y)
			val |= BIT(I965_FENCE_TILING_Y_SHIFT);
		val |= I965_FENCE_REG_VALID;
	}

	if (!pipelined) {
		struct drm_i915_private *dev_priv = fence->i915;

		/* To w/a incoherency with non-atomic 64-bit register updates,
		 * we split the 64-bit update into two 32-bit writes. In order
		 * for a partial fence not to be evaluated between writes, we
		 * precede the update with write to turn off the fence register,
		 * and only enable the fence as the last step.
		 *
		 * For extra levels of paranoia, we make sure each step lands
		 * before applying the next step.
		 */
		I915_WRITE(fence_reg_lo, 0);
		POSTING_READ(fence_reg_lo);

		I915_WRITE(fence_reg_hi, upper_32_bits(val));
		I915_WRITE(fence_reg_lo, lower_32_bits(val));
		POSTING_READ(fence_reg_lo);
	}
}

static void i915_write_fence_reg(struct drm_i915_fence_reg *fence,
				 struct i915_vma *vma)
{
	u32 val;

	val = 0;
	if (vma) {
		unsigned int tiling = i915_gem_object_get_tiling(vma->obj);
		bool is_y_tiled = tiling == I915_TILING_Y;
		unsigned int stride = i915_gem_object_get_stride(vma->obj);

		GEM_BUG_ON(!i915_vma_is_map_and_fenceable(vma));
		GEM_BUG_ON(vma->node.start & ~I915_FENCE_START_MASK);
		GEM_BUG_ON(!is_power_of_2(vma->fence_size));
		GEM_BUG_ON(!IS_ALIGNED(vma->node.start, vma->fence_size));

		if (is_y_tiled && HAS_128_BYTE_Y_TILING(fence->i915))
			stride /= 128;
		else
			stride /= 512;
		GEM_BUG_ON(!is_power_of_2(stride));

		val = vma->node.start;
		if (is_y_tiled)
			val |= BIT(I830_FENCE_TILING_Y_SHIFT);
		val |= I915_FENCE_SIZE_BITS(vma->fence_size);
		val |= ilog2(stride) << I830_FENCE_PITCH_SHIFT;

		val |= I830_FENCE_REG_VALID;
	}

	if (!pipelined) {
		struct drm_i915_private *dev_priv = fence->i915;
		i915_reg_t reg = FENCE_REG(fence->id);

		I915_WRITE(reg, val);
		POSTING_READ(reg);
	}
}

static void i830_write_fence_reg(struct drm_i915_fence_reg *fence,
				 struct i915_vma *vma)
{
	u32 val;

	val = 0;
	if (vma) {
		unsigned int stride = i915_gem_object_get_stride(vma->obj);

		GEM_BUG_ON(!i915_vma_is_map_and_fenceable(vma));
		GEM_BUG_ON(vma->node.start & ~I830_FENCE_START_MASK);
		GEM_BUG_ON(!is_power_of_2(vma->fence_size));
		GEM_BUG_ON(!is_power_of_2(stride / 128));
		GEM_BUG_ON(!IS_ALIGNED(vma->node.start, vma->fence_size));

		val = vma->node.start;
		if (i915_gem_object_get_tiling(vma->obj) == I915_TILING_Y)
			val |= BIT(I830_FENCE_TILING_Y_SHIFT);
		val |= I830_FENCE_SIZE_BITS(vma->fence_size);
		val |= ilog2(stride / 128) << I830_FENCE_PITCH_SHIFT;
		val |= I830_FENCE_REG_VALID;
	}

	if (!pipelined) {
		struct drm_i915_private *dev_priv = fence->i915;
		i915_reg_t reg = FENCE_REG(fence->id);

		I915_WRITE(reg, val);
		POSTING_READ(reg);
	}
}

static void fence_write(struct drm_i915_fence_reg *fence,
			struct i915_vma *vma)
{
	/* Previous access through the fence register is marshalled by
	 * the mb() inside the fault handlers (i915_gem_release_mmaps)
	 * and explicitly managed for internal users.
	 */

	if (IS_GEN2(fence->i915))
		i830_write_fence_reg(fence, vma);
	else if (IS_GEN3(fence->i915))
		i915_write_fence_reg(fence, vma);
	else
		i965_write_fence_reg(fence, vma);

	/* Access through the fenced region afterwards is
	 * ordered by the posting reads whilst writing the registers.
	 */

	fence->dirty = false;
}

static int fence_update(struct drm_i915_fence_reg *fence,
			struct i915_vma *vma)
{
	int ret;

	if (vma) {
		if (!i915_vma_is_map_and_fenceable(vma))
			return -EINVAL;

		if (WARN(!i915_gem_object_get_stride(vma->obj) ||
			 !i915_gem_object_get_tiling(vma->obj),
			 "bogus fence setup with stride: 0x%x, tiling mode: %i\n",
			 i915_gem_object_get_stride(vma->obj),
			 i915_gem_object_get_tiling(vma->obj)))
			return -EINVAL;

		ret = i915_gem_active_retire(&vma->last_fence,
					     &vma->obj->base.dev->struct_mutex);
		if (ret)
			return ret;
	}

	if (fence->vma) {
		ret = i915_gem_active_retire(&fence->vma->last_fence,
				      &fence->vma->obj->base.dev->struct_mutex);
		if (ret)
			return ret;
	}

	if (fence->vma && fence->vma != vma) {
		/* Ensure that all userspace CPU access is completed before
		 * stealing the fence.
		 */
		i915_gem_release_mmap(fence->vma->obj);

		fence->vma->fence = NULL;
		fence->vma = NULL;

		list_move(&fence->link, &fence->i915->mm.fence_list);
	}

	/* We only need to update the register itself if the device is awake.
	 * If the device is currently powered down, we will defer the write
	 * to the runtime resume, see i915_gem_restore_fences().
	 */
	if (intel_runtime_pm_get_if_in_use(fence->i915)) {
		fence_write(fence, vma);
		intel_runtime_pm_put(fence->i915);
	}

	if (vma) {
		if (fence->vma != vma) {
			vma->fence = fence;
			fence->vma = vma;
		}

		list_move_tail(&fence->link, &fence->i915->mm.fence_list);
	}

	return 0;
}

/**
 * i915_vma_put_fence - force-remove fence for a VMA
 * @vma: vma to map linearly (not through a fence reg)
 *
 * This function force-removes any fence from the given object, which is useful
 * if the kernel wants to do untiled GTT access.
 *
 * Returns:
 *
 * 0 on success, negative error code on failure.
 */
int
i915_vma_put_fence(struct i915_vma *vma)
{
	struct drm_i915_fence_reg *fence = vma->fence;

	if (!fence)
		return 0;

	if (fence->pin_count)
		return -EBUSY;

	return fence_update(fence, NULL);
}

static struct drm_i915_fence_reg *fence_find(struct drm_i915_private *dev_priv)
{
	struct drm_i915_fence_reg *fence;

	list_for_each_entry(fence, &dev_priv->mm.fence_list, link) {
		if (fence->pin_count)
			continue;

		return fence;
	}

	/* Wait for completion of pending flips which consume fences */
	if (intel_has_pending_fb_unpin(dev_priv))
		return ERR_PTR(-EAGAIN);

	return ERR_PTR(-EDEADLK);
}

/**
 * i915_vma_get_fence - set up fencing for a vma
 * @vma: vma to map through a fence reg
 *
 * When mapping objects through the GTT, userspace wants to be able to write
 * to them without having to worry about swizzling if the object is tiled.
 * This function walks the fence regs looking for a free one for @obj,
 * stealing one if it can't find any.
 *
 * It then sets up the reg based on the object's properties: address, pitch
 * and tiling format.
 *
 * For an untiled surface, this removes any existing fence.
 *
 * Returns:
 *
 * 0 on success, negative error code on failure.
 */
int
i915_vma_get_fence(struct i915_vma *vma)
{
	struct drm_i915_fence_reg *fence;
	struct i915_vma *set = i915_gem_object_is_tiled(vma->obj) ? vma : NULL;

	/* Note that we revoke fences on runtime suspend. Therefore the user
	 * must keep the device awake whilst using the fence.
	 */
	assert_rpm_wakelock_held(vma->vm->i915);

	/* Just update our place in the LRU if our fence is getting reused. */
	if (vma->fence) {
		fence = vma->fence;
		if (!fence->dirty) {
			list_move_tail(&fence->link,
				       &fence->i915->mm.fence_list);
			return 0;
		}
	} else if (set) {
		fence = fence_find(vma->vm->i915);
		if (IS_ERR(fence))
			return PTR_ERR(fence);
	} else
		return 0;

	return fence_update(fence, set);
}

/**
 * i915_reserve_fence - Reserve a fence for vGPU
 * @dev_priv: i915 device private
 *
 * This function walks the fence regs looking for a free one and remove
 * it from the fence_list. It is used to reserve fence for vGPU to use.
 */
struct drm_i915_fence_reg *
i915_reserve_fence(struct drm_i915_private *dev_priv)
{
	struct drm_i915_fence_reg *fence;
	int count;
	int ret;

	lockdep_assert_held(&dev_priv->drm.struct_mutex);

	/* Keep at least one fence available for the display engine. */
	count = 0;
	list_for_each_entry(fence, &dev_priv->mm.fence_list, link)
		count += !fence->pin_count;
	if (count <= 1)
		return ERR_PTR(-ENOSPC);

	fence = fence_find(dev_priv);
	if (IS_ERR(fence))
		return fence;

	if (fence->vma) {
		/* Force-remove fence from VMA */
		ret = fence_update(fence, NULL);
		if (ret)
			return ERR_PTR(ret);
	}

	list_del(&fence->link);
	return fence;
}

/**
 * i915_unreserve_fence - Reclaim a reserved fence
 * @fence: the fence reg
 *
 * This function add a reserved fence register from vGPU to the fence_list.
 */
void i915_unreserve_fence(struct drm_i915_fence_reg *fence)
{
	lockdep_assert_held(&fence->i915->drm.struct_mutex);

	list_add(&fence->link, &fence->i915->mm.fence_list);
}

/**
 * i915_gem_revoke_fences - revoke fence state
 * @dev_priv: i915 device private
 *
 * Removes all GTT mmappings via the fence registers. This forces any user
 * of the fence to reacquire that fence before continuing with their access.
 * One use is during GPU reset where the fence register is lost and we need to
 * revoke concurrent userspace access via GTT mmaps until the hardware has been
 * reset and the fence registers have been restored.
 */
void i915_gem_revoke_fences(struct drm_i915_private *dev_priv)
{
	int i;

	lockdep_assert_held(&dev_priv->drm.struct_mutex);

	for (i = 0; i < dev_priv->num_fence_regs; i++) {
		struct drm_i915_fence_reg *fence = &dev_priv->fence_regs[i];

		if (fence->vma)
			i915_gem_release_mmap(fence->vma->obj);
	}
}

/**
 * i915_gem_restore_fences - restore fence state
 * @dev_priv: i915 device private
 *
 * Restore the hw fence state to match the software tracking again, to be called
 * after a gpu reset and on resume. Note that on runtime suspend we only cancel
 * the fences, to be reacquired by the user later.
 */
void i915_gem_restore_fences(struct drm_i915_private *dev_priv)
{
	int i;

	for (i = 0; i < dev_priv->num_fence_regs; i++) {
		struct drm_i915_fence_reg *reg = &dev_priv->fence_regs[i];
		struct i915_vma *vma = reg->vma;

		/*
		 * Commit delayed tiling changes if we have an object still
		 * attached to the fence, otherwise just clear the fence.
		 */
		if (vma && !i915_gem_object_is_tiled(vma->obj)) {
			GEM_BUG_ON(!reg->dirty);
			GEM_BUG_ON(!list_empty(&vma->obj->userfault_link));

			list_move(&reg->link, &dev_priv->mm.fence_list);
			vma->fence = NULL;
			vma = NULL;
		}

		fence_write(reg, vma);
		reg->vma = vma;
	}
}

/**
 * DOC: tiling swizzling details
 *
 * The idea behind tiling is to increase cache hit rates by rearranging
 * pixel data so that a group of pixel accesses are in the same cacheline.
 * Performance improvement from doing this on the back/depth buffer are on
 * the order of 30%.
 *
 * Intel architectures make this somewhat more complicated, though, by
 * adjustments made to addressing of data when the memory is in interleaved
 * mode (matched pairs of DIMMS) to improve memory bandwidth.
 * For interleaved memory, the CPU sends every sequential 64 bytes
 * to an alternate memory channel so it can get the bandwidth from both.
 *
 * The GPU also rearranges its accesses for increased bandwidth to interleaved
 * memory, and it matches what the CPU does for non-tiled.  However, when tiled
 * it does it a little differently, since one walks addresses not just in the
 * X direction but also Y.  So, along with alternating channels when bit
 * 6 of the address flips, it also alternates when other bits flip --  Bits 9
 * (every 512 bytes, an X tile scanline) and 10 (every two X tile scanlines)
 * are common to both the 915 and 965-class hardware.
 *
 * The CPU also sometimes XORs in higher bits as well, to improve
 * bandwidth doing strided access like we do so frequently in graphics.  This
 * is called "Channel XOR Randomization" in the MCH documentation.  The result
 * is that the CPU is XORing in either bit 11 or bit 17 to bit 6 of its address
 * decode.
 *
 * All of this bit 6 XORing has an effect on our memory management,
 * as we need to make sure that the 3d driver can correctly address object
 * contents.
 *
 * If we don't have interleaved memory, all tiling is safe and no swizzling is
 * required.
 *
 * When bit 17 is XORed in, we simply refuse to tile at all.  Bit
 * 17 is not just a page offset, so as we page an object out and back in,
 * individual pages in it will have different bit 17 addresses, resulting in
 * each 64 bytes being swapped with its neighbor!
 *
 * Otherwise, if interleaved, we have to tell the 3d driver what the address
 * swizzling it needs to do is, since it's writing with the CPU to the pages
 * (bit 6 and potentially bit 11 XORed in), and the GPU is reading from the
 * pages (bit 6, 9, and 10 XORed in), resulting in a cumulative bit swizzling
 * required by the CPU of XORing in bit 6, 9, 10, and potentially 11, in order
 * to match what the GPU expects.
 */

/**
 * i915_gem_detect_bit_6_swizzle - detect bit 6 swizzling pattern
 * @dev_priv: i915 device private
 *
 * Detects bit 6 swizzling of address lookup between IGD access and CPU
 * access through main memory.
 */
void
i915_gem_detect_bit_6_swizzle(struct drm_i915_private *dev_priv)
{
	uint32_t swizzle_x = I915_BIT_6_SWIZZLE_UNKNOWN;
	uint32_t swizzle_y = I915_BIT_6_SWIZZLE_UNKNOWN;

	if (INTEL_GEN(dev_priv) >= 8 || IS_VALLEYVIEW(dev_priv)) {
		/*
		 * On BDW+, swizzling is not used. We leave the CPU memory
		 * controller in charge of optimizing memory accesses without
		 * the extra address manipulation GPU side.
		 *
		 * VLV and CHV don't have GPU swizzling.
		 */
		swizzle_x = I915_BIT_6_SWIZZLE_NONE;
		swizzle_y = I915_BIT_6_SWIZZLE_NONE;
	} else if (INTEL_GEN(dev_priv) >= 6) {
		if (dev_priv->preserve_bios_swizzle) {
			if (I915_READ(DISP_ARB_CTL) &
			    DISP_TILE_SURFACE_SWIZZLING) {
				swizzle_x = I915_BIT_6_SWIZZLE_9_10;
				swizzle_y = I915_BIT_6_SWIZZLE_9;
			} else {
				swizzle_x = I915_BIT_6_SWIZZLE_NONE;
				swizzle_y = I915_BIT_6_SWIZZLE_NONE;
			}
		} else {
			uint32_t dimm_c0, dimm_c1;
			dimm_c0 = I915_READ(MAD_DIMM_C0);
			dimm_c1 = I915_READ(MAD_DIMM_C1);
			dimm_c0 &= MAD_DIMM_A_SIZE_MASK | MAD_DIMM_B_SIZE_MASK;
			dimm_c1 &= MAD_DIMM_A_SIZE_MASK | MAD_DIMM_B_SIZE_MASK;
			/* Enable swizzling when the channels are populated
			 * with identically sized dimms. We don't need to check
			 * the 3rd channel because no cpu with gpu attached
			 * ships in that configuration. Also, swizzling only
			 * makes sense for 2 channels anyway. */
			if (dimm_c0 == dimm_c1) {
				swizzle_x = I915_BIT_6_SWIZZLE_9_10;
				swizzle_y = I915_BIT_6_SWIZZLE_9;
			} else {
				swizzle_x = I915_BIT_6_SWIZZLE_NONE;
				swizzle_y = I915_BIT_6_SWIZZLE_NONE;
			}
		}
	} else if (IS_GEN5(dev_priv)) {
		/* On Ironlake whatever DRAM config, GPU always do
		 * same swizzling setup.
		 */
		swizzle_x = I915_BIT_6_SWIZZLE_9_10;
		swizzle_y = I915_BIT_6_SWIZZLE_9;
	} else if (IS_GEN2(dev_priv)) {
		/* As far as we know, the 865 doesn't have these bit 6
		 * swizzling issues.
		 */
		swizzle_x = I915_BIT_6_SWIZZLE_NONE;
		swizzle_y = I915_BIT_6_SWIZZLE_NONE;
	} else if (IS_MOBILE(dev_priv) ||
		   IS_I915G(dev_priv) || IS_I945G(dev_priv)) {
		uint32_t dcc;

		/* On 9xx chipsets, channel interleave by the CPU is
		 * determined by DCC.  For single-channel, neither the CPU
		 * nor the GPU do swizzling.  For dual channel interleaved,
		 * the GPU's interleave is bit 9 and 10 for X tiled, and bit
		 * 9 for Y tiled.  The CPU's interleave is independent, and
		 * can be based on either bit 11 (haven't seen this yet) or
		 * bit 17 (common).
		 */
		dcc = I915_READ(DCC);
		switch (dcc & DCC_ADDRESSING_MODE_MASK) {
		case DCC_ADDRESSING_MODE_SINGLE_CHANNEL:
		case DCC_ADDRESSING_MODE_DUAL_CHANNEL_ASYMMETRIC:
			swizzle_x = I915_BIT_6_SWIZZLE_NONE;
			swizzle_y = I915_BIT_6_SWIZZLE_NONE;
			break;
		case DCC_ADDRESSING_MODE_DUAL_CHANNEL_INTERLEAVED:
			if (dcc & DCC_CHANNEL_XOR_DISABLE) {
				/* This is the base swizzling by the GPU for
				 * tiled buffers.
				 */
				swizzle_x = I915_BIT_6_SWIZZLE_9_10;
				swizzle_y = I915_BIT_6_SWIZZLE_9;
			} else if ((dcc & DCC_CHANNEL_XOR_BIT_17) == 0) {
				/* Bit 11 swizzling by the CPU in addition. */
				swizzle_x = I915_BIT_6_SWIZZLE_9_10_11;
				swizzle_y = I915_BIT_6_SWIZZLE_9_11;
			} else {
				/* Bit 17 swizzling by the CPU in addition. */
				swizzle_x = I915_BIT_6_SWIZZLE_9_10_17;
				swizzle_y = I915_BIT_6_SWIZZLE_9_17;
			}
			break;
		}

		/* check for L-shaped memory aka modified enhanced addressing */
		if (IS_GEN4(dev_priv) &&
		    !(I915_READ(DCC2) & DCC2_MODIFIED_ENHANCED_DISABLE)) {
			swizzle_x = I915_BIT_6_SWIZZLE_UNKNOWN;
			swizzle_y = I915_BIT_6_SWIZZLE_UNKNOWN;
		}

		if (dcc == 0xffffffff) {
			DRM_ERROR("Couldn't read from MCHBAR.  "
				  "Disabling tiling.\n");
			swizzle_x = I915_BIT_6_SWIZZLE_UNKNOWN;
			swizzle_y = I915_BIT_6_SWIZZLE_UNKNOWN;
		}
	} else {
		/* The 965, G33, and newer, have a very flexible memory
		 * configuration.  It will enable dual-channel mode
		 * (interleaving) on as much memory as it can, and the GPU
		 * will additionally sometimes enable different bit 6
		 * swizzling for tiled objects from the CPU.
		 *
		 * Here's what I found on the G965:
		 *    slot fill         memory size  swizzling
		 * 0A   0B   1A   1B    1-ch   2-ch
		 * 512  0    0    0     512    0     O
		 * 512  0    512  0     16     1008  X
		 * 512  0    0    512   16     1008  X
		 * 0    512  0    512   16     1008  X
		 * 1024 1024 1024 0     2048   1024  O
		 *
		 * We could probably detect this based on either the DRB
		 * matching, which was the case for the swizzling required in
		 * the table above, or from the 1-ch value being less than
		 * the minimum size of a rank.
		 *
		 * Reports indicate that the swizzling actually
		 * varies depending upon page placement inside the
		 * channels, i.e. we see swizzled pages where the
		 * banks of memory are paired and unswizzled on the
		 * uneven portion, so leave that as unknown.
		 */
		if (I915_READ16(C0DRB3) == I915_READ16(C1DRB3)) {
			swizzle_x = I915_BIT_6_SWIZZLE_9_10;
			swizzle_y = I915_BIT_6_SWIZZLE_9;
		}
	}

	if (swizzle_x == I915_BIT_6_SWIZZLE_UNKNOWN ||
	    swizzle_y == I915_BIT_6_SWIZZLE_UNKNOWN) {
		/* Userspace likes to explode if it sees unknown swizzling,
		 * so lie. We will finish the lie when reporting through
		 * the get-tiling-ioctl by reporting the physical swizzle
		 * mode as unknown instead.
		 *
		 * As we don't strictly know what the swizzling is, it may be
		 * bit17 dependent, and so we need to also prevent the pages
		 * from being moved.
		 */
		dev_priv->quirks |= QUIRK_PIN_SWIZZLED_PAGES;
		swizzle_x = I915_BIT_6_SWIZZLE_NONE;
		swizzle_y = I915_BIT_6_SWIZZLE_NONE;
	}

	dev_priv->mm.bit_6_swizzle_x = swizzle_x;
	dev_priv->mm.bit_6_swizzle_y = swizzle_y;
}

/*
 * Swap every 64 bytes of this page around, to account for it having a new
 * bit 17 of its physical address and therefore being interpreted differently
 * by the GPU.
 */
static void
i915_gem_swizzle_page(struct page *page)
{
	char temp[64];
	char *vaddr;
	int i;

	vaddr = kmap(page);

	for (i = 0; i < PAGE_SIZE; i += 128) {
		memcpy(temp, &vaddr[i], 64);
		memcpy(&vaddr[i], &vaddr[i + 64], 64);
		memcpy(&vaddr[i + 64], temp, 64);
	}

	kunmap(page);
}

/**
 * i915_gem_object_do_bit_17_swizzle - fixup bit 17 swizzling
 * @obj: i915 GEM buffer object
 * @pages: the scattergather list of physical pages
 *
 * This function fixes up the swizzling in case any page frame number for this
 * object has changed in bit 17 since that state has been saved with
 * i915_gem_object_save_bit_17_swizzle().
 *
 * This is called when pinning backing storage again, since the kernel is free
 * to move unpinned backing storage around (either by directly moving pages or
 * by swapping them out and back in again).
 */
void
i915_gem_object_do_bit_17_swizzle(struct drm_i915_gem_object *obj,
				  struct sg_table *pages)
{
	struct sgt_iter sgt_iter;
	struct page *page;
	int i;

	if (obj->bit_17 == NULL)
		return;

	i = 0;
	for_each_sgt_page(page, sgt_iter, pages) {
		char new_bit_17 = page_to_phys(page) >> 17;
		if ((new_bit_17 & 0x1) != (test_bit(i, obj->bit_17) != 0)) {
			i915_gem_swizzle_page(page);
			set_page_dirty(page);
		}
		i++;
	}
}

/**
 * i915_gem_object_save_bit_17_swizzle - save bit 17 swizzling
 * @obj: i915 GEM buffer object
 * @pages: the scattergather list of physical pages
 *
 * This function saves the bit 17 of each page frame number so that swizzling
 * can be fixed up later on with i915_gem_object_do_bit_17_swizzle(). This must
 * be called before the backing storage can be unpinned.
 */
void
i915_gem_object_save_bit_17_swizzle(struct drm_i915_gem_object *obj,
				    struct sg_table *pages)
{
	const unsigned int page_count = obj->base.size >> PAGE_SHIFT;
	struct sgt_iter sgt_iter;
	struct page *page;
	int i;

	if (obj->bit_17 == NULL) {
		obj->bit_17 = kcalloc(BITS_TO_LONGS(page_count),
				      sizeof(long), GFP_KERNEL);
		if (obj->bit_17 == NULL) {
			DRM_ERROR("Failed to allocate memory for bit 17 "
				  "record\n");
			return;
		}
	}

	i = 0;

	for_each_sgt_page(page, sgt_iter, pages) {
		if (page_to_phys(page) & (1 << 17))
			__set_bit(i, obj->bit_17);
		else
			__clear_bit(i, obj->bit_17);
		i++;
	}
}
