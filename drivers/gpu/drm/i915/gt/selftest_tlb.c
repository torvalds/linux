// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include "i915_selftest.h"

#include "gem/i915_gem_internal.h"
#include "gem/i915_gem_region.h"

#include "gen8_engine_cs.h"
#include "i915_gem_ww.h"
#include "intel_engine_regs.h"
#include "intel_gpu_commands.h"
#include "intel_context.h"
#include "intel_gt.h"
#include "intel_ring.h"

#include "selftests/igt_flush_test.h"
#include "selftests/i915_random.h"

static void vma_set_qw(struct i915_vma *vma, u64 addr, u64 val)
{
	GEM_BUG_ON(addr < i915_vma_offset(vma));
	GEM_BUG_ON(addr >= i915_vma_offset(vma) + i915_vma_size(vma) + sizeof(val));
	memset64(page_mask_bits(vma->obj->mm.mapping) +
		 (addr - i915_vma_offset(vma)), val, 1);
}

static int
pte_tlbinv(struct intel_context *ce,
	   struct i915_vma *va,
	   struct i915_vma *vb,
	   u64 align,
	   void (*tlbinv)(struct i915_address_space *vm, u64 addr, u64 length),
	   u64 length,
	   struct rnd_state *prng)
{
	struct drm_i915_gem_object *batch;
	struct drm_mm_node vb_node;
	struct i915_request *rq;
	struct i915_vma *vma;
	u64 addr;
	int err;
	u32 *cs;

	batch = i915_gem_object_create_internal(ce->vm->i915, 4096);
	if (IS_ERR(batch))
		return PTR_ERR(batch);

	vma = i915_vma_instance(batch, ce->vm, NULL);
	if (IS_ERR(vma)) {
		err = PTR_ERR(vma);
		goto out;
	}

	err = i915_vma_pin(vma, 0, 0, PIN_USER);
	if (err)
		goto out;

	/* Pin va at random but aligned offset after vma */
	addr = round_up(vma->node.start + vma->node.size, align);
	/* MI_CONDITIONAL_BATCH_BUFFER_END limits address to 48b */
	addr = igt_random_offset(prng, addr, min(ce->vm->total, BIT_ULL(48)),
				 va->size, align);
	err = i915_vma_pin(va,  0, 0, addr | PIN_OFFSET_FIXED | PIN_USER);
	if (err) {
		pr_err("Cannot pin at %llx+%llx\n", addr, va->size);
		goto out;
	}
	GEM_BUG_ON(i915_vma_offset(va) != addr);
	if (vb != va) {
		vb_node = vb->node;
		vb->node = va->node; /* overwrites the _same_ PTE  */
	}

	/*
	 * Now choose random dword at the 1st pinned page.
	 *
	 * SZ_64K pages on dg1 require that the whole PT be marked
	 * containing 64KiB entries. So we make sure that vma
	 * covers the whole PT, despite being randomly aligned to 64KiB
	 * and restrict our sampling to the 2MiB PT within where
	 * we know that we will be using 64KiB pages.
	 */
	if (align == SZ_64K)
		addr = round_up(addr, SZ_2M);
	addr = igt_random_offset(prng, addr, addr + align, 8, 8);

	if (va != vb)
		pr_info("%s(%s): Sampling %llx, with alignment %llx, using PTE size %x (phys %x, sg %x), invalidate:%llx+%llx\n",
			ce->engine->name, va->obj->mm.region->name ?: "smem",
			addr, align, va->resource->page_sizes_gtt,
			va->page_sizes.phys, va->page_sizes.sg,
			addr & -length, length);

	cs = i915_gem_object_pin_map_unlocked(batch, I915_MAP_WC);
	*cs++ = MI_NOOP; /* for later termination */
	/*
	 * Sample the target to see if we spot the updated backing store.
	 * Gen8 VCS compares immediate value with bitwise-and of two
	 * consecutive DWORDS pointed by addr, other gen/engines compare value
	 * with DWORD pointed by addr. Moreover we want to exercise DWORD size
	 * invalidations. To fulfill all these requirements below values
	 * have been chosen.
	 */
	*cs++ = MI_CONDITIONAL_BATCH_BUFFER_END | MI_DO_COMPARE | 2;
	*cs++ = 0; /* break if *addr == 0 */
	*cs++ = lower_32_bits(addr);
	*cs++ = upper_32_bits(addr);
	vma_set_qw(va, addr, -1);
	vma_set_qw(vb, addr, 0);

