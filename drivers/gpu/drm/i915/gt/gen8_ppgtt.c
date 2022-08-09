// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020 Intel Corporation
 */

#include <linux/log2.h>

#include "gem/i915_gem_lmem.h"

#include "gen8_ppgtt.h"
#include "i915_scatterlist.h"
#include "i915_trace.h"
#include "i915_pvinfo.h"
#include "i915_vgpu.h"
#include "intel_gt.h"
#include "intel_gtt.h"

static u64 gen8_pde_encode(const dma_addr_t addr,
			   const enum i915_cache_level level)
{
	u64 pde = addr | GEN8_PAGE_PRESENT | GEN8_PAGE_RW;

	if (level != I915_CACHE_NONE)
		pde |= PPAT_CACHED_PDE;
	else
		pde |= PPAT_UNCACHED;

	return pde;
}

static u64 gen8_pte_encode(dma_addr_t addr,
			   enum i915_cache_level level,
			   u32 flags)
{
	gen8_pte_t pte = addr | GEN8_PAGE_PRESENT | GEN8_PAGE_RW;

	if (unlikely(flags & PTE_READ_ONLY))
		pte &= ~GEN8_PAGE_RW;

	if (flags & PTE_LM)
		pte |= GEN12_PPGTT_PTE_LM;

	switch (level) {
	case I915_CACHE_NONE:
		pte |= PPAT_UNCACHED;
		break;
	case I915_CACHE_WT:
		pte |= PPAT_DISPLAY_ELLC;
		break;
	default:
		pte |= PPAT_CACHED;
		break;
	}

	return pte;
}

static void gen8_ppgtt_notify_vgt(struct i915_ppgtt *ppgtt, bool create)
{
	struct drm_i915_private *i915 = ppgtt->vm.i915;
	struct intel_uncore *uncore = ppgtt->vm.gt->uncore;
	enum vgt_g2v_type msg;
	int i;

	if (create)
		atomic_inc(px_used(ppgtt->pd)); /* never remove */
	else
		atomic_dec(px_used(ppgtt->pd));

	mutex_lock(&i915->vgpu.lock);

	if (i915_vm_is_4lvl(&ppgtt->vm)) {
		const u64 daddr = px_dma(ppgtt->pd);

		intel_uncore_write(uncore,
				   vgtif_reg(pdp[0].lo), lower_32_bits(daddr));
		intel_uncore_write(uncore,
				   vgtif_reg(pdp[0].hi), upper_32_bits(daddr));

		msg = create ?
			VGT_G2V_PPGTT_L4_PAGE_TABLE_CREATE :
			VGT_G2V_PPGTT_L4_PAGE_TABLE_DESTROY;
	} else {
		for (i = 0; i < GEN8_3LVL_PDPES; i++) {
			const u64 daddr = i915_page_dir_dma_addr(ppgtt, i);

			intel_uncore_write(uncore,
					   vgtif_reg(pdp[i].lo),
					   lower_32_bits(daddr));
			intel_uncore_write(uncore,
					   vgtif_reg(pdp[i].hi),
					   upper_32_bits(daddr));
		}

		msg = create ?
			VGT_G2V_PPGTT_L3_PAGE_TABLE_CREATE :
			VGT_G2V_PPGTT_L3_PAGE_TABLE_DESTROY;
	}

	/* g2v_notify atomically (via hv trap) consumes the message packet. */
	intel_uncore_write(uncore, vgtif_reg(g2v_notify), msg);

	mutex_unlock(&i915->vgpu.lock);
}

/* Index shifts into the pagetable are offset by GEN8_PTE_SHIFT [12] */
#define GEN8_PAGE_SIZE (SZ_4K) /* page and page-directory sizes are the same */
#define GEN8_PTE_SHIFT (ilog2(GEN8_PAGE_SIZE))
#define GEN8_PDES (GEN8_PAGE_SIZE / sizeof(u64))
#define gen8_pd_shift(lvl) ((lvl) * ilog2(GEN8_PDES))
#define gen8_pd_index(i, lvl) i915_pde_index((i), gen8_pd_shift(lvl))
#define __gen8_pte_shift(lvl) (GEN8_PTE_SHIFT + gen8_pd_shift(lvl))
#define __gen8_pte_index(a, lvl) i915_pde_index((a), __gen8_pte_shift(lvl))

#define as_pd(x) container_of((x), typeof(struct i915_page_directory), pt)

