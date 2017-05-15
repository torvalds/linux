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

#define KB(x) ((x) * 1024)
#define MB(x) (KB(x) * 1024)

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

int i915_gem_stolen_insert_node_in_range(struct drm_i915_private *dev_priv,
					 struct drm_mm_node *node, u64 size,
					 unsigned alignment, u64 start, u64 end)
{
	int ret;

	if (!drm_mm_initialized(&dev_priv->mm.stolen))
		return -ENODEV;

	mutex_lock(&dev_priv->mm.stolen_lock);
	ret = drm_mm_insert_node_in_range(&dev_priv->mm.stolen, node,
					  size, alignment, 0,
					  start, end, DRM_MM_INSERT_BEST);
	mutex_unlock(&dev_priv->mm.stolen_lock);

	return ret;
}

int i915_gem_stolen_insert_node(struct drm_i915_private *dev_priv,
				struct drm_mm_node *node, u64 size,
				unsigned alignment)
{
	return i915_gem_stolen_insert_node_in_range(dev_priv, node, size,
						    alignment, 0, U64_MAX);
}

void i915_gem_stolen_remove_node(struct drm_i915_private *dev_priv,
				 struct drm_mm_node *node)
{
	mutex_lock(&dev_priv->mm.stolen_lock);
	drm_mm_remove_node(node);
	mutex_unlock(&dev_priv->mm.stolen_lock);
}

