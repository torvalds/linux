// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021-2023 Intel Corporation
 * Copyright (C) 2021-2002 Red Hat
 */

#include <drm/drm_managed.h>
#include <drm/drm_mm.h>

#include <drm/ttm/ttm_device.h>
#include <drm/ttm/ttm_placement.h>
#include <drm/ttm/ttm_range_manager.h>

#include <generated/xe_wa_oob.h>

#include "regs/xe_gt_regs.h"
#include "regs/xe_regs.h"
#include "xe_bo.h"
#include "xe_device.h"
#include "xe_gt.h"
#include "xe_mmio.h"
#include "xe_res_cursor.h"
#include "xe_sriov.h"
#include "xe_ttm_stolen_mgr.h"
#include "xe_ttm_vram_mgr.h"
#include "xe_wa.h"

struct xe_ttm_stolen_mgr {
	struct xe_ttm_vram_mgr base;

	/* PCI base offset */
	resource_size_t io_base;
	/* GPU base offset */
	resource_size_t stolen_base;

	void __iomem *mapping;
};

static inline struct xe_ttm_stolen_mgr *
to_stolen_mgr(struct ttm_resource_manager *man)
{
	return container_of(man, struct xe_ttm_stolen_mgr, base.manager);
}

/**
 * xe_ttm_stolen_cpu_access_needs_ggtt() - If we can't directly CPU access
 * stolen, can we then fallback to mapping through the GGTT.
 * @xe: xe device
 *
 * Some older integrated platforms don't support reliable CPU access for stolen,
 * however on such hardware we can always use the mappable part of the GGTT for
 * CPU access. Check if that's the case for this device.
 */
bool xe_ttm_stolen_cpu_access_needs_ggtt(struct xe_device *xe)
{
	return GRAPHICS_VERx100(xe) < 1270 && !IS_DGFX(xe);
}

static s64 detect_bar2_dgfx(struct xe_device *xe, struct xe_ttm_stolen_mgr *mgr)
{
	struct xe_tile *tile = xe_device_get_root_tile(xe);
	struct xe_gt *mmio = xe_root_mmio_gt(xe);
	struct pci_dev *pdev = to_pci_dev(xe->drm.dev);
	u64 stolen_size;
	u64 tile_offset;
	u64 tile_size;

	tile_offset = tile->mem.vram.io_start - xe->mem.vram.io_start;
	tile_size = tile->mem.vram.actual_physical_size;

	/* Use DSM base address instead for stolen memory */
	mgr->stolen_base = (xe_mmio_read64_2x32(mmio, DSMBASE) & BDSM_MASK) - tile_offset;
	if (drm_WARN_ON(&xe->drm, tile_size < mgr->stolen_base))
		return 0;

	stolen_size = tile_size - mgr->stolen_base;

	/* Verify usage fits in the actual resource available */
	if (mgr->stolen_base + stolen_size <= pci_resource_len(pdev, LMEM_BAR))
		mgr->io_base = tile->mem.vram.io_start + mgr->stolen_base;

	/*
	 * There may be few KB of platform dependent reserved memory at the end
	 * of vram which is not part of the DSM. Such reserved memory portion is
	 * always less then DSM granularity so align down the stolen_size to DSM
	 * granularity to accommodate such reserve vram portion.
	 */
	return ALIGN_DOWN(stolen_size, SZ_1M);
}

static u32 get_wopcm_size(struct xe_device *xe)
{
	u32 wopcm_size;
	u64 val;

	val = xe_mmio_read64_2x32(xe_root_mmio_gt(xe), STOLEN_RESERVED);
	val = REG_FIELD_GET64(WOPCM_SIZE_MASK, val);

	switch (val) {
	case 0x5 ... 0x6:
		val--;
		fallthrough;
	case 0x0 ... 0x3:
		wopcm_size = (1U << val) * SZ_1M;
		break;
	default:
		WARN(1, "Missing case wopcm_size=%llx\n", val);
		wopcm_size = 0;
	}

	return wopcm_size;
}

