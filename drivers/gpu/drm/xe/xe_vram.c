// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021-2024 Intel Corporation
 */

#include <kunit/visibility.h>
#include <linux/pci.h>

#include <drm/drm_managed.h>
#include <drm/drm_print.h>

#include "regs/xe_bars.h"
#include "regs/xe_gt_regs.h"
#include "regs/xe_regs.h"
#include "xe_assert.h"
#include "xe_device.h"
#include "xe_force_wake.h"
#include "xe_gt_mcr.h"
#include "xe_gt_sriov_vf.h"
#include "xe_mmio.h"
#include "xe_module.h"
#include "xe_sriov.h"
#include "xe_ttm_vram_mgr.h"
#include "xe_vram.h"
#include "xe_vram_types.h"

#define BAR_SIZE_SHIFT 20

/*
 * Release all the BARs that could influence/block LMEMBAR resizing, i.e.
 * assigned IORESOURCE_MEM_64 BARs
 */
static void release_bars(struct pci_dev *pdev)
{
	struct resource *res;
	int i;

	pci_dev_for_each_resource(pdev, res, i) {
		/* Resource already un-assigned, do not reset it */
		if (!res->parent)
			continue;

		/* No need to release unrelated BARs */
		if (!(res->flags & IORESOURCE_MEM_64))
			continue;

		pci_release_resource(pdev, i);
	}
}

static void resize_bar(struct xe_device *xe, int resno, resource_size_t size)
{
	struct pci_dev *pdev = to_pci_dev(xe->drm.dev);
	int bar_size = pci_rebar_bytes_to_size(size);
	int ret;

	release_bars(pdev);

	ret = pci_resize_resource(pdev, resno, bar_size);
	if (ret) {
		drm_info(&xe->drm, "Failed to resize BAR%d to %dM (%pe). Consider enabling 'Resizable BAR' support in your BIOS\n",
			 resno, 1 << bar_size, ERR_PTR(ret));
		return;
	}

	drm_info(&xe->drm, "BAR%d resized to %dM\n", resno, 1 << bar_size);
}

/*
 * if force_vram_bar_size is set, attempt to set to the requested size
 * else set to maximum possible size
 */
void xe_vram_resize_bar(struct xe_device *xe)
{
	int force_vram_bar_size = xe_modparam.force_vram_bar_size;
	struct pci_dev *pdev = to_pci_dev(xe->drm.dev);
	struct pci_bus *root = pdev->bus;
	resource_size_t current_size;
	resource_size_t rebar_size;
	struct resource *root_res;
	u32 bar_size_mask;
	u32 pci_cmd;
	int i;

	/* gather some relevant info */
	current_size = pci_resource_len(pdev, LMEM_BAR);
	bar_size_mask = pci_rebar_get_possible_sizes(pdev, LMEM_BAR);

	if (!bar_size_mask)
		return;

	if (force_vram_bar_size < 0)
		return;

	/* set to a specific size? */
	if (force_vram_bar_size) {
		u32 bar_size_bit;

		rebar_size = force_vram_bar_size * (resource_size_t)SZ_1M;

		bar_size_bit = bar_size_mask & BIT(pci_rebar_bytes_to_size(rebar_size));

		if (!bar_size_bit) {
			drm_info(&xe->drm,
				 "Requested size: %lluMiB is not supported by rebar sizes: 0x%x. Leaving default: %lluMiB\n",
				 (u64)rebar_size >> 20, bar_size_mask, (u64)current_size >> 20);
			return;
		}

		rebar_size = 1ULL << (__fls(bar_size_bit) + BAR_SIZE_SHIFT);

		if (rebar_size == current_size)
			return;
	} else {
		rebar_size = 1ULL << (__fls(bar_size_mask) + BAR_SIZE_SHIFT);

		/* only resize if larger than current */
		if (rebar_size <= current_size)
			return;
	}

	drm_info(&xe->drm, "Attempting to resize bar from %lluMiB -> %lluMiB\n",
		 (u64)current_size >> 20, (u64)rebar_size >> 20);

	while (root->parent)
		root = root->parent;

	pci_bus_for_each_resource(root, root_res, i) {
		if (root_res && root_res->flags & (IORESOURCE_MEM | IORESOURCE_MEM_64) &&
		    (u64)root_res->start > 0x100000000ul)
			break;
	}

	if (!root_res) {
		drm_info(&xe->drm, "Can't resize VRAM BAR - platform support is missing. Consider enabling 'Resizable BAR' support in your BIOS\n");
		return;
	}

	pci_read_config_dword(pdev, PCI_COMMAND, &pci_cmd);
	pci_write_config_dword(pdev, PCI_COMMAND, pci_cmd & ~PCI_COMMAND_MEMORY);

	resize_bar(xe, LMEM_BAR, rebar_size);

	pci_assign_unassigned_bus_resources(pdev->bus);
	pci_write_config_dword(pdev, PCI_COMMAND, pci_cmd);
}

