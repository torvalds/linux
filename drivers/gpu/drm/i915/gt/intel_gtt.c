// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020 Intel Corporation
 */

#include <linux/slab.h> /* fault-inject.h is not standalone! */

#include <linux/fault-inject.h>
#include <linux/sched/mm.h>

#include <drm/drm_cache.h>

#include "gem/i915_gem_internal.h"
#include "gem/i915_gem_lmem.h"
#include "i915_trace.h"
#include "i915_utils.h"
#include "intel_gt.h"
#include "intel_gt_regs.h"
#include "intel_gtt.h"


static bool intel_ggtt_update_needs_vtd_wa(struct drm_i915_private *i915)
{
	return IS_BROXTON(i915) && i915_vtd_active(i915);
}

bool intel_vm_no_concurrent_access_wa(struct drm_i915_private *i915)
{
	return IS_CHERRYVIEW(i915) || intel_ggtt_update_needs_vtd_wa(i915);
}

struct drm_i915_gem_object *alloc_pt_lmem(struct i915_address_space *vm, int sz)
{
	struct drm_i915_gem_object *obj;

	/*
	 * To avoid severe over-allocation when dealing with min_page_size
	 * restrictions, we override that behaviour here by allowing an object
	 * size and page layout which can be smaller. In practice this should be
	 * totally fine, since GTT paging structures are not typically inserted
	 * into the GTT.
	 *
	 * Note that we also hit this path for the scratch page, and for this
	 * case it might need to be 64K, but that should work fine here since we
	 * used the passed in size for the page size, which should ensure it
	 * also has the same alignment.
	 */
	obj = __i915_gem_object_create_lmem_with_ps(vm->i915, sz, sz,
						    vm->lmem_pt_obj_flags);
	/*
	 * Ensure all paging structures for this vm share the same dma-resv
	 * object underneath, with the idea that one object_lock() will lock
	 * them all at once.
	 */
	if (!IS_ERR(obj)) {
		obj->base.resv = i915_vm_resv_get(vm);
		obj->shares_resv_from = vm;
	}

	return obj;
}

struct drm_i915_gem_object *alloc_pt_dma(struct i915_address_space *vm, int sz)
{
	struct drm_i915_gem_object *obj;

	if (I915_SELFTEST_ONLY(should_fail(&vm->fault_attr, 1)))
		i915_gem_shrink_all(vm->i915);

	obj = i915_gem_object_create_internal(vm->i915, sz);
	/*
	 * Ensure all paging structures for this vm share the same dma-resv
	 * object underneath, with the idea that one object_lock() will lock
	 * them all at once.
	 */
	if (!IS_ERR(obj)) {
		obj->base.resv = i915_vm_resv_get(vm);
		obj->shares_resv_from = vm;
	}

	return obj;
}

int map_pt_dma(struct i915_address_space *vm, struct drm_i915_gem_object *obj)
{
	enum i915_map_type type;
	void *vaddr;

	type = i915_coherent_map_type(vm->i915, obj, true);
	vaddr = i915_gem_object_pin_map_unlocked(obj, type);
	if (IS_ERR(vaddr))
		return PTR_ERR(vaddr);

	i915_gem_object_make_unshrinkable(obj);
	return 0;
}

int map_pt_dma_locked(struct i915_address_space *vm, struct drm_i915_gem_object *obj)
{
	enum i915_map_type type;
	void *vaddr;

	type = i915_coherent_map_type(vm->i915, obj, true);
	vaddr = i915_gem_object_pin_map(obj, type);
	if (IS_ERR(vaddr))
		return PTR_ERR(vaddr);

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

		if (!kref_get_unless_zero(&obj->base.refcount)) {
			/*
			 * Unbind the dying vma to ensure the bound_list
			 * is completely drained. We leave the destruction to
			 * the object destructor.
			 */
			atomic_and(~I915_VMA_PIN_MASK, &vma->flags);
			WARN_ON(__i915_vma_unbind(vma));
			continue;
		}

		/* Keep the obj (and hence the vma) alive as _we_ destroy it */
		i915_vma_destroy_locked(vma);
		i915_gem_object_put(obj);
	}
	GEM_BUG_ON(!list_empty(&vm->bound_list));

	mutex_unlock(&vm->mutex);
}