static dma_addr_t i915_stolen_to_dma(struct drm_i915_private *dev_priv)
{
	struct pci_dev *pdev = dev_priv->drm.pdev;
	struct i915_ggtt *ggtt = &dev_priv->ggtt;
	struct resource *r;
	dma_addr_t base;

	/* Almost universally we can find the Graphics Base of Stolen Memory
	 * at register BSM (0x5c) in the igfx configuration space. On a few
	 * (desktop) machines this is also mirrored in the bridge device at
	 * different locations, or in the MCHBAR.
	 *
	 * On 865 we just check the TOUD register.
	 *
	 * On 830/845/85x the stolen memory base isn't available in any
	 * register. We need to calculate it as TOM-TSEG_SIZE-stolen_size.
	 *
	 */
	base = 0;
	if (INTEL_GEN(dev_priv) >= 3) {
		u32 bsm;

		pci_read_config_dword(pdev, INTEL_BSM, &bsm);

		base = bsm & INTEL_BSM_MASK;
	} else if (IS_I865G(dev_priv)) {
		u32 tseg_size = 0;
		u16 toud = 0;
		u8 tmp;

		pci_bus_read_config_byte(pdev->bus, PCI_DEVFN(0, 0),
					 I845_ESMRAMC, &tmp);

		if (tmp & TSEG_ENABLE) {
			switch (tmp & I845_TSEG_SIZE_MASK) {
			case I845_TSEG_SIZE_512K:
				tseg_size = KB(512);
				break;
			case I845_TSEG_SIZE_1M:
				tseg_size = MB(1);
				break;
			}
		}

		pci_bus_read_config_word(pdev->bus, PCI_DEVFN(0, 0),
					 I865_TOUD, &toud);

		base = (toud << 16) + tseg_size;
	} else if (IS_I85X(dev_priv)) {
		u32 tseg_size = 0;
		u32 tom;
		u8 tmp;

		pci_bus_read_config_byte(pdev->bus, PCI_DEVFN(0, 0),
					 I85X_ESMRAMC, &tmp);

		if (tmp & TSEG_ENABLE)
			tseg_size = MB(1);

		pci_bus_read_config_byte(pdev->bus, PCI_DEVFN(0, 1),
					 I85X_DRB3, &tmp);
		tom = tmp * MB(32);

		base = tom - tseg_size - ggtt->stolen_size;
	} else if (IS_I845G(dev_priv)) {
		u32 tseg_size = 0;
		u32 tom;
		u8 tmp;

		pci_bus_read_config_byte(pdev->bus, PCI_DEVFN(0, 0),
					 I845_ESMRAMC, &tmp);

		if (tmp & TSEG_ENABLE) {
			switch (tmp & I845_TSEG_SIZE_MASK) {
			case I845_TSEG_SIZE_512K:
				tseg_size = KB(512);
				break;
			case I845_TSEG_SIZE_1M:
				tseg_size = MB(1);
				break;
			}
		}

		pci_bus_read_config_byte(pdev->bus, PCI_DEVFN(0, 0),
					 I830_DRB3, &tmp);
		tom = tmp * MB(32);

		base = tom - tseg_size - ggtt->stolen_size;
	} else if (IS_I830(dev_priv)) {
		u32 tseg_size = 0;
		u32 tom;
		u8 tmp;

		pci_bus_read_config_byte(pdev->bus, PCI_DEVFN(0, 0),
					 I830_ESMRAMC, &tmp);

		if (tmp & TSEG_ENABLE) {
			if (tmp & I830_TSEG_SIZE_1M)
				tseg_size = MB(1);
			else
				tseg_size = KB(512);
		}

		pci_bus_read_config_byte(pdev->bus, PCI_DEVFN(0, 0),
					 I830_DRB3, &tmp);
		tom = tmp * MB(32);

		base = tom - tseg_size - ggtt->stolen_size;
	}

	if (base == 0 || add_overflows(base, ggtt->stolen_size))
		return 0;

	/* make sure we don't clobber the GTT if it's within stolen memory */
	if (INTEL_GEN(dev_priv) <= 4 &&
	    !IS_G33(dev_priv) && !IS_PINEVIEW(dev_priv) && !IS_G4X(dev_priv)) {
		struct {
			dma_addr_t start, end;
		} stolen[2] = {
			{ .start = base, .end = base + ggtt->stolen_size, },
			{ .start = base, .end = base + ggtt->stolen_size, },
		};
		u64 ggtt_start, ggtt_end;

		ggtt_start = I915_READ(PGTBL_CTL);
		if (IS_GEN4(dev_priv))
			ggtt_start = (ggtt_start & PGTBL_ADDRESS_LO_MASK) |
				     (ggtt_start & PGTBL_ADDRESS_HI_MASK) << 28;
		else
			ggtt_start &= PGTBL_ADDRESS_LO_MASK;
		ggtt_end = ggtt_start + ggtt_total_entries(ggtt) * 4;

		if (ggtt_start >= stolen[0].start && ggtt_start < stolen[0].end)
			stolen[0].end = ggtt_start;
		if (ggtt_end > stolen[1].start && ggtt_end <= stolen[1].end)
			stolen[1].start = ggtt_end;

		/* pick the larger of the two chunks */
		if (stolen[0].end - stolen[0].start >
		    stolen[1].end - stolen[1].start) {
			base = stolen[0].start;
			ggtt->stolen_size = stolen[0].end - stolen[0].start;
		} else {
			base = stolen[1].start;
			ggtt->stolen_size = stolen[1].end - stolen[1].start;
		}

		if (stolen[0].start != stolen[1].start ||
		    stolen[0].end != stolen[1].end) {
			dma_addr_t end = base + ggtt->stolen_size - 1;

			DRM_DEBUG_KMS("GTT within stolen memory at 0x%llx-0x%llx\n",
				      (unsigned long long)ggtt_start,
				      (unsigned long long)ggtt_end - 1);
			DRM_DEBUG_KMS("Stolen memory adjusted to %pad - %pad\n",
				      &base, &end);
		}
	}


	/* Verify that nothing else uses this physical address. Stolen
	 * memory should be reserved by the BIOS and hidden from the
	 * kernel. So if the region is already marked as busy, something
	 * is seriously wrong.
	 */
	r = devm_request_mem_region(dev_priv->drm.dev, base, ggtt->stolen_size,
				    "Graphics Stolen Memory");
	if (r == NULL) {
		/*
		 * One more attempt but this time requesting region from
		 * base + 1, as we have seen that this resolves the region
		 * conflict with the PCI Bus.
		 * This is a BIOS w/a: Some BIOS wrap stolen in the root
		 * PCI bus, but have an off-by-one error. Hence retry the
		 * reservation starting from 1 instead of 0.
		 */
		r = devm_request_mem_region(dev_priv->drm.dev, base + 1,
					    ggtt->stolen_size - 1,
					    "Graphics Stolen Memory");
		/*
		 * GEN3 firmware likes to smash pci bridges into the stolen
		 * range. Apparently this works.
		 */
		if (r == NULL && !IS_GEN3(dev_priv)) {
			dma_addr_t end = base + ggtt->stolen_size;

			DRM_ERROR("conflict detected with stolen region: [%pad - %pad]\n",
				  &base, &end);
			base = 0;
		}
	}

	return base;
}

