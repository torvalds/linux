/*
 *
 * (C) COPYRIGHT 2013-2018 ARM Limited. All rights reserved.
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
 * Base kernel core availability APIs
 */

#include <mali_kbase.h>
#include <mali_kbase_pm.h>
#include <backend/gpu/mali_kbase_pm_internal.h>
#ifdef MALI_BIFROST_NO_MALI
#include <backend/gpu/mali_kbase_model_dummy.h>
#endif

int kbase_pm_ca_init(struct kbase_device *kbdev)
{
#ifdef CONFIG_MALI_BIFROST_DEVFREQ
	struct kbase_pm_backend_data *pm_backend = &kbdev->pm.backend;

	if (kbdev->current_core_mask)
		pm_backend->ca_cores_enabled = kbdev->current_core_mask;
	else
		pm_backend->ca_cores_enabled =
				kbdev->gpu_props.props.raw_props.shader_present;
#endif

	return 0;
}

void kbase_pm_ca_term(struct kbase_device *kbdev)
{
}

#ifdef CONFIG_MALI_BIFROST_DEVFREQ
void kbase_devfreq_set_core_mask(struct kbase_device *kbdev, u64 core_mask)
{
	struct kbase_pm_backend_data *pm_backend = &kbdev->pm.backend;
	unsigned long flags;

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

	if (!(core_mask & kbdev->pm.debug_core_mask_all)) {
		dev_err(kbdev->dev, "OPP core mask 0x%llX does not intersect with debug mask 0x%llX\n",
				core_mask, kbdev->pm.debug_core_mask_all);
		goto unlock;
	}

	pm_backend->ca_cores_enabled = core_mask;

	kbase_pm_update_state(kbdev);

unlock:
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	dev_dbg(kbdev->dev, "Devfreq policy : new core mask=%llX\n",
			pm_backend->ca_cores_enabled);
}
#endif

u64 kbase_pm_ca_get_core_mask(struct kbase_device *kbdev)
{
#ifdef CONFIG_MALI_BIFROST_DEVFREQ
	struct kbase_pm_backend_data *pm_backend = &kbdev->pm.backend;
#endif

	lockdep_assert_held(&kbdev->hwaccess_lock);

#ifdef CONFIG_MALI_BIFROST_DEVFREQ
	return pm_backend->ca_cores_enabled & kbdev->pm.debug_core_mask_all;
#else
	return kbdev->gpu_props.props.raw_props.shader_present &
			kbdev->pm.debug_core_mask_all;
#endif
}

KBASE_EXPORT_TEST_API(kbase_pm_ca_get_core_mask);

u64 kbase_pm_ca_get_instr_core_mask(struct kbase_device *kbdev)
{
	lockdep_assert_held(&kbdev->hwaccess_lock);

#ifdef CONFIG_MALI_BIFROST_NO_MALI
	return (((1ull) << KBASE_DUMMY_MODEL_MAX_SHADER_CORES) - 1);
#else
	return kbdev->pm.backend.pm_shaders_core_mask;
#endif
}
