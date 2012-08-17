/**
 * Copyright (C) 2012 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "mali_osk.h"
#include "mali_kernel_common.h"
#include "mali_uk_types.h"
#include "mali_user_settings_db.h"
#include "mali_session.h"

static u32 mali_user_settings[_MALI_UK_USER_SETTING_MAX];
const char *_mali_uk_user_setting_descriptions[] = _MALI_UK_USER_SETTING_DESCRIPTIONS;

static void mali_user_settings_notify(_mali_uk_user_setting_t setting, u32 value)
{
	struct mali_session_data *session, *tmp;

	mali_session_lock();
	MALI_SESSION_FOREACH(session, tmp, link)
	{
		_mali_osk_notification_t *notobj = _mali_osk_notification_create(_MALI_NOTIFICATION_SETTINGS_CHANGED, sizeof(_mali_uk_settings_changed_s));
		_mali_uk_settings_changed_s *data = notobj->result_buffer;
		data->setting = setting;
		data->value = value;

		mali_session_send_notification(session, notobj);
	}
	mali_session_unlock();
}

void mali_set_user_setting(_mali_uk_user_setting_t setting, u32 value)
{
	mali_bool notify = MALI_FALSE;

	MALI_DEBUG_ASSERT(setting < _MALI_UK_USER_SETTING_MAX && setting >= 0);

	if (mali_user_settings[setting] != value)
	{
		notify = MALI_TRUE;
	}

	mali_user_settings[setting] = value;

	if (notify)
	{
		mali_user_settings_notify(setting, value);
	}
}

u32 mali_get_user_setting(_mali_uk_user_setting_t setting)
{
	MALI_DEBUG_ASSERT(setting < _MALI_UK_USER_SETTING_MAX && setting >= 0);

	return mali_user_settings[setting];
}

_mali_osk_errcode_t _mali_ukk_get_user_setting(_mali_uk_get_user_setting_s *args)
{
	_mali_uk_user_setting_t setting;
	MALI_DEBUG_ASSERT_POINTER(args);

	setting = args->setting;

	if (0 <= setting && _MALI_UK_USER_SETTING_MAX > setting)
	{
		args->value = mali_user_settings[setting];
		return _MALI_OSK_ERR_OK;
	}
	else
	{
		return _MALI_OSK_ERR_INVALID_ARGS;
	}
}

_mali_osk_errcode_t _mali_ukk_get_user_settings(_mali_uk_get_user_settings_s *args)
{
	MALI_DEBUG_ASSERT_POINTER(args);

	_mali_osk_memcpy(args->settings, mali_user_settings, (sizeof(u32) * _MALI_UK_USER_SETTING_MAX));

	return _MALI_OSK_ERR_OK;
}
