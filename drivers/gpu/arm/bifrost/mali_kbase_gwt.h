/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2010-2017, 2020-2021 ARM Limited. All rights reserved.
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

#if !defined(_KBASE_GWT_H)
#define _KBASE_GWT_H

#include <mali_kbase.h>
#include <uapi/gpu/arm/bifrost/mali_kbase_ioctl.h>

/**
 * kbase_gpu_gwt_start - Start the GPU write tracking
 * @kctx: Pointer to kernel context
 *
 * @return 0 on success, error on failure.
 */
int kbase_gpu_gwt_start(struct kbase_context *kctx);

/**
 * kbase_gpu_gwt_stop - Stop the GPU write tracking
 * @kctx: Pointer to kernel context
 *
 * @return 0 on success, error on failure.
 */
int kbase_gpu_gwt_stop(struct kbase_context *kctx);

/**
 * kbase_gpu_gwt_dump - Pass page address of faulting addresses to user space.
 * @kctx:	Pointer to kernel context
 * @gwt_dump:	User space data to be passed.
 *
 * @return 0 on success, error on failure.
 */
int kbase_gpu_gwt_dump(struct kbase_context *kctx,
			union kbase_ioctl_cinstr_gwt_dump *gwt_dump);

#endif /* _KBASE_GWT_H */
