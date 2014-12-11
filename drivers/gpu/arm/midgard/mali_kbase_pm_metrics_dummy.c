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
 * @file mali_kbase_pm_metrics_dummy.c
 * Dummy Metrics for power management.
 */

#include <mali_kbase.h>
#include <mali_kbase_pm.h>

#if KBASE_PM_EN
void kbase_pm_register_vsync_callback(struct kbase_device *kbdev)
{
	KBASE_DEBUG_ASSERT(kbdev != NULL);

	/* no VSync metrics will be available */
	kbdev->pm.metrics.platform_data = NULL;
}

void kbase_pm_unregister_vsync_callback(struct kbase_device *kbdev)
{
	KBASE_DEBUG_ASSERT(kbdev != NULL);
}
#endif  /* KBASE_PM_EN */
