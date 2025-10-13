// SPDX-License-Identifier: MIT
/*
 * Copyright © 2025 Intel Corporation
 */

#include <linux/debugfs.h>

#include "xe_bo.h"
#include "xe_device.h"
#include "xe_configfs.h"
#include "xe_psmi.h"

/*
 * PSMI capture support
 *
 * Requirement for PSMI capture is to have a physically contiguous buffer.  The
 * PSMI tool owns doing all necessary configuration (MMIO register writes are
 * done from user-space). However, KMD needs to provide the PSMI tool with the
 * required physical address of the base of PSMI buffer in case of VRAM.
 *
 * VRAM backed PSMI buffer:
 * Buffer is allocated as GEM object and with XE_BO_CREATE_PINNED_BIT flag which
 * creates a contiguous allocation. The physical address is returned from
 * psmi_debugfs_capture_addr_show(). PSMI tool can mmap the buffer via the
 * PCIBAR through sysfs.
 *
 * SYSTEM memory backed PSMI buffer:
 * Interface here does not support allocating from SYSTEM memory region.  The
 * PSMI tool needs to allocate memory themselves using hugetlbfs. In order to
 * get the physical address, user-space can query /proc/[pid]/pagemap. As an
 * alternative, CMA debugfs could also be used to allocate reserved CMA memory.
 */

static bool psmi_enabled(struct xe_device *xe)
{
	return xe_configfs_get_psmi_enabled(to_pci_dev(xe->drm.dev));
}

static void psmi_free_object(struct xe_bo *bo)
{
	xe_bo_lock(bo, NULL);
	xe_bo_unpin(bo);
	xe_bo_unlock(bo);
	xe_bo_put(bo);
}

/*
 * Free PSMI capture buffer objects.
 */
static void psmi_cleanup(struct xe_device *xe)
{
	unsigned long id, region_mask = xe->psmi.region_mask;
	struct xe_bo *bo;

	for_each_set_bit(id, &region_mask,
			 ARRAY_SIZE(xe->psmi.capture_obj)) {
		/* smem should never be set */
		xe_assert(xe, id);

		bo = xe->psmi.capture_obj[id];
		if (bo) {
			psmi_free_object(bo);
			xe->psmi.capture_obj[id] = NULL;
		}
	}
}

static struct xe_bo *psmi_alloc_object(struct xe_device *xe,
				       unsigned int id, size_t bo_size)
{
	struct xe_tile *tile;

	if (!id || !bo_size)
		return NULL;

	tile = &xe->tiles[id - 1];

	/* VRAM: Allocate GEM object for the capture buffer */
	return xe_bo_create_pin_range_novm(xe, tile, bo_size, 0, ~0ull,
					   ttm_bo_type_kernel,
					   XE_BO_FLAG_VRAM_IF_DGFX(tile) |
					   XE_BO_FLAG_PINNED |
					   XE_BO_FLAG_PINNED_LATE_RESTORE |
					   XE_BO_FLAG_NEEDS_CPU_ACCESS);
}

/*
 * Allocate PSMI capture buffer objects (via debugfs set function), based on
 * which regions the user has selected in region_mask.  @size: size in bytes
 * (should be power of 2)
 *
 * Always release/free the current buffer objects before attempting to allocate
 * new ones.  Size == 0 will free all current buffers.
 *
 * Note, we don't write any registers as the capture tool is already configuring
 * all PSMI registers itself via mmio space.
 */
static int psmi_resize_object(struct xe_device *xe, size_t size)
{
	unsigned long id, region_mask = xe->psmi.region_mask;
	struct xe_bo *bo = NULL;
	int err = 0;

	/* if resizing, free currently allocated buffers first */
	psmi_cleanup(xe);

	/* can set size to 0, in which case, now done */
	if (!size)
		return 0;

	for_each_set_bit(id, &region_mask,
			 ARRAY_SIZE(xe->psmi.capture_obj)) {
		/* smem should never be set */
		xe_assert(xe, id);

		bo = psmi_alloc_object(xe, id, size);
		if (IS_ERR(bo)) {
			err = PTR_ERR(bo);
			break;
		}
		xe->psmi.capture_obj[id] = bo;

		drm_info(&xe->drm,
			 "PSMI capture size requested: %zu bytes, allocated: %lu:%zu\n",
			 size, id, bo ? xe_bo_size(bo) : 0);
	}

	/* on error, reverse what was allocated */
	if (err)
		psmi_cleanup(xe);

	return err;
}