static unsigned int
gen8_pd_range(u64 start, u64 end, int lvl, unsigned int *idx)
{
	const int shift = gen8_pd_shift(lvl);
	const u64 mask = ~0ull << gen8_pd_shift(lvl + 1);

	GEM_BUG_ON(start >= end);
	end += ~mask >> gen8_pd_shift(1);

	*idx = i915_pde_index(start, shift);
	if ((start ^ end) & mask)
		return GEN8_PDES - *idx;
	else
		return i915_pde_index(end, shift) - *idx;
}

static bool gen8_pd_contains(u64 start, u64 end, int lvl)
{
	const u64 mask = ~0ull << gen8_pd_shift(lvl + 1);

	GEM_BUG_ON(start >= end);
	return (start ^ end) & mask && (start & ~mask) == 0;
}

static unsigned int gen8_pt_count(u64 start, u64 end)
{
	GEM_BUG_ON(start >= end);
	if ((start ^ end) >> gen8_pd_shift(1))
		return GEN8_PDES - (start & (GEN8_PDES - 1));
	else
		return end - start;
}

static unsigned int gen8_pd_top_count(const struct i915_address_space *vm)
{
	unsigned int shift = __gen8_pte_shift(vm->top);

	return (vm->total + (1ull << shift) - 1) >> shift;
}

static struct i915_page_directory *
gen8_pdp_for_page_index(struct i915_address_space * const vm, const u64 idx)
{
	struct i915_ppgtt * const ppgtt = i915_vm_to_ppgtt(vm);

	if (vm->top == 2)
		return ppgtt->pd;
	else
		return i915_pd_entry(ppgtt->pd, gen8_pd_index(idx, vm->top));
}

static struct i915_page_directory *
gen8_pdp_for_page_address(struct i915_address_space * const vm, const u64 addr)
{
	return gen8_pdp_for_page_index(vm, addr >> GEN8_PTE_SHIFT);
}

static void __gen8_ppgtt_cleanup(struct i915_address_space *vm,
				 struct i915_page_directory *pd,
				 int count, int lvl)
{
	if (lvl) {
		void **pde = pd->entry;

		do {
			if (!*pde)
				continue;

			__gen8_ppgtt_cleanup(vm, *pde, GEN8_PDES, lvl - 1);
		} while (pde++, --count);
	}

	free_px(vm, &pd->pt, lvl);
}

static void gen8_ppgtt_cleanup(struct i915_address_space *vm)
{
	struct i915_ppgtt *ppgtt = i915_vm_to_ppgtt(vm);

	if (intel_vgpu_active(vm->i915))
		gen8_ppgtt_notify_vgt(ppgtt, false);

	__gen8_ppgtt_cleanup(vm, ppgtt->pd, gen8_pd_top_count(vm), vm->top);
	free_scratch(vm);
}

static u64 __gen8_ppgtt_clear(struct i915_address_space * const vm,
			      struct i915_page_directory * const pd,
			      u64 start, const u64 end, int lvl)
{
	const struct drm_i915_gem_object * const scratch = vm->scratch[lvl];
	unsigned int idx, len;

	GEM_BUG_ON(end > vm->total >> GEN8_PTE_SHIFT);

	len = gen8_pd_range(start, end, lvl--, &idx);
	DBG("%s(%p):{ lvl:%d, start:%llx, end:%llx, idx:%d, len:%d, used:%d }\n",
	    __func__, vm, lvl + 1, start, end,
	    idx, len, atomic_read(px_used(pd)));
	GEM_BUG_ON(!len || len >= atomic_read(px_used(pd)));

	do {
		struct i915_page_table *pt = pd->entry[idx];

		if (atomic_fetch_inc(&pt->used) >> gen8_pd_shift(1) &&
		    gen8_pd_contains(start, end, lvl)) {
			DBG("%s(%p):{ lvl:%d, idx:%d, start:%llx, end:%llx } removing pd\n",
			    __func__, vm, lvl + 1, idx, start, end);
			clear_pd_entry(pd, idx, scratch);
			__gen8_ppgtt_cleanup(vm, as_pd(pt), I915_PDES, lvl);
			start += (u64)I915_PDES << gen8_pd_shift(lvl);
			continue;
		}

		if (lvl) {
			start = __gen8_ppgtt_clear(vm, as_pd(pt),
						   start, end, lvl);
		} else {
			unsigned int count;
			unsigned int pte = gen8_pd_index(start, 0);
			unsigned int num_ptes;
			u64 *vaddr;

			count = gen8_pt_count(start, end);
			DBG("%s(%p):{ lvl:%d, start:%llx, end:%llx, idx:%d, len:%d, used:%d } removing pte\n",
			    __func__, vm, lvl, start, end,
			    gen8_pd_index(start, 0), count,
			    atomic_read(&pt->used));
			GEM_BUG_ON(!count || count >= atomic_read(&pt->used));

			num_ptes = count;
			if (pt->is_compact) {
				GEM_BUG_ON(num_ptes % 16);
				GEM_BUG_ON(pte % 16);
				num_ptes /= 16;
				pte /= 16;
			}

			vaddr = px_vaddr(pt);
			memset64(vaddr + pte,
				 vm->scratch[0]->encode,
				 num_ptes);

			atomic_sub(count, &pt->used);
			start += count;
		}

		if (release_pd_entry(pd, idx, pt, scratch))
			free_px(vm, pt, lvl);
	} while (idx++, --len);

	return start;
}

