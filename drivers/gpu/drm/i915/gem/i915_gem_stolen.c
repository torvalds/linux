/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2008-2012 Intel Corporation
 */

#include <linux/errno.h>
#include <linux/mutex.h>

#include <drm/drm_mm.h>
#include <drm/intel/i915_drm.h>

#include "gem/i915_gem_lmem.h"
#include "gem/i915_gem_region.h"
#include "gt/intel_gt.h"
#include "gt/intel_gt_mcr.h"
#include "gt/intel_gt_regs.h"
#include "gt/intel_region_lmem.h"
#include "i915_drv.h"
#include "i915_gem_stolen.h"
#include "i915_pci.h"
#include "i915_reg.h"
#include "i915_utils.h"
#include "i915_vgpu.h"
#include "intel_mchbar_regs.h"
#include "intel_pci_config.h"

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

int i915_gem_stolen_insert_node_in_range(struct drm_i915_private *i915,
					 struct drm_mm_node *node, u64 size,
					 unsigned alignment, u64 start, u64 end)
{
	int ret;

	if (!drm_mm_initialized(&i915->mm.stolen))
		return -ENODEV;

	/* WaSkipStolenMemoryFirstPage:bdw+ */
	if (GRAPHICS_VER(i915) >= 8 && start < 4096)
		start = 4096;

	mutex_lock(&i915->mm.stolen_lock);
	ret = drm_mm_insert_node_in_range(&i915->mm.stolen, node,
					  size, alignment, 0,
					  start, end, DRM_MM_INSERT_BEST);
	mutex_unlock(&i915->mm.stolen_lock);

	return ret;
}

int i915_gem_stolen_insert_node(struct drm_i915_private *i915,
				struct drm_mm_node *node, u64 size,
				unsigned alignment)
{
	return i915_gem_stolen_insert_node_in_range(i915, node,
						    size, alignment,
						    I915_GEM_STOLEN_BIAS,
						    U64_MAX);
}

void i915_gem_stolen_remove_node(struct drm_i915_private *i915,
				 struct drm_mm_node *node)
{
	mutex_lock(&i915->mm.stolen_lock);
	drm_mm_remove_node(node);
	mutex_unlock(&i915->mm.stolen_lock);
}

static bool valid_stolen_size(struct drm_i915_private *i915, struct resource *dsm)
{
	return (dsm->start != 0 || HAS_LMEMBAR_SMEM_STOLEN(i915)) && dsm->end > dsm->start;
}

static int adjust_stolen(struct drm_i915_private *i915,
			 struct resource *dsm)
{
	struct i915_ggtt *ggtt = to_gt(i915)->ggtt;
	struct intel_uncore *uncore = ggtt->vm.gt->uncore;

	if (!valid_stolen_size(i915, dsm))
		return -EINVAL;

	/*
	 * Make sure we don't clobber the GTT if it's within stolen memory
	 *
	 * TODO: We have yet too encounter the case where the GTT wasn't at the
	 * end of stolen. With that assumption we could simplify this.
	 */
	if (GRAPHICS_VER(i915) <= 4 &&
	    !IS_G33(i915) && !IS_PINEVIEW(i915) && !IS_G4X(i915)) {
		struct resource stolen[2] = {*dsm, *dsm};
		struct resource ggtt_res;
		resource_size_t ggtt_start;

		ggtt_start = intel_uncore_read(uncore, PGTBL_CTL);
		if (GRAPHICS_VER(i915) == 4)
			ggtt_start = (ggtt_start & PGTBL_ADDRESS_LO_MASK) |
				     (ggtt_start & PGTBL_ADDRESS_HI_MASK) << 28;
		else
			ggtt_start &= PGTBL_ADDRESS_LO_MASK;

		ggtt_res = DEFINE_RES_MEM(ggtt_start, ggtt_total_entries(ggtt) * 4);

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
			drm_dbg(&i915->drm,
				"GTT within stolen memory at %pR\n",
				&ggtt_res);
			drm_dbg(&i915->drm, "Stolen memory adjusted to %pR\n",
				dsm);
		}
	}

	if (!valid_stolen_size(i915, dsm))
		return -EINVAL;

	return 0;
}

