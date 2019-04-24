/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2019 Intel Corporation
 */

#include "i915_drv.h"
#include "i915_gem_pm.h"
#include "i915_globals.h"
#include "intel_pm.h"

static void __i915_gem_park(struct drm_i915_private *i915)
{
	intel_wakeref_t wakeref;

	GEM_TRACE("\n");

	lockdep_assert_held(&i915->drm.struct_mutex);
	GEM_BUG_ON(i915->gt.active_requests);
	GEM_BUG_ON(!list_empty(&i915->gt.active_rings));

	if (!i915->gt.awake)
		return;

	/*
	 * Be paranoid and flush a concurrent interrupt to make sure
	 * we don't reactivate any irq tasklets after parking.
	 *
	 * FIXME: Note that even though we have waited for execlists to be idle,
	 * there may still be an in-flight interrupt even though the CSB
	 * is now empty. synchronize_irq() makes sure that a residual interrupt
	 * is completed before we continue, but it doesn't prevent the HW from
	 * raising a spurious interrupt later. To complete the shield we should
	 * coordinate disabling the CS irq with flushing the interrupts.
	 */
	synchronize_irq(i915->drm.irq);

	intel_engines_park(i915);
	i915_timelines_park(i915);

	i915_pmu_gt_parked(i915);
	i915_vma_parked(i915);

	wakeref = fetch_and_zero(&i915->gt.awake);
	GEM_BUG_ON(!wakeref);

	if (INTEL_GEN(i915) >= 6)
		gen6_rps_idle(i915);

	intel_display_power_put(i915, POWER_DOMAIN_GT_IRQ, wakeref);

	i915_globals_park();
}

static bool switch_to_kernel_context_sync(struct drm_i915_private *i915,
					  unsigned long mask)
{
	bool result = true;

	/*
	 * Even if we fail to switch, give whatever is running a small chance
	 * to save itself before we report the failure. Yes, this may be a
	 * false positive due to e.g. ENOMEM, caveat emptor!
	 */
	if (i915_gem_switch_to_kernel_context(i915, mask))
		result = false;

	if (i915_gem_wait_for_idle(i915,
				   I915_WAIT_LOCKED |
				   I915_WAIT_FOR_IDLE_BOOST,
				   I915_GEM_IDLE_TIMEOUT))
		result = false;

	if (!result) {
		if (i915_modparams.reset) { /* XXX hide warning from gem_eio */
			dev_err(i915->drm.dev,
				"Failed to idle engines, declaring wedged!\n");
			GEM_TRACE_DUMP();
		}

		/* Forcibly cancel outstanding work and leave the gpu quiet. */
		i915_gem_set_wedged(i915);
	}

	i915_retire_requests(i915); /* ensure we flush after wedging */
	return result;
}

static void idle_work_handler(struct work_struct *work)
{
	struct drm_i915_private *i915 =
		container_of(work, typeof(*i915), gem.idle_work.work);
	bool rearm_hangcheck;

	if (!READ_ONCE(i915->gt.awake))
		return;

	if (READ_ONCE(i915->gt.active_requests))
		return;

	rearm_hangcheck =
		cancel_delayed_work_sync(&i915->gpu_error.hangcheck_work);

	if (!mutex_trylock(&i915->drm.struct_mutex)) {
		/* Currently busy, come back later */
		mod_delayed_work(i915->wq,
				 &i915->gem.idle_work,
				 msecs_to_jiffies(50));
		goto out_rearm;
	}

	/*
	 * Flush out the last user context, leaving only the pinned
	 * kernel context resident. Should anything unfortunate happen
	 * while we are idle (such as the GPU being power cycled), no users
	 * will be harmed.
	 */
	if (!work_pending(&i915->gem.idle_work.work) &&
	    !i915->gt.active_requests) {
		++i915->gt.active_requests; /* don't requeue idle */

		switch_to_kernel_context_sync(i915, i915->gt.active_engines);

		if (!--i915->gt.active_requests) {
			__i915_gem_park(i915);
			rearm_hangcheck = false;
		}
	}

	mutex_unlock(&i915->drm.struct_mutex);

out_rearm:
	if (rearm_hangcheck) {
		GEM_BUG_ON(!i915->gt.awake);
		i915_queue_hangcheck(i915);
	}
}

