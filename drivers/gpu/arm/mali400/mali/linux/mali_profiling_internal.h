/*
 * Copyright (C) 2012-2014 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __MALI_PROFILING_INTERNAL_H__
#define __MALI_PROFILING_INTERNAL_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "mali_osk.h"

int _mali_internal_profiling_init(mali_bool auto_start);
void _mali_internal_profiling_term(void);

mali_bool _mali_internal_profiling_is_recording(void);
mali_bool _mali_internal_profiling_have_recording(void);
_mali_osk_errcode_t _mali_internal_profiling_clear(void);
_mali_osk_errcode_t _mali_internal_profiling_get_event(u32 index, u64 *timestamp, u32 *event_id, u32 data[5]);
u32 _mali_internal_profiling_get_count(void);
int _mali_internal_profiling_stop(u32 *count);
int _mali_internal_profiling_start(u32 *limit);

#ifdef __cplusplus
}
#endif

#endif /* __MALI_PROFILING_INTERNAL_H__ */
