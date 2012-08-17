/*
 * Copyright (C) 2011-2012 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "mali_pm.h"
#include "mali_kernel_common.h"
#include "mali_osk.h"
#include "mali_gp_scheduler.h"
#include "mali_pp_scheduler.h"
#include "mali_platform.h"
#include "mali_kernel_utilization.h"
#include "mali_kernel_core.h"
#include "mali_group.h"

#define MALI_PM_LIGHT_SLEEP_TIMEOUT 1000

enum mali_pm_scheme
{
	MALI_PM_SCHEME_DYNAMIC,
	MALI_PM_SCHEME_OS_SUSPENDED,
	MALI_PM_SCHEME_ALWAYS_ON
};

enum mali_pm_level
{
	MALI_PM_LEVEL_1_ON,
	MALI_PM_LEVEL_2_STANDBY,
	MALI_PM_LEVEL_3_LIGHT_SLEEP,
	MALI_PM_LEVEL_4_DEEP_SLEEP
};
static _mali_osk_lock_t *mali_pm_lock_set_next_state;
static _mali_osk_lock_t *mali_pm_lock_set_core_states;
static _mali_osk_lock_t *mali_pm_lock_execute_state_change;
static _mali_osk_irq_t *wq_irq;

static _mali_osk_timer_t *idle_timer = NULL;
static mali_bool idle_timer_running = MALI_FALSE;
static u32 mali_pm_event_number     = 0;

static u32 num_active_gps = 0;
static u32 num_active_pps = 0;

static enum mali_pm_scheme current_scheme = MALI_PM_SCHEME_DYNAMIC;
static enum mali_pm_level current_level = MALI_PM_LEVEL_1_ON;
static enum mali_pm_level next_level_dynamic = MALI_PM_LEVEL_2_STANDBY; /* Should be the state we go to when we go out of MALI_PM_SCHEME_ALWAYS_ON during init */



static _mali_osk_errcode_t mali_pm_upper_half(void *data);
static void mali_pm_bottom_half(void *data);
static void mali_pm_powerup(void);
static void mali_pm_powerdown(mali_power_mode power_mode);

static void timeout_light_sleep(void* arg);
#if 0
/* Deep sleep timout not supported */
static void timeout_deep_sleep(void* arg);
#endif
static u32 mali_pm_event_number_get(void);
static void mali_pm_event(enum mali_pm_event pm_event, mali_bool schedule_work, u32 timer_time );

_mali_osk_errcode_t mali_pm_initialize(void)
{
	mali_pm_lock_execute_state_change = _mali_osk_lock_init(_MALI_OSK_LOCKFLAG_ORDERED | _MALI_OSK_LOCKFLAG_ORDERED |_MALI_OSK_LOCKFLAG_NONINTERRUPTABLE, 0, _MALI_OSK_LOCK_ORDER_PM_EXECUTE);

	if (NULL != mali_pm_lock_execute_state_change )
	{
		mali_pm_lock_set_next_state = _mali_osk_lock_init(_MALI_OSK_LOCKFLAG_ONELOCK| _MALI_OSK_LOCKFLAG_SPINLOCK_IRQ |_MALI_OSK_LOCKFLAG_NONINTERRUPTABLE, 0, _MALI_OSK_LOCK_ORDER_LAST);

		if (NULL != mali_pm_lock_set_next_state)
		{
			mali_pm_lock_set_core_states = _mali_osk_lock_init(_MALI_OSK_LOCKFLAG_ORDERED | _MALI_OSK_LOCKFLAG_NONINTERRUPTABLE, 0, _MALI_OSK_LOCK_ORDER_PM_CORE_STATE);

			if (NULL != mali_pm_lock_set_core_states)
			{
				idle_timer = _mali_osk_timer_init();
				if (NULL != idle_timer)
				{
					wq_irq = _mali_osk_irq_init(_MALI_OSK_IRQ_NUMBER_PMM,
												mali_pm_upper_half,
												mali_pm_bottom_half,
												NULL,
												NULL,
												(void *)NULL,
												"Mali PM deferred work");
					if (NULL != wq_irq)
					{
						if (_MALI_OSK_ERR_OK == mali_platform_init())
						{
#if MALI_PMM_RUNTIME_JOB_CONTROL_ON
							_mali_osk_pm_dev_enable();
							mali_pm_powerup();
#endif
							return _MALI_OSK_ERR_OK;
						}

						_mali_osk_irq_term(wq_irq);
					}

					_mali_osk_timer_del(idle_timer);
					_mali_osk_timer_term(idle_timer);
				}
				_mali_osk_lock_term(mali_pm_lock_set_core_states);
			}
			_mali_osk_lock_term(mali_pm_lock_set_next_state);
		}
		_mali_osk_lock_term(mali_pm_lock_execute_state_change);
	}

	return _MALI_OSK_ERR_FAULT;
}

