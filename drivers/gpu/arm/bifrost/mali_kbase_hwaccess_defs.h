/*
 *
 * (C) COPYRIGHT 2014, 2016, 2018 ARM Limited. All rights reserved.
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

/**
 * struct kbase_hwaccess_data - object encapsulating the GPU backend specific
 *                              data for the HW access layer.
 *                              hwaccess_lock (a spinlock) must be held when
 *                              accessing this structure.
 * @active_kctx:     pointer to active kbase context which last submitted an
 *                   atom to GPU and while the context is active it can
 *                   submit new atoms to GPU from the irq context also, without
 *                   going through the bottom half of job completion path.
 * @backend:         GPU backend specific data for HW access layer
 */
struct kbase_hwaccess_data {
	struct kbase_context *active_kctx[BASE_JM_MAX_NR_SLOTS];

	struct kbase_backend_data backend;
};

#endif /* _KBASE_HWACCESS_DEFS_H_ */
