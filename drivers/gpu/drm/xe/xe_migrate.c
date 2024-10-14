// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020 Intel Corporation
 */

#include "xe_migrate.h"

#include <linux/bitfield.h>
#include <linux/sizes.h>

#include <drm/drm_managed.h>
#include <drm/ttm/ttm_tt.h>
#include <uapi/drm/xe_drm.h>

#include <generated/xe_wa_oob.h>

#include "instructions/xe_gpu_commands.h"
#include "instructions/xe_mi_commands.h"
#include "regs/xe_gtt_defs.h"
#include "tests/xe_test.h"
#include "xe_assert.h"
#include "xe_bb.h"
#include "xe_bo.h"
#include "xe_exec_queue.h"
#include "xe_ggtt.h"
#include "xe_gt.h"
#include "xe_hw_engine.h"
#include "xe_lrc.h"
#include "xe_map.h"
#include "xe_mocs.h"
#include "xe_pt.h"
#include "xe_res_cursor.h"
#include "xe_sched_job.h"
#include "xe_sync.h"
#include "xe_trace_bo.h"
#include "xe_vm.h"

/**
 * struct xe_migrate - migrate context.
 */
struct xe_migrate {
	/** @q: Default exec queue used for migration */
	struct xe_exec_queue *q;
	/** @tile: Backpointer to the tile this struct xe_migrate belongs to. */
	struct xe_tile *tile;
	/** @job_mutex: Timeline mutex for @eng. */
	struct mutex job_mutex;
	/** @pt_bo: Page-table buffer object. */
	struct xe_bo *pt_bo;
	/** @batch_base_ofs: VM offset of the migration batch buffer */
	u64 batch_base_ofs;
	/** @usm_batch_base_ofs: VM offset of the usm batch buffer */
	u64 usm_batch_base_ofs;
	/** @cleared_mem_ofs: VM offset of @cleared_bo. */
	u64 cleared_mem_ofs;
	/**
	 * @fence: dma-fence representing the last migration job batch.
	 * Protected by @job_mutex.
	 */
	struct dma_fence *fence;
	/**
	 * @vm_update_sa: For integrated, used to suballocate page-tables
	 * out of the pt_bo.
	 */
	struct drm_suballoc_manager vm_update_sa;
	/** @min_chunk_size: For dgfx, Minimum chunk size */
	u64 min_chunk_size;
};

#define MAX_PREEMPTDISABLE_TRANSFER SZ_8M /* Around 1ms. */
#define MAX_CCS_LIMITED_TRANSFER SZ_4M /* XE_PAGE_SIZE * (FIELD_MAX(XE2_CCS_SIZE_MASK) + 1) */
#define NUM_KERNEL_PDE 15
#define NUM_PT_SLOTS 32
#define LEVEL0_PAGE_TABLE_ENCODE_SIZE SZ_2M
#define MAX_NUM_PTE 512
#define IDENTITY_OFFSET 256ULL

/*
 * Although MI_STORE_DATA_IMM's "length" field is 10-bits, 0x3FE is the largest
 * legal value accepted.  Since that instruction field is always stored in
 * (val-2) format, this translates to 0x400 dwords for the true maximum length
 * of the instruction.  Subtracting the instruction header (1 dword) and
 * address (2 dwords), that leaves 0x3FD dwords (0x1FE qwords) for PTE values.
 */
#define MAX_PTE_PER_SDI 0x1FE

/**
 * xe_tile_migrate_exec_queue() - Get this tile's migrate exec queue.
 * @tile: The tile.
 *
 * Returns the default migrate exec queue of this tile.
 *
 * Return: The default migrate exec queue
 */
struct xe_exec_queue *xe_tile_migrate_exec_queue(struct xe_tile *tile)
{
	return tile->migrate->q;
}

static void xe_migrate_fini(struct drm_device *dev, void *arg)
{
	struct xe_migrate *m = arg;

	xe_vm_lock(m->q->vm, false);
	xe_bo_unpin(m->pt_bo);
	xe_vm_unlock(m->q->vm);

	dma_fence_put(m->fence);
	xe_bo_put(m->pt_bo);
	drm_suballoc_manager_fini(&m->vm_update_sa);
	mutex_destroy(&m->job_mutex);
	xe_vm_close_and_put(m->q->vm);
	xe_exec_queue_put(m->q);
}

static u64 xe_migrate_vm_addr(u64 slot, u32 level)
{
	XE_WARN_ON(slot >= NUM_PT_SLOTS);

	/* First slot is reserved for mapping of PT bo and bb, start from 1 */
	return (slot + 1ULL) << xe_pt_shift(level + 1);
}

static u64 xe_migrate_vram_ofs(struct xe_device *xe, u64 addr, bool is_comp_pte)
{
	/*
	 * Remove the DPA to get a correct offset into identity table for the
	 * migrate offset
	 */
	u64 identity_offset = IDENTITY_OFFSET;

	if (GRAPHICS_VER(xe) >= 20 && is_comp_pte)
		identity_offset += DIV_ROUND_UP_ULL(xe->mem.vram.actual_physical_size, SZ_1G);

	addr -= xe->mem.vram.dpa_base;
	return addr + (identity_offset << xe_pt_shift(2));
}

static void xe_migrate_program_identity(struct xe_device *xe, struct xe_vm *vm, struct xe_bo *bo,
					u64 map_ofs, u64 vram_offset, u16 pat_index, u64 pt_2m_ofs)
{
	u64 pos, ofs, flags;
	u64 entry;
	/* XXX: Unclear if this should be usable_size? */
	u64 vram_limit =  xe->mem.vram.actual_physical_size +
		xe->mem.vram.dpa_base;
	u32 level = 2;

	ofs = map_ofs + XE_PAGE_SIZE * level + vram_offset * 8;
	flags = vm->pt_ops->pte_encode_addr(xe, 0, pat_index, level,
					    true, 0);

	xe_assert(xe, IS_ALIGNED(xe->mem.vram.usable_size, SZ_2M));

	/*
	 * Use 1GB pages when possible, last chunk always use 2M
	 * pages as mixing reserved memory (stolen, WOCPM) with a single
	 * mapping is not allowed on certain platforms.
	 */
	for (pos = xe->mem.vram.dpa_base; pos < vram_limit;
	     pos += SZ_1G, ofs += 8) {
		if (pos + SZ_1G >= vram_limit) {
			entry = vm->pt_ops->pde_encode_bo(bo, pt_2m_ofs,
							  pat_index);
			xe_map_wr(xe, &bo->vmap, ofs, u64, entry);

			flags = vm->pt_ops->pte_encode_addr(xe, 0,
							    pat_index,
							    level - 1,
							    true, 0);

			for (ofs = pt_2m_ofs; pos < vram_limit;
			     pos += SZ_2M, ofs += 8)
				xe_map_wr(xe, &bo->vmap, ofs, u64, pos | flags);
			break;	/* Ensure pos == vram_limit assert correct */
		}

		xe_map_wr(xe, &bo->vmap, ofs, u64, pos | flags);
	}

	xe_assert(xe, pos == vram_limit);
}

static int xe_migrate_prepare_vm(struct xe_tile *tile, struct xe_migrate *m,
				 struct xe_vm *vm)
{
	struct xe_device *xe = tile_to_xe(tile);
	u16 pat_index = xe->pat.idx[XE_CACHE_WB];
	u8 id = tile->id;
	u32 num_entries = NUM_PT_SLOTS, num_level = vm->pt_root[id]->level;
#define VRAM_IDENTITY_MAP_COUNT	2
	u32 num_setup = num_level + VRAM_IDENTITY_MAP_COUNT;
#undef VRAM_IDENTITY_MAP_COUNT
	u32 map_ofs, level, i;
	struct xe_bo *bo, *batch = tile->mem.kernel_bb_pool->bo;
	u64 entry, pt29_ofs;

