// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020 Intel Corporation
 */

#include <linux/slab.h> /* fault-inject.h is not standalone! */

#include <linux/fault-inject.h>

#include "i915_trace.h"
#include "intel_gt.h"
#include "intel_gtt.h"

struct drm_i915_gem_object *alloc_pt_dma(struct i915_address_space *vm, int sz)
{
	if (I915_SELFTEST_ONLY(should_fail(&vm->fault_attr, 1)))
		i915_gem_shrink_all(vm->i915);

	return i915_gem_object_create_internal(vm->i915, sz);
}

int pin_pt_dma(struct i915_address_space *vm, struct drm_i915_gem_object *obj)
{
	int err;

	err = i915_gem_object_pin_pages(obj);
	if (err)
		return err;

	i915_gem_object_make_unshrinkable(obj);
	return 0;
}

void __i915_vm_close(struct i915_address_space *vm)
{
	struct i915_vma *vma, *vn;

	if (!atomic_dec_and_mutex_lock(&vm->open, &vm->mutex))
		return;

	list_for_each_entry_safe(vma, vn, &vm->bound_list, vm_link) {
		struct drm_i915_gem_object *obj = vma->obj;

		/* Keep the obj (and hence the vma) alive as _we_ destroy it */
		if (!kref_get_unless_zero(&obj->base.refcount))
			continue;

		atomic_and(~I915_VMA_PIN_MASK, &vma->flags);
		WARN_ON(__i915_vma_unbind(vma));
		__i915_vma_put(vma);

		i915_gem_object_put(obj);
	}
	GEM_BUG_ON(!list_empty(&vm->bound_list));

	mutex_unlock(&vm->mutex);
}

void i915_address_space_fini(struct i915_address_space *vm)
{
	drm_mm_takedown(&vm->mm);
	mutex_destroy(&vm->mutex);
}

static void __i915_vm_release(struct work_struct *work)
{
	struct i915_address_space *vm =
		container_of(work, struct i915_address_space, rcu.work);

	vm->cleanup(vm);
	i915_address_space_fini(vm);

	kfree(vm);
}

void i915_vm_release(struct kref *kref)
{
	struct i915_address_space *vm =
		container_of(kref, struct i915_address_space, ref);

	GEM_BUG_ON(i915_is_ggtt(vm));
	trace_i915_ppgtt_release(vm);

	queue_rcu_work(vm->i915->wq, &vm->rcu);
}

void i915_address_space_init(struct i915_address_space *vm, int subclass)
{
	kref_init(&vm->ref);
	INIT_RCU_WORK(&vm->rcu, __i915_vm_release);
	atomic_set(&vm->open, 1);

	/*
	 * The vm->mutex must be reclaim safe (for use in the shrinker).
	 * Do a dummy acquire now under fs_reclaim so that any allocation
	 * attempt holding the lock is immediately reported by lockdep.
	 */
	mutex_init(&vm->mutex);
	lockdep_set_subclass(&vm->mutex, subclass);
	i915_gem_shrinker_taints_mutex(vm->i915, &vm->mutex);

	GEM_BUG_ON(!vm->total);
	drm_mm_init(&vm->mm, 0, vm->total);
	vm->mm.head_node.color = I915_COLOR_UNEVICTABLE;

	INIT_LIST_HEAD(&vm->bound_list);
}

void clear_pages(struct i915_vma *vma)
{
	GEM_BUG_ON(!vma->pages);

	if (vma->pages != vma->obj->mm.pages) {
		sg_free_table(vma->pages);
		kfree(vma->pages);
	}
	vma->pages = NULL;

	memset(&vma->page_sizes, 0, sizeof(vma->page_sizes));
}

dma_addr_t __px_dma(struct drm_i915_gem_object *p)
{
	GEM_BUG_ON(!i915_gem_object_has_pages(p));
	return sg_dma_address(p->mm.pages->sgl);
}

struct page *__px_page(struct drm_i915_gem_object *p)
{
	GEM_BUG_ON(!i915_gem_object_has_pages(p));
	return sg_page(p->mm.pages->sgl);
}

void
fill_page_dma(struct drm_i915_gem_object *p, const u64 val, unsigned int count)
{
	struct page *page = __px_page(p);
	void *vaddr;

	vaddr = kmap(page);
	memset64(vaddr, val, count);
	clflush_cache_range(vaddr, PAGE_SIZE);
	kunmap(page);
}

