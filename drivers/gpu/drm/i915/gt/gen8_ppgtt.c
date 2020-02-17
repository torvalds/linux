// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020 Intel Corporation
 */

#include <linux/log2.h>

#include "gen8_ppgtt.h"
#include "i915_scatterlist.h"
#include "i915_trace.h"
#include "i915_vgpu.h"
#include "intel_gt.h"
#include "intel_gtt.h"

static u64 gen8_pde_encode(const dma_addr_t addr,
			   const enum i915_cache_level level)
{
	u64 pde = addr | _PAGE_PRESENT | _PAGE_RW;

	if (level != I915_CACHE_NONE)
		pde |= PPAT_CACHED_PDE;
	else
		pde |= PPAT_UNCACHED;

	return pde;
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

static inline unsigned int
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

static inline bool gen8_pd_contains(u64 start, u64 end, int lvl)
{
	const u64 mask = ~0ull << gen8_pd_shift(lvl + 1);

	GEM_BUG_ON(start >= end);
	return (start ^ end) & mask && (start & ~mask) == 0;
}

static inline unsigned int gen8_pt_count(u64 start, u64 end)
{
	GEM_BUG_ON(start >= end);
	if ((start ^ end) >> gen8_pd_shift(1))
		return GEN8_PDES - (start & (GEN8_PDES - 1));
	else
		return end - start;
}

static inline unsigned int
gen8_pd_top_count(const struct i915_address_space *vm)
{
	unsigned int shift = __gen8_pte_shift(vm->top);
	return (vm->total + (1ull << shift) - 1) >> shift;
}

static inline struct i915_page_directory *
gen8_pdp_for_page_index(struct i915_address_space * const vm, const u64 idx)
{
	struct i915_ppgtt * const ppgtt = i915_vm_to_ppgtt(vm);

	if (vm->top == 2)
		return ppgtt->pd;
	else
		return i915_pd_entry(ppgtt->pd, gen8_pd_index(idx, vm->top));
}

static inline struct i915_page_directory *
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

	free_px(vm, pd);
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
	const struct i915_page_scratch * const scratch = &vm->scratch[lvl];
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
			u64 *vaddr;

			count = gen8_pt_count(start, end);
			DBG("%s(%p):{ lvl:%d, start:%llx, end:%llx, idx:%d, len:%d, used:%d } removing pte\n",
			    __func__, vm, lvl, start, end,
			    gen8_pd_index(start, 0), count,
			    atomic_read(&pt->used));
			GEM_BUG_ON(!count || count >= atomic_read(&pt->used));

			vaddr = kmap_atomic_px(pt);
			memset64(vaddr + gen8_pd_index(start, 0),
				 vm->scratch[0].encode,
				 count);
			kunmap_atomic(vaddr);

			atomic_sub(count, &pt->used);
			start += count;
		}

		if (release_pd_entry(pd, idx, pt, scratch))
			free_px(vm, pt);
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

static int __gen8_ppgtt_alloc(struct i915_address_space * const vm,
			      struct i915_page_directory * const pd,
			      u64 * const start, const u64 end, int lvl)
{
	const struct i915_page_scratch * const scratch = &vm->scratch[lvl];
	struct i915_page_table *alloc = NULL;
	unsigned int idx, len;
	int ret = 0;

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

			pt = fetch_and_zero(&alloc);
			if (lvl) {
				if (!pt) {
					pt = &alloc_pd(vm)->pt;
					if (IS_ERR(pt)) {
						ret = PTR_ERR(pt);
						goto out;
					}
				}

				fill_px(pt, vm->scratch[lvl].encode);
			} else {
				if (!pt) {
					pt = alloc_pt(vm);
					if (IS_ERR(pt)) {
						ret = PTR_ERR(pt);
						goto out;
					}
				}

				if (intel_vgpu_active(vm->i915) ||
				    gen8_pt_count(*start, end) < I915_PDES)
					fill_px(pt, vm->scratch[lvl].encode);
			}

			spin_lock(&pd->lock);
			if (likely(!pd->entry[idx]))
				set_pd_entry(pd, idx, pt);
			else
				alloc = pt, pt = pd->entry[idx];
		}

		if (lvl) {
			atomic_inc(&pt->used);
			spin_unlock(&pd->lock);

			ret = __gen8_ppgtt_alloc(vm, as_pd(pt),
						 start, end, lvl);
			if (unlikely(ret)) {
				if (release_pd_entry(pd, idx, pt, scratch))
					free_px(vm, pt);
				goto out;
			}

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
out:
	if (alloc)
		free_px(vm, alloc);
	return ret;
}