	/* Can't bump NUM_PT_SLOTS too high */
	BUILD_BUG_ON(NUM_PT_SLOTS > SZ_2M/XE_PAGE_SIZE);
	/* Must be a multiple of 64K to support all platforms */
	BUILD_BUG_ON(NUM_PT_SLOTS * XE_PAGE_SIZE % SZ_64K);
	/* And one slot reserved for the 4KiB page table updates */
	BUILD_BUG_ON(!(NUM_KERNEL_PDE & 1));

	/* Need to be sure everything fits in the first PT, or create more */
	xe_tile_assert(tile, m->batch_base_ofs + batch->size < SZ_2M);

	bo = xe_bo_create_pin_map(vm->xe, tile, vm,
				  num_entries * XE_PAGE_SIZE,
				  ttm_bo_type_kernel,
				  XE_BO_FLAG_VRAM_IF_DGFX(tile) |
				  XE_BO_FLAG_PINNED);
	if (IS_ERR(bo))
		return PTR_ERR(bo);

	/* PT30 & PT31 reserved for 2M identity map */
	pt29_ofs = bo->size - 3 * XE_PAGE_SIZE;
	entry = vm->pt_ops->pde_encode_bo(bo, pt29_ofs, pat_index);
	xe_pt_write(xe, &vm->pt_root[id]->bo->vmap, 0, entry);

	map_ofs = (num_entries - num_setup) * XE_PAGE_SIZE;

	/* Map the entire BO in our level 0 pt */
	for (i = 0, level = 0; i < num_entries; level++) {
		entry = vm->pt_ops->pte_encode_bo(bo, i * XE_PAGE_SIZE,
						  pat_index, 0);

		xe_map_wr(xe, &bo->vmap, map_ofs + level * 8, u64, entry);

		if (vm->flags & XE_VM_FLAG_64K)
			i += 16;
		else
			i += 1;
	}

	if (!IS_DGFX(xe)) {
		/* Write out batch too */
		m->batch_base_ofs = NUM_PT_SLOTS * XE_PAGE_SIZE;
		for (i = 0; i < batch->size;
		     i += vm->flags & XE_VM_FLAG_64K ? XE_64K_PAGE_SIZE :
		     XE_PAGE_SIZE) {
			entry = vm->pt_ops->pte_encode_bo(batch, i,
							  pat_index, 0);

			xe_map_wr(xe, &bo->vmap, map_ofs + level * 8, u64,
				  entry);
			level++;
		}
		if (xe->info.has_usm) {
			xe_tile_assert(tile, batch->size == SZ_1M);

			batch = tile->primary_gt->usm.bb_pool->bo;
			m->usm_batch_base_ofs = m->batch_base_ofs + SZ_1M;
			xe_tile_assert(tile, batch->size == SZ_512K);

			for (i = 0; i < batch->size;
			     i += vm->flags & XE_VM_FLAG_64K ? XE_64K_PAGE_SIZE :
			     XE_PAGE_SIZE) {
				entry = vm->pt_ops->pte_encode_bo(batch, i,
								  pat_index, 0);

				xe_map_wr(xe, &bo->vmap, map_ofs + level * 8, u64,
					  entry);
				level++;
			}
		}
	} else {
		u64 batch_addr = xe_bo_addr(batch, 0, XE_PAGE_SIZE);

		m->batch_base_ofs = xe_migrate_vram_ofs(xe, batch_addr, false);

		if (xe->info.has_usm) {
			batch = tile->primary_gt->usm.bb_pool->bo;
			batch_addr = xe_bo_addr(batch, 0, XE_PAGE_SIZE);
			m->usm_batch_base_ofs = xe_migrate_vram_ofs(xe, batch_addr, false);
		}
	}

	for (level = 1; level < num_level; level++) {
		u32 flags = 0;

		if (vm->flags & XE_VM_FLAG_64K && level == 1)
			flags = XE_PDE_64K;

		entry = vm->pt_ops->pde_encode_bo(bo, map_ofs + (u64)(level - 1) *
						  XE_PAGE_SIZE, pat_index);
		xe_map_wr(xe, &bo->vmap, map_ofs + XE_PAGE_SIZE * level, u64,
			  entry | flags);
	}

	/* Write PDE's that point to our BO. */
	for (i = 0; i < map_ofs / PAGE_SIZE; i++) {
		entry = vm->pt_ops->pde_encode_bo(bo, (u64)i * XE_PAGE_SIZE,
						  pat_index);

		xe_map_wr(xe, &bo->vmap, map_ofs + XE_PAGE_SIZE +
			  (i + 1) * 8, u64, entry);
	}

	/* Set up a 1GiB NULL mapping at 255GiB offset. */
	level = 2;
	xe_map_wr(xe, &bo->vmap, map_ofs + XE_PAGE_SIZE * level + 255 * 8, u64,
		  vm->pt_ops->pte_encode_addr(xe, 0, pat_index, level, IS_DGFX(xe), 0)
		  | XE_PTE_NULL);
	m->cleared_mem_ofs = (255ULL << xe_pt_shift(level));

	/* Identity map the entire vram at 256GiB offset */
	if (IS_DGFX(xe)) {
		u64 pt30_ofs = bo->size - 2 * XE_PAGE_SIZE;

		xe_migrate_program_identity(xe, vm, bo, map_ofs, IDENTITY_OFFSET,
					    pat_index, pt30_ofs);
		xe_assert(xe, xe->mem.vram.actual_physical_size <=
					(MAX_NUM_PTE - IDENTITY_OFFSET) * SZ_1G);

		/*
		 * Identity map the entire vram for compressed pat_index for xe2+
		 * if flat ccs is enabled.
		 */
		if (GRAPHICS_VER(xe) >= 20 && xe_device_has_flat_ccs(xe)) {
			u16 comp_pat_index = xe->pat.idx[XE_CACHE_NONE_COMPRESSION];
			u64 vram_offset = IDENTITY_OFFSET +
				DIV_ROUND_UP_ULL(xe->mem.vram.actual_physical_size, SZ_1G);
			u64 pt31_ofs = bo->size - XE_PAGE_SIZE;

			xe_assert(xe, xe->mem.vram.actual_physical_size <= (MAX_NUM_PTE -
						IDENTITY_OFFSET - IDENTITY_OFFSET / 2) * SZ_1G);
			xe_migrate_program_identity(xe, vm, bo, map_ofs, vram_offset,
						    comp_pat_index, pt31_ofs);
		}
	}

	/*
	 * Example layout created above, with root level = 3:
	 * [PT0...PT7]: kernel PT's for copy/clear; 64 or 4KiB PTE's
	 * [PT8]: Kernel PT for VM_BIND, 4 KiB PTE's
	 * [PT9...PT26]: Userspace PT's for VM_BIND, 4 KiB PTE's
	 * [PT27 = PDE 0] [PT28 = PDE 1] [PT29 = PDE 2] [PT30 & PT31 = 2M vram identity map]
	 *
	 * This makes the lowest part of the VM point to the pagetables.
	 * Hence the lowest 2M in the vm should point to itself, with a few writes
	 * and flushes, other parts of the VM can be used either for copying and
	 * clearing.
	 *
	 * For performance, the kernel reserves PDE's, so about 20 are left
	 * for async VM updates.
	 *
	 * To make it easier to work, each scratch PT is put in slot (1 + PT #)
	 * everywhere, this allows lockless updates to scratch pages by using
	 * the different addresses in VM.
	 */
#define NUM_VMUSA_UNIT_PER_PAGE	32
#define VM_SA_UPDATE_UNIT_SIZE		(XE_PAGE_SIZE / NUM_VMUSA_UNIT_PER_PAGE)
#define NUM_VMUSA_WRITES_PER_UNIT	(VM_SA_UPDATE_UNIT_SIZE / sizeof(u64))
	drm_suballoc_manager_init(&m->vm_update_sa,
				  (size_t)(map_ofs / XE_PAGE_SIZE - NUM_KERNEL_PDE) *
				  NUM_VMUSA_UNIT_PER_PAGE, 0);

