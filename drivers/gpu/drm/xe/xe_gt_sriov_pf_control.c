// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023-2024 Intel Corporation
 */

#include <drm/drm_managed.h>

#include "abi/guc_actions_sriov_abi.h"

#include "xe_device.h"
#include "xe_gt.h"
#include "xe_gt_sriov_pf.h"
#include "xe_gt_sriov_pf_config.h"
#include "xe_gt_sriov_pf_control.h"
#include "xe_gt_sriov_pf_helpers.h"
#include "xe_gt_sriov_pf_migration.h"
#include "xe_gt_sriov_pf_monitor.h"
#include "xe_gt_sriov_printk.h"
#include "xe_guc_ct.h"
#include "xe_sriov.h"
#include "xe_sriov_pf_service.h"
#include "xe_tile.h"

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

	ret = xe_guc_ct_send_block(&guc->ct, request, ARRAY_SIZE(request));
	return ret > 0 ? -EPROTO : ret;
}

static int pf_send_vf_control_cmd(struct xe_gt *gt, unsigned int vfid, u32 cmd)
{
	int err;

	xe_gt_assert(gt, vfid != PFID);
	xe_gt_sriov_dbg_verbose(gt, "sending VF%u control command %s\n",
				vfid, control_cmd_to_string(cmd));

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
 * DOC: The VF state machine
 *
 * The simplified VF state machine could be presented as::
 *
 *	               pause--------------------------o
 *	              /                               |
 *	             /                                v
 *	      (READY)<------------------resume-----(PAUSED)
 *	         ^   \                             /    /
 *	         |    \                           /    /
 *	         |     stop---->(STOPPED)<----stop    /
 *	         |                  /                /
 *	         |                 /                /
 *	         o--------<-----flr                /
 *	          \                               /
 *	           o------<--------------------flr
 *
 * Where:
 *
 * * READY - represents a state in which VF is fully operable
 * * PAUSED - represents a state in which VF activity is temporarily suspended
 * * STOPPED - represents a state in which VF activity is definitely halted
 * * pause - represents a request to temporarily suspend VF activity
 * * resume - represents a request to resume VF activity
 * * stop - represents a request to definitely halt VF activity
 * * flr - represents a request to perform VF FLR to restore VF activity
 *
 * However, each state transition requires additional steps that involves
 * communication with GuC that might fail or be interrupted by other requests::
 *
 *	                   .................................WIP....
 *	                   :                                      :
 *	          pause--------------------->PAUSE_WIP----------------------------o
 *	         /         :                /         \           :               |
 *	        /          :    o----<---stop          flr--o     :               |
 *	       /           :    |           \         /     |     :               V
 *	(READY,RESUMED)<--------+------------RESUME_WIP<----+--<-----resume--(PAUSED)
 *	  ^ \  \           :    |                           |     :          /   /
 *	  |  \  \          :    |                           |     :         /   /
 *	  |   \  \         :    |                           |     :        /   /
 *	  |    \  \        :    o----<----------------------+--<-------stop   /
 *	  |     \  \       :    |                           |     :          /
 *	  |      \  \      :    V                           |     :         /
 *	  |       \  stop----->STOP_WIP---------flr--->-----o     :        /
 *	  |        \       :    |                           |     :       /
 *	  |         \      :    |                           V     :      /
 *	  |          flr--------+----->----------------->FLR_WIP<-----flr
 *	  |                :    |                        /  ^     :
 *	  |                :    |                       /   |     :
 *	  o--------<-------:----+-----<----------------o    |     :
 *	                   :    |                           |     :
 *	                   :....|...........................|.....:
 *	                        |                           |
 *	                        V                           |
 *	                     (STOPPED)--------------------flr
 *
 * For details about each internal WIP state machine see:
 *
 * * `The VF PAUSE state machine`_
 * * `The VF RESUME state machine`_
 * * `The VF STOP state machine`_
 * * `The VF FLR state machine`_
 */

#ifdef CONFIG_DRM_XE_DEBUG_SRIOV
static const char *control_bit_to_string(enum xe_gt_sriov_control_bits bit)
{
	switch (bit) {
#define CASE2STR(_X) \
	case XE_GT_SRIOV_STATE_##_X: return #_X
	CASE2STR(WIP);
	CASE2STR(FLR_WIP);
	CASE2STR(FLR_SEND_START);
	CASE2STR(FLR_WAIT_GUC);
	CASE2STR(FLR_GUC_DONE);
	CASE2STR(FLR_RESET_CONFIG);
	CASE2STR(FLR_RESET_DATA);
	CASE2STR(FLR_RESET_MMIO);
	CASE2STR(FLR_SEND_FINISH);
	CASE2STR(FLR_FAILED);
	CASE2STR(PAUSE_WIP);
	CASE2STR(PAUSE_SEND_PAUSE);
	CASE2STR(PAUSE_WAIT_GUC);
	CASE2STR(PAUSE_GUC_DONE);
	CASE2STR(PAUSE_SAVE_GUC);
	CASE2STR(PAUSE_FAILED);
	CASE2STR(PAUSED);
	CASE2STR(RESUME_WIP);
	CASE2STR(RESUME_SEND_RESUME);
	CASE2STR(RESUME_FAILED);
	CASE2STR(RESUMED);
	CASE2STR(STOP_WIP);
	CASE2STR(STOP_SEND_STOP);
	CASE2STR(STOP_FAILED);
	CASE2STR(STOPPED);
	CASE2STR(MISMATCH);
#undef  CASE2STR
	default: return "?";
	}
}
#endif

static unsigned long pf_get_default_timeout(enum xe_gt_sriov_control_bits bit)
{
	switch (bit) {
	case XE_GT_SRIOV_STATE_FLR_WAIT_GUC:
	case XE_GT_SRIOV_STATE_PAUSE_WAIT_GUC:
		return HZ / 2;
	case XE_GT_SRIOV_STATE_FLR_WIP:
	case XE_GT_SRIOV_STATE_FLR_RESET_CONFIG:
		return 5 * HZ;
	default:
		return HZ;
	}
}

static struct xe_gt_sriov_control_state *pf_pick_vf_control(struct xe_gt *gt, unsigned int vfid)
{
	xe_gt_assert(gt, IS_SRIOV_PF(gt_to_xe(gt)));
	xe_gt_assert(gt, vfid <= xe_gt_sriov_pf_get_totalvfs(gt));

	return &gt->sriov.pf.vfs[vfid].control;
}

static unsigned long *pf_peek_vf_state(struct xe_gt *gt, unsigned int vfid)
{
	struct xe_gt_sriov_control_state *cs = pf_pick_vf_control(gt, vfid);

	return &cs->state;
}

static bool pf_check_vf_state(struct xe_gt *gt, unsigned int vfid,
			      enum xe_gt_sriov_control_bits bit)
{
	return test_bit(bit, pf_peek_vf_state(gt, vfid));
}

static void pf_dump_vf_state(struct xe_gt *gt, unsigned int vfid)
{
	unsigned long state = *pf_peek_vf_state(gt, vfid);
	enum xe_gt_sriov_control_bits bit;

	if (state) {
		xe_gt_sriov_dbg_verbose(gt, "VF%u state %#lx%s%*pbl\n",
					vfid, state, state ? " bits " : "",
					(int)BITS_PER_LONG, &state);
		for_each_set_bit(bit, &state, BITS_PER_LONG)
			xe_gt_sriov_dbg_verbose(gt, "VF%u state %s(%d)\n",
						vfid, control_bit_to_string(bit), bit);
	} else {
		xe_gt_sriov_dbg_verbose(gt, "VF%u state READY\n", vfid);
	}
}

static bool pf_expect_vf_state(struct xe_gt *gt, unsigned int vfid,
			       enum xe_gt_sriov_control_bits bit)
{
	bool result = pf_check_vf_state(gt, vfid, bit);

	if (unlikely(!result))
		pf_dump_vf_state(gt, vfid);

	return result;
}

static bool pf_expect_vf_not_state(struct xe_gt *gt, unsigned int vfid,
				   enum xe_gt_sriov_control_bits bit)
{
	bool result = !pf_check_vf_state(gt, vfid, bit);

	if (unlikely(!result))
		pf_dump_vf_state(gt, vfid);

	return result;
}

static bool pf_enter_vf_state(struct xe_gt *gt, unsigned int vfid,
			      enum xe_gt_sriov_control_bits bit)
{
	if (!test_and_set_bit(bit, pf_peek_vf_state(gt, vfid))) {
		xe_gt_sriov_dbg_verbose(gt, "VF%u state %s(%d) enter\n",
					vfid, control_bit_to_string(bit), bit);
		return true;
	}
	return false;
}

static bool pf_exit_vf_state(struct xe_gt *gt, unsigned int vfid,
			     enum xe_gt_sriov_control_bits bit)
{
	if (test_and_clear_bit(bit, pf_peek_vf_state(gt, vfid))) {
		xe_gt_sriov_dbg_verbose(gt, "VF%u state %s(%d) exit\n",
					vfid, control_bit_to_string(bit), bit);
		return true;
	}
	return false;
}

static void pf_escape_vf_state(struct xe_gt *gt, unsigned int vfid,
			       enum xe_gt_sriov_control_bits bit)
{
	if (pf_exit_vf_state(gt, vfid, bit))
		xe_gt_sriov_dbg_verbose(gt, "VF%u state %s(%d) escaped by %ps\n",
					vfid, control_bit_to_string(bit), bit,
					__builtin_return_address(0));
}

static void pf_enter_vf_mismatch(struct xe_gt *gt, unsigned int vfid)
{
	if (pf_enter_vf_state(gt, vfid, XE_GT_SRIOV_STATE_MISMATCH)) {
		xe_gt_sriov_dbg(gt, "VF%u state mismatch detected by %ps\n",
				vfid, __builtin_return_address(0));
		pf_dump_vf_state(gt, vfid);
	}
}

static void pf_exit_vf_mismatch(struct xe_gt *gt, unsigned int vfid)
{
	if (pf_exit_vf_state(gt, vfid, XE_GT_SRIOV_STATE_MISMATCH))
		xe_gt_sriov_dbg(gt, "VF%u state mismatch cleared by %ps\n",
				vfid, __builtin_return_address(0));

	pf_exit_vf_state(gt, vfid, XE_GT_SRIOV_STATE_STOP_FAILED);
	pf_exit_vf_state(gt, vfid, XE_GT_SRIOV_STATE_PAUSE_FAILED);
	pf_exit_vf_state(gt, vfid, XE_GT_SRIOV_STATE_RESUME_FAILED);
	pf_exit_vf_state(gt, vfid, XE_GT_SRIOV_STATE_FLR_FAILED);
}

#define pf_enter_vf_state_machine_bug(gt, vfid) ({	\
	pf_enter_vf_mismatch((gt), (vfid));		\
})

