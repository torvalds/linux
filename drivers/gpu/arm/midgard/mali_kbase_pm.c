/*
 *
 * (C) COPYRIGHT ARM Limited. All rights reserved.
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





/**
 * @file mali_kbase_pm.c
 * Base kernel power management APIs
 */

#include <mali_kbase.h>
#include <mali_midg_regmap.h>

#include <mali_kbase_pm.h>

#if KBASE_PM_EN

void kbase_pm_register_access_enable(struct kbase_device *kbdev)
{
	struct kbase_pm_callback_conf *callbacks;

	callbacks = (struct kbase_pm_callback_conf *)kbasep_get_config_value(kbdev, kbdev->config_attributes, KBASE_CONFIG_ATTR_POWER_MANAGEMENT_CALLBACKS);

	if (callbacks)
		callbacks->power_on_callback(kbdev);
}

void kbase_pm_register_access_disable(struct kbase_device *kbdev)
{
	struct kbase_pm_callback_conf *callbacks;

	callbacks = (struct kbase_pm_callback_conf *)kbasep_get_config_value(kbdev, kbdev->config_attributes, KBASE_CONFIG_ATTR_POWER_MANAGEMENT_CALLBACKS);

	if (callbacks)
		callbacks->power_off_callback(kbdev);
}

mali_error kbase_pm_init(struct kbase_device *kbdev)
{
	mali_error ret = MALI_ERROR_NONE;
	struct kbase_pm_callback_conf *callbacks;

	KBASE_DEBUG_ASSERT(kbdev != NULL);

	mutex_init(&kbdev->pm.lock);

	kbdev->pm.gpu_powered = MALI_FALSE;
	kbdev->pm.suspending = MALI_FALSE;
#ifdef CONFIG_MALI_DEBUG
	kbdev->pm.driver_ready_for_irqs = MALI_FALSE;
#endif /* CONFIG_MALI_DEBUG */
	kbdev->pm.gpu_in_desired_state = MALI_TRUE;
	init_waitqueue_head(&kbdev->pm.gpu_in_desired_state_wait);

	callbacks = (struct kbase_pm_callback_conf *)kbasep_get_config_value(kbdev, kbdev->config_attributes, KBASE_CONFIG_ATTR_POWER_MANAGEMENT_CALLBACKS);
	if (callbacks) {
		kbdev->pm.callback_power_on = callbacks->power_on_callback;
		kbdev->pm.callback_power_off = callbacks->power_off_callback;
		kbdev->pm.callback_power_suspend =
					callbacks->power_suspend_callback;
		kbdev->pm.callback_power_resume =
					callbacks->power_resume_callback;
		kbdev->pm.callback_power_runtime_init = callbacks->power_runtime_init_callback;
		kbdev->pm.callback_power_runtime_term = callbacks->power_runtime_term_callback;
		kbdev->pm.callback_power_runtime_on = callbacks->power_runtime_on_callback;
		kbdev->pm.callback_power_runtime_off = callbacks->power_runtime_off_callback;
	} else {
		kbdev->pm.callback_power_on = NULL;
		kbdev->pm.callback_power_off = NULL;
		kbdev->pm.callback_power_suspend = NULL;
		kbdev->pm.callback_power_resume = NULL;
		kbdev->pm.callback_power_runtime_init = NULL;
		kbdev->pm.callback_power_runtime_term = NULL;
		kbdev->pm.callback_power_runtime_on = NULL;
		kbdev->pm.callback_power_runtime_off = NULL;
	}

	kbdev->pm.platform_dvfs_frequency = (u32) kbasep_get_config_value(kbdev, kbdev->config_attributes, KBASE_CONFIG_ATTR_POWER_MANAGEMENT_DVFS_FREQ);

	/* Initialise the metrics subsystem */
	ret = kbasep_pm_metrics_init(kbdev);
	if (MALI_ERROR_NONE != ret)
		return ret;

	init_waitqueue_head(&kbdev->pm.l2_powered_wait);
	kbdev->pm.l2_powered = 0;

	init_waitqueue_head(&kbdev->pm.reset_done_wait);
	kbdev->pm.reset_done = MALI_FALSE;

	init_waitqueue_head(&kbdev->pm.zero_active_count_wait);
	kbdev->pm.active_count = 0;

	spin_lock_init(&kbdev->pm.power_change_lock);
	spin_lock_init(&kbdev->pm.gpu_cycle_counter_requests_lock);
	spin_lock_init(&kbdev->pm.gpu_powered_lock);

	if (MALI_ERROR_NONE != kbase_pm_ca_init(kbdev))
		goto workq_fail;

	if (MALI_ERROR_NONE != kbase_pm_policy_init(kbdev))
		goto pm_policy_fail;

	return MALI_ERROR_NONE;

pm_policy_fail:
	kbase_pm_ca_term(kbdev);
workq_fail:
	kbasep_pm_metrics_term(kbdev);
	return MALI_ERROR_FUNCTION_FAILED;
}

