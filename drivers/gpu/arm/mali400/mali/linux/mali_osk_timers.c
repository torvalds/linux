/*
 * This confidential and proprietary software may be used only as
 * authorised by a licensing agreement from ARM Limited
 * (C) COPYRIGHT 2008-2014 ARM Limited
 * ALL RIGHTS RESERVED
 * The entire notice above must be reproduced on all authorised
 * copies and copies may only be made to the extent permitted
 * by a licensing agreement from ARM Limited.
 */

/**
 * @file mali_osk_timers.c
 * Implementation of the OS abstraction layer for the kernel device driver
 */

#include <linux/timer.h>
#include <linux/slab.h>
#include "mali_osk.h"
#include "mali_kernel_common.h"

struct _mali_osk_timer_t_struct {
	struct timer_list timer;
};

typedef void (*timer_timeout_function_t)(unsigned long);

_mali_osk_timer_t *_mali_osk_timer_init(void)
{
	_mali_osk_timer_t *t = (_mali_osk_timer_t *)kmalloc(sizeof(_mali_osk_timer_t), GFP_KERNEL);
	if (NULL != t) init_timer(&t->timer);
	return t;
}

void _mali_osk_timer_add(_mali_osk_timer_t *tim, u32 ticks_to_expire)
{
	MALI_DEBUG_ASSERT_POINTER(tim);
	tim->timer.expires = jiffies + ticks_to_expire;
	add_timer(&(tim->timer));
}

void _mali_osk_timer_mod(_mali_osk_timer_t *tim, u32 ticks_to_expire)
{
	MALI_DEBUG_ASSERT_POINTER(tim);
	mod_timer(&(tim->timer), jiffies + ticks_to_expire);
}

void _mali_osk_timer_del(_mali_osk_timer_t *tim)
{
	MALI_DEBUG_ASSERT_POINTER(tim);
	del_timer_sync(&(tim->timer));
}

void _mali_osk_timer_del_async(_mali_osk_timer_t *tim)
{
	MALI_DEBUG_ASSERT_POINTER(tim);
	del_timer(&(tim->timer));
}

mali_bool _mali_osk_timer_pending(_mali_osk_timer_t *tim)
{
	MALI_DEBUG_ASSERT_POINTER(tim);
	return 1 == timer_pending(&(tim->timer));
}

void _mali_osk_timer_setcallback(_mali_osk_timer_t *tim, _mali_osk_timer_callback_t callback, void *data)
{
	MALI_DEBUG_ASSERT_POINTER(tim);
	tim->timer.data = (unsigned long)data;
	tim->timer.function = (timer_timeout_function_t)callback;
}

void _mali_osk_timer_term(_mali_osk_timer_t *tim)
{
	MALI_DEBUG_ASSERT_POINTER(tim);
	kfree(tim);
}
