/*
 *
 * (C) COPYRIGHT 2012-2014 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 * SPDX-License-Identifier: GPL-2.0
 *
 */



#include <mali_kbase.h>

static struct kbasep_debug_assert_cb kbasep_debug_assert_registered_cb = {
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

