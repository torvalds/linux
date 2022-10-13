// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2010-2022 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU license.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 */

/*
 * GPU backend implementation of base kernel power management APIs
 */

#include <mali_kbase.h>
#include <gpu/mali_kbase_gpu_regmap.h>
#include <mali_kbase_config_defaults.h>

#include <mali_kbase_pm.h>
#if !MALI_USE_CSF
#include <mali_kbase_hwaccess_jm.h>
#include <backend/gpu/mali_kbase_js_internal.h>
#include <backend/gpu/mali_kbase_jm_internal.h>
#else
#include <linux/pm_runtime.h>
#include <mali_kbase_reset_gpu.h>
#endif /* !MALI_USE_CSF */
#include <hwcnt/mali_kbase_hwcnt_context.h>
#include <backend/gpu/mali_kbase_pm_internal.h>
#include <backend/gpu/mali_kbase_devfreq.h>
#include <mali_kbase_dummy_job_wa.h>
#include <backend/gpu/mali_kbase_irq_internal.h>

static void kbase_pm_gpu_poweroff_wait_wq(struct work_struct *data);
static void kbase_pm_hwcnt_disable_worker(struct work_struct *data);
static void kbase_pm_gpu_clock_control_worker(struct work_struct *data);

int kbase_pm_runtime_init(struct kbase_device *kbdev)
{
	struct kbase_pm_callback_conf *callbacks;

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
		kbdev->pm.backend.callback_power_runtime_idle =
					callbacks->power_runtime_idle_callback;
		kbdev->pm.backend.callback_soft_reset =
					callbacks->soft_reset_callback;
		kbdev->pm.backend.callback_power_runtime_gpu_idle =
					callbacks->power_runtime_gpu_idle_callback;
		kbdev->pm.backend.callback_power_runtime_gpu_active =
					callbacks->power_runtime_gpu_active_callback;

		if (callbacks->power_runtime_init_callback)
			return callbacks->power_runtime_init_callback(kbdev);
		else
			return 0;
	}

	kbdev->pm.backend.callback_power_on = NULL;
	kbdev->pm.backend.callback_power_off = NULL;
	kbdev->pm.backend.callback_power_suspend = NULL;
	kbdev->pm.backend.callback_power_resume = NULL;
	kbdev->pm.callback_power_runtime_init = NULL;
	kbdev->pm.callback_power_runtime_term = NULL;
	kbdev->pm.backend.callback_power_runtime_on = NULL;
	kbdev->pm.backend.callback_power_runtime_off = NULL;
	kbdev->pm.backend.callback_power_runtime_idle = NULL;
	kbdev->pm.backend.callback_soft_reset = NULL;
	kbdev->pm.backend.callback_power_runtime_gpu_idle = NULL;
	kbdev->pm.backend.callback_power_runtime_gpu_active = NULL;

	return 0;
}

void kbase_pm_runtime_term(struct kbase_device *kbdev)
{
	if (kbdev->pm.callback_power_runtime_term)
		kbdev->pm.callback_power_runtime_term(kbdev);
}

void kbase_pm_register_access_enable(struct kbase_device *kbdev)
{
	struct kbase_pm_callback_conf *callbacks;

	callbacks = (struct kbase_pm_callback_conf *)POWER_MANAGEMENT_CALLBACKS;

	if (callbacks)
		callbacks->power_on_callback(kbdev);

#ifdef CONFIG_MALI_ARBITER_SUPPORT
	if (WARN_ON(kbase_pm_is_gpu_lost(kbdev)))
		dev_err(kbdev->dev, "Attempting to power on while GPU lost\n");
#endif

	kbdev->pm.backend.gpu_powered = true;
}

void kbase_pm_register_access_disable(struct kbase_device *kbdev)
{
	struct kbase_pm_callback_conf *callbacks;

	callbacks = (struct kbase_pm_callback_conf *)POWER_MANAGEMENT_CALLBACKS;

	kbdev->pm.backend.gpu_powered = false;

	if (callbacks)
		callbacks->power_off_callback(kbdev);
}

