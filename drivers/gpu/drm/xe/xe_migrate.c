// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020 Intel Corporation
 */

#include "xe_migrate.h"

#include <linux/bitfield.h>
#include <linux/sizes.h>

#include <drm/drm_managed.h>
#include <drm/ttm/ttm_tt.h>
#include <drm/xe_drm.h>

#include "regs/xe_gpu_commands.h"
#include "tests/xe_test.h"
#include "xe_bb.h"
#include "xe_bo.h"
#include "xe_engine.h"
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
#include "xe_trace.h"
#include "xe_vm.h"

/**
 * struct xe_migrate - migrate context.
 */
struct xe_migrate {
	/** @eng: Default engine used for migration */
	struct xe_engine *eng;
	/** @gt: Backpointer to the gt this struct xe_migrate belongs to. */
	struct xe_gt *gt;
	/** @job_mutex: Timeline mutex for @eng. */
	struct mutex job_mutex;
	/** @pt_bo: Page-table buffer object. */
	struct xe_bo *pt_bo;
	/**
	 * @cleared_bo: Zeroed out bo used as a source for CCS metadata clears
	 */
	struct xe_bo *cleared_bo;
	/** @batch_base_ofs: VM offset of the migration batch buffer */
	u64 batch_base_ofs;
	/** @usm_batch_base_ofs: VM offset of the usm batch buffer */
	u64 usm_batch_base_ofs;
	/** @cleared_vram_ofs: VM offset of @cleared_bo. */
	u64 cleared_vram_ofs;
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
};

#define MAX_PREEMPTDISABLE_TRANSFER SZ_8M /* Around 1ms. */
#define NUM_KERNEL_PDE 17
#define NUM_PT_SLOTS 32
#define NUM_PT_PER_BLIT (MAX_PREEMPTDISABLE_TRANSFER / SZ_2M)

/**
 * xe_gt_migrate_engine() - Get this gt's migrate engine.
 * @gt: The gt.
 *
 * Returns the default migrate engine of this gt.
 * TODO: Perhaps this function is slightly misplaced, and even unneeded?
 *
 * Return: The default migrate engine
 */
struct xe_engine *xe_gt_migrate_engine(struct xe_gt *gt)
{
	return gt->migrate->eng;
}

static void xe_migrate_fini(struct drm_device *dev, void *arg)
{
	struct xe_migrate *m = arg;
	struct ww_acquire_ctx ww;

	xe_vm_lock(m->eng->vm, &ww, 0, false);
	xe_bo_unpin(m->pt_bo);
	if (m->cleared_bo)
		xe_bo_unpin(m->cleared_bo);
	xe_vm_unlock(m->eng->vm, &ww);

	dma_fence_put(m->fence);
	if (m->cleared_bo)
		xe_bo_put(m->cleared_bo);
	xe_bo_put(m->pt_bo);
	drm_suballoc_manager_fini(&m->vm_update_sa);
	mutex_destroy(&m->job_mutex);
	xe_vm_close_and_put(m->eng->vm);
	xe_engine_put(m->eng);
}

static u64 xe_migrate_vm_addr(u64 slot, u32 level)
{
	XE_BUG_ON(slot >= NUM_PT_SLOTS);

	/* First slot is reserved for mapping of PT bo and bb, start from 1 */
	return (slot + 1ULL) << xe_pt_shift(level + 1);
}

static u64 xe_migrate_vram_ofs(u64 addr)
{
	return addr + (256ULL << xe_pt_shift(2));
}

/*
 * For flat CCS clearing we need a cleared chunk of memory to copy from,
 * since the CCS clearing mode of XY_FAST_COLOR_BLT appears to be buggy
 * (it clears on only 14 bytes in each chunk of 16).
 * If clearing the main surface one can use the part of the main surface
 * already cleared, but for clearing as part of copying non-compressed
 * data out of system memory, we don't readily have a cleared part of
 * VRAM to copy from, so create one to use for that case.
 */
static int xe_migrate_create_cleared_bo(struct xe_migrate *m, struct xe_vm *vm)
{
	struct xe_gt *gt = m->gt;
	struct xe_device *xe = vm->xe;
	size_t cleared_size;
	u64 vram_addr;
	bool is_vram;

	if (!xe_device_has_flat_ccs(xe))
		return 0;

	cleared_size = xe_device_ccs_bytes(xe, MAX_PREEMPTDISABLE_TRANSFER);
	cleared_size = PAGE_ALIGN(cleared_size);
	m->cleared_bo = xe_bo_create_pin_map(xe, gt, vm, cleared_size,
					     ttm_bo_type_kernel,
					     XE_BO_CREATE_VRAM_IF_DGFX(gt) |
					     XE_BO_CREATE_PINNED_BIT);
	if (IS_ERR(m->cleared_bo))
		return PTR_ERR(m->cleared_bo);

	xe_map_memset(xe, &m->cleared_bo->vmap, 0, 0x00, cleared_size);
	vram_addr = xe_bo_addr(m->cleared_bo, 0, XE_PAGE_SIZE, &is_vram);
	XE_BUG_ON(!is_vram);
	m->cleared_vram_ofs = xe_migrate_vram_ofs(vram_addr);

	return 0;
}

static int xe_migrate_prepare_vm(struct xe_gt *gt, struct xe_migrate *m,
				 struct xe_vm *vm)
{
	u8 id = gt->info.id;
	u32 num_entries = NUM_PT_SLOTS, num_level = vm->pt_root[id]->level;
	u32 map_ofs, level, i;
	struct xe_device *xe = gt_to_xe(m->gt);
	struct xe_bo *bo, *batch = gt->kernel_bb_pool->bo;
	u64 entry;
	int ret;

