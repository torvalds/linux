// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include <drm/drm_managed.h>

#include "regs/xe_guc_regs.h"
#include "regs/xe_irq_regs.h"
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

#define memirq_assert(m, condition)	xe_tile_assert(memirq_to_tile(m), condition)
#define memirq_printk(m, _level, _fmt, ...)			\
	drm_##_level(&memirq_to_xe(m)->drm, "MEMIRQ%u: " _fmt,	\
		     memirq_to_tile(m)->id, ##__VA_ARGS__)

#ifdef CONFIG_DRM_XE_DEBUG_MEMIRQ
#define memirq_debug(m, _fmt, ...)	memirq_printk(m, dbg, _fmt, ##__VA_ARGS__)
#else
#define memirq_debug(...)
#endif

#define memirq_err(m, _fmt, ...)	memirq_printk(m, err, _fmt, ##__VA_ARGS__)
#define memirq_err_ratelimited(m, _fmt, ...)	\
	memirq_printk(m, err_ratelimited, _fmt, ##__VA_ARGS__)

static struct xe_tile *memirq_to_tile(struct xe_memirq *memirq)
{
	return container_of(memirq, struct xe_tile, memirq);
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
 *
 *
 * MSI-X use case
 *
 * When using MSI-X, hw engines report interrupt status and source to engine
 * instance 0. For this scenario, in order to differentiate between the
 * engines, we need to pass different status/source pointers in the LRC.
 *
 * The requirements on those pointers are:
 * - Interrupt status should be 4KiB aligned
 * - Interrupt source should be 64 bytes aligned
 *
 * To accommodate this, we duplicate the memirq page layout above -
 * allocating a page for each engine instance and pass this page in the LRC.
 * Note that the same page can be reused for different engine types.
 * For example, an LRC executing on CCS #x will have pointers to page #x,
 * and an LRC executing on BCS #x will have the same pointers.
 *
 * ::
 *
 *   0x0000   +==============================+  <== page for instance 0 (BCS0, CCS0, etc.)
 *            | Interrupt Status Report Page |
 *   0x0400   +==============================+
 *            | Interrupt Source Report Page |
 *   0x0440   +==============================+
 *            | Interrupt Enable Mask        |
 *            +==============================+
 *            | Not used                     |
 *   0x1000   +==============================+  <== page for instance 1 (BCS1, CCS1, etc.)
 *            | Interrupt Status Report Page |
 *   0x1400   +==============================+
 *            | Interrupt Source Report Page |
 *   0x1440   +==============================+
 *            | Not used                     |
 *   0x2000   +==============================+  <== page for instance 2 (BCS2, CCS2, etc.)
 *            | ...                          |
 *            +==============================+
 *
 */

static inline bool hw_reports_to_instance_zero(struct xe_memirq *memirq)
{
	/*
	 * When the HW engines are configured to use MSI-X,
	 * they report interrupt status and source to the offset of
	 * engine instance 0.
	 */
	return xe_device_has_msix(memirq_to_xe(memirq));
}

static int memirq_alloc_pages(struct xe_memirq *memirq)
{
	struct xe_device *xe = memirq_to_xe(memirq);
	struct xe_tile *tile = memirq_to_tile(memirq);
	size_t bo_size = hw_reports_to_instance_zero(memirq) ?
		XE_HW_ENGINE_MAX_INSTANCE * SZ_4K : SZ_4K;
	struct xe_bo *bo;
	int err;

	BUILD_BUG_ON(!IS_ALIGNED(XE_MEMIRQ_SOURCE_OFFSET(0), SZ_64));
	BUILD_BUG_ON(!IS_ALIGNED(XE_MEMIRQ_STATUS_OFFSET(0), SZ_4K));

	bo = xe_managed_bo_create_pin_map(xe, tile, bo_size,
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

	iosys_map_memset(&bo->vmap, 0, 0, bo_size);

	memirq->bo = bo;
	memirq->source = IOSYS_MAP_INIT_OFFSET(&bo->vmap, XE_MEMIRQ_SOURCE_OFFSET(0));
	memirq->status = IOSYS_MAP_INIT_OFFSET(&bo->vmap, XE_MEMIRQ_STATUS_OFFSET(0));
	memirq->mask = IOSYS_MAP_INIT_OFFSET(&bo->vmap, XE_MEMIRQ_ENABLE_OFFSET);

	memirq_assert(memirq, !memirq->source.is_iomem);
	memirq_assert(memirq, !memirq->status.is_iomem);
	memirq_assert(memirq, !memirq->mask.is_iomem);

	memirq_debug(memirq, "page offsets: bo %#x bo_size %zu source %#x status %#x\n",
		     xe_bo_ggtt_addr(bo), bo_size, XE_MEMIRQ_SOURCE_OFFSET(0),
		     XE_MEMIRQ_STATUS_OFFSET(0));

	return 0;

out:
	memirq_err(memirq, "Failed to allocate memirq page (%pe)\n", ERR_PTR(err));
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
 * If this function fails then the driver won't be able to operate correctly.
 * If `Memory Based Interrupts`_ are not used this function will return 0.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_memirq_init(struct xe_memirq *memirq)
{
	struct xe_device *xe = memirq_to_xe(memirq);
	int err;

	if (!xe_device_uses_memirq(xe))
		return 0;

	err = memirq_alloc_pages(memirq);
	if (unlikely(err))
		return err;

	/* we need to start with all irqs enabled */
	memirq_set_enable(memirq, true);

	return 0;
}

static u32 __memirq_source_page(struct xe_memirq *memirq, u16 instance)
{
	memirq_assert(memirq, instance <= XE_HW_ENGINE_MAX_INSTANCE);
	memirq_assert(memirq, memirq->bo);

	instance = hw_reports_to_instance_zero(memirq) ? instance : 0;
	return xe_bo_ggtt_addr(memirq->bo) + XE_MEMIRQ_SOURCE_OFFSET(instance);
}

/**
 * xe_memirq_source_ptr - Get GGTT's offset of the `Interrupt Source Report Page`_.
 * @memirq: the &xe_memirq to query
 * @hwe: the hw engine for which we want the report page
 *
 * Shall be called when `Memory Based Interrupts`_ are used
 * and xe_memirq_init() didn't fail.
 *
 * Return: GGTT's offset of the `Interrupt Source Report Page`_.
 */
u32 xe_memirq_source_ptr(struct xe_memirq *memirq, struct xe_hw_engine *hwe)
{
	memirq_assert(memirq, xe_device_uses_memirq(memirq_to_xe(memirq)));

	return __memirq_source_page(memirq, hwe->instance);
}

static u32 __memirq_status_page(struct xe_memirq *memirq, u16 instance)
{
	memirq_assert(memirq, instance <= XE_HW_ENGINE_MAX_INSTANCE);
	memirq_assert(memirq, memirq->bo);

	instance = hw_reports_to_instance_zero(memirq) ? instance : 0;
	return xe_bo_ggtt_addr(memirq->bo) + XE_MEMIRQ_STATUS_OFFSET(instance);
}

/**
 * xe_memirq_status_ptr - Get GGTT's offset of the `Interrupt Status Report Page`_.
 * @memirq: the &xe_memirq to query
 * @hwe: the hw engine for which we want the report page
 *
 * Shall be called when `Memory Based Interrupts`_ are used
 * and xe_memirq_init() didn't fail.
 *
 * Return: GGTT's offset of the `Interrupt Status Report Page`_.
 */
u32 xe_memirq_status_ptr(struct xe_memirq *memirq, struct xe_hw_engine *hwe)
{
	memirq_assert(memirq, xe_device_uses_memirq(memirq_to_xe(memirq)));

	return __memirq_status_page(memirq, hwe->instance);
}

/**
 * xe_memirq_enable_ptr - Get GGTT's offset of the Interrupt Enable Mask.
 * @memirq: the &xe_memirq to query
 *
 * Shall be called when `Memory Based Interrupts`_ are used
 * and xe_memirq_init() didn't fail.
 *
 * Return: GGTT's offset of the Interrupt Enable Mask.
 */
u32 xe_memirq_enable_ptr(struct xe_memirq *memirq)
{
	memirq_assert(memirq, xe_device_uses_memirq(memirq_to_xe(memirq)));
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
 * Shall be called when `Memory Based Interrupts`_ are used
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

	memirq_assert(memirq, xe_device_uses_memirq(memirq_to_xe(memirq)));

	source = __memirq_source_page(memirq, 0) + offset;
	status = __memirq_status_page(memirq, 0) + offset * SZ_16;

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
	memirq_err(memirq, "Failed to setup report pages in %s (%pe)\n",
		   guc_name(guc), ERR_PTR(err));
	return err;
}

/**
 * xe_memirq_reset - Disable processing of `Memory Based Interrupts`_.
 * @memirq: struct xe_memirq
 *
 * This is part of the driver IRQ setup flow.
 *
 * This function shall only be used on platforms that use
 * `Memory Based Interrupts`_.
 */
void xe_memirq_reset(struct xe_memirq *memirq)
{
	memirq_assert(memirq, xe_device_uses_memirq(memirq_to_xe(memirq)));

	if (memirq->bo)
		memirq_set_enable(memirq, false);
}

/**
 * xe_memirq_postinstall - Enable processing of `Memory Based Interrupts`_.
 * @memirq: the &xe_memirq
 *
 * This is part of the driver IRQ setup flow.
 *
 * This function shall only be used on platforms that use
 * `Memory Based Interrupts`_.
 */
void xe_memirq_postinstall(struct xe_memirq *memirq)
{
	memirq_assert(memirq, xe_device_uses_memirq(memirq_to_xe(memirq)));

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
			memirq_err_ratelimited(memirq,
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

	if (memirq_received(memirq, status, ilog2(GUC_INTR_SW_INT_0), name))
		xe_guc_irq_handler(guc, GUC_INTR_SW_INT_0);
}

/**
 * xe_memirq_hwe_handler - Check and process interrupts for a specific HW engine.
 * @memirq: the &xe_memirq
 * @hwe: the hw engine to process
 *
 * This function reads and dispatches `Memory Based Interrupts` for the provided HW engine.
 */
void xe_memirq_hwe_handler(struct xe_memirq *memirq, struct xe_hw_engine *hwe)
{
	u16 offset = hwe->irq_offset;
	u16 instance = hw_reports_to_instance_zero(memirq) ? hwe->instance : 0;
	struct iosys_map src_offset = IOSYS_MAP_INIT_OFFSET(&memirq->bo->vmap,
							    XE_MEMIRQ_SOURCE_OFFSET(instance));

	if (memirq_received(memirq, &src_offset, offset, "SRC")) {
		struct iosys_map status_offset =
			IOSYS_MAP_INIT_OFFSET(&memirq->bo->vmap,
					      XE_MEMIRQ_STATUS_OFFSET(instance) + offset * SZ_16);
		memirq_dispatch_engine(memirq, &status_offset, hwe);
	}
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

		for_each_hw_engine(hwe, gt, id)
			xe_memirq_hwe_handler(memirq, hwe);
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