static int gen8_ppgtt_alloc(struct i915_address_space *vm,
			    u64 start, u64 length)
{
	u64 from;
	int err;

	GEM_BUG_ON(!IS_ALIGNED(start, BIT_ULL(GEN8_PTE_SHIFT)));
	GEM_BUG_ON(!IS_ALIGNED(length, BIT_ULL(GEN8_PTE_SHIFT)));
	GEM_BUG_ON(range_overflows(start, length, vm->total));

	start >>= GEN8_PTE_SHIFT;
	length >>= GEN8_PTE_SHIFT;
	GEM_BUG_ON(length == 0);
	from = start;

	err = __gen8_ppgtt_alloc(vm, i915_vm_to_ppgtt(vm)->pd,
				 &start, start + length, vm->top);
	if (unlikely(err && from != start))
		__gen8_ppgtt_clear(vm, i915_vm_to_ppgtt(vm)->pd,
				   from, start, vm->top);

	return err;
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
	vaddr = kmap_atomic_px(i915_pt_entry(pd, gen8_pd_index(idx, 1)));
	do {
		GEM_BUG_ON(iter->sg->length < I915_GTT_PAGE_SIZE);
		vaddr[gen8_pd_index(idx, 0)] = pte_encode | iter->dma;

		iter->dma += I915_GTT_PAGE_SIZE;
		if (iter->dma >= iter->max) {
			iter->sg = __sg_next(iter->sg);
			if (!iter->sg) {
				idx = 0;
				break;
			}

			iter->dma = sg_dma_address(iter->sg);
			iter->max = iter->dma + iter->sg->length;
		}

		if (gen8_pd_index(++idx, 0) == 0) {
			if (gen8_pd_index(idx, 1) == 0) {
				/* Limited by sg length for 3lvl */
				if (gen8_pd_index(idx, 2) == 0)
					break;

				pd = pdp->entry[gen8_pd_index(idx, 2)];
			}

			kunmap_atomic(vaddr);
			vaddr = kmap_atomic_px(i915_pt_entry(pd, gen8_pd_index(idx, 1)));
		}
	} while (1);
	kunmap_atomic(vaddr);

	return idx;
}