static void gen8_ppgtt_clear(struct i915_address_space *vm,
			     u64 start, u64 length)
{
	GEM_BUG_ON(!IS_ALIGNED(start, BIT_ULL(GEN8_PTE_SHIFT)));
	GEM_BUG_ON(!IS_ALIGNED(length, BIT_ULL(GEN8_PTE_SHIFT)));
	GEM_BUG_ON(range_overflows(start, length, vm->total));

	start >>= GEN8_PTE_SHIFT;
	length >>= GEN8_PTE_SHIFT;
	GEM_BUG_ON(length == 0);

	__gen8_ppgtt_clear(vm, i915_vm_to_ppgtt(vm)->pd,
			   start, start + length, vm->top);
}

static void __gen8_ppgtt_alloc(struct i915_address_space * const vm,
			       struct i915_vm_pt_stash *stash,
			       struct i915_page_directory * const pd,
			       u64 * const start, const u64 end, int lvl)
{
	unsigned int idx, len;

	GEM_BUG_ON(end > vm->total >> GEN8_PTE_SHIFT);

	len = gen8_pd_range(*start, end, lvl--, &idx);
	DBG("%s(%p):{ lvl:%d, start:%llx, end:%llx, idx:%d, len:%d, used:%d }\n",
	    __func__, vm, lvl + 1, *start, end,
	    idx, len, atomic_read(px_used(pd)));
	GEM_BUG_ON(!len || (idx + len - 1) >> gen8_pd_shift(1));

	spin_lock(&pd->lock);
	GEM_BUG_ON(!atomic_read(px_used(pd))); /* Must be pinned! */
	do {
		struct i915_page_table *pt = pd->entry[idx];

		if (!pt) {
			spin_unlock(&pd->lock);

			DBG("%s(%p):{ lvl:%d, idx:%d } allocating new tree\n",
			    __func__, vm, lvl + 1, idx);

			pt = stash->pt[!!lvl];
			__i915_gem_object_pin_pages(pt->base);

			fill_px(pt, vm->scratch[lvl]->encode);

			spin_lock(&pd->lock);
			if (likely(!pd->entry[idx])) {
				stash->pt[!!lvl] = pt->stash;
				atomic_set(&pt->used, 0);
				set_pd_entry(pd, idx, pt);
			} else {
				pt = pd->entry[idx];
			}
		}

		if (lvl) {
			atomic_inc(&pt->used);
			spin_unlock(&pd->lock);

			__gen8_ppgtt_alloc(vm, stash,
					   as_pd(pt), start, end, lvl);

			spin_lock(&pd->lock);
			atomic_dec(&pt->used);
			GEM_BUG_ON(!atomic_read(&pt->used));
		} else {
			unsigned int count = gen8_pt_count(*start, end);

			DBG("%s(%p):{ lvl:%d, start:%llx, end:%llx, idx:%d, len:%d, used:%d } inserting pte\n",
			    __func__, vm, lvl, *start, end,
			    gen8_pd_index(*start, 0), count,
			    atomic_read(&pt->used));

			atomic_add(count, &pt->used);
			/* All other pdes may be simultaneously removed */
			GEM_BUG_ON(atomic_read(&pt->used) > NALLOC * I915_PDES);
			*start += count;
		}
	} while (idx++, --len);
	spin_unlock(&pd->lock);
}

static void gen8_ppgtt_alloc(struct i915_address_space *vm,
			     struct i915_vm_pt_stash *stash,
			     u64 start, u64 length)
{
	GEM_BUG_ON(!IS_ALIGNED(start, BIT_ULL(GEN8_PTE_SHIFT)));
	GEM_BUG_ON(!IS_ALIGNED(length, BIT_ULL(GEN8_PTE_SHIFT)));
	GEM_BUG_ON(range_overflows(start, length, vm->total));

	start >>= GEN8_PTE_SHIFT;
	length >>= GEN8_PTE_SHIFT;
	GEM_BUG_ON(length == 0);

	__gen8_ppgtt_alloc(vm, stash, i915_vm_to_ppgtt(vm)->pd,
			   &start, start + length, vm->top);
}