	/* Keep sampling until we get bored */
	*cs++ = MI_BATCH_BUFFER_START | BIT(8) | 1;
	*cs++ = lower_32_bits(i915_vma_offset(vma));
	*cs++ = upper_32_bits(i915_vma_offset(vma));

	i915_gem_object_flush_map(batch);

	rq = i915_request_create(ce);
	if (IS_ERR(rq)) {
		err = PTR_ERR(rq);
		goto out_va;
	}

	err = rq->engine->emit_bb_start(rq, i915_vma_offset(vma), 0, 0);
	if (err) {
		i915_request_add(rq);
		goto out_va;
	}

	i915_request_get(rq);
	i915_request_add(rq);

	/* Short sleep to sanitycheck the batch is spinning before we begin */
	msleep(10);
	if (va == vb) {
		if (!i915_request_completed(rq)) {
			pr_err("%s(%s): Semaphore sanitycheck failed %llx, with alignment %llx, using PTE size %x (phys %x, sg %x)\n",
			       ce->engine->name, va->obj->mm.region->name ?: "smem",
			       addr, align, va->resource->page_sizes_gtt,
			       va->page_sizes.phys, va->page_sizes.sg);
			err = -EIO;
		}
	} else if (!i915_request_completed(rq)) {
		struct i915_vma_resource vb_res = {
			.bi.pages = vb->obj->mm.pages,
			.bi.page_sizes = vb->obj->mm.page_sizes,
			.start = i915_vma_offset(vb),
			.vma_size = i915_vma_size(vb)
		};
		unsigned int pte_flags = 0;

		/* Flip the PTE between A and B */
		if (i915_gem_object_is_lmem(vb->obj))
			pte_flags |= PTE_LM;
		ce->vm->insert_entries(ce->vm, &vb_res, 0, pte_flags);

		/* Flush the PTE update to concurrent HW */
		tlbinv(ce->vm, addr & -length, length);

		if (wait_for(i915_request_completed(rq), HZ / 2)) {
			pr_err("%s: Request did not complete; the COND_BBE did not read the updated PTE\n",
			       ce->engine->name);
			err = -EINVAL;
		}
	} else {
		pr_err("Spinner ended unexpectedly\n");
		err = -EIO;
	}
	i915_request_put(rq);

	cs = page_mask_bits(batch->mm.mapping);
	*cs = MI_BATCH_BUFFER_END;
	wmb();

out_va:
	if (vb != va)
		vb->node = vb_node;
	i915_vma_unpin(va);
	if (i915_vma_unbind_unlocked(va))
		err = -EIO;
out:
	i915_gem_object_put(batch);
	return err;
}

static struct drm_i915_gem_object *create_lmem(struct intel_gt *gt)
{
	/*
	 * Allocation of largest possible page size allows to test all types
	 * of pages.
	 */
	return i915_gem_object_create_lmem(gt->i915, SZ_1G, I915_BO_ALLOC_CONTIGUOUS);
}

static struct drm_i915_gem_object *create_smem(struct intel_gt *gt)
{
	/*
	 * SZ_64K pages require covering the whole 2M PT (gen8 to tgl/dg1).
	 * While that does not require the whole 2M block to be contiguous
	 * it is easier to make it so, since we need that for SZ_2M pagees.
	 * Since we randomly offset the start of the vma, we need a 4M object
	 * so that there is a 2M range within it is suitable for SZ_64K PTE.
	 */
	return i915_gem_object_create_internal(gt->i915, SZ_4M);
}