static int request_smem_stolen(struct drm_i915_private *i915,
			       struct resource *dsm)
{
	struct resource *r;

	/*
	 * With stolen lmem, we don't need to request system memory for the
	 * address range since it's local to the gpu.
	 *
	 * Starting MTL, in IGFX devices the stolen memory is exposed via
	 * LMEMBAR and shall be considered similar to stolen lmem.
	 */
	if (HAS_LMEM(i915) || HAS_LMEMBAR_SMEM_STOLEN(i915))
		return 0;

	/*
	 * Verify that nothing else uses this physical address. Stolen
	 * memory should be reserved by the BIOS and hidden from the
	 * kernel. So if the region is already marked as busy, something
	 * is seriously wrong.
	 */
	r = devm_request_mem_region(i915->drm.dev, dsm->start,
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
		r = devm_request_mem_region(i915->drm.dev, dsm->start + 1,
					    resource_size(dsm) - 2,
					    "Graphics Stolen Memory");
		/*
		 * GEN3 firmware likes to smash pci bridges into the stolen
		 * range. Apparently this works.
		 */
		if (!r && GRAPHICS_VER(i915) != 3) {
			drm_err(&i915->drm,
				"conflict detected with stolen region: %pR\n",
				dsm);

			return -EBUSY;
		}
	}

	return 0;
}

static void i915_gem_cleanup_stolen(struct drm_i915_private *i915)
{
	if (!drm_mm_initialized(&i915->mm.stolen))
		return;

	drm_mm_takedown(&i915->mm.stolen);
}

static void g4x_get_stolen_reserved(struct drm_i915_private *i915,
				    struct intel_uncore *uncore,
				    resource_size_t *base,
				    resource_size_t *size)
{
	u32 reg_val = intel_uncore_read(uncore,
					IS_GM45(i915) ?
					CTG_STOLEN_RESERVED :
					ELK_STOLEN_RESERVED);
	resource_size_t stolen_top = i915->dsm.stolen.end + 1;

	drm_dbg(&i915->drm, "%s_STOLEN_RESERVED = %08x\n",
		IS_GM45(i915) ? "CTG" : "ELK", reg_val);

	if ((reg_val & G4X_STOLEN_RESERVED_ENABLE) == 0)
		return;

	/*
	 * Whether ILK really reuses the ELK register for this is unclear.
	 * Let's see if we catch anyone with this supposedly enabled on ILK.
	 */
	drm_WARN(&i915->drm, GRAPHICS_VER(i915) == 5,
		 "ILK stolen reserved found? 0x%08x\n",
		 reg_val);

	if (!(reg_val & G4X_STOLEN_RESERVED_ADDR2_MASK))
		return;

	*base = (reg_val & G4X_STOLEN_RESERVED_ADDR2_MASK) << 16;
	drm_WARN_ON(&i915->drm,
		    (reg_val & G4X_STOLEN_RESERVED_ADDR1_MASK) < *base);

	*size = stolen_top - *base;
}

