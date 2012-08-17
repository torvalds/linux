/*
 * Copyright (C) 2012 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <linux/module.h>

#include "mali_kernel_common.h"
#include "mali_osk.h"
#include "mali_ukk.h"
#include "mali_uk_types.h"
#include "mali_osk_profiling.h"
#include "mali_linux_trace.h"
#include "mali_gp.h"
#include "mali_pp.h"
#include "mali_l2_cache.h"
#include "mali_user_settings_db.h"

_mali_osk_errcode_t _mali_osk_profiling_init(mali_bool auto_start)
{
	if (MALI_TRUE == auto_start)
	{
		mali_set_user_setting(_MALI_UK_USER_SETTING_SW_EVENTS_ENABLE, MALI_TRUE);
	}

	return _MALI_OSK_ERR_OK;
}

void _mali_osk_profiling_term(void)
{
	/* Nothing to do */
}

_mali_osk_errcode_t _mali_osk_profiling_start(u32 * limit)
{
	/* Nothing to do */
	return _MALI_OSK_ERR_OK;
}

_mali_osk_errcode_t _mali_osk_profiling_stop(u32 *count)
{
	/* Nothing to do */
	return _MALI_OSK_ERR_OK;
}

u32 _mali_osk_profiling_get_count(void)
{
	return 0;
}

_mali_osk_errcode_t _mali_osk_profiling_get_event(u32 index, u64* timestamp, u32* event_id, u32 data[5])
{
	/* Nothing to do */
	return _MALI_OSK_ERR_OK;
}

_mali_osk_errcode_t _mali_osk_profiling_clear(void)
{
	/* Nothing to do */
	return _MALI_OSK_ERR_OK;
}

mali_bool _mali_osk_profiling_is_recording(void)
{
	return MALI_FALSE;
}

mali_bool _mali_osk_profiling_have_recording(void)
{
	return MALI_FALSE;
}

void _mali_osk_profiling_report_sw_counters(u32 *counters)
{
	trace_mali_sw_counters(_mali_osk_get_pid(), _mali_osk_get_tid(), NULL, counters);
}


_mali_osk_errcode_t _mali_ukk_profiling_start(_mali_uk_profiling_start_s *args)
{
	return _mali_osk_profiling_start(&args->limit);
}

_mali_osk_errcode_t _mali_ukk_profiling_add_event(_mali_uk_profiling_add_event_s *args)
{
	/* Always add process and thread identificator in the first two data elements for events from user space */
	_mali_osk_profiling_add_event(args->event_id, _mali_osk_get_pid(), _mali_osk_get_tid(), args->data[2], args->data[3], args->data[4]);

	return _MALI_OSK_ERR_OK;
}

_mali_osk_errcode_t _mali_ukk_profiling_stop(_mali_uk_profiling_stop_s *args)
{
	return _mali_osk_profiling_stop(&args->count);
}

_mali_osk_errcode_t _mali_ukk_profiling_get_event(_mali_uk_profiling_get_event_s *args)
{
	return _mali_osk_profiling_get_event(args->index, &args->timestamp, &args->event_id, args->data);
}

_mali_osk_errcode_t _mali_ukk_profiling_clear(_mali_uk_profiling_clear_s *args)
{
	return _mali_osk_profiling_clear();
}

_mali_osk_errcode_t _mali_ukk_sw_counters_report(_mali_uk_sw_counters_report_s *args)
{
	_mali_osk_profiling_report_sw_counters(args->counters);
	return _MALI_OSK_ERR_OK;
}

/**
 * Called by gator.ko to set HW counters
 *
 * @param counter_id The counter ID.
 * @param event_id Event ID that the counter should count (HW counter value from TRM).
 * 
 * @return 1 on success, 0 on failure.
 */
