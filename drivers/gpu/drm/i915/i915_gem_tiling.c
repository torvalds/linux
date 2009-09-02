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

#include <linux/acpi.h>
#include <linux/pnp.h>
#include "linux/string.h"
#include "linux/bitops.h"
#include "drmP.h"
#include "drm.h"
#include "i915_drm.h"
#include "i915_drv.h"

/** @file i915_gem_tiling.c
 *
 * Support for managing tiling state of buffer objects.
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
 * 17 is not just a page offset, so as we page an objet out and back in,
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

#define MCHBAR_I915 0x44
#define MCHBAR_I965 0x48
#define MCHBAR_SIZE (4*4096)

#define DEVEN_REG 0x54
#define   DEVEN_MCHBAR_EN (1 << 28)

/* Allocate space for the MCH regs if needed, return nonzero on error */
static int
intel_alloc_mchbar_resource(struct drm_device *dev)
{
	struct pci_dev *bridge_dev;
	drm_i915_private_t *dev_priv = dev->dev_private;
	int reg = IS_I965G(dev) ? MCHBAR_I965 : MCHBAR_I915;
	u32 temp_lo, temp_hi = 0;
	u64 mchbar_addr;
	int ret = 0;

	bridge_dev = pci_get_bus_and_slot(0, PCI_DEVFN(0,0));
	if (!bridge_dev) {
		DRM_DEBUG("no bridge dev?!\n");
		ret = -ENODEV;
		goto out;
	}

	if (IS_I965G(dev))
		pci_read_config_dword(bridge_dev, reg + 4, &temp_hi);
	pci_read_config_dword(bridge_dev, reg, &temp_lo);
	mchbar_addr = ((u64)temp_hi << 32) | temp_lo;

	/* If ACPI doesn't have it, assume we need to allocate it ourselves */
#ifdef CONFIG_PNP
	if (mchbar_addr &&
	    pnp_range_reserved(mchbar_addr, mchbar_addr + MCHBAR_SIZE)) {
		ret = 0;
		goto out_put;
	}
#endif

	/* Get some space for it */
	ret = pci_bus_alloc_resource(bridge_dev->bus, &dev_priv->mch_res,
				     MCHBAR_SIZE, MCHBAR_SIZE,
				     PCIBIOS_MIN_MEM,
				     0,   pcibios_align_resource,
				     bridge_dev);
	if (ret) {
		DRM_DEBUG("failed bus alloc: %d\n", ret);
		dev_priv->mch_res.start = 0;
		goto out_put;
	}

	if (IS_I965G(dev))
		pci_write_config_dword(bridge_dev, reg + 4,
				       upper_32_bits(dev_priv->mch_res.start));

	pci_write_config_dword(bridge_dev, reg,
			       lower_32_bits(dev_priv->mch_res.start));
out_put:
	pci_dev_put(bridge_dev);
out:
	return ret;
}

/* Setup MCHBAR if possible, return true if we should disable it again */
static bool
intel_setup_mchbar(struct drm_device *dev)
{
	struct pci_dev *bridge_dev;
	int mchbar_reg = IS_I965G(dev) ? MCHBAR_I965 : MCHBAR_I915;
	u32 temp;
	bool need_disable = false, enabled;

	bridge_dev = pci_get_bus_and_slot(0, PCI_DEVFN(0,0));
	if (!bridge_dev) {
		DRM_DEBUG("no bridge dev?!\n");
		goto out;
	}

	if (IS_I915G(dev) || IS_I915GM(dev)) {
		pci_read_config_dword(bridge_dev, DEVEN_REG, &temp);
		enabled = !!(temp & DEVEN_MCHBAR_EN);
	} else {
		pci_read_config_dword(bridge_dev, mchbar_reg, &temp);
		enabled = temp & 1;
	}

	/* If it's already enabled, don't have to do anything */
	if (enabled)
		goto out_put;

	if (intel_alloc_mchbar_resource(dev))
		goto out_put;

	need_disable = true;

	/* Space is allocated or reserved, so enable it. */
	if (IS_I915G(dev) || IS_I915GM(dev)) {
		pci_write_config_dword(bridge_dev, DEVEN_REG,
				       temp | DEVEN_MCHBAR_EN);
	} else {
		pci_read_config_dword(bridge_dev, mchbar_reg, &temp);
		pci_write_config_dword(bridge_dev, mchbar_reg, temp | 1);
	}
out_put:
	pci_dev_put(bridge_dev);
out:
	return need_disable;
}