	/* Can't bump NUM_PT_SLOTS too high */
	BUILD_BUG_ON(NUM_PT_SLOTS > SZ_2M/XE_PAGE_SIZE);
	/* Must be a multiple of 64K to support all platforms */
	BUILD_BUG_ON(NUM_PT_SLOTS * XE_PAGE_SIZE % SZ_64K);
	/* And one slot reserved for the 4KiB page table updates */
	BUILD_BUG_ON(!(NUM_KERNEL_PDE & 1));

	/* Need to be sure everything fits in the first PT, or create more */
	XE_BUG_ON(m->batch_base_ofs + batch->size >= SZ_2M);

	bo = xe_bo_create_pin_map(vm->xe, m->gt, vm,
				  num_entries * XE_PAGE_SIZE,
				  ttm_bo_type_kernel,
				  XE_BO_CREATE_VRAM_IF_DGFX(m->gt) |
				  XE_BO_CREATE_PINNED_BIT);
	if (IS_ERR(bo))
		return PTR_ERR(bo);

	ret = xe_migrate_create_cleared_bo(m, vm);
	if (ret) {
		xe_bo_put(bo);
		return ret;
	}

	entry = gen8_pde_encode(bo, bo->size - XE_PAGE_SIZE, XE_CACHE_WB);
	xe_pt_write(xe, &vm->pt_root[id]->bo->vmap, 0, entry);

	map_ofs = (num_entries - num_level) * XE_PAGE_SIZE;

	/* Map the entire BO in our level 0 pt */
	for (i = 0, level = 0; i < num_entries; level++) {
		entry = gen8_pte_encode(NULL, bo, i * XE_PAGE_SIZE,
					XE_CACHE_WB, 0, 0);

		xe_map_wr(xe, &bo->vmap, map_ofs + level * 8, u64, entry);

		if (vm->flags & XE_VM_FLAGS_64K)
			i += 16;
		else
			i += 1;
	}

	if (!IS_DGFX(xe)) {
		XE_BUG_ON(xe->info.supports_usm);

		/* Write out batch too */
		m->batch_base_ofs = NUM_PT_SLOTS * XE_PAGE_SIZE;
		for (i = 0; i < batch->size;
		     i += vm->flags & XE_VM_FLAGS_64K ? XE_64K_PAGE_SIZE :
		     XE_PAGE_SIZE) {
			entry = gen8_pte_encode(NULL, batch, i,
						XE_CACHE_WB, 0, 0);

			xe_map_wr(xe, &bo->vmap, map_ofs + level * 8, u64,
				  entry);
			level++;
		}
	} else {
		bool is_vram;
		u64 batch_addr = xe_bo_addr(batch, 0, XE_PAGE_SIZE, &is_vram);

		m->batch_base_ofs = xe_migrate_vram_ofs(batch_addr);

		if (xe->info.supports_usm) {
			batch = gt->usm.bb_pool->bo;
			batch_addr = xe_bo_addr(batch, 0, XE_PAGE_SIZE,
						&is_vram);
			m->usm_batch_base_ofs = xe_migrate_vram_ofs(batch_addr);
		}
	}

	for (level = 1; level < num_level; level++) {
		u32 flags = 0;

		if (vm->flags & XE_VM_FLAGS_64K && level == 1)
			flags = XE_PDE_64K;

		entry = gen8_pde_encode(bo, map_ofs + (level - 1) *
					XE_PAGE_SIZE, XE_CACHE_WB);
		xe_map_wr(xe, &bo->vmap, map_ofs + XE_PAGE_SIZE * level, u64,
			  entry | flags);
	}

	/* Write PDE's that point to our BO. */
	for (i = 0; i < num_entries - num_level; i++) {
		entry = gen8_pde_encode(bo, i * XE_PAGE_SIZE,
					XE_CACHE_WB);

		xe_map_wr(xe, &bo->vmap, map_ofs + XE_PAGE_SIZE +
			  (i + 1) * 8, u64, entry);
	}

	/* Identity map the entire vram at 256GiB offset */
	if (IS_DGFX(xe)) {
		u64 pos, ofs, flags;

		level = 2;
		ofs = map_ofs + XE_PAGE_SIZE * level + 256 * 8;
		flags = XE_PAGE_RW | XE_PAGE_PRESENT | PPAT_CACHED |
			XE_PPGTT_PTE_LM | XE_PDPE_PS_1G;

		/*
		 * Use 1GB pages, it shouldn't matter the physical amount of
		 * vram is less, when we don't access it.
		 */
		for (pos = 0; pos < xe->mem.vram.size; pos += SZ_1G, ofs += 8)
			xe_map_wr(xe, &bo->vmap, ofs, u64, pos | flags);
	}

	/*
	 * Example layout created above, with root level = 3:
	 * [PT0...PT7]: kernel PT's for copy/clear; 64 or 4KiB PTE's
	 * [PT8]: Kernel PT for VM_BIND, 4 KiB PTE's
	 * [PT9...PT28]: Userspace PT's for VM_BIND, 4 KiB PTE's
	 * [PT29 = PDE 0] [PT30 = PDE 1] [PT31 = PDE 2]
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
				  (map_ofs / XE_PAGE_SIZE - NUM_KERNEL_PDE) *
				  NUM_VMUSA_UNIT_PER_PAGE, 0);

	m->pt_bo = bo;
	return 0;
}

/**
 * xe_migrate_init() - Initialize a migrate context
 * @gt: Back-pointer to the gt we're initializing for.
 *
 * Return: Pointer to a migrate context on success. Error pointer on error.
 */
struct xe_migrate *xe_migrate_init(struct xe_gt *gt)
{
	struct xe_device *xe = gt_to_xe(gt);
	struct xe_migrate *m;
	struct xe_vm *vm;
	struct ww_acquire_ctx ww;
	int err;

