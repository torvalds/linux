/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2008-2012 Intel Corporation
 */

#include <linux/errno.h>
#include <linux/mutex.h>

#include <drm/drm_mm.h>
#include <drm/i915_drm.h>

#include "i915_drv.h"
#include "i915_gem_stolen.h"

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

	/* WaSkipStolenMemoryFirstPage:bdw+ */
	if (INTEL_GEN(dev_priv) >= 8 && start < 4096)
		start = 4096;

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

static int i915_adjust_stolen(struct drm_i915_private *dev_priv,
			      struct resource *dsm)
{
	struct i915_ggtt *ggtt = &dev_priv->ggtt;
	struct resource *r;

	if (dsm->start == 0 || dsm->end <= dsm->start)
		return -EINVAL;

	/*
	 * TODO: We have yet too encounter the case where the GTT wasn't at the
	 * end of stolen. With that assumption we could simplify this.
	 */

	/* Make sure we don't clobber the GTT if it's within stolen memory */
	if (INTEL_GEN(dev_priv) <= 4 &&
	    !IS_G33(dev_priv) && !IS_PINEVIEW(dev_priv) && !IS_G4X(dev_priv)) {
		struct resource stolen[2] = {*dsm, *dsm};
		struct resource ggtt_res;
		resource_size_t ggtt_start;

		ggtt_start = I915_READ(PGTBL_CTL);
		if (IS_GEN(dev_priv, 4))
			ggtt_start = (ggtt_start & PGTBL_ADDRESS_LO_MASK) |
				     (ggtt_start & PGTBL_ADDRESS_HI_MASK) << 28;
		else
			ggtt_start &= PGTBL_ADDRESS_LO_MASK;

		ggtt_res =
			(struct resource) DEFINE_RES_MEM(ggtt_start,
							 ggtt_total_entries(ggtt) * 4);

		if (ggtt_res.start >= stolen[0].start && ggtt_res.start < stolen[0].end)
			stolen[0].end = ggtt_res.start;
		if (ggtt_res.end > stolen[1].start && ggtt_res.end <= stolen[1].end)
			stolen[1].start = ggtt_res.end;

		/* Pick the larger of the two chunks */
		if (resource_size(&stolen[0]) > resource_size(&stolen[1]))
			*dsm = stolen[0];
		else
			*dsm = stolen[1];

		if (stolen[0].start != stolen[1].start ||
		    stolen[0].end != stolen[1].end) {
			DRM_DEBUG_DRIVER("GTT within stolen memory at %pR\n", &ggtt_res);
			DRM_DEBUG_DRIVER("Stolen memory adjusted to %pR\n", dsm);
		}
	}

	/*
	 * Verify that nothing else uses this physical address. Stolen
	 * memory should be reserved by the BIOS and hidden from the
	 * kernel. So if the region is already marked as busy, something
	 * is seriously wrong.
	 */
	r = devm_request_mem_region(dev_priv->drm.dev, dsm->start,
				    resource_size(dsm),
				    "Graphics Stolen Memory");
	if (r == NULL) {
		/*
		 * One more attempt but this time requesting region from
		 * start + 1, as we have seen that this resolves the region
		 * conflict with the PCI Bus.
		 * This is a BIOS w/a: Some BIOS wrap stolen in the root
		 * PCI bus, but have an off-by-one error. Hence retry the
		 * reservation starting from 1 instead of 0.
		 * There's also BIOS with off-by-one on the other end.
		 */
		r = devm_request_mem_region(dev_priv->drm.dev, dsm->start + 1,
					    resource_size(dsm) - 2,
					    "Graphics Stolen Memory");
		/*
		 * GEN3 firmware likes to smash pci bridges into the stolen
		 * range. Apparently this works.
		 */
		if (r == NULL && !IS_GEN(dev_priv, 3)) {
			DRM_ERROR("conflict detected with stolen region: %pR\n",
				  dsm);

			return -EBUSY;
		}
	}

	return 0;
}

void i915_gem_cleanup_stolen(struct drm_i915_private *dev_priv)
{
	if (!drm_mm_initialized(&dev_priv->mm.stolen))
		return;

	drm_mm_takedown(&dev_priv->mm.stolen);
}

