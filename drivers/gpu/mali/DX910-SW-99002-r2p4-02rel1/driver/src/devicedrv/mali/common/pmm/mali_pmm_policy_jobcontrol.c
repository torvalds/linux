/*
 * Copyright (C) 2010-2012 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/**
 * @file mali_pmm_policy_jobcontrol.c
 * Implementation of the power management module policy - job control
 */

#if USING_MALI_PMM

#include "mali_ukk.h"
#include "mali_kernel_common.h"
#include "mali_platform.h"

#include "mali_pmm.h"
#include "mali_pmm_system.h"
#include "mali_pmm_state.h"
#include "mali_pmm_policy.h"
#include "mali_pmm_policy_jobcontrol.h"

typedef struct _pmm_policy_data_job_control
{
	_pmm_policy_timer_t latency;  /**< Latency timeout timer for all cores */
	u32 core_active_start;        /**< Last time a core was set to active */
	u32 timeout;                  /**< Timeout in ticks for latency timer */
} _pmm_policy_data_job_control_t;


/* @ brief Local data for this policy 
 */
static _pmm_policy_data_job_control_t *data_job_control = NULL;

/* @brief Set up the timeout if it hasn't already been set and if there are active cores */
static void job_control_timeout_setup( _mali_pmm_internal_state_t *pmm, _pmm_policy_timer_t *pptimer )
{
	MALI_DEBUG_ASSERT_POINTER(pmm);
	MALI_DEBUG_ASSERT_POINTER(pptimer);

	/* Do we have an inactivity time out and some powered cores? */
	if( pptimer->timeout > 0 && pmm->cores_powered != 0 )
	{
		/* Is the system idle and all the powered cores are idle? */
		if( pmm->status == MALI_PMM_STATUS_IDLE && pmm->cores_idle == pmm->cores_powered )
		{
			if( pmm_policy_timer_start(pptimer) )
			{
				MALIPMM_DEBUG_PRINT( ("PMM policy - Job control: Setting in-activity latency timer\n") );
			}
		}
		else
		{
			/* We are not idle so there is no need for an inactivity timer
			 */
			if( pmm_policy_timer_stop(pptimer) )
			{
				MALIPMM_DEBUG_PRINT( ("PMM policy - Job control: Removing in-activity latency timer\n") );
			}
		}
	}
}

/* @brief Check the validity of the timeout - and if there is one set */
static mali_bool job_control_timeout_valid( _mali_pmm_internal_state_t *pmm, _pmm_policy_timer_t *pptimer, u32 timer_start )
{
	MALI_DEBUG_ASSERT_POINTER(pmm);
	MALI_DEBUG_ASSERT_POINTER(pptimer);

	/* Not a valid timer! */
	if( pptimer->timeout == 0 ) return MALI_FALSE;

	/* Are some cores powered and are they all idle? */
	if( (pmm->cores_powered != 0) && (pmm->cores_idle == pmm->cores_powered) )
	{
		/* Has latency timeout started after the last core was active? */
		if( pmm_policy_timer_valid( timer_start, data_job_control->core_active_start ) )
		{
			 return MALI_TRUE;
		}
		else
		{
			MALIPMM_DEBUG_PRINT( ("PMM: In-activity latency time out ignored - out of date\n") );
		}
	}
	else
	{
		if( pmm->cores_powered == 0 )
		{
			MALIPMM_DEBUG_PRINT( ("PMM: In-activity latency time out ignored - cores already off\n") );
		}
		else
		{
			MALIPMM_DEBUG_PRINT( ("PMM: In-activity latency time out ignored - cores active\n") );
		}
	}

	return MALI_FALSE;
}