static void pf_queue_control_worker(struct xe_gt *gt)
{
	struct xe_device *xe = gt_to_xe(gt);

	xe_gt_assert(gt, IS_SRIOV_PF(xe));

	queue_work(xe->sriov.wq, &gt->sriov.pf.control.worker);
}

static void pf_queue_vf(struct xe_gt *gt, unsigned int vfid)
{
	struct xe_gt_sriov_pf_control *pfc = &gt->sriov.pf.control;

	xe_gt_assert(gt, IS_SRIOV_PF(gt_to_xe(gt)));

	spin_lock(&pfc->lock);
	list_move_tail(&gt->sriov.pf.vfs[vfid].control.link, &pfc->list);
	spin_unlock(&pfc->lock);

	pf_queue_control_worker(gt);
}

static void pf_exit_vf_flr_wip(struct xe_gt *gt, unsigned int vfid);
static void pf_exit_vf_stop_wip(struct xe_gt *gt, unsigned int vfid);
static void pf_exit_vf_pause_wip(struct xe_gt *gt, unsigned int vfid);
static void pf_exit_vf_resume_wip(struct xe_gt *gt, unsigned int vfid);

static bool pf_enter_vf_wip(struct xe_gt *gt, unsigned int vfid)
{
	if (pf_enter_vf_state(gt, vfid, XE_GT_SRIOV_STATE_WIP)) {
		struct xe_gt_sriov_control_state *cs = pf_pick_vf_control(gt, vfid);

		reinit_completion(&cs->done);
		return true;
	}
	return false;
}

static void pf_exit_vf_wip(struct xe_gt *gt, unsigned int vfid)
{
	if (pf_exit_vf_state(gt, vfid, XE_GT_SRIOV_STATE_WIP)) {
		struct xe_gt_sriov_control_state *cs = pf_pick_vf_control(gt, vfid);

		pf_exit_vf_flr_wip(gt, vfid);
		pf_exit_vf_stop_wip(gt, vfid);
		pf_exit_vf_pause_wip(gt, vfid);
		pf_exit_vf_resume_wip(gt, vfid);

		complete_all(&cs->done);
	}
}

static int pf_wait_vf_wip_done(struct xe_gt *gt, unsigned int vfid, unsigned long timeout)
{
	struct xe_gt_sriov_control_state *cs = pf_pick_vf_control(gt, vfid);

	return wait_for_completion_timeout(&cs->done, timeout) ? 0 : -ETIMEDOUT;
}

