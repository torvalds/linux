// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include "gt/intel_engine_pm.h"
#include "gt/intel_gpu_commands.h"
#include "gt/intel_gt.h"
#include "gt/intel_ring.h"
#include "intel_gsc_uc_heci_cmd_submit.h"

struct gsc_heci_pkt {
	u64 addr_in;
	u32 size_in;
	u64 addr_out;
	u32 size_out;
};

static int emit_gsc_heci_pkt(struct i915_request *rq, struct gsc_heci_pkt *pkt)
{
	u32 *cs;

	cs = intel_ring_begin(rq, 8);
	if (IS_ERR(cs))
		return PTR_ERR(cs);

	*cs++ = GSC_HECI_CMD_PKT;
	*cs++ = lower_32_bits(pkt->addr_in);
	*cs++ = upper_32_bits(pkt->addr_in);
	*cs++ = pkt->size_in;
	*cs++ = lower_32_bits(pkt->addr_out);
	*cs++ = upper_32_bits(pkt->addr_out);
	*cs++ = pkt->size_out;
	*cs++ = 0;

	intel_ring_advance(rq, cs);

	return 0;
}

int intel_gsc_uc_heci_cmd_submit_packet(struct intel_gsc_uc *gsc, u64 addr_in,
					u32 size_in, u64 addr_out,
					u32 size_out)
{
	struct intel_context *ce = gsc->ce;
	struct i915_request *rq;
	struct gsc_heci_pkt pkt = {
	.addr_in = addr_in,
	.size_in = size_in,
	.addr_out = addr_out,
	.size_out = size_out
	};
	int err;

	if (!ce)
		return -ENODEV;

	rq = i915_request_create(ce);
	if (IS_ERR(rq))
		return PTR_ERR(rq);

	if (ce->engine->emit_init_breadcrumb) {
		err = ce->engine->emit_init_breadcrumb(rq);
		if (err)
			goto out_rq;
	}

	err = emit_gsc_heci_pkt(rq, &pkt);

	if (err)
		goto out_rq;

	err = ce->engine->emit_flush(rq, 0);

out_rq:
	i915_request_get(rq);

	if (unlikely(err))
		i915_request_set_error_once(rq, err);

	i915_request_add(rq);

	if (!err && i915_request_wait(rq, 0, msecs_to_jiffies(500)) < 0)
		err = -ETIME;

	i915_request_put(rq);

	if (err)
		drm_err(&gsc_uc_to_gt(gsc)->i915->drm,
			"Request submission for GSC heci cmd failed (%d)\n",
			err);

	return err;
}

void intel_gsc_uc_heci_cmd_emit_mtl_header(struct intel_gsc_mtl_header *header,
					   u8 heci_client_id, u32 message_size,
					   u64 host_session_id)
{
	host_session_id &= ~HOST_SESSION_MASK;
	if (heci_client_id == HECI_MEADDRESS_PXP)
		host_session_id |= HOST_SESSION_PXP_SINGLE;

	header->validity_marker = GSC_HECI_VALIDITY_MARKER;
	header->heci_client_id = heci_client_id;
	header->host_session_handle = host_session_id;
	header->header_version = MTL_GSC_HEADER_VERSION;
	header->message_size = message_size;
}
