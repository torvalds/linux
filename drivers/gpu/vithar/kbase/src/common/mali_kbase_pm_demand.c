/*
 *
 * (C) COPYRIGHT 2010-2012 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 * 
 */



/**
 * @file mali_kbase_pm_demand.c
 * A simple demand based power management policy
 */

#include <osk/mali_osk.h>
#include <kbase/src/common/mali_kbase.h>
#include <kbase/src/common/mali_kbase_pm.h>

/* Forward declaration for state change function, as it is required by
 * the power up and down functions */
static void demand_state_changed(kbase_device *kbdev);

/** Turns the cores on.
 *
 * This function turns all the cores of the GPU on.
 */
static void demand_power_up(kbase_device *kbdev)
{
	/* Inform the system that the transition has started */
	kbase_pm_power_transitioning(kbdev);

	/* Turn clocks and interrupts on */
	kbase_pm_clock_on(kbdev);
	kbase_pm_enable_interrupts(kbdev);
	
	kbase_pm_check_transitions(kbdev);

	kbdev->pm.policy_data.demand.state = KBASEP_PM_DEMAND_STATE_POWERING_UP;
}

/** Turn the cores off.
 *
 * This function turns all the cores of the GPU off.
 */
static void demand_power_down(kbase_device *kbdev)
{
	u64 cores;

	/* Inform the system that the transition has started */
	kbase_pm_power_transitioning(kbdev);

	/* Turn the cores off */
	cores = kbase_pm_get_present_cores(kbdev, KBASE_PM_CORE_SHADER);
	kbase_pm_invoke_power_down(kbdev, KBASE_PM_CORE_SHADER, cores);

	cores = kbase_pm_get_present_cores(kbdev, KBASE_PM_CORE_TILER);
	kbase_pm_invoke_power_down(kbdev, KBASE_PM_CORE_TILER, cores);

	kbdev->pm.policy_data.demand.state = KBASEP_PM_DEMAND_STATE_POWERING_DOWN;

	kbase_pm_check_transitions(kbdev);
}

/** Turn some cores on/off.
 *
 * This function turns on/off the cores needed by the scheduler.
 */
static void demand_change_gpu_state(kbase_device *kbdev)
{
	/* Update the bitmap of the cores we need */
	kbdev->pm.desired_shader_state = kbdev->shader_needed_bitmap;
	kbdev->pm.desired_tiler_state = kbdev->tiler_needed_bitmap;

	kbase_pm_check_transitions(kbdev);
}

/** Function to handle a GPU state change for the demand power policy
 *
 * This function is called whenever the GPU has transitioned to another state. It first checks that the transition is 
 * complete and then moves the state machine to the next state.
 */
static void demand_state_changed(kbase_device *kbdev)
{
	kbasep_pm_policy_demand *data = &kbdev->pm.policy_data.demand;

	switch(data->state) {
		case KBASEP_PM_DEMAND_STATE_CHANGING_POLICY:
		case KBASEP_PM_DEMAND_STATE_POWERING_UP:
		case KBASEP_PM_DEMAND_STATE_POWERING_DOWN:
			if (kbase_pm_get_pwr_active(kbdev)) {
				/* Cores are still transitioning - ignore the event */
				return;
			}
			break;
		default:
			/* Must not call kbase_pm_get_pwr_active here as the clock may be turned off */
			break;
	}

	switch(data->state)
	{
		case KBASEP_PM_DEMAND_STATE_CHANGING_POLICY:
			/* Signal power events before switching the policy */
			kbase_pm_power_up_done(kbdev);
			kbase_pm_power_down_done(kbdev);
			kbase_pm_change_policy(kbdev);
			break;
		case KBASEP_PM_DEMAND_STATE_POWERING_UP:
			data->state = KBASEP_PM_DEMAND_STATE_POWERED_UP;
			kbase_pm_power_up_done(kbdev);
			/* State changed, try to run jobs */
			kbase_js_try_run_jobs(kbdev);
			break;
		case KBASEP_PM_DEMAND_STATE_POWERING_DOWN:
			data->state = KBASEP_PM_DEMAND_STATE_POWERED_DOWN;
			/* Disable interrupts and turn the clock off */
			kbase_pm_disable_interrupts(kbdev);
			kbase_pm_clock_off(kbdev);
			kbase_pm_power_down_done(kbdev);
			break;
		case KBASEP_PM_DEMAND_STATE_POWERED_UP:
			/* Core states may have been changed, try to run jobs */
			kbase_js_try_run_jobs(kbdev);
			break;
		default:
			break;
	}
}