static void pf_enter_vf_ready(struct xe_gt *gt, unsigned int vfid)
{
	pf_exit_vf_state(gt, vfid, XE_GT_SRIOV_STATE_PAUSED);
	pf_exit_vf_state(gt, vfid, XE_GT_SRIOV_STATE_STOPPED);
	pf_exit_vf_state(gt, vfid, XE_GT_SRIOV_STATE_RESUMED);
	pf_exit_vf_mismatch(gt, vfid);
	pf_exit_vf_wip(gt, vfid);
}

/**
 * DOC: The VF PAUSE state machine
 *
 * The VF PAUSE state machine looks like::
 *
 *	 (READY,RESUMED)<-------------<---------------------o---------o
 *	    |                                                \         \
 *	   pause                                              \         \
 *	    |                                                  \         \
 *	....V...........................PAUSE_WIP........       \         \
 *	:    \                                          :        o         \
 *	:     \   o------<-----busy                     :        |          \
 *	:      \ /              /                       :        |           |
 *	:       PAUSE_SEND_PAUSE ---failed--->----------o--->(PAUSE_FAILED)  |
 *	:        |              \                       :        |           |
 *	:      acked             rejected---->----------o--->(MISMATCH)     /
 *	:        |                                      :                  /
 *	:        v                                      :                 /
 *	:       PAUSE_WAIT_GUC                          :                /
 *	:        |                                      :               /
 *	:       done                                    :              /
 *	:        |                                      :             /
 *	:        v                                      :            /
 *	:       PAUSE_GUC_DONE                          o-----restart
 *	:        |                                      :
 *	:        |   o---<--busy                        :
 *	:        v  /         /                         :
 *	:       PAUSE_SAVE_GUC                          :
 *	:      /                                        :
 *	:     /                                         :
 *	:....o..............o...............o...........:
 *	     |              |               |
 *	  completed        flr             stop
 *	     |              |               |
 *	     V         .....V.....    ......V.....
 *	 (PAUSED)      : FLR_WIP :    : STOP_WIP :
 *	               :.........:    :..........:
 *
 * For the full state machine view, see `The VF state machine`_.
 */

static void pf_exit_vf_pause_wip(struct xe_gt *gt, unsigned int vfid)
{
	if (pf_exit_vf_state(gt, vfid, XE_GT_SRIOV_STATE_PAUSE_WIP)) {
		pf_escape_vf_state(gt, vfid, XE_GT_SRIOV_STATE_PAUSE_SEND_PAUSE);
		pf_escape_vf_state(gt, vfid, XE_GT_SRIOV_STATE_PAUSE_WAIT_GUC);
		pf_escape_vf_state(gt, vfid, XE_GT_SRIOV_STATE_PAUSE_GUC_DONE);
		pf_escape_vf_state(gt, vfid, XE_GT_SRIOV_STATE_PAUSE_SAVE_GUC);
	}
}

static void pf_enter_vf_paused(struct xe_gt *gt, unsigned int vfid)
{
	if (!pf_enter_vf_state(gt, vfid, XE_GT_SRIOV_STATE_PAUSED))
		pf_enter_vf_state_machine_bug(gt, vfid);

	pf_exit_vf_state(gt, vfid, XE_GT_SRIOV_STATE_RESUMED);
	pf_exit_vf_mismatch(gt, vfid);
	pf_exit_vf_wip(gt, vfid);
}

static void pf_enter_vf_pause_completed(struct xe_gt *gt, unsigned int vfid)
{
	pf_enter_vf_paused(gt, vfid);
}

static void pf_enter_vf_pause_failed(struct xe_gt *gt, unsigned int vfid)
{
	pf_enter_vf_state(gt, vfid, XE_GT_SRIOV_STATE_PAUSE_FAILED);
	pf_exit_vf_wip(gt, vfid);
}

static void pf_enter_vf_pause_rejected(struct xe_gt *gt, unsigned int vfid)
{
	pf_enter_vf_mismatch(gt, vfid);
	pf_enter_vf_pause_failed(gt, vfid);
}

static void pf_enter_vf_pause_save_guc(struct xe_gt *gt, unsigned int vfid)
{
	if (!pf_enter_vf_state(gt, vfid, XE_GT_SRIOV_STATE_PAUSE_SAVE_GUC))
		pf_enter_vf_state_machine_bug(gt, vfid);
}

static bool pf_exit_vf_pause_save_guc(struct xe_gt *gt, unsigned int vfid)
{
	int err;

	if (!pf_exit_vf_state(gt, vfid, XE_GT_SRIOV_STATE_PAUSE_SAVE_GUC))
		return false;

	err = xe_gt_sriov_pf_migration_save_guc_state(gt, vfid);
	if (err) {
		/* retry if busy */
		if (err == -EBUSY) {
			pf_enter_vf_pause_save_guc(gt, vfid);
			return true;
		}
		/* give up on error */
		if (err == -EIO)
			pf_enter_vf_mismatch(gt, vfid);
	}

	pf_enter_vf_pause_completed(gt, vfid);
	return true;
}

static bool pf_exit_vf_pause_guc_done(struct xe_gt *gt, unsigned int vfid)
{
	if (!pf_exit_vf_state(gt, vfid, XE_GT_SRIOV_STATE_PAUSE_GUC_DONE))
		return false;

	pf_enter_vf_pause_save_guc(gt, vfid);
	return true;
}

static void pf_enter_vf_pause_guc_done(struct xe_gt *gt, unsigned int vfid)
{
	if (pf_enter_vf_state(gt, vfid, XE_GT_SRIOV_STATE_PAUSE_GUC_DONE))
		pf_queue_vf(gt, vfid);
}

static void pf_enter_pause_wait_guc(struct xe_gt *gt, unsigned int vfid)
{
	if (!pf_enter_vf_state(gt, vfid, XE_GT_SRIOV_STATE_PAUSE_WAIT_GUC))
		pf_enter_vf_state_machine_bug(gt, vfid);
}

static bool pf_exit_pause_wait_guc(struct xe_gt *gt, unsigned int vfid)
{
	return pf_exit_vf_state(gt, vfid, XE_GT_SRIOV_STATE_PAUSE_WAIT_GUC);
}

static void pf_enter_vf_pause_send_pause(struct xe_gt *gt, unsigned int vfid)
{
	if (!pf_enter_vf_state(gt, vfid, XE_GT_SRIOV_STATE_PAUSE_SEND_PAUSE))
		pf_enter_vf_state_machine_bug(gt, vfid);

	pf_queue_vf(gt, vfid);
}

