/*
 * Copyright (C) 2010-2011, 2013-2014 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __MALI_TIMESTAMP_H__
#define __MALI_TIMESTAMP_H__

#include "mali_osk.h"

MALI_STATIC_INLINE _mali_osk_errcode_t _mali_timestamp_reset(void)
{
	return _MALI_OSK_ERR_OK;
}

MALI_STATIC_INLINE u64 _mali_timestamp_get(void)
{
	return _mali_osk_boot_time_get_ns();
}

#endif /* __MALI_TIMESTAMP_H__ */
