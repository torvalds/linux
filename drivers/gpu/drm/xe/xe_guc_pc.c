// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include "xe_guc_pc.h"

#include <linux/delay.h>
#include <linux/ktime.h>

#include <drm/drm_managed.h>
#include <drm/drm_print.h>
#include <generated/xe_wa_oob.h>

#include "abi/guc_actions_slpc_abi.h"
#include "regs/xe_gt_regs.h"
#include "regs/xe_regs.h"
#include "xe_bo.h"
#include "xe_device.h"
#include "xe_force_wake.h"
#include "xe_gt.h"
#include "xe_gt_idle.h"
#include "xe_gt_printk.h"
#include "xe_gt_throttle.h"
#include "xe_gt_types.h"
#include "xe_guc.h"
#include "xe_guc_ct.h"
#include "xe_map.h"
#include "xe_mmio.h"
#include "xe_pcode.h"
#include "xe_pm.h"
#include "xe_sriov.h"
#include "xe_wa.h"

#define MCHBAR_MIRROR_BASE_SNB	0x140000

#define RP_STATE_CAP		XE_REG(MCHBAR_MIRROR_BASE_SNB + 0x5998)
#define   RP0_MASK		REG_GENMASK(7, 0)
#define   RP1_MASK		REG_GENMASK(15, 8)
#define   RPN_MASK		REG_GENMASK(23, 16)

#define FREQ_INFO_REC	XE_REG(MCHBAR_MIRROR_BASE_SNB + 0x5ef0)
#define   RPE_MASK		REG_GENMASK(15, 8)
#define   RPA_MASK		REG_GENMASK(31, 16)

#define GT_PERF_STATUS		XE_REG(0x1381b4)
#define   CAGF_MASK	REG_GENMASK(19, 11)

#define GT_FREQUENCY_MULTIPLIER	50
#define GT_FREQUENCY_SCALER	3

#define LNL_MERT_FREQ_CAP	800
#define BMG_MERT_FREQ_CAP	2133

#define SLPC_RESET_TIMEOUT_MS 5 /* roughly 5ms, but no need for precision */
#define SLPC_RESET_EXTENDED_TIMEOUT_MS 1000 /* To be used only at pc_start */

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
 *
 * Render-C States:
 * ================
 *
 * Render-C states is also a GuC PC feature that is now enabled in Xe for
 * all platforms.
 *
 */

static struct xe_guc *pc_to_guc(struct xe_guc_pc *pc)
{
	return container_of(pc, struct xe_guc, pc);
}

static struct xe_guc_ct *pc_to_ct(struct xe_guc_pc *pc)
{
	return &pc_to_guc(pc)->ct;
}

static struct xe_gt *pc_to_gt(struct xe_guc_pc *pc)
{
	return guc_to_gt(pc_to_guc(pc));
}

static struct xe_device *pc_to_xe(struct xe_guc_pc *pc)
{
	return guc_to_xe(pc_to_guc(pc));
}

static struct iosys_map *pc_to_maps(struct xe_guc_pc *pc)
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
			     enum slpc_global_state state,
			     int timeout_ms)
{
	int timeout_us = 1000 * timeout_ms;
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
	struct xe_guc_ct *ct = pc_to_ct(pc);
	u32 action[] = {
		GUC_ACTION_HOST2GUC_PC_SLPC_REQUEST,
		SLPC_EVENT(SLPC_EVENT_RESET, 2),
		xe_bo_ggtt_addr(pc->bo),
		0,
	};
	int ret;

	ret = xe_guc_ct_send(ct, action, ARRAY_SIZE(action), 0, 0);
	if (ret)
		xe_gt_err(pc_to_gt(pc), "GuC PC reset failed: %pe\n",
			  ERR_PTR(ret));

	return ret;
}

static int pc_action_query_task_state(struct xe_guc_pc *pc)
{
	struct xe_guc_ct *ct = pc_to_ct(pc);
	u32 action[] = {
		GUC_ACTION_HOST2GUC_PC_SLPC_REQUEST,
		SLPC_EVENT(SLPC_EVENT_QUERY_TASK_STATE, 2),
		xe_bo_ggtt_addr(pc->bo),
		0,
	};
	int ret;

	if (wait_for_pc_state(pc, SLPC_GLOBAL_STATE_RUNNING,
			      SLPC_RESET_TIMEOUT_MS))
		return -EAGAIN;

	/* Blocking here to ensure the results are ready before reading them */
	ret = xe_guc_ct_send_block(ct, action, ARRAY_SIZE(action));
	if (ret)
		xe_gt_err(pc_to_gt(pc), "GuC PC query task state failed: %pe\n",
			  ERR_PTR(ret));

	return ret;
}

