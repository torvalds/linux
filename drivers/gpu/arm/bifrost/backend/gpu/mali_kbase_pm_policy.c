/*
 *
 * (C) COPYRIGHT 2010-2020 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
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
 * SPDX-License-Identifier: GPL-2.0
 *
 */

/*
 * Power policy API implementations
 */

#include <mali_kbase.h>
#include <gpu/mali_kbase_gpu_regmap.h>
#include <mali_kbase_pm.h>
#include <backend/gpu/mali_kbase_pm_internal.h>

static const struct kbase_pm_policy *const all_policy_list[] = {
#ifdef CONFIG_MALI_BIFROST_NO_MALI
	&kbase_pm_always_on_policy_ops,
	&kbase_pm_coarse_demand_policy_ops,
#if !MALI_CUSTOMER_RELEASE
	&kbase_pm_always_on_demand_policy_ops,
#endif
#else				/* CONFIG_MALI_BIFROST_NO_MALI */
	&kbase_pm_coarse_demand_policy_ops,
#if !MALI_CUSTOMER_RELEASE
	&kbase_pm_always_on_demand_policy_ops,
#endif
	&kbase_pm_always_on_policy_ops
#endif /* CONFIG_MALI_BIFROST_NO_MALI */
};

void kbase_pm_policy_init(struct kbase_device *kbdev)
{
	kbdev->pm.backend.pm_current_policy = all_policy_list[0];
	kbdev->pm.backend.pm_current_policy->init(kbdev);
}

void kbase_pm_policy_term(struct kbase_device *kbdev)
{
	kbdev->pm.backend.pm_current_policy->term(kbdev);
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

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

	active = backend->pm_current_policy->get_core_active(kbdev);
	WARN((kbase_pm_is_active(kbdev) && !active),
		"GPU is active but policy '%s' is indicating that it can be powered off",
		kbdev->pm.backend.pm_current_policy->name);

	if (active) {
		/* Power on the GPU and any cores requested by the policy */
		if (!pm->backend.invoke_poweroff_wait_wq_when_l2_off &&
				pm->backend.poweroff_wait_in_progress) {
			KBASE_DEBUG_ASSERT(kbdev->pm.backend.gpu_powered);
			pm->backend.poweron_required = true;
			spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
		} else {
			/* Cancel the invocation of
			 * kbase_pm_gpu_poweroff_wait_wq() from the L2 state
			 * machine. This is safe - it
			 * invoke_poweroff_wait_wq_when_l2_off is true, then
			 * the poweroff work hasn't even been queued yet,
			 * meaning we can go straight to powering on.
			 */
			pm->backend.invoke_poweroff_wait_wq_when_l2_off = false;
			pm->backend.poweroff_wait_in_progress = false;
			pm->backend.l2_desired = true;

			spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
			kbase_pm_do_poweron(kbdev, false);
		}
	} else {
		/* It is an error for the power policy to power off the GPU
		 * when there are contexts active */
		KBASE_DEBUG_ASSERT(pm->active_count == 0);

		pm->backend.poweron_required = false;

		/* Request power off */
		if (pm->backend.gpu_powered) {
			spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

			/* Power off the GPU immediately */
			kbase_pm_do_poweroff(kbdev);
		} else {
			spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
		}
	}
}

void kbase_pm_update_dynamic_cores_onoff(struct kbase_device *kbdev)
{
	bool shaders_desired;

	lockdep_assert_held(&kbdev->hwaccess_lock);
	lockdep_assert_held(&kbdev->pm.lock);

	if (kbdev->pm.backend.pm_current_policy == NULL)
		return;
	if (kbdev->pm.backend.poweroff_wait_in_progress)
		return;
	/* In protected transition, don't allow outside shader core request
	 * affect transition, return directly
	 */
	if (kbdev->pm.backend.protected_transition_override)
		return;

	shaders_desired = kbdev->pm.backend.pm_current_policy->shaders_needed(kbdev);

	if (shaders_desired && kbase_pm_is_l2_desired(kbdev)) {
		kbase_pm_update_state(kbdev);
	}
}

void kbase_pm_update_cores_state_nolock(struct kbase_device *kbdev)
{
	bool shaders_desired;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	if (kbdev->pm.backend.pm_current_policy == NULL)
		return;
	if (kbdev->pm.backend.poweroff_wait_in_progress)
		return;

	if (kbdev->pm.backend.protected_transition_override)
		/* We are trying to change in/out of protected mode - force all
		 * cores off so that the L2 powers down */
		shaders_desired = false;
	else
		shaders_desired = kbdev->pm.backend.pm_current_policy->shaders_needed(kbdev);

	if (kbdev->pm.backend.shaders_desired != shaders_desired) {
		KBASE_KTRACE_ADD(kbdev, PM_CORES_CHANGE_DESIRED, NULL, kbdev->pm.backend.shaders_desired);

		kbdev->pm.backend.shaders_desired = shaders_desired;
		kbase_pm_update_state(kbdev);
	}
}

void kbase_pm_update_cores_state(struct kbase_device *kbdev)
{
	unsigned long flags;

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

	kbase_pm_update_cores_state_nolock(kbdev);

	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
}

int kbase_pm_list_policies(struct kbase_device *kbdev,
	const struct kbase_pm_policy * const **list)
{
	if (list)
		*list = all_policy_list;

	return ARRAY_SIZE(all_policy_list);
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
	const struct kbase_pm_policy *old_policy;
	unsigned long flags;

	KBASE_DEBUG_ASSERT(kbdev != NULL);
	KBASE_DEBUG_ASSERT(new_policy != NULL);

	KBASE_KTRACE_ADD(kbdev, PM_SET_POLICY, NULL, new_policy->id);

	/* During a policy change we pretend the GPU is active */
	/* A suspend won't happen here, because we're in a syscall from a
	 * userspace thread */
	kbase_pm_context_active(kbdev);

	kbase_pm_lock(kbdev);

	/* Remove the policy to prevent IRQ handlers from working on it */
	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	old_policy = kbdev->pm.backend.pm_current_policy;
	kbdev->pm.backend.pm_current_policy = NULL;
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	KBASE_KTRACE_ADD(kbdev, PM_CURRENT_POLICY_TERM, NULL, old_policy->id);
	if (old_policy->term)
		old_policy->term(kbdev);

	KBASE_KTRACE_ADD(kbdev, PM_CURRENT_POLICY_INIT, NULL, new_policy->id);
	if (new_policy->init)
		new_policy->init(kbdev);

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	kbdev->pm.backend.pm_current_policy = new_policy;
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	/* If any core power state changes were previously attempted, but
	 * couldn't be made because the policy was changing (current_policy was
	 * NULL), then re-try them here. */
	kbase_pm_update_active(kbdev);
	kbase_pm_update_cores_state(kbdev);

	kbase_pm_unlock(kbdev);

	/* Now the policy change is finished, we release our fake context active
	 * reference */
	kbase_pm_context_idle(kbdev);
}

KBASE_EXPORT_TEST_API(kbase_pm_set_policy);
