// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include "xe_gt_freq.h"

#include <linux/kobject.h>
#include <linux/sysfs.h>

#include <drm/drm_managed.h>
#include <drm/drm_print.h>

#include "xe_gt_sysfs.h"
#include "xe_gt_throttle.h"
#include "xe_gt_types.h"
#include "xe_guc_pc.h"
#include "xe_pm.h"

/**
 * DOC: Xe GT Frequency Management
 *
 * This component is responsible for the raw GT frequency management, including
 * the sysfs API.
 *
 * Underneath, Xe enables GuC SLPC automated frequency management. GuC is then
 * allowed to request PCODE any frequency between the Minimum and the Maximum
 * selected by this component. Furthermore, it is important to highlight that
 * PCODE is the ultimate decision maker of the actual running frequency, based
 * on thermal and other running conditions.
 *
 * Xe's Freq provides a sysfs API for frequency management:
 *
 * device/tile#/gt#/freq0/<item>_freq *read-only* files:
 *
 * - act_freq: The actual resolved frequency decided by PCODE.
 * - cur_freq: The current one requested by GuC PC to the PCODE.
 * - rpn_freq: The Render Performance (RP) N level, which is the minimal one.
 * - rpa_freq: The Render Performance (RP) A level, which is the achiveable one.
 *   Calculated by PCODE at runtime based on multiple running conditions
 * - rpe_freq: The Render Performance (RP) E level, which is the efficient one.
 *   Calculated by PCODE at runtime based on multiple running conditions
 * - rp0_freq: The Render Performance (RP) 0 level, which is the maximum one.
 *
 * device/tile#/gt#/freq0/<item>_freq *read-write* files:
 *
 * - min_freq: Min frequency request.
 * - max_freq: Max frequency request.
 *             If max <= min, then freq_min becomes a fixed frequency request.
 */

static struct xe_guc_pc *
dev_to_pc(struct device *dev)
{
	return &kobj_to_gt(dev->kobj.parent)->uc.guc.pc;
}

static struct xe_device *
dev_to_xe(struct device *dev)
{
	return gt_to_xe(kobj_to_gt(dev->kobj.parent));
}

static ssize_t act_freq_show(struct kobject *kobj,
			     struct kobj_attribute *attr, char *buf)
{
	struct device *dev = kobj_to_dev(kobj);
	struct xe_guc_pc *pc = dev_to_pc(dev);
	u32 freq;

	xe_pm_runtime_get(dev_to_xe(dev));
	freq = xe_guc_pc_get_act_freq(pc);
	xe_pm_runtime_put(dev_to_xe(dev));

	return sysfs_emit(buf, "%d\n", freq);
}
static struct kobj_attribute attr_act_freq = __ATTR_RO(act_freq);

static ssize_t cur_freq_show(struct kobject *kobj,
			     struct kobj_attribute *attr, char *buf)
{
	struct device *dev = kobj_to_dev(kobj);
	struct xe_guc_pc *pc = dev_to_pc(dev);
	u32 freq;
	ssize_t ret;

	xe_pm_runtime_get(dev_to_xe(dev));
	ret = xe_guc_pc_get_cur_freq(pc, &freq);
	xe_pm_runtime_put(dev_to_xe(dev));
	if (ret)
		return ret;

	return sysfs_emit(buf, "%d\n", freq);
}
static struct kobj_attribute attr_cur_freq = __ATTR_RO(cur_freq);

static ssize_t rp0_freq_show(struct kobject *kobj,
			     struct kobj_attribute *attr, char *buf)
{
	struct device *dev = kobj_to_dev(kobj);
	struct xe_guc_pc *pc = dev_to_pc(dev);
	u32 freq;

	xe_pm_runtime_get(dev_to_xe(dev));
	freq = xe_guc_pc_get_rp0_freq(pc);
	xe_pm_runtime_put(dev_to_xe(dev));

	return sysfs_emit(buf, "%d\n", freq);
}
static struct kobj_attribute attr_rp0_freq = __ATTR_RO(rp0_freq);

static ssize_t rpe_freq_show(struct kobject *kobj,
			     struct kobj_attribute *attr, char *buf)
{
	struct device *dev = kobj_to_dev(kobj);
	struct xe_guc_pc *pc = dev_to_pc(dev);
	u32 freq;

	xe_pm_runtime_get(dev_to_xe(dev));
	freq = xe_guc_pc_get_rpe_freq(pc);
	xe_pm_runtime_put(dev_to_xe(dev));

	return sysfs_emit(buf, "%d\n", freq);
}
static struct kobj_attribute attr_rpe_freq = __ATTR_RO(rpe_freq);

static ssize_t rpa_freq_show(struct kobject *kobj,
			     struct kobj_attribute *attr, char *buf)
{
	struct device *dev = kobj_to_dev(kobj);
	struct xe_guc_pc *pc = dev_to_pc(dev);
	u32 freq;