static void gen6_get_stolen_reserved(struct drm_i915_private *i915,
				     struct intel_uncore *uncore,
				     resource_size_t *base,
				     resource_size_t *size)
{
	u32 reg_val = intel_uncore_read(uncore, GEN6_STOLEN_RESERVED);

	drm_dbg(&i915->drm, "GEN6_STOLEN_RESERVED = %08x\n", reg_val);

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

static void vlv_get_stolen_reserved(struct drm_i915_private *i915,
				    struct intel_uncore *uncore,
				    resource_size_t *base,
				    resource_size_t *size)
{
	u32 reg_val = intel_uncore_read(uncore, GEN6_STOLEN_RESERVED);
	resource_size_t stolen_top = i915->dsm.stolen.end + 1;

	drm_dbg(&i915->drm, "GEN6_STOLEN_RESERVED = %08x\n", reg_val);

	if (!(reg_val & GEN6_STOLEN_RESERVED_ENABLE))
		return;

	switch (reg_val & GEN7_STOLEN_RESERVED_SIZE_MASK) {
	default:
		MISSING_CASE(reg_val & GEN7_STOLEN_RESERVED_SIZE_MASK);
		fallthrough;
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

static void gen7_get_stolen_reserved(struct drm_i915_private *i915,
				     struct intel_uncore *uncore,
				     resource_size_t *base,
				     resource_size_t *size)
{
	u32 reg_val = intel_uncore_read(uncore, GEN6_STOLEN_RESERVED);

	drm_dbg(&i915->drm, "GEN6_STOLEN_RESERVED = %08x\n", reg_val);

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

static void chv_get_stolen_reserved(struct drm_i915_private *i915,
				    struct intel_uncore *uncore,
				    resource_size_t *base,
				    resource_size_t *size)
{
	u32 reg_val = intel_uncore_read(uncore, GEN6_STOLEN_RESERVED);

	drm_dbg(&i915->drm, "GEN6_STOLEN_RESERVED = %08x\n", reg_val);

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

static void bdw_get_stolen_reserved(struct drm_i915_private *i915,
				    struct intel_uncore *uncore,
				    resource_size_t *base,
				    resource_size_t *size)
{
	u32 reg_val = intel_uncore_read(uncore, GEN6_STOLEN_RESERVED);
	resource_size_t stolen_top = i915->dsm.stolen.end + 1;

	drm_dbg(&i915->drm, "GEN6_STOLEN_RESERVED = %08x\n", reg_val);

	if (!(reg_val & GEN6_STOLEN_RESERVED_ENABLE))
		return;

	if (!(reg_val & GEN6_STOLEN_RESERVED_ADDR_MASK))
		return;

	*base = reg_val & GEN6_STOLEN_RESERVED_ADDR_MASK;
	*size = stolen_top - *base;
}

static void icl_get_stolen_reserved(struct drm_i915_private *i915,
				    struct intel_uncore *uncore,
				    resource_size_t *base,
				    resource_size_t *size)
{
	u64 reg_val = intel_uncore_read64(uncore, GEN6_STOLEN_RESERVED);

	drm_dbg(&i915->drm, "GEN6_STOLEN_RESERVED = 0x%016llx\n", reg_val);

	/* Wa_14019821291 */
	if (MEDIA_VER_FULL(i915) == IP_VER(13, 0)) {
		/*
		 * This workaround is primarily implemented by the BIOS.  We
		 * just need to figure out whether the BIOS has applied the
		 * workaround (meaning the programmed address falls within
		 * the DSM) and, if so, reserve that part of the DSM to
		 * prevent accidental reuse.  The DSM location should be just
		 * below the WOPCM.
		 */
		u64 gscpsmi_base = intel_uncore_read64_2x32(uncore,
							    MTL_GSCPSMI_BASEADDR_LSB,
							    MTL_GSCPSMI_BASEADDR_MSB);
		if (gscpsmi_base >= i915->dsm.stolen.start &&
		    gscpsmi_base < i915->dsm.stolen.end) {
			*base = gscpsmi_base;
			*size = i915->dsm.stolen.end - gscpsmi_base;
			return;
		}
	}

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

	if (HAS_LMEMBAR_SMEM_STOLEN(i915))
		/* the base is initialized to stolen top so subtract size to get base */
		*base -= *size;
	else
		*base = reg_val & GEN11_STOLEN_RESERVED_ADDR_MASK;
}

/*
 * Initialize i915->dsm.reserved to contain the reserved space within the Data
 * Stolen Memory. This is a range on the top of DSM that is reserved, not to
 * be used by driver, so must be excluded from the region passed to the
 * allocator later. In the spec this is also called as WOPCM.
 *
 * Our expectation is that the reserved space is at the top of the stolen
 * region, as it has been the case for every platform, and *never* at the
 * bottom, so the calculation here can be simplified.
 */
static int init_reserved_stolen(struct drm_i915_private *i915)
{
	struct intel_uncore *uncore = &i915->uncore;
	resource_size_t reserved_base, stolen_top;
	resource_size_t reserved_size;
	int ret = 0;

	stolen_top = i915->dsm.stolen.end + 1;
	reserved_base = stolen_top;
	reserved_size = 0;

	if (GRAPHICS_VER(i915) >= 11) {
		icl_get_stolen_reserved(i915, uncore,
					&reserved_base, &reserved_size);
	} else if (GRAPHICS_VER(i915) >= 8) {
		if (IS_LP(i915))
			chv_get_stolen_reserved(i915, uncore,
						&reserved_base, &reserved_size);
		else
			bdw_get_stolen_reserved(i915, uncore,
						&reserved_base, &reserved_size);
	} else if (GRAPHICS_VER(i915) >= 7) {
		if (IS_VALLEYVIEW(i915))
			vlv_get_stolen_reserved(i915, uncore,
						&reserved_base, &reserved_size);
		else
			gen7_get_stolen_reserved(i915, uncore,
						 &reserved_base, &reserved_size);
	} else if (GRAPHICS_VER(i915) >= 6) {
		gen6_get_stolen_reserved(i915, uncore,
					 &reserved_base, &reserved_size);
	} else if (GRAPHICS_VER(i915) >= 5 || IS_G4X(i915)) {
		g4x_get_stolen_reserved(i915, uncore,
					&reserved_base, &reserved_size);
	}

	/* No reserved stolen */
	if (reserved_base == stolen_top)
		goto bail_out;

	if (!reserved_base) {
		drm_err(&i915->drm,
			"inconsistent reservation %pa + %pa; ignoring\n",
			&reserved_base, &reserved_size);
		ret = -EINVAL;
		goto bail_out;
	}

	i915->dsm.reserved = DEFINE_RES_MEM(reserved_base, reserved_size);

	if (!resource_contains(&i915->dsm.stolen, &i915->dsm.reserved)) {
		drm_err(&i915->drm,
			"Stolen reserved area %pR outside stolen memory %pR\n",
			&i915->dsm.reserved, &i915->dsm.stolen);
		ret = -EINVAL;
		goto bail_out;
	}

	return 0;

bail_out:
	i915->dsm.reserved = DEFINE_RES_MEM(reserved_base, 0);

	return ret;
}

static int i915_gem_init_stolen(struct intel_memory_region *mem)
{
	struct drm_i915_private *i915 = mem->i915;

	mutex_init(&i915->mm.stolen_lock);

	if (intel_vgpu_active(i915)) {
		drm_notice(&i915->drm,
			   "%s, disabling use of stolen memory\n",
			   "iGVT-g active");
		return -ENOSPC;
	}

	if (i915_vtd_active(i915) && GRAPHICS_VER(i915) < 8) {
		drm_notice(&i915->drm,
			   "%s, disabling use of stolen memory\n",
			   "DMAR active");
		return -ENOSPC;
	}

	if (adjust_stolen(i915, &mem->region))
		return -ENOSPC;

	if (request_smem_stolen(i915, &mem->region))
		return -ENOSPC;

	i915->dsm.stolen = mem->region;

	if (init_reserved_stolen(i915))
		return -ENOSPC;

	/* Exclude the reserved region from driver use */
	mem->region.end = i915->dsm.reserved.start - 1;
	mem->io = DEFINE_RES_MEM(mem->io.start,
				 min(resource_size(&mem->io),
				     resource_size(&mem->region)));

	i915->dsm.usable_size = resource_size(&mem->region);

	drm_dbg(&i915->drm,
		"Memory reserved for graphics device: %lluK, usable: %lluK\n",
		(u64)resource_size(&i915->dsm.stolen) >> 10,
		(u64)i915->dsm.usable_size >> 10);

	if (i915->dsm.usable_size == 0)
		return -ENOSPC;

	/* Basic memrange allocator for stolen space. */
	drm_mm_init(&i915->mm.stolen, 0, i915->dsm.usable_size);

	/*
	 * Access to stolen lmem beyond certain size for MTL A0 stepping
	 * would crash the machine. Disable stolen lmem for userspace access
	 * by setting usable_size to zero.
	 */
	if (IS_METEORLAKE(i915) && INTEL_REVID(i915) == 0x0)
		i915->dsm.usable_size = 0;

	return 0;
}

static void dbg_poison(struct i915_ggtt *ggtt,
		       dma_addr_t addr, resource_size_t size,
		       u8 x)
{
#if IS_ENABLED(CONFIG_DRM_I915_DEBUG_GEM)
	if (!drm_mm_node_allocated(&ggtt->error_capture))
		return;

	if (ggtt->vm.bind_async_flags & I915_VMA_GLOBAL_BIND)
		return; /* beware stop_machine() inversion */

	GEM_BUG_ON(!IS_ALIGNED(size, PAGE_SIZE));

	mutex_lock(&ggtt->error_mutex);
	while (size) {
		void __iomem *s;

		ggtt->vm.insert_page(&ggtt->vm, addr,
				     ggtt->error_capture.start,
				     i915_gem_get_pat_index(ggtt->vm.i915,
							    I915_CACHE_NONE),
				     0);
		mb();

		s = io_mapping_map_wc(&ggtt->iomap,
				      ggtt->error_capture.start,
				      PAGE_SIZE);
		memset_io(s, x, PAGE_SIZE);
		io_mapping_unmap(s);

		addr += PAGE_SIZE;
		size -= PAGE_SIZE;
	}
	mb();
	ggtt->vm.clear_range(&ggtt->vm, ggtt->error_capture.start, PAGE_SIZE);
	mutex_unlock(&ggtt->error_mutex);
#endif
}

static struct sg_table *
i915_pages_create_for_stolen(struct drm_device *dev,
			     resource_size_t offset, resource_size_t size)
{
	struct drm_i915_private *i915 = to_i915(dev);
	struct sg_table *st;
	struct scatterlist *sg;

	GEM_BUG_ON(range_overflows(offset, size, resource_size(&i915->dsm.stolen)));

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

	sg_dma_address(sg) = (dma_addr_t)i915->dsm.stolen.start + offset;
	sg_dma_len(sg) = size;

	return st;
}

static int i915_gem_object_get_pages_stolen(struct drm_i915_gem_object *obj)
{
	struct drm_i915_private *i915 = to_i915(obj->base.dev);
	struct sg_table *pages =
		i915_pages_create_for_stolen(obj->base.dev,
					     obj->stolen->start,
					     obj->stolen->size);
	if (IS_ERR(pages))
		return PTR_ERR(pages);

	dbg_poison(to_gt(i915)->ggtt,
		   sg_dma_address(pages->sgl),
		   sg_dma_len(pages->sgl),
		   POISON_INUSE);

	__i915_gem_object_set_pages(obj, pages);

	return 0;
}

static void i915_gem_object_put_pages_stolen(struct drm_i915_gem_object *obj,
					     struct sg_table *pages)
{
	struct drm_i915_private *i915 = to_i915(obj->base.dev);
	/* Should only be called from i915_gem_object_release_stolen() */

	dbg_poison(to_gt(i915)->ggtt,
		   sg_dma_address(pages->sgl),
		   sg_dma_len(pages->sgl),
		   POISON_FREE);

	sg_free_table(pages);
	kfree(pages);
}

static void
i915_gem_object_release_stolen(struct drm_i915_gem_object *obj)
{
	struct drm_i915_private *i915 = to_i915(obj->base.dev);
	struct drm_mm_node *stolen = fetch_and_zero(&obj->stolen);

	GEM_BUG_ON(!stolen);
	i915_gem_stolen_remove_node(i915, stolen);
	kfree(stolen);

	i915_gem_object_release_memory_region(obj);
}

static const struct drm_i915_gem_object_ops i915_gem_object_stolen_ops = {
	.name = "i915_gem_object_stolen",
	.get_pages = i915_gem_object_get_pages_stolen,
	.put_pages = i915_gem_object_put_pages_stolen,
	.release = i915_gem_object_release_stolen,
};

static int __i915_gem_object_create_stolen(struct intel_memory_region *mem,
					   struct drm_i915_gem_object *obj,
					   struct drm_mm_node *stolen)
{
	static struct lock_class_key lock_class;
	unsigned int cache_level;
	unsigned int flags;
	int err;

	/*
	 * Stolen objects are always physically contiguous since we just
	 * allocate one big block underneath using the drm_mm range allocator.
	 */
	flags = I915_BO_ALLOC_CONTIGUOUS;

	drm_gem_private_object_init(&mem->i915->drm, &obj->base, stolen->size);
	i915_gem_object_init(obj, &i915_gem_object_stolen_ops, &lock_class, flags);

	obj->stolen = stolen;
	obj->read_domains = I915_GEM_DOMAIN_CPU | I915_GEM_DOMAIN_GTT;
	cache_level = HAS_LLC(mem->i915) ? I915_CACHE_LLC : I915_CACHE_NONE;
	i915_gem_object_set_cache_coherency(obj, cache_level);

	if (WARN_ON(!i915_gem_object_trylock(obj, NULL)))
		return -EBUSY;

	i915_gem_object_init_memory_region(obj, mem);

	err = i915_gem_object_pin_pages(obj);
	if (err)
		i915_gem_object_release_memory_region(obj);
	i915_gem_object_unlock(obj);

	return err;
}

static int _i915_gem_object_stolen_init(struct intel_memory_region *mem,
					struct drm_i915_gem_object *obj,
					resource_size_t offset,
					resource_size_t size,
					resource_size_t page_size,
					unsigned int flags)
{
	struct drm_i915_private *i915 = mem->i915;
	struct drm_mm_node *stolen;
	int ret;

	if (!drm_mm_initialized(&i915->mm.stolen))
		return -ENODEV;

	if (size == 0)
		return -EINVAL;

	/*
	 * With discrete devices, where we lack a mappable aperture there is no
	 * possible way to ever access this memory on the CPU side.
	 */
	if (mem->type == INTEL_MEMORY_STOLEN_LOCAL && !resource_size(&mem->io) &&
	    !(flags & I915_BO_ALLOC_GPU_ONLY))
		return -ENOSPC;

	stolen = kzalloc(sizeof(*stolen), GFP_KERNEL);
	if (!stolen)
		return -ENOMEM;

	if (offset != I915_BO_INVALID_OFFSET) {
		drm_dbg(&i915->drm,
			"creating preallocated stolen object: stolen_offset=%pa, size=%pa\n",
			&offset, &size);

		stolen->start = offset;
		stolen->size = size;
		mutex_lock(&i915->mm.stolen_lock);
		ret = drm_mm_reserve_node(&i915->mm.stolen, stolen);
		mutex_unlock(&i915->mm.stolen_lock);
	} else {
		ret = i915_gem_stolen_insert_node(i915, stolen, size,
						  mem->min_page_size);
	}
	if (ret)
		goto err_free;

	ret = __i915_gem_object_create_stolen(mem, obj, stolen);
	if (ret)
		goto err_remove;

	return 0;

err_remove:
	i915_gem_stolen_remove_node(i915, stolen);
err_free:
	kfree(stolen);
	return ret;
}

struct drm_i915_gem_object *
i915_gem_object_create_stolen(struct drm_i915_private *i915,
			      resource_size_t size)
{
	return i915_gem_object_create_region(i915->mm.stolen_region, size, 0, 0);
}

static int init_stolen_smem(struct intel_memory_region *mem)
{
	int err;

	/*
	 * Initialise stolen early so that we may reserve preallocated
	 * objects for the BIOS to KMS transition.
	 */
	err = i915_gem_init_stolen(mem);
	if (err)
		drm_dbg(&mem->i915->drm, "Skip stolen region: failed to setup\n");

	return 0;
}

static int release_stolen_smem(struct intel_memory_region *mem)
{
	i915_gem_cleanup_stolen(mem->i915);
	return 0;
}

static const struct intel_memory_region_ops i915_region_stolen_smem_ops = {
	.init = init_stolen_smem,
	.release = release_stolen_smem,
	.init_object = _i915_gem_object_stolen_init,
};

static int init_stolen_lmem(struct intel_memory_region *mem)
{
	int err;

	if (GEM_WARN_ON(resource_size(&mem->region) == 0))
		return 0;

	err = i915_gem_init_stolen(mem);
	if (err) {
		drm_dbg(&mem->i915->drm, "Skip stolen region: failed to setup\n");
		return 0;
	}

	if (resource_size(&mem->io) &&
	    !io_mapping_init_wc(&mem->iomap, mem->io.start, resource_size(&mem->io)))
		goto err_cleanup;

	return 0;

err_cleanup:
	i915_gem_cleanup_stolen(mem->i915);
	return err;
}

static int release_stolen_lmem(struct intel_memory_region *mem)
{
	if (resource_size(&mem->io))
		io_mapping_fini(&mem->iomap);
	i915_gem_cleanup_stolen(mem->i915);
	return 0;
}

static const struct intel_memory_region_ops i915_region_stolen_lmem_ops = {
	.init = init_stolen_lmem,
	.release = release_stolen_lmem,
	.init_object = _i915_gem_object_stolen_init,
};

static int mtl_get_gms_size(struct intel_uncore *uncore)
{
	u16 ggc, gms;

	ggc = intel_uncore_read16(uncore, GGC);

	/* check GGMS, should be fixed 0x3 (8MB) */
	if ((ggc & GGMS_MASK) != GGMS_MASK)
		return -EIO;

	/* return valid GMS value, -EIO if invalid */
	gms = REG_FIELD_GET(GMS_MASK, ggc);
	switch (gms) {
	case 0x0 ... 0x04:
		return gms * 32;
	case 0xf0 ... 0xfe:
		return (gms - 0xf0 + 1) * 4;
	default:
		MISSING_CASE(gms);
		return -EIO;
	}
}

struct intel_memory_region *
i915_gem_stolen_lmem_setup(struct drm_i915_private *i915, u16 type,
			   u16 instance)
{
	struct intel_uncore *uncore = &i915->uncore;
	struct pci_dev *pdev = to_pci_dev(i915->drm.dev);
	resource_size_t dsm_size, dsm_base, lmem_size;
	struct intel_memory_region *mem;
	resource_size_t io_start, io_size;
	resource_size_t min_page_size;
	int ret;

	if (WARN_ON_ONCE(instance))
		return ERR_PTR(-ENODEV);

	if (!i915_pci_resource_valid(pdev, GEN12_LMEM_BAR))
		return ERR_PTR(-ENXIO);

	if (HAS_LMEMBAR_SMEM_STOLEN(i915) || IS_DG1(i915)) {
		lmem_size = pci_resource_len(pdev, GEN12_LMEM_BAR);
	} else {
		resource_size_t lmem_range;

		lmem_range = intel_gt_mcr_read_any(to_gt(i915), XEHP_TILE0_ADDR_RANGE) & 0xFFFF;
		lmem_size = lmem_range >> XEHP_TILE_LMEM_RANGE_SHIFT;
		lmem_size *= SZ_1G;
	}

	if (HAS_LMEMBAR_SMEM_STOLEN(i915)) {
		/*
		 * MTL dsm size is in GGC register.
		 * Also MTL uses offset to GSMBASE in ptes, so i915
		 * uses dsm_base = 8MBs to setup stolen region, since
		 * DSMBASE = GSMBASE + 8MB.
		 */
		ret = mtl_get_gms_size(uncore);
		if (ret < 0) {
			drm_err(&i915->drm, "invalid MTL GGC register setting\n");
			return ERR_PTR(ret);
		}

		dsm_base = SZ_8M;
		dsm_size = (resource_size_t)(ret * SZ_1M);

		GEM_BUG_ON(pci_resource_len(pdev, GEN12_LMEM_BAR) != SZ_256M);
		GEM_BUG_ON((dsm_base + dsm_size) > lmem_size);
	} else {
		/* Use DSM base address instead for stolen memory */
		dsm_base = intel_uncore_read64(uncore, GEN6_DSMBASE) & GEN11_BDSM_MASK;
		if (lmem_size < dsm_base) {
			drm_dbg(&i915->drm,
				"Disabling stolen memory support due to OOB placement: lmem_size = %pa vs dsm_base = %pa\n",
				&lmem_size, &dsm_base);
			return NULL;
		}
		dsm_size = ALIGN_DOWN(lmem_size - dsm_base, SZ_1M);
	}

	if (i915_direct_stolen_access(i915)) {
		drm_dbg(&i915->drm, "Using direct DSM access\n");
		io_start = intel_uncore_read64(uncore, GEN6_DSMBASE) & GEN11_BDSM_MASK;
		io_size = dsm_size;
	} else if (pci_resource_len(pdev, GEN12_LMEM_BAR) < lmem_size) {
		io_start = 0;
		io_size = 0;
	} else {
		io_start = pci_resource_start(pdev, GEN12_LMEM_BAR) + dsm_base;
		io_size = dsm_size;
	}

	min_page_size = HAS_64K_PAGES(i915) ? I915_GTT_PAGE_SIZE_64K :
						I915_GTT_PAGE_SIZE_4K;

	mem = intel_memory_region_create(i915, dsm_base, dsm_size,
					 min_page_size,
					 io_start, io_size,
					 type, instance,
					 &i915_region_stolen_lmem_ops);
	if (IS_ERR(mem))
		return mem;

	intel_memory_region_set_name(mem, "stolen-local");

	mem->private = true;

	return mem;
}

struct intel_memory_region*
i915_gem_stolen_smem_setup(struct drm_i915_private *i915, u16 type,
			   u16 instance)
{
	struct intel_memory_region *mem;

	mem = intel_memory_region_create(i915,
					 intel_graphics_stolen_res.start,
					 resource_size(&intel_graphics_stolen_res),
					 PAGE_SIZE, 0, 0, type, instance,
					 &i915_region_stolen_smem_ops);
	if (IS_ERR(mem))
		return mem;

	intel_memory_region_set_name(mem, "stolen-system");

	mem->private = true;

	return mem;
}

bool i915_gem_object_is_stolen(const struct drm_i915_gem_object *obj)
{
	return obj->ops == &i915_gem_object_stolen_ops;
}

bool i915_gem_stolen_initialized(const struct drm_i915_private *i915)
{
	return drm_mm_initialized(&i915->mm.stolen);
}

u64 i915_gem_stolen_area_address(const struct drm_i915_private *i915)
{
	return i915->dsm.stolen.start;
}

u64 i915_gem_stolen_area_size(const struct drm_i915_private *i915)
{
	return resource_size(&i915->dsm.stolen);
}

u64 i915_gem_stolen_node_address(const struct drm_i915_private *i915,
				 const struct drm_mm_node *node)
{
	return i915->dsm.stolen.start + i915_gem_stolen_node_offset(node);
}

bool i915_gem_stolen_node_allocated(const struct drm_mm_node *node)
{
	return drm_mm_node_allocated(node);
}

u64 i915_gem_stolen_node_offset(const struct drm_mm_node *node)
{
	return node->start;
}

u64 i915_gem_stolen_node_size(const struct drm_mm_node *node)
{
	return node->size;
}