static void retire_work_handler(struct work_struct *work)
{
	struct drm_i915_private *i915 =
		container_of(work, typeof(*i915), gem.retire_work.work);

	/* Come back later if the device is busy... */
	if (mutex_trylock(&i915->drm.struct_mutex)) {
		i915_retire_requests(i915);
		mutex_unlock(&i915->drm.struct_mutex);
	}

	/*
	 * Keep the retire handler running until we are finally idle.
	 * We do not need to do this test under locking as in the worst-case
	 * we queue the retire worker once too often.
	 */
	if (READ_ONCE(i915->gt.awake))
		queue_delayed_work(i915->wq,
				   &i915->gem.retire_work,
				   round_jiffies_up_relative(HZ));
}

void i915_gem_park(struct drm_i915_private *i915)
{
	GEM_TRACE("\n");

	lockdep_assert_held(&i915->drm.struct_mutex);
	GEM_BUG_ON(i915->gt.active_requests);

	if (!i915->gt.awake)
		return;

	/* Defer the actual call to __i915_gem_park() to prevent ping-pongs */
	mod_delayed_work(i915->wq, &i915->gem.idle_work, msecs_to_jiffies(100));
}

void i915_gem_unpark(struct drm_i915_private *i915)
{
	GEM_TRACE("\n");

	lockdep_assert_held(&i915->drm.struct_mutex);
	GEM_BUG_ON(!i915->gt.active_requests);
	assert_rpm_wakelock_held(i915);

	if (i915->gt.awake)
		return;

	/*
	 * It seems that the DMC likes to transition between the DC states a lot
	 * when there are no connected displays (no active power domains) during
	 * command submission.
	 *
	 * This activity has negative impact on the performance of the chip with
	 * huge latencies observed in the interrupt handler and elsewhere.
	 *
	 * Work around it by grabbing a GT IRQ power domain whilst there is any
	 * GT activity, preventing any DC state transitions.
	 */
	i915->gt.awake = intel_display_power_get(i915, POWER_DOMAIN_GT_IRQ);
	GEM_BUG_ON(!i915->gt.awake);

	i915_globals_unpark();

	intel_enable_gt_powersave(i915);
	i915_update_gfx_val(i915);
	if (INTEL_GEN(i915) >= 6)
		gen6_rps_busy(i915);
	i915_pmu_gt_unparked(i915);

	intel_engines_unpark(i915);

	i915_queue_hangcheck(i915);

	queue_delayed_work(i915->wq,
			   &i915->gem.retire_work,
			   round_jiffies_up_relative(HZ));
}

bool i915_gem_load_power_context(struct drm_i915_private *i915)
{
	/* Force loading the kernel context on all engines */
	if (!switch_to_kernel_context_sync(i915, ALL_ENGINES))
		return false;

	/*
	 * Immediately park the GPU so that we enable powersaving and
	 * treat it as idle. The next time we issue a request, we will
	 * unpark and start using the engine->pinned_default_state, otherwise
	 * it is in limbo and an early reset may fail.
	 */
	__i915_gem_park(i915);

	return true;
}

