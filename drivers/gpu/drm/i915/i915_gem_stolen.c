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
	if (IS_VALLEYVIEW(dev)) {
		pci_read_config_dword(dev->pdev, 0x5c, &base);
		base &= ~((1<<20) - 1);
	} else if (INTEL_INFO(dev)->gen >= 6) {
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

static int i915_setup_compression(struct drm_device *dev, int size)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_mm_node *compressed_fb, *uninitialized_var(compressed_llb);

	/* Try to over-allocate to reduce reallocations and fragmentation */
	compressed_fb = drm_mm_search_free(&dev_priv->mm.stolen,
					   size <<= 1, 4096, 0);
	if (!compressed_fb)
		compressed_fb = drm_mm_search_free(&dev_priv->mm.stolen,
						   size >>= 1, 4096, 0);
	if (compressed_fb)
		compressed_fb = drm_mm_get_block(compressed_fb, size, 4096);
	if (!compressed_fb)
		goto err;

	if (HAS_PCH_SPLIT(dev))
		I915_WRITE(ILK_DPFC_CB_BASE, compressed_fb->start);
	else if (IS_GM45(dev)) {
		I915_WRITE(DPFC_CB_BASE, compressed_fb->start);
	} else {
		compressed_llb = drm_mm_search_free(&dev_priv->mm.stolen,
						    4096, 4096, 0);
		if (compressed_llb)
			compressed_llb = drm_mm_get_block(compressed_llb,
							  4096, 4096);
		if (!compressed_llb)
			goto err_fb;

		dev_priv->compressed_llb = compressed_llb;

		I915_WRITE(FBC_CFB_BASE,
			   dev_priv->mm.stolen_base + compressed_fb->start);
		I915_WRITE(FBC_LL_BASE,
			   dev_priv->mm.stolen_base + compressed_llb->start);
	}

	dev_priv->compressed_fb = compressed_fb;
	dev_priv->cfb_size = size;

	DRM_DEBUG_KMS("reserved %d bytes of contiguous stolen space for FBC\n",
		      size);

	return 0;

err_fb:
	drm_mm_put_block(compressed_fb);
err:
	pr_info_once("drm: not enough stolen space for compressed buffer (need %d more bytes), disabling. Hint: you may be able to increase stolen memory size in the BIOS to avoid this.\n", size);
	return -ENOSPC;
}

int i915_gem_stolen_setup_compression(struct drm_device *dev, int size)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	if (dev_priv->mm.stolen_base == 0)
		return -ENODEV;

	if (size < dev_priv->cfb_size)
		return 0;

	/* Release any current block */
	i915_gem_stolen_cleanup_compression(dev);

	return i915_setup_compression(dev, size);
}

void i915_gem_stolen_cleanup_compression(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	if (dev_priv->cfb_size == 0)
		return;

	if (dev_priv->compressed_fb)
		drm_mm_put_block(dev_priv->compressed_fb);

	if (dev_priv->compressed_llb)
		drm_mm_put_block(dev_priv->compressed_llb);

	dev_priv->cfb_size = 0;
}

void i915_gem_cleanup_stolen(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	i915_gem_stolen_cleanup_compression(dev);
	drm_mm_takedown(&dev_priv->mm.stolen);
}

int i915_gem_init_stolen(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	int bios_reserved = 0;

	dev_priv->mm.stolen_base = i915_stolen_to_physical(dev);
	if (dev_priv->mm.stolen_base == 0)
		return 0;

	DRM_DEBUG_KMS("found %zd bytes of stolen memory at %08lx\n",
		      dev_priv->gtt.stolen_size, dev_priv->mm.stolen_base);

	if (IS_VALLEYVIEW(dev))
		bios_reserved = 1024*1024; /* top 1M on VLV/BYT */

	/* Basic memrange allocator for stolen space */
	drm_mm_init(&dev_priv->mm.stolen, 0, dev_priv->gtt.stolen_size -
		    bios_reserved);

	return 0;
}

static struct sg_table *
i915_pages_create_for_stolen(struct drm_device *dev,
			     u32 offset, u32 size)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct sg_table *st;
	struct scatterlist *sg;

	DRM_DEBUG_DRIVER("offset=0x%x, size=%d\n", offset, size);
	BUG_ON(offset > dev_priv->gtt.stolen_size - size);

	/* We hide that we have no struct page backing our stolen object
	 * by wrapping the contiguous physical allocation with a fake
	 * dma mapping in a single scatterlist.
	 */

	st = kmalloc(sizeof(*st), GFP_KERNEL);
	if (st == NULL)
		return NULL;

	if (sg_alloc_table(st, 1, GFP_KERNEL)) {
		kfree(st);
		return NULL;
	}

	sg = st->sgl;
	sg->offset = 0;
	sg->length = size;

	sg_dma_address(sg) = (dma_addr_t)dev_priv->mm.stolen_base + offset;
	sg_dma_len(sg) = size;

	return st;
}