static int pc_action_set_param(struct xe_guc_pc *pc, u8 id, u32 value)
{
	struct xe_guc_ct *ct = pc_to_ct(pc);
	u32 action[] = {
		GUC_ACTION_HOST2GUC_PC_SLPC_REQUEST,
		SLPC_EVENT(SLPC_EVENT_PARAMETER_SET, 2),
		id,
		value,
	};
	int ret;

	if (wait_for_pc_state(pc, SLPC_GLOBAL_STATE_RUNNING,
			      SLPC_RESET_TIMEOUT_MS))
		return -EAGAIN;

	ret = xe_guc_ct_send(ct, action, ARRAY_SIZE(action), 0, 0);
	if (ret)
		xe_gt_err(pc_to_gt(pc), "GuC PC set param[%u]=%u failed: %pe\n",
			  id, value, ERR_PTR(ret));

	return ret;
}

static int pc_action_unset_param(struct xe_guc_pc *pc, u8 id)
{
	u32 action[] = {
		GUC_ACTION_HOST2GUC_PC_SLPC_REQUEST,
		SLPC_EVENT(SLPC_EVENT_PARAMETER_UNSET, 1),
		id,
	};
	struct xe_guc_ct *ct = &pc_to_guc(pc)->ct;
	int ret;

	if (wait_for_pc_state(pc, SLPC_GLOBAL_STATE_RUNNING,
			      SLPC_RESET_TIMEOUT_MS))
		return -EAGAIN;

	ret = xe_guc_ct_send(ct, action, ARRAY_SIZE(action), 0, 0);
	if (ret)
		xe_gt_err(pc_to_gt(pc), "GuC PC unset param failed: %pe",
			  ERR_PTR(ret));

	return ret;
}

static int pc_action_setup_gucrc(struct xe_guc_pc *pc, u32 mode)
{
	struct xe_guc_ct *ct = pc_to_ct(pc);
	u32 action[] = {
		GUC_ACTION_HOST2GUC_SETUP_PC_GUCRC,
		mode,
	};
	int ret;

	ret = xe_guc_ct_send(ct, action, ARRAY_SIZE(action), 0, 0);
	if (ret)
		xe_gt_err(pc_to_gt(pc), "GuC RC enable mode=%u failed: %pe\n",
			  mode, ERR_PTR(ret));
	return ret;
}

static u32 decode_freq(u32 raw)
{
	return DIV_ROUND_CLOSEST(raw * GT_FREQUENCY_MULTIPLIER,
				 GT_FREQUENCY_SCALER);
}

static u32 encode_freq(u32 freq)
{
	return DIV_ROUND_CLOSEST(freq * GT_FREQUENCY_SCALER,
				 GT_FREQUENCY_MULTIPLIER);
}

static u32 pc_get_min_freq(struct xe_guc_pc *pc)
{
	u32 freq;

	freq = FIELD_GET(SLPC_MIN_UNSLICE_FREQ_MASK,
			 slpc_shared_data_read(pc, task_state_data.freq));

	return decode_freq(freq);
}

static void pc_set_manual_rp_ctrl(struct xe_guc_pc *pc, bool enable)
{
	struct xe_gt *gt = pc_to_gt(pc);
	u32 state = enable ? RPSWCTL_ENABLE : RPSWCTL_DISABLE;

	/* Allow/Disallow punit to process software freq requests */
	xe_mmio_write32(&gt->mmio, RP_CONTROL, state);
}