	m->pt_bo = bo;
	return 0;
}

/*
 * Including the reserved copy engine is required to avoid deadlocks due to
 * migrate jobs servicing the faults gets stuck behind the job that faulted.
 */
static u32 xe_migrate_usm_logical_mask(struct xe_gt *gt)
{
	u32 logical_mask = 0;
	struct xe_hw_engine *hwe;
	enum xe_hw_engine_id id;

	for_each_hw_engine(hwe, gt, id) {
		if (hwe->class != XE_ENGINE_CLASS_COPY)
			continue;

		if (xe_gt_is_usm_hwe(gt, hwe))
			logical_mask |= BIT(hwe->logical_instance);
	}

	return logical_mask;
}

static bool xe_migrate_needs_ccs_emit(struct xe_device *xe)
{
	return xe_device_has_flat_ccs(xe) && !(GRAPHICS_VER(xe) >= 20 && IS_DGFX(xe));
}

/**
 * xe_migrate_init() - Initialize a migrate context
 * @tile: Back-pointer to the tile we're initializing for.
 *
 * Return: Pointer to a migrate context on success. Error pointer on error.
 */
struct xe_migrate *xe_migrate_init(struct xe_tile *tile)
{
	struct xe_device *xe = tile_to_xe(tile);
	struct xe_gt *primary_gt = tile->primary_gt;
	struct xe_migrate *m;
	struct xe_vm *vm;
	int err;

	m = drmm_kzalloc(&xe->drm, sizeof(*m), GFP_KERNEL);
	if (!m)
		return ERR_PTR(-ENOMEM);

	m->tile = tile;

	/* Special layout, prepared below.. */
	vm = xe_vm_create(xe, XE_VM_FLAG_MIGRATION |
			  XE_VM_FLAG_SET_TILE_ID(tile));
	if (IS_ERR(vm))
		return ERR_CAST(vm);

	xe_vm_lock(vm, false);
	err = xe_migrate_prepare_vm(tile, m, vm);
	xe_vm_unlock(vm);
	if (err) {
		xe_vm_close_and_put(vm);
		return ERR_PTR(err);
	}

	if (xe->info.has_usm) {
		struct xe_hw_engine *hwe = xe_gt_hw_engine(primary_gt,
							   XE_ENGINE_CLASS_COPY,
							   primary_gt->usm.reserved_bcs_instance,
							   false);
		u32 logical_mask = xe_migrate_usm_logical_mask(primary_gt);

		if (!hwe || !logical_mask)
			return ERR_PTR(-EINVAL);

		/*
		 * XXX: Currently only reserving 1 (likely slow) BCS instance on
		 * PVC, may want to revisit if performance is needed.
		 */
		m->q = xe_exec_queue_create(xe, vm, logical_mask, 1, hwe,
					    EXEC_QUEUE_FLAG_KERNEL |
					    EXEC_QUEUE_FLAG_PERMANENT |
					    EXEC_QUEUE_FLAG_HIGH_PRIORITY, 0);
	} else {
		m->q = xe_exec_queue_create_class(xe, primary_gt, vm,
						  XE_ENGINE_CLASS_COPY,
						  EXEC_QUEUE_FLAG_KERNEL |
						  EXEC_QUEUE_FLAG_PERMANENT, 0);
	}
	if (IS_ERR(m->q)) {
		xe_vm_close_and_put(vm);
		return ERR_CAST(m->q);
	}

	mutex_init(&m->job_mutex);
	fs_reclaim_acquire(GFP_KERNEL);
	might_lock(&m->job_mutex);
	fs_reclaim_release(GFP_KERNEL);

	err = drmm_add_action_or_reset(&xe->drm, xe_migrate_fini, m);
	if (err)
		return ERR_PTR(err);

	if (IS_DGFX(xe)) {
		if (xe_migrate_needs_ccs_emit(xe))
			/* min chunk size corresponds to 4K of CCS Metadata */
			m->min_chunk_size = SZ_4K * SZ_64K /
				xe_device_ccs_bytes(xe, SZ_64K);
		else
			/* Somewhat arbitrary to avoid a huge amount of blits */
			m->min_chunk_size = SZ_64K;
		m->min_chunk_size = roundup_pow_of_two(m->min_chunk_size);
		drm_dbg(&xe->drm, "Migrate min chunk size is 0x%08llx\n",
			(unsigned long long)m->min_chunk_size);
	}

	return m;
}

static u64 max_mem_transfer_per_pass(struct xe_device *xe)
{
	if (!IS_DGFX(xe) && xe_device_has_flat_ccs(xe))
		return MAX_CCS_LIMITED_TRANSFER;

	return MAX_PREEMPTDISABLE_TRANSFER;
}

static u64 xe_migrate_res_sizes(struct xe_migrate *m, struct xe_res_cursor *cur)
{
	struct xe_device *xe = tile_to_xe(m->tile);
	u64 size = min_t(u64, max_mem_transfer_per_pass(xe), cur->remaining);

	if (mem_type_is_vram(cur->mem_type)) {
		/*
		 * VRAM we want to blit in chunks with sizes aligned to
		 * min_chunk_size in order for the offset to CCS metadata to be
		 * page-aligned. If it's the last chunk it may be smaller.
		 *
		 * Another constraint is that we need to limit the blit to
		 * the VRAM block size, unless size is smaller than
		 * min_chunk_size.
		 */
		u64 chunk = max_t(u64, cur->size, m->min_chunk_size);

		size = min_t(u64, size, chunk);
		if (size > m->min_chunk_size)
			size = round_down(size, m->min_chunk_size);
	}

	return size;
}

static bool xe_migrate_allow_identity(u64 size, const struct xe_res_cursor *cur)
{
	/* If the chunk is not fragmented, allow identity map. */
	return cur->size >= size;
}

#define PTE_UPDATE_FLAG_IS_VRAM		BIT(0)
#define PTE_UPDATE_FLAG_IS_COMP_PTE	BIT(1)

static u32 pte_update_size(struct xe_migrate *m,
			   u32 flags,
			   struct ttm_resource *res,
			   struct xe_res_cursor *cur,
			   u64 *L0, u64 *L0_ofs, u32 *L0_pt,
			   u32 cmd_size, u32 pt_ofs, u32 avail_pts)
{
	u32 cmds = 0;
	bool is_vram = PTE_UPDATE_FLAG_IS_VRAM & flags;
	bool is_comp_pte = PTE_UPDATE_FLAG_IS_COMP_PTE & flags;

	*L0_pt = pt_ofs;
	if (is_vram && xe_migrate_allow_identity(*L0, cur)) {
		/* Offset into identity map. */
		*L0_ofs = xe_migrate_vram_ofs(tile_to_xe(m->tile),
					      cur->start + vram_region_gpu_offset(res),
					      is_comp_pte);
		cmds += cmd_size;
	} else {
		/* Clip L0 to available size */
		u64 size = min(*L0, (u64)avail_pts * SZ_2M);
		u32 num_4k_pages = (size + XE_PAGE_SIZE - 1) >> XE_PTE_SHIFT;

		*L0 = size;
		*L0_ofs = xe_migrate_vm_addr(pt_ofs, 0);

		/* MI_STORE_DATA_IMM */
		cmds += 3 * DIV_ROUND_UP(num_4k_pages, MAX_PTE_PER_SDI);

		/* PDE qwords */
		cmds += num_4k_pages * 2;

		/* Each chunk has a single blit command */
		cmds += cmd_size;
	}

	return cmds;
}