static void
intel_teardown_mchbar(struct drm_device *dev, bool disable)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct pci_dev *bridge_dev;
	int mchbar_reg = IS_I965G(dev) ? MCHBAR_I965 : MCHBAR_I915;
	u32 temp;

	bridge_dev = pci_get_bus_and_slot(0, PCI_DEVFN(0,0));
	if (!bridge_dev) {
		DRM_DEBUG("no bridge dev?!\n");
		return;
	}

	if (disable) {
		if (IS_I915G(dev) || IS_I915GM(dev)) {
			pci_read_config_dword(bridge_dev, DEVEN_REG, &temp);
			temp &= ~DEVEN_MCHBAR_EN;
			pci_write_config_dword(bridge_dev, DEVEN_REG, temp);
		} else {
			pci_read_config_dword(bridge_dev, mchbar_reg, &temp);
			temp &= ~1;
			pci_write_config_dword(bridge_dev, mchbar_reg, temp);
		}
	}

	if (dev_priv->mch_res.start)
		release_resource(&dev_priv->mch_res);
}

/**
 * Detects bit 6 swizzling of address lookup between IGD access and CPU
 * access through main memory.
 */
void
i915_gem_detect_bit_6_swizzle(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	uint32_t swizzle_x = I915_BIT_6_SWIZZLE_UNKNOWN;
	uint32_t swizzle_y = I915_BIT_6_SWIZZLE_UNKNOWN;
	bool need_disable;

	if (IS_IGDNG(dev)) {
		/* On IGDNG whatever DRAM config, GPU always do
		 * same swizzling setup.
		 */
		swizzle_x = I915_BIT_6_SWIZZLE_9_10;
		swizzle_y = I915_BIT_6_SWIZZLE_9;
	} else if (!IS_I9XX(dev)) {
		/* As far as we know, the 865 doesn't have these bit 6
		 * swizzling issues.
		 */
		swizzle_x = I915_BIT_6_SWIZZLE_NONE;
		swizzle_y = I915_BIT_6_SWIZZLE_NONE;
	} else if (IS_MOBILE(dev)) {
		uint32_t dcc;

		/* Try to make sure MCHBAR is enabled before poking at it */
		need_disable = intel_setup_mchbar(dev);

		/* On mobile 9xx chipsets, channel interleave by the CPU is
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
		if (dcc == 0xffffffff) {
			DRM_ERROR("Couldn't read from MCHBAR.  "
				  "Disabling tiling.\n");
			swizzle_x = I915_BIT_6_SWIZZLE_UNKNOWN;
			swizzle_y = I915_BIT_6_SWIZZLE_UNKNOWN;
		}

		intel_teardown_mchbar(dev, need_disable);
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
		 */
		if (I915_READ16(C0DRB3) != I915_READ16(C1DRB3)) {
			swizzle_x = I915_BIT_6_SWIZZLE_NONE;
			swizzle_y = I915_BIT_6_SWIZZLE_NONE;
		} else {
			swizzle_x = I915_BIT_6_SWIZZLE_9_10;
			swizzle_y = I915_BIT_6_SWIZZLE_9;
		}
	}

	dev_priv->mm.bit_6_swizzle_x = swizzle_x;
	dev_priv->mm.bit_6_swizzle_y = swizzle_y;
}


/**
 * Returns the size of the fence for a tiled object of the given size.
 */
static int
i915_get_fence_size(struct drm_device *dev, int size)
{
	int i;
	int start;

	if (IS_I965G(dev)) {
		/* The 965 can have fences at any page boundary. */
		return ALIGN(size, 4096);
	} else {
		/* Align the size to a power of two greater than the smallest
		 * fence size.
		 */
		if (IS_I9XX(dev))
			start = 1024 * 1024;
		else
			start = 512 * 1024;

		for (i = start; i < size; i <<= 1)
			;

		return i;
	}
}