	xe_pm_runtime_get(dev_to_xe(dev));
	freq = xe_guc_pc_get_rpa_freq(pc);
	xe_pm_runtime_put(dev_to_xe(dev));

	return sysfs_emit(buf, "%d\n", freq);
}
static struct kobj_attribute attr_rpa_freq = __ATTR_RO(rpa_freq);

static ssize_t rpn_freq_show(struct kobject *kobj,
			     struct kobj_attribute *attr, char *buf)
{
	struct device *dev = kobj_to_dev(kobj);
	struct xe_guc_pc *pc = dev_to_pc(dev);

	return sysfs_emit(buf, "%d\n", xe_guc_pc_get_rpn_freq(pc));
}
static struct kobj_attribute attr_rpn_freq = __ATTR_RO(rpn_freq);

static ssize_t min_freq_show(struct kobject *kobj,
			     struct kobj_attribute *attr, char *buf)
{
	struct device *dev = kobj_to_dev(kobj);
	struct xe_guc_pc *pc = dev_to_pc(dev);
	u32 freq;
	ssize_t ret;

	xe_pm_runtime_get(dev_to_xe(dev));
	ret = xe_guc_pc_get_min_freq(pc, &freq);
	xe_pm_runtime_put(dev_to_xe(dev));
	if (ret)
		return ret;

	return sysfs_emit(buf, "%d\n", freq);
}

static ssize_t min_freq_store(struct kobject *kobj,
			      struct kobj_attribute *attr, const char *buff, size_t count)
{
	struct device *dev = kobj_to_dev(kobj);
	struct xe_guc_pc *pc = dev_to_pc(dev);
	u32 freq;
	ssize_t ret;

	ret = kstrtou32(buff, 0, &freq);
	if (ret)
		return ret;

	xe_pm_runtime_get(dev_to_xe(dev));
	ret = xe_guc_pc_set_min_freq(pc, freq);
	xe_pm_runtime_put(dev_to_xe(dev));
	if (ret)
		return ret;

	return count;
}
static struct kobj_attribute attr_min_freq = __ATTR_RW(min_freq);

static ssize_t max_freq_show(struct kobject *kobj,
			     struct kobj_attribute *attr, char *buf)
{
	struct device *dev = kobj_to_dev(kobj);
	struct xe_guc_pc *pc = dev_to_pc(dev);
	u32 freq;
	ssize_t ret;

	xe_pm_runtime_get(dev_to_xe(dev));
	ret = xe_guc_pc_get_max_freq(pc, &freq);
	xe_pm_runtime_put(dev_to_xe(dev));
	if (ret)
		return ret;

	return sysfs_emit(buf, "%d\n", freq);
}

static ssize_t max_freq_store(struct kobject *kobj,
			      struct kobj_attribute *attr, const char *buff, size_t count)
{
	struct device *dev = kobj_to_dev(kobj);
	struct xe_guc_pc *pc = dev_to_pc(dev);
	u32 freq;
	ssize_t ret;

	ret = kstrtou32(buff, 0, &freq);
	if (ret)
		return ret;

	xe_pm_runtime_get(dev_to_xe(dev));
	ret = xe_guc_pc_set_max_freq(pc, freq);
	xe_pm_runtime_put(dev_to_xe(dev));
	if (ret)
		return ret;

	return count;
}
static struct kobj_attribute attr_max_freq = __ATTR_RW(max_freq);

static const struct attribute *freq_attrs[] = {
	&attr_act_freq.attr,
	&attr_cur_freq.attr,
	&attr_rp0_freq.attr,
	&attr_rpa_freq.attr,
	&attr_rpe_freq.attr,
	&attr_rpn_freq.attr,
	&attr_min_freq.attr,
	&attr_max_freq.attr,
	NULL
};

static void freq_fini(void *arg)
{
	struct kobject *kobj = arg;

	sysfs_remove_files(kobj, freq_attrs);
	kobject_put(kobj);
}

/**
 * xe_gt_freq_init - Initialize Xe Freq component
 * @gt: Xe GT object
 *
 * It needs to be initialized after GT Sysfs and GuC PC components are ready.
 *
 * Returns: Returns error value for failure and 0 for success.
 */
int xe_gt_freq_init(struct xe_gt *gt)
{
	struct xe_device *xe = gt_to_xe(gt);
	int err;

	if (xe->info.skip_guc_pc)
		return 0;

	gt->freq = kobject_create_and_add("freq0", gt->sysfs);
	if (!gt->freq)
		return -ENOMEM;

	err = sysfs_create_files(gt->freq, freq_attrs);
	if (err)
		return err;

	err = devm_add_action_or_reset(xe->drm.dev, freq_fini, gt->freq);
	if (err)
		return err;

	return xe_gt_throttle_init(gt);
}