static void __gen8_ppgtt_foreach(struct i915_address_space *vm,
				 struct i915_page_directory *pd,
				 u64 *start, u64 end, int lvl,
				 void (*fn)(struct i915_address_space *vm,
					    struct i915_page_table *pt,
					    void *data),
				 void *data)
{
	unsigned int idx, len;

	len = gen8_pd_range(*start, end, lvl--, &idx);

	spin_lock(&pd->lock);
	do {
		struct i915_page_table *pt = pd->entry[idx];

		atomic_inc(&pt->used);
		spin_unlock(&pd->lock);

		if (lvl) {
			__gen8_ppgtt_foreach(vm, as_pd(pt), start, end, lvl,
					     fn, data);
		} else {
			fn(vm, pt, data);
			*start += gen8_pt_count(*start, end);
		}

		spin_lock(&pd->lock);
		atomic_dec(&pt->used);
	} while (idx++, --len);
	spin_unlock(&pd->lock);
}

static void gen8_ppgtt_foreach(struct i915_address_space *vm,
			       u64 start, u64 length,
			       void (*fn)(struct i915_address_space *vm,
					  struct i915_page_table *pt,
					  void *data),
			       void *data)
{
	start >>= GEN8_PTE_SHIFT;
	length >>= GEN8_PTE_SHIFT;

	__gen8_ppgtt_foreach(vm, i915_vm_to_ppgtt(vm)->pd,
			     &start, start + length, vm->top,
			     fn, data);
}

static __always_inline u64
gen8_ppgtt_insert_pte(struct i915_ppgtt *ppgtt,
		      struct i915_page_directory *pdp,
		      struct sgt_dma *iter,
		      u64 idx,
		      enum i915_cache_level cache_level,
		      u32 flags)
{
	struct i915_page_directory *pd;
	const gen8_pte_t pte_encode = gen8_pte_encode(0, cache_level, flags);
	gen8_pte_t *vaddr;

	pd = i915_pd_entry(pdp, gen8_pd_index(idx, 2));
	vaddr = px_vaddr(i915_pt_entry(pd, gen8_pd_index(idx, 1)));
	do {
		GEM_BUG_ON(sg_dma_len(iter->sg) < I915_GTT_PAGE_SIZE);
		vaddr[gen8_pd_index(idx, 0)] = pte_encode | iter->dma;

		iter->dma += I915_GTT_PAGE_SIZE;
		if (iter->dma >= iter->max) {
			iter->sg = __sg_next(iter->sg);
			if (!iter->sg || sg_dma_len(iter->sg) == 0) {
				idx = 0;
				break;
			}

			iter->dma = sg_dma_address(iter->sg);
			iter->max = iter->dma + sg_dma_len(iter->sg);
		}

		if (gen8_pd_index(++idx, 0) == 0) {
			if (gen8_pd_index(idx, 1) == 0) {
				/* Limited by sg length for 3lvl */
				if (gen8_pd_index(idx, 2) == 0)
					break;

				pd = pdp->entry[gen8_pd_index(idx, 2)];
			}

			clflush_cache_range(vaddr, PAGE_SIZE);
			vaddr = px_vaddr(i915_pt_entry(pd, gen8_pd_index(idx, 1)));
		}
	} while (1);
	clflush_cache_range(vaddr, PAGE_SIZE);

	return idx;
}