static int i915_gem_object_get_pages_stolen(struct drm_i915_gem_object *obj)
{
	BUG();
	return -EINVAL;
}

static void i915_gem_object_put_pages_stolen(struct drm_i915_gem_object *obj)
{
	/* Should only be called during free */
	sg_free_table(obj->pages);
	kfree(obj->pages);
}

static const struct drm_i915_gem_object_ops i915_gem_object_stolen_ops = {
	.get_pages = i915_gem_object_get_pages_stolen,
	.put_pages = i915_gem_object_put_pages_stolen,
};

static struct drm_i915_gem_object *
_i915_gem_object_create_stolen(struct drm_device *dev,
			       struct drm_mm_node *stolen)
{
	struct drm_i915_gem_object *obj;

	obj = i915_gem_object_alloc(dev);
	if (obj == NULL)
		return NULL;

	if (drm_gem_private_object_init(dev, &obj->base, stolen->size))
		goto cleanup;

	i915_gem_object_init(obj, &i915_gem_object_stolen_ops);

	obj->pages = i915_pages_create_for_stolen(dev,
						  stolen->start, stolen->size);
	if (obj->pages == NULL)
		goto cleanup;

	obj->has_dma_mapping = true;
	i915_gem_object_pin_pages(obj);
	obj->stolen = stolen;

	obj->base.write_domain = I915_GEM_DOMAIN_GTT;
	obj->base.read_domains = I915_GEM_DOMAIN_GTT;
	obj->cache_level = I915_CACHE_NONE;

	return obj;

cleanup:
	i915_gem_object_free(obj);
	return NULL;
}

struct drm_i915_gem_object *
i915_gem_object_create_stolen(struct drm_device *dev, u32 size)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_i915_gem_object *obj;
	struct drm_mm_node *stolen;

	if (dev_priv->mm.stolen_base == 0)
		return NULL;

	DRM_DEBUG_KMS("creating stolen object: size=%x\n", size);
	if (size == 0)
		return NULL;

	stolen = drm_mm_search_free(&dev_priv->mm.stolen, size, 4096, 0);
	if (stolen)
		stolen = drm_mm_get_block(stolen, size, 4096);
	if (stolen == NULL)
		return NULL;

	obj = _i915_gem_object_create_stolen(dev, stolen);
	if (obj)
		return obj;

	drm_mm_put_block(stolen);
	return NULL;
}

struct drm_i915_gem_object *
i915_gem_object_create_stolen_for_preallocated(struct drm_device *dev,
					       u32 stolen_offset,
					       u32 gtt_offset,
					       u32 size)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_i915_gem_object *obj;
	struct drm_mm_node *stolen;

	if (dev_priv->mm.stolen_base == 0)
		return NULL;

	DRM_DEBUG_KMS("creating preallocated stolen object: stolen_offset=%x, gtt_offset=%x, size=%x\n",
			stolen_offset, gtt_offset, size);

	/* KISS and expect everything to be page-aligned */
	BUG_ON(stolen_offset & 4095);
	BUG_ON(size & 4095);

	if (WARN_ON(size == 0))
		return NULL;

	stolen = drm_mm_create_block(&dev_priv->mm.stolen,
				     stolen_offset, size,
				     false);
	if (stolen == NULL) {
		DRM_DEBUG_KMS("failed to allocate stolen space\n");
		return NULL;
	}

	obj = _i915_gem_object_create_stolen(dev, stolen);
	if (obj == NULL) {
		DRM_DEBUG_KMS("failed to allocate stolen object\n");
		drm_mm_put_block(stolen);
		return NULL;
	}

	/* Some objects just need physical mem from stolen space */
	if (gtt_offset == -1)
		return obj;

	/* To simplify the initialisation sequence between KMS and GTT,
	 * we allow construction of the stolen object prior to
	 * setting up the GTT space. The actual reservation will occur
	 * later.
	 */
	if (drm_mm_initialized(&dev_priv->mm.gtt_space)) {
		obj->gtt_space = drm_mm_create_block(&dev_priv->mm.gtt_space,
						     gtt_offset, size,
						     false);
		if (obj->gtt_space == NULL) {
			DRM_DEBUG_KMS("failed to allocate stolen GTT space\n");
			drm_gem_object_unreference(&obj->base);
			return NULL;
		}
	} else
		obj->gtt_space = I915_GTT_RESERVED;

	obj->gtt_offset = gtt_offset;
	obj->has_global_gtt_mapping = 1;

	list_add_tail(&obj->global_list, &dev_priv->mm.bound_list);
	list_add_tail(&obj->mm_list, &dev_priv->mm.inactive_list);

	return obj;
}

void
i915_gem_object_release_stolen(struct drm_i915_gem_object *obj)
{
	if (obj->stolen) {
		drm_mm_put_block(obj->stolen);
		obj->stolen = NULL;
	}
}
