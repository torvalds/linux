/*
 *
 * (C) COPYRIGHT 2010-2015 ARM Limited. All rights reserved.
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
 * Power policy API implementations
 */

#include <mali_kbase.h>
#include <mali_midg_regmap.h>
#include <mali_kbase_gator.h>
#include <mali_kbase_pm.h>
#include <mali_kbase_config_defaults.h>
#include <backend/gpu/mali_kbase_pm_internal.h>

static const struct kbase_pm_policy *const policy_list[] = {
#ifdef CONFIG_MALI_NO_MALI
	&kbase_pm_always_on_policy_ops,
	&kbase_pm_demand_policy_ops,
	&kbase_pm_coarse_demand_policy_ops,
#if !MALI_CUSTOMER_RELEASE
	&kbase_pm_demand_always_powered_policy_ops,
	&kbase_pm_fast_start_policy_ops,
#endif
#else				/* CONFIG_MALI_NO_MALI */
	&kbase_pm_demand_policy_ops,
	&kbase_pm_always_on_policy_ops,
	&kbase_pm_coarse_demand_policy_ops,
#if !MALI_CUSTOMER_RELEASE
	&kbase_pm_demand_always_powered_policy_ops,
	&kbase_pm_fast_start_policy_ops,
#endif
#endif /* CONFIG_MALI_NO_MALI */
};

/* The number of policies available in the system.
 * This is derived from the number of functions listed in policy_get_functions.
 */
#define POLICY_COUNT (sizeof(policy_list)/sizeof(*policy_list))


/* Function IDs for looking up Timeline Trace codes in
 * kbase_pm_change_state_trace_code */
enum kbase_pm_func_id {
	KBASE_PM_FUNC_ID_REQUEST_CORES_START,
	KBASE_PM_FUNC_ID_REQUEST_CORES_END,
	KBASE_PM_FUNC_ID_RELEASE_CORES_START,
	KBASE_PM_FUNC_ID_RELEASE_CORES_END,
	/* Note: kbase_pm_unrequest_cores() is on the slow path, and we neither
	 * expect to hit it nor tend to hit it very much anyway. We can detect
	 * whether we need more instrumentation by a difference between
	 * PM_CHECKTRANS events and PM_SEND/HANDLE_EVENT. */

	/* Must be the last */
	KBASE_PM_FUNC_ID_COUNT
};


/* State changes during request/unrequest/release-ing cores */
enum {
	KBASE_PM_CHANGE_STATE_SHADER = (1u << 0),
	KBASE_PM_CHANGE_STATE_TILER  = (1u << 1),

	/* These two must be last */
	KBASE_PM_CHANGE_STATE_MASK = (KBASE_PM_CHANGE_STATE_TILER |
						KBASE_PM_CHANGE_STATE_SHADER),
	KBASE_PM_CHANGE_STATE_COUNT = KBASE_PM_CHANGE_STATE_MASK + 1
};
typedef u32 kbase_pm_change_state;