static void
xehpsdv_ppgtt_insert_huge(struct i915_address_space *vm,
			  struct i915_vma_resource *vma_res,
			  struct sgt_dma *iter,
			  enum i915_cache_level cache_level,
			  u32 flags)
{
	const gen8_pte_t pte_encode = vm->pte_encode(0, cache_level, flags);
	unsigned int rem = sg_dma_len(iter->sg);
	u64 start = vma_res->start;

	GEM_BUG_ON(!i915_vm_is_4lvl(vm));

	do {
		struct i915_page_directory * const pdp =
			gen8_pdp_for_page_address(vm, start);
		struct i915_page_directory * const pd =
			i915_pd_entry(pdp, __gen8_pte_index(start, 2));
		struct i915_page_table *pt =
			i915_pt_entry(pd, __gen8_pte_index(start, 1));
		gen8_pte_t encode = pte_encode;
		unsigned int page_size;
		gen8_pte_t *vaddr;
		u16 index, max;

		max = I915_PDES;

		if (vma_res->bi.page_sizes.sg & I915_GTT_PAGE_SIZE_2M &&
		    IS_ALIGNED(iter->dma, I915_GTT_PAGE_SIZE_2M) &&
		    rem >= I915_GTT_PAGE_SIZE_2M &&
		    !__gen8_pte_index(start, 0)) {
			index = __gen8_pte_index(start, 1);
			encode |= GEN8_PDE_PS_2M;
			page_size = I915_GTT_PAGE_SIZE_2M;

			vaddr = px_vaddr(pd);
		} else {
			if (encode & GEN12_PPGTT_PTE_LM) {
				GEM_BUG_ON(__gen8_pte_index(start, 0) % 16);
				GEM_BUG_ON(rem < I915_GTT_PAGE_SIZE_64K);
				GEM_BUG_ON(!IS_ALIGNED(iter->dma,
						       I915_GTT_PAGE_SIZE_64K));

				index = __gen8_pte_index(start, 0) / 16;
				page_size = I915_GTT_PAGE_SIZE_64K;

				max /= 16;

				vaddr = px_vaddr(pd);
				vaddr[__gen8_pte_index(start, 1)] |= GEN12_PDE_64K;

				pt->is_compact = true;
			} else {
				GEM_BUG_ON(pt->is_compact);
				index =  __gen8_pte_index(start, 0);
				page_size = I915_GTT_PAGE_SIZE;
			}

			vaddr = px_vaddr(pt);
		}

		do {
			GEM_BUG_ON(rem < page_size);
			vaddr[index++] = encode | iter->dma;

			start += page_size;
			iter->dma += page_size;
			rem -= page_size;
			if (iter->dma >= iter->max) {
				iter->sg = __sg_next(iter->sg);
				if (!iter->sg)
					break;

				rem = sg_dma_len(iter->sg);
				if (!rem)
					break;

				iter->dma = sg_dma_address(iter->sg);
				iter->max = iter->dma + rem;

				if (unlikely(!IS_ALIGNED(iter->dma, page_size)))
					break;
			}
		} while (rem >= page_size && index < max);

		vma_res->page_sizes_gtt |= page_size;
	} while (iter->sg && sg_dma_len(iter->sg));
}

static void gen8_ppgtt_insert_huge(struct i915_address_space *vm,
				   struct i915_vma_resource *vma_res,
				   struct sgt_dma *iter,
				   enum i915_cache_level cache_level,
				   u32 flags)
{
	const gen8_pte_t pte_encode = gen8_pte_encode(0, cache_level, flags);
	unsigned int rem = sg_dma_len(iter->sg);
	u64 start = vma_res->start;

	GEM_BUG_ON(!i915_vm_is_4lvl(vm));

	do {
		struct i915_page_directory * const pdp =
			gen8_pdp_for_page_address(vm, start);
		struct i915_page_directory * const pd =
			i915_pd_entry(pdp, __gen8_pte_index(start, 2));
		gen8_pte_t encode = pte_encode;
		unsigned int maybe_64K = -1;
		unsigned int page_size;
		gen8_pte_t *vaddr;
		u16 index;

		if (vma_res->bi.page_sizes.sg & I915_GTT_PAGE_SIZE_2M &&
		    IS_ALIGNED(iter->dma, I915_GTT_PAGE_SIZE_2M) &&
		    rem >= I915_GTT_PAGE_SIZE_2M &&
		    !__gen8_pte_index(start, 0)) {
			index = __gen8_pte_index(start, 1);
			encode |= GEN8_PDE_PS_2M;
			page_size = I915_GTT_PAGE_SIZE_2M;

			vaddr = px_vaddr(pd);
		} else {
			struct i915_page_table *pt =
				i915_pt_entry(pd, __gen8_pte_index(start, 1));

			index = __gen8_pte_index(start, 0);
			page_size = I915_GTT_PAGE_SIZE;

			if (!index &&
			    vma_res->bi.page_sizes.sg & I915_GTT_PAGE_SIZE_64K &&
			    IS_ALIGNED(iter->dma, I915_GTT_PAGE_SIZE_64K) &&
			    (IS_ALIGNED(rem, I915_GTT_PAGE_SIZE_64K) ||
			     rem >= (I915_PDES - index) * I915_GTT_PAGE_SIZE))
				maybe_64K = __gen8_pte_index(start, 1);

			vaddr = px_vaddr(pt);
		}

		do {
			GEM_BUG_ON(sg_dma_len(iter->sg) < page_size);
			vaddr[index++] = encode | iter->dma;

			start += page_size;
			iter->dma += page_size;
			rem -= page_size;
			if (iter->dma >= iter->max) {
				iter->sg = __sg_next(iter->sg);
				if (!iter->sg)
					break;

				rem = sg_dma_len(iter->sg);
				if (!rem)
					break;

				iter->dma = sg_dma_address(iter->sg);
				iter->max = iter->dma + rem;

				if (maybe_64K != -1 && index < I915_PDES &&
				    !(IS_ALIGNED(iter->dma, I915_GTT_PAGE_SIZE_64K) &&
				      (IS_ALIGNED(rem, I915_GTT_PAGE_SIZE_64K) ||
				       rem >= (I915_PDES - index) * I915_GTT_PAGE_SIZE)))
					maybe_64K = -1;

				if (unlikely(!IS_ALIGNED(iter->dma, page_size)))
					break;
			}
		} while (rem >= page_size && index < I915_PDES);

		clflush_cache_range(vaddr, PAGE_SIZE);

		/*
		 * Is it safe to mark the 2M block as 64K? -- Either we have
		 * filled whole page-table with 64K entries, or filled part of
		 * it and have reached the end of the sg table and we have
		 * enough padding.
		 */
		if (maybe_64K != -1 &&
		    (index == I915_PDES ||
		     (i915_vm_has_scratch_64K(vm) &&
		      !iter->sg && IS_ALIGNED(vma_res->start +
					      vma_res->node_size,
					      I915_GTT_PAGE_SIZE_2M)))) {
			vaddr = px_vaddr(pd);
			vaddr[maybe_64K] |= GEN8_PDE_IPS_64K;
			clflush_cache_range(vaddr, PAGE_SIZE);
			page_size = I915_GTT_PAGE_SIZE_64K;

			/*
			 * We write all 4K page entries, even when using 64K
			 * pages. In order to verify that the HW isn't cheating
			 * by using the 4K PTE instead of the 64K PTE, we want
			 * to remove all the surplus entries. If the HW skipped
			 * the 64K PTE, it will read/write into the scratch page
			 * instead - which we detect as missing results during
			 * selftests.
			 */
			if (I915_SELFTEST_ONLY(vm->scrub_64K)) {
				u16 i;

				encode = vm->scratch[0]->encode;
				vaddr = px_vaddr(i915_pt_entry(pd, maybe_64K));

				for (i = 1; i < index; i += 16)
					memset64(vaddr + i, encode, 15);

				clflush_cache_range(vaddr, PAGE_SIZE);
			}
		}

		vma_res->page_sizes_gtt |= page_size;
	} while (iter->sg && sg_dma_len(iter->sg));
}

