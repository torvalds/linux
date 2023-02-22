// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021 Intel Corporation
 */

#include "xe_mmio.h"

#include <drm/drm_managed.h>
#include <drm/xe_drm.h>

#include "xe_device.h"
#include "xe_gt.h"
#include "xe_gt_mcr.h"
#include "xe_macros.h"
#include "xe_module.h"

#include "i915_reg.h"
#include "gt/intel_engine_regs.h"
#include "gt/intel_gt_regs.h"

#define XEHP_MTCFG_ADDR		_MMIO(0x101800)
#define TILE_COUNT		REG_GENMASK(15, 8)
#define GEN12_LMEM_BAR		2

static int xe_set_dma_info(struct xe_device *xe)
{
	unsigned int mask_size = xe->info.dma_mask_size;
	int err;

	/*
	 * We don't have a max segment size, so set it to the max so sg's
	 * debugging layer doesn't complain
	 */
	dma_set_max_seg_size(xe->drm.dev, UINT_MAX);

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
		drm_info(&xe->drm, "Failed to resize BAR%d to %dM (%pe)\n",
			 resno, 1 << bar_size, ERR_PTR(ret));
		return -1;
	}

	drm_info(&xe->drm, "BAR%d resized to %dM\n", resno, 1 << bar_size);
	return 1;
}

