/*
 * This confidential and proprietary software may be used only as
 * authorised by a licensing agreement from ARM Limited
 * (C) COPYRIGHT 2011-2014 ARM Limited
 * ALL RIGHTS RESERVED
 * The entire notice above must be reproduced on all authorised
 * copies and copies may only be made to the extent permitted
 * by a licensing agreement from ARM Limited.
 */

#include "mali_kernel_common.h"
#include "mali_osk.h"
#include "mali_ukk.h"

#if defined(CONFIG_MALI400_PROFILING)
#include "mali_osk_profiling.h"
#endif

_mali_osk_errcode_t _mali_ukk_vsync_event_report(_mali_uk_vsync_event_report_s *args)
{
	_mali_uk_vsync_event event = (_mali_uk_vsync_event)args->event;
	MALI_IGNORE(event); /* event is not used for release code, and that is OK */

#if defined(CONFIG_MALI400_PROFILING)
	/*
	 * Manually generate user space events in kernel space.
	 * This saves user space from calling kernel space twice in this case.
	 * We just need to remember to add pid and tid manually.
	 */
	if (event == _MALI_UK_VSYNC_EVENT_BEGIN_WAIT) {
		_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_SUSPEND |
					      MALI_PROFILING_EVENT_CHANNEL_SOFTWARE |
					      MALI_PROFILING_EVENT_REASON_SUSPEND_RESUME_SW_VSYNC,
					      _mali_osk_get_pid(), _mali_osk_get_tid(), 0, 0, 0);
	}

	if (event == _MALI_UK_VSYNC_EVENT_END_WAIT) {

		_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_RESUME |
					      MALI_PROFILING_EVENT_CHANNEL_SOFTWARE |
					      MALI_PROFILING_EVENT_REASON_SUSPEND_RESUME_SW_VSYNC,
					      _mali_osk_get_pid(), _mali_osk_get_tid(), 0, 0, 0);
	}
#endif

	MALI_DEBUG_PRINT(4, ("Received VSYNC event: %d\n", event));
	MALI_SUCCESS;
}

