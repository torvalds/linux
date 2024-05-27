// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023-2024 Intel Corporation
 */

#include "abi/guc_actions_sriov_abi.h"

#include "xe_device.h"
#include "xe_gt.h"
#include "xe_gt_sriov_pf_control.h"
#include "xe_gt_sriov_printk.h"
#include "xe_guc_ct.h"
#include "xe_sriov.h"

static const char *control_cmd_to_string(u32 cmd)
{
	switch (cmd) {
	case GUC_PF_TRIGGER_VF_PAUSE:
		return "PAUSE";
	case GUC_PF_TRIGGER_VF_RESUME:
		return "RESUME";
	case GUC_PF_TRIGGER_VF_STOP:
		return "STOP";
	case GUC_PF_TRIGGER_VF_FLR_START:
		return "FLR_START";
	case GUC_PF_TRIGGER_VF_FLR_FINISH:
		return "FLR_FINISH";
	default:
		return "<unknown>";
	}
}

static int guc_action_vf_control_cmd(struct xe_guc *guc, u32 vfid, u32 cmd)
{
	u32 request[PF2GUC_VF_CONTROL_REQUEST_MSG_LEN] = {
		FIELD_PREP(GUC_HXG_MSG_0_ORIGIN, GUC_HXG_ORIGIN_HOST) |
		FIELD_PREP(GUC_HXG_MSG_0_TYPE, GUC_HXG_TYPE_REQUEST) |
		FIELD_PREP(GUC_HXG_REQUEST_MSG_0_ACTION, GUC_ACTION_PF2GUC_VF_CONTROL),
		FIELD_PREP(PF2GUC_VF_CONTROL_REQUEST_MSG_1_VFID, vfid),
		FIELD_PREP(PF2GUC_VF_CONTROL_REQUEST_MSG_2_COMMAND, cmd),
	};
	int ret;

	/* XXX those two commands are now sent from the G2H handler */
	if (cmd == GUC_PF_TRIGGER_VF_FLR_START || cmd == GUC_PF_TRIGGER_VF_FLR_FINISH)
		return xe_guc_ct_send_g2h_handler(&guc->ct, request, ARRAY_SIZE(request));

	ret = xe_guc_ct_send_block(&guc->ct, request, ARRAY_SIZE(request));
	return ret > 0 ? -EPROTO : ret;
}

static int pf_send_vf_control_cmd(struct xe_gt *gt, unsigned int vfid, u32 cmd)
{
	int err;

	xe_gt_assert(gt, vfid != PFID);

	err = guc_action_vf_control_cmd(&gt->uc.guc, vfid, cmd);
	if (unlikely(err))
		xe_gt_sriov_err(gt, "VF%u control command %s failed (%pe)\n",
				vfid, control_cmd_to_string(cmd), ERR_PTR(err));
	return err;
}

static int pf_send_vf_pause(struct xe_gt *gt, unsigned int vfid)
{
	return pf_send_vf_control_cmd(gt, vfid, GUC_PF_TRIGGER_VF_PAUSE);
}

static int pf_send_vf_resume(struct xe_gt *gt, unsigned int vfid)
{
	return pf_send_vf_control_cmd(gt, vfid, GUC_PF_TRIGGER_VF_RESUME);
}

static int pf_send_vf_stop(struct xe_gt *gt, unsigned int vfid)
{
	return pf_send_vf_control_cmd(gt, vfid, GUC_PF_TRIGGER_VF_STOP);
}

static int pf_send_vf_flr_start(struct xe_gt *gt, unsigned int vfid)
{
	return pf_send_vf_control_cmd(gt, vfid, GUC_PF_TRIGGER_VF_FLR_START);
}

static int pf_send_vf_flr_finish(struct xe_gt *gt, unsigned int vfid)
{
	return pf_send_vf_control_cmd(gt, vfid, GUC_PF_TRIGGER_VF_FLR_FINISH);
}