static void gen8_ppgtt_insert(struct i915_address_space *vm,
			      struct i915_vma_resource *vma_res,
			      enum i915_cache_level cache_level,
			      u32 flags)
{
	struct i915_ppgtt * const ppgtt = i915_vm_to_ppgtt(vm);
	struct sgt_dma iter = sgt_dma(vma_res);

	if (vma_res->bi.page_sizes.sg > I915_GTT_PAGE_SIZE) {
		if (HAS_64K_PAGES(vm->i915))
			xehpsdv_ppgtt_insert_huge(vm, vma_res, &iter, cache_level, flags);
		else
			gen8_ppgtt_insert_huge(vm, vma_res, &iter, cache_level, flags);
	} else  {
		u64 idx = vma_res->start >> GEN8_PTE_SHIFT;

		do {
			struct i915_page_directory * const pdp =
				gen8_pdp_for_page_index(vm, idx);

			idx = gen8_ppgtt_insert_pte(ppgtt, pdp, &iter, idx,
						    cache_level, flags);
		} while (idx);

		vma_res->page_sizes_gtt = I915_GTT_PAGE_SIZE;
	}
}

static void gen8_ppgtt_insert_entry(struct i915_address_space *vm,
				    dma_addr_t addr,
				    u64 offset,
				    enum i915_cache_level level,
				    u32 flags)
{
	u64 idx = offset >> GEN8_PTE_SHIFT;
	struct i915_page_directory * const pdp =
		gen8_pdp_for_page_index(vm, idx);
	struct i915_page_directory *pd =
		i915_pd_entry(pdp, gen8_pd_index(idx, 2));
	struct i915_page_table *pt = i915_pt_entry(pd, gen8_pd_index(idx, 1));
	gen8_pte_t *vaddr;

	GEM_BUG_ON(pt->is_compact);

	vaddr = px_vaddr(pt);
	vaddr[gen8_pd_index(idx, 0)] = gen8_pte_encode(addr, level, flags);
	clflush_cache_range(&vaddr[gen8_pd_index(idx, 0)], sizeof(*vaddr));
}