static bool pf_exit_vf_pause_send_pause(struct xe_gt *gt, unsigned int vfid)
{
	int err;

	if (!pf_exit_vf_state(gt, vfid, XE_GT_SRIOV_STATE_PAUSE_SEND_PAUSE))
		return false;

	/* GuC may actually send a PAUSE_DONE before we get a RESPONSE */
	pf_enter_pause_wait_guc(gt, vfid);

	err = pf_send_vf_pause(gt, vfid);
	if (err) {
		/* send failed, so we shouldn't expect PAUSE_DONE from GuC */
		pf_exit_pause_wait_guc(gt, vfid);

		if (err == -EBUSY)
			pf_enter_vf_pause_send_pause(gt, vfid);
		else if (err == -EIO)
			pf_enter_vf_pause_rejected(gt, vfid);
		else
			pf_enter_vf_pause_failed(gt, vfid);
	} else {
		/*
		 * we have already moved to WAIT_GUC, maybe even to GUC_DONE
		 * but since GuC didn't complain, we may clear MISMATCH
		 */
		pf_exit_vf_mismatch(gt, vfid);
	}

	return true;
}

static bool pf_enter_vf_pause_wip(struct xe_gt *gt, unsigned int vfid)
{
	if (pf_enter_vf_state(gt, vfid, XE_GT_SRIOV_STATE_PAUSE_WIP)) {
		pf_enter_vf_wip(gt, vfid);
		pf_enter_vf_pause_send_pause(gt, vfid);
		return true;
	}

	return false;
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
	unsigned long timeout = pf_get_default_timeout(XE_GT_SRIOV_STATE_PAUSE_WIP);
	int err;

	if (pf_check_vf_state(gt, vfid, XE_GT_SRIOV_STATE_STOPPED)) {
		xe_gt_sriov_dbg(gt, "VF%u is stopped!\n", vfid);
		return -EPERM;
	}

	if (pf_check_vf_state(gt, vfid, XE_GT_SRIOV_STATE_PAUSED)) {
		xe_gt_sriov_dbg(gt, "VF%u was already paused!\n", vfid);
		return -ESTALE;
	}

	if (!pf_enter_vf_pause_wip(gt, vfid)) {
		xe_gt_sriov_dbg(gt, "VF%u pause already in progress!\n", vfid);
		return -EALREADY;
	}

	err = pf_wait_vf_wip_done(gt, vfid, timeout);
	if (err) {
		xe_gt_sriov_dbg(gt, "VF%u pause didn't finish in %u ms (%pe)\n",
				vfid, jiffies_to_msecs(timeout), ERR_PTR(err));
		return err;
	}

	if (pf_expect_vf_state(gt, vfid, XE_GT_SRIOV_STATE_PAUSED)) {
		xe_gt_sriov_info(gt, "VF%u paused!\n", vfid);
		return 0;
	}

	if (pf_check_vf_state(gt, vfid, XE_GT_SRIOV_STATE_PAUSE_FAILED)) {
		xe_gt_sriov_dbg(gt, "VF%u pause failed!\n", vfid);
		return -EIO;
	}

	xe_gt_sriov_dbg(gt, "VF%u pause was canceled!\n", vfid);
	return -ECANCELED;
}

/**
 * DOC: The VF RESUME state machine
 *
 * The VF RESUME state machine looks like::
 *
 *	 (PAUSED)<-----------------<------------------------o
 *	    |                                                \
 *	   resume                                             \
 *	    |                                                  \
 *	....V............................RESUME_WIP......       \
 *	:    \                                          :        o
 *	:     \   o-------<-----busy                    :        |
 *	:      \ /                /                     :        |
 *	:       RESUME_SEND_RESUME ---failed--->--------o--->(RESUME_FAILED)
 *	:       /                \                      :        |
 *	:    acked                rejected---->---------o--->(MISMATCH)
 *	:     /                                         :
 *	:....o..............o...............o.....o.....:
 *	     |              |               |      \
 *	  completed        flr            stop      restart-->(READY)
 *	     |              |               |
 *	     V         .....V.....    ......V.....
 *	 (RESUMED)     : FLR_WIP :    : STOP_WIP :
 *	               :.........:    :..........:
 *
 * For the full state machine view, see `The VF state machine`_.
 */

static void pf_exit_vf_resume_wip(struct xe_gt *gt, unsigned int vfid)
{
	if (pf_exit_vf_state(gt, vfid, XE_GT_SRIOV_STATE_RESUME_WIP))
		pf_escape_vf_state(gt, vfid, XE_GT_SRIOV_STATE_RESUME_SEND_RESUME);
}

static void pf_enter_vf_resumed(struct xe_gt *gt, unsigned int vfid)
{
	pf_enter_vf_state(gt, vfid, XE_GT_SRIOV_STATE_RESUMED);
	pf_exit_vf_state(gt, vfid, XE_GT_SRIOV_STATE_PAUSED);
	pf_exit_vf_mismatch(gt, vfid);
	pf_exit_vf_wip(gt, vfid);
}

static void pf_enter_vf_resume_completed(struct xe_gt *gt, unsigned int vfid)
{
	pf_enter_vf_resumed(gt, vfid);
}

static void pf_enter_vf_resume_failed(struct xe_gt *gt, unsigned int vfid)
{
	pf_enter_vf_state(gt, vfid, XE_GT_SRIOV_STATE_RESUME_FAILED);
	pf_exit_vf_wip(gt, vfid);
}

static void pf_enter_vf_resume_rejected(struct xe_gt *gt, unsigned int vfid)
{
	pf_enter_vf_mismatch(gt, vfid);
	pf_enter_vf_resume_failed(gt, vfid);
}

static void pf_enter_vf_resume_send_resume(struct xe_gt *gt, unsigned int vfid)
{
	if (!pf_enter_vf_state(gt, vfid, XE_GT_SRIOV_STATE_RESUME_SEND_RESUME))
		pf_enter_vf_state_machine_bug(gt, vfid);

	pf_queue_vf(gt, vfid);
}

static bool pf_exit_vf_resume_send_resume(struct xe_gt *gt, unsigned int vfid)
{
	int err;

	if (!pf_exit_vf_state(gt, vfid, XE_GT_SRIOV_STATE_RESUME_SEND_RESUME))
		return false;

	err = pf_send_vf_resume(gt, vfid);
	if (err == -EBUSY)
		pf_enter_vf_resume_send_resume(gt, vfid);
	else if (err == -EIO)
		pf_enter_vf_resume_rejected(gt, vfid);
	else if (err)
		pf_enter_vf_resume_failed(gt, vfid);
	else
		pf_enter_vf_resume_completed(gt, vfid);
	return true;
}

