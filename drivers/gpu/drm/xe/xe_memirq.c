// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include <drm/drm_managed.h>

#include "regs/xe_gt_regs.h"
#include "regs/xe_guc_regs.h"
#include "regs/xe_regs.h"

#include "xe_assert.h"
#include "xe_bo.h"
#include "xe_device.h"
#include "xe_device_types.h"
#include "xe_gt.h"
#include "xe_gt_printk.h"
#include "xe_guc.h"
#include "xe_hw_engine.h"
#include "xe_map.h"
#include "xe_memirq.h"
#include "xe_sriov.h"
#include "xe_sriov_printk.h"

#define memirq_assert(m, condition)	xe_tile_assert(memirq_to_tile(m), condition)
#define memirq_debug(m, msg...)		xe_sriov_dbg_verbose(memirq_to_xe(m), "MEMIRQ: " msg)

static struct xe_tile *memirq_to_tile(struct xe_memirq *memirq)
{
	return container_of(memirq, struct xe_tile, sriov.vf.memirq);
}

static struct xe_device *memirq_to_xe(struct xe_memirq *memirq)
{
	return tile_to_xe(memirq_to_tile(memirq));
}

static const char *guc_name(struct xe_guc *guc)
{
	return xe_gt_is_media_type(guc_to_gt(guc)) ? "media GuC" : "GuC";
}

/**
 * DOC: Memory Based Interrupts
 *
 * MMIO register based interrupts infrastructure used for non-virtualized mode
 * or SRIOV-8 (which supports 8 Virtual Functions) does not scale efficiently
 * to allow delivering interrupts to a large number of Virtual machines or
 * containers. Memory based interrupt status reporting provides an efficient
 * and scalable infrastructure.
 *
 * For memory based interrupt status reporting hardware sequence is:
 *  * Engine writes the interrupt event to memory
 *    (Pointer to memory location is provided by SW. This memory surface must
 *    be mapped to system memory and must be marked as un-cacheable (UC) on
 *    Graphics IP Caches)
 *  * Engine triggers an interrupt to host.
 */

/**
 * DOC: Memory Based Interrupts Page Layout
 *
 * `Memory Based Interrupts`_ requires three different objects, which are
 * called "page" in the specs, even if they aren't page-sized or aligned.
 *
 * To simplify the code we allocate a single page size object and then use
 * offsets to embedded "pages". The address of those "pages" are then
 * programmed in the HW via LRI and LRM in the context image.
 *
 * - _`Interrupt Status Report Page`: this page contains the interrupt
 *   status vectors for each unit. Each bit in the interrupt vectors is
 *   converted to a byte, with the byte being set to 0xFF when an
 *   interrupt is triggered; interrupt vectors are 16b big so each unit
 *   gets 16B. One space is reserved for each bit in one of the
 *   GT_INTR_DWx registers, so this object needs a total of 1024B.
 *   This object needs to be 4KiB aligned.
 *
 * - _`Interrupt Source Report Page`: this is the equivalent of the
 *   GEN11_GT_INTR_DWx registers, with each bit in those registers being
 *   mapped to a byte here. The offsets are the same, just bytes instead
 *   of bits. This object needs to be cacheline aligned.
 *
 * - Interrupt Mask: the HW needs a location to fetch the interrupt
 *   mask vector to be used by the LRM in the context, so we just use
 *   the next available space in the interrupt page.
 *
 * ::
 *
 *   0x0000   +===========+  <== Interrupt Status Report Page
 *            |           |
 *            |           |     ____ +----+----------------+
 *            |           |    /     |  0 | USER INTERRUPT |
 *            +-----------+ __/      |  1 |                |
 *            |  HWE(n)   | __       |    | CTX SWITCH     |
 *            +-----------+   \      |    | WAIT SEMAPHORE |
 *            |           |    \____ | 15 |                |
 *            |           |          +----+----------------+
 *            |           |
 *   0x0400   +===========+  <== Interrupt Source Report Page
 *            |  HWE(0)   |
 *            |  HWE(1)   |
 *            |           |
 *            |  HWE(x)   |
 *   0x0440   +===========+  <== Interrupt Enable Mask
 *            |           |
 *            |           |
 *            +-----------+
 */

