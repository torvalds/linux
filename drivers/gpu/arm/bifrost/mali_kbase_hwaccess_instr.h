/*
 *
 * (C) COPYRIGHT 2014-2015, 2017 ARM Limited. All rights reserved.
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



/*
 * HW Access instrumentation common APIs
 */

#ifndef _KBASE_HWACCESS_INSTR_H_
#define _KBASE_HWACCESS_INSTR_H_

#include <mali_kbase_instr_defs.h>

/**
 * kbase_instr_hwcnt_enable_internal - Enable HW counters collection
 * @kbdev:	Kbase device
 * @kctx:	Kbase context
 * @enable:	HW counter setup parameters
 *
 * Context: might sleep, waiting for reset to complete
 *
 * Return: 0 on success
 */
int kbase_instr_hwcnt_enable_internal(struct kbase_device *kbdev,
				struct kbase_context *kctx,
				struct kbase_ioctl_hwcnt_enable *enable);

/**
 * kbase_instr_hwcnt_disable_internal - Disable HW counters collection
 * @kctx: Kbase context
 *
 * Context: might sleep, waiting for an ongoing dump to complete
 *
 * Return: 0 on success
 */
int kbase_instr_hwcnt_disable_internal(struct kbase_context *kctx);

/**
 * kbase_instr_hwcnt_request_dump() - Request HW counter dump from GPU
 * @kctx:	Kbase context
 *
 * Caller must either wait for kbase_instr_hwcnt_dump_complete() to return true,
 * of call kbase_instr_hwcnt_wait_for_dump().
 *
 * Return: 0 on success
 */
int kbase_instr_hwcnt_request_dump(struct kbase_context *kctx);

/**
 * kbase_instr_hwcnt_wait_for_dump() - Wait until pending HW counter dump has
 *				       completed.
 * @kctx:	Kbase context
 *
 * Context: will sleep, waiting for dump to complete
 *
 * Return: 0 on success
 */
int kbase_instr_hwcnt_wait_for_dump(struct kbase_context *kctx);

/**
 * kbase_instr_hwcnt_dump_complete - Tell whether the HW counters dump has
 *				     completed
 * @kctx:	Kbase context
 * @success:	Set to true if successful
 *
 * Context: does not sleep.
 *
 * Return: true if the dump is complete
 */
bool kbase_instr_hwcnt_dump_complete(struct kbase_context *kctx,
						bool * const success);

/**
 * kbase_instr_hwcnt_clear() - Clear HW counters
 * @kctx:	Kbase context
 *
 * Context: might sleep, waiting for reset to complete
 *
 * Return: 0 on success
 */
int kbase_instr_hwcnt_clear(struct kbase_context *kctx);

/**
 * kbase_instr_backend_init() - Initialise the instrumentation backend
 * @kbdev:	Kbase device
 *
 * This function should be called during driver initialization.
 *
 * Return: 0 on success
 */
int kbase_instr_backend_init(struct kbase_device *kbdev);

/**
 * kbase_instr_backend_init() - Terminate the instrumentation backend
 * @kbdev:	Kbase device
 *
 * This function should be called during driver termination.
 */
void kbase_instr_backend_term(struct kbase_device *kbdev);

#endif /* _KBASE_HWACCESS_INSTR_H_ */
