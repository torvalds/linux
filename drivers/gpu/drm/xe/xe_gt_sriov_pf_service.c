// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023-2024 Intel Corporation
 */

#include <drm/drm_managed.h>

#include "abi/guc_actions_sriov_abi.h"
#include "abi/guc_relay_actions_abi.h"

#include "regs/xe_gt_regs.h"
#include "regs/xe_guc_regs.h"
#include "regs/xe_regs.h"

#include "xe_mmio.h"
#include "xe_gt_sriov_printk.h"
#include "xe_gt_sriov_pf_helpers.h"
#include "xe_gt_sriov_pf_service.h"
#include "xe_gt_sriov_pf_service_types.h"
#include "xe_guc_ct.h"
#include "xe_guc_hxg_helpers.h"
#include "xe_sriov_pf_service.h"

static const struct xe_reg tgl_runtime_regs[] = {
	RPM_CONFIG0,			/* _MMIO(0x0d00) */
	MIRROR_FUSE3,			/* _MMIO(0x9118) */
	XELP_EU_ENABLE,			/* _MMIO(0x9134) */
	XELP_GT_SLICE_ENABLE,		/* _MMIO(0x9138) */
	XELP_GT_GEOMETRY_DSS_ENABLE,	/* _MMIO(0x913c) */
	GT_VEBOX_VDBOX_DISABLE,		/* _MMIO(0x9140) */
	HUC_KERNEL_LOAD_INFO,		/* _MMIO(0xc1dc) */
};

static const struct xe_reg ats_m_runtime_regs[] = {
	RPM_CONFIG0,			/* _MMIO(0x0d00) */
	MIRROR_FUSE3,			/* _MMIO(0x9118) */
	MIRROR_FUSE1,			/* _MMIO(0x911c) */
	XELP_EU_ENABLE,			/* _MMIO(0x9134) */
	XELP_GT_GEOMETRY_DSS_ENABLE,	/* _MMIO(0x913c) */
	GT_VEBOX_VDBOX_DISABLE,		/* _MMIO(0x9140) */
	XEHP_GT_COMPUTE_DSS_ENABLE,	/* _MMIO(0x9144) */
	HUC_KERNEL_LOAD_INFO,		/* _MMIO(0xc1dc) */
};

static const struct xe_reg pvc_runtime_regs[] = {
	RPM_CONFIG0,			/* _MMIO(0x0d00) */
	MIRROR_FUSE3,			/* _MMIO(0x9118) */
	XELP_EU_ENABLE,			/* _MMIO(0x9134) */
	XELP_GT_GEOMETRY_DSS_ENABLE,	/* _MMIO(0x913c) */
	GT_VEBOX_VDBOX_DISABLE,		/* _MMIO(0x9140) */
	XEHP_GT_COMPUTE_DSS_ENABLE,	/* _MMIO(0x9144) */
	XEHPC_GT_COMPUTE_DSS_ENABLE_EXT,/* _MMIO(0x9148) */
	HUC_KERNEL_LOAD_INFO,		/* _MMIO(0xc1dc) */
};

static const struct xe_reg ver_1270_runtime_regs[] = {
	RPM_CONFIG0,			/* _MMIO(0x0d00) */
	XEHP_FUSE4,			/* _MMIO(0x9114) */
	MIRROR_FUSE3,			/* _MMIO(0x9118) */
	MIRROR_FUSE1,			/* _MMIO(0x911c) */
	XELP_EU_ENABLE,			/* _MMIO(0x9134) */
	XELP_GT_GEOMETRY_DSS_ENABLE,	/* _MMIO(0x913c) */
	GT_VEBOX_VDBOX_DISABLE,		/* _MMIO(0x9140) */
	XEHP_GT_COMPUTE_DSS_ENABLE,	/* _MMIO(0x9144) */
	XEHPC_GT_COMPUTE_DSS_ENABLE_EXT,/* _MMIO(0x9148) */
	HUC_KERNEL_LOAD_INFO,		/* _MMIO(0xc1dc) */
};