static bool resource_is_valid(struct pci_dev *pdev, int bar)
{
	if (!pci_resource_flags(pdev, bar))
		return false;

	if (pci_resource_flags(pdev, bar) & IORESOURCE_UNSET)
		return false;

	if (!pci_resource_len(pdev, bar))
		return false;

	return true;
}

static int determine_lmem_bar_size(struct xe_device *xe, struct xe_vram_region *lmem_bar)
{
	struct pci_dev *pdev = to_pci_dev(xe->drm.dev);

	if (!resource_is_valid(pdev, LMEM_BAR)) {
		drm_err(&xe->drm, "pci resource is not valid\n");
		return -ENXIO;
	}

	lmem_bar->io_start = pci_resource_start(pdev, LMEM_BAR);
	lmem_bar->io_size = pci_resource_len(pdev, LMEM_BAR);
	if (!lmem_bar->io_size)
		return -EIO;

	/* XXX: Need to change when xe link code is ready */
	lmem_bar->dpa_base = 0;

	/* set up a map to the total memory area. */
	lmem_bar->mapping = devm_ioremap_wc(&pdev->dev, lmem_bar->io_start, lmem_bar->io_size);

	return 0;
}

static inline u64 get_flat_ccs_offset(struct xe_gt *gt, u64 tile_size)
{
	struct xe_device *xe = gt_to_xe(gt);
	u64 offset;
	u32 reg;

	if (GRAPHICS_VER(xe) >= 20) {
		u64 ccs_size = tile_size / 512;
		u64 offset_hi, offset_lo;
		u32 nodes, num_enabled;

		reg = xe_mmio_read32(&gt->mmio, MIRROR_FUSE3);
		nodes = REG_FIELD_GET(XE2_NODE_ENABLE_MASK, reg);
		num_enabled = hweight32(nodes); /* Number of enabled l3 nodes */

		reg = xe_gt_mcr_unicast_read_any(gt, XE2_FLAT_CCS_BASE_RANGE_LOWER);
		offset_lo = REG_FIELD_GET(XE2_FLAT_CCS_BASE_LOWER_ADDR_MASK, reg);

		reg = xe_gt_mcr_unicast_read_any(gt, XE2_FLAT_CCS_BASE_RANGE_UPPER);
		offset_hi = REG_FIELD_GET(XE2_FLAT_CCS_BASE_UPPER_ADDR_MASK, reg);

		offset = offset_hi << 32; /* HW view bits 39:32 */
		offset |= offset_lo << 6; /* HW view bits 31:6 */
		offset *= num_enabled; /* convert to SW view */
		offset = round_up(offset, SZ_128K); /* SW must round up to nearest 128K */

		/* We don't expect any holes */
		xe_assert_msg(xe, offset == (xe_mmio_read64_2x32(&gt_to_tile(gt)->mmio, GSMBASE) -
					     ccs_size),
			      "Hole between CCS and GSM.\n");
	} else {
		reg = xe_gt_mcr_unicast_read_any(gt, XEHP_FLAT_CCS_BASE_ADDR);
		offset = (u64)REG_FIELD_GET(XEHP_FLAT_CCS_PTR, reg) * SZ_64K;
	}

	return offset;
}

/*
 * tile_vram_size() - Collect vram size and offset information
 * @tile: tile to get info for
 * @vram_size: available vram (size - device reserved portions)
 * @tile_size: actual vram size
 * @tile_offset: physical start point in the vram address space
 *
 * There are 4 places for size information:
 * - io size (from pci_resource_len of LMEM bar) (only used for small bar and DG1)
 * - TILEx size (actual vram size)
 * - GSMBASE offset (TILEx - "stolen")
 * - CSSBASE offset (TILEx - CSS space necessary)
 *
 * CSSBASE is always a lower/smaller offset then GSMBASE.
 *
 * The actual available size of memory is to the CCS or GSM base.
 * NOTE: multi-tile bases will include the tile offset.
 *
 */