static void __release_xe_bo(struct drm_device *drm, void *arg)
{
	struct xe_bo *bo = arg;

	xe_bo_unpin_map_no_vm(bo);
}

static int memirq_alloc_pages(struct xe_memirq *memirq)
{
	struct xe_device *xe = memirq_to_xe(memirq);
	struct xe_tile *tile = memirq_to_tile(memirq);
	struct xe_bo *bo;
	int err;

	BUILD_BUG_ON(!IS_ALIGNED(XE_MEMIRQ_SOURCE_OFFSET, SZ_64));
	BUILD_BUG_ON(!IS_ALIGNED(XE_MEMIRQ_STATUS_OFFSET, SZ_4K));

	/* XXX: convert to managed bo */
	bo = xe_bo_create_pin_map(xe, tile, NULL, SZ_4K,
				  ttm_bo_type_kernel,
				  XE_BO_FLAG_SYSTEM |
				  XE_BO_FLAG_GGTT |
				  XE_BO_FLAG_GGTT_INVALIDATE |
				  XE_BO_FLAG_NEEDS_UC |
				  XE_BO_FLAG_NEEDS_CPU_ACCESS);
	if (IS_ERR(bo)) {
		err = PTR_ERR(bo);
		goto out;
	}

	memirq_assert(memirq, !xe_bo_is_vram(bo));
	memirq_assert(memirq, !memirq->bo);

	iosys_map_memset(&bo->vmap, 0, 0, SZ_4K);

	memirq->bo = bo;
	memirq->source = IOSYS_MAP_INIT_OFFSET(&bo->vmap, XE_MEMIRQ_SOURCE_OFFSET);
	memirq->status = IOSYS_MAP_INIT_OFFSET(&bo->vmap, XE_MEMIRQ_STATUS_OFFSET);
	memirq->mask = IOSYS_MAP_INIT_OFFSET(&bo->vmap, XE_MEMIRQ_ENABLE_OFFSET);

	memirq_assert(memirq, !memirq->source.is_iomem);
	memirq_assert(memirq, !memirq->status.is_iomem);
	memirq_assert(memirq, !memirq->mask.is_iomem);

	memirq_debug(memirq, "page offsets: source %#x status %#x\n",
		     xe_memirq_source_ptr(memirq), xe_memirq_status_ptr(memirq));

	return drmm_add_action_or_reset(&xe->drm, __release_xe_bo, memirq->bo);

out:
	xe_sriov_err(memirq_to_xe(memirq),
		     "Failed to allocate memirq page (%pe)\n", ERR_PTR(err));
	return err;
}

static void memirq_set_enable(struct xe_memirq *memirq, bool enable)
{
	iosys_map_wr(&memirq->mask, 0, u32, enable ? GENMASK(15, 0) : 0);

	memirq->enabled = enable;
}

/**
 * xe_memirq_init - Initialize data used by `Memory Based Interrupts`_.
 * @memirq: the &xe_memirq to initialize
 *
 * Allocate `Interrupt Source Report Page`_ and `Interrupt Status Report Page`_
 * used by `Memory Based Interrupts`_.
 *
 * These allocations are managed and will be implicitly released on unload.
 *
 * Note: This function shall be called only by the VF driver.
 *
 * If this function fails then VF driver won't be able to operate correctly.
 * If `Memory Based Interrupts`_ are not used this function will return 0.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_memirq_init(struct xe_memirq *memirq)
{
	struct xe_device *xe = memirq_to_xe(memirq);
	int err;

	memirq_assert(memirq, IS_SRIOV_VF(xe));

	if (!xe_device_has_memirq(xe))
		return 0;

	err = memirq_alloc_pages(memirq);
	if (unlikely(err))
		return err;

	/* we need to start with all irqs enabled */
	memirq_set_enable(memirq, true);

	return 0;
}

/**
 * xe_memirq_source_ptr - Get GGTT's offset of the `Interrupt Source Report Page`_.
 * @memirq: the &xe_memirq to query
 *
 * Shall be called only on VF driver when `Memory Based Interrupts`_ are used
 * and xe_memirq_init() didn't fail.
 *
 * Return: GGTT's offset of the `Interrupt Source Report Page`_.
 */