int kbase_hwaccess_pm_init(struct kbase_device *kbdev)
{
	int ret = 0;

	KBASE_DEBUG_ASSERT(kbdev != NULL);

	mutex_init(&kbdev->pm.lock);

	kbdev->pm.backend.gpu_poweroff_wait_wq = alloc_workqueue("kbase_pm_poweroff_wait",
			WQ_HIGHPRI | WQ_UNBOUND, 1);
	if (!kbdev->pm.backend.gpu_poweroff_wait_wq)
		return -ENOMEM;

	INIT_WORK(&kbdev->pm.backend.gpu_poweroff_wait_work,
			kbase_pm_gpu_poweroff_wait_wq);

	kbdev->pm.backend.ca_cores_enabled = ~0ull;
	kbdev->pm.backend.gpu_powered = false;
	kbdev->pm.backend.gpu_ready = false;
	kbdev->pm.suspending = false;
#ifdef CONFIG_MALI_ARBITER_SUPPORT
	kbase_pm_set_gpu_lost(kbdev, false);
#endif
#ifdef CONFIG_MALI_BIFROST_DEBUG
	kbdev->pm.backend.driver_ready_for_irqs = false;
#endif /* CONFIG_MALI_BIFROST_DEBUG */
	init_waitqueue_head(&kbdev->pm.backend.gpu_in_desired_state_wait);

#if !MALI_USE_CSF
	/* Initialise the metrics subsystem */
	ret = kbasep_pm_metrics_init(kbdev);
	if (ret)
		return ret;
#else
	mutex_init(&kbdev->pm.backend.policy_change_lock);
	kbdev->pm.backend.policy_change_clamp_state_to_off = false;
	/* Due to dependency on kbase_ipa_control, the metrics subsystem can't
	 * be initialized here.
	 */
	CSTD_UNUSED(ret);
#endif

	init_waitqueue_head(&kbdev->pm.backend.reset_done_wait);
	kbdev->pm.backend.reset_done = false;

	init_waitqueue_head(&kbdev->pm.zero_active_count_wait);
	init_waitqueue_head(&kbdev->pm.resume_wait);
	kbdev->pm.active_count = 0;

	spin_lock_init(&kbdev->pm.backend.gpu_cycle_counter_requests_lock);

	init_waitqueue_head(&kbdev->pm.backend.poweroff_wait);

	if (kbase_pm_ca_init(kbdev) != 0)
		goto workq_fail;

	kbase_pm_policy_init(kbdev);

	if (kbase_pm_state_machine_init(kbdev) != 0)
		goto pm_state_machine_fail;

	kbdev->pm.backend.hwcnt_desired = false;
	kbdev->pm.backend.hwcnt_disabled = true;
	INIT_WORK(&kbdev->pm.backend.hwcnt_disable_work,
		kbase_pm_hwcnt_disable_worker);
	kbase_hwcnt_context_disable(kbdev->hwcnt_gpu_ctx);

#if MALI_USE_CSF && defined(KBASE_PM_RUNTIME)
	kbdev->pm.backend.gpu_sleep_supported =
		kbase_hw_has_feature(kbdev, BASE_HW_FEATURE_GPU_SLEEP) &&
		!kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_TURSEHW_1997) &&
		kbdev->pm.backend.callback_power_runtime_gpu_active &&
		kbdev->pm.backend.callback_power_runtime_gpu_idle;
#endif

	if (IS_ENABLED(CONFIG_MALI_HW_ERRATA_1485982_NOT_AFFECTED)) {
		kbdev->pm.backend.l2_always_on = false;
		kbdev->pm.backend.gpu_clock_slow_down_wa = false;

		return 0;
	}

	/* WA1: L2 always_on for GPUs being affected by GPU2017-1336 */
	if (!IS_ENABLED(CONFIG_MALI_HW_ERRATA_1485982_USE_CLOCK_ALTERNATIVE)) {
		kbdev->pm.backend.gpu_clock_slow_down_wa = false;
		if (kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_GPU2017_1336))
			kbdev->pm.backend.l2_always_on = true;
		else
			kbdev->pm.backend.l2_always_on = false;

		return 0;
	}

	/* WA3: Clock slow down for GPUs being affected by GPU2017-1336 */
	kbdev->pm.backend.l2_always_on = false;
	if (kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_GPU2017_1336)) {
		kbdev->pm.backend.gpu_clock_slow_down_wa = true;
		kbdev->pm.backend.gpu_clock_suspend_freq = 0;
		kbdev->pm.backend.gpu_clock_slow_down_desired = true;
		kbdev->pm.backend.gpu_clock_slowed_down = false;
		INIT_WORK(&kbdev->pm.backend.gpu_clock_control_work,
			kbase_pm_gpu_clock_control_worker);
	} else
		kbdev->pm.backend.gpu_clock_slow_down_wa = false;

	return 0;

pm_state_machine_fail:
	kbase_pm_policy_term(kbdev);
	kbase_pm_ca_term(kbdev);
workq_fail:
#if !MALI_USE_CSF
	kbasep_pm_metrics_term(kbdev);
#endif
	return -EINVAL;
}

void kbase_pm_do_poweron(struct kbase_device *kbdev, bool is_resume)
{
	lockdep_assert_held(&kbdev->pm.lock);

	/* Turn clocks and interrupts on - no-op if we haven't done a previous
	 * kbase_pm_clock_off()
	 */
	kbase_pm_clock_on(kbdev, is_resume);

	if (!is_resume) {
		unsigned long flags;

		/* Force update of L2 state - if we have abandoned a power off
		 * then this may be required to power the L2 back on.
		 */
		spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
		kbase_pm_update_state(kbdev);
		spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
	}

	/* Update core status as required by the policy */
	kbase_pm_update_cores_state(kbdev);

	/* NOTE: We don't wait to reach the desired state, since running atoms
	 * will wait for that state to be reached anyway
	 */
}

