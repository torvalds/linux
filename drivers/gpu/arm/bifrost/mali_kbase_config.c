// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2011-2015, 2017, 2020-2021 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU license.
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

int kbasep_platform_device_late_init(struct kbase_device *kbdev)
{
	struct kbase_platform_funcs_conf *platform_funcs_p;

	platform_funcs_p = (struct kbase_platform_funcs_conf *)PLATFORM_FUNCS;
	if (platform_funcs_p && platform_funcs_p->platform_late_init_func)
		platform_funcs_p->platform_late_init_func(kbdev);

	return 0;
}

void kbasep_platform_device_late_term(struct kbase_device *kbdev)
{
	struct kbase_platform_funcs_conf *platform_funcs_p;

	platform_funcs_p = (struct kbase_platform_funcs_conf *)PLATFORM_FUNCS;
	if (platform_funcs_p && platform_funcs_p->platform_late_term_func)
		platform_funcs_p->platform_late_term_func(kbdev);
}

#if !MALI_USE_CSF
int kbasep_platform_context_init(struct kbase_context *kctx)
{
	struct kbase_platform_funcs_conf *platform_funcs_p;

	platform_funcs_p = (struct kbase_platform_funcs_conf *)PLATFORM_FUNCS;
	if (platform_funcs_p && platform_funcs_p->platform_handler_context_init_func)
		return platform_funcs_p->platform_handler_context_init_func(kctx);

	return 0;
}

void kbasep_platform_context_term(struct kbase_context *kctx)
{
	struct kbase_platform_funcs_conf *platform_funcs_p;

	platform_funcs_p = (struct kbase_platform_funcs_conf *)PLATFORM_FUNCS;
	if (platform_funcs_p && platform_funcs_p->platform_handler_context_term_func)
		platform_funcs_p->platform_handler_context_term_func(kctx);
}

void kbasep_platform_event_atom_submit(struct kbase_jd_atom *katom)
{
	struct kbase_platform_funcs_conf *platform_funcs_p;

	platform_funcs_p = (struct kbase_platform_funcs_conf *)PLATFORM_FUNCS;
	if (platform_funcs_p && platform_funcs_p->platform_handler_atom_submit_func)
		platform_funcs_p->platform_handler_atom_submit_func(katom);
}

void kbasep_platform_event_atom_complete(struct kbase_jd_atom *katom)
{
	struct kbase_platform_funcs_conf *platform_funcs_p;

	platform_funcs_p = (struct kbase_platform_funcs_conf *)PLATFORM_FUNCS;
	if (platform_funcs_p && platform_funcs_p->platform_handler_atom_complete_func)
		platform_funcs_p->platform_handler_atom_complete_func(katom);
}
#endif