/*
 * Returns an address for the capture tool to use to find start of capture
 * buffer. Capture tool requires the capability to have a buffer allocated per
 * each tile (VRAM region), thus we return an address for each region.
 */
static int psmi_debugfs_capture_addr_show(struct seq_file *m, void *data)
{
	struct xe_device *xe = m->private;
	unsigned long id, region_mask;
	struct xe_bo *bo;
	u64 val;

	region_mask = xe->psmi.region_mask;
	for_each_set_bit(id, &region_mask,
			 ARRAY_SIZE(xe->psmi.capture_obj)) {
		/* smem should never be set */
		xe_assert(xe, id);

		/* VRAM region */
		bo = xe->psmi.capture_obj[id];
		if (!bo)
			continue;

		/* pinned, so don't need bo_lock */
		val = __xe_bo_addr(bo, 0, PAGE_SIZE);
		seq_printf(m, "%ld: 0x%llx\n", id, val);
	}

	return 0;
}

/*
 * Return capture buffer size, using the size from first allocated object that
 * is found. This works because all objects must be of the same size.
 */
static int psmi_debugfs_capture_size_get(void *data, u64 *val)
{
	unsigned long id, region_mask;
	struct xe_device *xe = data;
	struct xe_bo *bo;

	region_mask = xe->psmi.region_mask;
	for_each_set_bit(id, &region_mask,
			 ARRAY_SIZE(xe->psmi.capture_obj)) {
		/* smem should never be set */
		xe_assert(xe, id);

		bo = xe->psmi.capture_obj[id];
		if (bo) {
			*val = xe_bo_size(bo);
			return 0;
		}
	}

	/* no capture objects are allocated */
	*val = 0;

	return 0;
}

/*
 * Set size of PSMI capture buffer. This triggers the allocation of capture
 * buffer in each memory region as specified with prior write to
 * psmi_capture_region_mask.
 */
static int psmi_debugfs_capture_size_set(void *data, u64 val)
{
	struct xe_device *xe = data;

	/* user must have specified at least one region */
	if (!xe->psmi.region_mask)
		return -EINVAL;

	return psmi_resize_object(xe, val);
}

static int psmi_debugfs_capture_region_mask_get(void *data, u64 *val)
{
	struct xe_device *xe = data;

	*val = xe->psmi.region_mask;

	return 0;
}

/*
 * Select VRAM regions for multi-tile devices, only allowed when buffer is not
 * currently allocated.
 */
static int psmi_debugfs_capture_region_mask_set(void *data, u64 region_mask)
{
	struct xe_device *xe = data;
	u64 size = 0;

	/* SMEM is not supported (see comments at top of file) */
	if (region_mask & 0x1)
		return -EOPNOTSUPP;

	/* input bitmask should contain only valid TTM regions */
	if (!region_mask || region_mask & ~xe->info.mem_region_mask)
		return -EINVAL;

	/* only allow setting mask if buffer is not yet allocated */
	psmi_debugfs_capture_size_get(xe, &size);
	if (size)
		return -EBUSY;

	xe->psmi.region_mask = region_mask;

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(psmi_debugfs_capture_addr);

DEFINE_DEBUGFS_ATTRIBUTE(psmi_debugfs_capture_region_mask_fops,
			 psmi_debugfs_capture_region_mask_get,
			 psmi_debugfs_capture_region_mask_set,
			 "0x%llx\n");

DEFINE_DEBUGFS_ATTRIBUTE(psmi_debugfs_capture_size_fops,
			 psmi_debugfs_capture_size_get,
			 psmi_debugfs_capture_size_set,
			 "%lld\n");

void xe_psmi_debugfs_register(struct xe_device *xe)
{
	struct drm_minor *minor;

	if (!psmi_enabled(xe))
		return;

	minor = xe->drm.primary;
	if (!minor->debugfs_root)
		return;

	debugfs_create_file("psmi_capture_addr",
			    0400, minor->debugfs_root, xe,
			    &psmi_debugfs_capture_addr_fops);

	debugfs_create_file("psmi_capture_region_mask",
			    0600, minor->debugfs_root, xe,
			    &psmi_debugfs_capture_region_mask_fops);

	debugfs_create_file("psmi_capture_size",
			    0600, minor->debugfs_root, xe,
			    &psmi_debugfs_capture_size_fops);
}

static void psmi_fini(void *arg)
{
	psmi_cleanup(arg);
}

int xe_psmi_init(struct xe_device *xe)
{
	if (!psmi_enabled(xe))
		return 0;

	return devm_add_action(xe->drm.dev, psmi_fini, xe);
}
