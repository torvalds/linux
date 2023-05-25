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
#include "xe_gt.h"
#include "xe_gt_mcr.h"
#include "xe_macros.h"
#include "xe_module.h"

#define XEHP_MTCFG_ADDR		XE_REG(0x101800)
#define TILE_COUNT		REG_GENMASK(15, 8)

#define BAR_SIZE_SHIFT 20

static int xe_set_dma_info(struct xe_device *xe)
{
	unsigned int mask_size = xe->info.dma_mask_size;
	int err;

	dma_set_max_seg_size(xe->drm.dev, xe_sg_segment_size(xe->drm.dev));

	err = dma_set_mask(xe->drm.dev, DMA_BIT_MASK(mask_size));
	if (err)
		goto mask_err;

	err = dma_set_coherent_mask(xe->drm.dev, DMA_BIT_MASK(mask_size));
	if (err)
		goto mask_err;

	return 0;

mask_err:
	drm_err(&xe->drm, "Can't set DMA mask/consistent mask (%d)\n", err);
	return err;
}

static int
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
		return ret;
	}

	drm_info(&xe->drm, "BAR%d resized to %dM\n", resno, 1 << bar_size);
	return ret;
}

/*
 * if force_vram_bar_size is set, attempt to set to the requested size
 * else set to maximum possible size
 */
static int xe_resize_vram_bar(struct xe_device *xe)
{
	u64 force_vram_bar_size = xe_force_vram_bar_size;
	struct pci_dev *pdev = to_pci_dev(xe->drm.dev);
	struct pci_bus *root = pdev->bus;
	resource_size_t current_size;
	resource_size_t rebar_size;
	struct resource *root_res;
	u32 bar_size_mask;
	u32 pci_cmd;
	int i;
	int ret;

	/* gather some relevant info */
	current_size = pci_resource_len(pdev, GEN12_LMEM_BAR);
	bar_size_mask = pci_rebar_get_possible_sizes(pdev, GEN12_LMEM_BAR);

	if (!bar_size_mask)
		return 0;

	/* set to a specific size? */
	if (force_vram_bar_size) {
		u32 bar_size_bit;

		rebar_size = force_vram_bar_size * (resource_size_t)SZ_1M;

		bar_size_bit = bar_size_mask & BIT(pci_rebar_bytes_to_size(rebar_size));

		if (!bar_size_bit) {
			drm_info(&xe->drm,
				 "Requested size: %lluMiB is not supported by rebar sizes: 0x%x. Leaving default: %lluMiB\n",
				 (u64)rebar_size >> 20, bar_size_mask, (u64)current_size >> 20);
			return 0;
		}

		rebar_size = 1ULL << (__fls(bar_size_bit) + BAR_SIZE_SHIFT);

		if (rebar_size == current_size)
			return 0;
	} else {
		rebar_size = 1ULL << (__fls(bar_size_mask) + BAR_SIZE_SHIFT);

		/* only resize if larger than current */
		if (rebar_size <= current_size)
			return 0;
	}

	drm_info(&xe->drm, "Resizing bar from %lluMiB -> %lluMiB\n",
		 (u64)current_size >> 20, (u64)rebar_size >> 20);

	while (root->parent)
		root = root->parent;

	pci_bus_for_each_resource(root, root_res, i) {
		if (root_res && root_res->flags & (IORESOURCE_MEM | IORESOURCE_MEM_64) &&
		    root_res->start > 0x100000000ull)
			break;
	}

	if (!root_res) {
		drm_info(&xe->drm, "Can't resize VRAM BAR - platform support is missing. Consider enabling 'Resizable BAR' support in your BIOS\n");
		return -1;
	}

	pci_read_config_dword(pdev, PCI_COMMAND, &pci_cmd);
	pci_write_config_dword(pdev, PCI_COMMAND, pci_cmd & ~PCI_COMMAND_MEMORY);

	ret = _resize_bar(xe, GEN12_LMEM_BAR, rebar_size);

	pci_assign_unassigned_bus_resources(pdev->bus);
	pci_write_config_dword(pdev, PCI_COMMAND, pci_cmd);
	return ret;
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
	int err;

	if (!xe_pci_resource_valid(pdev, GEN12_LMEM_BAR)) {
		drm_err(&xe->drm, "pci resource is not valid\n");
		return -ENXIO;
	}

	err = xe_resize_vram_bar(xe);
	if (err)
		return err;

	xe->mem.vram.io_start = pci_resource_start(pdev, GEN12_LMEM_BAR);
	xe->mem.vram.io_size = pci_resource_len(pdev, GEN12_LMEM_BAR);
	if (!xe->mem.vram.io_size)
		return -EIO;

	/* set up a map to the total memory area. */
	xe->mem.vram.mapping = ioremap_wc(xe->mem.vram.io_start, xe->mem.vram.io_size);

	return 0;
}

