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

#define GET_CCS_BYTES(i915, size)	(HAS_FLAT_CCS(i915) ? \
					 DIV_ROUND_UP(size, NUM_BYTES_PER_CCS_BYTE) : 0)
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

static void xehpsdv_toggle_pdes(struct i915_address_space *vm,
				struct i915_page_table *pt,
				void *data)
{
	struct insert_pte_data *d = data;

	/*
	 * Insert a dummy PTE into every PT that will map to LMEM to ensure
	 * we have a correctly setup PDE structure for later use.
	 */
	vm->insert_page(vm, 0, d->offset, I915_CACHE_NONE, PTE_LM);
	GEM_BUG_ON(!pt->is_compact);
	d->offset += SZ_2M;
}

static void xehpsdv_insert_pte(struct i915_address_space *vm,
			       struct i915_page_table *pt,
			       void *data)
{
	struct insert_pte_data *d = data;

	/*
	 * We are playing tricks here, since the actual pt, from the hw
	 * pov, is only 256bytes with 32 entries, or 4096bytes with 512
	 * entries, but we are still guaranteed that the physical
	 * alignment is 64K underneath for the pt, and we are careful
	 * not to access the space in the void.
	 */
	vm->insert_page(vm, px_dma(pt), d->offset, I915_CACHE_NONE, PTE_LM);
	d->offset += SZ_64K;
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
	 * This changes quite a bit on platforms with HAS_64K_PAGES support,
	 * where we instead have three windows, each CHUNK_SIZE in size. The
	 * first is reserved for mapping system-memory, and that just uses the
	 * 512 entry layout using 4K GTT pages. The other two windows just map
	 * lmem pages and must use the new compact 32 entry layout using 64K GTT
	 * pages, which ensures we can address any lmem object that the user
	 * throws at us. We then also use the xehpsdv_toggle_pdes as a way of
	 * just toggling the PDE bit(GEN12_PDE_64K) for us, to enable the
	 * compact layout for each of these page-tables, that fall within the
	 * [CHUNK_SIZE, 3 * CHUNK_SIZE) range.
	 *
	 * We lay the ppGTT out as:
	 *
	 * [0, CHUNK_SZ) -> first window/object, maps smem
	 * [CHUNK_SZ, 2 * CHUNK_SZ) -> second window/object, maps lmem src
	 * [2 * CHUNK_SZ, 3 * CHUNK_SZ) -> third window/object, maps lmem dst
	 *
	 * For the PTE window it's also quite different, since each PTE must
	 * point to some 64K page, one for each PT(since it's in lmem), and yet
	 * each is only <= 4096bytes, but since the unused space within that PTE
	 * range is never touched, this should be fine.
	 *
	 * So basically each PT now needs 64K of virtual memory, instead of 4K,
	 * which looks like:
	 *
	 * [3 * CHUNK_SZ, 3 * CHUNK_SZ + ((3 * CHUNK_SZ / SZ_2M) * SZ_64K)] -> PTE
	 */

	vm = i915_ppgtt_create(gt, I915_BO_ALLOC_PM_EARLY);
	if (IS_ERR(vm))
		return ERR_CAST(vm);

	if (!vm->vm.allocate_va_range || !vm->vm.foreach) {
		err = -ENODEV;
		goto err_vm;
	}

	if (HAS_64K_PAGES(gt->i915))
		stash.pt_sz = I915_GTT_PAGE_SIZE_64K;

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
		if (HAS_64K_PAGES(gt->i915))
			sz = 3 * CHUNK_SZ;
		else
			sz = 2 * CHUNK_SZ;
		d.offset = base + sz;

		/*
		 * We need another page directory setup so that we can write
		 * the 8x512 PTE in each chunk.
		 */
		if (HAS_64K_PAGES(gt->i915))
			sz += (sz / SZ_2M) * SZ_64K;
		else
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
		if (HAS_64K_PAGES(gt->i915)) {
			vm->vm.foreach(&vm->vm, base, d.offset - base,
				       xehpsdv_insert_pte, &d);
			d.offset = base + CHUNK_SZ;
			vm->vm.foreach(&vm->vm,
				       d.offset,
				       2 * CHUNK_SZ,
				       xehpsdv_toggle_pdes, &d);
		} else {
			vm->vm.foreach(&vm->vm, base, d.offset - base,
				       insert_pte, &d);
		}
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
	bool has_64K_pages = HAS_64K_PAGES(rq->engine->i915);
	const u64 encode = rq->context->vm->pte_encode(0, cache_level,
						       is_lmem ? PTE_LM : 0);
	struct intel_ring *ring = rq->ring;
	int pkt, dword_length;
	u32 total = 0;
	u32 page_size;
	u32 *hdr, *cs;

	GEM_BUG_ON(GRAPHICS_VER(rq->engine->i915) < 8);

	page_size = I915_GTT_PAGE_SIZE;
	dword_length = 0x400;

	/* Compute the page directory offset for the target address range */
	if (has_64K_pages) {
		GEM_BUG_ON(!IS_ALIGNED(offset, SZ_2M));

		offset /= SZ_2M;
		offset *= SZ_64K;
		offset += 3 * CHUNK_SZ;

		if (is_lmem) {
			page_size = I915_GTT_PAGE_SIZE_64K;
			dword_length = 0x40;
		}
	} else {
		offset >>= 12;
		offset *= sizeof(u64);
		offset += 2 * CHUNK_SZ;
	}

	offset += (u64)rq->engine->instance << 32;

	cs = intel_ring_begin(rq, 6);
	if (IS_ERR(cs))
		return PTR_ERR(cs);

	/* Pack as many PTE updates as possible into a single MI command */
	pkt = min_t(int, dword_length, ring->space / sizeof(u32) + 5);
	pkt = min_t(int, pkt, (ring->size - ring->emit) / sizeof(u32) + 5);

	hdr = cs;
	*cs++ = MI_STORE_DATA_IMM | REG_BIT(21); /* as qword elements */
	*cs++ = lower_32_bits(offset);
	*cs++ = upper_32_bits(offset);

	do {
		if (cs - hdr >= pkt) {
			int dword_rem;

			*hdr += cs - hdr - 2;
			*cs++ = MI_NOOP;

			ring->emit = (void *)cs - ring->vaddr;
			intel_ring_advance(rq, cs);
			intel_ring_update_space(ring);

			cs = intel_ring_begin(rq, 6);
			if (IS_ERR(cs))
				return PTR_ERR(cs);

			dword_rem = dword_length;
			if (has_64K_pages) {
				if (IS_ALIGNED(total, SZ_2M)) {
					offset = round_up(offset, SZ_64K);
				} else {
					dword_rem = SZ_2M - (total & (SZ_2M - 1));
					dword_rem /= page_size;
					dword_rem *= 2;
				}
			}

			pkt = min_t(int, dword_rem, ring->space / sizeof(u32) + 5);
			pkt = min_t(int, pkt, (ring->size - ring->emit) / sizeof(u32) + 5);

			hdr = cs;
			*cs++ = MI_STORE_DATA_IMM | REG_BIT(21);
			*cs++ = lower_32_bits(offset);
			*cs++ = upper_32_bits(offset);
		}

		GEM_BUG_ON(!IS_ALIGNED(it->dma, page_size));

		*cs++ = lower_32_bits(encode | it->dma);
		*cs++ = upper_32_bits(encode | it->dma);

		offset += 8;
		total += page_size;

		it->dma += page_size;
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

/**
 * DOC: Flat-CCS - Memory compression for Local memory
 *
 * On Xe-HP and later devices, we use dedicated compression control state (CCS)
 * stored in local memory for each surface, to support the 3D and media
 * compression formats.
 *
 * The memory required for the CCS of the entire local memory is 1/256 of the
 * local memory size. So before the kernel boot, the required memory is reserved
 * for the CCS data and a secure register will be programmed with the CCS base
 * address.
 *
 * Flat CCS data needs to be cleared when a lmem object is allocated.
 * And CCS data can be copied in and out of CCS region through
 * XY_CTRL_SURF_COPY_BLT. CPU can't access the CCS data directly.
 *
 * I915 supports Flat-CCS on lmem only objects. When an objects has smem in
 * its preference list, on memory pressure, i915 needs to migrate the lmem
 * content into smem. If the lmem object is Flat-CCS compressed by userspace,
 * then i915 needs to decompress it. But I915 lack the required information
 * for such decompression. Hence I915 supports Flat-CCS only on lmem only objects.
 *
 * When we exhaust the lmem, Flat-CCS capable objects' lmem backing memory can
 * be temporarily evicted to smem, along with the auxiliary CCS state, where
 * it can be potentially swapped-out at a later point, if required.
 * If userspace later touches the evicted pages, then we always move
 * the backing memory back to lmem, which includes restoring the saved CCS state,
 * and potentially performing any required swap-in.
 *
 * For the migration of the lmem objects with smem in placement list, such as
 * {lmem, smem}, objects are treated as non Flat-CCS capable objects.
 */

static inline u32 *i915_flush_dw(u32 *cmd, u32 flags)
{
	*cmd++ = MI_FLUSH_DW | flags;
	*cmd++ = 0;
	*cmd++ = 0;

	return cmd;
}

static u32 calc_ctrl_surf_instr_size(struct drm_i915_private *i915, int size)
{
	u32 num_cmds, num_blks, total_size;

	if (!GET_CCS_BYTES(i915, size))
		return 0;

	/*
	 * XY_CTRL_SURF_COPY_BLT transfers CCS in 256 byte
	 * blocks. one XY_CTRL_SURF_COPY_BLT command can
	 * transfer upto 1024 blocks.
	 */
	num_blks = DIV_ROUND_UP(GET_CCS_BYTES(i915, size),
				NUM_CCS_BYTES_PER_BLOCK);
	num_cmds = DIV_ROUND_UP(num_blks, NUM_CCS_BLKS_PER_XFER);
	total_size = XY_CTRL_SURF_INSTR_SIZE * num_cmds;

	/*
	 * Adding a flush before and after XY_CTRL_SURF_COPY_BLT
	 */
	total_size += 2 * MI_FLUSH_DW_SIZE;

	return total_size;
}

static int emit_copy_ccs(struct i915_request *rq,
			 u32 dst_offset, u8 dst_access,
			 u32 src_offset, u8 src_access, int size)
{
	struct drm_i915_private *i915 = rq->engine->i915;
	int mocs = rq->engine->gt->mocs.uc_index << 1;
	u32 num_ccs_blks, ccs_ring_size;
	u32 *cs;

	ccs_ring_size = calc_ctrl_surf_instr_size(i915, size);
	WARN_ON(!ccs_ring_size);

	cs = intel_ring_begin(rq, round_up(ccs_ring_size, 2));
	if (IS_ERR(cs))
		return PTR_ERR(cs);

	num_ccs_blks = DIV_ROUND_UP(GET_CCS_BYTES(i915, size),
				    NUM_CCS_BYTES_PER_BLOCK);
	GEM_BUG_ON(num_ccs_blks > NUM_CCS_BLKS_PER_XFER);
	cs = i915_flush_dw(cs, MI_FLUSH_DW_LLC | MI_FLUSH_DW_CCS);

	/*
	 * The XY_CTRL_SURF_COPY_BLT instruction is used to copy the CCS
	 * data in and out of the CCS region.
	 *
	 * We can copy at most 1024 blocks of 256 bytes using one
	 * XY_CTRL_SURF_COPY_BLT instruction.
	 *
	 * In case we need to copy more than 1024 blocks, we need to add
	 * another instruction to the same batch buffer.
	 *
	 * 1024 blocks of 256 bytes of CCS represent a total 256KB of CCS.
	 *
	 * 256 KB of CCS represents 256 * 256 KB = 64 MB of LMEM.
	 */
	*cs++ = XY_CTRL_SURF_COPY_BLT |
		src_access << SRC_ACCESS_TYPE_SHIFT |
		dst_access << DST_ACCESS_TYPE_SHIFT |
		((num_ccs_blks - 1) & CCS_SIZE_MASK) << CCS_SIZE_SHIFT;
	*cs++ = src_offset;
	*cs++ = rq->engine->instance |
		FIELD_PREP(XY_CTRL_SURF_MOCS_MASK, mocs);
	*cs++ = dst_offset;
	*cs++ = rq->engine->instance |
		FIELD_PREP(XY_CTRL_SURF_MOCS_MASK, mocs);

	cs = i915_flush_dw(cs, MI_FLUSH_DW_LLC | MI_FLUSH_DW_CCS);
	if (ccs_ring_size & 1)
		*cs++ = MI_NOOP;

	intel_ring_advance(rq, cs);

	return 0;
}

static int emit_copy(struct i915_request *rq,
		     u32 dst_offset, u32 src_offset, int size)
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
		*cs++ = dst_offset;
		*cs++ = instance;
		*cs++ = 0;
		*cs++ = PAGE_SIZE;
		*cs++ = src_offset;
		*cs++ = instance;
	} else if (ver >= 8) {
		*cs++ = XY_SRC_COPY_BLT_CMD | BLT_WRITE_RGBA | (10 - 2);
		*cs++ = BLT_DEPTH_32 | BLT_ROP_SRC_COPY | PAGE_SIZE;
		*cs++ = 0;
		*cs++ = size >> PAGE_SHIFT << 16 | PAGE_SIZE / 4;
		*cs++ = dst_offset;
		*cs++ = instance;
		*cs++ = 0;
		*cs++ = PAGE_SIZE;
		*cs++ = src_offset;
		*cs++ = instance;
	} else {
		GEM_BUG_ON(instance);
		*cs++ = SRC_COPY_BLT_CMD | BLT_WRITE_RGBA | (6 - 2);
		*cs++ = BLT_DEPTH_32 | BLT_ROP_SRC_COPY | PAGE_SIZE;
		*cs++ = size >> PAGE_SHIFT << 16 | PAGE_SIZE;
		*cs++ = dst_offset;
		*cs++ = PAGE_SIZE;
		*cs++ = src_offset;
	}

	intel_ring_advance(rq, cs);
	return 0;
}

static int scatter_list_length(struct scatterlist *sg)
{
	int len = 0;

	while (sg && sg_dma_len(sg)) {
		len += sg_dma_len(sg);
		sg = sg_next(sg);
	};

	return len;
}

static void
calculate_chunk_sz(struct drm_i915_private *i915, bool src_is_lmem,
		   int *src_sz, u32 bytes_to_cpy, u32 ccs_bytes_to_cpy)
{
	if (ccs_bytes_to_cpy) {
		if (!src_is_lmem)
			/*
			 * When CHUNK_SZ is passed all the pages upto CHUNK_SZ
			 * will be taken for the blt. in Flat-ccs supported
			 * platform Smem obj will have more pages than required
			 * for main meory hence limit it to the required size
			 * for main memory
			 */
			*src_sz = min_t(int, bytes_to_cpy, CHUNK_SZ);
	} else { /* ccs handling is not required */
		*src_sz = CHUNK_SZ;
	}
}

static void get_ccs_sg_sgt(struct sgt_dma *it, u32 bytes_to_cpy)
{
	u32 len;

	do {
		GEM_BUG_ON(!it->sg || !sg_dma_len(it->sg));
		len = it->max - it->dma;
		if (len > bytes_to_cpy) {
			it->dma += bytes_to_cpy;
			break;
		}

		bytes_to_cpy -= len;

		it->sg = __sg_next(it->sg);
		it->dma = sg_dma_address(it->sg);
		it->max = it->dma + sg_dma_len(it->sg);
	} while (bytes_to_cpy);
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
	struct sgt_dma it_src = sg_sgt(src), it_dst = sg_sgt(dst), it_ccs;
	struct drm_i915_private *i915 = ce->engine->i915;
	u32 ccs_bytes_to_cpy = 0, bytes_to_cpy;
	enum i915_cache_level ccs_cache_level;
	u32 src_offset, dst_offset;
	u8 src_access, dst_access;
	struct i915_request *rq;
	int src_sz, dst_sz;
	bool ccs_is_src;
	int err;

	GEM_BUG_ON(ce->vm != ce->engine->gt->migrate.context->vm);
	GEM_BUG_ON(IS_DGFX(ce->engine->i915) && (!src_is_lmem && !dst_is_lmem));
	*out = NULL;

	GEM_BUG_ON(ce->ring->size < SZ_64K);

	src_sz = scatter_list_length(src);
	bytes_to_cpy = src_sz;

	if (HAS_FLAT_CCS(i915) && src_is_lmem ^ dst_is_lmem) {
		src_access = !src_is_lmem && dst_is_lmem;
		dst_access = !src_access;

		dst_sz = scatter_list_length(dst);
		if (src_is_lmem) {
			it_ccs = it_dst;
			ccs_cache_level = dst_cache_level;
			ccs_is_src = false;
		} else if (dst_is_lmem) {
			bytes_to_cpy = dst_sz;
			it_ccs = it_src;
			ccs_cache_level = src_cache_level;
			ccs_is_src = true;
		}

		/*
		 * When there is a eviction of ccs needed smem will have the
		 * extra pages for the ccs data
		 *
		 * TO-DO: Want to move the size mismatch check to a WARN_ON,
		 * but still we have some requests of smem->lmem with same size.
		 * Need to fix it.
		 */
		ccs_bytes_to_cpy = src_sz != dst_sz ? GET_CCS_BYTES(i915, bytes_to_cpy) : 0;
		if (ccs_bytes_to_cpy)
			get_ccs_sg_sgt(&it_ccs, bytes_to_cpy);
	}

	src_offset = 0;
	dst_offset = CHUNK_SZ;
	if (HAS_64K_PAGES(ce->engine->i915)) {
		src_offset = 0;
		dst_offset = 0;
		if (src_is_lmem)
			src_offset = CHUNK_SZ;
		if (dst_is_lmem)
			dst_offset = 2 * CHUNK_SZ;
	}

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

		calculate_chunk_sz(i915, src_is_lmem, &src_sz,
				   bytes_to_cpy, ccs_bytes_to_cpy);

		len = emit_pte(rq, &it_src, src_cache_level, src_is_lmem,
			       src_offset, src_sz);
		if (!len) {
			err = -EINVAL;
			goto out_rq;
		}
		if (len < 0) {
			err = len;
			goto out_rq;
		}

		err = emit_pte(rq, &it_dst, dst_cache_level, dst_is_lmem,
			       dst_offset, len);
		if (err < 0)
			goto out_rq;
		if (err < len) {
			err = -EINVAL;
			goto out_rq;
		}

		err = rq->engine->emit_flush(rq, EMIT_INVALIDATE);
		if (err)
			goto out_rq;

		err = emit_copy(rq, dst_offset,	src_offset, len);
		if (err)
			goto out_rq;

		bytes_to_cpy -= len;

		if (ccs_bytes_to_cpy) {
			int ccs_sz;

			err = rq->engine->emit_flush(rq, EMIT_INVALIDATE);
			if (err)
				goto out_rq;

			ccs_sz = GET_CCS_BYTES(i915, len);
			err = emit_pte(rq, &it_ccs, ccs_cache_level, false,
				       ccs_is_src ? src_offset : dst_offset,
				       ccs_sz);
			if (err < 0)
				goto out_rq;
			if (err < ccs_sz) {
				err = -EINVAL;
				goto out_rq;
			}

			err = rq->engine->emit_flush(rq, EMIT_INVALIDATE);
			if (err)
				goto out_rq;

			err = emit_copy_ccs(rq, dst_offset, dst_access,
					    src_offset, src_access, len);
			if (err)
				goto out_rq;

			err = rq->engine->emit_flush(rq, EMIT_INVALIDATE);
			if (err)
				goto out_rq;
			ccs_bytes_to_cpy -= ccs_sz;
		}

		/* Arbitration is re-enabled between requests. */
out_rq:
		if (*out)
			i915_request_put(*out);
		*out = i915_request_get(rq);
		i915_request_add(rq);

		if (err)
			break;

		if (!bytes_to_cpy && !ccs_bytes_to_cpy) {
			if (src_is_lmem)
				WARN_ON(it_src.sg && sg_dma_len(it_src.sg));
			else
				WARN_ON(it_dst.sg && sg_dma_len(it_dst.sg));
			break;
		}

		if (WARN_ON(!it_src.sg || !sg_dma_len(it_src.sg) ||
			    !it_dst.sg || !sg_dma_len(it_dst.sg) ||
			    (ccs_bytes_to_cpy && (!it_ccs.sg ||
						  !sg_dma_len(it_ccs.sg))))) {
			err = -EINVAL;
			break;
		}

		cond_resched();
	} while (1);

out_ce:
	return err;
}

