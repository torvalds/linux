/*
 *
 * (C) COPYRIGHT 2014-2016 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained
 * from Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 */




/*
 * Register-based HW access backend specific job scheduler APIs
 */

#include <mali_kbase.h>
#include <mali_kbase_hwaccess_jm.h>
#include <backend/gpu/mali_kbase_jm_internal.h>
#include <backend/gpu/mali_kbase_js_internal.h>

/*
 * Define for when dumping is enabled.
 * This should not be based on the instrumentation level as whether dumping is
 * enabled for a particular level is down to the integrator. However this is
 * being used for now as otherwise the cinstr headers would be needed.
 */
#define CINSTR_DUMPING_ENABLED (2 == MALI_INSTRUMENTATION_LEVEL)

/*
 * Hold the runpool_mutex for this
 */
static inline bool timer_callback_should_run(struct kbase_device *kbdev)
{
	struct kbase_backend_data *backend = &kbdev->hwaccess.backend;
	s8 nr_running_ctxs;

	lockdep_assert_held(&kbdev->js_data.runpool_mutex);

	/* Timer must stop if we are suspending */
	if (backend->suspend_timer)
		return false;

	/* nr_contexts_pullable is updated with the runpool_mutex. However, the
	 * locking in the caller gives us a barrier that ensures
	 * nr_contexts_pullable is up-to-date for reading */
	nr_running_ctxs = atomic_read(&kbdev->js_data.nr_contexts_runnable);

#ifdef CONFIG_MALI_DEBUG
	if (kbdev->js_data.softstop_always) {
		/* Debug support for allowing soft-stop on a single context */
		return true;
	}
#endif				/* CONFIG_MALI_DEBUG */

	if (kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_9435)) {
		/* Timeouts would have to be 4x longer (due to micro-
		 * architectural design) to support OpenCL conformance tests, so
		 * only run the timer when there's:
		 * - 2 or more CL contexts
		 * - 1 or more GLES contexts
		 *
		 * NOTE: We will treat a context that has both Compute and Non-
		 * Compute jobs will be treated as an OpenCL context (hence, we
		 * don't check KBASEP_JS_CTX_ATTR_NON_COMPUTE).
		 */
		{
			s8 nr_compute_ctxs =
				kbasep_js_ctx_attr_count_on_runpool(kbdev,
						KBASEP_JS_CTX_ATTR_COMPUTE);
			s8 nr_noncompute_ctxs = nr_running_ctxs -
							nr_compute_ctxs;

			return (bool) (nr_compute_ctxs >= 2 ||
							nr_noncompute_ctxs > 0);
		}
	} else {
		/* Run the timer callback whenever you have at least 1 context
		 */
		return (bool) (nr_running_ctxs > 0);
	}
}

static enum hrtimer_restart timer_callback(struct hrtimer *timer)
{
	unsigned long flags;
	struct kbase_device *kbdev;
	struct kbasep_js_device_data *js_devdata;
	struct kbase_backend_data *backend;
	int s;
	bool reset_needed = false;

	KBASE_DEBUG_ASSERT(timer != NULL);

	backend = container_of(timer, struct kbase_backend_data,
							scheduling_timer);
	kbdev = container_of(backend, struct kbase_device, hwaccess.backend);
	js_devdata = &kbdev->js_data;