/**
 * xe_mmio_tile_vram_size() - Collect vram size and offset information
 * @gt: tile to get info for
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
int xe_mmio_tile_vram_size(struct xe_gt *gt, u64 *vram_size, u64 *tile_size, u64 *tile_offset)
{
	u64 offset;
	int err;
	u32 reg;

	err = xe_force_wake_get(gt_to_fw(gt), XE_FW_GT);
	if (err)
		return err;

	/* actual size */
	if (unlikely(gt->xe->info.platform == XE_DG1)) {
		*tile_size = pci_resource_len(to_pci_dev(gt->xe->drm.dev), GEN12_LMEM_BAR);
		*tile_offset = 0;
	} else {
		reg = xe_gt_mcr_unicast_read_any(gt, XEHP_TILE_ADDR_RANGE(gt->info.id));
		*tile_size = (u64)REG_FIELD_GET(GENMASK(14, 8), reg) * SZ_1G;
		*tile_offset = (u64)REG_FIELD_GET(GENMASK(7, 1), reg) * SZ_1G;
	}

	/* minus device usage */
	if (gt->xe->info.has_flat_ccs) {
		reg = xe_gt_mcr_unicast_read_any(gt, XEHP_FLAT_CCS_BASE_ADDR);
		offset = (u64)REG_FIELD_GET(GENMASK(31, 8), reg) * SZ_64K;
	} else {
		offset = xe_mmio_read64(gt, GSMBASE);
	}

	/* remove the tile offset so we have just the available size */
	*vram_size = offset - *tile_offset;

	return xe_force_wake_put(gt_to_fw(gt), XE_FW_GT);
}

