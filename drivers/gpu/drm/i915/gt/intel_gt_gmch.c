// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include <drm/intel-gtt.h>
#include <drm/i915_drm.h>

#include <linux/agp_backend.h>
#include <linux/stop_machine.h>

#include "i915_drv.h"
#include "intel_gt_gmch.h"
#include "intel_gt_regs.h"
#include "intel_gt.h"
#include "i915_utils.h"

#include "gen8_ppgtt.h"

struct insert_page {
	struct i915_address_space *vm;
	dma_addr_t addr;
	u64 offset;
	enum i915_cache_level level;
};

static void gen8_set_pte(void __iomem *addr, gen8_pte_t pte)
{
	writeq(pte, addr);
}

static void nop_clear_range(struct i915_address_space *vm,
			    u64 start, u64 length)
{
}

static u64 snb_pte_encode(dma_addr_t addr,
			  enum i915_cache_level level,
			  u32 flags)
{
	gen6_pte_t pte = GEN6_PTE_ADDR_ENCODE(addr) | GEN6_PTE_VALID;

	switch (level) {
	case I915_CACHE_L3_LLC:
	case I915_CACHE_LLC:
		pte |= GEN6_PTE_CACHE_LLC;
		break;
	case I915_CACHE_NONE:
		pte |= GEN6_PTE_UNCACHED;
		break;
	default:
		MISSING_CASE(level);
	}

	return pte;
}

static u64 ivb_pte_encode(dma_addr_t addr,
			  enum i915_cache_level level,
			  u32 flags)
{
	gen6_pte_t pte = GEN6_PTE_ADDR_ENCODE(addr) | GEN6_PTE_VALID;

	switch (level) {
	case I915_CACHE_L3_LLC:
		pte |= GEN7_PTE_CACHE_L3_LLC;
		break;
	case I915_CACHE_LLC:
		pte |= GEN6_PTE_CACHE_LLC;
		break;
	case I915_CACHE_NONE:
		pte |= GEN6_PTE_UNCACHED;
		break;
	default:
		MISSING_CASE(level);
	}

	return pte;
}

static u64 byt_pte_encode(dma_addr_t addr,
			  enum i915_cache_level level,
			  u32 flags)
{
	gen6_pte_t pte = GEN6_PTE_ADDR_ENCODE(addr) | GEN6_PTE_VALID;

	if (!(flags & PTE_READ_ONLY))
		pte |= BYT_PTE_WRITEABLE;

	if (level != I915_CACHE_NONE)
		pte |= BYT_PTE_SNOOPED_BY_CPU_CACHES;

	return pte;
}

static u64 hsw_pte_encode(dma_addr_t addr,
			  enum i915_cache_level level,
			  u32 flags)
{
	gen6_pte_t pte = HSW_PTE_ADDR_ENCODE(addr) | GEN6_PTE_VALID;

	if (level != I915_CACHE_NONE)
		pte |= HSW_WB_LLC_AGE3;

	return pte;
}

static u64 iris_pte_encode(dma_addr_t addr,
			   enum i915_cache_level level,
			   u32 flags)
{
	gen6_pte_t pte = HSW_PTE_ADDR_ENCODE(addr) | GEN6_PTE_VALID;

	switch (level) {
	case I915_CACHE_NONE:
		break;
	case I915_CACHE_WT:
		pte |= HSW_WT_ELLC_LLC_AGE3;
		break;
	default:
		pte |= HSW_WB_ELLC_LLC_AGE3;
		break;
	}

	return pte;
}

static void gen5_ggtt_insert_page(struct i915_address_space *vm,
				  dma_addr_t addr,
				  u64 offset,
				  enum i915_cache_level cache_level,
				  u32 unused)
{
	unsigned int flags = (cache_level == I915_CACHE_NONE) ?
		AGP_USER_MEMORY : AGP_USER_CACHED_MEMORY;

	intel_gtt_insert_page(addr, offset >> PAGE_SHIFT, flags);
}

