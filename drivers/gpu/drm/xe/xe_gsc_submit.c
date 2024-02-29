// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include "xe_gsc_submit.h"

#include <linux/poison.h>

#include "abi/gsc_command_header_abi.h"
#include "xe_bb.h"
#include "xe_exec_queue.h"
#include "xe_gt_printk.h"
#include "xe_gt_types.h"
#include "xe_map.h"
#include "xe_sched_job.h"
#include "instructions/xe_gsc_commands.h"
#include "regs/xe_gsc_regs.h"

#define GSC_HDR_SIZE (sizeof(struct intel_gsc_mtl_header)) /* shorthand define */

#define mtl_gsc_header_wr(xe_, map_, offset_, field_, val_) \
	xe_map_wr_field(xe_, map_, offset_, struct intel_gsc_mtl_header, field_, val_)

#define mtl_gsc_header_rd(xe_, map_, offset_, field_) \
	xe_map_rd_field(xe_, map_, offset_, struct intel_gsc_mtl_header, field_)

/*
 * GSC FW allows us to define the host_session_handle as we see fit, as long
 * as we use unique identifier for each user, with handle 0 being reserved for
 * kernel usage.
 * To be able to differentiate which client subsystem owns the given session, we
 * include the client id in the top 8 bits of the handle.
 */
#define HOST_SESSION_CLIENT_MASK GENMASK_ULL(63, 56)

static struct xe_gt *
gsc_to_gt(struct xe_gsc *gsc)
{
	return container_of(gsc, struct xe_gt, uc.gsc);
}

/**
 * xe_gsc_emit_header - write the MTL GSC header in memory
 * @xe: the Xe device
 * @map: the iosys map to write to
 * @offset: offset from the start of the map at which to write the header
 * @heci_client_id: client id identifying the type of command (see abi for values)
 * @host_session_id: host session ID of the caller
 * @payload_size: size of the payload that follows the header
 *
 * Returns: offset memory location following the header
 */
u32 xe_gsc_emit_header(struct xe_device *xe, struct iosys_map *map, u32 offset,
		       u8 heci_client_id, u64 host_session_id, u32 payload_size)
{
	xe_assert(xe, !(host_session_id & HOST_SESSION_CLIENT_MASK));

	if (host_session_id)
		host_session_id |= FIELD_PREP(HOST_SESSION_CLIENT_MASK, heci_client_id);

	xe_map_memset(xe, map, offset, 0, GSC_HDR_SIZE);

	mtl_gsc_header_wr(xe, map, offset, validity_marker, GSC_HECI_VALIDITY_MARKER);
	mtl_gsc_header_wr(xe, map, offset, heci_client_id, heci_client_id);
	mtl_gsc_header_wr(xe, map, offset, host_session_handle, host_session_id);
	mtl_gsc_header_wr(xe, map, offset, header_version, MTL_GSC_HEADER_VERSION);
	mtl_gsc_header_wr(xe, map, offset, message_size, payload_size + GSC_HDR_SIZE);

	return offset + GSC_HDR_SIZE;
};

/**
 * xe_gsc_poison_header - poison the MTL GSC header in memory
 * @xe: the Xe device
 * @map: the iosys map to write to
 * @offset: offset from the start of the map at which the header resides
 */
void xe_gsc_poison_header(struct xe_device *xe, struct iosys_map *map, u32 offset)
{
	xe_map_memset(xe, map, offset, POISON_FREE, GSC_HDR_SIZE);
};

/**
 * xe_gsc_check_and_update_pending - check the pending bit and update the input
 * header with the retry handle from the output header
 * @xe: the Xe device
 * @in: the iosys map containing the input buffer
 * @offset_in: offset within the iosys at which the input buffer is located
 * @out: the iosys map containing the output buffer
 * @offset_out: offset within the iosys at which the output buffer is located
 *
 * Returns: true if the pending bit was set, false otherwise
 */
