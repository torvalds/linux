/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 *
 ******************************************************************************/
#ifndef __DRV_CONF_H__
#define __DRV_CONF_H__
#include "autoconf.h"


#ifdef CONFIG_ANDROID
/* Some Android build will restart the UI while non-printable ascii is passed */
/* between java and c/c++ layer (JNI). We force CONFIG_VALIDATE_SSID */
/* for Android here. If you are sure there is no risk on your system about this, */
/* mask this macro define to support non-printable ascii ssid. */
/* define CONFIG_VALIDATE_SSID */
/* Android expect dbm as the rx signal strength unit */
#define CONFIG_SIGNAL_DISPLAY_DBM
#endif

#if defined(CONFIG_HAS_EARLYSUSPEND) && defined (CONFIG_RESUME_IN_WORKQUEUE)
	#warning "You have CONFIG_HAS_EARLYSUSPEND enabled in your system, we disable CONFIG_RESUME_IN_WORKQUEUE automatically"
	#undef CONFIG_RESUME_IN_WORKQUEUE
#endif

#if defined(CONFIG_ANDROID_POWER) && defined (CONFIG_RESUME_IN_WORKQUEUE)
	#warning "You have CONFIG_ANDROID_POWER enabled in your system, we disable CONFIG_RESUME_IN_WORKQUEUE automatically"
	#undef CONFIG_RESUME_IN_WORKQUEUE
#endif

#ifdef CONFIG_RESUME_IN_WORKQUEUE /* this can be removed, because there is no case for this... */
	#if !defined(CONFIG_WAKELOCK) && !defined(CONFIG_ANDROID_POWER)
	#error "enable CONFIG_RESUME_IN_WORKQUEUE without CONFIG_WAKELOCK or CONFIG_ANDROID_POWER will suffer from the danger of wifi's unfunctionality..."
	#error "If you still want to enable CONFIG_RESUME_IN_WORKQUEUE in this case, mask this preprossor checking and GOOD LUCK..."
	#endif
#endif

#endif /*  __DRV_CONF_H__ */