static const struct xe_reg ver_2000_runtime_regs[] = {
	RPM_CONFIG0,			/* _MMIO(0x0d00) */
	XEHP_FUSE4,			/* _MMIO(0x9114) */
	MIRROR_FUSE3,			/* _MMIO(0x9118) */
	MIRROR_FUSE1,			/* _MMIO(0x911c) */
	XELP_EU_ENABLE,			/* _MMIO(0x9134) */
	XELP_GT_GEOMETRY_DSS_ENABLE,	/* _MMIO(0x913c) */
	GT_VEBOX_VDBOX_DISABLE,		/* _MMIO(0x9140) */
	XEHP_GT_COMPUTE_DSS_ENABLE,	/* _MMIO(0x9144) */
	XEHPC_GT_COMPUTE_DSS_ENABLE_EXT,/* _MMIO(0x9148) */
	XE2_GT_COMPUTE_DSS_2,		/* _MMIO(0x914c) */
	XE2_GT_GEOMETRY_DSS_1,		/* _MMIO(0x9150) */
	XE2_GT_GEOMETRY_DSS_2,		/* _MMIO(0x9154) */
	HUC_KERNEL_LOAD_INFO,		/* _MMIO(0xc1dc) */
};

static const struct xe_reg ver_3000_runtime_regs[] = {
	RPM_CONFIG0,			/* _MMIO(0x0d00) */
	XEHP_FUSE4,			/* _MMIO(0x9114) */
	MIRROR_FUSE3,			/* _MMIO(0x9118) */
	MIRROR_FUSE1,			/* _MMIO(0x911c) */
	MIRROR_L3BANK_ENABLE,		/* _MMIO(0x9130) */
	XELP_EU_ENABLE,			/* _MMIO(0x9134) */
	XELP_GT_GEOMETRY_DSS_ENABLE,	/* _MMIO(0x913c) */
	GT_VEBOX_VDBOX_DISABLE,		/* _MMIO(0x9140) */
	XEHP_GT_COMPUTE_DSS_ENABLE,	/* _MMIO(0x9144) */
	XEHPC_GT_COMPUTE_DSS_ENABLE_EXT,/* _MMIO(0x9148) */
	XE2_GT_COMPUTE_DSS_2,		/* _MMIO(0x914c) */
	XE2_GT_GEOMETRY_DSS_1,		/* _MMIO(0x9150) */
	XE2_GT_GEOMETRY_DSS_2,		/* _MMIO(0x9154) */
	HUC_KERNEL_LOAD_INFO,		/* _MMIO(0xc1dc) */
};

static const struct xe_reg *pick_runtime_regs(struct xe_device *xe, unsigned int *count)
{
	const struct xe_reg *regs;

	if (GRAPHICS_VERx100(xe) >= 3000) {
		*count = ARRAY_SIZE(ver_3000_runtime_regs);
		regs = ver_3000_runtime_regs;
	} else if (GRAPHICS_VERx100(xe) >= 2000) {
		*count = ARRAY_SIZE(ver_2000_runtime_regs);
		regs = ver_2000_runtime_regs;
	} else if (GRAPHICS_VERx100(xe) >= 1270) {
		*count = ARRAY_SIZE(ver_1270_runtime_regs);
		regs = ver_1270_runtime_regs;
	} else if (GRAPHICS_VERx100(xe) == 1260) {
		*count = ARRAY_SIZE(pvc_runtime_regs);
		regs = pvc_runtime_regs;
	} else if (GRAPHICS_VERx100(xe) == 1255) {
		*count = ARRAY_SIZE(ats_m_runtime_regs);
		regs = ats_m_runtime_regs;
	} else if (GRAPHICS_VERx100(xe) == 1200) {
		*count = ARRAY_SIZE(tgl_runtime_regs);
		regs = tgl_runtime_regs;
	} else {
		regs = ERR_PTR(-ENOPKG);
		*count = 0;
	}

	return regs;
}

