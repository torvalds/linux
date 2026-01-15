// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023-2024 Intel Corporation
 */

#include <drm/drm_managed.h>

#include "abi/guc_actions_sriov_abi.h"

#include "xe_gt.h"
#include "xe_gt_sriov_pf_helpers.h"
#include "xe_gt_sriov_pf_policy.h"
#include "xe_gt_sriov_printk.h"
#include "xe_guc.h"
#include "xe_guc_buf.h"
#include "xe_guc_ct.h"
#include "xe_guc_klv_helpers.h"
#include "xe_guc_submit.h"
#include "xe_pm.h"

/*
 * Return: number of KLVs that were successfully parsed and saved,
 *         negative error code on failure.
 */
static int guc_action_update_vgt_policy(struct xe_guc *guc, u64 addr, u32 size)
{
	u32 request[] = {
		GUC_ACTION_PF2GUC_UPDATE_VGT_POLICY,
		lower_32_bits(addr),
		upper_32_bits(addr),
		size,
	};

	return xe_guc_ct_send_block(&guc->ct, request, ARRAY_SIZE(request));
}

/*
 * Return: number of KLVs that were successfully parsed and saved,
 *         negative error code on failure.
 */
static int pf_send_policy_klvs(struct xe_gt *gt, struct xe_guc_buf buf, u32 num_dwords)
{
	struct xe_guc *guc = &gt->uc.guc;

	return guc_action_update_vgt_policy(guc, xe_guc_buf_flush(buf), num_dwords);
}

/*
 * Return: 0 on success, -ENOKEY if some KLVs were not updated, -EPROTO if reply was malformed,
 *         negative error code on failure.
 */
static int pf_push_policy_buf_klvs(struct xe_gt *gt, u32 num_klvs,
				   struct xe_guc_buf buf, u32 num_dwords)
{
	int ret;

	ret = pf_send_policy_klvs(gt, buf, num_dwords);

	if (ret != num_klvs) {
		int err = ret < 0 ? ret : ret < num_klvs ? -ENOKEY : -EPROTO;
		struct drm_printer p = xe_gt_info_printer(gt);
		void *klvs = xe_guc_buf_cpu_ptr(buf);

		xe_gt_sriov_notice(gt, "Failed to push %u policy KLV%s (%pe)\n",
				   num_klvs, str_plural(num_klvs), ERR_PTR(err));
		xe_guc_klv_print(klvs, num_dwords, &p);
		return err;
	}

	return 0;
}

/*
 * Return: 0 on success, -ENOBUFS if there is no free buffer for the indirect data,
 *         negative error code on failure.
 */
static int pf_push_policy_klvs(struct xe_gt *gt, u32 num_klvs,
			       const u32 *klvs, u32 num_dwords)
{
	CLASS(xe_guc_buf_from_data, buf)(&gt->uc.guc.buf, klvs, num_dwords * sizeof(u32));

	xe_gt_assert(gt, num_klvs == xe_guc_klv_count(klvs, num_dwords));

	if (!xe_guc_buf_is_valid(buf))
		return -ENOBUFS;

	return pf_push_policy_buf_klvs(gt, num_klvs, buf, num_dwords);
}

static int pf_push_policy_u32(struct xe_gt *gt, u16 key, u32 value)
{
	u32 klv[] = {
		PREP_GUC_KLV(key, 1),
		value,
	};

	return pf_push_policy_klvs(gt, 1, klv, ARRAY_SIZE(klv));
}

static int pf_push_policy_payload(struct xe_gt *gt, u16 key, void *payload, u32 num_dwords)
{
	CLASS(xe_guc_buf, buf)(&gt->uc.guc.buf, GUC_KLV_LEN_MIN + num_dwords);
	u32 *klv;

	if (!xe_guc_buf_is_valid(buf))
		return -ENOBUFS;

	klv = xe_guc_buf_cpu_ptr(buf);

	klv[0] = PREP_GUC_KLV(key, num_dwords);
	if (num_dwords)
		memcpy(&klv[1], payload, num_dwords * sizeof(u32));

	return pf_push_policy_buf_klvs(gt, 1, buf, GUC_KLV_LEN_MIN + num_dwords);
}