static void gen8_ppgtt_insert_huge(struct i915_vma *vma,
				   struct sgt_dma *iter,
				   enum i915_cache_level cache_level,
				   u32 flags)
{
	const gen8_pte_t pte_encode = gen8_pte_encode(0, cache_level, flags);
	u64 start = vma->node.start;
	dma_addr_t rem = iter->sg->length;

	GEM_BUG_ON(!i915_vm_is_4lvl(vma->vm));

	do {
		struct i915_page_directory * const pdp =
			gen8_pdp_for_page_address(vma->vm, start);
		struct i915_page_directory * const pd =
			i915_pd_entry(pdp, __gen8_pte_index(start, 2));
		gen8_pte_t encode = pte_encode;
		unsigned int maybe_64K = -1;
		unsigned int page_size;
		gen8_pte_t *vaddr;
		u16 index;

		if (vma->page_sizes.sg & I915_GTT_PAGE_SIZE_2M &&
		    IS_ALIGNED(iter->dma, I915_GTT_PAGE_SIZE_2M) &&
		    rem >= I915_GTT_PAGE_SIZE_2M &&
		    !__gen8_pte_index(start, 0)) {
			index = __gen8_pte_index(start, 1);
			encode |= GEN8_PDE_PS_2M;
			page_size = I915_GTT_PAGE_SIZE_2M;

			vaddr = kmap_atomic_px(pd);
		} else {
			struct i915_page_table *pt =
				i915_pt_entry(pd, __gen8_pte_index(start, 1));

			index = __gen8_pte_index(start, 0);
			page_size = I915_GTT_PAGE_SIZE;

			if (!index &&
			    vma->page_sizes.sg & I915_GTT_PAGE_SIZE_64K &&
			    IS_ALIGNED(iter->dma, I915_GTT_PAGE_SIZE_64K) &&
			    (IS_ALIGNED(rem, I915_GTT_PAGE_SIZE_64K) ||
			     rem >= (I915_PDES - index) * I915_GTT_PAGE_SIZE))
				maybe_64K = __gen8_pte_index(start, 1);

			vaddr = kmap_atomic_px(pt);
		}

		do {
			GEM_BUG_ON(iter->sg->length < page_size);
			vaddr[index++] = encode | iter->dma;

			start += page_size;
			iter->dma += page_size;
			rem -= page_size;
			if (iter->dma >= iter->max) {
				iter->sg = __sg_next(iter->sg);
				if (!iter->sg)
					break;

				rem = iter->sg->length;
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

		kunmap_atomic(vaddr);

		/*
		 * Is it safe to mark the 2M block as 64K? -- Either we have
		 * filled whole page-table with 64K entries, or filled part of
		 * it and have reached the end of the sg table and we have
		 * enough padding.
		 */
		if (maybe_64K != -1 &&
		    (index == I915_PDES ||
		     (i915_vm_has_scratch_64K(vma->vm) &&
		      !iter->sg && IS_ALIGNED(vma->node.start +
					      vma->node.size,
					      I915_GTT_PAGE_SIZE_2M)))) {
			vaddr = kmap_atomic_px(pd);
			vaddr[maybe_64K] |= GEN8_PDE_IPS_64K;
			kunmap_atomic(vaddr);
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
			if (I915_SELFTEST_ONLY(vma->vm->scrub_64K)) {
				u16 i;

				encode = vma->vm->scratch[0].encode;
				vaddr = kmap_atomic_px(i915_pt_entry(pd, maybe_64K));

				for (i = 1; i < index; i += 16)
					memset64(vaddr + i, encode, 15);

				kunmap_atomic(vaddr);
			}
		}

		vma->page_sizes.gtt |= page_size;
	} while (iter->sg);
}

static void gen8_ppgtt_insert(struct i915_address_space *vm,
			      struct i915_vma *vma,
			      enum i915_cache_level cache_level,
			      u32 flags)
{
	struct i915_ppgtt * const ppgtt = i915_vm_to_ppgtt(vm);
	struct sgt_dma iter = sgt_dma(vma);

	if (vma->page_sizes.sg > I915_GTT_PAGE_SIZE) {
		gen8_ppgtt_insert_huge(vma, &iter, cache_level, flags);
	} else  {
		u64 idx = vma->node.start >> GEN8_PTE_SHIFT;

		do {
			struct i915_page_directory * const pdp =
				gen8_pdp_for_page_index(vm, idx);

			idx = gen8_ppgtt_insert_pte(ppgtt, pdp, &iter, idx,
						    cache_level, flags);
		} while (idx);

		vma->page_sizes.gtt = I915_GTT_PAGE_SIZE;
	}
}

static int gen8_init_scratch(struct i915_address_space *vm)
{
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
		memcpy(vm->scratch, clone->scratch, sizeof(vm->scratch));
		px_dma(&vm->scratch[0]) = 0; /* no xfer of ownership */
		return 0;
	}

	ret = setup_scratch_page(vm, __GFP_HIGHMEM);
	if (ret)
		return ret;

	vm->scratch[0].encode =
		gen8_pte_encode(px_dma(&vm->scratch[0]),
				I915_CACHE_LLC, vm->has_read_only);

	for (i = 1; i <= vm->top; i++) {
		if (unlikely(setup_page_dma(vm, px_base(&vm->scratch[i]))))
			goto free_scratch;

		fill_px(&vm->scratch[i], vm->scratch[i - 1].encode);
		vm->scratch[i].encode =
			gen8_pde_encode(px_dma(&vm->scratch[i]),
					I915_CACHE_LLC);
	}

	return 0;

free_scratch:
	free_scratch(vm);
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

		pde = alloc_pd(vm);
		if (IS_ERR(pde))
			return PTR_ERR(pde);

		fill_px(pde, vm->scratch[1].encode);
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

	GEM_BUG_ON(count > ARRAY_SIZE(pd->entry));

	pd = __alloc_pd(offsetof(typeof(*pd), entry[count]));
	if (unlikely(!pd))
		return ERR_PTR(-ENOMEM);

	if (unlikely(setup_page_dma(vm, px_base(pd)))) {
		kfree(pd);
		return ERR_PTR(-ENOMEM);
	}

	fill_page_dma(px_base(pd), vm->scratch[vm->top].encode, count);
	atomic_inc(px_used(pd)); /* mark as pinned */
	return pd;
}

/*
 * GEN8 legacy ppgtt programming is accomplished through a max 4 PDP registers
 * with a net effect resembling a 2-level page table in normal x86 terms. Each
 * PDP represents 1GB of memory 4 * 512 * 512 * 4096 = 4GB legacy 32b address
 * space.
 *
 */
struct i915_ppgtt *gen8_ppgtt_create(struct intel_gt *gt)
{
	struct i915_ppgtt *ppgtt;
	int err;

	ppgtt = kzalloc(sizeof(*ppgtt), GFP_KERNEL);
	if (!ppgtt)
		return ERR_PTR(-ENOMEM);

	ppgtt_init(ppgtt, gt);
	ppgtt->vm.top = i915_vm_is_4lvl(&ppgtt->vm) ? 3 : 2;

	/*
	 * From bdw, there is hw support for read-only pages in the PPGTT.
	 *
	 * Gen11 has HSDES#:1807136187 unresolved. Disable ro support
	 * for now.
	 *
	 * Gen12 has inherited the same read-only fault issue from gen11.
	 */
	ppgtt->vm.has_read_only = !IS_GEN_RANGE(gt->i915, 11, 12);

	/*
	 * There are only few exceptions for gen >=6. chv and bxt.
	 * And we are not sure about the latter so play safe for now.
	 */
	if (IS_CHERRYVIEW(gt->i915) || IS_BROXTON(gt->i915))
		ppgtt->vm.pt_kmap_wc = true;

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
	ppgtt->vm.allocate_va_range = gen8_ppgtt_alloc;
	ppgtt->vm.clear_range = gen8_ppgtt_clear;

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
