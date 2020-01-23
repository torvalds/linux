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

int live_rc6_manual(void *arg)
{
	struct intel_gt *gt = arg;
	struct intel_rc6 *rc6 = &gt->rc6;
	intel_wakeref_t wakeref;
	u64 res[2];
	int err = 0;

	/*
	 * Our claim is that we can "encourage" the GPU to enter rc6 at will.
	 * Let's try it!
	 */

	if (!rc6->enabled)
		return 0;

	/* bsw/byt use a PCU and decouple RC6 from our manual control */
	if (IS_VALLEYVIEW(gt->i915) || IS_CHERRYVIEW(gt->i915))
		return 0;

	wakeref = intel_runtime_pm_get(gt->uncore->rpm);

	/* Force RC6 off for starters */
	__intel_rc6_disable(rc6);
	msleep(1); /* wakeup is not immediate, takes about 100us on icl */

	res[0] = intel_rc6_residency_ns(rc6, GEN6_GT_GFX_RC6);
	msleep(250);
	res[1] = intel_rc6_residency_ns(rc6, GEN6_GT_GFX_RC6);
	if ((res[1] - res[0]) >> 10) {
		pr_err("RC6 residency increased by %lldus while disabled for 250ms!\n",
		       (res[1] - res[0]) >> 10);
		err = -EINVAL;
		goto out_unlock;
	}

	/* Manually enter RC6 */
	intel_rc6_park(rc6);

	res[0] = intel_rc6_residency_ns(rc6, GEN6_GT_GFX_RC6);
	msleep(100);
	res[1] = intel_rc6_residency_ns(rc6, GEN6_GT_GFX_RC6);

	if (res[1] == res[0]) {
		pr_err("Did not enter RC6! RC6_STATE=%08x, RC6_CONTROL=%08x, residency=%lld\n",
		       intel_uncore_read_fw(gt->uncore, GEN6_RC_STATE),
		       intel_uncore_read_fw(gt->uncore, GEN6_RC_CONTROL),
		       res[0]);
		err = -EINVAL;
	}

	/* Restore what should have been the original state! */
	intel_rc6_unpark(rc6);

out_unlock:
	intel_runtime_pm_put(gt->uncore->rpm, wakeref);
	return err;
}

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
			ce = intel_context_create(engine);
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