static int pf_update_policy_bool(struct xe_gt *gt, u16 key, bool *policy, bool value)
{
	int err;

	err = pf_push_policy_u32(gt, key, value);
	if (unlikely(err)) {
		xe_gt_sriov_notice(gt, "Failed to update policy %#x '%s' to '%s' (%pe)\n",
				   key, xe_guc_klv_key_to_string(key),
				   str_enabled_disabled(value), ERR_PTR(err));
		return err;
	}

	xe_gt_sriov_dbg(gt, "policy key %#x '%s' updated to '%s'\n",
			key, xe_guc_klv_key_to_string(key),
			str_enabled_disabled(value));

	*policy = value;
	return 0;
}

static int pf_update_policy_u32(struct xe_gt *gt, u16 key, u32 *policy, u32 value)
{
	int err;

	err = pf_push_policy_u32(gt, key, value);
	if (unlikely(err)) {
		xe_gt_sriov_notice(gt, "Failed to update policy %#x '%s' to '%s' (%pe)\n",
				   key, xe_guc_klv_key_to_string(key),
				   str_enabled_disabled(value), ERR_PTR(err));
		return err;
	}

	xe_gt_sriov_dbg(gt, "policy key %#x '%s' updated to %u\n",
			key, xe_guc_klv_key_to_string(key), value);

	*policy = value;
	return 0;
}

static void pf_bulk_reset_sched_priority(struct xe_gt *gt, u32 priority)
{
	unsigned int total_vfs = 1 + xe_gt_sriov_pf_get_totalvfs(gt);
	unsigned int n;

	xe_gt_assert(gt, IS_SRIOV_PF(gt_to_xe(gt)));
	lockdep_assert_held(xe_gt_sriov_pf_master_mutex(gt));

	for (n = 0; n < total_vfs; n++)
		gt->sriov.pf.vfs[n].config.sched_priority = priority;
}

static int pf_provision_sched_if_idle(struct xe_gt *gt, bool enable)
{
	int err;

	xe_gt_assert(gt, IS_SRIOV_PF(gt_to_xe(gt)));
	lockdep_assert_held(xe_gt_sriov_pf_master_mutex(gt));

	err = pf_update_policy_bool(gt, GUC_KLV_VGT_POLICY_SCHED_IF_IDLE_KEY,
				    &gt->sriov.pf.policy.guc.sched_if_idle,
				    enable);

	if (!err)
		pf_bulk_reset_sched_priority(gt, enable ? GUC_SCHED_PRIORITY_NORMAL :
					     GUC_SCHED_PRIORITY_LOW);
	return err;
}

static int pf_reprovision_sched_if_idle(struct xe_gt *gt)
{
	xe_gt_assert(gt, IS_SRIOV_PF(gt_to_xe(gt)));
	lockdep_assert_held(xe_gt_sriov_pf_master_mutex(gt));

	return pf_provision_sched_if_idle(gt, gt->sriov.pf.policy.guc.sched_if_idle);
}

static void pf_sanitize_sched_if_idle(struct xe_gt *gt)
{
	xe_gt_assert(gt, IS_SRIOV_PF(gt_to_xe(gt)));
	lockdep_assert_held(xe_gt_sriov_pf_master_mutex(gt));

	gt->sriov.pf.policy.guc.sched_if_idle = false;
}

/**
 * xe_gt_sriov_pf_policy_set_sched_if_idle - Control the 'sched_if_idle' policy.
 * @gt: the &xe_gt where to apply the policy
 * @enable: the value of the 'sched_if_idle' policy
 *
 * This function can only be called on PF.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_gt_sriov_pf_policy_set_sched_if_idle(struct xe_gt *gt, bool enable)
{
	int err;

	mutex_lock(xe_gt_sriov_pf_master_mutex(gt));
	err = pf_provision_sched_if_idle(gt, enable);
	mutex_unlock(xe_gt_sriov_pf_master_mutex(gt));

	return err;
}

/**
 * xe_gt_sriov_pf_policy_get_sched_if_idle - Retrieve value of 'sched_if_idle' policy.
 * @gt: the &xe_gt where to read the policy from
 *
 * This function can only be called on PF.
 *
 * Return: value of 'sched_if_idle' policy.
 */
