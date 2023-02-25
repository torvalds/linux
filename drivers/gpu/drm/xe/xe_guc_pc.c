// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include "xe_guc_pc.h"

#include <linux/delay.h>

#include <drm/drm_managed.h>

#include "regs/xe_gt_regs.h"
#include "regs/xe_regs.h"
#include "xe_bo.h"
#include "xe_device.h"
#include "xe_gt.h"
#include "xe_gt_sysfs.h"
#include "xe_gt_types.h"
#include "xe_guc_ct.h"
#include "xe_map.h"
#include "xe_mmio.h"
#include "xe_pcode.h"

#include "intel_mchbar_regs.h"

/* For GEN6_RP_STATE_CAP.reg to be merged when the definition moves to Xe */
#define   RP0_MASK	REG_GENMASK(7, 0)
#define   RP1_MASK	REG_GENMASK(15, 8)
#define   RPN_MASK	REG_GENMASK(23, 16)

#define GEN10_FREQ_INFO_REC	_MMIO(MCHBAR_MIRROR_BASE_SNB + 0x5ef0)
#define   RPE_MASK		REG_GENMASK(15, 8)

/* For GEN6_RPNSWREQ.reg to be merged when the definition moves to Xe */
#define   REQ_RATIO_MASK	REG_GENMASK(31, 23)

/* For GEN6_GT_CORE_STATUS.reg to be merged when the definition moves to Xe */
#define   RCN_MASK	REG_GENMASK(2, 0)

#define GEN12_RPSTAT1		_MMIO(0x1381b4)
#define   GEN12_CAGF_MASK	REG_GENMASK(19, 11)

#define MTL_MIRROR_TARGET_WP1	_MMIO(0xc60)
#define   MTL_CAGF_MASK		REG_GENMASK(8, 0)

#define GT_FREQUENCY_MULTIPLIER	50
#define GEN9_FREQ_SCALER	3

/**
 * DOC: GuC Power Conservation (PC)
 *
 * GuC Power Conservation (PC) supports multiple features for the most
 * efficient and performing use of the GT when GuC submission is enabled,
 * including frequency management, Render-C states management, and various
 * algorithms for power balancing.
 *
 * Single Loop Power Conservation (SLPC) is the name given to the suite of
 * connected power conservation features in the GuC firmware. The firmware
 * exposes a programming interface to the host for the control of SLPC.
 *
 * Frequency management:
 * =====================
 *
 * Xe driver enables SLPC with all of its defaults features and frequency
 * selection, which varies per platform.
 * Xe's GuC PC provides a sysfs API for frequency management:
 *
 * device/gt#/freq_* *read-only* files:
 * - freq_act: The actual resolved frequency decided by PCODE.
 * - freq_cur: The current one requested by GuC PC to the Hardware.
 * - freq_rpn: The Render Performance (RP) N level, which is the minimal one.
 * - freq_rpe: The Render Performance (RP) E level, which is the efficient one.
 * - freq_rp0: The Render Performance (RP) 0 level, which is the maximum one.
 *
 * device/gt#/freq_* *read-write* files:
 * - freq_min: GuC PC min request.
 * - freq_max: GuC PC max request.
 *             If max <= min, then freq_min becomes a fixed frequency request.
 *
 * Render-C States:
 * ================
 *
 * Render-C states is also a GuC PC feature that is now enabled in Xe for
 * all platforms.
 * Xe's GuC PC provides a sysfs API for Render-C States:
 *
 * device/gt#/rc* *read-only* files:
 * - rc_status: Provide the actual immediate status of Render-C: (rc0 or rc6)
 * - rc6_residency: Provide the rc6_residency counter in units of 1.28 uSec.
 *                  Prone to overflows.
 */

static struct xe_guc *
pc_to_guc(struct xe_guc_pc *pc)
{
	return container_of(pc, struct xe_guc, pc);
}

