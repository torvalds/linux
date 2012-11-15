/*
 * Copyright © 2008-2012 Intel Corporation
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
 *    Chris Wilson <chris@chris-wilson.co.uk>
 *
 */

#include <drm/drmP.h>
#include <drm/i915_drm.h>
#include "i915_drv.h"

/*
 * The BIOS typically reserves some of the system's memory for the exclusive
 * use of the integrated graphics. This memory is no longer available for
 * use by the OS and so the user finds that his system has less memory
 * available than he put in. We refer to this memory as stolen.
 *
 * The BIOS will allocate its framebuffer from the stolen memory. Our
 * goal is try to reuse that object for our own fbcon which must always
 * be available for panics. Anything else we can reuse the stolen memory
 * for is a boon.
 */

static unsigned long i915_stolen_to_physical(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct pci_dev *pdev = dev_priv->bridge_dev;
	u32 base;

	/* On the machines I have tested the Graphics Base of Stolen Memory
	 * is unreliable, so on those compute the base by subtracting the
	 * stolen memory from the Top of Low Usable DRAM which is where the
	 * BIOS places the graphics stolen memory.
	 *
	 * On gen2, the layout is slightly different with the Graphics Segment
	 * immediately following Top of Memory (or Top of Usable DRAM). Note
	 * it appears that TOUD is only reported by 865g, so we just use the
	 * top of memory as determined by the e820 probe.
	 *
	 * XXX gen2 requires an unavailable symbol and 945gm fails with
	 * its value of TOLUD.
	 */
	base = 0;
	if (INTEL_INFO(dev)->gen >= 6) {
		/* Read Base Data of Stolen Memory Register (BDSM) directly.
		 * Note that there is also a MCHBAR miror at 0x1080c0 or
		 * we could use device 2:0x5c instead.
		*/
		pci_read_config_dword(pdev, 0xB0, &base);
		base &= ~4095; /* lower bits used for locking register */
	} else if (INTEL_INFO(dev)->gen > 3 || IS_G33(dev)) {
		/* Read Graphics Base of Stolen Memory directly */
		pci_read_config_dword(pdev, 0xA4, &base);
#if 0
	} else if (IS_GEN3(dev)) {
		u8 val;
		/* Stolen is immediately below Top of Low Usable DRAM */
		pci_read_config_byte(pdev, 0x9c, &val);
		base = val >> 3 << 27;
		base -= dev_priv->mm.gtt->stolen_size;
	} else {
		/* Stolen is immediately above Top of Memory */
		base = max_low_pfn_mapped << PAGE_SHIFT;
#endif
	}

	return base;
}

static void i915_warn_stolen(struct drm_device *dev)
{
	DRM_INFO("not enough stolen space for compressed buffer, disabling\n");
	DRM_INFO("hint: you may be able to increase stolen memory size in the BIOS to avoid this\n");
}

static void i915_setup_compression(struct drm_device *dev, int size)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_mm_node *compressed_fb, *uninitialized_var(compressed_llb);
	unsigned long cfb_base;
	unsigned long ll_base = 0;

	/* Just in case the BIOS is doing something questionable. */
	intel_disable_fbc(dev);

	compressed_fb = drm_mm_search_free(&dev_priv->mm.stolen, size, 4096, 0);
	if (compressed_fb)
		compressed_fb = drm_mm_get_block(compressed_fb, size, 4096);
	if (!compressed_fb)
		goto err;

	cfb_base = dev_priv->mm.stolen_base + compressed_fb->start;
	if (!cfb_base)
		goto err_fb;

	if (!(IS_GM45(dev) || HAS_PCH_SPLIT(dev))) {
		compressed_llb = drm_mm_search_free(&dev_priv->mm.stolen,
						    4096, 4096, 0);
		if (compressed_llb)
			compressed_llb = drm_mm_get_block(compressed_llb,
							  4096, 4096);
		if (!compressed_llb)
			goto err_fb;

		ll_base = dev_priv->mm.stolen_base + compressed_llb->start;
		if (!ll_base)
			goto err_llb;
	}

	dev_priv->cfb_size = size;

	dev_priv->compressed_fb = compressed_fb;
	if (HAS_PCH_SPLIT(dev))
		I915_WRITE(ILK_DPFC_CB_BASE, compressed_fb->start);
	else if (IS_GM45(dev)) {
		I915_WRITE(DPFC_CB_BASE, compressed_fb->start);
	} else {
		I915_WRITE(FBC_CFB_BASE, cfb_base);
		I915_WRITE(FBC_LL_BASE, ll_base);
		dev_priv->compressed_llb = compressed_llb;
	}

	DRM_DEBUG_KMS("FBC base 0x%08lx, ll base 0x%08lx, size %dM\n",
		      (long)cfb_base, (long)ll_base, size >> 20);
	return;

err_llb:
	drm_mm_put_block(compressed_llb);
err_fb:
	drm_mm_put_block(compressed_fb);
err:
	dev_priv->no_fbc_reason = FBC_STOLEN_TOO_SMALL;
	i915_warn_stolen(dev);
}

static void i915_cleanup_compression(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	drm_mm_put_block(dev_priv->compressed_fb);
	if (dev_priv->compressed_llb)
		drm_mm_put_block(dev_priv->compressed_llb);
}

void i915_gem_cleanup_stolen(struct drm_device *dev)
{
	if (I915_HAS_FBC(dev) && i915_powersave)
		i915_cleanup_compression(dev);
}

int i915_gem_init_stolen(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	unsigned long prealloc_size = dev_priv->mm.gtt->stolen_size;

	dev_priv->mm.stolen_base = i915_stolen_to_physical(dev);
	if (dev_priv->mm.stolen_base == 0)
		return 0;

	DRM_DEBUG_KMS("found %d bytes of stolen memory at %08lx\n",
		      dev_priv->mm.gtt->stolen_size, dev_priv->mm.stolen_base);

	/* Basic memrange allocator for stolen space */
	drm_mm_init(&dev_priv->mm.stolen, 0, prealloc_size);

	/* Try to set up FBC with a reasonable compressed buffer size */
	if (I915_HAS_FBC(dev) && i915_powersave) {
		int cfb_size;

		/* Leave 1M for line length buffer & misc. */

		/* Try to get a 32M buffer... */
		if (prealloc_size > (36*1024*1024))
			cfb_size = 32*1024*1024;
		else /* fall back to 7/8 of the stolen space */
			cfb_size = prealloc_size * 7 / 8;
		i915_setup_compression(dev, cfb_size);
	}

	return 0;
}