bool xe_gt_sriov_pf_policy_get_sched_if_idle(struct xe_gt *gt)
{
	bool enable;

	xe_gt_assert(gt, IS_SRIOV_PF(gt_to_xe(gt)));

	mutex_lock(xe_gt_sriov_pf_master_mutex(gt));
	enable = gt->sriov.pf.policy.guc.sched_if_idle;
	mutex_unlock(xe_gt_sriov_pf_master_mutex(gt));

	return enable;
}

static int pf_provision_reset_engine(struct xe_gt *gt, bool enable)
{
	xe_gt_assert(gt, IS_SRIOV_PF(gt_to_xe(gt)));
	lockdep_assert_held(xe_gt_sriov_pf_master_mutex(gt));

	return pf_update_policy_bool(gt, GUC_KLV_VGT_POLICY_RESET_AFTER_VF_SWITCH_KEY,
				     &gt->sriov.pf.policy.guc.reset_engine, enable);
}

static int pf_reprovision_reset_engine(struct xe_gt *gt)
{
	xe_gt_assert(gt, IS_SRIOV_PF(gt_to_xe(gt)));
	lockdep_assert_held(xe_gt_sriov_pf_master_mutex(gt));

	return pf_provision_reset_engine(gt, gt->sriov.pf.policy.guc.reset_engine);
}

static void pf_sanitize_reset_engine(struct xe_gt *gt)
{
	xe_gt_assert(gt, IS_SRIOV_PF(gt_to_xe(gt)));
	lockdep_assert_held(xe_gt_sriov_pf_master_mutex(gt));

	gt->sriov.pf.policy.guc.reset_engine = false;
}

/**
 * xe_gt_sriov_pf_policy_set_reset_engine - Control the 'reset_engine' policy.
 * @gt: the &xe_gt where to apply the policy
 * @enable: the value of the 'reset_engine' policy
 *
 * This function can only be called on PF.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_gt_sriov_pf_policy_set_reset_engine(struct xe_gt *gt, bool enable)
{
	int err;

	mutex_lock(xe_gt_sriov_pf_master_mutex(gt));
	err = pf_provision_reset_engine(gt, enable);
	mutex_unlock(xe_gt_sriov_pf_master_mutex(gt));

	return err;
}

/**
 * xe_gt_sriov_pf_policy_get_reset_engine - Retrieve value of 'reset_engine' policy.
 * @gt: the &xe_gt where to read the policy from
 *
 * This function can only be called on PF.
 *
 * Return: value of 'reset_engine' policy.
 */
bool xe_gt_sriov_pf_policy_get_reset_engine(struct xe_gt *gt)
{
	bool enable;

	xe_gt_assert(gt, IS_SRIOV_PF(gt_to_xe(gt)));

	mutex_lock(xe_gt_sriov_pf_master_mutex(gt));
	enable = gt->sriov.pf.policy.guc.reset_engine;
	mutex_unlock(xe_gt_sriov_pf_master_mutex(gt));

	return enable;
}

static int pf_provision_sample_period(struct xe_gt *gt, u32 value)
{
	xe_gt_assert(gt, IS_SRIOV_PF(gt_to_xe(gt)));
	lockdep_assert_held(xe_gt_sriov_pf_master_mutex(gt));

	return pf_update_policy_u32(gt, GUC_KLV_VGT_POLICY_ADVERSE_SAMPLE_PERIOD_KEY,
				    &gt->sriov.pf.policy.guc.sample_period, value);
}

static int pf_reprovision_sample_period(struct xe_gt *gt)
{
	xe_gt_assert(gt, IS_SRIOV_PF(gt_to_xe(gt)));
	lockdep_assert_held(xe_gt_sriov_pf_master_mutex(gt));

	return pf_provision_sample_period(gt, gt->sriov.pf.policy.guc.sample_period);
}

static void pf_sanitize_sample_period(struct xe_gt *gt)
{
	xe_gt_assert(gt, IS_SRIOV_PF(gt_to_xe(gt)));
	lockdep_assert_held(xe_gt_sriov_pf_master_mutex(gt));

	gt->sriov.pf.policy.guc.sample_period = 0;
}

