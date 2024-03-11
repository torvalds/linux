// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021-2023 Intel Corporation
 */

#include <linux/minmax.h>

#include "xe_mmio.h"

#include <drm/drm_managed.h>
#include <drm/xe_drm.h>

#include "regs/xe_engine_regs.h"
#include "regs/xe_gt_regs.h"
#include "regs/xe_regs.h"
#include "xe_bo.h"
#include "xe_device.h"
#include "xe_ggtt.h"
#include "xe_gt.h"
#include "xe_gt_mcr.h"
#include "xe_macros.h"
#include "xe_module.h"
#include "xe_tile.h"

#define XEHP_MTCFG_ADDR		XE_REG(0x101800)
#define TILE_COUNT		REG_GENMASK(15, 8)

#define BAR_SIZE_SHIFT 20

static void
_resize_bar(struct xe_device *xe, int resno, resource_size_t size)
{
	struct pci_dev *pdev = to_pci_dev(xe->drm.dev);
	int bar_size = pci_rebar_bytes_to_size(size);
	int ret;

	if (pci_resource_len(pdev, resno))
		pci_release_resource(pdev, resno);

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
static void xe_resize_vram_bar(struct xe_device *xe)
{
	u64 force_vram_bar_size = xe_modparam.force_vram_bar_size;
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

	_resize_bar(xe, LMEM_BAR, rebar_size);

	pci_assign_unassigned_bus_resources(pdev->bus);
	pci_write_config_dword(pdev, PCI_COMMAND, pci_cmd);
}

static bool xe_pci_resource_valid(struct pci_dev *pdev, int bar)
{
	if (!pci_resource_flags(pdev, bar))
		return false;

	if (pci_resource_flags(pdev, bar) & IORESOURCE_UNSET)
		return false;

	if (!pci_resource_len(pdev, bar))
		return false;

	return true;
}

static int xe_determine_lmem_bar_size(struct xe_device *xe)
{
	struct pci_dev *pdev = to_pci_dev(xe->drm.dev);

	if (!xe_pci_resource_valid(pdev, LMEM_BAR)) {
		drm_err(&xe->drm, "pci resource is not valid\n");
		return -ENXIO;
	}

	xe_resize_vram_bar(xe);

	xe->mem.vram.io_start = pci_resource_start(pdev, LMEM_BAR);
	xe->mem.vram.io_size = pci_resource_len(pdev, LMEM_BAR);
	if (!xe->mem.vram.io_size)
		return -EIO;

	/* XXX: Need to change when xe link code is ready */
	xe->mem.vram.dpa_base = 0;

	/* set up a map to the total memory area. */
	xe->mem.vram.mapping = ioremap_wc(xe->mem.vram.io_start, xe->mem.vram.io_size);

	return 0;
}

/**
 * xe_mmio_tile_vram_size() - Collect vram size and offset information
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
static int xe_mmio_tile_vram_size(struct xe_tile *tile, u64 *vram_size,
				  u64 *tile_size, u64 *tile_offset)
{
	struct xe_device *xe = tile_to_xe(tile);
	struct xe_gt *gt = tile->primary_gt;
	u64 offset;
	int err;
	u32 reg;

	err = xe_force_wake_get(gt_to_fw(gt), XE_FW_GT);
	if (err)
		return err;

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
		reg = xe_gt_mcr_unicast_read_any(gt, XEHP_FLAT_CCS_BASE_ADDR);
		offset = (u64)REG_FIELD_GET(GENMASK(31, 8), reg) * SZ_64K;
	} else {
		offset = xe_mmio_read64_2x32(gt, GSMBASE);
	}

	/* remove the tile offset so we have just the available size */
	*vram_size = offset - *tile_offset;

	return xe_force_wake_put(gt_to_fw(gt), XE_FW_GT);
}

