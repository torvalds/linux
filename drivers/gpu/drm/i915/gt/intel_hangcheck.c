/*
 * Copyright © 2016 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 */

#include "intel_reset.h"
#include "i915_drv.h"

struct hangcheck {
	u64 acthd;
	u32 ring;
	u32 head;
	enum intel_engine_hangcheck_action action;
	unsigned long action_timestamp;
	int deadlock;
	struct intel_instdone instdone;
	bool wedged:1;
	bool stalled:1;
};

static bool instdone_unchanged(u32 current_instdone, u32 *old_instdone)
{
	u32 tmp = current_instdone | *old_instdone;
	bool unchanged;

	unchanged = tmp == *old_instdone;
	*old_instdone |= tmp;

	return unchanged;
}

static bool subunits_stuck(struct intel_engine_cs *engine)
{
	struct drm_i915_private *dev_priv = engine->i915;
	struct intel_instdone instdone;
	struct intel_instdone *accu_instdone = &engine->hangcheck.instdone;
	bool stuck;
	int slice;
	int subslice;

	if (engine->id != RCS0)
		return true;

	intel_engine_get_instdone(engine, &instdone);

	/* There might be unstable subunit states even when
	 * actual head is not moving. Filter out the unstable ones by
	 * accumulating the undone -> done transitions and only
	 * consider those as progress.
	 */
	stuck = instdone_unchanged(instdone.instdone,
				   &accu_instdone->instdone);
	stuck &= instdone_unchanged(instdone.slice_common,
				    &accu_instdone->slice_common);

	for_each_instdone_slice_subslice(dev_priv, slice, subslice) {
		stuck &= instdone_unchanged(instdone.sampler[slice][subslice],
					    &accu_instdone->sampler[slice][subslice]);
		stuck &= instdone_unchanged(instdone.row[slice][subslice],
					    &accu_instdone->row[slice][subslice]);
	}

	return stuck;
}

static enum intel_engine_hangcheck_action
head_stuck(struct intel_engine_cs *engine, u64 acthd)
{
	if (acthd != engine->hangcheck.acthd) {

		/* Clear subunit states on head movement */
		memset(&engine->hangcheck.instdone, 0,
		       sizeof(engine->hangcheck.instdone));

		return ENGINE_ACTIVE_HEAD;
	}

	if (!subunits_stuck(engine))
		return ENGINE_ACTIVE_SUBUNITS;

	return ENGINE_DEAD;
}

static enum intel_engine_hangcheck_action
engine_stuck(struct intel_engine_cs *engine, u64 acthd)
{
	struct drm_i915_private *dev_priv = engine->i915;
	enum intel_engine_hangcheck_action ha;
	u32 tmp;

	ha = head_stuck(engine, acthd);
	if (ha != ENGINE_DEAD)
		return ha;

	if (IS_GEN(dev_priv, 2))
		return ENGINE_DEAD;

	/* Is the chip hanging on a WAIT_FOR_EVENT?
	 * If so we can simply poke the RB_WAIT bit
	 * and break the hang. This should work on
	 * all but the second generation chipsets.
	 */
	tmp = ENGINE_READ(engine, RING_CTL);
	if (tmp & RING_WAIT) {
		i915_handle_error(dev_priv, engine->mask, 0,
				  "stuck wait on %s", engine->name);
		ENGINE_WRITE(engine, RING_CTL, tmp);
		return ENGINE_WAIT_KICK;
	}

	return ENGINE_DEAD;
}

static void hangcheck_load_sample(struct intel_engine_cs *engine,
				  struct hangcheck *hc)
{
	hc->acthd = intel_engine_get_active_head(engine);
	hc->ring = ENGINE_READ(engine, RING_START);
	hc->head = ENGINE_READ(engine, RING_HEAD);
}

static void hangcheck_store_sample(struct intel_engine_cs *engine,
				   const struct hangcheck *hc)
{
	engine->hangcheck.acthd = hc->acthd;
	engine->hangcheck.last_ring = hc->ring;
	engine->hangcheck.last_head = hc->head;
}

static enum intel_engine_hangcheck_action
hangcheck_get_action(struct intel_engine_cs *engine,
		     const struct hangcheck *hc)
{
	if (intel_engine_is_idle(engine))
		return ENGINE_IDLE;

	if (engine->hangcheck.last_ring != hc->ring)
		return ENGINE_ACTIVE_SEQNO;

	if (engine->hangcheck.last_head != hc->head)
		return ENGINE_ACTIVE_SEQNO;

	return engine_stuck(engine, hc->acthd);
}

static void hangcheck_accumulate_sample(struct intel_engine_cs *engine,
					struct hangcheck *hc)
{
	unsigned long timeout = I915_ENGINE_DEAD_TIMEOUT;

	hc->action = hangcheck_get_action(engine, hc);

	/* We always increment the progress
	 * if the engine is busy and still processing
	 * the same request, so that no single request
	 * can run indefinitely (such as a chain of
	 * batches). The only time we do not increment
	 * the hangcheck score on this ring, if this
	 * engine is in a legitimate wait for another
	 * engine. In that case the waiting engine is a
	 * victim and we want to be sure we catch the
	 * right culprit. Then every time we do kick
	 * the ring, make it as a progress as the seqno
	 * advancement might ensure and if not, it
	 * will catch the hanging engine.
	 */