static struct xe_device *
pc_to_xe(struct xe_guc_pc *pc)
{
	struct xe_guc *guc = pc_to_guc(pc);
	struct xe_gt *gt = container_of(guc, struct xe_gt, uc.guc);

	return gt_to_xe(gt);
}

static struct xe_gt *
pc_to_gt(struct xe_guc_pc *pc)
{
	return container_of(pc, struct xe_gt, uc.guc.pc);
}

static struct xe_guc_pc *
dev_to_pc(struct device *dev)
{
	return &kobj_to_gt(&dev->kobj)->uc.guc.pc;
}

static struct iosys_map *
pc_to_maps(struct xe_guc_pc *pc)
{
	return &pc->bo->vmap;
}

#define slpc_shared_data_read(pc_, field_) \
	xe_map_rd_field(pc_to_xe(pc_), pc_to_maps(pc_), 0, \
			struct slpc_shared_data, field_)

#define slpc_shared_data_write(pc_, field_, val_) \
	xe_map_wr_field(pc_to_xe(pc_), pc_to_maps(pc_), 0, \
			struct slpc_shared_data, field_, val_)

#define SLPC_EVENT(id, count) \
	(FIELD_PREP(HOST2GUC_PC_SLPC_REQUEST_MSG_1_EVENT_ID, id) | \
	 FIELD_PREP(HOST2GUC_PC_SLPC_REQUEST_MSG_1_EVENT_ARGC, count))

static int wait_for_pc_state(struct xe_guc_pc *pc,
			     enum slpc_global_state state)
{
	int timeout_us = 5000; /* rought 5ms, but no need for precision */
	int slept, wait = 10;

	xe_device_assert_mem_access(pc_to_xe(pc));

	for (slept = 0; slept < timeout_us;) {
		if (slpc_shared_data_read(pc, header.global_state) == state)
			return 0;

		usleep_range(wait, wait << 1);
		slept += wait;
		wait <<= 1;
		if (slept + wait > timeout_us)
			wait = timeout_us - slept;
	}

	return -ETIMEDOUT;
}

static int pc_action_reset(struct xe_guc_pc *pc)
{
	struct  xe_guc_ct *ct = &pc_to_guc(pc)->ct;
	int ret;
	u32 action[] = {
		GUC_ACTION_HOST2GUC_PC_SLPC_REQUEST,
		SLPC_EVENT(SLPC_EVENT_RESET, 2),
		xe_bo_ggtt_addr(pc->bo),
		0,
	};

	ret = xe_guc_ct_send(ct, action, ARRAY_SIZE(action), 0, 0);
	if (ret)
		drm_err(&pc_to_xe(pc)->drm, "GuC PC reset: %pe", ERR_PTR(ret));

	return ret;
}

static int pc_action_shutdown(struct xe_guc_pc *pc)
{
	struct  xe_guc_ct *ct = &pc_to_guc(pc)->ct;
	int ret;
	u32 action[] = {
		GUC_ACTION_HOST2GUC_PC_SLPC_REQUEST,
		SLPC_EVENT(SLPC_EVENT_SHUTDOWN, 2),
		xe_bo_ggtt_addr(pc->bo),
		0,
	};

	ret = xe_guc_ct_send(ct, action, ARRAY_SIZE(action), 0, 0);
	if (ret)
		drm_err(&pc_to_xe(pc)->drm, "GuC PC shutdown %pe",
			ERR_PTR(ret));

	return ret;
}

static int pc_action_query_task_state(struct xe_guc_pc *pc)
{
	struct xe_guc_ct *ct = &pc_to_guc(pc)->ct;
	int ret;
	u32 action[] = {
		GUC_ACTION_HOST2GUC_PC_SLPC_REQUEST,
		SLPC_EVENT(SLPC_EVENT_QUERY_TASK_STATE, 2),
		xe_bo_ggtt_addr(pc->bo),
		0,
	};

	if (wait_for_pc_state(pc, SLPC_GLOBAL_STATE_RUNNING))
		return -EAGAIN;

	/* Blocking here to ensure the results are ready before reading them */
	ret = xe_guc_ct_send_block(ct, action, ARRAY_SIZE(action));
	if (ret)
		drm_err(&pc_to_xe(pc)->drm,
			"GuC PC query task state failed: %pe", ERR_PTR(ret));

	return ret;
}