static void emit_pte(struct xe_migrate *m,
		     struct xe_bb *bb, u32 at_pt,
		     bool is_vram, bool is_comp_pte,
		     struct xe_res_cursor *cur,
		     u32 size, struct ttm_resource *res)
{
	struct xe_device *xe = tile_to_xe(m->tile);
	struct xe_vm *vm = m->q->vm;
	u16 pat_index;
	u32 ptes;
	u64 ofs = (u64)at_pt * XE_PAGE_SIZE;
	u64 cur_ofs;

	/* Indirect access needs compression enabled uncached PAT index */
	if (GRAPHICS_VERx100(xe) >= 2000)
		pat_index = is_comp_pte ? xe->pat.idx[XE_CACHE_NONE_COMPRESSION] :
					  xe->pat.idx[XE_CACHE_WB];
	else
		pat_index = xe->pat.idx[XE_CACHE_WB];

	ptes = DIV_ROUND_UP(size, XE_PAGE_SIZE);

	while (ptes) {
		u32 chunk = min(MAX_PTE_PER_SDI, ptes);

		bb->cs[bb->len++] = MI_STORE_DATA_IMM | MI_SDI_NUM_QW(chunk);
		bb->cs[bb->len++] = ofs;
		bb->cs[bb->len++] = 0;

		cur_ofs = ofs;
		ofs += chunk * 8;
		ptes -= chunk;

		while (chunk--) {
			u64 addr, flags = 0;
			bool devmem = false;

			addr = xe_res_dma(cur) & PAGE_MASK;
			if (is_vram) {
				if (vm->flags & XE_VM_FLAG_64K) {
					u64 va = cur_ofs * XE_PAGE_SIZE / 8;

					xe_assert(xe, (va & (SZ_64K - 1)) ==
						  (addr & (SZ_64K - 1)));

					flags |= XE_PTE_PS64;
				}

				addr += vram_region_gpu_offset(res);
				devmem = true;
			}

			addr = vm->pt_ops->pte_encode_addr(m->tile->xe,
							   addr, pat_index,
							   0, devmem, flags);
			bb->cs[bb->len++] = lower_32_bits(addr);
			bb->cs[bb->len++] = upper_32_bits(addr);

			xe_res_next(cur, min_t(u32, size, PAGE_SIZE));
			cur_ofs += 8;
		}
	}
}

#define EMIT_COPY_CCS_DW 5
static void emit_copy_ccs(struct xe_gt *gt, struct xe_bb *bb,
			  u64 dst_ofs, bool dst_is_indirect,
			  u64 src_ofs, bool src_is_indirect,
			  u32 size)
{
	struct xe_device *xe = gt_to_xe(gt);
	u32 *cs = bb->cs + bb->len;
	u32 num_ccs_blks;
	u32 num_pages;
	u32 ccs_copy_size;
	u32 mocs;

	if (GRAPHICS_VERx100(xe) >= 2000) {
		num_pages = DIV_ROUND_UP(size, XE_PAGE_SIZE);
		xe_gt_assert(gt, FIELD_FIT(XE2_CCS_SIZE_MASK, num_pages - 1));

		ccs_copy_size = REG_FIELD_PREP(XE2_CCS_SIZE_MASK, num_pages - 1);
		mocs = FIELD_PREP(XE2_XY_CTRL_SURF_MOCS_INDEX_MASK, gt->mocs.uc_index);

	} else {
		num_ccs_blks = DIV_ROUND_UP(xe_device_ccs_bytes(gt_to_xe(gt), size),
					    NUM_CCS_BYTES_PER_BLOCK);
		xe_gt_assert(gt, FIELD_FIT(CCS_SIZE_MASK, num_ccs_blks - 1));

		ccs_copy_size = REG_FIELD_PREP(CCS_SIZE_MASK, num_ccs_blks - 1);
		mocs = FIELD_PREP(XY_CTRL_SURF_MOCS_MASK, gt->mocs.uc_index);
	}

	*cs++ = XY_CTRL_SURF_COPY_BLT |
		(src_is_indirect ? 0x0 : 0x1) << SRC_ACCESS_TYPE_SHIFT |
		(dst_is_indirect ? 0x0 : 0x1) << DST_ACCESS_TYPE_SHIFT |
		ccs_copy_size;
	*cs++ = lower_32_bits(src_ofs);
	*cs++ = upper_32_bits(src_ofs) | mocs;
	*cs++ = lower_32_bits(dst_ofs);
	*cs++ = upper_32_bits(dst_ofs) | mocs;

	bb->len = cs - bb->cs;
}

#define EMIT_COPY_DW 10
static void emit_copy(struct xe_gt *gt, struct xe_bb *bb,
		      u64 src_ofs, u64 dst_ofs, unsigned int size,
		      unsigned int pitch)
{
	struct xe_device *xe = gt_to_xe(gt);
	u32 mocs = 0;
	u32 tile_y = 0;

	xe_gt_assert(gt, size / pitch <= S16_MAX);
	xe_gt_assert(gt, pitch / 4 <= S16_MAX);
	xe_gt_assert(gt, pitch <= U16_MAX);

	if (GRAPHICS_VER(xe) >= 20)
		mocs = FIELD_PREP(XE2_XY_FAST_COPY_BLT_MOCS_INDEX_MASK, gt->mocs.uc_index);

	if (GRAPHICS_VERx100(xe) >= 1250)
		tile_y = XY_FAST_COPY_BLT_D1_SRC_TILE4 | XY_FAST_COPY_BLT_D1_DST_TILE4;

	bb->cs[bb->len++] = XY_FAST_COPY_BLT_CMD | (10 - 2);
	bb->cs[bb->len++] = XY_FAST_COPY_BLT_DEPTH_32 | pitch | tile_y | mocs;
	bb->cs[bb->len++] = 0;
	bb->cs[bb->len++] = (size / pitch) << 16 | pitch / 4;
	bb->cs[bb->len++] = lower_32_bits(dst_ofs);
	bb->cs[bb->len++] = upper_32_bits(dst_ofs);
	bb->cs[bb->len++] = 0;
	bb->cs[bb->len++] = pitch | mocs;
	bb->cs[bb->len++] = lower_32_bits(src_ofs);
	bb->cs[bb->len++] = upper_32_bits(src_ofs);
}

static u64 xe_migrate_batch_base(struct xe_migrate *m, bool usm)
{
	return usm ? m->usm_batch_base_ofs : m->batch_base_ofs;
}

static u32 xe_migrate_ccs_copy(struct xe_migrate *m,
			       struct xe_bb *bb,
			       u64 src_ofs, bool src_is_indirect,
			       u64 dst_ofs, bool dst_is_indirect, u32 dst_size,
			       u64 ccs_ofs, bool copy_ccs)
{
	struct xe_gt *gt = m->tile->primary_gt;
	u32 flush_flags = 0;

	if (!copy_ccs && dst_is_indirect) {
		/*
		 * If the src is already in vram, then it should already
		 * have been cleared by us, or has been populated by the
		 * user. Make sure we copy the CCS aux state as-is.
		 *
		 * Otherwise if the bo doesn't have any CCS metadata attached,
		 * we still need to clear it for security reasons.
		 */
		u64 ccs_src_ofs =  src_is_indirect ? src_ofs : m->cleared_mem_ofs;

		emit_copy_ccs(gt, bb,
			      dst_ofs, true,
			      ccs_src_ofs, src_is_indirect, dst_size);

		flush_flags = MI_FLUSH_DW_CCS;
	} else if (copy_ccs) {
		if (!src_is_indirect)
			src_ofs = ccs_ofs;
		else if (!dst_is_indirect)
			dst_ofs = ccs_ofs;

		xe_gt_assert(gt, src_is_indirect || dst_is_indirect);

		emit_copy_ccs(gt, bb, dst_ofs, dst_is_indirect, src_ofs,
			      src_is_indirect, dst_size);
		if (dst_is_indirect)
			flush_flags = MI_FLUSH_DW_CCS;
	}

	return flush_flags;
}