int xe_mmio_probe_vram(struct xe_device *xe)
{
	struct xe_tile *tile;
	resource_size_t io_size;
	u64 available_size = 0;
	u64 total_size = 0;
	u64 tile_offset;
	u64 tile_size;
	u64 vram_size;
	int err;
	u8 id;

	if (!IS_DGFX(xe))
		return 0;

	/* Get the size of the root tile's vram for later accessibility comparison */
	tile = xe_device_get_root_tile(xe);
	err = xe_mmio_tile_vram_size(tile, &vram_size, &tile_size, &tile_offset);
	if (err)
		return err;

	err = xe_determine_lmem_bar_size(xe);
	if (err)
		return err;

	drm_info(&xe->drm, "VISIBLE VRAM: %pa, %pa\n", &xe->mem.vram.io_start,
		 &xe->mem.vram.io_size);

	io_size = xe->mem.vram.io_size;

	/* tile specific ranges */
	for_each_tile(tile, xe, id) {
		err = xe_mmio_tile_vram_size(tile, &vram_size, &tile_size, &tile_offset);
		if (err)
			return err;

		tile->mem.vram.actual_physical_size = tile_size;
		tile->mem.vram.io_start = xe->mem.vram.io_start + tile_offset;
		tile->mem.vram.io_size = min_t(u64, vram_size, io_size);

		if (!tile->mem.vram.io_size) {
			drm_err(&xe->drm, "Tile without any CPU visible VRAM. Aborting.\n");
			return -ENODEV;
		}

		tile->mem.vram.dpa_base = xe->mem.vram.dpa_base + tile_offset;
		tile->mem.vram.usable_size = vram_size;
		tile->mem.vram.mapping = xe->mem.vram.mapping + tile_offset;

		if (tile->mem.vram.io_size < tile->mem.vram.usable_size)
			drm_info(&xe->drm, "Small BAR device\n");
		drm_info(&xe->drm, "VRAM[%u, %u]: Actual physical size %pa, usable size exclude stolen %pa, CPU accessible size %pa\n", id,
			 tile->id, &tile->mem.vram.actual_physical_size, &tile->mem.vram.usable_size, &tile->mem.vram.io_size);
		drm_info(&xe->drm, "VRAM[%u, %u]: DPA range: [%pa-%llx], io range: [%pa-%llx]\n", id, tile->id,
			 &tile->mem.vram.dpa_base, tile->mem.vram.dpa_base + (u64)tile->mem.vram.actual_physical_size,
			 &tile->mem.vram.io_start, tile->mem.vram.io_start + (u64)tile->mem.vram.io_size);

		/* calculate total size using tile size to get the correct HW sizing */
		total_size += tile_size;
		available_size += vram_size;

		if (total_size > xe->mem.vram.io_size) {
			drm_info(&xe->drm, "VRAM: %pa is larger than resource %pa\n",
				 &total_size, &xe->mem.vram.io_size);
		}

		io_size -= min_t(u64, tile_size, io_size);
	}

	xe->mem.vram.actual_physical_size = total_size;

	drm_info(&xe->drm, "Total VRAM: %pa, %pa\n", &xe->mem.vram.io_start,
		 &xe->mem.vram.actual_physical_size);
	drm_info(&xe->drm, "Available VRAM: %pa, %pa\n", &xe->mem.vram.io_start,
		 &available_size);

	return 0;
}

void xe_mmio_probe_tiles(struct xe_device *xe)
{
	size_t tile_mmio_size = SZ_16M, tile_mmio_ext_size = xe->info.tile_mmio_ext_size;
	u8 id, tile_count = xe->info.tile_count;
	struct xe_gt *gt = xe_root_mmio_gt(xe);
	struct xe_tile *tile;
	void __iomem *regs;
	u32 mtcfg;

	if (tile_count == 1)
		goto add_mmio_ext;

	if (!xe->info.skip_mtcfg) {
		mtcfg = xe_mmio_read64_2x32(gt, XEHP_MTCFG_ADDR);
		tile_count = REG_FIELD_GET(TILE_COUNT, mtcfg) + 1;
		if (tile_count < xe->info.tile_count) {
			drm_info(&xe->drm, "tile_count: %d, reduced_tile_count %d\n",
					xe->info.tile_count, tile_count);
			xe->info.tile_count = tile_count;

			/*
			 * FIXME: Needs some work for standalone media, but should be impossible
			 * with multi-tile for now.
			 */
			xe->info.gt_count = xe->info.tile_count;
		}
	}

	regs = xe->mmio.regs;
	for_each_tile(tile, xe, id) {
		tile->mmio.size = tile_mmio_size;
		tile->mmio.regs = regs;
		regs += tile_mmio_size;
	}

add_mmio_ext:
	/*
	 * By design, there's a contiguous multi-tile MMIO space (16MB hard coded per tile).
	 * When supported, there could be an additional contiguous multi-tile MMIO extension
	 * space ON TOP of it, and hence the necessity for distinguished MMIO spaces.
	 */
	if (xe->info.has_mmio_ext) {
		regs = xe->mmio.regs + tile_mmio_size * tile_count;

		for_each_tile(tile, xe, id) {
			tile->mmio_ext.size = tile_mmio_ext_size;
			tile->mmio_ext.regs = regs;

			regs += tile_mmio_ext_size;
		}
	}
}

static void mmio_fini(struct drm_device *drm, void *arg)
{
	struct xe_device *xe = arg;

	pci_iounmap(to_pci_dev(xe->drm.dev), xe->mmio.regs);
	if (xe->mem.vram.mapping)
		iounmap(xe->mem.vram.mapping);
}

static int xe_verify_lmem_ready(struct xe_device *xe)
{
	struct xe_gt *gt = xe_root_mmio_gt(xe);

	/*
	 * The boot firmware initializes local memory and assesses its health.
	 * If memory training fails, the punit will have been instructed to
	 * keep the GT powered down; we won't be able to communicate with it
	 * and we should not continue with driver initialization.
	 */
	if (IS_DGFX(xe) && !(xe_mmio_read32(gt, GU_CNTL) & LMEM_INIT)) {
		drm_err(&xe->drm, "VRAM not initialized by firmware\n");
		return -ENODEV;
	}

	return 0;
}

