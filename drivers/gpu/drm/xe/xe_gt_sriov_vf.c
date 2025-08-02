// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023-2024 Intel Corporation
 */

#include <linux/bitfield.h>
#include <linux/bsearch.h>

#include <drm/drm_managed.h>
#include <drm/drm_print.h>

#include "abi/guc_actions_sriov_abi.h"
#include "abi/guc_communication_mmio_abi.h"
#include "abi/guc_klvs_abi.h"
#include "abi/guc_relay_actions_abi.h"
#include "regs/xe_gt_regs.h"
#include "regs/xe_gtt_defs.h"

#include "xe_assert.h"
#include "xe_device.h"
#include "xe_ggtt.h"
#include "xe_gt_sriov_printk.h"
#include "xe_gt_sriov_vf.h"
#include "xe_gt_sriov_vf_types.h"
#include "xe_guc.h"
#include "xe_guc_hxg_helpers.h"
#include "xe_guc_relay.h"
#include "xe_lrc.h"
#include "xe_mmio.h"
#include "xe_sriov.h"
#include "xe_sriov_vf.h"
#include "xe_uc_fw.h"
#include "xe_wopcm.h"

#define make_u64_from_u32(hi, lo) ((u64)((u64)(u32)(hi) << 32 | (u32)(lo)))

static int guc_action_vf_reset(struct xe_guc *guc)
{
	u32 request[GUC_HXG_REQUEST_MSG_MIN_LEN] = {
		FIELD_PREP(GUC_HXG_MSG_0_ORIGIN, GUC_HXG_ORIGIN_HOST) |
		FIELD_PREP(GUC_HXG_MSG_0_TYPE, GUC_HXG_TYPE_REQUEST) |
		FIELD_PREP(GUC_HXG_REQUEST_MSG_0_ACTION, GUC_ACTION_VF2GUC_VF_RESET),
	};
	int ret;

	ret = xe_guc_mmio_send(guc, request, ARRAY_SIZE(request));

	return ret > 0 ? -EPROTO : ret;
}

#define GUC_RESET_VF_STATE_RETRY_MAX	10
static int vf_reset_guc_state(struct xe_gt *gt)
{
	unsigned int retry = GUC_RESET_VF_STATE_RETRY_MAX;
	struct xe_guc *guc = &gt->uc.guc;
	int err;

	do {
		err = guc_action_vf_reset(guc);
		if (!err || err != -ETIMEDOUT)
			break;
	} while (--retry);

	if (unlikely(err))
		xe_gt_sriov_err(gt, "Failed to reset GuC state (%pe)\n", ERR_PTR(err));
	return err;
}

/**
 * xe_gt_sriov_vf_reset - Reset GuC VF internal state.
 * @gt: the &xe_gt
 *
 * It requires functional `GuC MMIO based communication`_.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_gt_sriov_vf_reset(struct xe_gt *gt)
{
	if (!xe_device_uc_enabled(gt_to_xe(gt)))
		return -ENODEV;

	return vf_reset_guc_state(gt);
}

static int guc_action_match_version(struct xe_guc *guc,
				    struct xe_uc_fw_version *wanted,
				    struct xe_uc_fw_version *found)
{
	u32 request[VF2GUC_MATCH_VERSION_REQUEST_MSG_LEN] = {
		FIELD_PREP(GUC_HXG_MSG_0_ORIGIN, GUC_HXG_ORIGIN_HOST) |
		FIELD_PREP(GUC_HXG_MSG_0_TYPE, GUC_HXG_TYPE_REQUEST) |
		FIELD_PREP(GUC_HXG_REQUEST_MSG_0_ACTION,
			   GUC_ACTION_VF2GUC_MATCH_VERSION),
		FIELD_PREP(VF2GUC_MATCH_VERSION_REQUEST_MSG_1_BRANCH, wanted->branch) |
		FIELD_PREP(VF2GUC_MATCH_VERSION_REQUEST_MSG_1_MAJOR, wanted->major) |
		FIELD_PREP(VF2GUC_MATCH_VERSION_REQUEST_MSG_1_MINOR, wanted->minor),
	};
	u32 response[GUC_MAX_MMIO_MSG_LEN];
	int ret;

	BUILD_BUG_ON(VF2GUC_MATCH_VERSION_RESPONSE_MSG_LEN > GUC_MAX_MMIO_MSG_LEN);

	ret = xe_guc_mmio_send_recv(guc, request, ARRAY_SIZE(request), response);
	if (unlikely(ret < 0))
		return ret;

	if (unlikely(FIELD_GET(VF2GUC_MATCH_VERSION_RESPONSE_MSG_0_MBZ, response[0])))
		return -EPROTO;

	memset(found, 0, sizeof(struct xe_uc_fw_version));
	found->branch = FIELD_GET(VF2GUC_MATCH_VERSION_RESPONSE_MSG_1_BRANCH, response[1]);
	found->major = FIELD_GET(VF2GUC_MATCH_VERSION_RESPONSE_MSG_1_MAJOR, response[1]);
	found->minor = FIELD_GET(VF2GUC_MATCH_VERSION_RESPONSE_MSG_1_MINOR, response[1]);
	found->patch = FIELD_GET(VF2GUC_MATCH_VERSION_RESPONSE_MSG_1_PATCH, response[1]);

	return 0;
}

static int guc_action_match_version_any(struct xe_guc *guc,
					struct xe_uc_fw_version *found)
{
	struct xe_uc_fw_version wanted = {
		.branch = GUC_VERSION_BRANCH_ANY,
		.major = GUC_VERSION_MAJOR_ANY,
		.minor = GUC_VERSION_MINOR_ANY,
		.patch = 0
	};

	return guc_action_match_version(guc, &wanted, found);
}

static void vf_minimum_guc_version(struct xe_gt *gt, struct xe_uc_fw_version *ver)
{
	struct xe_device *xe = gt_to_xe(gt);

	memset(ver, 0, sizeof(struct xe_uc_fw_version));

	switch (xe->info.platform) {
	case XE_TIGERLAKE ... XE_PVC:
		/* 1.1 this is current baseline for Xe driver */
		ver->branch = 0;
		ver->major = 1;
		ver->minor = 1;
		break;
	default:
		/* 1.2 has support for the GMD_ID KLV */
		ver->branch = 0;
		ver->major = 1;
		ver->minor = 2;
		break;
	}
}