void i915_gem_suspend(struct drm_i915_private *i915)
{
	intel_wakeref_t wakeref;

	GEM_TRACE("\n");

	wakeref = intel_runtime_pm_get(i915);

	mutex_lock(&i915->drm.struct_mutex);

	/*
	 * We have to flush all the executing contexts to main memory so
	 * that they can saved in the hibernation image. To ensure the last
	 * context image is coherent, we have to switch away from it. That
	 * leaves the i915->kernel_context still active when
	 * we actually suspend, and its image in memory may not match the GPU
	 * state. Fortunately, the kernel_context is disposable and we do
	 * not rely on its state.
	 */
	switch_to_kernel_context_sync(i915, i915->gt.active_engines);

	mutex_unlock(&i915->drm.struct_mutex);
	i915_reset_flush(i915);

	drain_delayed_work(&i915->gem.retire_work);

	/*
	 * As the idle_work is rearming if it detects a race, play safe and
	 * repeat the flush until it is definitely idle.
	 */
	drain_delayed_work(&i915->gem.idle_work);

	flush_workqueue(i915->wq);

	/*
	 * Assert that we successfully flushed all the work and
	 * reset the GPU back to its idle, low power state.
	 */
	GEM_BUG_ON(i915->gt.awake);

	intel_uc_suspend(i915);

	intel_runtime_pm_put(i915, wakeref);
}

void i915_gem_suspend_late(struct drm_i915_private *i915)
{
	struct drm_i915_gem_object *obj;
	struct list_head *phases[] = {
		&i915->mm.unbound_list,
		&i915->mm.bound_list,
		NULL
	}, **phase;

	/*
	 * Neither the BIOS, ourselves or any other kernel
	 * expects the system to be in execlists mode on startup,
	 * so we need to reset the GPU back to legacy mode. And the only
	 * known way to disable logical contexts is through a GPU reset.
	 *
	 * So in order to leave the system in a known default configuration,
	 * always reset the GPU upon unload and suspend. Afterwards we then
	 * clean up the GEM state tracking, flushing off the requests and
	 * leaving the system in a known idle state.
	 *
	 * Note that is of the upmost importance that the GPU is idle and
	 * all stray writes are flushed *before* we dismantle the backing
	 * storage for the pinned objects.
	 *
	 * However, since we are uncertain that resetting the GPU on older
	 * machines is a good idea, we don't - just in case it leaves the
	 * machine in an unusable condition.
	 */

	mutex_lock(&i915->drm.struct_mutex);
	for (phase = phases; *phase; phase++) {
		list_for_each_entry(obj, *phase, mm.link)
			WARN_ON(i915_gem_object_set_to_gtt_domain(obj, false));
	}
	mutex_unlock(&i915->drm.struct_mutex);

	intel_uc_sanitize(i915);
	i915_gem_sanitize(i915);
}

void i915_gem_resume(struct drm_i915_private *i915)
{
	GEM_TRACE("\n");

	WARN_ON(i915->gt.awake);

	mutex_lock(&i915->drm.struct_mutex);
	intel_uncore_forcewake_get(&i915->uncore, FORCEWAKE_ALL);

	i915_gem_restore_gtt_mappings(i915);
	i915_gem_restore_fences(i915);

	/*
	 * As we didn't flush the kernel context before suspend, we cannot
	 * guarantee that the context image is complete. So let's just reset
	 * it and start again.
	 */
	intel_gt_resume(i915);

	if (i915_gem_init_hw(i915))
		goto err_wedged;

	intel_uc_resume(i915);

	/* Always reload a context for powersaving. */
	if (!i915_gem_load_power_context(i915))
		goto err_wedged;

out_unlock:
	intel_uncore_forcewake_put(&i915->uncore, FORCEWAKE_ALL);
	mutex_unlock(&i915->drm.struct_mutex);
	return;

err_wedged:
	if (!i915_reset_failed(i915)) {
		dev_err(i915->drm.dev,
			"Failed to re-initialize GPU, declaring it wedged!\n");
		i915_gem_set_wedged(i915);
	}
	goto out_unlock;
}

void i915_gem_init__pm(struct drm_i915_private *i915)
{
	INIT_DELAYED_WORK(&i915->gem.idle_work, idle_work_handler);
	INIT_DELAYED_WORK(&i915->gem.retire_work, retire_work_handler);
}