static void poison_scratch_page(struct drm_i915_gem_object *scratch)
{
	struct sgt_iter sgt;
	struct page *page;
	u8 val;

	val = 0;
	if (IS_ENABLED(CONFIG_DRM_I915_DEBUG_GEM))
		val = POISON_FREE;

	for_each_sgt_page(page, sgt, scratch->mm.pages) {
		void *vaddr;

		vaddr = kmap(page);
		memset(vaddr, val, PAGE_SIZE);
		kunmap(page);
	}
}

int setup_scratch_page(struct i915_address_space *vm)
{
	unsigned long size;

	/*
	 * In order to utilize 64K pages for an object with a size < 2M, we will
	 * need to support a 64K scratch page, given that every 16th entry for a
	 * page-table operating in 64K mode must point to a properly aligned 64K
	 * region, including any PTEs which happen to point to scratch.
	 *
	 * This is only relevant for the 48b PPGTT where we support
	 * huge-gtt-pages, see also i915_vma_insert(). However, as we share the
	 * scratch (read-only) between all vm, we create one 64k scratch page
	 * for all.
	 */
	size = I915_GTT_PAGE_SIZE_4K;
	if (i915_vm_is_4lvl(vm) &&
	    HAS_PAGE_SIZES(vm->i915, I915_GTT_PAGE_SIZE_64K))
		size = I915_GTT_PAGE_SIZE_64K;

	do {
		struct drm_i915_gem_object *obj;

		obj = vm->alloc_pt_dma(vm, size);
		if (IS_ERR(obj))
			goto skip;

		if (pin_pt_dma(vm, obj))
			goto skip_obj;

		/* We need a single contiguous page for our scratch */
		if (obj->mm.page_sizes.sg < size)
			goto skip_obj;

		/* And it needs to be correspondingly aligned */
		if (__px_dma(obj) & (size - 1))
			goto skip_obj;

		/*
		 * Use a non-zero scratch page for debugging.
		 *
		 * We want a value that should be reasonably obvious
		 * to spot in the error state, while also causing a GPU hang
		 * if executed. We prefer using a clear page in production, so
		 * should it ever be accidentally used, the effect should be
		 * fairly benign.
		 */
		poison_scratch_page(obj);

		vm->scratch[0] = obj;
		vm->scratch_order = get_order(size);
		return 0;

skip_obj:
		i915_gem_object_put(obj);
skip:
		if (size == I915_GTT_PAGE_SIZE_4K)
			return -ENOMEM;

		size = I915_GTT_PAGE_SIZE_4K;
	} while (1);
}

void free_scratch(struct i915_address_space *vm)
{
	int i;

	for (i = 0; i <= vm->top; i++)
		i915_gem_object_put(vm->scratch[i]);
}