KBASE_EXPORT_TEST_API(kbase_pm_init)

void kbase_pm_do_poweron(struct kbase_device *kbdev, mali_bool is_resume)
{
	lockdep_assert_held(&kbdev->pm.lock);

	/* Turn clocks and interrupts on - no-op if we haven't done a previous
	 * kbase_pm_clock_off() */
	kbase_pm_clock_on(kbdev, is_resume);

	/* Update core status as required by the policy */
	KBASE_TIMELINE_PM_CHECKTRANS(kbdev, SW_FLOW_PM_CHECKTRANS_PM_DO_POWERON_START);
	kbase_pm_update_cores_state(kbdev);
	KBASE_TIMELINE_PM_CHECKTRANS(kbdev, SW_FLOW_PM_CHECKTRANS_PM_DO_POWERON_END);

	/* NOTE: We don't wait to reach the desired state, since running atoms
	 * will wait for that state to be reached anyway */
}

void kbase_pm_do_poweroff(struct kbase_device *kbdev, mali_bool is_suspend)
{
	unsigned long flags;
	mali_bool cores_are_available;

	lockdep_assert_held(&kbdev->pm.lock);

	spin_lock_irqsave(&kbdev->pm.power_change_lock, flags);

	/* Force all cores off */
	kbdev->pm.desired_shader_state = 0;

	/* Force all cores to be unavailable, in the situation where
	 * transitions are in progress for some cores but not others,
	 * and kbase_pm_check_transitions_nolock can not immediately
	 * power off the cores */
	kbdev->shader_available_bitmap = 0;
	kbdev->tiler_available_bitmap = 0;
	kbdev->l2_available_bitmap = 0;

	KBASE_TIMELINE_PM_CHECKTRANS(kbdev, SW_FLOW_PM_CHECKTRANS_PM_DO_POWEROFF_START);
	cores_are_available = kbase_pm_check_transitions_nolock(kbdev);
	KBASE_TIMELINE_PM_CHECKTRANS(kbdev, SW_FLOW_PM_CHECKTRANS_PM_DO_POWEROFF_END);
	/* Don't need 'cores_are_available', because we don't return anything */
	CSTD_UNUSED(cores_are_available);

	spin_unlock_irqrestore(&kbdev->pm.power_change_lock, flags);

	/* NOTE: We won't wait to reach the core's desired state, even if we're
	 * powering off the GPU itself too. It's safe to cut the power whilst
	 * they're transitioning to off, because the cores should be idle and all
	 * cache flushes should already have occurred */

	/* Consume any change-state events */
	kbase_timeline_pm_check_handle_event(kbdev, KBASE_TIMELINE_PM_EVENT_GPU_STATE_CHANGED);
	/* Disable interrupts and turn the clock off */
	kbase_pm_clock_off(kbdev, is_suspend);
}

mali_error kbase_pm_powerup(struct kbase_device *kbdev)
{
	unsigned long flags;
	mali_error ret;

	KBASE_DEBUG_ASSERT(kbdev != NULL);

	mutex_lock(&kbdev->pm.lock);

	/* A suspend won't happen during startup/insmod */
	KBASE_DEBUG_ASSERT(!kbase_pm_is_suspending(kbdev));

	/* Power up the GPU, don't enable IRQs as we are not ready to receive them. */
	ret = kbase_pm_init_hw(kbdev, MALI_FALSE);
	if (ret != MALI_ERROR_NONE) {
		mutex_unlock(&kbdev->pm.lock);
		return ret;
	}

	kbasep_pm_read_present_cores(kbdev);

	kbdev->pm.debug_core_mask = kbdev->shader_present_bitmap;

	/* Pretend the GPU is active to prevent a power policy turning the GPU cores off */
	kbdev->pm.active_count = 1;

	spin_lock_irqsave(&kbdev->pm.gpu_cycle_counter_requests_lock, flags);
	/* Ensure cycle counter is off */
	kbdev->pm.gpu_cycle_counter_requests = 0;
	spin_unlock_irqrestore(&kbdev->pm.gpu_cycle_counter_requests_lock, flags);

	/* We are ready to receive IRQ's now as power policy is set up, so enable them now. */
#ifdef CONFIG_MALI_DEBUG
	spin_lock_irqsave(&kbdev->pm.gpu_powered_lock, flags);
	kbdev->pm.driver_ready_for_irqs = MALI_TRUE;
	spin_unlock_irqrestore(&kbdev->pm.gpu_powered_lock, flags);
#endif
	kbase_pm_enable_interrupts(kbdev);

	/* Turn on the GPU and any cores needed by the policy */
	kbase_pm_do_poweron(kbdev, MALI_FALSE);
	mutex_unlock(&kbdev->pm.lock);

	/* Idle the GPU and/or cores, if the policy wants it to */
	kbase_pm_context_idle(kbdev);

	return MALI_ERROR_NONE;
}