static int pc_action_set_param(struct xe_guc_pc *pc, u8 id, u32 value)
{
	struct xe_guc_ct *ct = &pc_to_guc(pc)->ct;
	int ret;
	u32 action[] = {
		GUC_ACTION_HOST2GUC_PC_SLPC_REQUEST,
		SLPC_EVENT(SLPC_EVENT_PARAMETER_SET, 2),
		id,
		value,
	};

	if (wait_for_pc_state(pc, SLPC_GLOBAL_STATE_RUNNING))
		return -EAGAIN;

	ret = xe_guc_ct_send(ct, action, ARRAY_SIZE(action), 0, 0);
	if (ret)
		drm_err(&pc_to_xe(pc)->drm, "GuC PC set param failed: %pe",
			ERR_PTR(ret));

	return ret;
}

static int pc_action_setup_gucrc(struct xe_guc_pc *pc, u32 mode)
{
	struct xe_guc_ct *ct = &pc_to_guc(pc)->ct;
	u32 action[] = {
		XE_GUC_ACTION_SETUP_PC_GUCRC,
		mode,
	};
	int ret;

	ret = xe_guc_ct_send(ct, action, ARRAY_SIZE(action), 0, 0);
	if (ret)
		drm_err(&pc_to_xe(pc)->drm, "GuC RC enable failed: %pe",
			ERR_PTR(ret));
	return ret;
}

static u32 decode_freq(u32 raw)
{
	return DIV_ROUND_CLOSEST(raw * GT_FREQUENCY_MULTIPLIER,
				 GEN9_FREQ_SCALER);
}

static u32 pc_get_min_freq(struct xe_guc_pc *pc)
{
	u32 freq;

	freq = FIELD_GET(SLPC_MIN_UNSLICE_FREQ_MASK,
			 slpc_shared_data_read(pc, task_state_data.freq));

	return decode_freq(freq);
}

static int pc_set_min_freq(struct xe_guc_pc *pc, u32 freq)
{
	/*
	 * Let's only check for the rpn-rp0 range. If max < min,
	 * min becomes a fixed request.
	 */
	if (freq < pc->rpn_freq || freq > pc->rp0_freq)
		return -EINVAL;

	/*
	 * GuC policy is to elevate minimum frequency to the efficient levels
	 * Our goal is to have the admin choices respected.
	 */
	pc_action_set_param(pc, SLPC_PARAM_IGNORE_EFFICIENT_FREQUENCY,
			    freq < pc->rpe_freq);

	return pc_action_set_param(pc,
				   SLPC_PARAM_GLOBAL_MIN_GT_UNSLICE_FREQ_MHZ,
				   freq);
}

static int pc_get_max_freq(struct xe_guc_pc *pc)
{
	u32 freq;

	freq = FIELD_GET(SLPC_MAX_UNSLICE_FREQ_MASK,
			 slpc_shared_data_read(pc, task_state_data.freq));

	return decode_freq(freq);
}

static int pc_set_max_freq(struct xe_guc_pc *pc, u32 freq)
{
	/*
	 * Let's only check for the rpn-rp0 range. If max < min,
	 * min becomes a fixed request.
	 * Also, overclocking is not supported.
	 */
	if (freq < pc->rpn_freq || freq > pc->rp0_freq)
		return -EINVAL;

	return pc_action_set_param(pc,
				   SLPC_PARAM_GLOBAL_MAX_GT_UNSLICE_FREQ_MHZ,
				   freq);
}

static void mtl_update_rpe_value(struct xe_guc_pc *pc)
{
	struct xe_gt *gt = pc_to_gt(pc);
	u32 reg;

	if (xe_gt_is_media_type(gt))
		reg = xe_mmio_read32(gt, MTL_MPE_FREQUENCY.reg);
	else
		reg = xe_mmio_read32(gt, MTL_GT_RPE_FREQUENCY.reg);

	pc->rpe_freq = REG_FIELD_GET(MTL_RPE_MASK, reg) * GT_FREQUENCY_MULTIPLIER;
}