/**
 * xe_gt_sriov_pf_control_pause_vf - Pause a VF.
 * @gt: the &xe_gt
 * @vfid: the VF identifier
 *
 * This function is for PF only.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_gt_sriov_pf_control_pause_vf(struct xe_gt *gt, unsigned int vfid)
{
	return pf_send_vf_pause(gt, vfid);
}

/**
 * xe_gt_sriov_pf_control_resume_vf - Resume a VF.
 * @gt: the &xe_gt
 * @vfid: the VF identifier
 *
 * This function is for PF only.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_gt_sriov_pf_control_resume_vf(struct xe_gt *gt, unsigned int vfid)
{
	return pf_send_vf_resume(gt, vfid);
}

/**
 * xe_gt_sriov_pf_control_stop_vf - Stop a VF.
 * @gt: the &xe_gt
 * @vfid: the VF identifier
 *
 * This function is for PF only.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_gt_sriov_pf_control_stop_vf(struct xe_gt *gt, unsigned int vfid)
{
	return pf_send_vf_stop(gt, vfid);
}

/**
 * DOC: The VF FLR Flow with GuC
 *
 *          PF                        GUC             PCI
 * ========================================================
 *          |                          |               |
 * (1)      |                         [ ] <----- FLR --|
 *          |                         [ ]              :
 * (2)     [ ] <-------- NOTIFY FLR --[ ]
 *         [ ]                         |
 * (3)     [ ]                         |
 *         [ ]                         |
 *         [ ]-- START FLR ---------> [ ]
 *          |                         [ ]
 * (4)      |                         [ ]
 *          |                         [ ]
 *         [ ] <--------- FLR DONE -- [ ]
 *         [ ]                         |
 * (5)     [ ]                         |
 *         [ ]                         |
 *         [ ]-- FINISH FLR --------> [ ]
 *          |                          |
 *
 * Step 1: PCI HW generates interrupt to the GuC about VF FLR
 * Step 2: GuC FW sends G2H notification to the PF about VF FLR
 * Step 2a: on some platforms G2H is only received from root GuC
 * Step 3: PF sends H2G request to the GuC to start VF FLR sequence
 * Step 3a: on some platforms PF must send H2G to all other GuCs
 * Step 4: GuC FW performs VF FLR cleanups and notifies the PF when done
 * Step 5: PF performs VF FLR cleanups and notifies the GuC FW when finished
 */

static bool needs_dispatch_flr(struct xe_device *xe)
{
	return xe->info.platform == XE_PVC;
}

static void pf_handle_vf_flr(struct xe_gt *gt, u32 vfid)
{
	struct xe_device *xe = gt_to_xe(gt);
	struct xe_gt *gtit;
	unsigned int gtid;

	xe_gt_sriov_info(gt, "VF%u FLR\n", vfid);

	if (needs_dispatch_flr(xe)) {
		for_each_gt(gtit, xe, gtid)
			pf_send_vf_flr_start(gtit, vfid);
	} else {
		pf_send_vf_flr_start(gt, vfid);
	}
}

static void pf_handle_vf_flr_done(struct xe_gt *gt, u32 vfid)
{
	pf_send_vf_flr_finish(gt, vfid);
}

static int pf_handle_vf_event(struct xe_gt *gt, u32 vfid, u32 eventid)
{
	switch (eventid) {
	case GUC_PF_NOTIFY_VF_FLR:
		pf_handle_vf_flr(gt, vfid);
		break;
	case GUC_PF_NOTIFY_VF_FLR_DONE:
		pf_handle_vf_flr_done(gt, vfid);
		break;
	case GUC_PF_NOTIFY_VF_PAUSE_DONE:
		break;
	case GUC_PF_NOTIFY_VF_FIXUP_DONE:
		break;
	default:
		return -ENOPKG;
	}
	return 0;
}

static int pf_handle_pf_event(struct xe_gt *gt, u32 eventid)
{
	switch (eventid) {
	case GUC_PF_NOTIFY_VF_ENABLE:
		xe_gt_sriov_dbg_verbose(gt, "VFs %s/%s\n",
					str_enabled_disabled(true),
					str_enabled_disabled(false));
		break;
	default:
		return -ENOPKG;
	}
	return 0;
}

/**
 * xe_gt_sriov_pf_control_process_guc2pf - Handle VF state notification from GuC.
 * @gt: the &xe_gt
 * @msg: the G2H message
 * @len: the length of the G2H message
 *
 * This function is for PF only.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_gt_sriov_pf_control_process_guc2pf(struct xe_gt *gt, const u32 *msg, u32 len)
{
	u32 vfid;
	u32 eventid;

	xe_gt_assert(gt, len);
	xe_gt_assert(gt, FIELD_GET(GUC_HXG_MSG_0_ORIGIN, msg[0]) == GUC_HXG_ORIGIN_GUC);
	xe_gt_assert(gt, FIELD_GET(GUC_HXG_MSG_0_TYPE, msg[0]) == GUC_HXG_TYPE_EVENT);
	xe_gt_assert(gt, FIELD_GET(GUC_HXG_EVENT_MSG_0_ACTION, msg[0]) ==
		     GUC_ACTION_GUC2PF_VF_STATE_NOTIFY);

	if (unlikely(!xe_device_is_sriov_pf(gt_to_xe(gt))))
		return -EPROTO;

	if (unlikely(FIELD_GET(GUC2PF_VF_STATE_NOTIFY_EVENT_MSG_0_MBZ, msg[0])))
		return -EPFNOSUPPORT;

	if (unlikely(len != GUC2PF_VF_STATE_NOTIFY_EVENT_MSG_LEN))
		return -EPROTO;

	vfid = FIELD_GET(GUC2PF_VF_STATE_NOTIFY_EVENT_MSG_1_VFID, msg[1]);
	eventid = FIELD_GET(GUC2PF_VF_STATE_NOTIFY_EVENT_MSG_2_EVENT, msg[2]);

	return vfid ? pf_handle_vf_event(gt, vfid, eventid) : pf_handle_pf_event(gt, eventid);
}