/**
 * xe_migrate_copy() - Copy content of TTM resources.
 * @m: The migration context.
 * @src_bo: The buffer object @src is currently bound to.
 * @dst_bo: If copying between resources created for the same bo, set this to
 * the same value as @src_bo. If copying between buffer objects, set it to
 * the buffer object @dst is currently bound to.
 * @src: The source TTM resource.
 * @dst: The dst TTM resource.
 * @copy_only_ccs: If true copy only CCS metadata
 *
 * Copies the contents of @src to @dst: On flat CCS devices,
 * the CCS metadata is copied as well if needed, or if not present,
 * the CCS metadata of @dst is cleared for security reasons.
 *
 * Return: Pointer to a dma_fence representing the last copy batch, or
 * an error pointer on failure. If there is a failure, any copy operation
 * started by the function call has been synced.
 */
struct dma_fence *xe_migrate_copy(struct xe_migrate *m,
				  struct xe_bo *src_bo,
				  struct xe_bo *dst_bo,
				  struct ttm_resource *src,
				  struct ttm_resource *dst,
				  bool copy_only_ccs)
{
	struct xe_gt *gt = m->tile->primary_gt;
	struct xe_device *xe = gt_to_xe(gt);
	struct dma_fence *fence = NULL;
	u64 size = src_bo->size;
	struct xe_res_cursor src_it, dst_it, ccs_it;
	u64 src_L0_ofs, dst_L0_ofs;
	u32 src_L0_pt, dst_L0_pt;
	u64 src_L0, dst_L0;
	int pass = 0;
	int err;
	bool src_is_pltt = src->mem_type == XE_PL_TT;
	bool dst_is_pltt = dst->mem_type == XE_PL_TT;
	bool src_is_vram = mem_type_is_vram(src->mem_type);
	bool dst_is_vram = mem_type_is_vram(dst->mem_type);
	bool copy_ccs = xe_device_has_flat_ccs(xe) &&
		xe_bo_needs_ccs_pages(src_bo) && xe_bo_needs_ccs_pages(dst_bo);
	bool copy_system_ccs = copy_ccs && (!src_is_vram || !dst_is_vram);
	bool use_comp_pat = xe_device_has_flat_ccs(xe) &&
		GRAPHICS_VER(xe) >= 20 && src_is_vram && !dst_is_vram;

	/* Copying CCS between two different BOs is not supported yet. */
	if (XE_WARN_ON(copy_ccs && src_bo != dst_bo))
		return ERR_PTR(-EINVAL);

	if (src_bo != dst_bo && XE_WARN_ON(src_bo->size != dst_bo->size))
		return ERR_PTR(-EINVAL);

	if (!src_is_vram)
		xe_res_first_sg(xe_bo_sg(src_bo), 0, size, &src_it);
	else
		xe_res_first(src, 0, size, &src_it);
	if (!dst_is_vram)
		xe_res_first_sg(xe_bo_sg(dst_bo), 0, size, &dst_it);
	else
		xe_res_first(dst, 0, size, &dst_it);

	if (copy_system_ccs)
		xe_res_first_sg(xe_bo_sg(src_bo), xe_bo_ccs_pages_start(src_bo),
				PAGE_ALIGN(xe_device_ccs_bytes(xe, size)),
				&ccs_it);

	while (size) {
		u32 batch_size = 2; /* arb_clear() + MI_BATCH_BUFFER_END */
		struct xe_sched_job *job;
		struct xe_bb *bb;
		u32 flush_flags = 0;
		u32 update_idx;
		u64 ccs_ofs, ccs_size;
		u32 ccs_pt;
		u32 pte_flags;

		bool usm = xe->info.has_usm;
		u32 avail_pts = max_mem_transfer_per_pass(xe) / LEVEL0_PAGE_TABLE_ENCODE_SIZE;

		src_L0 = xe_migrate_res_sizes(m, &src_it);
		dst_L0 = xe_migrate_res_sizes(m, &dst_it);

		drm_dbg(&xe->drm, "Pass %u, sizes: %llu & %llu\n",
			pass++, src_L0, dst_L0);

		src_L0 = min(src_L0, dst_L0);

		pte_flags = src_is_vram ? PTE_UPDATE_FLAG_IS_VRAM : 0;
		pte_flags |= use_comp_pat ? PTE_UPDATE_FLAG_IS_COMP_PTE : 0;
		batch_size += pte_update_size(m, pte_flags, src, &src_it, &src_L0,
					      &src_L0_ofs, &src_L0_pt, 0, 0,
					      avail_pts);

		pte_flags = dst_is_vram ? PTE_UPDATE_FLAG_IS_VRAM : 0;
		batch_size += pte_update_size(m, pte_flags, dst, &dst_it, &src_L0,
					      &dst_L0_ofs, &dst_L0_pt, 0,
					      avail_pts, avail_pts);

		if (copy_system_ccs) {
			ccs_size = xe_device_ccs_bytes(xe, src_L0);
			batch_size += pte_update_size(m, 0, NULL, &ccs_it, &ccs_size,
						      &ccs_ofs, &ccs_pt, 0,
						      2 * avail_pts,
						      avail_pts);
			xe_assert(xe, IS_ALIGNED(ccs_it.start, PAGE_SIZE));
		}

		/* Add copy commands size here */
		batch_size += ((copy_only_ccs) ? 0 : EMIT_COPY_DW) +
			((xe_migrate_needs_ccs_emit(xe) ? EMIT_COPY_CCS_DW : 0));

		bb = xe_bb_new(gt, batch_size, usm);
		if (IS_ERR(bb)) {
			err = PTR_ERR(bb);
			goto err_sync;
		}

		if (src_is_vram && xe_migrate_allow_identity(src_L0, &src_it))
			xe_res_next(&src_it, src_L0);
		else
			emit_pte(m, bb, src_L0_pt, src_is_vram, copy_system_ccs,
				 &src_it, src_L0, src);

		if (dst_is_vram && xe_migrate_allow_identity(src_L0, &dst_it))
			xe_res_next(&dst_it, src_L0);
		else
			emit_pte(m, bb, dst_L0_pt, dst_is_vram, copy_system_ccs,
				 &dst_it, src_L0, dst);

		if (copy_system_ccs)
			emit_pte(m, bb, ccs_pt, false, false, &ccs_it, ccs_size, src);

		bb->cs[bb->len++] = MI_BATCH_BUFFER_END;
		update_idx = bb->len;

		if (!copy_only_ccs)
			emit_copy(gt, bb, src_L0_ofs, dst_L0_ofs, src_L0, XE_PAGE_SIZE);

		if (xe_migrate_needs_ccs_emit(xe))
			flush_flags = xe_migrate_ccs_copy(m, bb, src_L0_ofs,
							  IS_DGFX(xe) ? src_is_vram : src_is_pltt,
							  dst_L0_ofs,
							  IS_DGFX(xe) ? dst_is_vram : dst_is_pltt,
							  src_L0, ccs_ofs, copy_ccs);

		job = xe_bb_create_migration_job(m->q, bb,
						 xe_migrate_batch_base(m, usm),
						 update_idx);
		if (IS_ERR(job)) {
			err = PTR_ERR(job);
			goto err;
		}

		xe_sched_job_add_migrate_flush(job, flush_flags);
		if (!fence) {
			err = xe_sched_job_add_deps(job, src_bo->ttm.base.resv,
						    DMA_RESV_USAGE_BOOKKEEP);
			if (!err && src_bo != dst_bo)
				err = xe_sched_job_add_deps(job, dst_bo->ttm.base.resv,
							    DMA_RESV_USAGE_BOOKKEEP);
			if (err)
				goto err_job;
		}

		mutex_lock(&m->job_mutex);
		xe_sched_job_arm(job);
		dma_fence_put(fence);
		fence = dma_fence_get(&job->drm.s_fence->finished);
		xe_sched_job_push(job);

		dma_fence_put(m->fence);
		m->fence = dma_fence_get(fence);

		mutex_unlock(&m->job_mutex);

		xe_bb_free(bb, fence);
		size -= src_L0;
		continue;

err_job:
		xe_sched_job_put(job);
err:
		xe_bb_free(bb, NULL);

err_sync:
		/* Sync partial copy if any. FIXME: under job_mutex? */
		if (fence) {
			dma_fence_wait(fence, false);
			dma_fence_put(fence);
		}

		return ERR_PTR(err);
	}

	return fence;
}

