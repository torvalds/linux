/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2021-2022 ARM Limited. All rights reserved.
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

/*
 * Concrete implementation of kbase_hwcnt_backend_csf_if interface for CSF FW
 */

#ifndef _KBASE_HWCNT_BACKEND_CSF_IF_FW_H_
#define _KBASE_HWCNT_BACKEND_CSF_IF_FW_H_

#include "hwcnt/backend/mali_kbase_hwcnt_backend_csf_if.h"

/**
 * kbase_hwcnt_backend_csf_if_fw_create() - Create a firmware CSF interface
 *                                          of hardware counter backend.
 * @kbdev: Non-NULL pointer to Kbase device.
 * @if_fw: Non-NULL pointer to backend interface structure that is filled in on
 *         creation success.
 * Return: 0 on success, else error code.
 */
int kbase_hwcnt_backend_csf_if_fw_create(struct kbase_device *kbdev,
					 struct kbase_hwcnt_backend_csf_if *if_fw);

/**
 * kbase_hwcnt_backend_csf_if_fw_destroy() - Destroy a firmware CSF interface of
 *                                           hardware counter backend.
 * @if_fw: Pointer to a CSF interface to destroy.
 */
void kbase_hwcnt_backend_csf_if_fw_destroy(struct kbase_hwcnt_backend_csf_if *if_fw);

#endif /* _KBASE_HWCNT_BACKEND_CSF_IF_FW_H_ */