void mali_pm_terminate(void)
{
	mali_platform_deinit();
	_mali_osk_irq_term(wq_irq);
	_mali_osk_timer_del(idle_timer);
	_mali_osk_timer_term(idle_timer);
	_mali_osk_lock_term(mali_pm_lock_execute_state_change);
	_mali_osk_lock_term(mali_pm_lock_set_next_state);
	_mali_osk_lock_term(mali_pm_lock_set_core_states);
}


inline void mali_pm_lock(void)
{
	_mali_osk_lock_wait(mali_pm_lock_set_next_state, _MALI_OSK_LOCKMODE_RW);
}

inline void mali_pm_unlock(void)
{
	_mali_osk_lock_signal(mali_pm_lock_set_next_state, _MALI_OSK_LOCKMODE_RW);
}

inline void mali_pm_execute_state_change_lock(void)
{
	_mali_osk_lock_wait(mali_pm_lock_execute_state_change,_MALI_OSK_LOCKMODE_RW);
}

inline void mali_pm_execute_state_change_unlock(void)
{
	_mali_osk_lock_signal(mali_pm_lock_execute_state_change, _MALI_OSK_LOCKMODE_RW);
}

static void mali_pm_powerup(void)
{
	MALI_DEBUG_PRINT(3, ("Mali PM: Setting GPU power mode to MALI_POWER_MODE_ON\n"));
	mali_platform_power_mode_change(MALI_POWER_MODE_ON);

#if MALI_PMM_RUNTIME_JOB_CONTROL_ON

	/* Aquire our reference */
	MALI_DEBUG_PRINT(4, ("Mali PM: Getting device PM reference (=> requesting MALI_POWER_MODE_ON)\n"));
	_mali_osk_pm_dev_activate();
#endif

	mali_group_power_on();
}

static void mali_pm_powerdown(mali_power_mode power_mode)
{
	if ( (MALI_PM_LEVEL_1_ON == current_level) || (MALI_PM_LEVEL_2_STANDBY == current_level) )
	{
		mali_group_power_off();
	}
	mali_platform_power_mode_change(power_mode);

#if MALI_PMM_RUNTIME_JOB_CONTROL_ON
	_mali_osk_pm_dev_idle();
#endif
}

mali_bool mali_pm_is_powered_on(void)
{
	mali_bool is_on = MALI_TRUE;

	if( ! (MALI_PM_SCHEME_ALWAYS_ON == current_scheme || MALI_PM_SCHEME_DYNAMIC == current_scheme) )
	{
		is_on = MALI_FALSE;
	}
	else if ( ! (MALI_PM_LEVEL_1_ON == current_level || MALI_PM_LEVEL_2_STANDBY == current_level))
	{
		is_on = MALI_FALSE;
	}
	else if ( ! (MALI_PM_LEVEL_1_ON == next_level_dynamic || MALI_PM_LEVEL_2_STANDBY == next_level_dynamic))
	{
		is_on = MALI_FALSE;
	}

	return is_on;
}

MALI_DEBUG_CODE(
static const char *state_as_string(enum mali_pm_level level)
{
	switch(level)
	{
		case MALI_PM_LEVEL_1_ON:
			return "MALI_PM_LEVEL_1_ON";
		case MALI_PM_LEVEL_2_STANDBY:
			return "MALI_PM_LEVEL_2_STANDBY";
		case MALI_PM_LEVEL_3_LIGHT_SLEEP:
			return "MALI_PM_LEVEL_3_LIGHT_SLEEP";
		case MALI_PM_LEVEL_4_DEEP_SLEEP:
			return "MALI_PM_LEVEL_4_DEEP_SLEEP";
		default:
			return "UNKNOWN LEVEL";
	}
});