void gtt_write_workarounds(struct intel_gt *gt)
{
	struct drm_i915_private *i915 = gt->i915;
	struct intel_uncore *uncore = gt->uncore;

	/*
	 * This function is for gtt related workarounds. This function is
	 * called on driver load and after a GPU reset, so you can place
	 * workarounds here even if they get overwritten by GPU reset.
	 */
	/* WaIncreaseDefaultTLBEntries:chv,bdw,skl,bxt,kbl,glk,cfl,cnl,icl */
	if (IS_BROADWELL(i915))
		intel_uncore_write(uncore,
				   GEN8_L3_LRA_1_GPGPU,
				   GEN8_L3_LRA_1_GPGPU_DEFAULT_VALUE_BDW);
	else if (IS_CHERRYVIEW(i915))
		intel_uncore_write(uncore,
				   GEN8_L3_LRA_1_GPGPU,
				   GEN8_L3_LRA_1_GPGPU_DEFAULT_VALUE_CHV);
	else if (IS_GEN9_LP(i915))
		intel_uncore_write(uncore,
				   GEN8_L3_LRA_1_GPGPU,
				   GEN9_L3_LRA_1_GPGPU_DEFAULT_VALUE_BXT);
	else if (INTEL_GEN(i915) >= 9 && INTEL_GEN(i915) <= 11)
		intel_uncore_write(uncore,
				   GEN8_L3_LRA_1_GPGPU,
				   GEN9_L3_LRA_1_GPGPU_DEFAULT_VALUE_SKL);

	/*
	 * To support 64K PTEs we need to first enable the use of the
	 * Intermediate-Page-Size(IPS) bit of the PDE field via some magical
	 * mmio, otherwise the page-walker will simply ignore the IPS bit. This
	 * shouldn't be needed after GEN10.
	 *
	 * 64K pages were first introduced from BDW+, although technically they
	 * only *work* from gen9+. For pre-BDW we instead have the option for
	 * 32K pages, but we don't currently have any support for it in our
	 * driver.
	 */
	if (HAS_PAGE_SIZES(i915, I915_GTT_PAGE_SIZE_64K) &&
	    INTEL_GEN(i915) <= 10)
		intel_uncore_rmw(uncore,
				 GEN8_GAMW_ECO_DEV_RW_IA,
				 0,
				 GAMW_ECO_ENABLE_64K_IPS_FIELD);

	if (IS_GEN_RANGE(i915, 8, 11)) {
		bool can_use_gtt_cache = true;

		/*
		 * According to the BSpec if we use 2M/1G pages then we also
		 * need to disable the GTT cache. At least on BDW we can see
		 * visual corruption when using 2M pages, and not disabling the
		 * GTT cache.
		 */
		if (HAS_PAGE_SIZES(i915, I915_GTT_PAGE_SIZE_2M))
			can_use_gtt_cache = false;

		/* WaGttCachingOffByDefault */
		intel_uncore_write(uncore,
				   HSW_GTT_CACHE_EN,
				   can_use_gtt_cache ? GTT_CACHE_EN_ALL : 0);
		drm_WARN_ON_ONCE(&i915->drm, can_use_gtt_cache &&
				 intel_uncore_read(uncore,
						   HSW_GTT_CACHE_EN) == 0);
	}
}

static void tgl_setup_private_ppat(struct intel_uncore *uncore)
{
	/* TGL doesn't support LLC or AGE settings */
	intel_uncore_write(uncore, GEN12_PAT_INDEX(0), GEN8_PPAT_WB);
	intel_uncore_write(uncore, GEN12_PAT_INDEX(1), GEN8_PPAT_WC);
	intel_uncore_write(uncore, GEN12_PAT_INDEX(2), GEN8_PPAT_WT);
	intel_uncore_write(uncore, GEN12_PAT_INDEX(3), GEN8_PPAT_UC);
	intel_uncore_write(uncore, GEN12_PAT_INDEX(4), GEN8_PPAT_WB);
	intel_uncore_write(uncore, GEN12_PAT_INDEX(5), GEN8_PPAT_WB);
	intel_uncore_write(uncore, GEN12_PAT_INDEX(6), GEN8_PPAT_WB);
	intel_uncore_write(uncore, GEN12_PAT_INDEX(7), GEN8_PPAT_WB);
}

static void cnl_setup_private_ppat(struct intel_uncore *uncore)
{
	intel_uncore_write(uncore,
			   GEN10_PAT_INDEX(0),
			   GEN8_PPAT_WB | GEN8_PPAT_LLC);
	intel_uncore_write(uncore,
			   GEN10_PAT_INDEX(1),
			   GEN8_PPAT_WC | GEN8_PPAT_LLCELLC);
	intel_uncore_write(uncore,
			   GEN10_PAT_INDEX(2),
			   GEN8_PPAT_WB | GEN8_PPAT_ELLC_OVERRIDE);
	intel_uncore_write(uncore,
			   GEN10_PAT_INDEX(3),
			   GEN8_PPAT_UC);
	intel_uncore_write(uncore,
			   GEN10_PAT_INDEX(4),
			   GEN8_PPAT_WB | GEN8_PPAT_LLCELLC | GEN8_PPAT_AGE(0));
	intel_uncore_write(uncore,
			   GEN10_PAT_INDEX(5),
			   GEN8_PPAT_WB | GEN8_PPAT_LLCELLC | GEN8_PPAT_AGE(1));
	intel_uncore_write(uncore,
			   GEN10_PAT_INDEX(6),
			   GEN8_PPAT_WB | GEN8_PPAT_LLCELLC | GEN8_PPAT_AGE(2));
	intel_uncore_write(uncore,
			   GEN10_PAT_INDEX(7),
			   GEN8_PPAT_WB | GEN8_PPAT_LLCELLC | GEN8_PPAT_AGE(3));
}