_mali_osk_errcode_t pmm_policy_init_job_control( _mali_pmm_internal_state_t *pmm )
{
	_mali_osk_errcode_t err;
	MALI_DEBUG_ASSERT_POINTER( pmm );
	MALI_DEBUG_ASSERT( data_job_control == NULL );
	
	data_job_control = (_pmm_policy_data_job_control_t *) _mali_osk_malloc(sizeof(*data_job_control));
	MALI_CHECK_NON_NULL( data_job_control, _MALI_OSK_ERR_NOMEM );

	data_job_control->core_active_start = _mali_osk_time_tickcount();
	data_job_control->timeout = MALI_PMM_POLICY_JOBCONTROL_INACTIVITY_TIMEOUT;
	
	err = pmm_policy_timer_init( &data_job_control->latency, data_job_control->timeout, MALI_PMM_EVENT_TIMEOUT );
	if( err != _MALI_OSK_ERR_OK )
	{
		_mali_osk_free( data_job_control );
		data_job_control = NULL;
		return err;
	}
	
	/* Start the latency timeout */
	job_control_timeout_setup( pmm, &data_job_control->latency );
	
	MALI_SUCCESS;
}

void pmm_policy_term_job_control(void)
{
	if( data_job_control != NULL )
	{
		pmm_policy_timer_term( &data_job_control->latency );
		_mali_osk_free( data_job_control );
		data_job_control = NULL;
	}
}

static void pmm_policy_job_control_job_queued( _mali_pmm_internal_state_t *pmm )
{
	mali_pmm_core_mask cores;
	mali_pmm_core_mask cores_subset;

	/* Make sure that all cores are powered in this
	 * simple policy
	 */
	cores = pmm->cores_registered;
	cores_subset = pmm_cores_to_power_up( pmm, cores );
	if( cores_subset != 0 )
	{
		/* There are some cores that need powering up */
		if( !pmm_invoke_power_up( pmm ) )
		{
			/* Need to wait until finished */
			pmm->status = MALI_PMM_STATUS_POLICY_POWER_UP;
		}
	}
}

