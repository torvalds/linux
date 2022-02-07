// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020 Intel Corporation
 */

#include "i915_drv.h"
#include "intel_context.h"
#include "intel_gpu_commands.h"
#include "intel_gt.h"
#include "intel_gtt.h"
#include "intel_migrate.h"
#include "intel_ring.h"

struct insert_pte_data {
	u64 offset;
};

#define CHUNK_SZ SZ_8M /* ~1ms at 8GiB/s preemption delay */

static bool engine_supports_migration(struct intel_engine_cs *engine)
{
	if (!engine)
		return false;

	/*
	 * We need the ability to prevent aribtration (MI_ARB_ON_OFF),
	 * the ability to write PTE using inline data (MI_STORE_DATA)
	 * and of course the ability to do the block transfer (blits).
	 */
	GEM_BUG_ON(engine->class != COPY_ENGINE_CLASS);

	return true;
}

static void insert_pte(struct i915_address_space *vm,
		       struct i915_page_table *pt,
		       void *data)
{
	struct insert_pte_data *d = data;

	vm->insert_page(vm, px_dma(pt), d->offset, I915_CACHE_NONE,
			i915_gem_object_is_lmem(pt->base) ? PTE_LM : 0);
	d->offset += PAGE_SIZE;
}

static struct i915_address_space *migrate_vm(struct intel_gt *gt)
{
	struct i915_vm_pt_stash stash = {};
	struct i915_ppgtt *vm;
	int err;
	int i;

	/*
	 * We construct a very special VM for use by all migration contexts,
	 * it is kept pinned so that it can be used at any time. As we need
	 * to pre-allocate the page directories for the migration VM, this
	 * limits us to only using a small number of prepared vma.
	 *
	 * To be able to pipeline and reschedule migration operations while
	 * avoiding unnecessary contention on the vm itself, the PTE updates
	 * are inline with the blits. All the blits use the same fixed
	 * addresses, with the backing store redirection being updated on the
	 * fly. Only 2 implicit vma are used for all migration operations.
	 *
	 * We lay the ppGTT out as:
	 *
	 *	[0, CHUNK_SZ) -> first object
	 *	[CHUNK_SZ, 2 * CHUNK_SZ) -> second object
	 *	[2 * CHUNK_SZ, 2 * CHUNK_SZ + 2 * CHUNK_SZ >> 9] -> PTE
	 *
	 * By exposing the dma addresses of the page directories themselves
	 * within the ppGTT, we are then able to rewrite the PTE prior to use.
	 * But the PTE update and subsequent migration operation must be atomic,
	 * i.e. within the same non-preemptible window so that we do not switch
	 * to another migration context that overwrites the PTE.
	 *
	 * TODO: Add support for huge LMEM PTEs
	 */

	vm = i915_ppgtt_create(gt, I915_BO_ALLOC_PM_EARLY);
	if (IS_ERR(vm))
		return ERR_CAST(vm);

	if (!vm->vm.allocate_va_range || !vm->vm.foreach) {
		err = -ENODEV;
		goto err_vm;
	}

	/*
	 * Each engine instance is assigned its own chunk in the VM, so
	 * that we can run multiple instances concurrently
	 */
	for (i = 0; i < ARRAY_SIZE(gt->engine_class[COPY_ENGINE_CLASS]); i++) {
		struct intel_engine_cs *engine;
		u64 base = (u64)i << 32;
		struct insert_pte_data d = {};
		struct i915_gem_ww_ctx ww;
		u64 sz;

		engine = gt->engine_class[COPY_ENGINE_CLASS][i];
		if (!engine_supports_migration(engine))
			continue;

		/*
		 * We copy in 8MiB chunks. Each PDE covers 2MiB, so we need
		 * 4x2 page directories for source/destination.
		 */
		sz = 2 * CHUNK_SZ;
		d.offset = base + sz;

		/*
		 * We need another page directory setup so that we can write
		 * the 8x512 PTE in each chunk.
		 */
		sz += (sz >> 12) * sizeof(u64);

		err = i915_vm_alloc_pt_stash(&vm->vm, &stash, sz);
		if (err)
			goto err_vm;

		for_i915_gem_ww(&ww, err, true) {
			err = i915_vm_lock_objects(&vm->vm, &ww);
			if (err)
				continue;
			err = i915_vm_map_pt_stash(&vm->vm, &stash);
			if (err)
				continue;

			vm->vm.allocate_va_range(&vm->vm, &stash, base, sz);
		}
		i915_vm_free_pt_stash(&vm->vm, &stash);
		if (err)
			goto err_vm;

		/* Now allow the GPU to rewrite the PTE via its own ppGTT */
		vm->vm.foreach(&vm->vm, base, d.offset - base, insert_pte, &d);
	}

