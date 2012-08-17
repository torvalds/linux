/*
 * Copyright (C) 2011-2012 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "mali_kernel_common.h"
#include "mali_osk.h"
#include "mali_ukk.h"

#if MALI_TIMELINE_PROFILING_ENABLED
#include "mali_osk_profiling.h"
#endif

_mali_osk_errcode_t _mali_ukk_vsync_event_report(_mali_uk_vsync_event_report_s *args)
{
	_mali_uk_vsync_event event = (_mali_uk_vsync_event)args->event;
	MALI_IGNORE(event); /* event is not used for release code, and that is OK */

#if MALI_TIMELINE_PROFILING_ENABLED
	/*
	 * Manually generate user space events in kernel space.
	 * This saves user space from calling kernel space twice in this case.
	 * We just need to remember to add pid and tid manually.
	 */
	if ( event==_MALI_UK_VSYNC_EVENT_BEGIN_WAIT)
	{
		_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_SUSPEND |
		                              MALI_PROFILING_EVENT_CHANNEL_SOFTWARE |
		                              MALI_PROFILING_EVENT_REASON_SUSPEND_RESUME_SW_VSYNC,
		                              _mali_osk_get_pid(), _mali_osk_get_tid(), 0, 0, 0);
	}

	if (event==_MALI_UK_VSYNC_EVENT_END_WAIT)
	{

		_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_RESUME |
		                              MALI_PROFILING_EVENT_CHANNEL_SOFTWARE |
		                              MALI_PROFILING_EVENT_REASON_SUSPEND_RESUME_SW_VSYNC,
		                              _mali_osk_get_pid(), _mali_osk_get_tid(), 0, 0, 0);
	}
#endif

	MALI_DEBUG_PRINT(4, ("Received VSYNC event: %d\n", event));
	MALI_SUCCESS;
}

