/* include/linux/android_alarm.h
 *
 * Copyright (C) 2006-2007 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef _LINUX_ANDROID_ALARM_H
#define _LINUX_ANDROID_ALARM_H

#include <linux/compat.h>
#include <linux/ioctl.h>

#include "uapi/android_alarm.h"

#ifdef CONFIG_COMPAT
#define ANDROID_ALARM_SET_COMPAT(type)		ALARM_IOW(2, type, \
							struct compat_timespec)
#define ANDROID_ALARM_SET_AND_WAIT_COMPAT(type)	ALARM_IOW(3, type, \
							struct compat_timespec)
#define ANDROID_ALARM_GET_TIME_COMPAT(type)	ALARM_IOW(4, type, \
							struct compat_timespec)
#define ANDROID_ALARM_SET_RTC_COMPAT		_IOW('a', 5, \
							struct compat_timespec)
#define ANDROID_ALARM_IOCTL_NR(cmd)		(_IOC_NR(cmd) & ((1<<4)-1))
#define ANDROID_ALARM_COMPAT_TO_NORM(cmd)  \
				ALARM_IOW(ANDROID_ALARM_IOCTL_NR(cmd), \
					ANDROID_ALARM_IOCTL_TO_TYPE(cmd), \
					struct timespec)

#endif

#endif