/**
 * xe_gt_sriov_pf_policy_set_sample_period - Control the 'sample_period' policy.
 * @gt: the &xe_gt where to apply the policy
 * @value: the value of the 'sample_period' policy
 *
 * This function can only be called on PF.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_gt_sriov_pf_policy_set_sample_period(struct xe_gt *gt, u32 value)
{
	int err;

	mutex_lock(xe_gt_sriov_pf_master_mutex(gt));
	err = pf_provision_sample_period(gt, value);
	mutex_unlock(xe_gt_sriov_pf_master_mutex(gt));

	return err;
}

/**
 * xe_gt_sriov_pf_policy_get_sample_period - Retrieve value of 'sample_period' policy.
 * @gt: the &xe_gt where to read the policy from
 *
 * This function can only be called on PF.
 *
 * Return: value of 'sample_period' policy.
 */
u32 xe_gt_sriov_pf_policy_get_sample_period(struct xe_gt *gt)
{
	u32 value;

	xe_gt_assert(gt, IS_SRIOV_PF(gt_to_xe(gt)));

	mutex_lock(xe_gt_sriov_pf_master_mutex(gt));
	value = gt->sriov.pf.policy.guc.sample_period;
	mutex_unlock(xe_gt_sriov_pf_master_mutex(gt));

	return value;
}

static void pf_sched_group_media_slices(struct xe_gt *gt, struct guc_sched_group **groups,
					u32 *num_groups)
{
	u8 slice_to_group[MAX_MEDIA_SLICES];
	u32 vecs_mask = VECS_INSTANCES(gt);
	u32 gsc_mask = GSCCS_INSTANCES(gt);
	u32 vcs_mask = VCS_INSTANCES(gt);
	struct guc_sched_group *values;
	struct xe_hw_engine *hwe;
	enum xe_hw_engine_id id;
	int group = 0;
	int slice;

	xe_gt_assert(gt, xe_gt_is_media_type(gt));

	/*
	 * Post-BMG the matching of video engines to slices changes, so for now
	 * we don't allow this mode on those platforms.
	 */
	if (gt_to_xe(gt)->info.platform > XE_BATTLEMAGE)
		return;

	/*
	 * On BMG and older platforms a media slice has 2 VCS and a VECS. We
	 * bundle the GSC with the first slice.
	 */
	for (slice = 0; slice < MAX_MEDIA_SLICES; slice++) {
		if ((vcs_mask & 0x3) || (vecs_mask & 0x1) || (gsc_mask & 0x1))
			slice_to_group[slice] = group++;

		vcs_mask >>= 2;
		vecs_mask >>= 1;
		gsc_mask >>= 1;
	}

	xe_gt_assert(gt, !vcs_mask);
	xe_gt_assert(gt, !vecs_mask);
	xe_gt_assert(gt, !gsc_mask);

	/* We need at least 2 slices to split them up */
	if (group < 2)
		return;

	/*
	 * If we have more groups than the GuC can support then we don't want to
	 * expose this specific mode, because the GuC will return an error if we
	 * try to enable it.
	 */
	if (group > gt->sriov.pf.policy.guc.sched_groups.max_groups) {
		xe_gt_sriov_notice(gt, "media_slice mode has too many groups: %u vs %u\n",
				   group, gt->sriov.pf.policy.guc.sched_groups.max_groups);
		return;
	}

	/* The GuC expects an array with a guc_sched_group entry for each group */
	values = drmm_kcalloc(&gt_to_xe(gt)->drm, group, sizeof(struct guc_sched_group),
			      GFP_KERNEL);
	if (!values)
		return;

	for_each_hw_engine(hwe, gt, id) {
		u8 guc_class = xe_engine_class_to_guc_class(hwe->class);

		switch (hwe->class) {
		case XE_ENGINE_CLASS_VIDEO_DECODE:
			slice = hwe->instance / 2;
			break;
		case XE_ENGINE_CLASS_VIDEO_ENHANCE:
			slice = hwe->instance;
			break;
		case XE_ENGINE_CLASS_OTHER:
			slice = 0;
			break;
		default:
			xe_gt_assert_msg(gt, false,
					 "unknown media gt class %u (%s) during EGS setup\n",
					 hwe->class, hwe->name);
			slice = 0;
		}

		values[slice_to_group[slice]].engines[guc_class] |= BIT(hwe->logical_instance);
	}

	*groups = values;
	*num_groups = group;
}