	return &vm->vm;

err_vm:
	i915_vm_put(&vm->vm);
	return ERR_PTR(err);
}

static struct intel_engine_cs *first_copy_engine(struct intel_gt *gt)
{
	struct intel_engine_cs *engine;
	int i;

	for (i = 0; i < ARRAY_SIZE(gt->engine_class[COPY_ENGINE_CLASS]); i++) {
		engine = gt->engine_class[COPY_ENGINE_CLASS][i];
		if (engine_supports_migration(engine))
			return engine;
	}

	return NULL;
}

static struct intel_context *pinned_context(struct intel_gt *gt)
{
	static struct lock_class_key key;
	struct intel_engine_cs *engine;
	struct i915_address_space *vm;
	struct intel_context *ce;

	engine = first_copy_engine(gt);
	if (!engine)
		return ERR_PTR(-ENODEV);

	vm = migrate_vm(gt);
	if (IS_ERR(vm))
		return ERR_CAST(vm);

	ce = intel_engine_create_pinned_context(engine, vm, SZ_512K,
						I915_GEM_HWS_MIGRATE,
						&key, "migrate");
	i915_vm_put(vm);
	return ce;
}

int intel_migrate_init(struct intel_migrate *m, struct intel_gt *gt)
{
	struct intel_context *ce;

	memset(m, 0, sizeof(*m));

	ce = pinned_context(gt);
	if (IS_ERR(ce))
		return PTR_ERR(ce);

	m->context = ce;
	return 0;
}

static int random_index(unsigned int max)
{
	return upper_32_bits(mul_u32_u32(get_random_u32(), max));
}

static struct intel_context *__migrate_engines(struct intel_gt *gt)
{
	struct intel_engine_cs *engines[MAX_ENGINE_INSTANCE];
	struct intel_engine_cs *engine;
	unsigned int count, i;

	count = 0;
	for (i = 0; i < ARRAY_SIZE(gt->engine_class[COPY_ENGINE_CLASS]); i++) {
		engine = gt->engine_class[COPY_ENGINE_CLASS][i];
		if (engine_supports_migration(engine))
			engines[count++] = engine;
	}

	return intel_context_create(engines[random_index(count)]);
}

struct intel_context *intel_migrate_create_context(struct intel_migrate *m)
{
	struct intel_context *ce;

	/*
	 * We randomly distribute contexts across the engines upon constrction,
	 * as they all share the same pinned vm, and so in order to allow
	 * multiple blits to run in parallel, we must construct each blit
	 * to use a different range of the vm for its GTT. This has to be
	 * known at construction, so we can not use the late greedy load
	 * balancing of the virtual-engine.
	 */
	ce = __migrate_engines(m->context->engine->gt);
	if (IS_ERR(ce))
		return ce;

	ce->ring = NULL;
	ce->ring_size = SZ_256K;

	i915_vm_put(ce->vm);
	ce->vm = i915_vm_get(m->context->vm);

	return ce;
}

static inline struct sgt_dma sg_sgt(struct scatterlist *sg)
{
	dma_addr_t addr = sg_dma_address(sg);

	return (struct sgt_dma){ sg, addr, addr + sg_dma_len(sg) };
}

static int emit_no_arbitration(struct i915_request *rq)
{
	u32 *cs;

	cs = intel_ring_begin(rq, 2);
	if (IS_ERR(cs))
		return PTR_ERR(cs);

	/* Explicitly disable preemption for this request. */
	*cs++ = MI_ARB_ON_OFF;
	*cs++ = MI_NOOP;
	intel_ring_advance(rq, cs);

	return 0;
}