static void tgl_update_rpe_value(struct xe_guc_pc *pc)
{
	struct xe_gt *gt = pc_to_gt(pc);
	struct xe_device *xe = gt_to_xe(gt);
	u32 reg;

	/*
	 * For PVC we still need to use fused RP1 as the approximation for RPe
	 * For other platforms than PVC we get the resolved RPe directly from
	 * PCODE at a different register
	 */
	if (xe->info.platform == XE_PVC)
		reg = xe_mmio_read32(gt, PVC_RP_STATE_CAP.reg);
	else
		reg = xe_mmio_read32(gt, GEN10_FREQ_INFO_REC.reg);

	pc->rpe_freq = REG_FIELD_GET(RPE_MASK, reg) * GT_FREQUENCY_MULTIPLIER;
}

static void pc_update_rp_values(struct xe_guc_pc *pc)
{
	struct xe_gt *gt = pc_to_gt(pc);
	struct xe_device *xe = gt_to_xe(gt);

	if (xe->info.platform == XE_METEORLAKE)
		mtl_update_rpe_value(pc);
	else
		tgl_update_rpe_value(pc);

	/*
	 * RPe is decided at runtime by PCODE. In the rare case where that's
	 * smaller than the fused min, we will trust the PCODE and use that
	 * as our minimum one.
	 */
	pc->rpn_freq = min(pc->rpn_freq, pc->rpe_freq);
}

static ssize_t freq_act_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct kobject *kobj = &dev->kobj;
	struct xe_gt *gt = kobj_to_gt(kobj);
	struct xe_device *xe = gt_to_xe(gt);
	u32 freq;
	ssize_t ret;

	/*
	 * When in RC6, actual frequency is 0. Let's block RC6 so we are able
	 * to verify that our freq requests are really happening.
	 */
	ret = xe_force_wake_get(gt_to_fw(gt), XE_FORCEWAKE_ALL);
	if (ret)
		return ret;

	xe_device_mem_access_get(gt_to_xe(gt));

	if (xe->info.platform == XE_METEORLAKE) {
		freq = xe_mmio_read32(gt, MTL_MIRROR_TARGET_WP1.reg);
		freq = REG_FIELD_GET(MTL_CAGF_MASK, freq);
	} else {
		freq = xe_mmio_read32(gt, GEN12_RPSTAT1.reg);
		freq = REG_FIELD_GET(GEN12_CAGF_MASK, freq);
	}

	xe_device_mem_access_put(gt_to_xe(gt));

	ret = sysfs_emit(buf, "%d\n", decode_freq(freq));

	XE_WARN_ON(xe_force_wake_put(gt_to_fw(gt), XE_FORCEWAKE_ALL));
	return ret;
}
static DEVICE_ATTR_RO(freq_act);

static ssize_t freq_cur_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct kobject *kobj = &dev->kobj;
	struct xe_gt *gt = kobj_to_gt(kobj);
	u32 freq;
	ssize_t ret;

	/*
	 * GuC SLPC plays with cur freq request when GuCRC is enabled
	 * Block RC6 for a more reliable read.
	 */
	ret = xe_force_wake_get(gt_to_fw(gt), XE_FORCEWAKE_ALL);
	if (ret)
		return ret;

	xe_device_mem_access_get(gt_to_xe(gt));
	freq = xe_mmio_read32(gt, GEN6_RPNSWREQ.reg);
	xe_device_mem_access_put(gt_to_xe(gt));

	freq = REG_FIELD_GET(REQ_RATIO_MASK, freq);
	ret = sysfs_emit(buf, "%d\n", decode_freq(freq));

	XE_WARN_ON(xe_force_wake_put(gt_to_fw(gt), XE_FORCEWAKE_ALL));
	return ret;
}
static DEVICE_ATTR_RO(freq_cur);

