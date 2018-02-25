/*
 *
 * (C) COPYRIGHT 2014, 2016 ARM Limited. All rights reserved.
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


/**
 * @file mali_kbase_hwaccess_gpu_defs.h
 * HW access common definitions
 */

#ifndef _KBASE_HWACCESS_DEFS_H_
#define _KBASE_HWACCESS_DEFS_H_

#include <mali_kbase_jm_defs.h>

/* The hwaccess_lock (a spinlock) must be held when accessing this structure */
struct kbase_hwaccess_data {
	struct kbase_context *active_kctx;

	struct kbase_backend_data backend;
};

#endif /* _KBASE_HWACCESS_DEFS_H_ */