static void pm_handle_power_off(struct kbase_device *kbdev)
{
	struct kbase_pm_backend_data *backend = &kbdev->pm.backend;
#if MALI_USE_CSF
	enum kbase_mcu_state mcu_state;
#endif
	unsigned long flags;

	lockdep_assert_held(&kbdev->pm.lock);

	if (backend->poweron_required)
		return;

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
#if MALI_USE_CSF && defined(KBASE_PM_RUNTIME)
	if (kbdev->pm.backend.gpu_wakeup_override) {
		spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
		return;
	}
#endif
	WARN_ON(backend->shaders_state !=
			KBASE_SHADERS_OFF_CORESTACK_OFF ||
		backend->l2_state != KBASE_L2_OFF);
#if MALI_USE_CSF
	mcu_state = backend->mcu_state;
	WARN_ON(!kbase_pm_is_mcu_inactive(kbdev, mcu_state));
#endif
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

#if MALI_USE_CSF && defined(KBASE_PM_RUNTIME)
	if (backend->callback_power_runtime_gpu_idle) {
		WARN_ON(backend->gpu_idled);
		backend->callback_power_runtime_gpu_idle(kbdev);
		backend->gpu_idled = true;
		return;
	}
#endif

	/* Disable interrupts and turn the clock off */
	if (!kbase_pm_clock_off(kbdev)) {
		/*
		 * Page/bus faults are pending, must drop locks to
		 * process.  Interrupts are disabled so no more faults
		 * should be generated at this point.
		 */
		kbase_pm_unlock(kbdev);
		kbase_flush_mmu_wqs(kbdev);
		kbase_pm_lock(kbdev);

#ifdef CONFIG_MALI_ARBITER_SUPPORT
		/* poweron_required may have changed while pm lock
		 * was released.
		 */
		if (kbase_pm_is_gpu_lost(kbdev))
			backend->poweron_required = false;
#endif

		/* Turn off clock now that fault have been handled. We
		 * dropped locks so poweron_required may have changed -
		 * power back on if this is the case (effectively only
		 * re-enabling of the interrupts would be done in this
		 * case, as the clocks to GPU were not withdrawn yet).
		 */
		if (backend->poweron_required)
			kbase_pm_clock_on(kbdev, false);
		else
			WARN_ON(!kbase_pm_clock_off(kbdev));
	}
}

static void kbase_pm_gpu_poweroff_wait_wq(struct work_struct *data)
{
	struct kbase_device *kbdev = container_of(data, struct kbase_device,
			pm.backend.gpu_poweroff_wait_work);
	struct kbase_pm_device_data *pm = &kbdev->pm;
	struct kbase_pm_backend_data *backend = &pm->backend;
	unsigned long flags;

	KBASE_KTRACE_ADD(kbdev, PM_POWEROFF_WAIT_WQ, NULL, 0);

#if !MALI_USE_CSF
	/* Wait for power transitions to complete. We do this with no locks held
	 * so that we don't deadlock with any pending workqueues.
	 */
	kbase_pm_wait_for_desired_state(kbdev);
#endif

	kbase_pm_lock(kbdev);

	pm_handle_power_off(kbdev);

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	backend->poweroff_wait_in_progress = false;
	if (backend->poweron_required) {
		backend->poweron_required = false;
		kbdev->pm.backend.l2_desired = true;
#if MALI_USE_CSF
		kbdev->pm.backend.mcu_desired = true;
#endif
		kbase_pm_update_state(kbdev);
		kbase_pm_update_cores_state_nolock(kbdev);
#if !MALI_USE_CSF
		kbase_backend_slot_update(kbdev);
#endif /* !MALI_USE_CSF */
	}
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	kbase_pm_unlock(kbdev);

	wake_up(&kbdev->pm.backend.poweroff_wait);
}

static void kbase_pm_l2_clock_slow(struct kbase_device *kbdev)
{
#if defined(CONFIG_MALI_BIFROST_DVFS)
	struct clk *clk = kbdev->clocks[0];
#endif

	if (!kbdev->pm.backend.gpu_clock_slow_down_wa)
		return;

	/* No suspend clock is specified */
	if (WARN_ON_ONCE(!kbdev->pm.backend.gpu_clock_suspend_freq))
		return;

#if defined(CONFIG_MALI_BIFROST_DEVFREQ)

	/* Suspend devfreq */
	devfreq_suspend_device(kbdev->devfreq);

	/* Keep the current freq to restore it upon resume */
	kbdev->previous_frequency = kbdev->current_nominal_freq;

	/* Slow down GPU clock to the suspend clock*/
	kbase_devfreq_force_freq(kbdev,
			kbdev->pm.backend.gpu_clock_suspend_freq);

#elif defined(CONFIG_MALI_BIFROST_DVFS) /* CONFIG_MALI_BIFROST_DEVFREQ */

	if (WARN_ON_ONCE(!clk))
		return;

	/* Stop the metrics gathering framework */
	kbase_pm_metrics_stop(kbdev);

	/* Keep the current freq to restore it upon resume */
	kbdev->previous_frequency = clk_get_rate(clk);

	/* Slow down GPU clock to the suspend clock*/
	if (WARN_ON_ONCE(clk_set_rate(clk,
				kbdev->pm.backend.gpu_clock_suspend_freq)))
		dev_err(kbdev->dev, "Failed to set suspend freq\n");

#endif /* CONFIG_MALI_BIFROST_DVFS */
}

static void kbase_pm_l2_clock_normalize(struct kbase_device *kbdev)
{
#if defined(CONFIG_MALI_BIFROST_DVFS)
	struct clk *clk = kbdev->clocks[0];
#endif

	if (!kbdev->pm.backend.gpu_clock_slow_down_wa)
		return;

#if defined(CONFIG_MALI_BIFROST_DEVFREQ)

	/* Restore GPU clock to the previous one */
	kbase_devfreq_force_freq(kbdev, kbdev->previous_frequency);

	/* Resume devfreq */
	devfreq_resume_device(kbdev->devfreq);

#elif defined(CONFIG_MALI_BIFROST_DVFS) /* CONFIG_MALI_BIFROST_DEVFREQ */

	if (WARN_ON_ONCE(!clk))
		return;

	/* Restore GPU clock */
	if (WARN_ON_ONCE(clk_set_rate(clk, kbdev->previous_frequency)))
		dev_err(kbdev->dev, "Failed to restore freq (%lu)\n",
			kbdev->previous_frequency);

	/* Restart the metrics gathering framework */
	kbase_pm_metrics_start(kbdev);

#endif /* CONFIG_MALI_BIFROST_DVFS */
}