/* Check pitch constriants for all chips & tiling formats */
static bool
i915_tiling_ok(struct drm_device *dev, int stride, int size, int tiling_mode)
{
	int tile_width;

	/* Linear is always fine */
	if (tiling_mode == I915_TILING_NONE)
		return true;

	if (!IS_I9XX(dev) ||
	    (tiling_mode == I915_TILING_Y && HAS_128_BYTE_Y_TILING(dev)))
		tile_width = 128;
	else
		tile_width = 512;

	/* check maximum stride & object size */
	if (IS_I965G(dev)) {
		/* i965 stores the end address of the gtt mapping in the fence
		 * reg, so dont bother to check the size */
		if (stride / 128 > I965_FENCE_MAX_PITCH_VAL)
			return false;
	} else if (IS_I9XX(dev)) {
		uint32_t pitch_val = ffs(stride / tile_width) - 1;

		/* XXX: For Y tiling, FENCE_MAX_PITCH_VAL is actually 6 (8KB)
		 * instead of 4 (2KB) on 945s.
		 */
		if (pitch_val > I915_FENCE_MAX_PITCH_VAL ||
		    size > (I830_FENCE_MAX_SIZE_VAL << 20))
			return false;
	} else {
		uint32_t pitch_val = ffs(stride / tile_width) - 1;

		if (pitch_val > I830_FENCE_MAX_PITCH_VAL ||
		    size > (I830_FENCE_MAX_SIZE_VAL << 19))
			return false;
	}

	/* 965+ just needs multiples of tile width */
	if (IS_I965G(dev)) {
		if (stride & (tile_width - 1))
			return false;
		return true;
	}

	/* Pre-965 needs power of two tile widths */
	if (stride < tile_width)
		return false;

	if (stride & (stride - 1))
		return false;

	/* We don't 0handle the aperture area covered by the fence being bigger
	 * than the object size.
	 */
	if (i915_get_fence_size(dev, size) != size)
		return false;

	return true;
}

static bool
i915_gem_object_fence_offset_ok(struct drm_gem_object *obj, int tiling_mode)
{
	struct drm_device *dev = obj->dev;
	struct drm_i915_gem_object *obj_priv = obj->driver_private;

	if (obj_priv->gtt_space == NULL)
		return true;

	if (tiling_mode == I915_TILING_NONE)
		return true;

	if (!IS_I965G(dev)) {
		if (obj_priv->gtt_offset & (obj->size - 1))
			return false;
		if (IS_I9XX(dev)) {
			if (obj_priv->gtt_offset & ~I915_FENCE_START_MASK)
				return false;
		} else {
			if (obj_priv->gtt_offset & ~I830_FENCE_START_MASK)
				return false;
		}
	}

	return true;
}

/**
 * Sets the tiling mode of an object, returning the required swizzling of
 * bit 6 of addresses in the object.
 */
int
i915_gem_set_tiling(struct drm_device *dev, void *data,
		   struct drm_file *file_priv)
{
	struct drm_i915_gem_set_tiling *args = data;
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct drm_gem_object *obj;
	struct drm_i915_gem_object *obj_priv;
	int ret = 0;

	obj = drm_gem_object_lookup(dev, file_priv, args->handle);
	if (obj == NULL)
		return -EINVAL;
	obj_priv = obj->driver_private;

	if (!i915_tiling_ok(dev, args->stride, obj->size, args->tiling_mode)) {
		mutex_lock(&dev->struct_mutex);
		drm_gem_object_unreference(obj);
		mutex_unlock(&dev->struct_mutex);
		return -EINVAL;
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

	mutex_lock(&dev->struct_mutex);
	if (args->tiling_mode != obj_priv->tiling_mode ||
	    args->stride != obj_priv->stride) {
		/* We need to rebind the object if its current allocation
		 * no longer meets the alignment restrictions for its new
		 * tiling mode. Otherwise we can just leave it alone, but
		 * need to ensure that any fence register is cleared.
		 */
		if (!i915_gem_object_fence_offset_ok(obj, args->tiling_mode))
		    ret = i915_gem_object_unbind(obj);
		else
		    ret = i915_gem_object_put_fence_reg(obj);
		if (ret != 0) {
			WARN(ret != -ERESTARTSYS,
			     "failed to reset object for tiling switch");
			args->tiling_mode = obj_priv->tiling_mode;
			args->stride = obj_priv->stride;
			goto err;
		}

		/* If we've changed tiling, GTT-mappings of the object
		 * need to re-fault to ensure that the correct fence register
		 * setup is in place.
		 */
		i915_gem_release_mmap(obj);

		obj_priv->tiling_mode = args->tiling_mode;
		obj_priv->stride = args->stride;
	}
err:
	drm_gem_object_unreference(obj);
	mutex_unlock(&dev->struct_mutex);

	return ret;
}

/**
 * Returns the current tiling mode and required bit 6 swizzling for the object.
 */
int
i915_gem_get_tiling(struct drm_device *dev, void *data,
		   struct drm_file *file_priv)
{
	struct drm_i915_gem_get_tiling *args = data;
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct drm_gem_object *obj;
	struct drm_i915_gem_object *obj_priv;

