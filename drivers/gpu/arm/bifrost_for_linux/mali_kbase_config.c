/*
 *
 * (C) COPYRIGHT 2011-2015 ARM Limited. All rights reserved.
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

int kbase_cpuprops_get_default_clock_speed(u32 * const clock_speed)
{
	KBASE_DEBUG_ASSERT(NULL != clock_speed);

	*clock_speed = 100;
	return 0;
}