static void kbase_pm_gpu_clock_control_worker(struct work_struct *data)
{
	struct kbase_device *kbdev = container_of(data, struct kbase_device,
			pm.backend.gpu_clock_control_work);
	struct kbase_pm_device_data *pm = &kbdev->pm;
	struct kbase_pm_backend_data *backend = &pm->backend;
	unsigned long flags;
	bool slow_down = false, normalize = false;

	/* Determine if GPU clock control is required */
	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	if (!backend->gpu_clock_slowed_down &&
			backend->gpu_clock_slow_down_desired) {
		slow_down = true;
		backend->gpu_clock_slowed_down = true;
	} else if (backend->gpu_clock_slowed_down &&
			!backend->gpu_clock_slow_down_desired) {
		normalize = true;
		backend->gpu_clock_slowed_down = false;
	}
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	/* Control GPU clock according to the request of L2 state machine.
	 * The GPU clock needs to be lowered for safe L2 power down
	 * and restored to previous speed at L2 power up.
	 */
	if (slow_down)
		kbase_pm_l2_clock_slow(kbdev);
	else if (normalize)
		kbase_pm_l2_clock_normalize(kbdev);

	/* Tell L2 state machine to transit to next state */
	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	kbase_pm_update_state(kbdev);
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
}

static void kbase_pm_hwcnt_disable_worker(struct work_struct *data)
{
	struct kbase_device *kbdev = container_of(data, struct kbase_device,
			pm.backend.hwcnt_disable_work);
	struct kbase_pm_device_data *pm = &kbdev->pm;
	struct kbase_pm_backend_data *backend = &pm->backend;
	unsigned long flags;

	bool do_disable;

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	do_disable = !backend->hwcnt_desired && !backend->hwcnt_disabled;
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	if (!do_disable)
		return;

	kbase_hwcnt_context_disable(kbdev->hwcnt_gpu_ctx);

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	do_disable = !backend->hwcnt_desired && !backend->hwcnt_disabled;

	if (do_disable) {
		/* PM state did not change while we were doing the disable,
		 * so commit the work we just performed and continue the state
		 * machine.
		 */
		backend->hwcnt_disabled = true;
		kbase_pm_update_state(kbdev);
#if !MALI_USE_CSF
		kbase_backend_slot_update(kbdev);
#endif /* !MALI_USE_CSF */
	} else {
		/* PM state was updated while we were doing the disable,
		 * so we need to undo the disable we just performed.
		 */
#if MALI_USE_CSF
		unsigned long lock_flags;

		kbase_csf_scheduler_spin_lock(kbdev, &lock_flags);
#endif
		kbase_hwcnt_context_enable(kbdev->hwcnt_gpu_ctx);
#if MALI_USE_CSF
		kbase_csf_scheduler_spin_unlock(kbdev, lock_flags);
#endif
	}

	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
}

#if MALI_USE_CSF && defined(KBASE_PM_RUNTIME)
/**
 * kbase_pm_do_poweroff_sync - Do the synchronous power down of GPU
 *
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 *
 * This function is called at the time of system suspend or device unload
 * to power down the GPU synchronously. This is needed as the power down of GPU
 * would usually happen from the runtime suspend callback function (if gpu_active
 * and gpu_idle callbacks are used) and runtime suspend operation is disabled
 * when system suspend takes place.
 * The function first waits for the @gpu_poweroff_wait_work to complete, which
 * could have been enqueued after the last PM reference was released.
 *
 * Return: 0 on success, negative value otherwise.
 */
static int kbase_pm_do_poweroff_sync(struct kbase_device *kbdev)
{
	struct kbase_pm_backend_data *backend = &kbdev->pm.backend;
	unsigned long flags;
	int ret = 0;

	WARN_ON(kbdev->pm.active_count);

	kbase_pm_wait_for_poweroff_work_complete(kbdev);

	kbase_pm_lock(kbdev);
	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	WARN_ON(backend->poweroff_wait_in_progress);
	WARN_ON(backend->gpu_sleep_mode_active);
	if (backend->gpu_powered) {

		backend->mcu_desired = false;
		backend->l2_desired = false;
		kbase_pm_update_state(kbdev);
		spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

		ret = kbase_pm_wait_for_desired_state(kbdev);
		if (ret) {
			dev_warn(
				kbdev->dev,
				"Wait for pm state change failed on synchronous power off");
			ret = -EBUSY;
			goto out;
		}

		/* Due to the power policy, GPU could have been kept active
		 * throughout and so need to invoke the idle callback before
		 * the power down.
		 */
		if (backend->callback_power_runtime_gpu_idle &&
		    !backend->gpu_idled) {
			backend->callback_power_runtime_gpu_idle(kbdev);
			backend->gpu_idled = true;
		}

		if (!kbase_pm_clock_off(kbdev)) {
			dev_warn(
				kbdev->dev,
				"Failed to turn off GPU clocks on synchronous power off, MMU faults pending");
			ret = -EBUSY;
		}
	} else {
		spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
	}

out:
	kbase_pm_unlock(kbdev);
	return ret;
}
#endif