static int tile_vram_size(struct xe_tile *tile, u64 *vram_size,
			  u64 *tile_size, u64 *tile_offset)
{
	struct xe_device *xe = tile_to_xe(tile);
	struct xe_gt *gt = tile->primary_gt;
	unsigned int fw_ref;
	u64 offset;
	u32 reg;

	if (IS_SRIOV_VF(xe)) {
		struct xe_tile *t;
		int id;

		offset = 0;
		for_each_tile(t, xe, id)
			for_each_if(t->id < tile->id)
				offset += xe_gt_sriov_vf_lmem(t->primary_gt);

		*tile_size = xe_gt_sriov_vf_lmem(gt);
		*vram_size = *tile_size;
		*tile_offset = offset;

		return 0;
	}

	fw_ref = xe_force_wake_get(gt_to_fw(gt), XE_FW_GT);
	if (!fw_ref)
		return -ETIMEDOUT;

	/* actual size */
	if (unlikely(xe->info.platform == XE_DG1)) {
		*tile_size = pci_resource_len(to_pci_dev(xe->drm.dev), LMEM_BAR);
		*tile_offset = 0;
	} else {
		reg = xe_gt_mcr_unicast_read_any(gt, XEHP_TILE_ADDR_RANGE(gt->info.id));
		*tile_size = (u64)REG_FIELD_GET(GENMASK(14, 8), reg) * SZ_1G;
		*tile_offset = (u64)REG_FIELD_GET(GENMASK(7, 1), reg) * SZ_1G;
	}

	/* minus device usage */
	if (xe->info.has_flat_ccs) {
		offset = get_flat_ccs_offset(gt, *tile_size);
	} else {
		offset = xe_mmio_read64_2x32(&tile->mmio, GSMBASE);
	}

	/* remove the tile offset so we have just the available size */
	*vram_size = offset - *tile_offset;

	xe_force_wake_put(gt_to_fw(gt), fw_ref);

	return 0;
}

static void vram_fini(void *arg)
{
	struct xe_device *xe = arg;
	struct xe_tile *tile;
	int id;

	xe->mem.vram->mapping = NULL;

	for_each_tile(tile, xe, id)
		tile->mem.vram->mapping = NULL;
}

struct xe_vram_region *xe_vram_region_alloc(struct xe_device *xe, u8 id, u32 placement)
{
	struct xe_vram_region *vram;
	struct drm_device *drm = &xe->drm;

	xe_assert(xe, id < xe->info.tile_count);

	vram = drmm_kzalloc(drm, sizeof(*vram), GFP_KERNEL);
	if (!vram)
		return NULL;

	vram->xe = xe;
	vram->id = id;
	vram->placement = placement;
#if defined(CONFIG_DRM_XE_PAGEMAP)
	vram->migrate = xe->tiles[id].migrate;
#endif
	return vram;
}

static void print_vram_region_info(struct xe_device *xe, struct xe_vram_region *vram)
{
	struct drm_device *drm = &xe->drm;

	if (vram->io_size < vram->usable_size)
		drm_info(drm, "Small BAR device\n");

	drm_info(drm,
		 "VRAM[%u]: Actual physical size %pa, usable size exclude stolen %pa, CPU accessible size %pa\n",
		 vram->id, &vram->actual_physical_size, &vram->usable_size, &vram->io_size);
	drm_info(drm, "VRAM[%u]: DPA range: [%pa-%llx], io range: [%pa-%llx]\n",
		 vram->id, &vram->dpa_base, vram->dpa_base + (u64)vram->actual_physical_size,
		 &vram->io_start, vram->io_start + (u64)vram->io_size);
}

