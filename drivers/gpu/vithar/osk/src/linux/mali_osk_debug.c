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



#include <osk/mali_osk.h>

oskp_debug_assert_cb oskp_debug_assert_registered_cb =
{
	NULL,
	NULL
};

void oskp_debug_print(const char *format, ...)
{
	va_list args;
	va_start(args, format);
	oskp_validate_format_string(format);
	vprintk(format, args);
	va_end(args);
}

s32 osk_snprintf(char *str, size_t size, const char *format, ...)
{
	va_list args;
	s32 ret;
	va_start(args, format);
	oskp_validate_format_string(format);
	ret = vsnprintf(str, size, format, args);
	va_end(args);
	return ret;
}

void osk_debug_assert_register_hook( osk_debug_assert_hook *func, void *param )
{
	oskp_debug_assert_registered_cb.func = func;
	oskp_debug_assert_registered_cb.param = param;
}

void oskp_debug_assert_call_hook( void )
{
	if ( oskp_debug_assert_registered_cb.func != NULL )
	{
		oskp_debug_assert_registered_cb.func( oskp_debug_assert_registered_cb.param );
	}
}

#if MALI_DEBUG
#include <linux/percpu.h>
DEFINE_PER_CPU(int[2], slot_in_use);

void osk_kmap_debug(int slot)
{
	int slot_was_in_use = get_cpu_var(slot_in_use[slot])++;
	put_cpu_var(slot_in_use);
	OSK_ASSERT(slot_was_in_use == 0);
	BUG_ON(slot_was_in_use);
}

void osk_kunmap_debug(int slot)
{
	int slot_was_in_use = get_cpu_var(slot_in_use[slot])--;
	put_cpu_var(slot_in_use);
	OSK_ASSERT(slot_was_in_use == 1);
	BUG_ON(!slot_was_in_use);
}
#endif /* MALI_DEBUG */
