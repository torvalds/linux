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
 * GPU backend implementation of base kernel power management APIs
 */

#include <mali_kbase.h>
#include <mali_midg_regmap.h>
#include <mali_kbase_config_defaults.h>
#ifdef CONFIG_MALI_PLATFORM_DEVICETREE
#include <linux/pm_runtime.h>
#endif /* CONFIG_MALI_PLATFORM_DEVICETREE */

#include <mali_kbase_pm.h>
#include <backend/gpu/mali_kbase_jm_internal.h>
#include <backend/gpu/mali_kbase_pm_internal.h>

void kbase_pm_register_access_enable(struct kbase_device *kbdev)
{
	struct kbase_pm_callback_conf *callbacks;

#ifdef CONFIG_MALI_PLATFORM_DEVICETREE
	pm_runtime_enable(kbdev->dev);
#endif /* CONFIG_MALI_PLATFORM_DEVICETREE */
	callbacks = (struct kbase_pm_callback_conf *)POWER_MANAGEMENT_CALLBACKS;

	if (callbacks)
		callbacks->power_on_callback(kbdev);

	kbdev->pm.backend.gpu_powered = true;
}

void kbase_pm_register_access_disable(struct kbase_device *kbdev)
{
	struct kbase_pm_callback_conf *callbacks;

	callbacks = (struct kbase_pm_callback_conf *)POWER_MANAGEMENT_CALLBACKS;

	if (callbacks)
		callbacks->power_off_callback(kbdev);

	kbdev->pm.backend.gpu_powered = false;
#ifdef CONFIG_MALI_PLATFORM_DEVICETREE
	pm_runtime_disable(kbdev->dev);
#endif
}

int kbase_hwaccess_pm_init(struct kbase_device *kbdev)
{
	int ret = 0;
	struct kbase_pm_callback_conf *callbacks;

	KBASE_DEBUG_ASSERT(kbdev != NULL);

	mutex_init(&kbdev->pm.lock);

	kbdev->pm.backend.gpu_powered = false;
	kbdev->pm.suspending = false;
#ifdef CONFIG_MALI_DEBUG
	kbdev->pm.backend.driver_ready_for_irqs = false;
#endif /* CONFIG_MALI_DEBUG */
	kbdev->pm.backend.gpu_in_desired_state = true;
	init_waitqueue_head(&kbdev->pm.backend.gpu_in_desired_state_wait);

	callbacks = (struct kbase_pm_callback_conf *)POWER_MANAGEMENT_CALLBACKS;
	if (callbacks) {
		kbdev->pm.backend.callback_power_on =
					callbacks->power_on_callback;
		kbdev->pm.backend.callback_power_off =
					callbacks->power_off_callback;
		kbdev->pm.backend.callback_power_suspend =
					callbacks->power_suspend_callback;
		kbdev->pm.backend.callback_power_resume =
					callbacks->power_resume_callback;
		kbdev->pm.callback_power_runtime_init =
					callbacks->power_runtime_init_callback;
		kbdev->pm.callback_power_runtime_term =
					callbacks->power_runtime_term_callback;
		kbdev->pm.backend.callback_power_runtime_on =
					callbacks->power_runtime_on_callback;
		kbdev->pm.backend.callback_power_runtime_off =
					callbacks->power_runtime_off_callback;
	} else {
		kbdev->pm.backend.callback_power_on = NULL;
		kbdev->pm.backend.callback_power_off = NULL;
		kbdev->pm.backend.callback_power_suspend = NULL;
		kbdev->pm.backend.callback_power_resume = NULL;
		kbdev->pm.callback_power_runtime_init = NULL;
		kbdev->pm.callback_power_runtime_term = NULL;
		kbdev->pm.backend.callback_power_runtime_on = NULL;
		kbdev->pm.backend.callback_power_runtime_off = NULL;
	}

	/* Initialise the metrics subsystem */
	ret = kbasep_pm_metrics_init(kbdev);
	if (ret)
		return ret;

	init_waitqueue_head(&kbdev->pm.backend.l2_powered_wait);
	kbdev->pm.backend.l2_powered = 0;

	init_waitqueue_head(&kbdev->pm.backend.reset_done_wait);
	kbdev->pm.backend.reset_done = false;

	init_waitqueue_head(&kbdev->pm.zero_active_count_wait);
	kbdev->pm.active_count = 0;

	spin_lock_init(&kbdev->pm.power_change_lock);
	spin_lock_init(&kbdev->pm.backend.gpu_cycle_counter_requests_lock);
	spin_lock_init(&kbdev->pm.backend.gpu_powered_lock);

	if (kbase_pm_ca_init(kbdev) != 0)
		goto workq_fail;

	if (kbase_pm_policy_init(kbdev) != 0)
		goto pm_policy_fail;

	return 0;

pm_policy_fail:
	kbase_pm_ca_term(kbdev);
workq_fail:
	kbasep_pm_metrics_term(kbdev);
	return -EINVAL;
}