static void pc_set_cur_freq(struct xe_guc_pc *pc, u32 freq)
{
	struct xe_gt *gt = pc_to_gt(pc);
	u32 rpnswreq;

	pc_set_manual_rp_ctrl(pc, true);

	/* Req freq is in units of 16.66 Mhz */
	rpnswreq = REG_FIELD_PREP(REQ_RATIO_MASK, encode_freq(freq));
	xe_mmio_write32(&gt->mmio, RPNSWREQ, rpnswreq);

	/* Sleep for a small time to allow pcode to respond */
	usleep_range(100, 300);

	pc_set_manual_rp_ctrl(pc, false);
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

static void mtl_update_rpa_value(struct xe_guc_pc *pc)
{
	struct xe_gt *gt = pc_to_gt(pc);
	u32 reg;

	if (xe_gt_is_media_type(gt))
		reg = xe_mmio_read32(&gt->mmio, MTL_MPA_FREQUENCY);
	else
		reg = xe_mmio_read32(&gt->mmio, MTL_GT_RPA_FREQUENCY);

	pc->rpa_freq = decode_freq(REG_FIELD_GET(MTL_RPA_MASK, reg));
}

static void mtl_update_rpe_value(struct xe_guc_pc *pc)
{
	struct xe_gt *gt = pc_to_gt(pc);
	u32 reg;

	if (xe_gt_is_media_type(gt))
		reg = xe_mmio_read32(&gt->mmio, MTL_MPE_FREQUENCY);
	else
		reg = xe_mmio_read32(&gt->mmio, MTL_GT_RPE_FREQUENCY);

	pc->rpe_freq = decode_freq(REG_FIELD_GET(MTL_RPE_MASK, reg));
}

static void tgl_update_rpa_value(struct xe_guc_pc *pc)
{
	struct xe_gt *gt = pc_to_gt(pc);
	struct xe_device *xe = gt_to_xe(gt);
	u32 reg;

	/*
	 * For PVC we still need to use fused RP0 as the approximation for RPa
	 * For other platforms than PVC we get the resolved RPa directly from
	 * PCODE at a different register
	 */
	if (xe->info.platform == XE_PVC) {
		reg = xe_mmio_read32(&gt->mmio, PVC_RP_STATE_CAP);
		pc->rpa_freq = REG_FIELD_GET(RP0_MASK, reg) * GT_FREQUENCY_MULTIPLIER;
	} else {
		reg = xe_mmio_read32(&gt->mmio, FREQ_INFO_REC);
		pc->rpa_freq = REG_FIELD_GET(RPA_MASK, reg) * GT_FREQUENCY_MULTIPLIER;
	}
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
	if (xe->info.platform == XE_PVC) {
		reg = xe_mmio_read32(&gt->mmio, PVC_RP_STATE_CAP);
		pc->rpe_freq = REG_FIELD_GET(RP1_MASK, reg) * GT_FREQUENCY_MULTIPLIER;
	} else {
		reg = xe_mmio_read32(&gt->mmio, FREQ_INFO_REC);
		pc->rpe_freq = REG_FIELD_GET(RPE_MASK, reg) * GT_FREQUENCY_MULTIPLIER;
	}
}

static void pc_update_rp_values(struct xe_guc_pc *pc)
{
	struct xe_gt *gt = pc_to_gt(pc);
	struct xe_device *xe = gt_to_xe(gt);

	if (GRAPHICS_VERx100(xe) >= 1270) {
		mtl_update_rpa_value(pc);
		mtl_update_rpe_value(pc);
	} else {
		tgl_update_rpa_value(pc);
		tgl_update_rpe_value(pc);
	}

	/*
	 * RPe is decided at runtime by PCODE. In the rare case where that's
	 * smaller than the fused min, we will trust the PCODE and use that
	 * as our minimum one.
	 */
	pc->rpn_freq = min(pc->rpn_freq, pc->rpe_freq);
}

/**
 * xe_guc_pc_get_act_freq - Get Actual running frequency
 * @pc: The GuC PC
 *
 * Returns: The Actual running frequency. Which might be 0 if GT is in Render-C sleep state (RC6).
 */
u32 xe_guc_pc_get_act_freq(struct xe_guc_pc *pc)
{
	struct xe_gt *gt = pc_to_gt(pc);
	struct xe_device *xe = gt_to_xe(gt);
	u32 freq;

	/* When in RC6, actual frequency reported will be 0. */
	if (GRAPHICS_VERx100(xe) >= 1270) {
		freq = xe_mmio_read32(&gt->mmio, MTL_MIRROR_TARGET_WP1);
		freq = REG_FIELD_GET(MTL_CAGF_MASK, freq);
	} else {
		freq = xe_mmio_read32(&gt->mmio, GT_PERF_STATUS);
		freq = REG_FIELD_GET(CAGF_MASK, freq);
	}

	freq = decode_freq(freq);

	return freq;
}

static u32 get_cur_freq(struct xe_gt *gt)
{
	u32 freq;

	freq = xe_mmio_read32(&gt->mmio, RPNSWREQ);
	freq = REG_FIELD_GET(REQ_RATIO_MASK, freq);
	return decode_freq(freq);
}

/**
 * xe_guc_pc_get_cur_freq - Get Current requested frequency
 * @pc: The GuC PC
 * @freq: A pointer to a u32 where the freq value will be returned
 *
 * Returns: 0 on success,
 *         -EAGAIN if GuC PC not ready (likely in middle of a reset).
 */
int xe_guc_pc_get_cur_freq(struct xe_guc_pc *pc, u32 *freq)
{
	struct xe_gt *gt = pc_to_gt(pc);
	unsigned int fw_ref;

	/*
	 * GuC SLPC plays with cur freq request when GuCRC is enabled
	 * Block RC6 for a more reliable read.
	 */
	fw_ref = xe_force_wake_get(gt_to_fw(gt), XE_FW_GT);
	if (!xe_force_wake_ref_has_domain(fw_ref, XE_FW_GT)) {
		xe_force_wake_put(gt_to_fw(gt), fw_ref);
		return -ETIMEDOUT;
	}

	*freq = get_cur_freq(gt);

	xe_force_wake_put(gt_to_fw(gt), fw_ref);
	return 0;
}

/**
 * xe_guc_pc_get_rp0_freq - Get the RP0 freq
 * @pc: The GuC PC
 *
 * Returns: RP0 freq.
 */
u32 xe_guc_pc_get_rp0_freq(struct xe_guc_pc *pc)
{
	return pc->rp0_freq;
}

/**
 * xe_guc_pc_get_rpa_freq - Get the RPa freq
 * @pc: The GuC PC
 *
 * Returns: RPa freq.
 */
u32 xe_guc_pc_get_rpa_freq(struct xe_guc_pc *pc)
{
	pc_update_rp_values(pc);

	return pc->rpa_freq;
}

/**
 * xe_guc_pc_get_rpe_freq - Get the RPe freq
 * @pc: The GuC PC
 *
 * Returns: RPe freq.
 */
u32 xe_guc_pc_get_rpe_freq(struct xe_guc_pc *pc)
{
	pc_update_rp_values(pc);

	return pc->rpe_freq;
}

/**
 * xe_guc_pc_get_rpn_freq - Get the RPn freq
 * @pc: The GuC PC
 *
 * Returns: RPn freq.
 */
u32 xe_guc_pc_get_rpn_freq(struct xe_guc_pc *pc)
{
	return pc->rpn_freq;
}

/**
 * xe_guc_pc_get_min_freq - Get the min operational frequency
 * @pc: The GuC PC
 * @freq: A pointer to a u32 where the freq value will be returned
 *
 * Returns: 0 on success,
 *         -EAGAIN if GuC PC not ready (likely in middle of a reset).
 */
int xe_guc_pc_get_min_freq(struct xe_guc_pc *pc, u32 *freq)
{
	int ret;

	xe_device_assert_mem_access(pc_to_xe(pc));

	mutex_lock(&pc->freq_lock);
	if (!pc->freq_ready) {
		/* Might be in the middle of a gt reset */
		ret = -EAGAIN;
		goto out;
	}

	ret = pc_action_query_task_state(pc);
	if (ret)
		goto out;

	*freq = pc_get_min_freq(pc);

out:
	mutex_unlock(&pc->freq_lock);
	return ret;
}

/**
 * xe_guc_pc_set_min_freq - Set the minimal operational frequency
 * @pc: The GuC PC
 * @freq: The selected minimal frequency
 *
 * Returns: 0 on success,
 *         -EAGAIN if GuC PC not ready (likely in middle of a reset),
 *         -EINVAL if value out of bounds.
 */
int xe_guc_pc_set_min_freq(struct xe_guc_pc *pc, u32 freq)
{
	int ret;

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
	return ret;
}

/**
 * xe_guc_pc_get_max_freq - Get Maximum operational frequency
 * @pc: The GuC PC
 * @freq: A pointer to a u32 where the freq value will be returned
 *
 * Returns: 0 on success,
 *         -EAGAIN if GuC PC not ready (likely in middle of a reset).
 */
int xe_guc_pc_get_max_freq(struct xe_guc_pc *pc, u32 *freq)
{
	int ret;

	mutex_lock(&pc->freq_lock);
	if (!pc->freq_ready) {
		/* Might be in the middle of a gt reset */
		ret = -EAGAIN;
		goto out;
	}

	ret = pc_action_query_task_state(pc);
	if (ret)
		goto out;

	*freq = pc_get_max_freq(pc);

out:
	mutex_unlock(&pc->freq_lock);
	return ret;
}

/**
 * xe_guc_pc_set_max_freq - Set the maximum operational frequency
 * @pc: The GuC PC
 * @freq: The selected maximum frequency value
 *
 * Returns: 0 on success,
 *         -EAGAIN if GuC PC not ready (likely in middle of a reset),
 *         -EINVAL if value out of bounds.
 */
int xe_guc_pc_set_max_freq(struct xe_guc_pc *pc, u32 freq)
{
	int ret;

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
	return ret;
}

/**
 * xe_guc_pc_c_status - get the current GT C state
 * @pc: XE_GuC_PC instance
 */
enum xe_gt_idle_state xe_guc_pc_c_status(struct xe_guc_pc *pc)
{
	struct xe_gt *gt = pc_to_gt(pc);
	u32 reg, gt_c_state;

	if (GRAPHICS_VERx100(gt_to_xe(gt)) >= 1270) {
		reg = xe_mmio_read32(&gt->mmio, MTL_MIRROR_TARGET_WP1);
		gt_c_state = REG_FIELD_GET(MTL_CC_MASK, reg);
	} else {
		reg = xe_mmio_read32(&gt->mmio, GT_CORE_STATUS);
		gt_c_state = REG_FIELD_GET(RCN_MASK, reg);
	}

	switch (gt_c_state) {
	case GT_C6:
		return GT_IDLE_C6;
	case GT_C0:
		return GT_IDLE_C0;
	default:
		return GT_IDLE_UNKNOWN;
	}
}

/**
 * xe_guc_pc_rc6_residency - rc6 residency counter
 * @pc: Xe_GuC_PC instance
 */
u64 xe_guc_pc_rc6_residency(struct xe_guc_pc *pc)
{
	struct xe_gt *gt = pc_to_gt(pc);
	u32 reg;

	reg = xe_mmio_read32(&gt->mmio, GT_GFX_RC6);

	return reg;
}

/**
 * xe_guc_pc_mc6_residency - mc6 residency counter
 * @pc: Xe_GuC_PC instance
 */
u64 xe_guc_pc_mc6_residency(struct xe_guc_pc *pc)
{
	struct xe_gt *gt = pc_to_gt(pc);
	u64 reg;

	reg = xe_mmio_read32(&gt->mmio, MTL_MEDIA_MC6);

	return reg;
}

static void mtl_init_fused_rp_values(struct xe_guc_pc *pc)
{
	struct xe_gt *gt = pc_to_gt(pc);
	u32 reg;

	xe_device_assert_mem_access(pc_to_xe(pc));

	if (xe_gt_is_media_type(gt))
		reg = xe_mmio_read32(&gt->mmio, MTL_MEDIAP_STATE_CAP);
	else
		reg = xe_mmio_read32(&gt->mmio, MTL_RP_STATE_CAP);

	pc->rp0_freq = decode_freq(REG_FIELD_GET(MTL_RP0_CAP_MASK, reg));

	pc->rpn_freq = decode_freq(REG_FIELD_GET(MTL_RPN_CAP_MASK, reg));
}

static void tgl_init_fused_rp_values(struct xe_guc_pc *pc)
{
	struct xe_gt *gt = pc_to_gt(pc);
	struct xe_device *xe = gt_to_xe(gt);
	u32 reg;

	xe_device_assert_mem_access(pc_to_xe(pc));

	if (xe->info.platform == XE_PVC)
		reg = xe_mmio_read32(&gt->mmio, PVC_RP_STATE_CAP);
	else
		reg = xe_mmio_read32(&gt->mmio, RP_STATE_CAP);
	pc->rp0_freq = REG_FIELD_GET(RP0_MASK, reg) * GT_FREQUENCY_MULTIPLIER;
	pc->rpn_freq = REG_FIELD_GET(RPN_MASK, reg) * GT_FREQUENCY_MULTIPLIER;
}

static void pc_init_fused_rp_values(struct xe_guc_pc *pc)
{
	struct xe_gt *gt = pc_to_gt(pc);
	struct xe_device *xe = gt_to_xe(gt);

	if (GRAPHICS_VERx100(xe) >= 1270)
		mtl_init_fused_rp_values(pc);
	else
		tgl_init_fused_rp_values(pc);
}

static u32 pc_max_freq_cap(struct xe_guc_pc *pc)
{
	struct xe_gt *gt = pc_to_gt(pc);

	if (XE_WA(gt, 22019338487)) {
		if (xe_gt_is_media_type(gt))
			return min(LNL_MERT_FREQ_CAP, pc->rp0_freq);
		else
			return min(BMG_MERT_FREQ_CAP, pc->rp0_freq);
	} else {
		return pc->rp0_freq;
	}
}

/**
 * xe_guc_pc_raise_unslice - Initialize RPx values and request a higher GT
 * frequency to allow faster GuC load times
 * @pc: Xe_GuC_PC instance
 */
void xe_guc_pc_raise_unslice(struct xe_guc_pc *pc)
{
	struct xe_gt *gt = pc_to_gt(pc);

	xe_force_wake_assert_held(gt_to_fw(gt), XE_FW_GT);
	pc_set_cur_freq(pc, pc_max_freq_cap(pc));
}

/**
 * xe_guc_pc_init_early - Initialize RPx values
 * @pc: Xe_GuC_PC instance
 */
void xe_guc_pc_init_early(struct xe_guc_pc *pc)
{
	struct xe_gt *gt = pc_to_gt(pc);

	xe_force_wake_assert_held(gt_to_fw(gt), XE_FW_GT);
	pc_init_fused_rp_values(pc);
}

static int pc_adjust_freq_bounds(struct xe_guc_pc *pc)
{
	int ret;

	lockdep_assert_held(&pc->freq_lock);

	ret = pc_action_query_task_state(pc);
	if (ret)
		goto out;

	/*
	 * GuC defaults to some RPmax that is not actually achievable without
	 * overclocking. Let's adjust it to the Hardware RP0, which is the
	 * regular maximum
	 */
	if (pc_get_max_freq(pc) > pc->rp0_freq) {
		ret = pc_set_max_freq(pc, pc->rp0_freq);
		if (ret)
			goto out;
	}

	/*
	 * Same thing happens for Server platforms where min is listed as
	 * RPMax
	 */
	if (pc_get_min_freq(pc) > pc->rp0_freq)
		ret = pc_set_min_freq(pc, pc->rp0_freq);

out:
	return ret;
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

static int pc_set_mert_freq_cap(struct xe_guc_pc *pc)
{
	int ret = 0;

	if (XE_WA(pc_to_gt(pc), 22019338487)) {
		/*
		 * Get updated min/max and stash them.
		 */
		ret = xe_guc_pc_get_min_freq(pc, &pc->stashed_min_freq);
		if (!ret)
			ret = xe_guc_pc_get_max_freq(pc, &pc->stashed_max_freq);
		if (ret)
			return ret;

		/*
		 * Ensure min and max are bound by MERT_FREQ_CAP until driver loads.
		 */
		mutex_lock(&pc->freq_lock);
		ret = pc_set_min_freq(pc, min(pc->rpe_freq, pc_max_freq_cap(pc)));
		if (!ret)
			ret = pc_set_max_freq(pc, min(pc->rp0_freq, pc_max_freq_cap(pc)));
		mutex_unlock(&pc->freq_lock);
	}

	return ret;
}

/**
 * xe_guc_pc_restore_stashed_freq - Set min/max back to stashed values
 * @pc: The GuC PC
 *
 * Returns: 0 on success,
 *          error code on failure
 */
int xe_guc_pc_restore_stashed_freq(struct xe_guc_pc *pc)
{
	int ret = 0;

	if (IS_SRIOV_VF(pc_to_xe(pc)) || pc_to_xe(pc)->info.skip_guc_pc)
		return 0;

	mutex_lock(&pc->freq_lock);
	ret = pc_set_max_freq(pc, pc->stashed_max_freq);
	if (!ret)
		ret = pc_set_min_freq(pc, pc->stashed_min_freq);
	mutex_unlock(&pc->freq_lock);

	return ret;
}

/**
 * xe_guc_pc_gucrc_disable - Disable GuC RC
 * @pc: Xe_GuC_PC instance
 *
 * Disables GuC RC by taking control of RC6 back from GuC.
 *
 * Return: 0 on success, negative error code on error.
 */
int xe_guc_pc_gucrc_disable(struct xe_guc_pc *pc)
{
	struct xe_device *xe = pc_to_xe(pc);
	struct xe_gt *gt = pc_to_gt(pc);
	unsigned int fw_ref;
	int ret = 0;

	if (xe->info.skip_guc_pc)
		return 0;

	ret = pc_action_setup_gucrc(pc, GUCRC_HOST_CONTROL);
	if (ret)
		return ret;

	fw_ref = xe_force_wake_get(gt_to_fw(gt), XE_FORCEWAKE_ALL);
	if (!xe_force_wake_ref_has_domain(fw_ref, XE_FORCEWAKE_ALL)) {
		xe_force_wake_put(gt_to_fw(gt), fw_ref);
		return -ETIMEDOUT;
	}

	xe_gt_idle_disable_c6(gt);

	xe_force_wake_put(gt_to_fw(gt), fw_ref);

	return 0;
}

/**
 * xe_guc_pc_override_gucrc_mode - override GUCRC mode
 * @pc: Xe_GuC_PC instance
 * @mode: new value of the mode.
 *
 * Return: 0 on success, negative error code on error
 */
int xe_guc_pc_override_gucrc_mode(struct xe_guc_pc *pc, enum slpc_gucrc_mode mode)
{
	int ret;

	xe_pm_runtime_get(pc_to_xe(pc));
	ret = pc_action_set_param(pc, SLPC_PARAM_PWRGATE_RC_MODE, mode);
	xe_pm_runtime_put(pc_to_xe(pc));

	return ret;
}

/**
 * xe_guc_pc_unset_gucrc_mode - unset GUCRC mode override
 * @pc: Xe_GuC_PC instance
 *
 * Return: 0 on success, negative error code on error
 */
int xe_guc_pc_unset_gucrc_mode(struct xe_guc_pc *pc)
{
	int ret;

	xe_pm_runtime_get(pc_to_xe(pc));
	ret = pc_action_unset_param(pc, SLPC_PARAM_PWRGATE_RC_MODE);
	xe_pm_runtime_put(pc_to_xe(pc));

	return ret;
}

static void pc_init_pcode_freq(struct xe_guc_pc *pc)
{
	u32 min = DIV_ROUND_CLOSEST(pc->rpn_freq, GT_FREQUENCY_MULTIPLIER);
	u32 max = DIV_ROUND_CLOSEST(pc->rp0_freq, GT_FREQUENCY_MULTIPLIER);

	XE_WARN_ON(xe_pcode_init_min_freq_table(gt_to_tile(pc_to_gt(pc)), min, max));
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

static int pc_action_set_strategy(struct xe_guc_pc *pc, u32 val)
{
	int ret = 0;

	ret = pc_action_set_param(pc,
				  SLPC_PARAM_STRATEGIES,
				  val);

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
	unsigned int fw_ref;
	ktime_t earlier;
	int ret;

	xe_gt_assert(gt, xe_device_uc_enabled(xe));

	fw_ref = xe_force_wake_get(gt_to_fw(gt), XE_FW_GT);
	if (!xe_force_wake_ref_has_domain(fw_ref, XE_FW_GT)) {
		xe_force_wake_put(gt_to_fw(gt), fw_ref);
		return -ETIMEDOUT;
	}

	if (xe->info.skip_guc_pc) {
		if (xe->info.platform != XE_PVC)
			xe_gt_idle_enable_c6(gt);

		/* Request max possible since dynamic freq mgmt is not enabled */
		pc_set_cur_freq(pc, UINT_MAX);

		ret = 0;
		goto out;
	}

	memset(pc->bo->vmap.vaddr, 0, size);
	slpc_shared_data_write(pc, header.size, size);

	earlier = ktime_get();
	ret = pc_action_reset(pc);
	if (ret)
		goto out;

	if (wait_for_pc_state(pc, SLPC_GLOBAL_STATE_RUNNING,
			      SLPC_RESET_TIMEOUT_MS)) {
		xe_gt_warn(gt, "GuC PC start taking longer than normal [freq = %dMHz (req = %dMHz), perf_limit_reasons = 0x%08X]\n",
			   xe_guc_pc_get_act_freq(pc), get_cur_freq(gt),
			   xe_gt_throttle_get_limit_reasons(gt));

		if (wait_for_pc_state(pc, SLPC_GLOBAL_STATE_RUNNING,
				      SLPC_RESET_EXTENDED_TIMEOUT_MS)) {
			xe_gt_err(gt, "GuC PC Start failed: Dynamic GT frequency control and GT sleep states are now disabled.\n");
			goto out;
		}

		xe_gt_warn(gt, "GuC PC excessive start time: %lldms",
			   ktime_ms_delta(ktime_get(), earlier));
	}

	ret = pc_init_freqs(pc);
	if (ret)
		goto out;

	ret = pc_set_mert_freq_cap(pc);
	if (ret)
		goto out;

	if (xe->info.platform == XE_PVC) {
		xe_guc_pc_gucrc_disable(pc);
		ret = 0;
		goto out;
	}

	ret = pc_action_setup_gucrc(pc, GUCRC_FIRMWARE_CONTROL);
	if (ret)
		goto out;

	/* Enable SLPC Optimized Strategy for compute */
	ret = pc_action_set_strategy(pc, SLPC_OPTIMIZED_STRATEGY_COMPUTE);

out:
	xe_force_wake_put(gt_to_fw(gt), fw_ref);
	return ret;
}

/**
 * xe_guc_pc_stop - Stop GuC's Power Conservation component
 * @pc: Xe_GuC_PC instance
 */
int xe_guc_pc_stop(struct xe_guc_pc *pc)
{
	struct xe_device *xe = pc_to_xe(pc);

	if (xe->info.skip_guc_pc) {
		xe_gt_idle_disable_c6(pc_to_gt(pc));
		return 0;
	}

	mutex_lock(&pc->freq_lock);
	pc->freq_ready = false;
	mutex_unlock(&pc->freq_lock);

	return 0;
}

/**
 * xe_guc_pc_fini_hw - Finalize GuC's Power Conservation component
 * @arg: opaque pointer that should point to Xe_GuC_PC instance
 */
static void xe_guc_pc_fini_hw(void *arg)
{
	struct xe_guc_pc *pc = arg;
	struct xe_device *xe = pc_to_xe(pc);
	unsigned int fw_ref;

	if (xe_device_wedged(xe))
		return;

	fw_ref = xe_force_wake_get(gt_to_fw(pc_to_gt(pc)), XE_FORCEWAKE_ALL);
	xe_guc_pc_gucrc_disable(pc);
	XE_WARN_ON(xe_guc_pc_stop(pc));

	/* Bind requested freq to mert_freq_cap before unload */
	pc_set_cur_freq(pc, min(pc_max_freq_cap(pc), pc->rpe_freq));

	xe_force_wake_put(gt_to_fw(pc_to_gt(pc)), fw_ref);
}

/**
 * xe_guc_pc_init - Initialize GuC's Power Conservation component
 * @pc: Xe_GuC_PC instance
 */
int xe_guc_pc_init(struct xe_guc_pc *pc)
{
	struct xe_gt *gt = pc_to_gt(pc);
	struct xe_tile *tile = gt_to_tile(gt);
	struct xe_device *xe = gt_to_xe(gt);
	struct xe_bo *bo;
	u32 size = PAGE_ALIGN(sizeof(struct slpc_shared_data));
	int err;

	if (xe->info.skip_guc_pc)
		return 0;

	err = drmm_mutex_init(&xe->drm, &pc->freq_lock);
	if (err)
		return err;

	bo = xe_managed_bo_create_pin_map(xe, tile, size,
					  XE_BO_FLAG_VRAM_IF_DGFX(tile) |
					  XE_BO_FLAG_GGTT |
					  XE_BO_FLAG_GGTT_INVALIDATE);
	if (IS_ERR(bo))
		return PTR_ERR(bo);

	pc->bo = bo;

	return devm_add_action_or_reset(xe->drm.dev, xe_guc_pc_fini_hw, pc);
}

static const char *pc_get_state_string(struct xe_guc_pc *pc)
{
	switch (slpc_shared_data_read(pc, header.global_state)) {
	case SLPC_GLOBAL_STATE_NOT_RUNNING:
		return "not running";
	case SLPC_GLOBAL_STATE_INITIALIZING:
		return "initializing";
	case SLPC_GLOBAL_STATE_RESETTING:
		return "resetting";
	case SLPC_GLOBAL_STATE_RUNNING:
		return "running";
	case SLPC_GLOBAL_STATE_SHUTTING_DOWN:
		return "shutting down";
	case SLPC_GLOBAL_STATE_ERROR:
		return "error";
	default:
		return "unknown";
	}
}

/**
 * xe_guc_pc_print - Print GuC's Power Conservation information for debug
 * @pc: Xe_GuC_PC instance
 * @p: drm_printer
 */
void xe_guc_pc_print(struct xe_guc_pc *pc, struct drm_printer *p)
{
	drm_printf(p, "SLPC Shared Data Header:\n");
	drm_printf(p, "\tSize: %x\n", slpc_shared_data_read(pc, header.size));
	drm_printf(p, "\tGlobal State: %s\n", pc_get_state_string(pc));

	if (pc_action_query_task_state(pc))
		return;

	drm_printf(p, "\nSLPC Tasks Status:\n");
	drm_printf(p, "\tGTPERF enabled: %s\n",
		   str_yes_no(slpc_shared_data_read(pc, task_state_data.status) &
			      SLPC_GTPERF_TASK_ENABLED));
	drm_printf(p, "\tDCC enabled: %s\n",
		   str_yes_no(slpc_shared_data_read(pc, task_state_data.status) &
			      SLPC_DCC_TASK_ENABLED));
	drm_printf(p, "\tDCC in use: %s\n",
		   str_yes_no(slpc_shared_data_read(pc, task_state_data.status) &
			      SLPC_IN_DCC));
	drm_printf(p, "\tBalancer enabled: %s\n",
		   str_yes_no(slpc_shared_data_read(pc, task_state_data.status) &
			      SLPC_BALANCER_ENABLED));
	drm_printf(p, "\tIBC enabled: %s\n",
		   str_yes_no(slpc_shared_data_read(pc, task_state_data.status) &
			      SLPC_IBC_TASK_ENABLED));
	drm_printf(p, "\tBalancer IA LMT enabled: %s\n",
		   str_yes_no(slpc_shared_data_read(pc, task_state_data.status) &
			      SLPC_BALANCER_IA_LMT_ENABLED));
	drm_printf(p, "\tBalancer IA LMT active: %s\n",
		   str_yes_no(slpc_shared_data_read(pc, task_state_data.status) &
			      SLPC_BALANCER_IA_LMT_ACTIVE));
}
