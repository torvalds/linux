/*
 * Copyright (C) 2010-2012 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "mali_kernel_common.h"
#include "mali_osk.h"
#include "mali_osk_mali.h"
#include "mali_ukk.h"
#include "mali_timestamp.h"
#include "mali_osk_profiling.h"
#include "mali_user_settings_db.h"
#include "mali_profiling_internal.h"

typedef struct mali_profiling_entry
{
	u64 timestamp;
	u32 event_id;
	u32 data[5];
} mali_profiling_entry;


typedef enum mali_profiling_state
{
	MALI_PROFILING_STATE_UNINITIALIZED,
	MALI_PROFILING_STATE_IDLE,
	MALI_PROFILING_STATE_RUNNING,
	MALI_PROFILING_STATE_RETURN,
} mali_profiling_state;

static _mali_osk_lock_t *lock = NULL;
static mali_profiling_state prof_state = MALI_PROFILING_STATE_UNINITIALIZED;
static mali_profiling_entry* profile_entries = NULL;
static _mali_osk_atomic_t profile_insert_index;
static u32 profile_mask = 0;
static inline void add_event(u32 event_id, u32 data0, u32 data1, u32 data2, u32 data3, u32 data4);

void probe_mali_timeline_event(void *data, TP_PROTO(unsigned int event_id, unsigned int d0, unsigned int d1, unsigned
			int d2, unsigned int d3, unsigned int d4))
{
	add_event(event_id, d0, d1, d2, d3, d4);
}

_mali_osk_errcode_t _mali_internal_profiling_init(mali_bool auto_start)
{
	profile_entries = NULL;
	profile_mask = 0;
	_mali_osk_atomic_init(&profile_insert_index, 0);

	lock = _mali_osk_lock_init(_MALI_OSK_LOCKFLAG_ORDERED | _MALI_OSK_LOCKFLAG_NONINTERRUPTABLE, 0, _MALI_OSK_LOCK_ORDER_PROFILING);
	if (NULL == lock)
	{
		return _MALI_OSK_ERR_FAULT;
	}

	prof_state = MALI_PROFILING_STATE_IDLE;

	if (MALI_TRUE == auto_start)
	{
		u32 limit = MALI_PROFILING_MAX_BUFFER_ENTRIES; /* Use maximum buffer size */

		mali_set_user_setting(_MALI_UK_USER_SETTING_SW_EVENTS_ENABLE, MALI_TRUE);
		if (_MALI_OSK_ERR_OK != _mali_internal_profiling_start(&limit))
		{
			return _MALI_OSK_ERR_FAULT;
		}
	}

	return _MALI_OSK_ERR_OK;
}

void _mali_internal_profiling_term(void)
{
	u32 count;

	/* Ensure profiling is stopped */
	_mali_internal_profiling_stop(&count);

	prof_state = MALI_PROFILING_STATE_UNINITIALIZED;

	if (NULL != profile_entries)
	{
		_mali_osk_vfree(profile_entries);
		profile_entries = NULL;
	}

	if (NULL != lock)
	{
		_mali_osk_lock_term(lock);
		lock = NULL;
	}
}

_mali_osk_errcode_t _mali_internal_profiling_start(u32 * limit)
{
	_mali_osk_errcode_t ret;
	mali_profiling_entry *new_profile_entries;

	_mali_osk_lock_wait(lock, _MALI_OSK_LOCKMODE_RW);

	if (MALI_PROFILING_STATE_RUNNING == prof_state)
	{
		_mali_osk_lock_signal(lock, _MALI_OSK_LOCKMODE_RW);
		return _MALI_OSK_ERR_BUSY;
	}

	new_profile_entries = _mali_osk_valloc(*limit * sizeof(mali_profiling_entry));

	if (NULL == new_profile_entries)
	{
		_mali_osk_vfree(new_profile_entries);
		return _MALI_OSK_ERR_NOMEM;
	}

	if (MALI_PROFILING_MAX_BUFFER_ENTRIES < *limit)
	{
		*limit = MALI_PROFILING_MAX_BUFFER_ENTRIES;
	}

	profile_mask = 1;
	while (profile_mask <= *limit)
	{
		profile_mask <<= 1;
	}
	profile_mask >>= 1;

	*limit = profile_mask;

	profile_mask--; /* turns the power of two into a mask of one less */

	if (MALI_PROFILING_STATE_IDLE != prof_state)
	{
		_mali_osk_lock_signal(lock, _MALI_OSK_LOCKMODE_RW);
		_mali_osk_vfree(new_profile_entries);
		return _MALI_OSK_ERR_INVALID_ARGS; /* invalid to call this function in this state */
	}

	profile_entries = new_profile_entries;

	ret = _mali_timestamp_reset();

	if (_MALI_OSK_ERR_OK == ret)
	{
		prof_state = MALI_PROFILING_STATE_RUNNING;
	}
	else
	{
		_mali_osk_vfree(profile_entries);
		profile_entries = NULL;
	}

	register_trace_mali_timeline_event(probe_mali_timeline_event, NULL);

	_mali_osk_lock_signal(lock, _MALI_OSK_LOCKMODE_RW);
	return ret;
}

