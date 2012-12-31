/*
 *
 * (C) COPYRIGHT 2011 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 * 
 */



/**
 * @file mali_kbase_pm_metrics.c
 * Metrics for power management
 */

#include <osk/mali_osk.h>

#include <kbase/src/common/mali_kbase.h>
#include <kbase/src/common/mali_kbase_pm.h>
#ifdef CONFIG_VITHAR_DVFS
#include <kbase/src/platform/mali_kbase_dvfs.h>
#endif

/* When VSync is being hit aim for utilisation between 70-90% */
#define KBASE_PM_VSYNC_MIN_UTILISATION          70
#define KBASE_PM_VSYNC_MAX_UTILISATION          90
/* Otherwise aim for 10-40% */
#define KBASE_PM_NO_VSYNC_MIN_UTILISATION       10
#define KBASE_PM_NO_VSYNC_MAX_UTILISATION       40

#ifndef CONFIG_VITHAR_DVFS
/* Frequency that DVFS clock frequency decisions should be made */
#define KBASE_PM_DVFS_FREQUENCY                 500
#endif

static void dvfs_callback(void *data)
{
	kbase_device *kbdev;
	kbase_pm_dvfs_action action;
	osk_error ret;

	OSK_ASSERT(data != NULL);

	kbdev = (kbase_device*)data;
#ifdef CONFIG_VITHAR_DVFS
	CSTD_UNUSED(action);
	kbase_platform_dvfs_event(kbdev, kbase_pm_get_dvfs_utilisation(kbdev));
#else
	action = kbase_pm_get_dvfs_action(kbdev);

	switch(action) {
		case KBASE_PM_DVFS_NOP:
			break;
		case KBASE_PM_DVFS_CLOCK_UP:
			/* Do whatever is required to increase the clock frequency */
			break;
		case KBASE_PM_DVFS_CLOCK_DOWN:
			/* Do whatever is required to decrease the clock frequency */
			break;
	}
#endif

	osk_spinlock_irq_lock(&kbdev->pm.metrics.lock);
	if (kbdev->pm.metrics.timer_active)
	{
		ret = osk_timer_start(&kbdev->pm.metrics.timer, KBASE_PM_DVFS_FREQUENCY);
		if (ret != OSK_ERR_NONE)
		{
			/* Handle the situation where the timer cannot be restarted */
		}
	}
	osk_spinlock_irq_unlock(&kbdev->pm.metrics.lock);
}

mali_error kbasep_pm_metrics_init(kbase_device *kbdev)
{
	osk_error osk_err;
	mali_error ret;

	OSK_ASSERT(kbdev != NULL);

	kbdev->pm.metrics.vsync_hit = 0;
	kbdev->pm.metrics.utilisation = 0;

	kbdev->pm.metrics.time_period_start = osk_time_now();
	kbdev->pm.metrics.time_busy = 0;
	kbdev->pm.metrics.time_idle = 0;
	kbdev->pm.metrics.gpu_active = MALI_TRUE;
	kbdev->pm.metrics.timer_active = MALI_TRUE;
#ifdef CONFIG_VITHAR_RT_PM
	kbdev->pm.cmu_pmu_status = 0;
#endif

	osk_err = osk_spinlock_irq_init(&kbdev->pm.metrics.lock, OSK_LOCK_ORDER_PM_METRICS);
	if (OSK_ERR_NONE != osk_err)
	{
		ret = MALI_ERROR_FUNCTION_FAILED;
		goto out;
	}

	osk_err = osk_timer_init(&kbdev->pm.metrics.timer);
	if (OSK_ERR_NONE != osk_err)
	{
		ret = MALI_ERROR_FUNCTION_FAILED;
		goto spinlock_free;
	}
	osk_timer_callback_set(&kbdev->pm.metrics.timer, dvfs_callback, kbdev);
	osk_err = osk_timer_start(&kbdev->pm.metrics.timer, KBASE_PM_DVFS_FREQUENCY);
	if (OSK_ERR_NONE != osk_err)
	{
		ret = MALI_ERROR_FUNCTION_FAILED;
		goto timer_free;
	}

	kbase_pm_register_vsync_callback(kbdev);
	ret = MALI_ERROR_NONE;
	goto out;

timer_free:
	osk_timer_stop(&kbdev->pm.metrics.timer);
	osk_timer_term(&kbdev->pm.metrics.timer);
spinlock_free:
	osk_spinlock_irq_term(&kbdev->pm.metrics.lock);
out:
	return ret;
}

void kbasep_pm_metrics_term(kbase_device *kbdev)
{
	OSK_ASSERT(kbdev != NULL);

	osk_spinlock_irq_lock(&kbdev->pm.metrics.lock);
	kbdev->pm.metrics.timer_active = MALI_FALSE;
	osk_spinlock_irq_unlock(&kbdev->pm.metrics.lock);

	osk_timer_stop(&kbdev->pm.metrics.timer);
	osk_timer_term(&kbdev->pm.metrics.timer);

	kbase_pm_unregister_vsync_callback(kbdev);

	osk_spinlock_irq_term(&kbdev->pm.metrics.lock);
}