/* lock the vm into the current ww, if we lock one, we lock all */
int i915_vm_lock_objects(struct i915_address_space *vm,
			 struct i915_gem_ww_ctx *ww)
{
	if (vm->scratch[0]->base.resv == &vm->_resv) {
		return i915_gem_object_lock(vm->scratch[0], ww);
	} else {
		struct i915_ppgtt *ppgtt = i915_vm_to_ppgtt(vm);

		/* We borrowed the scratch page from ggtt, take the top level object */
		return i915_gem_object_lock(ppgtt->pd->pt.base, ww);
	}
}

void i915_address_space_fini(struct i915_address_space *vm)
{
	drm_mm_takedown(&vm->mm);
	mutex_destroy(&vm->mutex);
}

/**
 * i915_vm_resv_release - Final struct i915_address_space destructor
 * @kref: Pointer to the &i915_address_space.resv_ref member.
 *
 * This function is called when the last lock sharer no longer shares the
 * &i915_address_space._resv lock.
 */
void i915_vm_resv_release(struct kref *kref)
{
	struct i915_address_space *vm =
		container_of(kref, typeof(*vm), resv_ref);

	dma_resv_fini(&vm->_resv);
	kfree(vm);
}

static void __i915_vm_release(struct work_struct *work)
{
	struct i915_address_space *vm =
		container_of(work, struct i915_address_space, release_work);

	/* Synchronize async unbinds. */
	i915_vma_resource_bind_dep_sync_all(vm);

	vm->cleanup(vm);
	i915_address_space_fini(vm);

	i915_vm_resv_put(vm);
}

void i915_vm_release(struct kref *kref)
{
	struct i915_address_space *vm =
		container_of(kref, struct i915_address_space, ref);

	GEM_BUG_ON(i915_is_ggtt(vm));
	trace_i915_ppgtt_release(vm);

	queue_work(vm->i915->wq, &vm->release_work);
}

void i915_address_space_init(struct i915_address_space *vm, int subclass)
{
	kref_init(&vm->ref);

	/*
	 * Special case for GGTT that has already done an early
	 * kref_init here.
	 */
	if (!kref_read(&vm->resv_ref))
		kref_init(&vm->resv_ref);

	vm->pending_unbind = RB_ROOT_CACHED;
	INIT_WORK(&vm->release_work, __i915_vm_release);
	atomic_set(&vm->open, 1);

	/*
	 * The vm->mutex must be reclaim safe (for use in the shrinker).
	 * Do a dummy acquire now under fs_reclaim so that any allocation
	 * attempt holding the lock is immediately reported by lockdep.
	 */
	mutex_init(&vm->mutex);
	lockdep_set_subclass(&vm->mutex, subclass);

	if (!intel_vm_no_concurrent_access_wa(vm->i915)) {
		i915_gem_shrinker_taints_mutex(vm->i915, &vm->mutex);
	} else {
		/*
		 * CHV + BXT VTD workaround use stop_machine(),
		 * which is allowed to allocate memory. This means &vm->mutex
		 * is the outer lock, and in theory we can allocate memory inside
		 * it through stop_machine().
		 *
		 * Add the annotation for this, we use trylock in shrinker.
		 */
		mutex_acquire(&vm->mutex.dep_map, 0, 0, _THIS_IP_);
		might_alloc(GFP_KERNEL);
		mutex_release(&vm->mutex.dep_map, _THIS_IP_);
	}
	dma_resv_init(&vm->_resv);

	GEM_BUG_ON(!vm->total);
	drm_mm_init(&vm->mm, 0, vm->total);

	memset64(vm->min_alignment, I915_GTT_MIN_ALIGNMENT,
		 ARRAY_SIZE(vm->min_alignment));

	if (HAS_64K_PAGES(vm->i915) && NEEDS_COMPACT_PT(vm->i915) &&
	    subclass == VM_CLASS_PPGTT) {
		vm->min_alignment[INTEL_MEMORY_LOCAL] = I915_GTT_PAGE_SIZE_2M;
		vm->min_alignment[INTEL_MEMORY_STOLEN_LOCAL] = I915_GTT_PAGE_SIZE_2M;
	} else if (HAS_64K_PAGES(vm->i915)) {
		vm->min_alignment[INTEL_MEMORY_LOCAL] = I915_GTT_PAGE_SIZE_64K;
		vm->min_alignment[INTEL_MEMORY_STOLEN_LOCAL] = I915_GTT_PAGE_SIZE_64K;
	}

	vm->mm.head_node.color = I915_COLOR_UNEVICTABLE;

	INIT_LIST_HEAD(&vm->bound_list);
}