static bool pf_enter_vf_resume_wip(struct xe_gt *gt, unsigned int vfid)
{
	if (pf_enter_vf_state(gt, vfid, XE_GT_SRIOV_STATE_RESUME_WIP)) {
		pf_enter_vf_wip(gt, vfid);
		pf_enter_vf_resume_send_resume(gt, vfid);
		return true;
	}

	return false;
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
	unsigned long timeout = pf_get_default_timeout(XE_GT_SRIOV_STATE_RESUME_WIP);
	int err;

	if (!pf_check_vf_state(gt, vfid, XE_GT_SRIOV_STATE_PAUSED)) {
		xe_gt_sriov_dbg(gt, "VF%u is not paused!\n", vfid);
		return -EPERM;
	}

	if (!pf_enter_vf_resume_wip(gt, vfid)) {
		xe_gt_sriov_dbg(gt, "VF%u resume already in progress!\n", vfid);
		return -EALREADY;
	}

	err = pf_wait_vf_wip_done(gt, vfid, timeout);
	if (err)
		return err;

	if (pf_expect_vf_state(gt, vfid, XE_GT_SRIOV_STATE_RESUMED)) {
		xe_gt_sriov_info(gt, "VF%u resumed!\n", vfid);
		return 0;
	}

	if (pf_check_vf_state(gt, vfid, XE_GT_SRIOV_STATE_RESUME_FAILED)) {
		xe_gt_sriov_dbg(gt, "VF%u resume failed!\n", vfid);
		return -EIO;
	}

	xe_gt_sriov_dbg(gt, "VF%u resume was canceled!\n", vfid);
	return -ECANCELED;
}

/**
 * DOC: The VF STOP state machine
 *
 * The VF STOP state machine looks like::
 *
 *	 (READY,PAUSED,RESUMED)<-------<--------------------o
 *	    |                                                \
 *	   stop                                               \
 *	    |                                                  \
 *	....V..............................STOP_WIP......       \
 *	:    \                                          :        o
 *	:     \   o----<----busy                        :        |
 *	:      \ /            /                         :        |
 *	:       STOP_SEND_STOP--------failed--->--------o--->(STOP_FAILED)
 *	:       /             \                         :        |
 *	:    acked             rejected-------->--------o--->(MISMATCH)
 *	:     /                                         :
 *	:....o..............o...............o...........:
 *	     |              |               |
 *	  completed        flr            restart
 *	     |              |               |
 *	     V         .....V.....          V
 *	 (STOPPED)     : FLR_WIP :       (READY)
 *	               :.........:
 *
 * For the full state machine view, see `The VF state machine`_.
 */

static void pf_exit_vf_stop_wip(struct xe_gt *gt, unsigned int vfid)
{
	if (pf_exit_vf_state(gt, vfid, XE_GT_SRIOV_STATE_STOP_WIP))
		pf_escape_vf_state(gt, vfid, XE_GT_SRIOV_STATE_STOP_SEND_STOP);
}

static void pf_enter_vf_stopped(struct xe_gt *gt, unsigned int vfid)
{
	if (!pf_enter_vf_state(gt, vfid, XE_GT_SRIOV_STATE_STOPPED))
		pf_enter_vf_state_machine_bug(gt, vfid);

	pf_exit_vf_state(gt, vfid, XE_GT_SRIOV_STATE_RESUMED);
	pf_exit_vf_state(gt, vfid, XE_GT_SRIOV_STATE_PAUSED);
	pf_exit_vf_mismatch(gt, vfid);
	pf_exit_vf_wip(gt, vfid);
}

static void pf_enter_vf_stop_completed(struct xe_gt *gt, unsigned int vfid)
{
	pf_enter_vf_stopped(gt, vfid);
}

static void pf_enter_vf_stop_failed(struct xe_gt *gt, unsigned int vfid)
{
	pf_enter_vf_state(gt, vfid, XE_GT_SRIOV_STATE_STOP_FAILED);
	pf_exit_vf_wip(gt, vfid);
}

static void pf_enter_vf_stop_rejected(struct xe_gt *gt, unsigned int vfid)
{
	pf_enter_vf_mismatch(gt, vfid);
	pf_enter_vf_stop_failed(gt, vfid);
}

static void pf_enter_vf_stop_send_stop(struct xe_gt *gt, unsigned int vfid)
{
	if (!pf_enter_vf_state(gt, vfid, XE_GT_SRIOV_STATE_STOP_SEND_STOP))
		pf_enter_vf_state_machine_bug(gt, vfid);

	pf_queue_vf(gt, vfid);
}

static bool pf_exit_vf_stop_send_stop(struct xe_gt *gt, unsigned int vfid)
{
	int err;

	if (!pf_exit_vf_state(gt, vfid, XE_GT_SRIOV_STATE_STOP_SEND_STOP))
		return false;

	err = pf_send_vf_stop(gt, vfid);
	if (err == -EBUSY)
		pf_enter_vf_stop_send_stop(gt, vfid);
	else if (err == -EIO)
		pf_enter_vf_stop_rejected(gt, vfid);
	else if (err)
		pf_enter_vf_stop_failed(gt, vfid);
	else
		pf_enter_vf_stop_completed(gt, vfid);
	return true;
}

static bool pf_enter_vf_stop_wip(struct xe_gt *gt, unsigned int vfid)
{
	if (pf_enter_vf_state(gt, vfid, XE_GT_SRIOV_STATE_STOP_WIP)) {
		pf_enter_vf_wip(gt, vfid);
		pf_enter_vf_stop_send_stop(gt, vfid);
		return true;
	}
	return false;
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
	unsigned long timeout = pf_get_default_timeout(XE_GT_SRIOV_STATE_STOP_WIP);
	int err;

	if (pf_check_vf_state(gt, vfid, XE_GT_SRIOV_STATE_STOPPED)) {
		xe_gt_sriov_dbg(gt, "VF%u was already stopped!\n", vfid);
		return -ESTALE;
	}

	if (!pf_enter_vf_stop_wip(gt, vfid)) {
		xe_gt_sriov_dbg(gt, "VF%u stop already in progress!\n", vfid);
		return -EALREADY;
	}

	err = pf_wait_vf_wip_done(gt, vfid, timeout);
	if (err)
		return err;

	if (pf_expect_vf_state(gt, vfid, XE_GT_SRIOV_STATE_STOPPED)) {
		xe_gt_sriov_info(gt, "VF%u stopped!\n", vfid);
		return 0;
	}

	if (pf_check_vf_state(gt, vfid, XE_GT_SRIOV_STATE_STOP_FAILED)) {
		xe_gt_sriov_dbg(gt, "VF%u stop failed!\n", vfid);
		return -EIO;
	}

	xe_gt_sriov_dbg(gt, "VF%u stop was canceled!\n", vfid);
	return -ECANCELED;
}

