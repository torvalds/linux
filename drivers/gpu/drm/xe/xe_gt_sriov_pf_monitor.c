// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023-2024 Intel Corporation
 */

#include "abi/guc_actions_sriov_abi.h"
#include "abi/guc_messages_abi.h"

#include "xe_gt_sriov_pf_config.h"
#include "xe_gt_sriov_pf_helpers.h"
#include "xe_gt_sriov_pf_monitor.h"
#include "xe_gt_sriov_printk.h"
#include "xe_guc_klv_helpers.h"
#include "xe_guc_klv_thresholds_set.h"

/**
 * xe_gt_sriov_pf_monitor_flr - Cleanup VF data after VF FLR.
 * @gt: the &xe_gt
 * @vfid: the VF identifier
 *
 * On FLR this function will reset all event data related to the VF.
 * This function is for PF only.
 */
void xe_gt_sriov_pf_monitor_flr(struct xe_gt *gt, u32 vfid)
{
	int e;

	xe_gt_assert(gt, IS_SRIOV_PF(gt_to_xe(gt)));
	xe_gt_sriov_pf_assert_vfid(gt, vfid);

	for (e = 0; e < XE_GUC_KLV_NUM_THRESHOLDS; e++)
		gt->sriov.pf.vfs[vfid].monitor.guc.events[e] = 0;
}

static void pf_update_event_counter(struct xe_gt *gt, u32 vfid,
				    enum xe_guc_klv_threshold_index e)
{
	xe_gt_assert(gt, IS_SRIOV_PF(gt_to_xe(gt)));
	xe_gt_assert(gt, e < XE_GUC_KLV_NUM_THRESHOLDS);

	gt->sriov.pf.vfs[vfid].monitor.guc.events[e]++;
}

static int pf_handle_vf_threshold_event(struct xe_gt *gt, u32 vfid, u32 threshold)
{
	char origin[8];
	int e;

	e = xe_guc_klv_threshold_key_to_index(threshold);
	xe_sriov_function_name(vfid, origin, sizeof(origin));

	/* was there a new KEY added that we missed? */
	if (unlikely(e < 0)) {
		xe_gt_sriov_notice(gt, "unknown threshold key %#x reported for %s\n",
				   threshold, origin);
		return -ENOTCONN;
	}

	xe_gt_sriov_dbg(gt, "%s exceeded threshold %u %s\n",
			origin, xe_gt_sriov_pf_config_get_threshold(gt, vfid, e),
			xe_guc_klv_key_to_string(threshold));

	pf_update_event_counter(gt, vfid, e);

	return 0;
}

/**
 * xe_gt_sriov_pf_monitor_process_guc2pf - Handle adverse event notification from the GuC.
 * @gt: the &xe_gt
 * @msg: G2H event message
 * @len: length of the message
 *
 * This function is intended for PF only.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_gt_sriov_pf_monitor_process_guc2pf(struct xe_gt *gt, const u32 *msg, u32 len)
{
	struct xe_device *xe = gt_to_xe(gt);
	u32 vfid;
	u32 threshold;

	xe_gt_assert(gt, len >= GUC_HXG_MSG_MIN_LEN);
	xe_gt_assert(gt, FIELD_GET(GUC_HXG_MSG_0_ORIGIN, msg[0]) == GUC_HXG_ORIGIN_GUC);
	xe_gt_assert(gt, FIELD_GET(GUC_HXG_MSG_0_TYPE, msg[0]) == GUC_HXG_TYPE_EVENT);
	xe_gt_assert(gt, FIELD_GET(GUC_HXG_EVENT_MSG_0_ACTION, msg[0]) ==
		     GUC_ACTION_GUC2PF_ADVERSE_EVENT);

	if (unlikely(!IS_SRIOV_PF(xe)))
		return -EPROTO;

	if (unlikely(FIELD_GET(GUC2PF_ADVERSE_EVENT_EVENT_MSG_0_MBZ, msg[0])))
		return -EPFNOSUPPORT;

	if (unlikely(len < GUC2PF_ADVERSE_EVENT_EVENT_MSG_LEN))
		return -EPROTO;

	vfid = FIELD_GET(GUC2PF_ADVERSE_EVENT_EVENT_MSG_1_VFID, msg[1]);
	threshold = FIELD_GET(GUC2PF_ADVERSE_EVENT_EVENT_MSG_2_THRESHOLD, msg[2]);

	if (unlikely(vfid > xe_gt_sriov_pf_get_totalvfs(gt)))
		return -EINVAL;

	return pf_handle_vf_threshold_event(gt, vfid, threshold);
}

/**
 * xe_gt_sriov_pf_monitor_print_events - Print adverse events counters.
 * @gt: the &xe_gt to print events from
 * @p: the &drm_printer
 *
 * Print adverse events counters for all VFs.
 * VFs with no events are not printed.
 *
 * This function can only be called on PF.
 */
void xe_gt_sriov_pf_monitor_print_events(struct xe_gt *gt, struct drm_printer *p)
{
	unsigned int n, total_vfs = xe_gt_sriov_pf_get_totalvfs(gt);
	const struct xe_gt_sriov_monitor *data;
	int e;

	xe_gt_assert(gt, IS_SRIOV_PF(gt_to_xe(gt)));

	for (n = 1; n <= total_vfs; n++) {
		data = &gt->sriov.pf.vfs[n].monitor;

		for (e = 0; e < XE_GUC_KLV_NUM_THRESHOLDS; e++)
			if (data->guc.events[e])
				break;

		/* skip empty unless in debug mode */
		if (e >= XE_GUC_KLV_NUM_THRESHOLDS &&
		    !IS_ENABLED(CONFIG_DRM_XE_DEBUG_SRIOV))
			continue;

#define __format(...) "%s:%u "
#define __value(TAG, NAME, ...) , #NAME, data->guc.events[MAKE_XE_GUC_KLV_THRESHOLD_INDEX(TAG)]

		drm_printf(p, "VF%u:\t" MAKE_XE_GUC_KLV_THRESHOLDS_SET(__format) "\n",
			   n MAKE_XE_GUC_KLV_THRESHOLDS_SET(__value));

#undef __format
#undef __value
	}
}