KBASE_EXPORT_TEST_API(kbase_pm_powerup)

void kbase_pm_context_active(struct kbase_device *kbdev)
{
	(void)kbase_pm_context_active_handle_suspend(kbdev, KBASE_PM_SUSPEND_HANDLER_NOT_POSSIBLE);
}

int kbase_pm_context_active_handle_suspend(struct kbase_device *kbdev, enum kbase_pm_suspend_handler suspend_handler)
{
	int c;
	int old_count;

	KBASE_DEBUG_ASSERT(kbdev != NULL);

	/* Trace timeline information about how long it took to handle the decision
	 * to powerup. Sometimes the event might be missed due to reading the count
	 * outside of mutex, but this is necessary to get the trace timing
	 * correct. */
	old_count = kbdev->pm.active_count;
	if (old_count == 0)
		kbase_timeline_pm_send_event(kbdev, KBASE_TIMELINE_PM_EVENT_GPU_ACTIVE);

	mutex_lock(&kbdev->pm.lock);
	if (kbase_pm_is_suspending(kbdev)) {
		switch (suspend_handler) {
		case KBASE_PM_SUSPEND_HANDLER_DONT_REACTIVATE:
			if (kbdev->pm.active_count != 0)
				break;
			/* FALLTHROUGH */
		case KBASE_PM_SUSPEND_HANDLER_DONT_INCREASE:
			mutex_unlock(&kbdev->pm.lock);
			if (old_count == 0)
				kbase_timeline_pm_handle_event(kbdev, KBASE_TIMELINE_PM_EVENT_GPU_ACTIVE);
			return 1;

		case KBASE_PM_SUSPEND_HANDLER_NOT_POSSIBLE:
			/* FALLTHROUGH */
		default:
			KBASE_DEBUG_ASSERT_MSG(MALI_FALSE, "unreachable");
			break;
		}
	}
	c = ++kbdev->pm.active_count;
	KBASE_TIMELINE_CONTEXT_ACTIVE(kbdev, c);

	KBASE_TRACE_ADD_REFCOUNT(kbdev, PM_CONTEXT_ACTIVE, NULL, NULL, 0u, c);

	/* Trace the event being handled */
	if (old_count == 0)
		kbase_timeline_pm_handle_event(kbdev, KBASE_TIMELINE_PM_EVENT_GPU_ACTIVE);

	if (c == 1) {
		/* First context active: Power on the GPU and any cores requested by
		 * the policy */
		kbase_pm_update_active(kbdev);

		kbasep_pm_record_gpu_active(kbdev);
	}

	mutex_unlock(&kbdev->pm.lock);

	return 0;
}

KBASE_EXPORT_TEST_API(kbase_pm_context_active)

void kbase_pm_context_idle(struct kbase_device *kbdev)
{
	int c;
	int old_count;

	KBASE_DEBUG_ASSERT(kbdev != NULL);

	/* Trace timeline information about how long it took to handle the decision
	 * to powerdown. Sometimes the event might be missed due to reading the
	 * count outside of mutex, but this is necessary to get the trace timing
	 * correct. */
	old_count = kbdev->pm.active_count;
	if (old_count == 0)
		kbase_timeline_pm_send_event(kbdev, KBASE_TIMELINE_PM_EVENT_GPU_IDLE);

	mutex_lock(&kbdev->pm.lock);

	c = --kbdev->pm.active_count;
	KBASE_TIMELINE_CONTEXT_ACTIVE(kbdev, c);

	KBASE_TRACE_ADD_REFCOUNT(kbdev, PM_CONTEXT_IDLE, NULL, NULL, 0u, c);

	KBASE_DEBUG_ASSERT(c >= 0);

	/* Trace the event being handled */
	if (old_count == 0)
		kbase_timeline_pm_handle_event(kbdev, KBASE_TIMELINE_PM_EVENT_GPU_IDLE);

	if (c == 0) {
		/* Last context has gone idle */
		kbase_pm_update_active(kbdev);

		kbasep_pm_record_gpu_idle(kbdev);

		/* Wake up anyone waiting for this to become 0 (e.g. suspend). The
		 * waiters must synchronize with us by locking the pm.lock after
		 * waiting */
		wake_up(&kbdev->pm.zero_active_count_wait);
	}

	mutex_unlock(&kbdev->pm.lock);
}