#ifdef CONFIG_MALI_TRACE_TIMELINE
/* Timeline Trace code lookups for each function */
static u32 kbase_pm_change_state_trace_code[KBASE_PM_FUNC_ID_COUNT]
					[KBASE_PM_CHANGE_STATE_COUNT] = {
	/* kbase_pm_request_cores */
	[KBASE_PM_FUNC_ID_REQUEST_CORES_START][0] = 0,
	[KBASE_PM_FUNC_ID_REQUEST_CORES_START][KBASE_PM_CHANGE_STATE_SHADER] =
		SW_FLOW_PM_CHECKTRANS_PM_REQUEST_CORES_SHADER_START,
	[KBASE_PM_FUNC_ID_REQUEST_CORES_START][KBASE_PM_CHANGE_STATE_TILER] =
		SW_FLOW_PM_CHECKTRANS_PM_REQUEST_CORES_TILER_START,
	[KBASE_PM_FUNC_ID_REQUEST_CORES_START][KBASE_PM_CHANGE_STATE_SHADER |
						KBASE_PM_CHANGE_STATE_TILER] =
		SW_FLOW_PM_CHECKTRANS_PM_REQUEST_CORES_SHADER_TILER_START,

	[KBASE_PM_FUNC_ID_REQUEST_CORES_END][0] = 0,
	[KBASE_PM_FUNC_ID_REQUEST_CORES_END][KBASE_PM_CHANGE_STATE_SHADER] =
		SW_FLOW_PM_CHECKTRANS_PM_REQUEST_CORES_SHADER_END,
	[KBASE_PM_FUNC_ID_REQUEST_CORES_END][KBASE_PM_CHANGE_STATE_TILER] =
		SW_FLOW_PM_CHECKTRANS_PM_REQUEST_CORES_TILER_END,
	[KBASE_PM_FUNC_ID_REQUEST_CORES_END][KBASE_PM_CHANGE_STATE_SHADER |
						KBASE_PM_CHANGE_STATE_TILER] =
		SW_FLOW_PM_CHECKTRANS_PM_REQUEST_CORES_SHADER_TILER_END,

	/* kbase_pm_release_cores */
	[KBASE_PM_FUNC_ID_RELEASE_CORES_START][0] = 0,
	[KBASE_PM_FUNC_ID_RELEASE_CORES_START][KBASE_PM_CHANGE_STATE_SHADER] =
		SW_FLOW_PM_CHECKTRANS_PM_RELEASE_CORES_SHADER_START,
	[KBASE_PM_FUNC_ID_RELEASE_CORES_START][KBASE_PM_CHANGE_STATE_TILER] =
		SW_FLOW_PM_CHECKTRANS_PM_RELEASE_CORES_TILER_START,
	[KBASE_PM_FUNC_ID_RELEASE_CORES_START][KBASE_PM_CHANGE_STATE_SHADER |
						KBASE_PM_CHANGE_STATE_TILER] =
		SW_FLOW_PM_CHECKTRANS_PM_RELEASE_CORES_SHADER_TILER_START,

	[KBASE_PM_FUNC_ID_RELEASE_CORES_END][0] = 0,
	[KBASE_PM_FUNC_ID_RELEASE_CORES_END][KBASE_PM_CHANGE_STATE_SHADER] =
		SW_FLOW_PM_CHECKTRANS_PM_RELEASE_CORES_SHADER_END,
	[KBASE_PM_FUNC_ID_RELEASE_CORES_END][KBASE_PM_CHANGE_STATE_TILER] =
		SW_FLOW_PM_CHECKTRANS_PM_RELEASE_CORES_TILER_END,
	[KBASE_PM_FUNC_ID_RELEASE_CORES_END][KBASE_PM_CHANGE_STATE_SHADER |
						KBASE_PM_CHANGE_STATE_TILER] =
		SW_FLOW_PM_CHECKTRANS_PM_RELEASE_CORES_SHADER_TILER_END
};

static inline void kbase_timeline_pm_cores_func(struct kbase_device *kbdev,
		enum kbase_pm_func_id func_id,
		kbase_pm_change_state state)
{
	int trace_code;

	KBASE_DEBUG_ASSERT(func_id >= 0 && func_id < KBASE_PM_FUNC_ID_COUNT);
	KBASE_DEBUG_ASSERT(state != 0 && (state & KBASE_PM_CHANGE_STATE_MASK) ==
									state);

	trace_code = kbase_pm_change_state_trace_code[func_id][state];
	KBASE_TIMELINE_PM_CHECKTRANS(kbdev, trace_code);
}

#else /* CONFIG_MALI_TRACE_TIMELINE */
static inline void kbase_timeline_pm_cores_func(struct kbase_device *kbdev,
		enum kbase_pm_func_id func_id, kbase_pm_change_state state)
{
}

#endif /* CONFIG_MALI_TRACE_TIMELINE */

/**
 * kbasep_pm_do_poweroff_cores - Process a poweroff request and power down any
 *                               requested shader cores
 * @kbdev: Device pointer
 */
static void kbasep_pm_do_poweroff_cores(struct kbase_device *kbdev)
{
	u64 prev_shader_state = kbdev->pm.backend.desired_shader_state;

	lockdep_assert_held(&kbdev->pm.power_change_lock);

	kbdev->pm.backend.desired_shader_state &=
			~kbdev->pm.backend.shader_poweroff_pending;

	kbdev->pm.backend.shader_poweroff_pending = 0;

	if (prev_shader_state != kbdev->pm.backend.desired_shader_state
			|| kbdev->pm.backend.ca_in_transition) {
		bool cores_are_available;

		KBASE_TIMELINE_PM_CHECKTRANS(kbdev,
			SW_FLOW_PM_CHECKTRANS_PM_RELEASE_CORES_DEFERRED_START);
		cores_are_available = kbase_pm_check_transitions_nolock(kbdev);
		KBASE_TIMELINE_PM_CHECKTRANS(kbdev,
			SW_FLOW_PM_CHECKTRANS_PM_RELEASE_CORES_DEFERRED_END);

		/* Don't need 'cores_are_available',
		 * because we don't return anything */
		CSTD_UNUSED(cores_are_available);
	}
}