	switch (hc->action) {
	case ENGINE_IDLE:
	case ENGINE_ACTIVE_SEQNO:
		/* Clear head and subunit states on seqno movement */
		hc->acthd = 0;

		memset(&engine->hangcheck.instdone, 0,
		       sizeof(engine->hangcheck.instdone));

		/* Intentional fall through */
	case ENGINE_WAIT_KICK:
	case ENGINE_WAIT:
		engine->hangcheck.action_timestamp = jiffies;
		break;

	case ENGINE_ACTIVE_HEAD:
	case ENGINE_ACTIVE_SUBUNITS:
		/*
		 * Seqno stuck with still active engine gets leeway,
		 * in hopes that it is just a long shader.
		 */
		timeout = I915_SEQNO_DEAD_TIMEOUT;
		break;

	case ENGINE_DEAD:
		break;

	default:
		MISSING_CASE(hc->action);
	}

	hc->stalled = time_after(jiffies,
				 engine->hangcheck.action_timestamp + timeout);
	hc->wedged = time_after(jiffies,
				 engine->hangcheck.action_timestamp +
				 I915_ENGINE_WEDGED_TIMEOUT);
}

static void hangcheck_declare_hang(struct drm_i915_private *i915,
				   unsigned int hung,
				   unsigned int stuck)
{
	struct intel_engine_cs *engine;
	intel_engine_mask_t tmp;
	char msg[80];
	int len;

	/* If some rings hung but others were still busy, only
	 * blame the hanging rings in the synopsis.
	 */
	if (stuck != hung)
		hung &= ~stuck;
	len = scnprintf(msg, sizeof(msg),
			"%s on ", stuck == hung ? "no progress" : "hang");
	for_each_engine_masked(engine, i915, hung, tmp)
		len += scnprintf(msg + len, sizeof(msg) - len,
				 "%s, ", engine->name);
	msg[len-2] = '\0';

	return i915_handle_error(i915, hung, I915_ERROR_CAPTURE, "%s", msg);
}

/*
 * This is called when the chip hasn't reported back with completed
 * batchbuffers in a long time. We keep track per ring seqno progress and
 * if there are no progress, hangcheck score for that ring is increased.
 * Further, acthd is inspected to see if the ring is stuck. On stuck case
 * we kick the ring. If we see no progress on three subsequent calls
 * we assume chip is wedged and try to fix it by resetting the chip.
 */
static void i915_hangcheck_elapsed(struct work_struct *work)
{
	struct drm_i915_private *dev_priv =
		container_of(work, typeof(*dev_priv),
			     gpu_error.hangcheck_work.work);
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	unsigned int hung = 0, stuck = 0, wedged = 0;
	intel_wakeref_t wakeref;

	if (!i915_modparams.enable_hangcheck)
		return;

	if (!READ_ONCE(dev_priv->gt.awake))
		return;

	if (i915_terminally_wedged(dev_priv))
		return;

	wakeref = intel_runtime_pm_get_if_in_use(dev_priv);
	if (!wakeref)
		return;

	/* As enabling the GPU requires fairly extensive mmio access,
	 * periodically arm the mmio checker to see if we are triggering
	 * any invalid access.
	 */
	intel_uncore_arm_unclaimed_mmio_detection(&dev_priv->uncore);

	for_each_engine(engine, dev_priv, id) {
		struct hangcheck hc;

		intel_engine_signal_breadcrumbs(engine);

		hangcheck_load_sample(engine, &hc);
		hangcheck_accumulate_sample(engine, &hc);
		hangcheck_store_sample(engine, &hc);

		if (hc.stalled) {
			hung |= engine->mask;
			if (hc.action != ENGINE_DEAD)
				stuck |= engine->mask;
		}

		if (hc.wedged)
			wedged |= engine->mask;
	}

	if (GEM_SHOW_DEBUG() && (hung | stuck)) {
		struct drm_printer p = drm_debug_printer("hangcheck");

		for_each_engine(engine, dev_priv, id) {
			if (intel_engine_is_idle(engine))
				continue;

			intel_engine_dump(engine, &p, "%s\n", engine->name);
		}
	}

	if (wedged) {
		dev_err(dev_priv->drm.dev,
			"GPU recovery timed out,"
			" cancelling all in-flight rendering.\n");
		GEM_TRACE_DUMP();
		i915_gem_set_wedged(dev_priv);
	}

	if (hung)
		hangcheck_declare_hang(dev_priv, hung, stuck);

	intel_runtime_pm_put(dev_priv, wakeref);

	/* Reset timer in case GPU hangs without another request being added */
	i915_queue_hangcheck(dev_priv);
}

void intel_engine_init_hangcheck(struct intel_engine_cs *engine)
{
	memset(&engine->hangcheck, 0, sizeof(engine->hangcheck));
	engine->hangcheck.action_timestamp = jiffies;
}

void intel_hangcheck_init(struct drm_i915_private *i915)
{
	INIT_DELAYED_WORK(&i915->gpu_error.hangcheck_work,
			  i915_hangcheck_elapsed);
}

#if IS_ENABLED(CONFIG_DRM_I915_SELFTEST)
#include "selftest_hangcheck.c"
#endif