	/* Loop through the slots */
	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	for (s = 0; s < kbdev->gpu_props.num_job_slots; s++) {
		struct kbase_jd_atom *atom = NULL;

		if (kbase_backend_nr_atoms_on_slot(kbdev, s) > 0) {
			atom = kbase_gpu_inspect(kbdev, s, 0);
			KBASE_DEBUG_ASSERT(atom != NULL);
		}

		if (atom != NULL) {
			/* The current version of the model doesn't support
			 * Soft-Stop */
			if (!kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_5736)) {
				u32 ticks = atom->sched_info.cfs.ticks++;

#if !CINSTR_DUMPING_ENABLED
				u32 soft_stop_ticks, hard_stop_ticks,
								gpu_reset_ticks;
				if (atom->core_req & BASE_JD_REQ_ONLY_COMPUTE) {
					soft_stop_ticks =
						js_devdata->soft_stop_ticks_cl;
					hard_stop_ticks =
						js_devdata->hard_stop_ticks_cl;
					gpu_reset_ticks =
						js_devdata->gpu_reset_ticks_cl;
				} else {
					soft_stop_ticks =
						js_devdata->soft_stop_ticks;
					hard_stop_ticks =
						js_devdata->hard_stop_ticks_ss;
					gpu_reset_ticks =
						js_devdata->gpu_reset_ticks_ss;
				}

				/* If timeouts have been changed then ensure
				 * that atom tick count is not greater than the
				 * new soft_stop timeout. This ensures that
				 * atoms do not miss any of the timeouts due to
				 * races between this worker and the thread
				 * changing the timeouts. */
				if (backend->timeouts_updated &&
						ticks > soft_stop_ticks)
					ticks = atom->sched_info.cfs.ticks =
							soft_stop_ticks;

				/* Job is Soft-Stoppable */
				if (ticks == soft_stop_ticks) {
					int disjoint_threshold =
		KBASE_DISJOINT_STATE_INTERLEAVED_CONTEXT_COUNT_THRESHOLD;
					u32 softstop_flags = 0u;
					/* Job has been scheduled for at least
					 * js_devdata->soft_stop_ticks ticks.
					 * Soft stop the slot so we can run
					 * other jobs.
					 */
					dev_dbg(kbdev->dev, "Soft-stop");
#if !KBASE_DISABLE_SCHEDULING_SOFT_STOPS
					/* nr_user_contexts_running is updated
					 * with the runpool_mutex, but we can't
					 * take that here.
					 *
					 * However, if it's about to be
					 * increased then the new context can't
					 * run any jobs until they take the
					 * hwaccess_lock, so it's OK to observe
					 * the older value.
					 *
					 * Similarly, if it's about to be
					 * decreased, the last job from another
					 * context has already finished, so it's
					 * not too bad that we observe the older
					 * value and register a disjoint event
					 * when we try soft-stopping */
					if (js_devdata->nr_user_contexts_running
							>= disjoint_threshold)
						softstop_flags |=
						JS_COMMAND_SW_CAUSES_DISJOINT;

					kbase_job_slot_softstop_swflags(kbdev,
						s, atom, softstop_flags);
#endif
				} else if (ticks == hard_stop_ticks) {
					/* Job has been scheduled for at least
					 * js_devdata->hard_stop_ticks_ss ticks.
					 * It should have been soft-stopped by
					 * now. Hard stop the slot.
					 */
#if !KBASE_DISABLE_SCHEDULING_HARD_STOPS
					int ms =
						js_devdata->scheduling_period_ns
								/ 1000000u;
					dev_warn(kbdev->dev, "JS: Job Hard-Stopped (took more than %lu ticks at %lu ms/tick)",
							(unsigned long)ticks,
							(unsigned long)ms);
					kbase_job_slot_hardstop(atom->kctx, s,
									atom);
#endif
				} else if (ticks == gpu_reset_ticks) {
					/* Job has been scheduled for at least
					 * js_devdata->gpu_reset_ticks_ss ticks.
					 * It should have left the GPU by now.
					 * Signal that the GPU needs to be
					 * reset.
					 */
					reset_needed = true;
				}
#else				/* !CINSTR_DUMPING_ENABLED */
				/* NOTE: During CINSTR_DUMPING_ENABLED, we use
				 * the alternate timeouts, which makes the hard-
				 * stop and GPU reset timeout much longer. We
				 * also ensure that we don't soft-stop at all.
				 */
				if (ticks == js_devdata->soft_stop_ticks) {
					/* Job has been scheduled for at least
					 * js_devdata->soft_stop_ticks. We do
					 * not soft-stop during
					 * CINSTR_DUMPING_ENABLED, however.
					 */
					dev_dbg(kbdev->dev, "Soft-stop");
				} else if (ticks ==
					js_devdata->hard_stop_ticks_dumping) {
					/* Job has been scheduled for at least
					 * js_devdata->hard_stop_ticks_dumping
					 * ticks. Hard stop the slot.
					 */
#if !KBASE_DISABLE_SCHEDULING_HARD_STOPS
					int ms =
						js_devdata->scheduling_period_ns
								/ 1000000u;
					dev_warn(kbdev->dev, "JS: Job Hard-Stopped (took more than %lu ticks at %lu ms/tick)",
							(unsigned long)ticks,
							(unsigned long)ms);
					kbase_job_slot_hardstop(atom->kctx, s,
									atom);
#endif
				} else if (ticks ==
					js_devdata->gpu_reset_ticks_dumping) {
					/* Job has been scheduled for at least
					 * js_devdata->gpu_reset_ticks_dumping
					 * ticks. It should have left the GPU by
					 * now. Signal that the GPU needs to be
					 * reset.
					 */
					reset_needed = true;
				}
#endif				/* !CINSTR_DUMPING_ENABLED */
			}
		}
	}