/* This could be used from another thread (work queue), if we need that */
static void mali_pm_process_next(void)
{
	enum mali_pm_level pm_level_to_set;

	_mali_osk_lock_wait(mali_pm_lock_execute_state_change, _MALI_OSK_LOCKMODE_RW);

	pm_level_to_set = current_level;

	if (MALI_PM_SCHEME_DYNAMIC == current_scheme)
	{
		pm_level_to_set = next_level_dynamic;

		MALI_DEBUG_PRINT(4, ("Mali PM: Dynamic scheme; Changing Mali GPU power state from %s to: %s\n", state_as_string(current_level), state_as_string(pm_level_to_set)));

		if (current_level == pm_level_to_set)
		{
			goto end_function; /* early out, no change in power level */
		}

		/* Start timers according to new state, so we get STANDBY -> LIGHT_SLEEP -> DEEP_SLEEP */

		if (MALI_TRUE == idle_timer_running)
		{
			/* There is an existing timeout, so delete it */
			_mali_osk_timer_del(idle_timer);
			idle_timer_running = MALI_FALSE;
		}

		/* Making sure that we turn on through the platform file
		   Since it was turned OFF directly through the platform file.
		   This might lead to double turn-on, but the plaform file supports that.*/
		if ( current_level == MALI_PM_LEVEL_4_DEEP_SLEEP)
		{
			mali_pm_powerup();
			mali_kernel_core_wakeup();

		}
		if (MALI_PM_LEVEL_1_ON == pm_level_to_set)
		{
			if (MALI_PM_LEVEL_2_STANDBY != current_level)
			{
				/* We only need to do anything if we came from one of the sleeping states */
				mali_pm_powerup();

				/* Wake up Mali cores since we came from a sleep state */
				mali_kernel_core_wakeup();
			}
		}
		else if (MALI_PM_LEVEL_2_STANDBY == pm_level_to_set)
		{
			/* This is just an internal state, so we don't bother to report it to the platform file */
			idle_timer_running = MALI_TRUE;
			_mali_osk_timer_setcallback(idle_timer, timeout_light_sleep, (void*) mali_pm_event_number_get());
			_mali_osk_timer_add(idle_timer, _mali_osk_time_mstoticks(MALI_PM_LIGHT_SLEEP_TIMEOUT));
		}
		else if (MALI_PM_LEVEL_3_LIGHT_SLEEP == pm_level_to_set)
		{
			mali_pm_powerdown(MALI_POWER_MODE_LIGHT_SLEEP);
		}
		else if (MALI_PM_LEVEL_4_DEEP_SLEEP == pm_level_to_set)
		{
			MALI_DEBUG_PRINT(2, ("Mali PM: Setting GPU power mode to MALI_POWER_MODE_DEEP_SLEEP\n"));
			mali_pm_powerdown(MALI_POWER_MODE_DEEP_SLEEP);
		}
	}
	else if (MALI_PM_SCHEME_OS_SUSPENDED == current_scheme)
	{
		MALI_DEBUG_PRINT(4, ("Mali PM: OS scheme; Changing Mali GPU power state from %s to: %s\n", state_as_string(current_level), state_as_string(MALI_PM_LEVEL_4_DEEP_SLEEP)));

		pm_level_to_set = MALI_PM_LEVEL_4_DEEP_SLEEP;

		if (current_level == pm_level_to_set)
		{
			goto end_function; /* early out, no change in power level */
		}

		/* Cancel any timers */
		if (MALI_TRUE == idle_timer_running)
		{
			/* There is an existing timeout, so delete it */
			_mali_osk_timer_del(idle_timer);
			idle_timer_running = MALI_FALSE;
		}

		MALI_DEBUG_PRINT(2, ("Mali PM: Setting GPU power mode to MALI_POWER_MODE_DEEP_SLEEP\n"));
		mali_pm_powerdown(MALI_POWER_MODE_DEEP_SLEEP);
		next_level_dynamic = current_level;
	}
	else if (MALI_PM_SCHEME_ALWAYS_ON == current_scheme)
	{
		MALI_DEBUG_PRINT(4, ("Mali PM: Always on scheme; Changing Mali GPU power state from %s to: %s\n", state_as_string(current_level), state_as_string(MALI_PM_LEVEL_1_ON)));

		pm_level_to_set = MALI_PM_LEVEL_1_ON;
		if (current_level == pm_level_to_set)
		{
			goto end_function; /* early out, no change in power level */
		}

		MALI_DEBUG_PRINT(2, ("Mali PM: Setting GPU power mode to MALI_POWER_MODE_ON\n"));
		mali_pm_powerup();
		if (MALI_PM_LEVEL_2_STANDBY != current_level)
		{
			/* Wake up Mali cores since we came from a sleep state */
			mali_kernel_core_wakeup();
		}
	}
	else
	{
		MALI_PRINT_ERROR(("MALI PM: Illegal scheme"));
	}

	current_level = pm_level_to_set;

end_function:
	_mali_osk_lock_signal(mali_pm_lock_execute_state_change, _MALI_OSK_LOCKMODE_RW);

}