static void gen6_ggtt_insert_page(struct i915_address_space *vm,
				  dma_addr_t addr,
				  u64 offset,
				  enum i915_cache_level level,
				  u32 flags)
{
	struct i915_ggtt *ggtt = i915_vm_to_ggtt(vm);
	gen6_pte_t __iomem *pte =
		(gen6_pte_t __iomem *)ggtt->gsm + offset / I915_GTT_PAGE_SIZE;

	iowrite32(vm->pte_encode(addr, level, flags), pte);

	ggtt->invalidate(ggtt);
}

static void gen8_ggtt_insert_page(struct i915_address_space *vm,
				  dma_addr_t addr,
				  u64 offset,
				  enum i915_cache_level level,
				  u32 flags)
{
	struct i915_ggtt *ggtt = i915_vm_to_ggtt(vm);
	gen8_pte_t __iomem *pte =
		(gen8_pte_t __iomem *)ggtt->gsm + offset / I915_GTT_PAGE_SIZE;

	gen8_set_pte(pte, gen8_ggtt_pte_encode(addr, level, flags));

	ggtt->invalidate(ggtt);
}

static void gen5_ggtt_insert_entries(struct i915_address_space *vm,
				     struct i915_vma_resource *vma_res,
				     enum i915_cache_level cache_level,
				     u32 unused)
{
	unsigned int flags = (cache_level == I915_CACHE_NONE) ?
		AGP_USER_MEMORY : AGP_USER_CACHED_MEMORY;

	intel_gtt_insert_sg_entries(vma_res->bi.pages, vma_res->start >> PAGE_SHIFT,
				    flags);
}

/*
 * Binds an object into the global gtt with the specified cache level.
 * The object will be accessible to the GPU via commands whose operands
 * reference offsets within the global GTT as well as accessible by the GPU
 * through the GMADR mapped BAR (i915->mm.gtt->gtt).
 */
static void gen6_ggtt_insert_entries(struct i915_address_space *vm,
				     struct i915_vma_resource *vma_res,
				     enum i915_cache_level level,
				     u32 flags)
{
	struct i915_ggtt *ggtt = i915_vm_to_ggtt(vm);
	gen6_pte_t __iomem *gte;
	gen6_pte_t __iomem *end;
	struct sgt_iter iter;
	dma_addr_t addr;

	gte = (gen6_pte_t __iomem *)ggtt->gsm;
	gte += vma_res->start / I915_GTT_PAGE_SIZE;
	end = gte + vma_res->node_size / I915_GTT_PAGE_SIZE;

	for_each_sgt_daddr(addr, iter, vma_res->bi.pages)
		iowrite32(vm->pte_encode(addr, level, flags), gte++);
	GEM_BUG_ON(gte > end);

	/* Fill the allocated but "unused" space beyond the end of the buffer */
	while (gte < end)
		iowrite32(vm->scratch[0]->encode, gte++);

	/*
	 * We want to flush the TLBs only after we're certain all the PTE
	 * updates have finished.
	 */
	ggtt->invalidate(ggtt);
}

static void gen8_ggtt_insert_entries(struct i915_address_space *vm,
				     struct i915_vma_resource *vma_res,
				     enum i915_cache_level level,
				     u32 flags)
{
	const gen8_pte_t pte_encode = gen8_ggtt_pte_encode(0, level, flags);
	struct i915_ggtt *ggtt = i915_vm_to_ggtt(vm);
	gen8_pte_t __iomem *gte;
	gen8_pte_t __iomem *end;
	struct sgt_iter iter;
	dma_addr_t addr;

	/*
	 * Note that we ignore PTE_READ_ONLY here. The caller must be careful
	 * not to allow the user to override access to a read only page.
	 */

	gte = (gen8_pte_t __iomem *)ggtt->gsm;
	gte += vma_res->start / I915_GTT_PAGE_SIZE;
	end = gte + vma_res->node_size / I915_GTT_PAGE_SIZE;

	for_each_sgt_daddr(addr, iter, vma_res->bi.pages)
		gen8_set_pte(gte++, pte_encode | addr);
	GEM_BUG_ON(gte > end);

	/* Fill the allocated but "unused" space beyond the end of the buffer */
	while (gte < end)
		gen8_set_pte(gte++, vm->scratch[0]->encode);

	/*
	 * We want to flush the TLBs only after we're certain all the PTE
	 * updates have finished.
	 */
	ggtt->invalidate(ggtt);
}

