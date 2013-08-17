/*
 *
 * (C) COPYRIGHT 2010-2012 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 */



/**
 * @file
 * Implementation of the OS abstraction layer for the kernel device driver
 */

#ifndef _OSK_ARCH_TIMERS_H
#define _OSK_ARCH_TIMERS_H

#ifndef _OSK_H_
#error "Include mali_osk.h directly"
#endif

#ifdef CONFIG_MALI_DEBUG
void oskp_debug_test_timer_stats( void );
#endif

enum hrtimer_restart oskp_timer_callback_wrapper( struct hrtimer * hr_timer );

OSK_STATIC_INLINE osk_error osk_timer_init(osk_timer * const tim)
{
	OSK_ASSERT(NULL != tim);

	OSK_DEBUG_CODE( oskp_debug_test_timer_stats() );

	hrtimer_init(&tim->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	tim->timer.function = NULL;

	OSK_DEBUG_CODE(	tim->active = MALI_FALSE );
	OSK_ASSERT(0 ==	object_is_on_stack(tim));
	return OSK_ERR_NONE;
}

OSK_STATIC_INLINE osk_error osk_timer_on_stack_init(osk_timer * const tim)
{
	OSK_ASSERT(NULL != tim);
	hrtimer_init_on_stack(&tim->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	tim->timer.function = NULL;

	OSK_DEBUG_CODE(	tim->active = MALI_FALSE );
	OSK_ASSERT(0 !=	object_is_on_stack(tim));
	return OSK_ERR_NONE;
}

OSK_STATIC_INLINE osk_error osk_timer_start(osk_timer *tim, u32 delay)
{
	osk_error err;
	u64 delay_ns = delay * (u64)1000000U;

	err = osk_timer_start_ns( tim, delay_ns );

	return err;
}

OSK_STATIC_INLINE osk_error osk_timer_start_ns(osk_timer *tim, u64 delay_ns)
{
	ktime_t kdelay;
	int was_active;
	OSK_ASSERT(NULL != tim);
	OSK_ASSERT(NULL != tim->timer.function);
	OSK_ASSERT(delay_ns != 0);

	kdelay = ns_to_ktime( delay_ns );

	was_active = hrtimer_start( &tim->timer, kdelay, HRTIMER_MODE_REL );

	OSK_ASSERT( was_active == 0 ); /* You cannot start a timer that has already been started */

	CSTD_UNUSED( was_active );
	OSK_DEBUG_CODE(	tim->active = MALI_TRUE );
	return OSK_ERR_NONE;
}

OSK_STATIC_INLINE osk_error osk_timer_modify(osk_timer *tim, u32 new_delay)
{
	osk_error err;
	u64 delay_ns = new_delay * (u64)1000000U;

	err = osk_timer_modify_ns( tim, delay_ns );
	return err;
}

OSK_STATIC_INLINE osk_error osk_timer_modify_ns(osk_timer *tim, u64 new_delay_ns)
{
	ktime_t kdelay;
	int was_active;
	OSK_ASSERT(NULL != tim);
	OSK_ASSERT(NULL != tim->timer.function);
	OSK_ASSERT(0 != new_delay_ns);

	kdelay = ns_to_ktime( new_delay_ns );

	/* hrtimers will stop the existing timer if it's running on any cpu, so
	 * it's safe just to start the timer again: */
	was_active = hrtimer_start( &tim->timer, kdelay, HRTIMER_MODE_REL );

	CSTD_UNUSED( was_active );
	OSK_DEBUG_CODE(	tim->active = MALI_TRUE );
	return OSK_ERR_NONE;
}

OSK_STATIC_INLINE void osk_timer_stop(osk_timer *tim)
{
	int was_active;
	OSK_ASSERT(NULL != tim);
	OSK_ASSERT(NULL != tim->timer.function);

	was_active = hrtimer_cancel(&tim->timer);

	CSTD_UNUSED( was_active );
	OSK_DEBUG_CODE( tim->active = MALI_FALSE );
}

OSK_STATIC_INLINE void osk_timer_callback_set(osk_timer *tim, osk_timer_callback callback, void *data)
{
	OSK_ASSERT(NULL != tim);
	OSK_ASSERT(NULL != callback);
	OSK_DEBUG_CODE(
		if (MALI_FALSE == tim->active)
		{
		}
	);

	tim->timer.function = &oskp_timer_callback_wrapper;

	/* osk_timer_callback uses void * for the callback parameter instead of unsigned long in Linux */
	tim->callback = callback;
	tim->data = data;
}

OSK_STATIC_INLINE void osk_timer_term(osk_timer *tim)
{
	OSK_ASSERT(NULL != tim);
	OSK_ASSERT(0 ==	object_is_on_stack(tim));
	OSK_DEBUG_CODE(
		if (MALI_FALSE == tim->active)
		{
		}
	);
	/* Nothing to do */
}

OSK_STATIC_INLINE void osk_timer_on_stack_term(osk_timer *tim)
{
	OSK_ASSERT(NULL != tim);
	OSK_ASSERT(0 !=	object_is_on_stack(tim));
	OSK_DEBUG_CODE(
		if (MALI_FALSE == tim->active)
		{
		}
	);
	destroy_hrtimer_on_stack(&tim->timer);
}

#endif /* _OSK_ARCH_TIMERS_H_ */