bool xe_gsc_check_and_update_pending(struct xe_device *xe,
				     struct iosys_map *in, u32 offset_in,
				     struct iosys_map *out, u32 offset_out)
{
	if (mtl_gsc_header_rd(xe, out, offset_out, flags) & GSC_OUTFLAG_MSG_PENDING) {
		u64 handle = mtl_gsc_header_rd(xe, out, offset_out, gsc_message_handle);

		mtl_gsc_header_wr(xe, in, offset_in, gsc_message_handle, handle);

		return true;
	}

	return false;
}

/**
 * xe_gsc_read_out_header - reads and validates the output header and returns
 * the offset of the reply following the header
 * @xe: the Xe device
 * @map: the iosys map containing the output buffer
 * @offset: offset within the iosys at which the output buffer is located
 * @min_payload_size: minimum size of the message excluding the gsc header
 * @payload_offset: optional pointer to be set to the payload offset
 *
 * Returns: -errno value on failure, 0 otherwise
 */
int xe_gsc_read_out_header(struct xe_device *xe,
			   struct iosys_map *map, u32 offset,
			   u32 min_payload_size,
			   u32 *payload_offset)
{
	u32 marker = mtl_gsc_header_rd(xe, map, offset, validity_marker);
	u32 size = mtl_gsc_header_rd(xe, map, offset, message_size);
	u32 status = mtl_gsc_header_rd(xe, map, offset, status);
	u32 payload_size = size - GSC_HDR_SIZE;

	if (marker != GSC_HECI_VALIDITY_MARKER)
		return -EPROTO;

	if (status != 0) {
		drm_err(&xe->drm, "GSC header readout indicates error: %d\n",
			status);
		return -EINVAL;
	}

	if (size < GSC_HDR_SIZE || payload_size < min_payload_size)
		return -ENODATA;

	if (payload_offset)
		*payload_offset = offset + GSC_HDR_SIZE;

	return 0;
}

/**
 * xe_gsc_pkt_submit_kernel - submit a kernel heci pkt to the GSC
 * @gsc: the GSC uC
 * @addr_in: GGTT address of the message to send to the GSC
 * @size_in: size of the message to send to the GSC
 * @addr_out: GGTT address for the GSC to write the reply to
 * @size_out: size of the memory reserved for the reply
 */
int xe_gsc_pkt_submit_kernel(struct xe_gsc *gsc, u64 addr_in, u32 size_in,
			     u64 addr_out, u32 size_out)
{
	struct xe_gt *gt = gsc_to_gt(gsc);
	struct xe_bb *bb;
	struct xe_sched_job *job;
	struct dma_fence *fence;
	long timeout;

	if (size_in < GSC_HDR_SIZE)
		return -ENODATA;

	if (size_out < GSC_HDR_SIZE)
		return -ENOMEM;

	bb = xe_bb_new(gt, 8, false);
	if (IS_ERR(bb))
		return PTR_ERR(bb);

	bb->cs[bb->len++] = GSC_HECI_CMD_PKT;
	bb->cs[bb->len++] = lower_32_bits(addr_in);
	bb->cs[bb->len++] = upper_32_bits(addr_in);
	bb->cs[bb->len++] = size_in;
	bb->cs[bb->len++] = lower_32_bits(addr_out);
	bb->cs[bb->len++] = upper_32_bits(addr_out);
	bb->cs[bb->len++] = size_out;
	bb->cs[bb->len++] = 0;

	job = xe_bb_create_job(gsc->q, bb);
	if (IS_ERR(job)) {
		xe_bb_free(bb, NULL);
		return PTR_ERR(job);
	}

	xe_sched_job_arm(job);
	fence = dma_fence_get(&job->drm.s_fence->finished);
	xe_sched_job_push(job);

	timeout = dma_fence_wait_timeout(fence, false, HZ);
	dma_fence_put(fence);
	xe_bb_free(bb, NULL);
	if (timeout < 0)
		return timeout;
	else if (!timeout)
		return -ETIME;

	return 0;
}
