// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include <drm/drm_managed.h>

#include "regs/xe_gt_regs.h"
#include "xe_assert.h"
#include "xe_gt.h"
#include "xe_gt_ccs_mode.h"
#include "xe_gt_printk.h"
#include "xe_gt_sysfs.h"
#include "xe_mmio.h"
#include "xe_sriov.h"

static void __xe_gt_apply_ccs_mode(struct xe_gt *gt, u32 num_engines)
{
	u32 mode = CCS_MODE_CSLICE_0_3_MASK; /* disable all by default */
	int num_slices = hweight32(CCS_MASK(gt));
	struct xe_device *xe = gt_to_xe(gt);
	int width, cslice = 0;
	u32 config = 0;

	xe_assert(xe, xe_gt_ccs_mode_enabled(gt));

	xe_assert(xe, num_engines && num_engines <= num_slices);
	xe_assert(xe, !(num_slices % num_engines));

	/*
	 * Loop over all available slices and assign each a user engine.
	 * For example, if there are four compute slices available, the
	 * assignment of compute slices to compute engines would be,
	 *
	 * With 1 engine (ccs0):
	 *   slice 0, 1, 2, 3: ccs0
	 *
	 * With 2 engines (ccs0, ccs1):
	 *   slice 0, 2: ccs0
	 *   slice 1, 3: ccs1
	 *
	 * With 4 engines (ccs0, ccs1, ccs2, ccs3):
	 *   slice 0: ccs0
	 *   slice 1: ccs1
	 *   slice 2: ccs2
	 *   slice 3: ccs3
	 */
	for (width = num_slices / num_engines; width; width--) {
		struct xe_hw_engine *hwe;
		enum xe_hw_engine_id id;

		for_each_hw_engine(hwe, gt, id) {
			if (hwe->class != XE_ENGINE_CLASS_COMPUTE)
				continue;

			if (hwe->logical_instance >= num_engines)
				break;

			config |= BIT(hwe->instance) << XE_HW_ENGINE_CCS0;

			/* If a slice is fused off, leave disabled */
			while ((CCS_MASK(gt) & BIT(cslice)) == 0)
				cslice++;

			mode &= ~CCS_MODE_CSLICE(cslice, CCS_MODE_CSLICE_MASK);
			mode |= CCS_MODE_CSLICE(cslice, hwe->instance);
			cslice++;
		}
	}

	/*
	 * Mask bits need to be set for the register. Though only Xe2+
	 * platforms require setting of mask bits, it won't harm for older
	 * platforms as these bits are unused there.
	 */
	mode |= CCS_MODE_CSLICE_0_3_MASK << 16;
	xe_mmio_write32(&gt->mmio, CCS_MODE, mode);

	xe_gt_dbg(gt, "CCS_MODE=%x config:%08x, num_engines:%d, num_slices:%d\n",
		  mode, config, num_engines, num_slices);
}

void xe_gt_apply_ccs_mode(struct xe_gt *gt)
{
	if (!gt->ccs_mode || IS_SRIOV_VF(gt_to_xe(gt)))
		return;

	__xe_gt_apply_ccs_mode(gt, gt->ccs_mode);
}

static ssize_t
num_cslices_show(struct device *kdev,
		 struct device_attribute *attr, char *buf)
{
	struct xe_gt *gt = kobj_to_gt(&kdev->kobj);

	return sysfs_emit(buf, "%u\n", hweight32(CCS_MASK(gt)));
}

static DEVICE_ATTR_RO(num_cslices);

static ssize_t
ccs_mode_show(struct device *kdev,
	      struct device_attribute *attr, char *buf)
{
	struct xe_gt *gt = kobj_to_gt(&kdev->kobj);

	return sysfs_emit(buf, "%u\n", gt->ccs_mode);
}

static ssize_t
ccs_mode_store(struct device *kdev, struct device_attribute *attr,
	       const char *buff, size_t count)
{
	struct xe_gt *gt = kobj_to_gt(&kdev->kobj);
	struct xe_device *xe = gt_to_xe(gt);
	u32 num_engines, num_slices;
	int ret;

	if (IS_SRIOV(xe)) {
		xe_gt_dbg(gt, "Can't change compute mode when running as %s\n",
			  xe_sriov_mode_to_string(xe_device_sriov_mode(xe)));
		return -EOPNOTSUPP;
	}

	ret = kstrtou32(buff, 0, &num_engines);
	if (ret)
		return ret;

	/*
	 * Ensure number of engines specified is valid and there is an
	 * exact multiple of engines for slices.
	 */
	num_slices = hweight32(CCS_MASK(gt));
	if (!num_engines || num_engines > num_slices || num_slices % num_engines) {
		xe_gt_dbg(gt, "Invalid compute config, %d engines %d slices\n",
			  num_engines, num_slices);
		return -EINVAL;
	}

	/* CCS mode can only be updated when there are no drm clients */
	mutex_lock(&xe->drm.filelist_mutex);
	if (!list_empty(&xe->drm.filelist)) {
		mutex_unlock(&xe->drm.filelist_mutex);
		xe_gt_dbg(gt, "Rejecting compute mode change as there are active drm clients\n");
		return -EBUSY;
	}

	if (gt->ccs_mode != num_engines) {
		xe_gt_info(gt, "Setting compute mode to %d\n", num_engines);
		gt->ccs_mode = num_engines;
		xe_gt_record_user_engines(gt);
		xe_gt_reset_async(gt);
	}

	mutex_unlock(&xe->drm.filelist_mutex);

	return count;
}

static DEVICE_ATTR_RW(ccs_mode);

static const struct attribute *gt_ccs_mode_attrs[] = {
	&dev_attr_ccs_mode.attr,
	&dev_attr_num_cslices.attr,
	NULL,
};

static void xe_gt_ccs_mode_sysfs_fini(void *arg)
{
	struct xe_gt *gt = arg;

	sysfs_remove_files(gt->sysfs, gt_ccs_mode_attrs);
}

/**
 * xe_gt_ccs_mode_sysfs_init - Initialize CCS mode sysfs interfaces
 * @gt: GT structure
 *
 * Through a per-gt 'ccs_mode' sysfs interface, the user can enable a fixed
 * number of compute hardware engines to which the available compute slices
 * are to be allocated. This user configuration change triggers a gt reset
 * and it is expected that there are no open drm clients while doing so.
 * The number of available compute slices is exposed to user through a per-gt
 * 'num_cslices' sysfs interface.
 *
 * Returns: Returns error value for failure and 0 for success.
 */
int xe_gt_ccs_mode_sysfs_init(struct xe_gt *gt)
{
	struct xe_device *xe = gt_to_xe(gt);
	int err;

	if (!xe_gt_ccs_mode_enabled(gt))
		return 0;

	err = sysfs_create_files(gt->sysfs, gt_ccs_mode_attrs);
	if (err)
		return err;

	return devm_add_action_or_reset(xe->drm.dev, xe_gt_ccs_mode_sysfs_fini, gt);
}