static int pf_alloc_runtime_info(struct xe_gt *gt)
{
	struct xe_device *xe = gt_to_xe(gt);
	const struct xe_reg *regs;
	unsigned int size;
	u32 *values;

	xe_gt_assert(gt, IS_SRIOV_PF(xe));
	xe_gt_assert(gt, !gt->sriov.pf.service.runtime.size);
	xe_gt_assert(gt, !gt->sriov.pf.service.runtime.regs);
	xe_gt_assert(gt, !gt->sriov.pf.service.runtime.values);

	regs = pick_runtime_regs(xe, &size);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	if (unlikely(!size))
		return 0;

	values = drmm_kcalloc(&xe->drm, size, sizeof(u32), GFP_KERNEL);
	if (!values)
		return -ENOMEM;

	gt->sriov.pf.service.runtime.size = size;
	gt->sriov.pf.service.runtime.regs = regs;
	gt->sriov.pf.service.runtime.values = values;

	return 0;
}

static void read_many(struct xe_gt *gt, unsigned int count,
		      const struct xe_reg *regs, u32 *values)
{
	while (count--)
		*values++ = xe_mmio_read32(&gt->mmio, *regs++);
}

static void pf_prepare_runtime_info(struct xe_gt *gt)
{
	const struct xe_reg *regs;
	unsigned int size;
	u32 *values;

	if (!gt->sriov.pf.service.runtime.size)
		return;

	size = gt->sriov.pf.service.runtime.size;
	regs = gt->sriov.pf.service.runtime.regs;
	values = gt->sriov.pf.service.runtime.values;

	read_many(gt, size, regs, values);

	if (IS_ENABLED(CONFIG_DRM_XE_DEBUG_SRIOV)) {
		struct drm_printer p = xe_gt_dbg_printer(gt);

		xe_gt_sriov_pf_service_print_runtime(gt, &p);
	}
}

/**
 * xe_gt_sriov_pf_service_init - Early initialization of the GT SR-IOV PF services.
 * @gt: the &xe_gt to initialize
 *
 * Performs early initialization of the GT SR-IOV PF services, including preparation
 * of the runtime info that will be shared with VFs.
 *
 * This function can only be called on PF.
 */
int xe_gt_sriov_pf_service_init(struct xe_gt *gt)
{
	int err;

	err = pf_alloc_runtime_info(gt);
	if (unlikely(err))
		goto failed;

	return 0;
failed:
	xe_gt_sriov_err(gt, "Failed to initialize service (%pe)\n", ERR_PTR(err));
	return err;
}

/**
 * xe_gt_sriov_pf_service_update - Update PF SR-IOV services.
 * @gt: the &xe_gt to update
 *
 * Updates runtime data shared with VFs.
 *
 * This function can be called more than once.
 * This function can only be called on PF.
 */
void xe_gt_sriov_pf_service_update(struct xe_gt *gt)
{
	pf_prepare_runtime_info(gt);
}

