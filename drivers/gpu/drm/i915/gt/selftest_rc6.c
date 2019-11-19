/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2019 Intel Corporation
 */

#include "intel_context.h"
#include "intel_engine_pm.h"
#include "intel_gt_requests.h"
#include "intel_ring.h"
#include "selftest_rc6.h"

#include "selftests/i915_random.h"

static const u32 *__live_rc6_ctx(struct intel_context *ce)
{
	struct i915_request *rq;
	const u32 *result;
	u32 cmd;
	u32 *cs;

	rq = intel_context_create_request(ce);
	if (IS_ERR(rq))
		return ERR_CAST(rq);

	cs = intel_ring_begin(rq, 4);
	if (IS_ERR(cs)) {
		i915_request_add(rq);
		return cs;
	}

	cmd = MI_STORE_REGISTER_MEM | MI_USE_GGTT;
	if (INTEL_GEN(rq->i915) >= 8)
		cmd++;

	*cs++ = cmd;
	*cs++ = i915_mmio_reg_offset(GEN8_RC6_CTX_INFO);
	*cs++ = ce->timeline->hwsp_offset + 8;
	*cs++ = 0;
	intel_ring_advance(rq, cs);

	result = rq->hwsp_seqno + 2;
	i915_request_add(rq);

	return result;
}

static struct intel_engine_cs **
randomised_engines(struct intel_gt *gt,
		   struct rnd_state *prng,
		   unsigned int *count)
{
	struct intel_engine_cs *engine, **engines;
	enum intel_engine_id id;
	int n;

	n = 0;
	for_each_engine(engine, gt, id)
		n++;
	if (!n)
		return NULL;

	engines = kmalloc_array(n, sizeof(*engines), GFP_KERNEL);
	if (!engines)
		return NULL;

	n = 0;
	for_each_engine(engine, gt, id)
		engines[n++] = engine;

	i915_prandom_shuffle(engines, sizeof(*engines), n, prng);

	*count = n;
	return engines;
}

int live_rc6_ctx_wa(void *arg)
{
	struct intel_gt *gt = arg;
	struct intel_engine_cs **engines;
	unsigned int n, count;
	I915_RND_STATE(prng);
	int err = 0;

	/* A read of CTX_INFO upsets rc6. Poke the bear! */
	if (INTEL_GEN(gt->i915) < 8)
		return 0;

	engines = randomised_engines(gt, &prng, &count);
	if (!engines)
		return 0;

	for (n = 0; n < count; n++) {
		struct intel_engine_cs *engine = engines[n];
		int pass;

		for (pass = 0; pass < 2; pass++) {
			struct intel_context *ce;
			unsigned int resets =
				i915_reset_engine_count(&gt->i915->gpu_error,
							engine);
			const u32 *res;

			/* Use a sacrifical context */
			ce = intel_context_create(engine->kernel_context->gem_context,
						  engine);
			if (IS_ERR(ce)) {
				err = PTR_ERR(ce);
				goto out;
			}

			intel_engine_pm_get(engine);
			res = __live_rc6_ctx(ce);
			intel_engine_pm_put(engine);
			intel_context_put(ce);
			if (IS_ERR(res)) {
				err = PTR_ERR(res);
				goto out;
			}

			if (intel_gt_wait_for_idle(gt, HZ / 5) == -ETIME) {
				intel_gt_set_wedged(gt);
				err = -ETIME;
				goto out;
			}

			intel_gt_pm_wait_for_idle(gt);
			pr_debug("%s: CTX_INFO=%0x\n",
				 engine->name, READ_ONCE(*res));

			if (resets !=
			    i915_reset_engine_count(&gt->i915->gpu_error,
						    engine)) {
				pr_err("%s: GPU reset required\n",
				       engine->name);
				add_taint_for_CI(TAINT_WARN);
				err = -EIO;
				goto out;
			}
		}
	}

out:
	kfree(engines);
	return err;
}