static void g4x_get_stolen_reserved(struct drm_i915_private *dev_priv,
				    resource_size_t *base,
				    resource_size_t *size)
{
	u32 reg_val = I915_READ(IS_GM45(dev_priv) ?
				CTG_STOLEN_RESERVED :
				ELK_STOLEN_RESERVED);
	resource_size_t stolen_top = dev_priv->dsm.end + 1;

	DRM_DEBUG_DRIVER("%s_STOLEN_RESERVED = %08x\n",
			 IS_GM45(dev_priv) ? "CTG" : "ELK", reg_val);

	if ((reg_val & G4X_STOLEN_RESERVED_ENABLE) == 0)
		return;

	/*
	 * Whether ILK really reuses the ELK register for this is unclear.
	 * Let's see if we catch anyone with this supposedly enabled on ILK.
	 */
	WARN(IS_GEN(dev_priv, 5), "ILK stolen reserved found? 0x%08x\n",
	     reg_val);

	if (!(reg_val & G4X_STOLEN_RESERVED_ADDR2_MASK))
		return;

	*base = (reg_val & G4X_STOLEN_RESERVED_ADDR2_MASK) << 16;
	WARN_ON((reg_val & G4X_STOLEN_RESERVED_ADDR1_MASK) < *base);

	*size = stolen_top - *base;
}