u32 xe_memirq_source_ptr(struct xe_memirq *memirq)
{
	memirq_assert(memirq, IS_SRIOV_VF(memirq_to_xe(memirq)));
	memirq_assert(memirq, xe_device_has_memirq(memirq_to_xe(memirq)));
	memirq_assert(memirq, memirq->bo);

	return xe_bo_ggtt_addr(memirq->bo) + XE_MEMIRQ_SOURCE_OFFSET;
}

/**
 * xe_memirq_status_ptr - Get GGTT's offset of the `Interrupt Status Report Page`_.
 * @memirq: the &xe_memirq to query
 *
 * Shall be called only on VF driver when `Memory Based Interrupts`_ are used
 * and xe_memirq_init() didn't fail.
 *
 * Return: GGTT's offset of the `Interrupt Status Report Page`_.
 */
u32 xe_memirq_status_ptr(struct xe_memirq *memirq)
{
	memirq_assert(memirq, IS_SRIOV_VF(memirq_to_xe(memirq)));
	memirq_assert(memirq, xe_device_has_memirq(memirq_to_xe(memirq)));
	memirq_assert(memirq, memirq->bo);

	return xe_bo_ggtt_addr(memirq->bo) + XE_MEMIRQ_STATUS_OFFSET;
}

/**
 * xe_memirq_enable_ptr - Get GGTT's offset of the Interrupt Enable Mask.
 * @memirq: the &xe_memirq to query
 *
 * Shall be called only on VF driver when `Memory Based Interrupts`_ are used
 * and xe_memirq_init() didn't fail.
 *
 * Return: GGTT's offset of the Interrupt Enable Mask.
 */
u32 xe_memirq_enable_ptr(struct xe_memirq *memirq)
{
	memirq_assert(memirq, IS_SRIOV_VF(memirq_to_xe(memirq)));
	memirq_assert(memirq, xe_device_has_memirq(memirq_to_xe(memirq)));
	memirq_assert(memirq, memirq->bo);

	return xe_bo_ggtt_addr(memirq->bo) + XE_MEMIRQ_ENABLE_OFFSET;
}

/**
 * xe_memirq_init_guc - Prepare GuC for `Memory Based Interrupts`_.
 * @memirq: the &xe_memirq
 * @guc: the &xe_guc to setup
 *
 * Register `Interrupt Source Report Page`_ and `Interrupt Status Report Page`_
 * to be used by the GuC when `Memory Based Interrupts`_ are required.
 *
 * Shall be called only on VF driver when `Memory Based Interrupts`_ are used
 * and xe_memirq_init() didn't fail.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_memirq_init_guc(struct xe_memirq *memirq, struct xe_guc *guc)
{
	bool is_media = xe_gt_is_media_type(guc_to_gt(guc));
	u32 offset = is_media ? ilog2(INTR_MGUC) : ilog2(INTR_GUC);
	u32 source, status;
	int err;

	memirq_assert(memirq, IS_SRIOV_VF(memirq_to_xe(memirq)));
	memirq_assert(memirq, xe_device_has_memirq(memirq_to_xe(memirq)));
	memirq_assert(memirq, memirq->bo);

	source = xe_memirq_source_ptr(memirq) + offset;
	status = xe_memirq_status_ptr(memirq) + offset * SZ_16;

	err = xe_guc_self_cfg64(guc, GUC_KLV_SELF_CFG_MEMIRQ_SOURCE_ADDR_KEY,
				source);
	if (unlikely(err))
		goto failed;

	err = xe_guc_self_cfg64(guc, GUC_KLV_SELF_CFG_MEMIRQ_STATUS_ADDR_KEY,
				status);
	if (unlikely(err))
		goto failed;

	return 0;

failed:
	xe_sriov_err(memirq_to_xe(memirq),
		     "Failed to setup report pages in %s (%pe)\n",
		     guc_name(guc), ERR_PTR(err));
	return err;
}

/**
 * xe_memirq_reset - Disable processing of `Memory Based Interrupts`_.
 * @memirq: struct xe_memirq
 *
 * This is part of the driver IRQ setup flow.
 *
 * This function shall only be used by the VF driver on platforms that use
 * `Memory Based Interrupts`_.
 */
void xe_memirq_reset(struct xe_memirq *memirq)
{
	memirq_assert(memirq, IS_SRIOV_VF(memirq_to_xe(memirq)));
	memirq_assert(memirq, xe_device_has_memirq(memirq_to_xe(memirq)));

	if (memirq->bo)
		memirq_set_enable(memirq, false);
}