static int xe_resize_lmem_bar(struct xe_device *xe, resource_size_t lmem_size)
{
	struct pci_dev *pdev = to_pci_dev(xe->drm.dev);
	struct pci_bus *root = pdev->bus;
	struct resource *root_res;
	resource_size_t rebar_size;
	resource_size_t current_size;
	u32 pci_cmd;
	int i;
	int ret;
	u64 force_lmem_bar_size = xe_force_lmem_bar_size;

	current_size = roundup_pow_of_two(pci_resource_len(pdev, GEN12_LMEM_BAR));

	if (force_lmem_bar_size) {
		u32 bar_sizes;

		rebar_size = force_lmem_bar_size * (resource_size_t)SZ_1M;
		bar_sizes = pci_rebar_get_possible_sizes(pdev, GEN12_LMEM_BAR);

		if (rebar_size == current_size)
			return 0;

		if (!(bar_sizes & BIT(pci_rebar_bytes_to_size(rebar_size))) ||
		    rebar_size >= roundup_pow_of_two(lmem_size)) {
			rebar_size = lmem_size;
			drm_info(&xe->drm,
				 "Given bar size is not within supported size, setting it to default: %llu\n",
				 (u64)lmem_size >> 20);
		}
	} else {
		rebar_size = current_size;

		if (rebar_size != roundup_pow_of_two(lmem_size))
			rebar_size = lmem_size;
		else
			return 0;
	}

	while (root->parent)
		root = root->parent;

	pci_bus_for_each_resource(root, root_res, i) {
		if (root_res && root_res->flags & (IORESOURCE_MEM | IORESOURCE_MEM_64) &&
		    root_res->start > 0x100000000ull)
			break;
	}

	if (!root_res) {
		drm_info(&xe->drm, "Can't resize LMEM BAR - platform support is missing\n");
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

int xe_mmio_total_vram_size(struct xe_device *xe, u64 *vram_size, u64 *usable_size)
{
	struct xe_gt *gt = xe_device_get_gt(xe, 0);
	struct pci_dev *pdev = to_pci_dev(xe->drm.dev);
	int err;
	u32 reg;

	if (!xe->info.has_flat_ccs)  {
		*vram_size = pci_resource_len(pdev, GEN12_LMEM_BAR);
		if (usable_size)
			*usable_size = min(*vram_size, xe_mmio_read64(gt, GEN12_GSMBASE.reg));
		return 0;
	}

	err = xe_force_wake_get(gt_to_fw(gt), XE_FW_GT);
	if (err)
		return err;

	reg = xe_gt_mcr_unicast_read_any(gt, XEHP_TILE0_ADDR_RANGE);
	*vram_size = (u64)REG_FIELD_GET(GENMASK(14, 8), reg) * SZ_1G;
	if (usable_size) {
		reg = xe_gt_mcr_unicast_read_any(gt, XEHP_FLAT_CCS_BASE_ADDR);
		*usable_size = (u64)REG_FIELD_GET(GENMASK(31, 8), reg) * SZ_64K;
		drm_info(&xe->drm, "lmem_size: 0x%llx usable_size: 0x%llx\n",
			 *vram_size, *usable_size);
	}

	return xe_force_wake_put(gt_to_fw(gt), XE_FW_GT);
}

int xe_mmio_probe_vram(struct xe_device *xe)
{
	struct pci_dev *pdev = to_pci_dev(xe->drm.dev);
	struct xe_gt *gt;
	u8 id;
	u64 lmem_size;
	u64 original_size;
	u64 current_size;
	u64 usable_size;
	int resize_result, err;

	if (!IS_DGFX(xe)) {
		xe->mem.vram.mapping = 0;
		xe->mem.vram.size = 0;
		xe->mem.vram.io_start = 0;

		for_each_gt(gt, xe, id) {
			gt->mem.vram.mapping = 0;
			gt->mem.vram.size = 0;
			gt->mem.vram.io_start = 0;
		}
		return 0;
	}

	if (!xe_pci_resource_valid(pdev, GEN12_LMEM_BAR)) {
		drm_err(&xe->drm, "pci resource is not valid\n");
		return -ENXIO;
	}

	gt = xe_device_get_gt(xe, 0);
	original_size = pci_resource_len(pdev, GEN12_LMEM_BAR);

	err = xe_mmio_total_vram_size(xe, &lmem_size, &usable_size);
	if (err)
		return err;

	resize_result = xe_resize_lmem_bar(xe, lmem_size);
	current_size = pci_resource_len(pdev, GEN12_LMEM_BAR);
	xe->mem.vram.io_start = pci_resource_start(pdev, GEN12_LMEM_BAR);

	xe->mem.vram.size = min(current_size, lmem_size);

	if (!xe->mem.vram.size)
		return -EIO;

	if (resize_result > 0)
		drm_info(&xe->drm, "Successfully resize LMEM from %lluMiB to %lluMiB\n",
			 (u64)original_size >> 20,
			 (u64)current_size >> 20);
	else if (xe->mem.vram.size < lmem_size && !xe_force_lmem_bar_size)
		drm_info(&xe->drm, "Using a reduced BAR size of %lluMiB. Consider enabling 'Resizable BAR' support in your BIOS.\n",
			 (u64)xe->mem.vram.size >> 20);
	if (xe->mem.vram.size < lmem_size)
		drm_warn(&xe->drm, "Restricting VRAM size to PCI resource size (0x%llx->0x%llx)\n",
			 lmem_size, (u64)xe->mem.vram.size);

	xe->mem.vram.mapping = ioremap_wc(xe->mem.vram.io_start, xe->mem.vram.size);
	xe->mem.vram.size = min_t(u64, xe->mem.vram.size, usable_size);

	drm_info(&xe->drm, "TOTAL VRAM: %pa, %pa\n", &xe->mem.vram.io_start, &xe->mem.vram.size);

	/* FIXME: Assuming equally partitioned VRAM, incorrect */
	if (xe->info.tile_count > 1) {
		u8 adj_tile_count = xe->info.tile_count;
		resource_size_t size, io_start;

		for_each_gt(gt, xe, id)
			if (xe_gt_is_media_type(gt))
				--adj_tile_count;

		XE_BUG_ON(!adj_tile_count);

		size = xe->mem.vram.size / adj_tile_count;
		io_start = xe->mem.vram.io_start;

		for_each_gt(gt, xe, id) {
			if (id && !xe_gt_is_media_type(gt))
				io_start += size;

			gt->mem.vram.size = size;
			gt->mem.vram.io_start = io_start;
			gt->mem.vram.mapping = xe->mem.vram.mapping +
				(io_start - xe->mem.vram.io_start);

			drm_info(&xe->drm, "VRAM[%u, %u]: %pa, %pa\n",
				 id, gt->info.vram_id, &gt->mem.vram.io_start,
				 &gt->mem.vram.size);
		}
	} else {
		gt->mem.vram.size = xe->mem.vram.size;
		gt->mem.vram.io_start = xe->mem.vram.io_start;
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

	mtcfg = xe_mmio_read64(gt, XEHP_MTCFG_ADDR.reg);
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
	if (IS_DGFX(xe) && !(xe_mmio_read32(gt, GU_CNTL.reg) & LMEM_INIT)) {
		drm_err(&xe->drm, "LMEM not initialized by firmware\n");
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

static const i915_reg_t mmio_read_whitelist[] = {
	RING_TIMESTAMP(RENDER_RING_BASE),
};

int xe_mmio_ioctl(struct drm_device *dev, void *data,
		  struct drm_file *file)
{
	struct xe_device *xe = to_xe_device(dev);
	struct drm_xe_mmio *args = data;
	unsigned int bits_flag, bytes;
	bool allowed;
	int ret = 0;

	if (XE_IOCTL_ERR(xe, args->extensions))
		return -EINVAL;

	if (XE_IOCTL_ERR(xe, args->flags & ~VALID_MMIO_FLAGS))
		return -EINVAL;

	if (XE_IOCTL_ERR(xe, !(args->flags & DRM_XE_MMIO_WRITE) && args->value))
		return -EINVAL;

	allowed = capable(CAP_SYS_ADMIN);
	if (!allowed && ((args->flags & ~DRM_XE_MMIO_BITS_MASK) == DRM_XE_MMIO_READ)) {
		unsigned int i;

		for (i = 0; i < ARRAY_SIZE(mmio_read_whitelist); i++) {
			if (mmio_read_whitelist[i].reg == args->addr) {
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

	xe_force_wake_get(gt_to_fw(&xe->gt[0]), XE_FORCEWAKE_ALL);

	if (args->flags & DRM_XE_MMIO_WRITE) {
		switch (bits_flag) {
		case DRM_XE_MMIO_32BIT:
			if (XE_IOCTL_ERR(xe, args->value > U32_MAX)) {
				ret = -EINVAL;
				goto exit;
			}
			xe_mmio_write32(to_gt(xe), args->addr, args->value);
			break;
		case DRM_XE_MMIO_64BIT:
			xe_mmio_write64(to_gt(xe), args->addr, args->value);
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
			args->value = xe_mmio_read32(to_gt(xe), args->addr);
			break;
		case DRM_XE_MMIO_64BIT:
			args->value = xe_mmio_read64(to_gt(xe), args->addr);
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