static void bxt_vtd_ggtt_wa(struct i915_address_space *vm)
{
	/*
	 * Make sure the internal GAM fifo has been cleared of all GTT
	 * writes before exiting stop_machine(). This guarantees that
	 * any aperture accesses waiting to start in another process
	 * cannot back up behind the GTT writes causing a hang.
	 * The register can be any arbitrary GAM register.
	 */
	intel_uncore_posting_read_fw(vm->gt->uncore, GFX_FLSH_CNTL_GEN6);
}

static int bxt_vtd_ggtt_insert_page__cb(void *_arg)
{
	struct insert_page *arg = _arg;

	gen8_ggtt_insert_page(arg->vm, arg->addr, arg->offset, arg->level, 0);
	bxt_vtd_ggtt_wa(arg->vm);

	return 0;
}

static void bxt_vtd_ggtt_insert_page__BKL(struct i915_address_space *vm,
					  dma_addr_t addr,
					  u64 offset,
					  enum i915_cache_level level,
					  u32 unused)
{
	struct insert_page arg = { vm, addr, offset, level };

	stop_machine(bxt_vtd_ggtt_insert_page__cb, &arg, NULL);
}

static int bxt_vtd_ggtt_insert_entries__cb(void *_arg)
{
	struct insert_entries *arg = _arg;

	gen8_ggtt_insert_entries(arg->vm, arg->vma_res, arg->level, arg->flags);
	bxt_vtd_ggtt_wa(arg->vm);

	return 0;
}

static void bxt_vtd_ggtt_insert_entries__BKL(struct i915_address_space *vm,
					     struct i915_vma_resource *vma_res,
					     enum i915_cache_level level,
					     u32 flags)
{
	struct insert_entries arg = { vm, vma_res, level, flags };

	stop_machine(bxt_vtd_ggtt_insert_entries__cb, &arg, NULL);
}

void intel_gt_gmch_gen5_chipset_flush(struct intel_gt *gt)
{
	intel_gtt_chipset_flush();
}

static void gmch_ggtt_invalidate(struct i915_ggtt *ggtt)
{
	intel_gtt_chipset_flush();
}

static void gen5_ggtt_clear_range(struct i915_address_space *vm,
					 u64 start, u64 length)
{
	intel_gtt_clear_range(start >> PAGE_SHIFT, length >> PAGE_SHIFT);
}

static void gen6_ggtt_clear_range(struct i915_address_space *vm,
				  u64 start, u64 length)
{
	struct i915_ggtt *ggtt = i915_vm_to_ggtt(vm);
	unsigned int first_entry = start / I915_GTT_PAGE_SIZE;
	unsigned int num_entries = length / I915_GTT_PAGE_SIZE;
	gen6_pte_t scratch_pte, __iomem *gtt_base =
		(gen6_pte_t __iomem *)ggtt->gsm + first_entry;
	const int max_entries = ggtt_total_entries(ggtt) - first_entry;
	int i;

	if (WARN(num_entries > max_entries,
		 "First entry = %d; Num entries = %d (max=%d)\n",
		 first_entry, num_entries, max_entries))
		num_entries = max_entries;

	scratch_pte = vm->scratch[0]->encode;
	for (i = 0; i < num_entries; i++)
		iowrite32(scratch_pte, &gtt_base[i]);
}

static void gen8_ggtt_clear_range(struct i915_address_space *vm,
				  u64 start, u64 length)
{
	struct i915_ggtt *ggtt = i915_vm_to_ggtt(vm);
	unsigned int first_entry = start / I915_GTT_PAGE_SIZE;
	unsigned int num_entries = length / I915_GTT_PAGE_SIZE;
	const gen8_pte_t scratch_pte = vm->scratch[0]->encode;
	gen8_pte_t __iomem *gtt_base =
		(gen8_pte_t __iomem *)ggtt->gsm + first_entry;
	const int max_entries = ggtt_total_entries(ggtt) - first_entry;
	int i;

	if (WARN(num_entries > max_entries,
		 "First entry = %d; Num entries = %d (max=%d)\n",
		 first_entry, num_entries, max_entries))
		num_entries = max_entries;

	for (i = 0; i < num_entries; i++)
		gen8_set_pte(&gtt_base[i], scratch_pte);
}