void mali_pm_always_on(mali_bool enable)
{
	if (MALI_TRUE == enable)
	{
		/* The event is processed in current thread synchronously */
		mali_pm_event(MALI_PM_EVENT_SCHEME_ALWAYS_ON, MALI_FALSE, 0 );
	}
	else
	{
		/* The event is processed in current thread synchronously */
		mali_pm_event(MALI_PM_EVENT_SCHEME_DYNAMIC_CONTROLL, MALI_FALSE, 0 );
	}
}

static _mali_osk_errcode_t mali_pm_upper_half(void *data)
{
	/* not used */
	return _MALI_OSK_ERR_OK;
}

static void mali_pm_bottom_half(void *data)
{
	mali_pm_process_next();
}

static u32 mali_pm_event_number_get(void)
{
	u32 retval;

	mali_pm_lock(); /* spinlock: mali_pm_lock_set_next_state */
	retval = ++mali_pm_event_number;
	if (0==retval ) retval = ++mali_pm_event_number;
	mali_pm_unlock();

	return retval;
}

static void mali_pm_event(enum mali_pm_event pm_event, mali_bool schedule_work, u32 timer_time )
{
	mali_pm_lock(); /* spinlock: mali_pm_lock_set_next_state */
	/* Only timer events should set this variable, all other events must set it to zero. */
	if ( 0 != timer_time )
	{
		MALI_DEBUG_ASSERT( (pm_event==MALI_PM_EVENT_TIMER_LIGHT_SLEEP) || (pm_event==MALI_PM_EVENT_TIMER_DEEP_SLEEP) );
		if ( mali_pm_event_number != timer_time )
		{
			/* In this case there have been processed newer events since the timer event was set up.
			   If so we always ignore the timing event */
			mali_pm_unlock();
			return;
		}
	}
	else
	{
		/* Delete possible ongoing timers
		if (  (MALI_PM_LEVEL_2_STANDBY==current_level) || (MALI_PM_LEVEL_3_LIGHT_SLEEP==current_level) )
		{
			_mali_osk_timer_del(idle_timer);
		}
		*/
	}
	mali_pm_event_number++;
	switch (pm_event)
	{
		case MALI_PM_EVENT_CORES_WORKING:
			next_level_dynamic = MALI_PM_LEVEL_1_ON;
			MALI_DEBUG_ASSERT( MALI_PM_SCHEME_OS_SUSPENDED    != current_scheme );
			break;
		case MALI_PM_EVENT_CORES_IDLE:
			next_level_dynamic = MALI_PM_LEVEL_2_STANDBY;
			/*MALI_DEBUG_ASSERT( MALI_PM_SCHEME_OS_SUSPENDED    != current_scheme );*/
			break;
		case MALI_PM_EVENT_TIMER_LIGHT_SLEEP:
			MALI_DEBUG_ASSERT( MALI_PM_SCHEME_ALWAYS_ON != current_scheme );
			MALI_DEBUG_ASSERT( MALI_PM_SCHEME_OS_SUSPENDED    != current_scheme );
			next_level_dynamic = MALI_PM_LEVEL_3_LIGHT_SLEEP;
			break;
		case MALI_PM_EVENT_TIMER_DEEP_SLEEP:
			MALI_DEBUG_ASSERT( MALI_PM_SCHEME_ALWAYS_ON != current_scheme );
			MALI_DEBUG_ASSERT( MALI_PM_SCHEME_OS_SUSPENDED    != current_scheme );
			next_level_dynamic = MALI_PM_LEVEL_4_DEEP_SLEEP;
			break;
		case MALI_PM_EVENT_OS_SUSPEND:
			MALI_DEBUG_ASSERT( MALI_PM_SCHEME_ALWAYS_ON != current_scheme );
			MALI_DEBUG_ASSERT( MALI_PM_SCHEME_OS_SUSPENDED    != current_scheme );
			current_scheme = MALI_PM_SCHEME_OS_SUSPENDED;
			next_level_dynamic = MALI_PM_LEVEL_4_DEEP_SLEEP; /* Dynamic scheme will go into level when we are resumed */
			break;
		case MALI_PM_EVENT_OS_RESUME:
			MALI_DEBUG_ASSERT(MALI_PM_SCHEME_OS_SUSPENDED == current_scheme );
			current_scheme = MALI_PM_SCHEME_DYNAMIC;
			break;
		case MALI_PM_EVENT_SCHEME_ALWAYS_ON:
			MALI_DEBUG_ASSERT( MALI_PM_SCHEME_OS_SUSPENDED != current_scheme );
			current_scheme = MALI_PM_SCHEME_ALWAYS_ON;
			break;
		case MALI_PM_EVENT_SCHEME_DYNAMIC_CONTROLL:
			MALI_DEBUG_ASSERT( MALI_PM_SCHEME_ALWAYS_ON == current_scheme || MALI_PM_SCHEME_DYNAMIC == current_scheme );
			current_scheme = MALI_PM_SCHEME_DYNAMIC;
			break;
		default:
			MALI_DEBUG_PRINT_ERROR(("Unknown next state."));
			mali_pm_unlock();
			return;
	}
	mali_pm_unlock();

	if (MALI_TRUE == schedule_work)
	{
		_mali_osk_irq_schedulework(wq_irq);
	}
	else
	{
		mali_pm_process_next();
	}
}