static u32 detect_bar2_integrated(struct xe_device *xe, struct xe_ttm_stolen_mgr *mgr)
{
	struct pci_dev *pdev = to_pci_dev(xe->drm.dev);
	struct xe_gt *media_gt = xe_device_get_root_tile(xe)->media_gt;
	u32 stolen_size, wopcm_size;
	u32 ggc, gms;

	ggc = xe_mmio_read32(xe_root_mmio_gt(xe), GGC);

	/*
	 * Check GGMS: it should be fixed 0x3 (8MB), which corresponds to the
	 * GTT size
	 */
	if (drm_WARN_ON(&xe->drm, (ggc & GGMS_MASK) != GGMS_MASK))
		return 0;

	/*
	 * Graphics >= 1270 uses the offset to the GSMBASE as address in the
	 * PTEs, together with the DM flag being set. Previously there was no
	 * such flag so the address was the io_base.
	 *
	 * DSMBASE = GSMBASE + 8MB
	 */
	mgr->stolen_base = SZ_8M;
	mgr->io_base = pci_resource_start(pdev, 2) + mgr->stolen_base;

	/* return valid GMS value, -EIO if invalid */
	gms = REG_FIELD_GET(GMS_MASK, ggc);
	switch (gms) {
	case 0x0 ... 0x04:
		stolen_size = gms * 32 * SZ_1M;
		break;
	case 0xf0 ... 0xfe:
		stolen_size = (gms - 0xf0 + 1) * 4 * SZ_1M;
		break;
	default:
		return 0;
	}

	/* Carve out the top of DSM as it contains the reserved WOPCM region */
	wopcm_size = get_wopcm_size(xe);
	if (drm_WARN_ON(&xe->drm, !wopcm_size))
		return 0;

	stolen_size -= wopcm_size;

	if (media_gt && XE_WA(media_gt, 14019821291)) {
		u64 gscpsmi_base = xe_mmio_read64_2x32(media_gt, GSCPSMI_BASE)
			& ~GENMASK_ULL(5, 0);

		/*
		 * This workaround is primarily implemented by the BIOS.  We
		 * just need to figure out whether the BIOS has applied the
		 * workaround (meaning the programmed address falls within
		 * the DSM) and, if so, reserve that part of the DSM to
		 * prevent accidental reuse.  The DSM location should be just
		 * below the WOPCM.
		 */
		if (gscpsmi_base >= mgr->io_base &&
		    gscpsmi_base < mgr->io_base + stolen_size) {
			xe_gt_dbg(media_gt,
				  "Reserving %llu bytes of DSM for Wa_14019821291\n",
				  mgr->io_base + stolen_size - gscpsmi_base);
			stolen_size = gscpsmi_base - mgr->io_base;
		}
	}

	if (drm_WARN_ON(&xe->drm, stolen_size + SZ_8M > pci_resource_len(pdev, 2)))
		return 0;

	return stolen_size;
}

extern struct resource intel_graphics_stolen_res;

static u64 detect_stolen(struct xe_device *xe, struct xe_ttm_stolen_mgr *mgr)
{
#ifdef CONFIG_X86
	/* Map into GGTT */
	mgr->io_base = pci_resource_start(to_pci_dev(xe->drm.dev), 2);

	/* Stolen memory is x86 only */
	mgr->stolen_base = intel_graphics_stolen_res.start;
	return resource_size(&intel_graphics_stolen_res);
#else
	return 0;
#endif
}

void xe_ttm_stolen_mgr_init(struct xe_device *xe)
{
	struct xe_ttm_stolen_mgr *mgr = drmm_kzalloc(&xe->drm, sizeof(*mgr), GFP_KERNEL);
	struct pci_dev *pdev = to_pci_dev(xe->drm.dev);
	u64 stolen_size, io_size;
	int err;

	if (!mgr) {
		drm_dbg_kms(&xe->drm, "Stolen mgr init failed\n");
		return;
	}

	if (IS_SRIOV_VF(xe))
		stolen_size = 0;
	else if (IS_DGFX(xe))
		stolen_size = detect_bar2_dgfx(xe, mgr);
	else if (GRAPHICS_VERx100(xe) >= 1270)
		stolen_size = detect_bar2_integrated(xe, mgr);
	else
		stolen_size = detect_stolen(xe, mgr);

	if (!stolen_size) {
		drm_dbg_kms(&xe->drm, "No stolen memory support\n");
		return;
	}

	/*
	 * We don't try to attempt partial visible support for stolen vram,
	 * since stolen is always at the end of vram, and the BAR size is pretty
	 * much always 256M, with small-bar.
	 */
	io_size = 0;
	if (mgr->io_base && !xe_ttm_stolen_cpu_access_needs_ggtt(xe))
		io_size = stolen_size;

	err = __xe_ttm_vram_mgr_init(xe, &mgr->base, XE_PL_STOLEN, stolen_size,
				     io_size, PAGE_SIZE);
	if (err) {
		drm_dbg_kms(&xe->drm, "Stolen mgr init failed: %i\n", err);
		return;
	}

	drm_dbg_kms(&xe->drm, "Initialized stolen memory support with %llu bytes\n",
		    stolen_size);

	if (io_size)
		mgr->mapping = devm_ioremap_wc(&pdev->dev, mgr->io_base, io_size);
}

