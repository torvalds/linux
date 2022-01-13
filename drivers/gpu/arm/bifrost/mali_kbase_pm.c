// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2010-2021 ARM Limited. All rights reserved.
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

/**
 * DOC: Base kernel power management APIs
 */

#include <mali_kbase.h>
#include <gpu/mali_kbase_gpu_regmap.h>
#include <mali_kbase_vinstr.h>
#include <mali_kbase_kinstr_prfcnt.h>
#include <mali_kbase_hwcnt_context.h>

#include <mali_kbase_pm.h>
#include <backend/gpu/mali_kbase_pm_internal.h>

#ifdef CONFIG_MALI_ARBITER_SUPPORT
#include <arbiter/mali_kbase_arbiter_pm.h>
#endif /* CONFIG_MALI_ARBITER_SUPPORT */

#include <backend/gpu/mali_kbase_clk_rate_trace_mgr.h>

int kbase_pm_powerup(struct kbase_device *kbdev, unsigned int flags)
{
	return kbase_hwaccess_pm_powerup(kbdev, flags);
}

void kbase_pm_halt(struct kbase_device *kbdev)
{
	kbase_hwaccess_pm_halt(kbdev);
}

void kbase_pm_context_active(struct kbase_device *kbdev)
{
	(void)kbase_pm_context_active_handle_suspend(kbdev,
		KBASE_PM_SUSPEND_HANDLER_NOT_POSSIBLE);
}

int kbase_pm_context_active_handle_suspend(struct kbase_device *kbdev,
	enum kbase_pm_suspend_handler suspend_handler)
{
	int c;

	KBASE_DEBUG_ASSERT(kbdev != NULL);
	dev_dbg(kbdev->dev, "%s - reason = %d, pid = %d\n", __func__,
		suspend_handler, current->pid);
	kbase_pm_lock(kbdev);

#ifdef CONFIG_MALI_ARBITER_SUPPORT
	if (kbase_arbiter_pm_ctx_active_handle_suspend(kbdev,
			suspend_handler)) {
		kbase_pm_unlock(kbdev);
		return 1;
	}
#endif /* CONFIG_MALI_ARBITER_SUPPORT */

	if (kbase_pm_is_suspending(kbdev)) {
		switch (suspend_handler) {
		case KBASE_PM_SUSPEND_HANDLER_DONT_REACTIVATE:
			if (kbdev->pm.active_count != 0)
				break;
			fallthrough;
		case KBASE_PM_SUSPEND_HANDLER_DONT_INCREASE:
			kbase_pm_unlock(kbdev);
			return 1;

		case KBASE_PM_SUSPEND_HANDLER_NOT_POSSIBLE:
			fallthrough;
		default:
			KBASE_DEBUG_ASSERT_MSG(false, "unreachable");
			break;
		}
	}
	c = ++kbdev->pm.active_count;
	KBASE_KTRACE_ADD(kbdev, PM_CONTEXT_ACTIVE, NULL, c);

	if (c == 1) {
		/* First context active: Power on the GPU and
		 * any cores requested by the policy
		 */
		kbase_hwaccess_pm_gpu_active(kbdev);
#ifdef CONFIG_MALI_ARBITER_SUPPORT
		kbase_arbiter_pm_vm_event(kbdev, KBASE_VM_REF_EVENT);
#endif /* CONFIG_MALI_ARBITER_SUPPORT */
		kbase_clk_rate_trace_manager_gpu_active(kbdev);
	}

	kbase_pm_unlock(kbdev);
	dev_dbg(kbdev->dev, "%s %d\n", __func__, kbdev->pm.active_count);

	return 0;
}

KBASE_EXPORT_TEST_API(kbase_pm_context_active);

void kbase_pm_context_idle(struct kbase_device *kbdev)
{
	int c;

	KBASE_DEBUG_ASSERT(kbdev != NULL);


	kbase_pm_lock(kbdev);

	c = --kbdev->pm.active_count;
	KBASE_KTRACE_ADD(kbdev, PM_CONTEXT_IDLE, NULL, c);

	KBASE_DEBUG_ASSERT(c >= 0);

	if (c == 0) {
		/* Last context has gone idle */
		kbase_hwaccess_pm_gpu_idle(kbdev);
		kbase_clk_rate_trace_manager_gpu_idle(kbdev);

		/* Wake up anyone waiting for this to become 0 (e.g. suspend).
		 * The waiters must synchronize with us by locking the pm.lock
		 * after waiting.
		 */
		wake_up(&kbdev->pm.zero_active_count_wait);
	}

	kbase_pm_unlock(kbdev);
	dev_dbg(kbdev->dev, "%s %d (pid = %d)\n", __func__,
		kbdev->pm.active_count, current->pid);
}

KBASE_EXPORT_TEST_API(kbase_pm_context_idle);

