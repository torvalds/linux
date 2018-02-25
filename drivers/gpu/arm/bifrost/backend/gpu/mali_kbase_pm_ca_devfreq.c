/*
 *
 * (C) COPYRIGHT 2017 ARM Limited. All rights reserved.
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
 * A core availability policy implementing core mask selection from devfreq OPPs
 *
 */

#include <mali_kbase.h>
#include <mali_kbase_pm.h>
#include <backend/gpu/mali_kbase_pm_internal.h>
#include <linux/version.h>

void kbase_devfreq_set_core_mask(struct kbase_device *kbdev, u64 core_mask)
{
	struct kbasep_pm_ca_policy_devfreq *data =
				&kbdev->pm.backend.ca_policy_data.devfreq;
	unsigned long flags;

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

	data->cores_desired = core_mask;

	/* Disable any cores that are now unwanted */
	data->cores_enabled &= data->cores_desired;

	kbdev->pm.backend.ca_in_transition = true;

	/* If there are no cores to be powered off then power on desired cores
	 */
	if (!(data->cores_used & ~data->cores_desired)) {
		data->cores_enabled = data->cores_desired;
		kbdev->pm.backend.ca_in_transition = false;
	}

	kbase_pm_update_cores_state_nolock(kbdev);

	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	dev_dbg(kbdev->dev, "Devfreq policy : new core mask=%llX %llX\n",
				data->cores_desired, data->cores_enabled);
}

static void devfreq_init(struct kbase_device *kbdev)
{
	struct kbasep_pm_ca_policy_devfreq *data =
				&kbdev->pm.backend.ca_policy_data.devfreq;

	if (kbdev->current_core_mask) {
		data->cores_enabled = kbdev->current_core_mask;
		data->cores_desired = kbdev->current_core_mask;
	} else {
		data->cores_enabled =
				kbdev->gpu_props.props.raw_props.shader_present;
		data->cores_desired =
				kbdev->gpu_props.props.raw_props.shader_present;
	}
	data->cores_used = 0;
	kbdev->pm.backend.ca_in_transition = false;
}

static void devfreq_term(struct kbase_device *kbdev)
{
}

static u64 devfreq_get_core_mask(struct kbase_device *kbdev)
{
	return kbdev->pm.backend.ca_policy_data.devfreq.cores_enabled;
}

static void devfreq_update_core_status(struct kbase_device *kbdev,
							u64 cores_ready,
							u64 cores_transitioning)
{
	struct kbasep_pm_ca_policy_devfreq *data =
				&kbdev->pm.backend.ca_policy_data.devfreq;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	data->cores_used = cores_ready | cores_transitioning;

	/* If in desired state then clear transition flag */
	if (data->cores_enabled == data->cores_desired)
		kbdev->pm.backend.ca_in_transition = false;

	/* If all undesired cores are now off then power on desired cores.
	 * The direct comparison against cores_enabled limits potential
	 * recursion to one level */
	if (!(data->cores_used & ~data->cores_desired) &&
				data->cores_enabled != data->cores_desired) {
		data->cores_enabled = data->cores_desired;

		kbase_pm_update_cores_state_nolock(kbdev);

		kbdev->pm.backend.ca_in_transition = false;
	}
}

/*
 * The struct kbase_pm_ca_policy structure for the devfreq core availability
 * policy.
 *
 * This is the static structure that defines the devfreq core availability power
 * policy's callback and name.
 */
const struct kbase_pm_ca_policy kbase_pm_ca_devfreq_policy_ops = {
	"devfreq",			/* name */
	devfreq_init,			/* init */
	devfreq_term,			/* term */
	devfreq_get_core_mask,		/* get_core_mask */
	devfreq_update_core_status,	/* update_core_status */
	0u,				/* flags */
	KBASE_PM_CA_POLICY_ID_DEVFREQ,	/* id */
};