static int emit_pte(struct i915_request *rq,
		    struct sgt_dma *it,
		    enum i915_cache_level cache_level,
		    bool is_lmem,
		    u64 offset,
		    int length)
{
	const u64 encode = rq->context->vm->pte_encode(0, cache_level,
						       is_lmem ? PTE_LM : 0);
	struct intel_ring *ring = rq->ring;
	int total = 0;
	u32 *hdr, *cs;
	int pkt;

	GEM_BUG_ON(GRAPHICS_VER(rq->engine->i915) < 8);

	/* Compute the page directory offset for the target address range */
	offset >>= 12;
	offset *= sizeof(u64);
	offset += 2 * CHUNK_SZ;
	offset += (u64)rq->engine->instance << 32;

	cs = intel_ring_begin(rq, 6);
	if (IS_ERR(cs))
		return PTR_ERR(cs);

	/* Pack as many PTE updates as possible into a single MI command */
	pkt = min_t(int, 0x400, ring->space / sizeof(u32) + 5);
	pkt = min_t(int, pkt, (ring->size - ring->emit) / sizeof(u32) + 5);

	hdr = cs;
	*cs++ = MI_STORE_DATA_IMM | REG_BIT(21); /* as qword elements */
	*cs++ = lower_32_bits(offset);
	*cs++ = upper_32_bits(offset);

	do {
		if (cs - hdr >= pkt) {
			*hdr += cs - hdr - 2;
			*cs++ = MI_NOOP;

			ring->emit = (void *)cs - ring->vaddr;
			intel_ring_advance(rq, cs);
			intel_ring_update_space(ring);

			cs = intel_ring_begin(rq, 6);
			if (IS_ERR(cs))
				return PTR_ERR(cs);

			pkt = min_t(int, 0x400, ring->space / sizeof(u32) + 5);
			pkt = min_t(int, pkt, (ring->size - ring->emit) / sizeof(u32) + 5);

			hdr = cs;
			*cs++ = MI_STORE_DATA_IMM | REG_BIT(21);
			*cs++ = lower_32_bits(offset);
			*cs++ = upper_32_bits(offset);
		}

		*cs++ = lower_32_bits(encode | it->dma);
		*cs++ = upper_32_bits(encode | it->dma);

		offset += 8;
		total += I915_GTT_PAGE_SIZE;

		it->dma += I915_GTT_PAGE_SIZE;
		if (it->dma >= it->max) {
			it->sg = __sg_next(it->sg);
			if (!it->sg || sg_dma_len(it->sg) == 0)
				break;

			it->dma = sg_dma_address(it->sg);
			it->max = it->dma + sg_dma_len(it->sg);
		}
	} while (total < length);

	*hdr += cs - hdr - 2;
	*cs++ = MI_NOOP;

	ring->emit = (void *)cs - ring->vaddr;
	intel_ring_advance(rq, cs);
	intel_ring_update_space(ring);

	return total;
}

static bool wa_1209644611_applies(int ver, u32 size)
{
	u32 height = size >> PAGE_SHIFT;

	if (ver != 11)
		return false;

	return height % 4 == 3 && height <= 8;
}

static int emit_copy(struct i915_request *rq, int size)
{
	const int ver = GRAPHICS_VER(rq->engine->i915);
	u32 instance = rq->engine->instance;
	u32 *cs;

	cs = intel_ring_begin(rq, ver >= 8 ? 10 : 6);
	if (IS_ERR(cs))
		return PTR_ERR(cs);

	if (ver >= 9 && !wa_1209644611_applies(ver, size)) {
		*cs++ = GEN9_XY_FAST_COPY_BLT_CMD | (10 - 2);
		*cs++ = BLT_DEPTH_32 | PAGE_SIZE;
		*cs++ = 0;
		*cs++ = size >> PAGE_SHIFT << 16 | PAGE_SIZE / 4;
		*cs++ = CHUNK_SZ; /* dst offset */
		*cs++ = instance;
		*cs++ = 0;
		*cs++ = PAGE_SIZE;
		*cs++ = 0; /* src offset */
		*cs++ = instance;
	} else if (ver >= 8) {
		*cs++ = XY_SRC_COPY_BLT_CMD | BLT_WRITE_RGBA | (10 - 2);
		*cs++ = BLT_DEPTH_32 | BLT_ROP_SRC_COPY | PAGE_SIZE;
		*cs++ = 0;
		*cs++ = size >> PAGE_SHIFT << 16 | PAGE_SIZE / 4;
		*cs++ = CHUNK_SZ; /* dst offset */
		*cs++ = instance;
		*cs++ = 0;
		*cs++ = PAGE_SIZE;
		*cs++ = 0; /* src offset */
		*cs++ = instance;
	} else {
		GEM_BUG_ON(instance);
		*cs++ = SRC_COPY_BLT_CMD | BLT_WRITE_RGBA | (6 - 2);
		*cs++ = BLT_DEPTH_32 | BLT_ROP_SRC_COPY | PAGE_SIZE;
		*cs++ = size >> PAGE_SHIFT << 16 | PAGE_SIZE;
		*cs++ = CHUNK_SZ; /* dst offset */
		*cs++ = PAGE_SIZE;
		*cs++ = 0; /* src offset */
	}

	intel_ring_advance(rq, cs);
	return 0;
}