void kbase_pm_do_poweron(struct kbase_device *kbdev, bool is_resume)
{
	lockdep_assert_held(&kbdev->pm.lock);

	/* Turn clocks and interrupts on - no-op if we haven't done a previous
	 * kbase_pm_clock_off() */
	kbase_pm_clock_on(kbdev, is_resume);

	/* Update core status as required by the policy */
	KBASE_TIMELINE_PM_CHECKTRANS(kbdev,
				SW_FLOW_PM_CHECKTRANS_PM_DO_POWERON_START);
	kbase_pm_update_cores_state(kbdev);
	KBASE_TIMELINE_PM_CHECKTRANS(kbdev,
				SW_FLOW_PM_CHECKTRANS_PM_DO_POWERON_END);

	/* NOTE: We don't wait to reach the desired state, since running atoms
	 * will wait for that state to be reached anyway */
}

bool kbase_pm_do_poweroff(struct kbase_device *kbdev, bool is_suspend)
{
	unsigned long flags;
	bool cores_are_available;

	lockdep_assert_held(&kbdev->pm.lock);

	spin_lock_irqsave(&kbdev->pm.power_change_lock, flags);

	/* Force all cores off */
	kbdev->pm.backend.desired_shader_state = 0;

	/* Force all cores to be unavailable, in the situation where
	 * transitions are in progress for some cores but not others,
	 * and kbase_pm_check_transitions_nolock can not immediately
	 * power off the cores */
	kbdev->shader_available_bitmap = 0;
	kbdev->tiler_available_bitmap = 0;
	kbdev->l2_available_bitmap = 0;

	KBASE_TIMELINE_PM_CHECKTRANS(kbdev,
				SW_FLOW_PM_CHECKTRANS_PM_DO_POWEROFF_START);
	cores_are_available = kbase_pm_check_transitions_nolock(kbdev);
	KBASE_TIMELINE_PM_CHECKTRANS(kbdev,
				SW_FLOW_PM_CHECKTRANS_PM_DO_POWEROFF_END);
	/* Don't need 'cores_are_available', because we don't return anything */
	CSTD_UNUSED(cores_are_available);

	spin_unlock_irqrestore(&kbdev->pm.power_change_lock, flags);

	/* NOTE: We won't wait to reach the core's desired state, even if we're
	 * powering off the GPU itself too. It's safe to cut the power whilst
	 * they're transitioning to off, because the cores should be idle and
	 * all cache flushes should already have occurred */

	/* Consume any change-state events */
	kbase_timeline_pm_check_handle_event(kbdev,
				KBASE_TIMELINE_PM_EVENT_GPU_STATE_CHANGED);
	/* Disable interrupts and turn the clock off */
	return kbase_pm_clock_off(kbdev, is_suspend);
}

int kbase_hwaccess_pm_powerup(struct kbase_device *kbdev,
		unsigned int flags)
{
	struct kbasep_js_device_data *js_devdata = &kbdev->js_data;
	unsigned long irq_flags;
	int ret;

	KBASE_DEBUG_ASSERT(kbdev != NULL);

	mutex_lock(&js_devdata->runpool_mutex);
	mutex_lock(&kbdev->pm.lock);

	/* A suspend won't happen during startup/insmod */
	KBASE_DEBUG_ASSERT(!kbase_pm_is_suspending(kbdev));

	/* Power up the GPU, don't enable IRQs as we are not ready to receive
	 * them. */
	ret = kbase_pm_init_hw(kbdev, flags);
	if (ret) {
		mutex_unlock(&kbdev->pm.lock);
		mutex_unlock(&js_devdata->runpool_mutex);
		return ret;
	}

	kbasep_pm_read_present_cores(kbdev);

	kbdev->pm.debug_core_mask =
			kbdev->gpu_props.props.raw_props.shader_present;

	/* Pretend the GPU is active to prevent a power policy turning the GPU
	 * cores off */
	kbdev->pm.active_count = 1;

	spin_lock_irqsave(&kbdev->pm.backend.gpu_cycle_counter_requests_lock,
								irq_flags);
	/* Ensure cycle counter is off */
	kbdev->pm.backend.gpu_cycle_counter_requests = 0;
	spin_unlock_irqrestore(
			&kbdev->pm.backend.gpu_cycle_counter_requests_lock,
								irq_flags);

	/* We are ready to receive IRQ's now as power policy is set up, so
	 * enable them now. */
#ifdef CONFIG_MALI_DEBUG
	spin_lock_irqsave(&kbdev->pm.backend.gpu_powered_lock, irq_flags);
	kbdev->pm.backend.driver_ready_for_irqs = true;
	spin_unlock_irqrestore(&kbdev->pm.backend.gpu_powered_lock, irq_flags);
#endif
	kbase_pm_enable_interrupts(kbdev);

	/* Turn on the GPU and any cores needed by the policy */
	kbase_pm_do_poweron(kbdev, false);
	mutex_unlock(&kbdev->pm.lock);
	mutex_unlock(&js_devdata->runpool_mutex);

	/* Idle the GPU and/or cores, if the policy wants it to */
	kbase_pm_context_idle(kbdev);

	return 0;
}