KBASE_EXPORT_TEST_API(kbase_pm_context_idle)

void kbase_pm_halt(struct kbase_device *kbdev)
{
	KBASE_DEBUG_ASSERT(kbdev != NULL);

	mutex_lock(&kbdev->pm.lock);
	kbase_pm_cancel_deferred_poweroff(kbdev);
	kbase_pm_do_poweroff(kbdev, MALI_FALSE);
	mutex_unlock(&kbdev->pm.lock);
}

KBASE_EXPORT_TEST_API(kbase_pm_halt)

void kbase_pm_term(struct kbase_device *kbdev)
{
	KBASE_DEBUG_ASSERT(kbdev != NULL);
	KBASE_DEBUG_ASSERT(kbdev->pm.active_count == 0);
	KBASE_DEBUG_ASSERT(kbdev->pm.gpu_cycle_counter_requests == 0);

	/* Free any resources the policy allocated */
	kbase_pm_policy_term(kbdev);
	kbase_pm_ca_term(kbdev);

	/* Shut down the metrics subsystem */
	kbasep_pm_metrics_term(kbdev);
}

KBASE_EXPORT_TEST_API(kbase_pm_term)

void kbase_pm_suspend(struct kbase_device *kbdev)
{
	int nr_keep_gpu_powered_ctxs;

	KBASE_DEBUG_ASSERT(kbdev);

	mutex_lock(&kbdev->pm.lock);
	KBASE_DEBUG_ASSERT(!kbase_pm_is_suspending(kbdev));
	kbdev->pm.suspending = MALI_TRUE;
	mutex_unlock(&kbdev->pm.lock);

	/* From now on, the active count will drop towards zero. Sometimes, it'll
	 * go up briefly before going down again. However, once it reaches zero it
	 * will stay there - guaranteeing that we've idled all pm references */

	/* Suspend job scheduler and associated components, so that it releases all
	 * the PM active count references */
	kbasep_js_suspend(kbdev);

	/* Suspend any counter collection that might be happening */
	kbase_instr_hwcnt_suspend(kbdev);

	/* Cancel the keep_gpu_powered calls */
	for (nr_keep_gpu_powered_ctxs = atomic_read(&kbdev->keep_gpu_powered_count);
		 nr_keep_gpu_powered_ctxs > 0;
		 --nr_keep_gpu_powered_ctxs) {
		kbase_pm_context_idle(kbdev);
	}

	/* Wait for the active count to reach zero. This is not the same as
	 * waiting for a power down, since not all policies power down when this
	 * reaches zero. */
	wait_event(kbdev->pm.zero_active_count_wait, kbdev->pm.active_count == 0);

	/* NOTE: We synchronize with anything that was just finishing a
	 * kbase_pm_context_idle() call by locking the pm.lock below */

	/* Force power off the GPU and all cores (regardless of policy), only after
	 * the PM active count reaches zero (otherwise, we risk turning it off
	 * prematurely) */
	mutex_lock(&kbdev->pm.lock);
	kbase_pm_cancel_deferred_poweroff(kbdev);
	kbase_pm_do_poweroff(kbdev, MALI_TRUE);
	mutex_unlock(&kbdev->pm.lock);
}

void kbase_pm_resume(struct kbase_device *kbdev)
{
	int nr_keep_gpu_powered_ctxs;

	/* MUST happen before any pm_context_active calls occur */
	mutex_lock(&kbdev->pm.lock);
	kbdev->pm.suspending = MALI_FALSE;
	kbase_pm_do_poweron(kbdev, MALI_TRUE);
	mutex_unlock(&kbdev->pm.lock);

	/* Initial active call, to power on the GPU/cores if needed */
	kbase_pm_context_active(kbdev);

	/* Restore the keep_gpu_powered calls */
	for (nr_keep_gpu_powered_ctxs = atomic_read(&kbdev->keep_gpu_powered_count);
		 nr_keep_gpu_powered_ctxs > 0;
		 --nr_keep_gpu_powered_ctxs) {
		kbase_pm_context_active(kbdev);
	}

	/* Re-enable instrumentation, if it was previously disabled */
	kbase_instr_hwcnt_resume(kbdev);

	/* Resume any blocked atoms (which may cause contexts to be scheduled in
	 * and dependent atoms to run) */
	kbase_resume_suspended_soft_jobs(kbdev);

	/* Resume the Job Scheduler and associated components, and start running
	 * atoms */
	kbasep_js_resume(kbdev);

	/* Matching idle call, to power off the GPU/cores if we didn't actually
	 * need it and the policy doesn't want it on */
	kbase_pm_context_idle(kbdev);
}
#endif /* KBASE_PM_EN */