void kbase_pm_do_poweroff(struct kbase_device *kbdev)
{
	unsigned long flags;

	lockdep_assert_held(&kbdev->pm.lock);

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

	if (!kbdev->pm.backend.gpu_powered)
		goto unlock_hwaccess;

	if (kbdev->pm.backend.poweroff_wait_in_progress)
		goto unlock_hwaccess;

#if MALI_USE_CSF
	kbdev->pm.backend.mcu_desired = false;
#else
	/* Force all cores off */
	kbdev->pm.backend.shaders_desired = false;
#endif
	kbdev->pm.backend.l2_desired = false;

	kbdev->pm.backend.poweroff_wait_in_progress = true;
	kbdev->pm.backend.invoke_poweroff_wait_wq_when_l2_off = true;

	/* l2_desired being false should cause the state machine to
	 * start powering off the L2. When it actually is powered off,
	 * the interrupt handler will call kbase_pm_l2_update_state()
	 * again, which will trigger the kbase_pm_gpu_poweroff_wait_wq.
	 * Callers of this function will need to wait on poweroff_wait.
	 */
	kbase_pm_update_state(kbdev);

unlock_hwaccess:
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
}

static bool is_poweroff_in_progress(struct kbase_device *kbdev)
{
	bool ret;
	unsigned long flags;

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	ret = (kbdev->pm.backend.poweroff_wait_in_progress == false);
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	return ret;
}

void kbase_pm_wait_for_poweroff_work_complete(struct kbase_device *kbdev)
{
	wait_event_killable(kbdev->pm.backend.poweroff_wait,
			is_poweroff_in_progress(kbdev));
}
KBASE_EXPORT_TEST_API(kbase_pm_wait_for_poweroff_work_complete);

/**
 * is_gpu_powered_down - Check whether GPU is powered down
 *
 * @kbdev: kbase device
 *
 * Return: true if GPU is powered down, false otherwise
 */
static bool is_gpu_powered_down(struct kbase_device *kbdev)
{
	bool ret;
	unsigned long flags;

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	ret = !kbdev->pm.backend.gpu_powered;
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	return ret;
}

void kbase_pm_wait_for_gpu_power_down(struct kbase_device *kbdev)
{
	wait_event_killable(kbdev->pm.backend.poweroff_wait,
			is_gpu_powered_down(kbdev));
}
KBASE_EXPORT_TEST_API(kbase_pm_wait_for_gpu_power_down);

int kbase_hwaccess_pm_powerup(struct kbase_device *kbdev,
		unsigned int flags)
{
	unsigned long irq_flags;
	int ret;

	KBASE_DEBUG_ASSERT(kbdev != NULL);

	kbase_pm_lock(kbdev);

	/* A suspend won't happen during startup/insmod */
	KBASE_DEBUG_ASSERT(!kbase_pm_is_suspending(kbdev));

	/* Power up the GPU, don't enable IRQs as we are not ready to receive
	 * them
	 */
	ret = kbase_pm_init_hw(kbdev, flags);
	if (ret) {
		kbase_pm_unlock(kbdev);
		return ret;
	}
#if MALI_USE_CSF
	kbdev->pm.debug_core_mask =
		kbdev->gpu_props.props.raw_props.shader_present;
	spin_lock_irqsave(&kbdev->hwaccess_lock, irq_flags);
	/* Set the initial value for 'shaders_avail'. It would be later
	 * modified only from the MCU state machine, when the shader core
	 * allocation enable mask request has completed. So its value would
	 * indicate the mask of cores that are currently being used by FW for
	 * the allocation of endpoints requested by CSGs.
	 */
	kbdev->pm.backend.shaders_avail = kbase_pm_ca_get_core_mask(kbdev);
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, irq_flags);
#else
	kbdev->pm.debug_core_mask_all = kbdev->pm.debug_core_mask[0] =
			kbdev->pm.debug_core_mask[1] =
			kbdev->pm.debug_core_mask[2] =
			kbdev->gpu_props.props.raw_props.shader_present;
#endif

	/* Pretend the GPU is active to prevent a power policy turning the GPU
	 * cores off
	 */
	kbdev->pm.active_count = 1;
#if MALI_USE_CSF && KBASE_PM_RUNTIME
	if (kbdev->pm.backend.callback_power_runtime_gpu_active) {
		/* Take the RPM reference count to match with the internal
		 * PM reference count
		 */
		kbdev->pm.backend.callback_power_runtime_gpu_active(kbdev);
		WARN_ON(kbdev->pm.backend.gpu_idled);
	}
#endif

	spin_lock_irqsave(&kbdev->pm.backend.gpu_cycle_counter_requests_lock,
								irq_flags);
	/* Ensure cycle counter is off */
	kbdev->pm.backend.gpu_cycle_counter_requests = 0;
	spin_unlock_irqrestore(
			&kbdev->pm.backend.gpu_cycle_counter_requests_lock,
								irq_flags);

	/* We are ready to receive IRQ's now as power policy is set up, so
	 * enable them now.
	 */
#ifdef CONFIG_MALI_BIFROST_DEBUG
	kbdev->pm.backend.driver_ready_for_irqs = true;
#endif
	kbase_pm_enable_interrupts(kbdev);

	WARN_ON(!kbdev->pm.backend.gpu_powered);
	/* GPU has been powered up (by kbase_pm_init_hw) and interrupts have
	 * been enabled, so GPU is ready for use and PM state machine can be
	 * exercised from this point onwards.
	 */
	kbdev->pm.backend.gpu_ready = true;

	/* Turn on the GPU and any cores needed by the policy */
#if MALI_USE_CSF
	/* Turn on the L2 caches, needed for firmware boot */
	spin_lock_irqsave(&kbdev->hwaccess_lock, irq_flags);
	kbdev->pm.backend.l2_desired = true;
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, irq_flags);
#endif
	kbase_pm_do_poweron(kbdev, false);
	kbase_pm_unlock(kbdev);

	return 0;
}