static int emit_clear(struct i915_request *rq, u32 offset, int size,
		      u32 value, bool is_lmem)
{
	struct drm_i915_private *i915 = rq->engine->i915;
	int mocs = rq->engine->gt->mocs.uc_index << 1;
	const int ver = GRAPHICS_VER(i915);
	int ring_sz;
	u32 *cs;

	GEM_BUG_ON(size >> PAGE_SHIFT > S16_MAX);

	if (HAS_FLAT_CCS(i915) && ver >= 12)
		ring_sz = XY_FAST_COLOR_BLT_DW;
	else if (ver >= 8)
		ring_sz = 8;
	else
		ring_sz = 6;

	cs = intel_ring_begin(rq, ring_sz);
	if (IS_ERR(cs))
		return PTR_ERR(cs);

	if (HAS_FLAT_CCS(i915) && ver >= 12) {
		*cs++ = XY_FAST_COLOR_BLT_CMD | XY_FAST_COLOR_BLT_DEPTH_32 |
			(XY_FAST_COLOR_BLT_DW - 2);
		*cs++ = FIELD_PREP(XY_FAST_COLOR_BLT_MOCS_MASK, mocs) |
			(PAGE_SIZE - 1);
		*cs++ = 0;
		*cs++ = size >> PAGE_SHIFT << 16 | PAGE_SIZE / 4;
		*cs++ = offset;
		*cs++ = rq->engine->instance;
		*cs++ = !is_lmem << XY_FAST_COLOR_BLT_MEM_TYPE_SHIFT;
		/* BG7 */
		*cs++ = value;
		*cs++ = 0;
		*cs++ = 0;
		*cs++ = 0;
		/* BG11 */
		*cs++ = 0;
		*cs++ = 0;
		/* BG13 */
		*cs++ = 0;
		*cs++ = 0;
		*cs++ = 0;
	} else if (ver >= 8) {
		*cs++ = XY_COLOR_BLT_CMD | BLT_WRITE_RGBA | (7 - 2);
		*cs++ = BLT_DEPTH_32 | BLT_ROP_COLOR_COPY | PAGE_SIZE;
		*cs++ = 0;
		*cs++ = size >> PAGE_SHIFT << 16 | PAGE_SIZE / 4;
		*cs++ = offset;
		*cs++ = rq->engine->instance;
		*cs++ = value;
		*cs++ = MI_NOOP;
	} else {
		*cs++ = XY_COLOR_BLT_CMD | BLT_WRITE_RGBA | (6 - 2);
		*cs++ = BLT_DEPTH_32 | BLT_ROP_COLOR_COPY | PAGE_SIZE;
		*cs++ = 0;
		*cs++ = size >> PAGE_SHIFT << 16 | PAGE_SIZE / 4;
		*cs++ = offset;
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
	struct drm_i915_private *i915 = ce->engine->i915;
	struct sgt_dma it = sg_sgt(sg);
	struct i915_request *rq;
	u32 offset;
	int err;

	GEM_BUG_ON(ce->vm != ce->engine->gt->migrate.context->vm);
	*out = NULL;

	GEM_BUG_ON(ce->ring->size < SZ_64K);

	offset = 0;
	if (HAS_64K_PAGES(i915) && is_lmem)
		offset = CHUNK_SZ;

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

		len = emit_pte(rq, &it, cache_level, is_lmem, offset, CHUNK_SZ);
		if (len <= 0) {
			err = len;
			goto out_rq;
		}

		err = rq->engine->emit_flush(rq, EMIT_INVALIDATE);
		if (err)
			goto out_rq;

		err = emit_clear(rq, offset, len, value, is_lmem);
		if (err)
			goto out_rq;

		if (HAS_FLAT_CCS(i915) && is_lmem && !value) {
			/*
			 * copy the content of memory into corresponding
			 * ccs surface
			 */
			err = emit_copy_ccs(rq, offset, INDIRECT_ACCESS, offset,
					    DIRECT_ACCESS, len);
			if (err)
				goto out_rq;
		}

		err = rq->engine->emit_flush(rq, EMIT_INVALIDATE);

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