static void vf_wanted_guc_version(struct xe_gt *gt, struct xe_uc_fw_version *ver)
{
	/* for now it's the same as minimum */
	return vf_minimum_guc_version(gt, ver);
}

static int vf_handshake_with_guc(struct xe_gt *gt)
{
	struct xe_uc_fw_version *guc_version = &gt->sriov.vf.guc_version;
	struct xe_uc_fw_version wanted = {0};
	struct xe_guc *guc = &gt->uc.guc;
	bool old = false;
	int err;

	xe_gt_assert(gt, IS_SRIOV_VF(gt_to_xe(gt)));

	/* select wanted version - prefer previous (if any) */
	if (guc_version->major || guc_version->minor) {
		wanted = *guc_version;
		old = true;
	} else {
		vf_wanted_guc_version(gt, &wanted);
		xe_gt_assert(gt, wanted.major != GUC_VERSION_MAJOR_ANY);

		/* First time we handshake, so record the minimum wanted */
		gt->sriov.vf.wanted_guc_version = wanted;
	}

	err = guc_action_match_version(guc, &wanted, guc_version);
	if (unlikely(err))
		goto fail;

	if (old) {
		/* we don't support interface version change */
		if (MAKE_GUC_VER_STRUCT(*guc_version) != MAKE_GUC_VER_STRUCT(wanted)) {
			xe_gt_sriov_err(gt, "New GuC interface version detected: %u.%u.%u.%u\n",
					guc_version->branch, guc_version->major,
					guc_version->minor, guc_version->patch);
			xe_gt_sriov_info(gt, "Previously used version was: %u.%u.%u.%u\n",
					 wanted.branch, wanted.major,
					 wanted.minor, wanted.patch);
			err = -EREMCHG;
			goto fail;
		} else {
			/* version is unchanged, no need to re-verify it */
			return 0;
		}
	}

	/* illegal */
	if (guc_version->major > wanted.major) {
		err = -EPROTO;
		goto unsupported;
	}

	/* there's no fallback on major version. */
	if (guc_version->major != wanted.major) {
		err = -ENOPKG;
		goto unsupported;
	}

	/* check against minimum version supported by us */
	vf_minimum_guc_version(gt, &wanted);
	xe_gt_assert(gt, wanted.major != GUC_VERSION_MAJOR_ANY);
	if (MAKE_GUC_VER_STRUCT(*guc_version) < MAKE_GUC_VER_STRUCT(wanted)) {
		err = -ENOKEY;
		goto unsupported;
	}

	xe_gt_sriov_dbg(gt, "using GuC interface version %u.%u.%u.%u\n",
			guc_version->branch, guc_version->major,
			guc_version->minor, guc_version->patch);

	return 0;

unsupported:
	xe_gt_sriov_err(gt, "Unsupported GuC version %u.%u.%u.%u (%pe)\n",
			guc_version->branch, guc_version->major,
			guc_version->minor, guc_version->patch,
			ERR_PTR(err));
fail:
	xe_gt_sriov_err(gt, "Unable to confirm GuC version %u.%u (%pe)\n",
			wanted.major, wanted.minor, ERR_PTR(err));

	/* try again with *any* just to query which version is supported */
	if (!guc_action_match_version_any(guc, &wanted))
		xe_gt_sriov_notice(gt, "GuC reports interface version %u.%u.%u.%u\n",
				   wanted.branch, wanted.major, wanted.minor, wanted.patch);
	return err;
}

/**
 * xe_gt_sriov_vf_bootstrap - Query and setup GuC ABI interface version.
 * @gt: the &xe_gt
 *
 * This function is for VF use only.
 * It requires functional `GuC MMIO based communication`_.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_gt_sriov_vf_bootstrap(struct xe_gt *gt)
{
	int err;

	if (!xe_device_uc_enabled(gt_to_xe(gt)))
		return -ENODEV;

	err = vf_reset_guc_state(gt);
	if (unlikely(err))
		return err;

	err = vf_handshake_with_guc(gt);
	if (unlikely(err))
		return err;

	return 0;
}

/**
 * xe_gt_sriov_vf_guc_versions - Minimum required and found GuC ABI versions
 * @gt: the &xe_gt
 * @wanted: pointer to the xe_uc_fw_version to be filled with the wanted version
 * @found: pointer to the xe_uc_fw_version to be filled with the found version
 *
 * This function is for VF use only and it can only be used after successful
 * version handshake with the GuC.
 */
void xe_gt_sriov_vf_guc_versions(struct xe_gt *gt,
				 struct xe_uc_fw_version *wanted,
				 struct xe_uc_fw_version *found)
{
	xe_gt_assert(gt, IS_SRIOV_VF(gt_to_xe(gt)));
	xe_gt_assert(gt, gt->sriov.vf.guc_version.major);