int
intel_context_migrate_copy(struct intel_context *ce,
			   const struct i915_deps *deps,
			   struct scatterlist *src,
			   enum i915_cache_level src_cache_level,
			   bool src_is_lmem,
			   struct scatterlist *dst,
			   enum i915_cache_level dst_cache_level,
			   bool dst_is_lmem,
			   struct i915_request **out)
{
	struct sgt_dma it_src = sg_sgt(src), it_dst = sg_sgt(dst);
	struct i915_request *rq;
	int err;

	GEM_BUG_ON(ce->vm != ce->engine->gt->migrate.context->vm);
	*out = NULL;

	GEM_BUG_ON(ce->ring->size < SZ_64K);

	do {
		int len;

		rq = i915_request_create(ce);
		if (IS_ERR(rq)) {
			err = PTR_ERR(rq);
			goto out_ce;
		}

		if (deps) {
			err = i915_request_await_deps(rq, deps);
			if (err)
				goto out_rq;

			if (rq->engine->emit_init_breadcrumb) {
				err = rq->engine->emit_init_breadcrumb(rq);
				if (err)
					goto out_rq;
			}

			deps = NULL;
		}

		/* The PTE updates + copy must not be interrupted. */
		err = emit_no_arbitration(rq);
		if (err)
			goto out_rq;

		len = emit_pte(rq, &it_src, src_cache_level, src_is_lmem, 0,
			       CHUNK_SZ);
		if (len <= 0) {
			err = len;
			goto out_rq;
		}

		err = emit_pte(rq, &it_dst, dst_cache_level, dst_is_lmem,
			       CHUNK_SZ, len);
		if (err < 0)
			goto out_rq;
		if (err < len) {
			err = -EINVAL;
			goto out_rq;
		}

		err = rq->engine->emit_flush(rq, EMIT_INVALIDATE);
		if (err)
			goto out_rq;

		err = emit_copy(rq, len);

		/* Arbitration is re-enabled between requests. */
out_rq:
		if (*out)
			i915_request_put(*out);
		*out = i915_request_get(rq);
		i915_request_add(rq);
		if (err || !it_src.sg || !sg_dma_len(it_src.sg))
			break;

		cond_resched();
	} while (1);

out_ce:
	return err;
}

static int emit_clear(struct i915_request *rq, int size, u32 value)
{
	const int ver = GRAPHICS_VER(rq->engine->i915);
	u32 instance = rq->engine->instance;
	u32 *cs;

	GEM_BUG_ON(size >> PAGE_SHIFT > S16_MAX);

	cs = intel_ring_begin(rq, ver >= 8 ? 8 : 6);
	if (IS_ERR(cs))
		return PTR_ERR(cs);

	if (ver >= 8) {
		*cs++ = XY_COLOR_BLT_CMD | BLT_WRITE_RGBA | (7 - 2);
		*cs++ = BLT_DEPTH_32 | BLT_ROP_COLOR_COPY | PAGE_SIZE;
		*cs++ = 0;
		*cs++ = size >> PAGE_SHIFT << 16 | PAGE_SIZE / 4;
		*cs++ = 0; /* offset */
		*cs++ = instance;
		*cs++ = value;
		*cs++ = MI_NOOP;
	} else {
		GEM_BUG_ON(instance);
		*cs++ = XY_COLOR_BLT_CMD | BLT_WRITE_RGBA | (6 - 2);
		*cs++ = BLT_DEPTH_32 | BLT_ROP_COLOR_COPY | PAGE_SIZE;
		*cs++ = 0;
		*cs++ = size >> PAGE_SHIFT << 16 | PAGE_SIZE / 4;
		*cs++ = 0;
		*cs++ = value;
	}

	intel_ring_advance(rq, cs);
	return 0;
}