static ssize_t freq_rp0_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct xe_guc_pc *pc = dev_to_pc(dev);

	return sysfs_emit(buf, "%d\n", pc->rp0_freq);
}
static DEVICE_ATTR_RO(freq_rp0);

static ssize_t freq_rpe_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct xe_guc_pc *pc = dev_to_pc(dev);

	pc_update_rp_values(pc);
	return sysfs_emit(buf, "%d\n", pc->rpe_freq);
}
static DEVICE_ATTR_RO(freq_rpe);

static ssize_t freq_rpn_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct xe_guc_pc *pc = dev_to_pc(dev);

	return sysfs_emit(buf, "%d\n", pc->rpn_freq);
}
static DEVICE_ATTR_RO(freq_rpn);

static ssize_t freq_min_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct xe_guc_pc *pc = dev_to_pc(dev);
	struct xe_gt *gt = pc_to_gt(pc);
	ssize_t ret;

	xe_device_mem_access_get(pc_to_xe(pc));
	mutex_lock(&pc->freq_lock);
	if (!pc->freq_ready) {
		/* Might be in the middle of a gt reset */
		ret = -EAGAIN;
		goto out;
	}

	/*
	 * GuC SLPC plays with min freq request when GuCRC is enabled
	 * Block RC6 for a more reliable read.
	 */
	ret = xe_force_wake_get(gt_to_fw(gt), XE_FORCEWAKE_ALL);
	if (ret)
		goto out;

	ret = pc_action_query_task_state(pc);
	if (ret)
		goto fw;

	ret = sysfs_emit(buf, "%d\n", pc_get_min_freq(pc));

fw:
	XE_WARN_ON(xe_force_wake_put(gt_to_fw(gt), XE_FORCEWAKE_ALL));
out:
	mutex_unlock(&pc->freq_lock);
	xe_device_mem_access_put(pc_to_xe(pc));
	return ret;
}

static ssize_t freq_min_store(struct device *dev, struct device_attribute *attr,
			      const char *buff, size_t count)
{
	struct xe_guc_pc *pc = dev_to_pc(dev);
	u32 freq;
	ssize_t ret;

	ret = kstrtou32(buff, 0, &freq);
	if (ret)
		return ret;

	xe_device_mem_access_get(pc_to_xe(pc));
	mutex_lock(&pc->freq_lock);
	if (!pc->freq_ready) {
		/* Might be in the middle of a gt reset */
		ret = -EAGAIN;
		goto out;
	}

	ret = pc_set_min_freq(pc, freq);
	if (ret)
		goto out;

	pc->user_requested_min = freq;

out:
	mutex_unlock(&pc->freq_lock);
	xe_device_mem_access_put(pc_to_xe(pc));
	return ret ?: count;
}
static DEVICE_ATTR_RW(freq_min);

static ssize_t freq_max_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct xe_guc_pc *pc = dev_to_pc(dev);
	ssize_t ret;

	xe_device_mem_access_get(pc_to_xe(pc));
	mutex_lock(&pc->freq_lock);
	if (!pc->freq_ready) {
		/* Might be in the middle of a gt reset */
		ret = -EAGAIN;
		goto out;
	}

	ret = pc_action_query_task_state(pc);
	if (ret)
		goto out;

	ret = sysfs_emit(buf, "%d\n", pc_get_max_freq(pc));

out:
	mutex_unlock(&pc->freq_lock);
	xe_device_mem_access_put(pc_to_xe(pc));
	return ret;
}