static enum hrtimer_restart
kbasep_pm_do_gpu_poweroff_callback(struct hrtimer *timer)
{
	struct kbase_device *kbdev;
	unsigned long flags;

	kbdev = container_of(timer, struct kbase_device,
						pm.backend.gpu_poweroff_timer);

	spin_lock_irqsave(&kbdev->pm.power_change_lock, flags);

	/* It is safe for this call to do nothing if the work item is already
	 * queued. The worker function will read the must up-to-date state of
	 * kbdev->pm.backend.gpu_poweroff_pending under lock.
	 *
	 * If a state change occurs while the worker function is processing,
	 * this call will succeed as a work item can be requeued once it has
	 * started processing.
	 */
	if (kbdev->pm.backend.gpu_poweroff_pending)
		queue_work(kbdev->pm.backend.gpu_poweroff_wq,
					&kbdev->pm.backend.gpu_poweroff_work);

	if (kbdev->pm.backend.shader_poweroff_pending) {
		kbdev->pm.backend.shader_poweroff_pending_time--;

		KBASE_DEBUG_ASSERT(
				kbdev->pm.backend.shader_poweroff_pending_time
									>= 0);

		if (!kbdev->pm.backend.shader_poweroff_pending_time)
			kbasep_pm_do_poweroff_cores(kbdev);
	}

	if (kbdev->pm.backend.poweroff_timer_needed) {
		spin_unlock_irqrestore(&kbdev->pm.power_change_lock, flags);

		hrtimer_add_expires(timer, kbdev->pm.gpu_poweroff_time);

		return HRTIMER_RESTART;
	}

	kbdev->pm.backend.poweroff_timer_running = false;
	spin_unlock_irqrestore(&kbdev->pm.power_change_lock, flags);

	return HRTIMER_NORESTART;
}

static void kbasep_pm_do_gpu_poweroff_wq(struct work_struct *data)
{
	unsigned long flags;
	struct kbase_device *kbdev;
	bool do_poweroff = false;

	kbdev = container_of(data, struct kbase_device,
						pm.backend.gpu_poweroff_work);

	mutex_lock(&kbdev->pm.lock);

	if (kbdev->pm.backend.gpu_poweroff_pending == 0) {
		mutex_unlock(&kbdev->pm.lock);
		return;
	}

	kbdev->pm.backend.gpu_poweroff_pending--;

	if (kbdev->pm.backend.gpu_poweroff_pending > 0) {
		mutex_unlock(&kbdev->pm.lock);
		return;
	}

	KBASE_DEBUG_ASSERT(kbdev->pm.backend.gpu_poweroff_pending == 0);

	spin_lock_irqsave(&kbdev->pm.power_change_lock, flags);

	/* Only power off the GPU if a request is still pending */
	if (!kbdev->pm.backend.pm_current_policy->get_core_active(kbdev))
		do_poweroff = true;

	spin_unlock_irqrestore(&kbdev->pm.power_change_lock, flags);

	if (do_poweroff) {
		kbdev->pm.backend.poweroff_timer_needed = false;
		hrtimer_cancel(&kbdev->pm.backend.gpu_poweroff_timer);
		kbdev->pm.backend.poweroff_timer_running = false;

		/* Power off the GPU */
		if (!kbase_pm_do_poweroff(kbdev, false)) {
			/* GPU can not be powered off at present */
			kbdev->pm.backend.poweroff_timer_needed = true;
			kbdev->pm.backend.poweroff_timer_running = true;
			hrtimer_start(&kbdev->pm.backend.gpu_poweroff_timer,
					kbdev->pm.gpu_poweroff_time,
					HRTIMER_MODE_REL);
		}
	}

	mutex_unlock(&kbdev->pm.lock);
}