/*
 * The GGTT and PPGTT need a private PPAT setup in order to handle cacheability
 * bits. When using advanced contexts each context stores its own PAT, but
 * writing this data shouldn't be harmful even in those cases.
 */
static void bdw_setup_private_ppat(struct intel_uncore *uncore)
{
	struct drm_i915_private *i915 = uncore->i915;
	u64 pat;

	pat = GEN8_PPAT(0, GEN8_PPAT_WB | GEN8_PPAT_LLC) |	/* for normal objects, no eLLC */
	      GEN8_PPAT(1, GEN8_PPAT_WC | GEN8_PPAT_LLCELLC) |	/* for something pointing to ptes? */
	      GEN8_PPAT(3, GEN8_PPAT_UC) |			/* Uncached objects, mostly for scanout */
	      GEN8_PPAT(4, GEN8_PPAT_WB | GEN8_PPAT_LLCELLC | GEN8_PPAT_AGE(0)) |
	      GEN8_PPAT(5, GEN8_PPAT_WB | GEN8_PPAT_LLCELLC | GEN8_PPAT_AGE(1)) |
	      GEN8_PPAT(6, GEN8_PPAT_WB | GEN8_PPAT_LLCELLC | GEN8_PPAT_AGE(2)) |
	      GEN8_PPAT(7, GEN8_PPAT_WB | GEN8_PPAT_LLCELLC | GEN8_PPAT_AGE(3));

	/* for scanout with eLLC */
	if (INTEL_GEN(i915) >= 9)
		pat |= GEN8_PPAT(2, GEN8_PPAT_WB | GEN8_PPAT_ELLC_OVERRIDE);
	else
		pat |= GEN8_PPAT(2, GEN8_PPAT_WT | GEN8_PPAT_LLCELLC);

	intel_uncore_write(uncore, GEN8_PRIVATE_PAT_LO, lower_32_bits(pat));
	intel_uncore_write(uncore, GEN8_PRIVATE_PAT_HI, upper_32_bits(pat));
}

static void chv_setup_private_ppat(struct intel_uncore *uncore)
{
	u64 pat;

	/*
	 * Map WB on BDW to snooped on CHV.
	 *
	 * Only the snoop bit has meaning for CHV, the rest is
	 * ignored.
	 *
	 * The hardware will never snoop for certain types of accesses:
	 * - CPU GTT (GMADR->GGTT->no snoop->memory)
	 * - PPGTT page tables
	 * - some other special cycles
	 *
	 * As with BDW, we also need to consider the following for GT accesses:
	 * "For GGTT, there is NO pat_sel[2:0] from the entry,
	 * so RTL will always use the value corresponding to
	 * pat_sel = 000".
	 * Which means we must set the snoop bit in PAT entry 0
	 * in order to keep the global status page working.
	 */

	pat = GEN8_PPAT(0, CHV_PPAT_SNOOP) |
	      GEN8_PPAT(1, 0) |
	      GEN8_PPAT(2, 0) |
	      GEN8_PPAT(3, 0) |
	      GEN8_PPAT(4, CHV_PPAT_SNOOP) |
	      GEN8_PPAT(5, CHV_PPAT_SNOOP) |
	      GEN8_PPAT(6, CHV_PPAT_SNOOP) |
	      GEN8_PPAT(7, CHV_PPAT_SNOOP);

	intel_uncore_write(uncore, GEN8_PRIVATE_PAT_LO, lower_32_bits(pat));
	intel_uncore_write(uncore, GEN8_PRIVATE_PAT_HI, upper_32_bits(pat));
}

void setup_private_pat(struct intel_uncore *uncore)
{
	struct drm_i915_private *i915 = uncore->i915;

	GEM_BUG_ON(INTEL_GEN(i915) < 8);

	if (INTEL_GEN(i915) >= 12)
		tgl_setup_private_ppat(uncore);
	else if (INTEL_GEN(i915) >= 10)
		cnl_setup_private_ppat(uncore);
	else if (IS_CHERRYVIEW(i915) || IS_GEN9_LP(i915))
		chv_setup_private_ppat(uncore);
	else
		bdw_setup_private_ppat(uncore);
}

#if IS_ENABLED(CONFIG_DRM_I915_SELFTEST)
#include "selftests/mock_gtt.c"
#endif