/**
 * xe_sriov_gt_pf_policy_has_sched_groups_support() - Checks whether scheduler
 * groups are supported.
 * @gt: the &xe_gt
 *
 * This function can only be called on PF.
 *
 * Return: true if scheduler groups are supported, false otherwise.
 */
bool xe_sriov_gt_pf_policy_has_sched_groups_support(struct xe_gt *gt)
{
	xe_gt_assert(gt, IS_SRIOV_PF(gt_to_xe(gt)));

	/*
	 * The GuC supports scheduler groups from v70.53.0, but a fix for it has
	 * been merged in v70.55.1, so we require the latter. The feature is
	 * also only enabled on BMG and newer FW.
	 */
	return GUC_FIRMWARE_VER_AT_LEAST(&gt->uc.guc, 70, 55, 1) &&
	       gt_to_xe(gt)->info.platform >= XE_BATTLEMAGE;
}

static void pf_init_sched_groups(struct xe_gt *gt)
{
	enum xe_sriov_sched_group_modes m;

	if (!xe_sriov_gt_pf_policy_has_sched_groups_support(gt))
		return;

	/*
	 * The GuC interface supports up to 8 groups. However, the GuC only
	 * fully allocates resources for a subset of groups, based on the number
	 * of engines and expected usage. The plan is for this to become
	 * queryable via H2G, but for now GuC FW for all devices supports a
	 * maximum of 2 groups so we can just hardcode that.
	 */
	gt->sriov.pf.policy.guc.sched_groups.max_groups = 2;

	for (m = XE_SRIOV_SCHED_GROUPS_DISABLED + 1; m < XE_SRIOV_SCHED_GROUPS_MODES_COUNT; m++) {
		u32 *num_groups = &gt->sriov.pf.policy.guc.sched_groups.modes[m].num_groups;
		struct guc_sched_group **groups =
			&gt->sriov.pf.policy.guc.sched_groups.modes[m].groups;

		switch (m) {
		case XE_SRIOV_SCHED_GROUPS_MEDIA_SLICES:
			/* this mode only has groups on the media GT */
			if (xe_gt_is_media_type(gt))
				pf_sched_group_media_slices(gt, groups, num_groups);
			break;
		case XE_SRIOV_SCHED_GROUPS_DISABLED:
		case XE_SRIOV_SCHED_GROUPS_MODES_COUNT:
			/*
			 * By defining m of type enum xe_sriov_sched_group_modes
			 * we can get the compiler to automatically flag
			 * missing cases if new enum entries are added. However,
			 * to keep the compiler happy we also need to add the
			 * cases that are excluded from the loop.
			 */
			xe_gt_assert(gt, false);
			break;
		}

		xe_gt_assert(gt, *num_groups < GUC_MAX_SCHED_GROUPS);

		if (*num_groups)
			gt->sriov.pf.policy.guc.sched_groups.supported_modes |= BIT(m);
	}
}

/**
 * xe_sriov_gt_pf_policy_has_multi_group_modes() - check whether the GT supports
 * any scheduler modes that have multiple groups
 * @gt: the &xe_gt to check
 *
 * This function can only be called on PF.
 *
 * Return: true if the GT supports modes with multiple groups, false otherwise.
 */
bool xe_sriov_gt_pf_policy_has_multi_group_modes(struct xe_gt *gt)
{
	return gt->sriov.pf.policy.guc.sched_groups.supported_modes;
}

/**
 * xe_sriov_gt_pf_policy_has_sched_group_mode() - check whether the GT supports
 * a specific scheduler group mode
 * @gt: the &xe_gt to check
 * @mode: the mode to check
 *
 * This function can only be called on PF.
 *
 * Return: true if the GT supports the specified mode, false otherwise.
 */