/**
 * DOC: The VF FLR state machine
 *
 * The VF FLR state machine looks like::
 *
 *	 (READY,PAUSED,STOPPED)<------------<--------------o
 *	    |                                               \
 *	   flr                                               \
 *	    |                                                 \
 *	....V..........................FLR_WIP...........      \
 *	:    \                                          :       \
 *	:     \   o----<----busy                        :        |
 *	:      \ /            /                         :        |
 *	:       FLR_SEND_START---failed----->-----------o--->(FLR_FAILED)<---o
 *	:        |            \                         :        |           |
 *	:      acked           rejected----->-----------o--->(MISMATCH)      |
 *	:        |                                      :        ^           |
 *	:        v                                      :        |           |
 *	:       FLR_WAIT_GUC                            :        |           |
 *	:        |                                      :        |           |
 *	:       done                                    :        |           |
 *	:        |                                      :        |           |
 *	:        v                                      :        |           |
 *	:       FLR_GUC_DONE                            :        |           |
 *	:        |                                      :        |           |
 *	:       FLR_RESET_CONFIG---failed--->-----------o--------+-----------o
 *	:        |                                      :        |           |
 *	:       FLR_RESET_DATA                          :        |           |
 *	:        |                                      :        |           |
 *	:       FLR_RESET_MMIO                          :        |           |
 *	:        |                                      :        |           |
 *	:        | o----<----busy                       :        |           |
 *	:        |/            /                        :        |           |
 *	:       FLR_SEND_FINISH----failed--->-----------o--------+-----------o
 *	:       /             \                         :        |
 *	:     acked            rejected----->-----------o--------o
 *	:     /                                         :
 *	:....o..............................o...........:
 *	     |                              |
 *	  completed                       restart
 *	     |                             /
 *	     V                            /
 *	  (READY)<----------<------------o
 *
 * For the full state machine view, see `The VF state machine`_.
 */

static void pf_enter_vf_flr_send_start(struct xe_gt *gt, unsigned int vfid)
{
	if (!pf_enter_vf_state(gt, vfid, XE_GT_SRIOV_STATE_FLR_SEND_START))
		pf_enter_vf_state_machine_bug(gt, vfid);

	pf_queue_vf(gt, vfid);
}

static void pf_enter_vf_flr_wip(struct xe_gt *gt, unsigned int vfid)
{
	if (!pf_enter_vf_state(gt, vfid, XE_GT_SRIOV_STATE_FLR_WIP)) {
		xe_gt_sriov_dbg(gt, "VF%u FLR is already in progress\n", vfid);
		return;
	}

	pf_enter_vf_wip(gt, vfid);
	pf_enter_vf_flr_send_start(gt, vfid);
}

static void pf_exit_vf_flr_wip(struct xe_gt *gt, unsigned int vfid)
{
	if (pf_exit_vf_state(gt, vfid, XE_GT_SRIOV_STATE_FLR_WIP)) {
		pf_escape_vf_state(gt, vfid, XE_GT_SRIOV_STATE_FLR_SEND_FINISH);
		pf_escape_vf_state(gt, vfid, XE_GT_SRIOV_STATE_FLR_RESET_MMIO);
		pf_escape_vf_state(gt, vfid, XE_GT_SRIOV_STATE_FLR_RESET_DATA);
		pf_escape_vf_state(gt, vfid, XE_GT_SRIOV_STATE_FLR_RESET_CONFIG);
		pf_escape_vf_state(gt, vfid, XE_GT_SRIOV_STATE_FLR_GUC_DONE);
		pf_escape_vf_state(gt, vfid, XE_GT_SRIOV_STATE_FLR_WAIT_GUC);
		pf_escape_vf_state(gt, vfid, XE_GT_SRIOV_STATE_FLR_SEND_START);
	}
}

static void pf_enter_vf_flr_completed(struct xe_gt *gt, unsigned int vfid)
{
	pf_enter_vf_ready(gt, vfid);
}

static void pf_enter_vf_flr_failed(struct xe_gt *gt, unsigned int vfid)
{
	if (pf_enter_vf_state(gt, vfid, XE_GT_SRIOV_STATE_FLR_FAILED))
		xe_gt_sriov_notice(gt, "VF%u FLR failed!\n", vfid);
	pf_exit_vf_wip(gt, vfid);
}

static void pf_enter_vf_flr_rejected(struct xe_gt *gt, unsigned int vfid)
{
	pf_enter_vf_mismatch(gt, vfid);
	pf_enter_vf_flr_failed(gt, vfid);
}

static void pf_enter_vf_flr_send_finish(struct xe_gt *gt, unsigned int vfid)
{
	if (!pf_enter_vf_state(gt, vfid, XE_GT_SRIOV_STATE_FLR_SEND_FINISH))
		pf_enter_vf_state_machine_bug(gt, vfid);

	pf_queue_vf(gt, vfid);
}

static bool pf_exit_vf_flr_send_finish(struct xe_gt *gt, unsigned int vfid)
{
	int err;

	if (!pf_exit_vf_state(gt, vfid, XE_GT_SRIOV_STATE_FLR_SEND_FINISH))
		return false;

	err = pf_send_vf_flr_finish(gt, vfid);
	if (err == -EBUSY)
		pf_enter_vf_flr_send_finish(gt, vfid);
	else if (err == -EIO)
		pf_enter_vf_flr_rejected(gt, vfid);
	else if (err)
		pf_enter_vf_flr_failed(gt, vfid);
	else
		pf_enter_vf_flr_completed(gt, vfid);
	return true;
}

static void pf_enter_vf_flr_reset_mmio(struct xe_gt *gt, unsigned int vfid)
{
	if (!pf_enter_vf_state(gt, vfid, XE_GT_SRIOV_STATE_FLR_RESET_MMIO))
		pf_enter_vf_state_machine_bug(gt, vfid);

	pf_queue_vf(gt, vfid);
}

static bool pf_exit_vf_flr_reset_mmio(struct xe_gt *gt, unsigned int vfid)
{
	if (!pf_exit_vf_state(gt, vfid, XE_GT_SRIOV_STATE_FLR_RESET_MMIO))
		return false;

	xe_gt_sriov_pf_sanitize_hw(gt, vfid);

	pf_enter_vf_flr_send_finish(gt, vfid);
	return true;
}

static void pf_enter_vf_flr_reset_data(struct xe_gt *gt, unsigned int vfid)
{
	if (!pf_enter_vf_state(gt, vfid, XE_GT_SRIOV_STATE_FLR_RESET_DATA))
		pf_enter_vf_state_machine_bug(gt, vfid);

	pf_queue_vf(gt, vfid);
}