int
intel_context_migrate_clear(struct intel_context *ce,
			    const struct i915_deps *deps,
			    struct scatterlist *sg,
			    enum i915_cache_level cache_level,
			    bool is_lmem,
			    u32 value,
			    struct i915_request **out)
{
	struct sgt_dma it = sg_sgt(sg);
	struct i915_request *rq;
	int err;

	GEM_BUG_ON(ce->vm != ce->engine->gt->migrate.context->vm);
	*out = NULL;

	GEM_BUG_ON(ce->ring->size < SZ_64K);

	do {
		int len;

		rq = i915_request_create(ce);
		if (IS_ERR(rq)) {
			err = PTR_ERR(rq);
			goto out_ce;
		}

		if (deps) {
			err = i915_request_await_deps(rq, deps);
			if (err)
				goto out_rq;

			if (rq->engine->emit_init_breadcrumb) {
				err = rq->engine->emit_init_breadcrumb(rq);
				if (err)
					goto out_rq;
			}

			deps = NULL;
		}

		/* The PTE updates + clear must not be interrupted. */
		err = emit_no_arbitration(rq);
		if (err)
			goto out_rq;

		len = emit_pte(rq, &it, cache_level, is_lmem, 0, CHUNK_SZ);
		if (len <= 0) {
			err = len;
			goto out_rq;
		}

		err = rq->engine->emit_flush(rq, EMIT_INVALIDATE);
		if (err)
			goto out_rq;

		err = emit_clear(rq, len, value);

		/* Arbitration is re-enabled between requests. */
out_rq:
		if (*out)
			i915_request_put(*out);
		*out = i915_request_get(rq);
		i915_request_add(rq);
		if (err || !it.sg || !sg_dma_len(it.sg))
			break;

		cond_resched();
	} while (1);

out_ce:
	return err;
}

int intel_migrate_copy(struct intel_migrate *m,
		       struct i915_gem_ww_ctx *ww,
		       const struct i915_deps *deps,
		       struct scatterlist *src,
		       enum i915_cache_level src_cache_level,
		       bool src_is_lmem,
		       struct scatterlist *dst,
		       enum i915_cache_level dst_cache_level,
		       bool dst_is_lmem,
		       struct i915_request **out)
{
	struct intel_context *ce;
	int err;

	*out = NULL;
	if (!m->context)
		return -ENODEV;

	ce = intel_migrate_create_context(m);
	if (IS_ERR(ce))
		ce = intel_context_get(m->context);
	GEM_BUG_ON(IS_ERR(ce));

	err = intel_context_pin_ww(ce, ww);
	if (err)
		goto out;

	err = intel_context_migrate_copy(ce, deps,
					 src, src_cache_level, src_is_lmem,
					 dst, dst_cache_level, dst_is_lmem,
					 out);

	intel_context_unpin(ce);
out:
	intel_context_put(ce);
	return err;
}

int
intel_migrate_clear(struct intel_migrate *m,
		    struct i915_gem_ww_ctx *ww,
		    const struct i915_deps *deps,
		    struct scatterlist *sg,
		    enum i915_cache_level cache_level,
		    bool is_lmem,
		    u32 value,
		    struct i915_request **out)
{
	struct intel_context *ce;
	int err;

	*out = NULL;
	if (!m->context)
		return -ENODEV;

	ce = intel_migrate_create_context(m);
	if (IS_ERR(ce))
		ce = intel_context_get(m->context);
	GEM_BUG_ON(IS_ERR(ce));

	err = intel_context_pin_ww(ce, ww);
	if (err)
		goto out;

	err = intel_context_migrate_clear(ce, deps, sg, cache_level,
					  is_lmem, value, out);

	intel_context_unpin(ce);
out:
	intel_context_put(ce);
	return err;
}

void intel_migrate_fini(struct intel_migrate *m)
{
	struct intel_context *ce;

	ce = fetch_and_zero(&m->context);
	if (!ce)
		return;

	intel_engine_destroy_pinned_context(ce);
}

#if IS_ENABLED(CONFIG_DRM_I915_SELFTEST)
#include "selftest_migrate.c"
#endif