bool xe_sriov_gt_pf_policy_has_sched_group_mode(struct xe_gt *gt,
						enum xe_sriov_sched_group_modes mode)
{
	if (mode == XE_SRIOV_SCHED_GROUPS_DISABLED)
		return true;

	return gt->sriov.pf.policy.guc.sched_groups.supported_modes & BIT(mode);
}

static int __pf_provision_sched_groups(struct xe_gt *gt, enum xe_sriov_sched_group_modes mode)
{
	struct guc_sched_group *groups = gt->sriov.pf.policy.guc.sched_groups.modes[mode].groups;
	u32 num_groups = gt->sriov.pf.policy.guc.sched_groups.modes[mode].num_groups;

	return pf_push_policy_payload(gt, GUC_KLV_VGT_POLICY_ENGINE_GROUP_CONFIG_KEY,
				      groups, num_groups * GUC_MAX_ENGINE_CLASSES);
}

static int pf_provision_sched_groups(struct xe_gt *gt, enum xe_sriov_sched_group_modes mode)
{
	int err;

	xe_gt_assert(gt, IS_SRIOV_PF(gt_to_xe(gt)));
	lockdep_assert_held(xe_gt_sriov_pf_master_mutex(gt));

	if (!xe_sriov_gt_pf_policy_has_sched_group_mode(gt, mode))
		return -EINVAL;

	/* already in the desired mode */
	if (gt->sriov.pf.policy.guc.sched_groups.current_mode == mode)
		return 0;

	/*
	 * We don't allow changing this with VFs active since it is hard for
	 * VFs to check.
	 */
	if (xe_sriov_pf_num_vfs(gt_to_xe(gt)))
		return -EBUSY;

	/*
	 * The GuC silently ignores the setting if any MLRC contexts are
	 * registered. We expect the admin to make sure that all apps that use
	 * MLRC are terminated before scheduler groups are enabled, so this
	 * check is just to make sure that the exec_queue destruction has been
	 * completed.
	 */
	if (mode != XE_SRIOV_SCHED_GROUPS_DISABLED &&
	    xe_guc_has_registered_mlrc_queues(&gt->uc.guc)) {
		xe_gt_sriov_notice(gt, "can't enable sched groups with active MLRC queues\n");
		return -EPERM;
	}

	err = __pf_provision_sched_groups(gt, mode);
	if (err)
		return err;

	gt->sriov.pf.policy.guc.sched_groups.current_mode = mode;

	return 0;
}

static int pf_reprovision_sched_groups(struct xe_gt *gt)
{
	xe_gt_assert(gt, IS_SRIOV_PF(gt_to_xe(gt)));
	lockdep_assert_held(xe_gt_sriov_pf_master_mutex(gt));

	/* We only have something to provision if we have possible groups */
	if (!xe_sriov_gt_pf_policy_has_multi_group_modes(gt))
		return 0;

	return __pf_provision_sched_groups(gt, gt->sriov.pf.policy.guc.sched_groups.current_mode);
}

static void pf_sanitize_sched_groups(struct xe_gt *gt)
{
	xe_gt_assert(gt, IS_SRIOV_PF(gt_to_xe(gt)));
	lockdep_assert_held(xe_gt_sriov_pf_master_mutex(gt));

	gt->sriov.pf.policy.guc.sched_groups.current_mode = XE_SRIOV_SCHED_GROUPS_DISABLED;
}

/**
 * xe_gt_sriov_pf_policy_set_sched_groups_mode() - Control the 'sched_groups' policy.
 * @gt: the &xe_gt where to apply the policy
 * @mode: the sched_group mode to be activated
 *
 * This function can only be called on PF.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_gt_sriov_pf_policy_set_sched_groups_mode(struct xe_gt *gt,
						enum xe_sriov_sched_group_modes mode)
{
	if (!xe_sriov_gt_pf_policy_has_multi_group_modes(gt))
		return -ENODEV;

	guard(mutex)(xe_gt_sriov_pf_master_mutex(gt));
	return pf_provision_sched_groups(gt, mode);
}

/**
 * xe_gt_sriov_pf_policy_sched_groups_enabled() - check whether the GT has
 * multiple scheduler groups enabled
 * @gt: the &xe_gt to check
 *
 * This function can only be called on PF.
 *
 * Return: true if the GT has multiple groups enabled, false otherwise.
 */