	if (wanted)
		*wanted = gt->sriov.vf.wanted_guc_version;

	if (found)
		*found = gt->sriov.vf.guc_version;
}

static int guc_action_vf_notify_resfix_done(struct xe_guc *guc)
{
	u32 request[GUC_HXG_REQUEST_MSG_MIN_LEN] = {
		FIELD_PREP(GUC_HXG_MSG_0_ORIGIN, GUC_HXG_ORIGIN_HOST) |
		FIELD_PREP(GUC_HXG_MSG_0_TYPE, GUC_HXG_TYPE_REQUEST) |
		FIELD_PREP(GUC_HXG_REQUEST_MSG_0_ACTION, GUC_ACTION_VF2GUC_NOTIFY_RESFIX_DONE),
	};
	int ret;

	ret = xe_guc_mmio_send(guc, request, ARRAY_SIZE(request));

	return ret > 0 ? -EPROTO : ret;
}

/**
 * xe_gt_sriov_vf_notify_resfix_done - Notify GuC about resource fixups apply completed.
 * @gt: the &xe_gt struct instance linked to target GuC
 *
 * Returns: 0 if the operation completed successfully, or a negative error
 * code otherwise.
 */
int xe_gt_sriov_vf_notify_resfix_done(struct xe_gt *gt)
{
	struct xe_guc *guc = &gt->uc.guc;
	int err;

	xe_gt_assert(gt, IS_SRIOV_VF(gt_to_xe(gt)));

	err = guc_action_vf_notify_resfix_done(guc);
	if (unlikely(err))
		xe_gt_sriov_err(gt, "Failed to notify GuC about resource fixup done (%pe)\n",
				ERR_PTR(err));
	else
		xe_gt_sriov_dbg_verbose(gt, "sent GuC resource fixup done\n");

	return err;
}

static int guc_action_query_single_klv(struct xe_guc *guc, u32 key,
				       u32 *value, u32 value_len)
{
	u32 request[VF2GUC_QUERY_SINGLE_KLV_REQUEST_MSG_LEN] = {
		FIELD_PREP(GUC_HXG_MSG_0_ORIGIN, GUC_HXG_ORIGIN_HOST) |
		FIELD_PREP(GUC_HXG_MSG_0_TYPE, GUC_HXG_TYPE_REQUEST) |
		FIELD_PREP(GUC_HXG_REQUEST_MSG_0_ACTION,
			   GUC_ACTION_VF2GUC_QUERY_SINGLE_KLV),
		FIELD_PREP(VF2GUC_QUERY_SINGLE_KLV_REQUEST_MSG_1_KEY, key),
	};
	u32 response[GUC_MAX_MMIO_MSG_LEN];
	u32 length;
	int ret;

	BUILD_BUG_ON(VF2GUC_QUERY_SINGLE_KLV_RESPONSE_MSG_MAX_LEN > GUC_MAX_MMIO_MSG_LEN);
	ret = xe_guc_mmio_send_recv(guc, request, ARRAY_SIZE(request), response);
	if (unlikely(ret < 0))
		return ret;

	if (unlikely(FIELD_GET(VF2GUC_QUERY_SINGLE_KLV_RESPONSE_MSG_0_MBZ, response[0])))
		return -EPROTO;

	length = FIELD_GET(VF2GUC_QUERY_SINGLE_KLV_RESPONSE_MSG_0_LENGTH, response[0]);
	if (unlikely(length > value_len))
		return -EOVERFLOW;
	if (unlikely(length < value_len))
		return -ENODATA;

	switch (value_len) {
	default:
		xe_gt_WARN_ON(guc_to_gt(guc), value_len > 3);
		fallthrough;
	case 3:
		value[2] = FIELD_GET(VF2GUC_QUERY_SINGLE_KLV_RESPONSE_MSG_3_VALUE96, response[3]);
		fallthrough;
	case 2:
		value[1] = FIELD_GET(VF2GUC_QUERY_SINGLE_KLV_RESPONSE_MSG_2_VALUE64, response[2]);
		fallthrough;
	case 1:
		value[0] = FIELD_GET(VF2GUC_QUERY_SINGLE_KLV_RESPONSE_MSG_1_VALUE32, response[1]);
		fallthrough;
	case 0:
		break;
	}

	return 0;
}

static int guc_action_query_single_klv32(struct xe_guc *guc, u32 key, u32 *value32)
{
	return guc_action_query_single_klv(guc, key, value32, hxg_sizeof(u32));
}

static int guc_action_query_single_klv64(struct xe_guc *guc, u32 key, u64 *value64)
{
	u32 value[2];
	int err;

	err = guc_action_query_single_klv(guc, key, value, hxg_sizeof(value));
	if (unlikely(err))
		return err;

	*value64 = make_u64_from_u32(value[1], value[0]);
	return 0;
}

static bool has_gmdid(struct xe_device *xe)
{
	return GRAPHICS_VERx100(xe) >= 1270;
}

/**
 * xe_gt_sriov_vf_gmdid - Query GMDID over MMIO.
 * @gt: the &xe_gt
 *
 * This function is for VF use only.
 *
 * Return: value of GMDID KLV on success or 0 on failure.
 */