_mali_osk_errcode_t pmm_policy_process_job_control( _mali_pmm_internal_state_t *pmm, mali_pmm_message_t *event )
{
	mali_pmm_core_mask cores;
	mali_pmm_core_mask cores_subset;
	MALI_DEBUG_ASSERT_POINTER(pmm);
	MALI_DEBUG_ASSERT_POINTER(event);
	MALI_DEBUG_ASSERT_POINTER(data_job_control);

	MALIPMM_DEBUG_PRINT( ("PMM: Job control policy process start - status=%d\n", pmm->status) );

	/* Mainly the data is the cores */
	cores = pmm_cores_from_event_data( pmm, event );

#if MALI_STATE_TRACKING
	pmm->mali_last_pmm_status = pmm->status;
#endif /* MALI_STATE_TRACKING */

	switch( pmm->status )
	{
	/**************** IDLE ****************/
	case MALI_PMM_STATUS_IDLE:
		switch( event->id )
		{
		case MALI_PMM_EVENT_OS_POWER_UP:
			/* Not expected in this state */
			break;

		case MALI_PMM_EVENT_JOB_SCHEDULED:

			/* Update idle cores to indicate active - remove these! */
			pmm_cores_set_active( pmm, cores );
			/* Remember when this happened */
			data_job_control->core_active_start = event->ts;
#if MALI_POWER_MGMT_TEST_SUITE
			_mali_osk_pmm_policy_events_notifications(MALI_PMM_EVENT_JOB_SCHEDULED);
#endif

			/*** FALL THROUGH to QUEUED to check POWER UP ***/

		case MALI_PMM_EVENT_JOB_QUEUED:
		
			pmm_policy_job_control_job_queued( pmm );
#if MALI_POWER_MGMT_TEST_SUITE
			_mali_osk_pmm_policy_events_notifications(MALI_PMM_EVENT_JOB_QUEUED);
#endif
			break;
		
		case MALI_PMM_EVENT_DVFS_PAUSE:

			cores_subset = pmm_cores_to_power_down( pmm, cores, MALI_FALSE );
			if ( cores_subset != 0 )
			{
				if ( !pmm_power_down_okay( pmm ) )
				{
					pmm->is_dvfs_active = 1;
					pmm->status = MALI_PMM_STATUS_OS_POWER_DOWN;
					pmm_save_os_event_data( pmm, event->data );
					break;
				}
			}
			pmm->status = MALI_PMM_STATUS_DVFS_PAUSE;
			_mali_osk_pmm_dvfs_operation_done(0);
			break;

		case MALI_PMM_EVENT_OS_POWER_DOWN:

			/* Need to power down all cores even if we need to wait for them */
			cores_subset = pmm_cores_to_power_down( pmm, cores, MALI_FALSE );
			if( cores_subset != 0 )
			{
				/* There are some cores that need powering down */
				if( !pmm_invoke_power_down( pmm, MALI_POWER_MODE_DEEP_SLEEP ) )
				{
					/* We need to wait until they are idle */
					
					pmm->status = MALI_PMM_STATUS_OS_POWER_DOWN;
					/* Save the OS data to respond later */
					pmm_save_os_event_data( pmm, event->data );
					/* Exit this case - as we have to wait */
					break;
				}
			}
			else
			{
				 mali_platform_power_mode_change(MALI_POWER_MODE_DEEP_SLEEP);

			}
			/* Set waiting status */
			pmm->status = MALI_PMM_STATUS_OS_WAITING;
			/* All cores now down - respond to OS power event */
			_mali_osk_pmm_power_down_done( event->data );
			break;

		case MALI_PMM_EVENT_JOB_FINISHED:

			/* Update idle cores - add these! */
			pmm_cores_set_idle( pmm, cores );
#if MALI_POWER_MGMT_TEST_SUITE
			 _mali_osk_pmm_policy_events_notifications(MALI_PMM_EVENT_JOB_FINISHED);
#endif
			if( data_job_control->timeout > 0 )
			{
				/* Wait for time out to fire */
				break;
			}
			/* For job control policy - turn off all cores */
			cores = pmm->cores_powered;
			
			/*** FALL THROUGH to TIMEOUT TEST as NO TIMEOUT ***/

		case MALI_PMM_EVENT_TIMEOUT:

			/* Main job control policy - turn off cores after inactivity */
			if( job_control_timeout_valid( pmm, &data_job_control->latency, (u32)event->data ) )
 			{
				/* Valid timeout of inactivity - so find out if we can power down 
				 * immedately - if we can't then this means the cores are still in fact
				 * active 
				 */
				cores_subset = pmm_cores_to_power_down( pmm, cores, MALI_TRUE );
				if( cores_subset != 0 )
				{
					/* Check if we can really power down, if not then we are not
					 * really in-active
					 */
					if( !pmm_invoke_power_down( pmm, MALI_POWER_MODE_LIGHT_SLEEP ) )
					{
						pmm_power_down_cancel( pmm );
					}
				}
				/* else there are no cores powered up! */
			}
#if MALI_POWER_MGMT_TEST_SUITE
			_mali_osk_pmm_policy_events_notifications(MALI_PMM_EVENT_TIMEOUT);
#endif
			break;

		default:
			/* Unexpected event */
			MALI_ERROR(_MALI_OSK_ERR_ITEM_NOT_FOUND);
		}
		break;

	/******************DVFS PAUSE**************/
	case MALI_PMM_STATUS_DVFS_PAUSE:
		switch ( event->id )
		{
		case MALI_PMM_EVENT_DVFS_RESUME:

			if ( pmm->cores_powered != 0 )
			{
				pmm->cores_ack_down =0;
				pmm_power_down_cancel( pmm );
				pmm->status = MALI_PMM_STATUS_IDLE;
			}
			else
			{
				pmm_policy_job_control_job_queued( pmm );
			}
			_mali_osk_pmm_dvfs_operation_done( 0 );
			break;

		case MALI_PMM_EVENT_OS_POWER_DOWN:
			/* Set waiting status */
			pmm->status = MALI_PMM_STATUS_OS_WAITING;
			if ( pmm->cores_powered != 0 )
			{
				if ( pmm_invoke_power_down( pmm, MALI_POWER_MODE_DEEP_SLEEP ) )
				{
					_mali_osk_pmm_power_down_done( 0 );
					break;
				}
			}
			else	
			{
				 mali_platform_power_mode_change(MALI_POWER_MODE_DEEP_SLEEP);
			}
			_mali_osk_pmm_power_down_done( 0 );
			break;
		default:
			break;
		}
	break;

	/**************** POWER UP ****************/
	case MALI_PMM_STATUS_OS_POWER_UP:
	case MALI_PMM_STATUS_POLICY_POWER_UP:
		switch( event->id )
		{
		case MALI_PMM_EVENT_INTERNAL_POWER_UP_ACK:
			/* Make sure cores powered off equal what we expect */
			MALI_DEBUG_ASSERT( cores == pmm->cores_pend_up );
			pmm_cores_set_up_ack( pmm, cores );

			if( pmm_invoke_power_up( pmm ) )
			{
				if( pmm->status == MALI_PMM_STATUS_OS_POWER_UP )
				{
					/* Get the OS data and respond to the power up */
					_mali_osk_pmm_power_up_done( pmm_retrieve_os_event_data( pmm ) );
				}
				pmm->status = MALI_PMM_STATUS_IDLE;
			}
			break;

		default:
			/* Unexpected event */
			MALI_ERROR(_MALI_OSK_ERR_ITEM_NOT_FOUND);
		}
		break;

	/**************** POWER DOWN ****************/
	case MALI_PMM_STATUS_OS_POWER_DOWN:
	case MALI_PMM_STATUS_POLICY_POWER_DOWN:
		switch( event->id )
		{

		case MALI_PMM_EVENT_INTERNAL_POWER_DOWN_ACK:
			
			pmm_cores_set_down_ack( pmm, cores );
			
			if ( pmm->is_dvfs_active == 1 )
			{
				if( pmm_power_down_okay( pmm ) )
				{
					pmm->is_dvfs_active = 0;
					pmm->status = MALI_PMM_STATUS_DVFS_PAUSE;
					_mali_osk_pmm_dvfs_operation_done( pmm_retrieve_os_event_data( pmm ) );
				}
				break;
			}
			
			/* Now check if we can power down */
			if( pmm_invoke_power_down( pmm, MALI_POWER_MODE_DEEP_SLEEP ) )
			{
				if( pmm->status == MALI_PMM_STATUS_OS_POWER_DOWN )
				{
					/* Get the OS data and respond to the power down */
					_mali_osk_pmm_power_down_done( pmm_retrieve_os_event_data( pmm ) );
				}
				pmm->status = MALI_PMM_STATUS_OS_WAITING;
			}
			break;

		default:
			/* Unexpected event */
			MALI_ERROR(_MALI_OSK_ERR_ITEM_NOT_FOUND);
		}
		break;
		
	case MALI_PMM_STATUS_OS_WAITING:
		switch( event->id )
		{
		case MALI_PMM_EVENT_OS_POWER_UP:
			cores_subset = pmm_cores_to_power_up( pmm, cores );
			if( cores_subset != 0 )
			{
				/* There are some cores that need powering up */
				if( !pmm_invoke_power_up( pmm ) )
				{
					/* Need to wait until power up complete */
					pmm->status = MALI_PMM_STATUS_OS_POWER_UP;
					/* Save the OS data to respond later */
					pmm_save_os_event_data( pmm, event->data );
					/* Exit this case - as we have to wait */
					break;
				}
			}
			pmm->status = MALI_PMM_STATUS_IDLE;
			/* All cores now up - respond to OS power up event */
			_mali_osk_pmm_power_up_done( event->data );
			break;
			
		default:
			/* All other messages are ignored in this state */
			break;
		}
		break;	
	
	default:
		/* Unexpected state */
		MALI_ERROR(_MALI_OSK_ERR_FAULT);
	}

	/* Set in-activity latency timer - if required */
	job_control_timeout_setup( pmm, &data_job_control->latency );

	/* Update the PMM state */
	pmm_update_system_state( pmm );
#if MALI_STATE_TRACKING
	pmm->mali_new_event_status = event->id;
#endif /* MALI_STATE_TRACKING */

	MALIPMM_DEBUG_PRINT( ("PMM: Job control policy process end - status=%d and event=%d\n", pmm->status,event->id) );

	MALI_SUCCESS;
}

void pmm_policy_check_job_control()
{
	MALI_DEBUG_ASSERT_POINTER(data_job_control);

	/* Latency timer must have expired raise the event */
	pmm_policy_timer_raise_event(&data_job_control->latency);
}


#endif /* USING_MALI_PMM */