u64 xe_ttm_stolen_io_offset(struct xe_bo *bo, u32 offset)
{
	struct xe_device *xe = xe_bo_device(bo);
	struct ttm_resource_manager *ttm_mgr = ttm_manager_type(&xe->ttm, XE_PL_STOLEN);
	struct xe_ttm_stolen_mgr *mgr = to_stolen_mgr(ttm_mgr);
	struct xe_res_cursor cur;

	XE_WARN_ON(!mgr->io_base);

	if (xe_ttm_stolen_cpu_access_needs_ggtt(xe))
		return mgr->io_base + xe_bo_ggtt_addr(bo) + offset;

	xe_res_first(bo->ttm.resource, offset, 4096, &cur);
	return mgr->io_base + cur.start;
}

static int __xe_ttm_stolen_io_mem_reserve_bar2(struct xe_device *xe,
					       struct xe_ttm_stolen_mgr *mgr,
					       struct ttm_resource *mem)
{
	struct xe_res_cursor cur;

	if (!mgr->io_base)
		return -EIO;

	xe_res_first(mem, 0, 4096, &cur);
	mem->bus.offset = cur.start;

	drm_WARN_ON(&xe->drm, !(mem->placement & TTM_PL_FLAG_CONTIGUOUS));

	if (mem->placement & TTM_PL_FLAG_CONTIGUOUS && mgr->mapping)
		mem->bus.addr = (u8 __force *)mgr->mapping + mem->bus.offset;

	mem->bus.offset += mgr->io_base;
	mem->bus.is_iomem = true;
	mem->bus.caching = ttm_write_combined;

	return 0;
}

static int __xe_ttm_stolen_io_mem_reserve_stolen(struct xe_device *xe,
						 struct xe_ttm_stolen_mgr *mgr,
						 struct ttm_resource *mem)
{
#ifdef CONFIG_X86
	struct xe_bo *bo = ttm_to_xe_bo(mem->bo);

	XE_WARN_ON(IS_DGFX(xe));

	/* XXX: Require BO to be mapped to GGTT? */
	if (drm_WARN_ON(&xe->drm, !(bo->flags & XE_BO_FLAG_GGTT)))
		return -EIO;

	/* GGTT is always contiguously mapped */
	mem->bus.offset = xe_bo_ggtt_addr(bo) + mgr->io_base;

	mem->bus.is_iomem = true;
	mem->bus.caching = ttm_write_combined;

	return 0;
#else
	/* How is it even possible to get here without gen12 stolen? */
	drm_WARN_ON(&xe->drm, 1);
	return -EIO;
#endif
}

int xe_ttm_stolen_io_mem_reserve(struct xe_device *xe, struct ttm_resource *mem)
{
	struct ttm_resource_manager *ttm_mgr = ttm_manager_type(&xe->ttm, XE_PL_STOLEN);
	struct xe_ttm_stolen_mgr *mgr = ttm_mgr ? to_stolen_mgr(ttm_mgr) : NULL;

	if (!mgr || !mgr->io_base)
		return -EIO;

	if (xe_ttm_stolen_cpu_access_needs_ggtt(xe))
		return __xe_ttm_stolen_io_mem_reserve_stolen(xe, mgr, mem);
	else
		return __xe_ttm_stolen_io_mem_reserve_bar2(xe, mgr, mem);
}

u64 xe_ttm_stolen_gpu_offset(struct xe_device *xe)
{
	struct xe_ttm_stolen_mgr *mgr =
		to_stolen_mgr(ttm_manager_type(&xe->ttm, XE_PL_STOLEN));

	return mgr->stolen_base;
}