static bool pf_exit_vf_flr_reset_data(struct xe_gt *gt, unsigned int vfid)
{
	if (!pf_exit_vf_state(gt, vfid, XE_GT_SRIOV_STATE_FLR_RESET_DATA))
		return false;

	if (xe_tile_is_root(gt->tile) && xe_gt_is_main_type(gt))
		xe_sriov_pf_service_reset_vf(gt_to_xe(gt), vfid);

	xe_gt_sriov_pf_monitor_flr(gt, vfid);

	pf_enter_vf_flr_reset_mmio(gt, vfid);
	return true;
}

static void pf_enter_vf_flr_reset_config(struct xe_gt *gt, unsigned int vfid)
{
	if (!pf_enter_vf_state(gt, vfid, XE_GT_SRIOV_STATE_FLR_RESET_CONFIG))
		pf_enter_vf_state_machine_bug(gt, vfid);

	pf_queue_vf(gt, vfid);
}

static bool pf_exit_vf_flr_reset_config(struct xe_gt *gt, unsigned int vfid)
{
	unsigned long timeout = pf_get_default_timeout(XE_GT_SRIOV_STATE_FLR_RESET_CONFIG);
	int err;

	if (!pf_exit_vf_state(gt, vfid, XE_GT_SRIOV_STATE_FLR_RESET_CONFIG))
		return false;

	err = xe_gt_sriov_pf_config_sanitize(gt, vfid, timeout);
	if (err)
		pf_enter_vf_flr_failed(gt, vfid);
	else
		pf_enter_vf_flr_reset_data(gt, vfid);
	return true;
}

static void pf_enter_vf_flr_wait_guc(struct xe_gt *gt, unsigned int vfid)
{
	if (!pf_enter_vf_state(gt, vfid, XE_GT_SRIOV_STATE_FLR_WAIT_GUC))
		pf_enter_vf_state_machine_bug(gt, vfid);
}

static bool pf_exit_vf_flr_wait_guc(struct xe_gt *gt, unsigned int vfid)
{
	return pf_exit_vf_state(gt, vfid, XE_GT_SRIOV_STATE_FLR_WAIT_GUC);
}

static bool pf_exit_vf_flr_send_start(struct xe_gt *gt, unsigned int vfid)
{
	int err;

	if (!pf_exit_vf_state(gt, vfid, XE_GT_SRIOV_STATE_FLR_SEND_START))
		return false;

	/* GuC may actually send a FLR_DONE before we get a RESPONSE */
	pf_enter_vf_flr_wait_guc(gt, vfid);

	err = pf_send_vf_flr_start(gt, vfid);
	if (err) {
		/* send failed, so we shouldn't expect FLR_DONE from GuC */
		pf_exit_vf_flr_wait_guc(gt, vfid);

		if (err == -EBUSY)
			pf_enter_vf_flr_send_start(gt, vfid);
		else if (err == -EIO)
			pf_enter_vf_flr_rejected(gt, vfid);
		else
			pf_enter_vf_flr_failed(gt, vfid);
	} else {
		/*
		 * we have already moved to WAIT_GUC, maybe even to GUC_DONE
		 * but since GuC didn't complain, we may clear MISMATCH
		 */
		pf_exit_vf_mismatch(gt, vfid);
	}

	return true;
}

static bool pf_exit_vf_flr_guc_done(struct xe_gt *gt, unsigned int vfid)
{
	if (!pf_exit_vf_state(gt, vfid, XE_GT_SRIOV_STATE_FLR_GUC_DONE))
		return false;

	pf_enter_vf_flr_reset_config(gt, vfid);
	return true;
}

static void pf_enter_vf_flr_guc_done(struct xe_gt *gt, unsigned int vfid)
{
	if (pf_enter_vf_state(gt, vfid, XE_GT_SRIOV_STATE_FLR_GUC_DONE))
		pf_queue_vf(gt, vfid);
}