void *__px_vaddr(struct drm_i915_gem_object *p)
{
	enum i915_map_type type;

	GEM_BUG_ON(!i915_gem_object_has_pages(p));
	return page_unpack_bits(p->mm.mapping, &type);
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
	void *vaddr = __px_vaddr(p);

	memset64(vaddr, val, count);
	clflush_cache_range(vaddr, PAGE_SIZE);
}

static void poison_scratch_page(struct drm_i915_gem_object *scratch)
{
	void *vaddr = __px_vaddr(scratch);
	u8 val;

	val = 0;
	if (IS_ENABLED(CONFIG_DRM_I915_DEBUG_GEM))
		val = POISON_FREE;

	memset(vaddr, val, scratch->base.size);
	drm_clflush_virt_range(vaddr, scratch->base.size);
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

		obj = vm->alloc_scratch_dma(vm, size);
		if (IS_ERR(obj))
			goto skip;

		if (map_pt_dma(vm, obj))
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

		/*
		 * If we need 64K minimum GTT pages for device local-memory,
		 * like on XEHPSDV, then we need to fail the allocation here,
		 * otherwise we can't safely support the insertion of
		 * local-memory pages for this vm, since the HW expects the
		 * correct physical alignment and size when the page-table is
		 * operating in 64K GTT mode, which includes any scratch PTEs,
		 * since userspace can still touch them.
		 */
		if (HAS_64K_PAGES(vm->i915))
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
	else if (GRAPHICS_VER(i915) >= 9 && GRAPHICS_VER(i915) <= 11)
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
	    GRAPHICS_VER(i915) <= 10)
		intel_uncore_rmw(uncore,
				 GEN8_GAMW_ECO_DEV_RW_IA,
				 0,
				 GAMW_ECO_ENABLE_64K_IPS_FIELD);

	if (IS_GRAPHICS_VER(i915, 8, 11)) {
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

static void icl_setup_private_ppat(struct intel_uncore *uncore)
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
	if (GRAPHICS_VER(i915) >= 9)
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

	GEM_BUG_ON(GRAPHICS_VER(i915) < 8);

	if (GRAPHICS_VER(i915) >= 12)
		tgl_setup_private_ppat(uncore);
	else if (GRAPHICS_VER(i915) >= 11)
		icl_setup_private_ppat(uncore);
	else if (IS_CHERRYVIEW(i915) || IS_GEN9_LP(i915))
		chv_setup_private_ppat(uncore);
	else
		bdw_setup_private_ppat(uncore);
}

struct i915_vma *
__vm_create_scratch_for_read(struct i915_address_space *vm, unsigned long size)
{
	struct drm_i915_gem_object *obj;
	struct i915_vma *vma;

	obj = i915_gem_object_create_internal(vm->i915, PAGE_ALIGN(size));
	if (IS_ERR(obj))
		return ERR_CAST(obj);

	i915_gem_object_set_cache_coherency(obj, I915_CACHING_CACHED);

	vma = i915_vma_instance(obj, vm, NULL);
	if (IS_ERR(vma)) {
		i915_gem_object_put(obj);
		return vma;
	}

	return vma;
}

struct i915_vma *
__vm_create_scratch_for_read_pinned(struct i915_address_space *vm, unsigned long size)
{
	struct i915_vma *vma;
	int err;

	vma = __vm_create_scratch_for_read(vm, size);
	if (IS_ERR(vma))
		return vma;

	err = i915_vma_pin(vma, 0, 0,
			   i915_vma_is_ggtt(vma) ? PIN_GLOBAL : PIN_USER);
	if (err) {
		i915_vma_put(vma);
		return ERR_PTR(err);
	}

	return vma;
}

#if IS_ENABLED(CONFIG_DRM_I915_SELFTEST)
#include "selftests/mock_gtt.c"
#endif