u32 xe_gt_sriov_vf_gmdid(struct xe_gt *gt)
{
	const char *type = xe_gt_is_media_type(gt) ? "media" : "graphics";
	struct xe_guc *guc = &gt->uc.guc;
	u32 value;
	int err;

	xe_gt_assert(gt, IS_SRIOV_VF(gt_to_xe(gt)));
	xe_gt_assert(gt, !GRAPHICS_VERx100(gt_to_xe(gt)) || has_gmdid(gt_to_xe(gt)));
	xe_gt_assert(gt, gt->sriov.vf.guc_version.major > 1 || gt->sriov.vf.guc_version.minor >= 2);

	err = guc_action_query_single_klv32(guc, GUC_KLV_GLOBAL_CFG_GMD_ID_KEY, &value);
	if (unlikely(err)) {
		xe_gt_sriov_err(gt, "Failed to obtain %s GMDID (%pe)\n",
				type, ERR_PTR(err));
		return 0;
	}

	xe_gt_sriov_dbg(gt, "%s GMDID = %#x\n", type, value);
	return value;
}

static int vf_get_ggtt_info(struct xe_gt *gt)
{
	struct xe_gt_sriov_vf_selfconfig *config = &gt->sriov.vf.self_config;
	struct xe_guc *guc = &gt->uc.guc;
	u64 start, size;
	int err;

	xe_gt_assert(gt, IS_SRIOV_VF(gt_to_xe(gt)));

	err = guc_action_query_single_klv64(guc, GUC_KLV_VF_CFG_GGTT_START_KEY, &start);
	if (unlikely(err))
		return err;

	err = guc_action_query_single_klv64(guc, GUC_KLV_VF_CFG_GGTT_SIZE_KEY, &size);
	if (unlikely(err))
		return err;

	if (config->ggtt_size && config->ggtt_size != size) {
		xe_gt_sriov_err(gt, "Unexpected GGTT reassignment: %lluK != %lluK\n",
				size / SZ_1K, config->ggtt_size / SZ_1K);
		return -EREMCHG;
	}

	xe_gt_sriov_dbg_verbose(gt, "GGTT %#llx-%#llx = %lluK\n",
				start, start + size - 1, size / SZ_1K);

	config->ggtt_shift = start - (s64)config->ggtt_base;
	config->ggtt_base = start;
	config->ggtt_size = size;

	return config->ggtt_size ? 0 : -ENODATA;
}

static int vf_get_lmem_info(struct xe_gt *gt)
{
	struct xe_gt_sriov_vf_selfconfig *config = &gt->sriov.vf.self_config;
	struct xe_guc *guc = &gt->uc.guc;
	char size_str[10];
	u64 size;
	int err;

	xe_gt_assert(gt, IS_SRIOV_VF(gt_to_xe(gt)));

	err = guc_action_query_single_klv64(guc, GUC_KLV_VF_CFG_LMEM_SIZE_KEY, &size);
	if (unlikely(err))
		return err;

	if (config->lmem_size && config->lmem_size != size) {
		xe_gt_sriov_err(gt, "Unexpected LMEM reassignment: %lluM != %lluM\n",
				size / SZ_1M, config->lmem_size / SZ_1M);
		return -EREMCHG;
	}

	string_get_size(size, 1, STRING_UNITS_2, size_str, sizeof(size_str));
	xe_gt_sriov_dbg_verbose(gt, "LMEM %lluM %s\n", size / SZ_1M, size_str);

	config->lmem_size = size;

	return config->lmem_size ? 0 : -ENODATA;
}

static int vf_get_submission_cfg(struct xe_gt *gt)
{
	struct xe_gt_sriov_vf_selfconfig *config = &gt->sriov.vf.self_config;
	struct xe_guc *guc = &gt->uc.guc;
	u32 num_ctxs, num_dbs;
	int err;

	xe_gt_assert(gt, IS_SRIOV_VF(gt_to_xe(gt)));

	err = guc_action_query_single_klv32(guc, GUC_KLV_VF_CFG_NUM_CONTEXTS_KEY, &num_ctxs);
	if (unlikely(err))
		return err;

	err = guc_action_query_single_klv32(guc, GUC_KLV_VF_CFG_NUM_DOORBELLS_KEY, &num_dbs);
	if (unlikely(err))
		return err;

	if (config->num_ctxs && config->num_ctxs != num_ctxs) {
		xe_gt_sriov_err(gt, "Unexpected CTXs reassignment: %u != %u\n",
				num_ctxs, config->num_ctxs);
		return -EREMCHG;
	}
	if (config->num_dbs && config->num_dbs != num_dbs) {
		xe_gt_sriov_err(gt, "Unexpected DBs reassignment: %u != %u\n",
				num_dbs, config->num_dbs);
		return -EREMCHG;
	}

	xe_gt_sriov_dbg_verbose(gt, "CTXs %u DBs %u\n", num_ctxs, num_dbs);

	config->num_ctxs = num_ctxs;
	config->num_dbs = num_dbs;

	return config->num_ctxs ? 0 : -ENODATA;
}

static void vf_cache_gmdid(struct xe_gt *gt)
{
	xe_gt_assert(gt, has_gmdid(gt_to_xe(gt)));
	xe_gt_assert(gt, IS_SRIOV_VF(gt_to_xe(gt)));

	gt->sriov.vf.runtime.gmdid = xe_gt_sriov_vf_gmdid(gt);
}

/**
 * xe_gt_sriov_vf_query_config - Query SR-IOV config data over MMIO.
 * @gt: the &xe_gt
 *
 * This function is for VF use only.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_gt_sriov_vf_query_config(struct xe_gt *gt)
{
	struct xe_device *xe = gt_to_xe(gt);
	int err;

	err = vf_get_ggtt_info(gt);
	if (unlikely(err))
		return err;

	if (IS_DGFX(xe) && xe_gt_is_main_type(gt)) {
		err = vf_get_lmem_info(gt);
		if (unlikely(err))
			return err;
	}

	err = vf_get_submission_cfg(gt);
	if (unlikely(err))
		return err;

	if (has_gmdid(xe))
		vf_cache_gmdid(gt);

	return 0;
}

/**
 * xe_gt_sriov_vf_guc_ids - VF GuC context IDs configuration.
 * @gt: the &xe_gt
 *
 * This function is for VF use only.
 *
 * Return: number of GuC context IDs assigned to VF.
 */