int xe_mmio_probe_vram(struct xe_device *xe)
{
	struct xe_gt *gt;
	u64 tile_offset;
	u64 tile_size;
	u64 vram_size;
	int err;
	u8 id;

	if (!IS_DGFX(xe))
		return 0;

	/* Get the size of the gt0 vram for later accessibility comparison */
	gt = xe_device_get_gt(xe, 0);
	err = xe_mmio_tile_vram_size(gt, &vram_size, &tile_size, &tile_offset);
	if (err)
		return err;

	err = xe_determine_lmem_bar_size(xe);
	if (err)
		return err;

	/* small bar issues will only cover gt0 sizes */
	if (xe->mem.vram.io_size < vram_size)
		drm_warn(&xe->drm, "Restricting VRAM size to PCI resource size (0x%llx->0x%llx)\n",
			 vram_size, (u64)xe->mem.vram.io_size);

	/* Limit size to available memory to account for the current memory algorithm */
	xe->mem.vram.io_size = min_t(u64, xe->mem.vram.io_size, vram_size);
	xe->mem.vram.size = xe->mem.vram.io_size;

	drm_info(&xe->drm, "VISIBLE VRAM: %pa, %pa\n", &xe->mem.vram.io_start,
		 &xe->mem.vram.io_size);

	/* FIXME: Assuming equally partitioned VRAM, incorrect */
	if (xe->info.tile_count > 1) {
		u8 adj_tile_count = xe->info.tile_count;
		resource_size_t size, io_start, io_size;

		for_each_gt(gt, xe, id)
			if (xe_gt_is_media_type(gt))
				--adj_tile_count;

		XE_BUG_ON(!adj_tile_count);

		size = xe->mem.vram.size / adj_tile_count;
		io_start = xe->mem.vram.io_start;
		io_size = xe->mem.vram.io_size;

		for_each_gt(gt, xe, id) {
			if (id && !xe_gt_is_media_type(gt)) {
				io_size -= min(io_size, size);
				io_start += io_size;
			}

			gt->mem.vram.size = size;

			/*
			 * XXX: multi-tile small-bar might be wild. Hopefully
			 * full tile without any mappable vram is not something
			 * we care about.
			 */

			gt->mem.vram.io_size = min(size, io_size);
			if (io_size) {
				gt->mem.vram.io_start = io_start;
				gt->mem.vram.mapping = xe->mem.vram.mapping +
					(io_start - xe->mem.vram.io_start);
			} else {
				drm_err(&xe->drm, "Tile without any CPU visible VRAM. Aborting.\n");
				return -ENODEV;
			}

			drm_info(&xe->drm, "VRAM[%u, %u]: %pa, %pa\n",
				 id, gt->info.vram_id, &gt->mem.vram.io_start,
				 &gt->mem.vram.size);
		}
	} else {
		gt->mem.vram.size = xe->mem.vram.size;
		gt->mem.vram.io_start = xe->mem.vram.io_start;
		gt->mem.vram.io_size = xe->mem.vram.io_size;
		gt->mem.vram.mapping = xe->mem.vram.mapping;

		drm_info(&xe->drm, "VRAM: %pa\n", &gt->mem.vram.size);
	}
	return 0;
}