int xe_mmio_init(struct xe_device *xe)
{
	struct pci_dev *pdev = to_pci_dev(xe->drm.dev);
	const int mmio_bar = 0;

	/*
	 * Map the entire BAR.
	 * The first 16MB of the BAR, belong to the root tile, and include:
	 * registers (0-4MB), reserved space (4MB-8MB) and GGTT (8MB-16MB).
	 */
	xe->mmio.size = pci_resource_len(pdev, mmio_bar);
	xe->mmio.regs = pci_iomap(pdev, mmio_bar, 0);
	if (xe->mmio.regs == NULL) {
		drm_err(&xe->drm, "failed to map registers\n");
		return -EIO;
	}

	return drmm_add_action_or_reset(&xe->drm, mmio_fini, xe);
}

int xe_mmio_root_tile_init(struct xe_device *xe)
{
	struct xe_tile *root_tile = xe_device_get_root_tile(xe);
	int err;

	/* Setup first tile; other tiles (if present) will be setup later. */
	root_tile->mmio.size = SZ_16M;
	root_tile->mmio.regs = xe->mmio.regs;

	err = xe_verify_lmem_ready(xe);
	if (err)
		return err;

	return 0;
}

/**
 * xe_mmio_read64_2x32() - Read a 64-bit register as two 32-bit reads
 * @gt: MMIO target GT
 * @reg: register to read value from
 *
 * Although Intel GPUs have some 64-bit registers, the hardware officially
 * only supports GTTMMADR register reads of 32 bits or smaller.  Even if
 * a readq operation may return a reasonable value, that violation of the
 * spec shouldn't be relied upon and all 64-bit register reads should be
 * performed as two 32-bit reads of the upper and lower dwords.
 *
 * When reading registers that may be changing (such as
 * counters), a rollover of the lower dword between the two 32-bit reads
 * can be problematic.  This function attempts to ensure the upper dword has
 * stabilized before returning the 64-bit value.
 *
 * Note that because this function may re-read the register multiple times
 * while waiting for the value to stabilize it should not be used to read
 * any registers where read operations have side effects.
 *
 * Returns the value of the 64-bit register.
 */
u64 xe_mmio_read64_2x32(struct xe_gt *gt, struct xe_reg reg)
{
	struct xe_reg reg_udw = { .addr = reg.addr + 0x4 };
	u32 ldw, udw, oldudw, retries;

	if (reg.addr < gt->mmio.adj_limit) {
		reg.addr += gt->mmio.adj_offset;
		reg_udw.addr += gt->mmio.adj_offset;
	}

	oldudw = xe_mmio_read32(gt, reg_udw);
	for (retries = 5; retries; --retries) {
		ldw = xe_mmio_read32(gt, reg);
		udw = xe_mmio_read32(gt, reg_udw);

		if (udw == oldudw)
			break;

		oldudw = udw;
	}

	xe_gt_WARN(gt, retries == 0,
		   "64-bit read of %#x did not stabilize\n", reg.addr);

	return (u64)udw << 32 | ldw;
}

/**
 * xe_mmio_wait32() - Wait for a register to match the desired masked value
 * @gt: MMIO target GT
 * @reg: register to read value from
 * @mask: mask to be applied to the value read from the register
 * @val: desired value after applying the mask
 * @timeout_us: time out after this period of time. Wait logic tries to be
 * smart, applying an exponential backoff until @timeout_us is reached.
 * @out_val: if not NULL, points where to store the last unmasked value
 * @atomic: needs to be true if calling from an atomic context
 *
 * This function polls for the desired masked value and returns zero on success
 * or -ETIMEDOUT if timed out.
 *
 * Note that @timeout_us represents the minimum amount of time to wait before
 * giving up. The actual time taken by this function can be a little more than
 * @timeout_us for different reasons, specially in non-atomic contexts. Thus,
 * it is possible that this function succeeds even after @timeout_us has passed.
 */
int xe_mmio_wait32(struct xe_gt *gt, struct xe_reg reg, u32 mask, u32 val, u32 timeout_us,
		   u32 *out_val, bool atomic)
{
	ktime_t cur = ktime_get_raw();
	const ktime_t end = ktime_add_us(cur, timeout_us);
	int ret = -ETIMEDOUT;
	s64 wait = 10;
	u32 read;

	for (;;) {
		read = xe_mmio_read32(gt, reg);
		if ((read & mask) == val) {
			ret = 0;
			break;
		}

		cur = ktime_get_raw();
		if (!ktime_before(cur, end))
			break;

		if (ktime_after(ktime_add_us(cur, wait), end))
			wait = ktime_us_delta(end, cur);

		if (atomic)
			udelay(wait);
		else
			usleep_range(wait, wait << 1);
		wait <<= 1;
	}

	if (ret != 0) {
		read = xe_mmio_read32(gt, reg);
		if ((read & mask) == val)
			ret = 0;
	}

	if (out_val)
		*out_val = read;

	return ret;
}