void i915_gem_cleanup_stolen(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);

	if (!drm_mm_initialized(&dev_priv->mm.stolen))
		return;

	drm_mm_takedown(&dev_priv->mm.stolen);
}

static void g4x_get_stolen_reserved(struct drm_i915_private *dev_priv,
				    dma_addr_t *base, u32 *size)
{
	struct i915_ggtt *ggtt = &dev_priv->ggtt;
	uint32_t reg_val = I915_READ(IS_GM45(dev_priv) ?
				     CTG_STOLEN_RESERVED :
				     ELK_STOLEN_RESERVED);
	dma_addr_t stolen_top = dev_priv->mm.stolen_base + ggtt->stolen_size;

	*base = (reg_val & G4X_STOLEN_RESERVED_ADDR2_MASK) << 16;

	WARN_ON((reg_val & G4X_STOLEN_RESERVED_ADDR1_MASK) < *base);

	/* On these platforms, the register doesn't have a size field, so the
	 * size is the distance between the base and the top of the stolen
	 * memory. We also have the genuine case where base is zero and there's
	 * nothing reserved. */
	if (*base == 0)
		*size = 0;
	else
		*size = stolen_top - *base;
}

static void gen6_get_stolen_reserved(struct drm_i915_private *dev_priv,
				     dma_addr_t *base, u32 *size)
{
	uint32_t reg_val = I915_READ(GEN6_STOLEN_RESERVED);

	*base = reg_val & GEN6_STOLEN_RESERVED_ADDR_MASK;

	switch (reg_val & GEN6_STOLEN_RESERVED_SIZE_MASK) {
	case GEN6_STOLEN_RESERVED_1M:
		*size = 1024 * 1024;
		break;
	case GEN6_STOLEN_RESERVED_512K:
		*size = 512 * 1024;
		break;
	case GEN6_STOLEN_RESERVED_256K:
		*size = 256 * 1024;
		break;
	case GEN6_STOLEN_RESERVED_128K:
		*size = 128 * 1024;
		break;
	default:
		*size = 1024 * 1024;
		MISSING_CASE(reg_val & GEN6_STOLEN_RESERVED_SIZE_MASK);
	}
}

static void gen7_get_stolen_reserved(struct drm_i915_private *dev_priv,
				     dma_addr_t *base, u32 *size)
{
	uint32_t reg_val = I915_READ(GEN6_STOLEN_RESERVED);

	*base = reg_val & GEN7_STOLEN_RESERVED_ADDR_MASK;

	switch (reg_val & GEN7_STOLEN_RESERVED_SIZE_MASK) {
	case GEN7_STOLEN_RESERVED_1M:
		*size = 1024 * 1024;
		break;
	case GEN7_STOLEN_RESERVED_256K:
		*size = 256 * 1024;
		break;
	default:
		*size = 1024 * 1024;
		MISSING_CASE(reg_val & GEN7_STOLEN_RESERVED_SIZE_MASK);
	}
}

static void chv_get_stolen_reserved(struct drm_i915_private *dev_priv,
				    dma_addr_t *base, u32 *size)
{
	uint32_t reg_val = I915_READ(GEN6_STOLEN_RESERVED);

	*base = reg_val & GEN6_STOLEN_RESERVED_ADDR_MASK;

	switch (reg_val & GEN8_STOLEN_RESERVED_SIZE_MASK) {
	case GEN8_STOLEN_RESERVED_1M:
		*size = 1024 * 1024;
		break;
	case GEN8_STOLEN_RESERVED_2M:
		*size = 2 * 1024 * 1024;
		break;
	case GEN8_STOLEN_RESERVED_4M:
		*size = 4 * 1024 * 1024;
		break;
	case GEN8_STOLEN_RESERVED_8M:
		*size = 8 * 1024 * 1024;
		break;
	default:
		*size = 8 * 1024 * 1024;
		MISSING_CASE(reg_val & GEN8_STOLEN_RESERVED_SIZE_MASK);
	}
}