/**
 * xe_gt_sriov_pf_control_trigger_flr - Start a VF FLR sequence.
 * @gt: the &xe_gt
 * @vfid: the VF identifier
 *
 * This function is for PF only.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_gt_sriov_pf_control_trigger_flr(struct xe_gt *gt, unsigned int vfid)
{
	unsigned long timeout = pf_get_default_timeout(XE_GT_SRIOV_STATE_FLR_WIP);
	int err;

	pf_enter_vf_flr_wip(gt, vfid);

	err = pf_wait_vf_wip_done(gt, vfid, timeout);
	if (err) {
		xe_gt_sriov_notice(gt, "VF%u FLR didn't finish in %u ms (%pe)\n",
				   vfid, jiffies_to_msecs(timeout), ERR_PTR(err));
		return err;
	}

	if (!pf_expect_vf_not_state(gt, vfid, XE_GT_SRIOV_STATE_FLR_FAILED))
		return -EIO;

	return 0;
}

/**
 * DOC: The VF FLR Flow with GuC
 *
 * The VF FLR flow includes several steps::
 *
 *	         PF                        GUC             PCI
 *	========================================================
 *	         |                          |               |
 *	(1)      |                         [ ] <----- FLR --|
 *	         |                         [ ]              :
 *	(2)     [ ] <-------- NOTIFY FLR --[ ]
 *	        [ ]                         |
 *	(3)     [ ]                         |
 *	        [ ]                         |
 *	        [ ]-- START FLR ---------> [ ]
 *	         |                         [ ]
 *	(4)      |                         [ ]
 *	         |                         [ ]
 *	        [ ] <--------- FLR DONE -- [ ]
 *	        [ ]                         |
 *	(5)     [ ]                         |
 *	        [ ]                         |
 *	        [ ]-- FINISH FLR --------> [ ]
 *	         |                          |
 *
 * * Step 1: PCI HW generates interrupt to the GuC about VF FLR
 * * Step 2: GuC FW sends G2H notification to the PF about VF FLR
 * * Step 2a: on some platforms G2H is only received from root GuC
 * * Step 3: PF sends H2G request to the GuC to start VF FLR sequence
 * * Step 3a: on some platforms PF must send H2G to all other GuCs
 * * Step 4: GuC FW performs VF FLR cleanups and notifies the PF when done
 * * Step 5: PF performs VF FLR cleanups and notifies the GuC FW when finished
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
			pf_enter_vf_flr_wip(gtit, vfid);
	} else {
		pf_enter_vf_flr_wip(gt, vfid);
	}
}

static void pf_handle_vf_flr_done(struct xe_gt *gt, u32 vfid)
{
	if (!pf_exit_vf_flr_wait_guc(gt, vfid)) {
		xe_gt_sriov_dbg(gt, "Received out of order 'VF%u FLR done'\n", vfid);
		pf_enter_vf_mismatch(gt, vfid);
		return;
	}

	pf_enter_vf_flr_guc_done(gt, vfid);
}

static void pf_handle_vf_pause_done(struct xe_gt *gt, u32 vfid)
{
	if (!pf_exit_pause_wait_guc(gt, vfid)) {
		xe_gt_sriov_dbg(gt, "Received out of order 'VF%u PAUSE done'\n", vfid);
		pf_enter_vf_mismatch(gt, vfid);
		return;
	}

	pf_enter_vf_pause_guc_done(gt, vfid);
}

static int pf_handle_vf_event(struct xe_gt *gt, u32 vfid, u32 eventid)
{
	xe_gt_sriov_dbg_verbose(gt, "received VF%u event %#x\n", vfid, eventid);

	if (vfid > xe_gt_sriov_pf_get_totalvfs(gt))
		return -EPROTO;

	switch (eventid) {
	case GUC_PF_NOTIFY_VF_FLR:
		pf_handle_vf_flr(gt, vfid);
		break;
	case GUC_PF_NOTIFY_VF_FLR_DONE:
		pf_handle_vf_flr_done(gt, vfid);
		break;
	case GUC_PF_NOTIFY_VF_PAUSE_DONE:
		pf_handle_vf_pause_done(gt, vfid);
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

static bool pf_process_vf_state_machine(struct xe_gt *gt, unsigned int vfid)
{
	if (pf_exit_vf_flr_send_start(gt, vfid))
		return true;

	if (pf_check_vf_state(gt, vfid, XE_GT_SRIOV_STATE_FLR_WAIT_GUC)) {
		xe_gt_sriov_dbg_verbose(gt, "VF%u in %s\n", vfid,
					control_bit_to_string(XE_GT_SRIOV_STATE_FLR_WAIT_GUC));
		return false;
	}

	if (pf_exit_vf_flr_guc_done(gt, vfid))
		return true;

	if (pf_exit_vf_flr_reset_config(gt, vfid))
		return true;

	if (pf_exit_vf_flr_reset_data(gt, vfid))
		return true;

	if (pf_exit_vf_flr_reset_mmio(gt, vfid))
		return true;

	if (pf_exit_vf_flr_send_finish(gt, vfid))
		return true;

	if (pf_exit_vf_stop_send_stop(gt, vfid))
		return true;

	if (pf_exit_vf_pause_send_pause(gt, vfid))
		return true;

	if (pf_check_vf_state(gt, vfid, XE_GT_SRIOV_STATE_PAUSE_WAIT_GUC)) {
		xe_gt_sriov_dbg_verbose(gt, "VF%u in %s\n", vfid,
					control_bit_to_string(XE_GT_SRIOV_STATE_PAUSE_WAIT_GUC));
		return true;
	}

	if (pf_exit_vf_pause_guc_done(gt, vfid))
		return true;

	if (pf_exit_vf_pause_save_guc(gt, vfid))
		return true;

	if (pf_exit_vf_resume_send_resume(gt, vfid))
		return true;

	return false;
}

static unsigned int pf_control_state_index(struct xe_gt *gt,
					   struct xe_gt_sriov_control_state *cs)
{
	return container_of(cs, struct xe_gt_sriov_metadata, control) - gt->sriov.pf.vfs;
}

static void pf_worker_find_work(struct xe_gt *gt)
{
	struct xe_gt_sriov_pf_control *pfc = &gt->sriov.pf.control;
	struct xe_gt_sriov_control_state *cs;
	unsigned int vfid;
	bool empty;
	bool more;

	spin_lock(&pfc->lock);
	cs = list_first_entry_or_null(&pfc->list, struct xe_gt_sriov_control_state, link);
	if (cs)
		list_del_init(&cs->link);
	empty = list_empty(&pfc->list);
	spin_unlock(&pfc->lock);

	if (!cs)
		return;

	/* VF metadata structures are indexed by the VFID */
	vfid = pf_control_state_index(gt, cs);
	xe_gt_assert(gt, vfid <= xe_gt_sriov_pf_get_totalvfs(gt));

	more = pf_process_vf_state_machine(gt, vfid);
	if (more)
		pf_queue_vf(gt, vfid);
	else if (!empty)
		pf_queue_control_worker(gt);
}

static void control_worker_func(struct work_struct *w)
{
	struct xe_gt *gt = container_of(w, struct xe_gt, sriov.pf.control.worker);

	xe_gt_assert(gt, IS_SRIOV_PF(gt_to_xe(gt)));
	pf_worker_find_work(gt);
}

static void pf_stop_worker(struct xe_gt *gt)
{
	xe_gt_assert(gt, IS_SRIOV_PF(gt_to_xe(gt)));
	cancel_work_sync(&gt->sriov.pf.control.worker);
}

static void control_fini_action(struct drm_device *dev, void *data)
{
	struct xe_gt *gt = data;

	pf_stop_worker(gt);
}

/**
 * xe_gt_sriov_pf_control_init() - Initialize PF's control data.
 * @gt: the &xe_gt
 *
 * This function is for PF only.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_gt_sriov_pf_control_init(struct xe_gt *gt)
{
	struct xe_device *xe = gt_to_xe(gt);
	unsigned int n, totalvfs;

	xe_gt_assert(gt, IS_SRIOV_PF(xe));

	totalvfs = xe_sriov_pf_get_totalvfs(xe);
	for (n = 0; n <= totalvfs; n++) {
		struct xe_gt_sriov_control_state *cs = pf_pick_vf_control(gt, n);

		init_completion(&cs->done);
		INIT_LIST_HEAD(&cs->link);
	}

	spin_lock_init(&gt->sriov.pf.control.lock);
	INIT_LIST_HEAD(&gt->sriov.pf.control.list);
	INIT_WORK(&gt->sriov.pf.control.worker, control_worker_func);

	return drmm_add_action_or_reset(&xe->drm, control_fini_action, gt);
}

/**
 * xe_gt_sriov_pf_control_restart() - Restart SR-IOV control data after a GT reset.
 * @gt: the &xe_gt
 *
 * Any per-VF status maintained by the PF or any ongoing VF control activity
 * performed by the PF must be reset or cancelled when the GT is reset.
 *
 * This function is for PF only.
 */
void xe_gt_sriov_pf_control_restart(struct xe_gt *gt)
{
	struct xe_device *xe = gt_to_xe(gt);
	unsigned int n, totalvfs;

	xe_gt_assert(gt, IS_SRIOV_PF(xe));

	pf_stop_worker(gt);

	totalvfs = xe_sriov_pf_get_totalvfs(xe);
	for (n = 1; n <= totalvfs; n++)
		pf_enter_vf_ready(gt, n);
}
