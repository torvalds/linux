/*
 *
 * (C) COPYRIGHT 2012 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 */



/**
 * @file mali_kbase_pm_coarse_demand.c
 * "Coarse Demand" power management policy
 */

#include <osk/mali_osk.h>
#include <kbase/src/common/mali_kbase.h>
#include <kbase/src/common/mali_kbase_pm.h>


/** Function to handle a GPU state change for the coarse_demand power policy.
 *
 * This function is called whenever the GPU has transitioned to another state. It first checks that the transition is
 * complete and then moves the state machine to the next state.
 *
 * @param kbdev     The kbase device structure for the device
 */
static void coarse_demand_state_changed(kbase_device *kbdev)
{
	kbasep_pm_policy_coarse_demand *data = &kbdev->pm.policy_data.coarse_demand;

	/* No need to early-out if cores transitioning during the POWERING_UP state */
	if (data->state != KBASEP_PM_COARSE_DEMAND_STATE_POWERING_UP
		&& kbase_pm_get_pwr_active(kbdev)) {
		/* Cores are still transitioning - ignore the event */
		return;
	}

	switch(data->state)
	{
	case KBASEP_PM_COARSE_DEMAND_STATE_POWERING_UP:
		/* All cores are ready, inform the OS */
		data->state = KBASEP_PM_COARSE_DEMAND_STATE_POWERED_UP;
		kbase_pm_power_up_done(kbdev);
		/*
		 * No need to submit jobs:
		 * - All cores will be powered on
		 * - Cores are made available even while they're transitioning to 'powered on'
		 * - We signal power_up_done after calling kbase_pm_check_transitions(), which makes the cores available.
		 * Hence, the submission of jobs will already be handled by the call-path that invoked kbase_pm_context_active()
		 */

		break;
	case KBASEP_PM_COARSE_DEMAND_STATE_POWERING_DOWN:
		data->state = KBASEP_PM_COARSE_DEMAND_STATE_POWERED_DOWN;
		/* All cores have transitioned, turn the clock and interrupts off */
		kbase_pm_clock_off(kbdev);

		/* Inform the OS */
		kbase_pm_power_down_done(kbdev);

		break;
	case KBASEP_PM_COARSE_DEMAND_STATE_CHANGING_POLICY:
		/* Signal power events before switching the policy */
		kbase_pm_power_up_done(kbdev);
		kbase_pm_power_down_done(kbdev);
		kbase_pm_change_policy(kbdev);

		break;
	default:
		break;
	}
}

/** Turn the GPU off.
 *
 * Turns the GPU off - assuming that no Job Chains are currently running on the GPU.
 */
static void coarse_demand_power_down(kbase_device *kbdev)
{
	u64 cores;

	/* Inform the system that the transition has started */
	kbase_pm_power_transitioning(kbdev);

	cores = kbase_pm_get_present_cores(kbdev, KBASE_PM_CORE_SHADER);
	kbase_pm_invoke_power_down(kbdev, KBASE_PM_CORE_SHADER, cores);

	cores = kbase_pm_get_present_cores(kbdev, KBASE_PM_CORE_TILER);
	kbase_pm_invoke_power_down(kbdev, KBASE_PM_CORE_TILER, cores);

	/* Note we don't call kbase_pm_check_transitions because we don't want to wait
	 * for the above transitions to take place before turning the GPU power domain off */

	kbdev->pm.policy_data.coarse_demand.state = KBASEP_PM_COARSE_DEMAND_STATE_POWERING_DOWN;

	/* Ensure that the OS is informed even if we didn't do anything */
	coarse_demand_state_changed(kbdev);
}

/** Turn the GPU off, safe against jobs that are currently running.
 *
 * Turns the GPU off in a way that will wait for jobs to finish first.
 */
static void coarse_demand_suspend(kbase_device *kbdev)
{
	u64 cores;

	/* Inform the system that the transition has started */
	kbase_pm_power_transitioning(kbdev);

	/* Turn the cores off */
	cores = kbase_pm_get_present_cores(kbdev, KBASE_PM_CORE_SHADER);
	kbase_pm_invoke_power_down(kbdev, KBASE_PM_CORE_SHADER, cores);

	cores = kbase_pm_get_present_cores(kbdev, KBASE_PM_CORE_TILER);
	kbase_pm_invoke_power_down(kbdev, KBASE_PM_CORE_TILER, cores);

	kbdev->pm.policy_data.coarse_demand.state = KBASEP_PM_COARSE_DEMAND_STATE_POWERING_DOWN;

	kbase_pm_check_transitions(kbdev);
}


/** Turns the cores on.
 *
 * This function turns all the cores of the GPU on.
 */