static int vram_region_init(struct xe_device *xe, struct xe_vram_region *vram,
			    struct xe_vram_region *lmem_bar, u64 offset, u64 usable_size,
			    u64 region_size, resource_size_t remain_io_size)
{
	/* Check if VRAM region is already initialized */
	if (vram->mapping)
		return 0;

	vram->actual_physical_size = region_size;
	vram->io_start = lmem_bar->io_start + offset;
	vram->io_size = min_t(u64, usable_size, remain_io_size);

	if (!vram->io_size) {
		drm_err(&xe->drm, "Tile without any CPU visible VRAM. Aborting.\n");
		return -ENODEV;
	}

	vram->dpa_base = lmem_bar->dpa_base + offset;
	vram->mapping = lmem_bar->mapping + offset;
	vram->usable_size = usable_size;

	print_vram_region_info(xe, vram);

	return 0;
}

/**
 * xe_vram_probe() - Probe VRAM configuration
 * @xe: the &xe_device
 *
 * Collect VRAM size and offset information for all tiles.
 *
 * Return: 0 on success, error code on failure
 */
int xe_vram_probe(struct xe_device *xe)
{
	struct xe_tile *tile;
	struct xe_vram_region lmem_bar;
	resource_size_t remain_io_size;
	u64 available_size = 0;
	u64 total_size = 0;
	int err;
	u8 id;

	if (!IS_DGFX(xe))
		return 0;

	err = determine_lmem_bar_size(xe, &lmem_bar);
	if (err)
		return err;
	drm_info(&xe->drm, "VISIBLE VRAM: %pa, %pa\n", &lmem_bar.io_start, &lmem_bar.io_size);

	remain_io_size = lmem_bar.io_size;

	for_each_tile(tile, xe, id) {
		u64 region_size;
		u64 usable_size;
		u64 tile_offset;

		err = tile_vram_size(tile, &usable_size, &region_size, &tile_offset);
		if (err)
			return err;

		total_size += region_size;
		available_size += usable_size;

		err = vram_region_init(xe, tile->mem.vram, &lmem_bar, tile_offset, usable_size,
				       region_size, remain_io_size);
		if (err)
			return err;

		if (total_size > lmem_bar.io_size) {
			drm_info(&xe->drm, "VRAM: %pa is larger than resource %pa\n",
				 &total_size, &lmem_bar.io_size);
		}

		remain_io_size -= min_t(u64, tile->mem.vram->actual_physical_size, remain_io_size);
	}

	err = vram_region_init(xe, xe->mem.vram, &lmem_bar, 0, available_size, total_size,
			       lmem_bar.io_size);
	if (err)
		return err;

	return devm_add_action_or_reset(xe->drm.dev, vram_fini, xe);
}

/**
 * xe_vram_region_io_start - Get the IO start of a VRAM region
 * @vram: the VRAM region
 *
 * Return: the IO start of the VRAM region, or 0 if not valid
 */
resource_size_t xe_vram_region_io_start(const struct xe_vram_region *vram)
{
	return vram ? vram->io_start : 0;
}

/**
 * xe_vram_region_io_size - Get the IO size of a VRAM region
 * @vram: the VRAM region
 *
 * Return: the IO size of the VRAM region, or 0 if not valid
 */
resource_size_t xe_vram_region_io_size(const struct xe_vram_region *vram)
{
	return vram ? vram->io_size : 0;
}

/**
 * xe_vram_region_dpa_base - Get the DPA base of a VRAM region
 * @vram: the VRAM region
 *
 * Return: the DPA base of the VRAM region, or 0 if not valid
 */
resource_size_t xe_vram_region_dpa_base(const struct xe_vram_region *vram)
{
	return vram ? vram->dpa_base : 0;
}

/**
 * xe_vram_region_usable_size - Get the usable size of a VRAM region
 * @vram: the VRAM region
 *
 * Return: the usable size of the VRAM region, or 0 if not valid
 */
resource_size_t xe_vram_region_usable_size(const struct xe_vram_region *vram)
{
	return vram ? vram->usable_size : 0;
}

/**
 * xe_vram_region_actual_physical_size - Get the actual physical size of a VRAM region
 * @vram: the VRAM region
 *
 * Return: the actual physical size of the VRAM region, or 0 if not valid
 */
resource_size_t xe_vram_region_actual_physical_size(const struct xe_vram_region *vram)
{
	return vram ? vram->actual_physical_size : 0;
}
EXPORT_SYMBOL_IF_KUNIT(xe_vram_region_actual_physical_size);