static void __xehpsdv_ppgtt_insert_entry_lm(struct i915_address_space *vm,
					    dma_addr_t addr,
					    u64 offset,
					    enum i915_cache_level level,
					    u32 flags)
{
	u64 idx = offset >> GEN8_PTE_SHIFT;
	struct i915_page_directory * const pdp =
		gen8_pdp_for_page_index(vm, idx);
	struct i915_page_directory *pd =
		i915_pd_entry(pdp, gen8_pd_index(idx, 2));
	struct i915_page_table *pt = i915_pt_entry(pd, gen8_pd_index(idx, 1));
	gen8_pte_t *vaddr;

	GEM_BUG_ON(!IS_ALIGNED(addr, SZ_64K));
	GEM_BUG_ON(!IS_ALIGNED(offset, SZ_64K));

	if (!pt->is_compact) {
		vaddr = px_vaddr(pd);
		vaddr[gen8_pd_index(idx, 1)] |= GEN12_PDE_64K;
		pt->is_compact = true;
	}

	vaddr = px_vaddr(pt);
	vaddr[gen8_pd_index(idx, 0) / 16] = gen8_pte_encode(addr, level, flags);
}

static void xehpsdv_ppgtt_insert_entry(struct i915_address_space *vm,
				       dma_addr_t addr,
				       u64 offset,
				       enum i915_cache_level level,
				       u32 flags)
{
	if (flags & PTE_LM)
		return __xehpsdv_ppgtt_insert_entry_lm(vm, addr, offset,
						       level, flags);

	return gen8_ppgtt_insert_entry(vm, addr, offset, level, flags);
}

static int gen8_init_scratch(struct i915_address_space *vm)
{
	u32 pte_flags;
	int ret;
	int i;

	/*
	 * If everybody agrees to not to write into the scratch page,
	 * we can reuse it for all vm, keeping contexts and processes separate.
	 */
	if (vm->has_read_only && vm->gt->vm && !i915_is_ggtt(vm->gt->vm)) {
		struct i915_address_space *clone = vm->gt->vm;

		GEM_BUG_ON(!clone->has_read_only);

		vm->scratch_order = clone->scratch_order;
		for (i = 0; i <= vm->top; i++)
			vm->scratch[i] = i915_gem_object_get(clone->scratch[i]);

		return 0;
	}

	ret = setup_scratch_page(vm);
	if (ret)
		return ret;

	pte_flags = vm->has_read_only;
	if (i915_gem_object_is_lmem(vm->scratch[0]))
		pte_flags |= PTE_LM;

	vm->scratch[0]->encode =
		gen8_pte_encode(px_dma(vm->scratch[0]),
				I915_CACHE_NONE, pte_flags);

	for (i = 1; i <= vm->top; i++) {
		struct drm_i915_gem_object *obj;

		obj = vm->alloc_pt_dma(vm, I915_GTT_PAGE_SIZE_4K);
		if (IS_ERR(obj))
			goto free_scratch;

		ret = map_pt_dma(vm, obj);
		if (ret) {
			i915_gem_object_put(obj);
			goto free_scratch;
		}

		fill_px(obj, vm->scratch[i - 1]->encode);
		obj->encode = gen8_pde_encode(px_dma(obj), I915_CACHE_NONE);

		vm->scratch[i] = obj;
	}

	return 0;

free_scratch:
	while (i--)
		i915_gem_object_put(vm->scratch[i]);
	return -ENOMEM;
}

static int gen8_preallocate_top_level_pdp(struct i915_ppgtt *ppgtt)
{
	struct i915_address_space *vm = &ppgtt->vm;
	struct i915_page_directory *pd = ppgtt->pd;
	unsigned int idx;

	GEM_BUG_ON(vm->top != 2);
	GEM_BUG_ON(gen8_pd_top_count(vm) != GEN8_3LVL_PDPES);

	for (idx = 0; idx < GEN8_3LVL_PDPES; idx++) {
		struct i915_page_directory *pde;
		int err;

		pde = alloc_pd(vm);
		if (IS_ERR(pde))
			return PTR_ERR(pde);

		err = map_pt_dma(vm, pde->pt.base);
		if (err) {
			free_pd(vm, pde);
			return err;
		}

		fill_px(pde, vm->scratch[1]->encode);
		set_pd_entry(pd, idx, pde);
		atomic_inc(px_used(pde)); /* keep pinned */
	}
	wmb();

	return 0;
}

static struct i915_page_directory *
gen8_alloc_top_pd(struct i915_address_space *vm)
{
	const unsigned int count = gen8_pd_top_count(vm);
	struct i915_page_directory *pd;
	int err;

	GEM_BUG_ON(count > I915_PDES);

	pd = __alloc_pd(count);
	if (unlikely(!pd))
		return ERR_PTR(-ENOMEM);

	pd->pt.base = vm->alloc_pt_dma(vm, I915_GTT_PAGE_SIZE_4K);
	if (IS_ERR(pd->pt.base)) {
		err = PTR_ERR(pd->pt.base);
		pd->pt.base = NULL;
		goto err_pd;
	}

	err = map_pt_dma(vm, pd->pt.base);
	if (err)
		goto err_pd;

	fill_page_dma(px_base(pd), vm->scratch[vm->top]->encode, count);
	atomic_inc(px_used(pd)); /* mark as pinned */
	return pd;

err_pd:
	free_pd(vm, pd);
	return ERR_PTR(err);
}