/**
 * xe_memirq_postinstall - Enable processing of `Memory Based Interrupts`_.
 * @memirq: the &xe_memirq
 *
 * This is part of the driver IRQ setup flow.
 *
 * This function shall only be used by the VF driver on platforms that use
 * `Memory Based Interrupts`_.
 */
void xe_memirq_postinstall(struct xe_memirq *memirq)
{
	memirq_assert(memirq, IS_SRIOV_VF(memirq_to_xe(memirq)));
	memirq_assert(memirq, xe_device_has_memirq(memirq_to_xe(memirq)));

	if (memirq->bo)
		memirq_set_enable(memirq, true);
}

static bool memirq_received(struct xe_memirq *memirq, struct iosys_map *vector,
			    u16 offset, const char *name)
{
	u8 value;

	value = iosys_map_rd(vector, offset, u8);
	if (value) {
		if (value != 0xff)
			xe_sriov_err_ratelimited(memirq_to_xe(memirq),
						 "Unexpected memirq value %#x from %s at %u\n",
						 value, name, offset);
		iosys_map_wr(vector, offset, u8, 0x00);
	}

	return value;
}

static void memirq_dispatch_engine(struct xe_memirq *memirq, struct iosys_map *status,
				   struct xe_hw_engine *hwe)
{
	memirq_debug(memirq, "STATUS %s %*ph\n", hwe->name, 16, status->vaddr);

	if (memirq_received(memirq, status, ilog2(GT_RENDER_USER_INTERRUPT), hwe->name))
		xe_hw_engine_handle_irq(hwe, GT_RENDER_USER_INTERRUPT);
}

static void memirq_dispatch_guc(struct xe_memirq *memirq, struct iosys_map *status,
				struct xe_guc *guc)
{
	const char *name = guc_name(guc);

	memirq_debug(memirq, "STATUS %s %*ph\n", name, 16, status->vaddr);

	if (memirq_received(memirq, status, ilog2(GUC_INTR_GUC2HOST), name))
		xe_guc_irq_handler(guc, GUC_INTR_GUC2HOST);
}

/**
 * xe_memirq_handler - The `Memory Based Interrupts`_ Handler.
 * @memirq: the &xe_memirq
 *
 * This function reads and dispatches `Memory Based Interrupts`.
 */
void xe_memirq_handler(struct xe_memirq *memirq)
{
	struct xe_device *xe = memirq_to_xe(memirq);
	struct xe_tile *tile = memirq_to_tile(memirq);
	struct xe_hw_engine *hwe;
	enum xe_hw_engine_id id;
	struct iosys_map map;
	unsigned int gtid;
	struct xe_gt *gt;

	if (!memirq->bo)
		return;

	memirq_assert(memirq, !memirq->source.is_iomem);
	memirq_debug(memirq, "SOURCE %*ph\n", 32, memirq->source.vaddr);
	memirq_debug(memirq, "SOURCE %*ph\n", 32, memirq->source.vaddr + 32);

	for_each_gt(gt, xe, gtid) {
		if (gt->tile != tile)
			continue;

		for_each_hw_engine(hwe, gt, id) {
			if (memirq_received(memirq, &memirq->source, hwe->irq_offset, "SRC")) {
				map = IOSYS_MAP_INIT_OFFSET(&memirq->status,
							    hwe->irq_offset * SZ_16);
				memirq_dispatch_engine(memirq, &map, hwe);
			}
		}
	}

	/* GuC and media GuC (if present) must be checked separately */

	if (memirq_received(memirq, &memirq->source, ilog2(INTR_GUC), "SRC")) {
		map = IOSYS_MAP_INIT_OFFSET(&memirq->status, ilog2(INTR_GUC) * SZ_16);
		memirq_dispatch_guc(memirq, &map, &tile->primary_gt->uc.guc);
	}

	if (!tile->media_gt)
		return;

	if (memirq_received(memirq, &memirq->source, ilog2(INTR_MGUC), "SRC")) {
		map = IOSYS_MAP_INIT_OFFSET(&memirq->status, ilog2(INTR_MGUC) * SZ_16);
		memirq_dispatch_guc(memirq, &map, &tile->media_gt->uc.guc);
	}
}
