/*
 *
 * (C) COPYRIGHT ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained
 * from Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 */





#include <mali_kbase.h>

kbasep_debug_assert_cb kbasep_debug_assert_registered_cb = {
	NULL,
	NULL
};

void kbase_debug_assert_register_hook(kbase_debug_assert_hook *func, void *param)
{
	kbasep_debug_assert_registered_cb.func = func;
	kbasep_debug_assert_registered_cb.param = param;
}

void kbasep_debug_assert_call_hook(void)
{
	if (kbasep_debug_assert_registered_cb.func != NULL)
		kbasep_debug_assert_registered_cb.func(kbasep_debug_assert_registered_cb.param);
}
KBASE_EXPORT_SYMBOL(kbasep_debug_assert_call_hook);

