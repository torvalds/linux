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
#include "xe_mmio.h"
#include "xe_sriov.h"
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

static int vf_reset_guc_state(struct xe_gt *gt)
{
	struct xe_guc *guc = &gt->uc.guc;
	int err;

	err = guc_action_vf_reset(guc);
	if (unlikely(err))
		xe_gt_sriov_err(gt, "Failed to reset GuC state (%pe)\n", ERR_PTR(err));
	return err;
}

static int guc_action_match_version(struct xe_guc *guc,
				    u32 wanted_branch, u32 wanted_major, u32 wanted_minor,
				    u32 *branch, u32 *major, u32 *minor, u32 *patch)
{
	u32 request[VF2GUC_MATCH_VERSION_REQUEST_MSG_LEN] = {
		FIELD_PREP(GUC_HXG_MSG_0_ORIGIN, GUC_HXG_ORIGIN_HOST) |
		FIELD_PREP(GUC_HXG_MSG_0_TYPE, GUC_HXG_TYPE_REQUEST) |
		FIELD_PREP(GUC_HXG_REQUEST_MSG_0_ACTION,
			   GUC_ACTION_VF2GUC_MATCH_VERSION),
		FIELD_PREP(VF2GUC_MATCH_VERSION_REQUEST_MSG_1_BRANCH, wanted_branch) |
		FIELD_PREP(VF2GUC_MATCH_VERSION_REQUEST_MSG_1_MAJOR, wanted_major) |
		FIELD_PREP(VF2GUC_MATCH_VERSION_REQUEST_MSG_1_MINOR, wanted_minor),
	};
	u32 response[GUC_MAX_MMIO_MSG_LEN];
	int ret;

	BUILD_BUG_ON(VF2GUC_MATCH_VERSION_RESPONSE_MSG_LEN > GUC_MAX_MMIO_MSG_LEN);

	ret = xe_guc_mmio_send_recv(guc, request, ARRAY_SIZE(request), response);
	if (unlikely(ret < 0))
		return ret;

	if (unlikely(FIELD_GET(VF2GUC_MATCH_VERSION_RESPONSE_MSG_0_MBZ, response[0])))
		return -EPROTO;

	*branch = FIELD_GET(VF2GUC_MATCH_VERSION_RESPONSE_MSG_1_BRANCH, response[1]);
	*major = FIELD_GET(VF2GUC_MATCH_VERSION_RESPONSE_MSG_1_MAJOR, response[1]);
	*minor = FIELD_GET(VF2GUC_MATCH_VERSION_RESPONSE_MSG_1_MINOR, response[1]);
	*patch = FIELD_GET(VF2GUC_MATCH_VERSION_RESPONSE_MSG_1_PATCH, response[1]);

	return 0;
}

static void vf_minimum_guc_version(struct xe_gt *gt, u32 *branch, u32 *major, u32 *minor)
{
	struct xe_device *xe = gt_to_xe(gt);

	switch (xe->info.platform) {
	case XE_TIGERLAKE ... XE_PVC:
		/* 1.1 this is current baseline for Xe driver */
		*branch = 0;
		*major = 1;
		*minor = 1;
		break;
	default:
		/* 1.2 has support for the GMD_ID KLV */
		*branch = 0;
		*major = 1;
		*minor = 2;
		break;
	}
}

static void vf_wanted_guc_version(struct xe_gt *gt, u32 *branch, u32 *major, u32 *minor)
{
	/* for now it's the same as minimum */
	return vf_minimum_guc_version(gt, branch, major, minor);
}