static void emit_clear_link_copy(struct xe_gt *gt, struct xe_bb *bb, u64 src_ofs,
				 u32 size, u32 pitch)
{
	struct xe_device *xe = gt_to_xe(gt);
	u32 *cs = bb->cs + bb->len;
	u32 len = PVC_MEM_SET_CMD_LEN_DW;

	*cs++ = PVC_MEM_SET_CMD | PVC_MEM_SET_MATRIX | (len - 2);
	*cs++ = pitch - 1;
	*cs++ = (size / pitch) - 1;
	*cs++ = pitch - 1;
	*cs++ = lower_32_bits(src_ofs);
	*cs++ = upper_32_bits(src_ofs);
	if (GRAPHICS_VERx100(xe) >= 2000)
		*cs++ = FIELD_PREP(XE2_MEM_SET_MOCS_INDEX_MASK, gt->mocs.uc_index);
	else
		*cs++ = FIELD_PREP(PVC_MEM_SET_MOCS_INDEX_MASK, gt->mocs.uc_index);

	xe_gt_assert(gt, cs - bb->cs == len + bb->len);

	bb->len += len;
}

static void emit_clear_main_copy(struct xe_gt *gt, struct xe_bb *bb,
				 u64 src_ofs, u32 size, u32 pitch, bool is_vram)
{
	struct xe_device *xe = gt_to_xe(gt);
	u32 *cs = bb->cs + bb->len;
	u32 len = XY_FAST_COLOR_BLT_DW;

	if (GRAPHICS_VERx100(xe) < 1250)
		len = 11;

	*cs++ = XY_FAST_COLOR_BLT_CMD | XY_FAST_COLOR_BLT_DEPTH_32 |
		(len - 2);
	if (GRAPHICS_VERx100(xe) >= 2000)
		*cs++ = FIELD_PREP(XE2_XY_FAST_COLOR_BLT_MOCS_INDEX_MASK, gt->mocs.uc_index) |
			(pitch - 1);
	else
		*cs++ = FIELD_PREP(XY_FAST_COLOR_BLT_MOCS_MASK, gt->mocs.uc_index) |
			(pitch - 1);
	*cs++ = 0;
	*cs++ = (size / pitch) << 16 | pitch / 4;
	*cs++ = lower_32_bits(src_ofs);
	*cs++ = upper_32_bits(src_ofs);
	*cs++ = (is_vram ? 0x0 : 0x1) <<  XY_FAST_COLOR_BLT_MEM_TYPE_SHIFT;
	*cs++ = 0;
	*cs++ = 0;
	*cs++ = 0;
	*cs++ = 0;

	if (len > 11) {
		*cs++ = 0;
		*cs++ = 0;
		*cs++ = 0;
		*cs++ = 0;
		*cs++ = 0;
	}

	xe_gt_assert(gt, cs - bb->cs == len + bb->len);

	bb->len += len;
}

static bool has_service_copy_support(struct xe_gt *gt)
{
	/*
	 * What we care about is whether the architecture was designed with
	 * service copy functionality (specifically the new MEM_SET / MEM_COPY
	 * instructions) so check the architectural engine list rather than the
	 * actual list since these instructions are usable on BCS0 even if
	 * all of the actual service copy engines (BCS1-BCS8) have been fused
	 * off.
	 */
	return gt->info.engine_mask & GENMASK(XE_HW_ENGINE_BCS8,
					      XE_HW_ENGINE_BCS1);
}

static u32 emit_clear_cmd_len(struct xe_gt *gt)
{
	if (has_service_copy_support(gt))
		return PVC_MEM_SET_CMD_LEN_DW;
	else
		return XY_FAST_COLOR_BLT_DW;
}

static void emit_clear(struct xe_gt *gt, struct xe_bb *bb, u64 src_ofs,
		       u32 size, u32 pitch, bool is_vram)
{
	if (has_service_copy_support(gt))
		emit_clear_link_copy(gt, bb, src_ofs, size, pitch);
	else
		emit_clear_main_copy(gt, bb, src_ofs, size, pitch,
				     is_vram);
}

/**
 * xe_migrate_clear() - Copy content of TTM resources.
 * @m: The migration context.
 * @bo: The buffer object @dst is currently bound to.
 * @dst: The dst TTM resource to be cleared.
 * @clear_flags: flags to specify which data to clear: CCS, BO, or both.
 *
 * Clear the contents of @dst to zero when XE_MIGRATE_CLEAR_FLAG_BO_DATA is set.
 * On flat CCS devices, the CCS metadata is cleared to zero with XE_MIGRATE_CLEAR_FLAG_CCS_DATA.
 * Set XE_MIGRATE_CLEAR_FLAG_FULL to clear bo as well as CCS metadata.
 * TODO: Eliminate the @bo argument.
 *
 * Return: Pointer to a dma_fence representing the last clear batch, or
 * an error pointer on failure. If there is a failure, any clear operation
 * started by the function call has been synced.
 */