void kbase_hwaccess_pm_halt(struct kbase_device *kbdev)
{
	KBASE_DEBUG_ASSERT(kbdev != NULL);

#if MALI_USE_CSF && defined(KBASE_PM_RUNTIME)
	WARN_ON(kbase_pm_do_poweroff_sync(kbdev));
#else
	mutex_lock(&kbdev->pm.lock);
	kbase_pm_do_poweroff(kbdev);
	mutex_unlock(&kbdev->pm.lock);

	kbase_pm_wait_for_poweroff_work_complete(kbdev);
#endif
}

KBASE_EXPORT_TEST_API(kbase_hwaccess_pm_halt);

void kbase_hwaccess_pm_term(struct kbase_device *kbdev)
{
	KBASE_DEBUG_ASSERT(kbdev != NULL);
	KBASE_DEBUG_ASSERT(kbdev->pm.active_count == 0);
	KBASE_DEBUG_ASSERT(kbdev->pm.backend.gpu_cycle_counter_requests == 0);

	cancel_work_sync(&kbdev->pm.backend.hwcnt_disable_work);

	if (kbdev->pm.backend.hwcnt_disabled) {
		unsigned long flags;
#if MALI_USE_CSF
		kbase_csf_scheduler_spin_lock(kbdev, &flags);
		kbase_hwcnt_context_enable(kbdev->hwcnt_gpu_ctx);
		kbase_csf_scheduler_spin_unlock(kbdev, flags);
#else
		spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
		kbase_hwcnt_context_enable(kbdev->hwcnt_gpu_ctx);
		spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
#endif
	}

	/* Free any resources the policy allocated */
	kbase_pm_state_machine_term(kbdev);
	kbase_pm_policy_term(kbdev);
	kbase_pm_ca_term(kbdev);

#if !MALI_USE_CSF
	/* Shut down the metrics subsystem */
	kbasep_pm_metrics_term(kbdev);
#else
	if (WARN_ON(mutex_is_locked(&kbdev->pm.backend.policy_change_lock))) {
		mutex_lock(&kbdev->pm.backend.policy_change_lock);
		mutex_unlock(&kbdev->pm.backend.policy_change_lock);
	}
	mutex_destroy(&kbdev->pm.backend.policy_change_lock);
#endif

	destroy_workqueue(kbdev->pm.backend.gpu_poweroff_wait_wq);
}

void kbase_pm_power_changed(struct kbase_device *kbdev)
{
	unsigned long flags;

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	kbase_pm_update_state(kbdev);

#if !MALI_USE_CSF
	kbase_backend_slot_update(kbdev);
#endif /* !MALI_USE_CSF */

	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
}

#if MALI_USE_CSF
void kbase_pm_set_debug_core_mask(struct kbase_device *kbdev, u64 new_core_mask)
{
	lockdep_assert_held(&kbdev->hwaccess_lock);
	lockdep_assert_held(&kbdev->pm.lock);

	kbdev->pm.debug_core_mask = new_core_mask;
	kbase_pm_update_dynamic_cores_onoff(kbdev);
}
KBASE_EXPORT_TEST_API(kbase_pm_set_debug_core_mask);
#else
void kbase_pm_set_debug_core_mask(struct kbase_device *kbdev,
		u64 new_core_mask_js0, u64 new_core_mask_js1,
		u64 new_core_mask_js2)
{
	lockdep_assert_held(&kbdev->hwaccess_lock);
	lockdep_assert_held(&kbdev->pm.lock);

	if (kbase_dummy_job_wa_enabled(kbdev)) {
		dev_warn_once(kbdev->dev, "Change of core mask not supported for slot 0 as dummy job WA is enabled");
		new_core_mask_js0 = kbdev->pm.debug_core_mask[0];
	}

	kbdev->pm.debug_core_mask[0] = new_core_mask_js0;
	kbdev->pm.debug_core_mask[1] = new_core_mask_js1;
	kbdev->pm.debug_core_mask[2] = new_core_mask_js2;
	kbdev->pm.debug_core_mask_all = new_core_mask_js0 | new_core_mask_js1 |
			new_core_mask_js2;

	kbase_pm_update_dynamic_cores_onoff(kbdev);
}
#endif /* MALI_USE_CSF */

void kbase_hwaccess_pm_gpu_active(struct kbase_device *kbdev)
{
	kbase_pm_update_active(kbdev);
}

void kbase_hwaccess_pm_gpu_idle(struct kbase_device *kbdev)
{
	kbase_pm_update_active(kbdev);
}

int kbase_hwaccess_pm_suspend(struct kbase_device *kbdev)
{
	int ret = 0;

#if MALI_USE_CSF && defined(KBASE_PM_RUNTIME)
	ret = kbase_pm_do_poweroff_sync(kbdev);
	if (ret)
		return ret;
#else
	/* Force power off the GPU and all cores (regardless of policy), only
	 * after the PM active count reaches zero (otherwise, we risk turning it
	 * off prematurely)
	 */
	kbase_pm_lock(kbdev);

	kbase_pm_do_poweroff(kbdev);

#if !MALI_USE_CSF
	kbase_backend_timer_suspend(kbdev);
#endif /* !MALI_USE_CSF */

	kbase_pm_unlock(kbdev);

	kbase_pm_wait_for_poweroff_work_complete(kbdev);
#endif

	WARN_ON(kbdev->pm.backend.gpu_powered);
	WARN_ON(atomic_read(&kbdev->faults_pending));

	if (kbdev->pm.backend.callback_power_suspend)
		kbdev->pm.backend.callback_power_suspend(kbdev);

	return ret;
}