static void coarse_demand_power_up(kbase_device *kbdev)
{
	u64 cores;

	/* Inform the system that the transition has started */
	kbase_pm_power_transitioning(kbdev);

	/* Turn the clock on and enable interrupts */
	kbase_pm_clock_on(kbdev);

	/* Turn the cores on */
	cores = kbase_pm_get_present_cores(kbdev, KBASE_PM_CORE_SHADER);
	kbase_pm_invoke_power_up(kbdev, KBASE_PM_CORE_SHADER, cores);

	cores = kbase_pm_get_present_cores(kbdev, KBASE_PM_CORE_TILER);
	kbase_pm_invoke_power_up(kbdev, KBASE_PM_CORE_TILER, cores);

	kbase_pm_check_transitions(kbdev);

	kbdev->pm.policy_data.coarse_demand.state = KBASEP_PM_COARSE_DEMAND_STATE_POWERING_UP;

	/* Ensure that the OS is informed even if we didn't do anything */
	coarse_demand_state_changed(kbdev);
}

/** The event callback function for the coarse_demand power policy.
 *
 * This function is called to handle the events for the power policy. It calls the relevant handler function depending
 * on the type of the event.
 *
 * @param kbdev     The kbase device structure for the device
 * @param event     The event that should be processed
 */
static void coarse_demand_event(kbase_device *kbdev, kbase_pm_event event)
{
	kbasep_pm_policy_coarse_demand *data = &kbdev->pm.policy_data.coarse_demand;

	switch(event)
	{
	case KBASE_PM_EVENT_POLICY_INIT:
		coarse_demand_power_up(kbdev);
		break;
	case KBASE_PM_EVENT_SYSTEM_SUSPEND:
		switch (data->state)
		{
			case KBASEP_PM_COARSE_DEMAND_STATE_POWERING_DOWN:
				break;
			case KBASEP_PM_COARSE_DEMAND_STATE_POWERED_DOWN:
				kbase_pm_power_down_done(kbdev);
				break;
			default:
				coarse_demand_suspend(kbdev);
		}
		break;
	case KBASE_PM_EVENT_GPU_IDLE:
		switch (data->state)
		{
			case KBASEP_PM_COARSE_DEMAND_STATE_POWERING_DOWN:
				break;
			case KBASEP_PM_COARSE_DEMAND_STATE_POWERED_DOWN:
				kbase_pm_power_down_done(kbdev);
				break;
			default:
				coarse_demand_power_down(kbdev);
		}
		break;
	case KBASE_PM_EVENT_GPU_ACTIVE:
	case KBASE_PM_EVENT_SYSTEM_RESUME:
		switch (data->state)
		{
			case KBASEP_PM_COARSE_DEMAND_STATE_POWERING_UP:
				break;
			case KBASEP_PM_COARSE_DEMAND_STATE_POWERED_UP:
				kbase_pm_power_up_done(kbdev);
				break;
			default:
				coarse_demand_power_up(kbdev);
		}
		break;
	case KBASE_PM_EVENT_GPU_STATE_CHANGED:
		coarse_demand_state_changed(kbdev);
		break;
	case KBASE_PM_EVENT_POLICY_CHANGE:
		if (data->state == KBASEP_PM_COARSE_DEMAND_STATE_POWERED_UP ||
		    data->state == KBASEP_PM_COARSE_DEMAND_STATE_POWERED_DOWN)
		{
			kbase_pm_change_policy(kbdev);
		}
		else
		{
			data->state = KBASEP_PM_COARSE_DEMAND_STATE_CHANGING_POLICY;
		}
		break;
	case KBASE_PM_EVENT_CHANGE_GPU_STATE:
		if (data->state != KBASEP_PM_COARSE_DEMAND_STATE_POWERED_DOWN &&
			data->state != KBASEP_PM_COARSE_DEMAND_STATE_POWERING_DOWN)
		{
			/*
			 * Update anyone waiting for power up events.
			 */
			kbase_pm_check_transitions(kbdev);
		}
		break;
	default:
		/* Unrecognised event - this should never happen */
		OSK_ASSERT(0);
	}
}

/** Initialize the coarse_demand power policy
 *
 * This sets up the private @ref kbase_pm_device_data.policy_data field of the device for use with the coarse_demand power
 * policy.
 *
 * @param kbdev     The kbase device structure for the device
 */
static void coarse_demand_init(kbase_device *kbdev)
{
	kbasep_pm_policy_coarse_demand *data = &kbdev->pm.policy_data.coarse_demand;

	data->state = KBASEP_PM_COARSE_DEMAND_STATE_POWERED_UP;
}

/** Terminate the coarse_demand power policy
 *
 * This frees the resources that were allocated by @ref coarse_demand_init.
 *
 * @param kbdev     The kbase device structure for the device
 */
static void coarse_demand_term(kbase_device *kbdev)
{
	CSTD_UNUSED(kbdev);
}

/** The @ref kbase_pm_policy structure for the coarse_demand power policy
 *
 * This is the extern structure that defines the coarse_demand power policy's callback and name.
 */
const kbase_pm_policy kbase_pm_coarse_demand_policy_ops =
{
	"coarse_demand",                /* name */
	coarse_demand_init,             /* init */
	coarse_demand_term,             /* term */
	coarse_demand_event,            /* event */
	KBASE_PM_POLICY_FLAG_NO_CORE_TRANSITIONS, /* flags */
	KBASE_PM_POLICY_ID_COARSE_DEMAND, /* id */
};

KBASE_EXPORT_TEST_API(kbase_pm_coarse_demand_policy_ops)