void kbase_hwaccess_pm_halt(struct kbase_device *kbdev)
{
	KBASE_DEBUG_ASSERT(kbdev != NULL);

	mutex_lock(&kbdev->pm.lock);
	kbase_pm_cancel_deferred_poweroff(kbdev);
	if (!kbase_pm_do_poweroff(kbdev, false)) {
		/* Page/bus faults are pending, must drop pm.lock to process.
		 * Interrupts are disabled so no more faults should be
		 * generated at this point */
		mutex_unlock(&kbdev->pm.lock);
		kbase_flush_mmu_wqs(kbdev);
		mutex_lock(&kbdev->pm.lock);
		WARN_ON(!kbase_pm_do_poweroff(kbdev, false));
	}
	mutex_unlock(&kbdev->pm.lock);
}

KBASE_EXPORT_TEST_API(kbase_hwaccess_pm_halt);

void kbase_hwaccess_pm_term(struct kbase_device *kbdev)
{
	KBASE_DEBUG_ASSERT(kbdev != NULL);
	KBASE_DEBUG_ASSERT(kbdev->pm.active_count == 0);
	KBASE_DEBUG_ASSERT(kbdev->pm.backend.gpu_cycle_counter_requests == 0);

	/* Free any resources the policy allocated */
	kbase_pm_policy_term(kbdev);
	kbase_pm_ca_term(kbdev);

	/* Shut down the metrics subsystem */
	kbasep_pm_metrics_term(kbdev);
}

void kbase_pm_power_changed(struct kbase_device *kbdev)
{
	bool cores_are_available;
	unsigned long flags;

	KBASE_TIMELINE_PM_CHECKTRANS(kbdev,
				SW_FLOW_PM_CHECKTRANS_GPU_INTERRUPT_START);
	spin_lock_irqsave(&kbdev->pm.power_change_lock, flags);
	cores_are_available = kbase_pm_check_transitions_nolock(kbdev);
	spin_unlock_irqrestore(&kbdev->pm.power_change_lock, flags);
	KBASE_TIMELINE_PM_CHECKTRANS(kbdev,
				SW_FLOW_PM_CHECKTRANS_GPU_INTERRUPT_END);

	if (cores_are_available) {
		/* Log timelining information that a change in state has
		 * completed */
		kbase_timeline_pm_handle_event(kbdev,
				KBASE_TIMELINE_PM_EVENT_GPU_STATE_CHANGED);

		spin_lock_irqsave(&kbdev->js_data.runpool_irq.lock, flags);
		kbase_gpu_slot_update(kbdev);
		spin_unlock_irqrestore(&kbdev->js_data.runpool_irq.lock, flags);
	}
}

void kbase_pm_set_debug_core_mask(struct kbase_device *kbdev, u64 new_core_mask)
{
	kbdev->pm.debug_core_mask = new_core_mask;

	kbase_pm_update_cores_state_nolock(kbdev);
}

void kbase_hwaccess_pm_gpu_active(struct kbase_device *kbdev)
{
	kbase_pm_update_active(kbdev);
}

void kbase_hwaccess_pm_gpu_idle(struct kbase_device *kbdev)
{
	kbase_pm_update_active(kbdev);
}

void kbase_hwaccess_pm_suspend(struct kbase_device *kbdev)
{
	struct kbasep_js_device_data *js_devdata = &kbdev->js_data;

	/* Force power off the GPU and all cores (regardless of policy), only
	 * after the PM active count reaches zero (otherwise, we risk turning it
	 * off prematurely) */
	mutex_lock(&js_devdata->runpool_mutex);
	mutex_lock(&kbdev->pm.lock);
	kbase_pm_cancel_deferred_poweroff(kbdev);
	if (!kbase_pm_do_poweroff(kbdev, true)) {
		/* Page/bus faults are pending, must drop pm.lock to process.
		 * Interrupts are disabled so no more faults should be
		 * generated at this point */
		mutex_unlock(&kbdev->pm.lock);
		kbase_flush_mmu_wqs(kbdev);
		mutex_lock(&kbdev->pm.lock);
		WARN_ON(!kbase_pm_do_poweroff(kbdev, false));
	}

	mutex_unlock(&kbdev->pm.lock);
	mutex_unlock(&js_devdata->runpool_mutex);
}

void kbase_hwaccess_pm_resume(struct kbase_device *kbdev)
{
	struct kbasep_js_device_data *js_devdata = &kbdev->js_data;

	mutex_lock(&js_devdata->runpool_mutex);
	mutex_lock(&kbdev->pm.lock);
	kbdev->pm.suspending = false;
	kbase_pm_do_poweron(kbdev, true);
	mutex_unlock(&kbdev->pm.lock);
	mutex_unlock(&js_devdata->runpool_mutex);
}