static void xe_mmio_probe_tiles(struct xe_device *xe)
{
	struct xe_gt *gt = xe_device_get_gt(xe, 0);
	u32 mtcfg;
	u8 adj_tile_count;
	u8 id;

	if (xe->info.tile_count == 1)
		return;

	mtcfg = xe_mmio_read64(gt, XEHP_MTCFG_ADDR);
	adj_tile_count = xe->info.tile_count =
		REG_FIELD_GET(TILE_COUNT, mtcfg) + 1;
	if (xe->info.media_verx100 >= 1300)
		xe->info.tile_count *= 2;

	drm_info(&xe->drm, "tile_count: %d, adj_tile_count %d\n",
		 xe->info.tile_count, adj_tile_count);

	if (xe->info.tile_count > 1) {
		const int mmio_bar = 0;
		size_t size;
		void *regs;

		if (adj_tile_count > 1) {
			pci_iounmap(to_pci_dev(xe->drm.dev), xe->mmio.regs);
			xe->mmio.size = SZ_16M * adj_tile_count;
			xe->mmio.regs = pci_iomap(to_pci_dev(xe->drm.dev),
						  mmio_bar, xe->mmio.size);
		}

		size = xe->mmio.size / adj_tile_count;
		regs = xe->mmio.regs;

		for_each_gt(gt, xe, id) {
			if (id && !xe_gt_is_media_type(gt))
				regs += size;
			gt->mmio.size = size;
			gt->mmio.regs = regs;
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

int xe_mmio_init(struct xe_device *xe)
{
	struct xe_gt *gt = xe_device_get_gt(xe, 0);
	const int mmio_bar = 0;
	int err;

	/*
	 * Map the entire BAR, which includes registers (0-4MB), reserved space
	 * (4MB-8MB), and GGTT (8MB-16MB). Other parts of the driver (GTs,
	 * GGTTs) will derive the pointers they need from the mapping in the
	 * device structure.
	 */
	xe->mmio.size = SZ_16M;
	xe->mmio.regs = pci_iomap(to_pci_dev(xe->drm.dev), mmio_bar,
				  xe->mmio.size);
	if (xe->mmio.regs == NULL) {
		drm_err(&xe->drm, "failed to map registers\n");
		return -EIO;
	}

	err = drmm_add_action_or_reset(&xe->drm, mmio_fini, xe);
	if (err)
		return err;

	/* 1 GT for now, 1 to 1 mapping, may change on multi-GT devices */
	gt->mmio.size = xe->mmio.size;
	gt->mmio.regs = xe->mmio.regs;

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

	err = xe_set_dma_info(xe);
	if (err)
		return err;

	xe_mmio_probe_tiles(xe);

	return 0;
}

#define VALID_MMIO_FLAGS (\
	DRM_XE_MMIO_BITS_MASK |\
	DRM_XE_MMIO_READ |\
	DRM_XE_MMIO_WRITE)

static const struct xe_reg mmio_read_whitelist[] = {
	RING_TIMESTAMP(RENDER_RING_BASE),
};

int xe_mmio_ioctl(struct drm_device *dev, void *data,
		  struct drm_file *file)
{
	struct xe_device *xe = to_xe_device(dev);
	struct drm_xe_mmio *args = data;
	unsigned int bits_flag, bytes;
	struct xe_reg reg;
	bool allowed;
	int ret = 0;

	if (XE_IOCTL_ERR(xe, args->extensions) ||
	    XE_IOCTL_ERR(xe, args->reserved[0] || args->reserved[1]))
		return -EINVAL;

	if (XE_IOCTL_ERR(xe, args->flags & ~VALID_MMIO_FLAGS))
		return -EINVAL;

	if (XE_IOCTL_ERR(xe, !(args->flags & DRM_XE_MMIO_WRITE) && args->value))
		return -EINVAL;

	allowed = capable(CAP_SYS_ADMIN);
	if (!allowed && ((args->flags & ~DRM_XE_MMIO_BITS_MASK) == DRM_XE_MMIO_READ)) {
		unsigned int i;

		for (i = 0; i < ARRAY_SIZE(mmio_read_whitelist); i++) {
			if (mmio_read_whitelist[i].addr == args->addr) {
				allowed = true;
				break;
			}
		}
	}

	if (XE_IOCTL_ERR(xe, !allowed))
		return -EPERM;

	bits_flag = args->flags & DRM_XE_MMIO_BITS_MASK;
	bytes = 1 << bits_flag;
	if (XE_IOCTL_ERR(xe, args->addr + bytes > xe->mmio.size))
		return -EINVAL;

	/*
	 * TODO: migrate to xe_gt_mcr to lookup the mmio range and handle
	 * multicast registers. Steering would need uapi extension.
	 */
	reg = XE_REG(args->addr);

	xe_force_wake_get(gt_to_fw(&xe->gt[0]), XE_FORCEWAKE_ALL);

	if (args->flags & DRM_XE_MMIO_WRITE) {
		switch (bits_flag) {
		case DRM_XE_MMIO_32BIT:
			if (XE_IOCTL_ERR(xe, args->value > U32_MAX)) {
				ret = -EINVAL;
				goto exit;
			}
			xe_mmio_write32(to_gt(xe), reg, args->value);
			break;
		case DRM_XE_MMIO_64BIT:
			xe_mmio_write64(to_gt(xe), reg, args->value);
			break;
		default:
			drm_dbg(&xe->drm, "Invalid MMIO bit size");
			fallthrough;
		case DRM_XE_MMIO_8BIT: /* TODO */
		case DRM_XE_MMIO_16BIT: /* TODO */
			ret = -ENOTSUPP;
			goto exit;
		}
	}

	if (args->flags & DRM_XE_MMIO_READ) {
		switch (bits_flag) {
		case DRM_XE_MMIO_32BIT:
			args->value = xe_mmio_read32(to_gt(xe), reg);
			break;
		case DRM_XE_MMIO_64BIT:
			args->value = xe_mmio_read64(to_gt(xe), reg);
			break;
		default:
			drm_dbg(&xe->drm, "Invalid MMIO bit size");
			fallthrough;
		case DRM_XE_MMIO_8BIT: /* TODO */
		case DRM_XE_MMIO_16BIT: /* TODO */
			ret = -ENOTSUPP;
		}
	}

exit:
	xe_force_wake_put(gt_to_fw(&xe->gt[0]), XE_FORCEWAKE_ALL);

	return ret;
}