	XE_BUG_ON(xe_gt_is_media_type(gt));

	m = drmm_kzalloc(&xe->drm, sizeof(*m), GFP_KERNEL);
	if (!m)
		return ERR_PTR(-ENOMEM);

	m->gt = gt;

	/* Special layout, prepared below.. */
	vm = xe_vm_create(xe, XE_VM_FLAG_MIGRATION |
			  XE_VM_FLAG_SET_GT_ID(gt));
	if (IS_ERR(vm))
		return ERR_CAST(vm);

	xe_vm_lock(vm, &ww, 0, false);
	err = xe_migrate_prepare_vm(gt, m, vm);
	xe_vm_unlock(vm, &ww);
	if (err) {
		xe_vm_close_and_put(vm);
		return ERR_PTR(err);
	}

	if (xe->info.supports_usm) {
		struct xe_hw_engine *hwe = xe_gt_hw_engine(gt,
							   XE_ENGINE_CLASS_COPY,
							   gt->usm.reserved_bcs_instance,
							   false);
		if (!hwe)
			return ERR_PTR(-EINVAL);

		m->eng = xe_engine_create(xe, vm,
					  BIT(hwe->logical_instance), 1,
					  hwe, ENGINE_FLAG_KERNEL);
	} else {
		m->eng = xe_engine_create_class(xe, gt, vm,
						XE_ENGINE_CLASS_COPY,
						ENGINE_FLAG_KERNEL);
	}
	if (IS_ERR(m->eng)) {
		xe_vm_close_and_put(vm);
		return ERR_CAST(m->eng);
	}
	if (xe->info.supports_usm)
		m->eng->priority = XE_ENGINE_PRIORITY_KERNEL;

	mutex_init(&m->job_mutex);

	err = drmm_add_action_or_reset(&xe->drm, xe_migrate_fini, m);
	if (err)
		return ERR_PTR(err);

