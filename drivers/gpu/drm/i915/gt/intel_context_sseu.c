// SPDX-License-Identifier: MIT
/*
 * Copyright © 2019 Intel Corporation
 */

#include "i915_drv.h"
#include "i915_vma.h"
#include "intel_context.h"
#include "intel_engine_pm.h"
#include "intel_gpu_commands.h"
#include "intel_lrc.h"
#include "intel_lrc_reg.h"
#include "intel_ring.h"
#include "intel_sseu.h"

static int gen8_emit_rpcs_config(struct i915_request *rq,
				 const struct intel_context *ce,
				 const struct intel_sseu sseu)
{
	u64 offset;
	u32 *cs;

	cs = intel_ring_begin(rq, 4);
	if (IS_ERR(cs))
		return PTR_ERR(cs);

	offset = i915_ggtt_offset(ce->state) +
		 LRC_STATE_OFFSET + CTX_R_PWR_CLK_STATE * 4;

	*cs++ = MI_STORE_DWORD_IMM_GEN4 | MI_USE_GGTT;
	*cs++ = lower_32_bits(offset);
	*cs++ = upper_32_bits(offset);
	*cs++ = intel_sseu_make_rpcs(rq->engine->gt, &sseu);

	intel_ring_advance(rq, cs);

	return 0;
}

static int
gen8_modify_rpcs(struct intel_context *ce, const struct intel_sseu sseu)
{
	struct i915_request *rq;
	int ret;

	lockdep_assert_held(&ce->pin_mutex);

	/*
	 * If the context is not idle, we have to submit an ordered request to
	 * modify its context image via the kernel context (writing to our own
	 * image, or into the registers directory, does not stick). Pristine
	 * and idle contexts will be configured on pinning.
	 */
	if (!intel_context_pin_if_active(ce))
		return 0;

	rq = intel_engine_create_kernel_request(ce->engine);
	if (IS_ERR(rq)) {
		ret = PTR_ERR(rq);
		goto out_unpin;
	}

	/* Serialise with the remote context */
	ret = intel_context_prepare_remote_request(ce, rq);
	if (ret == 0)
		ret = gen8_emit_rpcs_config(rq, ce, sseu);

	i915_request_add(rq);
out_unpin:
	intel_context_unpin(ce);
	return ret;
}

int
intel_context_reconfigure_sseu(struct intel_context *ce,
			       const struct intel_sseu sseu)
{
	int ret;

	GEM_BUG_ON(INTEL_GEN(ce->engine->i915) < 8);

	ret = intel_context_lock_pinned(ce);
	if (ret)
		return ret;

	/* Nothing to do if unmodified. */
	if (!memcmp(&ce->sseu, &sseu, sizeof(sseu)))
		goto unlock;

	ret = gen8_modify_rpcs(ce, sseu);
	if (!ret)
		ce->sseu = sseu;

unlock:
	intel_context_unlock_pinned(ce);
	return ret;
}