/* Return: length of the response message or a negative error code on failure. */
static int pf_process_handshake_msg(struct xe_gt *gt, u32 origin,
				    const u32 *request, u32 len, u32 *response, u32 size)
{
	u32 wanted_major, wanted_minor;
	u32 major, minor;
	u32 mbz;
	int err;

	if (unlikely(len != VF2PF_HANDSHAKE_REQUEST_MSG_LEN))
		return -EMSGSIZE;

	mbz = FIELD_GET(VF2PF_HANDSHAKE_REQUEST_MSG_0_MBZ, request[0]);
	if (unlikely(mbz))
		return -EPFNOSUPPORT;

	wanted_major = FIELD_GET(VF2PF_HANDSHAKE_REQUEST_MSG_1_MAJOR, request[1]);
	wanted_minor = FIELD_GET(VF2PF_HANDSHAKE_REQUEST_MSG_1_MINOR, request[1]);

	err = xe_sriov_pf_service_handshake_vf(gt_to_xe(gt), origin, wanted_major, wanted_minor,
					       &major, &minor);
	if (err < 0)
		return err;

	xe_gt_assert(gt, major || minor);
	xe_gt_assert(gt, size >= VF2PF_HANDSHAKE_RESPONSE_MSG_LEN);

	response[0] = FIELD_PREP(GUC_HXG_MSG_0_ORIGIN, GUC_HXG_ORIGIN_HOST) |
		      FIELD_PREP(GUC_HXG_MSG_0_TYPE, GUC_HXG_TYPE_RESPONSE_SUCCESS) |
		      FIELD_PREP(GUC_HXG_RESPONSE_MSG_0_DATA0, 0);
	response[1] = FIELD_PREP(VF2PF_HANDSHAKE_RESPONSE_MSG_1_MAJOR, major) |
		      FIELD_PREP(VF2PF_HANDSHAKE_RESPONSE_MSG_1_MINOR, minor);

	return VF2PF_HANDSHAKE_RESPONSE_MSG_LEN;
}

struct reg_data {
	u32 offset;
	u32 value;
} __packed;
static_assert(hxg_sizeof(struct reg_data) == 2);

/* Return: number of entries copied or negative error code on failure. */
static int pf_service_runtime_query(struct xe_gt *gt, u32 start, u32 limit,
				    struct reg_data *data, u32 *remaining)
{
	struct xe_gt_sriov_pf_service_runtime_regs *runtime;
	unsigned int count, i;
	u32 addr;

	xe_gt_assert(gt, IS_SRIOV_PF(gt_to_xe(gt)));

	runtime = &gt->sriov.pf.service.runtime;

	if (start > runtime->size)
		return -ERANGE;

	count = min_t(u32, runtime->size - start, limit);

	for (i = 0; i < count; ++i, ++data) {
		addr = runtime->regs[start + i].addr;
		data->offset = xe_mmio_adjusted_addr(&gt->mmio, addr);
		data->value = runtime->values[start + i];
	}

	*remaining = runtime->size - start - count;
	return count;
}

/* Return: length of the response message or a negative error code on failure. */
static int pf_process_runtime_query_msg(struct xe_gt *gt, u32 origin,
					const u32 *msg, u32 msg_len, u32 *response, u32 resp_size)
{
	const u32 chunk_size = hxg_sizeof(struct reg_data);
	struct reg_data *reg_data_buf;
	u32 limit, start, max_chunks;
	u32 remaining = 0;
	int ret;

	/* this action is available from ABI 1.0 */
	if (!xe_sriov_pf_service_is_negotiated(gt_to_xe(gt), origin, 1, 0))
		return -EACCES;

	if (unlikely(msg_len > VF2PF_QUERY_RUNTIME_REQUEST_MSG_LEN))
		return -EMSGSIZE;
	if (unlikely(msg_len < VF2PF_QUERY_RUNTIME_REQUEST_MSG_LEN))
		return -EPROTO;
	if (unlikely(resp_size < VF2PF_QUERY_RUNTIME_RESPONSE_MSG_MIN_LEN))
		return -EINVAL;

	limit = FIELD_GET(VF2PF_QUERY_RUNTIME_REQUEST_MSG_0_LIMIT, msg[0]);
	start = FIELD_GET(VF2PF_QUERY_RUNTIME_REQUEST_MSG_1_START, msg[1]);

	resp_size = min_t(u32, resp_size, VF2PF_QUERY_RUNTIME_RESPONSE_MSG_MAX_LEN);
	max_chunks = (resp_size - VF2PF_QUERY_RUNTIME_RESPONSE_MSG_MIN_LEN) / chunk_size;
	limit = limit == VF2PF_QUERY_RUNTIME_NO_LIMIT ? max_chunks : min_t(u32, max_chunks, limit);
	reg_data_buf = (void *)(response + VF2PF_QUERY_RUNTIME_RESPONSE_MSG_MIN_LEN);

	ret = pf_service_runtime_query(gt, start, limit, reg_data_buf, &remaining);
	if (ret < 0)
		return ret;

	response[0] = FIELD_PREP(GUC_HXG_MSG_0_ORIGIN, GUC_HXG_ORIGIN_HOST) |
		      FIELD_PREP(GUC_HXG_MSG_0_TYPE, GUC_HXG_TYPE_RESPONSE_SUCCESS) |
		      FIELD_PREP(VF2PF_QUERY_RUNTIME_RESPONSE_MSG_0_COUNT, ret);
	response[1] = FIELD_PREP(VF2PF_QUERY_RUNTIME_RESPONSE_MSG_1_REMAINING, remaining);

	return VF2PF_QUERY_RUNTIME_RESPONSE_MSG_MIN_LEN + ret * hxg_sizeof(struct reg_data);
}