u16 xe_gt_sriov_vf_guc_ids(struct xe_gt *gt)
{
	xe_gt_assert(gt, IS_SRIOV_VF(gt_to_xe(gt)));
	xe_gt_assert(gt, gt->sriov.vf.guc_version.major);
	xe_gt_assert(gt, gt->sriov.vf.self_config.num_ctxs);

	return gt->sriov.vf.self_config.num_ctxs;
}

/**
 * xe_gt_sriov_vf_lmem - VF LMEM configuration.
 * @gt: the &xe_gt
 *
 * This function is for VF use only.
 *
 * Return: size of the LMEM assigned to VF.
 */
u64 xe_gt_sriov_vf_lmem(struct xe_gt *gt)
{
	xe_gt_assert(gt, IS_SRIOV_VF(gt_to_xe(gt)));
	xe_gt_assert(gt, gt->sriov.vf.guc_version.major);
	xe_gt_assert(gt, gt->sriov.vf.self_config.lmem_size);

	return gt->sriov.vf.self_config.lmem_size;
}

/**
 * xe_gt_sriov_vf_ggtt - VF GGTT configuration.
 * @gt: the &xe_gt
 *
 * This function is for VF use only.
 *
 * Return: size of the GGTT assigned to VF.
 */
u64 xe_gt_sriov_vf_ggtt(struct xe_gt *gt)
{
	xe_gt_assert(gt, IS_SRIOV_VF(gt_to_xe(gt)));
	xe_gt_assert(gt, gt->sriov.vf.guc_version.major);
	xe_gt_assert(gt, gt->sriov.vf.self_config.ggtt_size);

	return gt->sriov.vf.self_config.ggtt_size;
}

/**
 * xe_gt_sriov_vf_ggtt_base - VF GGTT base offset.
 * @gt: the &xe_gt
 *
 * This function is for VF use only.
 *
 * Return: base offset of the GGTT assigned to VF.
 */
u64 xe_gt_sriov_vf_ggtt_base(struct xe_gt *gt)
{
	xe_gt_assert(gt, IS_SRIOV_VF(gt_to_xe(gt)));
	xe_gt_assert(gt, gt->sriov.vf.guc_version.major);
	xe_gt_assert(gt, gt->sriov.vf.self_config.ggtt_size);

	return gt->sriov.vf.self_config.ggtt_base;
}

/**
 * xe_gt_sriov_vf_ggtt_shift - Return shift in GGTT range due to VF migration
 * @gt: the &xe_gt struct instance
 *
 * This function is for VF use only.
 *
 * Return: The shift value; could be negative
 */
s64 xe_gt_sriov_vf_ggtt_shift(struct xe_gt *gt)
{
	struct xe_gt_sriov_vf_selfconfig *config = &gt->sriov.vf.self_config;

	xe_gt_assert(gt, IS_SRIOV_VF(gt_to_xe(gt)));
	xe_gt_assert(gt, xe_gt_is_main_type(gt));

	return config->ggtt_shift;
}

static int relay_action_handshake(struct xe_gt *gt, u32 *major, u32 *minor)
{
	u32 request[VF2PF_HANDSHAKE_REQUEST_MSG_LEN] = {
		FIELD_PREP(GUC_HXG_MSG_0_ORIGIN, GUC_HXG_ORIGIN_HOST) |
		FIELD_PREP(GUC_HXG_MSG_0_TYPE, GUC_HXG_TYPE_REQUEST) |
		FIELD_PREP(GUC_HXG_REQUEST_MSG_0_ACTION, GUC_RELAY_ACTION_VF2PF_HANDSHAKE),
		FIELD_PREP(VF2PF_HANDSHAKE_REQUEST_MSG_1_MAJOR, *major) |
		FIELD_PREP(VF2PF_HANDSHAKE_REQUEST_MSG_1_MINOR, *minor),
	};
	u32 response[VF2PF_HANDSHAKE_RESPONSE_MSG_LEN];
	int ret;

	xe_gt_assert(gt, IS_SRIOV_VF(gt_to_xe(gt)));

	ret = xe_guc_relay_send_to_pf(&gt->uc.guc.relay,
				      request, ARRAY_SIZE(request),
				      response, ARRAY_SIZE(response));
	if (unlikely(ret < 0))
		return ret;

	if (unlikely(ret != VF2PF_HANDSHAKE_RESPONSE_MSG_LEN))
		return -EPROTO;

	if (unlikely(FIELD_GET(VF2PF_HANDSHAKE_RESPONSE_MSG_0_MBZ, response[0])))
		return -EPROTO;

	*major = FIELD_GET(VF2PF_HANDSHAKE_RESPONSE_MSG_1_MAJOR, response[1]);
	*minor = FIELD_GET(VF2PF_HANDSHAKE_RESPONSE_MSG_1_MINOR, response[1]);

	return 0;
}

static void vf_connect_pf(struct xe_device *xe, u16 major, u16 minor)
{
	xe_assert(xe, IS_SRIOV_VF(xe));

	xe->sriov.vf.pf_version.major = major;
	xe->sriov.vf.pf_version.minor = minor;
}

