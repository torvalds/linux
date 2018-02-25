/*
 *
 * (C) COPYRIGHT 2011-2015,2017 ARM Limited. All rights reserved.
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
#include <mali_kbase_defs.h>
#include <mali_kbase_config_defaults.h>

int kbasep_platform_device_init(struct kbase_device *kbdev)
{
	struct kbase_platform_funcs_conf *platform_funcs_p;

	platform_funcs_p = (struct kbase_platform_funcs_conf *)PLATFORM_FUNCS;
	if (platform_funcs_p && platform_funcs_p->platform_init_func)
		return platform_funcs_p->platform_init_func(kbdev);

	return 0;
}

void kbasep_platform_device_term(struct kbase_device *kbdev)
{
	struct kbase_platform_funcs_conf *platform_funcs_p;

	platform_funcs_p = (struct kbase_platform_funcs_conf *)PLATFORM_FUNCS;
	if (platform_funcs_p && platform_funcs_p->platform_term_func)
		platform_funcs_p->platform_term_func(kbdev);
}

