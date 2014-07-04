/*
 * This confidential and proprietary software may be used only as
 * authorised by a licensing agreement from ARM Limited
 * (C) COPYRIGHT 2012-2013 ARM Limited
 * ALL RIGHTS RESERVED
 * The entire notice above must be reproduced on all authorised
 * copies and copies may only be made to the extent permitted
 * by a licensing agreement from ARM Limited.
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
_mali_osk_errcode_t _mali_internal_profiling_get_event(u32 index, u64* timestamp, u32* event_id, u32 data[5]);
u32 _mali_internal_profiling_get_count(void);
int _mali_internal_profiling_stop(u32 * count);
int _mali_internal_profiling_start(u32 * limit);

#ifdef __cplusplus
}
#endif

#endif /* __MALI_PROFILING_INTERNAL_H__ */