static void vf_disconnect_pf(struct xe_device *xe)
{
	vf_connect_pf(xe, 0, 0);
}

static int vf_handshake_with_pf(struct xe_gt *gt)
{
	struct xe_device *xe = gt_to_xe(gt);
	u32 major_wanted = GUC_RELAY_VERSION_LATEST_MAJOR;
	u32 minor_wanted = GUC_RELAY_VERSION_LATEST_MINOR;
	u32 major = major_wanted, minor = minor_wanted;
	int err;

	err = relay_action_handshake(gt, &major, &minor);
	if (unlikely(err))
		goto failed;

	if (!major && !minor) {
		err = -ENODATA;
		goto failed;
	}

	xe_gt_sriov_dbg(gt, "using VF/PF ABI %u.%u\n", major, minor);
	vf_connect_pf(xe, major, minor);
	return 0;

failed:
	xe_gt_sriov_err(gt, "Unable to confirm VF/PF ABI version %u.%u (%pe)\n",
			major, minor, ERR_PTR(err));
	vf_disconnect_pf(xe);
	return err;
}

/**
 * xe_gt_sriov_vf_connect - Establish connection with the PF driver.
 * @gt: the &xe_gt
 *
 * This function is for VF use only.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_gt_sriov_vf_connect(struct xe_gt *gt)
{
	int err;

	err = vf_handshake_with_pf(gt);
	if (unlikely(err))
		goto failed;

	return 0;

failed:
	xe_gt_sriov_err(gt, "Failed to get version info (%pe)\n", ERR_PTR(err));
	return err;
}

/**
 * xe_gt_sriov_vf_default_lrcs_hwsp_rebase - Update GGTT references in HWSP of default LRCs.
 * @gt: the &xe_gt struct instance
 */
void xe_gt_sriov_vf_default_lrcs_hwsp_rebase(struct xe_gt *gt)
{
	struct xe_hw_engine *hwe;
	enum xe_hw_engine_id id;

	for_each_hw_engine(hwe, gt, id)
		xe_default_lrc_update_memirq_regs_with_address(hwe);
}

/**
 * xe_gt_sriov_vf_migrated_event_handler - Start a VF migration recovery,
 *   or just mark that a GuC is ready for it.
 * @gt: the &xe_gt struct instance linked to target GuC
 *
 * This function shall be called only by VF.
 */
void xe_gt_sriov_vf_migrated_event_handler(struct xe_gt *gt)
{
	struct xe_device *xe = gt_to_xe(gt);

	xe_gt_assert(gt, IS_SRIOV_VF(xe));

	set_bit(gt->info.id, &xe->sriov.vf.migration.gt_flags);
	/*
	 * We need to be certain that if all flags were set, at least one
	 * thread will notice that and schedule the recovery.
	 */
	smp_mb__after_atomic();

	xe_gt_sriov_info(gt, "ready for recovery after migration\n");
	xe_sriov_vf_start_migration_recovery(xe);
}

static bool vf_is_negotiated(struct xe_gt *gt, u16 major, u16 minor)
{
	struct xe_device *xe = gt_to_xe(gt);

	xe_gt_assert(gt, IS_SRIOV_VF(xe));

	return major == xe->sriov.vf.pf_version.major &&
	       minor <= xe->sriov.vf.pf_version.minor;
}

static int vf_prepare_runtime_info(struct xe_gt *gt, unsigned int num_regs)
{
	struct vf_runtime_reg *regs = gt->sriov.vf.runtime.regs;
	unsigned int regs_size = round_up(num_regs, 4);
	struct xe_device *xe = gt_to_xe(gt);

	xe_gt_assert(gt, IS_SRIOV_VF(xe));

	if (regs) {
		if (num_regs <= gt->sriov.vf.runtime.regs_size) {
			memset(regs, 0, num_regs * sizeof(*regs));
			gt->sriov.vf.runtime.num_regs = num_regs;
			return 0;
		}

		drmm_kfree(&xe->drm, regs);
		gt->sriov.vf.runtime.regs = NULL;
		gt->sriov.vf.runtime.num_regs = 0;
		gt->sriov.vf.runtime.regs_size = 0;
	}

	regs = drmm_kcalloc(&xe->drm, regs_size, sizeof(*regs), GFP_KERNEL);
	if (unlikely(!regs))
		return -ENOMEM;

	gt->sriov.vf.runtime.regs = regs;
	gt->sriov.vf.runtime.num_regs = num_regs;
	gt->sriov.vf.runtime.regs_size = regs_size;
	return 0;
}