static void gen5_gmch_remove(struct i915_address_space *vm)
{
	intel_gmch_remove();
}

static void gen6_gmch_remove(struct i915_address_space *vm)
{
	struct i915_ggtt *ggtt = i915_vm_to_ggtt(vm);

	iounmap(ggtt->gsm);
	free_scratch(vm);
}

/*
 * Certain Gen5 chipsets require idling the GPU before
 * unmapping anything from the GTT when VT-d is enabled.
 */
static bool needs_idle_maps(struct drm_i915_private *i915)
{
	/*
	 * Query intel_iommu to see if we need the workaround. Presumably that
	 * was loaded first.
	 */
	if (!i915_vtd_active(i915))
		return false;

	if (GRAPHICS_VER(i915) == 5 && IS_MOBILE(i915))
		return true;

	if (GRAPHICS_VER(i915) == 12)
		return true; /* XXX DMAR fault reason 7 */

	return false;
}

static unsigned int gen6_gttmmadr_size(struct drm_i915_private *i915)
{
	/*
	 * GEN6: GTTMMADR size is 4MB and GTTADR starts at 2MB offset
	 * GEN8: GTTMMADR size is 16MB and GTTADR starts at 8MB offset
	 */
	GEM_BUG_ON(GRAPHICS_VER(i915) < 6);
	return (GRAPHICS_VER(i915) < 8) ? SZ_4M : SZ_16M;
}

static unsigned int gen6_get_total_gtt_size(u16 snb_gmch_ctl)
{
	snb_gmch_ctl >>= SNB_GMCH_GGMS_SHIFT;
	snb_gmch_ctl &= SNB_GMCH_GGMS_MASK;
	return snb_gmch_ctl << 20;
}

static unsigned int gen8_get_total_gtt_size(u16 bdw_gmch_ctl)
{
	bdw_gmch_ctl >>= BDW_GMCH_GGMS_SHIFT;
	bdw_gmch_ctl &= BDW_GMCH_GGMS_MASK;
	if (bdw_gmch_ctl)
		bdw_gmch_ctl = 1 << bdw_gmch_ctl;

#ifdef CONFIG_X86_32
	/* Limit 32b platforms to a 2GB GGTT: 4 << 20 / pte size * I915_GTT_PAGE_SIZE */
	if (bdw_gmch_ctl > 4)
		bdw_gmch_ctl = 4;
#endif

	return bdw_gmch_ctl << 20;
}

static unsigned int gen6_gttadr_offset(struct drm_i915_private *i915)
{
	return gen6_gttmmadr_size(i915) / 2;
}

static int ggtt_probe_common(struct i915_ggtt *ggtt, u64 size)
{
	struct drm_i915_private *i915 = ggtt->vm.i915;
	struct pci_dev *pdev = to_pci_dev(i915->drm.dev);
	phys_addr_t phys_addr;
	u32 pte_flags;
	int ret;

	GEM_WARN_ON(pci_resource_len(pdev, 0) != gen6_gttmmadr_size(i915));
	phys_addr = pci_resource_start(pdev, 0) + gen6_gttadr_offset(i915);

	/*
	 * On BXT+/ICL+ writes larger than 64 bit to the GTT pagetable range
	 * will be dropped. For WC mappings in general we have 64 byte burst
	 * writes when the WC buffer is flushed, so we can't use it, but have to
	 * resort to an uncached mapping. The WC issue is easily caught by the
	 * readback check when writing GTT PTE entries.
	 */
	if (IS_GEN9_LP(i915) || GRAPHICS_VER(i915) >= 11)
		ggtt->gsm = ioremap(phys_addr, size);
	else
		ggtt->gsm = ioremap_wc(phys_addr, size);
	if (!ggtt->gsm) {
		drm_err(&i915->drm, "Failed to map the ggtt page table\n");
		return -ENOMEM;
	}

	kref_init(&ggtt->vm.resv_ref);
	ret = setup_scratch_page(&ggtt->vm);
	if (ret) {
		drm_err(&i915->drm, "Scratch setup failed\n");
		/* iounmap will also get called at remove, but meh */
		iounmap(ggtt->gsm);
		return ret;
	}

	pte_flags = 0;
	if (i915_gem_object_is_lmem(ggtt->vm.scratch[0]))
		pte_flags |= PTE_LM;

	ggtt->vm.scratch[0]->encode =
		ggtt->vm.pte_encode(px_dma(ggtt->vm.scratch[0]),
				    I915_CACHE_NONE, pte_flags);

	return 0;
}