struct dma_fence *xe_migrate_clear(struct xe_migrate *m,
				   struct xe_bo *bo,
				   struct ttm_resource *dst,
				   u32 clear_flags)
{
	bool clear_vram = mem_type_is_vram(dst->mem_type);
	bool clear_bo_data = XE_MIGRATE_CLEAR_FLAG_BO_DATA & clear_flags;
	bool clear_ccs = XE_MIGRATE_CLEAR_FLAG_CCS_DATA & clear_flags;
	struct xe_gt *gt = m->tile->primary_gt;
	struct xe_device *xe = gt_to_xe(gt);
	bool clear_only_system_ccs = false;
	struct dma_fence *fence = NULL;
	u64 size = bo->size;
	struct xe_res_cursor src_it;
	struct ttm_resource *src = dst;
	int err;

	if (WARN_ON(!clear_bo_data && !clear_ccs))
		return NULL;

	if (!clear_bo_data && clear_ccs && !IS_DGFX(xe))
		clear_only_system_ccs = true;

	if (!clear_vram)
		xe_res_first_sg(xe_bo_sg(bo), 0, bo->size, &src_it);
	else
		xe_res_first(src, 0, bo->size, &src_it);

	while (size) {
		u64 clear_L0_ofs;
		u32 clear_L0_pt;
		u32 flush_flags = 0;
		u64 clear_L0;
		struct xe_sched_job *job;
		struct xe_bb *bb;
		u32 batch_size, update_idx;
		u32 pte_flags;

		bool usm = xe->info.has_usm;
		u32 avail_pts = max_mem_transfer_per_pass(xe) / LEVEL0_PAGE_TABLE_ENCODE_SIZE;

		clear_L0 = xe_migrate_res_sizes(m, &src_it);

		/* Calculate final sizes and batch size.. */
		pte_flags = clear_vram ? PTE_UPDATE_FLAG_IS_VRAM : 0;
		batch_size = 2 +
			pte_update_size(m, pte_flags, src, &src_it,
					&clear_L0, &clear_L0_ofs, &clear_L0_pt,
					clear_bo_data ? emit_clear_cmd_len(gt) : 0, 0,
					avail_pts);

		if (xe_migrate_needs_ccs_emit(xe))
			batch_size += EMIT_COPY_CCS_DW;

		/* Clear commands */

		if (WARN_ON_ONCE(!clear_L0))
			break;

		bb = xe_bb_new(gt, batch_size, usm);
		if (IS_ERR(bb)) {
			err = PTR_ERR(bb);
			goto err_sync;
		}

		size -= clear_L0;
		/* Preemption is enabled again by the ring ops. */
		if (clear_vram && xe_migrate_allow_identity(clear_L0, &src_it))
			xe_res_next(&src_it, clear_L0);
		else
			emit_pte(m, bb, clear_L0_pt, clear_vram, clear_only_system_ccs,
				 &src_it, clear_L0, dst);

		bb->cs[bb->len++] = MI_BATCH_BUFFER_END;
		update_idx = bb->len;

		if (clear_bo_data)
			emit_clear(gt, bb, clear_L0_ofs, clear_L0, XE_PAGE_SIZE, clear_vram);

		if (xe_migrate_needs_ccs_emit(xe)) {
			emit_copy_ccs(gt, bb, clear_L0_ofs, true,
				      m->cleared_mem_ofs, false, clear_L0);
			flush_flags = MI_FLUSH_DW_CCS;
		}

		job = xe_bb_create_migration_job(m->q, bb,
						 xe_migrate_batch_base(m, usm),
						 update_idx);
		if (IS_ERR(job)) {
			err = PTR_ERR(job);
			goto err;
		}

		xe_sched_job_add_migrate_flush(job, flush_flags);
		if (!fence) {
			/*
			 * There can't be anything userspace related at this
			 * point, so we just need to respect any potential move
			 * fences, which are always tracked as
			 * DMA_RESV_USAGE_KERNEL.
			 */
			err = xe_sched_job_add_deps(job, bo->ttm.base.resv,
						    DMA_RESV_USAGE_KERNEL);
			if (err)
				goto err_job;
		}

		mutex_lock(&m->job_mutex);
		xe_sched_job_arm(job);
		dma_fence_put(fence);
		fence = dma_fence_get(&job->drm.s_fence->finished);
		xe_sched_job_push(job);

		dma_fence_put(m->fence);
		m->fence = dma_fence_get(fence);

		mutex_unlock(&m->job_mutex);

		xe_bb_free(bb, fence);
		continue;

err_job:
		xe_sched_job_put(job);
err:
		xe_bb_free(bb, NULL);
err_sync:
		/* Sync partial copies if any. FIXME: job_mutex? */
		if (fence) {
			dma_fence_wait(m->fence, false);
			dma_fence_put(fence);
		}

		return ERR_PTR(err);
	}

	if (clear_ccs)
		bo->ccs_cleared = true;

	return fence;
}

static void write_pgtable(struct xe_tile *tile, struct xe_bb *bb, u64 ppgtt_ofs,
			  const struct xe_vm_pgtable_update_op *pt_op,
			  const struct xe_vm_pgtable_update *update,
			  struct xe_migrate_pt_update *pt_update)
{
	const struct xe_migrate_pt_update_ops *ops = pt_update->ops;
	u32 chunk;
	u32 ofs = update->ofs, size = update->qwords;

	/*
	 * If we have 512 entries (max), we would populate it ourselves,
	 * and update the PDE above it to the new pointer.
	 * The only time this can only happen if we have to update the top
	 * PDE. This requires a BO that is almost vm->size big.
	 *
	 * This shouldn't be possible in practice.. might change when 16K
	 * pages are used. Hence the assert.
	 */
	xe_tile_assert(tile, update->qwords < MAX_NUM_PTE);
	if (!ppgtt_ofs)
		ppgtt_ofs = xe_migrate_vram_ofs(tile_to_xe(tile),
						xe_bo_addr(update->pt_bo, 0,
							   XE_PAGE_SIZE), false);

	do {
		u64 addr = ppgtt_ofs + ofs * 8;

		chunk = min(size, MAX_PTE_PER_SDI);

		/* Ensure populatefn can do memset64 by aligning bb->cs */
		if (!(bb->len & 1))
			bb->cs[bb->len++] = MI_NOOP;

		bb->cs[bb->len++] = MI_STORE_DATA_IMM | MI_SDI_NUM_QW(chunk);
		bb->cs[bb->len++] = lower_32_bits(addr);
		bb->cs[bb->len++] = upper_32_bits(addr);
		if (pt_op->bind)
			ops->populate(pt_update, tile, NULL, bb->cs + bb->len,
				      ofs, chunk, update);
		else
			ops->clear(pt_update, tile, NULL, bb->cs + bb->len,
				   ofs, chunk, update);

		bb->len += chunk * 2;
		ofs += chunk;
		size -= chunk;
	} while (size);
}

struct xe_vm *xe_migrate_get_vm(struct xe_migrate *m)
{
	return xe_vm_get(m->q->vm);
}

#if IS_ENABLED(CONFIG_DRM_XE_KUNIT_TEST)
struct migrate_test_params {
	struct xe_test_priv base;
	bool force_gpu;
};

#define to_migrate_test_params(_priv) \
	container_of(_priv, struct migrate_test_params, base)
#endif

static struct dma_fence *
xe_migrate_update_pgtables_cpu(struct xe_migrate *m,
			       struct xe_migrate_pt_update *pt_update)
{
	XE_TEST_DECLARE(struct migrate_test_params *test =
			to_migrate_test_params
			(xe_cur_kunit_priv(XE_TEST_LIVE_MIGRATE));)
	const struct xe_migrate_pt_update_ops *ops = pt_update->ops;
	struct xe_vm *vm = pt_update->vops->vm;
	struct xe_vm_pgtable_update_ops *pt_update_ops =
		&pt_update->vops->pt_update_ops[pt_update->tile_id];
	int err;
	u32 i, j;

	if (XE_TEST_ONLY(test && test->force_gpu))
		return ERR_PTR(-ETIME);

	if (ops->pre_commit) {
		pt_update->job = NULL;
		err = ops->pre_commit(pt_update);
		if (err)
			return ERR_PTR(err);
	}

	for (i = 0; i < pt_update_ops->num_ops; ++i) {
		const struct xe_vm_pgtable_update_op *pt_op =
			&pt_update_ops->ops[i];

		for (j = 0; j < pt_op->num_entries; j++) {
			const struct xe_vm_pgtable_update *update =
				&pt_op->entries[j];

			if (pt_op->bind)
				ops->populate(pt_update, m->tile,
					      &update->pt_bo->vmap, NULL,
					      update->ofs, update->qwords,
					      update);
			else
				ops->clear(pt_update, m->tile,
					   &update->pt_bo->vmap, NULL,
					   update->ofs, update->qwords, update);
		}
	}

	trace_xe_vm_cpu_bind(vm);
	xe_device_wmb(vm->xe);

	return dma_fence_get_stub();
}

static struct dma_fence *
__xe_migrate_update_pgtables(struct xe_migrate *m,
			     struct xe_migrate_pt_update *pt_update,
			     struct xe_vm_pgtable_update_ops *pt_update_ops)
{
	const struct xe_migrate_pt_update_ops *ops = pt_update->ops;
	struct xe_tile *tile = m->tile;
	struct xe_gt *gt = tile->primary_gt;
	struct xe_device *xe = tile_to_xe(tile);
	struct xe_sched_job *job;
	struct dma_fence *fence;
	struct drm_suballoc *sa_bo = NULL;
	struct xe_bb *bb;
	u32 i, j, batch_size = 0, ppgtt_ofs, update_idx, page_ofs = 0;
	u32 num_updates = 0, current_update = 0;
	u64 addr;
	int err = 0;
	bool is_migrate = pt_update_ops->q == m->q;
	bool usm = is_migrate && xe->info.has_usm;