	obj = drm_gem_object_lookup(dev, file_priv, args->handle);
	if (obj == NULL)
		return -EINVAL;
	obj_priv = obj->driver_private;

	mutex_lock(&dev->struct_mutex);

	args->tiling_mode = obj_priv->tiling_mode;
	switch (obj_priv->tiling_mode) {
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
	if (args->swizzle_mode == I915_BIT_6_SWIZZLE_9_17)
		args->swizzle_mode = I915_BIT_6_SWIZZLE_9;
	if (args->swizzle_mode == I915_BIT_6_SWIZZLE_9_10_17)
		args->swizzle_mode = I915_BIT_6_SWIZZLE_9_10;

	drm_gem_object_unreference(obj);
	mutex_unlock(&dev->struct_mutex);

	return 0;
}

/**
 * Swap every 64 bytes of this page around, to account for it having a new
 * bit 17 of its physical address and therefore being interpreted differently
 * by the GPU.
 */
static int
i915_gem_swizzle_page(struct page *page)
{
	char *vaddr;
	int i;
	char temp[64];

	vaddr = kmap(page);
	if (vaddr == NULL)
		return -ENOMEM;

	for (i = 0; i < PAGE_SIZE; i += 128) {
		memcpy(temp, &vaddr[i], 64);
		memcpy(&vaddr[i], &vaddr[i + 64], 64);
		memcpy(&vaddr[i + 64], temp, 64);
	}

	kunmap(page);

	return 0;
}

void
i915_gem_object_do_bit_17_swizzle(struct drm_gem_object *obj)
{
	struct drm_device *dev = obj->dev;
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct drm_i915_gem_object *obj_priv = obj->driver_private;
	int page_count = obj->size >> PAGE_SHIFT;
	int i;

	if (dev_priv->mm.bit_6_swizzle_x != I915_BIT_6_SWIZZLE_9_10_17)
		return;

	if (obj_priv->bit_17 == NULL)
		return;

	for (i = 0; i < page_count; i++) {
		char new_bit_17 = page_to_phys(obj_priv->pages[i]) >> 17;
		if ((new_bit_17 & 0x1) !=
		    (test_bit(i, obj_priv->bit_17) != 0)) {
			int ret = i915_gem_swizzle_page(obj_priv->pages[i]);
			if (ret != 0) {
				DRM_ERROR("Failed to swizzle page\n");
				return;
			}
			set_page_dirty(obj_priv->pages[i]);
		}
	}
}

void
i915_gem_object_save_bit_17_swizzle(struct drm_gem_object *obj)
{
	struct drm_device *dev = obj->dev;
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct drm_i915_gem_object *obj_priv = obj->driver_private;
	int page_count = obj->size >> PAGE_SHIFT;
	int i;

	if (dev_priv->mm.bit_6_swizzle_x != I915_BIT_6_SWIZZLE_9_10_17)
		return;

	if (obj_priv->bit_17 == NULL) {
		obj_priv->bit_17 = kmalloc(BITS_TO_LONGS(page_count) *
					   sizeof(long), GFP_KERNEL);
		if (obj_priv->bit_17 == NULL) {
			DRM_ERROR("Failed to allocate memory for bit 17 "
				  "record\n");
			return;
		}
	}

	for (i = 0; i < page_count; i++) {
		if (page_to_phys(obj_priv->pages[i]) & (1 << 17))
			__set_bit(i, obj_priv->bit_17);
		else
			__clear_bit(i, obj_priv->bit_17);
	}
}