int intel_gt_gmch_gen5_probe(struct i915_ggtt *ggtt)
{
	struct drm_i915_private *i915 = ggtt->vm.i915;
	phys_addr_t gmadr_base;
	int ret;

	ret = intel_gmch_probe(i915->bridge_dev, to_pci_dev(i915->drm.dev), NULL);
	if (!ret) {
		drm_err(&i915->drm, "failed to set up gmch\n");
		return -EIO;
	}

	intel_gtt_get(&ggtt->vm.total, &gmadr_base, &ggtt->mappable_end);

	ggtt->gmadr =
		(struct resource)DEFINE_RES_MEM(gmadr_base, ggtt->mappable_end);

	ggtt->vm.alloc_pt_dma = alloc_pt_dma;
	ggtt->vm.alloc_scratch_dma = alloc_pt_dma;

	if (needs_idle_maps(i915)) {
		drm_notice(&i915->drm,
			   "Flushing DMA requests before IOMMU unmaps; performance may be degraded\n");
		ggtt->do_idle_maps = true;
	}

	ggtt->vm.insert_page = gen5_ggtt_insert_page;
	ggtt->vm.insert_entries = gen5_ggtt_insert_entries;
	ggtt->vm.clear_range = gen5_ggtt_clear_range;
	ggtt->vm.cleanup = gen5_gmch_remove;

	ggtt->invalidate = gmch_ggtt_invalidate;

	ggtt->vm.vma_ops.bind_vma    = intel_ggtt_bind_vma;
	ggtt->vm.vma_ops.unbind_vma  = intel_ggtt_unbind_vma;

	if (unlikely(ggtt->do_idle_maps))
		drm_notice(&i915->drm,
			   "Applying Ironlake quirks for intel_iommu\n");

	return 0;
}

int intel_gt_gmch_gen6_probe(struct i915_ggtt *ggtt)
{
	struct drm_i915_private *i915 = ggtt->vm.i915;
	struct pci_dev *pdev = to_pci_dev(i915->drm.dev);
	unsigned int size;
	u16 snb_gmch_ctl;

	ggtt->gmadr = intel_pci_resource(pdev, 2);
	ggtt->mappable_end = resource_size(&ggtt->gmadr);

	/*
	 * 64/512MB is the current min/max we actually know of, but this is
	 * just a coarse sanity check.
	 */
	if (ggtt->mappable_end < (64<<20) || ggtt->mappable_end > (512<<20)) {
		drm_err(&i915->drm, "Unknown GMADR size (%pa)\n",
			&ggtt->mappable_end);
		return -ENXIO;
	}

	pci_read_config_word(pdev, SNB_GMCH_CTRL, &snb_gmch_ctl);

	size = gen6_get_total_gtt_size(snb_gmch_ctl);
	ggtt->vm.total = (size / sizeof(gen6_pte_t)) * I915_GTT_PAGE_SIZE;

	ggtt->vm.alloc_pt_dma = alloc_pt_dma;
	ggtt->vm.alloc_scratch_dma = alloc_pt_dma;

	ggtt->vm.clear_range = nop_clear_range;
	if (!HAS_FULL_PPGTT(i915) || intel_scanout_needs_vtd_wa(i915))
		ggtt->vm.clear_range = gen6_ggtt_clear_range;
	ggtt->vm.insert_page = gen6_ggtt_insert_page;
	ggtt->vm.insert_entries = gen6_ggtt_insert_entries;
	ggtt->vm.cleanup = gen6_gmch_remove;

	ggtt->invalidate = gen6_ggtt_invalidate;

	if (HAS_EDRAM(i915))
		ggtt->vm.pte_encode = iris_pte_encode;
	else if (IS_HASWELL(i915))
		ggtt->vm.pte_encode = hsw_pte_encode;
	else if (IS_VALLEYVIEW(i915))
		ggtt->vm.pte_encode = byt_pte_encode;
	else if (GRAPHICS_VER(i915) >= 7)
		ggtt->vm.pte_encode = ivb_pte_encode;
	else
		ggtt->vm.pte_encode = snb_pte_encode;

	ggtt->vm.vma_ops.bind_vma    = intel_ggtt_bind_vma;
	ggtt->vm.vma_ops.unbind_vma  = intel_ggtt_unbind_vma;

	return ggtt_probe_common(ggtt, size);
}