static void timeout_light_sleep(void* arg)
{
	/* State change only if no newer power events have happend from the time in arg.
	    Actual work will be scheduled on worker thread. */
	mali_pm_event(MALI_PM_EVENT_TIMER_LIGHT_SLEEP, MALI_TRUE, (u32) arg);
}

void mali_pm_core_event(enum mali_core_event core_event)
{
	mali_bool transition_working = MALI_FALSE;
	mali_bool transition_idle = MALI_FALSE;

	_mali_osk_lock_wait(mali_pm_lock_set_core_states, _MALI_OSK_LOCKMODE_RW);

	switch (core_event)
	{
		case MALI_CORE_EVENT_GP_START:
			if (num_active_pps + num_active_gps == 0)
			{
				transition_working = MALI_TRUE;
			}
			num_active_gps++;
			break;
		case MALI_CORE_EVENT_GP_STOP:
			if (num_active_pps + num_active_gps == 1)
			{
				transition_idle = MALI_TRUE;
			}
			num_active_gps--;
			break;
		case MALI_CORE_EVENT_PP_START:
			if (num_active_pps + num_active_gps == 0)
			{
				transition_working = MALI_TRUE;
			}
			num_active_pps++;
			break;
		case MALI_CORE_EVENT_PP_STOP:
			if (num_active_pps + num_active_gps == 1)
			{
				transition_idle = MALI_TRUE;
			}
			num_active_pps--;
			break;
	}

	if (transition_working == MALI_TRUE)
	{
#ifdef CONFIG_MALI400_GPU_UTILIZATION
		mali_utilization_core_start(_mali_osk_time_get_ns());
#endif
		mali_pm_event(MALI_PM_EVENT_CORES_WORKING, MALI_FALSE, 0); /* process event in same thread */
	}
	else if (transition_idle == MALI_TRUE)
	{
#ifdef CONFIG_MALI400_GPU_UTILIZATION
		mali_utilization_core_end(_mali_osk_time_get_ns());
#endif
		mali_pm_event(MALI_PM_EVENT_CORES_IDLE, MALI_FALSE, 0); /* process event in same thread */
	}

	_mali_osk_lock_signal(mali_pm_lock_set_core_states, _MALI_OSK_LOCKMODE_RW);
}

void mali_pm_os_suspend(void)
{
	MALI_DEBUG_PRINT(2, ("Mali PM: OS suspending...\n"));

	mali_gp_scheduler_suspend();
	mali_pp_scheduler_suspend();
	mali_pm_event(MALI_PM_EVENT_OS_SUSPEND, MALI_FALSE, 0); /* process event in same thread */

	MALI_DEBUG_PRINT(2, ("Mali PM: OS suspend completed\n"));
}

void mali_pm_os_resume(void)
{
	MALI_DEBUG_PRINT(2, ("Mali PM: OS resuming...\n"));

	mali_pm_event(MALI_PM_EVENT_OS_RESUME, MALI_FALSE, 0); /* process event in same thread */
	mali_gp_scheduler_resume();
	mali_pp_scheduler_resume();

	MALI_DEBUG_PRINT(2, ("Mali PM: OS resume completed\n"));
}

void mali_pm_runtime_suspend(void)
{
	MALI_DEBUG_PRINT(2, ("Mali PM: OS runtime suspended\n"));
}

void mali_pm_runtime_resume(void)
{
	MALI_DEBUG_PRINT(3, ("Mali PM: OS runtime resumed\n"));
}