static void bdw_get_stolen_reserved(struct drm_i915_private *dev_priv,
				    dma_addr_t *base, u32 *size)
{
	struct i915_ggtt *ggtt = &dev_priv->ggtt;
	uint32_t reg_val = I915_READ(GEN6_STOLEN_RESERVED);
	dma_addr_t stolen_top;

	stolen_top = dev_priv->mm.stolen_base + ggtt->stolen_size;

	*base = reg_val & GEN6_STOLEN_RESERVED_ADDR_MASK;

	/* On these platforms, the register doesn't have a size field, so the
	 * size is the distance between the base and the top of the stolen
	 * memory. We also have the genuine case where base is zero and there's
	 * nothing reserved. */
	if (*base == 0)
		*size = 0;
	else
		*size = stolen_top - *base;
}

int i915_gem_init_stolen(struct drm_i915_private *dev_priv)
{
	struct i915_ggtt *ggtt = &dev_priv->ggtt;
	dma_addr_t reserved_base, stolen_top;
	u32 reserved_total, reserved_size;
	u32 stolen_usable_start;

	mutex_init(&dev_priv->mm.stolen_lock);

	if (intel_vgpu_active(dev_priv)) {
		DRM_INFO("iGVT-g active, disabling use of stolen memory\n");
		return 0;
	}

#ifdef CONFIG_INTEL_IOMMU
	if (intel_iommu_gfx_mapped && INTEL_GEN(dev_priv) < 8) {
		DRM_INFO("DMAR active, disabling use of stolen memory\n");
		return 0;
	}
#endif

	if (ggtt->stolen_size == 0)
		return 0;

	dev_priv->mm.stolen_base = i915_stolen_to_dma(dev_priv);
	if (dev_priv->mm.stolen_base == 0)
		return 0;

	stolen_top = dev_priv->mm.stolen_base + ggtt->stolen_size;
	reserved_base = 0;
	reserved_size = 0;

	switch (INTEL_INFO(dev_priv)->gen) {
	case 2:
	case 3:
		break;
	case 4:
		if (IS_G4X(dev_priv))
			g4x_get_stolen_reserved(dev_priv,
						&reserved_base, &reserved_size);
		break;
	case 5:
		/* Assume the gen6 maximum for the older platforms. */
		reserved_size = 1024 * 1024;
		reserved_base = stolen_top - reserved_size;
		break;
	case 6:
		gen6_get_stolen_reserved(dev_priv,
					 &reserved_base, &reserved_size);
		break;
	case 7:
		gen7_get_stolen_reserved(dev_priv,
					 &reserved_base, &reserved_size);
		break;
	default:
		if (IS_LP(dev_priv))
			chv_get_stolen_reserved(dev_priv,
						&reserved_base, &reserved_size);
		else
			bdw_get_stolen_reserved(dev_priv,
						&reserved_base, &reserved_size);
		break;
	}

	/* It is possible for the reserved base to be zero, but the register
	 * field for size doesn't have a zero option. */
	if (reserved_base == 0) {
		reserved_size = 0;
		reserved_base = stolen_top;
	}

	if (reserved_base < dev_priv->mm.stolen_base ||
	    reserved_base + reserved_size > stolen_top) {
		dma_addr_t reserved_top = reserved_base + reserved_size;
		DRM_DEBUG_KMS("Stolen reserved area [%pad - %pad] outside stolen memory [%pad - %pad]\n",
			      &reserved_base, &reserved_top,
			      &dev_priv->mm.stolen_base, &stolen_top);
		return 0;
	}

	ggtt->stolen_reserved_base = reserved_base;
	ggtt->stolen_reserved_size = reserved_size;

	/* It is possible for the reserved area to end before the end of stolen
	 * memory, so just consider the start. */
	reserved_total = stolen_top - reserved_base;

	DRM_DEBUG_KMS("Memory reserved for graphics device: %uK, usable: %uK\n",
		      ggtt->stolen_size >> 10,
		      (ggtt->stolen_size - reserved_total) >> 10);

	stolen_usable_start = 0;
	/* WaSkipStolenMemoryFirstPage:bdw+ */
	if (INTEL_GEN(dev_priv) >= 8)
		stolen_usable_start = 4096;

	ggtt->stolen_usable_size =
		ggtt->stolen_size - reserved_total - stolen_usable_start;

	/* Basic memrange allocator for stolen space. */
	drm_mm_init(&dev_priv->mm.stolen, stolen_usable_start,
		    ggtt->stolen_usable_size);

	return 0;
}

