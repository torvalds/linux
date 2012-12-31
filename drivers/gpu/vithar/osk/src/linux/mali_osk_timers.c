/*
 *
 * (C) COPYRIGHT 2011 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 */



#include <osk/mali_osk.h>

#if MALI_DEBUG != 0
#include <linux/delay.h>

#define TIMER_PERIOD_NS 100
#define TIMER_TEST_TIME_MS 1000
typedef struct oskp_time_test
{
	osk_timer timer;
	u32 val;
	mali_bool should_stop;
} oskp_time_test;

static mali_bool oskp_timer_has_been_checked = MALI_FALSE;
#endif

enum hrtimer_restart oskp_timer_callback_wrapper( struct hrtimer * hr_timer )
{
	osk_timer *tim;

	tim = CONTAINER_OF( hr_timer, osk_timer, timer );
	tim->callback( tim->data );

	return HRTIMER_NORESTART;
}

#if MALI_DEBUG != 0
static void oskp_check_timer_callback( void *data )
{
	oskp_time_test *time_tester = (oskp_time_test*)data;

	(time_tester->val)++;

	if ( time_tester->should_stop == MALI_FALSE )
	{
		osk_error err;
		err = osk_timer_start_ns( &time_tester->timer, TIMER_PERIOD_NS );
		if ( err != OSK_ERR_NONE )
		{
			OSK_PRINT_WARN( OSK_BASE_CORE, "OSK Timer couldn't restart - testing stats will be inaccurate" );
		}
	}
}

void oskp_debug_test_timer_stats( void )
{
	oskp_time_test time_tester;
	osk_ticks start_timestamp;
	osk_ticks end_timestamp;
	u32 msec_elapsed;
	osk_error err;

	if ( oskp_timer_has_been_checked != MALI_FALSE )
	{
		return;
	}
	oskp_timer_has_been_checked = MALI_TRUE;

	OSK_MEMSET( &time_tester, 0, sizeof(time_tester) );

	err = osk_timer_on_stack_init( &time_tester.timer );
	if ( err != OSK_ERR_NONE )
	{
		goto fail_init;
	}

	osk_timer_callback_set( &time_tester.timer, &oskp_check_timer_callback, &time_tester );

	start_timestamp = osk_time_now();
	err = osk_timer_start_ns( &time_tester.timer, TIMER_PERIOD_NS );
	if ( err != OSK_ERR_NONE )
	{
		goto fail_start;
	}

	msleep( TIMER_TEST_TIME_MS );

	time_tester.should_stop = MALI_TRUE;

	osk_timer_stop( &time_tester.timer );
	end_timestamp = osk_time_now();

	msec_elapsed = osk_time_elapsed( start_timestamp, end_timestamp );

	OSK_PRINT( OSK_BASE_CORE, "OSK Timer did %d iterations in %dms", time_tester.val, msec_elapsed );

	osk_timer_on_stack_term( &time_tester.timer );
	return;

 fail_start:
	osk_timer_on_stack_term( &time_tester.timer );
 fail_init:
	OSK_PRINT_WARN( OSK_BASE_CORE, "OSK Timer couldn't init/start for testing stats" );
	return;
}
#endif
