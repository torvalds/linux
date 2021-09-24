// SPDX-License-Identifier: MIT
/*
 * Copyright(c) 2020, Intel Corporation. All rights reserved.
 */

#include "intel_pxp.h"
#include "intel_pxp_cmd.h"
#include "intel_pxp_session.h"
#include "gt/intel_context.h"
#include "gt/intel_engine_pm.h"
#include "gt/intel_gpu_commands.h"
#include "gt/intel_ring.h"

#include "i915_trace.h"

/* stall until prior PXP and MFX/HCP/HUC objects are cmopleted */
#define MFX_WAIT_PXP (MFX_WAIT | \
		      MFX_WAIT_DW0_PXP_SYNC_CONTROL_FLAG | \
		      MFX_WAIT_DW0_MFX_SYNC_CONTROL_FLAG)

static u32 *pxp_emit_session_selection(u32 *cs, u32 idx)
{
	*cs++ = MFX_WAIT_PXP;

	/* pxp off */
	*cs++ = MI_FLUSH_DW;
	*cs++ = 0;
	*cs++ = 0;

	/* select session */
	*cs++ = MI_SET_APPID | MI_SET_APPID_SESSION_ID(idx);

	*cs++ = MFX_WAIT_PXP;

	/* pxp on */
	*cs++ = MI_FLUSH_DW | MI_FLUSH_DW_PROTECTED_MEM_EN |
		MI_FLUSH_DW_OP_STOREDW | MI_FLUSH_DW_STORE_INDEX;
	*cs++ = I915_GEM_HWS_PXP_ADDR | MI_FLUSH_DW_USE_GTT;
	*cs++ = 0;

	*cs++ = MFX_WAIT_PXP;

	return cs;
}

static u32 *pxp_emit_inline_termination(u32 *cs)
{
	/* session inline termination */
	*cs++ = CRYPTO_KEY_EXCHANGE;
	*cs++ = 0;

	return cs;
}

static u32 *pxp_emit_session_termination(u32 *cs, u32 idx)
{
	cs = pxp_emit_session_selection(cs, idx);
	cs = pxp_emit_inline_termination(cs);

	return cs;
}

static u32 *pxp_emit_wait(u32 *cs)
{
	/* wait for cmds to go through */
	*cs++ = MFX_WAIT_PXP;
	*cs++ = 0;

	return cs;
}

/*
 * if we ever need to terminate more than one session, we can submit multiple
 * selections and terminations back-to-back with a single wait at the end
 */
#define SELECTION_LEN 10
#define TERMINATION_LEN 2
#define SESSION_TERMINATION_LEN(x) ((SELECTION_LEN + TERMINATION_LEN) * (x))
#define WAIT_LEN 2

static void pxp_request_commit(struct i915_request *rq)
{
	struct i915_sched_attr attr = { .priority = I915_PRIORITY_MAX };
	struct intel_timeline * const tl = i915_request_timeline(rq);

	lockdep_unpin_lock(&tl->mutex, rq->cookie);

	trace_i915_request_add(rq);
	__i915_request_commit(rq);
	__i915_request_queue(rq, &attr);

	mutex_unlock(&tl->mutex);
}

int intel_pxp_terminate_session(struct intel_pxp *pxp, u32 id)
{
	struct i915_request *rq;
	struct intel_context *ce = pxp->ce;
	u32 *cs;
	int err = 0;

	if (!intel_pxp_is_enabled(pxp))
		return 0;

	rq = i915_request_create(ce);
	if (IS_ERR(rq))
		return PTR_ERR(rq);

	if (ce->engine->emit_init_breadcrumb) {
		err = ce->engine->emit_init_breadcrumb(rq);
		if (err)
			goto out_rq;
	}

	cs = intel_ring_begin(rq, SESSION_TERMINATION_LEN(1) + WAIT_LEN);
	if (IS_ERR(cs)) {
		err = PTR_ERR(cs);
		goto out_rq;
	}

	cs = pxp_emit_session_termination(cs, id);
	cs = pxp_emit_wait(cs);

	intel_ring_advance(rq, cs);

out_rq:
	i915_request_get(rq);

	if (unlikely(err))
		i915_request_set_error_once(rq, err);

	pxp_request_commit(rq);

	if (!err && i915_request_wait(rq, 0, HZ / 5) < 0)
		err = -ETIME;

	i915_request_put(rq);

	return err;
}