static ssize_t freq_max_store(struct device *dev, struct device_attribute *attr,
			      const char *buff, size_t count)
{
	struct xe_guc_pc *pc = dev_to_pc(dev);
	u32 freq;
	ssize_t ret;

	ret = kstrtou32(buff, 0, &freq);
	if (ret)
		return ret;

	xe_device_mem_access_get(pc_to_xe(pc));
	mutex_lock(&pc->freq_lock);
	if (!pc->freq_ready) {
		/* Might be in the middle of a gt reset */
		ret = -EAGAIN;
		goto out;
	}

	ret = pc_set_max_freq(pc, freq);
	if (ret)
		goto out;

	pc->user_requested_max = freq;

out:
	mutex_unlock(&pc->freq_lock);
	xe_device_mem_access_put(pc_to_xe(pc));
	return ret ?: count;
}
static DEVICE_ATTR_RW(freq_max);

static ssize_t rc_status_show(struct device *dev,
			      struct device_attribute *attr, char *buff)
{
	struct xe_guc_pc *pc = dev_to_pc(dev);
	struct xe_gt *gt = pc_to_gt(pc);
	u32 reg;

	xe_device_mem_access_get(gt_to_xe(gt));
	reg = xe_mmio_read32(gt, GEN6_GT_CORE_STATUS.reg);
	xe_device_mem_access_put(gt_to_xe(gt));

	switch (REG_FIELD_GET(RCN_MASK, reg)) {
	case GEN6_RC6:
		return sysfs_emit(buff, "rc6\n");
	case GEN6_RC0:
		return sysfs_emit(buff, "rc0\n");
	default:
		return -ENOENT;
	}
}
static DEVICE_ATTR_RO(rc_status);

static ssize_t rc6_residency_show(struct device *dev,
				  struct device_attribute *attr, char *buff)
{
	struct xe_guc_pc *pc = dev_to_pc(dev);
	struct xe_gt *gt = pc_to_gt(pc);
	u32 reg;
	ssize_t ret;

	ret = xe_force_wake_get(gt_to_fw(gt), XE_FORCEWAKE_ALL);
	if (ret)
		return ret;

	xe_device_mem_access_get(pc_to_xe(pc));
	reg = xe_mmio_read32(gt, GEN6_GT_GFX_RC6.reg);
	xe_device_mem_access_put(pc_to_xe(pc));

	ret = sysfs_emit(buff, "%u\n", reg);

	XE_WARN_ON(xe_force_wake_put(gt_to_fw(gt), XE_FORCEWAKE_ALL));
	return ret;
}
static DEVICE_ATTR_RO(rc6_residency);

static const struct attribute *pc_attrs[] = {
	&dev_attr_freq_act.attr,
	&dev_attr_freq_cur.attr,
	&dev_attr_freq_rp0.attr,
	&dev_attr_freq_rpe.attr,
	&dev_attr_freq_rpn.attr,
	&dev_attr_freq_min.attr,
	&dev_attr_freq_max.attr,
	&dev_attr_rc_status.attr,
	&dev_attr_rc6_residency.attr,
	NULL
};

static void mtl_init_fused_rp_values(struct xe_guc_pc *pc)
{
	struct xe_gt *gt = pc_to_gt(pc);
	u32 reg;

	xe_device_assert_mem_access(pc_to_xe(pc));

	if (xe_gt_is_media_type(gt))
		reg = xe_mmio_read32(gt, MTL_MEDIAP_STATE_CAP.reg);
	else
		reg = xe_mmio_read32(gt, MTL_RP_STATE_CAP.reg);
	pc->rp0_freq = REG_FIELD_GET(MTL_RP0_CAP_MASK, reg) *
		GT_FREQUENCY_MULTIPLIER;
	pc->rpn_freq = REG_FIELD_GET(MTL_RPN_CAP_MASK, reg) *
		GT_FREQUENCY_MULTIPLIER;
}

static void tgl_init_fused_rp_values(struct xe_guc_pc *pc)
{
	struct xe_gt *gt = pc_to_gt(pc);
	struct xe_device *xe = gt_to_xe(gt);
	u32 reg;

	xe_device_assert_mem_access(pc_to_xe(pc));

	if (xe->info.platform == XE_PVC)
		reg = xe_mmio_read32(gt, PVC_RP_STATE_CAP.reg);
	else
		reg = xe_mmio_read32(gt, GEN6_RP_STATE_CAP.reg);
	pc->rp0_freq = REG_FIELD_GET(RP0_MASK, reg) * GT_FREQUENCY_MULTIPLIER;
	pc->rpn_freq = REG_FIELD_GET(RPN_MASK, reg) * GT_FREQUENCY_MULTIPLIER;
}