static struct sg_table *
i915_pages_create_for_stolen(struct drm_device *dev,
			     u32 offset, u32 size)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct sg_table *st;
	struct scatterlist *sg;

	GEM_BUG_ON(range_overflows(offset, size, dev_priv->ggtt.stolen_size));

	/* We hide that we have no struct page backing our stolen object
	 * by wrapping the contiguous physical allocation with a fake
	 * dma mapping in a single scatterlist.
	 */

	st = kmalloc(sizeof(*st), GFP_KERNEL);
	if (st == NULL)
		return ERR_PTR(-ENOMEM);

	if (sg_alloc_table(st, 1, GFP_KERNEL)) {
		kfree(st);
		return ERR_PTR(-ENOMEM);
	}

	sg = st->sgl;
	sg->offset = 0;
	sg->length = size;

	sg_dma_address(sg) = (dma_addr_t)dev_priv->mm.stolen_base + offset;
	sg_dma_len(sg) = size;

	return st;
}

static struct sg_table *
i915_gem_object_get_pages_stolen(struct drm_i915_gem_object *obj)
{
	return i915_pages_create_for_stolen(obj->base.dev,
					    obj->stolen->start,
					    obj->stolen->size);
}

static void i915_gem_object_put_pages_stolen(struct drm_i915_gem_object *obj,
					     struct sg_table *pages)
{
	/* Should only be called from i915_gem_object_release_stolen() */
	sg_free_table(pages);
	kfree(pages);
}

static void
i915_gem_object_release_stolen(struct drm_i915_gem_object *obj)
{
	struct drm_i915_private *dev_priv = to_i915(obj->base.dev);
	struct drm_mm_node *stolen = fetch_and_zero(&obj->stolen);

	GEM_BUG_ON(!stolen);

	__i915_gem_object_unpin_pages(obj);

	i915_gem_stolen_remove_node(dev_priv, stolen);
	kfree(stolen);
}

static const struct drm_i915_gem_object_ops i915_gem_object_stolen_ops = {
	.get_pages = i915_gem_object_get_pages_stolen,
	.put_pages = i915_gem_object_put_pages_stolen,
	.release = i915_gem_object_release_stolen,
};

static struct drm_i915_gem_object *
_i915_gem_object_create_stolen(struct drm_i915_private *dev_priv,
			       struct drm_mm_node *stolen)
{
	struct drm_i915_gem_object *obj;

	obj = i915_gem_object_alloc(dev_priv);
	if (obj == NULL)
		return NULL;

	drm_gem_private_object_init(&dev_priv->drm, &obj->base, stolen->size);
	i915_gem_object_init(obj, &i915_gem_object_stolen_ops);

	obj->stolen = stolen;
	obj->base.read_domains = I915_GEM_DOMAIN_CPU | I915_GEM_DOMAIN_GTT;
	obj->cache_level = HAS_LLC(dev_priv) ? I915_CACHE_LLC : I915_CACHE_NONE;

	if (i915_gem_object_pin_pages(obj))
		goto cleanup;

	return obj;

cleanup:
	i915_gem_object_free(obj);
	return NULL;
}

