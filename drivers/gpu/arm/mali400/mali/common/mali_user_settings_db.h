/**
 * Copyright (C) 2012-2013, 2016-2017 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __MALI_USER_SETTINGS_DB_H__
#define __MALI_USER_SETTINGS_DB_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "mali_uk_types.h"

/** @brief Set Mali user setting in DB
 *
 * Update the DB with a new value for \a setting. If the value is different from theprevious set value running sessions will be notified of the change.
 *
 * @param setting the setting to be changed
 * @param value the new value to set
 */
void mali_set_user_setting(_mali_uk_user_setting_t setting, u32 value);

/** @brief Get current Mali user setting value from DB
 *
 * @param setting the setting to extract
 * @return the value of the selected setting
 */
u32 mali_get_user_setting(_mali_uk_user_setting_t setting);

#ifdef __cplusplus
}
#endif
#endif  /* __MALI_KERNEL_USER_SETTING__ */