/*
 * GEN8 legacy ppgtt programming is accomplished through a max 4 PDP registers
 * with a net effect resembling a 2-level page table in normal x86 terms. Each
 * PDP represents 1GB of memory 4 * 512 * 512 * 4096 = 4GB legacy 32b address
 * space.
 *
 */
struct i915_ppgtt *gen8_ppgtt_create(struct intel_gt *gt,
				     unsigned long lmem_pt_obj_flags)
{
	struct i915_ppgtt *ppgtt;
	int err;

	ppgtt = kzalloc(sizeof(*ppgtt), GFP_KERNEL);
	if (!ppgtt)
		return ERR_PTR(-ENOMEM);

	ppgtt_init(ppgtt, gt, lmem_pt_obj_flags);
	ppgtt->vm.top = i915_vm_is_4lvl(&ppgtt->vm) ? 3 : 2;
	ppgtt->vm.pd_shift = ilog2(SZ_4K * SZ_4K / sizeof(gen8_pte_t));

	/*
	 * From bdw, there is hw support for read-only pages in the PPGTT.
	 *
	 * Gen11 has HSDES#:1807136187 unresolved. Disable ro support
	 * for now.
	 *
	 * Gen12 has inherited the same read-only fault issue from gen11.
	 */
	ppgtt->vm.has_read_only = !IS_GRAPHICS_VER(gt->i915, 11, 12);

	if (HAS_LMEM(gt->i915)) {
		ppgtt->vm.alloc_pt_dma = alloc_pt_lmem;

		/*
		 * On some platforms the hw has dropped support for 4K GTT pages
		 * when dealing with LMEM, and due to the design of 64K GTT
		 * pages in the hw, we can only mark the *entire* page-table as
		 * operating in 64K GTT mode, since the enable bit is still on
		 * the pde, and not the pte. And since we still need to allow
		 * 4K GTT pages for SMEM objects, we can't have a "normal" 4K
		 * page-table with scratch pointing to LMEM, since that's
		 * undefined from the hw pov. The simplest solution is to just
		 * move the 64K scratch page to SMEM on such platforms and call
		 * it a day, since that should work for all configurations.
		 */
		if (HAS_64K_PAGES(gt->i915))
			ppgtt->vm.alloc_scratch_dma = alloc_pt_dma;
		else
			ppgtt->vm.alloc_scratch_dma = alloc_pt_lmem;
	} else {
		ppgtt->vm.alloc_pt_dma = alloc_pt_dma;
		ppgtt->vm.alloc_scratch_dma = alloc_pt_dma;
	}

	err = gen8_init_scratch(&ppgtt->vm);
	if (err)
		goto err_free;

	ppgtt->pd = gen8_alloc_top_pd(&ppgtt->vm);
	if (IS_ERR(ppgtt->pd)) {
		err = PTR_ERR(ppgtt->pd);
		goto err_free_scratch;
	}

	if (!i915_vm_is_4lvl(&ppgtt->vm)) {
		err = gen8_preallocate_top_level_pdp(ppgtt);
		if (err)
			goto err_free_pd;
	}

	ppgtt->vm.bind_async_flags = I915_VMA_LOCAL_BIND;
	ppgtt->vm.insert_entries = gen8_ppgtt_insert;
	if (HAS_64K_PAGES(gt->i915))
		ppgtt->vm.insert_page = xehpsdv_ppgtt_insert_entry;
	else
		ppgtt->vm.insert_page = gen8_ppgtt_insert_entry;
	ppgtt->vm.allocate_va_range = gen8_ppgtt_alloc;
	ppgtt->vm.clear_range = gen8_ppgtt_clear;
	ppgtt->vm.foreach = gen8_ppgtt_foreach;

	ppgtt->vm.pte_encode = gen8_pte_encode;

	if (intel_vgpu_active(gt->i915))
		gen8_ppgtt_notify_vgt(ppgtt, true);

	ppgtt->vm.cleanup = gen8_ppgtt_cleanup;

	return ppgtt;

err_free_pd:
	__gen8_ppgtt_cleanup(&ppgtt->vm, ppgtt->pd,
			     gen8_pd_top_count(&ppgtt->vm), ppgtt->vm.top);
err_free_scratch:
	free_scratch(&ppgtt->vm);
err_free:
	kfree(ppgtt);
	return ERR_PTR(err);
}