static int
mem_tlbinv(struct intel_gt *gt,
	   struct drm_i915_gem_object *(*create_fn)(struct intel_gt *),
	   void (*tlbinv)(struct i915_address_space *vm, u64 addr, u64 length))
{
	unsigned int ppgtt_size = RUNTIME_INFO(gt->i915)->ppgtt_size;
	struct intel_engine_cs *engine;
	struct drm_i915_gem_object *A, *B;
	struct i915_ppgtt *ppgtt;
	struct i915_vma *va, *vb;
	enum intel_engine_id id;
	I915_RND_STATE(prng);
	void *vaddr;
	int err;

	/*
	 * Check that the TLB invalidate is able to revoke an active
	 * page. We load a page into a spinning COND_BBE loop and then
	 * remap that page to a new physical address. The old address, and
	 * so the loop keeps spinning, is retained in the TLB cache until
	 * we issue an invalidate.
	 */

	A = create_fn(gt);
	if (IS_ERR(A))
		return PTR_ERR(A);

	vaddr = i915_gem_object_pin_map_unlocked(A, I915_MAP_WC);
	if (IS_ERR(vaddr)) {
		err = PTR_ERR(vaddr);
		goto out_a;
	}

	B = create_fn(gt);
	if (IS_ERR(B)) {
		err = PTR_ERR(B);
		goto out_a;
	}

	vaddr = i915_gem_object_pin_map_unlocked(B, I915_MAP_WC);
	if (IS_ERR(vaddr)) {
		err = PTR_ERR(vaddr);
		goto out_b;
	}

	GEM_BUG_ON(A->base.size != B->base.size);
	if ((A->mm.page_sizes.phys | B->mm.page_sizes.phys) & (A->base.size - 1))
		pr_warn("Failed to allocate contiguous pages for size %zx\n",
			A->base.size);

	ppgtt = i915_ppgtt_create(gt, 0);
	if (IS_ERR(ppgtt)) {
		err = PTR_ERR(ppgtt);
		goto out_b;
	}

	va = i915_vma_instance(A, &ppgtt->vm, NULL);
	if (IS_ERR(va)) {
		err = PTR_ERR(va);
		goto out_vm;
	}

	vb = i915_vma_instance(B, &ppgtt->vm, NULL);
	if (IS_ERR(vb)) {
		err = PTR_ERR(vb);
		goto out_vm;
	}

	err = 0;
	for_each_engine(engine, gt, id) {
		struct i915_gem_ww_ctx ww;
		struct intel_context *ce;
		int bit;

		ce = intel_context_create(engine);
		if (IS_ERR(ce)) {
			err = PTR_ERR(ce);
			break;
		}

		i915_vm_put(ce->vm);
		ce->vm = i915_vm_get(&ppgtt->vm);

		for_i915_gem_ww(&ww, err, true)
			err = intel_context_pin_ww(ce, &ww);
		if (err)
			goto err_put;

		for_each_set_bit(bit,
				 (unsigned long *)&RUNTIME_INFO(gt->i915)->page_sizes,
				 BITS_PER_TYPE(RUNTIME_INFO(gt->i915)->page_sizes)) {
			unsigned int len;

			if (BIT_ULL(bit) < i915_vm_obj_min_alignment(va->vm, va->obj))
				continue;

			/* sanitycheck the semaphore wake up */
			err = pte_tlbinv(ce, va, va,
					 BIT_ULL(bit),
					 NULL, SZ_4K,
					 &prng);
			if (err)
				goto err_unpin;

			for (len = 2; len <= ppgtt_size; len = min(2 * len, ppgtt_size)) {
				err = pte_tlbinv(ce, va, vb,
						 BIT_ULL(bit),
						 tlbinv,
						 BIT_ULL(len),
						 &prng);
				if (err)
					goto err_unpin;
				if (len == ppgtt_size)
					break;
			}
		}
err_unpin:
		intel_context_unpin(ce);
err_put:
		intel_context_put(ce);
		if (err)
			break;
	}

	if (igt_flush_test(gt->i915))
		err = -EIO;

out_vm:
	i915_vm_put(&ppgtt->vm);
out_b:
	i915_gem_object_put(B);
out_a:
	i915_gem_object_put(A);
	return err;
}

static void tlbinv_full(struct i915_address_space *vm, u64 addr, u64 length)
{
	intel_gt_invalidate_tlb(vm->gt, intel_gt_tlb_seqno(vm->gt) | 1);
}

static int invalidate_full(void *arg)
{
	struct intel_gt *gt = arg;
	int err;

	if (GRAPHICS_VER(gt->i915) < 8)
		return 0; /* TLB invalidate not implemented */

	err = mem_tlbinv(gt, create_smem, tlbinv_full);
	if (err == 0)
		err = mem_tlbinv(gt, create_lmem, tlbinv_full);
	if (err == -ENODEV || err == -ENXIO)
		err = 0;

	return err;
}

int intel_tlb_live_selftests(struct drm_i915_private *i915)
{
	static const struct i915_subtest tests[] = {
		SUBTEST(invalidate_full),
	};
	struct intel_gt *gt;
	unsigned int i;

	for_each_gt(gt, i915, i) {
		int err;

		if (intel_gt_is_wedged(gt))
			continue;

		err = intel_gt_live_subtests(tests, gt);
		if (err)
			return err;
	}

	return 0;
}