bool xe_gt_sriov_pf_policy_sched_groups_enabled(struct xe_gt *gt)
{
	return gt->sriov.pf.policy.guc.sched_groups.current_mode != XE_SRIOV_SCHED_GROUPS_DISABLED;
}

static void pf_sanitize_guc_policies(struct xe_gt *gt)
{
	pf_sanitize_sched_if_idle(gt);
	pf_sanitize_reset_engine(gt);
	pf_sanitize_sample_period(gt);
	pf_sanitize_sched_groups(gt);
}

/**
 * xe_gt_sriov_pf_policy_sanitize - Reset policy settings.
 * @gt: the &xe_gt
 *
 * This function can only be called on PF.
 *
 * Return: 0 on success or a negative error code on failure.
 */
void xe_gt_sriov_pf_policy_sanitize(struct xe_gt *gt)
{
	mutex_lock(xe_gt_sriov_pf_master_mutex(gt));
	pf_sanitize_guc_policies(gt);
	mutex_unlock(xe_gt_sriov_pf_master_mutex(gt));
}

/**
 * xe_gt_sriov_pf_policy_reprovision - Reprovision (and optionally reset) policy settings.
 * @gt: the &xe_gt
 * @reset: if true will reprovision using default values instead of latest
 *
 * This function can only be called on PF.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_gt_sriov_pf_policy_reprovision(struct xe_gt *gt, bool reset)
{
	int err = 0;

	xe_pm_runtime_get_noresume(gt_to_xe(gt));

	mutex_lock(xe_gt_sriov_pf_master_mutex(gt));
	if (reset)
		pf_sanitize_guc_policies(gt);
	err |= pf_reprovision_sched_if_idle(gt);
	err |= pf_reprovision_reset_engine(gt);
	err |= pf_reprovision_sample_period(gt);
	err |= pf_reprovision_sched_groups(gt);
	mutex_unlock(xe_gt_sriov_pf_master_mutex(gt));

	xe_pm_runtime_put(gt_to_xe(gt));

	return err ? -ENXIO : 0;
}

/**
 * xe_gt_sriov_pf_policy_init() - Initializes the SW state of the PF policies.
 * @gt: the &xe_gt
 *
 * This function can only be called on PF. This function does not touch the HW,
 * but must be called after the engines have been initialized.
 */
void xe_gt_sriov_pf_policy_init(struct xe_gt *gt)
{
	pf_init_sched_groups(gt);
}

static void print_guc_policies(struct drm_printer *p, struct xe_gt_sriov_guc_policies *policy)
{
	drm_printf(p, "%s:\t%s\n",
		   xe_guc_klv_key_to_string(GUC_KLV_VGT_POLICY_SCHED_IF_IDLE_KEY),
		   str_enabled_disabled(policy->sched_if_idle));
	drm_printf(p, "%s:\t%s\n",
		   xe_guc_klv_key_to_string(GUC_KLV_VGT_POLICY_RESET_AFTER_VF_SWITCH_KEY),
		   str_enabled_disabled(policy->reset_engine));
	drm_printf(p, "%s:\t%u %s\n",
		   xe_guc_klv_key_to_string(GUC_KLV_VGT_POLICY_ADVERSE_SAMPLE_PERIOD_KEY),
		   policy->sample_period, policy->sample_period ? "ms" : "(disabled)");
}

/**
 * xe_gt_sriov_pf_policy_print - Dump actual policy values.
 * @gt: the &xe_gt where to read the policy from
 * @p: the &drm_printer
 *
 * This function can only be called on PF.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_gt_sriov_pf_policy_print(struct xe_gt *gt, struct drm_printer *p)
{
	xe_gt_assert(gt, IS_SRIOV_PF(gt_to_xe(gt)));

	mutex_lock(xe_gt_sriov_pf_master_mutex(gt));
	print_guc_policies(p, &gt->sriov.pf.policy.guc);
	mutex_unlock(xe_gt_sriov_pf_master_mutex(gt));

	return 0;
}