int _mali_profiling_set_event(u32 counter_id, s32 event_id)
{

	if (counter_id == COUNTER_VP_C0)
	{
		struct mali_gp_core* gp_core = mali_gp_get_global_gp_core();
		if (NULL != gp_core)
		{
			if (MALI_TRUE == mali_gp_core_set_counter_src0(gp_core, event_id))
			{
				return 1;
			}
		}
	}
	else if (counter_id == COUNTER_VP_C1)
	{
		struct mali_gp_core* gp_core = mali_gp_get_global_gp_core();
		if (NULL != gp_core)
		{
			if (MALI_TRUE == mali_gp_core_set_counter_src1(gp_core, event_id))
			{
				return 1;
			}
		}
	}
	else if (counter_id >= COUNTER_FP0_C0 && counter_id <= COUNTER_FP3_C1)
	{
		u32 core_id = (counter_id - COUNTER_FP0_C0) >> 1;
		struct mali_pp_core* pp_core = mali_pp_get_global_pp_core(core_id);
		if (NULL != pp_core)
		{
			u32 counter_src = (counter_id - COUNTER_FP0_C0) & 1;
			if (0 == counter_src)
			{
				if (MALI_TRUE == mali_pp_core_set_counter_src0(pp_core, event_id))
				{
					return 1;
				}
			}
			else
			{
				if (MALI_TRUE == mali_pp_core_set_counter_src1(pp_core, event_id))
				{
					return 1;
				}
			}
		}
	}
	else if (counter_id >= COUNTER_L2_C0 && counter_id <= COUNTER_L2_C1)
	{
		u32 core_id = (counter_id - COUNTER_L2_C0) >> 1;
		struct mali_l2_cache_core* l2_cache_core = mali_l2_cache_core_get_glob_l2_core(core_id);
		if (NULL != l2_cache_core)
		{
			u32 counter_src = (counter_id - COUNTER_L2_C0) & 1;
			if (0 == counter_src)
			{
				if (MALI_TRUE == mali_l2_cache_core_set_counter_src0(l2_cache_core, event_id))
				{
					return 1;
				}
			}
			else
			{
				if (MALI_TRUE == mali_l2_cache_core_set_counter_src1(l2_cache_core, event_id))
				{
					return 1;
				}
			}
		}
	}

	return 0;
}

/**
 * Called by gator.ko to retrieve the L2 cache counter values for the first L2 cache. 
 * The L2 cache counters are unique in that they are polled by gator, rather than being
 * transmitted via the tracepoint mechanism. 
 *
 * @param src0 First L2 cache counter ID.
 * @param val0 First L2 cache counter value.
 * @param src1 Second L2 cache counter ID.
 * @param val1 Second L2 cache counter value.
 */
void _mali_profiling_get_counters(u32 *src0, u32 *val0, u32 *src1, u32 *val1)
{
	 struct mali_l2_cache_core *l2_cache = mali_l2_cache_core_get_glob_l2_core(0);
	 if (NULL != l2_cache)
	 {
		if (MALI_TRUE == mali_l2_cache_lock_power_state(l2_cache))
		{
			/* It is now safe to access the L2 cache core in order to retrieve the counters */
			mali_l2_cache_core_get_counter_values(l2_cache, src0, val0, src1, val1);
		}
		mali_l2_cache_unlock_power_state(l2_cache);
	 }
}

/*
 * List of possible actions to be controlled by Streamline.
 * The following numbers are used by gator to control the frame buffer dumping and s/w counter reporting.
 * We cannot use the enums in mali_uk_types.h because they are unknown inside gator.
 */
#define FBDUMP_CONTROL_ENABLE (1)
#define FBDUMP_CONTROL_RATE (2)
#define SW_COUNTER_ENABLE (3)
#define FBDUMP_CONTROL_RESIZE_FACTOR (4)

/**
 * Called by gator to control the production of profiling information at runtime.
 */
void _mali_profiling_control(u32 action, u32 value)
{
	switch(action)
	{
	case FBDUMP_CONTROL_ENABLE:
		mali_set_user_setting(_MALI_UK_USER_SETTING_COLORBUFFER_CAPTURE_ENABLED, (value == 0 ? MALI_FALSE : MALI_TRUE));
		break;
	case FBDUMP_CONTROL_RATE:
		mali_set_user_setting(_MALI_UK_USER_SETTING_BUFFER_CAPTURE_N_FRAMES, value);
		break;
	case SW_COUNTER_ENABLE:
		mali_set_user_setting(_MALI_UK_USER_SETTING_SW_COUNTER_ENABLED, value);
		break;
	case FBDUMP_CONTROL_RESIZE_FACTOR:
		mali_set_user_setting(_MALI_UK_USER_SETTING_BUFFER_CAPTURE_RESIZE_FACTOR, value);
		break;
	default:
		break;	/* Ignore unimplemented actions */
	}
}

EXPORT_SYMBOL(_mali_profiling_set_event);
EXPORT_SYMBOL(_mali_profiling_get_counters);
EXPORT_SYMBOL(_mali_profiling_control);