	return m;
}

static void emit_arb_clear(struct xe_bb *bb)
{
	/* 1 dword */
	bb->cs[bb->len++] = MI_ARB_ON_OFF | MI_ARB_DISABLE;
}

static u64 xe_migrate_res_sizes(struct xe_res_cursor *cur)
{
	/*
	 * For VRAM we use identity mapped pages so we are limited to current
	 * cursor size. For system we program the pages ourselves so we have no
	 * such limitation.
	 */
	return min_t(u64, MAX_PREEMPTDISABLE_TRANSFER,
		     mem_type_is_vram(cur->mem_type) ? cur->size :
		     cur->remaining);
}

static u32 pte_update_size(struct xe_migrate *m,
			   bool is_vram,
			   struct ttm_resource *res,
			   struct xe_res_cursor *cur,
			   u64 *L0, u64 *L0_ofs, u32 *L0_pt,
			   u32 cmd_size, u32 pt_ofs, u32 avail_pts)
{
	u32 cmds = 0;

	*L0_pt = pt_ofs;
	if (!is_vram) {
		/* Clip L0 to available size */
		u64 size = min(*L0, (u64)avail_pts * SZ_2M);
		u64 num_4k_pages = DIV_ROUND_UP(size, XE_PAGE_SIZE);

		*L0 = size;
		*L0_ofs = xe_migrate_vm_addr(pt_ofs, 0);

		/* MI_STORE_DATA_IMM */
		cmds += 3 * DIV_ROUND_UP(num_4k_pages, 0x1ff);

		/* PDE qwords */
		cmds += num_4k_pages * 2;

		/* Each chunk has a single blit command */
		cmds += cmd_size;
	} else {
		/* Offset into identity map. */
		*L0_ofs = xe_migrate_vram_ofs(cur->start +
					      vram_region_gpu_offset(res));
		cmds += cmd_size;
	}

	return cmds;
}

static void emit_pte(struct xe_migrate *m,
		     struct xe_bb *bb, u32 at_pt,
		     bool is_vram,
		     struct xe_res_cursor *cur,
		     u32 size, struct xe_bo *bo)
{
	u32 ptes;
	u64 ofs = at_pt * XE_PAGE_SIZE;
	u64 cur_ofs;

	/*
	 * FIXME: Emitting VRAM PTEs to L0 PTs is forbidden. Currently
	 * we're only emitting VRAM PTEs during sanity tests, so when
	 * that's moved to a Kunit test, we should condition VRAM PTEs
	 * on running tests.
	 */

	ptes = DIV_ROUND_UP(size, XE_PAGE_SIZE);

	while (ptes) {
		u32 chunk = min(0x1ffU, ptes);

		bb->cs[bb->len++] = MI_STORE_DATA_IMM | BIT(21) |
			(chunk * 2 + 1);
		bb->cs[bb->len++] = ofs;
		bb->cs[bb->len++] = 0;

		cur_ofs = ofs;
		ofs += chunk * 8;
		ptes -= chunk;

		while (chunk--) {
			u64 addr;

			addr = xe_res_dma(cur) & PAGE_MASK;
			if (is_vram) {
				/* Is this a 64K PTE entry? */
				if ((m->eng->vm->flags & XE_VM_FLAGS_64K) &&
				    !(cur_ofs & (16 * 8 - 1))) {
					XE_WARN_ON(!IS_ALIGNED(addr, SZ_64K));
					addr |= XE_PTE_PS64;
				}

				addr += vram_region_gpu_offset(bo->ttm.resource);
				addr |= XE_PPGTT_PTE_LM;
			}
			addr |= PPAT_CACHED | XE_PAGE_PRESENT | XE_PAGE_RW;
			bb->cs[bb->len++] = lower_32_bits(addr);
			bb->cs[bb->len++] = upper_32_bits(addr);

			xe_res_next(cur, min(size, (u32)PAGE_SIZE));
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
	u32 *cs = bb->cs + bb->len;
	u32 num_ccs_blks;
	u32 mocs = xe_mocs_index_to_value(gt->mocs.uc_index);

	num_ccs_blks = DIV_ROUND_UP(xe_device_ccs_bytes(gt_to_xe(gt), size),
				    NUM_CCS_BYTES_PER_BLOCK);
	XE_BUG_ON(num_ccs_blks > NUM_CCS_BLKS_PER_XFER);
	*cs++ = XY_CTRL_SURF_COPY_BLT |
		(src_is_indirect ? 0x0 : 0x1) << SRC_ACCESS_TYPE_SHIFT |
		(dst_is_indirect ? 0x0 : 0x1) << DST_ACCESS_TYPE_SHIFT |
		((num_ccs_blks - 1) & CCS_SIZE_MASK) << CCS_SIZE_SHIFT;
	*cs++ = lower_32_bits(src_ofs);
	*cs++ = upper_32_bits(src_ofs) |
		FIELD_PREP(XY_CTRL_SURF_MOCS_MASK, mocs);
	*cs++ = lower_32_bits(dst_ofs);
	*cs++ = upper_32_bits(dst_ofs) |
		FIELD_PREP(XY_CTRL_SURF_MOCS_MASK, mocs);

	bb->len = cs - bb->cs;
}

#define EMIT_COPY_DW 10
static void emit_copy(struct xe_gt *gt, struct xe_bb *bb,
		      u64 src_ofs, u64 dst_ofs, unsigned int size,
		      unsigned pitch)
{
	XE_BUG_ON(size / pitch > S16_MAX);
	XE_BUG_ON(pitch / 4 > S16_MAX);
	XE_BUG_ON(pitch > U16_MAX);

	bb->cs[bb->len++] = XY_FAST_COPY_BLT_CMD | (10 - 2);
	bb->cs[bb->len++] = XY_FAST_COPY_BLT_DEPTH_32 | pitch;
	bb->cs[bb->len++] = 0;
	bb->cs[bb->len++] = (size / pitch) << 16 | pitch / 4;
	bb->cs[bb->len++] = lower_32_bits(dst_ofs);
	bb->cs[bb->len++] = upper_32_bits(dst_ofs);
	bb->cs[bb->len++] = 0;
	bb->cs[bb->len++] = pitch;
	bb->cs[bb->len++] = lower_32_bits(src_ofs);
	bb->cs[bb->len++] = upper_32_bits(src_ofs);
}

static int job_add_deps(struct xe_sched_job *job, struct dma_resv *resv,
			enum dma_resv_usage usage)
{
	return drm_sched_job_add_resv_dependencies(&job->drm, resv, usage);
}

static u64 xe_migrate_batch_base(struct xe_migrate *m, bool usm)
{
	return usm ? m->usm_batch_base_ofs : m->batch_base_ofs;
}

static u32 xe_migrate_ccs_copy(struct xe_migrate *m,
			       struct xe_bb *bb,
			       u64 src_ofs, bool src_is_vram,
			       u64 dst_ofs, bool dst_is_vram, u32 dst_size,
			       u64 ccs_ofs, bool copy_ccs)
{
	struct xe_gt *gt = m->gt;
	u32 flush_flags = 0;

	if (xe_device_has_flat_ccs(gt_to_xe(gt)) && !copy_ccs && dst_is_vram) {
		/*
		 * If the src is already in vram, then it should already
		 * have been cleared by us, or has been populated by the
		 * user. Make sure we copy the CCS aux state as-is.
		 *
		 * Otherwise if the bo doesn't have any CCS metadata attached,
		 * we still need to clear it for security reasons.
		 */
		u64 ccs_src_ofs =  src_is_vram ? src_ofs : m->cleared_vram_ofs;

		emit_copy_ccs(gt, bb,
			      dst_ofs, true,
			      ccs_src_ofs, src_is_vram, dst_size);

		flush_flags = MI_FLUSH_DW_CCS;
	} else if (copy_ccs) {
		if (!src_is_vram)
			src_ofs = ccs_ofs;
		else if (!dst_is_vram)
			dst_ofs = ccs_ofs;

		/*
		 * At the moment, we don't support copying CCS metadata from
		 * system to system.
		 */
		XE_BUG_ON(!src_is_vram && !dst_is_vram);

		emit_copy_ccs(gt, bb, dst_ofs, dst_is_vram, src_ofs,
			      src_is_vram, dst_size);
		if (dst_is_vram)
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
				  struct ttm_resource *dst)
{
	struct xe_gt *gt = m->gt;
	struct xe_device *xe = gt_to_xe(gt);
	struct dma_fence *fence = NULL;
	u64 size = src_bo->size;
	struct xe_res_cursor src_it, dst_it, ccs_it;
	u64 src_L0_ofs, dst_L0_ofs;
	u32 src_L0_pt, dst_L0_pt;
	u64 src_L0, dst_L0;
	int pass = 0;
	int err;
	bool src_is_vram = mem_type_is_vram(src->mem_type);
	bool dst_is_vram = mem_type_is_vram(dst->mem_type);
	bool copy_ccs = xe_device_has_flat_ccs(xe) &&
		xe_bo_needs_ccs_pages(src_bo) && xe_bo_needs_ccs_pages(dst_bo);
	bool copy_system_ccs = copy_ccs && (!src_is_vram || !dst_is_vram);

	/* Copying CCS between two different BOs is not supported yet. */
	if (XE_WARN_ON(copy_ccs && src_bo != dst_bo))
		return ERR_PTR(-EINVAL);

	if (src_bo != dst_bo && XE_WARN_ON(src_bo->size != dst_bo->size))
		return ERR_PTR(-EINVAL);

	if (!src_is_vram)
		xe_res_first_sg(xe_bo_get_sg(src_bo), 0, size, &src_it);
	else
		xe_res_first(src, 0, size, &src_it);
	if (!dst_is_vram)
		xe_res_first_sg(xe_bo_get_sg(dst_bo), 0, size, &dst_it);
	else
		xe_res_first(dst, 0, size, &dst_it);

	if (copy_system_ccs)
		xe_res_first_sg(xe_bo_get_sg(src_bo), xe_bo_ccs_pages_start(src_bo),
				PAGE_ALIGN(xe_device_ccs_bytes(xe, size)),
				&ccs_it);

	while (size) {
		u32 batch_size = 2; /* arb_clear() + MI_BATCH_BUFFER_END */
		struct xe_sched_job *job;
		struct xe_bb *bb;
		u32 flush_flags;
		u32 update_idx;
		u64 ccs_ofs, ccs_size;
		u32 ccs_pt;
		bool usm = xe->info.supports_usm;

		src_L0 = xe_migrate_res_sizes(&src_it);
		dst_L0 = xe_migrate_res_sizes(&dst_it);

		drm_dbg(&xe->drm, "Pass %u, sizes: %llu & %llu\n",
			pass++, src_L0, dst_L0);

		src_L0 = min(src_L0, dst_L0);

		batch_size += pte_update_size(m, src_is_vram, src, &src_it, &src_L0,
					      &src_L0_ofs, &src_L0_pt, 0, 0,
					      NUM_PT_PER_BLIT);

		batch_size += pte_update_size(m, dst_is_vram, dst, &dst_it, &src_L0,
					      &dst_L0_ofs, &dst_L0_pt, 0,
					      NUM_PT_PER_BLIT, NUM_PT_PER_BLIT);

		if (copy_system_ccs) {
			ccs_size = xe_device_ccs_bytes(xe, src_L0);
			batch_size += pte_update_size(m, false, NULL, &ccs_it, &ccs_size,
						      &ccs_ofs, &ccs_pt, 0,
						      2 * NUM_PT_PER_BLIT,
						      NUM_PT_PER_BLIT);
		}

		/* Add copy commands size here */
		batch_size += EMIT_COPY_DW +
			(xe_device_has_flat_ccs(xe) ? EMIT_COPY_CCS_DW : 0);

		bb = xe_bb_new(gt, batch_size, usm);
		if (IS_ERR(bb)) {
			err = PTR_ERR(bb);
			goto err_sync;
		}

		/* Preemption is enabled again by the ring ops. */
		if (!src_is_vram || !dst_is_vram)
			emit_arb_clear(bb);

		if (!src_is_vram)
			emit_pte(m, bb, src_L0_pt, src_is_vram, &src_it, src_L0,
				 src_bo);
		else
			xe_res_next(&src_it, src_L0);

		if (!dst_is_vram)
			emit_pte(m, bb, dst_L0_pt, dst_is_vram, &dst_it, src_L0,
				 dst_bo);
		else
			xe_res_next(&dst_it, src_L0);

		if (copy_system_ccs)
			emit_pte(m, bb, ccs_pt, false, &ccs_it, ccs_size, src_bo);

		bb->cs[bb->len++] = MI_BATCH_BUFFER_END;
		update_idx = bb->len;

		emit_copy(gt, bb, src_L0_ofs, dst_L0_ofs, src_L0,
			  XE_PAGE_SIZE);
		flush_flags = xe_migrate_ccs_copy(m, bb, src_L0_ofs, src_is_vram,
						  dst_L0_ofs, dst_is_vram,
						  src_L0, ccs_ofs, copy_ccs);

		mutex_lock(&m->job_mutex);
		job = xe_bb_create_migration_job(m->eng, bb,
						 xe_migrate_batch_base(m, usm),
						 update_idx);
		if (IS_ERR(job)) {
			err = PTR_ERR(job);
			goto err;
		}

		xe_sched_job_add_migrate_flush(job, flush_flags);
		if (!fence) {
			err = job_add_deps(job, src_bo->ttm.base.resv,
					   DMA_RESV_USAGE_BOOKKEEP);
			if (!err && src_bo != dst_bo)
				err = job_add_deps(job, dst_bo->ttm.base.resv,
						   DMA_RESV_USAGE_BOOKKEEP);
			if (err)
				goto err_job;
		}

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
		mutex_unlock(&m->job_mutex);
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
	u32 *cs = bb->cs + bb->len;
	u32 mocs = xe_mocs_index_to_value(gt->mocs.uc_index);
	u32 len = PVC_MEM_SET_CMD_LEN_DW;

	*cs++ = PVC_MEM_SET_CMD | PVC_MS_MATRIX | (len - 2);
	*cs++ = pitch - 1;
	*cs++ = (size / pitch) - 1;
	*cs++ = pitch - 1;
	*cs++ = lower_32_bits(src_ofs);
	*cs++ = upper_32_bits(src_ofs);
	*cs++ = FIELD_PREP(PVC_MS_MOCS_INDEX_MASK, mocs);

	XE_BUG_ON(cs - bb->cs != len + bb->len);

	bb->len += len;
}

static void emit_clear_main_copy(struct xe_gt *gt, struct xe_bb *bb,
				 u64 src_ofs, u32 size, u32 pitch, bool is_vram)
{
	struct xe_device *xe = gt_to_xe(gt);
	u32 *cs = bb->cs + bb->len;
	u32 len = XY_FAST_COLOR_BLT_DW;
	u32 mocs = xe_mocs_index_to_value(gt->mocs.uc_index);

	if (GRAPHICS_VERx100(xe) < 1250)
		len = 11;

	*cs++ = XY_FAST_COLOR_BLT_CMD | XY_FAST_COLOR_BLT_DEPTH_32 |
		(len - 2);
	*cs++ = FIELD_PREP(XY_FAST_COLOR_BLT_MOCS_MASK, mocs) |
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

	XE_BUG_ON(cs - bb->cs != len + bb->len);

	bb->len += len;
}

static u32 emit_clear_cmd_len(struct xe_device *xe)
{
	if (xe->info.has_link_copy_engine)
		return PVC_MEM_SET_CMD_LEN_DW;
	else
		return XY_FAST_COLOR_BLT_DW;
}

static int emit_clear(struct xe_gt *gt, struct xe_bb *bb, u64 src_ofs,
		      u32 size, u32 pitch, bool is_vram)
{
	struct xe_device *xe = gt_to_xe(gt);

	if (xe->info.has_link_copy_engine) {
		emit_clear_link_copy(gt, bb, src_ofs, size, pitch);

	} else {
		emit_clear_main_copy(gt, bb, src_ofs, size, pitch,
				     is_vram);
	}

	return 0;
}

/**
 * xe_migrate_clear() - Copy content of TTM resources.
 * @m: The migration context.
 * @bo: The buffer object @dst is currently bound to.
 * @dst: The dst TTM resource to be cleared.
 *
 * Clear the contents of @dst to zero. On flat CCS devices,
 * the CCS metadata is cleared to zero as well on VRAM destinations.
 * TODO: Eliminate the @bo argument.
 *
 * Return: Pointer to a dma_fence representing the last clear batch, or
 * an error pointer on failure. If there is a failure, any clear operation
 * started by the function call has been synced.
 */
struct dma_fence *xe_migrate_clear(struct xe_migrate *m,
				   struct xe_bo *bo,
				   struct ttm_resource *dst)
{
	bool clear_vram = mem_type_is_vram(dst->mem_type);
	struct xe_gt *gt = m->gt;
	struct xe_device *xe = gt_to_xe(gt);
	struct dma_fence *fence = NULL;
	u64 size = bo->size;
	struct xe_res_cursor src_it;
	struct ttm_resource *src = dst;
	int err;
	int pass = 0;

	if (!clear_vram)
		xe_res_first_sg(xe_bo_get_sg(bo), 0, bo->size, &src_it);
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
		bool usm = xe->info.supports_usm;

		clear_L0 = xe_migrate_res_sizes(&src_it);
		drm_dbg(&xe->drm, "Pass %u, size: %llu\n", pass++, clear_L0);

		/* Calculate final sizes and batch size.. */
		batch_size = 2 +
			pte_update_size(m, clear_vram, src, &src_it,
					&clear_L0, &clear_L0_ofs, &clear_L0_pt,
					emit_clear_cmd_len(xe), 0,
					NUM_PT_PER_BLIT);
		if (xe_device_has_flat_ccs(xe) && clear_vram)
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

		/* TODO: Add dependencies here */

		/* Preemption is enabled again by the ring ops. */
		if (!clear_vram) {
			emit_arb_clear(bb);
			emit_pte(m, bb, clear_L0_pt, clear_vram, &src_it, clear_L0,
				 bo);
		} else {
			xe_res_next(&src_it, clear_L0);
		}
		bb->cs[bb->len++] = MI_BATCH_BUFFER_END;
		update_idx = bb->len;

		emit_clear(gt, bb, clear_L0_ofs, clear_L0, XE_PAGE_SIZE,
			   clear_vram);
		if (xe_device_has_flat_ccs(xe) && clear_vram) {
			emit_copy_ccs(gt, bb, clear_L0_ofs, true,
				      m->cleared_vram_ofs, false, clear_L0);
			flush_flags = MI_FLUSH_DW_CCS;
		}

		mutex_lock(&m->job_mutex);
		job = xe_bb_create_migration_job(m->eng, bb,
						 xe_migrate_batch_base(m, usm),
						 update_idx);
		if (IS_ERR(job)) {
			err = PTR_ERR(job);
			goto err;
		}

		xe_sched_job_add_migrate_flush(job, flush_flags);

		xe_sched_job_arm(job);
		dma_fence_put(fence);
		fence = dma_fence_get(&job->drm.s_fence->finished);
		xe_sched_job_push(job);

		dma_fence_put(m->fence);
		m->fence = dma_fence_get(fence);

		mutex_unlock(&m->job_mutex);

		xe_bb_free(bb, fence);
		continue;

err:
		mutex_unlock(&m->job_mutex);
		xe_bb_free(bb, NULL);
err_sync:
		/* Sync partial copies if any. FIXME: job_mutex? */
		if (fence) {
			dma_fence_wait(m->fence, false);
			dma_fence_put(fence);
		}

		return ERR_PTR(err);
	}

	return fence;
}

static void write_pgtable(struct xe_gt *gt, struct xe_bb *bb, u64 ppgtt_ofs,
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
	 * pages are used. Hence the BUG_ON.
	 */
	XE_BUG_ON(update->qwords > 0x1ff);
	if (!ppgtt_ofs) {
		bool is_vram;

		ppgtt_ofs = xe_migrate_vram_ofs(xe_bo_addr(update->pt_bo, 0,
							   XE_PAGE_SIZE,
							   &is_vram));
		XE_BUG_ON(!is_vram);
	}

	do {
		u64 addr = ppgtt_ofs + ofs * 8;
		chunk = min(update->qwords, 0x1ffU);

		/* Ensure populatefn can do memset64 by aligning bb->cs */
		if (!(bb->len & 1))
			bb->cs[bb->len++] = MI_NOOP;

		bb->cs[bb->len++] = MI_STORE_DATA_IMM | BIT(21) |
			(chunk * 2 + 1);
		bb->cs[bb->len++] = lower_32_bits(addr);
		bb->cs[bb->len++] = upper_32_bits(addr);
		ops->populate(pt_update, gt, NULL, bb->cs + bb->len, ofs, chunk,
			      update);

		bb->len += chunk * 2;
		ofs += chunk;
		size -= chunk;
	} while (size);
}

struct xe_vm *xe_migrate_get_vm(struct xe_migrate *m)
{
	return xe_vm_get(m->eng->vm);
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
			       struct xe_vm *vm, struct xe_bo *bo,
			       const struct  xe_vm_pgtable_update *updates,
			       u32 num_updates, bool wait_vm,
			       struct xe_migrate_pt_update *pt_update)
{
	XE_TEST_DECLARE(struct migrate_test_params *test =
			to_migrate_test_params
			(xe_cur_kunit_priv(XE_TEST_LIVE_MIGRATE));)
	const struct xe_migrate_pt_update_ops *ops = pt_update->ops;
	struct dma_fence *fence;
	int err;
	u32 i;

	if (XE_TEST_ONLY(test && test->force_gpu))
		return ERR_PTR(-ETIME);

	if (bo && !dma_resv_test_signaled(bo->ttm.base.resv,
					  DMA_RESV_USAGE_KERNEL))
		return ERR_PTR(-ETIME);

	if (wait_vm && !dma_resv_test_signaled(&vm->resv,
					       DMA_RESV_USAGE_BOOKKEEP))
		return ERR_PTR(-ETIME);

	if (ops->pre_commit) {
		err = ops->pre_commit(pt_update);
		if (err)
			return ERR_PTR(err);
	}
	for (i = 0; i < num_updates; i++) {
		const struct xe_vm_pgtable_update *update = &updates[i];

		ops->populate(pt_update, m->gt, &update->pt_bo->vmap, NULL,
			      update->ofs, update->qwords, update);
	}

	if (vm) {
		trace_xe_vm_cpu_bind(vm);
		xe_device_wmb(vm->xe);
	}

	fence = dma_fence_get_stub();

	return fence;
}

static bool no_in_syncs(struct xe_sync_entry *syncs, u32 num_syncs)
{
	int i;

	for (i = 0; i < num_syncs; i++) {
		struct dma_fence *fence = syncs[i].fence;

		if (fence && !test_bit(DMA_FENCE_FLAG_SIGNALED_BIT,
				       &fence->flags))
			return false;
	}

	return true;
}

/**
 * xe_migrate_update_pgtables() - Pipelined page-table update
 * @m: The migrate context.
 * @vm: The vm we'll be updating.
 * @bo: The bo whose dma-resv we will await before updating, or NULL if userptr.
 * @eng: The engine to be used for the update or NULL if the default
 * migration engine is to be used.
 * @updates: An array of update descriptors.
 * @num_updates: Number of descriptors in @updates.
 * @syncs: Array of xe_sync_entry to await before updating. Note that waits
 * will block the engine timeline.
 * @num_syncs: Number of entries in @syncs.
 * @pt_update: Pointer to a struct xe_migrate_pt_update, which contains
 * pointers to callback functions and, if subclassed, private arguments to
 * those.
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
			   struct xe_vm *vm,
			   struct xe_bo *bo,
			   struct xe_engine *eng,
			   const struct xe_vm_pgtable_update *updates,
			   u32 num_updates,
			   struct xe_sync_entry *syncs, u32 num_syncs,
			   struct xe_migrate_pt_update *pt_update)
{
	const struct xe_migrate_pt_update_ops *ops = pt_update->ops;
	struct xe_gt *gt = m->gt;
	struct xe_device *xe = gt_to_xe(gt);
	struct xe_sched_job *job;
	struct dma_fence *fence;
	struct drm_suballoc *sa_bo = NULL;
	struct xe_vma *vma = pt_update->vma;
	struct xe_bb *bb;
	u32 i, batch_size, ppgtt_ofs, update_idx, page_ofs = 0;
	u64 addr;
	int err = 0;
	bool usm = !eng && xe->info.supports_usm;
	bool first_munmap_rebind = vma && vma->first_munmap_rebind;

	/* Use the CPU if no in syncs and engine is idle */
	if (no_in_syncs(syncs, num_syncs) && (!eng || xe_engine_is_idle(eng))) {
		fence =  xe_migrate_update_pgtables_cpu(m, vm, bo, updates,
							num_updates,
							first_munmap_rebind,
							pt_update);
		if (!IS_ERR(fence) || fence == ERR_PTR(-EAGAIN))
			return fence;
	}

	/* fixed + PTE entries */
	if (IS_DGFX(xe))
		batch_size = 2;
	else
		batch_size = 6 + num_updates * 2;

	for (i = 0; i < num_updates; i++) {
		u32 num_cmds = DIV_ROUND_UP(updates[i].qwords, 0x1ff);

		/* align noop + MI_STORE_DATA_IMM cmd prefix */
		batch_size += 4 * num_cmds + updates[i].qwords * 2;
	}

	/*
	 * XXX: Create temp bo to copy from, if batch_size becomes too big?
	 *
	 * Worst case: Sum(2 * (each lower level page size) + (top level page size))
	 * Should be reasonably bound..
	 */
	XE_BUG_ON(batch_size >= SZ_128K);

	bb = xe_bb_new(gt, batch_size, !eng && xe->info.supports_usm);
	if (IS_ERR(bb))
		return ERR_CAST(bb);

	/* For sysmem PTE's, need to map them in our hole.. */
	if (!IS_DGFX(xe)) {
		ppgtt_ofs = NUM_KERNEL_PDE - 1;
		if (eng) {
			XE_BUG_ON(num_updates > NUM_VMUSA_WRITES_PER_UNIT);

			sa_bo = drm_suballoc_new(&m->vm_update_sa, 1,
						 GFP_KERNEL, true, 0);
			if (IS_ERR(sa_bo)) {
				err = PTR_ERR(sa_bo);
				goto err;
			}

			ppgtt_ofs = NUM_KERNEL_PDE +
				(drm_suballoc_soffset(sa_bo) /
				 NUM_VMUSA_UNIT_PER_PAGE);
			page_ofs = (drm_suballoc_soffset(sa_bo) %
				    NUM_VMUSA_UNIT_PER_PAGE) *
				VM_SA_UPDATE_UNIT_SIZE;
		}

		/* Preemption is enabled again by the ring ops. */
		emit_arb_clear(bb);

		/* Map our PT's to gtt */
		bb->cs[bb->len++] = MI_STORE_DATA_IMM | BIT(21) |
			(num_updates * 2 + 1);
		bb->cs[bb->len++] = ppgtt_ofs * XE_PAGE_SIZE + page_ofs;
		bb->cs[bb->len++] = 0; /* upper_32_bits */

		for (i = 0; i < num_updates; i++) {
			struct xe_bo *pt_bo = updates[i].pt_bo;

			BUG_ON(pt_bo->size != SZ_4K);

			addr = gen8_pte_encode(NULL, pt_bo, 0, XE_CACHE_WB,
					       0, 0);
			bb->cs[bb->len++] = lower_32_bits(addr);
			bb->cs[bb->len++] = upper_32_bits(addr);
		}

		bb->cs[bb->len++] = MI_BATCH_BUFFER_END;
		update_idx = bb->len;

		addr = xe_migrate_vm_addr(ppgtt_ofs, 0) +
			(page_ofs / sizeof(u64)) * XE_PAGE_SIZE;
		for (i = 0; i < num_updates; i++)
			write_pgtable(m->gt, bb, addr + i * XE_PAGE_SIZE,
				      &updates[i], pt_update);
	} else {
		/* phys pages, no preamble required */
		bb->cs[bb->len++] = MI_BATCH_BUFFER_END;
		update_idx = bb->len;

		/* Preemption is enabled again by the ring ops. */
		emit_arb_clear(bb);
		for (i = 0; i < num_updates; i++)
			write_pgtable(m->gt, bb, 0, &updates[i], pt_update);
	}

	if (!eng)
		mutex_lock(&m->job_mutex);

	job = xe_bb_create_migration_job(eng ?: m->eng, bb,
					 xe_migrate_batch_base(m, usm),
					 update_idx);
	if (IS_ERR(job)) {
		err = PTR_ERR(job);
		goto err_bb;
	}

	/* Wait on BO move */
	if (bo) {
		err = job_add_deps(job, bo->ttm.base.resv,
				   DMA_RESV_USAGE_KERNEL);
		if (err)
			goto err_job;
	}

	/*
	 * Munmap style VM unbind, need to wait for all jobs to be complete /
	 * trigger preempts before moving forward
	 */
	if (first_munmap_rebind) {
		err = job_add_deps(job, &vm->resv,
				   DMA_RESV_USAGE_BOOKKEEP);
		if (err)
			goto err_job;
	}

	for (i = 0; !err && i < num_syncs; i++)
		err = xe_sync_entry_add_deps(&syncs[i], job);

	if (err)
		goto err_job;

	if (ops->pre_commit) {
		err = ops->pre_commit(pt_update);
		if (err)
			goto err_job;
	}
	xe_sched_job_arm(job);
	fence = dma_fence_get(&job->drm.s_fence->finished);
	xe_sched_job_push(job);

	if (!eng)
		mutex_unlock(&m->job_mutex);

	xe_bb_free(bb, fence);
	drm_suballoc_free(sa_bo, fence);

	return fence;

err_job:
	xe_sched_job_put(job);
err_bb:
	if (!eng)
		mutex_unlock(&m->job_mutex);
	xe_bb_free(bb, NULL);
err:
	drm_suballoc_free(sa_bo, NULL);
	return ERR_PTR(err);
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