static void pc_init_fused_rp_values(struct xe_guc_pc *pc)
{
	struct xe_gt *gt = pc_to_gt(pc);
	struct xe_device *xe = gt_to_xe(gt);

	if (xe->info.platform == XE_METEORLAKE)
		mtl_init_fused_rp_values(pc);
	else
		tgl_init_fused_rp_values(pc);
}
static int pc_adjust_freq_bounds(struct xe_guc_pc *pc)
{
	int ret;

	lockdep_assert_held(&pc->freq_lock);

	ret = pc_action_query_task_state(pc);
	if (ret)
		return ret;

	/*
	 * GuC defaults to some RPmax that is not actually achievable without
	 * overclocking. Let's adjust it to the Hardware RP0, which is the
	 * regular maximum
	 */
	if (pc_get_max_freq(pc) > pc->rp0_freq)
		pc_set_max_freq(pc, pc->rp0_freq);

	/*
	 * Same thing happens for Server platforms where min is listed as
	 * RPMax
	 */
	if (pc_get_min_freq(pc) > pc->rp0_freq)
		pc_set_min_freq(pc, pc->rp0_freq);

	return 0;
}

static int pc_adjust_requested_freq(struct xe_guc_pc *pc)
{
	int ret = 0;

	lockdep_assert_held(&pc->freq_lock);

	if (pc->user_requested_min != 0) {
		ret = pc_set_min_freq(pc, pc->user_requested_min);
		if (ret)
			return ret;
	}

	if (pc->user_requested_max != 0) {
		ret = pc_set_max_freq(pc, pc->user_requested_max);
		if (ret)
			return ret;
	}

	return ret;
}

static int pc_gucrc_disable(struct xe_guc_pc *pc)
{
	struct xe_gt *gt = pc_to_gt(pc);
	int ret;

	xe_device_assert_mem_access(pc_to_xe(pc));

	ret = pc_action_setup_gucrc(pc, XE_GUCRC_HOST_CONTROL);
	if (ret)
		return ret;

	ret = xe_force_wake_get(gt_to_fw(gt), XE_FORCEWAKE_ALL);
	if (ret)
		return ret;

	xe_mmio_write32(gt, GEN9_PG_ENABLE.reg, 0);
	xe_mmio_write32(gt, GEN6_RC_CONTROL.reg, 0);
	xe_mmio_write32(gt, GEN6_RC_STATE.reg, 0);

	XE_WARN_ON(xe_force_wake_put(gt_to_fw(gt), XE_FORCEWAKE_ALL));
	return 0;
}

static void pc_init_pcode_freq(struct xe_guc_pc *pc)
{
	u32 min = DIV_ROUND_CLOSEST(pc->rpn_freq, GT_FREQUENCY_MULTIPLIER);
	u32 max = DIV_ROUND_CLOSEST(pc->rp0_freq, GT_FREQUENCY_MULTIPLIER);

	XE_WARN_ON(xe_pcode_init_min_freq_table(pc_to_gt(pc), min, max));
}

static int pc_init_freqs(struct xe_guc_pc *pc)
{
	int ret;

	mutex_lock(&pc->freq_lock);

	ret = pc_adjust_freq_bounds(pc);
	if (ret)
		goto out;

	ret = pc_adjust_requested_freq(pc);
	if (ret)
		goto out;

	pc_update_rp_values(pc);

	pc_init_pcode_freq(pc);

	/*
	 * The frequencies are really ready for use only after the user
	 * requested ones got restored.
	 */
	pc->freq_ready = true;

out:
	mutex_unlock(&pc->freq_lock);
	return ret;
}

/**
 * xe_guc_pc_start - Start GuC's Power Conservation component
 * @pc: Xe_GuC_PC instance
 */
