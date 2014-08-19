/*
 * This confidential and proprietary software may be used only as
 * authorised by a licensing agreement from ARM Limited
 * (C) COPYRIGHT 2010-2011, 2013-2014 ARM Limited
 * ALL RIGHTS RESERVED
 * The entire notice above must be reproduced on all authorised
 * copies and copies may only be made to the extent permitted
 * by a licensing agreement from ARM Limited.
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
	return _mali_osk_time_get_ns();
}

#endif /* __MALI_TIMESTAMP_H__ */