#if KBASE_GPU_RESET_EN
	if (reset_needed) {
		dev_err(kbdev->dev, "JS: Job has been on the GPU for too long (JS_RESET_TICKS_SS/DUMPING timeout hit). Issueing GPU soft-reset to resolve.");

		if (kbase_prepare_to_reset_gpu_locked(kbdev))
			kbase_reset_gpu_locked(kbdev);
	}
#endif /* KBASE_GPU_RESET_EN */
	/* the timer is re-issued if there is contexts in the run-pool */

	if (backend->timer_running)
		hrtimer_start(&backend->scheduling_timer,
			HR_TIMER_DELAY_NSEC(js_devdata->scheduling_period_ns),
			HRTIMER_MODE_REL);

	backend->timeouts_updated = false;

	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	return HRTIMER_NORESTART;
}

void kbase_backend_ctx_count_changed(struct kbase_device *kbdev)
{
	struct kbasep_js_device_data *js_devdata = &kbdev->js_data;
	struct kbase_backend_data *backend = &kbdev->hwaccess.backend;
	unsigned long flags;

	lockdep_assert_held(&js_devdata->runpool_mutex);

	if (!timer_callback_should_run(kbdev)) {
		/* Take spinlock to force synchronisation with timer */
		spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
		backend->timer_running = false;
		spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
		/* From now on, return value of timer_callback_should_run() will
		 * also cause the timer to not requeue itself. Its return value
		 * cannot change, because it depends on variables updated with
		 * the runpool_mutex held, which the caller of this must also
		 * hold */
		hrtimer_cancel(&backend->scheduling_timer);
	}

	if (timer_callback_should_run(kbdev) && !backend->timer_running) {
		/* Take spinlock to force synchronisation with timer */
		spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
		backend->timer_running = true;
		spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
		hrtimer_start(&backend->scheduling_timer,
			HR_TIMER_DELAY_NSEC(js_devdata->scheduling_period_ns),
							HRTIMER_MODE_REL);

		KBASE_TRACE_ADD(kbdev, JS_POLICY_TIMER_START, NULL, NULL, 0u,
									0u);
	}
}

int kbase_backend_timer_init(struct kbase_device *kbdev)
{
	struct kbase_backend_data *backend = &kbdev->hwaccess.backend;

	hrtimer_init(&backend->scheduling_timer, CLOCK_MONOTONIC,
							HRTIMER_MODE_REL);
	backend->scheduling_timer.function = timer_callback;

	backend->timer_running = false;

	return 0;
}

void kbase_backend_timer_term(struct kbase_device *kbdev)
{
	struct kbase_backend_data *backend = &kbdev->hwaccess.backend;

	hrtimer_cancel(&backend->scheduling_timer);
}

void kbase_backend_timer_suspend(struct kbase_device *kbdev)
{
	struct kbase_backend_data *backend = &kbdev->hwaccess.backend;

	backend->suspend_timer = true;

	kbase_backend_ctx_count_changed(kbdev);
}

void kbase_backend_timer_resume(struct kbase_device *kbdev)
{
	struct kbase_backend_data *backend = &kbdev->hwaccess.backend;

	backend->suspend_timer = false;

	kbase_backend_ctx_count_changed(kbdev);
}

void kbase_backend_timeouts_changed(struct kbase_device *kbdev)
{
	struct kbase_backend_data *backend = &kbdev->hwaccess.backend;

	backend->timeouts_updated = true;
}