/**
 * xe_gt_sriov_pf_service_process_request - Service GT level SR-IOV request message from the VF.
 * @gt: the &xe_gt that provides the service
 * @origin: VF number that is requesting the service
 * @msg: request message
 * @msg_len: length of the request message (in dwords)
 * @response: placeholder for the response message
 * @resp_size: length of the response message buffer (in dwords)
 *
 * This function processes `Relay Message`_ request from the VF.
 *
 * Return: length of the response message or a negative error code on failure.
 */
int xe_gt_sriov_pf_service_process_request(struct xe_gt *gt, u32 origin,
					   const u32 *msg, u32 msg_len,
					   u32 *response, u32 resp_size)
{
	u32 action, data __maybe_unused;
	int ret;

	xe_gt_assert(gt, msg_len >= GUC_HXG_MSG_MIN_LEN);
	xe_gt_assert(gt, FIELD_GET(GUC_HXG_MSG_0_TYPE, msg[0]) == GUC_HXG_TYPE_REQUEST);

	action = FIELD_GET(GUC_HXG_REQUEST_MSG_0_ACTION, msg[0]);
	data = FIELD_GET(GUC_HXG_REQUEST_MSG_0_DATA0, msg[0]);
	xe_gt_sriov_dbg_verbose(gt, "service action %#x:%u from VF%u\n",
				action, data, origin);

	switch (action) {
	case GUC_RELAY_ACTION_VF2PF_HANDSHAKE:
		ret = pf_process_handshake_msg(gt, origin, msg, msg_len, response, resp_size);
		break;
	case GUC_RELAY_ACTION_VF2PF_QUERY_RUNTIME:
		ret = pf_process_runtime_query_msg(gt, origin, msg, msg_len, response, resp_size);
		break;
	default:
		ret = -EOPNOTSUPP;
		break;
	}

	return ret;
}

/**
 * xe_gt_sriov_pf_service_print_runtime - Print PF runtime data shared with VFs.
 * @gt: the &xe_gt
 * @p: the &drm_printer
 *
 * This function is for PF use only.
 */
int xe_gt_sriov_pf_service_print_runtime(struct xe_gt *gt, struct drm_printer *p)
{
	const struct xe_reg *regs;
	unsigned int size;
	u32 *values;

	xe_gt_assert(gt, IS_SRIOV_PF(gt_to_xe(gt)));

	size = gt->sriov.pf.service.runtime.size;
	regs = gt->sriov.pf.service.runtime.regs;
	values = gt->sriov.pf.service.runtime.values;

	for (; size--; regs++, values++) {
		drm_printf(p, "reg[%#x] = %#x\n",
			   xe_mmio_adjusted_addr(&gt->mmio, regs->addr), *values);
	}

	return 0;
}