struct drm_i915_gem_object *
i915_gem_object_create_stolen(struct drm_i915_private *dev_priv, u32 size)
{
	struct drm_i915_gem_object *obj;
	struct drm_mm_node *stolen;
	int ret;

	if (!drm_mm_initialized(&dev_priv->mm.stolen))
		return NULL;

	if (size == 0)
		return NULL;

	stolen = kzalloc(sizeof(*stolen), GFP_KERNEL);
	if (!stolen)
		return NULL;

	ret = i915_gem_stolen_insert_node(dev_priv, stolen, size, 4096);
	if (ret) {
		kfree(stolen);
		return NULL;
	}

	obj = _i915_gem_object_create_stolen(dev_priv, stolen);
	if (obj)
		return obj;

	i915_gem_stolen_remove_node(dev_priv, stolen);
	kfree(stolen);
	return NULL;
}

struct drm_i915_gem_object *
i915_gem_object_create_stolen_for_preallocated(struct drm_i915_private *dev_priv,
					       u32 stolen_offset,
					       u32 gtt_offset,
					       u32 size)
{
	struct i915_ggtt *ggtt = &dev_priv->ggtt;
	struct drm_i915_gem_object *obj;
	struct drm_mm_node *stolen;
	struct i915_vma *vma;
	int ret;

	if (!drm_mm_initialized(&dev_priv->mm.stolen))
		return NULL;

	lockdep_assert_held(&dev_priv->drm.struct_mutex);

	DRM_DEBUG_KMS("creating preallocated stolen object: stolen_offset=%x, gtt_offset=%x, size=%x\n",
			stolen_offset, gtt_offset, size);

	/* KISS and expect everything to be page-aligned */
	if (WARN_ON(size == 0) ||
	    WARN_ON(!IS_ALIGNED(size, I915_GTT_PAGE_SIZE)) ||
	    WARN_ON(!IS_ALIGNED(stolen_offset, I915_GTT_MIN_ALIGNMENT)))
		return NULL;

	stolen = kzalloc(sizeof(*stolen), GFP_KERNEL);
	if (!stolen)
		return NULL;

	stolen->start = stolen_offset;
	stolen->size = size;
	mutex_lock(&dev_priv->mm.stolen_lock);
	ret = drm_mm_reserve_node(&dev_priv->mm.stolen, stolen);
	mutex_unlock(&dev_priv->mm.stolen_lock);
	if (ret) {
		DRM_DEBUG_KMS("failed to allocate stolen space\n");
		kfree(stolen);
		return NULL;
	}

	obj = _i915_gem_object_create_stolen(dev_priv, stolen);
	if (obj == NULL) {
		DRM_DEBUG_KMS("failed to allocate stolen object\n");
		i915_gem_stolen_remove_node(dev_priv, stolen);
		kfree(stolen);
		return NULL;
	}

	/* Some objects just need physical mem from stolen space */
	if (gtt_offset == I915_GTT_OFFSET_NONE)
		return obj;

	ret = i915_gem_object_pin_pages(obj);
	if (ret)
		goto err;

	vma = i915_vma_instance(obj, &ggtt->base, NULL);
	if (IS_ERR(vma)) {
		ret = PTR_ERR(vma);
		goto err_pages;
	}

	/* To simplify the initialisation sequence between KMS and GTT,
	 * we allow construction of the stolen object prior to
	 * setting up the GTT space. The actual reservation will occur
	 * later.
	 */
	ret = i915_gem_gtt_reserve(&ggtt->base, &vma->node,
				   size, gtt_offset, obj->cache_level,
				   0);
	if (ret) {
		DRM_DEBUG_KMS("failed to allocate stolen GTT space\n");
		goto err_pages;
	}

	GEM_BUG_ON(!drm_mm_node_allocated(&vma->node));

	vma->pages = obj->mm.pages;
	vma->flags |= I915_VMA_GLOBAL_BIND;
	__i915_vma_set_map_and_fenceable(vma);
	list_move_tail(&vma->vm_link, &ggtt->base.inactive_list);
	list_move_tail(&obj->global_link, &dev_priv->mm.bound_list);
	obj->bind_count++;

	return obj;

err_pages:
	i915_gem_object_unpin_pages(obj);
err:
	i915_gem_object_put(obj);
	return NULL;
}