	for (i = 0; i < pt_update_ops->num_ops; ++i) {
		struct xe_vm_pgtable_update_op *pt_op = &pt_update_ops->ops[i];
		struct xe_vm_pgtable_update *updates = pt_op->entries;

		num_updates += pt_op->num_entries;
		for (j = 0; j < pt_op->num_entries; ++j) {
			u32 num_cmds = DIV_ROUND_UP(updates[j].qwords,
						    MAX_PTE_PER_SDI);

			/* align noop + MI_STORE_DATA_IMM cmd prefix */
			batch_size += 4 * num_cmds + updates[j].qwords * 2;
		}
	}

	/* fixed + PTE entries */
	if (IS_DGFX(xe))
		batch_size += 2;
	else
		batch_size += 6 * (num_updates / MAX_PTE_PER_SDI + 1) +
			num_updates * 2;

	bb = xe_bb_new(gt, batch_size, usm);
	if (IS_ERR(bb))
		return ERR_CAST(bb);

	/* For sysmem PTE's, need to map them in our hole.. */
	if (!IS_DGFX(xe)) {
		u32 ptes, ofs;

		ppgtt_ofs = NUM_KERNEL_PDE - 1;
		if (!is_migrate) {
			u32 num_units = DIV_ROUND_UP(num_updates,
						     NUM_VMUSA_WRITES_PER_UNIT);

			if (num_units > m->vm_update_sa.size) {
				err = -ENOBUFS;
				goto err_bb;
			}
			sa_bo = drm_suballoc_new(&m->vm_update_sa, num_units,
						 GFP_KERNEL, true, 0);
			if (IS_ERR(sa_bo)) {
				err = PTR_ERR(sa_bo);
				goto err_bb;
			}

			ppgtt_ofs = NUM_KERNEL_PDE +
				(drm_suballoc_soffset(sa_bo) /
				 NUM_VMUSA_UNIT_PER_PAGE);
			page_ofs = (drm_suballoc_soffset(sa_bo) %
				    NUM_VMUSA_UNIT_PER_PAGE) *
				VM_SA_UPDATE_UNIT_SIZE;
		}

		/* Map our PT's to gtt */
		i = 0;
		j = 0;
		ptes = num_updates;
		ofs = ppgtt_ofs * XE_PAGE_SIZE + page_ofs;
		while (ptes) {
			u32 chunk = min(MAX_PTE_PER_SDI, ptes);
			u32 idx = 0;

			bb->cs[bb->len++] = MI_STORE_DATA_IMM |
				MI_SDI_NUM_QW(chunk);
			bb->cs[bb->len++] = ofs;
			bb->cs[bb->len++] = 0; /* upper_32_bits */

			for (; i < pt_update_ops->num_ops; ++i) {
				struct xe_vm_pgtable_update_op *pt_op =
					&pt_update_ops->ops[i];
				struct xe_vm_pgtable_update *updates = pt_op->entries;

				for (; j < pt_op->num_entries; ++j, ++current_update, ++idx) {
					struct xe_vm *vm = pt_update->vops->vm;
					struct xe_bo *pt_bo = updates[j].pt_bo;

					if (idx == chunk)
						goto next_cmd;

					xe_tile_assert(tile, pt_bo->size == SZ_4K);

					/* Map a PT at most once */
					if (pt_bo->update_index < 0)
						pt_bo->update_index = current_update;

					addr = vm->pt_ops->pte_encode_bo(pt_bo, 0,
									 XE_CACHE_WB, 0);
					bb->cs[bb->len++] = lower_32_bits(addr);
					bb->cs[bb->len++] = upper_32_bits(addr);
				}

				j = 0;
			}

next_cmd:
			ptes -= chunk;
			ofs += chunk * sizeof(u64);
		}

		bb->cs[bb->len++] = MI_BATCH_BUFFER_END;
		update_idx = bb->len;

		addr = xe_migrate_vm_addr(ppgtt_ofs, 0) +
			(page_ofs / sizeof(u64)) * XE_PAGE_SIZE;
		for (i = 0; i < pt_update_ops->num_ops; ++i) {
			struct xe_vm_pgtable_update_op *pt_op =
				&pt_update_ops->ops[i];
			struct xe_vm_pgtable_update *updates = pt_op->entries;

			for (j = 0; j < pt_op->num_entries; ++j) {
				struct xe_bo *pt_bo = updates[j].pt_bo;

				write_pgtable(tile, bb, addr +
					      pt_bo->update_index * XE_PAGE_SIZE,
					      pt_op, &updates[j], pt_update);
			}
		}
	} else {
		/* phys pages, no preamble required */
		bb->cs[bb->len++] = MI_BATCH_BUFFER_END;
		update_idx = bb->len;

		for (i = 0; i < pt_update_ops->num_ops; ++i) {
			struct xe_vm_pgtable_update_op *pt_op =
				&pt_update_ops->ops[i];
			struct xe_vm_pgtable_update *updates = pt_op->entries;

			for (j = 0; j < pt_op->num_entries; ++j)
				write_pgtable(tile, bb, 0, pt_op, &updates[j],
					      pt_update);
		}
	}

	job = xe_bb_create_migration_job(pt_update_ops->q, bb,
					 xe_migrate_batch_base(m, usm),
					 update_idx);
	if (IS_ERR(job)) {
		err = PTR_ERR(job);
		goto err_sa;
	}

	if (ops->pre_commit) {
		pt_update->job = job;
		err = ops->pre_commit(pt_update);
		if (err)
			goto err_job;
	}
	if (is_migrate)
		mutex_lock(&m->job_mutex);

	xe_sched_job_arm(job);
	fence = dma_fence_get(&job->drm.s_fence->finished);
	xe_sched_job_push(job);

	if (is_migrate)
		mutex_unlock(&m->job_mutex);

	xe_bb_free(bb, fence);
	drm_suballoc_free(sa_bo, fence);

	return fence;

err_job:
	xe_sched_job_put(job);
err_sa:
	drm_suballoc_free(sa_bo, NULL);
err_bb:
	xe_bb_free(bb, NULL);
	return ERR_PTR(err);
}

/**
 * xe_migrate_update_pgtables() - Pipelined page-table update
 * @m: The migrate context.
 * @pt_update: PT update arguments
 *
 * Perform a pipelined page-table update. The update descriptors are typically
 * built under the same lock critical section as a call to this function. If
 * using the default engine for the updates, they will be performed in the
 * order they grab the job_mutex. If different engines are used, external
 * synchronization is needed for overlapping updates to maintain page-table
 * consistency. Note that the meaing of "overlapping" is that the updates
 * touch the same page-table, which might be a higher-level page-directory.
 * If no pipelining is needed, then updates may be performed by the cpu.
 *
 * Return: A dma_fence that, when signaled, indicates the update completion.
 */
struct dma_fence *
xe_migrate_update_pgtables(struct xe_migrate *m,
			   struct xe_migrate_pt_update *pt_update)

{
	struct xe_vm_pgtable_update_ops *pt_update_ops =
		&pt_update->vops->pt_update_ops[pt_update->tile_id];
	struct dma_fence *fence;

	fence =  xe_migrate_update_pgtables_cpu(m, pt_update);

	/* -ETIME indicates a job is needed, anything else is legit error */
	if (!IS_ERR(fence) || PTR_ERR(fence) != -ETIME)
		return fence;

	return __xe_migrate_update_pgtables(m, pt_update, pt_update_ops);
}

/**
 * xe_migrate_wait() - Complete all operations using the xe_migrate context
 * @m: Migrate context to wait for.
 *
 * Waits until the GPU no longer uses the migrate context's default engine
 * or its page-table objects. FIXME: What about separate page-table update
 * engines?
 */
void xe_migrate_wait(struct xe_migrate *m)
{
	if (m->fence)
		dma_fence_wait(m->fence, false);
}

#if IS_ENABLED(CONFIG_DRM_XE_KUNIT_TEST)
#include "tests/xe_migrate.c"
#endif