void kbase_hwaccess_pm_resume(struct kbase_device *kbdev)
{
	kbase_pm_lock(kbdev);

	kbdev->pm.suspending = false;
#ifdef CONFIG_MALI_ARBITER_SUPPORT
	if (kbase_pm_is_gpu_lost(kbdev)) {
		dev_dbg(kbdev->dev, "%s: GPU lost in progress\n", __func__);
		kbase_pm_unlock(kbdev);
		return;
	}
#endif
	kbase_pm_do_poweron(kbdev, true);

#if !MALI_USE_CSF
	kbase_backend_timer_resume(kbdev);
#endif /* !MALI_USE_CSF */

	wake_up_all(&kbdev->pm.resume_wait);
	kbase_pm_unlock(kbdev);
}

#ifdef CONFIG_MALI_ARBITER_SUPPORT
void kbase_pm_handle_gpu_lost(struct kbase_device *kbdev)
{
	unsigned long flags;
	ktime_t end_timestamp = ktime_get_raw();
	struct kbase_arbiter_vm_state *arb_vm_state = kbdev->pm.arb_vm_state;

	if (!kbdev->arb.arb_if)
		return;

	mutex_lock(&kbdev->pm.lock);
	mutex_lock(&arb_vm_state->vm_state_lock);
	if (kbdev->pm.backend.gpu_powered &&
			!kbase_pm_is_gpu_lost(kbdev)) {
		kbase_pm_set_gpu_lost(kbdev, true);

		/* GPU is no longer mapped to VM.  So no interrupts will
		 * be received and Mali registers have been replaced by
		 * dummy RAM
		 */
		WARN(!kbase_is_gpu_removed(kbdev),
			"GPU is still available after GPU lost event\n");

		/* Full GPU reset will have been done by hypervisor, so
		 * cancel
		 */
		atomic_set(&kbdev->hwaccess.backend.reset_gpu,
				KBASE_RESET_GPU_NOT_PENDING);
		hrtimer_cancel(&kbdev->hwaccess.backend.reset_timer);
		kbase_synchronize_irqs(kbdev);

		/* Clear all jobs running on the GPU */
		spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
		kbdev->protected_mode = false;
		kbase_backend_reset(kbdev, &end_timestamp);
		kbase_pm_metrics_update(kbdev, NULL);
		kbase_pm_update_state(kbdev);
		spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

		/* Cancel any pending HWC dumps */
		spin_lock_irqsave(&kbdev->hwcnt.lock, flags);
		if (kbdev->hwcnt.backend.state == KBASE_INSTR_STATE_DUMPING ||
				kbdev->hwcnt.backend.state == KBASE_INSTR_STATE_FAULT) {
			kbdev->hwcnt.backend.state = KBASE_INSTR_STATE_FAULT;
			kbdev->hwcnt.backend.triggered = 1;
			wake_up(&kbdev->hwcnt.backend.wait);
		}
		spin_unlock_irqrestore(&kbdev->hwcnt.lock, flags);
	}
	mutex_unlock(&arb_vm_state->vm_state_lock);
	mutex_unlock(&kbdev->pm.lock);
}

#endif /* CONFIG_MALI_ARBITER_SUPPORT */

#if MALI_USE_CSF && defined(KBASE_PM_RUNTIME)
int kbase_pm_force_mcu_wakeup_after_sleep(struct kbase_device *kbdev)
{
	unsigned long flags;

	lockdep_assert_held(&kbdev->pm.lock);

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	/* Set the override flag to force the power up of L2 cache */
	kbdev->pm.backend.gpu_wakeup_override = true;
	kbase_pm_update_state(kbdev);
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	return kbase_pm_wait_for_desired_state(kbdev);
}

static int pm_handle_mcu_sleep_on_runtime_suspend(struct kbase_device *kbdev)
{
	unsigned long flags;
	int ret;

	lockdep_assert_held(&kbdev->csf.scheduler.lock);
	lockdep_assert_held(&kbdev->pm.lock);

#ifdef CONFIG_MALI_BIFROST_DEBUG
	/* In case of no active CSG on slot, powering up L2 could be skipped and
	 * proceed directly to suspend GPU.
	 * ToDo: firmware has to be reloaded after wake-up as no halt command
	 * has been sent when GPU was put to sleep mode.
	 */
	if (!kbase_csf_scheduler_get_nr_active_csgs(kbdev))
		dev_info(
			kbdev->dev,
			"No active CSGs. Can skip the power up of L2 and go for suspension directly");
#endif

	ret = kbase_pm_force_mcu_wakeup_after_sleep(kbdev);
	if (ret) {
		spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
		dev_warn(
			kbdev->dev,
			"Waiting for MCU to wake up failed on runtime suspend");
		kbdev->pm.backend.gpu_wakeup_override = false;
		spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
		return ret;
	}

	/* Check if a Doorbell mirror interrupt occurred meanwhile */
	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	if (kbdev->pm.backend.gpu_sleep_mode_active &&
	    kbdev->pm.backend.exit_gpu_sleep_mode) {
		dev_dbg(kbdev->dev, "DB mirror interrupt occurred during runtime suspend after L2 power up");
		kbdev->pm.backend.gpu_wakeup_override = false;
		spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
		return -EBUSY;
	}
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
	/* Need to release the kbdev->pm.lock to avoid lock ordering issue
	 * with kctx->reg.lock, which is taken if the sync wait condition is
	 * evaluated after the CSG suspend operation.
	 */
	kbase_pm_unlock(kbdev);
	ret = kbase_csf_scheduler_handle_runtime_suspend(kbdev);
	kbase_pm_lock(kbdev);

	/* Power down L2 cache */
	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	kbdev->pm.backend.gpu_wakeup_override = false;
	kbase_pm_update_state(kbdev);
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	/* After re-acquiring the kbdev->pm.lock, check if the device
	 * became active (or active then idle) meanwhile.
	 */
	if (kbdev->pm.active_count ||
	    kbdev->pm.backend.poweroff_wait_in_progress) {
		dev_dbg(kbdev->dev,
			"Device became active on runtime suspend after suspending Scheduler");
		ret = -EBUSY;
	}

	if (ret)
		return ret;

	ret = kbase_pm_wait_for_desired_state(kbdev);
	if (ret)
		dev_warn(kbdev->dev, "Wait for power down failed on runtime suspend");

	return ret;
}