static int vf_handshake_with_guc(struct xe_gt *gt)
{
	struct xe_gt_sriov_vf_guc_version *guc_version = &gt->sriov.vf.guc_version;
	struct xe_guc *guc = &gt->uc.guc;
	u32 wanted_branch, wanted_major, wanted_minor;
	u32 branch, major, minor, patch;
	int err;

	xe_gt_assert(gt, IS_SRIOV_VF(gt_to_xe(gt)));

	/* select wanted version - prefer previous (if any) */
	if (guc_version->major || guc_version->minor) {
		wanted_branch = guc_version->branch;
		wanted_major = guc_version->major;
		wanted_minor = guc_version->minor;
	} else {
		vf_wanted_guc_version(gt, &wanted_branch, &wanted_major, &wanted_minor);
		xe_gt_assert(gt, wanted_major != GUC_VERSION_MAJOR_ANY);
	}

	err = guc_action_match_version(guc, wanted_branch, wanted_major, wanted_minor,
				       &branch, &major, &minor, &patch);
	if (unlikely(err))
		goto fail;

	/* we don't support interface version change */
	if ((guc_version->major || guc_version->minor) &&
	    (guc_version->branch != branch || guc_version->major != major ||
	     guc_version->minor != minor)) {
		xe_gt_sriov_err(gt, "New GuC interface version detected: %u.%u.%u.%u\n",
				branch, major, minor, patch);
		xe_gt_sriov_info(gt, "Previously used version was: %u.%u.%u.%u\n",
				 guc_version->branch, guc_version->major,
				 guc_version->minor, guc_version->patch);
		err = -EREMCHG;
		goto fail;
	}

	/* illegal */
	if (major > wanted_major) {
		err = -EPROTO;
		goto unsupported;
	}

	/* there's no fallback on major version. */
	if (major != wanted_major) {
		err = -ENOPKG;
		goto unsupported;
	}

	/* check against minimum version supported by us */
	vf_minimum_guc_version(gt, &wanted_branch, &wanted_major, &wanted_minor);
	xe_gt_assert(gt, major != GUC_VERSION_MAJOR_ANY);
	if (major < wanted_major || (major == wanted_major && minor < wanted_minor)) {
		err = -ENOKEY;
		goto unsupported;
	}

	xe_gt_sriov_dbg(gt, "using GuC interface version %u.%u.%u.%u\n",
			branch, major, minor, patch);

	guc_version->branch = branch;
	guc_version->major = major;
	guc_version->minor = minor;
	guc_version->patch = patch;
	return 0;

unsupported:
	xe_gt_sriov_err(gt, "Unsupported GuC version %u.%u.%u.%u (%pe)\n",
			branch, major, minor, patch, ERR_PTR(err));
fail:
	xe_gt_sriov_err(gt, "Unable to confirm GuC version %u.%u (%pe)\n",
			wanted_major, wanted_minor, ERR_PTR(err));

	/* try again with *any* just to query which version is supported */
	if (!guc_action_match_version(guc, GUC_VERSION_BRANCH_ANY,
				      GUC_VERSION_MAJOR_ANY, GUC_VERSION_MINOR_ANY,
				      &branch, &major, &minor, &patch))
		xe_gt_sriov_notice(gt, "GuC reports interface version %u.%u.%u.%u\n",
				   branch, major, minor, patch);
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

	err = vf_reset_guc_state(gt);
	if (unlikely(err))
		return err;

	err = vf_handshake_with_guc(gt);
	if (unlikely(err))
		return err;

	return 0;
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

	if (IS_DGFX(xe) && !xe_gt_is_media_type(gt)) {
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

static struct xe_ggtt_node *
vf_balloon_ggtt_node(struct xe_ggtt *ggtt, u64 start, u64 end)
{
	struct xe_ggtt_node *node;
	int err;

	node = xe_ggtt_node_init(ggtt);
	if (IS_ERR(node))
		return node;

	err = xe_ggtt_node_insert_balloon(node, start, end);
	if (err) {
		xe_ggtt_node_fini(node);
		return ERR_PTR(err);
	}

	return node;
}

static int vf_balloon_ggtt(struct xe_gt *gt)
{
	struct xe_gt_sriov_vf_selfconfig *config = &gt->sriov.vf.self_config;
	struct xe_tile *tile = gt_to_tile(gt);
	struct xe_ggtt *ggtt = tile->mem.ggtt;
	struct xe_device *xe = gt_to_xe(gt);
	u64 start, end;

	xe_gt_assert(gt, IS_SRIOV_VF(xe));
	xe_gt_assert(gt, !xe_gt_is_media_type(gt));

	if (!config->ggtt_size)
		return -ENODATA;

	/*
	 * VF can only use part of the GGTT as allocated by the PF:
	 *
	 *      WOPCM                                  GUC_GGTT_TOP
	 *      |<------------ Total GGTT size ------------------>|
	 *
	 *           VF GGTT base -->|<- size ->|
	 *
	 *      +--------------------+----------+-----------------+
	 *      |////////////////////|   block  |\\\\\\\\\\\\\\\\\|
	 *      +--------------------+----------+-----------------+
	 *
	 *      |<--- balloon[0] --->|<-- VF -->|<-- balloon[1] ->|
	 */

	start = xe_wopcm_size(xe);
	end = config->ggtt_base;
	if (end != start) {
		tile->sriov.vf.ggtt_balloon[0] = vf_balloon_ggtt_node(ggtt, start, end);
		if (IS_ERR(tile->sriov.vf.ggtt_balloon[0]))
			return PTR_ERR(tile->sriov.vf.ggtt_balloon[0]);
	}

	start = config->ggtt_base + config->ggtt_size;
	end = GUC_GGTT_TOP;
	if (end != start) {
		tile->sriov.vf.ggtt_balloon[1] = vf_balloon_ggtt_node(ggtt, start, end);
		if (IS_ERR(tile->sriov.vf.ggtt_balloon[1])) {
			xe_ggtt_node_remove_balloon(tile->sriov.vf.ggtt_balloon[0]);
			return PTR_ERR(tile->sriov.vf.ggtt_balloon[1]);
		}
	}

	return 0;
}

static void deballoon_ggtt(struct drm_device *drm, void *arg)
{
	struct xe_tile *tile = arg;

	xe_tile_assert(tile, IS_SRIOV_VF(tile_to_xe(tile)));
	xe_ggtt_node_remove_balloon(tile->sriov.vf.ggtt_balloon[1]);
	xe_ggtt_node_remove_balloon(tile->sriov.vf.ggtt_balloon[0]);
}

/**
 * xe_gt_sriov_vf_prepare_ggtt - Prepare a VF's GGTT configuration.
 * @gt: the &xe_gt
 *
 * This function is for VF use only.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_gt_sriov_vf_prepare_ggtt(struct xe_gt *gt)
{
	struct xe_tile *tile = gt_to_tile(gt);
	struct xe_device *xe = tile_to_xe(tile);
	int err;

	if (xe_gt_is_media_type(gt))
		return 0;

	err = vf_balloon_ggtt(gt);
	if (err)
		return err;

	return drmm_add_action_or_reset(&xe->drm, deballoon_ggtt, tile);
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

static void vf_connect_pf(struct xe_gt *gt, u16 major, u16 minor)
{
	xe_gt_assert(gt, IS_SRIOV_VF(gt_to_xe(gt)));

	gt->sriov.vf.pf_version.major = major;
	gt->sriov.vf.pf_version.minor = minor;
}

static void vf_disconnect_pf(struct xe_gt *gt)
{
	vf_connect_pf(gt, 0, 0);
}

static int vf_handshake_with_pf(struct xe_gt *gt)
{
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
	vf_connect_pf(gt, major, minor);
	return 0;

failed:
	xe_gt_sriov_err(gt, "Unable to confirm VF/PF ABI version %u.%u (%pe)\n",
			major, minor, ERR_PTR(err));
	vf_disconnect_pf(gt);
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

static bool vf_is_negotiated(struct xe_gt *gt, u16 major, u16 minor)
{
	xe_gt_assert(gt, IS_SRIOV_VF(gt_to_xe(gt)));

	return major == gt->sriov.vf.pf_version.major &&
	       minor <= gt->sriov.vf.pf_version.minor;
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
	u32 addr = xe_mmio_adjusted_addr(gt, reg.addr);
	struct vf_runtime_reg *rr;

	xe_gt_assert(gt, IS_SRIOV_VF(gt_to_xe(gt)));
	xe_gt_assert(gt, gt->sriov.vf.pf_version.major);
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
	u32 addr = xe_mmio_adjusted_addr(gt, reg.addr);

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

	if (IS_DGFX(xe) && !xe_gt_is_media_type(gt)) {
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
	struct xe_gt_sriov_vf_guc_version *guc_version = &gt->sriov.vf.guc_version;
	struct xe_gt_sriov_vf_relay_version *pf_version = &gt->sriov.vf.pf_version;
	u32 branch, major, minor;

	xe_gt_assert(gt, IS_SRIOV_VF(gt_to_xe(gt)));

	drm_printf(p, "GuC ABI:\n");

	vf_minimum_guc_version(gt, &branch, &major, &minor);
	drm_printf(p, "\tbase:\t%u.%u.%u.*\n", branch, major, minor);

	vf_wanted_guc_version(gt, &branch, &major, &minor);
	drm_printf(p, "\twanted:\t%u.%u.%u.*\n", branch, major, minor);

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