int xe_guc_pc_start(struct xe_guc_pc *pc)
{
	struct xe_device *xe = pc_to_xe(pc);
	struct xe_gt *gt = pc_to_gt(pc);
	u32 size = PAGE_ALIGN(sizeof(struct slpc_shared_data));
	int ret;

	XE_WARN_ON(!xe_device_guc_submission_enabled(xe));

	xe_device_mem_access_get(pc_to_xe(pc));

	memset(pc->bo->vmap.vaddr, 0, size);
	slpc_shared_data_write(pc, header.size, size);

	ret = xe_force_wake_get(gt_to_fw(gt), XE_FORCEWAKE_ALL);
	if (ret)
		return ret;

	ret = pc_action_reset(pc);
	if (ret)
		goto out;

	if (wait_for_pc_state(pc, SLPC_GLOBAL_STATE_RUNNING)) {
		drm_err(&pc_to_xe(pc)->drm, "GuC PC Start failed\n");
		ret = -EIO;
		goto out;
	}

	ret = pc_init_freqs(pc);
	if (ret)
		goto out;

	if (xe->info.platform == XE_PVC) {
		pc_gucrc_disable(pc);
		ret = 0;
		goto out;
	}

	ret = pc_action_setup_gucrc(pc, XE_GUCRC_FIRMWARE_CONTROL);

out:
	xe_device_mem_access_put(pc_to_xe(pc));
	XE_WARN_ON(xe_force_wake_put(gt_to_fw(gt), XE_FORCEWAKE_ALL));
	return ret;
}

/**
 * xe_guc_pc_stop - Stop GuC's Power Conservation component
 * @pc: Xe_GuC_PC instance
 */
int xe_guc_pc_stop(struct xe_guc_pc *pc)
{
	int ret;

	xe_device_mem_access_get(pc_to_xe(pc));

	ret = pc_gucrc_disable(pc);
	if (ret)
		goto out;

	mutex_lock(&pc->freq_lock);
	pc->freq_ready = false;
	mutex_unlock(&pc->freq_lock);

	ret = pc_action_shutdown(pc);
	if (ret)
		goto out;

	if (wait_for_pc_state(pc, SLPC_GLOBAL_STATE_NOT_RUNNING)) {
		drm_err(&pc_to_xe(pc)->drm, "GuC PC Shutdown failed\n");
		ret = -EIO;
	}

out:
	xe_device_mem_access_put(pc_to_xe(pc));
	return ret;
}

static void pc_fini(struct drm_device *drm, void *arg)
{
	struct xe_guc_pc *pc = arg;

	XE_WARN_ON(xe_guc_pc_stop(pc));
	sysfs_remove_files(pc_to_gt(pc)->sysfs, pc_attrs);
	xe_bo_unpin_map_no_vm(pc->bo);
}

/**
 * xe_guc_pc_init - Initialize GuC's Power Conservation component
 * @pc: Xe_GuC_PC instance
 */
int xe_guc_pc_init(struct xe_guc_pc *pc)
{
	struct xe_gt *gt = pc_to_gt(pc);
	struct xe_device *xe = gt_to_xe(gt);
	struct xe_bo *bo;
	u32 size = PAGE_ALIGN(sizeof(struct slpc_shared_data));
	int err;

	mutex_init(&pc->freq_lock);

	bo = xe_bo_create_pin_map(xe, gt, NULL, size,
				  ttm_bo_type_kernel,
				  XE_BO_CREATE_VRAM_IF_DGFX(gt) |
				  XE_BO_CREATE_GGTT_BIT);

	if (IS_ERR(bo))
		return PTR_ERR(bo);

	pc->bo = bo;

	pc_init_fused_rp_values(pc);

	err = sysfs_create_files(gt->sysfs, pc_attrs);
	if (err)
		return err;

	err = drmm_add_action_or_reset(&xe->drm, pc_fini, pc);
	if (err)
		return err;

	return 0;
}