int kbase_pm_handle_runtime_suspend(struct kbase_device *kbdev)
{
	enum kbase_mcu_state mcu_state;
	bool exit_early = false;
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	/* This check is needed for the case where Kbase had invoked the
	 * @power_off_callback directly.
	 */
	if (!kbdev->pm.backend.gpu_powered) {
		dev_dbg(kbdev->dev, "GPU already powered down on runtime suspend");
		exit_early = true;
	}
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	if (exit_early)
		goto out;

	ret = kbase_reset_gpu_try_prevent(kbdev);
	if (ret == -ENOMEM) {
		dev_dbg(kbdev->dev, "Quit runtime suspend as GPU is in bad state");
		/* Finish the runtime suspend, no point in trying again as GPU is
		 * in irrecoverable bad state.
		 */
		goto out;
	} else if (ret) {
		dev_dbg(kbdev->dev, "Quit runtime suspend for failing to prevent gpu reset");
		ret = -EBUSY;
		goto out;
	}

	kbase_csf_scheduler_lock(kbdev);
	kbase_pm_lock(kbdev);

	/*
	 * This is to handle the case where GPU device becomes active and idle
	 * very quickly whilst the runtime suspend callback is executing.
	 * This is useful for the following scenario :-
	 * - GPU goes idle and pm_callback_runtime_gpu_idle() is called.
	 * - Auto-suspend timer expires and kbase_device_runtime_suspend()
	 *   is called.
	 * - GPU becomes active and pm_callback_runtime_gpu_active() calls
	 *   pm_runtime_get().
	 * - Shortly after that GPU becomes idle again.
	 * - kbase_pm_handle_runtime_suspend() gets called.
	 * - pm_callback_runtime_gpu_idle() is called.
	 *
	 * We do not want to power down the GPU immediately after it goes idle.
	 * So if we notice that GPU had become active when the runtime suspend
	 * had already kicked in, we abort the runtime suspend.
	 * By aborting the runtime suspend, we defer the power down of GPU.
	 *
	 * This check also helps prevent warnings regarding L2 and MCU states
	 * inside the pm_handle_power_off() function. The warning stems from
	 * the fact that pm.lock is released before invoking Scheduler function
	 * to suspend the CSGs.
	 */
	if (kbdev->pm.active_count ||
	    kbdev->pm.backend.poweroff_wait_in_progress) {
		dev_dbg(kbdev->dev, "Device became active on runtime suspend");
		ret = -EBUSY;
		goto unlock;
	}

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	if (kbdev->pm.backend.gpu_sleep_mode_active &&
	    kbdev->pm.backend.exit_gpu_sleep_mode) {
		dev_dbg(kbdev->dev, "DB mirror interrupt occurred during runtime suspend before L2 power up");
		ret = -EBUSY;
		spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
		goto unlock;
	}

	mcu_state = kbdev->pm.backend.mcu_state;
	WARN_ON(!kbase_pm_is_mcu_inactive(kbdev, mcu_state));
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	if (mcu_state == KBASE_MCU_IN_SLEEP) {
		ret = pm_handle_mcu_sleep_on_runtime_suspend(kbdev);
		if (ret)
			goto unlock;
	}

	/* Disable interrupts and turn off the GPU clocks */
	if (!kbase_pm_clock_off(kbdev)) {
		dev_warn(kbdev->dev, "Failed to turn off GPU clocks on runtime suspend, MMU faults pending");

		WARN_ON(!kbdev->poweroff_pending);
		/* Previous call to kbase_pm_clock_off() would have disabled
		 * the interrupts and also synchronized with the interrupt
		 * handlers, so more fault work items can't be enqueued.
		 *
		 * Can't wait for the completion of MMU fault work items as
		 * there is a possibility of a deadlock since the fault work
		 * items would do the group termination which requires the
		 * Scheduler lock.
		 */
		ret = -EBUSY;
		goto unlock;
	}

	wake_up(&kbdev->pm.backend.poweroff_wait);
	WARN_ON(kbdev->pm.backend.gpu_powered);
	dev_dbg(kbdev->dev, "GPU power down complete");

unlock:
	kbase_pm_unlock(kbdev);
	kbase_csf_scheduler_unlock(kbdev);
	kbase_reset_gpu_allow(kbdev);
out:
	if (ret) {
		ret = -EBUSY;
		pm_runtime_mark_last_busy(kbdev->dev);
	}

	return ret;
}
#endif