static int vf_query_runtime_info(struct xe_gt *gt)
{
	u32 request[VF2PF_QUERY_RUNTIME_REQUEST_MSG_LEN];
	u32 response[VF2PF_QUERY_RUNTIME_RESPONSE_MSG_MIN_LEN + 32]; /* up to 16 regs */
	u32 limit = (ARRAY_SIZE(response) - VF2PF_QUERY_RUNTIME_RESPONSE_MSG_MIN_LEN) / 2;
	u32 count, remaining, num, i;
	u32 start = 0;
	int ret;

	xe_gt_assert(gt, IS_SRIOV_VF(gt_to_xe(gt)));
	xe_gt_assert(gt, limit);

	/* this is part of the 1.0 PF/VF ABI */
	if (!vf_is_negotiated(gt, 1, 0))
		return -ENOPKG;

	request[0] = FIELD_PREP(GUC_HXG_MSG_0_ORIGIN, GUC_HXG_ORIGIN_HOST) |
		     FIELD_PREP(GUC_HXG_MSG_0_TYPE, GUC_HXG_TYPE_REQUEST) |
		     FIELD_PREP(GUC_HXG_REQUEST_MSG_0_ACTION,
				GUC_RELAY_ACTION_VF2PF_QUERY_RUNTIME) |
		     FIELD_PREP(VF2PF_QUERY_RUNTIME_REQUEST_MSG_0_LIMIT, limit);

repeat:
	request[1] = FIELD_PREP(VF2PF_QUERY_RUNTIME_REQUEST_MSG_1_START, start);
	ret = xe_guc_relay_send_to_pf(&gt->uc.guc.relay,
				      request, ARRAY_SIZE(request),
				      response, ARRAY_SIZE(response));
	if (unlikely(ret < 0))
		goto failed;

	if (unlikely(ret < VF2PF_QUERY_RUNTIME_RESPONSE_MSG_MIN_LEN)) {
		ret = -EPROTO;
		goto failed;
	}
	if (unlikely((ret - VF2PF_QUERY_RUNTIME_RESPONSE_MSG_MIN_LEN) % 2)) {
		ret = -EPROTO;
		goto failed;
	}

	num = (ret - VF2PF_QUERY_RUNTIME_RESPONSE_MSG_MIN_LEN) / 2;
	count = FIELD_GET(VF2PF_QUERY_RUNTIME_RESPONSE_MSG_0_COUNT, response[0]);
	remaining = FIELD_GET(VF2PF_QUERY_RUNTIME_RESPONSE_MSG_1_REMAINING, response[1]);

	xe_gt_sriov_dbg_verbose(gt, "count=%u num=%u ret=%d start=%u remaining=%u\n",
				count, num, ret, start, remaining);

	if (unlikely(count != num)) {
		ret = -EPROTO;
		goto failed;
	}

	if (start == 0) {
		ret = vf_prepare_runtime_info(gt, num + remaining);
		if (unlikely(ret < 0))
			goto failed;
	} else if (unlikely(start + num > gt->sriov.vf.runtime.num_regs)) {
		ret = -EPROTO;
		goto failed;
	}

	for (i = 0; i < num; ++i) {
		struct vf_runtime_reg *reg = &gt->sriov.vf.runtime.regs[start + i];

		reg->offset = response[VF2PF_QUERY_RUNTIME_RESPONSE_MSG_MIN_LEN + 2 * i];
		reg->value = response[VF2PF_QUERY_RUNTIME_RESPONSE_MSG_MIN_LEN + 2 * i + 1];
	}

	if (remaining) {
		start += num;
		goto repeat;
	}

	return 0;

failed:
	vf_prepare_runtime_info(gt, 0);
	return ret;
}

static void vf_show_runtime_info(struct xe_gt *gt)
{
	struct vf_runtime_reg *vf_regs = gt->sriov.vf.runtime.regs;
	unsigned int size = gt->sriov.vf.runtime.num_regs;

	xe_gt_assert(gt, IS_SRIOV_VF(gt_to_xe(gt)));

	for (; size--; vf_regs++)
		xe_gt_sriov_dbg(gt, "runtime(%#x) = %#x\n",
				vf_regs->offset, vf_regs->value);
}

/**
 * xe_gt_sriov_vf_query_runtime - Query SR-IOV runtime data.
 * @gt: the &xe_gt
 *
 * This function is for VF use only.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_gt_sriov_vf_query_runtime(struct xe_gt *gt)
{
	int err;

	err = vf_query_runtime_info(gt);
	if (unlikely(err))
		goto failed;

	if (IS_ENABLED(CONFIG_DRM_XE_DEBUG))
		vf_show_runtime_info(gt);

	return 0;

failed:
	xe_gt_sriov_err(gt, "Failed to get runtime info (%pe)\n",
			ERR_PTR(err));
	return err;
}

static int vf_runtime_reg_cmp(const void *a, const void *b)
{
	const struct vf_runtime_reg *ra = a;
	const struct vf_runtime_reg *rb = b;

	return (int)ra->offset - (int)rb->offset;
}

static struct vf_runtime_reg *vf_lookup_reg(struct xe_gt *gt, u32 addr)
{
	struct xe_gt_sriov_vf_runtime *runtime = &gt->sriov.vf.runtime;
	struct vf_runtime_reg key = { .offset = addr };

	xe_gt_assert(gt, IS_SRIOV_VF(gt_to_xe(gt)));

	return bsearch(&key, runtime->regs, runtime->num_regs, sizeof(key),
		       vf_runtime_reg_cmp);
}

/**
 * xe_gt_sriov_vf_read32 - Get a register value from the runtime data.
 * @gt: the &xe_gt
 * @reg: the register to read
 *
 * This function is for VF use only.
 * This function shall be called after VF has connected to PF.
 * This function is dedicated for registers that VFs can't read directly.
 *
 * Return: register value obtained from the PF or 0 if not found.
 */
u32 xe_gt_sriov_vf_read32(struct xe_gt *gt, struct xe_reg reg)
{
	u32 addr = xe_mmio_adjusted_addr(&gt->mmio, reg.addr);
	struct vf_runtime_reg *rr;

	xe_gt_assert(gt, IS_SRIOV_VF(gt_to_xe(gt)));
	xe_gt_assert(gt, !reg.vf);

	if (reg.addr == GMD_ID.addr) {
		xe_gt_sriov_dbg_verbose(gt, "gmdid(%#x) = %#x\n",
					addr, gt->sriov.vf.runtime.gmdid);
		return gt->sriov.vf.runtime.gmdid;
	}

	rr = vf_lookup_reg(gt, addr);
	if (!rr) {
		xe_gt_WARN(gt, IS_ENABLED(CONFIG_DRM_XE_DEBUG),
			   "VF is trying to read an inaccessible register %#x+%#x\n",
			   reg.addr, addr - reg.addr);
		return 0;
	}

	xe_gt_sriov_dbg_verbose(gt, "runtime[%#x] = %#x\n", addr, rr->value);
	return rr->value;
}