static unsigned int chv_get_total_gtt_size(u16 gmch_ctrl)
{
	gmch_ctrl >>= SNB_GMCH_GGMS_SHIFT;
	gmch_ctrl &= SNB_GMCH_GGMS_MASK;

	if (gmch_ctrl)
		return 1 << (20 + gmch_ctrl);

	return 0;
}

int intel_gt_gmch_gen8_probe(struct i915_ggtt *ggtt)
{
	struct drm_i915_private *i915 = ggtt->vm.i915;
	struct pci_dev *pdev = to_pci_dev(i915->drm.dev);
	unsigned int size;
	u16 snb_gmch_ctl;

	/* TODO: We're not aware of mappable constraints on gen8 yet */
	if (!HAS_LMEM(i915)) {
		ggtt->gmadr = intel_pci_resource(pdev, 2);
		ggtt->mappable_end = resource_size(&ggtt->gmadr);
	}

	pci_read_config_word(pdev, SNB_GMCH_CTRL, &snb_gmch_ctl);
	if (IS_CHERRYVIEW(i915))
		size = chv_get_total_gtt_size(snb_gmch_ctl);
	else
		size = gen8_get_total_gtt_size(snb_gmch_ctl);

	ggtt->vm.alloc_pt_dma = alloc_pt_dma;
	ggtt->vm.alloc_scratch_dma = alloc_pt_dma;
	ggtt->vm.lmem_pt_obj_flags = I915_BO_ALLOC_PM_EARLY;

	ggtt->vm.total = (size / sizeof(gen8_pte_t)) * I915_GTT_PAGE_SIZE;
	ggtt->vm.cleanup = gen6_gmch_remove;
	ggtt->vm.insert_page = gen8_ggtt_insert_page;
	ggtt->vm.clear_range = nop_clear_range;
	if (intel_scanout_needs_vtd_wa(i915))
		ggtt->vm.clear_range = gen8_ggtt_clear_range;

	ggtt->vm.insert_entries = gen8_ggtt_insert_entries;

	/*
	 * Serialize GTT updates with aperture access on BXT if VT-d is on,
	 * and always on CHV.
	 */
	if (intel_vm_no_concurrent_access_wa(i915)) {
		ggtt->vm.insert_entries = bxt_vtd_ggtt_insert_entries__BKL;
		ggtt->vm.insert_page    = bxt_vtd_ggtt_insert_page__BKL;
		ggtt->vm.bind_async_flags =
			I915_VMA_GLOBAL_BIND | I915_VMA_LOCAL_BIND;
	}

	ggtt->invalidate = gen8_ggtt_invalidate;

	ggtt->vm.vma_ops.bind_vma    = intel_ggtt_bind_vma;
	ggtt->vm.vma_ops.unbind_vma  = intel_ggtt_unbind_vma;

	ggtt->vm.pte_encode = gen8_ggtt_pte_encode;

	setup_private_pat(ggtt->vm.gt->uncore);

	return ggtt_probe_common(ggtt, size);
}

int intel_gt_gmch_gen5_enable_hw(struct drm_i915_private *i915)
{
	if (GRAPHICS_VER(i915) < 6 && !intel_enable_gtt())
		return -EIO;

	return 0;
}
