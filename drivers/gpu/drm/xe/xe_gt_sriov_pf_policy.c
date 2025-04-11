// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023-2024 Intel Corporation
 */

#include "abi/guc_actions_sriov_abi.h"

#include "xe_bo.h"
#include "xe_gt.h"
#include "xe_gt_sriov_pf_helpers.h"
#include "xe_gt_sriov_pf_policy.h"
#include "xe_gt_sriov_printk.h"
#include "xe_guc_buf.h"
#include "xe_guc_ct.h"
#include "xe_guc_klv_helpers.h"
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

static void pf_sanitize_guc_policies(struct xe_gt *gt)
{
	pf_sanitize_sched_if_idle(gt);
	pf_sanitize_reset_engine(gt);
	pf_sanitize_sample_period(gt);
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
	mutex_unlock(xe_gt_sriov_pf_master_mutex(gt));

	xe_pm_runtime_put(gt_to_xe(gt));

	return err ? -ENXIO : 0;
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