/**
 * xe_gt_sriov_vf_write32 - Handle a write to an inaccessible register.
 * @gt: the &xe_gt
 * @reg: the register to write
 * @val: value to write
 *
 * This function is for VF use only.
 * Currently it will trigger a WARN if running on debug build.
 */
void xe_gt_sriov_vf_write32(struct xe_gt *gt, struct xe_reg reg, u32 val)
{
	u32 addr = xe_mmio_adjusted_addr(&gt->mmio, reg.addr);

	xe_gt_assert(gt, IS_SRIOV_VF(gt_to_xe(gt)));
	xe_gt_assert(gt, !reg.vf);

	/*
	 * In the future, we may want to handle selected writes to inaccessible
	 * registers in some custom way, but for now let's just log a warning
	 * about such attempt, as likely we might be doing something wrong.
	 */
	xe_gt_WARN(gt, IS_ENABLED(CONFIG_DRM_XE_DEBUG),
		   "VF is trying to write %#x to an inaccessible register %#x+%#x\n",
		   val, reg.addr, addr - reg.addr);
}

/**
 * xe_gt_sriov_vf_print_config - Print VF self config.
 * @gt: the &xe_gt
 * @p: the &drm_printer
 *
 * This function is for VF use only.
 */
void xe_gt_sriov_vf_print_config(struct xe_gt *gt, struct drm_printer *p)
{
	struct xe_gt_sriov_vf_selfconfig *config = &gt->sriov.vf.self_config;
	struct xe_device *xe = gt_to_xe(gt);
	char buf[10];

	xe_gt_assert(gt, IS_SRIOV_VF(gt_to_xe(gt)));

	drm_printf(p, "GGTT range:\t%#llx-%#llx\n",
		   config->ggtt_base,
		   config->ggtt_base + config->ggtt_size - 1);

	string_get_size(config->ggtt_size, 1, STRING_UNITS_2, buf, sizeof(buf));
	drm_printf(p, "GGTT size:\t%llu (%s)\n", config->ggtt_size, buf);

	drm_printf(p, "GGTT shift on last restore:\t%lld\n", config->ggtt_shift);

	if (IS_DGFX(xe) && xe_gt_is_main_type(gt)) {
		string_get_size(config->lmem_size, 1, STRING_UNITS_2, buf, sizeof(buf));
		drm_printf(p, "LMEM size:\t%llu (%s)\n", config->lmem_size, buf);
	}

	drm_printf(p, "GuC contexts:\t%u\n", config->num_ctxs);
	drm_printf(p, "GuC doorbells:\t%u\n", config->num_dbs);
}

/**
 * xe_gt_sriov_vf_print_runtime - Print VF's runtime regs received from PF.
 * @gt: the &xe_gt
 * @p: the &drm_printer
 *
 * This function is for VF use only.
 */
void xe_gt_sriov_vf_print_runtime(struct xe_gt *gt, struct drm_printer *p)
{
	struct vf_runtime_reg *vf_regs = gt->sriov.vf.runtime.regs;
	unsigned int size = gt->sriov.vf.runtime.num_regs;

	xe_gt_assert(gt, IS_SRIOV_VF(gt_to_xe(gt)));

	for (; size--; vf_regs++)
		drm_printf(p, "%#x = %#x\n", vf_regs->offset, vf_regs->value);
}

/**
 * xe_gt_sriov_vf_print_version - Print VF ABI versions.
 * @gt: the &xe_gt
 * @p: the &drm_printer
 *
 * This function is for VF use only.
 */
void xe_gt_sriov_vf_print_version(struct xe_gt *gt, struct drm_printer *p)
{
	struct xe_device *xe = gt_to_xe(gt);
	struct xe_uc_fw_version *guc_version = &gt->sriov.vf.guc_version;
	struct xe_uc_fw_version *wanted = &gt->sriov.vf.wanted_guc_version;
	struct xe_sriov_vf_relay_version *pf_version = &xe->sriov.vf.pf_version;
	struct xe_uc_fw_version ver;

	xe_gt_assert(gt, IS_SRIOV_VF(gt_to_xe(gt)));

	drm_printf(p, "GuC ABI:\n");

	vf_minimum_guc_version(gt, &ver);
	drm_printf(p, "\tbase:\t%u.%u.%u.*\n", ver.branch, ver.major, ver.minor);

	drm_printf(p, "\twanted:\t%u.%u.%u.*\n",
		   wanted->branch, wanted->major, wanted->minor);

	drm_printf(p, "\thandshake:\t%u.%u.%u.%u\n",
		   guc_version->branch, guc_version->major,
		   guc_version->minor, guc_version->patch);

	drm_printf(p, "PF ABI:\n");

	drm_printf(p, "\tbase:\t%u.%u\n",
		   GUC_RELAY_VERSION_BASE_MAJOR, GUC_RELAY_VERSION_BASE_MINOR);
	drm_printf(p, "\twanted:\t%u.%u\n",
		   GUC_RELAY_VERSION_LATEST_MAJOR, GUC_RELAY_VERSION_LATEST_MINOR);
	drm_printf(p, "\thandshake:\t%u.%u\n",
		   pf_version->major, pf_version->minor);
}