int kbase_pm_driver_suspend(struct kbase_device *kbdev)
{
	KBASE_DEBUG_ASSERT(kbdev);

	/* Suspend HW counter intermediaries. This blocks until workers and timers
	 * are no longer running.
	 */
	kbase_vinstr_suspend(kbdev->vinstr_ctx);
	kbase_kinstr_prfcnt_suspend(kbdev->kinstr_prfcnt_ctx);

	/* Disable GPU hardware counters.
	 * This call will block until counters are disabled.
	 */
	kbase_hwcnt_context_disable(kbdev->hwcnt_gpu_ctx);

	mutex_lock(&kbdev->pm.lock);
	if (WARN_ON(kbase_pm_is_suspending(kbdev))) {
		mutex_unlock(&kbdev->pm.lock);
		return 0;
	}
	kbdev->pm.suspending = true;
	mutex_unlock(&kbdev->pm.lock);

#ifdef CONFIG_MALI_ARBITER_SUPPORT
	if (kbdev->arb.arb_if) {
		int i;
		unsigned long flags;

		spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
		kbdev->js_data.runpool_irq.submit_allowed = 0;
		kbase_disjoint_state_up(kbdev);
		for (i = 0; i < kbdev->gpu_props.num_job_slots; i++)
			kbase_job_slot_softstop(kbdev, i, NULL);
		spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
	}
#endif /* CONFIG_MALI_ARBITER_SUPPORT */

	/* From now on, the active count will drop towards zero. Sometimes,
	 * it'll go up briefly before going down again. However, once
	 * it reaches zero it will stay there - guaranteeing that we've idled
	 * all pm references
	 */

#if !MALI_USE_CSF
	/* Suspend job scheduler and associated components, so that it releases all
	 * the PM active count references
	 */
	kbasep_js_suspend(kbdev);
#else
	if (kbase_csf_scheduler_pm_suspend(kbdev)) {
		mutex_lock(&kbdev->pm.lock);
		kbdev->pm.suspending = false;
		mutex_unlock(&kbdev->pm.lock);
		return -1;
	}
#endif

	/* Wait for the active count to reach zero. This is not the same as
	 * waiting for a power down, since not all policies power down when this
	 * reaches zero.
	 */
	dev_dbg(kbdev->dev, ">wait_event - waiting for active_count == 0 (pid = %d)\n",
		current->pid);
	wait_event(kbdev->pm.zero_active_count_wait,
		kbdev->pm.active_count == 0);
	dev_dbg(kbdev->dev, ">wait_event - waiting done\n");

	/* NOTE: We synchronize with anything that was just finishing a
	 * kbase_pm_context_idle() call by locking the pm.lock below
	 */
	if (kbase_hwaccess_pm_suspend(kbdev)) {
		mutex_lock(&kbdev->pm.lock);
		kbdev->pm.suspending = false;
		mutex_unlock(&kbdev->pm.lock);
		return -1;
	}

#ifdef CONFIG_MALI_ARBITER_SUPPORT
	if (kbdev->arb.arb_if) {
		mutex_lock(&kbdev->pm.arb_vm_state->vm_state_lock);
		kbase_arbiter_pm_vm_stopped(kbdev);
		mutex_unlock(&kbdev->pm.arb_vm_state->vm_state_lock);
	}
#endif /* CONFIG_MALI_ARBITER_SUPPORT */

	return 0;
}

void kbase_pm_driver_resume(struct kbase_device *kbdev, bool arb_gpu_start)
{
	unsigned long flags;

	/* MUST happen before any pm_context_active calls occur */
	kbase_hwaccess_pm_resume(kbdev);

	/* Initial active call, to power on the GPU/cores if needed */
#ifdef CONFIG_MALI_ARBITER_SUPPORT
	if (kbase_pm_context_active_handle_suspend(kbdev,
			(arb_gpu_start ?
				KBASE_PM_SUSPEND_HANDLER_VM_GPU_GRANTED :
				KBASE_PM_SUSPEND_HANDLER_NOT_POSSIBLE)))
		return;
#else
	kbase_pm_context_active(kbdev);
#endif

#if !MALI_USE_CSF
	/* Resume any blocked atoms (which may cause contexts to be scheduled in
	 * and dependent atoms to run)
	 */
	kbase_resume_suspended_soft_jobs(kbdev);

	/* Resume the Job Scheduler and associated components, and start running
	 * atoms
	 */
	kbasep_js_resume(kbdev);
#endif

	/* Matching idle call, to power off the GPU/cores if we didn't actually
	 * need it and the policy doesn't want it on
	 */
	kbase_pm_context_idle(kbdev);

	/* Re-enable GPU hardware counters */
#if MALI_USE_CSF
	kbase_csf_scheduler_spin_lock(kbdev, &flags);
	kbase_hwcnt_context_enable(kbdev->hwcnt_gpu_ctx);
	kbase_csf_scheduler_spin_unlock(kbdev, flags);
#else
	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	kbase_hwcnt_context_enable(kbdev->hwcnt_gpu_ctx);
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
#endif

	/* Resume HW counters intermediaries. */
	kbase_vinstr_resume(kbdev->vinstr_ctx);
	kbase_kinstr_prfcnt_resume(kbdev->kinstr_prfcnt_ctx);
}

int kbase_pm_suspend(struct kbase_device *kbdev)
{
	int result = 0;
#ifdef CONFIG_MALI_ARBITER_SUPPORT
	if (kbdev->arb.arb_if)
		kbase_arbiter_pm_vm_event(kbdev, KBASE_VM_OS_SUSPEND_EVENT);
	else
		result = kbase_pm_driver_suspend(kbdev);
#else
	result = kbase_pm_driver_suspend(kbdev);
#endif /* CONFIG_MALI_ARBITER_SUPPORT */

	return result;
}

void kbase_pm_resume(struct kbase_device *kbdev)
{
#ifdef CONFIG_MALI_ARBITER_SUPPORT
	if (kbdev->arb.arb_if)
		kbase_arbiter_pm_vm_event(kbdev, KBASE_VM_OS_RESUME_EVENT);
	else
		kbase_pm_driver_resume(kbdev, false);
#else
	kbase_pm_driver_resume(kbdev, false);
#endif /* CONFIG_MALI_ARBITER_SUPPORT */
}