static void gen6_get_stolen_reserved(struct drm_i915_private *dev_priv,
				     resource_size_t *base,
				     resource_size_t *size)
{
	u32 reg_val = I915_READ(GEN6_STOLEN_RESERVED);

	DRM_DEBUG_DRIVER("GEN6_STOLEN_RESERVED = %08x\n", reg_val);

	if (!(reg_val & GEN6_STOLEN_RESERVED_ENABLE))
		return;

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

static void vlv_get_stolen_reserved(struct drm_i915_private *dev_priv,
				    resource_size_t *base,
				    resource_size_t *size)
{
	u32 reg_val = I915_READ(GEN6_STOLEN_RESERVED);
	resource_size_t stolen_top = dev_priv->dsm.end + 1;

	DRM_DEBUG_DRIVER("GEN6_STOLEN_RESERVED = %08x\n", reg_val);

	if (!(reg_val & GEN6_STOLEN_RESERVED_ENABLE))
		return;

	switch (reg_val & GEN7_STOLEN_RESERVED_SIZE_MASK) {
	default:
		MISSING_CASE(reg_val & GEN7_STOLEN_RESERVED_SIZE_MASK);
		/* fall through */
	case GEN7_STOLEN_RESERVED_1M:
		*size = 1024 * 1024;
		break;
	}

	/*
	 * On vlv, the ADDR_MASK portion is left as 0 and HW deduces the
	 * reserved location as (top - size).
	 */
	*base = stolen_top - *size;
}

static void gen7_get_stolen_reserved(struct drm_i915_private *dev_priv,
				     resource_size_t *base,
				     resource_size_t *size)
{
	u32 reg_val = I915_READ(GEN6_STOLEN_RESERVED);

	DRM_DEBUG_DRIVER("GEN6_STOLEN_RESERVED = %08x\n", reg_val);

	if (!(reg_val & GEN6_STOLEN_RESERVED_ENABLE))
		return;

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
				    resource_size_t *base,
				    resource_size_t *size)
{
	u32 reg_val = I915_READ(GEN6_STOLEN_RESERVED);

	DRM_DEBUG_DRIVER("GEN6_STOLEN_RESERVED = %08x\n", reg_val);

	if (!(reg_val & GEN6_STOLEN_RESERVED_ENABLE))
		return;

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
				    resource_size_t *base,
				    resource_size_t *size)
{
	u32 reg_val = I915_READ(GEN6_STOLEN_RESERVED);
	resource_size_t stolen_top = dev_priv->dsm.end + 1;

	DRM_DEBUG_DRIVER("GEN6_STOLEN_RESERVED = %08x\n", reg_val);

	if (!(reg_val & GEN6_STOLEN_RESERVED_ENABLE))
		return;

	if (!(reg_val & GEN6_STOLEN_RESERVED_ADDR_MASK))
		return;

	*base = reg_val & GEN6_STOLEN_RESERVED_ADDR_MASK;
	*size = stolen_top - *base;
}

static void icl_get_stolen_reserved(struct drm_i915_private *i915,
				    resource_size_t *base,
				    resource_size_t *size)
{
	u64 reg_val = intel_uncore_read64(&i915->uncore, GEN6_STOLEN_RESERVED);

	DRM_DEBUG_DRIVER("GEN6_STOLEN_RESERVED = 0x%016llx\n", reg_val);

	*base = reg_val & GEN11_STOLEN_RESERVED_ADDR_MASK;

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

int i915_gem_init_stolen(struct drm_i915_private *dev_priv)
{
	resource_size_t reserved_base, stolen_top;
	resource_size_t reserved_total, reserved_size;

	mutex_init(&dev_priv->mm.stolen_lock);

	if (intel_vgpu_active(dev_priv)) {
		dev_notice(dev_priv->drm.dev,
			   "%s, disabling use of stolen memory\n",
			   "iGVT-g active");
		return 0;
	}

	if (intel_vtd_active() && INTEL_GEN(dev_priv) < 8) {
		dev_notice(dev_priv->drm.dev,
			   "%s, disabling use of stolen memory\n",
			   "DMAR active");
		return 0;
	}

	if (resource_size(&intel_graphics_stolen_res) == 0)
		return 0;

	dev_priv->dsm = intel_graphics_stolen_res;

	if (i915_adjust_stolen(dev_priv, &dev_priv->dsm))
		return 0;

	GEM_BUG_ON(dev_priv->dsm.start == 0);
	GEM_BUG_ON(dev_priv->dsm.end <= dev_priv->dsm.start);

	stolen_top = dev_priv->dsm.end + 1;
	reserved_base = stolen_top;
	reserved_size = 0;

	switch (INTEL_GEN(dev_priv)) {
	case 2:
	case 3:
		break;
	case 4:
		if (!IS_G4X(dev_priv))
			break;
		/* fall through */
	case 5:
		g4x_get_stolen_reserved(dev_priv,
					&reserved_base, &reserved_size);
		break;
	case 6:
		gen6_get_stolen_reserved(dev_priv,
					 &reserved_base, &reserved_size);
		break;
	case 7:
		if (IS_VALLEYVIEW(dev_priv))
			vlv_get_stolen_reserved(dev_priv,
						&reserved_base, &reserved_size);
		else
			gen7_get_stolen_reserved(dev_priv,
						 &reserved_base, &reserved_size);
		break;
	case 8:
	case 9:
	case 10:
		if (IS_LP(dev_priv))
			chv_get_stolen_reserved(dev_priv,
						&reserved_base, &reserved_size);
		else
			bdw_get_stolen_reserved(dev_priv,
						&reserved_base, &reserved_size);
		break;
	case 11:
	default:
		icl_get_stolen_reserved(dev_priv, &reserved_base,
					&reserved_size);
		break;
	}

	/*
	 * Our expectation is that the reserved space is at the top of the
	 * stolen region and *never* at the bottom. If we see !reserved_base,
	 * it likely means we failed to read the registers correctly.
	 */
	if (!reserved_base) {
		DRM_ERROR("inconsistent reservation %pa + %pa; ignoring\n",
			  &reserved_base, &reserved_size);
		reserved_base = stolen_top;
		reserved_size = 0;
	}

	dev_priv->dsm_reserved =
		(struct resource) DEFINE_RES_MEM(reserved_base, reserved_size);

	if (!resource_contains(&dev_priv->dsm, &dev_priv->dsm_reserved)) {
		DRM_ERROR("Stolen reserved area %pR outside stolen memory %pR\n",
			  &dev_priv->dsm_reserved, &dev_priv->dsm);
		return 0;
	}

	/* It is possible for the reserved area to end before the end of stolen
	 * memory, so just consider the start. */
	reserved_total = stolen_top - reserved_base;

	DRM_DEBUG_DRIVER("Memory reserved for graphics device: %lluK, usable: %lluK\n",
			 (u64)resource_size(&dev_priv->dsm) >> 10,
			 ((u64)resource_size(&dev_priv->dsm) - reserved_total) >> 10);

	dev_priv->stolen_usable_size =
		resource_size(&dev_priv->dsm) - reserved_total;

	/* Basic memrange allocator for stolen space. */
	drm_mm_init(&dev_priv->mm.stolen, 0, dev_priv->stolen_usable_size);

	return 0;
}

static struct sg_table *
i915_pages_create_for_stolen(struct drm_device *dev,
			     resource_size_t offset, resource_size_t size)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct sg_table *st;
	struct scatterlist *sg;

	GEM_BUG_ON(range_overflows(offset, size, resource_size(&dev_priv->dsm)));

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

	sg_dma_address(sg) = (dma_addr_t)dev_priv->dsm.start + offset;
	sg_dma_len(sg) = size;

	return st;
}

static int i915_gem_object_get_pages_stolen(struct drm_i915_gem_object *obj)
{
	struct sg_table *pages =
		i915_pages_create_for_stolen(obj->base.dev,
					     obj->stolen->start,
					     obj->stolen->size);
	if (IS_ERR(pages))
		return PTR_ERR(pages);

	__i915_gem_object_set_pages(obj, pages, obj->stolen->size);

	return 0;
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
	unsigned int cache_level;

	obj = i915_gem_object_alloc();
	if (obj == NULL)
		return NULL;

	drm_gem_private_object_init(&dev_priv->drm, &obj->base, stolen->size);
	i915_gem_object_init(obj, &i915_gem_object_stolen_ops);

	obj->stolen = stolen;
	obj->read_domains = I915_GEM_DOMAIN_CPU | I915_GEM_DOMAIN_GTT;
	cache_level = HAS_LLC(dev_priv) ? I915_CACHE_LLC : I915_CACHE_NONE;
	i915_gem_object_set_cache_coherency(obj, cache_level);

	if (i915_gem_object_pin_pages(obj))
		goto cleanup;

	return obj;

cleanup:
	i915_gem_object_free(obj);
	return NULL;
}

struct drm_i915_gem_object *
i915_gem_object_create_stolen(struct drm_i915_private *dev_priv,
			      resource_size_t size)
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
					       resource_size_t stolen_offset,
					       resource_size_t gtt_offset,
					       resource_size_t size)
{
	struct i915_ggtt *ggtt = &dev_priv->ggtt;
	struct drm_i915_gem_object *obj;
	struct drm_mm_node *stolen;
	struct i915_vma *vma;
	int ret;

	if (!drm_mm_initialized(&dev_priv->mm.stolen))
		return NULL;

	lockdep_assert_held(&dev_priv->drm.struct_mutex);

	DRM_DEBUG_DRIVER("creating preallocated stolen object: stolen_offset=%pa, gtt_offset=%pa, size=%pa\n",
			 &stolen_offset, &gtt_offset, &size);

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
		DRM_DEBUG_DRIVER("failed to allocate stolen space\n");
		kfree(stolen);
		return NULL;
	}

	obj = _i915_gem_object_create_stolen(dev_priv, stolen);
	if (obj == NULL) {
		DRM_DEBUG_DRIVER("failed to allocate stolen object\n");
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

	vma = i915_vma_instance(obj, &ggtt->vm, NULL);
	if (IS_ERR(vma)) {
		ret = PTR_ERR(vma);
		goto err_pages;
	}

	/* To simplify the initialisation sequence between KMS and GTT,
	 * we allow construction of the stolen object prior to
	 * setting up the GTT space. The actual reservation will occur
	 * later.
	 */
	ret = i915_gem_gtt_reserve(&ggtt->vm, &vma->node,
				   size, gtt_offset, obj->cache_level,
				   0);
	if (ret) {
		DRM_DEBUG_DRIVER("failed to allocate stolen GTT space\n");
		goto err_pages;
	}

	GEM_BUG_ON(!drm_mm_node_allocated(&vma->node));

	vma->pages = obj->mm.pages;
	vma->flags |= I915_VMA_GLOBAL_BIND;
	__i915_vma_set_map_and_fenceable(vma);

	mutex_lock(&ggtt->vm.mutex);
	list_move_tail(&vma->vm_link, &ggtt->vm.bound_list);
	mutex_unlock(&ggtt->vm.mutex);

	GEM_BUG_ON(i915_gem_object_is_shrinkable(obj));
	atomic_inc(&obj->bind_count);

	return obj;

err_pages:
	i915_gem_object_unpin_pages(obj);
err:
	i915_gem_object_put(obj);
	return NULL;
}