void kbasep_pm_record_gpu_idle(kbase_device *kbdev)
{
	osk_ticks now = osk_time_now();

	OSK_ASSERT(kbdev != NULL);

	osk_spinlock_irq_lock(&kbdev->pm.metrics.lock);

	OSK_ASSERT(kbdev->pm.metrics.gpu_active == MALI_TRUE);

	kbdev->pm.metrics.gpu_active = MALI_FALSE;

	kbdev->pm.metrics.time_busy += osk_time_elapsed(kbdev->pm.metrics.time_period_start, now);
	kbdev->pm.metrics.time_period_start = now;

	osk_spinlock_irq_unlock(&kbdev->pm.metrics.lock);
}

void kbasep_pm_record_gpu_active(kbase_device *kbdev)
{
	osk_ticks now = osk_time_now();

	OSK_ASSERT(kbdev != NULL);

	osk_spinlock_irq_lock(&kbdev->pm.metrics.lock);

	OSK_ASSERT(kbdev->pm.metrics.gpu_active == MALI_FALSE);

	kbdev->pm.metrics.gpu_active = MALI_TRUE;

	kbdev->pm.metrics.time_idle += osk_time_elapsed(kbdev->pm.metrics.time_period_start, now);
	kbdev->pm.metrics.time_period_start = now;

	osk_spinlock_irq_unlock(&kbdev->pm.metrics.lock);
}

void kbase_pm_report_vsync(kbase_device *kbdev, int buffer_updated)
{
	OSK_ASSERT(kbdev != NULL);

	osk_spinlock_irq_lock(&kbdev->pm.metrics.lock);
	kbdev->pm.metrics.vsync_hit = buffer_updated;
	osk_spinlock_irq_unlock(&kbdev->pm.metrics.lock);
}

kbase_pm_dvfs_action kbase_pm_get_dvfs_action(kbase_device *kbdev)
{
	int utilisation;
	kbase_pm_dvfs_action action;
	osk_ticks now = osk_time_now();

	OSK_ASSERT(kbdev != NULL);

	osk_spinlock_irq_lock(&kbdev->pm.metrics.lock);

	if (kbdev->pm.metrics.gpu_active)
	{
		kbdev->pm.metrics.time_busy += osk_time_elapsed(kbdev->pm.metrics.time_period_start, now);
		kbdev->pm.metrics.time_period_start = now;
	}
	else
	{
		kbdev->pm.metrics.time_idle += osk_time_elapsed(kbdev->pm.metrics.time_period_start, now);
		kbdev->pm.metrics.time_period_start = now;
	}

	if (kbdev->pm.metrics.time_idle + kbdev->pm.metrics.time_busy == 0)
	{
		/* No data - so we return NOP */
		action = KBASE_PM_DVFS_NOP;
		goto out;
	}

	utilisation = (100*kbdev->pm.metrics.time_busy) / (kbdev->pm.metrics.time_idle + kbdev->pm.metrics.time_busy);

	if (kbdev->pm.metrics.vsync_hit)
	{
		/* VSync is being met */
		if (utilisation < KBASE_PM_VSYNC_MIN_UTILISATION)
		{
			action = KBASE_PM_DVFS_CLOCK_DOWN;
		}
		else if (utilisation > KBASE_PM_VSYNC_MAX_UTILISATION)
		{
			action = KBASE_PM_DVFS_CLOCK_UP;
		}
		else
		{
			action = KBASE_PM_DVFS_NOP;
		}
	}
	else
	{
		/* VSync is being missed */
		if (utilisation < KBASE_PM_NO_VSYNC_MIN_UTILISATION)
		{
			action = KBASE_PM_DVFS_CLOCK_DOWN;
		}
		else if (utilisation > KBASE_PM_NO_VSYNC_MAX_UTILISATION)
		{
			action = KBASE_PM_DVFS_CLOCK_UP;
		}
		else
		{
			action = KBASE_PM_DVFS_NOP;
		}
	}

out:

	kbdev->pm.metrics.time_idle = 0;
	kbdev->pm.metrics.time_busy = 0;

	osk_spinlock_irq_unlock(&kbdev->pm.metrics.lock);

	return action;
}

#ifdef CONFIG_VITHAR_DVFS
int kbase_pm_get_dvfs_utilisation(kbase_device *kbdev)
{
	int utilisation=0;
	osk_ticks now = osk_time_now();

	OSK_ASSERT(kbdev != NULL);

	osk_spinlock_irq_lock(&kbdev->pm.metrics.lock);

	if (kbdev->pm.metrics.gpu_active)
	{
		kbdev->pm.metrics.time_busy += osk_time_elapsed(kbdev->pm.metrics.time_period_start, now);
		kbdev->pm.metrics.time_period_start = now;
	}
	else
	{
		kbdev->pm.metrics.time_idle += osk_time_elapsed(kbdev->pm.metrics.time_period_start, now);
		kbdev->pm.metrics.time_period_start = now;
	}

	if (kbdev->pm.metrics.time_idle + kbdev->pm.metrics.time_busy == 0)
	{
		/* No data - so we return NOP */
		goto out;
	}

	utilisation = (100*kbdev->pm.metrics.time_busy) / (kbdev->pm.metrics.time_idle + kbdev->pm.metrics.time_busy);

out:

	kbdev->pm.metrics.time_idle = 0;
	kbdev->pm.metrics.time_busy = 0;

	osk_spinlock_irq_unlock(&kbdev->pm.metrics.lock);

	return utilisation;
}
#endif
