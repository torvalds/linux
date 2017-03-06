/**
 * Copyright (C) 2012-2014, 2016-2017 ARM Limited. All rights reserved.
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
#include "mali_uk_types.h"
#include "mali_user_settings_db.h"
#include "mali_session.h"

static u32 mali_user_settings[_MALI_UK_USER_SETTING_MAX];
const char *_mali_uk_user_setting_descriptions[] = _MALI_UK_USER_SETTING_DESCRIPTIONS;

static void mali_user_settings_notify(_mali_uk_user_setting_t setting, u32 value)
{
	mali_bool done = MALI_FALSE;

	/*
	 * This function gets a bit complicated because we can't hold the session lock while
	 * allocating notification objects.
	 */

	while (!done) {
		u32 i;
		u32 num_sessions_alloc;
		u32 num_sessions_with_lock;
		u32 used_notification_objects = 0;
		_mali_osk_notification_t **notobjs;

		/* Pre allocate the number of notifications objects we need right now (might change after lock has been taken) */
		num_sessions_alloc = mali_session_get_count();
		if (0 == num_sessions_alloc) {
			/* No sessions to report to */
			return;
		}

		notobjs = (_mali_osk_notification_t **)_mali_osk_malloc(sizeof(_mali_osk_notification_t *) * num_sessions_alloc);
		if (NULL == notobjs) {
			MALI_PRINT_ERROR(("Failed to notify user space session about num PP core change (alloc failure)\n"));
			return;
		}

		for (i = 0; i < num_sessions_alloc; i++) {
			notobjs[i] = _mali_osk_notification_create(_MALI_NOTIFICATION_SETTINGS_CHANGED,
					sizeof(_mali_uk_settings_changed_s));
			if (NULL != notobjs[i]) {
				_mali_uk_settings_changed_s *data;
				data = notobjs[i]->result_buffer;

				data->setting = setting;
				data->value = value;
			} else {
				MALI_PRINT_ERROR(("Failed to notify user space session about setting change (alloc failure %u)\n", i));
			}
		}

		mali_session_lock();

		/* number of sessions will not change while we hold the lock */
		num_sessions_with_lock = mali_session_get_count();

		if (num_sessions_alloc >= num_sessions_with_lock) {
			/* We have allocated enough notification objects for all the sessions atm */
			struct mali_session_data *session, *tmp;
			MALI_SESSION_FOREACH(session, tmp, link) {
				MALI_DEBUG_ASSERT(used_notification_objects < num_sessions_alloc);
				if (NULL != notobjs[used_notification_objects]) {
					mali_session_send_notification(session, notobjs[used_notification_objects]);
					notobjs[used_notification_objects] = NULL; /* Don't track this notification object any more */
				}
				used_notification_objects++;
			}
			done = MALI_TRUE;
		}

		mali_session_unlock();

		/* Delete any remaining/unused notification objects */
		for (; used_notification_objects < num_sessions_alloc; used_notification_objects++) {
			if (NULL != notobjs[used_notification_objects]) {
				_mali_osk_notification_delete(notobjs[used_notification_objects]);
			}
		}

		_mali_osk_free(notobjs);
	}
}

void mali_set_user_setting(_mali_uk_user_setting_t setting, u32 value)
{
	mali_bool notify = MALI_FALSE;

	if (setting >= _MALI_UK_USER_SETTING_MAX) {
		MALI_DEBUG_PRINT_ERROR(("Invalid user setting %ud\n"));
		return;
	}

	if (mali_user_settings[setting] != value) {
		notify = MALI_TRUE;
	}

	mali_user_settings[setting] = value;

	if (notify) {
		mali_user_settings_notify(setting, value);
	}
}

u32 mali_get_user_setting(_mali_uk_user_setting_t setting)
{
	if (setting >= _MALI_UK_USER_SETTING_MAX) {
		return 0;
	}

	return mali_user_settings[setting];
}

_mali_osk_errcode_t _mali_ukk_get_user_setting(_mali_uk_get_user_setting_s *args)
{
	_mali_uk_user_setting_t setting;
	MALI_DEBUG_ASSERT_POINTER(args);

	setting = args->setting;

	if (_MALI_UK_USER_SETTING_MAX > setting) {
		args->value = mali_user_settings[setting];
		return _MALI_OSK_ERR_OK;
	} else {
		return _MALI_OSK_ERR_INVALID_ARGS;
	}
}

_mali_osk_errcode_t _mali_ukk_get_user_settings(_mali_uk_get_user_settings_s *args)
{
	MALI_DEBUG_ASSERT_POINTER(args);

	_mali_osk_memcpy(args->settings, mali_user_settings, sizeof(mali_user_settings));

	return _MALI_OSK_ERR_OK;
}