int kbase_pm_policy_init(struct kbase_device *kbdev)
{
	struct workqueue_struct *wq;

	wq = alloc_workqueue("kbase_pm_do_poweroff",
			WQ_HIGHPRI | WQ_UNBOUND, 1);
	if (!wq)
		return -ENOMEM;

	kbdev->pm.backend.gpu_poweroff_wq = wq;
	INIT_WORK(&kbdev->pm.backend.gpu_poweroff_work,
			kbasep_pm_do_gpu_poweroff_wq);
	hrtimer_init(&kbdev->pm.backend.gpu_poweroff_timer,
			CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	kbdev->pm.backend.gpu_poweroff_timer.function =
			kbasep_pm_do_gpu_poweroff_callback;
	kbdev->pm.backend.pm_current_policy = policy_list[0];
	kbdev->pm.backend.pm_current_policy->init(kbdev);
	kbdev->pm.gpu_poweroff_time =
			HR_TIMER_DELAY_NSEC(DEFAULT_PM_GPU_POWEROFF_TICK_NS);
	kbdev->pm.poweroff_shader_ticks = DEFAULT_PM_POWEROFF_TICK_SHADER;
	kbdev->pm.poweroff_gpu_ticks = DEFAULT_PM_POWEROFF_TICK_GPU;

	return 0;
}

void kbase_pm_policy_term(struct kbase_device *kbdev)
{
	kbdev->pm.backend.pm_current_policy->term(kbdev);
	destroy_workqueue(kbdev->pm.backend.gpu_poweroff_wq);
}

void kbase_pm_cancel_deferred_poweroff(struct kbase_device *kbdev)
{
	unsigned long flags;

	lockdep_assert_held(&kbdev->pm.lock);

	kbdev->pm.backend.poweroff_timer_needed = false;
	hrtimer_cancel(&kbdev->pm.backend.gpu_poweroff_timer);
	spin_lock_irqsave(&kbdev->pm.power_change_lock, flags);
	kbdev->pm.backend.poweroff_timer_running = false;

	/* If wq is already running but is held off by pm.lock, make sure it has
	 * no effect */
	kbdev->pm.backend.gpu_poweroff_pending = 0;

	kbdev->pm.backend.shader_poweroff_pending = 0;
	kbdev->pm.backend.shader_poweroff_pending_time = 0;

	spin_unlock_irqrestore(&kbdev->pm.power_change_lock, flags);
}

void kbase_pm_update_active(struct kbase_device *kbdev)
{
	struct kbase_pm_device_data *pm = &kbdev->pm;
	struct kbase_pm_backend_data *backend = &pm->backend;
	unsigned long flags;
	bool active;

	lockdep_assert_held(&pm->lock);

	/* pm_current_policy will never be NULL while pm.lock is held */
	KBASE_DEBUG_ASSERT(backend->pm_current_policy);

	spin_lock_irqsave(&pm->power_change_lock, flags);

	active = backend->pm_current_policy->get_core_active(kbdev);

	if (active) {
		if (backend->gpu_poweroff_pending) {
			/* Cancel any pending power off request */
			backend->gpu_poweroff_pending = 0;

			/* If a request was pending then the GPU was still
			 * powered, so no need to continue */
			if (!kbdev->poweroff_pending) {
				spin_unlock_irqrestore(&pm->power_change_lock,
						flags);
				return;
			}
		}

		if (!backend->poweroff_timer_running && !backend->gpu_powered &&
				(pm->poweroff_gpu_ticks ||
				pm->poweroff_shader_ticks)) {
			backend->poweroff_timer_needed = true;
			backend->poweroff_timer_running = true;
			hrtimer_start(&backend->gpu_poweroff_timer,
					pm->gpu_poweroff_time,
					HRTIMER_MODE_REL);
		}

		spin_unlock_irqrestore(&pm->power_change_lock, flags);

		/* Power on the GPU and any cores requested by the policy */
		kbase_pm_do_poweron(kbdev, false);
	} else {
		/* It is an error for the power policy to power off the GPU
		 * when there are contexts active */
		KBASE_DEBUG_ASSERT(pm->active_count == 0);

		if (backend->shader_poweroff_pending) {
			backend->shader_poweroff_pending = 0;
			backend->shader_poweroff_pending_time = 0;
		}

		/* Request power off */
		if (pm->backend.gpu_powered) {
			if (pm->poweroff_gpu_ticks) {
				backend->gpu_poweroff_pending =
						pm->poweroff_gpu_ticks;
				backend->poweroff_timer_needed = true;
				if (!backend->poweroff_timer_running) {
					/* Start timer if not running (eg if
					 * power policy has been changed from
					 * always_on to something else). This
					 * will ensure the GPU is actually
					 * powered off */
					backend->poweroff_timer_running
							= true;
					hrtimer_start(
						&backend->gpu_poweroff_timer,
						pm->gpu_poweroff_time,
						HRTIMER_MODE_REL);
				}
				spin_unlock_irqrestore(&pm->power_change_lock,
						flags);
			} else {
				spin_unlock_irqrestore(&pm->power_change_lock,
						flags);

				/* Power off the GPU immediately */
				if (!kbase_pm_do_poweroff(kbdev, false)) {
					/* GPU can not be powered off at present
					 */
					spin_lock_irqsave(
							&pm->power_change_lock,
							flags);
					backend->poweroff_timer_needed = true;
					if (!backend->poweroff_timer_running) {
						backend->poweroff_timer_running
								= true;
						hrtimer_start(
						&backend->gpu_poweroff_timer,
							pm->gpu_poweroff_time,
							HRTIMER_MODE_REL);
					}
					spin_unlock_irqrestore(
							&pm->power_change_lock,
							flags);
				}
			}
		} else {
			spin_unlock_irqrestore(&pm->power_change_lock, flags);
		}
	}
}

void kbase_pm_update_cores_state_nolock(struct kbase_device *kbdev)
{
	u64 desired_bitmap;
	bool cores_are_available;
	bool do_poweroff = false;

	lockdep_assert_held(&kbdev->pm.power_change_lock);

	if (kbdev->pm.backend.pm_current_policy == NULL)
		return;

	desired_bitmap =
		kbdev->pm.backend.pm_current_policy->get_core_mask(kbdev);
	desired_bitmap &= kbase_pm_ca_get_core_mask(kbdev);

	/* Enable core 0 if tiler required, regardless of core availability */
	if (kbdev->tiler_needed_cnt > 0 || kbdev->tiler_inuse_cnt > 0)
		desired_bitmap |= 1;

	if (kbdev->pm.backend.desired_shader_state != desired_bitmap)
		KBASE_TRACE_ADD(kbdev, PM_CORES_CHANGE_DESIRED, NULL, NULL, 0u,
							(u32)desired_bitmap);
	/* Are any cores being powered on? */
	if (~kbdev->pm.backend.desired_shader_state & desired_bitmap ||
	    kbdev->pm.backend.ca_in_transition) {
		/* Check if we are powering off any cores before updating shader
		 * state */
		if (kbdev->pm.backend.desired_shader_state & ~desired_bitmap) {
			/* Start timer to power off cores */
			kbdev->pm.backend.shader_poweroff_pending |=
				(kbdev->pm.backend.desired_shader_state &
							~desired_bitmap);

			if (kbdev->pm.poweroff_shader_ticks)
				kbdev->pm.backend.shader_poweroff_pending_time =
						kbdev->pm.poweroff_shader_ticks;
			else
				do_poweroff = true;
		}

		kbdev->pm.backend.desired_shader_state = desired_bitmap;

		/* If any cores are being powered on, transition immediately */
		cores_are_available = kbase_pm_check_transitions_nolock(kbdev);
	} else if (kbdev->pm.backend.desired_shader_state & ~desired_bitmap) {
		/* Start timer to power off cores */
		kbdev->pm.backend.shader_poweroff_pending |=
				(kbdev->pm.backend.desired_shader_state &
							~desired_bitmap);
		if (kbdev->pm.poweroff_shader_ticks)
			kbdev->pm.backend.shader_poweroff_pending_time =
					kbdev->pm.poweroff_shader_ticks;
		else
			kbasep_pm_do_poweroff_cores(kbdev);
	} else if (kbdev->pm.active_count == 0 && desired_bitmap != 0 &&
				kbdev->pm.backend.poweroff_timer_needed) {
		/* If power policy is keeping cores on despite there being no
		 * active contexts then disable poweroff timer as it isn't
		 * required.
		 * Only reset poweroff_timer_needed if we're not in the middle
		 * of the power off callback */
		kbdev->pm.backend.poweroff_timer_needed = false;
	}

	/* Ensure timer does not power off wanted cores and make sure to power
	 * off unwanted cores */
	if (kbdev->pm.backend.shader_poweroff_pending != 0) {
		kbdev->pm.backend.shader_poweroff_pending &=
				~(kbdev->pm.backend.desired_shader_state &
								desired_bitmap);
		if (kbdev->pm.backend.shader_poweroff_pending == 0)
			kbdev->pm.backend.shader_poweroff_pending_time = 0;
	}

	/* Shader poweroff is deferred to the end of the function, to eliminate
	 * issues caused by the core availability policy recursing into this
	 * function */
	if (do_poweroff)
		kbasep_pm_do_poweroff_cores(kbdev);

	/* Don't need 'cores_are_available', because we don't return anything */
	CSTD_UNUSED(cores_are_available);
}

void kbase_pm_update_cores_state(struct kbase_device *kbdev)
{
	unsigned long flags;

	spin_lock_irqsave(&kbdev->pm.power_change_lock, flags);

	kbase_pm_update_cores_state_nolock(kbdev);

	spin_unlock_irqrestore(&kbdev->pm.power_change_lock, flags);
}

int kbase_pm_list_policies(const struct kbase_pm_policy * const **list)
{
	if (!list)
		return POLICY_COUNT;

	*list = policy_list;

	return POLICY_COUNT;
}

KBASE_EXPORT_TEST_API(kbase_pm_list_policies);

const struct kbase_pm_policy *kbase_pm_get_policy(struct kbase_device *kbdev)
{
	KBASE_DEBUG_ASSERT(kbdev != NULL);

	return kbdev->pm.backend.pm_current_policy;
}

KBASE_EXPORT_TEST_API(kbase_pm_get_policy);

void kbase_pm_set_policy(struct kbase_device *kbdev,
				const struct kbase_pm_policy *new_policy)
{
	struct kbasep_js_device_data *js_devdata = &kbdev->js_data;
	const struct kbase_pm_policy *old_policy;
	unsigned long flags;

	KBASE_DEBUG_ASSERT(kbdev != NULL);
	KBASE_DEBUG_ASSERT(new_policy != NULL);

	KBASE_TRACE_ADD(kbdev, PM_SET_POLICY, NULL, NULL, 0u, new_policy->id);

	/* During a policy change we pretend the GPU is active */
	/* A suspend won't happen here, because we're in a syscall from a
	 * userspace thread */
	kbase_pm_context_active(kbdev);

	mutex_lock(&js_devdata->runpool_mutex);
	mutex_lock(&kbdev->pm.lock);

	/* Remove the policy to prevent IRQ handlers from working on it */
	spin_lock_irqsave(&kbdev->pm.power_change_lock, flags);
	old_policy = kbdev->pm.backend.pm_current_policy;
	kbdev->pm.backend.pm_current_policy = NULL;
	spin_unlock_irqrestore(&kbdev->pm.power_change_lock, flags);

	KBASE_TRACE_ADD(kbdev, PM_CURRENT_POLICY_TERM, NULL, NULL, 0u,
								old_policy->id);
	if (old_policy->term)
		old_policy->term(kbdev);

	KBASE_TRACE_ADD(kbdev, PM_CURRENT_POLICY_INIT, NULL, NULL, 0u,
								new_policy->id);
	if (new_policy->init)
		new_policy->init(kbdev);

	spin_lock_irqsave(&kbdev->pm.power_change_lock, flags);
	kbdev->pm.backend.pm_current_policy = new_policy;
	spin_unlock_irqrestore(&kbdev->pm.power_change_lock, flags);

	/* If any core power state changes were previously attempted, but
	 * couldn't be made because the policy was changing (current_policy was
	 * NULL), then re-try them here. */
	kbase_pm_update_active(kbdev);
	kbase_pm_update_cores_state(kbdev);

	mutex_unlock(&kbdev->pm.lock);
	mutex_unlock(&js_devdata->runpool_mutex);

	/* Now the policy change is finished, we release our fake context active
	 * reference */
	kbase_pm_context_idle(kbdev);
}

KBASE_EXPORT_TEST_API(kbase_pm_set_policy);

/* Check whether a state change has finished, and trace it as completed */
static void
kbase_pm_trace_check_and_finish_state_change(struct kbase_device *kbdev)
{
	if ((kbdev->shader_available_bitmap &
					kbdev->pm.backend.desired_shader_state)
				== kbdev->pm.backend.desired_shader_state &&
		(kbdev->tiler_available_bitmap &
					kbdev->pm.backend.desired_tiler_state)
				== kbdev->pm.backend.desired_tiler_state)
		kbase_timeline_pm_check_handle_event(kbdev,
				KBASE_TIMELINE_PM_EVENT_GPU_STATE_CHANGED);
}

void kbase_pm_request_cores(struct kbase_device *kbdev,
				bool tiler_required, u64 shader_cores)
{
	unsigned long flags;
	u64 cores;

	kbase_pm_change_state change_gpu_state = 0u;

	KBASE_DEBUG_ASSERT(kbdev != NULL);

	spin_lock_irqsave(&kbdev->pm.power_change_lock, flags);

	cores = shader_cores;
	while (cores) {
		int bitnum = fls64(cores) - 1;
		u64 bit = 1ULL << bitnum;

		/* It should be almost impossible for this to overflow. It would
		 * require 2^32 atoms to request a particular core, which would
		 * require 2^24 contexts to submit. This would require an amount
		 * of memory that is impossible on a 32-bit system and extremely
		 * unlikely on a 64-bit system. */
		int cnt = ++kbdev->shader_needed_cnt[bitnum];

		if (1 == cnt) {
			kbdev->shader_needed_bitmap |= bit;
			change_gpu_state |= KBASE_PM_CHANGE_STATE_SHADER;
		}

		cores &= ~bit;
	}

	if (tiler_required) {
		int cnt = ++kbdev->tiler_needed_cnt;

		if (1 == cnt)
			change_gpu_state |= KBASE_PM_CHANGE_STATE_TILER;

		KBASE_DEBUG_ASSERT(kbdev->tiler_needed_cnt != 0);
	}

	if (change_gpu_state) {
		KBASE_TRACE_ADD(kbdev, PM_REQUEST_CHANGE_SHADER_NEEDED, NULL,
				NULL, 0u, (u32) kbdev->shader_needed_bitmap);

		kbase_timeline_pm_cores_func(kbdev,
					KBASE_PM_FUNC_ID_REQUEST_CORES_START,
							change_gpu_state);
		kbase_pm_update_cores_state_nolock(kbdev);
		kbase_timeline_pm_cores_func(kbdev,
					KBASE_PM_FUNC_ID_REQUEST_CORES_END,
							change_gpu_state);
	}

	spin_unlock_irqrestore(&kbdev->pm.power_change_lock, flags);
}

KBASE_EXPORT_TEST_API(kbase_pm_request_cores);

void kbase_pm_unrequest_cores(struct kbase_device *kbdev,
				bool tiler_required, u64 shader_cores)
{
	unsigned long flags;

	kbase_pm_change_state change_gpu_state = 0u;

	KBASE_DEBUG_ASSERT(kbdev != NULL);

	spin_lock_irqsave(&kbdev->pm.power_change_lock, flags);

	while (shader_cores) {
		int bitnum = fls64(shader_cores) - 1;
		u64 bit = 1ULL << bitnum;
		int cnt;

		KBASE_DEBUG_ASSERT(kbdev->shader_needed_cnt[bitnum] > 0);

		cnt = --kbdev->shader_needed_cnt[bitnum];

		if (0 == cnt) {
			kbdev->shader_needed_bitmap &= ~bit;

			change_gpu_state |= KBASE_PM_CHANGE_STATE_SHADER;
		}

		shader_cores &= ~bit;
	}

	if (tiler_required) {
		int cnt;

		KBASE_DEBUG_ASSERT(kbdev->tiler_needed_cnt > 0);

		cnt = --kbdev->tiler_needed_cnt;

		if (0 == cnt)
			change_gpu_state |= KBASE_PM_CHANGE_STATE_TILER;
	}

	if (change_gpu_state) {
		KBASE_TRACE_ADD(kbdev, PM_UNREQUEST_CHANGE_SHADER_NEEDED, NULL,
				NULL, 0u, (u32) kbdev->shader_needed_bitmap);

		kbase_pm_update_cores_state_nolock(kbdev);

		/* Trace that any state change effectively completes immediately
		 * - no-one will wait on the state change */
		kbase_pm_trace_check_and_finish_state_change(kbdev);
	}

	spin_unlock_irqrestore(&kbdev->pm.power_change_lock, flags);
}

KBASE_EXPORT_TEST_API(kbase_pm_unrequest_cores);

enum kbase_pm_cores_ready
kbase_pm_register_inuse_cores(struct kbase_device *kbdev,
				bool tiler_required, u64 shader_cores)
{
	unsigned long flags;
	u64 prev_shader_needed;	/* Just for tracing */
	u64 prev_shader_inuse;	/* Just for tracing */

	spin_lock_irqsave(&kbdev->pm.power_change_lock, flags);

	prev_shader_needed = kbdev->shader_needed_bitmap;
	prev_shader_inuse = kbdev->shader_inuse_bitmap;

	/* If desired_shader_state does not contain the requested cores, then
	 * power management is not attempting to powering those cores (most
	 * likely due to core availability policy) and a new job affinity must
	 * be chosen */
	if ((kbdev->pm.backend.desired_shader_state & shader_cores) !=
							shader_cores) {
		spin_unlock_irqrestore(&kbdev->pm.power_change_lock, flags);

		return KBASE_NEW_AFFINITY;
	}

	if ((kbdev->shader_available_bitmap & shader_cores) != shader_cores ||
	    (tiler_required && !kbdev->tiler_available_bitmap)) {
		/* Trace ongoing core transition */
		kbase_timeline_pm_l2_transition_start(kbdev);
		spin_unlock_irqrestore(&kbdev->pm.power_change_lock, flags);
		return KBASE_CORES_NOT_READY;
	}

	/* If we started to trace a state change, then trace it has being
	 * finished by now, at the very latest */
	kbase_pm_trace_check_and_finish_state_change(kbdev);
	/* Trace core transition done */
	kbase_timeline_pm_l2_transition_done(kbdev);

	while (shader_cores) {
		int bitnum = fls64(shader_cores) - 1;
		u64 bit = 1ULL << bitnum;
		int cnt;

		KBASE_DEBUG_ASSERT(kbdev->shader_needed_cnt[bitnum] > 0);

		cnt = --kbdev->shader_needed_cnt[bitnum];

		if (0 == cnt)
			kbdev->shader_needed_bitmap &= ~bit;

		/* shader_inuse_cnt should not overflow because there can only
		 * be a very limited number of jobs on the h/w at one time */

		kbdev->shader_inuse_cnt[bitnum]++;
		kbdev->shader_inuse_bitmap |= bit;

		shader_cores &= ~bit;
	}

	if (tiler_required) {
		KBASE_DEBUG_ASSERT(kbdev->tiler_needed_cnt > 0);

		--kbdev->tiler_needed_cnt;

		kbdev->tiler_inuse_cnt++;

		KBASE_DEBUG_ASSERT(kbdev->tiler_inuse_cnt != 0);
	}

	if (prev_shader_needed != kbdev->shader_needed_bitmap)
		KBASE_TRACE_ADD(kbdev, PM_REGISTER_CHANGE_SHADER_NEEDED, NULL,
				NULL, 0u, (u32) kbdev->shader_needed_bitmap);

	if (prev_shader_inuse != kbdev->shader_inuse_bitmap)
		KBASE_TRACE_ADD(kbdev, PM_REGISTER_CHANGE_SHADER_INUSE, NULL,
				NULL, 0u, (u32) kbdev->shader_inuse_bitmap);

	spin_unlock_irqrestore(&kbdev->pm.power_change_lock, flags);

	return KBASE_CORES_READY;
}

KBASE_EXPORT_TEST_API(kbase_pm_register_inuse_cores);

void kbase_pm_release_cores(struct kbase_device *kbdev,
				bool tiler_required, u64 shader_cores)
{
	unsigned long flags;
	kbase_pm_change_state change_gpu_state = 0u;

	KBASE_DEBUG_ASSERT(kbdev != NULL);

	spin_lock_irqsave(&kbdev->pm.power_change_lock, flags);

	while (shader_cores) {
		int bitnum = fls64(shader_cores) - 1;
		u64 bit = 1ULL << bitnum;
		int cnt;

		KBASE_DEBUG_ASSERT(kbdev->shader_inuse_cnt[bitnum] > 0);

		cnt = --kbdev->shader_inuse_cnt[bitnum];

		if (0 == cnt) {
			kbdev->shader_inuse_bitmap &= ~bit;
			change_gpu_state |= KBASE_PM_CHANGE_STATE_SHADER;
		}

		shader_cores &= ~bit;
	}

	if (tiler_required) {
		int cnt;

		KBASE_DEBUG_ASSERT(kbdev->tiler_inuse_cnt > 0);

		cnt = --kbdev->tiler_inuse_cnt;

		if (0 == cnt)
			change_gpu_state |= KBASE_PM_CHANGE_STATE_TILER;
	}

	if (change_gpu_state) {
		KBASE_TRACE_ADD(kbdev, PM_RELEASE_CHANGE_SHADER_INUSE, NULL,
				NULL, 0u, (u32) kbdev->shader_inuse_bitmap);

		kbase_timeline_pm_cores_func(kbdev,
					KBASE_PM_FUNC_ID_RELEASE_CORES_START,
							change_gpu_state);
		kbase_pm_update_cores_state_nolock(kbdev);
		kbase_timeline_pm_cores_func(kbdev,
					KBASE_PM_FUNC_ID_RELEASE_CORES_END,
							change_gpu_state);

		/* Trace that any state change completed immediately */
		kbase_pm_trace_check_and_finish_state_change(kbdev);
	}

	spin_unlock_irqrestore(&kbdev->pm.power_change_lock, flags);
}

KBASE_EXPORT_TEST_API(kbase_pm_release_cores);

void kbase_pm_request_cores_sync(struct kbase_device *kbdev,
					bool tiler_required,
					u64 shader_cores)
{
	kbase_pm_request_cores(kbdev, tiler_required, shader_cores);

	kbase_pm_check_transitions_sync(kbdev);
}

KBASE_EXPORT_TEST_API(kbase_pm_request_cores_sync);

void kbase_pm_request_l2_caches(struct kbase_device *kbdev)
{
	unsigned long flags;
	u32 prior_l2_users_count;

	spin_lock_irqsave(&kbdev->pm.power_change_lock, flags);

	prior_l2_users_count = kbdev->l2_users_count++;

	KBASE_DEBUG_ASSERT(kbdev->l2_users_count != 0);

	/* if the GPU is reset while the l2 is on, l2 will be off but
	 * prior_l2_users_count will be > 0. l2_available_bitmap will have been
	 * set to 0 though by kbase_pm_init_hw */
	if (!prior_l2_users_count || !kbdev->l2_available_bitmap)
		kbase_pm_check_transitions_nolock(kbdev);

	spin_unlock_irqrestore(&kbdev->pm.power_change_lock, flags);
	wait_event(kbdev->pm.backend.l2_powered_wait,
					kbdev->pm.backend.l2_powered == 1);

	/* Trace that any state change completed immediately */
	kbase_pm_trace_check_and_finish_state_change(kbdev);
}

KBASE_EXPORT_TEST_API(kbase_pm_request_l2_caches);

void kbase_pm_request_l2_caches_l2_is_on(struct kbase_device *kbdev)
{
	unsigned long flags;

	spin_lock_irqsave(&kbdev->pm.power_change_lock, flags);

	kbdev->l2_users_count++;

	spin_unlock_irqrestore(&kbdev->pm.power_change_lock, flags);
}

KBASE_EXPORT_TEST_API(kbase_pm_request_l2_caches_l2_is_on);

void kbase_pm_release_l2_caches(struct kbase_device *kbdev)
{
	unsigned long flags;

	spin_lock_irqsave(&kbdev->pm.power_change_lock, flags);

	KBASE_DEBUG_ASSERT(kbdev->l2_users_count > 0);

	--kbdev->l2_users_count;

	if (!kbdev->l2_users_count) {
		kbase_pm_check_transitions_nolock(kbdev);
		/* Trace that any state change completed immediately */
		kbase_pm_trace_check_and_finish_state_change(kbdev);
	}

	spin_unlock_irqrestore(&kbdev->pm.power_change_lock, flags);
}

KBASE_EXPORT_TEST_API(kbase_pm_release_l2_caches);