/** The event callback function for the demand power policy.
 *
 * This function is called to handle the events for the power policy. It calls the relevant handler function depending 
 * on the type of the event.
 *
 * @param kbdev     The kbase device structure for the device
 * @param event     The event that should be processed
 */
static void demand_event(kbase_device *kbdev, kbase_pm_event event)
{
	kbasep_pm_policy_demand *data = &kbdev->pm.policy_data.demand;
	
	switch(event)
	{
		case KBASE_PM_EVENT_POLICY_INIT:
			demand_power_up(kbdev);
			break;
		case KBASE_PM_EVENT_POLICY_CHANGE:
			if (data->state == KBASEP_PM_DEMAND_STATE_POWERED_UP ||
			    data->state == KBASEP_PM_DEMAND_STATE_POWERED_DOWN)
			{
				kbase_pm_change_policy(kbdev);
			}
			else
			{
				data->state = KBASEP_PM_DEMAND_STATE_CHANGING_POLICY;
			}
			break;
		case KBASE_PM_EVENT_SYSTEM_RESUME:
		case KBASE_PM_EVENT_GPU_ACTIVE:
			switch (data->state)
			{
				case KBASEP_PM_DEMAND_STATE_POWERING_UP:
					break;
				case KBASEP_PM_DEMAND_STATE_POWERED_UP:
					kbase_pm_power_up_done(kbdev);
					break;
				default:	
					demand_power_up(kbdev);
			}
			break;
		case KBASE_PM_EVENT_SYSTEM_SUSPEND:
		case KBASE_PM_EVENT_GPU_IDLE:
			switch (data->state)
			{
				case KBASEP_PM_DEMAND_STATE_POWERING_DOWN:
					break;
				case KBASEP_PM_DEMAND_STATE_POWERED_DOWN:
					kbase_pm_power_down_done(kbdev);
					break;
				default:	
					demand_power_down(kbdev);
			}
			break;
		case KBASE_PM_EVENT_CHANGE_GPU_STATE:
			if (data->state != KBASEP_PM_DEMAND_STATE_POWERED_DOWN &&
			    data->state != KBASEP_PM_DEMAND_STATE_POWERING_DOWN)
			{
				demand_change_gpu_state(kbdev);
			}
			break;
		case KBASE_PM_EVENT_GPU_STATE_CHANGED:
			demand_state_changed(kbdev);
			break;
		default:
			/* unrecognized event, should never happen */
			OSK_ASSERT(0);
	}
}

/** Initialize the demand power policy.
 *
 * This sets up the private @ref kbase_pm_device_data.policy_data field of the device for use with the demand power 
 * policy.
 *
 * @param kbdev     The kbase device structure for the device
 */
static void demand_init(kbase_device *kbdev)
{
	kbdev->pm.policy_data.demand.state = KBASEP_PM_DEMAND_STATE_POWERED_UP;
}

/** Terminate the demand power policy.
 *
 * This frees the resources that were allocated by @ref demand_init.
 *
 * @param kbdev     The kbase device structure for the device
 */
static void demand_term(kbase_device *kbdev)
{
	CSTD_UNUSED(kbdev);
}

/** The @ref kbase_pm_policy structure for the demand power policy.
 *
 * This is the static structure that defines the demand power policy's callback and name.
 */
const kbase_pm_policy kbase_pm_demand_policy_ops =
{
	"demand",                   /* name */
	demand_init,                /* init */
	demand_term,                /* term */
	demand_event,               /* event */
};


KBASE_EXPORT_TEST_API(kbase_pm_demand_policy_ops)
