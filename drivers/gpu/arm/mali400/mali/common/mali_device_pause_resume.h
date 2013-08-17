/*
 * Copyright (C) 2011-2012 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __MALI_DEVICE_PAUSE_RESUME_H__
#define __MALI_DEVICE_PAUSE_RESUME_H__

#include "mali_osk.h"

/**
 * Pause the scheduling and power state changes of Mali device driver.
 * mali_dev_resume() must always be called as soon as possible after this function
 * in order to resume normal operation of the Mali driver.
 *
 * @param power_is_on Receives the power current status of Mali GPU. MALI_TRUE if GPU is powered on
 */
void mali_dev_pause(mali_bool *power_is_on);

/**
 * Resume scheduling and allow power changes in Mali device driver.
 * This must always be called after mali_dev_pause().
 */
void mali_dev_resume(void);

#endif /* __MALI_DEVICE_PAUSE_RESUME_H__ */