static inline void add_event(u32 event_id, u32 data0, u32 data1, u32 data2, u32 data3, u32 data4)
{
	u32 cur_index = (_mali_osk_atomic_inc_return(&profile_insert_index) - 1) & profile_mask;

	profile_entries[cur_index].timestamp = _mali_timestamp_get();
	profile_entries[cur_index].event_id = event_id;
	profile_entries[cur_index].data[0] = data0;
	profile_entries[cur_index].data[1] = data1;
	profile_entries[cur_index].data[2] = data2;
	profile_entries[cur_index].data[3] = data3;
	profile_entries[cur_index].data[4] = data4;

	/* If event is "leave API function", add current memory usage to the event
	 * as data point 4.  This is used in timeline profiling to indicate how
	 * much memory was used when leaving a function. */
	if (event_id == (MALI_PROFILING_EVENT_TYPE_SINGLE|MALI_PROFILING_EVENT_CHANNEL_SOFTWARE|MALI_PROFILING_EVENT_REASON_SINGLE_SW_LEAVE_API_FUNC))
	{
		profile_entries[cur_index].data[4] = _mali_ukk_report_memory_usage();
	}
}

_mali_osk_errcode_t _mali_internal_profiling_stop(u32 * count)
{
	_mali_osk_lock_wait(lock, _MALI_OSK_LOCKMODE_RW);

	if (MALI_PROFILING_STATE_RUNNING != prof_state)
	{
		_mali_osk_lock_signal(lock, _MALI_OSK_LOCKMODE_RW);
		return _MALI_OSK_ERR_INVALID_ARGS; /* invalid to call this function in this state */
	}

	/* go into return state (user to retreive events), no more events will be added after this */
	prof_state = MALI_PROFILING_STATE_RETURN;

	unregister_trace_mali_timeline_event(probe_mali_timeline_event, NULL);

	_mali_osk_lock_signal(lock, _MALI_OSK_LOCKMODE_RW);

	tracepoint_synchronize_unregister();

	*count = _mali_osk_atomic_read(&profile_insert_index);
	if (*count > profile_mask) *count = profile_mask;

	return _MALI_OSK_ERR_OK;
}

u32 _mali_internal_profiling_get_count(void)
{
	u32 retval = 0;

	_mali_osk_lock_wait(lock, _MALI_OSK_LOCKMODE_RW);
	if (MALI_PROFILING_STATE_RETURN == prof_state)
	{
		retval = _mali_osk_atomic_read(&profile_insert_index);
		if (retval > profile_mask) retval = profile_mask;
	}
	_mali_osk_lock_signal(lock, _MALI_OSK_LOCKMODE_RW);

	return retval;
}

_mali_osk_errcode_t _mali_internal_profiling_get_event(u32 index, u64* timestamp, u32* event_id, u32 data[5])
{
	u32 raw_index = _mali_osk_atomic_read(&profile_insert_index);

	_mali_osk_lock_wait(lock, _MALI_OSK_LOCKMODE_RW);

	if (index < profile_mask)
	{
		if ((raw_index & ~profile_mask) != 0)
		{
			index += raw_index;
			index &= profile_mask;
		}

		if (prof_state != MALI_PROFILING_STATE_RETURN)
		{
			_mali_osk_lock_signal(lock, _MALI_OSK_LOCKMODE_RW);
			return _MALI_OSK_ERR_INVALID_ARGS; /* invalid to call this function in this state */
		}

		if(index >= raw_index)
		{
			_mali_osk_lock_signal(lock, _MALI_OSK_LOCKMODE_RW);
			return _MALI_OSK_ERR_FAULT;
		}

		*timestamp = profile_entries[index].timestamp;
		*event_id = profile_entries[index].event_id;
		data[0] = profile_entries[index].data[0];
		data[1] = profile_entries[index].data[1];
		data[2] = profile_entries[index].data[2];
		data[3] = profile_entries[index].data[3];
		data[4] = profile_entries[index].data[4];
	}
	else
	{
		_mali_osk_lock_signal(lock, _MALI_OSK_LOCKMODE_RW);
		return _MALI_OSK_ERR_FAULT;
	}

	_mali_osk_lock_signal(lock, _MALI_OSK_LOCKMODE_RW);
	return _MALI_OSK_ERR_OK;
}

_mali_osk_errcode_t _mali_internal_profiling_clear(void)
{
	_mali_osk_lock_wait(lock, _MALI_OSK_LOCKMODE_RW);

	if (MALI_PROFILING_STATE_RETURN != prof_state)
	{
		_mali_osk_lock_signal(lock, _MALI_OSK_LOCKMODE_RW);
		return _MALI_OSK_ERR_INVALID_ARGS; /* invalid to call this function in this state */
	}

	prof_state = MALI_PROFILING_STATE_IDLE;
	profile_mask = 0;
	_mali_osk_atomic_init(&profile_insert_index, 0);

	if (NULL != profile_entries)
	{
		_mali_osk_vfree(profile_entries);
		profile_entries = NULL;
	}

	_mali_osk_lock_signal(lock, _MALI_OSK_LOCKMODE_RW);
	return _MALI_OSK_ERR_OK;
}

mali_bool _mali_internal_profiling_is_recording(void)
{
	return prof_state == MALI_PROFILING_STATE_RUNNING ? MALI_TRUE : MALI_FALSE;
}

mali_bool _mali_internal_profiling_have_recording(void)
{
	return prof_state == MALI_PROFILING_STATE_RETURN ? MALI_TRUE : MALI_FALSE;
}
